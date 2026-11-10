// Tests for chords: reading a pack, working out a chord's notes, and the Y
// button on the note screen. No window and no sound card.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app.hpp"
#include "chords.hpp"
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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, START = SDLK_RETURN;
const SDL_Keycode L1 = SDLK_q, R1 = SDLK_w;
const SDL_Keycode STICK_UP = SDLK_i, STICK_RIGHT = SDLK_l, STICK_LEFT = SDLK_j, STICK_DOWN = SDLK_k;

// A handheld with one Phazed track, on its note screen.
struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    Rig() {
        app.session().addTrack(TrackKind::Phazed);
        app.project().track = 1;
        app.session().view = View::Pattern;
        notes().moveCursorTo(60, 0);
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
    std::vector<float> render(int audioFrames) {
        std::vector<float> out(2 * audioFrames);
        engine->render(out.data(), audioFrames);
        return out;
    }
    float listen(int audioFrames = 4000) {
        float p = 0.f;
        for (float s : render(audioFrames)) p = std::fabs(s) > p ? std::fabs(s) : p;
        return p;
    }
    // Let anything that was played to be heard die away.
    void quiet() {
        frames(40);
        for (int i = 0; i < 40 && listen(48000) > 1e-5f; ++i) {}
    }
    NoteScreen& notes() { return app.notes(); }
    NotePattern& pat() { return app.project().tracks[1].notes[0]; }
    const ChordEvent* chordOn(int step) { const int i = pat().chordAt(step); return i < 0 ? nullptr : &pat().chords[i]; }
};

bool keysAre(const ChordEvent& c, std::vector<int> want) {
    int keys[kChordNotes];
    const int n = c.pitches(keys);
    return std::vector<int>(keys, keys + n) == want;
}

ChordEvent chord(int root, std::vector<int> intervals, int inversion = 0) {
    ChordEvent c;
    c.root = root;
    c.inversion = inversion;
    c.length = 1;
    c.velocity = 1.f;
    for (int v : intervals) c.intervals[c.count++] = v;
    return c;
}

const char* kPack = R"json({
  "schemaVersion": 2, "name": "Test \"Pack\"", "sourceTonic": 9,
  "scale": {"id": "major", "pitchClasses": [0, 2, 4, 5, 7, 9, 11]},
  "legacy": {"deep": [[1, 2], [3, {"x": null, "y": true, "z": false}]]},
  "chords": [
    {"legacyName": "Amaj7", "root": {"degree": 0, "alteration": 0}, "intervals": [0, 4, 7, 11]},
    {"legacyName": "F#m7", "root": {"alteration": 0, "degree": 5}, "intervals": [0, 3, 7, 10]},
    {"quality": "major(open)", "root": {"degree": 3, "alteration": 0}, "intervals": [0, 7, 16]},
    {"quality": "minor7", "root": {"degree": 6, "alteration": -1}, "intervals": [0, 3, 7, 10]},
    {"quality": "sus2", "root": {"degree": 1, "alteration": 2}, "intervals": [0, 2, 7]},
    {"legacyName": "bad interval", "root": {"degree": 0, "alteration": 0}, "intervals": [0, -4, 7]},
    {"legacyName": "bad degree", "root": {"degree": 7, "alteration": 0}, "intervals": [0, 4, 7]},
    {"legacyName": "bad alteration", "root": {"degree": 0, "alteration": 3}, "intervals": [0, 4, 7]},
    {"legacyName": "too many", "root": {"degree": 0, "alteration": 0}, "intervals": [0, 1, 2, 3, 4, 5, 6, 7, 8]},
    {"legacyName": "half", "root": {"degree": 0, "alteration": 0}, "intervals": [0, 3.5, 7]},
    {"legacyName": "none", "root": {"degree": 0, "alteration": 0}, "intervals": []},
    {"legacyName": "A very long chord name indeed", "root": {"degree": 0, "alteration": 0}, "intervals": [0, 1, 2, 3, 4, 5, 6, 7]}
  ]
})json";

