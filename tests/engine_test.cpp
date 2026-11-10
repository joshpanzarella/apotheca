// Engine tests. No framework: each CHECK prints its line on failure and the
// program exits non-zero if any failed.

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/rate_converter.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

enum { KICK = 0, SNARE = 1, CL_HAT = 2, OP_HAT = 3, TOM = 5, RIM = 6, CRASH = 7 };
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

static void testKitHitsHard() {
    // Loudness of the first tenth of a second, at nearly the same peak. The
    // first kit measured 0.36 for the kick and 0.14 for the snare; this one
    // measures 0.81 and 0.47. Fail if a change gives much of that back.
    DrumKit kit;
    kit.buildBuiltIn(48000.f);
    const auto loudness = [&](int lane) {
        double sum = 0.0;
        for (int i = 0; i < 4800; ++i) sum += double(kit.samples[lane][i]) * kit.samples[lane][i];
        return std::sqrt(sum / 4800.0);
    };
    CHECK(loudness(KICK) > 0.7);
    CHECK(loudness(SNARE) > 0.4);
    // A kick must start at once: half its peak inside the first 3 ms.
    float early = 0.f;
    for (int i = 0; i < 144; ++i) early = std::fmax(early, std::fabs(kit.samples[KICK][i]));
    CHECK(early > 0.5f);
    // The snare starts at once too: near full level inside the first millisecond.
    float crack = 0.f;
    for (int i = 0; i < 48; ++i) crack = std::fmax(crack, std::fabs(kit.samples[SNARE][i]));
    CHECK(crack > 0.8f);
}

static void testCeiling() {
    // One full hit passes untouched.
    Engine one;
    DrumKit kit;
    kit.buildBuiltIn(48000.f);
    one.send(trigger(KICK));
    const std::vector<float> clean = renderFrames(one, 2000);
    bool untouched = true;
    for (int i = 0; i < 2000; ++i) untouched = untouched && std::fabs(clean[2 * i] - 0.65f * kit.samples[KICK][i]) < 1e-6f;
    CHECK(untouched);

    // Everything at once would be far over full scale. It comes out under
    // it, and still loud.
    Engine all;
    for (int lane = 0; lane < kDrumLanes; ++lane) all.send(trigger(lane));
    const std::vector<float> out = renderFrames(all, 4000);
    CHECK(peakOf(out) <= 1.f);
    CHECK(peakOf(out) > 0.9f);
    // Rounded off, not folded: louder in never means quieter out.
    Engine two;
    two.send(trigger(KICK));
    two.send(trigger(TOM));
    const std::vector<float> pair = renderFrames(two, 2000);
    bool keepsOrder = true;
    for (int i = 0; i < 2000; ++i) {
        const float in = 0.65f * (kit.samples[KICK][i] + kit.samples[TOM][i]);
        if (in * pair[2 * i] < 0.f || std::fabs(pair[2 * i]) > std::fabs(in) + 1e-6f) keepsOrder = false;
    }
    CHECK(keepsOrder);
}

static void testLaneLevel() {
    Engine full, half, muted;
    half.send(make(Command::SetLaneLevel, 0, SNARE, 0, 0.5f));
    muted.send(make(Command::SetLaneLevel, 0, SNARE, 0, 0.f));
    for (Engine* e : {&full, &half, &muted}) e->send(trigger(SNARE));
    const std::vector<float> a = renderFrames(full, 3000);
    const std::vector<float> b = renderFrames(half, 3000);
    bool halved = true;
    for (std::size_t i = 0; i < a.size(); ++i) halved = halved && std::fabs(b[i] - 0.5f * a[i]) < 1e-6f;
    CHECK(halved);
    CHECK(peakOf(a) > 0.1f);
    CHECK(peakOf(renderFrames(muted, 3000)) == 0.f);

    // Only that lane.
    muted.send(trigger(KICK));
    CHECK(peakOf(renderFrames(muted, 3000)) > 0.1f);

    // It acts on a sound that is already ringing.
    Engine live;
    live.send(trigger(CRASH));
    renderFrames(live, 1000);
    live.send(make(Command::SetLaneLevel, 0, CRASH, 0, 0.f));
    CHECK(peakOf(renderFrames(live, 1000)) == 0.f);

    // Nonsense is ignored, and too much is held to full.
    Engine odd;
    odd.send(make(Command::SetLaneLevel, 0, -1, 0, 0.f));
    odd.send(make(Command::SetLaneLevel, 0, kDrumLanes, 0, 0.f));
    odd.send(make(Command::SetLaneLevel, 0, SNARE, 0, std::nanf("")));
    odd.send(make(Command::SetLaneLevel, 0, SNARE, 0, -1.f));
    odd.send(make(Command::SetLaneLevel, 0, SNARE, 0, 7.f));
    odd.send(trigger(SNARE));
    CHECK(renderFrames(odd, 3000) == a);
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

static void testNudgeMovesAHitOffTheGrid() {
    const int quarter = kStep / 4;
    Engine late, early, wrapped;
    late.send(step(RIM, 4, 1.f));
    late.send(make(Command::SetStepNudge, 0, RIM, 4, 0.25f));
    early.send(step(RIM, 4, 1.f));
    early.send(make(Command::SetStepNudge, 0, RIM, 4, -0.25f));
    // An early hit on step 1 belongs to the end of the bar before.
    wrapped.send(step(RIM, 0, 1.f));
    wrapped.send(make(Command::SetStepNudge, 0, RIM, 0, -0.25f));
    for (Engine* e : {&late, &early, &wrapped}) e->send(make(Command::Play));

    const int a = onsetOf(renderFrames(late, 16 * kStep));
    CHECK(a >= 4 * kStep + quarter && a <= 4 * kStep + quarter + 2);
    const int b = onsetOf(renderFrames(early, 16 * kStep));
    CHECK(b >= 4 * kStep - quarter && b <= 4 * kStep - quarter + 2);
    const int c = onsetOf(renderFrames(wrapped, 16 * kStep));
    CHECK(c >= 16 * kStep - quarter && c <= 16 * kStep - quarter + 2);

    // The limits are half a step either way, and nonsense is ignored.
    Engine far;
    far.send(step(RIM, 4, 1.f));
    far.send(make(Command::SetStepNudge, 0, RIM, 4, 9.f));
    far.send(make(Command::SetStepNudge, 0, RIM, 4, std::nanf("")));
    far.send(make(Command::SetStepNudge, 0, RIM, kSteps, 0.f));
    far.send(make(Command::SetStepNudge, 0, kDrumLanes, 4, 0.f));
    far.send(make(Command::Play));
    const int d = onsetOf(renderFrames(far, 16 * kStep));
    CHECK(d >= 4 * kStep + kStep / 2 && d <= 4 * kStep + kStep / 2 + 2);

    // Stop forgets a hit that was waiting for its moment.
    Engine stopped;
    stopped.send(step(RIM, 0, 1.f));
    stopped.send(make(Command::SetStepNudge, 0, RIM, 0, 0.4f));
    stopped.send(make(Command::Play));
    renderFrames(stopped, 100);
    stopped.send(make(Command::Stop));
    CHECK(peakOf(renderFrames(stopped, 4 * kStep)) == 0.f);
}

static void testNudgedHitsKeepTheirOwnLevels() {
    // A late hit of one step and an early hit of the next can both be
    // waiting inside the same step. Each must play, at its own level.
    Engine engine;
    engine.send(step(RIM, 2, 1.f));
    engine.send(make(Command::SetStepNudge, 0, RIM, 2, 0.2f));
    engine.send(step(RIM, 3, 0.5f));
    engine.send(make(Command::SetStepNudge, 0, RIM, 3, -0.2f));
    engine.send(make(Command::Play));
    const std::vector<float> out = renderFrames(engine, 6 * kStep);
    const int first = 2 * kStep + kStep / 5, second = 3 * kStep - kStep / 5;
    const std::vector<float> one(out.begin() + 2 * first, out.begin() + 2 * (first + 2000));
    const std::vector<float> two(out.begin() + 2 * second, out.begin() + 2 * (second + 2000));
    CHECK(peakOf(std::vector<float>(out.begin(), out.begin() + 2 * (first - 2))) == 0.f);
    CHECK(peakOf(one) > 0.3f);
    CHECK(std::fabs(peakOf(two) - 0.5f * peakOf(one)) < 0.02f);
}

static void testChance() {
    // Count the bars in which the hit plays.
    const auto barsPlayed = [](float chance, int bars) {
        Engine engine;
        engine.send(step(RIM, 0, 1.f));
        engine.send(make(Command::SetStepChance, 0, RIM, 0, chance));
        engine.send(make(Command::Play));
        int played = 0;
        for (int bar = 0; bar < bars; ++bar) played += peakOf(renderFrames(engine, 16 * kStep)) > 0.f ? 1 : 0;
        return played;
    };
    CHECK(barsPlayed(1.f, 40) == 40);
    CHECK(barsPlayed(0.f, 40) == 0);
    const int half = barsPlayed(0.5f, 200);
    CHECK(half > 70 && half < 130);
    const int rare = barsPlayed(0.1f, 200);
    CHECK(rare > 5 && rare < 45);
    CHECK(barsPlayed(7.f, 20) == 20);          // held to "always"
    CHECK(barsPlayed(std::nanf(""), 20) == 20); // ignored, so still "always"

    // It works on a nudged hit too.
    Engine nudged;
    nudged.send(step(RIM, 4, 1.f));
    nudged.send(make(Command::SetStepNudge, 0, RIM, 4, -0.3f));
    nudged.send(make(Command::SetStepChance, 0, RIM, 4, 0.5f));
    nudged.send(make(Command::Play));
    int played = 0;
    for (int bar = 0; bar < 200; ++bar) played += peakOf(renderFrames(nudged, 16 * kStep)) > 0.f ? 1 : 0;
    CHECK(played > 70 && played < 130);
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
        e->send(trigger(KICK, 0.4f)); // quiet enough to stay under the ceiling
        e->send(trigger(CL_HAT, 0.4f));
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
        e->send(make(Command::SetStepNudge, 0, SNARE, 2, -0.37f));
        e->send(make(Command::SetStepNudge, 0, CL_HAT, 1, 0.21f));
        e->send(step(RIM, 3, 0.9f));
        e->send(make(Command::SetStepChance, 0, RIM, 3, 0.5f));
        e->send(make(Command::Play));
    }
    const int total = 60000; // more than a bar, so the dice are rolled several times
    const std::vector<float> a = renderFrames(whole, total);
    std::vector<float> b;
    const int sizes[] = {1, 63, 64, 65, 7, 300, 5413, 4087, 10000, 40000};
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

// ---- the rate converter ----------------------------------------------------------

// Convert a pure tone and measure two things: how loud it comes out, and how
// much of the output is anything other than that tone.
struct Converted { double level, impurity; };
static Converted convertTone(double inRate, double outRate, double hz) {
    RateConverter converter;
    converter.prepare(inRate, outRate);
    long n = 0;
    const auto pull = [&](float& l, float& r) {
        l = r = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * hz * static_cast<double>(n++) / inRate));
    };
    std::vector<float> left(20000), right(20000);
    converter.render(pull, left.data(), right.data(), 2000); // let it fill
    converter.render(pull, left.data(), right.data(), 20000);
    // The best-fitting tone at `hz`, by projecting onto a sine and a cosine.
    double ss = 0, cc = 0, sy = 0, cy = 0;
    for (int i = 0; i < 20000; ++i) {
        const double s = std::sin(2.0 * 3.14159265358979323846 * hz * i / outRate), c = std::cos(2.0 * 3.14159265358979323846 * hz * i / outRate);
        ss += s * s; cc += c * c; sy += s * left[i]; cy += c * left[i];
    }
    const double a = sy / ss, b = cy / cc;
    double rest = 0;
    for (int i = 0; i < 20000; ++i) {
        const double fit = a * std::sin(2.0 * 3.14159265358979323846 * hz * i / outRate) + b * std::cos(2.0 * 3.14159265358979323846 * hz * i / outRate);
        rest += (left[i] - fit) * (left[i] - fit);
    }
    bool sidesMatch = true;
    for (int i = 0; i < 20000; ++i) sidesMatch = sidesMatch && left[i] == right[i];
    CHECK(sidesMatch);
    return {std::sqrt(a * a + b * b), std::sqrt(rest / 20000.0) / (std::sqrt(a * a + b * b) / std::sqrt(2.0) + 1e-12)};
}

