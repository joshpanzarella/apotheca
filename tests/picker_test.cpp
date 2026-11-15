// The list of recordings changing the card itself (picker plan, batch 2): + NEW FOLDER, carrying a
// recording to another folder with hold R1 (the open song, the saved songs and the revs follow),
// and hold B on an empty folder. With real files, in a folder of its own under /tmp.
// (The columns, the pace and hold B on a recording: sample_test.)

#include <SDL.h>

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app.hpp"
#include "canvas.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "transfer.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, START = SDLK_RETURN, R1 = SDLK_w, L1 = SDLK_q;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;
const SDL_Keycode STICK_DOWN = SDLK_k, STICK_RIGHT = SDLK_l;
typedef SamplePicker::Kind Kind;
typedef Session::FileJob Job;

std::string tempFolder() {
    char pattern[] = "/tmp/apotheca-picker-XXXXXX";
    const char* made = mkdtemp(pattern);
    return made ? made : "/tmp";
}

bool exists(const std::string& path) {
    struct stat info;
    return lstat(path.c_str(), &info) == 0;
}
std::string contents(const std::string& path) {
    std::string text;
    readTextFile(path, text);
    return text;
}

// A card: `root`/samples and `root`/songs.
struct Card {
    std::string root = tempFolder(), samples = root + "/samples", songs = root + "/songs";
    Card() {
        mkdir(samples.c_str(), 0755);
        mkdir(songs.c_str(), 0755);
    }
    void folder(const std::string& name) const { mkdir((samples + "/" + name).c_str(), 0755); }
    // A recording `frames` long: they are told apart by how long they sound.
    void wav(const std::string& name, int frames, const char* ending = ".wav") const {
        std::vector<float> made(static_cast<std::size_t>(frames));
        for (std::size_t n = 0; n < made.size(); ++n) made[n] = 0.5f * std::sin(0.05f * static_cast<float>(n));
        CHECK(writeWav(samples + "/" + name + ending, made, 48000));
    }
    bool has(const std::string& file) const { return exists(samples + "/" + file); }
};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    explicit Rig(const Card& card) {
        app.session().useSampleFolder(card.samples);
        app.session().useSongFolder(card.songs, 3);
        app.session().view = View::Device;
        app.kit().goTo(5, KitScreen::kSound);
        frames(1);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) { for (int i = 0; i < times; ++i) { key(code, true); key(code, false); } }
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    // How many frames of sound come out of the next `n`.
    int sounding(int n) {
        std::vector<float> out(2 * static_cast<std::size_t>(n));
        engine->render(out.data(), n);
        int first = -1, last = -1;
        for (std::size_t i = 0; i < out.size(); i += 2) {
            if (out[i] == 0.f) continue;
            if (first < 0) first = static_cast<int>(i / 2);
            last = static_cast<int>(i / 2);
        }
        return first < 0 ? 0 : last - first + 1;
    }
    SamplePicker& list() { return app.kit().picker(); }
    Session& session() { return app.session(); }
    std::string lane(int n = 5) { return app.project().tracks[0].laneSample[n]; }
    bool on(Kind kind, const char* name, int count = 0) {
        const SamplePicker::Row* row = list().under();
        return row && row->kind == kind && row->name == name && row->count == count;
    }
    // Type on the keys: from the first key (where they open), by the D-pad, A on each.
    void type(const char* word, int* at) {
        for (const char* c = word; *c; ++c) {
            const int to = static_cast<int>(std::strchr(SamplePicker::kKeys, *c) - SamplePicker::kKeys);
            for (; *at / 10 != to / 10; *at = (*at + 10) % 40) tap(DOWN);
            for (; *at % 10 != to % 10; *at = *at / 10 * 10 + (*at % 10 + 1) % 10) tap(RIGHT);
            tap(A);
        }
    }
    void type(const char* word) {
        int at = 0;
        type(word, &at);
    }
    // The cursor onto the row of this kind and name, in the column showing.
    bool goTo(Kind kind, const std::string& name) {
        tap(L1, 20);
        for (int i = 0; i < list().count(); ++i, tap(DOWN))
            if (list().under() && list().under()->kind == kind && list().under()->name == name) return true;
        return false;
    }
    // Drawn, the hints bar holds every hint.
    bool hintsFit(Canvas& canvas) {
        app.draw(canvas.r, *canvas.fonts);
        if (ui::gHintsOver != 0.f) std::printf("    the hints are %.0f pixels over\n", static_cast<double>(ui::gHintsOver));
        return ui::gHintsOver == 0.f;
    }
};

