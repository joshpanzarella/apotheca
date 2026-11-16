// CHOKE: two choke groups on a drum track (drums.hpp, DrumMachine::setLaneChoke). A hit on a lane
// in a group cuts every other lane of that group still sounding, with the 3 ms fade a lane already
// cuts its own last hit with. Measured, not listened to.
//
// The lanes are given recordings that hold one value from end to end, so the output says how far
// each has faded: the "open hat" (lane 3) holds 0.5 for a second, and the "closed hat" (lane 2) is
// silence (it cuts, and adds nothing of its own), unless a test says otherwise.
//
//   1. a hit cuts the other lanes of its group within the fade; another group, or none, is not cut
//   2. hits on the same sample do not cut each other: both were written
//   3. a lane cuts itself exactly as it did; a group of one is no group; nothing grouped is the
//      sound as it was, to the bit
//   4. a ratchet: each piece is a hit (it cuts, and a later piece of the cut lane sounds again)
//   5. slices carried on: a hit that carries its lane's sound on still cuts; the run is unbroken
//   6. a thrown copy on a bus is cut with its voice; a hit heard by hand (X) cuts too
//   7. the same to the bit at any block length and on any number of helpers
//   8. a group changed while a lane sounds cuts nothing by itself; a track taken away leaves no
//      group behind; numbers out of range are NONE

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "apotheca/effects.hpp"
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

static const int kStep = 6000;  // a step at 120 BPM, 48 kHz
static const int kBar = 16 * kStep;
static const int kFade = 144;   // the choke's 3 ms at 48 kHz
static const int kClosed = 2, kOpen = 3;
static const float kHeld = 0.5f;

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}

struct Probe : Effect { // on a bus: writes down what it is given, gives nothing back
    std::vector<float> fed;
    Probe() { fed.reserve(48000 * 10); }
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
    void sound(int lane, float value, int length, int track = 0) {
        std::unique_ptr<SampleData> s(new SampleData);
        s->data.assign(static_cast<std::size_t>(length), value);
        CHECK(engine->setLaneSample(track, lane, std::move(s)));
    }
    // The open hat holds 0.5 for a second; the closed hat is a second of silence.
    void hats(int track = 0) {
        sound(kOpen, kHeld, 48000, track);
        sound(kClosed, 0.f, 48000, track);
    }
    void hit(int lane, int step, float level = 1.f, float nudge = 0.f, int ratchet = 0, int throwTo = 0, int track = 0) {
        send(make(Command::SetStep, track, lane, step, level));
        if (nudge != 0.f) send(make(Command::SetStepNudge, track, lane, step, nudge));
        if (ratchet || throwTo) {
            StepFx fx;
            fx.ratchet = static_cast<std::uint8_t>(ratchet);
            fx.throwTo = static_cast<std::uint8_t>(throwTo);
            Command c = make(Command::SetStepFx, track, lane, step, 0.f);
            c.d = fx.ratchet;
            c.extra = fx.cutAndThrow();
            send(c);
        }
    }
    void choke(int lane, int group, int track = 0) { send(make(Command::SetLaneChoke, track, lane, group)); }
    void play() { send(make(Command::Play, 0, 0, 1)); }
    float at(long i) const { return out[static_cast<std::size_t>(i)]; }
};

// From `from` on, the output falls to nothing within the fade and stays there until `to`. (What a
// held 0.5 comes out as is the track's usual level times it: read off the sample before.)
static bool cutAt(const Rig& rig, long from, long to) {
    const float was = rig.at(from - 1);
    if (!(was > 0.f)) return false;
    if (!(rig.at(from + kFade / 2) > 0.f && rig.at(from + kFade / 2) < was)) return false; // a fade, not a step
    for (long i = from + 1; i < from + kFade + 2; ++i)
        if (!(rig.at(i) <= rig.at(i - 1))) return false;
    for (long i = from + kFade + 2; i < to; ++i)
        if (rig.at(i) != 0.f) return false;
    return true;
}
// Held at the one value the whole way, not silence.
static bool held(const Rig& rig, long from, long to) {
    const float was = rig.at(from);
    if (!(was > 0.f)) return false;
    for (long i = from; i < to; ++i)
        if (rig.at(i) != was) return false;
    return true;
}

