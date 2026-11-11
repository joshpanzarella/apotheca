// Tests for songs on disk: the file format, saving as you go, opening, and
// the engine being told everything when a song comes back.

#include <SDL.h>

#include <sys/stat.h>
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
#include "song_file.hpp"

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

const int kStep = 6000, kBar = 16 * kStep;
const SDL_Keycode X = SDLK_s, Y = SDLK_a;
const SDL_Keycode A = SDLK_x, B = SDLK_z, START = SDLK_RETURN, SELECT = SDLK_BACKSPACE, L2 = SDLK_1, UP = SDLK_UP, DOWN = SDLK_DOWN;

std::string tempFolder() {
    char pattern[] = "/tmp/apotheca-test-XXXXXX";
    const char* made = mkdtemp(pattern);
    return made ? made : "/tmp";
}
bool exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    explicit Rig(const std::string& folder = "", std::uint32_t seed = 7) {
        if (!folder.empty()) app.session().useSongFolder(folder, seed);
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
    std::vector<float> render(int n) {
        std::vector<float> out(2 * n);
        engine->render(out.data(), n);
        return out;
    }
    Project& project() { return app.project(); }
    Session& session() { return app.session(); }
};

float peakOf(const std::vector<float>& v) {
    float p = 0.f;
    for (float s : v) p = std::fabs(s) > p ? std::fabs(s) : p;
    return p;
}

// A song with something of everything in it, made without a sound (as if the
// song were playing, so nothing is auditioned and the engine stays untouched
// until it is asked to play).
void fillSong(Rig& rig) {
    Project& p = rig.project();
    p.playing = true;
    PatternScreen& drums = rig.app.drums();
    drums.setStep(0, 0, 1.f);
    drums.setStep(0, 8, 0.7f);
    drums.setStep(1, 4, 0.9f);
    drums.setStep(2, 2, 0.5f);
    drums.setNudge(2, 2, 0.3f);
    drums.setNudge(1, 4, -0.2f);
    p.scene = 2;
    drums.setStep(6, 5, 0.6f);
    p.scene = 0;
    drums.moveCursorTo(3, -1); // a drum's volume, the way the buttons set it
    rig.key(A, true);
    rig.tap(DOWN, 4);
    rig.key(A, false);
    drums.moveCursorTo(0, 0);

    rig.session().addTrack(TrackKind::Phazed);
    rig.session().addTrack(TrackKind::Twin);
    p.track = 1;
    rig.app.notes().moveCursorTo(48, 0);
    rig.tap(A);
    rig.key(A, true);
    rig.tap(SDLK_DOWN, 3);
    rig.key(A, false);
    rig.app.notes().moveCursorTo(55, 8);
    rig.tap(A);
    rig.session().view = View::Device;
    rig.app.phazed().goTo(0, SynthScreen::kPreset);
    rig.key(A, true);
    rig.tap(UP, 9);
    rig.key(A, false);
    rig.app.phazed().goTo(0, SynthScreen::kFirstMacro + phazed::Tone);
    rig.key(A, true);
    rig.tap(UP, 4);
    rig.key(A, false);
    rig.app.phazed().goTo(1, phazed::Level);
    rig.key(A, true);
    rig.tap(DOWN, 3);
    rig.key(A, false);

    p.track = 2;
    p.scene = 1;
    rig.session().view = View::Pattern;
    rig.app.notes().moveCursorTo(60, 0);
    rig.tap(A);
    rig.app.notes().moveCursorTo(64, 0);
    rig.tap(A);
    rig.session().view = View::Device;
    rig.app.twin().goTo(0, SynthScreen::kPreset);
    rig.key(A, true);
    rig.tap(UP, 4);
    rig.key(A, false);
    rig.app.twin().goTo(1, 2); // V SHAPE
    rig.key(A, true);
    rig.tap(UP, 7);
    rig.key(A, false);

    p.scene = 0;
    p.track = 0;
    rig.session().view = View::Song;
    rig.app.song().moveCursorTo(0, -1);
    rig.tap(A); // scene 1 plays twice
    rig.app.song().moveCursorTo(0, 0);
    p.playing = false;
    p.tempo = 97.f;
    Command tempo;
    tempo.type = Command::SetTempo;
    tempo.value = 97.f;
    rig.engine->send(tempo);
}

