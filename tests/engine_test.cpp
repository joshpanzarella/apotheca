// Engine tests. No framework: each CHECK prints its line on failure and the
// program exits non-zero if any failed.

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/envelope.hpp"
#include "apotheca/twin.hpp"
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

// Helpers are started and stopped many times over, at once and after some work.
// A helper that misses the word to stop sleeps for good, and stopping waits for
// it for good: this check does not fail then, it never comes back. (ctest gives
// it a time limit.)
static void testHelpersAlwaysStop() {
    std::unique_ptr<Engine> engine(new Engine);
    engine->addSynthTrack(1, SynthKind::Phazed);
    engine->addSynthTrack(2, SynthKind::Phazed);
    std::vector<float> out(2 * 64);
    for (int round = 0; round < 3000; ++round) {
        engine->useWorkers(1 + round % 3);
        if (round % 2) engine->render(out.data(), 64);
        engine->useWorkers(0);
    }
    CHECK(engine->workers() == 0);
}

// When the machine cannot keep up, synths are told to do plainer work.
// Tracks worked out on several processor cores at once: the same sound, to the last bit.
static void testSharingTracksAcrossCores() {
    // A busy song: drums and five synths of every kind, each with notes, an effect, and automation on one.
    const auto song = [](int helpers) {
        std::unique_ptr<Engine> engine(new Engine);
        engine->useWorkers(helpers);
        for (int s = 0; s < kSteps; s += 2) engine->send(step(s % 4 == 0 ? KICK : RIM, s, 0.9f));
        const SynthKind kinds[5] = {SynthKind::Phazed, SynthKind::Twin, SynthKind::Qmod, SynthKind::Twin, SynthKind::Qmod};
        for (int t = 1; t <= 5; ++t) {
            CHECK(engine->addSynthTrack(t, kinds[t - 1]));
            for (int n = 0; n < 4; ++n) {
                Command note = make(Command::SetNote, t, n, n * 4 + (t % 3), 0.7f);
                note.c = 40 + 5 * t + 3 * n;
                note.d = 3;
                engine->send(note);
            }
            if (t % 2) CHECK(engine->setEffect(t, 0, EffectKind::Delay));
        }
        Command sweep = make(Command::SetAutomation, 3, 1, 4, 0.9f);
        engine->send(sweep);
        engine->send(make(Command::Play, 0, 0, 1));
        std::vector<float> all;
        // In blocks of odd sizes, as a sound card might ask, for three bars.
        for (int done = 0, size = 37; done < 3 * 16 * kStep; done += size, size = size % 301 + 64) {
            const std::vector<float> got = renderFrames(*engine, size);
            all.insert(all.end(), got.begin(), got.end());
        }
        float peaks = 0.f;
        for (int t = 0; t <= 5; ++t) peaks += engine->trackPeak(t);
        all.push_back(peaks); // the meters agree too
        return all;
    };
    const std::vector<float> alone = song(0);
    float loudest = 0.f;
    for (float v : alone) loudest = std::fmax(loudest, std::fabs(v));
    CHECK(loudest > 0.1f && alone.size() > 100000);
    for (int helpers : {1, 2, 3, 7}) CHECK(song(helpers) == alone);
    CHECK(song(2) == song(2)); // and the same again each time
    // Helpers can be taken on and let go while the song plays, and asking for none, or too many, is safe.
    Engine engine;
    engine.send(step(KICK, 0, 1.f));
    CHECK(engine.addSynthTrack(1, SynthKind::Qmod));
    engine.send(make(Command::Play, 0, 0, 1));
    CHECK(engine.workers() == 0);
    for (int n : {3, 0, 1, -5, 99, 2}) {
        engine.useWorkers(n);
        CHECK(engine.workers() == (n < 0 ? 0 : (n > kNumTracks - 1 ? kNumTracks - 1 : n)));
        CHECK(peakOf(renderFrames(engine, 16 * kStep)) > 0.1f);
    }
    // One track, or none: nothing to share, and it still plays.
    Engine one;
    one.useWorkers(3);
    one.send(trigger(KICK));
    CHECK(peakOf(renderFrames(one, 2000)) > 0.1f);
}

// A scene whose bars are shorter than sixteen steps: the bar comes round early.
static void testOddStepCounts() {
    const auto onsets = [](Engine& engine, int steps) {
        std::vector<int> at;
        const std::vector<float> out = renderFrames(engine, steps * kStep);
        for (int s = 0; s < steps; ++s) {
            bool silentBefore = s == 0 || out[2 * (s * kStep - 1)] == 0.f;
            if (out[2 * (s * kStep)] != 0.f || out[2 * (s * kStep + 2)] != 0.f) { if (silentBefore || true) at.push_back(s); }
        }
        return at;
    };
    // A rim on step 1 only: with sixteen steps it comes every sixteen; with thirteen, every thirteen.
    {
        Engine engine;
        engine.send(step(RIM, 0, 1.f));
        engine.send(make(Command::Play, 0, 0, 1));
        const std::vector<float> out = renderFrames(engine, 40 * kStep);
        const auto starts = [&](int s) { return out[2 * (s * kStep + 1)] != 0.f && (s == 0 || out[2 * (s * kStep - 1)] == 0.f); };
        CHECK(starts(0) && starts(16) && starts(32) && !starts(13) && !starts(26));
    }
    {
        Engine engine;
        engine.send(step(RIM, 0, 1.f));
        engine.send(step(RIM, 14, 1.f)); // past the end of a thirteen-step bar: never played
        Command steps = make(Command::SetSceneSteps, 0, 13);
        engine.send(steps);
        engine.send(make(Command::Play, 0, 0, 1));
        const std::vector<float> out = renderFrames(engine, 40 * kStep);
        const auto starts = [&](int s) { return out[2 * (s * kStep + 1)] != 0.f && (s == 0 || out[2 * (s * kStep - 1)] == 0.f); };
        CHECK(starts(0) && starts(13) && starts(26) && starts(39) && !starts(14) && !starts(16) && !starts(32));
        CHECK(engine.playStep() >= 0 && engine.playStep() < 13);
    }
    {
        // The song counts bars by the scene's own length: scene 1 (five steps, twice) then scene 2 (sixteen).
        Engine engine;
        engine.send(step(RIM, 0, 1.f));
        Command second = step(RIM, 0, 1.f);
        second.scene = 1;
        engine.send(second);
        engine.send(make(Command::SetSceneCount, 0, 2));
        Command twice = make(Command::SetSceneRepeats, 0, 2);
        engine.send(twice);
        engine.send(make(Command::SetSceneSteps, 0, 5));
        engine.send(make(Command::Play));
        const std::vector<float> out = renderFrames(engine, 30 * kStep);
        const auto starts = [&](int s) { return out[2 * (s * kStep + 1)] != 0.f && (s == 0 || out[2 * (s * kStep - 1)] == 0.f); };
        CHECK(starts(0) && starts(5) && starts(10) && starts(26) && !starts(15) && !starts(16) && !starts(21));
        // Out of range is held to 1..16, and a scene that does not exist is nothing.
        Engine odd;
        odd.send(step(RIM, 0, 1.f));
        odd.send(make(Command::SetSceneSteps, 0, 0));
        Command stray = make(Command::SetSceneSteps, 0, 4);
        stray.scene = kMaxScenes;
        odd.send(stray);
        odd.send(make(Command::Play, 0, 0, 1));
        const std::vector<float> one = renderFrames(odd, 3 * kStep);
        CHECK(one[2 * (kStep + 1)] != 0.f && one[2 * (2 * kStep + 1)] != 0.f); // one step long: the rim on every step
        Engine big;
        big.send(step(RIM, 0, 1.f));
        big.send(make(Command::SetSceneSteps, 0, 99));
        big.send(make(Command::Play, 0, 0, 1));
        const std::vector<float> full = renderFrames(big, 17 * kStep);
        CHECK(full[2 * (16 * kStep + 1)] != 0.f && full[2 * (16 * kStep - 1)] == 0.f);
    }
    (void)onsets;
}