static void testRateConverter() {
    // 44100 to 48000 is the one the app uses. A tone must come out at the
    // same level, with everything else at least 60 dB (a thousand times) down.
    // (Each tone fits a whole number of times into the stretch measured, so
    // the measurement itself adds nothing.)
    for (double hz : {120.0, 1200.0, 4800.0, 9600.0, 15000.0}) {
        const Converted c = convertTone(44100.0, 48000.0, hz);
        CHECK(std::fabs(c.level - 1.0) < 0.02);
        CHECK(c.impurity < 0.001);
    }
    // Still good near the top of hearing.
    CHECK(convertTone(44100.0, 48000.0, 18000.0).impurity < 0.003);
    // Going down in rate, a tone the lower rate cannot carry is removed, not
    // folded back as a false tone.
    const Converted kept = convertTone(44100.0, 32000.0, 5000.0);
    CHECK(std::fabs(kept.level - 1.0) < 0.02 && kept.impurity < 0.001);
    RateConverter down;
    down.prepare(44100.0, 32000.0);
    long n = 0;
    std::vector<float> left(8000), right(8000);
    down.render([&](float& l, float& r) { l = r = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 19000.0 * static_cast<double>(n++) / 44100.0)); },
                left.data(), right.data(), 8000);
    float folded = 0.f;
    for (int i = 2000; i < 8000; ++i) folded = std::fabs(left[i]) > folded ? std::fabs(left[i]) : folded;
    CHECK(folded < 0.01f);
    // A steady level stays exactly steady.
    RateConverter flat;
    flat.prepare(44100.0, 48000.0);
    flat.render([](float& l, float& r) { l = 0.5f; r = -0.25f; }, left.data(), right.data(), 8000);
    for (int i = 100; i < 8000; ++i) {
        CHECK(std::fabs(left[i] - 0.5f) < 1e-5f && std::fabs(right[i] + 0.25f) < 1e-5f);
        if (std::fabs(left[i] - 0.5f) >= 1e-5f) break;
    }
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

// ---- recordings on drum lanes ---------------------------------------------------------

// A recording that is easy to measure: steady at `first` for its first half,
// then steady at `second`.
static std::unique_ptr<SampleData> twoHalves(int frames, float first, float second) {
    std::unique_ptr<SampleData> s(new SampleData);
    s->data.assign(static_cast<std::size_t>(frames), first);
    for (int i = frames / 2; i < frames; ++i) s->data[static_cast<std::size_t>(i)] = second;
    return s;
}
// How many frames from the first sound to the last.
static int soundingFrames(const std::vector<float>& buffer) {
    int first = -1, last = -1;
    for (std::size_t i = 0; i < buffer.size(); i += 2) {
        if (buffer[i] == 0.f) continue;
        if (first < 0) first = static_cast<int>(i / 2);
        last = static_cast<int>(i / 2);
    }
    return first < 0 ? 0 : last - first + 1;
}
static const float kTrackLevel = 0.65f;
static bool about(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static void testARecordingOnALane() {
    std::unique_ptr<Engine> engine(new Engine);
    const std::vector<float> kick = (engine->send(trigger(0)), renderFrames(*engine, 48000));
    CHECK(soundingFrames(kick) > 5000);
    renderFrames(*engine, 48000);

    CHECK(engine->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.4f)));
    engine->send(trigger(0));
    std::vector<float> out = renderFrames(*engine, 4000);
    CHECK(soundingFrames(out) == 1000); // the recording, all of it, and nothing after
    CHECK(about(out[0], 0.2f * kTrackLevel) && about(out[2 * 499], 0.2f * kTrackLevel));
    CHECK(about(out[2 * 500], 0.4f * kTrackLevel) && about(out[2 * 999], 0.4f * kTrackLevel));
    CHECK(out[0] == out[1]); // both sides
    // The other lanes are still the kit.
    engine->send(trigger(1));
    CHECK(soundingFrames(renderFrames(*engine, 48000)) > 2000);
    // Level and the lane's volume work on it as on a drum.
    engine->send(trigger(0, 0.5f));
    CHECK(about(renderFrames(*engine, 4000)[0], 0.1f * kTrackLevel));
    engine->send(make(Command::SetLaneLevel, 0, 0, 0, 0.5f));
    engine->send(trigger(0));
    CHECK(about(renderFrames(*engine, 4000)[0], 0.1f * kTrackLevel));
    engine->send(make(Command::SetLaneLevel, 0, 0, 0, 1.f));

    // A different recording replaces it, and stops the old one ringing.
    engine->send(trigger(0));
    renderFrames(*engine, 100);
    CHECK(engine->setLaneSample(0, 0, twoHalves(300, 0.3f, 0.3f)));
    CHECK(peakOf(renderFrames(*engine, 2000)) == 0.f);
    engine->collectGarbage();
    engine->send(trigger(0));
    out = renderFrames(*engine, 2000);
    CHECK(soundingFrames(out) == 300 && about(out[0], 0.3f * kTrackLevel));
    // Nothing brings the drum back.
    CHECK(engine->setLaneSample(0, 0, nullptr));
    engine->send(trigger(0));
    out = renderFrames(*engine, 48000);
    CHECK(out == kick);
    engine->collectGarbage();

    // Only a drum track's lanes can take one.
    CHECK(!engine->setLaneSample(1, 0, twoHalves(10, 1.f, 1.f))); // an empty track
    CHECK(engine->addSynthTrack(1));
    CHECK(!engine->setLaneSample(1, 0, twoHalves(10, 1.f, 1.f)));
    CHECK(!engine->setLaneSample(0, kDrumLanes, twoHalves(10, 1.f, 1.f)));
    CHECK(!engine->setLaneSample(0, -1, twoHalves(10, 1.f, 1.f)));
    CHECK(!engine->setLaneSample(kNumTracks, 0, twoHalves(10, 1.f, 1.f)));
    CHECK(engine->addDrumTrack(2));
    CHECK(engine->setLaneSample(2, 3, twoHalves(400, 0.2f, 0.2f)));
    engine->send(trigger(3, 1.f, 2));
    CHECK(soundingFrames(renderFrames(*engine, 2000)) == 400);
    // An empty recording is the same as none.
    std::unique_ptr<SampleData> empty(new SampleData);
    CHECK(engine->setLaneSample(2, 3, std::move(empty)));
    engine->send(trigger(3, 1.f, 2));
    CHECK(soundingFrames(renderFrames(*engine, 48000)) > 2000);

    // Taking the track away takes its recordings with it.
    CHECK(engine->setLaneSample(2, 3, twoHalves(400, 0.2f, 0.2f)));
    renderFrames(*engine, 64);
    CHECK(engine->removeTrack(2));
    CHECK(!engine->setLaneSample(2, 3, twoHalves(400, 0.2f, 0.2f)));
    renderFrames(*engine, 64);
    engine->collectGarbage();
    CHECK(engine->addDrumTrack(2));
    engine->send(trigger(3, 1.f, 2));
    CHECK(soundingFrames(renderFrames(*engine, 48000)) > 2000); // the kit's drum again
    // One still waiting in the queue when the engine goes is freed with it.
    CHECK(engine->setLaneSample(0, 5, twoHalves(400, 0.2f, 0.2f)));
}

