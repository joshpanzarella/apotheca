// FEEL: HUMAN TIME and HUMAN LEVEL for each lane of a drum track, live (feel_rule.hpp).
//
// Nobody listens to these: they are measured. A lane is given a recording that is one sample of
// 1 and then silence, so every hit is one sample in the output: where it is says when the hit
// came, how tall it is says how loud.
//
//   1. the rule by hand: the rolls, the main beats, the most, a run of slices
//   2. a lane at rest plays as it always did, to the bit, with a felt lane beside it
//   3. HUMAN TIME: every hit where the rule says, another place each time round, the beats half
//   4. HUMAN LEVEL: every hit as loud as the rule says, on the grid
//   5. the song's first step lands on the top; a nudge and the feel add up
//   6. a ratchet's pieces move together
//   7. the same to the bit at any block length and on any number of helpers
//   8. Play from the top is the same performance again
//   9. played once (a bounce): the last step asks nothing early of the first
//  10. the feel turned while the song plays: no hit lost, none played twice
//  11. slices in order still carry the sound on: a run moves as one
//  12. DUCK is told a moved hit where it sounds; a thrown hit is thrown where it sounds
//  13. a track taken away leaves no feel behind; the command's numbers are held to their limits
//  14. asked again after what follows has changed: on the same sample, or on its step, never lost
//  15. a scene put in during the last step before it: its felt hits where a song built so has them

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

static const int kStep = 6000;       // a step at 120 BPM, 48 kHz
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
    void play(bool once = false) {
        Command c = make(Command::Play, 0, 0, once ? 0 : 1);
        c.c = once ? 1 : 0;
        send(c);
    }
    // Where the output is not silent, from `from` up to `to`.
    std::vector<long> onsets(long from = 0, long to = -1) const {
        std::vector<long> at;
        const long end = to < 0 || to > static_cast<long>(out.size()) ? static_cast<long>(out.size()) : to;
        for (long i = from; i < end; ++i)
            if (out[static_cast<std::size_t>(i)] != 0.f) at.push_back(i);
        return at;
    }
};

// Where the rule puts the hit of `step` of bar `bar` of the cell, played as step number `n` since
// Play (a loop of one bar: n % 16 == step), in frames: `head` the step its run begins with.
static long placeOf(int track, int lane, int bar, int step, long n, int time, float nudge = 0.f, int head = -1) {
    if (head < 0) head = step;
    const std::uint32_t seed = feelSeed(track, lane, bar, head, n - (step - head));
    const double moved = std::ceil(static_cast<double>(nudge) * kStep + feelSamples(time, head, seed, kPerMs, kStep));
    const long at = n * kStep + static_cast<long>(moved);
    return n == 0 && at < 0 ? 0 : at;
}

// ---- 1 -------------------------------------------------------------------------------------------

