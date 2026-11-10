// Engine tests. No framework: each CHECK prints its line on failure and the
// program exits non-zero if any failed.

#include <cmath>
#include <cstdio>
#include <memory>
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

int main() {
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
