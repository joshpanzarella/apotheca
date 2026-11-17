// SWING: every second sixteenth of a drum lane played late, up to a triplet (feel_rule.hpp).
//
// Nobody listens to these: they are measured, as FEEL's are (feel_test.cpp). A lane is given a
// recording that is one sample of 1 and then silence, so every hit is one sample in the output:
// where it is says when the hit came.
//
//   1. the rule by hand: which steps, how late, "up to a triplet" pinned
//   2. a lane at 0 plays as it always did, to the bit, beside a swung lane and beside FEEL
//   3. every amount: each hit where the rule says, at 120 and at 133 BPM
//   4. a nudge, HUMAN TIME and the swing add up; a hit nudged early is still asked ahead
//   5. a swung ratchet is squeezed into what is left of its step, and cuts nothing after it
//   6. the same to the bit at any block length (1 to 333), on any number of helpers, the tempo turned
//   7. Play from the top is the same again; played once (a bounce), the last step's hit is in
//   8. the swing turned while the song plays: no hit lost, none played twice
//   9. slices in order still carry the sound on: a run takes its first hit's swing
//  10. DUCK is told a swung hit where it sounds; a thrown hit is thrown where it sounds
//  11. a track taken away leaves no swing behind; the command's numbers are held to their limits
//  12. a scene put in during the last step: what follows is where a song built so has it

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "apotheca/effects.hpp"
#include "apotheca/engine.hpp"
#include "apotheca/feel_rule.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static const int kStep = 6000; // a step at 120 BPM, 48 kHz
static const int kBar = 16 * kStep;
static const double kPerMs = 48.0;

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}

// Told the drum hits, as DUCK is: writes down the frame each lane's were said on.
struct Listener : Effect {
    long frame = 0;
    std::vector<long> told[kDrumLanes];
    Listener() {
        for (auto& t : told) t.reserve(4096);
    }
    void setParam(int, float) override {}
    void drumHits(unsigned lanes) override {
        for (int lane = 0; lane < kDrumLanes; ++lane)
            if ((lanes >> lane) & 1u && told[lane].size() < told[lane].capacity()) told[lane].push_back(frame);
    }
    void process(float*, float*, int frames) override { frame += frames; }
};
struct Probe : Effect { // on a bus: writes down what it is given, gives nothing back
    std::vector<float> fed;
    Probe() { fed.reserve(48000 * 20); }
    void setParam(int, float) override {}
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) {
            if (fed.size() < fed.capacity()) fed.push_back(left[i]);
            left[i] = right[i] = 0.f;
        }
    }
};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    std::vector<float> out; // the left side of everything rendered
    explicit Rig(int helpers = 0) {
        if (helpers) engine->useWorkers(helpers);
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
    // One sample of 1 and then nothing, on a lane.
    void click(int track, int lane) {
        std::unique_ptr<SampleData> s(new SampleData);
        s->data.assign(4, 0.f);
        s->data[0] = 1.f;
        CHECK(engine->setLaneSample(track, lane, std::move(s)));
    }
    void clicks(int track) {
        for (int lane = 0; lane < kDrumLanes; ++lane) click(track, lane);
    }
    void hit(int lane, int step, float level = 0.8f, float nudge = 0.f, int ratchet = 0, int throwTo = 0, int track = 0, int bar = 0) {
        Command c = make(Command::SetStep, track, lane, step, level);
        c.bar = bar;
        send(c);
        if (nudge != 0.f) {
            c = make(Command::SetStepNudge, track, lane, step, nudge);
            c.bar = bar;
            send(c);
        }
        if (ratchet || throwTo) {
            StepFx fx;
            fx.ratchet = static_cast<std::uint8_t>(ratchet);
            fx.throwTo = static_cast<std::uint8_t>(throwTo);
            c = make(Command::SetStepFx, track, lane, step, 0.f);
            c.bar = bar;
            c.d = fx.ratchet;
            c.extra = fx.cutAndThrow();
            send(c);
        }
    }
    void feel(int lane, int time, int level, int track = 0) {
        Command c = make(Command::SetLaneFeel, track, lane, time);
        c.c = level;
        send(c);
    }
    void swing(int lane, int amount, int track = 0) { send(make(Command::SetLaneSwing, track, lane, amount)); }
    void play(bool once = false) {
        Command c = make(Command::Play, 0, 0, once ? 0 : 1);
        c.c = once ? 1 : 0;
        send(c);
    }
    std::vector<long> onsets(long from = 0, long to = -1) const {
        std::vector<long> at;
        const long end = to < 0 || to > static_cast<long>(out.size()) ? static_cast<long>(out.size()) : to;
        for (long i = from; i < end; ++i)
            if (out[static_cast<std::size_t>(i)] != 0.f) at.push_back(i);
        return at;
    }
};

// How late SWING puts a hit on `step`, whole, at 120 BPM.
static long lateOf(int amount, int step) { return static_cast<long>(std::ceil(swingSamples(amount, step, kStep))); }