static void testWhereAHitStarts() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.4f)));
    // From the middle: half as long, and already in the second half.
    engine->send(make(Command::Trigger, 0, 0, 5000, 1.f));
    std::vector<float> out = renderFrames(*engine, 4000);
    CHECK(soundingFrames(out) <= 500 && soundingFrames(out) >= 498);
    CHECK(about(out[2 * 250], 0.4f * kTrackLevel));
    // It fades up over about a millisecond instead of clicking in.
    CHECK(std::fabs(out[0]) < 0.01f && std::fabs(out[2 * 10]) < 0.4f * kTrackLevel * 0.5f);
    CHECK(about(out[2 * 60], 0.4f * kTrackLevel));
    for (int i = 1; i < 60; ++i) CHECK(out[2 * i] >= out[2 * (i - 1)]);
    // From the very start there is no fade: a drum keeps its front.
    engine->send(make(Command::Trigger, 0, 0, 0, 1.f));
    CHECK(about(renderFrames(*engine, 4000)[0], 0.2f * kTrackLevel));
    // Past the end: nothing. Before the start: the start.
    engine->send(make(Command::Trigger, 0, 0, 20000, 1.f));
    CHECK(soundingFrames(renderFrames(*engine, 4000)) == 0);
    engine->send(make(Command::Trigger, 0, 0, -300, 1.f));
    CHECK(soundingFrames(renderFrames(*engine, 4000)) == 1000);

    // In a pattern each hit has its own.
    engine->send(step(0, 0, 1.f));
    engine->send(step(0, 4, 1.f));
    engine->send(make(Command::SetStepStart, 0, 0, 4, 0.75f));
    engine->send(make(Command::SetStepStart, 0, 0, 99, 0.5f)); // no such step: ignored
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 8 * kStep);
    std::vector<float> first(out.begin(), out.begin() + 2 * 4 * kStep), second(out.begin() + 2 * 4 * kStep, out.end());
    CHECK(soundingFrames(first) == 1000);
    CHECK(soundingFrames(second) <= 250 && soundingFrames(second) >= 248);
    engine->send(make(Command::Stop));
    // A hit moved late still starts where it was told to.
    engine->send(make(Command::SetStepNudge, 0, 0, 4, 0.25f));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 8 * kStep);
    second.assign(out.begin() + 2 * 4 * kStep, out.end());
    CHECK(onsetOf(second) >= kStep / 4 - 1 && onsetOf(second) <= kStep / 4 + 1);
    CHECK(soundingFrames(second) <= 250 && soundingFrames(second) >= 248);
    engine->send(make(Command::Stop));
    renderFrames(*engine, 64);

    // A whole song loaded at once brings its starts.
    std::unique_ptr<TrackImage> image(new TrackImage);
    image->level[0][0][2] = 1.f;
    image->start[0][0][2] = 0.5f;
    CHECK(engine->setTrackImage(0, std::move(image)));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 8 * kStep);
    CHECK(onsetOf(out) >= 2 * kStep && onsetOf(out) <= 2 * kStep + 1 && soundingFrames(out) <= 500 && soundingFrames(out) >= 498);
    // And removing the track forgets them.
    engine->send(make(Command::Stop));
    CHECK(engine->removeTrack(0));
    CHECK(engine->addDrumTrack(0));
    CHECK(engine->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.4f)));
    engine->send(step(0, 2, 1.f));
    engine->send(make(Command::Play));
    CHECK(soundingFrames(renderFrames(*engine, 8 * kStep)) == 1000);
}

// A note may be any length, and is let go on its own sample.
static void testANoteEndsPartWayThroughAStep() {
    // The same note at three lengths. Each plays exactly like a longer one
    // until the moment it is let go, and differently from then on.
    const auto bar = [](int whole, float extra) {
        std::unique_ptr<Engine> engine(new Engine);
        CHECK(engine->addSynthTrack(1));
        Command c = make(Command::SetNote, 1, 0, 2, 0.9f); // slot 0, step 3
        c.c = 60;
        c.d = whole;
        c.extra = extra;
        engine->send(c);
        engine->send(make(Command::Play, 0, 0, 1));
        return renderFrames(*engine, 12 * kStep);
    };
    const auto partsAt = [](const std::vector<float>& a, const std::vector<float>& b) {
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) return static_cast<int>(i / 2);
        return -1;
    };
    const std::vector<float> two = bar(2, 0.f), twoAndAHalf = bar(2, 0.5f), three = bar(3, 0.f), four = bar(4, 0.f);
    CHECK(peakOf(four) > 0.01f);
    // Phazed's own output is a fixed 12 to 15 samples behind (its sample-rate converter).
    const int start = 2 * kStep + 13;
    CHECK(std::abs(partsAt(two, four) - (start + 2 * kStep)) <= 3);
    CHECK(std::abs(partsAt(three, four) - (start + 3 * kStep)) <= 3);
    CHECK(std::abs(partsAt(twoAndAHalf, four) - (start + 2 * kStep + kStep / 2)) <= 3);
    CHECK(std::abs(partsAt(bar(0, 0.25f), four) - (start + kStep / 4)) <= 3); // shorter than a step
    // Written either way, the same length is the same sound.
    CHECK(bar(2, 1.f) == three);
    // Nonsense: no note at all, or a whole-step one.
    CHECK(peakOf(bar(0, 0.f)) == 0.f && peakOf(bar(0, 0.01f)) == 0.f && peakOf(bar(-1, 0.5f)) == 0.f);
    CHECK(bar(2, std::nanf("")) == two);

    // A note that ends exactly where the same key starts again is let go
    // first and then played: the second one is not swallowed.
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(1));
    for (int slot = 0; slot < 2; ++slot) {
        Command c = make(Command::SetNote, 1, slot, slot * 2, 0.9f);
        c.c = 60;
        c.d = 2;
        engine->send(c);
    }
    engine->send(make(Command::Play, 0, 0, 1));
    const std::vector<float> both = renderFrames(*engine, 4 * kStep);
    const std::vector<float> firstHalf(both.begin(), both.begin() + 2 * 2 * kStep), secondHalf(both.begin() + 2 * 2 * kStep, both.end());
    CHECK(peakOf(secondHalf) > 0.5f * peakOf(firstHalf));
    // And Stop lets go of a note that was part-way through.
    engine->send(make(Command::Stop));
    renderFrames(*engine, 96000);
    CHECK(peakOf(renderFrames(*engine, 4000)) < 1e-4f);
}