// Automation: a macro or an effect's setting moved by the pattern.
static void testAutomation() {
    // A QMOD track holding one plain note right through the bar; its OCTAVE macro (lane 7) is what gets automated,
    // because an octave up is easy to hear and to count: twice as many waves.
    const auto rig = [] {
        std::unique_ptr<Engine> engine(new Engine);
        CHECK(engine->addSynthTrack(1, SynthKind::Qmod));
        engine->send(make(Command::SetSetting, 1, qmod::Tension, 0));
        engine->send(make(Command::SetSetting, 1, qmod::Sustain, 100));
        engine->send(make(Command::SetSetting, 1, qmod::Attack, 0));
        Command note = make(Command::SetNote, 1, 0, 0, 0.8f);
        note.c = 45;
        note.d = 16;
        engine->send(note);
        return engine;
    };
    const auto automate = [](Engine& engine, int lane, int index, float value, int scene = 0) {
        Command c = make(Command::SetAutomation, 1, lane, index, value);
        c.scene = scene;
        engine.send(c);
    };
    // How many times the wave goes up through zero in each step of what is rendered next.
    const auto waves = [](Engine& engine, int steps) {
        std::vector<int> out;
        for (int s = 0; s < steps; ++s) {
            const std::vector<float> got = renderFrames(engine, kStep);
            int n = 0;
            for (int i = 1; i < kStep; ++i) n += got[2 * (i - 1)] < 0.f && got[2 * i] >= 0.f ? 1 : 0;
            out.push_back(n);
        }
        return out;
    };
    const int lane = qmod::MacroOctave;
    {
        // Nothing written: the macro stays where its knob is, all bar long.
        std::unique_ptr<Engine> engine = rig();
        engine->send(make(Command::Play, 0, 0, 1));
        const std::vector<int> plain = waves(*engine, 16);
        for (int s = 1; s < 16; ++s) CHECK(std::abs(plain[s] - plain[1]) <= 1);
        const int low = plain[4];
        CHECK(low >= 12 && low <= 15); // A2 is 110 Hz: 13.75 waves a step

        // Steps 5 to 8 written "up an octave": twice the waves there, and back down after, with no key touched.
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000);
        for (int i = 4; i < 8; ++i) automate(*engine, lane, i, 1.f);
        engine->send(make(Command::Play, 0, 0, 1));
        std::vector<int> got = waves(*engine, 32);
        for (int bar = 0; bar < 2; ++bar) { // and the same the second time round
            CHECK(std::abs(got[bar * 16 + 2] - low) <= 1 && std::abs(got[bar * 16 + 3] - low) <= 1);
            CHECK(std::abs(got[bar * 16 + 5] - 2 * low) <= 2 && std::abs(got[bar * 16 + 7] - 2 * low) <= 2);
            CHECK(std::abs(got[bar * 16 + 9] - low) <= 1 && std::abs(got[bar * 16 + 15] - low) <= 1);
        }
        // It glides there over the first half of the step: the step it arrives in is between the two.
        CHECK(got[4] > low + 2 && got[4] < 2 * low - 1);
        // Stopped: the macro is back where its knob rests, at once. (Stopped mid-way through the high steps.)
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000);
        engine->send(make(Command::Play, 0, 0, 1));
        waves(*engine, 6);
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000);
        engine->send(make(Command::NoteOn, 1, 45, 0, 0.8f));
        const std::vector<int> held = waves(*engine, 2);
        CHECK(std::abs(held[1] - low) <= 1);
        engine->send(make(Command::NoteOff, 1, 45));
        renderFrames(*engine, 48000);
        // The knob moved (a plain SetMacro): the steps with nothing written follow the knob; the written ones stay written.
        engine->send(make(Command::SetMacro, 1, lane, 0, 0.f)); // OCTAVE down
        engine->send(make(Command::Play, 0, 0, 1));
        got = waves(*engine, 16);
        CHECK(got[2] < low - 4 && std::abs(got[6] - 2 * low) <= 2 && got[12] < low - 4);
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000);
        engine->send(make(Command::SetMacro, 1, lane, 0, 0.5f));
        // One step unwritten (a value below zero): that step rests again.
        automate(*engine, lane, 6, -1.f);
        automate(*engine, lane, 7, -1.f);
        engine->send(make(Command::Play, 0, 0, 1));
        got = waves(*engine, 16);
        CHECK(std::abs(got[5] - 2 * low) <= 2 && std::abs(got[7] - low) <= 1);
        // The whole lane cleared: plain again. Clearing "every lane" does the same.
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000);
        Command clear = make(Command::ClearAutomation, 1, lane);
        engine->send(clear);
        engine->send(make(Command::Play, 0, 0, 1));
        got = waves(*engine, 16);
        for (int s2 = 1; s2 < 16; ++s2) CHECK(std::abs(got[s2] - low) <= 1);
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000);
        automate(*engine, lane, 2, 1.f);
        automate(*engine, 3, 2, 1.f);
        engine->send(make(Command::ClearAutomation, 1, -1));
        engine->send(make(Command::Play, 0, 0, 1));
        got = waves(*engine, 8);
        CHECK(std::abs(got[2] - low) <= 1 && std::abs(got[3] - low) <= 1);
        // Nonsense is ignored: no such lane, step, scene or track; not a number.
        automate(*engine, kAutoLanes, 0, 1.f);
        automate(*engine, -1, 0, 1.f);
        automate(*engine, lane, kAutoSteps, 1.f);
        automate(*engine, lane, -1, 1.f);
        automate(*engine, lane, 0, 1.f, kMaxScenes);
        automate(*engine, lane, 1, std::nanf(""));
        Command stray = make(Command::SetAutomation, kNumTracks, lane, 0, 1.f);
        engine->send(stray);
        got = waves(*engine, 16);
        CHECK(std::abs(got[9] - low) <= 1 && std::abs(got[10] - low) <= 1);
    }
    {
        // A cell of two bars: the second bar's steps are 17 to 32 of the lane.
        std::unique_ptr<Engine> engine = rig();
        Command bars = make(Command::SetCellBars, 1, 2);
        engine->send(bars);
        Command note = make(Command::SetNote, 1, 0, 0, 0.8f);
        note.c = 45;
        note.d = 16;
        note.bar = 1;
        engine->send(note);
        for (int i = 16 + 8; i < 16 + 12; ++i) automate(*engine, lane, i, 1.f);
        engine->send(make(Command::Play, 0, 0, 1));
        const std::vector<int> got = waves(*engine, 32);
        const int low = got[2];
        CHECK(std::abs(got[9] - low) <= 1 && std::abs(got[16 + 9] - 2 * low) <= 2 && std::abs(got[16 + 14] - low) <= 1);
    }
    {
        // A whole track sent at once (a song opened) brings its automation; one sent without any clears what was there.
        std::unique_ptr<Engine> engine = rig();
        for (int i = 0; i < 4; ++i) automate(*engine, lane, i, 1.f);
        std::unique_ptr<TrackImage> image(new TrackImage);
        image->notes[0][0].step = 0;
        image->notes[0][0].pitch = 45;
        image->notes[0][0].length = 16.f;
        image->notes[0][0].velocity = 0.8f;
        image->automation.reset(new float[kMaxScenes * kAutoLanes * kAutoSteps]);
        for (int i = 0; i < kMaxScenes * kAutoLanes * kAutoSteps; ++i) image->automation[i] = kAutoNone;
        for (int i = 8; i < 12; ++i) image->automation[(0 * kAutoLanes + lane) * kAutoSteps + i] = 1.f;
        CHECK(engine->setTrackImage(1, std::move(image)));
        engine->send(make(Command::Play, 0, 0, 1));
        std::vector<int> got = waves(*engine, 16);
        const int low = got[5];
        CHECK(std::abs(got[1] - low) <= 2 && std::abs(got[9] - 2 * low) <= 2 && std::abs(got[14] - low) <= 1);
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000);
        std::unique_ptr<TrackImage> bare(new TrackImage);
        bare->notes[0][0].step = 0;
        bare->notes[0][0].pitch = 45;
        bare->notes[0][0].length = 16.f;
        bare->notes[0][0].velocity = 0.8f;
        CHECK(engine->setTrackImage(1, std::move(bare)));
        engine->send(make(Command::Play, 0, 0, 1));
        got = waves(*engine, 16);
        CHECK(std::abs(got[9] - low) <= 1);
    }
    {
        // An effect's setting: a filter shut right down, its MIX automated. Where MIX is written full the
        // sound is the filtered one (far quieter); elsewhere it rests where the knob is (none of it).
        std::unique_ptr<Engine> engine = rig();
        CHECK(engine->setEffect(1, 0, EffectKind::Filter));
        engine->send(effectParam(1, 0, 0, 0.f)); // cutoff: 30 Hz
        engine->send(effectParam(1, 0, 3, 0.f)); // mix: none
        const int mix = autoLaneOfEffect(0, 3);
        for (int i = 8; i < 12; ++i) automate(*engine, mix, i, 1.f);
        engine->send(make(Command::Play, 0, 0, 1));
        std::vector<float> loud;
        for (int s2 = 0; s2 < 16; ++s2) loud.push_back(peakOf(renderFrames(*engine, kStep)));
        CHECK(loud[4] > 0.05f && loud[10] < 0.4f * loud[4] && loud[14] > 0.8f * loud[4]);
        // Another effect put in the slot: its settings rest at their own defaults, and the old lane does not drive it wrongly when stopped.
        engine->send(make(Command::Stop));
        renderFrames(*engine, 48000);
        CHECK(engine->setEffect(1, 0, EffectKind::Drive));
        renderFrames(*engine, 4800);
        engine->send(make(Command::NoteOn, 1, 45, 0, 0.8f));
        CHECK(peakOf(renderFrames(*engine, 9600)) > 0.02f);
    }
}