void testReadingAPack() {
    ChordPack pack;
    CHECK(parseChordPack(kPack, pack));
    CHECK(pack.name == "Test \"Pack\"");
    CHECK(pack.chords.size() == 6); // the six that make sense
    if (pack.chords.size() != 6) return;
    // A major: the root is the key plus the scale's note plus the alteration.
    CHECK(std::strcmp(pack.chords[0].name, "Amaj7") == 0 && pack.chords[0].rootKey == 9 && pack.chords[0].count == 4);
    CHECK(pack.chords[0].intervals[1] == 4 && pack.chords[0].intervals[3] == 11);
    CHECK(std::strcmp(pack.chords[1].name, "F#m7") == 0 && pack.chords[1].rootKey == 6);
    // No name given: the root's name and the quality, "major" left unsaid.
    CHECK(std::strcmp(pack.chords[2].name, "D(open)") == 0 && pack.chords[2].rootKey == 2);
    CHECK(std::strcmp(pack.chords[3].name, "Gm7") == 0 && pack.chords[3].rootKey == 7); // G#, a semitone down
    CHECK(std::strcmp(pack.chords[4].name, "C#sus2") == 0 && pack.chords[4].rootKey == 1); // B, two up, comes round
    CHECK(std::strlen(pack.chords[5].name) == 15 && pack.chords[5].count == 8);
    CHECK(std::strchr(pack.chords[5].name, ' ') == nullptr); // one word, for the song file

    // Things that are not packs leave the pack alone.
    ChordPack same = pack;
    const char* bad[] = {"", "nonsense", "[1, 2]", "{\"chords\": 3}", "{\"chords\": []}", "{\"name\": \"x\"}",
                         "{\"chords\": [{\"intervals\": [0, 4, 7]}]} trailing", "{\"chords\": [{\"intervals\": [0, 4, 7]}]",
                         "{\"chords\": [{\"intervals\": [0, 4, 7,]}]}", "{\"chords\" [{\"intervals\": [0]}]}",
                         "{\"chords\": [{\"intervals\": [0, 4, 7]}], \"name\": \"unfinished}"};
    for (const char* text : bad) {
        CHECK(!parseChordPack(text, same));
        CHECK(same.name == pack.name && same.chords.size() == 6);
    }
    // Too deep to be real: refused, not a crash.
    CHECK(!parseChordPack(std::string(100000, '['), same));
    CHECK(!parseChordPack("{\"chords\": [{\"intervals\": [0]}], \"x\": " + std::string(40, '[') + std::string(40, ']') + "}", same));
    CHECK(parseChordPack("{\"chords\": [{\"intervals\": [0]}], \"x\": " + std::string(20, '[') + std::string(20, ']') + "}", same));

    // The least a pack can be: C major assumed, and a name made up.
    ChordPack least;
    CHECK(parseChordPack("{\"chords\":[{\"intervals\":[0,4,7],\"root\":{\"degree\":4}}]}", least));
    CHECK(least.name == "pack" && least.chords.size() == 1 && least.chords[0].rootKey == 7);
    CHECK(std::strcmp(least.chords[0].name, "G") == 0);
}

void testThePacksThatShip() {
    const std::vector<ChordPack> packs = loadChordPacks(APOTHECA_SOURCE_DIR "/assets/chords");
    CHECK(packs.size() == 7); // the built-in one and six files
    CHECK(packs[0].name == "Plain C" && packs[0].chords.size() == 13);
    for (std::size_t i = 0; i < packs.size(); ++i) {
        CHECK(!packs[i].name.empty() && !packs[i].chords.empty());
        if (i >= 2) CHECK(packs[i - 1].name < packs[i].name); // by name after the built-in one
        for (const ChordShape& c : packs[i].chords) CHECK(c.name[0] && c.count >= 1 && c.rootKey >= 0 && c.rootKey < 12);
    }
    // A folder that is not there still leaves the built-in pack.
    CHECK(loadChordPacks("/no/such/folder").size() == 1);
    CHECK(loadChordPacks("").size() == 1);
}

