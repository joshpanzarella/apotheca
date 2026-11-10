// Engine tests. No framework: each CHECK prints its line on failure and the
// program exits non-zero if any failed.

#include <cmath>
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

enum { KICK = 0, SNARE = 1, CL_HAT = 2, OP_HAT = 3, RIM = 6, CRASH = 7 };
static const int kStep = 6000; // frames in one step at 120 BPM and 48 kHz

static float peakOf(const std::vector<float>& buffer) {
    float peak = 0.f;
    for (float s : buffer) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    return peak;
}

// Index of the first frame that is not silent, or -1.
static int onsetOf(const std::vector<float>& buffer) {
    for (std::size_t i = 0; i < buffer.size(); i += 2) {
        if (buffer[i] != 0.f) return static_cast<int>(i / 2);
    }
    return -1;
}

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
static Command trigger(int lane, float velocity = 1.f, int track = 0) { return make(Command::Trigger, track, lane, 0, velocity); }
static Command step(int lane, int stepIndex, float velocity, int track = 0) { return make(Command::SetStep, track, lane, stepIndex, velocity); }
static Command effectParam(int track, int slot, int param, float value) { return make(Command::SetEffectParam, track, slot, param, value); }

static std::vector<float> renderFrames(Engine& engine, int frames) {
    std::vector<float> out(2 * frames, 99.f); // 99 so an unwritten frame shows
    engine.render(out.data(), frames);
    return out;
}

static void testQueue() {
    SpscQueue<int, 4> q;
    int v = 0;
    CHECK(!q.pop(v));
    CHECK(q.push(1) && q.push(2) && q.push(3));
    CHECK(!q.push(4)); // holds N-1
    CHECK(q.pop(v) && v == 1);
    CHECK(q.push(4));
    CHECK(q.pop(v) && v == 2);
    CHECK(q.pop(v) && v == 3);
    CHECK(q.pop(v) && v == 4);
    CHECK(!q.pop(v));
}

static void testBuiltInKit() {
    DrumKit a, b;
    a.buildBuiltIn(48000.f);
    b.buildBuiltIn(48000.f);
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const std::vector<float>& s = a.samples[lane];
        CHECK(s.size() > 1000);
        float peak = 0.f;
        bool finite = true;
        for (float v : s) {
            finite = finite && std::isfinite(v);
            peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        }
        CHECK(finite);
        CHECK(peak > 0.5f && peak <= 1.f);
        CHECK(s.back() == 0.f); // faded to exactly nothing, so no click
        CHECK(s == b.samples[lane]);         // the same on every start
        CHECK(DrumKit::laneName(lane)[0] != '\0');
        for (int other = 0; other < lane; ++other) CHECK(s != a.samples[other]);
    }
    // A closed hat is much shorter than an open one, and a crash is the longest.
    CHECK(a.samples[CL_HAT].size() * 4 < a.samples[OP_HAT].size());
    CHECK(a.samples[CRASH].size() > a.samples[OP_HAT].size());
    // Other sample rates keep the length in seconds.
    DrumKit slow;
    slow.buildBuiltIn(24000.f);
    CHECK(slow.samples[KICK].size() * 2 == a.samples[KICK].size());
    CHECK(DrumKit::laneName(-1)[0] == '\0' && DrumKit::laneName(kDrumLanes)[0] == '\0');
}

static void testSilentUntilPlayed() {
    Engine engine;
    CHECK(peakOf(renderFrames(engine, 500)) == 0.f);
    CHECK(engine.playStep() == -1);
}

static void testTriggerSoundsOnTheDrumTrack() {
    Engine engine;
    engine.send(trigger(KICK));
    const std::vector<float> out = renderFrames(engine, 2000);
    CHECK(peakOf(out) > 0.3f);
    CHECK(engine.trackPeak(0) > 0.3f);
    for (int t = 1; t < kNumTracks; ++t) CHECK(engine.trackPeak(t) == 0.f);
    CHECK(out[801] == out[800]); // both sides carry the same signal for now

    // Only track 0 has an instrument in a new project.
    Engine other;
    other.send(trigger(KICK, 1.f, 1));
    CHECK(peakOf(renderFrames(other, 2000)) == 0.f);
}