// The envelope every synth shares (envelope.hpp).
static void testTheSharedEnvelope() {
    using namespace envelope;
    // The times: attack 1 ms to 8 s; decay and release 2 ms to 20 s; settings out of range are held.
    CHECK(std::fabs(attackSeconds(0.f) - 0.001f) < 1e-6f && std::fabs(attackSeconds(100.f) - 8.f) < 1e-3f);
    CHECK(std::fabs(fallSeconds(0.f) - 0.002f) < 1e-6f && std::fabs(fallSeconds(100.f) - 20.f) < 1e-2f);
    CHECK(attackSeconds(-5.f) == attackSeconds(0.f) && fallSeconds(500.f) == fallSeconds(100.f));
    CHECK(attackSeconds(50.f) > 0.08f && attackSeconds(50.f) < 0.1f && fallSeconds(50.f) > 0.19f && fallSeconds(50.f) < 0.21f);

    const float sr = 48000.f;
    Shape shape;
    shape.set(0.1f, 0.5f, 0.4f, 1.f, sr);
    Envelope e;
    CHECK(e.idle() && e.next(shape) == 0.f); // nothing until a note
    e.on();
    // The attack arrives at the top in its time, to the sample or nearly, and never goes over.
    int arrived = -1;
    float halfAt = -1.f, most = 0.f, biggestStep = 0.f, last = 0.f;
    for (int i = 0; i < 6000; ++i) {
        const float v = e.next(shape);
        biggestStep = std::fmax(biggestStep, v - last);
        last = v;
        most = std::fmax(most, v);
        if (halfAt < 0.f && v >= 0.5f) halfAt = static_cast<float>(i);
        if (arrived < 0 && v >= 1.f) arrived = i;
    }
    CHECK(std::abs(arrived - 4800) <= 2 && most == 1.f);
    // Snappy: half-way up in well under half the time (a straight ramp takes exactly half).
    CHECK(halfAt > 0.3f * 4800.f && halfAt < 0.42f * 4800.f);
    // Never a jump: the largest step is the very first one, and it is small.
    CHECK(biggestStep < 1.5f * 1.1f / 4800.f * 1.01f);
    // Decay heads for the sustain level: 60 dB of the way there in its time.
    for (int i = 0; i < 24000 - (6000 - 4800); ++i) e.next(shape);
    CHECK(std::fabs(e.level - 0.4f) < 0.6f * 0.0012f && e.level > 0.4f);
    for (int i = 0; i < 48000; ++i) e.next(shape);
    CHECK(std::fabs(e.level - 0.4f) < 1e-4f && e.stage == Envelope::Decay); // held there as long as the key is
    // Release: down 60 dB in its time, silent and finished a little after.
    e.off();
    for (int i = 0; i < 48000; ++i) e.next(shape);
    CHECK(e.level < 0.4f * 0.00105f && e.level > 0.4f * 0.00095f && !e.idle());
    for (int i = 0; i < 16000; ++i) e.next(shape);
    CHECK(e.idle() && e.level == 0.f);
    e.off(); // letting go of nothing is nothing
    CHECK(e.idle());
    // A note played again part-way down starts from where the envelope is: no jump back to nothing.
    e.on();
    for (int i = 0; i < 4800; ++i) e.next(shape);
    e.off();
    for (int i = 0; i < 12000; ++i) e.next(shape);
    const float before = e.level;
    CHECK(before > 0.1f && before < 0.3f);
    e.on();
    const float after = e.next(shape);
    CHECK(after > before && after - before < 0.001f);
    // The shortest attack is still a ramp of many samples, not a step.
    shape.setFrom(0.f, 0.f, 100.f, 0.f, sr);
    Envelope quick;
    quick.on();
    float first = quick.next(shape);
    int n = 1;
    while (quick.stage == Envelope::Attack && n < 1000) quick.next(shape), ++n;
    CHECK(first < 0.04f && n >= 46 && n <= 50); // 1 ms
    // The longest release, from the usual settings: twenty seconds to fall 60 dB.
    shape.setFrom(0.f, 0.f, 100.f, 100.f, sr);
    quick.off();
    for (int i = 0; i < 480000; ++i) quick.next(shape); // ten seconds: half-way, 30 dB down
    CHECK(quick.level > 0.028f && quick.level < 0.036f);

    // Twin Analog and the sampler both use it. A Twin note let go at RELEASE 100 is still
    // clearly there six seconds later (it used to be gone, to the ear, by then), and one at RELEASE 0 has gone in a blink.
    const auto twinTail = [](int release, int afterFrames) {
        std::unique_ptr<Engine> engine(new Engine);
        CHECK(engine->addSynthTrack(1, SynthKind::Twin));
        engine->send(make(Command::SetSetting, 1, twin::Release, release));
        engine->send(make(Command::SetSetting, 1, twin::Sustain, 100));
        engine->send(make(Command::NoteOn, 1, 57, 0, 0.8f));
        const float held = peakOf(renderFrames(*engine, 9600));
        engine->send(make(Command::NoteOff, 1, 57));
        renderFrames(*engine, afterFrames);
        return peakOf(renderFrames(*engine, 2400)) / held;
    };
    const float long6 = twinTail(100, 6 * 48000);
    CHECK(long6 > 0.08f && long6 < 0.2f); // 18 dB down after 6 of its 20 seconds
    CHECK(twinTail(0, 480) < 0.1f); // the note itself has gone; what is left is the output filter settling
    // And its attack at 0 is up within a millisecond or so, without a step.
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(1, SynthKind::Twin));
    engine->send(make(Command::SetSetting, 1, twin::Attack, 0));
    engine->send(make(Command::NoteOn, 1, 57, 0, 0.8f));
    const std::vector<float> start = renderFrames(*engine, 480);
    CHECK(std::fabs(start[0]) < 0.02f && peakOf(start) > 0.05f);
}