// ---- 1 -------------------------------------------------------------------------------------------

static void testCutsTheGroup() {
    // The open hat on step 1, the closed hat on step 3: in one group, the open hat is cut there.
    const auto run = [](int closedGroup, int openGroup) {
        std::unique_ptr<Rig> rig(new Rig);
        rig->hats();
        rig->hit(kOpen, 0);
        rig->hit(kClosed, 2);
        rig->choke(kClosed, closedGroup);
        rig->choke(kOpen, openGroup);
        rig->play();
        rig->render(kBar / 2);
        return rig;
    };
    for (int group : {1, 2}) {
        std::unique_ptr<Rig> rig = run(group, group);
        CHECK(held(*rig, 0, 2 * kStep));
        CHECK(cutAt(*rig, 2 * kStep, 7 * kStep));
    }
    // Another group, or none on either side: it rings on.
    for (const auto& pair : {std::make_pair(1, 2), std::make_pair(2, 1), std::make_pair(0, 1), std::make_pair(1, 0), std::make_pair(0, 0)}) {
        std::unique_ptr<Rig> rig = run(pair.first, pair.second);
        CHECK(held(*rig, 0, 7 * kStep));
    }
    // Every lane of the group is cut, and only those: a third lane in the group, a fourth in the other.
    Rig rig;
    rig.hats();
    rig.sound(5, 0.25f, 48000);
    rig.sound(6, 0.125f, 48000);
    rig.choke(kOpen, 1);
    rig.choke(5, 1);
    rig.choke(6, 2);
    rig.choke(kClosed, 1);
    rig.hit(kOpen, 0);
    rig.hit(5, 0);
    rig.hit(6, 0);
    rig.hit(kClosed, 4);
    rig.play();
    rig.render(kBar / 2);
    const float all = rig.at(4 * kStep - 1), seventh = rig.at(4 * kStep + kFade + 2);
    CHECK(std::fabs(seventh / all - 0.125f / 0.875f) < 1e-5f); // lane 7 rings on alone...
    CHECK(held(rig, 4 * kStep + kFade + 2, 7 * kStep));       // ...untouched
}

// A voice already fading (here at the end of its part, the 1 ms fade) keeps its own fade: a hit of its
// group does not slow it to 3 ms.
static void testAFadeKept() {
    const auto run = [](int closedGroup) {
        std::unique_ptr<Rig> rig(new Rig);
        rig->hats();
        rig->send(make(Command::SetLanePart, 0, kOpen, 0, 0.25f)); // its part ends at 12000, step 3's first sample
        rig->choke(kOpen, 1);
        rig->choke(kClosed, closedGroup);
        rig->hit(kOpen, 0);
        rig->hit(kClosed, 2, 1.f, 0.00390625f); // a 256th of a step late: 24 samples into the open hat's 48-sample fade
        rig->play();
        rig->render(kBar / 2);
        return rig;
    };
    std::unique_ptr<Rig> grouped = run(1), apart = run(0);
    for (long i = 2 * kStep; i < 3 * kStep; ++i) CHECK(grouped->at(i) == apart->at(i));
    CHECK(apart->at(2 * kStep + 10) > 0.f && apart->at(2 * kStep + 60) == 0.f); // (it was fading there, and is gone in 1 ms)
}

// ---- 2 -------------------------------------------------------------------------------------------

static void testSameSample() {
    // Written on one step, the two both play, in whichever order the engine starts them.
    for (const auto& lanes : {std::make_pair(kClosed, kOpen), std::make_pair(kOpen, kClosed)}) {
        Rig rig;
        rig.sound(lanes.first, 0.f, 48000);
        rig.sound(lanes.second, kHeld, 48000);
        rig.choke(lanes.first, 1);
        rig.choke(lanes.second, 1);
        rig.hit(lanes.first, 0);
        rig.hit(lanes.second, 0);
        rig.hit(lanes.first, 8); // (and again after many blocks)
        rig.hit(lanes.second, 8);
        rig.play();
        rig.render(kBar);
        CHECK(held(rig, 0, 15 * kStep));
    }
    // Hit by hand at once, the same: no block between them.
    Rig rig;
    rig.hats();
    rig.choke(kClosed, 2);
    rig.choke(kOpen, 2);
    rig.render(1000); // (not the first block there is)
    rig.out.clear();
    rig.send(make(Command::Trigger, 0, kOpen, 0, 1.f));
    rig.send(make(Command::Trigger, 0, kClosed, 0, 1.f));
    rig.render(kStep);
    CHECK(held(rig, 0, kStep));
    // ...and the next block's hit does cut.
    rig.send(make(Command::Trigger, 0, kClosed, 0, 1.f));
    rig.render(kStep);
    CHECK(cutAt(rig, kStep, 2 * kStep));
}

