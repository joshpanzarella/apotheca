// GLIDE: a note that slides to its pitch from the note before it, for a chosen length.
// What the engine tells a synth (which key from, for how long), that each synth takes that
// long, and that the rule the piano roll draws from (glide_rule.hpp) says what the engine does.
// No framework: each CHECK prints its line on failure.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static const int kStep = 6000; // frames in one step at 120 BPM and 48 kHz

// A synth that makes no sound and writes down what it is told.
struct Began {
    long frame;
    int pitch, from; // from -1: a plain note
    float seconds;
};
struct Spy : Instrument {
    std::vector<Began>* log;
    long frame = 0;
    explicit Spy(std::vector<Began>* to) : log(to) {}
    void prepare(float) override {}
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int, float) override {}
    void noteOn(int note, float) override { log->push_back({frame, note, -1, 0.f}); }
    void noteOnFrom(int note, float, int from, float seconds) override { log->push_back({frame, note, from, seconds}); }
    void noteOff(int) override {}
    void allNotesOff() override {}
    void render(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
        frame += frames;
    }
};

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
// What a note's ratchet, glide and THROW are in a command (as Session::sendNoteBar packs them).
static int packed(int ratchet, int glide, int throwTo = 0) { return (ratchet >= 2 ? ratchet : 0) + (glide ? 256 : 0) + (throwTo << 9) + (glide >= 2 ? glide << 11 : 0); }
static Command noteAt(int slot, int step, int pitch, float length, int effects = 0, float nudge = 0.f, int bar = 0, int track = 1) {
    Command c = make(Command::SetNote, track, slot, step, 0.9f);
    c.c = pitch;
    c.d = static_cast<int>(length);
    c.extra = length - static_cast<float>(c.d);
    c.e = effects;
    c.shift = nudge;
    c.bar = bar;
    return c;
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    std::vector<Began> log;
    Rig() {
        log.reserve(100000);
        Command c = make(Command::InstallSynth, 1);
        c.pointer = new Spy(&log);
        CHECK(engine->send(c));
    }
    void play(int steps) {
        engine->send(make(Command::Play, 0, 0, 1));
        run(steps);
    }
    void run(int steps, int frames = kStep) {
        std::vector<float> out(2 * 1000);
        for (long left = static_cast<long>(steps) * frames; left > 0; left -= 1000) engine->render(out.data(), left < 1000 ? static_cast<int>(left) : 1000);
    }
    // The note that began on `pitch` nearest that many steps after Play (and within two frames of it).
    const Began* at(double steps, int pitch, int frames = kStep) const {
        for (const Began& b : log)
            if (b.pitch == pitch && std::labs(b.frame - std::lround(steps * frames)) <= 2) return &b;
        return nullptr;
    }
    int count(int pitch) const {
        int n = 0;
        for (const Began& b : log) n += b.pitch == pitch;
        return n;
    }
};
static bool about(double a, double b) { return std::fabs(a - b) <= 1e-6 * (std::fabs(b) > 1.0 ? std::fabs(b) : 1.0); }
static const double kStepSeconds = 0.125; // at 120

// The numbers and the order a player sees them in.
static void testTheLengths() {
    CHECK(kGlideOff == 0 && kGlideHalf == 1); // 1 was "on" in every song saved before a glide had a length
    CHECK(kGlideQuarter == 2 && kGlideOne == 3 && kGlideTwo == 4 && kGlideFull == 5 && kGlideKinds == 6);
    const int order[] = {kGlideOff, kGlideQuarter, kGlideHalf, kGlideOne, kGlideTwo, kGlideFull};
    for (int place = 0; place < kGlideKinds; ++place) CHECK(glideAt(place) == order[place] && glidePlace(order[place]) == place);
    CHECK(glideAt(-1) == kGlideOff && glideAt(99) == kGlideFull && glidePlace(77) == 0);
    CHECK(glideKind(0) == kGlideOff && glideKind(-3) == kGlideOff && glideKind(5) == kGlideFull && glideKind(6) == kGlideHalf && glideKind(200) == kGlideHalf);
    CHECK(glideSteps(kGlideQuarter, 9, 0) == 0.25 && glideSteps(kGlideHalf, 9, 0) == 0.5 && glideSteps(kGlideOne, 9, 0) == 1.0 && glideSteps(kGlideTwo, 9, 0) == 2.0 && glideSteps(kGlideFull, 9, 0) == 9.0);
    CHECK(glideSteps(kGlideHalf, 9, 1) == 0.5 && glideSteps(kGlideOne, 9, 1) == 1.0); // a ratchet of one is no ratchet
    CHECK(glideSteps(kGlideHalf, 9, 2) == 0.85 / 2 && glideSteps(kGlideQuarter, 9, 2) == 0.25 && glideSteps(kGlideFull, 9, 4) == 0.85 / 4 && glideSteps(kGlideQuarter, 9, 4) == 0.85 / 4);
    for (int kind = 0; kind < kGlideKinds; ++kind) CHECK(glideName(kind) && glideName(kind)[0]);
    CHECK(std::string(glideName(kGlideHalf)) == "1/2 STEP" && std::string(glideName(kGlideFull)) == "FULL" && std::string(glideName(kGlideOff)) == "OFF" && std::string(glideName(kGlideTwo)) == "2 STEPS");
    CHECK(std::string(glideName(kGlideQuarter)) == "1/4 STEP" && std::string(glideName(kGlideOne)) == "1 STEP");
}

// Each length reaches the synth as that many steps, in seconds, at the tempo then.
static void testEachLengthIsTold() {
    const double want[kGlideKinds] = {0, 0.5, 0.25, 1, 2, 4};
    for (int tempo : {120, 60}) {
        for (int kind = 0; kind < kGlideKinds; ++kind) {
            Rig rig;
            rig.engine->send(make(Command::SetTempo, 0, 0, 0, static_cast<float>(tempo)));
            rig.engine->send(noteAt(0, 0, 57, 1.f));
            rig.engine->send(noteAt(1, 4, 69, 2.f, packed(0, kind)));
            const int frames = kStep * 120 / tempo;
            rig.engine->send(make(Command::Play, 0, 0, 1));
            rig.run(8, frames);
            const Began* b = rig.at(4, 69, frames);
            CHECK(b != nullptr);
            if (!b) continue;
            if (kind == kGlideOff) CHECK(b->from == -1);
            else CHECK(b->from == 57 && about(b->seconds, want[kind] * kStepSeconds * 120 / tempo)); // (FULL: the note before began four steps ago)
        }
    }
    // An old song's "on" (256 and nothing more) is half a step. A length nobody knows is too.
    for (int e : {256, 256 + (6 << 11), 256 + (7 << 11)}) {
        Rig rig;
        rig.engine->send(noteAt(0, 0, 57, 1.f));
        rig.engine->send(noteAt(1, 4, 69, 2.f, e));
        rig.play(8);
        const Began* b = rig.at(4, 69);
        CHECK(b && b->from == 57 && about(b->seconds, 0.5 * kStepSeconds));
    }
    // A length with no 256: no glide. (Nothing sends that; the 256 is the switch.)
    {
        Rig rig;
        rig.engine->send(noteAt(0, 0, 57, 1.f));
        rig.engine->send(noteAt(1, 4, 69, 2.f, kGlideTwo << 11));
        rig.play(8);
        CHECK(rig.at(4, 69) && rig.at(4, 69)->from == -1);
    }
    // The ratchet and THROW beside it in the same number are as they were.
    {
        Rig rig;
        rig.engine->send(noteAt(0, 0, 57, 1.f));
        rig.engine->send(noteAt(1, 4, 69, 2.f, packed(3, kGlideTwo, 3)));
        rig.engine->send(noteAt(2, 8, 72, 1.f, packed(4, kGlideOff, 1)));
        rig.play(12);
        CHECK(rig.count(69) == 3 && rig.count(72) == 4);
        const Began* b = rig.at(4, 69);
        CHECK(b && b->from == 57 && about(b->seconds, 0.85 / 3 * kStepSeconds));
    }
    // A whole track handed over at once (a song opening) carries the lengths too.
    {
        Rig rig;
        std::unique_ptr<TrackImage> image(new TrackImage);
        image->notes[0][0].step = 0, image->notes[0][0].pitch = 57, image->notes[0][0].length = 1.f, image->notes[0][0].velocity = 0.9f;
        image->notes[0][1].step = 4, image->notes[0][1].pitch = 69, image->notes[0][1].length = 1.f, image->notes[0][1].velocity = 0.9f, image->notes[0][1].glide = kGlideTwo;
        image->notes[0][2].step = 8, image->notes[0][2].pitch = 60, image->notes[0][2].length = 1.f, image->notes[0][2].velocity = 0.9f, image->notes[0][2].glide = 9; // not known: on
        Command c = make(Command::SetTrackImage, 1);
        c.pointer = image.get();
        if (rig.engine->send(c)) image.release();
        rig.play(12);
        CHECK(rig.at(4, 69) && rig.at(4, 69)->from == 57 && about(rig.at(4, 69)->seconds, 2 * kStepSeconds));
        CHECK(rig.at(8, 60) && rig.at(8, 60)->from == 69 && about(rig.at(8, 60)->seconds, 0.5 * kStepSeconds));
    }
}

// With a ratchet only the first piece slides, and it is there before the second begins,
// however long the glide was set: a long glide is cut to 85% of the first piece.
static void testAGlideOnARatchet() {
    struct Row { int ratchet, kind; double steps; };
    const Row rows[] = {{2, kGlideHalf, 0.425}, {3, kGlideHalf, 0.85 / 3}, {4, kGlideHalf, 0.2125}, // as before a glide had a length
                        {2, kGlideQuarter, 0.25}, {4, kGlideQuarter, 0.2125}, {2, kGlideOne, 0.425}, {2, kGlideTwo, 0.425}, {3, kGlideFull, 0.85 / 3}};
    for (const Row& row : rows) {
        Rig rig;
        rig.engine->send(noteAt(0, 0, 57, 1.f));
        rig.engine->send(noteAt(1, 4, 69, 2.f, packed(row.ratchet, row.kind)));
        rig.play(8);
        int slides = 0;
        for (const Began& b : rig.log) slides += b.pitch == 69 && b.from >= 0;
        CHECK(rig.count(69) == row.ratchet && slides == 1);
        const Began* b = rig.at(4, 69);
        CHECK(b && b->from == 57 && about(b->seconds, row.steps * kStepSeconds));
    }
}