static void testPanic() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(1));
    for (int s = 0; s < kSteps; ++s) engine->send(step(s % 2 == 0 ? 0 : 7, s, 1.f)); // kicks and crashes: something always ringing
    Command note = make(Command::SetNote, 1, 0, 0, 1.f);
    note.c = 48;
    note.d = 16;
    engine->send(note);
    engine->send(make(Command::Play, 0, 0, 1));
    CHECK(peakOf(renderFrames(*engine, 3 * kStep)) > 0.2f);
    CHECK(!engine->silenced() && engine->playStep() >= 0);

    // Panic: nothing within 5 ms, without a click on the way down.
    engine->send(make(Command::Panic, 0, 1));
    const std::vector<float> down = renderFrames(*engine, 480);
    CHECK(down[0] != 0.f);                 // a fade, not a cut
    for (int i = 245; i < 480; ++i) CHECK(down[2 * i] == 0.f && down[2 * i + 1] == 0.f);
    CHECK(engine->silenced() && engine->playStep() == -1 && engine->playScene() == -1);
    // It stays shut for as long as it is held, whatever is asked for.
    engine->send(trigger(0));
    engine->send(make(Command::NoteOn, 1, 60, 0, 1.f));
    CHECK(peakOf(renderFrames(*engine, 48000)) == 0.f);
    engine->send(make(Command::NoteOff, 1, 60));
    CHECK(peakOf(renderFrames(*engine, 96000)) == 0.f);
    CHECK(engine->silenced());
    // The song has stopped: nothing plays by itself.
    CHECK(engine->playStep() == -1);

    // Let go. What was ringing has died away behind the gate, so it opens,
    // and it opens onto silence: the crash that was cut does not come back.
    engine->send(make(Command::Panic, 0, 0));
    CHECK(peakOf(renderFrames(*engine, 24000)) == 0.f);
    CHECK(!engine->silenced());
    // And everything works as before.
    engine->send(trigger(0));
    CHECK(peakOf(renderFrames(*engine, 4000)) > 0.2f);
    renderFrames(*engine, 96000);

    // Let go while something still rings: the gate waits for it. A crash is
    // cut at once, so use a held synth note, which is only let go.
    engine->send(make(Command::NoteOn, 1, 48, 0, 1.f));
    renderFrames(*engine, 4800);
    engine->send(make(Command::Panic, 0, 1));
    engine->send(make(Command::Panic, 0, 0)); // a quick squeeze
    const std::vector<float> squeeze = renderFrames(*engine, 2400);
    for (int i = 245; i < 2400; ++i) CHECK(squeeze[2 * i] == 0.f);
    CHECK(engine->silenced());
    // The next thing played opens it at once, fading up over 30 ms.
    renderFrames(*engine, 480);
    engine->send(trigger(0));
    const std::vector<float> up = renderFrames(*engine, 4000);
    CHECK(peakOf(up) > 0.2f && std::fabs(up[2 * 100]) < 0.2f); // the kick's front is under the fade
    CHECK(!engine->silenced());
    // Three seconds at most, even if something never dies away.
    renderFrames(*engine, 96000);
    engine->send(make(Command::NoteOn, 1, 48, 0, 1.f));
    renderFrames(*engine, 4800);
    engine->send(make(Command::Panic, 0, 1));
    renderFrames(*engine, 480);
    CHECK(engine->silenced());
    engine->send(make(Command::Panic, 0, 0));
    renderFrames(*engine, 3 * 48000 + 4800);
    CHECK(!engine->silenced());
    // A note started while it is held never dies away. Let go, and the gate
    // waits for it, but only until three seconds after it shut.
    engine->send(make(Command::Panic, 0, 1));
    engine->send(make(Command::NoteOn, 1, 48, 0, 1.f));
    renderFrames(*engine, 48000);
    engine->send(make(Command::Panic, 0, 0));
    CHECK(peakOf(renderFrames(*engine, 48000)) == 0.f && engine->silenced()); // two seconds in: still shut
    renderFrames(*engine, 48000 + 4800);
    CHECK(!engine->silenced());                         // three seconds: open, though it never went quiet
    CHECK(peakOf(renderFrames(*engine, 4800)) > 0.f);   // what is left of the note is there
    engine->send(make(Command::NoteOff, 1, 48));
    renderFrames(*engine, 4 * 48000);

    // A crash rings for nearly two seconds. Panic cuts it, so a quick
    // squeeze is over in a tenth of a second, not two.
    {
        std::unique_ptr<Engine> drums(new Engine);
        drums->send(trigger(7));
        renderFrames(*drums, 4800);
        drums->send(make(Command::Panic, 0, 1));
        renderFrames(*drums, 480);
        drums->send(make(Command::Panic, 0, 0));
        renderFrames(*drums, 9600);
        CHECK(!drums->silenced());
        CHECK(peakOf(renderFrames(*drums, 48000)) == 0.f); // and the crash is gone, not waiting behind the gate
    }

    // Panic twice over is still one panic.
    engine->send(make(Command::Panic, 0, 1));
    engine->send(make(Command::Panic, 0, 1));
    CHECK(peakOf(renderFrames(*engine, 4800)) < 1.f && engine->silenced());
    engine->send(make(Command::Panic, 0, 0));
    renderFrames(*engine, 4 * 48000);
    CHECK(!engine->silenced());
}

static void testCatchingTheLastBar() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.4f)));
    engine->send(step(0, 0, 1.f));
    engine->send(step(0, 8, 0.5f));
    // Stopped, nothing played: a bar's worth of silence.
    renderFrames(*engine, 5000);
    std::vector<float> bar = engine->lastBar();
    CHECK(bar.size() == 16u * kStep && peakOf(bar) == 0.f);

    // Play. Part-way through the second bar, the catch is the first bar,
    // exactly: bar line to bar line, the hits where they were.
    engine->send(make(Command::Play, 0, 0, 1));
    renderFrames(*engine, 16 * kStep + 7 * kStep + 123);
    bar = engine->lastBar();
    CHECK(bar.size() == 16u * kStep);
    CHECK(about(bar[0], 0.2f * kTrackLevel) && about(bar[999], 0.4f * kTrackLevel) && bar[1000] == 0.f);
    CHECK(bar[8 * kStep - 1] == 0.f && about(bar[8 * kStep], 0.1f * kTrackLevel) && about(bar[8 * kStep + 999], 0.2f * kTrackLevel));
    CHECK(bar[16 * kStep - 1] == 0.f);
    // The same thing asked again is the same bar; a bar later it is the next one.
    CHECK(engine->lastBar() == bar);
    engine->send(step(0, 4, 1.f));
    renderFrames(*engine, 2 * 16 * kStep); // on into the bar after the one that has the new hit
    const std::vector<float> next = engine->lastBar();
    CHECK(next.size() == 16u * kStep && next != bar && about(next[4 * kStep], 0.2f * kTrackLevel));
    // Stopped part-way through a bar: the catch is still the last whole one.
    engine->send(make(Command::Stop));
    renderFrames(*engine, 20000);
    CHECK(engine->lastBar() == next);
    // Played by hand after stopping: now the catch is what was just heard.
    engine->send(trigger(0, 0.5f));
    renderFrames(*engine, 3000);
    bar = engine->lastBar();
    CHECK(bar.size() == 16u * kStep && about(bar[bar.size() - 3000], 0.1f * kTrackLevel) && bar[bar.size() - 1] == 0.f);
    // Started again, two steps in: no whole bar yet, so a bar's worth up to
    // now, not the stretch of time since the song last began.
    engine->send(make(Command::Play, 0, 0, 1));
    renderFrames(*engine, 2 * kStep);
    bar = engine->lastBar();
    CHECK(bar.size() == 16u * kStep && about(bar[bar.size() - 2 * kStep], 0.2f * kTrackLevel));
    renderFrames(*engine, 16 * kStep);
    CHECK(engine->lastBar().size() == 16u * kStep && about(engine->lastBar()[0], 0.2f * kTrackLevel));
    // A panic: the silence after it is what is caught, not the din before.
    engine->send(make(Command::Panic, 0, 1));
    renderFrames(*engine, 20 * kStep);
    CHECK(peakOf(engine->lastBar()) == 0.f);
    engine->send(make(Command::Panic, 0, 0));
    renderFrames(*engine, 48000);
    // A tempo change: the bar caught is as long as the bar that was played.
    renderFrames(*engine, 20000);
    engine->send(make(Command::SetTempo, 0, 0, 0, 240.f));
    engine->send(make(Command::Play, 0, 0, 1));
    renderFrames(*engine, 8 * kStep + 100);
    bar = engine->lastBar();
    CHECK(bar.size() == 8u * kStep && about(bar[0], 0.2f * kTrackLevel) && about(bar[2 * kStep], 0.2f * kTrackLevel));
    // Stopped a long while (longer than it remembers): the last bar's worth
    // of what was heard, which is silence, not something ten seconds stale.
    engine->send(make(Command::Stop));
    renderFrames(*engine, kCatchFrames + 5000);
    bar = engine->lastBar();
    CHECK(bar.size() == 8u * kStep && peakOf(bar) == 0.f);
    // Stopped and played by hand: what was just heard is what is caught.
    engine->send(trigger(0));
    renderFrames(*engine, 3000);
    bar = engine->lastBar();
    CHECK(bar.size() == 8u * kStep && about(bar[bar.size() - 3000], 0.2f * kTrackLevel) && bar[bar.size() - 1] == 0.f);
    // The slowest tempo: a bar is six seconds, and still fits.
    engine->send(make(Command::SetTempo, 0, 0, 0, 40.f));
    engine->send(make(Command::Play, 0, 0, 1));
    renderFrames(*engine, 288000 + 1000);
    bar = engine->lastBar();
    CHECK(bar.size() == 288000u && about(bar[0], 0.2f * kTrackLevel));
}

// ---- the effects ------------------------------------------------------------------------

// Run a signal through one effect by itself.
static std::vector<float> through(Effect& effect, const std::vector<float>& in) {
    std::vector<float> left = in, right = in;
    for (std::size_t at = 0; at < in.size(); at += 64) {
        const int n = static_cast<int>(in.size() - at < 64 ? in.size() - at : 64);
        effect.process(left.data() + at, right.data() + at, n);
    }
    for (std::size_t i = 0; i < in.size(); ++i) CHECK(std::isfinite(left[i]) && std::isfinite(right[i]));
    return left;
}
static std::vector<float> sine(float hz, int frames, float level = 0.5f) {
    std::vector<float> v(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) v[static_cast<std::size_t>(i)] = level * std::sin(6.2831853f * hz * static_cast<float>(i) / 48000.f);
    return v;
}
static float rmsOf(const std::vector<float>& v, int from, int to) {
    double e = 0.0;
    for (int i = from; i < to; ++i) e += static_cast<double>(v[static_cast<std::size_t>(i)]) * v[static_cast<std::size_t>(i)];
    return static_cast<float>(std::sqrt(e / (to - from)));
}