// ---- 1 -------------------------------------------------------------------------------------------

static void testRule() {
    // Steps 2, 4, ... 16 counted from 1: the odd ones counted from 0. The main beats never.
    for (int step = 0; step < kSteps; ++step) {
        CHECK(swingOffStep(step) == (step % 2 == 1));
        if (feelOnBeat(step)) CHECK(!swingOffStep(step));
    }
    // Up to a triplet: at 100% a third of a step, exactly (6000 samples a step at 120 BPM: 2000).
    CHECK(swingSamples(100, 1, kStep) == 2000.0);
    CHECK(swingSamples(100, 15, kStep) == 2000.0);
    CHECK(swingSamples(100, 0, kStep) == 0.0 && swingSamples(100, 14, kStep) == 0.0);
    CHECK(swingSamples(0, 1, kStep) == 0.0 && swingSamples(-20, 1, kStep) == 0.0);
    // ...the eighth played 2 to 1: the first sixteenth is two thirds of the eighth, the second one third.
    const double eighth = 2.0 * kStep, first = kStep + swingSamples(100, 1, kStep);
    CHECK(std::fabs(first / eighth - 2.0 / 3.0) < 1e-12);
    CHECK(std::fabs(kSwingMostOfStep - 1.0 / 3.0) < 1e-15);
    // The six clicks, by hand: 1/30, 1/12, 1/6, 1/4, 1/3 of a step.
    CHECK(swingSamples(10, 3, kStep) == 200.0 && swingSamples(25, 3, kStep) == 500.0 && swingSamples(50, 3, kStep) == 1000.0 && swingSamples(75, 3, kStep) == 1500.0);
    // A step that is a part of a sample long (133 BPM): not yet whole.
    const double step133 = 48000.0 * 60.0 / 133.0 / 4.0;
    CHECK(std::fabs(swingSamples(100, 5, step133) - step133 / 3.0) < 1e-9);
    // The numbers from outside: held.
    CHECK(laneSwingOf(-1) == 0 && laneSwingOf(0) == 0 && laneSwingOf(37) == 37 && laneSwingOf(100) == 100 && laneSwingOf(101) == 100 && laneSwingOf(100000) == 100);
}

// ---- 2 -------------------------------------------------------------------------------------------

static void busy(Rig& rig) {
    for (int s = 0; s < kSteps; ++s) {
        rig.hit(0, s, 0.5f + 0.03f * s, s % 5 == 1 ? 0.3f : (s % 5 == 3 ? -0.25f : 0.f), s % 6 == 2 ? 3 : 0);
        if (s % 2 == 0) rig.hit(1, s, 0.7f, s % 4 == 0 ? -0.4f : 0.f);
        if (s % 3 == 0) rig.hit(2, s, 0.9f, 0.f, s == 9 ? 4 : 0, s % 2 ? kThrowEcho : 0);
    }
}

static void testAtRest() {
    // The kit's own drums on lanes 1 to 3, at 0; a lane with no hits swung; a lane swung and put back.
    for (int felt = 0; felt < 2; ++felt) {
        Rig plain, swung;
        busy(plain);
        busy(swung);
        if (felt) { // ...and beside FEEL: a felt lane is as it was too
            plain.feel(0, 75, 50);
            swung.feel(0, 75, 50);
        }
        swung.swing(3, 100); // a lane with no hits
        swung.swing(0, 50);
        swung.swing(0, 0); // ...and one put back at 0 before Play
        plain.play();
        swung.play();
        plain.render(3 * kBar, 256);
        swung.render(3 * kBar, 256);
        CHECK(plain.out == swung.out);
        CHECK(*std::max_element(plain.out.begin(), plain.out.end()) > 0.1f);
    }
    // ...and the same busy song swung is another song (the fixture reaches the swing).
    Rig plain, swung;
    busy(plain);
    busy(swung);
    swung.swing(0, 50);
    plain.play();
    swung.play();
    plain.render(kBar);
    swung.render(kBar);
    CHECK(plain.out != swung.out);
    // Swung, stopped part way through a bar, put back at 0, played again: plain from the top.
    Rig was, never;
    for (Rig* rig : {&was, &never}) {
        rig->clicks(0);
        for (int s = 0; s < kSteps; ++s) rig->hit(0, s, 0.8f, 0.f, s == 7 ? 3 : 0);
    }
    was.swing(0, 100);
    was.play();
    was.render(5 * kStep + 1000); // (the hit on step 6 waits its 2000 samples)
    was.send(make(Command::Stop));
    was.swing(0, 0);
    was.render(4000);
    was.out.clear();
    was.play();
    never.play();
    was.render(2 * kBar);
    never.render(2 * kBar);
    CHECK(was.out == never.out);
}

// ---- 3 -------------------------------------------------------------------------------------------