// FULL: as long as it has been since the note it slides from began.
static void testFull() {
    Rig rig;
    rig.engine->send(noteAt(0, 0, 57, 1.f, packed(0, kGlideFull)));         // the first note after Play: nothing to come from
    rig.engine->send(noteAt(1, 3, 69, 1.f, packed(0, kGlideFull), 0.25f));  // 3.25 steps after it
    rig.engine->send(noteAt(2, 6, 64, 1.f, packed(0, kGlideFull), -0.25f)); // 2.5 after that
    rig.engine->send(noteAt(3, 8, 64, 1.f, packed(0, kGlideFull)));         // the same key: nowhere to slide
    rig.engine->send(noteAt(4, 9, 60, 1.f, packed(4, kGlideOff)));          // a ratchet...
    rig.engine->send(noteAt(5, 12, 72, 1.f, packed(0, kGlideFull)));        // ...FULL is from where the note began, not its last piece
    rig.play(32);
    CHECK(rig.at(0, 57) && rig.at(0, 57)->from == -1);
    CHECK(rig.at(3.25, 69) && rig.at(3.25, 69)->from == 57 && about(rig.at(3.25, 69)->seconds, 3.25 * kStepSeconds));
    CHECK(rig.at(5.75, 64) && rig.at(5.75, 64)->from == 69 && about(rig.at(5.75, 64)->seconds, 2.5 * kStepSeconds));
    CHECK(rig.at(8, 64) && rig.at(8, 64)->from == -1);
    CHECK(rig.at(12, 72) && rig.at(12, 72)->from == 60 && about(rig.at(12, 72)->seconds, 3 * kStepSeconds));
    // The second time round the first note has something to come from: the last note of the loop, four steps before.
    CHECK(rig.at(16, 57) && rig.at(16, 57)->from == 72 && about(rig.at(16, 57)->seconds, 4 * kStepSeconds));
    // Stop and Play: the first note is the first again.
    rig.log.clear();
    rig.engine->send(make(Command::Stop));
    rig.run(1);
    rig.engine->send(make(Command::Play, 0, 0, 1));
    rig.run(4);
    CHECK(!rig.log.empty() && rig.log[0].pitch == 57 && rig.log[0].from == -1);
    CHECK(rig.log.size() >= 2 && rig.log[1].pitch == 69 && rig.log[1].from == 57 && about(rig.log[1].seconds, 3.25 * kStepSeconds));
}

// Which note it slides from: the last the track began. A ratchet's later pieces are not notes
// beginning. Notes that begin together are one moment: none slides from another of them, and
// what follows slides from the highest.
static void testWhichNoteItSlidesFrom() {
    {
        Rig rig;
        rig.engine->send(noteAt(0, 0, 60, 1.f, packed(4, kGlideOff))); // pieces at 0, 0.25, 0.5, 0.75
        rig.engine->send(noteAt(1, 0, 64, 0.5f, 0, 0.3f));             // begins between two of them
        rig.engine->send(noteAt(2, 2, 72, 1.f, packed(0, kGlideFull)));
        rig.play(4);
        CHECK(rig.count(60) == 4);
        CHECK(rig.at(2, 72) && rig.at(2, 72)->from == 64 && about(rig.at(2, 72)->seconds, 1.7 * kStepSeconds));
    }
    {
        Rig rig;
        // Three together, the highest neither first nor last in the engine's list; one of them glides.
        rig.engine->send(noteAt(0, 4, 64, 1.f));
        rig.engine->send(noteAt(1, 4, 67, 1.f, packed(0, kGlideFull)));
        rig.engine->send(noteAt(2, 4, 60, 1.f, packed(0, kGlideOne)));
        rig.engine->send(noteAt(3, 0, 48, 1.f));
        rig.engine->send(noteAt(4, 8, 55, 1.f, packed(0, kGlideFull)));
        rig.engine->send(noteAt(5, 10, 52, 1.f, packed(0, kGlideQuarter), 0.2f)); // late ones, together: they wait their moment
        rig.engine->send(noteAt(6, 10, 79, 1.f, 0, 0.2f));
        rig.engine->send(noteAt(7, 10, 76, 1.f, 0, 0.2f));
        rig.engine->send(noteAt(8, 12, 50, 1.f, packed(0, kGlideFull)));
        rig.play(16);
        CHECK(rig.at(4, 67) && rig.at(4, 67)->from == 48 && about(rig.at(4, 67)->seconds, 4 * kStepSeconds)); // from the note before them all
        CHECK(rig.at(4, 60) && rig.at(4, 60)->from == 48 && about(rig.at(4, 60)->seconds, 1 * kStepSeconds));
        CHECK(rig.at(8, 55) && rig.at(8, 55)->from == 67 && about(rig.at(8, 55)->seconds, 4 * kStepSeconds)); // from the highest of them
        CHECK(rig.at(10.2, 52) && rig.at(10.2, 52)->from == 55);
        CHECK(rig.at(12, 50) && rig.at(12, 50)->from == 79 && about(rig.at(12, 50)->seconds, 1.8 * kStepSeconds));
    }
}

// ---- GLIDE FROM: which key of the moment before, by the track's type -----------------------

static Command glideFrom(int type, int track = 1) { return make(Command::SetGlideFrom, track, type); }
// One key of a chord: which of its keys it is, and what the chord does besides sound.
static Command chordKey(int slot, int step, int pitch, float length, int index, int count, ChordPlay play = ChordPlay(), float nudge = 0.f) {
    Command c = noteAt(slot, step, pitch, length, 0, nudge);
    c.play = play;
    c.play.index = static_cast<std::uint8_t>(index);
    c.play.count = static_cast<std::uint8_t>(count);
    return c;
}