void testAChordsNotes() {
    CHECK(keysAre(chord(60, {0, 4, 7}), {60, 64, 67}));
    CHECK(keysAre(chord(60, {7, 0, 4}), {60, 64, 67})); // lowest first, however it was written
    // Inversions: the bottom goes up an octave, or the top comes down.
    CHECK(keysAre(chord(60, {0, 4, 7}, 1), {64, 67, 72}));
    CHECK(keysAre(chord(60, {0, 4, 7}, 2), {67, 72, 76}));
    CHECK(keysAre(chord(60, {0, 4, 7}, 3), {72, 76, 79}));
    CHECK(keysAre(chord(60, {0, 4, 7}, -1), {55, 60, 64}));
    CHECK(keysAre(chord(60, {0, 4, 7}, -2), {52, 55, 60}));
    // No key twice, and nothing off the ends of the keyboard.
    CHECK(keysAre(chord(60, {0, 12, 7}, 1), {67, 72}));
    CHECK(keysAre(chord(120, {0, 4, 7, 10}), {120, 124, 127}));
    CHECK(keysAre(chord(2, {0, 4, 7}, -1), {2, 6}));
    CHECK(keysAre(chord(60, {}), {}));

    // A chord from a pack lands on the nearest key of its name.
    ChordShape g;
    g.rootKey = 7;
    g.count = 3;
    g.intervals[1] = 4;
    g.intervals[2] = 7;
    std::snprintf(g.name, sizeof g.name, "G");
    CHECK(makeChord(g, 60, 3, 4, 0.5f, 0).root == 55); // G below middle C is nearer than G above
    CHECK(makeChord(g, 61, 0, 4, 0.5f, 0).root == 55); // six away: the lower one
    CHECK(makeChord(g, 62, 0, 4, 0.5f, 0).root == 67);
    CHECK(makeChord(g, 67, 0, 4, 0.5f, 0).root == 67);
    CHECK(makeChord(g, 0, 0, 4, 0.5f, 0).root == 7);
    CHECK(makeChord(g, 127, 0, 4, 0.5f, 0).root == 127);
    g.rootKey = 0;
    CHECK(makeChord(g, 127, 0, 4, 0.5f, 0).root == 120); // the C above is off the keyboard
    g.rootKey = 7;
    const ChordEvent c = makeChord(g, 60, 3, 4, 0.5f, 9);
    CHECK(c.step == 3 && c.length == 4 && c.velocity == 0.5f && c.count == 3 && c.inversion == kMaxInversion);
    CHECK(makeChord(g, 60, 3, 4, 0.5f, -9).inversion == -kMaxInversion);
    CHECK(std::strcmp(c.name, "G") == 0 && c.intervals[2] == 7);
}

void testChordsAndLooseNotesShareTheSlots() {
    NotePattern p;
    CHECK(p.empty() && p.used() == 0);
    p.notes[0].velocity = 1.f; p.notes[0].pitch = 40; p.notes[0].length = 2;
    p.notes[2].velocity = 1.f; p.notes[2].pitch = 41; p.notes[2].length = 2;
    p.chords[3] = chord(60, {0, 4, 7});
    p.chords[3].step = 4;
    p.chords[3].length = 6;
    p.chords[3].velocity = 0.6f;
    CHECK(!p.empty() && p.used() == 5);
    CHECK(p.chordAt(3) == -1 && p.chordAt(4) == 3 && p.chordAt(9) == 3 && p.chordAt(10) == -1);
    CHECK(p.freeChord() == 0);
    NoteEvent out[kMaxNotes];
    CHECK(p.render(out) == 5);
    CHECK(out[0].pitch == 40 && out[2].pitch == 41); // loose notes stay where they are
    CHECK(out[1].pitch == 60 && out[3].pitch == 64 && out[4].pitch == 67); // the chord takes the gaps
    CHECK(out[1].step == 4 && out[1].length == 6 && out[1].velocity == 0.6f);
    CHECK(out[5].velocity == 0.f);
    // Only a chord: not empty.
    NotePattern only;
    only.chords[0] = chord(60, {0, 4, 7});
    CHECK(!only.empty() && only.used() == 3);
    // Full: the chord's notes that do not fit are dropped, and nothing is overwritten.
    for (int i = 0; i < kMaxNotes - 1; ++i) { only.notes[i].velocity = 1.f; only.notes[i].pitch = 30; only.notes[i].length = 1; }
    CHECK(only.render(out) == kMaxNotes);
    CHECK(out[kMaxNotes - 1].pitch == 60 && out[kMaxNotes - 2].pitch == 30);
}