static void testEffectsOneByOne() {
    CHECK(makeEffect(EffectKind::None, 48000.f, 120.f) == nullptr);
    CHECK(makeEffect(EffectKind::Count, 48000.f, 120.f) == nullptr);
    // Every one of them: with MIX at nothing the sound comes through
    // untouched, and nonsense settings do no harm.
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        CHECK(info.params >= 3 && info.params <= kEffectParams && info.name[0]);
        int mix = -1;
        for (int p = 0; p < info.params; ++p)
            if (std::string(info.paramNames[p]) == "MIX") mix = p;
        CHECK(mix >= 0);
        std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, 120.f);
        CHECK(effect != nullptr);
        if (!effect || mix < 0) continue;
        const std::vector<float> in = sine(330.f, 48000); // long enough for an echo to come back
        std::vector<float> wet = through(*effect, in);
        CHECK(wet != in); // at its defaults it does something
        effect->reset();
        effect->setParam(mix, 0.f);
        if (kind == EffectKind::Drive) effect->setParam(3, 0.5f); // its OUTPUT: the middle is unchanged
        std::vector<float> dry = through(*effect, in);
        float worst = 0.f;
        for (std::size_t i = 0; i < in.size(); ++i) worst = std::fabs(dry[i] - in[i]) > worst ? std::fabs(dry[i] - in[i]) : worst;
        CHECK(worst < 1e-6f);
        effect->setParam(mix, 1.f);
        for (int p = -1; p <= kEffectParams; ++p) {
            effect->setParam(p, std::nanf(""));
            effect->setParam(p, 99.f);
            effect->setParam(p, -99.f);
        }
        effect->setTempo(0.f);
        effect->setTempo(std::nanf(""));
        through(*effect, in); // checks every sample is a number
        std::vector<float> loud(4800, 4.f);
        through(*effect, loud);
    }
    CHECK(std::string(effectInfo(EffectKind::None).name) == "NONE" && std::string(effectInfo(static_cast<EffectKind>(99)).name) == "NONE");

    // FILTER: low-pass keeps the low tone and loses the high one; high-pass the other way.
    {
        std::unique_ptr<Effect> f = makeEffect(EffectKind::Filter, 48000.f, 120.f);
        f->setParam(0, 0.5f); // about 800 Hz
        f->setParam(1, 0.f);
        const std::vector<float> low = sine(100.f, 24000), high = sine(8000.f, 24000);
        const float lowIn = rmsOf(low, 12000, 24000), highIn = rmsOf(high, 12000, 24000);
        CHECK(rmsOf(through(*f, low), 12000, 24000) > 0.9f * lowIn);
        f->reset();
        CHECK(rmsOf(through(*f, high), 12000, 24000) < 0.05f * highIn);
        f->setParam(2, 1.f); // high-pass
        f->reset();
        CHECK(rmsOf(through(*f, low), 12000, 24000) < 0.05f * lowIn);
        f->reset();
        CHECK(rmsOf(through(*f, high), 12000, 24000) > 0.9f * highIn);
        f->setParam(2, 0.5f); // band-pass: both go
        f->reset();
        CHECK(rmsOf(through(*f, low), 12000, 24000) < 0.5f * lowIn);
        f->reset();
        CHECK(rmsOf(through(*f, high), 12000, 24000) < 0.5f * highIn);
        // Resonance lifts what sits at the cutoff. Opening the cutoff lets the high tone back.
        f->setParam(2, 0.f);
        const std::vector<float> at = sine(807.f, 24000, 0.1f);
        f->reset();
        const float plain = rmsOf(through(*f, at), 12000, 24000);
        f->setParam(1, 1.f);
        f->reset();
        CHECK(rmsOf(through(*f, at), 12000, 24000) > 4.f * plain);
        f->setParam(1, 0.f);
        f->setParam(0, 1.f);
        f->reset();
        CHECK(rmsOf(through(*f, high), 12000, 24000) > 0.8f * highIn);
        // Reset empties it: after a loud burst, silence in is silence out.
        through(*f, sine(200.f, 4800, 1.f));
        f->reset();
        CHECK(peakOf(through(*f, std::vector<float>(480, 0.f))) == 0.f);
    }
    // DRIVE: never louder than its ceiling however hard it is hit, and it bends the wave.
    {
        std::unique_ptr<Effect> d = makeEffect(EffectKind::Drive, 48000.f, 120.f);
        d->setParam(0, 1.f);
        d->setParam(1, 1.f);
        d->setParam(3, 0.5f);
        const std::vector<float> out = through(*d, sine(200.f, 9600, 1.f));
        CHECK(peakOf(out) < 0.2f && peakOf(out) > 0.1f); // tanh, made up by 1/sqrt(40)
        // Flat on top: most of the time it sits near its peak.
        int nearPeak = 0;
        for (int i = 4800; i < 9600; ++i) nearPeak += std::fabs(out[static_cast<std::size_t>(i)]) > 0.9f * peakOf(out);
        CHECK(nearPeak > 4800 * 0.7);
        d->setParam(0, 0.f); // no drive: nearly the wave that went in
        d->reset();
        const std::vector<float> in = sine(200.f, 9600, 0.1f);
        const std::vector<float> clean = through(*d, in);
        CHECK(std::fabs(rmsOf(clean, 4800, 9600) - rmsOf(in, 4800, 9600)) < 0.01f);
        d->setParam(3, 1.f); // OUTPUT up: twice as loud
        d->reset();
        CHECK(std::fabs(rmsOf(through(*d, in), 4800, 9600) - 2.f * rmsOf(in, 4800, 9600)) < 0.02f);
        d->setParam(3, 0.f);
        CHECK(peakOf(through(*d, in)) == 0.f);
    }
    // DELAY: a click comes back when the tempo says, quieter each time.
    {
        std::unique_ptr<Effect> d = makeEffect(EffectKind::Delay, 48000.f, 120.f);
        d->setParam(0, 0.5f); // a quarter note: four steps
        d->setParam(1, 0.5f);
        d->setParam(2, 1.f);
        d->setParam(3, 1.f);
        std::vector<float> click(80000, 0.f);
        click[0] = 1.f;
        std::vector<float> out = through(*d, click);
        CHECK(out[0] == 1.f); // the click itself
        CHECK(peakOf(std::vector<float>(out.begin() + 1, out.begin() + 23990)) == 0.f);
        float first = 0.f, second = 0.f;
        for (int i = 23995; i < 24010; ++i) first = std::fabs(out[static_cast<std::size_t>(i)]) > first ? std::fabs(out[static_cast<std::size_t>(i)]) : first;
        for (int i = 47990; i < 48020; ++i) second = std::fabs(out[static_cast<std::size_t>(i)]) > second ? std::fabs(out[static_cast<std::size_t>(i)]) : second;
        CHECK(first > 0.5f && second > 0.1f && second < 0.6f * first);
        // Faster song, sooner echo. The time setting picks the note value.
        d->reset();
        d->setTempo(240.f);
        out = through(*d, click);
        CHECK(peakOf(std::vector<float>(out.begin() + 11995, out.begin() + 12010)) > 0.5f);
        d->reset();
        d->setParam(0, 0.f); // a sixteenth
        out = through(*d, click);
        CHECK(peakOf(std::vector<float>(out.begin() + 2995, out.begin() + 3010)) > 0.5f);
        CHECK(delaySteps(0.f) == 1 && delaySteps(0.2f) == 2 && delaySteps(0.4f) == 3 && delaySteps(0.5f) == 4 && delaySteps(0.7f) == 6 && delaySteps(1.f) == 8);
        // The slowest tempo and the longest time still fits its memory.
        d->reset();
        d->setTempo(40.f);
        d->setParam(0, 1.f);
        std::vector<float> longClick(150000, 0.f);
        longClick[0] = 1.f;
        out = through(*d, longClick);
        CHECK(peakOf(std::vector<float>(out.begin() + 143990, out.begin() + 144010)) > 0.5f);
        // TONE works on what is fed back: the first echo is the click as it
        // was, and each one after is duller, so spread out and lower.
        d->setTempo(120.f);
        d->setParam(0, 0.f);
        d->setParam(1, 0.6f);
        const auto secondEcho = [&](float tone) {
            d->setParam(2, tone);
            d->reset();
            const std::vector<float> echoes = through(*d, click);
            CHECK(peakOf(std::vector<float>(echoes.begin() + 5995, echoes.begin() + 6010)) > 0.9f);
            return peakOf(std::vector<float>(echoes.begin() + 11995, echoes.begin() + 12100));
        };
        const float bright = secondEcho(1.f), dull = secondEcho(0.f);
        CHECK(bright > 0.3f && dull > 0.005f && dull < 0.3f * bright);
        d->setParam(2, 1.f);
        // No feedback: one echo and no more. Reset: none at all.
        d->setTempo(120.f);
        d->setParam(0, 0.f);
        d->setParam(1, 0.f);
        d->reset();
        out = through(*d, click);
        CHECK(peakOf(std::vector<float>(out.begin() + 5995, out.begin() + 6010)) > 0.5f);   // a sixteenth at 120
        CHECK(peakOf(std::vector<float>(out.begin() + 6010, out.end())) == 0.f);              // and nothing after it
        through(*d, sine(300.f, 4800));
        d->reset();
        CHECK(peakOf(through(*d, std::vector<float>(48000, 0.f))) == 0.f);
    }
    // REVERB: a click leaves a tail that dies away; a bigger room, a longer one.
    {
        std::unique_ptr<Effect> r = makeEffect(EffectKind::Reverb, 48000.f, 120.f);
        r->setParam(2, 1.f);
        std::vector<float> click(96000, 0.f);
        click[0] = 1.f;
        const auto tail = [&](float size) {
            r->setParam(0, size);
            r->reset();
            const std::vector<float> out = through(*r, click);
            CHECK(rmsOf(out, 2400, 9600) > 1e-4f);                         // there is a tail
            CHECK(rmsOf(out, 48000, 52800) < rmsOf(out, 2400, 7200));     // and it dies away
            return rmsOf(out, 24000, 28800);
        };
        const float small = tail(0.f), big = tail(1.f);
        CHECK(big > 5.f * small);
        // The two sides are not the same: that is the width.
        r->reset();
        std::vector<float> left = click, right = click;
        r->process(left.data(), right.data(), 9600);
        CHECK(std::vector<float>(left.begin(), left.begin() + 9600) != std::vector<float>(right.begin(), right.begin() + 9600));
        r->reset();
        CHECK(peakOf(through(*r, std::vector<float>(48000, 0.f))) == 0.f);
    }
    // CRUSH: few bits, few levels; a low rate, the same value held.
    {
        std::unique_ptr<Effect> c = makeEffect(EffectKind::Crush, 48000.f, 120.f);
        c->setParam(0, 1.f); // three bits
        c->setParam(1, 0.f);
        c->setParam(2, 1.f);
        const std::vector<float> out = through(*c, sine(100.f, 9600, 0.9f));
        std::vector<float> levels;
        for (float v : out) {
            bool seen = false;
            for (float l : levels) seen = seen || std::fabs(l - v) < 1e-6f;
            if (!seen) levels.push_back(v);
        }
        CHECK(levels.size() >= 5 && levels.size() <= 9);
        c->setParam(0, 0.f); // sixteen bits: as good as untouched
        c->reset();
        const std::vector<float> in = sine(100.f, 9600, 0.9f);
        const std::vector<float> fine = through(*c, in);
        float worst = 0.f;
        for (std::size_t i = 0; i < in.size(); ++i) worst = std::fabs(fine[i] - in[i]) > worst ? std::fabs(fine[i] - in[i]) : worst;
        CHECK(worst < 1e-4f);
        c->setParam(1, 1.f); // one sample in forty
        c->reset();
        const std::vector<float> held = through(*c, in);
        int changes = 0;
        for (std::size_t i = 1; i < held.size(); ++i) changes += held[i] != held[i - 1];
        CHECK(changes > 200 && changes < 250); // 9600 / 40
    }
}