// A moment of some keys, then a moment of gliding notes four steps on: which key each comes
// from under each type. (RANDOM: one of them. The expected keys are worked out by hand here.)
static void testEachTypeIsPlayed() {
    struct Case {
        const char* name;
        std::vector<int> old, now; // `now` in the order the engine is given them (not sorted)
        std::vector<int> top, paired, nearest;
    };
    const Case cases[] = {
        {"4 to 2", {60, 48, 67, 55}, {62, 58}, {67, 67}, {67, 48}, {60, 60}},
        {"2 to 4", {60, 48}, {70, 50, 56, 55}, {60, 60, 60, 60}, {60, 48, 60, 48}, {60, 48, 60, 60}},
        {"4 to 4", {48, 55, 60, 67}, {80, 49, 47, 50}, {67, 67, 67, 67}, {67, 55, 48, 60}, {67, 48, 48, 48}},
        {"4 to 3", {48, 55, 60, 67}, {70, 40, 52}, {67, 67, 67}, {67, 48, 60}, {67, 48, 55}},
        {"1 to 3", {52}, {40, 60, 80}, {52, 52, 52}, {52, 52, 52}, {52, 52, 52}},
        {"3 to 1", {48, 55, 67}, {50}, {67}, {67}, {48}},
        {"1 to 1", {52}, {64}, {52}, {52}, {52}},
        {"a tie", {48, 60}, {54}, {60}, {60}, {60}},
        {"its own key", {48, 60, 64}, {60, 70}, {64, 64}, {48, 64}, {-1, 64}}, // NEAREST: the same key does not slide
    };
    for (const Case& c : cases) {
        for (int type = 0; type < kGlideFroms; ++type) {
            for (float nudge : {0.f, 0.2f, -0.2f}) { // on the step, late (they wait), early (asked a step ahead)
                Rig rig;
                rig.engine->send(glideFrom(type));
                int slot = 0;
                for (int key : c.old) rig.engine->send(noteAt(slot++, 2, key, 1.f));
                for (int key : c.now) rig.engine->send(noteAt(slot++, 6, key, 1.f, packed(0, kGlideFull), nudge));
                rig.play(10);
                const std::vector<int>& want = type == kFromTop ? c.top : type == kFromPaired ? c.paired : c.nearest;
                for (std::size_t i = 0; i < c.now.size(); ++i) {
                    const Began* b = rig.at(6.0 + nudge, c.now[i]);
                    CHECK(b != nullptr);
                    if (!b) continue;
                    bool ok = false;
                    if (type == kFromRandom) {
                        for (int key : c.old) ok = ok || b->from == key || (b->from == -1 && key == c.now[i]);
                    } else {
                        ok = b->from == want[i];
                    }
                    if (ok && b->from >= 0) ok = about(b->seconds, (4.0 + static_cast<double>(nudge)) * kStepSeconds); // FULL: since the moment it comes from began
                    CHECK(ok);
                    if (!ok) std::printf("  %s, %s, nudge %.1f: key %d from %d for %.4f s (wanted from %d)\n", c.name, glideFromName(type), nudge, c.now[i], b->from, b->seconds, type == kFromRandom ? -2 : want[i]);
                }
                for (int key : c.old) CHECK(rig.at(2, key) && rig.at(2, key)->from == -1);
            }
        }
    }
    // A type nobody knows is TOP; a track with no GLIDE FROM told is TOP; another track's is its own.
    for (int told : {-1, 0, 4, 77}) {
        Rig rig;
        Command c = make(Command::InstallSynth, 2);
        std::vector<Began> other;
        c.pointer = new Spy(&other);
        CHECK(rig.engine->send(c));
        rig.engine->send(glideFrom(kFromPaired, 2));
        if (told >= 0) rig.engine->send(glideFrom(told));
        for (int track : {1, 2}) {
            rig.engine->send(noteAt(0, 2, 48, 1.f, 0, 0.f, 0, track));
            rig.engine->send(noteAt(1, 2, 60, 1.f, 0, 0.f, 0, track));
            rig.engine->send(noteAt(2, 6, 50, 1.f, packed(0, kGlideOne), 0.f, 0, track));
            rig.engine->send(noteAt(3, 6, 70, 1.f, packed(0, kGlideOne), 0.f, 0, track));
        }
        rig.play(10);
        CHECK(rig.at(6, 50) && rig.at(6, 50)->from == 60 && rig.at(6, 70) && rig.at(6, 70)->from == 60);
        int low = -2;
        for (const Began& b : other) low = b.pitch == 50 ? b.from : low;
        CHECK(low == 48);
    }
    // Turned while the song plays: the notes that begin after it go by it.
    {
        Rig rig;
        rig.engine->send(noteAt(0, 2, 48, 1.f));
        rig.engine->send(noteAt(1, 2, 60, 1.f));
        rig.engine->send(noteAt(2, 6, 50, 1.f, packed(0, kGlideOne)));
        rig.engine->send(noteAt(3, 6, 70, 1.f, packed(0, kGlideOne)));
        rig.play(16);
        rig.engine->send(glideFrom(kFromPaired));
        rig.run(16);
        rig.engine->send(glideFrom(kFromNearest));
        rig.run(16);
        rig.engine->send(glideFrom(kFromTop));
        rig.run(16);
        const int want[4] = {60, 48, 48, 60};
        for (int round = 0; round < 4; ++round) CHECK(rig.at(round * 16 + 6, 50) && rig.at(round * 16 + 6, 50)->from == want[round] && rig.at(round * 16 + 6, 70)->from == 60);
    }
    // A long song: what was told of a step is forgotten when the next comes, so the list of
    // what is coming never fills (it holds 128), and the fiftieth time round is as the first.
    {
        Rig rig;
        rig.engine->send(glideFrom(kFromPaired));
        rig.engine->send(noteAt(0, 2, 48, 1.f));
        rig.engine->send(noteAt(1, 2, 60, 1.f));
        rig.engine->send(noteAt(2, 6, 50, 1.f, packed(0, kGlideOne)));
        rig.engine->send(noteAt(3, 6, 70, 1.f, packed(0, kGlideOne)));
        rig.engine->send(noteAt(4, 10, 45, 1.f, packed(0, kGlideOne), -0.25f));
        rig.engine->send(noteAt(5, 10, 75, 1.f, packed(0, kGlideOne), -0.25f));
        rig.play(16 * 50);
        int low = 0, high = 0, early = 0;
        for (const Began& b : rig.log) low += b.pitch == 50 && b.from == 48, high += b.pitch == 70 && b.from == 60, early += b.pitch == 45 && b.from == 50;
        CHECK(low == 50 && high == 50 && early == 50);
    }
    // A track taken away and another put in its place: TOP again.
    {
        Rig rig;
        rig.engine->send(glideFrom(kFromPaired));
        rig.run(1);
        CHECK(rig.engine->send(make(Command::RemoveInstrument, 1)));
        rig.run(1);
        rig.engine->collectGarbage();
        std::vector<Began> log;
        Command c = make(Command::InstallSynth, 1);
        c.pointer = new Spy(&log);
        CHECK(rig.engine->send(c));
        rig.engine->send(noteAt(0, 2, 48, 1.f));
        rig.engine->send(noteAt(1, 2, 60, 1.f));
        rig.engine->send(noteAt(2, 6, 50, 1.f, packed(0, kGlideOne)));
        rig.engine->send(noteAt(3, 6, 70, 1.f, packed(0, kGlideOne)));
        rig.play(10);
        int low = -2;
        for (const Began& b : log) low = b.pitch == 50 ? b.from : low;
        CHECK(low == 60);
    }
    // A track handed over whole (a song opening, an undo) keeps its GLIDE FROM: it is the track's, not the patterns'.
    {
        Rig rig;
        rig.engine->send(glideFrom(kFromPaired));
        std::unique_ptr<TrackImage> image(new TrackImage);
        const int keys[4] = {48, 60, 50, 70};
        for (int i = 0; i < 4; ++i) {
            TrackImage::Note& n = image->notes[0][i];
            n.step = i < 2 ? 2 : 6, n.pitch = static_cast<std::uint8_t>(keys[i]), n.length = 1.f, n.velocity = 0.9f, n.glide = i < 2 ? kGlideOff : kGlideOne;
        }
        Command c = make(Command::SetTrackImage, 1);
        c.pointer = image.get();
        if (rig.engine->send(c)) image.release();
        rig.play(10);
        CHECK(rig.at(6, 50) && rig.at(6, 50)->from == 48 && rig.at(6, 70) && rig.at(6, 70)->from == 60);
    }
}

// What a moment is does not change with the type: a ratchet's later pieces are not notes
// beginning, and only its first piece slides, from the key picked, never for longer than 85%
// of the piece; FULL is the time since the picked key's moment began.
static void testTypesWithARatchet() {
    for (int type = 0; type < kGlideFroms; ++type) {
        Rig rig;
        rig.engine->send(glideFrom(type));
        rig.engine->send(noteAt(0, 1, 48, 1.f));
        rig.engine->send(noteAt(1, 1, 60, 1.f, packed(4, kGlideOff)));  // pieces at 1, 1.25, 1.5, 1.75: one moment with 48
        rig.engine->send(noteAt(2, 4, 50, 2.f, packed(3, kGlideFull))); // a ratchet that glides
        rig.engine->send(noteAt(3, 4, 70, 2.f, packed(0, kGlideFull)));
        rig.engine->send(noteAt(4, 8, 40, 1.f, packed(0, kGlideFull))); // after the ratchet: from its moment, not its last piece
        rig.engine->send(noteAt(5, 8, 90, 1.f, packed(2, kGlideTwo)));
        rig.play(12);
        CHECK(rig.count(60) == 4 && rig.count(50) == 3 && rig.count(90) == 2);
        int slides = 0;
        for (const Began& b : rig.log) slides += (b.pitch == 50 || b.pitch == 90) && b.from >= 0;
        CHECK(slides == 2);
        const Began *low = rig.at(4, 50), *high = rig.at(4, 70), *after = rig.at(8, 40), *last = rig.at(8, 90);
        CHECK(low && high && after && last);
        if (!low || !high || !after || !last) continue;
        const int wantLow = type == kFromTop ? 60 : 48, wantAfter = type == kFromTop ? 70 : 50;
        if (type != kFromRandom) CHECK(low->from == wantLow && high->from == 60 && after->from == wantAfter && last->from == 70);
        else CHECK((low->from == 48 || low->from == 60) && (high->from == 48 || high->from == 60) && (after->from == 50 || after->from == 70) && (last->from == 50 || last->from == 70));
        CHECK(about(low->seconds, 0.85 / 3 * kStepSeconds) && about(high->seconds, 3 * kStepSeconds)); // (the moment began at step 1, whichever of its keys is picked)
        CHECK(about(after->seconds, 4 * kStepSeconds) && about(last->seconds, 0.425 * kStepSeconds));
    }
}

// A chord that is strummed, humanised or an arpeggio is still one moment, at its own time,
// and its keys as written are what the notes after it come from, under every type. With
// PAIRED its keys count in their own moment too: a bass note under a chord comes from the
// bottom of the moment before and a melody note over it from the top.
static void testTypesWithChords() {
    const ChordPlay plays[] = {ChordPlay(), chordPlayOf(kStrumWide, kOrderDown, 0, 0, 0), chordPlayOf(kStrumOff, 0, 100, 100, 100), chordPlayOf(kStrumWide, kOrderRandom, 100, 0, 50),
                               chordPlayOf(kStrumQuarter, kOrderDown, 0, 0, 0), chordPlayOf(kStrumHalf, kOrderRandom, 100, 0, 0), chordPlayOf(kStrumOff, 0, 0, 100, 0)};
    for (const ChordPlay& play : plays) {
        for (int type = 0; type < kGlideFroms; ++type) {
            for (float nudge : {0.f, 0.25f, -0.25f}) { // the chord's
                Rig rig;
                rig.engine->send(glideFrom(type));
                const int first[4] = {50, 55, 59, 62}, chord[3] = {60, 64, 67};
                // (The loose notes are in the slots before the chords': the engine comes to them first.)
                for (int k = 0; k < 4; ++k) rig.engine->send(chordKey(10 + k, 2, first[k], 3.f, k, 4, play, nudge));
                // The second chord, a bass note under it, a melody note over it and one between its keys, all in one moment.
                for (int k = 0; k < 3; ++k) rig.engine->send(chordKey(14 + k, 8, chord[k], 3.f, k, 3, play, nudge));
                rig.engine->send(noteAt(0, 8, 40, 1.f, packed(0, kGlideFull), nudge));
                rig.engine->send(noteAt(1, 8, 80, 1.f, packed(0, kGlideFull), nudge));
                rig.engine->send(noteAt(2, 8, 63, 1.f, packed(0, kGlideFull), nudge));
                // Notes while the second chord sounds (an arpeggio is in the middle of its round), and after it.
                rig.engine->send(noteAt(3, 10, 61, 0.5f, packed(0, kGlideFull)));
                rig.engine->send(noteAt(4, 13, 30, 0.5f, packed(0, kGlideFull)));
                rig.engine->send(noteAt(5, 13, 100, 0.5f, packed(0, kGlideFull)));
                rig.play(16);
                const Began *bass = rig.at(8.0 + nudge, 40), *melody = rig.at(8.0 + nudge, 80), *middle = rig.at(8.0 + nudge, 63), *during = rig.at(10, 61), *low = rig.at(13, 30), *high = rig.at(13, 100);
                CHECK(bass && melody && middle && during && low && high);
                if (!bass || !melody || !middle || !during || !low || !high) continue;
                bool ok = about(bass->seconds, 6 * kStepSeconds) && about(melody->seconds, 6 * kStepSeconds) && about(middle->seconds, 6 * kStepSeconds) && about(during->seconds, (2.0 - nudge) * kStepSeconds) && about(low->seconds, 3 * kStepSeconds) && about(high->seconds, 3 * kStepSeconds);
                if (type == kFromTop) ok = ok && bass->from == 62 && melody->from == 62 && middle->from == 62 && during->from == 80 && low->from == 61 && high->from == 61;
                // PAIRED: the moment at 8 is six keys (40, 60, 63, 64, 67, 80) from four: the third of six comes from the
                // second of four (of the three loose notes alone it would be the third). 61 is alone, from the top of the six.
                if (type == kFromPaired) ok = ok && bass->from == 50 && melody->from == 62 && middle->from == 55 && during->from == 80 && low->from == 61 && high->from == 61;
                if (type == kFromNearest) ok = ok && bass->from == 50 && melody->from == 62 && middle->from == 62 && during->from == 60 && low->from == 61 && high->from == 61;
                if (type == kFromRandom) {
                    const auto in = [](int key, std::initializer_list<int> keys) {
                        for (int k : keys)
                            if (k == key) return true;
                        return false;
                    };
                    ok = ok && in(bass->from, {50, 55, 59, 62}) && in(melody->from, {50, 55, 59, 62}) && in(middle->from, {50, 55, 59, 62}) && in(during->from, {40, 60, 63, 64, 67, 80}) && low->from == 61 && high->from == 61;
                }
                CHECK(ok);
                if (!ok) std::printf("  chord (strum %d, time %d), %s, nudge %.2f: bass from %d (%.3f s), melody from %d, middle from %d, during from %d (%.3f s), after from %d and %d\n", play.strum, play.time, glideFromName(type), nudge, bass->from, bass->seconds, melody->from, middle->from, during->from, during->seconds, low->from, high->from);
            }
        }
    }
    // After a chord alone (no loose notes with it), two notes: bottom from bottom, top from top; and
    // an arpeggio's own sounding notes are never what a glide comes from.
    for (int strum : {kStrumOff, kStrumTight, kStrumQuarter, kStrumStep}) {
        for (int type = 0; type < 3; ++type) {
            Rig rig;
            rig.engine->send(glideFrom(type));
            const int chord[4] = {48, 55, 60, 67};
            for (int k = 0; k < 4; ++k) rig.engine->send(chordKey(k, 0, chord[k], 6.f, k, 4, chordPlayOf(strum, kOrderUp, strum == kStrumTight ? 50 : 0, 0, 0)));
            rig.engine->send(noteAt(4, 5, 58, 1.f, packed(0, kGlideFull), 0.5f)); // (as an arpeggio's note sounds: 5.5)
            rig.engine->send(noteAt(5, 5, 66, 1.f, packed(0, kGlideFull), 0.5f));
            rig.play(8);
            const Began *low = rig.at(5.5, 58), *high = rig.at(5.5, 66);
            CHECK(low && high);
            if (!low || !high) continue;
            const int wantLow = type == kFromTop ? 67 : type == kFromPaired ? 48 : 60, wantHigh = 67;
            CHECK(low->from == wantLow && high->from == wantHigh && about(low->seconds, 5.5 * kStepSeconds) && about(high->seconds, 5.5 * kStepSeconds));
        }
    }
}