static void testFallingBehindLowersTheQuality() {
    const int block = 256;
    {
        Engine engine;
        CHECK(engine.qualityLimit() == 3);
        // One slow block is a hiccup, not a habit.
        engine.noteLoad(0.95f, block);
        engine.noteLoad(0.5f, block);
        engine.noteLoad(0.95f, block);
        CHECK(engine.qualityLimit() == 3 && std::fabs(engine.load() - 0.95f) < 1e-6f);
        // Two in a row: down a notch. And again, to the bottom, and no further.
        engine.noteLoad(0.95f, block);
        CHECK(engine.qualityLimit() == 2);
        engine.noteLoad(0.86f, block);
        CHECK(engine.qualityLimit() == 2); // the count began again
        engine.noteLoad(0.86f, block);
        CHECK(engine.qualityLimit() == 1);
        for (int i = 0; i < 10; ++i) engine.noteLoad(2.f, block);
        CHECK(engine.qualityLimit() == 0);
        // Just under the line is keeping up.
        Engine fine;
        for (int i = 0; i < 100; ++i) fine.noteLoad(0.85f, block);
        CHECK(fine.qualityLimit() == 3);
        // Back up a notch only after ten seconds with plenty to spare.
        const int tenSeconds = 480000 / block;
        for (int i = 0; i < tenSeconds - 1; ++i) engine.noteLoad(0.2f, block);
        CHECK(engine.qualityLimit() == 0);
        engine.noteLoad(0.2f, block);
        CHECK(engine.qualityLimit() == 1);
        // "Getting by" (between 35% and 85%) is not "plenty": it stays where it is, and the ten seconds start again.
        for (int i = 0; i < tenSeconds - 1; ++i) engine.noteLoad(0.2f, block);
        engine.noteLoad(0.4f, block);
        for (int i = 0; i < tenSeconds - 1; ++i) engine.noteLoad(0.2f, block);
        CHECK(engine.qualityLimit() == 1);
        engine.noteLoad(0.2f, block);
        CHECK(engine.qualityLimit() == 2);
        for (int i = 0; i < 3 * tenSeconds; ++i) engine.noteLoad(0.f, block);
        CHECK(engine.qualityLimit() == 3); // and never past the top
        engine.noteLoad(std::nanf(""), block); // nonsense is ignored
        engine.noteLoad(-1.f, block);
        CHECK(engine.qualityLimit() == 3);
    }
    // Twin Analog at x8, held to x1, sounds as it does set to x1; a synth added
    // while the limit is on joins under it; let back up, it is x8 again.
    const auto twin = [](int oversample, int slowBlocks, bool addAfter) {
        std::unique_ptr<Engine> engine(new Engine);
        if (!addAfter) CHECK(engine->addSynthTrack(1, SynthKind::Twin));
        renderFrames(*engine, 64);
        for (int i = 0; i < slowBlocks; ++i) engine->noteLoad(1.5f, 256);
        if (addAfter) CHECK(engine->addSynthTrack(1, SynthKind::Twin));
        engine->send(make(Command::SetSetting, 1, twin::Oversample, oversample));
        Command note = make(Command::NoteOn, 1, 57, 0, 0.8f);
        engine->send(note);
        return renderFrames(*engine, 4000);
    };
    const std::vector<float> plain = twin(0, 0, false), best = twin(3, 0, false);
    CHECK(plain != best);
    CHECK(twin(3, 6, false) == plain); // three notches down from x8 is x1
    CHECK(twin(3, 6, true) == plain);
    CHECK(twin(3, 2, false) != plain && twin(3, 2, false) != best); // one notch: x4, neither
    CHECK(twin(0, 6, false) == plain); // a limit never raises what the player set
    CHECK(twin(1, 2, false) == twin(1, 0, false)); // x2 is under a limit of x4: untouched
    // With the watch off (as in every test and offline render), render() never times itself.
    Engine unwatched;
    for (int i = 0; i < 50; ++i) renderFrames(unwatched, 16);
    CHECK(unwatched.qualityLimit() == 3 && unwatched.load() == 0.f);
    // With it on it does: something is measured.
    Engine watched;
    watched.watchLoad(true);
    renderFrames(watched, 256);
    CHECK(watched.load() > 0.f);
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
    // An early hit on step 1 belongs to the end of the bar before. On the very
    // first bar there is no "before", so it plays at the top instead of
    // being missed (the owner put a recording on step 1, a touch early, and
    // heard nothing until the bar came round).
    wrapped.send(step(RIM, 0, 1.f));
    wrapped.send(make(Command::SetStepNudge, 0, RIM, 0, -0.25f));
    for (Engine* e : {&late, &early, &wrapped}) e->send(make(Command::Play));

    const int a = onsetOf(renderFrames(late, 16 * kStep));
    CHECK(a >= 4 * kStep + quarter && a <= 4 * kStep + quarter + 2);
    const int b = onsetOf(renderFrames(early, 16 * kStep));
    CHECK(b >= 4 * kStep - quarter && b <= 4 * kStep - quarter + 2);
    const std::vector<float> round = renderFrames(wrapped, 17 * kStep); // a bar and a step, to see over the bar line
    CHECK(onsetOf(round) >= 0 && onsetOf(round) <= 2);
    // ...and from then on just before each bar line, once: louder there than a moment before it.
    const auto loudest = [&](int from, int to) { float p = 0.f; for (int i = 2 * from; i < 2 * to; ++i) p = std::fmax(p, std::fabs(round[i])); return p; };
    CHECK(loudest(16 * kStep - quarter, 16 * kStep - quarter + 200) > 10.f * loudest(16 * kStep - quarter - 2000, 16 * kStep - quarter - 2) + 0.01f);
    {
        // The same sound as one hit played by itself, sample for sample, on
        // past the bar line: nothing starts it again there.
        Engine lone;
        lone.send(trigger(RIM));
        const std::vector<float> one = renderFrames(lone, 4000);
        int at = 8 * kStep;
        while (at < 17 * kStep && round[2 * at] == 0.f) ++at;
        CHECK(at >= 16 * kStep - quarter && at <= 16 * kStep - quarter + 2);
        bool same = true;
        for (int i = 0; i < 4000 && same; ++i) same = round[2 * (at + i)] == one[2 * (onsetOf(one) + i)];
        CHECK(same);
    }
    // It is not played twice at the top: the same as a hit that is simply on step 1.
    {
        Engine plain, nudged;
        plain.send(step(RIM, 0, 1.f));
        nudged.send(step(RIM, 0, 1.f));
        nudged.send(make(Command::SetStepNudge, 0, RIM, 0, -0.02f));
        plain.send(make(Command::Play));
        nudged.send(make(Command::Play));
        CHECK(renderFrames(plain, 8 * kStep) == renderFrames(nudged, 8 * kStep));
        // Only on the first step after Play: a hit on another step that is early is not moved.
        Engine other;
        other.send(step(RIM, 1, 1.f));
        other.send(make(Command::SetStepNudge, 0, RIM, 1, -0.25f));
        other.send(make(Command::Play));
        const int at = onsetOf(renderFrames(other, 4 * kStep));
        CHECK(at >= kStep - quarter && at <= kStep - quarter + 2);
    }

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

// ---- slices: a recording cut into pieces, a piece to a hit ------------------------------

// A recording one bar long whose every sixteenth is a different, steady value: slice n (from 0) is (n + 1) / 20.
static std::unique_ptr<SampleData> sixteenSlices() {
    std::unique_ptr<SampleData> s(new SampleData);
    s->data.resize(static_cast<std::size_t>(16 * kStep));
    for (int i = 0; i < 16 * kStep; ++i) s->data[static_cast<std::size_t>(i)] = static_cast<float>(i / kStep + 1) / 20.f;
    return s;
}
static float sliceValue(int slice) { return static_cast<float>(slice + 1) / 20.f * kTrackLevel; }
static Command part(int lane, float value) { return make(Command::SetLanePart, 0, lane, 0, value); }
static Command startOf(int stepIndex, float value) { return make(Command::SetStepStart, 0, 0, stepIndex, value); }

static void testAHitPlaysOnlyItsSlice() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, sixteenSlices()));
    // As it was: a hit part-way in plays on to the end of the sound.
    engine->send(make(Command::Trigger, 0, 0, 5000, 1.f));
    CHECK(soundingFrames(renderFrames(*engine, 17 * kStep)) >= 8 * kStep - 2);
    // The lane set to a sixteenth: a hit plays one slice, and fades in a millisecond.
    engine->send(part(0, 1.f / 16.f));
    engine->send(make(Command::Trigger, 0, 0, 5000, 1.f));
    std::vector<float> out = renderFrames(*engine, 17 * kStep);
    CHECK(soundingFrames(out) >= kStep && soundingFrames(out) <= kStep + 50);
    CHECK(about(out[2 * (kStep / 2)], sliceValue(8)));
    CHECK(about(out[2 * (kStep - 10)], sliceValue(8))); // at full level to the very end of the slice
    CHECK(out[2 * (kStep + 60)] == 0.f);
    // From the top likewise.
    engine->send(make(Command::Trigger, 0, 0, 0, 1.f));
    out = renderFrames(*engine, 17 * kStep);
    CHECK(soundingFrames(out) >= kStep && soundingFrames(out) <= kStep + 50 && about(out[0], sliceValue(0)));
    // One hit can ask for its own length whatever the lane says (hearing a slice): an eighth, here.
    Command hear = make(Command::Trigger, 0, 0, 2500, 1.f);
    hear.c = 1250;
    engine->send(hear);
    out = renderFrames(*engine, 17 * kStep);
    CHECK(soundingFrames(out) >= 2 * kStep && soundingFrames(out) <= 2 * kStep + 50);
    // Nonsense is "all of it". Another lane is not touched.
    for (float bad : {0.f, -1.f, 1.f, 7.f, std::nanf("")}) {
        engine->send(part(0, bad));
        engine->send(make(Command::Trigger, 0, 0, 5000, 1.f));
        CHECK(soundingFrames(renderFrames(*engine, 17 * kStep)) >= 8 * kStep - 2);
    }
    engine->send(part(1, 1.f / 16.f));
    engine->send(part(-1, 0.5f));
    engine->send(part(kDrumLanes, 0.5f));
    engine->send(make(Command::Trigger, 0, 0, 5000, 1.f));
    CHECK(soundingFrames(renderFrames(*engine, 17 * kStep)) >= 8 * kStep - 2);

    // A whole song loaded at once brings each lane's part.
    std::unique_ptr<TrackImage> image(new TrackImage);
    image->lanePart[0] = 1.f / 16.f;
    CHECK(engine->setTrackImage(0, std::move(image)));
    engine->send(make(Command::Trigger, 0, 0, 5000, 1.f));
    out = renderFrames(*engine, 17 * kStep);
    CHECK(soundingFrames(out) >= kStep && soundingFrames(out) <= kStep + 50);
}