// + NEW FOLDER: the last row of SAMPLES and of a first-level folder. A: the keys; Y or START keeps.
void testMakingAFolder() {
    Card card;
    card.wav("one", 2000);
    card.wav("two", 3000);
    Rig rig(card);
    SamplePicker& list = rig.list();
    rig.tap(A);
    // No folders yet: the recordings themselves, and + NEW FOLDER after them.
    CHECK(list.isOpen() && list.flat() && list.count() == 4 && list.rows()[3].kind == Kind::NewFolder);
    rig.tap(DOWN);
    CHECK(rig.lane() == "one");
    rig.sounding(96000);
    rig.tap(DOWN, 2);
    // On it nothing goes on the lane and nothing is heard, X or no X.
    rig.tap(X);
    CHECK(rig.on(Kind::NewFolder, "") && rig.lane() == "two");
    rig.tap(UP);
    rig.sounding(96000);
    rig.tap(DOWN);
    CHECK(rig.on(Kind::NewFolder, "") && rig.sounding(20000) == 0 && !list.typing());
    // A: the keys. Every button is theirs: nothing typed, B leaves, and the list is still open (its let-go is no tap).
    rig.tap(A);
    CHECK(list.typing() && list.typed().empty() && list.isOpen());
    rig.tap(B);
    CHECK(!list.typing() && list.isOpen() && rig.on(Kind::NewFolder, ""));
    // Y with nothing typed makes nothing.
    rig.tap(A);
    rig.tap(Y);
    CHECK(!list.typing() && list.isOpen() && rig.session().sampleFolders().empty());
    // Typed: no space first, no two together, B deletes the last, twelve at most.
    rig.tap(A);
    {
        int at = 0;
        rig.type(" K  ", &at);
        CHECK(list.typed() == "K ");
        rig.tap(B, 2);
        CHECK(list.typed().empty() && list.typing());
        rig.type("ABCDEFGHIJKLMNOP", &at);
        CHECK(list.typed() == "ABCDEFGHIJKL" && SamplePicker::kFolderNameLength == 12);
        rig.tap(B, 12);
        // The keys go round at their edges; the stick moves over them, and a held direction runs.
        rig.tap(B); // (leave, and come back: the first key again)
        rig.tap(A);
        rig.tap(LEFT);
        rig.tap(UP);
        rig.tap(A);
        CHECK(list.typed().empty()); // the last key is a space: not first
        rig.tap(LEFT);
        rig.tap(A);
        CHECK(list.typed() == "_");
        rig.tap(B);
        rig.tap(DOWN);
        rig.tap(RIGHT, 2);
        rig.key(STICK_RIGHT, true);
        rig.frames(1);
        rig.key(STICK_RIGHT, false);
        rig.frames(1);
        rig.tap(A);
        CHECK(list.typed() == "B");
        rig.key(DOWN, true);
        rig.frames(34); // 544 ms: one at once, then three more at the list's pace: round the four rows
        rig.key(DOWN, false);
        rig.tap(A);
        CHECK(list.typed() == "BB");
        rig.tap(B, 3);
        CHECK(!list.typing());
    }
    // KICKS, and a space after it that is not kept. START keeps it too, and does not start the song.
    rig.tap(A);
    rig.type("KICKS ");
    CHECK(list.typed() == "KICKS ");
    rig.tap(START);
    CHECK(!list.typing() && list.isOpen() && !rig.app.project().playing);
    // Made, in small letters; the list has folders now (the loose ones are UNSORTED), and the cursor is on the new one.
    struct stat info;
    CHECK(lstat((card.samples + "/kicks").c_str(), &info) == 0 && S_ISDIR(info.st_mode) && !card.has("KICKS "));
    CHECK(rig.session().sampleFolders() == std::vector<std::string>({"kicks"}));
    CHECK(!list.flat() && list.depth() == 0 && list.count() == 4 && list.cursor() == 2 && rig.on(Kind::Folder, "kicks") && list.said() == "MADE KICKS");
    CHECK(list.rows()[1].kind == Kind::Folder && list.rows()[1].name.empty() && list.rows()[1].count == 2 && list.rows()[3].kind == Kind::NewFolder);
    CHECK(rig.lane() == "two"); // the lane keeps what it had
    rig.frames(SamplePicker::kSaidMs / 16 + 2);
    CHECK(list.said().empty());
    // The same name again, capitals or not: refused, said, and the keys stay for another.
    rig.tap(DOWN);
    rig.tap(A);
    rig.type("KICKS");
    rig.tap(Y);
    CHECK(list.typing() && list.said() == "THERE IS ONE OF THAT NAME" && rig.session().sampleFolders().size() == 1);
    rig.tap(B, 6);
    CHECK(!list.typing());
    // UNSORTED holds no folders: no + NEW FOLDER in it.
    CHECK(rig.goTo(Kind::Folder, ""));
    rig.tap(RIGHT);
    CHECK(list.depth() == 1 && list.count() == 2 && list.rows()[1].kind == Kind::Recording);
    rig.tap(LEFT);
    // In KICKS: it is empty but for + NEW FOLDER. A folder made there is in it.
    CHECK(rig.goTo(Kind::Folder, "kicks"));
    rig.tap(RIGHT);
    CHECK(list.depth() == 1 && list.count() == 1 && rig.on(Kind::NewFolder, ""));
    rig.tap(A);
    rig.type("LONG 1");
    rig.tap(Y);
    CHECK(!list.typing() && lstat((card.samples + "/kicks/long 1").c_str(), &info) == 0 && S_ISDIR(info.st_mode));
    CHECK(list.depth() == 1 && list.count() == 2 && list.cursor() == 0 && rig.on(Kind::Folder, "kicks/long 1") && list.said() == "MADE LONG 1");
    // Two levels and no further: none is offered in a folder in a folder.
    rig.tap(RIGHT);
    CHECK(list.depth() == 2 && list.count() == 0);
    rig.tap(A); // (nothing under the cursor: nothing happens)
    CHECK(list.isOpen() && !list.typing());
    rig.tap(LEFT);
    rig.tap(LEFT);
    CHECK(list.depth() == 0 && rig.on(Kind::Folder, "kicks"));
    rig.tap(B);
    CHECK(!list.isOpen() && rig.lane().empty()); // B's tap: as it was when the list opened

    // A folder whose name leaves no room for a recording's is not made, and it says so.
    const std::string wide = "a folder with a name that is fifty letters long ab";
    CHECK(wide.size() == 50);
    card.folder(wide);
    Rig other(card);
    other.tap(A);
    other.tap(LEFT); // (the song the first left behind may have a loose recording on: out of UNSORTED)
    CHECK(other.goTo(Kind::Folder, wide));
    other.tap(RIGHT);
    other.tap(A);
    other.type("ABCDEFGHIJKL");
    other.tap(Y);
    CHECK(other.list().typing() && other.list().said() == "THAT NAME IS TOO LONG HERE" && other.session().sampleFolders().size() == 3);
    other.tap(B, 11);
    other.tap(Y); // one letter fits
    CHECK(!other.list().typing() && other.session().sampleFolders().size() == 4 && exists(card.samples + "/" + wide + "/a"));

    // Asked directly: only a plain new name, in a real folder of the first level.
    Session& session = rig.session();
    CHECK(session.makeSampleFolder("") == Job::Failed && session.makeSampleFolder("../out") == Job::Failed && session.makeSampleFolder(".hidden") == Job::Failed);
    CHECK(session.makeSampleFolder("kicks/long 1/deeper") == Job::Failed && session.makeSampleFolder("nowhere/in") == Job::Failed);
    CHECK(session.makeSampleFolder("KICKS") == Job::Taken && session.makeSampleFolder("kicks/Long 1") == Job::Taken);
    CHECK(writeTextFile(card.samples + "/a file", "x") && session.makeSampleFolder("a file") == Job::Taken);
    CHECK(session.makeSampleFolder(std::string(62, 'x')) == Job::TooLong && session.makeSampleFolder(std::string(61, 'x')) == Job::Done);
    CHECK(symlink(card.root.c_str(), (card.samples + "/link").c_str()) == 0 && session.makeSampleFolder("link/in") == Job::Failed && !exists(card.root + "/in"));
    CHECK(session.makeSampleFolder("kicks/short") == Job::Done && exists(card.samples + "/kicks/short"));
    CHECK(!exists(card.root + "/out") && !exists(card.samples + "/.hidden"));
}