void testATapOfYPlacesTheChordInHand() {
    Rig rig;
    rig.key(Y, true);
    CHECK(rig.notes().picking() && rig.notes().chordCount() == 0); // nothing lands until Y is let go
    CHECK(rig.listen() > 0.01f); // but it is heard
    rig.key(Y, false);
    CHECK(!rig.notes().picking() && rig.notes().chordCount() == 1);
    const ChordEvent* c = rig.chordOn(0);
    CHECK(c && std::strcmp(c->name, "C") == 0 && c->root == 60 && c->step == 0 && c->length == 4 && c->velocity == 0.8f);
    CHECK(c && keysAre(*c, {60, 64, 67}));
    CHECK(rig.notes().cursorStep() == 4 && rig.notes().cursorPitch() == 60); // on to where the next one goes
    CHECK(rig.notes().noteCount() == 0 && rig.pat().used() == 3);
    // Again, and a different one: hold, right twice, let go.
    rig.tap(Y);
    CHECK(rig.chordOn(4) && std::strcmp(rig.chordOn(4)->name, "C") == 0);
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT, 2);
    CHECK(rig.notes().handIndex() == 2 && rig.notes().chordCount() == 2);
    rig.key(Y, false);
    CHECK(rig.chordOn(8) && std::strcmp(rig.chordOn(8)->name, "Em") == 0 && rig.chordOn(8)->root == 64);
    // The last four steps; then the cursor comes round to the start.
    rig.tap(Y);
    CHECK(rig.notes().chordCount() == 4 && rig.notes().cursorStep() == 0);
    CHECK(rig.chordOn(15) && rig.chordOn(15)->step == 12);

    // The engine plays them: the bar is not silent, and stops being heard when emptied.
    rig.quiet();
    rig.tap(START);
    CHECK(rig.listen(24000) > 0.01f);
}

void testPickingWithTheDPadAndTheStick() {
    Rig rig;
    rig.key(Y, true);
    rig.tap(SDLK_LEFT);
    CHECK(rig.notes().handIndex() == 12); // round to the last
    rig.tap(SDLK_RIGHT);
    CHECK(rig.notes().handIndex() == 0);
    // Inversions stop at three each way.
    rig.tap(SDLK_UP, 5);
    CHECK(rig.notes().handInversion() == 3);
    rig.tap(SDLK_DOWN, 9);
    CHECK(rig.notes().handInversion() == -3);
    rig.tap(SDLK_UP, 4);
    CHECK(rig.notes().handInversion() == 1);
    // The stick does the same: one at once, then about seven a second.
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(rig.notes().handIndex() == 1);
    rig.frames(31); // half a second
    CHECK(rig.notes().handIndex() >= 4 && rig.notes().handIndex() <= 5);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    const int hand = rig.notes().handIndex();
    rig.key(STICK_UP, true);
    rig.frames(1);
    CHECK(rig.notes().handInversion() == 2 && rig.notes().handIndex() == hand);
    rig.key(STICK_UP, false);
    rig.frames(1);
    // A held D-pad repeats, but slowly enough to hear each chord.
    rig.key(SDLK_RIGHT, true);
    rig.frames(60); // about a second
    rig.key(SDLK_RIGHT, false);
    CHECK(rig.notes().handIndex() - hand >= 5 && rig.notes().handIndex() - hand <= 8);
    CHECK(rig.notes().cursorStep() == 0); // the cursor has not moved through any of it
    // What lands is what was chosen.
    const int chosen = rig.notes().handIndex();
    rig.key(Y, false);
    const ChordEvent* c = rig.chordOn(0);
    CHECK(c && c->inversion == 2 && std::strcmp(c->name, builtInPack().chords[static_cast<std::size_t>(chosen)].name) == 0);
    // X hears the chord in hand again; A and R1 are not listening.
    rig.quiet();
    rig.notes().moveCursorTo(60, 8);
    rig.key(Y, true);
    rig.quiet();
    rig.tap(A);
    CHECK(rig.notes().noteCount() == 0);
    CHECK(rig.listen() < 1e-4f);
    rig.tap(X);
    CHECK(rig.listen() > 0.01f);
    rig.key(Y, false);
}

void testBCallsItOff() {
    Rig rig;
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT);
    rig.tap(B);
    CHECK(!rig.notes().picking());
    rig.key(Y, false);
    CHECK(rig.notes().chordCount() == 0 && rig.notes().cursorStep() == 0 && rig.pat().empty());
    // The chord chosen is still in hand for next time.
    rig.tap(Y);
    CHECK(rig.chordOn(0) && std::strcmp(rig.chordOn(0)->name, "Dm") == 0);
}