static void testEffectsInTheEngine() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.2f)));
    const auto hit = [&](int frames) {
        engine->send(trigger(0));
        return renderFrames(*engine, frames);
    };
    const std::vector<float> dry = hit(30000);
    CHECK(soundingFrames(dry) == 1000);
    // A delay on the track, put in while it runs: the hit comes back a quarter note later.
    CHECK(engine->setEffect(0, 0, EffectKind::Delay));
    std::vector<float> out = hit(30000);
    CHECK(soundingFrames(out) > 24000 && std::fabs(out[2 * 24500]) > 0.01f);
    // Its settings by command. MIX to nothing: dry again.
    engine->send(effectParam(0, 0, 3, 0.f));
    renderFrames(*engine, 200000);
    CHECK(hit(30000) == dry);
    engine->send(effectParam(0, 0, 3, 1.f));
    engine->send(effectParam(0, 0, 1, 0.f));
    // It follows the engine's tempo, whenever that is set.
    engine->send(make(Command::SetTempo, 0, 0, 0, 240.f));
    renderFrames(*engine, 200000);
    out = hit(30000);
    CHECK(std::fabs(out[2 * 12500]) > 0.01f && std::fabs(out[2 * 24500]) == 0.f);
    CHECK(engine->setEffect(0, 0, EffectKind::Delay)); // a new one learns the tempo as it goes in
    engine->send(effectParam(0, 0, 1, 0.f));
    renderFrames(*engine, 100);
    out = hit(30000);
    CHECK(std::fabs(out[2 * 12500]) > 0.01f && std::fabs(out[2 * 24500]) == 0.f);
    engine->send(make(Command::SetTempo, 0, 0, 0, 120.f));
    // Another kind replaces it; None empties the slot. The old ones are handed back.
    CHECK(engine->setEffect(0, 0, EffectKind::Crush));
    engine->send(effectParam(0, 0, 0, 1.f));
    renderFrames(*engine, 100000);
    out = hit(30000);
    CHECK(soundingFrames(out) >= 996 && soundingFrames(out) <= 1003 && out != dry); // it holds each value a sample or two, so it starts and ends that much late
    CHECK(engine->setEffect(0, 0, EffectKind::None));
    renderFrames(*engine, 100000);
    CHECK(hit(30000) == dry);
    engine->collectGarbage();
    // The second slot comes after the first; the master's after every track.
    CHECK(engine->setEffect(0, 1, EffectKind::Delay) && engine->setEffect(kMasterTrack, 1, EffectKind::Delay));
    engine->send(effectParam(0, 1, 1, 0.f));
    engine->send(effectParam(kMasterTrack, 1, 1, 0.f));
    out = hit(60000);
    CHECK(std::fabs(out[2 * 24500]) > 0.01f && std::fabs(out[2 * 48500]) > 0.001f); // an echo, and the echo's echo
    // Panic empties them: no echo comes back after it.
    engine->send(trigger(0));
    renderFrames(*engine, 2000);
    engine->send(make(Command::Panic, 0, 1));
    renderFrames(*engine, 1000);
    engine->send(make(Command::Panic, 0, 0));
    CHECK(peakOf(renderFrames(*engine, 60000)) == 0.f);
    // Nowhere to put one: refused.
    CHECK(!engine->setEffect(0, 2, EffectKind::Delay) && !engine->setEffect(0, -1, EffectKind::Delay));
    CHECK(!engine->setEffect(12, 0, EffectKind::Delay) && !engine->setEffect(-2, 0, EffectKind::Delay));
    // One still waiting in the queue when the engine goes is freed with it.
    CHECK(engine->setEffect(3, 0, EffectKind::Reverb));
}

// ---- the sampler -----------------------------------------------------------------------