static void testPlaces() {
    for (int amount : {10, 25, 50, 75, 100, 37}) {
        Rig rig;
        rig.clicks(0);
        for (int s = 0; s < kSteps; ++s) rig.hit(2, s);
        rig.swing(2, amount);
        rig.play();
        rig.render(3 * kBar);
        const std::vector<long> at = rig.onsets(0, 3 * kBar - kStep);
        CHECK(static_cast<int>(at.size()) == 3 * kSteps - 1);
        for (int n = 0; n < static_cast<int>(at.size()); ++n) {
            const int step = n % kSteps;
            const long want = static_cast<long>(n) * kStep + (step % 2 ? static_cast<long>(amount) * kStep / 300 : 0);
            CHECK(at[static_cast<std::size_t>(n)] == want); // the same every time round: no roll
            CHECK(want == static_cast<long>(n) * kStep + lateOf(amount, step));
        }
    }
    // At 100%: the off sixteenth two thirds of the way through each eighth (a triplet's third note).
    {
        Rig rig;
        rig.clicks(0);
        for (int s = 0; s < kSteps; ++s) rig.hit(0, s);
        rig.swing(0, 100);
        rig.play();
        rig.render(kBar);
        const std::vector<long> at = rig.onsets();
        CHECK(at.size() == 16);
        for (std::size_t k = 0; k + 1 < at.size(); k += 2) {
            const long eighth = 2 * kStep, from = static_cast<long>(k / 2) * eighth;
            CHECK(at[k] == from && 3 * (at[k + 1] - from) == 2 * eighth);
        }
    }
    // 133 BPM, a step a part of a sample long: every swung hit is the plain hit's place plus the
    // swing made whole (it rounds up), every other hit where the plain one is.
    const double step133 = 48000.0 * 60.0 / 133.0 / 4.0;
    for (int amount : {25, 100}) {
        Rig plain, swung;
        for (Rig* rig : {&plain, &swung}) {
            rig->clicks(0);
            rig->send(make(Command::SetTempo, 0, 0, 0, 133.f));
            for (int s = 0; s < kSteps; ++s) rig->hit(4, s);
            rig->render(64);
        }
        swung.swing(4, amount);
        plain.play();
        swung.play();
        plain.render(3 * kBar);
        swung.render(3 * kBar);
        const std::vector<long> a = plain.onsets(), b = swung.onsets(0, static_cast<long>(plain.out.size()) - kStep);
        CHECK(b.size() > 40 && a.size() >= b.size());
        int fractional = 0;
        for (std::size_t n = 0; n < b.size() && n < a.size(); ++n) {
            const double late = swingSamples(amount, static_cast<int>(n % kSteps), step133);
            CHECK(b[n] == a[n] + static_cast<long>(std::ceil(late)));
            fractional += late != std::floor(late);
        }
        CHECK(fractional > 10); // (the fixture reaches a swing that is not whole)
    }
}

// ---- 4 -------------------------------------------------------------------------------------------

static void testNudgeAndTime() {
    // Nudged late, nudged early, on an off step and on an on step: the swing adds to the nudge.
    // A hit nudged early on an off step is still asked a step ahead, and swung it comes late.
    Rig rig;
    rig.clicks(0);
    rig.hit(1, 1, 0.8f, 0.2f);   // off, late: 0.2 + 1/3
    rig.hit(1, 3, 0.8f, -0.2f);  // off, early: -0.2 + 1/3, late after all
    rig.hit(1, 5, 0.8f, -0.45f); // off, early by more than the swing: early still
    rig.hit(1, 6, 0.8f, -0.3f);  // on, early: not swung
    rig.hit(1, 8, 0.8f, 0.3f);   // on, late: not swung
    rig.swing(1, 100);
    rig.play();
    rig.render(2 * kBar);
    const std::vector<long> at = rig.onsets(0, kBar);
    // (Each made whole once: nudge, as the float it is kept as, plus the swing, rounded up.)
    const auto place = [](int step, float nudge) { return static_cast<long>(step) * kStep + static_cast<long>(std::ceil(static_cast<double>(nudge) * kStep + swingSamples(100, step, kStep))); };
    const long want[5] = {place(1, 0.2f), place(3, -0.2f), place(5, -0.45f), place(6, -0.3f), place(8, 0.3f)};
    CHECK(want[0] == 1 * kStep + 3201 && want[1] == 3 * kStep + 800 && want[2] == 5 * kStep - 699 && want[3] == 6 * kStep - 1800 && want[4] == 8 * kStep + 1801);
    CHECK(at.size() == 5);
    for (std::size_t k = 0; k < at.size() && k < 5; ++k) CHECK(at[k] == want[k]);
    // HUMAN TIME and the swing: each hit at its nudge + its roll + the swing, made whole once.
    for (int lane : {0, 3, 6}) {
        Rig both;
        both.clicks(0);
        for (int s = 0; s < kSteps; ++s) both.hit(lane, s, 0.8f, s % 4 == 3 ? 0.25f : (s % 4 == 1 ? -0.2f : 0.f));
        both.feel(lane, 100, 0);
        both.swing(lane, 75);
        both.play();
        both.render(3 * kBar + kStep);
        const std::vector<long> got = both.onsets(0, 3 * kBar - 1000); // (the last step: nudged late and swung, up to 3576 on; the next: up to 288 early)
        CHECK(got.size() == 48);
        for (int n = 1; n < 48 && n < static_cast<int>(got.size()); ++n) {
            const int s = n % kSteps;
            const double nudge = s % 4 == 3 ? 0.25 : (s % 4 == 1 ? -0.2 : 0.0);
            const double roll = feelSamples(100, s, feelSeed(0, lane, 0, s, n), kPerMs, kStep);
            CHECK(got[static_cast<std::size_t>(n)] == static_cast<long>(n) * kStep + static_cast<long>(std::ceil(static_cast<double>(static_cast<float>(nudge)) * kStep + roll + swingSamples(75, s, kStep))));
        }
    }
}