void testSwappingAChordInPlace() {
    Rig rig;
    rig.tap(Y);                         // C on steps 1-4
    rig.notes().moveCursorTo(72, 2);    // anywhere in its steps will do
    rig.key(A, true);                   // a loose note above it, to show it is left alone
    rig.key(A, false);
    rig.notes().moveCursorTo(50, 2);
    rig.key(Y, true);
    CHECK(rig.notes().handIndex() == 0); // starts from the chord that is there
    rig.tap(SDLK_RIGHT, 3);
    rig.tap(SDLK_UP);
    const ChordEvent* c = rig.chordOn(0);
    CHECK(c && std::strcmp(c->name, "F") == 0 && c->step == 0 && c->length == 4 && c->velocity == 0.8f && c->inversion == 1);
    CHECK(c && c->root == 65); // the F nearest the old root, not nearest the cursor
    rig.key(Y, false);
    CHECK(rig.notes().chordCount() == 1 && rig.notes().noteCount() == 1); // nothing new landed
    CHECK(rig.notes().cursorStep() == 2 && rig.notes().cursorPitch() == 50);
    // Holding Y on it again starts from its chord and inversion, whatever
    // was in hand in between.
    rig.notes().moveCursorTo(50, 9);
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT, 4);
    rig.tap(SDLK_DOWN, 3);
    rig.tap(B);
    rig.key(Y, false);
    CHECK(rig.notes().handIndex() == 7 && rig.notes().handInversion() == -2 && rig.notes().chordCount() == 1);
    rig.notes().moveCursorTo(50, 2);
    rig.key(Y, true);
    CHECK(rig.notes().handIndex() == 3 && rig.notes().handInversion() == 1);
    rig.key(Y, false);
    CHECK(rig.chordOn(0) && std::strcmp(rig.chordOn(0)->name, "F") == 0 && rig.notes().chordCount() == 1);
}

void testBreakingAChordIntoNotes() {
    // Two handhelds with the same chord; one breaks it up. They must sound the same.
    Rig whole, broken;
    for (Rig* rig : {&whole, &broken}) {
        rig->key(Y, true);
        rig->tap(SDLK_RIGHT, 7); // Cmaj7
        rig->key(Y, false);
        rig->notes().moveCursorTo(60, 1);
    }
    broken.key(Y, true);
    broken.tap(B);
    broken.key(Y, false);
    CHECK(broken.notes().chordCount() == 0 && broken.notes().noteCount() == 4 && broken.pat().used() == 4);
    CHECK(whole.notes().chordCount() == 1 && whole.notes().noteCount() == 0 && whole.pat().used() == 4);
    const int want[4] = {60, 64, 67, 71};
    for (int i = 0; i < 4; ++i) {
        const int slot = broken.pat().at(want[i], 3);
        CHECK(slot >= 0 && broken.pat().notes[slot].step == 0 && broken.pat().notes[slot].length == 4 && broken.pat().notes[slot].velocity == 0.8f);
    }
    whole.quiet();
    broken.quiet();
    whole.tap(START);
    broken.tap(START);
    const std::vector<float> a = whole.render(48000), b = broken.render(48000);
    float peak = 0.f;
    for (float s : a) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    CHECK(peak > 0.01f);
    CHECK(a == b);
}