// ---- 3 -------------------------------------------------------------------------------------------

static void testAsItWas() {
    // A lane alone in a group, hit again and again, ratcheted, cut, sliced and played backwards:
    // what it does to itself is what it did with no group, to the bit.
    const auto build = [](Rig& rig, int group) {
        std::unique_ptr<SampleData> s(new SampleData);
        s->data.resize(static_cast<std::size_t>(kBar));
        for (int i = 0; i < kBar; ++i) s->data[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::sin(i * 0.031 + 1e-7 * i * i));
        CHECK(rig.engine->setLaneSample(0, kOpen, std::move(s)));
        rig.choke(kOpen, group);
        for (int step = 0; step < kSteps; step += 3) rig.hit(kOpen, step, 0.7f, step == 6 ? 0.2f : 0.f, step == 9 ? 3 : 0, step == 12 ? kThrowEcho : 0);
        for (int step = 0; step < kSteps; step += 3) rig.send(make(Command::SetStepStart, 0, kOpen, step, step / 32.f));
        rig.hit(1, 4); // the kit's snare beside it, in no group
        rig.play();
        rig.render(2 * kBar, 200);
    };
    Rig plain, alone;
    build(plain, 0);
    build(alone, 1);
    CHECK(plain.out == alone.out);
    float loudest = 0.f;
    for (float v : plain.out) loudest = std::max(loudest, std::fabs(v));
    CHECK(loudest > 0.3f);
    // A kit with no group: the commands sent at NONE change nothing.
    Rig told, untold;
    for (Rig* r : {&told, &untold}) {
        for (int lane = 0; lane < kDrumLanes; ++lane) r->hit(lane, lane * 2);
        r->hit(kOpen, 3);
    }
    for (int lane = 0; lane < kDrumLanes; ++lane) told.choke(lane, 0);
    told.play();
    untold.play();
    told.render(kBar);
    untold.render(kBar);
    CHECK(told.out == untold.out);
}

// ---- 4 -------------------------------------------------------------------------------------------

static void testRatchet() {
    // The open hat ratcheted in two (pieces at 0 and half a step), the closed hat a quarter of a step
    // in: it cuts the first piece, and the second sounds again from its start.
    {
        Rig rig;
        rig.hats();
        rig.choke(kOpen, 1);
        rig.choke(kClosed, 1);
        rig.hit(kOpen, 0, 1.f, 0.f, 2);
        rig.hit(kClosed, 0, 1.f, 0.25f);
        rig.play();
        rig.render(kBar / 2);
        CHECK(held(rig, 0, kStep / 4));
        CHECK(cutAt(rig, kStep / 4, kStep / 2));
        CHECK(held(rig, kStep / 2, 7 * kStep));
    }
    // The closed hat ratcheted in four, a step after the open: its first piece cuts. The open hat
    // hit again between two pieces is cut by the next piece.
    {
        Rig rig;
        rig.hats();
        rig.choke(kOpen, 2);
        rig.choke(kClosed, 2);
        rig.hit(kOpen, 0);
        rig.hit(kClosed, 1, 1.f, 0.f, 4);
        rig.hit(kOpen, 1, 1.f, 0.125f); // 750 samples into the step: between the first piece (0) and the second (1500)
        rig.play();
        rig.render(kBar / 2);
        CHECK(cutAt(rig, kStep, kStep + 750));
        CHECK(held(rig, kStep + 750, kStep + 1500));
        CHECK(cutAt(rig, kStep + 1500, 7 * kStep));
    }
}

// ---- 5 -------------------------------------------------------------------------------------------