void testTextRoundTrip() {
    Rig rig;
    fillSong(rig);
    rig.project().tracks[0].drums[0][0].fromDice[2][2] = true;
    rig.project().tracks[0].drums[0][0].diceSeed[2] = 123456789u;
    const std::string text = songText(rig.project());
    CHECK(text.compare(0, 16, "apotheca-song 1\n") == 0);
    CHECK(text.find("track drums") != std::string::npos && text.find("track phazed") != std::string::npos && text.find("track twin") != std::string::npos);
    CHECK(text.find("tempo 97") != std::string::npos);

    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    CHECK(songText(*back) == text); // nothing lost, nothing added
    // And spot checks, in case reading and writing were wrong in the same way.
    CHECK(back->trackCount() == 3);
    CHECK(back->tempo == 97.f && back->repeats[0] == 2 && back->repeats[1] == 1);
    const TrackData& drums = back->tracks[0];
    CHECK(drums.kind == TrackKind::Drums);
    CHECK(drums.drums[0][0].level[0][0] == 1.f && std::fabs(drums.drums[0][0].level[0][8] - 0.7f) < 1e-5f);
    CHECK(std::fabs(drums.drums[0][0].nudge[2][2] - 0.3f) < 1e-5f && std::fabs(drums.drums[0][0].nudge[1][4] + 0.2f) < 1e-5f);
    CHECK(drums.drums[0][0].fromDice[2][2] && !drums.drums[0][0].fromDice[0][0] && drums.drums[0][0].diceSeed[2] == 123456789u);
    CHECK(std::fabs(drums.drums[2][0].level[6][5] - 0.6f) < 1e-5f && drums.drums[1][0].empty());
    CHECK(std::fabs(drums.laneLevel[3] - 0.6f) < 1e-5f && drums.laneLevel[0] == 1.f);
    const TrackData& one = back->tracks[1];
    CHECK(one.kind == TrackKind::Phazed && one.preset == 9);
    CHECK(std::fabs(one.macro[phazed::Tone] - 0.7f) < 1e-5f && one.macro[phazed::Motion] == 0.5f);
    CHECK(one.patch.value[phazed::Level] == Phazed::presetPatch(9).value[phazed::Level] - 3);
    CHECK(one.notes[0][0].at(48, 3) >= 0 && one.notes[0][0].at(48, 4) < 0 && one.notes[0][0].at(55, 8) >= 0);
    const TrackData& two = back->tracks[2];
    CHECK(two.kind == TrackKind::Twin && two.preset == 4);
    CHECK(two.twin.value[twin::VShape] == rig.project().tracks[2].twin.value[twin::VShape]);
    CHECK(two.twin.value[twin::VShape] != Twin::presetPatch(4).value[twin::VShape]);
    CHECK(two.notes[1][0].at(60, 0) >= 0 && two.notes[1][0].at(64, 0) >= 0 && two.notes[0][0].empty());
}

