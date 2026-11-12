// THROW: a drum hit or a note sent in full to a send bus, by itself.
//
// Nobody listens to these: they are measured. A probe stands on each bus and
// writes down what it is given, and that is compared, to the sample, with what
// the thrown hit sounds like alone.
//
//   1. the drum machine's second way out (a thrown hit, and only it)
//   2. a thrown hit in the engine: among others, at the track's level, not from a muted track
//   3. a thrown note: the track's send held open for the note's length, 5 ms ramps
//   4. Stop and panic leave no send open
//   5. the commands that carry it
//   6. a song with throws, the same to the last bit on 0 to 3 helpers

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
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

static const double kPi = 3.141592653589793;
static const int kStep = 6000;  // a step at 120 BPM, 48 kHz
static const int kGlide = 240;  // 5 ms: a send from shut to open
static const float kLevel = 0.65f; // a new track's level

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}

static float peakOf(const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    float peak = 0.f;
    for (std::size_t i = from; i < v.size() && i < to; ++i) peak = std::fabs(v[i]) > peak ? std::fabs(v[i]) : peak;
    return peak;
}
static float largestJump(const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    float jump = 0.f;
    for (std::size_t i = from + 1; i < v.size() && i < to; ++i) jump = std::fabs(v[i] - v[i - 1]) > jump ? std::fabs(v[i] - v[i - 1]) : jump;
    return jump;
}

// Stands on a bus and writes down what it is given. It gives nothing back,
// so the engine's output is the song without the bus.
struct Probe : Effect {
    std::vector<float> fed, fedRight;
    int runs = 0, resets = 0;
    Probe() {
        fed.reserve(48000 * 30); // room up front: process() must not allocate
        fedRight.reserve(48000 * 30);
    }
    void reset() override { ++resets; }
    void setParam(int, float) override {}
    void process(float* left, float* right, int frames) override {
        ++runs;
        for (int i = 0; i < frames; ++i) {
            if (fed.size() < fed.capacity()) {
                fed.push_back(left[i]);
                fedRight.push_back(right[i]);
            }
            left[i] = right[i] = 0.f;
        }
    }
};
// A steady 100 Hz sine in place of whatever the track's instrument made.
struct SineSource : Effect {
    double phase = 0.0;
    void setParam(int, float) override {}
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) {
            left[i] = static_cast<float>(0.5 * std::sin(phase));
            right[i] = 0.5f * left[i]; // half as loud on the right, so the two sides can be told apart
            phase += 2.0 * kPi * 100.0 / 48000.0;
        }
    }
};
struct Halve : Effect {
    void setParam(int, float) override {}
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) {
            left[i] *= 0.5f;
            right[i] *= 0.5f;
        }
    }
};

// An engine with a probe on each bus, and a count of the frames it has rendered.
struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Probe* echo = new Probe;
    Probe* space = new Probe;
    std::vector<float> out; // the left side of everything rendered
    Rig() {
        engine->setBusEffect(0, std::unique_ptr<Effect>(echo));
        engine->setBusEffect(1, std::unique_ptr<Effect>(space));
    }
    void send(const Command& c) { CHECK(engine->send(c)); }
    void render(int frames, int block = 256) {
        std::vector<float> both(static_cast<std::size_t>(2 * block));
        while (frames > 0) {
            const int n = frames < block ? frames : block;
            engine->render(both.data(), n);
            for (int i = 0; i < n; ++i) out.push_back(both[static_cast<std::size_t>(2 * i)]);
            frames -= n;
        }
    }
    // What a bus was given on frame `i` of the render. A bus at rest is not run, so its probe
    // starts writing when it wakes: that frame is `woke(bus)`.
    long woke(const Probe* p) const { return static_cast<long>(out.size()) - static_cast<long>(p->fed.size()); }
    float fedAt(const Probe* p, long i) const {
        const long at = i - woke(p);
        return at < 0 || at >= static_cast<long>(p->fed.size()) ? 0.f : p->fed[static_cast<std::size_t>(at)];
    }
    float fedRightAt(const Probe* p, long i) const {
        const long at = i - woke(p);
        return at < 0 || at >= static_cast<long>(p->fedRight.size()) ? 0.f : p->fedRight[static_cast<std::size_t>(at)];
    }
    void hit(int lane, int step, float level = 0.9f, int throwTo = 0, int ratchet = 0, int cut = 0, int track = 0) {
        send(make(Command::SetStep, track, lane, step, level));
        StepFx fx;
        fx.throwTo = static_cast<std::uint8_t>(throwTo);
        fx.ratchet = static_cast<std::uint8_t>(ratchet);
        fx.cut = static_cast<std::uint8_t>(cut);
        Command c = make(Command::SetStepFx, track, lane, step, 0.f);
        c.d = fx.ratchet;
        c.extra = fx.cutAndThrow();
        send(c);
    }
    void note(int track, int slot, int step, int pitch, int length, int throwTo, int ratchet = 0, float nudge = 0.f) {
        Command c = make(Command::SetNote, track, slot, step, 0.8f);
        c.c = pitch;
        c.d = length;
        c.shift = nudge;
        c.e = ratchet + (throwTo << 9);
        send(c);
    }
    // A synth track whose sound is a steady sine: what its send is doing can then be read off the bus.
    void sineSynth(int track) {
        CHECK(engine->addSynthTrack(track, SynthKind::Phazed));
        engine->setTrackEffect(track, 0, std::unique_ptr<Effect>(new SineSource));
    }
    void play() { send(make(Command::Play, 0, 0, 1)); }
};