// ---- 5 -------------------------------------------------------------------------------------------

static void testRatchet() {
    Rig rig;
    rig.clicks(0);
    rig.hit(1, 3, 0.7f, 0.f, 4); // off: squeezed into 6000 - 2000
    rig.hit(1, 4, 0.7f);         // the next step's hit: not cut, and where it always is
    rig.hit(1, 6, 0.7f, 0.f, 4); // on: as an unswung one
    rig.hit(1, 9, 0.7f, 0.f, 3); // off, by three: 1333 apart, the last 5999 into the step
    rig.hit(2, 11, 0.7f, 0.f, 2); // another lane at 50%: 1000 late, 2500 apart
    rig.swing(1, 100);
    rig.swing(2, 50);
    rig.play();
    rig.render(kBar + kStep);
    const std::vector<long> at = rig.onsets(0, kBar);
    const long want[] = {3 * kStep + 2000, 3 * kStep + 3000, 3 * kStep + 4000, 3 * kStep + 5000, 4 * kStep,
                         6 * kStep,        6 * kStep + 1500, 6 * kStep + 3000, 6 * kStep + 4500,
                         9 * kStep + 2000, 9 * kStep + 3333, 9 * kStep + 4666, 11 * kStep + 1000, 11 * kStep + 3500};
    CHECK(at.size() == sizeof want / sizeof want[0]);
    for (std::size_t k = 0; k < at.size() && k < sizeof want / sizeof want[0]; ++k) CHECK(at[k] == want[k]);
    // Every piece of a squeezed ratchet before the next step line, at every amount and count.
    for (int amount : {10, 25, 50, 75, 100}) {
        for (int pieces = 2; pieces <= 4; ++pieces) {
            Rig r;
            r.clicks(0);
            r.hit(0, 5, 0.7f, 0.f, pieces);
            r.swing(0, amount);
            r.play();
            r.render(kBar);
            const std::vector<long> p = r.onsets();
            CHECK(static_cast<int>(p.size()) == pieces);
            if (p.empty()) continue;
            CHECK(p.front() == 5 * kStep + lateOf(amount, 5) && p.back() < 6 * kStep);
            const long gap = static_cast<long>(std::floor((kStep - swingSamples(amount, 5, kStep)) / pieces));
            for (std::size_t k = 1; k < p.size(); ++k) CHECK(p[k] - p[k - 1] == gap);
        }
    }
}

// ---- 6, 7 ----------------------------------------------------------------------------------------

// Three drum tracks, the kit's own drums and recordings, lanes swung and felt their own ways,
// nudges, ratchets, throws into real effects, a DUCK, a tempo that makes a step a part of a sample long.
static std::vector<float> swingSong(int helpers, int block, bool tempoTurned = false) {
    Rig rig(helpers);
    for (int t : {1, 4}) CHECK(rig.engine->addDrumTrack(t));
    rig.clicks(4);
    const int amounts[6] = {0, 10, 25, 50, 75, 100};
    for (int t : {0, 1, 4}) {
        for (int s = 0; s < kSteps; ++s) {
            for (int lane = 0; lane < kDrumLanes; ++lane) {
                if ((s * 3 + lane * 5 + t) % 4 != 0 && s % 2 == 0) continue;
                rig.hit(lane, s, 0.5f + 0.05f * lane, (s + lane) % 5 == 1 ? 0.35f : ((s + lane) % 5 == 3 ? -0.3f : 0.f), (s + lane) % 7 == 0 ? 2 + lane % 3 : 0,
                        (s + t) % 6 == 0 ? 1 + s % 3 : 0, t);
            }
        }
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            rig.swing(lane, amounts[1 + (lane + t) % 5], t);
            if (t == 4) rig.feel(lane, amounts[(lane * 2 + 1) % 6], amounts[lane % 6], t);
        }
    }
    CHECK(rig.engine->setEffect(kEchoBus, 0, EffectKind::Tape) && rig.engine->setEffect(kSpaceBus, 0, EffectKind::Plate));
    CHECK(rig.engine->setEffect(1, 0, EffectKind::Duck));
    rig.send(make(Command::SetTempo, 0, 0, 0, 133.f));
    rig.render(480, block);
    rig.play();
    if (tempoTurned) {
        // The tempo turned while swung hits wait, at moments that are no step's and no block's.
        const float to[5] = {97.f, 171.f, 133.f, 61.f, 201.f};
        for (int k = 0; k < 5; ++k) {
            rig.render(kBar / 2 + 1237 * (k + 1), block);
            rig.send(make(Command::SetTempo, 0, 0, 0, to[k]));
        }
        rig.render(kBar / 2 - 1237 * 15, block);
    } else {
        rig.render(3 * kBar, block);
    }
    rig.out.erase(rig.out.begin(), rig.out.begin() + 480);
    return rig.out;
}