static void testRule() {
    // The main beats: steps 1, 5, 9, 13.
    for (int step = 0; step < kSteps; ++step) CHECK(feelOnBeat(step) == (step == 0 || step == 4 || step == 8 || step == 12));
    CHECK(feelShare(0) == 0.5 && feelShare(1) == 1.0 && feelShare(12) == 0.5 && feelShare(15) == 1.0);
    // The most, by hand: 12 ms at 48 kHz is 576 samples; 6 ms on a beat; in proportion to the amount.
    const std::uint32_t seed = feelSeed(0, 2, 0, 3, 19);
    const double roll = feelTimeRoll(seed);
    CHECK(roll >= -1.0 && roll < 1.0 && roll != 0.0);
    CHECK(std::fabs(feelSamples(100, 3, seed, kPerMs, kStep) - 576.0 * roll) < 1e-9);
    CHECK(std::fabs(feelSamples(100, 4, seed, kPerMs, kStep) - 288.0 * roll) < 1e-9);
    CHECK(std::fabs(feelSamples(25, 3, seed, kPerMs, kStep) - 144.0 * roll) < 1e-9);
    CHECK(feelSamples(0, 3, seed, kPerMs, kStep) == 0.0);
    // Never more than a quarter of a step, however short the step.
    CHECK(std::fabs(feelSamples(100, 3, seed, kPerMs, 400.0) - 100.0 * roll) < 1e-9);
    // HUMAN LEVEL: 15% of the level, 7.5% on a beat; held between the quietest and full.
    const double lroll = feelLevelRoll(seed);
    CHECK(lroll >= -1.0 && lroll < 1.0 && lroll != roll);
    CHECK(std::fabs(feelLevel(0.5f, 100, 3, seed) - 0.5 * (1.0 + 0.15 * lroll)) < 1e-6);
    CHECK(std::fabs(feelLevel(0.5f, 100, 8, seed) - 0.5 * (1.0 + 0.075 * lroll)) < 1e-6);
    CHECK(std::fabs(feelLevel(0.5f, 50, 3, seed) - 0.5 * (1.0 + 0.075 * lroll)) < 1e-6);
    CHECK(feelLevel(0.5f, 0, 3, seed) == 0.5f);
    for (int n = 0; n < 200; ++n) {
        const std::uint32_t s = feelSeed(1, 0, 0, 1, n);
        CHECK(feelLevel(1.f, 100, 1, s) <= 1.f);
        CHECK(feelLevel(0.021f, 100, 1, s) >= 0.02f);
    }
    // The rolls: everything that says where and which time changes them; spread both ways.
    CHECK(feelSeed(0, 2, 0, 3, 19) != feelSeed(1, 2, 0, 3, 19));
    CHECK(feelSeed(0, 2, 0, 3, 19) != feelSeed(0, 3, 0, 3, 19));
    CHECK(feelSeed(0, 2, 0, 3, 19) != feelSeed(0, 2, 1, 3, 19));
    CHECK(feelSeed(0, 2, 0, 3, 19) != feelSeed(0, 2, 0, 4, 19));
    CHECK(feelSeed(0, 2, 0, 3, 19) != feelSeed(0, 2, 0, 3, 35));
    double sum = 0.0, low = 1.0, high = -1.0;
    int early = 0;
    for (int n = 0; n < 4000; ++n) {
        const double r = feelTimeRoll(feelSeed(0, n % 8, 0, n % 16, n));
        sum += r;
        low = r < low ? r : low;
        high = r > high ? r : high;
        early += r < 0.0;
    }
    CHECK(std::fabs(sum / 4000.0) < 0.05 && low < -0.95 && high > 0.95);
    CHECK(early > 1800 && early < 2200);
    // Numbers from outside.
    CHECK(laneFeelOf(-5, 300).time == 0 && laneFeelOf(-5, 300).level == 100);
    CHECK(laneFeelOf(25, 75).time == 25 && laneFeelOf(25, 75).level == 75);
    CHECK(LaneFeel().atRest() && !laneFeelOf(10, 0).atRest() && !laneFeelOf(0, 10).atRest());
    // A run of slices: a hit from further into its sound, after a hit on the step before, takes
    // the step its run begins with. Step 1 always begins one; a gap ends one; a hit from the top begins one.
    float level[kSteps] = {}, start[kSteps] = {};
    for (int s : {0, 1, 2, 3, 6, 7, 9, 10, 11}) level[s] = 0.8f;
    start[1] = 0.1f;
    start[2] = 0.2f;
    start[3] = 0.3f;
    start[6] = 0.5f; // nothing on the step before it
    start[7] = 0.6f;
    start[10] = 0.f; // from the top: its own
    start[11] = 0.7f;
    start[0] = 0.9f; // the bar's first step: its own
    const int want[kSteps] = {0, 0, 0, 0, 4, 5, 6, 6, 8, 9, 10, 10, 12, 13, 14, 15};
    // The rule the engine asks: how far back the run begins, over the bar lines inside the cell.
    struct OneBar {
        float level[kDrumLanes][kSteps] = {};
        float start[kDrumLanes][kSteps] = {};
    };
    std::unique_ptr<OneBar[]> cell(new OneBar[3]);
    for (int s = 0; s < kSteps; ++s) {
        cell[0].level[2][s] = level[s];
        cell[0].start[2][s] = start[s];
    }
    for (int s = 0; s < kSteps; ++s) CHECK(feelRunBack(cell.get(), 0, 2, s) == s - want[s]);
    // Bar 2 goes on from bar 1: its first step starts further in, and bar 1's last step has a hit.
    cell[0].level[2][14] = cell[0].level[2][15] = 0.8f;
    cell[0].start[2][14] = 0.f, cell[0].start[2][15] = 0.95f;
    for (int b = 1; b < 3; ++b)
        for (int s = 0; s < 4; ++s) cell[b].level[2][s] = 0.8f, cell[b].start[2][s] = 0.1f * (s + 1);
    CHECK(feelRunBack(cell.get(), 1, 2, 0) == 2);  // back to step 15 of bar 1, whose run begins on 14
    CHECK(feelRunBack(cell.get(), 1, 2, 3) == 5);
    CHECK(feelRunBack(cell.get(), 1, 3, 0) == 0);  // another lane: nothing there
    CHECK(feelRunBack(cell.get(), 2, 2, 0) == 0);  // bar 2's last step is empty: bar 3 begins its own
    CHECK(feelRunBack(cell.get(), 2, 2, 2) == 2);
    cell[1].start[2][0] = 0.f;                     // from the top: its own, whatever is before it
    CHECK(feelRunBack(cell.get(), 1, 2, 0) == 0 && feelRunBack(cell.get(), 1, 2, 3) == 3);
    // The cell's first bar never goes back (to its last bar: another time round).
    cell[2].level[2][15] = 0.8f;
    CHECK(feelRunBack(cell.get(), 0, 2, 0) == 0);
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
    // Lanes 1 and 2 at rest, the kit's own drums; lane 3 felt (and silent: a click at level... its own
    // hits taken out of the comparison by giving it none).
    Rig plain, felt;
    busy(plain);
    busy(felt);
    felt.feel(3, 100, 100); // a lane with no hits
    felt.feel(0, 50, 50);
    felt.feel(0, 0, 0); // ...and one put back at rest before Play
    plain.play();
    felt.play();
    plain.render(3 * kBar, 256);
    felt.render(3 * kBar, 256);
    CHECK(plain.out == felt.out);
    CHECK(*std::max_element(plain.out.begin(), plain.out.end()) > 0.1f);
    // Played with a feel, stopped part way through a bar, the feel taken off, played again: the
    // lane is plain again from the top, every hit there (nothing is remembered from the playing before).
    Rig was, never;
    for (Rig* rig : {&was, &never}) {
        rig->clicks(0);
        for (int s = 0; s < kSteps; ++s) rig->hit(0, s);
    }
    was.feel(0, 100, 100);
    was.play();
    was.render(5 * kStep + 2000);
    was.send(make(Command::Stop));
    was.feel(0, 0, 0);
    was.render(4000);
    was.out.clear();
    was.play();
    never.play();
    was.render(2 * kBar);
    never.render(2 * kBar);
    CHECK(was.out == never.out);
    CHECK(never.onsets().size() == 32);
}

// ---- 3 -------------------------------------------------------------------------------------------