// The break laid out a slice to a step, in order, is the break: no seam where one slice hands over to the next.
static void testSlicesInOrderHaveNoSeams() {
    const auto bar = [](float lanePart, bool everyStep, int swapStep = -1, int swapSlice = 0, int gapStep = -1, float level9 = 1.f, bool steady = false) {
        std::unique_ptr<Engine> engine(new Engine);
        CHECK(engine->setLaneSample(0, 0, steady ? twoHalves(16 * kStep, 0.5f, 0.5f) : sixteenSlices()));
        engine->send(part(0, lanePart));
        for (int s = 0; s < (everyStep ? kSteps : 1); ++s) {
            if (s == gapStep) continue;
            engine->send(step(0, s, s == 9 ? level9 : 1.f));
            engine->send(startOf(s, static_cast<float>(s == swapStep ? swapSlice : s) / 16.f));
        }
        engine->send(make(Command::Play));
        return renderFrames(*engine, 16 * kStep);
    };
    const std::vector<float> whole = bar(0.f, false); // one hit, the whole recording
    CHECK(about(whole[2 * (5 * kStep + 100)], sliceValue(5)));
    // Sixteen hits in order, each playing on: the same, to the sample.
    CHECK(bar(0.f, true) == whole);
    // ...and each playing only its own slice: still the same.
    CHECK(bar(1.f / 16.f, true) == whole);

    // A slice out of order is a cut: step 5 plays slice 12, and step 6 goes back to its own.
    const std::vector<float> swapped = bar(1.f / 16.f, true, 5, 12);
    CHECK(about(swapped[2 * (5 * kStep + 200)], sliceValue(12)));
    CHECK(about(swapped[2 * (6 * kStep + 200)], sliceValue(6)));
    CHECK(about(swapped[2 * (4 * kStep + 200)], sliceValue(4)));
    // A step with no hit: silence when hits play one slice, the recording running on when they do not.
    const std::vector<float> gapped = bar(1.f / 16.f, true, -1, 0, 7);
    CHECK(gapped[2 * (7 * kStep + 200)] == 0.f && about(gapped[2 * (8 * kStep + 200)], sliceValue(8)));
    CHECK(about(gapped[2 * (7 * kStep - 5)], sliceValue(6))); // slice 7 (from 0: 6) plays right up to its end
    const std::vector<float> ranOn = bar(0.f, true, -1, 0, 7);
    CHECK(about(ranOn[2 * (7 * kStep + 200)], sliceValue(7)));

    // A quieter hit in the run: the level moves there over a millisecond, with no jump and no dip.
    // (A recording that is one steady value, so any step in the output is the level's doing.)
    const std::vector<float> soft = bar(1.f / 16.f, true, -1, 0, -1, 0.5f, true);
    const float full = 0.5f * kTrackLevel;
    CHECK(about(soft[2 * (9 * kStep - 5)], full) && about(soft[2 * (9 * kStep + 200)], 0.5f * full) && about(soft[2 * (10 * kStep + 200)], full));
    float lowest = 1.f, jump = 0.f;
    for (int i = 8 * kStep + 100; i < 11 * kStep; ++i) {
        lowest = soft[2 * i] < lowest ? soft[2 * i] : lowest;
        jump = std::fabs(soft[2 * i] - soft[2 * (i - 1)]) > jump ? std::fabs(soft[2 * i] - soft[2 * (i - 1)]) : jump;
    }
    CHECK(lowest > 0.49f * full); // never below where it was going
    CHECK(jump < 0.01f);          // and never a step: at the slice's line it would be a click

    // A next hit that comes a few samples late finds its slice just beginning to fade: it comes back up,
    // and carries on from where the sound is (not from the slice's start again).
    {
        std::unique_ptr<Engine> late(new Engine);
        std::unique_ptr<SampleData> ramp(new SampleData);
        ramp->data.resize(static_cast<std::size_t>(16 * kStep));
        for (int i = 0; i < 16 * kStep; ++i) ramp->data[static_cast<std::size_t>(i)] = 0.25f + 0.5f * static_cast<float>(i) / (16 * kStep);
        CHECK(late->setLaneSample(0, 0, std::move(ramp)));
        late->send(part(0, 1.f / 16.f));
        late->send(make(Command::Trigger, 0, 0, 1875, 1.f)); // slice 3
        renderFrames(*late, kStep + 12);                      // twelve samples into its fade
        late->send(make(Command::Trigger, 0, 0, 2500, 1.f)); // slice 4, twelve samples late
        const std::vector<float> on = renderFrames(*late, 400);
        const auto rampAt = [](int i) { return (0.25f + 0.5f * static_cast<float>(i) / (16 * kStep)) * kTrackLevel; };
        CHECK(on[0] > 0.6f * rampAt(4 * kStep + 12) && on[0] < 0.9f * rampAt(4 * kStep + 12)); // part-way down, not gone
        for (int i = 1; i < 100; ++i) CHECK(on[2 * i] >= on[2 * (i - 1)]);                       // and only up from there
        CHECK(about(on[2 * 100], rampAt(4 * kStep + 12 + 100)));                                 // the sound where it had got to
    }

    // A hit from the very top always starts the sound again, even if the sound is at the top (two taps are two hits).
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.4f)));
    engine->send(make(Command::Trigger, 0, 0, 0, 1.f));
    renderFrames(*engine, 20);
    engine->send(make(Command::Trigger, 0, 0, 0, 1.f));
    const std::vector<float> twice = renderFrames(*engine, 4000);
    CHECK(twice[2 * 10] > 0.2f * kTrackLevel * 1.5f); // the first is still fading under the second
}

// ---- step effects: what one hit does besides play ----------------------------------------

// A click: one sample, then silence. Where it comes out says exactly when a hit was played.
static std::unique_ptr<SampleData> click() {
    std::unique_ptr<SampleData> s(new SampleData);
    s->data.assign(1000, 0.f);
    s->data[0] = 0.8f;
    return s;
}
// A slope from 0.25 up to 0.75 over one bar: its value says where in the recording the sound is.
static std::unique_ptr<SampleData> slope() {
    std::unique_ptr<SampleData> s(new SampleData);
    s->data.resize(static_cast<std::size_t>(16 * kStep));
    for (int i = 0; i < 16 * kStep; ++i) s->data[static_cast<std::size_t>(i)] = 0.25f + 0.5f * static_cast<float>(i) / (16 * kStep);
    return s;
}
static float slopeAt(double i) { return (0.25f + 0.5f * static_cast<float>(i / (16 * kStep))) * kTrackLevel; }
static std::vector<int> clicksIn(const std::vector<float>& out) {
    std::vector<int> at;
    for (std::size_t i = 0; i < out.size(); i += 2)
        if (out[i] > 0.3f) at.push_back(static_cast<int>(i / 2));
    return at;
}
static Command fxOf(int stepIndex, int pitch, int ratchet, bool reverse, int cut, int lane = 0) {
    Command c = make(Command::SetStepFx, 0, lane, stepIndex, reverse ? 1.f : 0.f);
    c.c = pitch;
    c.d = ratchet;
    c.extra = static_cast<float>(cut);
    return c;
}
// A hit by hand, with step effects.
static Command hitWith(int pitch, int ratchet, bool reverse, int cut, int start = 0, int part = 0) {
    Command c = make(Command::Trigger, 0, 0, start, 1.f);
    c.c = part;
    c.d = pitch;
    c.bar = ratchet;
    c.scene = reverse ? 1 : 0;
    c.extra = static_cast<float>(cut);
    return c;
}