static void testBlocksAndHelpers() {
    const std::vector<float> usual = swingSong(0, 256);
    CHECK(*std::max_element(usual.begin(), usual.end()) > 0.1f);
    for (int block : {1024, 333, 64, 37, 5, 1}) CHECK(swingSong(0, block) == usual);
    for (int helpers : {1, 2, 3}) CHECK(swingSong(helpers, 256) == usual);
    CHECK(swingSong(3, 37) == usual);
    CHECK(swingSong(2, 1) == usual);
    const std::vector<float> turned = swingSong(0, 256, true);
    CHECK(turned != usual);
    for (int block : {1, 5, 37, 64, 333}) CHECK(swingSong(0, block, true) == turned);
    CHECK(swingSong(3, 1, true) == turned);
}

static void testPlayTwiceAndOnce() {
    for (int block : {256, 37}) {
        Rig rig;
        CHECK(rig.engine->addDrumTrack(1));
        rig.clicks(0);
        rig.clicks(1);
        for (int s = 0; s < kSteps; ++s) {
            rig.hit(s % 3, s, 0.7f, s % 4 == 1 ? -0.2f : 0.f, s % 5 == 0 ? 3 : 0);
            rig.hit(3 + s % 2, s, 0.6f, 0.f, 0, 0, 1);
        }
        for (int lane = 0; lane < 5; ++lane) {
            rig.swing(lane, 25 * (lane % 4 + 1));
            rig.swing(lane, 50, 1);
            rig.feel(lane, 50, 100, 1);
        }
        rig.play();
        rig.render(4 * kBar, block);
        const std::vector<float> first = rig.out;
        rig.send(make(Command::Stop));
        rig.render(9000, 100);
        rig.out.clear();
        rig.play();
        rig.render(4 * kBar, block);
        CHECK(rig.out == first);
    }
    // Played once (a bounce) and played round: the last step's swung hit is in, late as ever, and the
    // next time round's first hit is on its step. (Once, nothing early is asked of the first step; the
    // swing asks nothing early, so the two are the same.)
    std::vector<long> was;
    for (int once = 0; once < 2; ++once) {
        Rig rig;
        rig.clicks(0);
        rig.hit(3, 0);
        rig.hit(3, 15);
        rig.swing(3, 100);
        Command c = make(Command::Play, 0, 0, 0); // the song (one scene of one bar), not a scene alone
        c.c = once;
        rig.send(c);
        rig.render(2 * kBar);
        const std::vector<long> at = rig.onsets();
        CHECK(at.size() == 4u);
        if (at.size() == 4) CHECK(at[0] == 0 && at[1] == 15 * kStep + 2000 && at[2] == kBar && at[3] == kBar + 15 * kStep + 2000);
        if (once) CHECK(at == was);
        was = at;
    }
}

// ---- 8 -------------------------------------------------------------------------------------------

static void testTurnedWhilePlaying() {
    // The swing turned up and down at every kind of moment in a step, while every step has a hit:
    // each step still has exactly one, at its place or up to a third of a step after it.
    for (int phase = 0; phase < 12; ++phase) {
        Rig rig;
        rig.clicks(0);
        for (int s = 0; s < kSteps; ++s) rig.hit(0, s, 0.8f, s % 4 == 3 ? -0.2f : 0.f);
        rig.play();
        const int total = 8 * kBar;
        int done = 0, turn = 0;
        rig.render(phase * 517 + 13, 64);
        done += phase * 517 + 13;
        while (done < total) {
            const int amounts[5] = {100, 0, 25, 75, 0};
            rig.swing(0, amounts[turn % 5]);
            if (turn % 3 == 1) rig.feel(0, 50, 0);
            if (turn % 3 == 2) rig.feel(0, 0, 0);
            ++turn;
            const int n = kStep + 911 + 373 * (turn % 5);
            rig.render(n, 64);
            done += n;
        }
        const std::vector<long> at = rig.onsets(0, total - kStep / 2);
        const int steps = total / kStep; // (the last: nudged early, swung up to 2000 and felt, inside the window)
        CHECK(static_cast<int>(at.size()) == steps);
        std::vector<int> seen(static_cast<std::size_t>(steps + 2), 0);
        for (long a : at) {
            int best = -1;
            for (int n = static_cast<int>(a / kStep) - 1; n <= static_cast<int>(a / kStep) + 1; ++n) {
                if (n < 0 || n > steps) continue;
                const double nudge = n % 4 == 3 ? -0.2 : 0.0;
                const double off = static_cast<double>(a) - (n + nudge) * kStep;
                if (off >= -289.0 && off <= 2000.0 + 289.0 && (n % 2 == 1 || off <= 289.0)) best = n;
            }
            CHECK(best >= 0);
            if (best >= 0) ++seen[static_cast<std::size_t>(best)];
        }
        for (int n = 0; n < steps; ++n) CHECK(seen[static_cast<std::size_t>(n)] == 1);
    }
}