static void testTime() {
    Rig rig;
    rig.clicks(0);
    const int lane = 2;
    for (int s = 0; s < kSteps; ++s) rig.hit(lane, s);
    rig.feel(lane, 100, 0);
    rig.play();
    const int rounds = 6;
    rig.render(rounds * kBar + kStep / 2);
    const std::vector<long> at = rig.onsets(0, rounds * kBar - kStep / 2);
    CHECK(static_cast<int>(at.size()) == rounds * kSteps); // none lost, none twice
    int beatsFar = 0, moved = 0, same = 0;
    long most = 0, mostBeat = 0;
    for (int n = 0; n < rounds * kSteps && n < static_cast<int>(at.size()); ++n) {
        const int step = n % kSteps;
        CHECK(at[static_cast<std::size_t>(n)] == placeOf(0, lane, 0, step, n, 100));
        const long off = at[static_cast<std::size_t>(n)] - static_cast<long>(n) * kStep;
        if (off != 0) ++moved;
        if (feelOnBeat(step)) {
            if (std::labs(off) > 288) ++beatsFar;
            mostBeat = std::labs(off) > mostBeat ? std::labs(off) : mostBeat;
        } else {
            most = std::labs(off) > most ? std::labs(off) : most;
        }
        CHECK(std::labs(off) <= 576);
        if (n >= kSteps && n < 2 * kSteps + kSteps) { // each time round another place
            const long before = at[static_cast<std::size_t>(n - kSteps)] - static_cast<long>(n - kSteps) * kStep;
            if (before == off) ++same;
        }
    }
    CHECK(beatsFar == 0);         // a main beat: never more than 6 ms
    CHECK(mostBeat > 150);        // ...but it moves
    CHECK(most > 400);            // between them: up to 12 ms
    CHECK(moved > rounds * kSteps - 6);
    CHECK(same <= 1);
    // Half the amount: half as far, the same way (the roll is the hit's, not the amount's).
    Rig half;
    half.clicks(0);
    for (int s = 0; s < kSteps; ++s) half.hit(lane, s);
    half.feel(lane, 50, 0);
    half.play();
    half.render(2 * kBar);
    const std::vector<long> h = half.onsets(0, kBar + kBar / 2);
    for (int n = 1; n < 20 && n < static_cast<int>(h.size()); ++n) {
        const double full = static_cast<double>(at[static_cast<std::size_t>(n)] - static_cast<long>(n) * kStep);
        const double got = static_cast<double>(h[static_cast<std::size_t>(n)] - static_cast<long>(n) * kStep);
        CHECK(std::fabs(got - full / 2.0) <= 1.0);
    }
    // A cell two bars long: the second bar's hits roll as the second bar's (and the third round is the first bar again).
    Rig two;
    two.clicks(0);
    Command bars = make(Command::SetCellBars, 0, 2);
    two.send(bars);
    for (int s = 0; s < kSteps; ++s) {
        two.hit(lane, s);
        two.hit(lane, s, 0.8f, 0.f, 0, 0, 0, 1);
    }
    two.feel(lane, 100, 0);
    two.play();
    two.render(3 * kBar);
    const std::vector<long> t = two.onsets(0, 3 * kBar - kStep / 2);
    CHECK(t.size() == 48);
    int otherBar = 0;
    for (int n = 1; n < 47 && n < static_cast<int>(t.size()); ++n) {
        CHECK(t[static_cast<std::size_t>(n)] == placeOf(0, lane, (n / kSteps) % 2, n % kSteps, n, 100));
        otherBar += t[static_cast<std::size_t>(n)] != placeOf(0, lane, 0, n % kSteps, n, 100);
    }
    CHECK(otherBar >= 12);
    // Another lane, another track: other rolls.
    Rig other;
    CHECK(other.engine->addDrumTrack(3));
    other.clicks(3);
    for (int s = 0; s < kSteps; ++s) other.hit(lane, s, 0.8f, 0.f, 0, 0, 3);
    other.feel(lane, 100, 0, 3);
    other.play();
    other.render(2 * kBar);
    const std::vector<long> o = other.onsets(0, kBar + kBar / 2);
    int alike = 0;
    for (int n = 1; n < 20 && n < static_cast<int>(o.size()); ++n) {
        CHECK(o[static_cast<std::size_t>(n)] == placeOf(3, lane, 0, n % kSteps, n, 100));
        alike += o[static_cast<std::size_t>(n)] == at[static_cast<std::size_t>(n)];
    }
    CHECK(alike <= 1);
}

// ---- 4 -------------------------------------------------------------------------------------------

static void testLevel() {
    Rig plain, felt;
    for (Rig* rig : {&plain, &felt}) {
        rig->clicks(0);
        for (int s = 0; s < kSteps; ++s) rig->hit(4, s, 0.6f);
    }
    felt.feel(4, 0, 100);
    plain.play();
    felt.play();
    plain.render(4 * kBar);
    felt.render(4 * kBar);
    const std::vector<long> a = plain.onsets(), b = felt.onsets();
    CHECK(a == b); // HUMAN LEVEL alone moves nothing in time
    CHECK(static_cast<int>(a.size()) == 4 * kSteps);
    int louder = 0, quieter = 0;
    double mostBeat = 0.0, most = 0.0;
    for (std::size_t n = 0; n < a.size(); ++n) {
        const int step = static_cast<int>(n % kSteps);
        const double want = feelLevel(0.6f, 100, step, feelSeed(0, 4, 0, step, static_cast<long>(n))) / 0.6;
        const double got = felt.out[static_cast<std::size_t>(a[n])] / plain.out[static_cast<std::size_t>(a[n])];
        CHECK(std::fabs(got - want) < 2e-5);
        louder += got > 1.0;
        quieter += got < 1.0;
        (feelOnBeat(step) ? mostBeat : most) = std::max(feelOnBeat(step) ? mostBeat : most, std::fabs(got - 1.0));
    }
    CHECK(louder > 20 && quieter > 20);
    CHECK(mostBeat <= 0.0751 && mostBeat > 0.04 && most <= 0.1501 && most > 0.12);
}

// ---- 5 -------------------------------------------------------------------------------------------