// RANDOM: worked out from where the note is and which time it is played. Play from the top is
// the same performance again; no two times round are alike; two tracks with the same notes
// differ; every key of the moment before has its turn; and it is the rule's own roll.
static void testRandomIsRolledForItsPlace() {
    const int chord[4] = {48, 55, 60, 67};
    const auto fill = [&](Rig& rig, int track) {
        int slot = 0;
        for (int step = 0; step < 16; step += 4) {
            for (int key : chord) rig.engine->send(noteAt(slot++, step, key, 1.f, 0, 0.f, 0, track));
            rig.engine->send(noteAt(slot++, step + 2, 72, 1.f, packed(0, kGlideFull), step == 8 ? 0.25f : step == 12 ? -0.25f : 0.f, 0, track));
            rig.engine->send(noteAt(slot++, step + 2, 36, 1.f, packed(0, kGlideOne), step == 8 ? 0.25f : step == 12 ? -0.25f : 0.f, 0, track));
        }
    };
    Rig rig;
    std::vector<Began> other;
    Command c = make(Command::InstallSynth, 2);
    c.pointer = new Spy(&other);
    CHECK(rig.engine->send(c));
    rig.engine->send(glideFrom(kFromRandom));
    rig.engine->send(glideFrom(kFromRandom, 2));
    fill(rig, 1);
    fill(rig, 2);
    const int rounds = 12;
    rig.play(16 * rounds);
    std::vector<int> high, low, highOther;
    for (const Began& b : rig.log)
        if (b.pitch == 72) high.push_back(b.from);
        else if (b.pitch == 36) low.push_back(b.from);
    for (const Began& b : other)
        if (b.pitch == 72) highOther.push_back(b.from);
    CHECK(high.size() == 4u * rounds && low.size() == high.size() && highOther.size() == high.size());
    if (high.size() != 4u * rounds || low.size() != high.size() || highOther.size() != high.size()) return;
    GlideKeys keys;
    for (int key : chord) keys.add(key);
    int picked[128] = {}, roundsDiffer = 0, tracksDiffer = 0, notesDiffer = 0;
    for (std::size_t i = 0; i < high.size(); ++i) {
        CHECK(keys.has(high[i]) && keys.has(low[i]));
        ++picked[high[i] < 0 ? 0 : high[i]];
        const double nudge = i % 4 == 2 ? 0.25 : i % 4 == 3 ? -0.25 : 0.0;
        const double at = static_cast<double>(i / 4) * 16.0 + static_cast<double>(i % 4) * 4.0 + 2.0 + static_cast<double>(static_cast<float>(nudge));
        CHECK(high[i] == glideFromKey(kFromRandom, keys, 72, 0, 1, glideSeed(1, at)) && low[i] == glideFromKey(kFromRandom, keys, 36, 0, 1, glideSeed(1, at)));
        CHECK(highOther[i] == glideFromKey(kFromRandom, keys, 72, 0, 1, glideSeed(2, at)));
        roundsDiffer += i >= 4 && high[i] != high[i - 4];
        tracksDiffer += high[i] != highOther[i];
        notesDiffer += high[i] != low[i];
        const Began* b = rig.at(at, 72);
        CHECK(b && about(b->seconds, (2.0 + nudge) * kStepSeconds)); // FULL: since the chord, whichever key
    }
    for (int key : chord) CHECK(picked[key] >= 4);
    CHECK(roundsDiffer > 15 && tracksDiffer > 15 && notesDiffer > 15);
    // Stop, and Play from the top: the same again.
    const std::vector<Began> first = rig.log;
    rig.log.clear();
    rig.engine->send(make(Command::Stop));
    rig.run(3);
    rig.play(16 * rounds);
    CHECK(first.size() == rig.log.size());
    for (std::size_t i = 0; i < first.size() && i < rig.log.size(); ++i) CHECK(first[i].pitch == rig.log[i].pitch && first[i].from == rig.log[i].from && first[i].seconds == rig.log[i].seconds);
}

// Two notes within a 256th of a step, one just before a step's line and one on it. The notes
// a step begins are told a step at a time, so to PAIRED the first is alone when it begins
// (from the top) and the second is one of two. Not what a player would draw; it takes two
// notes under half a millisecond apart across a line. Held here as it is, and the roll draws
// the same.
static void testPairedAcrossAStepLine() {
    Rig rig;
    rig.engine->send(glideFrom(kFromPaired));
    rig.engine->send(noteAt(0, 0, 48, 1.f));
    rig.engine->send(noteAt(1, 0, 60, 1.f));
    rig.engine->send(noteAt(2, 4, 50, 1.f, packed(0, kGlideFull), -0.002f));
    rig.engine->send(noteAt(3, 4, 70, 1.f, packed(0, kGlideFull)));
    rig.engine->send(noteAt(4, 8, 52, 1.f, packed(0, kGlideFull), 0.002f)); // both after the line: a pair
    rig.engine->send(noteAt(5, 8, 72, 1.f, packed(0, kGlideFull)));
    rig.play(12);
    const Began *early = rig.at(4.0 - 0.002, 50), *on = rig.at(4, 70), *late = rig.at(8.002, 52), *with = rig.at(8, 72);
    CHECK(early && on && late && with);
    if (!early || !on || !late || !with) return;
    CHECK(early->from == 60 && on->from == 60); // (told whole, the early one would come from 48)
    CHECK(late->from == 50 && with->from == 70);
    std::vector<GlideNote> notes(kGlideNotes);
    std::vector<GlideLine> lines(kGlideNotes);
    const int keys[6] = {48, 60, 50, 70, 52, 72}, steps[6] = {0, 0, 4, 4, 8, 8};
    const float nudges[6] = {0.f, 0.f, -0.002f, 0.f, 0.002f, 0.f};
    for (int i = 0; i < 6; ++i) notes[i].sounds = true, notes[i].pitch = keys[i], notes[i].step = steps[i], notes[i].nudge = nudges[i], notes[i].glide = i >= 2 ? kGlideFull : kGlideOff;
    glideLines(notes.data(), 1, 16, lines.data(), kFromPaired, 1);
    CHECK(lines[2].from == 60 && lines[3].from == 60 && lines[4].from == 50 && lines[5].from == 70);
}