// Hold R1 on a recording carries it: steer, let go, and it is in that folder. The open song, the
// saved songs and the revs follow.
void testCarrying() {
    Card card;
    for (const char* dir : {"kicks", "drums", "drums/snares", "void"}) card.folder(dir);
    card.wav("tom", 2000);
    card.wav("kicks/808", 3000);
    card.wav("kicks/909", 4000);
    card.wav("drums/snares/rim", 5000);
    card.wav("drums/Clap", 6000, ".WAV");
    // A saved song and a rev that name them.
    const std::string before = "apotheca-song 1\ntempo 97\ntrack drums\nsample 1 0 kicks/808\nsample 2 0 kicks/909\ntrack sampler\nsound kicks/808\n";
    const std::string saved = card.songs + "/saved.song", rev = card.songs + "/saved.2.rev";
    CHECK(writeTextFile(saved, before) && writeTextFile(rev, before));
    Rig rig(card);
    Session& session = rig.session();
    SamplePicker& list = rig.list();
    session.setLaneSample(0, 5, "kicks/808");
    session.setLaneSample(0, 6, "kicks/808");
    session.saveNow();
    const std::string openFile = card.songs + "/" + session.songName() + ".song";
    CHECK(contents(openFile).find("kicks/808") != std::string::npos);
    rig.frames(1);
    rig.tap(A);
    CHECK(list.isOpen() && list.depth() == 1 && list.folderName() == "kicks" && list.count() == 3 && rig.on(Kind::Recording, "kicks/808"));

    // A tap of R1 is still a page (when it is let go): to the end, and nothing is carried.
    rig.key(R1, true);
    CHECK(list.cursor() == 0);
    rig.frames(5);
    CHECK(list.carried().empty());
    rig.key(R1, false);
    CHECK(list.cursor() == 2 && rig.on(Kind::NewFolder, "") && list.carried().empty());
    // On a row that is no recording R1 pages at once, and held there carries nothing.
    rig.tap(L1);
    rig.tap(LEFT);
    CHECK(list.depth() == 0 && list.cursor() == 3);
    rig.key(R1, true);
    CHECK(list.cursor() == list.count() - 1);
    rig.frames(40);
    CHECK(list.carried().empty());
    rig.key(R1, false);
    CHECK(list.cursor() == list.count() - 1);
    CHECK(rig.goTo(Kind::Folder, "kicks"));
    rig.tap(RIGHT);
    CHECK(rig.on(Kind::Recording, "kicks/808") && rig.lane() == "kicks/808");

    // Held: after kCarryHoldMs it is carried. The top line says so, and where it would go.
    rig.sounding(96000);
    rig.key(R1, true);
    rig.frames(static_cast<int>(SamplePicker::kCarryHoldMs / 16)); // (the first frame starts the count: 16 ms short)
    CHECK(list.carried().empty());
    rig.frames(2);
    CHECK(list.carried() == "kicks/808" && list.carryTo() == "kicks" && SamplePicker::kCarryHoldMs == 300);
    // Steering: the rows go by and nothing goes on the lane, nothing is heard; A and X do nothing.
    rig.tap(DOWN);
    rig.frames(10);
    CHECK(list.cursor() == 1 && rig.lane() == "kicks/808" && rig.sounding(20000) == 0);
    rig.tap(A);
    rig.tap(X);
    rig.tap(Y);
    CHECK(list.isOpen() && list.carried() == "kicks/808" && rig.lane() == "kicks/808" && rig.sounding(20000) == 0);
    // Out, onto the folder it is in; down to another: that is where it would go.
    rig.tap(LEFT);
    CHECK(list.depth() == 0 && list.carryTo() == "kicks");
    rig.tap(DOWN);
    CHECK(rig.on(Kind::Folder, "void") && list.carryTo() == "void" && card.has("kicks/808.wav"));
    // Let go: it is there. The lanes that had it have it by its new name, and play on; the list stays.
    rig.key(R1, false);
    CHECK(list.carried().empty() && list.isOpen() && card.has("void/808.wav") && !card.has("kicks/808.wav"));
    CHECK(rig.lane(5) == "void/808" && rig.lane(6) == "void/808" && !session.laneSound(0, 5).missing && !session.laneSound(0, 6).missing);
    CHECK(list.said() == "MOVED TO VOID" && rig.on(Kind::Folder, "void", 1) && list.depth() == 0);
    CHECK(session.sampleNames() == std::vector<std::string>({"tom", "drums/Clap", "drums/snares/rim", "kicks/909", "void/808"}));
    // The saved song and its rev name it where it is now, the rest of them as it was; the open song is written at once.
    std::string after = before;
    after.replace(after.find("sample 1 0 kicks/808"), 20, "sample 1 0 void/808");
    after.replace(after.find("sound kicks/808"), 15, "sound void/808");
    CHECK(contents(saved) == after && contents(rev) == after);
    CHECK(contents(openFile).find("kicks/808") == std::string::npos && contents(openFile).find("sample 6 0 void/808") != std::string::npos);
    // B's tap puts back what was on when the list opened: the same recording, where it is now.
    rig.tap(RIGHT);
    CHECK(list.depth() == 1 && rig.on(Kind::Recording, "void/808") && rig.lane() == "void/808");
    rig.tap(LEFT);
    CHECK(rig.goTo(Kind::Folder, "kicks"));
    rig.tap(RIGHT);
    CHECK(rig.lane() == "kicks/909" && list.count() == 2);
    rig.tap(B);
    CHECK(!list.isOpen() && rig.lane() == "void/808");
    rig.frames(2);
    rig.tap(A);
    CHECK(list.isOpen() && list.folderName() == "void" && rig.on(Kind::Recording, "void/808"));

    // A direction with R1 carries at once, with no wait. Out of every folder: let go on SAMPLES
    // on a row that is no folder (or on UNSORTED).
    rig.key(R1, true);
    rig.tap(LEFT);
    CHECK(list.carried() == "void/808" && list.depth() == 0 && list.carryTo() == "void");
    rig.tap(L1);
    CHECK(list.cursor() == 0 && rig.on(Kind::First, "") && list.carryTo().empty());
    rig.tap(DOWN);
    CHECK(rig.on(Kind::Folder, "", 1) && list.carryTo().empty());
    rig.key(R1, false);
    CHECK(card.has("808.wav") && !card.has("void/808.wav") && rig.lane() == "808" && list.said() == "MOVED TO UNSORTED" && rig.on(Kind::Folder, "", 2));
    CHECK(exists(card.samples + "/void")); // the folder it left stays, empty
    after.replace(after.find("sample 1 0 void/808"), 19, "sample 1 0 808");
    after.replace(after.find("sound void/808"), 14, "sound 808");
    CHECK(contents(saved) == after && contents(rev) == after);

    // Into a folder in a folder, steering INTO it: let go inside, and the cursor is on it there, and it is on the lane.
    rig.tap(RIGHT);
    CHECK(list.depth() == 1 && list.folderName().empty() && rig.on(Kind::Recording, "808"));
    // (The stick with R1 held carries at once too, before the hold is up. Called off: what the cursor came to is on.)
    rig.key(R1, true);
    rig.key(STICK_DOWN, true);
    rig.frames(2);
    rig.key(STICK_DOWN, false);
    CHECK(list.carried() == "808" && list.cursor() == 1 && rig.lane() == "808");
    rig.tap(B);
    rig.key(R1, false);
    CHECK(list.carried().empty() && list.cursor() == 1 && rig.lane() == "tom" && card.has("808.wav"));
    rig.key(R1, true);
    rig.tap(UP);
    CHECK(list.carried() == "tom" && list.cursor() == 0 && rig.lane() == "tom");
    rig.tap(LEFT);
    CHECK(rig.goTo(Kind::Folder, "drums"));
    rig.tap(RIGHT);
    CHECK(list.depth() == 1 && rig.on(Kind::Folder, "drums/snares", 1) && list.carryTo() == "drums/snares");
    rig.tap(RIGHT);
    CHECK(list.depth() == 2 && rig.on(Kind::Recording, "drums/snares/rim") && list.carryTo() == "drums/snares" && rig.lane() == "tom");
    rig.sounding(96000);
    rig.key(R1, false);
    CHECK(card.has("drums/snares/tom.wav") && !card.has("tom.wav") && list.said() == "MOVED TO SNARES");
    CHECK(list.depth() == 2 && list.count() == 2 && rig.on(Kind::Recording, "drums/snares/tom") && rig.lane() == "drums/snares/tom" && rig.sounding(20000) > 1500);

    // B with R1 held calls it off: nothing moves, neither let-go does anything, and what the cursor is on is on.
    rig.key(R1, true);
    rig.frames(25);
    CHECK(list.carried() == "drums/snares/tom");
    rig.tap(UP);
    rig.tap(LEFT);
    rig.tap(DOWN);
    CHECK(rig.on(Kind::Recording, "drums/Clap") && list.carryTo() == "drums" && rig.lane() == "drums/snares/tom");
    rig.key(B, true);
    CHECK(list.carried().empty() && list.isOpen() && rig.lane() == "drums/Clap");
    rig.key(B, false);
    rig.key(R1, false);
    rig.frames(2);
    CHECK(list.isOpen() && card.has("drums/snares/tom.wav") && !card.has("drums/tom.wav") && rig.on(Kind::Recording, "drums/Clap") && list.said() != "MOVED TO DRUMS");

    // Let go where it is already: nothing happens, nothing is said. Its file ends .WAV and keeps that.
    rig.frames(SamplePicker::kSaidMs / 16 + 2);
    rig.key(R1, true);
    rig.frames(25);
    rig.key(R1, false);
    CHECK(list.said().empty() && card.has("drums/Clap.WAV") && rig.on(Kind::Recording, "drums/Clap"));
    rig.key(R1, true);
    rig.tap(LEFT);
    CHECK(rig.goTo(Kind::Folder, "void"));
    rig.key(R1, false);
    CHECK(card.has("void/Clap.WAV") && !card.has("void/Clap.wav") && !card.has("drums/Clap.WAV") && rig.lane() == "void/Clap" && !session.laneSound(0, 5).missing);

    // One of that name is there already (capitals not counted): refused, and it says so. Nothing is written over.
    card.wav("void/RIM", 7000);
    CHECK(rig.goTo(Kind::Folder, "drums"));
    rig.tap(RIGHT);
    rig.tap(RIGHT);
    CHECK(rig.on(Kind::Recording, "drums/snares/rim"));
    rig.key(R1, true);
    rig.tap(LEFT);
    rig.tap(LEFT);
    CHECK(rig.goTo(Kind::Folder, "void"));
    rig.key(R1, false);
    CHECK(list.said() == "NOT MOVED: ONE OF THAT NAME IS THERE" && card.has("drums/snares/rim.wav") && card.has("void/RIM.wav") && !card.has("void/rim.wav"));
    CHECK(session.sampleNames().size() == 6);

    // A name that would not fit where it is going (kSampleNameRoom, folders and all): refused, said.
    const std::string wide = "a folder with a name that is fifty letters long ab", longName = "a recording of twenty";
    card.folder(wide);
    card.wav(longName, 2000);
    rig.tap(B);
    rig.tap(A);
    rig.tap(LEFT);
    CHECK(rig.goTo(Kind::Folder, ""));
    rig.tap(RIGHT);
    CHECK(rig.goTo(Kind::Recording, longName));
    rig.key(R1, true);
    rig.tap(LEFT);
    CHECK(rig.goTo(Kind::Folder, wide));
    rig.key(R1, false);
    CHECK(list.said() == "NOT MOVED: ITS NAME WOULD BE TOO LONG THERE" && card.has(longName + ".wav") && !card.has(wide + "/" + longName + ".wav"));
    rig.tap(B);

    // A card that will not rename from one folder to another: moved the long way, the same.
    Transfer::renameBroken = true;
    CHECK(session.moveSample("808", "kicks/808") == Job::Done && card.has("kicks/808.wav") && !card.has("808.wav") && !card.has("kicks/808.wav.part"));
    Transfer::renameBroken = false;
    std::vector<float> read;
    CHECK(readWav(card.samples + "/kicks/808.wav", read) && read.size() == 3000);
    after.replace(after.find("sample 1 0 808"), 14, "sample 1 0 kicks/808");
    after.replace(after.find("sound 808"), 9, "sound kicks/808");
    CHECK(contents(saved) == after && contents(rev) == after);
    // Asked directly: only a recording that is there, to a real folder that is there, under a name a recording may have.
    CHECK(session.moveSample("kicks/808", "kicks/808") == Job::Same);
    CHECK(session.moveSample("no such", "kicks/no such") == Job::Failed && session.moveSample("../x", "kicks/x") == Job::Failed);
    CHECK(session.moveSample("kicks/808", "../808") == Job::Failed && session.moveSample("kicks/808", "a/b/c/808") == Job::Failed && session.moveSample("kicks/808", "") == Job::Failed);
    CHECK(session.moveSample("kicks/808", "nowhere/808") == Job::Failed && !exists(card.samples + "/nowhere"));
    CHECK(symlink(card.root.c_str(), (card.samples + "/link").c_str()) == 0 && session.moveSample("kicks/808", "link/808") == Job::Failed && !exists(card.root + "/808.wav"));
    CHECK(session.moveSample("kicks/808", "kicks/909") == Job::Taken && session.moveSample("kicks/808", "void/rim") == Job::Taken);
    CHECK(writeTextFile(card.samples + "/void/808.WAV", "x") && session.moveSample("kicks/808", "void/808") == Job::Taken && contents(card.samples + "/void/808.WAV") == "x");
    CHECK(session.moveSample("kicks/808", std::string(64, 'x')) == Job::TooLong);
    card.folder("void/909.wav"); // (a folder, of all things, where its file would go: not listed as a recording, and in the way)
    CHECK(session.moveSample("kicks/909", "void/909") == Job::Taken && card.has("kicks/909.wav"));
    CHECK(card.has("kicks/808.wav") && contents(saved) == after);
}