static void testVelocityIsLinearGain() {
    Engine loud, soft;
    loud.send(trigger(SNARE, 1.f));
    soft.send(trigger(SNARE, 0.5f));
    const std::vector<float> a = renderFrames(loud, 3000);
    const std::vector<float> b = renderFrames(soft, 3000);
    bool half = true;
    for (std::size_t i = 0; i < a.size(); ++i) half = half && std::fabs(b[i] - 0.5f * a[i]) < 1e-6f;
    CHECK(half);
    CHECK(peakOf(a) > 0.1f);
}

static void testLanesAreDifferentSounds() {
    Engine kick, snare;
    kick.send(trigger(KICK));
    snare.send(trigger(SNARE));
    CHECK(renderFrames(kick, 2000) != renderFrames(snare, 2000));
}

static void testRetriggerDoesNotClick() {
    // Retrigger the kick while it is loud. The old sound must fade, not stop
    // dead: no jump between neighbouring samples bigger than the sample's own
    // steepest slope allows.
    Engine engine;
    engine.send(trigger(KICK));
    const std::vector<float> first = renderFrames(engine, 2400);
    float steepest = 0.f;
    for (std::size_t i = 2; i < first.size(); i += 2) steepest = std::fmax(steepest, std::fabs(first[i] - first[i - 2]));
    engine.send(trigger(KICK));
    const std::vector<float> second = renderFrames(engine, 2400);
    CHECK(std::fabs(second[0] - first[first.size() - 2]) < 2.f * steepest);
    float jump = 0.f;
    for (std::size_t i = 2; i < second.size(); i += 2) jump = std::fmax(jump, std::fabs(second[i] - second[i - 2]));
    CHECK(jump < 2.5f * steepest);
    // And the old sound is really gone a few milliseconds later, not doubled.
    Engine once;
    once.send(trigger(KICK));
    const std::vector<float> clean = renderFrames(once, 2400);
    bool same = true;
    for (std::size_t i = 2 * 400; i < clean.size(); ++i) same = same && std::fabs(clean[i] - second[i]) < 1e-6f;
    CHECK(same);
}

static void testPatternPlaysOnTheExactSample() {
    Engine engine;
    engine.send(step(RIM, 4, 1.f));
    engine.send(make(Command::Play));
    const std::vector<float> out = renderFrames(engine, 6 * kStep);
    const int onset = onsetOf(out);
    CHECK(onset >= 4 * kStep && onset <= 4 * kStep + 2);
    CHECK(engine.playStep() == 5);
}

static void testPatternLoopsAfterSixteenSteps() {
    Engine engine;
    engine.send(step(RIM, 0, 1.f));
    engine.send(make(Command::Play));
    const std::vector<float> first = renderFrames(engine, 16 * kStep);
    CHECK(onsetOf(first) <= 2);
    // One hit in the bar, not two: silent from step 2 to the end.
    CHECK(peakOf(std::vector<float>(first.begin() + 2 * kStep, first.end())) == 0.f);
    const std::vector<float> second = renderFrames(engine, 16 * kStep);
    CHECK(first == second); // the second bar is the first bar again
    CHECK(engine.playStep() == 15);
}

static void testStepVelocityAndClearing() {
    Engine loud, soft, cleared;
    loud.send(step(SNARE, 0, 1.f));
    soft.send(step(SNARE, 0, 0.25f));
    cleared.send(step(SNARE, 0, 1.f));
    cleared.send(step(SNARE, 0, 0.f));
    for (Engine* e : {&loud, &soft, &cleared}) e->send(make(Command::Play));
    const float a = peakOf(renderFrames(loud, kStep));
    const float b = peakOf(renderFrames(soft, kStep));
    CHECK(a > 0.1f);
    CHECK(std::fabs(b - 0.25f * a) < 1e-5f);
    CHECK(peakOf(renderFrames(cleared, kStep)) == 0.f);
}