// Where the scene goes round: a note half a step late on the last step and a note half a step
// early on the first are one moment, of two keys. The engine's last step asks the first step's
// early notes a step ahead and so tells both; the roll must tell the same (it told a step only
// its own round's notes, and drew the late note as alone: from the top).
static void testPairedWhereTheSceneGoesRound() {
    for (int type = 0; type < kGlideFroms; ++type) {
        Rig rig;
        rig.engine->send(glideFrom(type));
        rig.engine->send(noteAt(0, 8, 50, 1.f));
        rig.engine->send(noteAt(1, 8, 60, 1.f));
        rig.engine->send(noteAt(2, 15, 55, 0.25f, packed(0, kGlideFull), 0.5f));
        rig.engine->send(noteAt(3, 0, 70, 0.25f, packed(0, kGlideFull), -0.5f));
        rig.play(3 * 16 + 2);
        const Began *late = rig.at(2 * 16 + 15.5, 55), *early = rig.at(3 * 16 - 0.5, 70);
        CHECK(late && early);
        if (!late || !early) continue;
        if (type == kFromPaired) CHECK(late->from == 50 && early->from == 60); // the lower of two from the bottom, the upper from the top
        if (type == kFromTop) CHECK(late->from == 60 && early->from == 60);
        std::vector<GlideNote> notes(kGlideNotes);
        std::vector<GlideLine> lines(kGlideNotes);
        const int keys[4] = {50, 60, 55, 70}, steps[4] = {8, 8, 15, 0};
        const float nudges[4] = {0.f, 0.f, 0.5f, -0.5f};
        for (int i = 0; i < 4; ++i) notes[i].sounds = true, notes[i].pitch = keys[i], notes[i].step = steps[i], notes[i].nudge = nudges[i], notes[i].glide = i >= 2 ? kGlideFull : kGlideOff;
        glideLines(notes.data(), 1, 16, lines.data(), type, 1, 2);
        CHECK(lines[2].from == late->from && lines[3].from == early->from);
        CHECK(about(late->seconds, lines[2].steps * kStepSeconds) && about(early->seconds, lines[3].steps * kStepSeconds));
        if (lines[2].from != late->from) std::printf("  %s, the late note on the last step: the engine from %d, the roll from %d\n", glideFromName(type), late->from, lines[2].from);
    }
}

// RANDOM on the roll is the roll for the first time the scene plays after Play. Play always
// starts on the scene being worked on (Session::togglePlay), the song or the scene alone, so
// that is what is heard whichever scene the roll shows. Held here for a later scene, both ways
// of playing. What is NOT the same, and is held as it is: a song played through from an
// earlier scene reaches this one at another time, and rolls other keys there.
static void testRandomOnALaterScene() {
    // Four chords of four keys, a gliding note two steps after each.
    const int roots[4] = {40, 47, 54, 61}, shape[4] = {0, 4, 7, 11}, after[4] = {30, 90, 31, 91};
    std::vector<GlideNote> notes(kGlideNotes);
    std::vector<GlideLine> lines(kGlideNotes);
    for (int i = 0; i < 20; ++i) {
        GlideNote& n = notes[static_cast<std::size_t>(i)];
        const int group = i / 5, k = i % 5;
        n.sounds = true, n.step = group * 4 + (k == 4 ? 2 : 0), n.pitch = k == 4 ? after[group] : roots[group] + shape[k], n.glide = k == 4 ? kGlideFull : kGlideOff;
    }
    glideLines(notes.data(), 1, 16, lines.data(), kFromRandom, 1);
    int differs = 0, notTheTop = 0;
    for (int way = 0; way < 3; ++way) { // 0: the song from scene 2; 1: scene 2 alone, going round; 2: the song from scene 1
        Rig rig;
        rig.engine->send(make(Command::SetSceneCount, 0, 2));
        rig.engine->send(glideFrom(kFromRandom));
        for (int i = 0; i < 20; ++i) {
            const GlideNote& n = notes[static_cast<std::size_t>(i)];
            Command c = noteAt(i, n.step, n.pitch, 1.f, packed(0, n.glide));
            c.scene = 1;
            rig.engine->send(c);
        }
        rig.engine->send(make(Command::Play, 0, way == 2 ? 0 : 1, way == 1 ? 1 : 0));
        rig.run(way == 2 ? 31 : 15);
        // (way 2: scene 1 is empty and plays first, so these are the only notes there all the same)
        for (int group = 0; group < 4; ++group) {
            const Began* b = rig.at((way == 2 ? 16 : 0) + group * 4 + 2, after[group]);
            const int drawn = lines[static_cast<std::size_t>(group * 5 + 4)].from;
            CHECK(b != nullptr && drawn >= roots[group] && drawn <= roots[group] + 11);
            if (!b) continue;
            if (way < 2) CHECK(b->from == drawn);
            else differs += b->from != drawn;
            notTheTop += b->from != roots[group] + 11;
        }
    }
    CHECK(notTheTop > 0); // (it was RANDOM that was asked)
    CHECK(differs > 0);   // the song from an earlier scene: other rolls. Known, and said in the notes.
}

// The same performance however the sound is cut into blocks, and on any number of helpers.
static void testTypesAtAnyBlockLength() {
    for (int type = 0; type < kGlideFroms; ++type) {
        std::vector<Began> first;
        for (int way = 0; way < 6; ++way) {
            Rig rig;
            if (way == 5) rig.engine->useWorkers(3);
            rig.engine->send(glideFrom(type));
            rig.engine->send(make(Command::SetTempo, 0, 0, 0, 133.f)); // (a step is not a whole number of samples)
            const int first4[4] = {50, 55, 59, 62}, chord[3] = {60, 64, 67};
            int slot = 0;
            for (int k = 0; k < 4; ++k) rig.engine->send(chordKey(slot++, 1, first4[k], 2.f, k, 4, chordPlayOf(kStrumWide, kOrderUpDown, 100, 0, 0)));
            for (int k = 0; k < 3; ++k) rig.engine->send(chordKey(slot++, 9, chord[k], 3.f, k, 3, chordPlayOf(kStrumQuarter, kOrderRandom, 50, 0, 0), -0.2f));
            const int steps[] = {0, 4, 4, 4, 6, 6, 9, 9, 12, 12, 12, 15, 15};
            const int keys[] = {45, 40, 70, 57, 41, 71, 30, 90, 42, 72, 58, 43, 73};
            const float nudges[] = {0.f, 0.f, 0.f, 0.f, 0.31f, 0.31f, -0.2f, -0.2f, -0.37f, -0.37f, -0.37f, 0.f, -0.002f};
            for (int i = 0; i < 13; ++i) rig.engine->send(noteAt(slot++, steps[i], keys[i], 0.5f, packed(i % 5 == 4 ? 3 : 0, 1 + i % 5), nudges[i]));
            rig.engine->send(make(Command::Play, 0, 0, 1));
            const int frames = way == 0 ? 1000 : way == 1 ? 1 : way == 2 ? 7 : way == 3 ? 64 : way == 4 ? 480 : 256;
            std::vector<float> out(2 * 1000);
            const long whole = way == 1 ? 48000L * 4 : 48000L * 8; // (a sample at a time is slow: two rounds)
            for (long left = whole; left > 0; left -= frames) rig.engine->render(out.data(), left < frames ? static_cast<int>(left) : frames);
            if (way == 0) {
                first = rig.log;
                int slides = 0;
                for (const Began& b : first) slides += b.from >= 0;
                CHECK(slides > 40);
                continue;
            }
            std::size_t same = 0;
            while (same < rig.log.size() && same < first.size() && rig.log[same].pitch == first[same].pitch && rig.log[same].from == first[same].from && rig.log[same].seconds == first[same].seconds) ++same;
            CHECK(same == rig.log.size() && rig.log.size() > 60 && (way == 1 || rig.log.size() == first.size()));
            if (same != rig.log.size()) std::printf("  %s, way %d: differs at note %zu of %zu\n", glideFromName(type), way, same, rig.log.size());
        }
    }
}

// ---- the rule the roll draws from says what the engine does ------------------------------

static std::uint32_t seed = 0x1234567u;
static int roll(int outOf) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<int>((seed >> 12) % static_cast<std::uint32_t>(outOf));
}