static void testTopAndNudge() {
    // Many lanes and tracks, so that some first hits roll early: they land on the top.
    Rig rig;
    for (int t : {2, 5}) CHECK(rig.engine->addDrumTrack(t));
    int early = 0;
    for (int t : {0, 2, 5}) {
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            if (feelTimeRoll(feelSeed(t, lane, 0, 0, 0)) < -0.1) ++early;
        }
    }
    CHECK(early >= 5);
    // One at a time (clicks at one moment would add up).
    for (int t : {0, 2, 5}) {
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            Rig one;
            if (t) CHECK(one.engine->addDrumTrack(t));
            one.clicks(t);
            one.hit(lane, 0, 0.8f, 0.f, 0, 0, t);
            one.hit(lane, 5, 0.8f, 0.3f, 0, 0, t);   // late by a nudge
            one.hit(lane, 10, 0.8f, -0.3f, 0, 0, t); // early by a nudge
            one.feel(lane, 100, 0, t);
            one.play();
            one.render(2 * kBar + kStep);
            const std::vector<long> at = one.onsets(0, 2 * kBar + kStep / 2);
            CHECK(at.size() == 7);
            if (at.size() != 7) continue;
            CHECK(at[0] == placeOf(t, lane, 0, 0, 0, 100)); // at the top or after it, never lost
            CHECK(at[0] >= 0 && at[0] <= 288);
            CHECK(at[1] == placeOf(t, lane, 0, 5, 5, 100, 0.3f));
            CHECK(at[2] == placeOf(t, lane, 0, 10, 10, 100, -0.3f));
            CHECK(at[3] == placeOf(t, lane, 0, 0, 16, 100)); // the second time round: early if it rolls so
            CHECK(at[4] == placeOf(t, lane, 0, 5, 21, 100, 0.3f));
            CHECK(at[5] == placeOf(t, lane, 0, 10, 26, 100, -0.3f));
            CHECK(at[6] == placeOf(t, lane, 0, 0, 32, 100));
        }
    }
}

// ---- 6 -------------------------------------------------------------------------------------------

static void testRatchet() {
    Rig rig;
    rig.clicks(0);
    rig.hit(1, 3, 0.7f, 0.f, 4);
    rig.hit(1, 9, 0.7f, 0.2f, 3);
    rig.feel(1, 100, 100);
    rig.play();
    rig.render(3 * kBar);
    const std::vector<long> at = rig.onsets(0, 3 * kBar - kStep);
    CHECK(at.size() == 21);
    if (at.size() != 21) return;
    for (int round = 0; round < 3; ++round) {
        const std::size_t i = static_cast<std::size_t>(round * 7);
        const long n = round * kSteps;
        CHECK(at[i] == placeOf(0, 1, 0, 3, n + 3, 100));
        for (int k = 1; k < 4; ++k) {
            CHECK(at[i + static_cast<std::size_t>(k)] == at[i] + k * (kStep / 4)); // together, evenly
            CHECK(rig.out[static_cast<std::size_t>(at[i + static_cast<std::size_t>(k)])] == rig.out[static_cast<std::size_t>(at[i])]); // one level for the hit
        }
        CHECK(at[i + 4] == placeOf(0, 1, 0, 9, n + 9, 100, 0.2f));
        for (int k = 1; k < 3; ++k) CHECK(at[i + 4 + static_cast<std::size_t>(k)] == at[i + 4] + k * (kStep / 3));
    }
    CHECK(rig.out[static_cast<std::size_t>(at[0])] != rig.out[static_cast<std::size_t>(at[7])]); // another level the next time round
    // A ratchet on the song's first step, on a lane whose first roll is early: its first piece
    // is on the top and the rest are counted from there.
    int lane = -1;
    for (int l = 0; l < kDrumLanes && lane < 0; ++l)
        if (feelTimeRoll(feelSeed(0, l, 0, 0, 0)) < -0.3) lane = l;
    CHECK(lane >= 0);
    Rig top;
    top.clicks(0);
    top.hit(lane, 0, 0.7f, 0.f, 3);
    top.feel(lane, 100, 0);
    top.play();
    top.render(kBar / 2);
    const std::vector<long> first = top.onsets();
    CHECK(first.size() == 3);
    if (first.size() == 3) CHECK(first[0] == 0 && first[1] == 2000 && first[2] == 4000);
}

// ---- 7, 8 ----------------------------------------------------------------------------------------

// Three drum tracks, the kit's own drums and recordings, every lane felt its own way, nudges,
// ratchets, throws into real effects, a tempo that makes a step a part of a sample long.
static std::vector<float> feelSong(int helpers, int block, float tempo = 133.f, bool tempoTurned = false) {
    Rig rig(helpers);
    for (int t : {1, 4}) CHECK(rig.engine->addDrumTrack(t));
    rig.clicks(4);
    const int amounts[6] = {0, 10, 25, 50, 75, 100};
    for (int t : {0, 1, 4}) {
        for (int s = 0; s < kSteps; ++s) {
            for (int lane = 0; lane < kDrumLanes; ++lane) {
                if ((s * 3 + lane * 5 + t) % 4 != 0) continue;
                // (Track 1 has HUMAN LEVEL alone, and so no nudge and no ratchet: a plain lane's are a part
                // of a sample long, and play a sample sooner or later by the block, as they always did.)
                const bool plain = t == 1;
                rig.hit(lane, s, 0.5f + 0.05f * lane, plain ? 0.f : ((s + lane) % 5 == 1 ? 0.35f : ((s + lane) % 5 == 3 ? -0.3f : 0.f)), !plain && (s + lane) % 7 == 0 ? 2 + lane % 3 : 0,
                        (s + t) % 6 == 0 ? 1 + s % 3 : 0, t);
            }
        }
        for (int lane = 0; lane < kDrumLanes; ++lane) rig.feel(lane, t == 1 ? 0 : amounts[1 + (lane + t) % 5], amounts[(lane * 2 + t + 1) % 6], t);
    }
    CHECK(rig.engine->setEffect(kEchoBus, 0, EffectKind::Tape) && rig.engine->setEffect(kSpaceBus, 0, EffectKind::Plate));
    CHECK(rig.engine->setEffect(1, 0, EffectKind::Duck));
    rig.send(make(Command::SetTempo, 0, 0, 0, tempo));
    rig.render(480, block);
    rig.play();
    if (tempoTurned) {
        // The tempo turned while felt hits wait, at moments that are no step's and no block's.
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
    const std::vector<float> usual = feelSong(0, 256);
    CHECK(*std::max_element(usual.begin(), usual.end()) > 0.1f);
    for (int block : {1024, 64, 37, 5}) CHECK(feelSong(0, block) == usual);
    for (int helpers : {1, 2, 3}) CHECK(feelSong(helpers, 256) == usual);
    CHECK(feelSong(3, 37) == usual);
    // The tempo turned while the song plays: a felt hit that was waiting is still whole samples off.
    const std::vector<float> turned = feelSong(0, 256, 133.f, true);
    CHECK(turned != usual);
    for (int block : {1, 5, 37, 64, 333}) CHECK(feelSong(0, block, 133.f, true) == turned);
    CHECK(feelSong(3, 1, 133.f, true) == turned);
    // (and it is another song without the feel: the fixture reaches it)
}

static void testPlayTwice() {
    // Only the clicks and the kit's short drums, no buses: silence comes back before the second Play.
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
            rig.feel(lane, 100, 75);
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
        // ...and the rounds within one playing are not alike.
        CHECK(!std::equal(first.begin() + kBar, first.begin() + 2 * kBar, first.begin() + 2 * kBar));
    }
}