static void testTheSampler() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(1, SynthKind::Sampler));
    const float full = 1.f * 0.8f * sampler::kVoiceGain * kTrackLevel; // velocity, LEVEL at its default, voice gain, track level
    const auto set = [&](int id, int value) { engine->send(make(Command::SetSetting, 1, id, value)); };
    const auto play = [&](int note, int frames, float velocity = 1.f) {
        engine->send(make(Command::NoteOn, 1, note, 0, velocity));
        return renderFrames(*engine, frames);
    };
    const auto quiet = [&] {
        engine->send(make(Command::NoteOff, 1, 60));
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000 * 5);
    };
    // No recording: no sound.
    CHECK(peakOf(play(60, 4800)) == 0.f);
    // A recording: a ramp, so where the sampler is reading shows in the level.
    std::unique_ptr<SampleData> ramp(new SampleData);
    ramp->data.resize(48000);
    for (int i = 0; i < 48000; ++i) ramp->data[static_cast<std::size_t>(i)] = 0.25f + 0.5f * static_cast<float>(i) / 48000.f;
    CHECK(engine->setTrackSample(1, std::move(ramp)));
    set(sampler::Attack, 0);

    // At its root key it plays as it was made: a second long, the same values.
    std::vector<float> out = play(60, 60000);
    CHECK(std::abs(soundingFrames(out) - 48000) <= 2);
    CHECK(about(out[2 * 24000] / full, 0.5f) && about(out[2 * 47000] / full, 0.25f + 0.5f * 47000.f / 48000.f));
    CHECK(out[2 * 100] == out[2 * 100 + 1]);
    // An octave up: twice as fast, half as long. An octave down: twice as long.
    quiet();
    out = play(72, 60000);
    CHECK(std::abs(soundingFrames(out) - 24000) <= 2 && about(out[2 * 12000] / full, 0.5f));
    quiet();
    out = play(48, 120000);
    CHECK(std::abs(soundingFrames(out) - 96000) <= 4 && about(out[2 * 48000] / full, 0.5f));
    // ROOT moves which key that is; TUNE bends it by cents (100 is a semitone).
    quiet();
    set(sampler::Root, 72);
    CHECK(std::abs(soundingFrames(play(72, 60000)) - 48000) <= 2);
    quiet();
    set(sampler::Root, 60);
    set(sampler::Tune, 100);
    CHECK(std::abs(soundingFrames(play(59, 60000)) - 48000) <= 60); // a semitone down, a semitone up: as made
    quiet();
    set(sampler::Tune, 0);
    // Velocity and LEVEL are plain gains.
    out = play(60, 4800, 0.5f);
    CHECK(about(out[2 * 2400] / full, 0.5f * (0.25f + 0.5f * 2400.f / 48000.f)));
    quiet();
    set(sampler::Level, 40);
    out = play(60, 4800);
    CHECK(about(out[2 * 2400] / full, 0.5f * (0.25f + 0.5f * 2400.f / 48000.f)));
    quiet();
    set(sampler::Level, 80);
    // START: a note begins part-way in.
    set(sampler::Start, 50);
    out = play(60, 60000);
    CHECK(std::abs(soundingFrames(out) - 24000) <= 2 && about(out[2 * 100] / full, 0.25f + 0.5f * 24100.f / 48000.f));
    quiet();
    // LOOP: it goes round from START for as long as the key is held.
    set(sampler::Loop, 1);
    out = play(60, 100000);
    CHECK(soundingFrames(out) == 100000);
    CHECK(about(out[2 * 30000] / full, 0.25f + 0.5f * (24000.f + 6001.f) / 48000.f)); // round once, 6001 in (a loop is one sample short of the half)
    engine->send(make(Command::NoteOff, 1, 60));
    CHECK(soundingFrames(renderFrames(*engine, 48000)) < 6000); // let go: it fades over the release and stops
    set(sampler::Loop, 0);
    set(sampler::Start, 0);
    quiet();
    // Letting go fades it over RELEASE; a longer one takes longer.
    const auto tail = [&](int release) {
        set(sampler::Release, release);
        play(60, 4800);
        engine->send(make(Command::NoteOff, 1, 60));
        const int frames = soundingFrames(renderFrames(*engine, 48000 * 5));
        quiet();
        return frames;
    };
    const int shortTail = tail(0), longTail = tail(60);
    CHECK(shortTail >= 200 && shortTail <= 260); // five milliseconds
    CHECK(longTail > 10 * shortTail && longTail < 43200);
    set(sampler::Release, 20);
    // ATTACK fades it in.
    set(sampler::Attack, 60);
    out = play(60, 9600);
    CHECK(std::fabs(out[2 * 10]) < 0.01f * full && std::fabs(out[2 * 2000]) < std::fabs(out[2 * 4000]) && std::fabs(out[2 * 9000] / full) < 0.4f);
    set(sampler::Attack, 0);
    quiet();
    // Eight notes at once, and a ninth takes the oldest's place.
    for (int n = 0; n < 8; ++n) engine->send(make(Command::NoteOn, 1, 60 + n, 0, 0.1f));
    out = renderFrames(*engine, 2400);
    CHECK(peakOf(out) > 6.f * 0.1f * 0.25f * full);
    engine->send(make(Command::NoteOn, 1, 80, 0, 0.1f));
    CHECK(peakOf(renderFrames(*engine, 2400)) < 9.f * 0.1f * full);
    for (int n = 0; n < 8; ++n) engine->send(make(Command::NoteOff, 1, 60 + n));
    engine->send(make(Command::NoteOff, 1, 80));
    quiet();
    // The same key again restarts its note, it does not stack another on it.
    play(60, 2400, 0.5f);
    out = play(60, 2400, 0.5f);
    CHECK(about(out[2 * 1200] / full, 0.5f * (0.25f + 0.5f * 1200.f / 48000.f)));
    quiet();
    // A pattern plays it, to the sample, like any synth. Nonsense settings do no harm.
    Command note = make(Command::SetNote, 1, 0, 4, 1.f);
    note.c = 60;
    note.d = 2;
    engine->send(note);
    engine->send(make(Command::Play, 0, 0, 1));
    out = renderFrames(*engine, 16 * kStep);
    CHECK(onsetOf(out) == 4 * kStep && soundingFrames(out) > 2 * kStep && soundingFrames(out) < 2 * kStep + 4000);
    engine->send(make(Command::Stop));
    for (int id = -1; id <= sampler::kSettings; ++id) { set(id, 99999); set(id, -99999); }
    play(60, 4800);
    for (int id = 0; id < sampler::kSettings; ++id) set(id, sampler::settingDefault(id));
    set(sampler::Attack, 0);
    quiet();
    // Between two samples it reads the value in between: an octave down, a
    // recording that flips 0, 1, 0, 1 passes through the halves.
    {
        std::unique_ptr<SampleData> flip(new SampleData);
        flip->data.resize(2000);
        for (int i = 0; i < 2000; ++i) flip->data[static_cast<std::size_t>(i)] = static_cast<float>(i % 2);
        CHECK(engine->setTrackSample(1, std::move(flip)));
        out = play(48, 1000);
        int halves = 0;
        for (int i = 100; i < 900; ++i) halves += about(out[2 * static_cast<std::size_t>(i)] / full, 0.5f);
        CHECK(halves > 350);
        quiet();
        // A longer recording put in while a note is sounding: the note stops; it does not carry on into the new one.
        play(60, 500);
        std::unique_ptr<SampleData> longer(new SampleData);
        longer->data.assign(48000, 0.5f);
        CHECK(engine->setTrackSample(1, std::move(longer)));
        CHECK(peakOf(renderFrames(*engine, 4800)) == 0.f);
        quiet();
        std::unique_ptr<SampleData> back(new SampleData);
        back->data.resize(48000);
        for (int i = 0; i < 48000; ++i) back->data[static_cast<std::size_t>(i)] = 0.25f + 0.5f * static_cast<float>(i) / 48000.f;
        CHECK(engine->setTrackSample(1, std::move(back)));
    }
    // A new recording replaces the old and stops what was reading it. None: silence.
    play(60, 2400);
    std::unique_ptr<SampleData> flat(new SampleData);
    flat->data.assign(1000, 0.5f);
    CHECK(engine->setTrackSample(1, std::move(flat)));
    CHECK(peakOf(renderFrames(*engine, 4800)) == 0.f);
    engine->send(make(Command::NoteOff, 1, 60));
    out = play(60, 4800);
    CHECK(std::abs(soundingFrames(out) - 999) <= 1 && about(out[2 * 500] / full, 0.5f));
    quiet();
    CHECK(engine->setTrackSample(1, nullptr));
    CHECK(peakOf(play(60, 4800)) == 0.f);
    engine->collectGarbage();
    // Only a synth track takes one. Removing the track takes its recording with it.
    CHECK(!engine->setTrackSample(0, nullptr) && !engine->setTrackSample(5, nullptr) && !engine->setTrackSample(12, nullptr));
    std::unique_ptr<SampleData> again(new SampleData);
    again->data.assign(1000, 0.5f);
    CHECK(engine->setTrackSample(1, std::move(again)));
    renderFrames(*engine, 64);
    CHECK(engine->removeTrack(1));
    renderFrames(*engine, 64);
    engine->collectGarbage();
    CHECK(engine->addSynthTrack(1, SynthKind::Sampler));
    CHECK(peakOf(play(60, 4800)) == 0.f);
    // One still waiting in the queue when the engine goes is freed with it.
    std::unique_ptr<SampleData> last(new SampleData);
    last->data.assign(10, 0.5f);
    CHECK(engine->setTrackSample(1, std::move(last)));
    CHECK(sampler::settingMin(99) == 0 && sampler::settingMax(-1) == 0 && sampler::settingDefault(sampler::Root) == 60);
}

// A note can sit early or late of its step, as a drum hit can.
static void testANoteNudgedOffItsStep() {
    // A sampler playing a steady recording: where a note starts and stops
    // shows to the sample.
    const auto bar = [](float nudge, int stepIndex = 4, int bars = 1) {
        std::unique_ptr<Engine> engine(new Engine);
        CHECK(engine->addSynthTrack(1, SynthKind::Sampler));
        std::unique_ptr<SampleData> flat(new SampleData);
        flat->data.assign(48000, 0.5f);
        CHECK(engine->setTrackSample(1, std::move(flat)));
        engine->send(make(Command::SetSetting, 1, sampler::Attack, 0));
        engine->send(make(Command::SetSetting, 1, sampler::Release, 0));
        Command c = make(Command::SetNote, 1, 0, stepIndex, 1.f);
        c.c = 60;
        c.d = 2;
        c.shift = nudge;
        engine->send(c);
        engine->send(make(Command::Play, 0, 0, 1));
        return renderFrames(*engine, bars * 16 * kStep);
    };
    const std::vector<float> onTime = bar(0.f);
    CHECK(onsetOf(onTime) == 4 * kStep);
    // Late and early by a quarter of a step; the length goes with it.
    std::vector<float> out = bar(0.25f);
    CHECK(std::abs(onsetOf(out) - (4 * kStep + kStep / 4)) <= 1);
    CHECK(std::abs(soundingFrames(out) - soundingFrames(onTime)) <= 2);
    out = bar(-0.25f);
    CHECK(std::abs(onsetOf(out) - (4 * kStep - kStep / 4)) <= 1);
    CHECK(std::abs(soundingFrames(out) - soundingFrames(onTime)) <= 2);
    // Half a step is the most, whatever is asked; nonsense is "on time".
    CHECK(std::abs(onsetOf(bar(3.f)) - (4 * kStep + kStep / 2)) <= 1);
    CHECK(std::abs(onsetOf(bar(-3.f)) - (4 * kStep - kStep / 2)) <= 1);
    CHECK(onsetOf(bar(std::nanf(""))) == 4 * kStep);
    // An early note on step 1 belongs to the end of the bar before: nothing
    // the first time round, then just before each bar line.
    out = bar(-0.25f, 0, 2);
    CHECK(std::abs(onsetOf(out) - (16 * kStep - kStep / 4)) <= 1);
    // A late note on step 16 runs on over the bar line and is not cut there.
    out = bar(0.25f, 15, 2);
    CHECK(std::abs(onsetOf(out) - (15 * kStep + kStep / 4)) <= 1);
    CHECK(out[2 * (16 * kStep + 100)] != 0.f);
    // A whole song loaded at once brings its nudges; Stop forgets a note that was waiting.
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(1, SynthKind::Sampler));
    std::unique_ptr<SampleData> flat(new SampleData);
    flat->data.assign(48000, 0.5f);
    CHECK(engine->setTrackSample(1, std::move(flat)));
    engine->send(make(Command::SetSetting, 1, sampler::Attack, 0));
    std::unique_ptr<TrackImage> image(new TrackImage);
    image->notes[0][0].step = 2;
    image->notes[0][0].pitch = 60;
    image->notes[0][0].length = 1.f;
    image->notes[0][0].velocity = 1.f;
    image->notes[0][0].nudge = 0.4f;
    CHECK(engine->setTrackImage(1, std::move(image)));
    engine->send(make(Command::Play, 0, 0, 1));
    out = renderFrames(*engine, 2 * kStep + 100); // the note is waiting: it is due 0.4 of a step on
    CHECK(peakOf(out) == 0.f);
    engine->send(make(Command::Stop));
    CHECK(peakOf(renderFrames(*engine, 4 * kStep)) == 0.f); // stopped: it never comes
    engine->send(make(Command::Play, 0, 0, 1));
    out = renderFrames(*engine, 8 * kStep);
    CHECK(std::abs(onsetOf(out) - (2 * kStep + 2 * kStep / 5)) <= 1);
}