// The list by itself, with jobs that only count: what it asks for, and what it does not.
void testTheListByItself() {
    SamplePicker list;
    Input input;
    int used = 0;
    std::vector<std::pair<std::string, std::string>> moves;
    SamplePicker::Jobs jobs;
    jobs.use = [&](const std::string&) { ++used; };
    jobs.move = [&](const std::string& from, const std::string& to) {
        moves.push_back({from, to});
        return Job::Done;
    };
    Uint32 clock = 1000;
    const auto key = [&](SDL_Keycode code, bool down, bool seen = true) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press) && seen) list.onButton(press, true);
    };
    const auto frames = [&](int count) { for (int i = 0; i < count; ++i) list.update(input, clock += 16, true); };
    list.open({"kicks/808", "kicks/909"}, {"kicks", "void"}, "kicks/808", "TITLE", "FIRST", jobs);
    frames(1);
    // With no way to make a folder there is no + NEW FOLDER.
    CHECK(list.depth() == 1 && list.count() == 2 && list.cursor() == 0);
    // Carried onto another folder: asked for once, by its whole names. Nothing is put on the lane for it,
    // and B's tap after it puts nothing on either: what was on is what is on, under its new name.
    key(R1, true);
    key(LEFT, true);
    key(LEFT, false);
    key(DOWN, true);
    key(DOWN, false);
    CHECK(list.carryTo() == "void");
    key(R1, false);
    CHECK(moves.size() == 1 && moves[0].first == "kicks/808" && moves[0].second == "void/808" && used == 0);
    key(B, true);
    key(B, false);
    CHECK(!list.isOpen() && used == 0);
    // R1's let-go never seen (another screen took it): called off. Nothing is moved, and no page is turned later.
    list.open({"kicks/808", "kicks/909"}, {"kicks", "void"}, "kicks/808", "TITLE", "FIRST", jobs);
    key(R1, true);
    frames(25);
    CHECK(list.carried() == "kicks/808");
    key(R1, false, false);
    frames(1);
    CHECK(list.carried().empty() && moves.size() == 1);
    key(R1, true, false);
    key(R1, false);
    CHECK(list.cursor() == 0 && moves.size() == 1);
    // With no way to move, R1 on a recording is a page at once, and nothing is ever carried.
    jobs.move = nullptr;
    list.open({"kicks/808", "kicks/909"}, {"kicks", "void"}, "kicks/808", "TITLE", "FIRST", jobs);
    key(R1, true);
    CHECK(list.cursor() == 1);
    frames(25);
    CHECK(list.carried().empty());
    key(R1, false);
}