// ---- 9 -------------------------------------------------------------------------------------------

static void testOnce() {
    // A lane and a track whose first hit rolls early the second time round (step number 16).
    int track = -1, lane = -1;
    for (int t = 0; t < kNumTracks && track < 0; ++t)
        for (int l = 0; l < kDrumLanes && track < 0; ++l)
            if (feelTimeRoll(feelSeed(t, l, 0, 0, 16)) < -0.5) {
                track = t;
                lane = l;
            }
    CHECK(track >= 0);
    if (track < 0) return;
    for (int once = 0; once < 2; ++once) {
        Rig rig;
        if (track) CHECK(rig.engine->addDrumTrack(track));
        rig.clicks(track);
        rig.hit(lane, 0, 0.8f, 0.f, 0, 0, track);
        rig.feel(lane, 100, 0, track);
        Command c = make(Command::Play, 0, 0, 0); // the song (one scene of one bar), not a scene alone
        c.c = once;
        rig.send(c);
        rig.render(kBar + kStep);
        const std::vector<long> at = rig.onsets();
        if (once) { // nothing of the first step in the song's last moments (a bounce stops there itself)
            CHECK(at.size() == 2);
            if (at.size() == 2) CHECK(at[1] >= kBar);
        } else {
            CHECK(at.size() == 2);
            if (at.size() == 2) CHECK(at[1] < kBar && at[1] == placeOf(track, lane, 0, 0, 16, 100));
        }
    }
}

// ---- 10 ------------------------------------------------------------------------------------------

static void testTurnedWhilePlaying() {
    // The feel turned on and off, at every kind of moment in a step, while every step has a hit:
    // each step still has exactly one, within 12 ms of its place.
    for (int phase = 0; phase < 12; ++phase) {
        Rig rig;
        rig.clicks(0);
        for (int s = 0; s < kSteps; ++s) rig.hit(0, s, 0.8f, s % 4 == 2 ? -0.2f : (s % 4 == 3 ? 0.2f : 0.f));
        rig.play();
        const int total = 8 * kBar;
        int done = 0, turn = 0;
        rig.render(phase * 517 + 13, 64);
        done += phase * 517 + 13;
        while (done < total) {
            const int amounts[4] = {100, 0, 25, 0};
            rig.feel(0, amounts[turn % 4], turn % 3 == 0 ? 50 : 0);
            ++turn;
            const int n = 2 * kStep + 911 + 373 * (turn % 5);
            rig.render(n, 64);
            done += n;
        }
        const std::vector<long> at = rig.onsets(0, total - kStep);
        const int steps = total / kStep - 1;
        CHECK(static_cast<int>(at.size()) == steps);
        std::vector<int> seen(static_cast<std::size_t>(steps + 2), 0);
        for (long a : at) {
            // which step it is the hit of: its place less its nudge, to the nearest step
            int best = -1;
            for (int n = static_cast<int>(a / kStep) - 1; n <= static_cast<int>(a / kStep) + 1; ++n) {
                if (n < 0 || n > steps) continue;
                const int s = n % kSteps;
                const double nudge = s % 4 == 2 ? -0.2 : (s % 4 == 3 ? 0.2 : 0.0);
                if (std::fabs(static_cast<double>(a) - (n + nudge) * kStep) <= 577.0) best = n;
            }
            CHECK(best >= 0);
            if (best >= 0) ++seen[static_cast<std::size_t>(best)];
        }
        for (int n = 0; n < steps; ++n) CHECK(seen[static_cast<std::size_t>(n)] == 1);
    }
}

// ---- 11 ------------------------------------------------------------------------------------------