// ---- 9 -------------------------------------------------------------------------------------------

// A recording cut into slices and played in order: one unbroken sound. With SWING the run takes
// its first hit's swing: begun on an on step it is not moved at all, begun on an off step it is
// moved late as a whole, and with HUMAN TIME it moves by the first hit's roll and swing together.
static void testSlices() {
    // How many slices a run has: nine, or on through the second bar, never past the cell's end.
    const auto slicesIn = [](int first, int bars) { return std::min(9 + (bars - 1) * kSteps, bars * kSteps - first); };
    const auto build = [&](Rig& rig, int first, int swing, int time, int bars) {
        std::unique_ptr<SampleData> s(new SampleData);
        s->data.resize(static_cast<std::size_t>(bars * kBar));
        for (int i = 0; i < bars * kBar; ++i) s->data[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::sin(i * 0.031 + 1e-7 * i * i / bars));
        CHECK(rig.engine->setLaneSample(0, 0, std::move(s)));
        if (bars > 1) rig.send(make(Command::SetCellBars, 0, bars));
        rig.send(make(Command::SetLanePart, 0, 0, 0, 1.f / (16.f * bars)));
        for (int k = first; k < first + slicesIn(first, bars); ++k) {
            rig.hit(0, k % kSteps, 0.8f, 0.f, 0, 0, 0, k / kSteps);
            Command c = make(Command::SetStepStart, 0, 0, k % kSteps, (k - first + 1) / (16.f * bars));
            c.bar = k / kSteps;
            rig.send(c);
        }
        rig.swing(0, swing);
        rig.feel(0, time, 0);
        rig.play();
        rig.render((4 * bars + 1) * kBar);
    };
    // (Each run's first slice starts a slice into the sound, so it is a run's first by having no hit before it.)
    struct Case {
        int first, swing, time, bars;
    };
    const Case cases[] = {{0, 100, 0, 1}, {1, 100, 0, 1}, {3, 50, 0, 1}, {1, 75, 100, 1}, {2, 75, 100, 1}, {9, 100, 0, 2}};
    for (const Case& k : cases) {
        Rig plain, swung;
        build(plain, k.first, 0, 0, k.bars);
        build(swung, k.first, k.swing, k.time, k.bars);
        const long cell = static_cast<long>(k.bars) * kSteps;
        for (int round = 1; round < 4; ++round) {
            const long n = round * cell + k.first; // the run's first hit, as the step since Play
            const double roll = k.time ? feelSamples(k.time, k.first, feelSeed(0, 0, 0, k.first, n), kPerMs, kStep) : 0.0;
            const long shift = static_cast<long>(std::ceil(roll + swingSamples(k.swing, k.first, kStep)));
            CHECK((k.first % 2 == 1) == (shift != 0) || k.time != 0);
            float worst = 0.f, loudest = 0.f;
            const long from = n * kStep + 600, to = (n + slicesIn(k.first, k.bars)) * kStep - 700;
            for (long i = from; i < to; ++i) {
                const float a = plain.out[static_cast<std::size_t>(i)], b = swung.out[static_cast<std::size_t>(i + shift)];
                worst = std::fabs(a - b) > worst ? std::fabs(a - b) : worst;
                loudest = std::fabs(a) > loudest ? std::fabs(a) : loudest;
            }
            CHECK(loudest > 0.3f);
            CHECK(worst == 0.f); // the same sound to the sample: no seam anywhere in the run
        }
    }
    // Slices NOT in a run (an empty step between each) are each a hit of their own: the off ones swung.
    Rig apart;
    apart.click(0, 0);
    for (int s = 0; s < kSteps; s += 3) {
        apart.hit(0, s);
        apart.send(make(Command::SetStepStart, 0, 0, s, 0.f));
    }
    apart.swing(0, 100);
    apart.play();
    apart.render(kBar);
    const std::vector<long> at = apart.onsets();
    CHECK(at.size() == 6);
    for (std::size_t k = 0; k < at.size(); ++k) {
        const int s = static_cast<int>(k) * 3;
        CHECK(at[k] == s * kStep + (s % 2 ? 2000 : 0));
    }
}