// ---- 1. the drum machine's second way out ------------------------------------------------------

struct Machine {
    DrumMachine drums;
    std::vector<float> out, echo, space;
    std::vector<int> wrote; // per block
    Machine() {
        drums.setKit(&DrumKit::builtIn(48000.f));
        drums.prepare(48000.f);
    }
    void render(int frames, int block = 64, bool withBuses = true) {
        std::vector<float> l(static_cast<std::size_t>(block)), r(l), e(l), s(l);
        while (frames > 0) {
            const int n = frames < block ? frames : block;
            for (int i = 0; i < n; ++i) e[static_cast<std::size_t>(i)] = s[static_cast<std::size_t>(i)] = 7.f; // what was there before
            const int mask = withBuses ? drums.render(l.data(), r.data(), n, e.data(), s.data()) : drums.render(l.data(), r.data(), n);
            wrote.push_back(mask);
            for (int i = 0; i < n; ++i) {
                const std::size_t k = static_cast<std::size_t>(i);
                CHECK(l[k] == r[k]);
                out.push_back(l[k]);
                // A way out that was not written is left as it was found; here it counts as silence.
                if (!(mask & 1)) CHECK(e[k] == 7.f);
                if (!(mask & 2)) CHECK(s[k] == 7.f);
                echo.push_back(mask & 1 ? e[k] : 0.f);
                space.push_back(mask & 2 ? s[k] : 0.f);
            }
            frames -= n;
        }
    }
};
static HitPlay thrown(int to, float rate = 1.f) {
    HitPlay how;
    how.throwTo = to;
    how.rate = rate;
    return how;
}