// Hold B on an EMPTY folder deletes it. Nothing else of a folder is ever deleted.
void testDeletingAnEmptyFolder() {
    Card card;
    for (const char* dir : {"drums", "drums/snares", "junk", "kicks", "void"}) card.folder(dir);
    card.wav("kicks/808", 3000);
    card.wav("tom", 2000);
    CHECK(writeTextFile(card.samples + "/junk/notes.txt", "not a recording"));
    Rig rig(card);
    Session& session = rig.session();
    SamplePicker& list = rig.list();
    rig.tap(A);
    CHECK(list.depth() == 0 && list.count() == 7);
    const auto hold = [&](int frames) {
        rig.key(B, true);
        rig.frames(frames);
    };
    // A folder with a recording in it, one with a folder in it, and UNSORTED: nothing fills; let go, it is B's tap.
    for (const char* name : {"kicks", "drums", ""}) {
        CHECK(rig.goTo(Kind::Folder, name));
        hold(80);
        CHECK(list.isOpen() && list.deletePart() == 0.f && list.count() == 7);
        rig.key(B, false);
        CHECK(!list.isOpen() && session.sampleFolders().size() == 5);
        rig.tap(A);
    }
    // The empty one: the bar fills. Let go before the second is up: a tap, and it is still there.
    CHECK(rig.goTo(Kind::Folder, "void"));
    hold(30);
    CHECK(list.isOpen() && list.deletePart() > 0.3f && list.deletePart() < 0.6f);
    rig.key(B, false);
    CHECK(!list.isOpen() && exists(card.samples + "/void"));
    rig.tap(A);
    // Another button calls it off.
    CHECK(rig.goTo(Kind::Folder, "void"));
    hold(30);
    rig.tap(UP);
    rig.frames(60);
    CHECK(list.deletePart() == 0.f && exists(card.samples + "/void") && rig.on(Kind::Folder, "kicks", 1));
    rig.key(B, false);
    rig.tap(A);
    // Held for the second: it is gone, from the card and the list; letting go is no tap. The cursor is on the row that took its place.
    CHECK(rig.goTo(Kind::Folder, "void"));
    hold(62);
    CHECK(list.deletePart() > 0.9f && exists(card.samples + "/void") && SamplePicker::kDeleteHoldMs == 1000);
    rig.frames(3);
    CHECK(!exists(card.samples + "/void") && list.count() == 6 && list.deletePart() == 0.f && rig.on(Kind::NewFolder, ""));
    rig.key(B, false);
    CHECK(list.isOpen() && session.sampleFolders().size() == 4);
    // One with a file in it that is no recording reads as empty, and is NOT deleted: it says why.
    CHECK(rig.goTo(Kind::Folder, "junk"));
    hold(70);
    rig.key(B, false);
    CHECK(list.isOpen() && exists(card.samples + "/junk/notes.txt") && list.said() == "NOT DELETED: OTHER FILES ARE IN IT" && list.count() == 6 && rig.on(Kind::Folder, "junk"));
    // The folder in a folder first, from inside; then the one it was in.
    CHECK(rig.goTo(Kind::Folder, "drums"));
    rig.tap(RIGHT);
    CHECK(rig.on(Kind::Folder, "drums/snares"));
    hold(70);
    rig.key(B, false);
    CHECK(!exists(card.samples + "/drums/snares") && exists(card.samples + "/drums") && list.depth() == 1 && list.count() == 1 && rig.on(Kind::NewFolder, ""));
    rig.tap(LEFT);
    CHECK(rig.on(Kind::Folder, "drums"));
    hold(70);
    rig.key(B, false);
    CHECK(!exists(card.samples + "/drums") && list.isOpen() && session.sampleFolders() == std::vector<std::string>({"junk", "kicks"}));
    // Nothing of a recording went with any of it.
    CHECK(session.sampleNames() == std::vector<std::string>({"tom", "kicks/808"}));

    // Asked directly.
    CHECK(session.deleteSampleFolder("kicks") == Job::NotEmpty && session.deleteSampleFolder("junk") == Job::NotEmpty && card.has("kicks/808.wav"));
    CHECK(session.deleteSampleFolder("") == Job::Failed && session.deleteSampleFolder("no such") == Job::Failed && session.deleteSampleFolder("../songs") == Job::Failed && exists(card.songs));
    CHECK(session.deleteSampleFolder("tom.wav") == Job::Failed && card.has("tom.wav"));
    const std::string elsewhere = card.root + "/elsewhere";
    mkdir(elsewhere.c_str(), 0755);
    mkdir((elsewhere + "/in").c_str(), 0755);
    CHECK(symlink(elsewhere.c_str(), (card.samples + "/link").c_str()) == 0);
    CHECK(session.deleteSampleFolder("link") == Job::Failed && session.deleteSampleFolder("link/in") == Job::Failed && exists(elsewhere + "/in") && exists(card.samples + "/link"));
    card.folder("gone");
    CHECK(session.deleteSampleFolder("gone") == Job::Done && !exists(card.samples + "/gone"));
}