// ---- 10 ------------------------------------------------------------------------------------------

static void testDuckAndThrow() {
    Rig rig;
    Listener* listener = new Listener;
    Probe* echo = new Probe;
    rig.engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(listener));
    rig.engine->setBusEffect(0, std::unique_ptr<Effect>(echo));
    rig.clicks(0);
    for (int s = 1; s < kSteps; s += 2) rig.hit(5, s, 0.8f, 0.f, 0, kThrowEcho);
    rig.swing(5, 100);
    rig.play();
    rig.render(3 * kBar);
    const std::vector<long> at = rig.onsets(0, 3 * kBar - 1000);
    CHECK(at.size() == 24);
    for (std::size_t i = 0; i < at.size(); ++i) CHECK(at[i] % kStep == 2000);
    CHECK(listener->told[5].size() >= at.size());
    for (std::size_t i = 0; i < at.size() && i < listener->told[5].size(); ++i) CHECK(listener->told[5][i] == at[i]);
    const long woke = static_cast<long>(rig.out.size()) - static_cast<long>(echo->fed.size());
    std::size_t thrown = 0;
    for (std::size_t i = 0; i < echo->fed.size(); ++i) {
        if (echo->fed[i] == 0.f) continue;
        if (thrown < at.size()) CHECK(woke + static_cast<long>(i) == at[thrown]);
        ++thrown;
    }
    CHECK(thrown >= at.size());
}

// ---- 11 ------------------------------------------------------------------------------------------

static void testGoneAndLimits() {
    Rig rig;
    CHECK(rig.engine->addDrumTrack(2));
    rig.clicks(2);
    for (int s = 0; s < kSteps; ++s) rig.hit(1, s, 0.8f, 0.f, 0, 0, 2);
    rig.swing(1, 100, 2);
    rig.render(256);
    CHECK(rig.engine->removeTrack(2));
    rig.render(2048);
    rig.engine->collectGarbage();
    CHECK(rig.engine->addDrumTrack(2));
    rig.clicks(2);
    for (int s = 0; s < kSteps; ++s) rig.hit(1, s, 0.8f, 0.f, 0, 0, 2);
    rig.out.clear();
    rig.play();
    rig.render(2 * kBar);
    const std::vector<long> at = rig.onsets();
    CHECK(at.size() == 32);
    for (std::size_t n = 0; n < at.size(); ++n) CHECK(at[n] == static_cast<long>(n) * kStep); // on the grid: the new kit plays straight
    // Numbers out of range: held (300 is 100, below 0 is 0); a lane or a track that is not there changes nothing.
    Rig wide, full, below, rest;
    for (Rig* r : {&wide, &full, &below, &rest}) {
        r->clicks(0);
        for (int s = 0; s < kSteps; ++s) r->hit(1, s, 0.6f);
    }
    wide.swing(1, 300);
    wide.swing(8, 100);
    wide.swing(-1, 100);
    wide.send(make(Command::SetLaneSwing, kNumTracks, 1, 50));
    wide.send(make(Command::SetLaneSwing, -1, 1, 50));
    full.swing(1, 100);
    below.swing(1, -40);
    for (Rig* r : {&wide, &full, &below, &rest}) {
        r->play();
        r->render(2 * kBar);
    }
    CHECK(wide.out == full.out);
    CHECK(below.out == rest.out);
    CHECK(full.out != rest.out);
}

// ---- 12 ------------------------------------------------------------------------------------------