static void testStepEffectPitch() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, slope()));
    // As it is: a bar long, and the slope as recorded.
    engine->send(hitWith(0, 0, false, 0));
    std::vector<float> out = renderFrames(*engine, 40 * kStep);
    CHECK(soundingFrames(out) == 16 * kStep && about(out[2 * 3000], slopeAt(3000)));
    // An octave up: twice as fast, so half as long, and twice as far along at any moment.
    engine->send(hitWith(12, 0, false, 0));
    out = renderFrames(*engine, 40 * kStep);
    CHECK(soundingFrames(out) == 8 * kStep && about(out[2 * 3000], slopeAt(6000)) && about(out[2 * 30001], slopeAt(60002)));
    // An octave down: half as fast, twice as long, with the samples between filled in on a straight line.
    engine->send(hitWith(-12, 0, false, 0));
    out = renderFrames(*engine, 40 * kStep);
    CHECK(soundingFrames(out) == 32 * kStep && about(out[2 * 3001], slopeAt(1500.5)) && about(out[2 * 60000], slopeAt(30000)));
    // A fifth up: 2^(7/12) as fast.
    engine->send(hitWith(7, 0, false, 0));
    out = renderFrames(*engine, 40 * kStep);
    const double fifth = std::pow(2.0, 7.0 / 12.0);
    CHECK(std::abs(soundingFrames(out) - static_cast<int>(16 * kStep / fifth)) <= 2 && std::fabs(out[2 * 30000] - slopeAt(30000 * fifth)) < 1e-3f);
    // Between two samples it is on the straight line from one to the next: a recording that goes
    // 0.2, 0.6, 0.2, 0.6... at half speed comes out 0.2, 0.4, 0.6, 0.4...
    {
        std::unique_ptr<Engine> zig(new Engine);
        std::unique_ptr<SampleData> teeth(new SampleData);
        teeth->data.resize(4000);
        for (std::size_t i = 0; i < teeth->data.size(); ++i) teeth->data[i] = i % 2 ? 0.6f : 0.2f;
        CHECK(zig->setLaneSample(0, 0, std::move(teeth)));
        zig->send(hitWith(-12, 0, false, 0));
        const std::vector<float> half = renderFrames(*zig, 400);
        CHECK(about(half[2 * 100], 0.2f * kTrackLevel) && about(half[2 * 101], 0.4f * kTrackLevel) && about(half[2 * 102], 0.6f * kTrackLevel) && about(half[2 * 103], 0.4f * kTrackLevel));
    }
    // Past an octave either way is an octave.
    engine->send(hitWith(40, 0, false, 0));
    CHECK(soundingFrames(renderFrames(*engine, 40 * kStep)) == 8 * kStep);
    engine->send(hitWith(-40, 0, false, 0));
    CHECK(soundingFrames(renderFrames(*engine, 40 * kStep)) == 32 * kStep);

    // In a pattern: this hit and no other.
    engine->send(step(0, 0, 1.f));
    engine->send(step(0, 8, 1.f));
    engine->send(fxOf(8, 12, 0, false, 0));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 16 * kStep);
    CHECK(about(out[2 * 3000], slopeAt(3000)) && about(out[2 * (8 * kStep + 3000)], slopeAt(6000)));
    engine->send(make(Command::Stop));
}

static void testStepEffectReverse() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, slope()));
    // The whole sound backwards: it starts at its end and stops at its start, and is as long.
    engine->send(hitWith(0, 0, true, 0));
    std::vector<float> out = renderFrames(*engine, 40 * kStep);
    CHECK(soundingFrames(out) >= 16 * kStep - 2 && soundingFrames(out) <= 16 * kStep);
    CHECK(about(out[2 * 3000], slopeAt(16 * kStep - 1 - 3000)) && about(out[2 * 60000], slopeAt(16 * kStep - 1 - 60000)));
    // It fades in and it fades out: no click at either end (the slope is far from zero at both).
    CHECK(std::fabs(out[0]) < 0.02f && out[2 * 10] < out[2 * 30] && about(out[2 * 60], slopeAt(16 * kStep - 61)));
    CHECK(std::fabs(out[2 * (16 * kStep - 2)]) < 0.03f && out[2 * (16 * kStep - 30)] > out[2 * (16 * kStep - 10)]);
    CHECK(about(out[2 * (16 * kStep - 60)], slopeAt(59)));
    // A slice backwards: from the slice's end to its start, and no further.
    engine->send(part(0, 1.f / 16.f));
    engine->send(hitWith(0, 0, true, 0, 2500)); // slice 4
    out = renderFrames(*engine, 40 * kStep);
    CHECK(soundingFrames(out) >= kStep - 2 && soundingFrames(out) <= kStep);
    CHECK(about(out[2 * 1000], slopeAt(5 * kStep - 1 - 1000)) && about(out[2 * 5000], slopeAt(5 * kStep - 1 - 5000)));
    // From a start, with no slices: what it would have played (from there to the end), backwards.
    engine->send(part(0, 0.f));
    engine->send(hitWith(0, 0, true, 0, 7500));
    out = renderFrames(*engine, 40 * kStep);
    CHECK(soundingFrames(out) >= 4 * kStep - 2 && soundingFrames(out) <= 4 * kStep && about(out[2 * 1000], slopeAt(16 * kStep - 1 - 1000)));
    // Backwards and an octave up: half as long.
    engine->send(hitWith(12, 0, true, 0));
    out = renderFrames(*engine, 40 * kStep);
    CHECK(soundingFrames(out) >= 8 * kStep - 2 && soundingFrames(out) <= 8 * kStep && about(out[2 * 3000], slopeAt(16 * kStep - 1 - 6000)));
    CHECK(std::fabs(out[2 * (8 * kStep - 2)]) < 0.06f); // still faded out by its start

    // In a pattern.
    engine->send(step(0, 4, 1.f));
    engine->send(fxOf(4, 0, 0, true, 0));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 8 * kStep);
    CHECK(about(out[2 * (4 * kStep + 3000)], slopeAt(16 * kStep - 1 - 3000)));
    engine->send(make(Command::Stop));
    // A hit backwards never "carries on" a sound that is at its start: it goes to its own end and comes back.
    renderFrames(*engine, 40 * kStep);
    engine->send(make(Command::Trigger, 0, 0, 0, 1.f));
    renderFrames(*engine, 4 * kStep); // forwards, a quarter of the way in
    engine->send(hitWith(0, 0, true, 0, 2500));
    out = renderFrames(*engine, 400);
    CHECK(about(out[2 * 300], slopeAt(16 * kStep - 1 - 300)));
    renderFrames(*engine, 40 * kStep);
    // A hit forwards never carries on a sound that is running backwards, wherever it is.
    engine->send(hitWith(0, 0, true, 0));
    renderFrames(*engine, 8 * kStep); // now at half-way, going down
    engine->send(make(Command::Trigger, 0, 0, 5000, 1.f));
    out = renderFrames(*engine, 400);
    CHECK(about(out[2 * 300], slopeAt(8 * kStep + 300))); // going up from the middle: a new start
}