// A recording two bars long, cut into thirty-two and played in order through both bars of its
// cell: one unbroken sound, over the bar line inside the cell too. With HUMAN TIME the whole run
// moves as one (bar 2 rolled by itself met bar 1's sound up to 9 ms off and cut it).
static void testSlicesOverABarLine() {
    const auto build = [](Rig& rig, int time) {
        std::unique_ptr<SampleData> s(new SampleData);
        s->data.resize(static_cast<std::size_t>(2 * kBar));
        for (int i = 0; i < 2 * kBar; ++i) s->data[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::sin(i * 0.031 + 0.5e-7 * i * i));
        CHECK(rig.engine->setLaneSample(0, 0, std::move(s)));
        Command bars = make(Command::SetCellBars, 0, 2);
        rig.send(bars);
        rig.send(make(Command::SetLanePart, 0, 0, 0, 1.f / 32.f));
        for (int k = 0; k < 2 * kSteps; ++k) {
            rig.hit(0, k % kSteps, 0.8f, 0.f, 0, 0, 0, k / kSteps);
            Command c = make(Command::SetStepStart, 0, 0, k % kSteps, k / 32.f);
            c.bar = k / kSteps;
            rig.send(c);
        }
        rig.feel(0, time, 0);
        rig.play();
        rig.render(8 * kBar);
    };
    Rig plain, felt;
    build(plain, 0);
    build(felt, 100);
    int movedRounds = 0, barsApart = 0;
    for (int round = 1; round < 4; ++round) {
        const long n = round * 2 * kSteps;
        const long shift = placeOf(0, 0, 0, 0, n, 100) - n * kStep; // the run's first hit: bar 1, step 1
        if (shift != 0) ++movedRounds;
        if (placeOf(0, 0, 1, 0, n + kSteps, 100) - (n + kSteps) * kStep != shift) ++barsApart; // what bar 2 would roll by itself
        float worst = 0.f, loudest = 0.f;
        for (long i = n * kStep + 600; i < (n + 2 * kSteps) * kStep - 700; ++i) {
            const float a = plain.out[static_cast<std::size_t>(i)], b = felt.out[static_cast<std::size_t>(i + shift)];
            worst = std::fabs(a - b) > worst ? std::fabs(a - b) : worst;
            loudest = std::fabs(a) > loudest ? std::fabs(a) : loudest;
        }
        CHECK(loudest > 0.3f);
        CHECK(worst == 0.f); // the same sound to the sample, through the bar line
    }
    CHECK(movedRounds >= 2 && barsApart >= 2); // (the fixture reaches it: bar 2's own roll is another)
}

static void testSlices() {
    // A recording one bar long, cut into sixteen and played in order on the first eight steps:
    // one unbroken sound. With HUMAN TIME the run moves as one, by its first hit's roll, and is
    // still unbroken: the same samples, sooner or later.
    const auto build = [](Rig& rig, int time, int level) {
        std::unique_ptr<SampleData> s(new SampleData);
        s->data.resize(static_cast<std::size_t>(kBar));
        for (int i = 0; i < kBar; ++i) s->data[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::sin(i * 0.031 + 1e-7 * i * i));
        CHECK(rig.engine->setLaneSample(0, 0, std::move(s)));
        rig.send(make(Command::SetLanePart, 0, 0, 0, 1.f / 16.f));
        for (int k = 0; k < 8; ++k) {
            rig.hit(0, k, 0.8f);
            rig.send(make(Command::SetStepStart, 0, 0, k, k / 16.f));
        }
        rig.feel(0, time, level);
        rig.play();
        rig.render(4 * kBar);
    };
    Rig plain, felt;
    build(plain, 0, 0);
    build(felt, 100, 0);
    int movedRounds = 0;
    for (int round = 1; round < 4; ++round) {
        const long n = round * kSteps;
        const long shift = placeOf(0, 0, 0, 0, n, 100) - n * kStep; // the run's first hit
        if (shift != 0) ++movedRounds;
        float worst = 0.f, loudest = 0.f;
        for (long i = n * kStep + 600; i < n * kStep + 9 * kStep; ++i) { // (from 600: clear of the round before's end)
            const float a = plain.out[static_cast<std::size_t>(i)], b = felt.out[static_cast<std::size_t>(i + shift)];
            worst = std::fabs(a - b) > worst ? std::fabs(a - b) : worst;
            loudest = std::fabs(a) > loudest ? std::fabs(a) : loudest;
        }
        CHECK(loudest > 0.3f);
        CHECK(worst == 0.f); // the same sound, to the sample: no seam anywhere in the run
    }
    CHECK(movedRounds >= 2);
    // HUMAN LEVEL in a run is each hit's own: the sound still carries on (no jump), at another level.
    Rig loud;
    build(loud, 100, 100);
    float jump = 0.f, plainJump = 0.f;
    for (long i = kBar + 601; i < kBar + 7 * kStep; ++i) {
        jump = std::max(jump, std::fabs(loud.out[static_cast<std::size_t>(i)] - loud.out[static_cast<std::size_t>(i - 1)]));
        plainJump = std::max(plainJump, std::fabs(plain.out[static_cast<std::size_t>(i)] - plain.out[static_cast<std::size_t>(i - 1)]));
    }
    CHECK(jump < plainJump * 1.3f);
    CHECK(loud.out != felt.out);
    // Each slice's LEVEL is its own roll, not its run's: in the middle of step k the sound is the
    // plain sound times what the rule gives step k (the run did not move: HUMAN TIME is 0).
    Rig own;
    build(own, 0, 100);
    int steps = 0, apart = 0;
    for (int round = 1; round < 4; ++round) {
        double before = -1.0;
        for (int k = 0; k < 8; ++k) {
            const long n = round * kSteps + k, from = n * kStep + kStep / 2;
            double a = 0.0, b = 0.0;
            for (long i = from; i < from + kStep / 4; ++i) {
                a += std::fabs(plain.out[static_cast<std::size_t>(i)]);
                b += std::fabs(own.out[static_cast<std::size_t>(i)]);
            }
            const double want = feelLevel(0.8f, 100, k, feelSeed(0, 0, 0, k, n)) / 0.8;
            CHECK(a > 1.0 && std::fabs(b / a - want) < 0.002);
            if (before >= 0.0 && std::fabs(want - before) > 0.01) ++apart;
            before = want;
            ++steps;
        }
    }
    CHECK(steps == 24 && apart >= 12); // (the fixture tells one slice's level from the next's)
    testSlicesOverABarLine();
}

// ---- 12 ------------------------------------------------------------------------------------------

