// CHOKE in the app: a new kit's hats in CHOKE 1, the KIT page's CHOKE column (hold A + up / down, B),
// the mark on the drum grid's lane name read off the drawn screen, the song file, undo and a song
// sent again. The engine's side (the cut, its fade, ratchets, slices, THROW, blocks) is
// tests/choke_test.cpp.
//
// What the engine was told is read from what it plays: the open hat's lane (4) holds a recording of
// one steady value a second long, the closed hat's lane (3) its built-in tick. The open hat is hit
// on step 1 and the closed hat on step 5: in one group, from just after the tick the output is silent.

#include <SDL.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "controls.hpp"
#include "pattern_layout.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "temp_folder.hpp"

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

const SDL_Keycode A = SDLK_x, B = SDLK_z, UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;
const int kStep = 6000; // 120 BPM at 48 kHz

struct TempFolder {
    std::string path;
    bool made = false;
    TempFolder() {
        path = tempFolder("choke");
        made = true;
        CHECK(writeWav(path + "/held.wav", std::vector<float>(48000, 0.5f), 48000));
    }
    ~TempFolder() {
        if (!made) return;
        std::remove((path + "/held.wav").c_str());
        rmdir(path.c_str());
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    Canvas canvas;

    explicit Rig(const std::string& sampleFolder = "") {
        if (!sampleFolder.empty()) app.session().useSampleFolder(sampleFolder);
        app.session().view = View::Pattern;
        frames(2);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) {
            key(code, true);
            frames(1);
            key(code, false);
            frames(1);
        }
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256); // (a test that never renders never empties the queue)
            engine->render(out.data(), 256);
        }
    }
    void settle() { frames(50); } // the burst of edits is over: a step of undo
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    TrackData& kit(int track = 0) { return project().tracks[track]; }
    // The open hat held on track `track`, hit on step 1; the closed hat on step 5.
    void hats(int track = 0) {
        for (int t = 0; t < project().trackCount(); ++t) { // (the other kits' hats out of the way)
            if (project().tracks[t].kind != TrackKind::Drums) continue;
            project().track = t;
            for (int s = 0; s < kSteps; ++s) app.drums().setStep(kOpenHatLane, s, 0.f), app.drums().setStep(kClosedHatLane, s, 0.f);
        }
        project().track = track;
        session().setLaneSample(track, kOpenHatLane, "held");
        for (int s = 0; s < kSteps; ++s) app.drums().setStep(kOpenHatLane, s, 0.f), app.drums().setStep(kClosedHatLane, s, 0.f);
        app.drums().setStep(kOpenHatLane, 0, 1.f);
        app.drums().setStep(kClosedHatLane, 4, 1.f);
        frames(4);
    }
    // Played from the top: is the open hat cut by the closed one? (Sounding up to step 5, and then
    // silent from when the tick is over: true. Still sounding there: false.)
    bool cut() {
        frames(30); // (whatever was heard by hand has rung out)
        session().togglePlay(false);
        std::vector<float> all, out(2 * 256);
        while (all.size() < static_cast<std::size_t>(7 * kStep)) {
            engine->render(out.data(), 256);
            for (int i = 0; i < 256; ++i) all.push_back(out[static_cast<std::size_t>(2 * i)]);
        }
        session().togglePlay(false);
        frames(4);
        const float before = all[static_cast<std::size_t>(4 * kStep - 1)];
        CHECK(before != 0.f && all[static_cast<std::size_t>(kStep)] == before); // (the open hat sounds, steady, up to there)
        bool silent = true, steady = true;
        for (long i = 4 * kStep + 4000; i < 7 * kStep; ++i) {
            silent = silent && all[static_cast<std::size_t>(i)] == 0.f;
            steady = steady && all[static_cast<std::size_t>(i)] == before;
        }
        CHECK(silent || steady);
        return silent;
    }
    void draw() {
        frames(1);
        canvas.clear();
        app.draw(canvas.r, *canvas.fonts);
        SDL_RenderPresent(canvas.r);
    }
    // The mark by a lane's name on the grid (nameY: the top of the name's box): how many pieces.
    int pieces(int nameY) const {
        using namespace pattern_layout;
        const int y = nameY + kCellH - kChokeMarkInset - kChokeMarkH + 1;
        int count = 0;
        for (int piece = 0; piece < 3; ++piece) {
            const int x = kChokeMarkRight - kChokeMarkW - piece * (kChokeMarkW + kChokeMarkGap);
            if (x < kNameX) break;
            bool whole = true;
            for (int px = x; px < x + kChokeMarkW; ++px) whole = whole && canvas.is(px, y, ui::kBrass);
            count += whole ? 1 : 0;
        }
        // Nothing brass where a piece does not stand: the gap, and just over the mark.
        CHECK(!canvas.is(kChokeMarkRight - kChokeMarkW - 1, y, ui::kBrass) && !canvas.is(kChokeMarkRight - 1, y - kChokeMarkH, ui::kBrass));
        return count;
    }
    int gridPieces(int lane) const { return pieces(pattern_layout::kGridTop + lane * pattern_layout::kLaneH); }
};