static void testSlices() {
    // Lane 1 holds a bar of a sine cut into sixteen and played in order on the first eight steps: the
    // hits after the first carry its sound on. Lane 2 holds 0.5, hit on step 1 with it, in its group.
    const auto build = [](Rig& rig, bool second, int group) {
        std::unique_ptr<SampleData> s(new SampleData);
        s->data.resize(static_cast<std::size_t>(kBar));
        for (int i = 0; i < kBar; ++i) s->data[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::sin(i * 0.031 + 1e-7 * i * i));
        CHECK(rig.engine->setLaneSample(0, 0, std::move(s)));
        rig.sound(1, kHeld, 48000);
        rig.send(make(Command::SetLanePart, 0, 0, 0, 1.f / 16.f));
        for (int k = 0; k < 8; ++k) {
            rig.hit(0, k, 0.8f);
            rig.send(make(Command::SetStepStart, 0, 0, k, k / 16.f));
        }
        if (second) rig.hit(1, 0);
        rig.choke(0, group);
        rig.choke(1, group);
        rig.play();
        rig.render(kBar);
    };
    Rig alone, both, grouped;
    build(alone, false, 0); // (lane 1 in no group: its run as it always was)
    build(both, true, 0);
    build(grouped, true, 1);
    // Not grouped: the two add up. Grouped: the slice that carries the sound on, on step 2, still
    // cuts lane 2 (it is a hit of its lane), and from there it is lane 1 alone, unbroken.
    for (long i = 0; i < kStep; ++i) CHECK(grouped.at(i) == both.at(i));
    CHECK(both.at(kStep + kFade + 10) - alone.at(kStep + kFade + 10) > 0.1f);
    for (long i = kStep + kFade + 2; i < 8 * kStep; ++i) CHECK(grouped.at(i) == alone.at(i));
    float jump = 0.f;
    for (long i = kStep + kFade + 2; i < 8 * kStep; ++i) jump = std::max(jump, std::fabs(grouped.at(i) - grouped.at(i - 1)));
    CHECK(jump < 0.02f); // no seam: a sine of 0.4 moves 0.0124 a sample at most
}

// ---- 6 -------------------------------------------------------------------------------------------

static void testThrownAndByHand() {
    // The open hat thrown to ECHO: what the bus is given falls with it.
    Rig rig;
    Probe* echo = new Probe;
    rig.engine->setBusEffect(0, std::unique_ptr<Effect>(echo));
    rig.hats();
    rig.choke(kOpen, 1);
    rig.choke(kClosed, 1);
    rig.hit(kOpen, 0, 1.f, 0.f, 0, kThrowEcho);
    rig.hit(kClosed, 2);
    rig.play();
    rig.render(kBar / 2);
    CHECK(cutAt(rig, 2 * kStep, 7 * kStep));
    const long woke = static_cast<long>(rig.out.size()) - static_cast<long>(echo->fed.size()); // (the bus wakes with the first hit)
    CHECK(woke == 0);
    long lastSound = -1;
    for (std::size_t i = 0; i < echo->fed.size(); ++i)
        if (echo->fed[i] != 0.f) lastSound = static_cast<long>(i);
    CHECK(echo->fed[static_cast<std::size_t>(2 * kStep - 1)] > 0.1f);
    CHECK(lastSound >= 2 * kStep && lastSound <= 2 * kStep + kFade + 1);
    // A hit by hand (X held on the closed hat) cuts the open hat the song is playing.
    Rig hand;
    hand.hats();
    hand.choke(kOpen, 2);
    hand.choke(kClosed, 2);
    hand.hit(kOpen, 0);
    hand.play();
    hand.render(kStep);
    Command x = make(Command::Trigger, 0, kClosed, 0, 1.f);
    x.e = 1; // a held audition
    hand.send(x);
    hand.render(kStep);
    CHECK(cutAt(hand, kStep, 2 * kStep));
}

// ---- 7 -------------------------------------------------------------------------------------------