static void testDuckAndThrow() {
    Rig rig;
    Listener* listener = new Listener;
    Probe* echo = new Probe;
    rig.engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(listener));
    rig.engine->setBusEffect(0, std::unique_ptr<Effect>(echo));
    rig.clicks(0);
    for (int s = 0; s < kSteps; s += 2) rig.hit(5, s, 0.8f, 0.f, 0, kThrowEcho);
    rig.feel(5, 100, 0);
    rig.play();
    rig.render(3 * kBar);
    const std::vector<long> at = rig.onsets(0, 3 * kBar - kStep);
    CHECK(at.size() == 24);
    // Told on the very frame each hit sounds.
    CHECK(listener->told[5].size() >= at.size());
    for (std::size_t i = 0; i < at.size() && i < listener->told[5].size(); ++i) CHECK(listener->told[5][i] == at[i]);
    for (int lane = 0; lane < kDrumLanes; ++lane)
        if (lane != 5) CHECK(listener->told[lane].empty());
    // Thrown on the very frame it sounds (the bus wakes with the first hit).
    const long woke = static_cast<long>(rig.out.size()) - static_cast<long>(echo->fed.size());
    std::size_t thrown = 0;
    for (std::size_t i = 0; i < echo->fed.size(); ++i) {
        if (echo->fed[i] == 0.f) continue;
        if (thrown < at.size()) CHECK(woke + static_cast<long>(i) == at[thrown]);
        ++thrown;
    }
    CHECK(thrown >= at.size());
}

// ---- 14 ------------------------------------------------------------------------------------------

// What was asked a step ahead is dropped and asked again when what follows changes (a scene
// move; AT END switched on the song's last step, which is how it is reached here). A felt hit
// asked again comes on the very sample it would have; taken back for good, it does not come; and
// asked again after its early moment has gone by, it comes on its step (never lost, never twice).
static void testAskedAgain() {
    // Two bars in the cell, a tempo that makes a step a part of a sample long, a hit on step 1 of
    // each bar and on the last step: the last step of the song asks bar 1's first hit ahead.
    int earlyLanes = 0;
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const auto build = [&](Rig& rig) {
            rig.clicks(0);
            rig.send(make(Command::SetTempo, 0, 0, 0, 133.f));
            rig.hit(lane, 0);
            rig.hit(lane, 15);
            rig.feel(lane, 100, 0);
            rig.render(64);
            rig.send(make(Command::Play, 0, 0, 0)); // the song: one scene of one bar, round and round
        };
        const double step = 48000.0 * 60.0 / 133.0 / 4.0;
        const int lastStep = static_cast<int>(15 * step) + 1; // the first sample of the last step, or the one after
        Rig alone;
        build(alone);
        alone.render(static_cast<int>(18 * step), 64);
        const std::vector<long> want = alone.onsets();
        CHECK(want.size() == 3); // steps 1 and 16, then the second round's step 1
        // Switched to STOP and back to LOOP, at many moments of the last step.
        int back = 0, late = 0;
        for (int into = 3; into < static_cast<int>(step) - 3; into += 211) {
            Rig rig;
            build(rig);
            rig.render(lastStep + into, 64);
            rig.send(make(Command::SetSongEnd, 0, 1));
            rig.render(5, 64);
            rig.send(make(Command::SetSongEnd, 0, 0));
            rig.render(static_cast<int>(18 * step) - lastStep - into - 5, 64);
            const std::vector<long> got = rig.onsets();
            CHECK(got.size() == want.size()); // none lost, none twice
            if (got.size() != want.size()) continue;
            if (got == want) {
                CHECK(rig.out == alone.out);
                ++back;
            } else { // its early moment had gone by when it was asked again: on its step, and everything else as it was
                ++late;
                CHECK(got[0] == want[0] && got[1] == want[1]);
                CHECK(got[2] > want[2] && got[2] == static_cast<long>(std::ceil(64 + 16 * step - 1e-9)));
                CHECK(lastStep + into + 5 + 64 > want[2]);
            }
        }
        CHECK(back >= 20);
        (void)late;
        // Taken back before its early moment and asked for again after it: it comes on its step.
        const long line = 64 + static_cast<long>(std::ceil(16 * step - 1e-9)); // the first sample of the second round
        if (want.size() == 3 && want[2] < line - 100) {
            ++earlyLanes;
            Rig rig;
            build(rig);
            rig.render(static_cast<int>(want[2]) - 64 - 60, 64);
            rig.send(make(Command::SetSongEnd, 0, 1));
            rig.render(90, 64);
            rig.send(make(Command::SetSongEnd, 0, 0));
            rig.render(static_cast<int>(18 * step) - static_cast<int>(want[2]) + 64 + 60 - 90, 64);
            const std::vector<long> got = rig.onsets();
            CHECK(got.size() == 3);
            if (got.size() == 3) CHECK(got[0] == want[0] && got[1] == want[1] && got[2] == line);
            // The same hit a ratchet, its first piece sounded when what follows changes: it is not begun
            // a second time on its step. (Its pieces still waiting go, as an early hit's always did.)
            Rig ratchet;
            build(ratchet);
            ratchet.hit(lane, 0, 0.8f, 0.f, 4);
            ratchet.render(static_cast<int>(want[2]) - 64 + 30, 64);
            ratchet.send(make(Command::SetSongEnd, 0, 1));
            ratchet.render(20, 64);
            ratchet.send(make(Command::SetSongEnd, 0, 0));
            ratchet.render(static_cast<int>(18 * step) - static_cast<int>(want[2]) + 64 - 50, 64);
            const std::vector<long> pieces = ratchet.onsets(want[2]);
            CHECK(pieces.size() == 1 && pieces[0] == want[2]);
            // ...and the same when its first piece sounds in the asking itself: taken back before its
            // moment, asked again ON the sample it is due (it sounds there, at once), then what follows
            // changed twice more in the step. It has sounded: it keeps its mark.
            Rig atOnce;
            build(atOnce);
            atOnce.hit(lane, 0, 0.8f, 0.f, 4);
            atOnce.render(static_cast<int>(want[2]) - 64 - 60, 64);
            atOnce.send(make(Command::SetSongEnd, 0, 1));
            atOnce.render(60, 64);
            atOnce.send(make(Command::SetSongEnd, 0, 0));
            for (int end : {1, 0}) {
                atOnce.render(20, 64);
                atOnce.send(make(Command::SetSongEnd, 0, end));
            }
            atOnce.render(static_cast<int>(18 * step) - static_cast<int>(want[2]) + 64 - 40, 64);
            const std::vector<long> once = atOnce.onsets(want[2] - 200);
            CHECK(once.size() == 1 && once[0] == want[2]);
        }
        // Taken back for good: STOP on the last step, and the early hit of the top does not come.
        Rig stop;
        build(stop);
        stop.render(lastStep + 40, 64);
        stop.send(make(Command::SetSongEnd, 0, 1));
        stop.render(static_cast<int>(3 * step), 64);
        CHECK(stop.onsets().size() == 2);
    }
    CHECK(earlyLanes >= 2);
}