void testShapingAndRemovingAWholeChord() {
    Rig rig;
    rig.tap(Y); // C on steps 1-4, cursor now on step 5
    rig.tap(Y); // and another on 5-8
    rig.notes().moveCursorTo(64, 1); // on the first chord's E
    CHECK(rig.notes().chordHere() && rig.notes().chordHere()->step == 0);
    rig.key(A, true);
    CHECK(rig.notes().noteCount() == 0); // A takes hold of the chord; it does not drop a note on it
    rig.tap(SDLK_RIGHT);
    CHECK(std::fabs(rig.chordOn(0)->velocity - 0.9f) < 1e-4f);
    CHECK(rig.chordOn(4)->velocity == 0.8f); // only this chord
    rig.tap(SDLK_UP, 5);
    CHECK(rig.chordOn(0)->length == 2); // never out from under the cursor
    rig.tap(SDLK_DOWN, 9);
    CHECK(rig.chordOn(0)->length == 4); // up to the next chord, no further
    rig.tap(SDLK_UP);
    CHECK(rig.chordOn(0)->length == 3);
    // The stick, the same.
    rig.key(STICK_RIGHT, true);
    rig.frames(40);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(rig.chordOn(0)->velocity == 1.f);
    rig.key(A, false);
    CHECK(rig.notes().noteCount() == 0);
    // The stick with A held slides the chord's end freely; the D-pad snaps it.
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(4);
    rig.key(STICK_UP, false);
    rig.frames(1);
    const float freeLength = rig.chordOn(0)->length;
    CHECK(freeLength > 2.f && freeLength < 3.f && freeLength != std::floor(freeLength));
    {
        // Every note of the chord goes to the engine that long.
        NoteEvent out[kMaxNotes];
        rig.pat().render(out);
        int same = 0;
        for (const NoteEvent& n : out) same += n.velocity > 0.f && n.step == 0 && n.length == freeLength;
        CHECK(same == 3);
    }
    // A chord that ends part-way through a step: the next one goes on the step after.
    rig.key(A, false);
    rig.notes().moveCursorTo(60, 9);
    rig.tap(Y);
    CHECK(rig.chordOn(9) && rig.chordOn(9)->length == freeLength && rig.notes().cursorStep() == 12);
    rig.notes().moveCursorTo(60, 9);
    rig.tap(B);
    rig.notes().moveCursorTo(64, 1);
    rig.key(A, true);
    rig.tap(SDLK_DOWN);
    CHECK(rig.chordOn(0)->length == 3.f);
    rig.key(A, false);
    // The next chord placed is as long as that one was left.
    rig.notes().moveCursorTo(60, 9);
    rig.tap(Y);
    CHECK(rig.chordOn(9) && rig.chordOn(9)->length == 3 && rig.notes().cursorStep() == 12);

    // In a chord's steps but not on one of its notes, A and B are about loose notes.
    rig.notes().moveCursorTo(62, 1);
    CHECK(!rig.notes().chordHere());
    rig.tap(B);
    CHECK(rig.notes().chordCount() == 3);
    rig.tap(A);
    CHECK(rig.notes().noteCount() == 1 && rig.notes().chordCount() == 3);
    // A loose note on top of a chord's note is what the cursor has.
    rig.notes().moveCursorTo(64, 1);
    rig.pat().notes[5].velocity = 0.5f; rig.pat().notes[5].pitch = 64; rig.pat().notes[5].step = 1; rig.pat().notes[5].length = 1;
    CHECK(!rig.notes().chordHere() && rig.notes().noteHere());
    rig.tap(B);
    CHECK(rig.notes().noteCount() == 1 && rig.notes().chordCount() == 3 && rig.notes().chordHere());
    // B on the chord's note removes the whole chord.
    rig.tap(B);
    CHECK(rig.notes().chordCount() == 2 && rig.chordOn(0) == nullptr && rig.chordOn(4) != nullptr);
    // And the engine was told: with every chord gone and the loose note too, the bar is silent.
    rig.notes().moveCursorTo(62, 1);
    rig.tap(B);
    rig.notes().moveCursorTo(60, 4);
    rig.tap(B);
    rig.notes().moveCursorTo(60, 9);
    rig.tap(B); // a chord goes last: removing one must reach the engine by itself
    CHECK(rig.pat().empty());
    rig.quiet();
    rig.tap(START);
    CHECK(rig.listen(96000) < 1e-4f);
}

