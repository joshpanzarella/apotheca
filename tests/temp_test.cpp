// The tests' temporary folders (tests/temp_folder.hpp): each is deleted when the program that made
// it ends, with all that is in it, and nothing else is. And no test makes one any other way: on
// 2026-11-16 the tests had left 33,000 folders (16 GB) in /tmp, which filled the disk mid-build.
//
//   temp_test SOURCE_DIR    every test (SOURCE_DIR: where tests/ is, for the check of the sources)
//
// What is deleted at the end is seen from outside: the folders are made in a child of fork(), which
// then exits, and the parent looks.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "temp_folder.hpp"

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

bool exists(const std::string& path) {
    struct stat info;
    return lstat(path.c_str(), &info) == 0;
}
bool isFolder(const std::string& path) {
    struct stat info;
    return lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}
bool write(const std::string& path, const char* text) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fputs(text, f);
    return std::fclose(f) == 0;
}
bool read(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buffer[4096];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) out.append(buffer, n);
    std::fclose(f);
    return true;
}
std::vector<std::string> namesIn(const std::string& folder) {
    std::vector<std::string> names;
    if (DIR* d = opendir(folder.c_str())) {
        while (dirent* e = readdir(d))
            if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) names.push_back(e->d_name);
        closedir(d);
    }
    return names;
}