// ---- a new kit -------------------------------------------------------------------------------------

void testANewKit() {
    const TempFolder temp;
    {
        // The first track of a project as made (and of a song read from a file) has no group...
        Rig rig(temp.path);
        for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.kit().laneChoke[lane] == kChokeNone);
        rig.hats();
        CHECK(!rig.cut());
        // ...a drum track added has its hats in CHOKE 1, and the engine is told.
        CHECK(rig.session().addTrack(TrackKind::Drums));
        for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.kit(1).laneChoke[lane] == (lane == kClosedHatLane || lane == kOpenHatLane ? 1 : kChokeNone));
        rig.hats(1);
        CHECK(rig.cut());
        // Adding it is not a step of undo, nor are its groups.
        rig.settle();
        const int steps = rig.session().undoSteps();
        CHECK(rig.session().addTrack(TrackKind::Drums));
        rig.settle();
        CHECK(rig.session().undoSteps() == steps);
        // A synth track given the kit: a new kit.
        CHECK(rig.session().addTrack(TrackKind::Phazed));
        CHECK(rig.session().setTrackKind(3, TrackKind::Drums));
        CHECK(rig.kit(3).laneChoke[kClosedHatLane] == 1 && rig.kit(3).laneChoke[kOpenHatLane] == 1 && rig.kit(3).laneChoke[0] == kChokeNone);
        rig.hats(3);
        CHECK(rig.cut());
        // A new song: its kit too.
        rig.session().newSong();
        rig.frames(4);
        CHECK(rig.kit().laneChoke[kClosedHatLane] == 1 && rig.kit().laneChoke[kOpenHatLane] == 1 && rig.kit().laneChoke[1] == kChokeNone);
        rig.hats();
        CHECK(rig.cut());
    }
    {
        // The first time there is no song on the card (a song folder with nothing in it): a new song.
        Rig rig(temp.path);
        const std::string songs = tempFolder("chokes");
        rig.session().useSongFolder(songs, 1);
        rig.frames(2);
        CHECK(rig.kit().laneChoke[kClosedHatLane] == 1 && rig.kit().laneChoke[kOpenHatLane] == 1);
        rig.hats();
        CHECK(rig.cut());
        rmdir(songs.c_str());
    }
}

// ---- the KIT page ----------------------------------------------------------------------------------