static void testDrumMachine() {
    CHECK(kThrowEcho == 1 && kThrowSpace == 2 && kThrowChoices == 4);
    // Not thrown: nothing is written to either way out.
    {
        Machine m;
        m.drums.trigger(1, 0.9f);
        m.render(4800);
        CHECK(peakOf(m.out) > 0.5f && peakOf(m.echo) == 0.f && peakOf(m.space) == 0.f);
        for (int mask : m.wrote) CHECK(mask == 0);
    }
    // Thrown to ECHO, to SPACE, to both: the hit, to the sample, where it was thrown and nowhere else.
    for (int to = 1; to <= 3; ++to) {
        Machine m;
        m.drums.trigger(1, 0.9f, 0.f, -1.f, thrown(to));
        m.render(20000);
        CHECK(peakOf(m.out) > 0.5f);
        CHECK(to & 1 ? m.echo == m.out : peakOf(m.echo) == 0.f);
        CHECK(to & 2 ? m.space == m.out : peakOf(m.space) == 0.f);
        CHECK(m.wrote.front() == to);
        CHECK(m.wrote.back() == 0); // the sound is over: nothing more is written
    }
    // Asked for without the two ways out: it plays as any hit, and nothing is touched.
    {
        Machine with, without;
        with.drums.trigger(1, 0.9f, 0.f, -1.f, thrown(3));
        without.drums.trigger(1, 0.9f, 0.f, -1.f, thrown(3));
        with.render(4800);
        without.render(4800, 64, false);
        CHECK(with.out == without.out);
    }
    // Given only one of the two ways out, a hit thrown to both is written to that one.
    {
        DrumMachine drums;
        drums.setKit(&DrumKit::builtIn(48000.f));
        drums.prepare(48000.f);
        drums.trigger(1, 0.9f, 0.f, -1.f, thrown(3));
        float l[64], r[64], e[64];
        CHECK(drums.render(l, r, 64, e, nullptr) == 1);
        bool same = true;
        for (int i = 0; i < 64; ++i) same = same && e[i] == l[i];
        CHECK(same && l[20] != 0.f);
        CHECK(drums.render(l, r, 64, nullptr, e) == 2);
    }
    // One thrown hit among others: other lanes at the same moment, and the same lane before and
    // after. The way out holds that hit as it sounds by itself (cut short by the next, as it is),
    // and the sound itself is as it was without any THROW, to the bit.
    {
        Machine all, alone, plain;
        const auto play = [](Machine& m, bool others, int to) {
            if (others) {
                m.drums.trigger(0, 1.f);
                m.drums.trigger(1, 0.8f); // the same lane, a step before
                m.drums.trigger(3, 0.7f);
            }
            m.render(kStep);
            if (others) m.drums.trigger(0, 1.f);
            if (others) m.drums.trigger(2, 0.6f);
            m.drums.trigger(1, 0.9f, 0.f, -1.f, thrown(to)); // THE hit
            m.render(kStep);
            m.drums.trigger(1, 0.5f); // the same lane, a step after: it cuts the thrown one off
            if (others) m.drums.trigger(7, 0.5f);
            m.render(3 * kStep);
        };
        play(all, true, kThrowEcho);
        play(alone, false, kThrowEcho);
        play(plain, true, 0);
        CHECK(all.out == plain.out && all.out != alone.out);
        CHECK(all.echo == alone.echo && peakOf(all.space) == 0.f);
        CHECK(peakOf(all.echo, 0, kStep) == 0.f);                         // nothing from the step before
        CHECK(peakOf(all.echo, kStep, 2 * kStep) > 0.5f);                 // the hit
        CHECK(peakOf(all.echo, 2 * kStep, 2 * kStep + 144) > 0.f);        // its 3 ms fade under the next hit...
        CHECK(peakOf(all.echo, 2 * kStep + 145) == 0.f);                  // ...and nothing of that next hit
        for (std::size_t i = kStep; i < 2 * kStep; ++i)
            if (all.echo[i] != alone.out[i]) { CHECK(!"the thrown hit is not the hit"); break; }
    }
    // A hit that starts part-way into its sound comes up over a millisecond, on both ways out alike.
    {
        Machine m;
        m.drums.trigger(7, 0.9f, 0.3f, -1.f, thrown(kThrowEcho));
        m.render(4800);
        CHECK(m.echo == m.out && peakOf(m.out) > 0.05f && std::fabs(m.out[2]) < std::fabs(m.out[40]));
    }
    // A lane's level and a hit's pitch go with it.
    {
        Machine m;
        m.drums.setLaneLevel(1, 0.25f);
        m.drums.trigger(1, 0.9f, 0.f, -1.f, thrown(kThrowSpace, 0.5f));
        m.render(9600);
        CHECK(m.space == m.out && peakOf(m.out) > 0.1f && peakOf(m.out) < 0.26f);
    }
    // A hit that carries the lane's sound on (slices in order) hands on its own THROW: the sound
    // slides into the bus over a millisecond, and out of it again, with no step. The sound
    // itself does not change.
    {
        std::vector<float> wave(48000);
        for (std::size_t i = 0; i < wave.size(); ++i) wave[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 100.0 * static_cast<double>(i) / 48000.0));
        Machine m, plain;
        for (Machine* which : {&m, &plain}) {
            which->drums.setLaneSample(0, wave.data(), 48000);
            const int to = which == &m ? kThrowEcho : 0;
            which->drums.trigger(0, 1.f, 0.25f, 0.25f);
            which->render(12000);
            which->drums.trigger(0, 1.f, 0.5f, 0.25f, thrown(to)); // where the sound has just got to: carried on
            which->render(12000);
            which->drums.trigger(0, 1.f, 0.75f, 0.2f); // and on again, not thrown
            which->render(12000);
        }
        CHECK(m.out == plain.out && peakOf(m.out, 11900, 24100) > 0.49f);
        CHECK(peakOf(m.echo, 0, 12000) == 0.f);
        bool rises = true, full = true, falls = true;
        for (int i = 0; i < 48; ++i) rises = rises && std::fabs(m.echo[12000 + i] - m.out[12000 + i] * static_cast<float>(i) / 48.f) < 1e-5f;
        for (int i = 50; i < 12000; ++i) full = full && m.echo[12000 + i] == m.out[12000 + i];
        for (int i = 0; i < 48; ++i) falls = falls && std::fabs(m.echo[24000 + i] - m.out[24000 + i] * (1.f - static_cast<float>(i) / 48.f)) < 1e-5f;
        CHECK(rises);
        CHECK(full);
        CHECK(falls);
        CHECK(peakOf(m.echo, 24050) == 0.f && m.wrote.back() == 0); // (a sample or two late: 48 steps of 1/48 fall a hair short)
        const float sine = static_cast<float>(0.5 * 2.0 * std::sin(kPi * 100.0 / 48000.0)); // the wave's own largest step
        CHECK(largestJump(m.echo) < sine * 2.6f); // a 1 ms ramp on a 100 Hz wave: 0.5 / 48 a sample at most, on top of the wave's own
    }
    // reset() ends a thrown hit too.
    {
        Machine m;
        m.drums.trigger(7, 1.f, 0.f, -1.f, thrown(3));
        m.render(640);
        m.drums.reset();
        m.render(640);
        CHECK(peakOf(m.echo, 0, 640) > 0.f && peakOf(m.echo, 640) == 0.f && m.wrote.back() == 0);
    }
}

// ---- 2. a thrown hit in the engine ------------------------------------------------------------

// What one snare on step 4 sounds like by itself (cut off by one on step 5 if `next`).
static std::vector<float> snareAlone(bool next, float level = 0.9f) {
    Rig rig;
    rig.hit(1, 4, level);
    if (next) rig.hit(1, 5, 0.5f);
    rig.play();
    rig.render(8 * kStep);
    return rig.out;
}

