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
    CHECK(glideSteps(kGlideHalf, 9, 1) == 0.5); // a ratchet of one is no ratchet
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

// ---- the rule the roll draws from says what the engine does ------------------------------

static std::uint32_t seed = 0x1234567u;
static int roll(int outOf) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<int>((seed >> 12) % static_cast<std::uint32_t>(outOf));
}

static void testTheRollsRuleIsTheEngines() {
    int glidesSeen = 0, wraps = 0, together = 0, plainGlides = 0;
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
        for (int i = 0; i < count; ++i) {
            const int bar = roll(cellBars), slot = roll(kMaxNotes);
            GlideNote& n = notes[static_cast<std::size_t>(bar * kGlideNotes + slot)];
            if (n.sounds) continue;
            n.sounds = true;
            n.step = roll(4) == 0 ? roll(3) * 15 / 2 : roll(16); // (often step 1 or 16, where the loop goes round)
            n.pitch = 40 + roll(30);
            n.nudge = roll(3) == 0 ? static_cast<float>(roll(19) - 9) * 0.05f : 0.f;
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
        }
        std::vector<GlideLine> lines(notes.size());
        glideLines(notes.data(), cellBars, sceneSteps, lines.data());
        rig.play(3 * sceneSteps + 2);
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
                if (!same) std::printf("  song %d bar %d slot %d (step %d, key %d, nudge %.2f, x%d, glide %d): the engine from %d for %.4f s, the rule from %d for %.4f s\n", song, bar, slot, n.step, n.pitch, n.nudge, n.ratchet, n.glide, b->from, b->seconds, line.from, line.steps * kStepSeconds);
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
    CHECK(glidesSeen > 300 && wraps > 10 && together > 50);
    (void)plainGlides;
    if (failures) std::printf("  the rule and the engine: %d glides, %d across the loop's end, %d beginning with another note\n", glidesSeen, wraps, together);
}

// The rule by itself, where a made-up song would not say why.
static void testTheRule() {
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
    for (SynthKind kind : {SynthKind::Twin, SynthKind::Qmod, SynthKind::Sampler, SynthKind::Phazed}) {
        const char* name = kind == SynthKind::Twin ? "twin" : kind == SynthKind::Qmod ? "qmod" : kind == SynthKind::Sampler ? "sampler" : "phazed";
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
    testTheRule();
    testTheRollsRuleIsTheEngines();
    testEverySynthTakesTheLength();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("glide tests passed\n");
    return 0;
}