static void testStopAndRestart() {
    Engine engine;
    engine.send(step(RIM, 0, 1.f));
    engine.send(step(RIM, 2, 1.f));
    engine.send(make(Command::Play));
    renderFrames(engine, kStep);
    engine.send(make(Command::Stop));
    renderFrames(engine, 4 * kStep); // long enough for the rim to ring out
    CHECK(engine.playStep() == -1);
    CHECK(peakOf(renderFrames(engine, 4 * kStep)) == 0.f); // step 2 never fired
    // Play starts from step 1 again, at once.
    engine.send(make(Command::Play));
    CHECK(onsetOf(renderFrames(engine, 100)) <= 2);
    CHECK(engine.playStep() == 0);
}

static void testTempo() {
    Engine engine;
    engine.send(make(Command::SetTempo, 0, 0, 0, 60.f)); // half speed: 12000 frames a step
    engine.send(step(RIM, 1, 1.f));
    engine.send(make(Command::Play));
    const int onset = onsetOf(renderFrames(engine, 3 * 12000));
    CHECK(onset >= 12000 && onset <= 12002);

    // Out-of-range tempos are held to the limits; nonsense is ignored.
    Engine fast;
    fast.send(make(Command::SetTempo, 0, 0, 0, 100000.f));
    fast.send(make(Command::SetTempo, 0, 0, 0, std::nanf("")));
    fast.send(step(RIM, 1, 1.f));
    fast.send(make(Command::Play));
    const int limit = static_cast<int>(48000.f * 60.f / kMaxTempo / 4.f);
    const int fastOnset = onsetOf(renderFrames(fast, 3 * limit));
    CHECK(fastOnset >= limit && fastOnset <= limit + 2);
}

static void testTrackLevel() {
    Engine engine;
    engine.send(make(Command::SetTrackLevel, 0, 0, 0, 0.f));
    engine.send(trigger(KICK));
    CHECK(peakOf(renderFrames(engine, 1000)) == 0.f);
}

static void testTrackEffectSlotsRunInSeries() {
    Engine plain, shaped;
    shaped.setTrackEffect(0, 0, std::unique_ptr<Effect>(new GainEffect));
    shaped.setTrackEffect(0, 1, std::unique_ptr<Effect>(new GainEffect));
    shaped.send(effectParam(0, 0, 0, 0.5f));
    shaped.send(effectParam(0, 1, 0, 0.5f));
    plain.send(trigger(KICK));
    shaped.send(trigger(KICK));
    const float a = peakOf(renderFrames(plain, 2000));
    const float b = peakOf(renderFrames(shaped, 2000));
    CHECK(a > 0.1f);
    CHECK(std::fabs(b - 0.25f * a) < 1e-4f);

    // An effect on another track leaves the drums alone.
    Engine other;
    other.setTrackEffect(1, 0, std::unique_ptr<Effect>(new GainEffect));
    other.send(effectParam(1, 0, 0, 0.f));
    other.send(trigger(KICK));
    CHECK(std::fabs(peakOf(renderFrames(other, 2000)) - a) < 1e-6f);
}

static void testMasterEffectSlots() {
    Engine plain, shaped;
    shaped.setMasterEffect(0, std::unique_ptr<Effect>(new GainEffect));
    shaped.setMasterEffect(1, std::unique_ptr<Effect>(new GainEffect));
    shaped.send(effectParam(kMasterTrack, 0, 0, 0.5f));
    shaped.send(effectParam(kMasterTrack, 1, 0, 0.5f));
    for (Engine* e : {&plain, &shaped}) {
        e->send(trigger(KICK));
        e->send(trigger(CL_HAT));
    }
    const std::vector<float> a = renderFrames(plain, 2000);
    const std::vector<float> b = renderFrames(shaped, 2000);
    bool scaled = true;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(b[i] - 0.25f * a[i]) > 1e-5f) scaled = false;
    }
    CHECK(scaled);
    CHECK(peakOf(a) > 0.1f);
    // The master effect is after the track meters.
    CHECK(std::fabs(shaped.trackPeak(0) - plain.trackPeak(0)) < 1e-6f);
}