static void testThrownHit() {
    const std::vector<float> alone = snareAlone(false);
    // Among others. Kick and hat around it, the same lane a step before and a step after, and
    // the track's own slot halving the sound: the bus is given the hit, whole and by itself.
    {
        Rig rig;
        rig.engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(new Halve));
        for (int s = 0; s < kSteps; s += 4) rig.hit(0, s, 1.f);
        for (int s = 0; s < kSteps; ++s) rig.hit(2, s, 0.6f);
        rig.hit(1, 3, 0.8f);
        rig.hit(1, 4, 0.9f, kThrowEcho);
        rig.hit(1, 5, 0.5f);
        rig.play();
        CHECK(!rig.engine->busAwake(0));
        rig.render(8 * kStep, 301);
        CHECK(rig.woke(rig.echo) == 4 * kStep); // it woke the resting bus, on the hit's own sample
        CHECK(rig.engine->busAwake(0) && !rig.engine->busAwake(1) && rig.space->runs == 0);
        const std::vector<float> cutShort = snareAlone(true);
        bool same = true, right = true;
        for (long i = 0; i < 8 * kStep; ++i) {
            // To the end of its step it is the hit alone; then its 3 ms fade under the next hit, and nothing.
            const float want = i < 5 * kStep ? alone[static_cast<std::size_t>(i)] : (i < 5 * kStep + 146 ? rig.fedAt(rig.echo, i) : 0.f);
            if (same && rig.fedAt(rig.echo, i) != want) std::printf("  frame %ld: fed %g, wanted %g\n", i, rig.fedAt(rig.echo, i), want);
            same = same && rig.fedAt(rig.echo, i) == want;
        }
        for (std::size_t i = 0; i < rig.echo->fed.size(); ++i) right = right && rig.echo->fed[i] == rig.echo->fedRight[i];
        CHECK(same && right);
        CHECK(peakOf(alone, 4 * kStep, 5 * kStep) > 0.4f && peakOf(rig.echo->fed) == peakOf(alone, 4 * kStep, 5 * kStep));
        CHECK(std::fabs(rig.fedAt(rig.echo, 5 * kStep + 72)) > 0.f); // (the fade is there)
        (void)cutShort;
    }
    // SPACE, and BOTH.
    for (int to = 2; to <= 3; ++to) {
        Rig rig;
        rig.hit(0, 0, 1.f);
        rig.hit(1, 4, 0.9f, to);
        rig.play();
        rig.render(8 * kStep);
        bool same = true;
        for (long i = 0; i < 8 * kStep; ++i) {
            same = same && rig.fedAt(rig.space, i) == alone[static_cast<std::size_t>(i)];
            if (to == 3) same = same && rig.fedAt(rig.echo, i) == alone[static_cast<std::size_t>(i)];
        }
        CHECK(same && (to == 3 || rig.echo->runs == 0));
    }
    // At the track's level.
    {
        Rig rig;
        rig.send(make(Command::SetTrackLevel, 0, 0, 0, 0.2f));
        rig.hit(1, 4, 0.9f, kThrowEcho);
        rig.play();
        rig.render(8 * kStep);
        CHECK(std::fabs(peakOf(rig.echo->fed) - peakOf(alone) * 0.2f / kLevel) < 1e-6f);
    }
    // A muted track throws nothing, and wakes nothing.
    {
        Rig rig;
        rig.send(make(Command::SetTrackHeard, 0, 0));
        rig.hit(1, 4, 0.9f, 3);
        rig.play();
        rig.render(8 * kStep);
        CHECK(rig.echo->runs == 0 && rig.space->runs == 0 && !rig.engine->busAwake(0) && !rig.engine->busAwake(1) && peakOf(rig.out) == 0.f);
    }
    // Muted while a thrown hit rings: the bus's share fades with the track's (5 ms), to nothing.
    {
        Rig rig;
        rig.hit(7, 0, 1.f, kThrowEcho);
        rig.play();
        rig.render(4800);
        rig.send(make(Command::SetTrackHeard, 0, 0));
        rig.render(4800);
        CHECK(peakOf(rig.echo->fed, 4000, 4800) > 0.05f && peakOf(rig.echo->fed, 4800 + kGlide + 1) == 0.f);
        CHECK(std::fabs(rig.echo->fed[4800 + 120]) > 0.f);
    }
    // With the track's send up as well: "in full" is a send of 1, not the knob's share on top
    // of it. At half, the bus has half the track and the hit's other half.
    {
        Rig rig;
        rig.send(make(Command::SetSend, 0, 0, 0, 0.5f));
        rig.render(1024); // the send has got to its half
        rig.hit(1, 4, 0.9f, kThrowEcho);
        rig.play();
        rig.render(8 * kStep);
        bool whole = true;
        for (long i = 0; i < 8 * kStep; ++i) whole = whole && std::fabs(rig.fedAt(rig.echo, 1024 + i) - alone[static_cast<std::size_t>(i)]) < 1e-6f;
        CHECK(whole && peakOf(rig.echo->fed) > 0.4f);
    }
    // Every piece of a ratchet is thrown (they are all the one hit), and a cut hit is thrown cut.
    {
        Rig rig, plain;
        rig.hit(1, 4, 0.6f, kThrowEcho, 3, 2); // (quieter: two pieces overlap for 3 ms, and must stay under the output's soft ceiling)
        plain.hit(1, 4, 0.6f, 0, 3, 2);
        rig.play();
        plain.play();
        rig.render(8 * kStep);
        plain.render(8 * kStep);
        bool same = true;
        for (long i = 0; i < 8 * kStep; ++i) {
            if (same && rig.fedAt(rig.echo, i) != plain.out[static_cast<std::size_t>(i)]) std::printf("  frame %ld: fed %g, wanted %g\n", i, rig.fedAt(rig.echo, i), plain.out[static_cast<std::size_t>(i)]);
            same = same && rig.fedAt(rig.echo, i) == plain.out[static_cast<std::size_t>(i)];
        }
        CHECK(same);
        CHECK(rig.out == plain.out && plain.echo->runs == 0);
        for (int piece = 0; piece < 3; ++piece) CHECK(peakOf(plain.out, 4 * kStep + piece * 2000, 4 * kStep + piece * 2000 + 500) > 0.2f);
    }
    // Heard by hand (X on a hit): the hit as the pattern would play it, thrown too.
    {
        Rig rig;
        Command c = make(Command::Trigger, 0, 1, 0, 0.9f);
        StepFx fx;
        fx.cut = 2;
        fx.throwTo = kThrowSpace;
        c.extra = fx.cutAndThrow();
        rig.send(c);
        rig.render(2 * kStep);
        CHECK(rig.echo->runs == 0 && peakOf(rig.space->fed) > 0.4f);
        CHECK(peakOf(rig.space->fed, kStep / 2 + 60) == 0.f); // cut after half a step
    }
}