// ---- 15 ------------------------------------------------------------------------------------------

// A scene put in while the last step of the scene before it plays (NEXT SCENE: Engine::moveScenes).
// That step asked the felt hits of what followed then; what follows now is the new scene. Its felt
// hit comes on the very sample it has in a song built with the scene in it from the start.
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
    int same = 0, goneBy = 0;
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        for (float nudge : {0.f, 0.25f, -0.5f}) {
            // Scene 1: steps 1 and 16. What follows it: `follows` (steps 1 and 5).
            const auto build = [&](Rig& rig, int scenes) {
                rig.clicks(0);
                rig.send(make(Command::SetTempo, 0, 0, 0, 133.f));
                hit(rig, 0, lane, 0, 0.f);
                hit(rig, 0, lane, 15, 0.f);
                const int follows = scenes - 1;
                hit(rig, follows, lane, 0, 0.f);
                hit(rig, follows, lane, 4, 0.f);
                if (scenes == 3) { // the scene put in, the long way: steps 1 (nudged) and 9
                    hit(rig, 1, lane, 0, nudge);
                    hit(rig, 1, lane, 8, 0.f);
                }
                rig.feel(lane, 100, 0);
                rig.send(make(Command::SetSceneCount, 0, scenes));
                rig.render(64);
                rig.send(make(Command::Play, 0, 0, 0));
            };
            Rig told;
            build(told, 3);
            told.render(whole - 64, 64);
            const std::vector<long> want = told.onsets();
            for (int into : {40, 700, 4000, 4800, 5300}) {
                if (nudge != 0.f && into > 4800) continue; // (the hit asked before, on the grid, may have sounded by then: heard, not to be mended)
                Rig moved;
                build(moved, 2);
                moved.render(lastStep + into - 64, 64);
                std::unique_ptr<SceneImage> scenes[kNumTracks];
                scenes[0].reset(new SceneImage);
                scenes[0]->level[0][lane][0] = scenes[0]->level[0][lane][8] = 0.8f;
                scenes[0]->nudge[0][lane][0] = nudge;
                CHECK(moved.engine->moveScenes(1, 1, scenes, 3, 1, 0));
                moved.render(whole - lastStep - into, 64);
                const std::vector<long> got = moved.onsets();
                CHECK(want.size() > 4);
                if (want.size() <= 4) continue;
                if (nudge >= 0.f || want[2] >= lastStep + into) {
                    // Every hit on the sample it has in the song built that way. (On the grid, the hit
                    // asked before and the new scene's are one roll: the same place, sounded or not.)
                    CHECK(got == want);
                    ++same;
                } else {
                    // Written early and its moment gone by at the move: not played late, as a plain
                    // early hit is not. Everything else where it is.
                    CHECK(got.size() + 1 == want.size());
                    if (got.size() + 1 == want.size()) CHECK(got[0] == want[0] && got[1] == want[1] && std::equal(got.begin() + 2, got.end(), want.begin() + 3));
                    ++goneBy;
                }
            }
        }
    }
    CHECK(same >= 80 && goneBy >= 12);
}

// ---- 13 ------------------------------------------------------------------------------------------

static void testGoneAndLimits() {
    Rig rig;
    CHECK(rig.engine->addDrumTrack(2));
    rig.clicks(2);
    for (int s = 0; s < kSteps; ++s) rig.hit(1, s, 0.8f, 0.f, 0, 0, 2);
    rig.feel(1, 100, 100, 2);
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
    for (std::size_t n = 0; n < at.size(); ++n) {
        CHECK(at[n] == static_cast<long>(n) * kStep); // on the grid: the new kit plays plain
        CHECK(rig.out[static_cast<std::size_t>(at[n])] == rig.out[static_cast<std::size_t>(at[0])]);
    }
    // Numbers out of range: held (300 is 100), a lane that is not there changes nothing.
    Rig wide, full;
    for (Rig* r : {&wide, &full}) {
        r->clicks(0);
        for (int s = 0; s < kSteps; ++s) r->hit(1, s, 0.6f);
    }
    wide.feel(1, 300, 999);
    wide.feel(8, 100, 100);
    wide.feel(-1, 100, 100);
    full.feel(1, 100, 100);
    wide.play();
    full.play();
    wide.render(2 * kBar);
    full.render(2 * kBar);
    CHECK(wide.out == full.out);
    Rig below, rest;
    for (Rig* r : {&below, &rest}) {
        r->clicks(0);
        for (int s = 0; s < kSteps; ++s) r->hit(1, s, 0.6f);
    }
    below.feel(1, -40, -1);
    below.play();
    rest.play();
    below.render(2 * kBar);
    rest.render(2 * kBar);
    CHECK(below.out == rest.out);
}

int main(int argc, char** argv) {
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testRule", testRule}, {"testAtRest", testAtRest}, {"testTime", testTime}, {"testLevel", testLevel}, {"testTopAndNudge", testTopAndNudge},
        {"testRatchet", testRatchet}, {"testBlocksAndHelpers", testBlocksAndHelpers}, {"testPlayTwice", testPlayTwice}, {"testOnce", testOnce},
        {"testTurnedWhilePlaying", testTurnedWhilePlaying}, {"testSlices", testSlices}, {"testDuckAndThrow", testDuckAndThrow}, {"testGoneAndLimits", testGoneAndLimits}, {"testAskedAgain", testAskedAgain}, {"testASceneMoved", testASceneMoved},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("feel_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