void testBadFiles() {
    std::unique_ptr<Project> p(new Project);
    p->tempo = 133.f;
    // Not a song: refused, and the project is left alone.
    CHECK(!parseSong("", *p));
    CHECK(!parseSong("hello\ntempo 90\n", *p));
    CHECK(p->tempo == 133.f);
    // A song with nonsense in it: the nonsense is skipped or held to limits.
    const std::string odd =
        "apotheca-song 1\n"
        "tempo 9999\n"
        "repeats 0 99 3\n"
        "hit 1 1 1\n"                      // before any track
        "track kazoo\n"                    // an instrument this version does not have
        "scene 1\nhit 1 1 1\n"             // ...so its lines are skipped
        "track drums\n"
        "lanes 5 -1 nonsense\n"
        "scene 0\nhit 1 1 1\n"             // no such scene
        "scene 99\nhit 1 2 1\n"
        "scene 1\n"
        "hit 0 1 1\nhit 9 1 1\nhit 1 0 1\nhit 1 17 1\nhit 1 3 0\nhit 1 4 nan\n" // none of these
        "hit 2 5 7 -9 0 1\n"               // held to limits
        "hit 3 6 0.5\n"                    // short: the rest take their usual values
        "note 1 60 1 1\n"                  // a note on a drum track
        "something new 1 2 3\n"            // a line from a later version
        "track phazed\n"
        "preset 9999\n"
        "macros 7 -7\n"
        "settings 999 -999\n"
        "scene 1\n"
        "note 16 60 9 0.5\nnote 0 60 1 1\nnote 1 128 1 1\nnote 1 60 1 0\n"
        "hit 1 1 1\n";                     // a hit on a synth track
    CHECK(parseSong(odd, *p));
    CHECK(p->tempo == kMaxTempo);
    CHECK(p->repeats[0] == 1 && p->repeats[1] == kMaxRepeats && p->repeats[2] == 3 && p->repeats[3] == 1);
    CHECK(p->trackCount() == 2 && p->tracks[0].kind == TrackKind::Drums && p->tracks[1].kind == TrackKind::Phazed);
    const DrumPattern& d = p->tracks[0].drums[0][0];
    int hits = 0;
    for (int lane = 0; lane < kDrumLanes; ++lane) for (int step = 0; step < kSteps; ++step) hits += d.level[lane][step] > 0.f ? 1 : 0;
    CHECK(hits == 2);
    CHECK(d.level[1][4] == 1.f && d.nudge[1][4] == -0.5f && std::fabs(d.chance[1][4] - 0.05f) < 1e-6f && d.fromDice[1][4]);
    CHECK(d.level[2][5] == 0.5f && d.nudge[2][5] == 0.f && d.chance[2][5] == 1.f);
    CHECK(p->tracks[0].laneLevel[0] == 1.f && p->tracks[0].laneLevel[1] == 0.f && p->tracks[0].laneLevel[2] == 1.f);
    CHECK(p->tracks[0].notes[0][0].empty());
    const TrackData& synth = p->tracks[1];
    CHECK(synth.preset == Phazed::presetCount() - 1);
    CHECK(synth.macro[0] == 1.f && synth.macro[1] == 0.f && synth.macro[2] == 0.5f);
    CHECK(synth.patch.value[0] == phazed::settingMax(0) && synth.patch.value[1] == phazed::settingMin(1));
    int notes = 0;
    for (const NoteEvent& n : synth.notes[0][0].notes) notes += n.velocity > 0.f ? 1 : 0;
    CHECK(notes == 1);
    CHECK(synth.notes[0][0].notes[0].step == 15 && synth.notes[0][0].notes[0].length == 1); // cut to the end of the bar
    CHECK(synth.drums[0][0].empty());

    // More than twelve tracks: twelve. More than 64 notes in a bar: 64. No tracks: one drum track.
    std::string many = "apotheca-song 1\n";
    for (int i = 0; i < 15; ++i) many += "track drums\n";
    CHECK(parseSong(many, *p) && p->trackCount() == kNumTracks);
    std::string crowded = "apotheca-song 1\ntrack twin\nscene 1\n";
    for (int i = 0; i < 80; ++i) crowded += "note " + std::to_string(i % 16 + 1) + " " + std::to_string(30 + i) + " 1 1\n";
    CHECK(parseSong(crowded, *p));
    int kept = 0;
    for (const NoteEvent& n : p->tracks[0].notes[0][0].notes) kept += n.velocity > 0.f ? 1 : 0;
    CHECK(kept == kMaxNotes);
    CHECK(parseSong("apotheca-song 1\n", *p) && p->trackCount() == 1 && p->tracks[0].kind == TrackKind::Drums);
    // A file from a later version still opens.
    CHECK(parseSong("apotheca-song 7\ntempo 100\n", *p) && p->tempo == 100.f);
}

void testSavedAsYouGoAndBackAtTheNextStart() {
    const std::string folder = tempFolder();
    std::string name, text;
    {
        Rig rig(folder);
        name = rig.session().songName();
        CHECK(!name.empty() && name.find(' ') != std::string::npos); // two words
        rig.frames(200);
        CHECK(!exists(folder + "/" + name + ".song")); // an empty song is not worth a file
        fillSong(rig);
        rig.frames(10);
        CHECK(!exists(folder + "/" + name + ".song")); // not yet: two seconds have not passed
        rig.frames(140);
        CHECK(exists(folder + "/" + name + ".song"));
        CHECK(exists(folder + "/last.txt"));
        text = songText(rig.project());
        std::string onDisk;
        CHECK(readTextFile(folder + "/" + name + ".song", onDisk) && onDisk == text);
        // Nothing changed: nothing is written. (Take the file away and see it stay away.)
        std::remove((folder + "/" + name + ".song").c_str());
        rig.frames(300);
        CHECK(!exists(folder + "/" + name + ".song"));
        // One more hit, and it is back with the hit in it.
        rig.app.drums().setStep(7, 7, 1.f);
        rig.frames(140);
        CHECK(readTextFile(folder + "/" + name + ".song", onDisk) && onDisk.find("hit 8 8 1") != std::string::npos);
        // A change in the last moments is caught on the way out.
        rig.app.drums().setStep(7, 9, 1.f);
        rig.session().saveNow();
        CHECK(readTextFile(folder + "/" + name + ".song", onDisk) && onDisk.find("hit 8 10 1") != std::string::npos);
        text = songText(rig.project());
    }
    // The next start: the same song, by name, with everything in it.
    Rig next(folder, 99);
    CHECK(next.session().songName() == name);
    CHECK(songText(next.project()) == text);
    CHECK(next.project().trackCount() == 3);
    // No half-written file is left about.
    CHECK(!exists(folder + "/" + name + ".song.part"));
}