// The hints bar holds every hint in every state, and says what the buttons do there.
void testTheHintsFit() {
    Card card;
    for (const char* dir : {"kicks", "void"}) card.folder(dir);
    card.wav("kicks/808", 3000);
    card.wav("tom", 2000);
    Rig rig(card);
    SamplePicker& list = rig.list();
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    rig.tap(A);
    CHECK(rig.on(Kind::First, "") && rig.hintsFit(canvas));
    CHECK(rig.goTo(Kind::Folder, "kicks") && rig.hintsFit(canvas));
    CHECK(rig.goTo(Kind::Folder, "void") && rig.hintsFit(canvas));
    CHECK(rig.goTo(Kind::NewFolder, "") && rig.hintsFit(canvas));
    rig.tap(A);
    CHECK(list.typing() && rig.hintsFit(canvas));
    rig.type("AB");
    CHECK(rig.hintsFit(canvas));
    rig.tap(B, 3);
    CHECK(rig.goTo(Kind::Folder, "void"));
    rig.tap(RIGHT);
    CHECK(list.count() == 1 && rig.on(Kind::NewFolder, "") && rig.hintsFit(canvas));
    rig.tap(LEFT);
    CHECK(rig.goTo(Kind::Folder, "kicks"));
    rig.tap(RIGHT);
    CHECK(rig.on(Kind::Recording, "kicks/808") && rig.hintsFit(canvas));
    rig.key(R1, true);
    rig.frames(25);
    CHECK(!list.carried().empty() && rig.hintsFit(canvas));
    rig.tap(LEFT);
    CHECK(rig.hintsFit(canvas));
    rig.key(B, true);
    rig.key(B, false);
    rig.key(R1, false);
    rig.tap(RIGHT);
    rig.key(B, true);
    rig.frames(10);
    CHECK(list.deletePart() > 0.f && rig.hintsFit(canvas));
    rig.tap(UP);
    rig.key(B, false);

    // With no folders the list is one column: a recording there, and + NEW FOLDER.
    Card plain;
    plain.wav("one", 2000);
    Rig flat(plain);
    flat.tap(A);
    flat.tap(DOWN);
    CHECK(flat.list().flat() && flat.on(Kind::Recording, "one") && flat.hintsFit(canvas));
    flat.tap(DOWN);
    CHECK(flat.on(Kind::NewFolder, "") && flat.hintsFit(canvas));
}

} // namespace

int main(int, char**) {
    testMakingAFolder();
    testCarrying();
    testTheListByItself();
    testDeletingAnEmptyFolder();
    testTheHintsFit();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("picker tests passed\n");
    return 0;
}