void testWhereAChordCannotGo() {
    Rig rig;
    // Near the end of the bar it is cut short.
    rig.notes().moveCursorTo(60, 14);
    rig.tap(Y);
    CHECK(rig.chordOn(14) && rig.chordOn(14)->length == 2 && rig.notes().cursorStep() == 0);
    // In front of another chord it stops where that one starts.
    rig.notes().moveCursorTo(60, 12);
    rig.tap(Y);
    CHECK(rig.chordOn(12) && rig.chordOn(12)->length == 2 && rig.chordOn(14)->step == 14 && rig.notes().cursorStep() == 14);
    CHECK(rig.notes().chordCount() == 2);

    // Sixty-four notes is the bar's limit, chords' notes included.
    for (int i = 0; i < kMaxNotes - 6 - 2; ++i) {
        NoteEvent& n = rig.pat().notes[i];
        n.velocity = 1.f; n.pitch = 20; n.step = 0; n.length = 1;
    }
    CHECK(rig.pat().used() == kMaxNotes - 2);
    rig.notes().moveCursorTo(60, 0);
    rig.tap(Y);
    CHECK(rig.notes().chordCount() == 2 && rig.notes().cursorStep() == 0); // three notes do not fit in two
    // Nor can a swap grow a chord past it.
    rig.pat().notes[60].velocity = 1.f; rig.pat().notes[60].pitch = 21; rig.pat().notes[60].length = 1;
    rig.pat().notes[61].velocity = 1.f; rig.pat().notes[61].pitch = 22; rig.pat().notes[61].length = 1;
    CHECK(rig.pat().used() == kMaxNotes);
    rig.notes().moveCursorTo(60, 12);
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT, 7); // Cmaj7 has four notes
    CHECK(std::strcmp(rig.chordOn(12)->name, "Cmaj7") != 0 && rig.pat().used() == kMaxNotes);
    rig.key(Y, false);
    // And a loose note has nowhere to go either.
    rig.notes().moveCursorTo(90, 5);
    rig.tap(A);
    CHECK(rig.pat().used() == kMaxNotes && rig.notes().noteHere() == nullptr);
    // Y is not listening while A has hold of something.
    rig.notes().moveCursorTo(20, 0);
    rig.key(A, true);
    rig.key(Y, true);
    CHECK(!rig.notes().picking());
    rig.key(Y, false);
    rig.key(A, false);
    CHECK(rig.notes().chordCount() == 2);
}

void testPacksAndTheSongFile() {
    Rig rig;
    rig.app.session().useChordFolder(APOTHECA_SOURCE_DIR "/assets/chords");
    const std::vector<ChordPack>& packs = rig.app.session().packs;
    CHECK(packs.size() == 7 && rig.app.session().packIndex() == 0);
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT, 2);
    rig.tap(R1);
    CHECK(rig.app.session().packIndex() == 1 && packs[1].name == rig.app.project().chordPack);
    CHECK(rig.notes().handIndex() == 0); // a new pack starts at its first chord
    rig.tap(L1, 2);
    CHECK(rig.app.session().packIndex() == 6); // round the end
    rig.tap(R1, 2);
    CHECK(rig.app.session().packIndex() == 1);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_DOWN);
    rig.key(Y, false);
    const ChordEvent* c = rig.chordOn(0);
    CHECK(c && std::strcmp(c->name, packs[1].chords[1].name) == 0 && c->inversion == -1);
    rig.tap(Y);
    rig.notes().moveCursorTo(40, 9);
    rig.tap(A);

    // The song file keeps the pack and the chords, as chords.
    const std::string text = songText(rig.app.project());
    CHECK(text.find("pack " + packs[1].name + "\n") != std::string::npos);
    CHECK(text.find("\nchord 1 4 0.8 -1 ") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    CHECK(songText(*back) == text);
    const NotePattern& p = back->tracks[1].notes[0];
    const int slot = p.chordAt(0);
    CHECK(slot >= 0 && c && p.chords[slot].root == c->root && p.chords[slot].count == c->count && p.chords[slot].length == 4);
    CHECK(slot >= 0 && std::strcmp(p.chords[slot].name, c->name) == 0 && p.chords[slot].intervals[c->count - 1] == c->intervals[c->count - 1]);
    CHECK(p.chordAt(4) >= 0 && p.used() == rig.pat().used());
    CHECK(std::strcmp(back->chordPack, rig.app.project().chordPack) == 0);
    // A song whose pack is not on this device falls back to the built-in one, and keeps its chords.
    Rig other;
    other.app.project() = *back;
    CHECK(other.app.session().packIndex() == 0);

    // Chord lines that make no sense are left out; the song still opens.
    const std::string head = "apotheca-song 1\ntrack phazed\nscene 1\n";
    const auto chordsIn = [&](const std::string& lines) {
        std::unique_ptr<Project> song(new Project);
        if (!parseSong(head + lines, *song)) return -1;
        int count = 0;
        for (const ChordEvent& e : song->tracks[0].notes[0].chords) count += e.velocity > 0.f ? 1 : 0;
        return count;
    };
    CHECK(chordsIn("chord 1 4 0.8 0 60 C 0 4 7\n") == 1);
    CHECK(chordsIn("chord 1 4 0.8 0 60 C 0 4 7\nchord 5 4 0.8 0 60 C 0 4 7\n") == 2);
    CHECK(chordsIn("chord 1 4 0.8 0 60 C 0 4 7\nchord 4 4 0.8 0 60 C 0 4 7\n") == 1); // two at once
    CHECK(chordsIn("chord 1 3.5 0.8 0 60 C 0 4 7\nchord 4 4 0.8 0 60 C 0 4 7\n") == 1); // the first runs into step 4
    CHECK(chordsIn("chord 1 2.5 0.8 0 60 C 0 4 7\nchord 4 4 0.8 0 60 C 0 4 7\n") == 2); // ...and this one does not
    CHECK(chordsIn("chord 5 4 0.8 0 60 C 0 4 7\nchord 1 4.5 0.8 0 60 C 0 4 7\n") == 1);
    CHECK(chordsIn("chord 1 4 0.8 0 60 C\n") == 0);            // no notes
    CHECK(chordsIn("chord 0 4 0.8 0 60 C 0 4 7\n") == 0);      // no such step
    CHECK(chordsIn("chord 17 4 0.8 0 60 C 0 4 7\n") == 0);
    CHECK(chordsIn("chord 1 4 0 0 60 C 0 4 7\n") == 0);        // silent
    CHECK(chordsIn("chord 1 4 0.8 0 128 C 0 4 7\n") == 0);     // no such key
    CHECK(chordsIn("chord 1 4 0.8 0\n") == 0);
    {
        std::unique_ptr<Project> song(new Project);
        CHECK(parseSong(head + "chord 15 9 7 -8 60 ? 0 4 99 7\n", *song));
        const ChordEvent& e = song->tracks[0].notes[0].chords[0];
        CHECK(e.length == 2 && e.velocity == 1.f && e.inversion == -kMaxInversion && e.count == 3 && e.name[0] == 0);
        // Sixty-four notes, however the file is written.
        std::string many;
        for (int i = 0; i < 62; ++i) many += "note 1 30 1 1\n";
        CHECK(parseSong(head + many + "chord 1 4 0.8 0 60 C 0 4 7\n", *song));
        CHECK(song->tracks[0].notes[0].used() == 62);
        CHECK(parseSong(head + "chord 1 4 0.8 0 60 C 0 4 7\n" + many, *song));
        CHECK(song->tracks[0].notes[0].used() == kMaxNotes);
    }
}