void testAnOpenedSongSoundsLikeTheOneSaved() {
    const std::string folder = tempFolder();
    Rig made(folder);
    fillSong(made);
    made.session().saveNow();
    Rig opened(folder, 99);
    CHECK(songText(opened.project()) == songText(made.project()));

    // Play both from the top: every sample must match. The first has been
    // told each thing as it was done; the second, everything at once from the file.
    for (Rig* rig : {&made, &opened}) {
        rig->session().view = View::Song;
        rig->frames(1);
        rig->render(96000); // making the song played a drum by hand once; let that ring out
        rig->tap(START);
    }
    const std::vector<float> a = made.render(6 * kBar), b = opened.render(6 * kBar);
    CHECK(peakOf(a) > 0.05f);
    CHECK(a == b);
    for (int track = 0; track < 3; ++track) CHECK(opened.engine->trackPeak(track) > 0.f || made.engine->trackPeak(track) == 0.f);
    CHECK(opened.engine->playScene() == made.engine->playScene());
}

void testNewSongAndTheList() {
    const std::string folder = tempFolder();
    Rig rig(folder);
    fillSong(rig);
    rig.session().saveNow();
    const std::string first = rig.session().songName();

    rig.session().newSong();
    const std::string second = rig.session().songName();
    CHECK(second != first);
    CHECK(rig.project().trackCount() == 1 && rig.project().tracks[0].drums[0][0].empty() && rig.project().tempo == 120.f);
    // The engine was emptied with it: playing makes no sound, and the two synths are gone.
    rig.tap(L2);
    rig.tap(START);
    CHECK(peakOf(rig.render(2 * kBar)) == 0.f);
    rig.tap(START);
    Command note;
    note.type = Command::NoteOn;
    note.track = 1;
    note.a = 60;
    note.value = 1.f;
    rig.engine->send(note);
    CHECK(peakOf(rig.render(4000)) == 0.f);
    // And its tracks can be used again.
    CHECK(rig.session().addTrack(TrackKind::Twin));
    rig.engine->send(note);
    CHECK(peakOf(rig.render(4000)) > 0.f);
    rig.project().playing = true; // silent placing
    rig.app.drums().setStep(4, 4, 1.f);
    rig.project().playing = false;
    rig.session().saveNow();

    // The list: both, the one just saved first.
    const std::vector<std::string> names = rig.session().songNames();
    CHECK(names.size() == 2);
    CHECK(names.size() == 2 && names[0] == second && names[1] == first);

    // Opening the first brings all of it back, and saves the second on the
    // way, with a change made a moment ago and not yet written.
    rig.project().playing = true;
    rig.app.drums().setStep(5, 5, 1.f);
    rig.project().playing = false;
    CHECK(rig.session().openSong(first));
    CHECK(rig.session().songName() == first && rig.project().trackCount() == 3);
    CHECK(rig.project().tracks[1].kind == TrackKind::Phazed); // track 2 is Phazed again, not the Twin just added
    CHECK(!rig.session().openSong("no such song"));
    CHECK(rig.session().songName() == first);
    std::string onDisk;
    CHECK(readTextFile(folder + "/" + second + ".song", onDisk) && onDisk.find("track twin") != std::string::npos);
    CHECK(onDisk.find("hit 6 6 1") != std::string::npos);
    // The next start opens the one that was open last.
    Rig next(folder, 5);
    CHECK(next.session().songName() == first);
}