// ---- cells longer than a bar ---------------------------------------------------------

static void testCellsOfSeveralBars() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.2f)));
    const float hit = 0.2f * kTrackLevel;
    const auto at = [&](int bar, int stepIndex, float velocity, int scene = 0) {
        Command c = step(0, stepIndex, velocity);
        c.scene = scene;
        c.bar = bar;
        engine->send(c);
    };
    const auto bars = [&](int count, int scene = 0, int track = 0) {
        Command c = make(Command::SetCellBars, track, count);
        c.scene = scene;
        engine->send(c);
    };
    // Where the drum sounds, as bar.step, over a stretch of playing.
    const auto heard = [&](int barsToPlay) {
        const std::vector<float> out = renderFrames(*engine, barsToPlay * 16 * kStep);
        std::vector<int> starts;
        for (int i = 0; i < barsToPlay * 16; ++i)
            if (out[2 * static_cast<std::size_t>(i * kStep)] != 0.f) starts.push_back(i);
        return starts;
    };
    const auto play = [&](int loop) { engine->send(make(Command::Play, 0, 0, loop)); };
    const auto stop = [&] { engine->send(make(Command::Stop)); renderFrames(*engine, 20000); };

    // One bar, as ever: step 1 every bar.
    at(0, 0, 1.f);
    play(1);
    CHECK(heard(3) == std::vector<int>({0, 16, 32}));
    stop();
    // Four bars: the first bar's hit comes once in four; a hit written into bar 3 plays in bar 3.
    bars(4);
    at(2, 4, 1.f);
    play(1);
    CHECK(heard(8) == std::vector<int>({0, 36, 64, 100}));
    CHECK(engine->playBar() == 3 && engine->playScene() == 0);
    stop();
    // A bar past the cell's length is kept but not played; shorten the cell and it goes round sooner.
    bars(2);
    play(1);
    CHECK(heard(4) == std::vector<int>({0, 32}));
    stop();
    bars(4);

    // A second track with a one-bar cell loops under it: four times to its once.
    CHECK(engine->addDrumTrack(1));
    CHECK(engine->setLaneSample(1, 0, twoHalves(500, 0.1f, 0.1f)));
    Command c = step(0, 8, 1.f, 1);
    engine->send(c);
    play(1);
    std::vector<float> out = renderFrames(*engine, 4 * 16 * kStep);
    for (int bar = 0; bar < 4; ++bar) CHECK(about(out[2 * static_cast<std::size_t>((bar * 16 + 8) * kStep)], 0.1f * kTrackLevel));
    CHECK(about(out[0], hit) && about(out[2 * static_cast<std::size_t>(36 * kStep)], hit) && out[2 * static_cast<std::size_t>(16 * kStep)] == 0.f);
    stop();
    // A two-bar cell under a four-bar one goes round twice.
    bars(2, 0, 1);
    c = step(0, 2, 1.f, 1);
    c.bar = 1;
    engine->send(c);
    play(1);
    out = renderFrames(*engine, 4 * 16 * kStep);
    CHECK(about(out[2 * static_cast<std::size_t>(18 * kStep)], 0.1f * kTrackLevel) && about(out[2 * static_cast<std::size_t>(50 * kStep)], 0.1f * kTrackLevel));
    CHECK(out[2 * static_cast<std::size_t>(2 * kStep)] == 0.f && out[2 * static_cast<std::size_t>(34 * kStep)] == 0.f);
    stop();
    CHECK(engine->removeTrack(1));
    renderFrames(*engine, 64);

    // In the song, the repeat count is whole passes of the scene: four bars
    // twice, then scene 2 (one bar), then round again.
    at(0, 15, 0.5f, 1); // scene 2: a hit on step 16 only
    engine->send(make(Command::SetSceneCount, 0, 2));
    Command repeats = make(Command::SetSceneRepeats, 0, 2);
    engine->send(repeats);
    play(0);
    CHECK(heard(10) == std::vector<int>({0, 36, 64, 100, 143, 144}));
    stop();
    // The read-outs follow: the bar of the scene, and which pass.
    play(0);
    renderFrames(*engine, (4 * 16 + 2 * 16 + 3) * kStep + 100);
    CHECK(engine->playScene() == 0 && engine->playRepeat() == 1 && engine->playBar() == 2 && engine->playStep() == 3);
    renderFrames(*engine, 2 * 16 * kStep);
    CHECK(engine->playScene() == 1 && engine->playRepeat() == 0 && engine->playBar() == 0);
    stop();
    // A hit nudged early on step 1 of bar 2 sounds at the end of bar 1.
    at(1, 0, 1.f);
    Command nudge = make(Command::SetStepNudge, 0, 0, 0, -0.25f);
    nudge.bar = 1;
    engine->send(nudge);
    play(1);
    out = renderFrames(*engine, 2 * 16 * kStep);
    CHECK(about(out[2 * static_cast<std::size_t>(16 * kStep - kStep / 4 + 10)], hit) && out[2 * static_cast<std::size_t>(16 * kStep - kStep / 4 - 10)] == 0.f);
    stop();
    at(1, 0, 0.f);

    // Notes: a synth cell of four bars with a note in its third.
    CHECK(engine->addSynthTrack(2, SynthKind::Sampler));
    std::unique_ptr<SampleData> flat(new SampleData);
    flat->data.assign(48000, 0.5f);
    CHECK(engine->setTrackSample(2, std::move(flat)));
    engine->send(make(Command::SetSetting, 2, sampler::Attack, 0));
    bars(1); // the drums back to one bar; the sampler's cell alone makes the scene four
    for (int b = 0; b < 4; ++b) at(b, 0, 0.f);
    at(2, 4, 0.f);
    bars(4, 0, 2);
    Command note = make(Command::SetNote, 2, 0, 0, 1.f);
    note.c = 60;
    note.d = 1;
    note.bar = 2;
    engine->send(note);
    play(1);
    out = renderFrames(*engine, 8 * 16 * kStep);
    CHECK(onsetOf(out) == 32 * kStep);
    CHECK(out[2 * static_cast<std::size_t>(96 * kStep + 10)] != 0.f && out[2 * static_cast<std::size_t>(64 * kStep + 10)] == 0.f);
    stop();

    // Lengths are held to 1..8; a bar that does not exist is not written; no such scene, nothing.
    bars(99, 0, 2);
    play(1);
    out = renderFrames(*engine, 11 * 16 * kStep);
    CHECK(out[2 * static_cast<std::size_t>((8 * 16 + 32) * kStep + 10)] != 0.f); // round after eight
    stop();
    bars(0, 0, 2);
    note.bar = 0;
    engine->send(note);
    play(1);
    CHECK(onsetOf(renderFrames(*engine, 16 * kStep)) == 0);
    stop();
    note.bar = 8;
    note.b = 4;
    engine->send(note);
    note.bar = -1;
    engine->send(note);
    Command far = make(Command::SetCellBars, 2, 4);
    far.scene = 16;
    engine->send(far);
    play(1);
    out = renderFrames(*engine, 16 * kStep);
    CHECK(onsetOf(out) == 0 && out[2 * static_cast<std::size_t>(4 * kStep + 10)] == 0.f);
    stop();

    // A whole song loaded at once brings its lengths and its later bars.
    std::unique_ptr<TrackImage> image(new TrackImage);
    image->bars[0] = 2;
    image->level[1][0][3] = 1.f; // scene 1, bar 2, lane 1, step 4
    CHECK(engine->setTrackImage(0, std::move(image)));
    CHECK(engine->removeTrack(2));
    engine->send(make(Command::SetSceneCount, 0, 1));
    play(1);
    CHECK(heard(4) == std::vector<int>({19, 51}));
    stop();
    // Taking the track away forgets its lengths.
    CHECK(engine->removeTrack(0));
    CHECK(engine->addDrumTrack(0));
    CHECK(engine->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.2f)));
    at(0, 0, 1.f);
    play(1);
    CHECK(heard(3) == std::vector<int>({0, 16, 32}));
}

int main() {
    testCellsOfSeveralBars();
    testANoteNudgedOffItsStep();
    testTheSampler();
    testEffectsOneByOne();
    testEffectsInTheEngine();
    testCatchingTheLastBar();
    testPanic();
    testANoteEndsPartWayThroughAStep();
    testARecordingOnALane();
    testWhereAHitStarts();
    testQueue();
    testBuiltInKit();
    testSilentUntilPlayed();
    testTriggerSoundsOnTheDrumTrack();
    testVelocityIsLinearGain();
    testLanesAreDifferentSounds();
    testRetriggerDoesNotClick();
    testKitHitsHard();
    testCeiling();
    testLaneLevel();
    testPatternPlaysOnTheExactSample();
    testNudgeMovesAHitOffTheGrid();
    testNudgedHitsKeepTheirOwnLevels();
    testChance();
    testPatternLoopsAfterSixteenSteps();
    testStepVelocityAndClearing();
    testStopAndRestart();
    testTempo();
    testTrackLevel();
    testTrackEffectSlotsRunInSeries();
    testMasterEffectSlots();
    testLowpassCutsHighs();
    testBlockSizeDoesNotChangeTheSound();
    testRateConverter();
    testBadCommandsAreIgnored();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("engine tests passed\n");
    return 0;
}