static void testTheRollsRuleIsTheEngines(int type) {
    int glidesSeen = 0, wraps = 0, together = 0, plainGlides = 0, notTheTop = 0, firstTime = 0;
    for (int song = 0; song < 120; ++song) {
        const int cellBars = 1 + roll(3);
        const int sceneBars = song % 4 == 3 ? cellBars + 1 + roll(2) : cellBars; // sometimes the scene is longer than the cell
        int sceneSteps = sceneBars * 16;
        if (song % 5 == 4) sceneSteps -= 1 + roll(20) % (sceneSteps - 1); // sometimes it is cut short
        Rig rig;
        std::vector<Began> other;
        if (sceneBars > cellBars) { // another track holds the scene open
            Command c = make(Command::InstallSynth, 2);
            c.pointer = new Spy(&other);
            CHECK(rig.engine->send(c));
            Command bars = make(Command::SetCellBars, 2, sceneBars);
            CHECK(rig.engine->send(bars));
        }
        rig.engine->send(make(Command::SetCellBars, 1, cellBars));
        if (sceneSteps != sceneBars * 16) rig.engine->send(make(Command::SetSceneSteps, 0, sceneSteps));
        std::vector<GlideNote> notes(static_cast<std::size_t>(cellBars * kGlideNotes));
        const int count = 2 + roll(14);
        const GlideNote* lastPlaced = nullptr;
        if (type != kFromTop) rig.engine->send(make(Command::SetGlideFrom, 1, type));
        for (int i = 0; i < count; ++i) {
            const int bar = lastPlaced && roll(3) ? static_cast<int>((lastPlaced - notes.data()) / kGlideNotes) : roll(cellBars), slot = roll(kMaxNotes);
            GlideNote& n = notes[static_cast<std::size_t>(bar * kGlideNotes + slot)];
            if (n.sounds) continue;
            n.sounds = true;
            n.step = roll(4) == 0 ? roll(3) * 15 / 2 : roll(16); // (often step 1 or 16, where the loop goes round)
            n.pitch = 40 + roll(30);
            n.nudge = roll(3) == 0 ? static_cast<float>(roll(21) - 10) * 0.05f : 0.f; // (to half a step each way)
            // Often it begins with the note placed before it, or a hair after it: several keys to a moment.
            if (lastPlaced && roll(5) < 3) n.step = lastPlaced->step, n.nudge = lastPlaced->nudge + (roll(4) == 0 ? 0.002f : 0.f);
            if (n.nudge > 0.5f) n.nudge = 0.5f; // (the most a note can be nudged, and it is reached: the last step, half a step late)
            n.ratchet = roll(3) == 0 ? 2 + roll(3) : 0;
            n.glide = roll(4) == 0 ? 0 : 1 + roll(5);
            // No two on one key at one moment (the second would only cut the first off).
            bool clash = false;
            for (int s = 0; s < kGlideNotes; ++s) {
                const GlideNote& o = notes[static_cast<std::size_t>(bar * kGlideNotes + s)];
                clash = clash || (s != slot && o.sounds && o.pitch == n.pitch);
            }
            if (clash) {
                n = GlideNote();
                continue;
            }
            rig.engine->send(noteAt(slot, n.step, n.pitch, 0.5f, packed(n.ratchet, n.glide), n.nudge, bar));
            lastPlaced = &n;
        }
        // (The track is 1. RANDOM rolls anew each time round: the third time is asked for here,
        // and the first, which is what the roll draws, below.)
        std::vector<GlideLine> lines(notes.size()), top(notes.size()), drawn(notes.size());
        glideLines(notes.data(), cellBars, sceneSteps, lines.data(), type, 1, 2);
        glideLines(notes.data(), cellBars, sceneSteps, top.data());
        glideLines(notes.data(), cellBars, sceneSteps, drawn.data(), type, 1);
        rig.play(3 * sceneSteps + 2);
        // The first time round, as the roll draws it. What comes from the song's first moment is
        // left out: the first time, nothing was before that moment (or, early on step 1, it was
        // played at the top), so only a note with another moment between is the same.
        for (int bar = 0; bar < cellBars && bar * 16 < sceneSteps; ++bar) {
            for (int slot = 0; slot < kGlideNotes; ++slot) {
                const GlideNote& n = notes[static_cast<std::size_t>(bar * kGlideNotes + slot)];
                const int left = sceneSteps - bar * 16;
                if (!n.sounds || n.step >= (left < 16 ? left : 16)) continue;
                const double at = bar * 16 + n.step + static_cast<double>(n.nudge);
                bool between = false;
                for (int b = 0; b < cellBars && b * 16 < sceneSteps; ++b)
                    for (int s = 0; s < kGlideNotes; ++s) {
                        const GlideNote& o = notes[static_cast<std::size_t>(b * kGlideNotes + s)];
                        const double then = b * 16 + o.step + static_cast<double>(o.nudge);
                        between = between || (o.sounds && o.step < (sceneSteps - b * 16 < 16 ? sceneSteps - b * 16 : 16) && then > 0.01 && then < at - 0.01);
                    }
                if (!between) continue;
                const GlideLine& line = drawn[static_cast<std::size_t>(bar * kGlideNotes + slot)];
                const Began* b = rig.at(at, n.pitch);
                CHECK(b != nullptr);
                if (!b) continue;
                const bool same = b->from == line.from && (line.from < 0 || about(b->seconds, line.steps * kStepSeconds));
                CHECK(same);
                if (!same) std::printf("  %s, the first time round, song %d bar %d slot %d (step %d, key %d, nudge %.3f): the engine from %d for %.4f s, the roll from %d for %.4f s\n", glideFromName(type), song, bar, slot, n.step, n.pitch, n.nudge, b->from, b->seconds, line.from, line.steps * kStepSeconds);
                firstTime += line.from >= 0;
            }
        }
        // The third time round: each note, the first time its bar plays.
        for (int bar = 0; bar < cellBars && bar * 16 < sceneSteps; ++bar) {
            for (int slot = 0; slot < kGlideNotes; ++slot) {
                const GlideNote& n = notes[static_cast<std::size_t>(bar * kGlideNotes + slot)];
                const int left = sceneSteps - bar * 16;
                if (!n.sounds || n.step >= (left < 16 ? left : 16)) continue;
                const GlideLine& line = lines[static_cast<std::size_t>(bar * kGlideNotes + slot)];
                const double at = 2.0 * sceneSteps + bar * 16 + n.step + static_cast<double>(n.nudge);
                const Began* b = rig.at(at, n.pitch);
                CHECK(b != nullptr);
                if (!b) continue;
                const bool same = b->from == line.from && (line.from < 0 || about(b->seconds, line.steps * kStepSeconds));
                CHECK(same);
                notTheTop += line.from >= 0 && line.from != top[static_cast<std::size_t>(bar * kGlideNotes + slot)].from;
                if (!same) std::printf("  %s, song %d bar %d slot %d (step %d, key %d, nudge %.3f, x%d, glide %d): the engine from %d for %.4f s, the rule from %d for %.4f s\n", glideFromName(type), song, bar, slot, n.step, n.pitch, n.nudge, n.ratchet, n.glide, b->from, b->seconds, line.from, line.steps * kStepSeconds);
                if (n.glide == kGlideOff) CHECK(line.from == -1);
                glidesSeen += line.from >= 0;
                plainGlides += n.glide != kGlideOff && line.from < 0;
                wraps += line.from >= 0 && line.steps > 0 && n.glide == kGlideFull && n.ratchet < 2 && bar * 16 + n.step + n.nudge - line.steps < -0.01;
                for (int s = 0; s < kGlideNotes; ++s) {
                    const GlideNote& o = notes[static_cast<std::size_t>(bar * kGlideNotes + s)];
                    together += s != slot && o.sounds && n.glide && o.step == n.step && o.nudge == n.nudge;
                }
            }
        }
    }
    // The songs did hold what the test is for.
    CHECK(glidesSeen > 300 && wraps > 10 && together > 50 && firstTime > 150);
    CHECK(type == kFromTop ? notTheTop == 0 : notTheTop > 60); // the other types did pick another key, often
    (void)plainGlides;
    if (failures || std::getenv("GLIDE_PRINT")) std::printf("  the rule and the engine, %s: %d glides, %d across the loop's end, %d beginning with another note, %d not from the top, %d held the first time round\n", glideFromName(type), glidesSeen, wraps, together, notTheTop, firstTime);
}