// ---- 3. a thrown note --------------------------------------------------------------------------

// The sine a synth track gives, at its level, with no bus in the way.
static std::vector<float> plainSine(int frames) {
    Rig rig;
    rig.sineSynth(1);
    rig.render(frames);
    return rig.out;
}

// Is what the bus was given the track's sound times a send that opens at `from` and shuts at
// `to` (frames), each over 5 ms, sitting at `base` otherwise?
static bool gated(const Rig& rig, const Probe* p, const std::vector<float>& plain, long from, long to, float base = 0.f, long start = 0) {
    for (long i = start; i < static_cast<long>(rig.out.size()); ++i) {
        float open = 0.f;
        if (i >= from && i < to) open = i - from + 1 >= kGlide ? 1.f : static_cast<float>(i - from + 1) / kGlide;
        else if (i >= to) open = i - to + 1 >= kGlide ? 0.f : 1.f - static_cast<float>(i - to + 1) / kGlide;
        const float want = plain[static_cast<std::size_t>(i)] * (base + open * (1.f - base));
        // (Up to one sample late at the end of a ramp: 240 steps of 1/240 fall a hair short of 1.)
        const bool steady = (open == 0.f || open == 1.f) && i != from + kGlide - 1 && i != to + kGlide - 1 && i != from + kGlide && i != to + kGlide;
        if (steady ? rig.fedAt(p, i) != want : std::fabs(rig.fedAt(p, i) - want) > 2e-5f) {
            std::printf("  frame %ld: fed %g, wanted %g\n", i, rig.fedAt(p, i), want);
            return false;
        }
        // The right side goes the same way: the sine is half as loud there, and half is exact.
        if (rig.fedRightAt(p, i) != 0.5f * rig.fedAt(p, i)) {
            std::printf("  frame %ld: fed %g on the right, wanted %g\n", i, rig.fedRightAt(p, i), 0.5f * rig.fedAt(p, i));
            return false;
        }
    }
    return true;
}