static std::vector<float> busy(int block, int helpers, bool grouped = true) {
    Rig rig(helpers);
    CHECK(rig.engine->addDrumTrack(4));
    for (int t : {0, 4}) {
        rig.sound(kOpen, kHeld, 30000, t);
        if (grouped) {
            rig.choke(kOpen, 1, t);
            rig.choke(kClosed, 1, t);
            rig.choke(5, 2, t);
            rig.choke(7, 2, t);
            rig.choke(6, 1, t);
        }
        for (int s = 0; s < kSteps; ++s) {
            if (s % 4 == 2) rig.hit(kOpen, s, 0.9f, 0.f, s == 10 ? 3 : 0, s == 6 ? kThrowSpace : 0, t);
            if (s % 2 == 0) rig.hit(kClosed, s, 0.6f, t == 4 ? 0.125f : 0.f, 0, 0, t);
            if (s % 5 == 1) rig.hit(5, s, 0.8f, 0.f, 0, 0, t);
            if (s % 7 == 3) rig.hit(7, s, 0.7f, -0.25f, 2, 0, t);
            if (s == 9) rig.hit(6, s, 1.f, 0.375f, 0, 0, t);
        }
    }
    // (At 120 BPM: a step of a whole number of samples, the only tempo a song with nudges and
    // ratchets is the same to the bit at in any blocks, as on main.)
    rig.play();
    rig.render(3 * kBar, block);
    return rig.out;
}

static void testBlocksAndHelpers() {
    const std::vector<float> plain = busy(256, 0);
    float loudest = 0.f;
    for (float v : plain) loudest = std::max(loudest, std::fabs(v));
    CHECK(loudest > 0.3f);
    CHECK(busy(256, 0, false) != plain); // (the groups cut something in it)
    for (int block : {1, 7, 64, 333}) CHECK(busy(block, 0) == plain);
    CHECK(busy(256, 3) == plain);
    CHECK(busy(48, 2) == plain);
}

// ---- 8 -------------------------------------------------------------------------------------------

static void testChangedAndGone() {
    // Grouped while the open hat sounds: it rings on until a hit of its group.
    Rig rig;
    rig.hats();
    rig.hit(kOpen, 0);
    rig.hit(kClosed, 3);
    rig.play();
    rig.render(kStep);
    rig.choke(kOpen, 1);
    rig.choke(kClosed, 1);
    rig.render(kStep);
    CHECK(held(rig, 0, 2 * kStep));
    rig.render(kBar / 2);
    CHECK(cutAt(rig, 3 * kStep, 7 * kStep));
    // A track taken away and made again: no group.
    Rig gone;
    CHECK(gone.engine->addDrumTrack(2));
    gone.choke(kOpen, 1, 2);
    gone.choke(kClosed, 1, 2);
    gone.render(256);
    CHECK(gone.engine->removeTrack(2));
    gone.render(2048);
    gone.engine->collectGarbage();
    CHECK(gone.engine->addDrumTrack(2));
    gone.hats(2);
    gone.hit(kOpen, 0, 1.f, 0.f, 0, 0, 2);
    gone.hit(kClosed, 2, 1.f, 0.f, 0, 0, 2);
    gone.out.clear();
    gone.play();
    gone.render(kBar / 2);
    CHECK(held(gone, 0, 7 * kStep));
    // Out of range: a group that is not 1 or 2 is NONE; a lane or a track that is not there changes nothing.
    for (int group : {3, -1, 255}) {
        Rig odd;
        odd.hats();
        odd.choke(kOpen, 1);
        odd.choke(kClosed, 1);
        odd.choke(kOpen, group); // (both in the one group nobody knows: in none)
        odd.choke(kClosed, group);
        odd.choke(8, 1);
        odd.choke(-1, 1);
        odd.send(make(Command::SetLaneChoke, 11, kClosed, 1));
        odd.hit(kOpen, 0);
        odd.hit(kClosed, 2);
        odd.play();
        odd.render(kBar / 2);
        CHECK(held(odd, 0, 7 * kStep));
    }
}

int main(int argc, char** argv) {
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testCutsTheGroup", testCutsTheGroup}, {"testSameSample", testSameSample}, {"testAFadeKept", testAFadeKept}, {"testAsItWas", testAsItWas}, {"testRatchet", testRatchet},
        {"testSlices", testSlices}, {"testThrownAndByHand", testThrownAndByHand}, {"testBlocksAndHelpers", testBlocksAndHelpers}, {"testChangedAndGone", testChangedAndGone},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("choke_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