void testTheSongListOnScreen() {
    const std::string folder = tempFolder();
    Rig rig(folder);
    fillSong(rig);
    rig.session().saveNow();
    const std::string first = rig.session().songName();
    rig.session().view = View::Song;

    // SELECT, alone, opens the list. B closes it.
    rig.tap(SELECT);
    CHECK(rig.app.song().listingSongs());
    rig.tap(B);
    CHECK(!rig.app.song().listingSongs());
    rig.tap(SELECT);
    rig.tap(SELECT);
    CHECK(!rig.app.song().listingSongs());
    // SELECT held with something else is not "alone".
    rig.key(SELECT, true);
    rig.tap(DOWN);
    rig.key(SELECT, false);
    CHECK(!rig.app.song().listingSongs());

    // The top row starts a new song.
    rig.tap(SELECT);
    rig.tap(A);
    CHECK(!rig.app.song().listingSongs());
    const std::string second = rig.session().songName();
    CHECK(second != first && rig.project().trackCount() == 1);
    rig.project().playing = true;
    rig.app.drums().setStep(1, 1, 1.f);
    rig.project().playing = false;

    // Below it, the saved songs. The first one made is there to open.
    rig.tap(SELECT);
    rig.tap(UP); // already at the top: stays
    rig.tap(DOWN);
    rig.tap(A);
    CHECK(rig.session().songName() == first || rig.session().songName() == second);
    if (rig.app.song().listingRevs()) rig.tap(SELECT); // it was the open song: A showed its revs. Close them.
    CHECK(!rig.app.song().listingSongs());
    if (rig.session().songName() == second) { // the new one was listed first; the old one is next
        rig.tap(SELECT);
        rig.tap(DOWN, 2);
        rig.tap(A);
    }
    CHECK(rig.session().songName() == first && rig.project().trackCount() == 3);
    CHECK(rig.app.song().cursorScene() == 0 && rig.app.song().cursorColumn() == 0);
    // While the list is open the buttons do nothing else.
    rig.tap(SELECT);
    rig.tap(START);
    CHECK(!rig.project().playing);
    rig.tap(B);
    // Without a folder (as in the other tests) the list still offers a new song.
    Rig bare;
    bare.session().view = View::Song;
    bare.app.drums().setStep(0, 0, 1.f);
    bare.tap(SELECT);
    bare.tap(A);
    CHECK(bare.project().tracks[0].drums[0][0].empty());
}

void testFreeNoteLengthsAreKept() {
    std::unique_ptr<Project> song(new Project);
    CHECK(parseSong("apotheca-song 1\ntrack phazed\nscene 1\nnote 3 60 2.375 0.8\nnote 16 61 9 0.8\nnote 1 62 0.001 0.8\nchord 5 1.5 0.8 0 60 C 0 4 7\n", *song));
    const NotePattern& p = song->tracks[0].notes[0][0];
    CHECK(p.notes[0].length == 2.375f);
    CHECK(p.notes[1].length == 1.f);              // not past the end of the bar
    CHECK(p.notes[2].length == kShortestNote);    // nor shorter than the engine will play
    CHECK(p.chords[0].length == 1.5f);
    const std::string text = songText(*song);
    CHECK(text.find("note 3 60 2.375 0.8\n") != std::string::npos && text.find("chord 5 1.5 0.8 ") != std::string::npos);
    std::unique_ptr<Project> again(new Project);
    CHECK(parseSong(text, *again) && songText(*again) == text);
}