void testTheKitPage() {
    const TempFolder temp;
    Rig rig(temp.path);
    KitScreen& kit = rig.app.kit();
    TrackData& track = rig.kit();
    rig.hats();
    rig.session().view = View::Device;
    rig.frames(1);
    CHECK(kit.cursorColumn() == KitScreen::kSound);
    // Right: FIT, CHOKE, EDIT, and round to SOUND; left back the other way.
    rig.tap(RIGHT);
    CHECK(kit.cursorColumn() == KitScreen::kFit);
    rig.tap(RIGHT);
    CHECK(kit.cursorColumn() == KitScreen::kChoke);
    rig.tap(RIGHT);
    CHECK(kit.cursorColumn() == KitScreen::kEdit);
    rig.tap(RIGHT);
    CHECK(kit.cursorColumn() == KitScreen::kSound);
    rig.tap(LEFT);
    CHECK(kit.cursorColumn() == KitScreen::kEdit);
    rig.tap(LEFT);
    CHECK(kit.cursorColumn() == KitScreen::kChoke);
    rig.tap(LEFT);
    CHECK(kit.cursorColumn() == KitScreen::kFit);
    rig.tap(RIGHT);
    // On the open hat: hold A, up is CHOKE 1, then 2, and it stops; down back to NONE, and it stops.
    rig.tap(DOWN, 3);
    CHECK(kit.cursorLane() == kOpenHatLane && kit.cursorColumn() == KitScreen::kChoke);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(UP);
    CHECK(track.laneChoke[kOpenHatLane] == 1 && kit.cursorLane() == kOpenHatLane); // (A held: up is not the cursor)
    rig.tap(UP);
    CHECK(track.laneChoke[kOpenHatLane] == 2);
    rig.tap(UP);
    CHECK(track.laneChoke[kOpenHatLane] == 2);
    rig.tap(LEFT); // sideways does nothing while A is held
    CHECK(kit.cursorColumn() == KitScreen::kChoke);
    rig.tap(DOWN, 3);
    CHECK(track.laneChoke[kOpenHatLane] == kChokeNone);
    rig.tap(UP);
    rig.key(A, false);
    rig.frames(1);
    CHECK(track.laneChoke[kOpenHatLane] == 1);
    CHECK(!kit.picker().isOpen()); // (a tap of A opens the list only on SOUND; this was no tap)
    // The closed hat to 1 as well: the engine has both, so the open hat is cut.
    rig.tap(UP);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    rig.frames(1);
    CHECK(track.laneChoke[kClosedHatLane] == 1);
    CHECK(rig.cut());
    // The closed hat in 2: not the open hat's group.
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(track.laneChoke[kClosedHatLane] == 2);
    CHECK(!rig.cut());
    // B: none. A tap of A on CHOKE does nothing.
    rig.tap(B);
    CHECK(track.laneChoke[kClosedHatLane] == kChokeNone && track.laneSample[kClosedHatLane][0] == 0 && track.laneFit[kClosedHatLane] == 0);
    rig.tap(A);
    CHECK(track.laneChoke[kClosedHatLane] == kChokeNone && !kit.picker().isOpen());
    // B on the other columns is still theirs: the recording, the fit. The group stays.
    rig.session().setLaneChoke(0, kOpenHatLane, 2);
    rig.tap(DOWN);
    rig.tap(LEFT);
    rig.tap(B);
    CHECK(track.laneFit[kOpenHatLane] == 0 && track.laneChoke[kOpenHatLane] == 2);
    rig.tap(LEFT);
    rig.tap(B);
    CHECK(track.laneSample[kOpenHatLane][0] == 0 && track.laneChoke[kOpenHatLane] == 2);
    // The hints fit, on every column, with A up and held.
    for (int column = 0; column < KitScreen::kColumns; ++column) {
        kit.goTo(kOpenHatLane, column);
        rig.draw();
        CHECK(ui::gHintsOver == 0.f);
        rig.key(A, true);
        rig.draw();
        CHECK(ui::gHintsOver == 0.f);
        rig.key(A, false);
        rig.frames(1);
    }
}

// ---- the mark on the grid -----------------------------------------------------------------------------

void testTheMark() {
    const TempFolder temp;
    Rig rig(temp.path);
    if (!rig.canvas.ok) return;
    Session& session = rig.session();
    session.setLaneChoke(0, 0, 1);
    session.setLaneChoke(0, 5, 2);
    session.setLaneChoke(0, 7, 2);
    session.setLaneFeel(0, 5, 100, 100); // (beside the feel's mark)
    rig.app.drums().moveCursorTo(5, -1); // (the cursor's outline on one of them)
    for (int theme : {kThemeBrown, kThemePaper}) {
        session.setTheme(theme);
        rig.draw();
        for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.gridPieces(lane) == rig.kit().laneChoke[lane]);
    }
    session.setTheme(kThemeBrown);
    // Taken off: the mark goes.
    session.setLaneChoke(0, 7, kChokeNone);
    rig.draw();
    CHECK(rig.gridPieces(7) == 0 && rig.gridPieces(5) == 2);
    // The slice view's name wears it too.
    session.setLaneSample(0, 5, "held");
    rig.frames(4);
    rig.app.drums().setSlicing(true);
    rig.draw();
    CHECK(rig.app.drums().slicing() && rig.pieces(pattern_layout::kGridTop) == 2);
    session.setLaneChoke(0, 5, 1);
    rig.draw();
    CHECK(rig.pieces(pattern_layout::kGridTop) == 1);
}

