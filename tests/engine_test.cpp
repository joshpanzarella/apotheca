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

static float peakOf(const std::vector<float>& buffer) {
    float peak = 0.f;
    for (float s : buffer) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    return peak;
}

static Command noteOn(int track, int pitch, float velocity = 1.f) {
    Command c;
    c.type = Command::NoteOn;
    c.track = track;
    c.a = pitch;
    c.value = velocity;
    return c;
}

static Command effectParam(int track, int slot, int param, float value) {
    Command c;
    c.type = Command::SetEffectParam;
    c.track = track;
    c.a = slot;
    c.b = param;
    c.value = value;
    return c;
}

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

static void testSilentUntilPlayed() {
    Engine engine;
    CHECK(peakOf(renderFrames(engine, 500)) == 0.f);
}

static void testNoteSoundsOnItsTrackOnly() {
    Engine engine;
    engine.send(noteOn(3, 69));
    const std::vector<float> out = renderFrames(engine, 1000);
    CHECK(peakOf(out) > 0.5f);
    CHECK(engine.trackPeak(3) > 0.5f);
    for (int t = 0; t < kNumTracks; ++t) {
        if (t != 3) CHECK(engine.trackPeak(t) == 0.f);
    }
    // Both sides carry the same signal for now.
    CHECK(out[201] == out[200]);
}

static void testNoteOffFades() {
    Engine engine;
    engine.send(noteOn(0, 69));
    renderFrames(engine, 480);
    Command off;
    off.type = Command::NoteOff;
    off.track = 0;
    engine.send(off);
    renderFrames(engine, 48000); // one second, far past the 80 ms release
    CHECK(peakOf(renderFrames(engine, 480)) == 0.f);
}

static void testTrackLevel() {
    Engine engine;
    Command level;
    level.type = Command::SetTrackLevel;
    level.track = 0;
    level.value = 0.f;
    engine.send(level);
    engine.send(noteOn(0, 69));
    CHECK(peakOf(renderFrames(engine, 1000)) == 0.f);
}

static void testTrackEffectSlotsRunInSeries() {
    Engine plain, shaped;
    shaped.setTrackEffect(0, 0, std::unique_ptr<Effect>(new GainEffect));
    shaped.setTrackEffect(0, 1, std::unique_ptr<Effect>(new GainEffect));
    shaped.send(effectParam(0, 0, 0, 0.5f));
    shaped.send(effectParam(0, 1, 0, 0.5f));
    plain.send(noteOn(0, 69));
    shaped.send(noteOn(0, 69));
    const float a = peakOf(renderFrames(plain, 1000));
    const float b = peakOf(renderFrames(shaped, 1000));
    CHECK(std::fabs(b - 0.25f * a) < 1e-4f);

    // A track's effect leaves the other tracks alone.
    Engine other;
    other.setTrackEffect(0, 0, std::unique_ptr<Effect>(new GainEffect));
    other.send(effectParam(0, 0, 0, 0.f));
    other.send(noteOn(1, 69));
    CHECK(std::fabs(peakOf(renderFrames(other, 1000)) - a) < 1e-4f);
}

static void testMasterEffectSlots() {
    Engine plain, shaped;
    shaped.setMasterEffect(0, std::unique_ptr<Effect>(new GainEffect));
    shaped.setMasterEffect(1, std::unique_ptr<Effect>(new GainEffect));
    shaped.send(effectParam(kMasterTrack, 0, 0, 0.5f));
    shaped.send(effectParam(kMasterTrack, 1, 0, 0.5f));
    for (Engine* e : {&plain, &shaped}) {
        e->send(noteOn(0, 69));
        e->send(noteOn(5, 76));
    }
    const std::vector<float> a = renderFrames(plain, 1000);
    const std::vector<float> b = renderFrames(shaped, 1000);
    bool scaled = true;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(b[i] - 0.25f * a[i]) > 1e-5f) scaled = false;
    }
    CHECK(scaled);
    // The master effect is after the track meters.
    CHECK(std::fabs(shaped.trackPeak(0) - plain.trackPeak(0)) < 1e-6f);
}

static void testLowpassCutsHighs() {
    Engine open, dark;
    dark.setTrackEffect(0, 0, std::unique_ptr<Effect>(new LowpassEffect));
    dark.send(effectParam(0, 0, 0, 200.f));
    open.send(noteOn(0, 105)); // about 3.5 kHz
    dark.send(noteOn(0, 105));
    const float a = peakOf(renderFrames(open, 2000));
    const float b = peakOf(renderFrames(dark, 2000));
    CHECK(b < 0.15f * a);
    CHECK(b > 0.f);
}

static void testBlockSizeDoesNotChangeTheSound() {
    Engine whole, pieces;
    whole.setTrackEffect(0, 0, std::unique_ptr<Effect>(new LowpassEffect));
    pieces.setTrackEffect(0, 0, std::unique_ptr<Effect>(new LowpassEffect));
    whole.send(noteOn(0, 60));
    pieces.send(noteOn(0, 60));

    const int total = 1000;
    const std::vector<float> a = renderFrames(whole, total);
    std::vector<float> b;
    const int sizes[] = {1, 63, 64, 65, 7, 300, 500};
    for (int n : sizes) {
        const std::vector<float> part = renderFrames(pieces, n);
        b.insert(b.end(), part.begin(), part.end());
    }
    CHECK(b.size() == a.size());
    bool same = b.size() == a.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
    CHECK(same);
}

static void testBadCommandsAreIgnored() {
    Engine engine;
    engine.send(noteOn(-5, 69));
    engine.send(noteOn(kNumTracks, 69));
    engine.send(effectParam(0, 7, 0, 1.f));
    engine.send(effectParam(0, 0, 0, 1.f)); // empty slot
    engine.send(noteOn(0, 69, std::nanf("")));
    const std::vector<float> out = renderFrames(engine, 500);
    CHECK(peakOf(out) == 0.f);
}

int main() {
    testQueue();
    testSilentUntilPlayed();
    testNoteSoundsOnItsTrackOnly();
    testNoteOffFades();
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