void testAnOpenedSongPlaysItsChords() {
    // applyProject is how an opened song reaches the engine. Two fresh
    // handhelds open the same bar: one with the chord, one with the chord
    // written out by hand as loose notes. They must sound the same.
    Rig made, chordSong, noteSong;
    made.tap(Y);
    chordSong.app.project() = made.app.project();
    noteSong.app.project() = made.app.project();
    NotePattern& p = noteSong.pat();
    p.render(p.notes);
    p.chords[p.chordAt(0)] = ChordEvent();
    CHECK(p.used() == 3 && chordSong.pat().used() == 3 && chordSong.notes().chordCount() == 1);
    chordSong.app.session().applyProject();
    noteSong.app.session().applyProject();
    chordSong.tap(START);
    noteSong.tap(START);
    const std::vector<float> a = chordSong.render(48000), b = noteSong.render(48000);
    float peak = 0.f;
    for (float s : a) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    CHECK(peak > 0.01f);
    CHECK(a == b);
}

void testNothingIsHeardWhileTheSongPlays() {
    Rig rig;
    rig.tap(START); // an empty bar, playing
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT);
    rig.tap(X);
    CHECK(rig.listen(2000) < 1e-4f); // choosing is silent; the song will play the chord
    rig.tap(B);
    rig.key(Y, false);
}

} // namespace

int main(int, char**) {
    testReadingAPack();
    testThePacksThatShip();
    testAChordsNotes();
    testChordsAndLooseNotesShareTheSlots();
    testATapOfYPlacesTheChordInHand();
    testPickingWithTheDPadAndTheStick();
    testBCallsItOff();
    testSwappingAChordInPlace();
    testBreakingAChordIntoNotes();
    testShapingAndRemovingAWholeChord();
    testWhereAChordCannotGo();
    testPacksAndTheSongFile();
    testAnOpenedSongPlaysItsChords();
    testNothingIsHeardWhileTheSongPlays();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("chord tests passed\n");
    return 0;
}