static void testStepEffectCut() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, slope()));
    const auto lasts = [&](int cut, int pitch = 0) {
        engine->send(hitWith(pitch, 0, false, cut));
        const std::vector<float> out = renderFrames(*engine, 40 * kStep);
        return soundingFrames(out);
    };
    CHECK(lasts(0) == 16 * kStep);
    CHECK(lasts(1) >= kStep && lasts(1) <= kStep + 50);         // a step, and a millisecond's fade
    CHECK(lasts(2) >= kStep / 2 && lasts(2) <= kStep / 2 + 50); // half a step
    CHECK(lasts(3) >= kStep / 4 && lasts(3) <= kStep / 4 + 50); // a quarter
    CHECK(lasts(4) == 16 * kStep && lasts(-1) == 16 * kStep);   // nonsense: not cut
    CHECK(lasts(1, 12) >= kStep && lasts(1, 12) <= kStep + 50); // a step is a step whatever the pitch
    // It is at full level right up to the cut.
    engine->send(hitWith(0, 0, false, 1));
    std::vector<float> out = renderFrames(*engine, 40 * kStep);
    CHECK(about(out[2 * (kStep - 5)], slopeAt(kStep - 5)) && out[2 * (kStep + 60)] == 0.f);
    // A step at the tempo: half as fast, twice as long.
    engine->send(make(Command::SetTempo, 0, 0, 0, 60.f));
    CHECK(lasts(1) >= 2 * kStep && lasts(1) <= 2 * kStep + 50);
    engine->send(make(Command::SetTempo, 0, 0, 0, 120.f));

    // In a pattern: the cut hit stops, the others ring on until the next hit.
    engine->send(step(0, 0, 1.f));
    engine->send(step(0, 4, 1.f));
    engine->send(fxOf(0, 0, 0, false, 2));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 8 * kStep);
    CHECK(out[2 * (kStep / 2 - 5)] != 0.f && out[2 * (kStep / 2 + 60)] == 0.f && out[2 * (3 * kStep)] == 0.f);
    CHECK(out[2 * (5 * kStep)] != 0.f && out[2 * (7 * kStep)] != 0.f);
    engine->send(make(Command::Stop));
}

static void testStepEffectRatchet() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, click()));
    engine->send(step(0, 0, 1.f));
    engine->send(step(0, 4, 1.f));
    engine->send(step(0, 8, 1.f));
    engine->send(step(0, 12, 1.f));
    engine->send(fxOf(4, 0, 2, false, 0));
    engine->send(fxOf(8, 0, 3, false, 0));
    engine->send(fxOf(12, 0, 4, false, 0));
    engine->send(make(Command::Play));
    std::vector<float> out = renderFrames(*engine, 16 * kStep);
    // One hit; two, half a step apart; three, a third apart; four, a quarter apart: each on its exact sample.
    const std::vector<int> expected = {0, 4 * kStep, 4 * kStep + 3000, 8 * kStep, 8 * kStep + 2000, 8 * kStep + 4000,
                                       12 * kStep, 12 * kStep + 1500, 12 * kStep + 3000, 12 * kStep + 4500};
    CHECK(clicksIn(out) == expected);
    // Round again it is the same (nothing was left waiting, nothing doubled).
    out = renderFrames(*engine, 16 * kStep);
    CHECK(clicksIn(out) == expected);
    engine->send(make(Command::Stop));
    renderFrames(*engine, 4000);

    // A hit moved late takes its ratchet with it; one moved early too.
    engine->send(make(Command::SetStepNudge, 0, 0, 8, 0.25f));
    engine->send(make(Command::SetStepNudge, 0, 0, 12, -0.5f));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 16 * kStep);
    const std::vector<int> moved = {0, 4 * kStep, 4 * kStep + 3000, 8 * kStep + 1500, 8 * kStep + 3500, 8 * kStep + 5500,
                                    12 * kStep - 3000, 12 * kStep - 1500, 12 * kStep, 12 * kStep + 1500};
    CHECK(clicksIn(out) == moved);
    engine->send(make(Command::Stop));
    renderFrames(*engine, 4000);
    engine->send(make(Command::SetStepNudge, 0, 0, 8, 0.f));
    engine->send(make(Command::SetStepNudge, 0, 0, 12, 0.f));

    // Stopping part-way through a ratchet stops it: the hits still waiting do not come.
    engine->send(make(Command::Play));
    renderFrames(*engine, 12 * kStep + 2000); // two of the four have played
    engine->send(make(Command::Stop));
    CHECK(clicksIn(renderFrames(*engine, 4 * kStep)).empty());

    // A chance is rolled once for the whole ratchet: all of its hits or none.
    engine->send(make(Command::SetStepChance, 0, 0, 12, 0.5f));
    engine->send(make(Command::Play));
    int all = 0, none = 0, some = 0;
    for (int bar = 0; bar < 60; ++bar) {
        out = renderFrames(*engine, 16 * kStep);
        int count = 0;
        for (int at : clicksIn(out)) count += at >= 12 * kStep;
        all += count == 4;
        none += count == 0;
        some += count != 0 && count != 4;
    }
    CHECK(some == 0 && all > 10 && none > 10);
    engine->send(make(Command::Stop));
    renderFrames(*engine, 4000);

    // Heard by hand, with the song stopped: the whole ratchet, at the tempo.
    engine->send(hitWith(0, 3, false, 0));
    CHECK((clicksIn(renderFrames(*engine, 2 * kStep)) == std::vector<int>{0, 2000, 4000}));
    engine->send(hitWith(0, 9, false, 0)); // more than four is four
    CHECK(clicksIn(renderFrames(*engine, 2 * kStep)).size() == 4);
    engine->send(hitWith(0, 1, false, 0));
    CHECK(clicksIn(renderFrames(*engine, 2 * kStep)).size() == 1);

    // Ratchet and cut together: each hit stops when its time is up, or when the next one comes.
    std::unique_ptr<Engine> held(new Engine);
    CHECK(held->setLaneSample(0, 0, slope()));
    held->send(hitWith(0, 2, false, 3)); // two hits, each a quarter of a step long
    out = renderFrames(*held, 2 * kStep);
    CHECK(out[2 * 1400] != 0.f && out[2 * 1600] == 0.f && out[2 * 2900] == 0.f && out[2 * 3100] != 0.f && out[2 * 4400] != 0.f && out[2 * 4600] == 0.f);
    held->send(hitWith(0, 4, false, 1)); // four hits, cut after a step: the next hit comes first, and the last stops at the step's end
    out = renderFrames(*held, 3 * kStep);
    CHECK(out[2 * 1400] != 0.f && out[2 * 2900] != 0.f && out[2 * 5900] != 0.f && out[2 * (kStep + 60)] == 0.f);
    // Each hit of a ratchet starts the sound again from where the hit starts.
    CHECK(about(out[2 * 1700], slopeAt(200)) && about(out[2 * 4700], slopeAt(200)));
}