static void testThrownNote() {
    const std::vector<float> plain = plainSine(16 * kStep);
    CHECK(std::fabs(peakOf(plain) - 0.5f * kLevel) < 1e-4f);
    // A note of two steps on step 2: the send opens with it, is full for its length, and shuts.
    for (int to = 1; to <= 3; ++to) {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 2, 60, 2, to);
        rig.play();
        rig.render(8 * kStep, 173);
        if (to & 1) CHECK(rig.woke(rig.echo) == 2 * kStep && gated(rig, rig.echo, plain, 2 * kStep, 4 * kStep));
        else CHECK(rig.echo->runs == 0);
        if (to & 2) CHECK(rig.woke(rig.space) == 2 * kStep && gated(rig, rig.space, plain, 2 * kStep, 4 * kStep));
        else CHECK(rig.space->runs == 0);
    }
    // Not thrown: the bus is never touched.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 2, 60, 2, 0, 3);
        rig.play();
        rig.render(8 * kStep);
        CHECK(rig.echo->runs == 0 && rig.space->runs == 0 && rig.out == std::vector<float>(plain.begin(), plain.begin() + 8 * kStep));
    }
    // It does not click: the ramps are no steeper than 1/240 of the sine's height a sample.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 2, 60, 2, kThrowEcho);
        rig.play();
        rig.render(8 * kStep);
        const float sine = static_cast<float>(0.5 * kLevel * 2.0 * std::sin(kPi * 100.0 / 48000.0));
        CHECK(largestJump(rig.echo->fed) < sine + 0.5f * kLevel / kGlide + 1e-5f);
    }
    // A ratchet's pieces are one note: the send stays open through the gaps between them.
    for (int ratchet = 2; ratchet <= 4; ++ratchet) {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 2, 60, 2, kThrowEcho, ratchet);
        rig.play();
        rig.render(8 * kStep);
        CHECK(gated(rig, rig.echo, plain, 2 * kStep, 4 * kStep));
    }
    // A note pushed late opens its send late, when it starts; one pushed early, early.
    {
        Rig late, early;
        late.sineSynth(1);
        late.note(1, 0, 2, 60, 2, kThrowEcho, 3, 0.25f);
        late.play();
        late.render(8 * kStep);
        CHECK(gated(late, late.echo, plain, 2 * kStep + kStep / 4, 4 * kStep + kStep / 4));
        early.sineSynth(1);
        early.note(1, 0, 2, 60, 2, kThrowSpace, 0, -0.25f);
        early.play();
        early.render(8 * kStep);
        CHECK(gated(early, early.space, plain, 2 * kStep - kStep / 4, 4 * kStep - kStep / 4));
    }
    // Two thrown notes that overlap: open from the first's start to the last one's end. And a
    // short one inside a long one does not shut the long one's send.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 2, 60, 4, kThrowEcho);
        rig.note(1, 1, 3, 64, 1, kThrowEcho);
        rig.note(1, 2, 5, 67, 3, kThrowEcho);
        rig.play();
        rig.render(12 * kStep);
        CHECK(gated(rig, rig.echo, plain, 2 * kStep, 8 * kStep));
    }
    // With the send knob at a quarter: up from the quarter to full and back to it. Never more than full.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.send(make(Command::SetSend, 1, 0, 0, 0.25f));
        rig.render(1024);
        rig.note(1, 0, 2, 60, 2, kThrowEcho);
        rig.play();
        rig.render(8 * kStep);
        CHECK(gated(rig, rig.echo, plain, 1024 + 2 * kStep, 1024 + 4 * kStep, 0.25f, 1024));
    }
    // The knob turned as a thrown note starts: the knob's own 5 ms glide goes on under the THROW,
    // and when the note is over the send is where the knob was put, exactly.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 0, 60, 2, kThrowEcho);
        rig.send(make(Command::SetSend, 1, 0, 0, 0.5f));
        rig.play();
        rig.render(8 * kStep, 64);
        bool rises = true, lands = true;
        for (int i = 0; i < kGlide - 1; ++i) {
            const float now = i + 1 >= kGlide / 2 ? 0.5f : static_cast<float>(i + 1) / kGlide, open = static_cast<float>(i + 1) / kGlide;
            rises = rises && std::fabs(rig.fedAt(rig.echo, i) - plain[static_cast<std::size_t>(i)] * (now + open * (1.f - now))) < 2e-5f;
        }
        for (long i = 2 * kStep + kGlide + 1; i < 8 * kStep; ++i) lands = lands && rig.fedAt(rig.echo, i) == plain[static_cast<std::size_t>(i)] * 0.5f;
        CHECK(rises);
        CHECK(lands);
    }
    // At full already, a THROW changes nothing.
    {
        Rig rig, same;
        for (Rig* r : {&rig, &same}) {
            r->sineSynth(1);
            r->send(make(Command::SetSend, 1, 0, 0, 1.f));
            r->render(1024);
            r->note(1, 0, 2, 60, 2, r == &rig ? kThrowEcho : 0);
            r->play();
            r->render(8 * kStep);
        }
        CHECK(rig.echo->fed == same.echo->fed && peakOf(rig.echo->fed) > 0.3f);
    }
    // A muted track's thrown note sends nothing.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.send(make(Command::SetTrackHeard, 1, 0));
        rig.render(1024);
        rig.note(1, 0, 2, 60, 2, 3);
        rig.play();
        rig.render(8 * kStep);
        CHECK(rig.echo->runs == 0 && rig.space->runs == 0);
    }
    // It goes round with the loop: the second pass opens the send again, the same.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 14, 60, 1, kThrowEcho);
        rig.play();
        rig.render(32 * kStep);
        CHECK(peakOf(rig.echo->fed, 0, kStep) > 0.3f);
        const long second = 30 * kStep - rig.woke(rig.echo);
        CHECK(second > 0 && peakOf(rig.echo->fed, static_cast<std::size_t>(second - 6 * kStep), static_cast<std::size_t>(second)) == 0.f);
        CHECK(peakOf(rig.echo->fed, static_cast<std::size_t>(second), static_cast<std::size_t>(second + kStep)) > 0.3f);
    }
}

// ---- 4. Stop and panic leave no send open --------------------------------------------------------