static void testLowpassCutsHighs() {
    Engine open, dark;
    dark.setTrackEffect(0, 0, std::unique_ptr<Effect>(new LowpassEffect));
    dark.send(effectParam(0, 0, 0, 200.f));
    open.send(trigger(CL_HAT)); // nearly all above 7 kHz
    dark.send(trigger(CL_HAT));
    const float a = peakOf(renderFrames(open, 2000));
    const float b = peakOf(renderFrames(dark, 2000));
    CHECK(b < 0.15f * a);
    CHECK(b > 0.f);
}

static void testBlockSizeDoesNotChangeTheSound() {
    Engine whole, pieces;
    for (Engine* e : {&whole, &pieces}) {
        e->setTrackEffect(0, 0, std::unique_ptr<Effect>(new LowpassEffect));
        e->send(make(Command::SetTempo, 0, 0, 0, 133.f)); // a step is not a whole number of frames
        e->send(step(KICK, 0, 1.f));
        e->send(step(CL_HAT, 1, 0.7f));
        e->send(step(SNARE, 2, 0.9f));
        e->send(make(Command::Play));
    }
    const int total = 20000;
    const std::vector<float> a = renderFrames(whole, total);
    std::vector<float> b;
    const int sizes[] = {1, 63, 64, 65, 7, 300, 5413, 4087, 10000};
    for (int n : sizes) {
        const std::vector<float> part = renderFrames(pieces, n);
        b.insert(b.end(), part.begin(), part.end());
    }
    CHECK(b.size() == a.size());
    bool same = b.size() == a.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
    CHECK(same);
    CHECK(peakOf(a) > 0.1f);
}

static void testBadCommandsAreIgnored() {
    Engine engine;
    engine.send(trigger(KICK, 1.f, -5));
    engine.send(trigger(KICK, 1.f, kNumTracks));
    engine.send(trigger(-1));
    engine.send(trigger(kDrumLanes));
    engine.send(trigger(KICK, 0.f));
    engine.send(trigger(KICK, std::nanf("")));
    engine.send(effectParam(0, 7, 0, 1.f));
    engine.send(effectParam(0, 0, 0, 1.f)); // empty slot
    engine.send(step(-1, 0, 1.f));
    engine.send(step(kDrumLanes, 0, 1.f));
    engine.send(step(KICK, -1, 1.f));
    engine.send(step(KICK, kSteps, 1.f));
    engine.send(step(KICK, 0, std::nanf("")));
    engine.send(step(KICK, 0, 1.f, kNumTracks));
    engine.send(make(Command::Play));
    CHECK(peakOf(renderFrames(engine, 17 * kStep)) == 0.f);
}

int main() {
    testQueue();
    testBuiltInKit();
    testSilentUntilPlayed();
    testTriggerSoundsOnTheDrumTrack();
    testVelocityIsLinearGain();
    testLanesAreDifferentSounds();
    testRetriggerDoesNotClick();
    testPatternPlaysOnTheExactSample();
    testPatternLoopsAfterSixteenSteps();
    testStepVelocityAndClearing();
    testStopAndRestart();
    testTempo();
    testTrackLevel();
    testTrackEffectSlotsRunInSeries();
    testMasterEffectSlots();
    testLowpassCutsHighs();
    testBlockSizeDoesNotChangeTheSound();
    testBadCommandsAreIgnored();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("engine tests passed\n");
    return 0;
}