static void testStepEffectsTravel() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->setLaneSample(0, 0, slope()));
    // Nonsense is held to what there is, or ignored.
    engine->send(step(0, 2, 1.f));
    engine->send(fxOf(2, 99, 99, false, 99));
    engine->send(fxOf(99, 12, 2, true, 1)); // no such step
    engine->send(fxOf(2, 12, 2, true, 1, kDrumLanes)); // no such lane
    engine->send(make(Command::Play));
    std::vector<float> out = renderFrames(*engine, 8 * kStep);
    CHECK(about(out[2 * (2 * kStep + 1000)], slopeAt(2000))); // an octave up at most
    CHECK(about(out[2 * (2 * kStep + 1500 + 1000)], slopeAt(2000))); // four hits at most: the second, a quarter of a step on
    CHECK(out[2 * (6 * kStep)] != 0.f); // a cut that is not one of the three: not cut
    engine->send(make(Command::Stop));
    renderFrames(*engine, 40 * kStep);

    // Emptying the cell takes the effects with the hits: a new hit on the same step is plain.
    engine->send(make(Command::ClearCell));
    engine->send(step(0, 2, 1.f));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 8 * kStep);
    CHECK(about(out[2 * (2 * kStep + 1000)], slopeAt(1000)) && about(out[2 * (2 * kStep + 2500)], slopeAt(2500)));
    engine->send(make(Command::Stop));
    renderFrames(*engine, 40 * kStep);

    // A whole song loaded at once brings them.
    std::unique_ptr<TrackImage> image(new TrackImage);
    image->level[0][0][2] = 1.f;
    image->fx[0][0][2].pitch = -12;
    image->fx[0][0][2].reverse = 1;
    image->fx[0][0][2].cut = 1;
    CHECK(engine->setTrackImage(0, std::move(image)));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 8 * kStep);
    CHECK(about(out[2 * (2 * kStep + 3000)], slopeAt(16 * kStep - 1 - 1500)));
    CHECK(out[2 * (3 * kStep - 5)] != 0.f && out[2 * (3 * kStep + 60)] == 0.f);
    engine->send(make(Command::Stop));
    // And removing the track forgets them.
    CHECK(engine->removeTrack(0));
    CHECK(engine->addDrumTrack(0));
    CHECK(engine->setLaneSample(0, 0, slope()));
    engine->send(step(0, 2, 1.f));
    engine->send(make(Command::Play));
    out = renderFrames(*engine, 8 * kStep);
    CHECK(about(out[2 * (2 * kStep + 3000)], slopeAt(3000)) && out[2 * (4 * kStep)] != 0.f);
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
    // FILTER, the five types.
    {
        // Songs kept when there were three types (a third of the range each) open as what they were.
        CHECK(filterType(0.f) == FilterType::LowPass2 && filterType(1.f / 6.f) == FilterType::LowPass2 && filterType(0.5f) == FilterType::BandPass);
        CHECK(filterType(5.f / 6.f) == FilterType::HighPass && filterType(1.f) == FilterType::HighPass);
        CHECK(filterType(0.3f) == FilterType::LowPass4 && filterType(0.7f) == FilterType::Dj && filterType(-1.f) == FilterType::LowPass2);
        std::unique_ptr<Effect> f = makeEffect(EffectKind::Filter, 48000.f, 120.f);
        f->setParam(0, 0.5f); // about 800 Hz
        f->setParam(1, 0.f);
        const auto kept = [&](float hz) {
            const std::vector<float> tone = sine(hz, 24000);
            f->reset();
            return rmsOf(through(*f, tone), 12000, 24000) / rmsOf(tone, 12000, 24000);
        };
        // Four poles fall twice as fast as two: two octaves above the corner,
        // two poles leave about a sixteenth, four about a 256th. Below it, both let the sound by.
        f->setParam(2, 0.1f);
        const float two = kept(3200.f), twoLow = kept(100.f);
        f->setParam(2, 0.3f);
        const float four = kept(3200.f), fourLow = kept(100.f);
        CHECK(two > 0.03f && two < 0.12f && four > 0.002f && four < 0.008f && twoLow > 0.9f && fourLow > 0.9f);
        // Resonance still lifts the corner on the four-pole one.
        const float plain = kept(807.f);
        f->setParam(1, 1.f);
        CHECK(kept(807.f) > 4.f * plain);
        f->setParam(1, 0.f);

        // The DJ filter. In the middle: nothing at all, sample for sample.
        f->setParam(2, 0.7f);
        f->setParam(0, 0.5f);
        const std::vector<float> both = sine(100.f, 4800, 0.5f);
        f->reset();
        CHECK(through(*f, both) == both);
        f->setParam(0, 0.515f); // anywhere in the little dead part round the middle
        CHECK(through(*f, both) == both);
        f->setParam(0, 0.485f);
        CHECK(through(*f, both) == both);
        // Turned down: the top goes, the bottom stays. Turned up: the other way.
        f->setParam(0, 0.25f);
        CHECK(kept(8000.f) < 0.05f && kept(100.f) > 0.9f);
        f->setParam(0, 0.f);
        CHECK(kept(1000.f) < 0.01f && kept(40.f) > 0.5f);
        f->setParam(0, 0.75f);
        CHECK(kept(100.f) < 0.15f && kept(8000.f) > 0.9f);
        f->setParam(0, 1.f);
        CHECK(kept(1000.f) < 0.05f && kept(16000.f) > 0.5f);
        // Just out of the middle it is only beginning: most of the sound is still as it was.
        f->setParam(0, 0.53f);
        CHECK(kept(40.f) > 0.6f && kept(8000.f) > 0.95f);
        f->setParam(0, 0.47f);
        CHECK(kept(40.f) > 0.95f && kept(15000.f) > 0.5f);
        // Where its corner is, for the screen.
        float hz = 1.f;
        CHECK(djFilter(0.5f, hz) == 0 && hz == 0.f);
        CHECK(djFilter(0.f, hz) == -1 && std::fabs(hz - 60.f) < 0.5f);
        CHECK(djFilter(0.479f, hz) == -1 && hz > 18000.f && hz < 20000.f);
        CHECK(djFilter(0.521f, hz) == 1 && hz > 20.f && hz < 22.f);
        CHECK(djFilter(1.f, hz) == 1 && std::fabs(hz - 8000.f) < 1.f);
        // Back from the middle after a loud sound: no burst of what it held.
        f->setParam(0, 0.2f);
        through(*f, sine(200.f, 4800, 1.f));
        f->setParam(0, 0.5f);
        through(*f, std::vector<float>(64, 0.f));
        f->setParam(0, 0.2f);
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
    CHECK(shortTail >= 100 && shortTail <= 150); // two milliseconds to fall 60 dB, and a little more to silence
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
    // An early note on step 1 belongs to the end of the bar before. The
    // first time round there is no "before": it starts at the top, the same
    // as a note that is on time. After that, just before each bar line.
    out = bar(-0.25f, 0, 2);
    CHECK(onsetOf(out) == onsetOf(bar(0.f, 0, 2)) && onsetOf(out) <= 1);
    CHECK(out[2 * (8 * kStep)] == 0.f); // two steps long: silent by the middle of the bar
    CHECK(out[2 * (16 * kStep - kStep / 4 - 50)] == 0.f && out[2 * (16 * kStep - kStep / 4 + 50)] != 0.f);
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

    // A long note runs on over the bar lines: forty steps from the middle of
    // bar 1 is still sounding all through bar 2 and into bar 3, and then stops.
    {
        Command longNote = make(Command::SetNote, 2, 1, 8, 1.f);
        longNote.c = 72;
        longNote.d = 40;
        engine->send(make(Command::SetSetting, 2, sampler::Loop, 1));
        engine->send(make(Command::SetSetting, 2, sampler::Release, 0));
        engine->send(longNote);
        note.b = 15; // the short note out of the way, late in its bar
        engine->send(note);
        play(1);
        out = renderFrames(*engine, 4 * 16 * kStep);
        for (int stepIndex = 8; stepIndex < 47; ++stepIndex) CHECK(out[2 * static_cast<std::size_t>(stepIndex * kStep + 100)] != 0.f);
        CHECK(out[2 * static_cast<std::size_t>(48 * kStep + 2000)] == 0.f && out[2 * static_cast<std::size_t>(7 * kStep)] == 0.f);
        stop();
        longNote.value = 0.f;
        engine->send(longNote);
        note.b = 0;
        engine->send(note);
        engine->send(make(Command::SetSetting, 2, sampler::Loop, 0));
    }
    // A track with no instrument cannot make a scene longer, whatever it is told.
    {
        std::unique_ptr<Engine> bare(new Engine);
        CHECK(bare->setLaneSample(0, 0, twoHalves(1000, 0.2f, 0.2f)));
        bare->send(step(0, 0, 1.f));
        Command empty = make(Command::SetCellBars, 7, 8);
        bare->send(empty);
        bare->send(make(Command::Play, 0, 0, 1));
        const std::vector<float> two = renderFrames(*bare, 2 * 16 * kStep);
        CHECK(two[2 * static_cast<std::size_t>(16 * kStep)] != 0.f);
        renderFrames(*bare, 8 * kStep);
        CHECK(bare->playBar() == 0); // the scene is still one bar long: it is in its first bar again, not its third
        // ClearCell empties notes as well as hits, in every bar of the cell.
        CHECK(bare->addSynthTrack(1, SynthKind::Sampler));
        std::unique_ptr<SampleData> flat2(new SampleData);
        flat2->data.assign(4800, 0.5f);
        CHECK(bare->setTrackSample(1, std::move(flat2)));
        Command n = make(Command::SetNote, 1, 0, 4, 1.f);
        n.c = 60;
        n.d = 1;
        n.bar = 3;
        bare->send(n);
        bare->send(make(Command::SetCellBars, 1, 4));
        Command clear = make(Command::ClearCell, 1);
        bare->send(clear);
        bare->send(make(Command::Stop));
        renderFrames(*bare, 20000);
        bare->send(step(0, 0, 0.f));
        bare->send(make(Command::Play, 0, 0, 1));
        CHECK(peakOf(renderFrames(*bare, 4 * 16 * kStep)) == 0.f);
    }
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
    testAHitPlaysOnlyItsSlice();
    testSlicesInOrderHaveNoSeams();
    testStepEffectPitch();
    testStepEffectReverse();
    testStepEffectCut();
    testStepEffectRatchet();
    testStepEffectsTravel();
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
    testSharingTracksAcrossCores();
    testHelpersAlwaysStop();
    testOddStepCounts();
    testAutomation();
    testTheSharedEnvelope();
    testFallingBehindLowersTheQuality();
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