void testRevs() {
    const std::string folder = tempFolder();
    Rig rig(folder);
    Session& session = rig.session();
    fillSong(rig);
    rig.project().playing = false;
    session.saveNow();
    const std::string name = session.songName();
    const std::string first = songText(rig.project());
    const float firstTempo = rig.project().tempo;
    CHECK(firstTempo != 90.f && firstTempo != 150.f && firstTempo != 77.f);

    // A rev is the song as it is now, in a file of its own.
    CHECK(session.revs().empty());
    CHECK(session.saveRev() == 1);
    CHECK(exists(folder + "/" + name + ".1.rev"));
    std::string text;
    CHECK(readTextFile(folder + "/" + name + ".1.rev", text) && text == first);
    CHECK(!session.revWhen(1).empty() && session.revWhen(2).empty());
    rig.project().tempo = 90.f;
    CHECK(session.saveRev() == 2);
    CHECK(session.revs() == std::vector<int>({2, 1})); // newest first
    CHECK(readTextFile(folder + "/" + name + ".song", text) && text.find("tempo 90") != std::string::npos); // saving a rev saves the song too
    CHECK(session.songNames().size() == 1); // revs are not songs
    // Files that only look like revs are not listed.
    writeTextFile(folder + "/" + name + ".x.rev", first);
    writeTextFile(folder + "/" + name + ".0.rev", first);
    writeTextFile(folder + "/" + name + "..rev", first);
    writeTextFile(folder + "/" + name + ".1234567.rev", first);
    writeTextFile(folder + "/someone else.7.rev", first);
    writeTextFile(folder + "/" + name + ".5.song", first);
    writeTextFile(folder + "/" + name + ".7.txt", first);
    CHECK(session.revs() == std::vector<int>({2, 1}));
    std::remove((folder + "/" + name + ".5.song").c_str());

    // Going back: where the song was is kept first, because no rev holds it.
    rig.project().tempo = 150.f;
    const std::string third = songText(rig.project());
    CHECK(session.openRev(1));
    CHECK(session.revs() == std::vector<int>({3, 2, 1}));
    CHECK(readTextFile(folder + "/" + name + ".3.rev", text) && text == third);
    CHECK(songText(rig.project()) == first && rig.project().tempo == firstTempo);
    CHECK(readTextFile(folder + "/" + name + ".song", text) && text == first); // and the song file follows
    CHECK(readTextFile(folder + "/" + name + ".1.rev", text) && text == first); // the rev itself is never rewritten
    CHECK(session.songName() == name);
    // Now the song is what rev 1 holds: going elsewhere needs no new rev.
    CHECK(session.openRev(2));
    CHECK(session.revs().size() == 3 && rig.project().tempo == 90.f);
    CHECK(session.openRev(2)); // to where it already is
    CHECK(session.revs().size() == 3);
    // A rev that is not there, or is not a song: nothing happens.
    CHECK(!session.openRev(99));
    writeTextFile(folder + "/" + name + ".9.rev", "not a song");
    rig.project().tempo = 101.f;
    CHECK(!session.openRev(9));
    CHECK(rig.project().tempo == 101.f && session.revs().size() == 4);
    std::remove((folder + "/" + name + ".9.rev").c_str());

    // The engine goes back too: empty the song, hear nothing, go back, hear them.
    {
        std::unique_ptr<Project> blank(new Project);
        rig.project() = *blank;
    }
    session.applyProject();
    rig.session().view = View::Pattern;
    rig.tap(START);
    CHECK(peakOf(rig.render(24000)) < 1e-5f);
    rig.tap(START);
    CHECK(session.openRev(1));
    rig.tap(START);
    CHECK(peakOf(rig.render(24000)) > 0.01f);
    rig.tap(START);

    // ---- on screen: SELECT, then X saves one ----------------------------------------
    rig.session().view = View::Song;
    const int before = static_cast<int>(session.revs().size());
    rig.tap(SELECT);
    rig.tap(X);
    CHECK(static_cast<int>(session.revs().size()) == before + 1 && rig.app.song().listingSongs());
    // A on the song that is open shows its revs.
    rig.tap(DOWN);
    rig.tap(A);
    CHECK(rig.app.song().listingRevs() && rig.app.song().listCursor() == 0);
    CHECK(session.songName() == name);
    rig.tap(A); // the top row: save a rev
    CHECK(static_cast<int>(session.revs().size()) == before + 2 && rig.app.song().listingRevs());
    rig.tap(X); // X does it here as well, and the list shows the new one
    CHECK(static_cast<int>(session.revs().size()) == before + 3);
    rig.tap(DOWN, 50);
    CHECK(rig.app.song().listCursor() == before + 3); // stops at the oldest
    rig.tap(UP, 50);
    CHECK(rig.app.song().listCursor() == 0);
    // B goes back to the songs, not out.
    rig.tap(B);
    CHECK(rig.app.song().listingSongs() && !rig.app.song().listingRevs());
    rig.tap(A);
    CHECK(rig.app.song().listingRevs());
    // A on a rev goes back to it and closes the list. The oldest is rev 1.
    rig.project().tempo = 77.f;
    rig.tap(DOWN, 50);
    rig.tap(A);
    CHECK(!rig.app.song().listingSongs() && rig.project().tempo == firstTempo);
    CHECK(songText(rig.project()) == first);
    // SELECT again opens on the songs, not the revs.
    rig.tap(SELECT);
    CHECK(rig.app.song().listingSongs() && !rig.app.song().listingRevs());
    rig.tap(SELECT);

    // Revs belong to their song.
    session.newSong();
    CHECK(session.revs().empty());
    CHECK(session.openSong(name) && session.revs().size() >= 6);

    // An empty song that was never written has nothing to keep.
    session.newSong();
    CHECK(session.saveRev() == 0 && session.revs().empty());

    // With no song folder there is nowhere to keep one.
    Rig loose;
    CHECK(loose.session().saveRev() == 0 && loose.session().revs().empty() && !loose.session().openRev(1));
}

