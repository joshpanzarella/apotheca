// A test's folders of its own, and every one of them taken away when the test program ends.
//
//   const std::string folder = tempFolder("design");   // $TMPDIR (or /tmp) + "/apotheca-design-XXXXXX"
//
// Each folder is made with mkdtemp and remembered. When the program ends normally (main returns,
// or exit() is called) every folder it made is deleted with all that is in it. Nothing is deleted
// before that, so a test may open a folder again with a second Session, as the handheld would.
//
// The tests made their folders by hand and never took them away: on 2026-11-16 33,000 of them
// (16 GB) filled the disk in the middle of a build. tests/temp_test.cpp holds this file to its
// rules, and fails a test source that makes a temporary folder any other way.
//
// - Set APOTHECA_KEEP_TEMP=1 to keep the folders for a look (each is named on stderr at the end).
// - A program that crashes (SIGABRT, SIGSEGV) or is killed keeps its folders: nothing is deleted
//   from a signal handler, on purpose. Deleting a tree is no work for a program in an unknown state.
// - Only what this process made is deleted (a child of fork() that exits leaves its parent's), only
//   a folder straight in the temporary root whose name starts "apotheca-", and links are deleted as
//   links, never followed (nftw with FTW_PHYS): picker_test and transfer_test plant links to the
//   folder above.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <ftw.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace temp_folder {

// Where the folders go: $TMPDIR if it is set, else /tmp.
inline std::string root() {
    const char* given = std::getenv("TMPDIR");
    std::string r = given && *given ? given : "/tmp";
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    return r;
}

// Is this a folder this file may delete: straight in the root, its name "apotheca-" and more,
// nothing that climbs out?
inline bool removable(const std::string& path, const std::string& under = root()) {
    const std::string start = under + "/apotheca-";
    if (path.size() <= start.size() || path.compare(0, start.size(), start) != 0) return false;
    const std::string name = path.substr(under.size() + 1);
    return name.find('/') == std::string::npos && name != "." && name != "..";
}

inline int removeOne(const char* path, const struct stat*, int, struct FTW*) {
    std::remove(path); // a folder (its insides are gone first: FTW_DEPTH), a file, or a link itself
    return 0;          // on to the rest whatever happened
}

// Deletes the folder and all in it; false (and nothing touched) for one it may not delete.
inline bool removeTree(const std::string& path) {
    if (!removable(path)) return false;
    nftw(path.c_str(), removeOne, 16, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
    struct stat info;
    return lstat(path.c_str(), &info) != 0;
}

inline bool keeping() {
    const char* keep = std::getenv("APOTHECA_KEEP_TEMP");
    return keep && *keep && std::string(keep) != "0";
}

// Every folder made, with the process that made it. Never destroyed: it is read at exit.
inline std::vector<std::pair<std::string, pid_t>>& made() {
    static auto* folders = new std::vector<std::pair<std::string, pid_t>>();
    return *folders;
}

inline void removeAll() {
    for (const auto& folder : made()) {
        if (folder.second != getpid()) continue; // a parent's, copied into a child of fork()
        if (keeping()) std::fprintf(stderr, "kept %s (APOTHECA_KEEP_TEMP)\n", folder.first.c_str());
        else if (!removeTree(folder.first)) std::fprintf(stderr, "could not delete %s\n", folder.first.c_str());
    }
}

// A new empty folder, "apotheca-NAME-" and six letters, deleted when the program ends.
// NAME is a plain word (no "/"). If no folder can be made the program stops: a test must never
// fall back on writing into the temporary root itself.
inline std::string tempFolder(const char* name) {
    std::string pattern = root() + "/apotheca-" + name + "-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back(0);
    if (std::string(name).find('/') != std::string::npos || !mkdtemp(buffer.data())) {
        std::fprintf(stderr, "could not make a temporary folder %s\n", pattern.c_str());
        std::exit(1);
    }
    static bool registered = false;
    if (!registered) {
        made(); // (made before the handler is registered, so it outlives it either way)
        std::atexit(removeAll);
        registered = true;
    }
    made().emplace_back(buffer.data(), getpid());
    return buffer.data();
}

} // namespace temp_folder

using temp_folder::tempFolder;