// Runs `child` in a child of fork(), which then exits normally (its exit handlers run) with what
// `child` returns. What it writes to `say` comes back here. Returns the exit status, -1 if it did
// not exit normally.
template <typename F>
int inAChild(F child, std::string* said = nullptr) {
    std::fflush(stdout);
    std::fflush(stderr);
    int pipes[2];
    if (pipe(pipes) != 0) return -1;
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        close(pipes[0]);
        if (std::FILE* quiet = std::freopen("/dev/null", "w", stderr)) (void)quiet; // ("kept ..." and the like)
        std::string say;
        const int code = child(say);
        if (!say.empty() && ::write(pipes[1], say.data(), say.size()) < 0) std::exit(9);
        close(pipes[1]);
        std::exit(code);
    }
    close(pipes[1]);
    std::string got;
    char buffer[512];
    ssize_t n;
    while ((n = ::read(pipes[0], buffer, sizeof buffer)) > 0) got.append(buffer, static_cast<std::size_t>(n));
    close(pipes[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
    if (said) *said = got;
    return WEXITSTATUS(status);
}

// A folder made is new, empty, the owner's alone, named for the test, straight in the root.
void testAFolderIsMade() {
    const std::string a = tempFolder("selftest"), b = tempFolder("selftest");
    CHECK(a != b);
    CHECK(isFolder(a) && isFolder(b) && namesIn(a).empty());
    const std::string start = temp_folder::root() + "/apotheca-selftest-";
    CHECK(a.compare(0, start.size(), start) == 0 && a.size() == start.size() + 6);
    struct stat info;
    CHECK(lstat(a.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700);
    CHECK(temp_folder::removable(a));
}

// At the end the folder goes, with folders, files and links in it; what a link points at stays.
void testDeletedAtTheEnd() {
    const std::string outside = tempFolder("selftest-outside");
    CHECK(write(outside + "/kept.txt", "kept"));
    std::string made;
    const int code = inAChild([&](std::string& say) {
        const std::string inside = tempFolder("selftest");
        say = inside;
        bool ok = mkdir((inside + "/a").c_str(), 0755) == 0 && mkdir((inside + "/a/b").c_str(), 0755) == 0;
        ok = ok && write(inside + "/a/b/deep.txt", "deep") && write(inside + "/top.txt", "top");
        ok = ok && symlink(outside.c_str(), (inside + "/a/link").c_str()) == 0;                   // a link to a folder
        ok = ok && symlink((outside + "/kept.txt").c_str(), (inside + "/file-link").c_str()) == 0; // to a file
        ok = ok && symlink((inside + "/nowhere").c_str(), (inside + "/a/b/dangling").c_str()) == 0;
        return ok ? 0 : 3;
    }, &made);
    CHECK(code == 0);
    CHECK(!made.empty() && temp_folder::removable(made));
    CHECK(!made.empty() && !exists(made));
    // Links are deleted as links, never followed.
    std::string text;
    CHECK(isFolder(outside) && read(outside + "/kept.txt", text) && text == "kept");
}

// A folder the test deleted itself already is no harm, and the others still go.
void testGoneAlready() {
    std::string made;
    const int code = inAChild([&](std::string& say) {
        const std::string one = tempFolder("selftest"), two = tempFolder("selftest");
        say = one + "\n" + two;
        return rmdir(two.c_str()) == 0 ? 5 : 3; // a test that tidied up after itself
    }, &made);
    CHECK(code == 5);
    const std::size_t cut = made.find('\n');
    CHECK(cut != std::string::npos);
    if (cut != std::string::npos) CHECK(!exists(made.substr(0, cut)) && !exists(made.substr(cut + 1)));
}

// APOTHECA_KEEP_TEMP=1 keeps them for a look; 0 does not.
void testKeptWhenAsked() {
    for (const char* keep : {"1", "0"}) {
        std::string made;
        const int code = inAChild([&](std::string& say) {
            setenv("APOTHECA_KEEP_TEMP", keep, 1);
            say = tempFolder("selftest");
            return write(say + "/look.txt", "look") ? 0 : 3;
        }, &made);
        CHECK(code == 0 && !made.empty());
        if (keep[0] == '1') {
            CHECK(exists(made + "/look.txt"));
            CHECK(temp_folder::removeTree(made) && !exists(made));
        } else {
            CHECK(!exists(made));
        }
    }
}

// A child of fork() that exits leaves its parent's folders alone: only what a process made is its.
void testAChildLeavesItsParentsFolders() {
    const std::string mine = tempFolder("selftest");
    CHECK(write(mine + "/mine.txt", "mine"));
    CHECK(inAChild([](std::string&) { return 0; }) == 0);
    CHECK(exists(mine + "/mine.txt"));
}

// $TMPDIR is the root when it is set.
void testTmpdir() {
    const std::string root = tempFolder("selftest-root");
    std::string made;
    const int code = inAChild([&](std::string& say) {
        setenv("TMPDIR", (root + "/").c_str(), 1);
        say = tempFolder("selftest");
        return 0;
    }, &made);
    CHECK(code == 0);
    CHECK(made.compare(0, root.size() + 19, root + "/apotheca-selftest-") == 0);
    CHECK(namesIn(root).empty()); // and deleted from there at the end
}

// A folder it cannot make, or a name with a "/" in it, stops the program; nothing falls back on
// writing into the root itself.
void testNoFolderStops() {
    CHECK(inAChild([](std::string&) {
        // (a folder inside one of its own, which the pattern alone would have made)
        const std::string mine = tempFolder("selftest");
        tempFolder((mine.substr(temp_folder::root().size() + 10) + "/inner").c_str());
        return 0;
    }) == 1);
    CHECK(inAChild([](std::string&) {
        setenv("TMPDIR", "/no/such/folder/at/all", 1);
        tempFolder("selftest");
        return 0;
    }) == 1);
}

// Only a folder straight in the root whose name starts "apotheca-" may be deleted.
void testWhatMayBeDeleted() {
    using temp_folder::removable;
    CHECK(removable("/r/apotheca-x", "/r"));
    CHECK(removable("/r/apotheca-design-AbC123", "/r"));
    CHECK(!removable("/r", "/r"));
    CHECK(!removable("/r/", "/r"));
    CHECK(!removable("/r/apotheca-", "/r"));
    CHECK(!removable("/r/other-x", "/r"));
    CHECK(!removable("/r/xapotheca-x", "/r"));
    CHECK(!removable("/r/apotheca-x/inner", "/r"));
    CHECK(!removable("/r/apotheca-x/..", "/r"));
    CHECK(!removable("/r/apotheca-../..", "/r"));
    CHECK(!removable("/rr/apotheca-x", "/r"));
    CHECK(!removable("/s/apotheca-x", "/r"));
    CHECK(!removable("apotheca-x", "/r"));
    CHECK(!removable("", "/r"));
    // Refused is untouched.
    const std::string folder = tempFolder("selftest");
    CHECK(mkdir((folder + "/inner").c_str(), 0755) == 0 && write(folder + "/inner/x.txt", "x"));
    CHECK(!temp_folder::removeTree(folder + "/inner"));
    CHECK(exists(folder + "/inner/x.txt"));
}

// ---- no test makes a temporary folder any other way ---------------------------------------------

// What a test source may not say (outside temp_folder.hpp). Written in two pieces here, so that
// this file does not say them itself.
const std::vector<std::string>& banned() {
    static const std::vector<std::string> words = {
        std::string("mkd") + "temp(", std::string("mks") + "temp(", std::string("tmp") + "nam(",
        std::string("tmp") + "file(", std::string("/tmp/") + "apotheca", std::string("temp_") + "directory_path",
    };
    return words;
}

bool endsWith(const std::string& s, const char* end) {
    const std::size_t n = std::strlen(end);
    return s.size() >= n && s.compare(s.size() - n, n, end) == 0;
}

// The fault in one source file, or "" if there is none.
std::string fault(const std::string& name, const std::string& text) {
    if (name == "temp_folder.hpp") return "";
    if (endsWith(name, ".sh")) {
        // A shell test may make its folder with mktemp, if it deletes it on the way out.
        if (text.find(std::string("mk") + "temp") != std::string::npos &&
            (text.find("trap") == std::string::npos || text.find("rm -rf") == std::string::npos))
            return "makes a folder and has no trap that deletes it";
        return "";
    }
    for (const std::string& word : banned())
        if (text.find(word) != std::string::npos) return "says " + word + " (use tempFolder, tests/temp_folder.hpp)";
    return "";
}

void testTheCheckCatches() {
    // Each planted fault is caught, and the right way is not.
    CHECK(!fault("x_test.cpp", std::string("    char p[] = \"x-XXXXXX\";\n    ") + "mkd" + "temp(p);\n").empty());
    CHECK(!fault("x_test.cpp", std::string("    const char* got = mkd") + "temp(pattern);\n").empty());
    CHECK(!fault("x.hpp", std::string("    int fd = mks") + "temp(name);\n").empty());
    CHECK(!fault("x_test.cpp", std::string("    const std::string folder = \"/tmp/") + "apotheca-fixed\";\n").empty());
    CHECK(!fault("x_test.cpp", std::string("    std::FILE* f = tmp") + "file();\n").empty());
    CHECK(!fault("x_test.cpp", std::string("    char* n = tmp") + "nam(nullptr);\n").empty());
    CHECK(!fault("x.inc", std::string("auto t = std::filesystem::temp_") + "directory_path();\n").empty());
    CHECK(!fault("x_test.sh", std::string("root=$(mk") + "temp -d)\n").empty());
    CHECK(!fault("x_test.sh", std::string("root=$(mk") + "temp -d)\ntrap 'echo bye' EXIT\n").empty());
    CHECK(fault("x_test.sh", std::string("root=$(mk") + "temp -d)\ntrap 'rm -rf \"$root\"' EXIT\n").empty());
    CHECK(fault("x_test.cpp", "    const std::string folder = tempFolder(\"x\");\n").empty());
    CHECK(fault("x_test.cpp", "    CHECK(!safe(\"/tmp/c.wav\"));\n").empty()); // a path that is no folder made
    CHECK(fault("temp_folder.hpp", std::string("mkd") + "temp(buffer.data())").empty());
}

void testNoTestMakesItsOwn(const std::string& sourceDir) {
    const std::string folder = sourceDir + "/tests";
    const std::vector<std::string> names = namesIn(folder);
    int read_ = 0, shells = 0;
    for (const std::string& name : names) {
        if (!(endsWith(name, ".cpp") || endsWith(name, ".hpp") || endsWith(name, ".h") || endsWith(name, ".inc") || endsWith(name, ".sh"))) continue;
        std::string text;
        if (!read(folder + "/" + name, text)) {
            std::printf("FAIL could not read tests/%s\n", name.c_str());
            ++failures;
            continue;
        }
        ++read_;
        if (endsWith(name, ".sh")) ++shells;
        const std::string why = fault(name, text);
        if (!why.empty()) {
            std::printf("FAIL tests/%s %s\n", name.c_str(), why.c_str());
            ++failures;
        }
    }
    CHECK(read_ > 40);  // (the folder is right: it holds the tests)
    CHECK(shells >= 2); // (launcher_test.sh and words_test.sh are read too)
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: temp_test SOURCE_DIR\n");
        return 2;
    }
    testAFolderIsMade();
    testDeletedAtTheEnd();
    testGoneAlready();
    testKeptWhenAsked();
    testAChildLeavesItsParentsFolders();
    testTmpdir();
    testNoFolderStops();
    testWhatMayBeDeleted();
    testTheCheckCatches();
    testNoTestMakesItsOwn(argv[1]);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("temp tests passed\n");
    return 0;
}