// ---- the song file ------------------------------------------------------------------------------------

void testTheSongFile() {
    Rig rig;
    Session& session = rig.session();
    CHECK(songText(rig.project()).find("choke") == std::string::npos); // none in no group: written as before
    session.chokeNewKit(0);
    session.setLaneChoke(0, 6, 2);
    const std::string text = songText(rig.project());
    CHECK(text.find("\nchoke 3 1\nchoke 4 1\nchoke 7 2\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(back->tracks[0].laneChoke[lane] == rig.kit().laneChoke[lane]);
    // A group nobody knows is none; a lane that is not there, or a line with no group, is passed by; under a
    // synth track it is not taken. Windows line ends too.
    std::string odd = text;
    const std::string mine = "choke 3 1\nchoke 4 1\nchoke 7 2\n";
    odd.replace(odd.find(mine), mine.size(), "choke 3 7\r\nchoke 4 2\r\nchoke 9 1\nchoke 0 1\nchoke 5\nchoke 6 -1\nchoke 2 1e300\nchoke 8 1.0\n");
    CHECK(parseSong(odd, *back));
    const std::uint8_t want[kDrumLanes] = {0, 0, 0, 2, 0, 0, 0, 1};
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(back->tracks[0].laneChoke[lane] == want[lane]);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(back->tracks[0].laneFeel[lane].atRest()); // (lane 0 wrote nothing outside the eight)
    std::unique_ptr<Project> synth(new Project);
    synth->tracks[0].kind = TrackKind::Phazed;
    std::string synthText = songText(*synth);
    synthText += "choke 3 1\n";
    CHECK(parseSong(synthText, *back));
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(back->tracks[0].laneChoke[lane] == kChokeNone);
}

// ---- undo, and a song sent again ------------------------------------------------------------------------

void testUndoAndASongSentAgain() {
    const TempFolder temp;
    Rig rig(temp.path);
    Session& session = rig.session();
    rig.hats();
    rig.settle();
    const int steps = session.undoSteps();
    session.view = View::Device;
    rig.app.kit().goTo(kClosedHatLane, KitScreen::kChoke);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    rig.app.kit().goTo(kOpenHatLane, KitScreen::kChoke);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    rig.settle();
    CHECK(rig.kit().laneChoke[kClosedHatLane] == 1 && rig.kit().laneChoke[kOpenHatLane] == 1);
    CHECK(session.undoSteps() == steps + 1); // (the turns of one burst: one step)
    CHECK(rig.cut());
    CHECK(session.undo());
    CHECK(rig.kit().laneChoke[kClosedHatLane] == kChokeNone && rig.kit().laneChoke[kOpenHatLane] == kChokeNone);
    CHECK(!rig.cut());
    CHECK(session.redo());
    CHECK(rig.kit().laneChoke[kClosedHatLane] == 1 && rig.kit().laneChoke[kOpenHatLane] == 1);
    CHECK(rig.cut());
    // A hit edited after: undoing it leaves the groups.
    rig.settle();
    rig.app.drums().setStep(5, 3, 0.8f);
    rig.settle();
    CHECK(session.undo());
    CHECK(rig.app.drums().level(5, 3) == 0.f && rig.kit().laneChoke[kOpenHatLane] == 1);
    // The whole song sent again: the engine has the groups...
    session.applyProject();
    rig.frames(4);
    CHECK(rig.cut());
    // ...and a song with none: not cut, though the engine had them.
    rig.kit().laneChoke[kClosedHatLane] = kChokeNone; // (as a song read from a file: the project alone changes)
    session.applyProject();
    rig.frames(4);
    CHECK(!rig.cut());
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testANewKit", testANewKit}, {"testTheKitPage", testTheKitPage}, {"testTheMark", testTheMark}, {"testTheSongFile", testTheSongFile},
        {"testUndoAndASongSentAgain", testUndoAndASongSentAgain},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("chokeapp_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