static void testStopAndPanic() {
    const std::vector<float> plain = plainSine(16 * kStep);
    // Stop, part way through a thrown note: the send shuts from there, in 5 ms, and stays shut.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 2, 60, 8, 3);
        rig.play();
        rig.render(4 * kStep);
        rig.send(make(Command::Stop));
        rig.render(4 * kStep);
        CHECK(gated(rig, rig.echo, plain, 2 * kStep, 4 * kStep) && gated(rig, rig.space, plain, 2 * kStep, 4 * kStep));
        // Played again with the note taken out: nothing is sent.
        const std::size_t before = rig.echo->fed.size();
        rig.note(1, 0, 2, 60, 0, 0);
        Command none = make(Command::SetNote, 1, 0, 2, 0.f);
        rig.send(none);
        rig.play();
        rig.render(8 * kStep);
        CHECK(peakOf(rig.echo->fed, before) == 0.f);
    }
    // Play, part way through one: the song starts again from the top, with its sends shut.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 2, 60, 8, kThrowEcho);
        rig.play();
        rig.render(4 * kStep);
        rig.play();
        rig.render(kStep);
        CHECK(gated(rig, rig.echo, plain, 2 * kStep, 4 * kStep));
    }
    // Panic: the output shuts, and behind it the send is shut at once. Let go, nothing is sent.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 2, 60, 8, kThrowEcho);
        rig.hit(7, 2, 1.f, kThrowSpace);
        rig.play();
        rig.render(4 * kStep);
        CHECK(peakOf(rig.echo->fed) > 0.3f && peakOf(rig.space->fed) > 0.1f);
        rig.send(make(Command::Panic, 0, 1));
        rig.render(480);
        const std::size_t echoAt = rig.echo->fed.size(), spaceAt = rig.space->fed.size();
        rig.send(make(Command::Panic, 0, 0));
        rig.render(4 * kStep);
        CHECK(peakOf(rig.echo->fed, echoAt) == 0.f && peakOf(rig.space->fed, spaceAt) == 0.f);
    }
    // A track taken away while its thrown note sounds, and another put there: its send is as set (shut).
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 0, 60, 16, kThrowEcho);
        rig.play();
        rig.render(2 * kStep);
        CHECK(rig.engine->removeTrack(1));
        rig.render(64);
        rig.engine->collectGarbage();
        CHECK(rig.engine->addSynthTrack(1, SynthKind::Phazed));
        const std::size_t at = rig.echo->fed.size();
        rig.render(2 * kStep);
        CHECK(peakOf(rig.echo->fed, at) == 0.f);
    }
    // prepare() shuts them too.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.note(1, 0, 0, 60, 16, kThrowEcho);
        rig.play();
        rig.render(2 * kStep);
        rig.engine->prepare(48000.f);
        const std::size_t at = rig.echo->fed.size();
        rig.render(2 * kStep);
        CHECK(peakOf(rig.echo->fed, at) == 0.f);
    }
}

// ---- 5. the commands that carry it ---------------------------------------------------------------

static void testCommands() {
    // The cut and the THROW ride in one number, and neither disturbs the other.
    for (int cut = 0; cut < kCutChoices; ++cut) {
        for (int to = 0; to < kThrowChoices; ++to) {
            StepFx fx;
            fx.cut = static_cast<std::uint8_t>(cut);
            fx.throwTo = static_cast<std::uint8_t>(to);
            CHECK(fx.cutAndThrow() == static_cast<float>(cut + 4 * to));
            CHECK(fx.any() == (cut != 0 || to != 0));
            Rig rig;
            rig.hit(1, 0, 0.9f, to, 0, cut);
            rig.play();
            rig.render(4 * kStep);
            CHECK((rig.echo->runs > 0) == ((to & 1) != 0) && (rig.space->runs > 0) == ((to & 2) != 0));
            // CUT 1, 2, 3: a step, a half, a quarter.
            const int life = cut == 1 ? kStep : cut == 2 ? kStep / 2 : cut == 3 ? kStep / 4 : 0;
            if (life) CHECK(peakOf(rig.out, static_cast<std::size_t>(life - 100), static_cast<std::size_t>(life)) > 0.f && peakOf(rig.out, static_cast<std::size_t>(life + 60)) == 0.f);
            else CHECK(peakOf(rig.out, kStep + 60) > 0.f);
        }
    }
    // Nonsense in that number: neither a cut nor a THROW.
    for (float bad : {-1.f, 16.f, 21.f, 1e9f, std::nanf("")}) {
        Rig rig;
        rig.send(make(Command::SetStep, 0, 1, 0, 0.9f));
        Command c = make(Command::SetStepFx, 0, 1, 0, 0.f);
        c.extra = bad;
        rig.send(c);
        rig.play();
        rig.render(4 * kStep);
        CHECK(rig.echo->runs == 0 && rig.space->runs == 0 && peakOf(rig.out, kStep + 60) > 0.f);
    }
    CHECK(throwName(0)[0] == 'O' && throwName(1)[0] == 'E' && throwName(2)[0] == 'S' && throwName(3)[0] == 'B' && throwName(9)[0] == 'O');
    // A whole track handed over at once (how a song is loaded) carries both kinds.
    {
        Rig rig;
        rig.sineSynth(1);
        std::unique_ptr<TrackImage> drums(new TrackImage), synth(new TrackImage);
        drums->level[0][1][4] = 0.9f;
        drums->fx[0][1][4].throwTo = kThrowSpace;
        synth->notes[0][0].step = 2;
        synth->notes[0][0].pitch = 60;
        synth->notes[0][0].length = 2.f;
        synth->notes[0][0].velocity = 0.8f;
        synth->notes[0][0].throwTo = kThrowEcho;
        CHECK(rig.engine->setTrackImage(0, std::move(drums)) && rig.engine->setTrackImage(1, std::move(synth)));
        rig.play();
        rig.render(8 * kStep);
        rig.engine->collectGarbage();
        CHECK(rig.woke(rig.echo) == 2 * kStep && rig.woke(rig.space) == 4 * kStep); // the note on step 2, the hit on step 4
        CHECK(peakOf(rig.space->fed, 0, kStep) > 0.4f);
        CHECK(peakOf(rig.echo->fed, 0, 2 * kStep) > 0.3f && peakOf(rig.echo->fed, 2 * kStep + kGlide) == 0.f);
    }
    // A cell emptied takes its throws with it.
    {
        Rig rig;
        rig.sineSynth(1);
        rig.hit(1, 4, 0.9f, 3);
        rig.note(1, 0, 2, 60, 2, 3);
        rig.send(make(Command::ClearCell, 0));
        rig.send(make(Command::ClearCell, 1));
        rig.send(make(Command::SetStep, 0, 1, 4, 0.9f));
        rig.play();
        rig.render(8 * kStep);
        CHECK(rig.echo->runs == 0 && rig.space->runs == 0 && peakOf(rig.out) > 0.3f);
    }
}