// A scene put in while the last step of the scene before it plays (NEXT SCENE: Engine::moveScenes),
// at 133 BPM: every swung hit of what follows on the very sample a song built with the scene in it
// has it, with HUMAN TIME on the lane and without.
static void testASceneMoved() {
    const double step = 48000.0 * 60.0 / 133.0 / 4.0;
    const int lastStep = 64 + static_cast<int>(15 * step) + 1;
    const int whole = 64 + static_cast<int>(50 * step);
    const auto hit = [](Rig& rig, int scene, int lane, int at, float nudge) {
        Command c = make(Command::SetStep, 0, lane, at, 0.8f);
        c.scene = scene;
        rig.send(c);
        if (nudge != 0.f) {
            c = make(Command::SetStepNudge, 0, lane, at, nudge);
            c.scene = scene;
            rig.send(c);
        }
    };
    int same = 0;
    for (int lane = 0; lane < kDrumLanes; lane += 3) {
        for (int time : {0, 100}) {
            const auto build = [&](Rig& rig, int scenes) {
                rig.clicks(0);
                rig.send(make(Command::SetTempo, 0, 0, 0, 133.f));
                hit(rig, 0, lane, 0, 0.f);
                hit(rig, 0, lane, 15, 0.f); // swung, on the step the scene is put in during
                const int follows = scenes - 1;
                hit(rig, follows, lane, 1, 0.f);
                hit(rig, follows, lane, 5, -0.1f);
                if (scenes == 3) { // the scene put in, the long way
                    hit(rig, 1, lane, 0, -0.25f);
                    hit(rig, 1, lane, 1, 0.f);
                    hit(rig, 1, lane, 9, 0.2f);
                }
                rig.swing(lane, 100);
                rig.feel(lane, time, 0);
                rig.send(make(Command::SetSceneCount, 0, scenes));
                rig.render(64);
                rig.send(make(Command::Play, 0, 0, 0));
            };
            Rig told;
            build(told, 3);
            told.render(whole - 64, 64);
            const std::vector<long> want = told.onsets();
            CHECK(want.size() >= 6);
            for (int into : {40, 700, 2100}) { // (before the step's own swung hit has sounded, and after)
                Rig moved;
                build(moved, 2);
                moved.render(lastStep + into - 64, 64);
                std::unique_ptr<SceneImage> scenes[kNumTracks];
                scenes[0].reset(new SceneImage);
                scenes[0]->level[0][lane][0] = scenes[0]->level[0][lane][1] = scenes[0]->level[0][lane][9] = 0.8f;
                scenes[0]->nudge[0][lane][0] = -0.25f;
                scenes[0]->nudge[0][lane][9] = 0.2f;
                CHECK(moved.engine->moveScenes(1, 1, scenes, 3, 1, 0));
                moved.render(whole - lastStep - into, 64);
                CHECK(moved.onsets() == want);
                ++same;
            }
        }
    }
    CHECK(same == 18);
}

// ---- --print -------------------------------------------------------------------------------------

// A sum of a busy drum song with no swing anywhere (FEEL, nudges, ratchets, chance, throws, a DUCK,
// tempo changes, played round and played once), to hold beside the engine before SWING.
static void printSums() {
    for (int once = 0; once < 2; ++once) {
        Rig rig(2);
        for (int t : {1, 4}) CHECK(rig.engine->addDrumTrack(t));
        rig.clicks(4);
        for (int t : {0, 1, 4})
            for (int s = 0; s < kSteps; ++s)
                for (int lane = 0; lane < kDrumLanes; ++lane) {
                    if ((s * 3 + lane * 5 + t) % 3 != 0) continue;
                    rig.hit(lane, s, 0.4f + 0.07f * lane, (s + lane) % 5 == 1 ? 0.35f : ((s + lane) % 5 == 3 ? -0.3f : 0.f), (s + lane) % 7 == 0 ? 2 + lane % 3 : 0, (s + t) % 6 == 0 ? 1 + s % 3 : 0, t);
                    if ((s + lane) % 4 == 2) rig.send(make(Command::SetStepChance, t, lane, s, 0.5f));
                }
        for (int lane = 0; lane < kDrumLanes; ++lane) rig.feel(lane, lane % 3 ? 50 : 0, lane % 2 ? 75 : 0, 4);
        CHECK(rig.engine->setEffect(kEchoBus, 0, EffectKind::Tape) && rig.engine->setEffect(kSpaceBus, 0, EffectKind::Plate));
        CHECK(rig.engine->setEffect(1, 0, EffectKind::Duck));
        rig.send(make(Command::SetTempo, 0, 0, 0, 133.f));
        rig.render(480, 100);
        rig.play(once != 0);
        const float to[3] = {97.f, 171.f, 133.f};
        for (int k = 0; k < 3; ++k) {
            rig.render(kBar + 1237 * (k + 1), 100);
            rig.send(make(Command::SetTempo, 0, 0, 0, to[k]));
        }
        rig.render(kBar, 100);
        std::uint64_t sum = 1469598103934665603ull;
        for (float f : rig.out) {
            std::uint32_t bits;
            std::memcpy(&bits, &f, sizeof bits);
            sum = (sum ^ bits) * 1099511628211ull;
        }
        std::printf("busy drum song, %s: %016llx (%zu samples)\n", once ? "once" : "round", static_cast<unsigned long long>(sum), rig.out.size());
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--print") == 0) {
        printSums();
        return failures == 0 ? 0 : 1;
    }
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testRule", testRule}, {"testAtRest", testAtRest}, {"testPlaces", testPlaces}, {"testNudgeAndTime", testNudgeAndTime}, {"testRatchet", testRatchet},
        {"testBlocksAndHelpers", testBlocksAndHelpers}, {"testPlayTwiceAndOnce", testPlayTwiceAndOnce}, {"testTurnedWhilePlaying", testTurnedWhilePlaying},
        {"testSlices", testSlices}, {"testDuckAndThrow", testDuckAndThrow}, {"testGoneAndLimits", testGoneAndLimits}, {"testASceneMoved", testASceneMoved},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("swing_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