// GLIDE FROM by itself: the keys of a moment, which of them each type picks, and what a
// PAIRED note knows of its own moment.
static void testTheKeysAndThePicking() {
    CHECK(kFromTop == 0 && kFromPaired == 1 && kFromNearest == 2 && kFromRandom == 3 && kGlideFroms == 4); // the numbers are in songs
    CHECK(glideFromKind(0) == kFromTop && glideFromKind(1) == kFromPaired && glideFromKind(2) == kFromNearest && glideFromKind(3) == kFromRandom);
    CHECK(glideFromKind(4) == kFromTop && glideFromKind(-1) == kFromTop && glideFromKind(200) == kFromTop);
    CHECK(std::string(glideFromName(0)) == "TOP" && std::string(glideFromName(1)) == "PAIRED" && std::string(glideFromName(2)) == "NEAREST" && std::string(glideFromName(3)) == "RANDOM" && std::string(glideFromName(9)) == "TOP");
    {
        GlideKeys keys;
        CHECK(keys.empty() && keys.count() == 0 && keys.top() == -1 && keys.at(0) == -1);
        for (int pitch : {64, 0, 127, 63, 64, 100, -1, 128}) keys.add(pitch); // (64 twice is one key; what is no key is not kept)
        CHECK(!keys.empty() && keys.count() == 5 && keys.top() == 127);
        CHECK(keys.at(0) == 0 && keys.at(1) == 63 && keys.at(2) == 64 && keys.at(3) == 100 && keys.at(4) == 127 && keys.at(5) == -1);
        CHECK(keys.has(63) && keys.has(64) && !keys.has(65) && !keys.has(-1) && !keys.has(128) && !keys.has(1));
        CHECK(keys.below(0) == 0 && keys.below(64) == 2 && keys.below(65) == 3 && keys.below(127) == 4 && keys.below(500) == 5);
        // Every key there is fits: nothing is ever left out of a moment.
        GlideKeys all;
        for (int pitch = 0; pitch < 128; ++pitch) all.add(pitch);
        CHECK(all.count() == 128 && all.at(77) == 77 && all.top() == 127);
    }
    const auto keysOf = [](std::initializer_list<int> pitches) {
        GlideKeys keys;
        for (int pitch : pitches) keys.add(pitch);
        return keys;
    };
    const GlideKeys four = keysOf({48, 55, 60, 67}), two = keysOf({48, 60}), none;
    for (int type = 0; type < kGlideFroms; ++type) {
        CHECK(glideFromKey(type, none, 60, 0, 1, 7) == -1);              // nothing before: nothing to come from
        CHECK(glideFromKey(type, keysOf({52}), 60, 0, 1, 7) == 52);      // one key before: that one, whatever the type
        CHECK(glideFromKey(type, keysOf({52}), 60, 2, 4, 7) == 52);
    }
    CHECK(glideFromKey(kFromTop, four, 30, 0, 4, 0) == 67 && glideFromKey(kFromTop, four, 90, 3, 4, 0) == 67);
    CHECK(glideFromKey(9, four, 30, 0, 4, 0) == 67); // (a type nobody knows: TOP)
    // PAIRED: new i of n from old round(i * (m - 1) / (n - 1)), a half rounded up.
    struct Pair { int m, n; int want[6]; };
    const Pair pairs[] = {{4, 2, {0, 3}}, {2, 4, {0, 0, 1, 1}}, {4, 4, {0, 1, 2, 3}}, {4, 3, {0, 2, 3}}, {3, 2, {0, 2}}, {2, 3, {0, 1, 1}},
                          {3, 5, {0, 1, 1, 2, 2}}, {5, 3, {0, 2, 4}}, {1, 4, {0, 0, 0, 0}}, {6, 4, {0, 2, 3, 5}}, {2, 6, {0, 0, 0, 1, 1, 1}}};
    for (const Pair& pair : pairs) {
        GlideKeys old;
        for (int k = 0; k < pair.m; ++k) old.add(40 + 5 * k);
        for (int i = 0; i < pair.n; ++i) {
            const int got = glideFromKey(kFromPaired, old, 20 + i, i, pair.n, 0);
            CHECK(got == 40 + 5 * pair.want[i]);
            if (got != 40 + 5 * pair.want[i]) std::printf("  paired: %d old, new %d of %d: from old %d, wanted old %d\n", pair.m, i, pair.n, (got - 40) / 5, pair.want[i]);
        }
    }
    CHECK(glideFromKey(kFromPaired, four, 20, 0, 1, 0) == 67 && glideFromKey(kFromPaired, four, 20, 0, 0, 0) == 67); // one new note alone: from the top
    CHECK(glideFromKey(kFromPaired, four, 20, 9, 2, 0) == 67 && glideFromKey(kFromPaired, four, 20, -3, 2, 0) == 48); // (nonsense is held in)
    // NEAREST: the least far; a tie is the higher; its own key is the nearest of all.
    CHECK(glideFromKey(kFromNearest, four, 49, 0, 1, 0) == 48 && glideFromKey(kFromNearest, four, 30, 0, 1, 0) == 48 && glideFromKey(kFromNearest, four, 100, 0, 1, 0) == 67);
    CHECK(glideFromKey(kFromNearest, four, 58, 0, 1, 0) == 60 && glideFromKey(kFromNearest, four, 57, 0, 1, 0) == 55 && glideFromKey(kFromNearest, four, 55, 0, 1, 0) == 55);
    CHECK(glideFromKey(kFromNearest, two, 54, 0, 1, 0) == 60 && glideFromKey(kFromNearest, two, 53, 0, 1, 0) == 48 && glideFromKey(kFromNearest, two, 55, 0, 1, 0) == 60);
    CHECK(glideFromKey(kFromNearest, keysOf({0, 127}), 63, 0, 1, 0) == 0 && glideFromKey(kFromNearest, keysOf({0, 127}), 64, 0, 1, 0) == 127);
    // RANDOM: one of the keys, the same for the same seed and key, every key in its turn, each about as often.
    {
        int picked[128] = {};
        for (std::uint32_t at = 0; at < 4000; ++at) {
            const int key = glideFromKey(kFromRandom, four, 72, 0, 1, glideSeed(1, at));
            CHECK(four.has(key) && key == glideFromKey(kFromRandom, four, 72, 0, 1, glideSeed(1, at)));
            ++picked[key < 0 ? 0 : key];
        }
        for (int key : {48, 55, 60, 67}) CHECK(picked[key] > 850 && picked[key] < 1150);
        int byKey = 0, byTrack = 0, byStep = 0, byPart = 0;
        for (int at = 0; at < 200; ++at) {
            const int key = glideFromKey(kFromRandom, four, 72, 0, 1, glideSeed(1, at));
            byKey += key != glideFromKey(kFromRandom, four, 73, 0, 1, glideSeed(1, at));       // another note of the moment rolls for itself
            byTrack += key != glideFromKey(kFromRandom, four, 72, 0, 1, glideSeed(2, at));     // another track
            byStep += key != glideFromKey(kFromRandom, four, 72, 0, 1, glideSeed(1, at + 1));  // another time
            byPart += key != glideFromKey(kFromRandom, four, 72, 0, 1, glideSeed(1, at + 0.25)); // (the same whole step: the same roll)
        }
        CHECK(byKey > 100 && byTrack > 100 && byStep > 100 && byPart == 0);
        CHECK(glideSeed(1, 15.75) == glideSeed(1, 16.0) && glideSeed(1, 16.25) == glideSeed(1, 16.0) && glideSeed(1, 16.5) != glideSeed(1, 16.0)); // a nudged note rolls with its step
    }
    // The trail, PAIRED. Nothing told as coming: each note is alone when it begins, and joins what has begun.
    {
        GlideTrail trail;
        for (int pitch : {48, 55, 60, 67}) trail.began(pitch, 0.0);
        CHECK(trail.from(kFromPaired, 50, 4.0, 0).pitch == 67 && trail.from(kFromPaired, 50, 4.0, 0).at == 0.0); // alone: from the top
        trail.began(50, 4.0);
        CHECK(trail.from(kFromPaired, 70, 4.0, 0).pitch == 67 && trail.from(kFromPaired, 40, 4.0, 0).pitch == 48); // with 50 begun: the higher of two, the lower of two
        CHECK(trail.from(kFromPaired, 50, 4.0, 0).pitch == 67);                                                    // (its own key again is no other note)
        CHECK(trail.from(kFromNearest, 58, 4.0, 0).pitch == 60 && trail.from(kFromTop, 58, 4.0, 0).pitch == 67);
        CHECK(trail.from(kFromNearest, 58, 5.0, 0).pitch == 50 && trail.from(kFromPaired, 58, 5.0, 0).pitch == 50 && trail.from(kFromPaired, 58, 5.0, 0).at == 4.0);
    }
    // Told as coming: the first to begin knows the whole moment.
    {
        GlideTrail trail;
        for (int pitch : {48, 55, 60, 67}) trail.began(pitch, 0.0);
        trail.window(4.0);
        trail.tellComing(70, 4.0);
        trail.tellComing(50, 4.0);
        trail.tellComing(90, 4.5); // another moment of the same step: not counted with these
        trail.tellComing(91, 4.5);
        trail.tellComing(92, 4.5);
        CHECK(trail.comingCount == 5);
        CHECK(trail.from(kFromPaired, 50, 4.0, 0).pitch == 48 && trail.from(kFromPaired, 70, 4.0, 0).pitch == 67);
        trail.began(70, 4.0);
        CHECK(trail.from(kFromPaired, 50, 4.0, 0).pitch == 48 && trail.from(kFromPaired, 70, 4.0, 0).pitch == 67); // the same once one has begun
        trail.began(50, 4.0);
        // The next moment: three, from the two of the moment before (and not from the four before that).
        CHECK(trail.from(kFromPaired, 90, 4.5, 0).pitch == 50 && trail.from(kFromPaired, 91, 4.5, 0).pitch == 70 && trail.from(kFromPaired, 92, 4.5, 0).pitch == 70);
        CHECK(trail.from(kFromPaired, 90, 4.5, 0).at == 4.0);
        // A note that was not told, between the two moments: alone. One not told that begins with the three: one of four.
        CHECK(trail.from(kFromPaired, 40, 4.25, 0).pitch == 70);
        CHECK(trail.from(kFromPaired, 80, 4.5, 0).pitch == 50 && trail.from(kFromPaired, 99, 4.5, 0).pitch == 70 && trail.from(kFromPaired, 91, 4.5 + 1.0 / 512.0, 0).pitch == 70);
        // One of the three that begins a hair after the first of them, asked before any has begun: still one of three.
        CHECK(trail.from(kFromPaired, 90, 4.5 + 1.0 / 512.0, 0).pitch == 50 && trail.from(kFromPaired, 80, 4.5 + 1.0 / 512.0, 0).pitch == 50);
        // The next step: what was told before it is forgotten, what is told for later is kept.
        trail.tellComing(60, 5.0);
        trail.window(5.0);
        CHECK(trail.comingCount == 1 && trail.coming[0].pitch == 60 && trail.coming[0].at == 5.0);
        trail.clear();
        CHECK(trail.comingCount == 0);
    }
    // The most that can be told: 128, which is what a step can ask. One more is not kept, and
    // nothing is written past the end.
    {
        GlideTrail trail;
        trail.began(40, 0.0);
        trail.began(80, 0.0);
        for (int i = 0; i < kGlideComing + 40; ++i) trail.tellComing(i % 128, 4.0);
        CHECK(kGlideComing == 128 && trail.comingCount == kGlideComing);
        CHECK(trail.last.any && trail.last.at == 0.0 && trail.last.keys.count() == 2); // (what stands after the list in the trail is whole)
        CHECK(trail.from(kFromPaired, 0, 4.0, 0).pitch == 40 && trail.from(kFromPaired, 127, 4.0, 0).pitch == 80 && trail.from(kFromPaired, 63, 4.0, 0).pitch == 40 && trail.from(kFromPaired, 64, 4.0, 0).pitch == 80);
    }
    // A cell of more notes than can be told at once (two bars, four to a step: 128, and twice
    // round is 256): each step's are told when it comes and forgotten after, so the last are
    // paired as the first are.
    {
        std::vector<GlideNote> notes(2 * kGlideNotes);
        std::vector<GlideLine> lines(2 * kGlideNotes);
        for (int i = 0; i < 2 * kGlideNotes; ++i) {
            GlideNote& n = notes[static_cast<std::size_t>(i)];
            const int step = i / 4, key = i % 4;
            n.sounds = true, n.step = step % 16, n.glide = kGlideHalf;
            n.pitch = 40 + (step % 2) * 2 + key * 10; // 40 50 60 70, then 42 52 62 72, and so on
        }
        // (A bar's slots: the first bar holds steps 0..15, the second the same again.)
        glideLines(notes.data(), 2, 32, lines.data(), kFromPaired, 1);
        int wrong = 0;
        for (int i = 0; i < 2 * kGlideNotes; ++i) {
            const int step = i / 4, key = i % 4;
            wrong += lines[static_cast<std::size_t>(i)].from != 40 + ((step + 1) % 2) * 2 + key * 10; // each from the key in its own place, a step before
        }
        CHECK(wrong == 0);
        glideLines(notes.data(), 2, 32, lines.data(), kFromTop, 1);
        CHECK(lines[0].from == 72 && lines[2 * kGlideNotes - 1].from == 70);
    }
    // The roll's lines, by type: a chord of four, then two notes; then four notes after those two.
    {
        std::vector<GlideNote> notes(kGlideNotes);
        std::vector<GlideLine> lines(kGlideNotes);
        const auto put = [&](int slot, int step, int pitch, int glide, float nudge = 0.f) {
            GlideNote& n = notes[static_cast<std::size_t>(slot)];
            n.sounds = true, n.step = step, n.pitch = pitch, n.glide = glide, n.nudge = nudge;
        };
        const int chord[4] = {48, 55, 60, 67};
        for (int k = 0; k < 4; ++k) put(k, 0, chord[k], kGlideOff);
        put(4, 4, 62, kGlideFull);
        put(5, 4, 58, kGlideOne);
        put(6, 8, 59, kGlideFull);
        put(7, 8, 70, kGlideFull);
        put(8, 8, 50, kGlideFull, 0.002f); // (within a 256th: the same moment)
        put(9, 8, 61, kGlideFull);
        const int want[3][6] = {{67, 67, 62, 62, 62, 62}, {67, 48, 58, 62, 58, 62}, {60, 60, 58, 62, 58, 62}};
        for (int type = 0; type < 3; ++type) {
            glideLines(notes.data(), 1, 16, lines.data(), type, 3);
            for (int i = 0; i < 6; ++i) CHECK(lines[static_cast<std::size_t>(4 + i)].from == want[type][i]);
            CHECK(lines[4].steps == 4.0 && lines[5].steps == 1.0 && lines[6].steps == 4.0 && lines[8].steps == 4.0 + static_cast<double>(0.002f)); // FULL: since the moment it comes from began
            for (int k = 0; k < 4; ++k) CHECK(lines[static_cast<std::size_t>(k)].from == -1); // (the chord's keys do not glide)
        }
        // RANDOM: each from a key of the moment before, by the roll for the first time the scene plays (or another time round).
        for (int timesRound = 0; timesRound < 3; ++timesRound) {
            glideLines(notes.data(), 1, 16, lines.data(), kFromRandom, 3, timesRound);
            GlideKeys before, two;
            for (int key : chord) before.add(key);
            two.add(62), two.add(58);
            CHECK(lines[4].from == glideFromKey(kFromRandom, before, 62, 0, 1, glideSeed(3, 4.0 + 16.0 * timesRound)) && lines[5].from == glideFromKey(kFromRandom, before, 58, 0, 1, glideSeed(3, 4.0 + 16.0 * timesRound)));
            CHECK(lines[7].from == glideFromKey(kFromRandom, two, 70, 0, 1, glideSeed(3, 8.0 + 16.0 * timesRound)) && lines[4].steps == 4.0);
        }
        int differ = 0; // (and it is not the same key every time)
        for (int track = 0; track < 12; ++track) {
            glideLines(notes.data(), 1, 16, lines.data(), kFromRandom, track);
            differ += lines[4].from != 67;
        }
        CHECK(differ >= 3 && differ <= 11);
    }
}