void testASongMadeAMomentAgoIsInTheList() {
    // Nothing has been saved yet when SELECT is pressed: the list must still
    // show the song, so its revs can be reached.
    const std::string folder = tempFolder();
    Rig rig(folder);
    fillSong(rig);
    rig.project().playing = false;
    rig.session().view = View::Song;
    CHECK(rig.session().songNames().empty());
    rig.tap(SELECT);
    rig.tap(DOWN);
    CHECK(rig.app.song().listCursor() == 1);
    rig.tap(A);
    CHECK(rig.app.song().listingRevs());
    rig.tap(SELECT);
}

void testRenamingAndDeletingSongs() {
    const std::string folder = tempFolder();
    Rig rig(folder);
    Session& session = rig.session();
    SongScreen& song = rig.app.song();
    fillSong(rig);
    rig.project().playing = false;
    session.saveNow();
    const std::string first = session.songName();
    const std::string text = songText(rig.project());
    CHECK(session.saveRev() == 1 && session.saveRev() == 2);
    // A neighbour whose name starts the same way must never be touched.
    writeTextFile(folder + "/" + first + " two.song", text);
    writeTextFile(folder + "/" + first + " two.1.rev", text);

    // ---- a new name: Y on the open song in the list ---------------------------------
    session.view = View::Song;
    // Put the list's cursor on a song, wherever the list has it just now.
    const auto goToSong = [&](const std::string& name) {
        rig.tap(UP, 20);
        for (std::size_t i = 0; i < song.listedSongs().size(); ++i)
            if (song.listedSongs()[i] == name) rig.tap(DOWN, static_cast<int>(i) + 1);
    };
    rig.tap(SELECT);
    goToSong(first);
    rig.tap(Y);
    const std::string second = session.songName();
    CHECK(second != first && !exists(folder + "/" + first + ".song") && exists(folder + "/" + second + ".song"));
    CHECK(exists(folder + "/" + second + ".1.rev") && exists(folder + "/" + second + ".2.rev") && !exists(folder + "/" + first + ".1.rev"));
    CHECK(session.revs() == std::vector<int>({2, 1}));
    std::string last;
    CHECK(readTextFile(folder + "/last.txt", last) && last == second + "\n");
    CHECK(exists(folder + "/" + first + " two.song") && exists(folder + "/" + first + " two.1.rev"));
    CHECK(song.listingSongs() && song.listedSongs()[static_cast<std::size_t>(song.listCursor() - 1)] == second); // the cursor is still on it
    CHECK(songText(rig.project()) == text);
    rig.tap(Y); // again: another
    CHECK(session.songName() != second && session.songName() != first);
    const std::string third = session.songName();
    // Y on a song that is not the open one, or on NEW SONG, does nothing.
    rig.tap(UP, 9);
    rig.tap(Y);
    CHECK(session.songName() == third);
    goToSong(first + " two");
    rig.tap(Y);
    CHECK(session.songName() == third && exists(folder + "/" + first + " two.song"));

    // ---- deleting: hold B on a song for a second --------------------------------------
    // A tap of B only closes the list.
    rig.tap(B);
    CHECK(!song.listingSongs() && exists(folder + "/" + first + " two.song"));
    rig.tap(SELECT);
    goToSong(first + " two");
    // Held, but not long enough.
    rig.key(B, true);
    rig.frames(50); // 0.8 s
    CHECK(exists(folder + "/" + first + " two.song") && song.listingSongs());
    rig.key(B, false);
    CHECK(!song.listingSongs());
    // Held, and the cursor moved off it: called off.
    rig.tap(SELECT);
    goToSong(first + " two");
    rig.key(B, true);
    rig.frames(30);
    rig.key(SDLK_i, true); // the stick, up: no button is pressed, but the cursor leaves the row
    rig.frames(2);
    rig.key(SDLK_i, false);
    rig.frames(60);
    rig.key(B, false);
    CHECK(exists(folder + "/" + first + " two.song") && song.listingSongs());
    goToSong(first + " two");
    // Held for the whole second: gone from the list, and moved, not destroyed.
    rig.key(B, true);
    rig.frames(64);
    CHECK(!exists(folder + "/" + first + " two.song") && !exists(folder + "/" + first + " two.1.rev"));
    CHECK(exists(folder + "/deleted/" + first + " two.song") && exists(folder + "/deleted/" + first + " two.1.rev"));
    CHECK(song.listingSongs() && song.listedSongs() == std::vector<std::string>({third}));
    rig.key(B, false); // letting go after that does not also close the list
    CHECK(song.listingSongs());
    CHECK(session.songName() == third && exists(folder + "/" + third + ".song") && exists(folder + "/" + third + ".2.rev"));
    CHECK(readTextFile(folder + "/deleted/" + first + " two.song", last) && last == text);
    // NEW SONG cannot be deleted.
    rig.tap(UP, 5);
    rig.key(B, true);
    rig.frames(80);
    rig.key(B, false);
    CHECK(exists(folder + "/" + third + ".song"));

    // Deleting the open song: a new, empty one takes its place, and nothing is written over what was moved.
    // Files that only look like its own stay where they are.
    writeTextFile(folder + "/" + third + "s.song", text);
    writeTextFile(folder + "/" + third + ".old.rev", text);
    writeTextFile(folder + "/" + third + ".7x.rev", text);
    rig.tap(SELECT);
    goToSong(third);
    rig.key(B, true);
    rig.frames(64);
    rig.key(B, false);
    CHECK(!exists(folder + "/" + third + ".song") && exists(folder + "/deleted/" + third + ".song") && exists(folder + "/deleted/" + third + ".1.rev"));
    CHECK(session.songName() != third && rig.project().trackCount() == 1 && rig.project().tracks[0].drums[0][0].empty());
    CHECK(song.listedSongs() == std::vector<std::string>({third + "s"}));
    CHECK(exists(folder + "/" + third + "s.song") && exists(folder + "/" + third + ".old.rev") && exists(folder + "/" + third + ".7x.rev"));
    rig.frames(200); // the autosave comes round: an empty song is still not worth a file
    session.saveNow();
    CHECK(session.songNames().size() == 1 && !exists(folder + "/" + third + ".song"));
    CHECK(readTextFile(folder + "/deleted/" + third + ".song", last) && last == text);
    // At the next start there is no "last song" to come back to.
    Rig next(folder);
    CHECK(next.project().trackCount() == 1 && next.project().tracks[0].drums[0][0].empty());
    // What cannot be deleted.
    CHECK(!session.deleteSong("no such song") && !session.deleteSong(""));
    // A name is never a path: nothing outside the song folder can be reached.
    const std::string outside = folder.substr(folder.rfind('/') + 1) + "-victim";
    writeTextFile(folder + "/../" + outside + ".song", text);
    CHECK(!session.deleteSong("../" + outside) && exists(folder + "/../" + outside + ".song"));
    std::remove((folder + "/../" + outside + ".song").c_str());
    Rig loose;
    CHECK(!loose.session().deleteSong("x") && loose.session().renameSong() == loose.session().songName());
}

void testNamesDoNotRepeat() {
    const std::string folder = tempFolder();
    Rig rig(folder, 3);
    std::vector<std::string> seen;
    for (int i = 0; i < 40; ++i) {
        rig.project().playing = true;
        rig.app.drums().setStep(0, i % 16, 1.f);
        rig.project().playing = false;
        rig.session().saveNow();
        for (const std::string& name : seen) CHECK(name != rig.session().songName());
        seen.push_back(rig.session().songName());
        rig.session().newSong();
    }
    CHECK(rig.session().songNames().size() == 40);
}

} // namespace

int main(int, char**) {
    testTextRoundTrip();
    testBadFiles();
    testSavedAsYouGoAndBackAtTheNextStart();
    testAnOpenedSongSoundsLikeTheOneSaved();
    testNewSongAndTheList();
    testTheSongListOnScreen();
    testFreeNoteLengthsAreKept();
    testRevs();
    testASongMadeAMomentAgoIsInTheList();
    testRenamingAndDeletingSongs();
    testNamesDoNotRepeat();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("save tests passed\n");
    return 0;
}