// ---- 6. the same on any number of helpers --------------------------------------------------------

// Drums and three synths, real effects on both buses, sends from every track,
// thrown hits (ratcheted, cut, pitched) and thrown notes (ratcheted, nudged),
// a mute part way. Asked for in blocks of odd sizes.
static std::vector<float> throwSong(int helpers, bool throws = true) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->useWorkers(helpers);
    CHECK(engine->addDrumTrack(4));
    for (int track : {0, 4}) {
        for (int s = 0; s < kSteps; ++s) {
            const int lane = s % 4 == 0 ? 0 : (s % 4 == 2 ? 1 : 2 + (s + track) % 3);
            engine->send(make(Command::SetStep, track, lane, s, 0.9f));
            StepFx fx;
            fx.throwTo = static_cast<std::uint8_t>(throws ? (s + track / 4) % 4 : 0);
            fx.ratchet = static_cast<std::uint8_t>(s % 5 == 0 ? 3 : 0);
            fx.cut = static_cast<std::uint8_t>(s % 3);
            Command c = make(Command::SetStepFx, track, lane, s, s % 7 == 0 ? 1.f : 0.f);
            c.c = s % 6 == 0 ? -5 : 0;
            c.d = fx.ratchet;
            c.extra = fx.cutAndThrow();
            engine->send(c);
        }
    }
    const SynthKind kinds[3] = {SynthKind::Phazed, SynthKind::Qmod, SynthKind::Twin};
    for (int t = 1; t <= 3; ++t) {
        CHECK(engine->addSynthTrack(t, kinds[t - 1]));
        for (int n = 0; n < 4; ++n) {
            Command note = make(Command::SetNote, t, n, n * 4 + (t % 3), 0.7f);
            note.c = 40 + 5 * t + 3 * n;
            note.d = 3;
            note.shift = n == 2 ? 0.3f : (n == 3 ? -0.2f : 0.f);
            note.e = (n == 1 ? 3 : 0) + (throws ? ((n + t) % 4) << 9 : 0);
            engine->send(note);
        }
    }
    CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Tape) && engine->setEffect(kSpaceBus, 0, EffectKind::Hall));
    CHECK(engine->setEffect(0, 2, EffectKind::Crush) && engine->setEffect(2, 2, EffectKind::Filter));
    engine->send(make(Command::SetSend, 0, 0, 0, 0.3f));
    engine->send(make(Command::SetSend, 2, 1, 0, 0.6f));
    std::vector<float> all(2 * 1024);
    engine->render(all.data(), 1024);
    engine->send(make(Command::Play, 0, 0, 1));
    int round = 0;
    for (int done = 0, size = 37; done < 3 * 16 * kStep; done += size, size = size % 301 + 64, ++round) {
        if (round == 300) engine->send(make(Command::SetTrackHeard, 3, 0));
        if (round == 600) engine->send(make(Command::SetTrackHeard, 3, 1));
        if (round == 700) engine->send(make(Command::SetTrackHeard, 4, 0));
        std::vector<float> got(static_cast<std::size_t>(2 * size), 99.f);
        engine->render(got.data(), size);
        all.insert(all.end(), got.begin(), got.end());
        engine->collectGarbage();
    }
    all.push_back(engine->busAwake(0) ? 1.f : 0.f);
    all.push_back(engine->busAwake(1) ? 1.f : 0.f);
    return all;
}

static void testOnAnyHelperCount() {
    const std::vector<float> alone = throwSong(0);
    CHECK(peakOf(alone) > 0.1f && alone.size() > 500000);
    for (float v : alone) {
        if (v != v || v == 99.f) {
            CHECK(!"a sample that is not a number, or was never written");
            break;
        }
    }
    CHECK(throwSong(0) == alone); // twice from fresh
    for (int helpers : {1, 2, 3}) CHECK(throwSong(helpers) == alone);
    // The throws are heard: it is not the same song without them.
    const std::vector<float> dry = throwSong(0, false);
    CHECK(dry.size() == alone.size() && dry != alone);
    double apart = 0.0;
    for (std::size_t i = 0; i + 2 < alone.size(); ++i) apart += (static_cast<double>(alone[i]) - dry[i]) * (static_cast<double>(alone[i]) - dry[i]);
    CHECK(std::sqrt(apart / static_cast<double>(alone.size())) > 0.005);
}

int main() {
    testDrumMachine();
    testThrownHit();
    testThrownNote();
    testStopAndPanic();
    testCommands();
    testOnAnyHelperCount();
    if (failures == 0) std::printf("throw_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