// The rule by itself, where a made-up song would not say why.
static void testTheRule() {
    {
        // "Together" is 1/256 of a step, and exactly that far apart is still together.
        GlideTrail trail;
        trail.began(50, 0.0);
        trail.began(60, 2.0);
        const auto from = [&](double at) { return trail.from(kFromTop, 1, at, 0); };
        CHECK(from(2.0 + 1.0 / 256.0).pitch == 50 && from(2.0 + 1.0 / 256.0 + 1e-9).pitch == 60);
        CHECK(from(2.0 + 1.0 / 128.0).pitch == 60 && from(2.0 + 1.0 / 512.0).pitch == 50);
        trail.began(72, 2.0 + 1.0 / 256.0); // one moment with 60: the higher is what follows slides from
        CHECK(from(3.0).pitch == 72 && from(3.0).at == 2.0 && from(2.0).pitch == 50);
        trail.began(40, 2.0 + 1.0 / 200.0); // a moment of its own
        CHECK(from(3.0).pitch == 40 && from(2.0 + 1.0 / 200.0).pitch == 72);
        // Every key of a moment is kept, the lower ones too, and the moment before it.
        CHECK(trail.before.keys.count() == 2 && trail.before.keys.has(60) && trail.before.keys.has(72) && trail.before.at == 2.0);
        trail.clear();
        CHECK(!trail.last.any && !trail.before.any && trail.comingCount == 0 && from(9.0).pitch == -1);
    }
    testTheKeysAndThePicking();
    std::vector<GlideNote> notes(2 * kGlideNotes);
    std::vector<GlideLine> lines(2 * kGlideNotes);
    const auto put = [&](int bar, int slot, int step, int pitch, int glide, float nudge = 0.f, int ratchet = 0) {
        GlideNote& n = notes[static_cast<std::size_t>(bar * kGlideNotes + slot)];
        n.sounds = true, n.step = step, n.pitch = pitch, n.glide = glide, n.nudge = nudge, n.ratchet = ratchet;
    };
    // One note alone: it comes round to itself, and does not slide.
    put(0, 0, 4, 60, kGlideFull);
    glideLines(notes.data(), 1, 16, lines.data());
    CHECK(lines[0].from == -1);
    // Into step 1 from the loop's last note; and a note early on step 1 is the loop's last.
    put(0, 1, 0, 64, kGlideFull);
    put(0, 2, 12, 67, kGlideHalf);
    glideLines(notes.data(), 1, 16, lines.data());
    CHECK(lines[1].from == 67 && lines[1].steps == 4.0 && lines[0].from == 64 && lines[0].steps == 4.0 && lines[2].from == 60 && lines[2].steps == 0.5);
    put(0, 3, 0, 72, kGlideOff, -0.25f);
    glideLines(notes.data(), 1, 16, lines.data());
    CHECK(lines[1].from == 72 && lines[1].steps == 0.25 && lines[3].from == -1);
    // A scene cut to 12 steps: the note on step 13 is never played, and the loop is shorter.
    glideLines(notes.data(), 1, 12, lines.data());
    CHECK(lines[2].from == -1 && lines[1].from == 72 && lines[0].from == 64 && lines[0].steps == 4.0);
    notes[3] = GlideNote();
    glideLines(notes.data(), 1, 12, lines.data());
    CHECK(lines[1].from == 60 && lines[1].steps == 8.0);
    // Two bars: the second bar's first note comes from the first bar's last; a ratchet cuts the slide short.
    put(1, 5, 0, 50, kGlideTwo, 0.f, 2);
    glideLines(notes.data(), 2, 32, lines.data());
    CHECK(lines[kGlideNotes + 5].from == 67 && lines[kGlideNotes + 5].steps == 0.425 && lines[1].from == 50 && lines[1].steps == 16.0);
    // A cell of one bar in a scene of two: the bar plays twice, and is answered for the first time.
    glideLines(notes.data(), 1, 32, lines.data());
    CHECK(lines[1].from == 67 && lines[1].steps == 4.0);
    // Nonsense in: nothing out.
    glideLines(notes.data(), 0, 16, lines.data());
    glideLines(notes.data(), 1, 0, lines.data());
    CHECK(lines[1].from == -1 && lines[0].from == -1);
}

// ---- each synth takes the length it is given ------------------------------------------------

// The pitch over a stretch of sound: the shift at which it best matches itself. (Counting
// crossings cannot read Twin, with its two oscillators.)
static double pitchOver(const std::vector<float>& out, int from, int frames) {
    double best = -1e30;
    int bestLag = 1;
    for (int lag = 48; lag <= 600; ++lag) { // 1000 Hz down to 80
        double sum = 0.0;
        for (int i = from; i < from + frames; ++i) sum += static_cast<double>(out[2 * i]) * out[2 * (i + lag)];
        if (sum > best * 1.02 + 1e-12 || best < 0.0) { // (a longer shift must be better by more than a hair: two waves match as well as one)
            if (sum > best) {
                best = sum;
                bestLag = lag;
            }
        }
    }
    return 48000.0 / bestLag;
}

static void testEverySynthTakesTheLength() {
    for (SynthKind kind : {SynthKind::Twin, SynthKind::Qmod, SynthKind::Sampler, SynthKind::Phazed, SynthKind::Jx3p}) {
        const char* name = kind == SynthKind::Twin ? "twin" : kind == SynthKind::Qmod ? "qmod" : kind == SynthKind::Sampler ? "sampler" : kind == SynthKind::Jx3p ? "jx3p" : "phazed";
        // A quiet low note, then one an octave up four steps later. Its pitch from 0.55 to 0.95 of a
        // step into it, at 60 BPM (a step is a quarter of a second).
        const auto early = [&](int glide) {
            std::unique_ptr<Engine> engine(new Engine);
            CHECK(engine->addSynthTrack(1, kind));
            if (kind == SynthKind::Sampler) {
                std::unique_ptr<SampleData> tone(new SampleData); // 220 Hz, long
                tone->data.resize(192000);
                for (std::size_t i = 0; i < tone->data.size(); ++i) tone->data[i] = 0.5f * std::sin(6.2831853f * 220.f * static_cast<float>(i) / 48000.f);
                CHECK(engine->setTrackSample(1, std::move(tone)));
                engine->send(make(Command::SetSetting, 1, sampler::Loop, 1));
            }
            engine->send(make(Command::SetTempo, 0, 0, 0, 60.f));
            Command first = noteAt(0, 0, 57, 1.f);
            first.value = 0.02f;
            engine->send(first);
            engine->send(noteAt(1, 4, 69, 10.f, packed(0, glide)));
            engine->send(make(Command::Play, 0, 0, 1));
            std::vector<float> out(2 * 16 * 12000);
            engine->render(out.data(), 16 * 12000);
            return pitchOver(out, 4 * 12000 + 6600, 4800);
        };
        const double plain = early(kGlideOff), quarter = early(kGlideQuarter), half = early(kGlideHalf), one = early(kGlideOne), two = early(kGlideTwo), full = early(kGlideFull);
        // A quarter and a half step are over by then. One step, two and FULL (four) are still on
        // their way, each where a straight slide of that length would be (0.84, 0.65 and 0.57 of
        // the pitch, on average over the stretch).
        const bool ok = std::fabs(quarter - plain) < 0.02 * plain && std::fabs(half - plain) < 0.02 * plain && one < 0.9 * plain && one > 0.75 * plain && two < 0.7 * plain && two > 0.6 * plain && full < 0.61 * plain && full > 0.53 * plain;
        CHECK(ok);
        if (!ok || std::getenv("GLIDE_PRINT")) std::printf("  %s: plain %.0f Hz; 1/4 %.0f, 1/2 %.0f, 1 %.0f, 2 %.0f, FULL %.0f\n", name, plain, quarter, half, one, two, full);
    }
}

int main() {
    testTheLengths();
    testEachLengthIsTold();
    testAGlideOnARatchet();
    testFull();
    testWhichNoteItSlidesFrom();
    testEachTypeIsPlayed();
    testTypesWithARatchet();
    testTypesWithChords();
    testRandomIsRolledForItsPlace();
    testPairedAcrossAStepLine();
    testPairedWhereTheSceneGoesRound();
    testRandomOnALaterScene();
    testTypesAtAnyBlockLength();
    testTheRule();
    for (int type = 0; type < kGlideFroms; ++type) testTheRollsRuleIsTheEngines(type);
    testEverySynthTakesTheLength();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("glide tests passed\n");
    return 0;
}
