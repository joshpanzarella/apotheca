// The timings' own song (app/bench_run.cpp: `bench`, `apotheca --bench`, the WiFi page's SPEED
// TEST). The figures are read by a person; what is held here is that the song they time is the
// song the docs say it is, and that the real-time rows count what they say. Nothing here times
// anything: a slow machine passes.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/effects.hpp"
#include "bench_run.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

// The song, then `presets` chosen on the two JAX-EP tracks (6 and 7; -1: left as the song has
// them), then a second of it. Engines are 1.4 MB each: on the heap.
std::vector<float> aSecond(int preset, Engine** kept = nullptr) {
    Engine* engine = new Engine;
    sendBusiestSong(*engine);
    if (preset >= 0)
        for (int track : {6, 7}) {
            Command c;
            c.type = Command::SelectPreset;
            c.track = track;
            c.a = preset;
            CHECK(engine->send(c));
        }
    std::vector<float> out(2 * 48000), block(512);
    for (int at = 0; at + 256 <= 48000; at += 256) {
        engine->render(block.data(), 256);
        engine->collectGarbage();
        std::memcpy(&out[2 * static_cast<std::size_t>(at)], block.data(), 512 * sizeof(float));
    }
    CHECK(engine->dropped() == 0);
    if (kept) *kept = engine;
    else delete engine;
    return out;
}

float apart(const std::vector<float>& a, const std::vector<float>& b) {
    float most = 0.f;
    for (std::size_t i = 0; i < a.size(); ++i) most = std::max(most, std::fabs(a[i] - b[i]));
    return most;
}

// The two JAX-EP tracks are on A1, as a new track in the program is: choosing A1 on them once
// more changes nothing, to the bit. (The engine makes the synth on its start patch, which is not
// a preset: a song that forgot to choose would be timing a plain saw.) And the song is what the
// comment over it, README and CLAUDE.md say: twelve tracks, three effects each, the buses, the
// master, and it sounds.
void testTheSongIsWhatItSays() {
    Engine* engine = nullptr;
    const std::vector<float> song = aSecond(-1, &engine);
    float peak = 0.f;
    bool sound = true;
    for (float v : song) {
        if (!std::isfinite(v)) sound = false;
        peak = std::max(peak, std::fabs(v));
    }
    CHECK(sound);
    CHECK(peak > 0.05f);
    std::printf("the busiest song: a second of it, peak %.3f\n", peak);

    CHECK(engine->playStep() >= 0); // playing
    CHECK(engine->flushesDenormals()); // as in the program
    for (int t = 0; t < kNumTracks; ++t) {
        CHECK(engine->trackPeak(t) > 0.f); // every one of the twelve sounds
        CHECK(engine->effectKindIn(t, 0) == static_cast<int>(EffectKind::Comp));
        CHECK(engine->effectKindIn(t, 1) == static_cast<int>(t % 2 ? EffectKind::Chorus : EffectKind::Saturate));
        CHECK(engine->effectKindIn(t, 2) == static_cast<int>(t % 2 ? EffectKind::Plate : EffectKind::Hall));
    }
    CHECK(engine->effectKindIn(kMasterTrack, 0) == static_cast<int>(EffectKind::Comp));
    CHECK(engine->effectKindIn(kMasterTrack, 1) == static_cast<int>(EffectKind::Saturate));
    CHECK(engine->effectKindIn(kEchoBus, 0) == static_cast<int>(EffectKind::Tape));
    CHECK(engine->effectKindIn(kSpaceBus, 0) == static_cast<int>(EffectKind::Plate));
    CHECK(engine->busAwake(0) && engine->busAwake(1));
    delete engine;

    const float again = apart(song, aSecond(-1));
    CHECK(again == 0.f); // (the song is the same song twice: or nothing below means anything)
    const float onA1 = apart(song, aSecond(0));
    CHECK(onA1 == 0.f);
    const float onAnother = apart(song, aSecond(5)); // (the check can see: another preset is another sound)
    CHECK(onAnother > 1e-3f);
    std::printf("  A1 chosen again: %.3g apart; another preset: %.3g apart\n", onA1, onAnother);
}

// The real-time rows: the blocks asked for are the blocks timed, it was playing, and the
// helpers asked for are the helpers it had.
void testTheRowsCountWhatTheySay() {
    const WholeSongTimes alone = timeWholeSong(0, 0.25);
    CHECK(alone.blocks == 46); // a quarter of a second in blocks of 256 at 48 kHz
    CHECK(alone.helpers == 0);
    CHECK(alone.peak > 0.05f);
    CHECK(alone.meanMs > 0.0 && alone.worstMs >= alone.p99Ms && alone.p99Ms >= 0.0);
    CHECK(alone.over >= 0 && alone.over <= alone.blocks);
    const WholeSongTimes helped = timeWholeSong(2, 0.25);
    CHECK(helped.blocks == 46);
    CHECK(helped.helpers == 2);
    CHECK(helped.peak > 0.05f);
}

}

int main() {
    testTheSongIsWhatItSays();
    testTheRowsCountWhatTheySay();
    if (failures) {
        std::printf("%d bench test failure(s)\n", failures);
        return 1;
    }
    std::printf("bench tests passed\n");
    return 0;
}
