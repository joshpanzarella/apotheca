// Engine tests for the song: scenes, repeats, synth note patterns, adding
// tracks while running, and the scope.

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
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

enum { KICK = 0, RIM = 6 };
static const int kStep = 6000;       // frames in a step at 120 BPM, 48 kHz
static const int kBar = 16 * kStep;

static Command make(Command::Type type, int track = 0, int scene = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.scene = scene;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
static Command hit(int scene, int lane, int step, float level = 1.f) { return make(Command::SetStep, 0, scene, lane, step, level); }
static Command note(int track, int scene, int slot, int step, int pitch, int length, float velocity = 1.f) {
    Command c = make(Command::SetNote, track, scene, slot, step, velocity);
    c.c = pitch;
    c.d = length;
    return c;
}
static Command play(int scene = 0, bool loop = false) { return make(Command::Play, 0, 0, scene, loop ? 1 : 0); }

static std::vector<float> renderFrames(Engine& engine, int frames) {
    std::vector<float> out(2 * frames, 99.f);
    engine.render(out.data(), frames);
    return out;
}
static float peakOf(const std::vector<float>& buffer, int fromFrame = 0, int toFrame = -1) {
    float peak = 0.f;
    const std::size_t end = toFrame < 0 ? buffer.size() : static_cast<std::size_t>(2 * toFrame);
    for (std::size_t i = 2 * static_cast<std::size_t>(fromFrame); i < end; ++i) peak = std::fabs(buffer[i]) > peak ? std::fabs(buffer[i]) : peak;
    return peak;
}

// Which scene a bar played, told by which step its rim hit fell on: scene n
// has its only hit on step n + 1.
static int sceneHeardIn(Engine& engine) {
    const std::vector<float> bar = renderFrames(engine, kBar);
    int found = -1, count = 0;
    for (int step = 0; step < 16; ++step) {
        if (peakOf(bar, step * kStep, step * kStep + 400) > 0.05f && (step == 0 || peakOf(bar, step * kStep - 200, step * kStep) < 0.05f)) {
            found = step;
            ++count;
        }
    }
    return count == 1 ? found : -1 - count;
}

static Engine* markedSong(int scenes) {
    Engine* engine = new Engine;
    for (int scene = 0; scene < scenes; ++scene) engine->send(hit(scene, RIM, scene));
    engine->send(make(Command::SetSceneCount, 0, 0, scenes));
    return engine;
}

static void testScenesPlayInOrderAndComeRound() {
    std::unique_ptr<Engine> engine(markedSong(3));
    engine->send(play());
    const int expected[] = {0, 1, 2, 0, 1, 2, 0};
    for (int scene : expected) CHECK(sceneHeardIn(*engine) == scene);
}

static void testRepeatsPerScene() {
    std::unique_ptr<Engine> engine(markedSong(3));
    engine->send(make(Command::SetSceneRepeats, 0, 0, 2)); // scene 1 twice
    engine->send(make(Command::SetSceneRepeats, 0, 2, 3)); // scene 3 three times
    engine->send(play());
    const int expected[] = {0, 0, 1, 2, 2, 2, 0, 0, 1};
    for (int scene : expected) CHECK(sceneHeardIn(*engine) == scene);
}

static void testReadOutsFollowTheSong() {
    std::unique_ptr<Engine> engine(markedSong(2));
    engine->send(make(Command::SetSceneRepeats, 0, 0, 2));
    CHECK(engine->playScene() == -1);
    engine->send(play());
    renderFrames(*engine, kStep);
    CHECK(engine->playScene() == 0 && engine->playRepeat() == 0 && engine->playStep() == 0);
    renderFrames(*engine, kBar);
    CHECK(engine->playScene() == 0 && engine->playRepeat() == 1);
    renderFrames(*engine, kBar);
    CHECK(engine->playScene() == 1 && engine->playRepeat() == 0);
    engine->send(make(Command::Stop));
    renderFrames(*engine, 100);
    CHECK(engine->playScene() == -1 && engine->playStep() == -1);
}

static void testStartFromAnyScene() {
    std::unique_ptr<Engine> engine(markedSong(4));
    engine->send(play(2));
    const int expected[] = {2, 3, 0, 1};
    for (int scene : expected) CHECK(sceneHeardIn(*engine) == scene);
    // Past the end of the song there is nothing to start on: it starts at the top.
    std::unique_ptr<Engine> beyond(markedSong(2));
    beyond->send(hit(7, RIM, 7));
    beyond->send(play(7));
    CHECK(sceneHeardIn(*beyond) == 0);
}

static void testLoopOneScene() {
    std::unique_ptr<Engine> engine(markedSong(3));
    engine->send(make(Command::SetSceneRepeats, 0, 1, 2));
    engine->send(play(1, true));
    for (int bar = 0; bar < 5; ++bar) CHECK(sceneHeardIn(*engine) == 1);
    CHECK(engine->playScene() == 1);
    // A scene beyond the song's end can still be looped, to work on it.
    std::unique_ptr<Engine> beyond(markedSong(2));
    beyond->send(hit(7, RIM, 7));
    beyond->send(play(7, true));
    for (int bar = 0; bar < 3; ++bar) CHECK(sceneHeardIn(*beyond) == 7);
}

static void testSceneCountAndRepeatLimits() {
    std::unique_ptr<Engine> engine(markedSong(3));
    engine->send(make(Command::SetSceneCount, 0, 0, 0));    // held to 1
    engine->send(make(Command::SetSceneRepeats, 0, 0, 0));  // held to 1
    engine->send(make(Command::SetSceneRepeats, 0, -1, 5)); // ignored
    engine->send(make(Command::SetSceneRepeats, 0, kMaxScenes, 5));
    engine->send(play());
    for (int bar = 0; bar < 3; ++bar) CHECK(sceneHeardIn(*engine) == 0);
    std::unique_ptr<Engine> many(markedSong(2));
    many->send(make(Command::SetSceneRepeats, 0, 0, 999)); // held to 16
    many->send(play());
    for (int bar = 0; bar < kMaxRepeats; ++bar) CHECK(sceneHeardIn(*many) == 0);
    CHECK(sceneHeardIn(*many) == 1);
}

static void testAnEarlyHitBelongsToTheNextBarsScene() {
    // Scene 2 has a hit on step 1, nudged early. It must sound at the very
    // end of scene 1's bar, and not at the end of scene 2's own bar (after
    // which scene 1 plays again).
    Engine engine;
    engine.send(hit(1, RIM, 0));
    engine.send(make(Command::SetStepNudge, 0, 1, RIM, 0, -0.25f));
    engine.send(make(Command::SetSceneCount, 0, 0, 2));
    engine.send(play());
    const std::vector<float> first = renderFrames(engine, kBar);
    CHECK(peakOf(first, 0, kBar - kStep / 4 - 2) == 0.f);
    CHECK(peakOf(first, kBar - kStep / 4, kBar) > 0.05f);
    const std::vector<float> second = renderFrames(engine, kBar);
    CHECK(peakOf(second, kStep, kBar) == 0.f);
}

static void testScenesKeepSeparatePatterns() {
    Engine engine;
    engine.send(hit(0, KICK, 0));
    engine.send(make(Command::SetStepChance, 0, 0, KICK, 0, 0.f)); // scene 1's kick never plays
    engine.send(hit(1, KICK, 0));                                   // scene 2's always does
    engine.send(make(Command::SetSceneCount, 0, 0, 2));
    engine.send(play());
    CHECK(peakOf(renderFrames(engine, kBar)) == 0.f);
    CHECK(peakOf(renderFrames(engine, kBar)) > 0.1f);
}

// ---- synth note patterns ----------------------------------------------------------

static int organPreset() {
    for (int i = 0; i < Phazed::presetCount(); ++i) {
        if (std::string(Phazed::presetCategory(i)) == "Organs") return i;
    }
    return 0;
}

static void testNotesPlayForTheirLength() {
    Engine engine;
    CHECK(engine.addSynthTrack(1));
    engine.send(make(Command::SelectPreset, 1, 0, organPreset()));
    engine.send(note(1, 0, 0, 4, 60, 2)); // step 5, two steps long
    engine.send(play(0, true));
    const std::vector<float> bar = renderFrames(engine, kBar);
    CHECK(peakOf(bar, 0, 4 * kStep - 10) == 0.f);                 // nothing before it
    CHECK(peakOf(bar, 4 * kStep + 2000, 6 * kStep) > 0.01f);      // sounding through its two steps
    // Let go at the start of step 7: an organ dies away at once.
    CHECK(peakOf(bar, 10 * kStep, kBar) < 0.2f * peakOf(bar, 4 * kStep + 2000, 6 * kStep));
    // The same note twice as long is still sounding two steps later.
    Engine longer;
    longer.addSynthTrack(1);
    longer.send(make(Command::SelectPreset, 1, 0, organPreset()));
    longer.send(note(1, 0, 0, 4, 60, 8));
    longer.send(play(0, true));
    const std::vector<float> held = renderFrames(longer, kBar);
    CHECK(peakOf(held, 10 * kStep, 11 * kStep) > 0.5f * peakOf(held, 5 * kStep, 6 * kStep));
}

static void testChordsAndEmptySlots() {
    Engine one, three;
    for (Engine* e : {&one, &three}) {
        e->addSynthTrack(1);
        e->send(make(Command::SelectPreset, 1, 0, organPreset()));
        e->send(note(1, 0, 0, 0, 60, 4));
    }
    three.send(note(1, 0, 1, 0, 64, 4));
    three.send(note(1, 0, 2, 0, 67, 4));
    three.send(note(1, 0, 3, 0, 72, 4));
    three.send(note(1, 0, 3, 0, 72, 4, 0.f)); // slot 3 emptied again
    one.send(play(0, true));
    three.send(play(0, true));
    const float a = peakOf(renderFrames(one, 4 * kStep), 2 * kStep);
    const float b = peakOf(renderFrames(three, 4 * kStep), 2 * kStep);
    CHECK(a > 0.01f);
    CHECK(b > 1.4f * a);

    // Nonsense notes are not stored.
    Engine odd;
    odd.addSynthTrack(1);
    odd.send(note(1, 0, 0, 16, 60, 1));  // no such step
    odd.send(note(1, 0, 1, 0, 128, 1));  // no such key
    odd.send(note(1, 0, 2, 0, 60, 0));   // no length
    odd.send(note(1, 0, kMaxNotes, 0, 60, 1));
    odd.send(note(1, kMaxScenes, 0, 0, 60, 1));
    odd.send(note(1, 0, 3, 0, 60, 1, std::nanf("")));
    odd.send(play(0, true));
    CHECK(peakOf(renderFrames(odd, kBar)) == 0.f);
}

static void testStopLetsGoOfNotes() {
    Engine engine;
    engine.addSynthTrack(1);
    engine.send(make(Command::SelectPreset, 1, 0, organPreset()));
    engine.send(note(1, 0, 0, 0, 60, 16));
    engine.send(play(0, true));
    CHECK(peakOf(renderFrames(engine, 2 * kStep), kStep) > 0.01f);
    engine.send(make(Command::Stop));
    renderFrames(engine, 48000);
    CHECK(peakOf(renderFrames(engine, 4800)) < 0.001f);
    // And Play does not leave an old note hanging either.
    engine.send(note(1, 0, 0, 0, 60, 16, 0.f));
    engine.send(make(Command::NoteOn, 1, 0, 72, 0, 1.f)); // a key held by hand
    engine.send(play(0, true));
    renderFrames(engine, 48000);
    CHECK(peakOf(renderFrames(engine, 4800)) < 0.001f);
}

static void testNotesFollowTheScene() {
    Engine engine;
    engine.addSynthTrack(1);
    engine.send(make(Command::SelectPreset, 1, 0, organPreset()));
    engine.send(note(1, 1, 0, 0, 60, 2)); // only scene 2 has a note
    engine.send(make(Command::SetSceneCount, 0, 0, 2));
    engine.send(play());
    CHECK(peakOf(renderFrames(engine, kBar)) == 0.f);
    CHECK(peakOf(renderFrames(engine, kBar), 0, 2 * kStep) > 0.01f);
}

// ---- adding tracks, and the scope ---------------------------------------------------

static void testTracksCanBeAddedWhileRunning() {
    Engine engine;
    renderFrames(engine, 1000); // already running
    CHECK(engine.addDrumTrack(3));
    CHECK(!engine.addDrumTrack(3));  // taken, even before the audio thread has seen it
    CHECK(!engine.addSynthTrack(3));
    CHECK(!engine.addDrumTrack(0));
    CHECK(!engine.addDrumTrack(-1) && !engine.addDrumTrack(kNumTracks));
    CHECK(engine.addSynthTrack(4));
    Command trigger = make(Command::Trigger, 3, 0, KICK, 0, 1.f);
    engine.send(trigger);
    engine.send(make(Command::NoteOn, 4, 0, 60, 0, 1.f));
    renderFrames(engine, 4000);
    CHECK(engine.trackPeak(3) > 0.05f);
    CHECK(engine.trackPeak(4) > 0.f);
    CHECK(engine.trackPeak(0) == 0.f);
    // A second drum track has its own pattern.
    engine.send(make(Command::SetStep, 3, 0, RIM, 0, 1.f));
    engine.send(play(0, true));
    renderFrames(engine, 2000);
    CHECK(engine.trackPeak(3) > 0.05f && engine.trackPeak(0) == 0.f);
}

static void testASynthAddedButNeverPlayedIsNotLeaked() {
    // Add a synth and destroy the engine before any render(): the test passes
    // by not crashing, and the leak checker (when run under one) by silence.
    Engine engine;
    CHECK(engine.addSynthTrack(5));
}

static void testScope() {
    Engine engine;
    float seen[256];
    engine.readScope(seen, 256);
    for (float s : seen) CHECK(s == 0.f);

    engine.setScopeTrack(0);
    engine.send(make(Command::Trigger, 0, 0, KICK, 0, 1.f));
    const std::vector<float> out = renderFrames(engine, 2000);
    engine.readScope(seen, 256);
    // The scope holds what the track just put out: the last 256 frames.
    bool same = true;
    for (int i = 0; i < 256; ++i) same = same && std::fabs(seen[i] - out[2 * (2000 - 256 + i)]) < 1e-6f;
    CHECK(same);
    CHECK(peakOf(std::vector<float>(seen, seen + 256)) > 0.05f);

    // It watches one track only.
    Engine other;
    other.setScopeTrack(1);
    other.send(make(Command::Trigger, 0, 0, KICK, 0, 1.f));
    renderFrames(other, 2000);
    other.readScope(seen, 256);
    for (float s : seen) CHECK(s == 0.f);
    // Asking for more than it keeps is held to what it keeps.
    std::vector<float> big(4 * kScopeSize, 7.f);
    engine.readScope(big.data(), 4 * kScopeSize);
    CHECK(big[kScopeSize] == 7.f);
}

int main() {
    testScenesPlayInOrderAndComeRound();
    testRepeatsPerScene();
    testReadOutsFollowTheSong();
    testStartFromAnyScene();
    testLoopOneScene();
    testSceneCountAndRepeatLimits();
    testAnEarlyHitBelongsToTheNextBarsScene();
    testScenesKeepSeparatePatterns();
    testNotesPlayForTheirLength();
    testChordsAndEmptySlots();
    testStopLetsGoOfNotes();
    testNotesFollowTheScene();
    testTracksCanBeAddedWhileRunning();
    testASynthAddedButNeverPlayedIsNotLeaked();
    testScope();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("song tests passed\n");
    return 0;
}
