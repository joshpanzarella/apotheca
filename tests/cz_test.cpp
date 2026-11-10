// Tests for the CZ synth inside Apotheca's engine. EasyCZ has its own tests
// for the sound itself (in its own repository); these check the joins: that
// it is in tune at the engine's sample rate, that every preset plays, and
// that the engine's commands reach it.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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

static Command make(Command::Type type, int track, int a = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.value = value;
    return c;
}

static std::vector<float> renderFrames(Engine& engine, int frames) {
    std::vector<float> out(2 * frames, 99.f);
    engine.render(out.data(), frames);
    return out;
}

static float peakOf(const std::vector<float>& buffer) {
    float peak = 0.f;
    for (float s : buffer) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    return peak;
}

// The length of one cycle, in frames: the delay at which the sound best
// matches itself.
static double periodOf(const std::vector<float>& stereo, int minLag, int maxLag) {
    const int n = static_cast<int>(stereo.size() / 2);
    const int window = n - maxLag - 1;
    double best = -1e30;
    int bestLag = 0;
    std::vector<double> score(maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag) {
        double sum = 0.0;
        for (int i = 0; i < window; ++i) sum += double(stereo[2 * i]) * stereo[2 * (i + lag)];
        score[lag] = sum;
    }
    for (int lag = minLag + 1; lag <= maxLag; ++lag) best = score[lag] > best ? score[lag] : best;
    // A sound also matches itself two and three cycles on. Take the first
    // delay that is a peak and nearly as good as the best: one cycle.
    for (int lag = minLag + 1; lag <= maxLag; ++lag) {
        if (score[lag] >= 0.9 * best && score[lag] >= score[lag - 1] && score[lag] >= score[lag + 1]) { bestLag = lag; break; }
    }
    // Refine between whole frames with a parabola through the three points.
    const double a = score[bestLag - 1], b = score[bestLag], c = score[bestLag + 1];
    const double denom = a - 2.0 * b + c;
    return bestLag + (denom != 0.0 ? 0.5 * (a - c) / denom : 0.0);
}

// A plain, steady preset to measure pitch on: the first in the Organs folder.
static int organPreset() {
    for (int i = 0; i < CzSynth::presetCount(); ++i) {
        if (std::strcmp(CzSynth::presetCategory(i), "Organs") == 0) return i;
    }
    return 0;
}

// The pitch a note plays at, in Hz, at a given engine sample rate.
static double pitchOf(float sampleRate, int note) {
    Engine engine;
    engine.prepare(sampleRate);
    engine.addSynthTrack(1);
    engine.send(make(Command::SelectPreset, 1, organPreset()));
    engine.send(make(Command::NoteOn, 1, note, 1.f));
    renderFrames(engine, static_cast<int>(sampleRate * 0.4f)); // past the attack
    const std::vector<float> steady = renderFrames(engine, 16384);
    return sampleRate / periodOf(steady, 20, 1200);
}

// How far `hz` is from the nearest octave of `target`, in cents.
static double centsFromOctaveOf(double hz, double target) {
    const double octaves = std::log2(hz / target);
    return 1200.0 * (octaves - std::round(octaves));
}

static void testPresetsAreThere() {
    CHECK(CzSynth::presetCount() >= 60);
    int folders = 0;
    const char* last = "";
    for (int i = 0; i < CzSynth::presetCount(); ++i) {
        CHECK(CzSynth::presetName(i)[0] != '\0');
        CHECK(CzSynth::presetCategory(i)[0] != '\0');
        if (std::strcmp(CzSynth::presetCategory(i), last) != 0) ++folders;
        last = CzSynth::presetCategory(i);
    }
    CHECK(folders == 7); // each folder's presets sit together
    CHECK(CzSynth::presetName(-1)[0] == '\0' && CzSynth::presetName(CzSynth::presetCount())[0] == '\0');
}

static void testASynthTrackIsAddedOnce() {
    Engine engine;
    CHECK(!engine.addSynthTrack(0));  // the drums are there
    CHECK(engine.addSynthTrack(1));
    CHECK(!engine.addSynthTrack(1));  // already a synth
    CHECK(!engine.addSynthTrack(-1));
    CHECK(!engine.addSynthTrack(kNumTracks));
    CHECK(peakOf(renderFrames(engine, 2000)) == 0.f); // silent until played
}

static void testNotesPlayAndStop() {
    Engine engine;
    engine.prepare(44100.f);
    engine.addSynthTrack(1);
    engine.send(make(Command::SelectPreset, 1, organPreset()));
    engine.send(make(Command::NoteOn, 1, 60, 1.f));
    const std::vector<float> out = renderFrames(engine, 8000);
    CHECK(peakOf(out) > 0.02f);
    CHECK(engine.trackPeak(1) > 0.f);
    CHECK(engine.trackPeak(0) == 0.f); // the drums did not sound
    engine.send(make(Command::NoteOff, 1, 60));
    renderFrames(engine, 44100 * 3);
    CHECK(peakOf(renderFrames(engine, 8000)) < 0.001f);

    // Notes only reach a synth track.
    Engine drumsOnly;
    drumsOnly.send(make(Command::NoteOn, 0, 60, 1.f));
    drumsOnly.send(make(Command::NoteOn, 5, 60, 1.f));
    drumsOnly.send(make(Command::SelectPreset, 0, 3));
    CHECK(peakOf(renderFrames(drumsOnly, 8000)) == 0.f);

    // Nonsense is ignored.
    Engine odd;
    odd.addSynthTrack(1);
    odd.send(make(Command::NoteOn, 1, -1, 1.f));
    odd.send(make(Command::NoteOn, 1, 128, 1.f));
    odd.send(make(Command::NoteOn, 1, 60, 0.f));
    odd.send(make(Command::NoteOn, 1, 60, std::nanf("")));
    odd.send(make(Command::NoteOff, 1, 999));
    odd.send(make(Command::SelectPreset, 1, -1));
    odd.send(make(Command::SelectPreset, 1, 9999));
    CHECK(peakOf(renderFrames(odd, 8000)) == 0.f);
}

static void testInTuneAtTheEnginesRate() {
    // A above middle C is 440 Hz. A preset may sit an octave up or down, so
    // the check is against the nearest octave of 440, within 6 cents (about a
    // sixteenth of a semitone).
    const double native = pitchOf(44100.f, 69);
    CHECK(std::fabs(centsFromOctaveOf(native, 440.0)) < 6.0);
    // The same note at other rates, which go through the rate conversion.
    const double at48 = pitchOf(48000.f, 69);
    CHECK(std::fabs(1200.0 * std::log2(at48 / native)) < 3.0);
    const double at32 = pitchOf(32000.f, 69);
    CHECK(std::fabs(1200.0 * std::log2(at32 / native)) < 3.0);
    // An octave up is twice the pitch.
    const double high = pitchOf(48000.f, 81);
    CHECK(std::fabs(1200.0 * std::log2(high / at48) - 1200.0) < 6.0);
}

static void testEveryPresetPlays() {
    Engine engine;
    engine.prepare(44100.f);
    engine.addSynthTrack(1);
    for (int preset = 0; preset < CzSynth::presetCount(); ++preset) {
        engine.send(make(Command::SelectPreset, 1, preset));
        renderFrames(engine, 2000); // the fade a preset change makes
        engine.send(make(Command::NoteOn, 1, 60, 1.f));
        engine.send(make(Command::NoteOn, 1, 67, 1.f));
        const std::vector<float> out = renderFrames(engine, 22050);
        float peak = 0.f;
        bool finite = true;
        for (float s : out) {
            finite = finite && std::isfinite(s);
            peak = std::fabs(s) > peak ? std::fabs(s) : peak;
        }
        if (!finite || peak <= 0.005f || peak > 1.f) {
            std::printf("  preset %d \"%s\": peak %.4f%s\n", preset, CzSynth::presetName(preset), peak, finite ? "" : ", not finite");
        }
        CHECK(finite);
        CHECK(peak > 0.005f);
        CHECK(peak <= 1.f);
        engine.send(make(Command::NoteOff, 1, 60));
        engine.send(make(Command::NoteOff, 1, 67));
    }
}

static void testPresetsSoundDifferent() {
    // The same note on the first preset of each folder: seven different sounds.
    std::vector<std::vector<float>> sounds;
    const char* last = "";
    for (int preset = 0; preset < CzSynth::presetCount(); ++preset) {
        if (std::strcmp(CzSynth::presetCategory(preset), last) == 0) continue;
        last = CzSynth::presetCategory(preset);
        Engine engine;
        engine.prepare(44100.f);
        engine.addSynthTrack(1);
        engine.send(make(Command::SelectPreset, 1, preset));
        renderFrames(engine, 2000);
        engine.send(make(Command::NoteOn, 1, 60, 1.f));
        sounds.push_back(renderFrames(engine, 8000));
    }
    CHECK(sounds.size() == 7);
    for (std::size_t a = 0; a < sounds.size(); ++a)
        for (std::size_t b = 0; b < a; ++b) CHECK(sounds[a] != sounds[b]);
}

static void testChords() {
    Engine one, eight;
    for (Engine* e : {&one, &eight}) {
        e->prepare(44100.f);
        e->addSynthTrack(1);
        e->send(make(Command::SelectPreset, 1, organPreset()));
    }
    one.send(make(Command::NoteOn, 1, 60, 1.f));
    const int chord[8] = {48, 55, 60, 64, 67, 71, 74, 79};
    for (int note : chord) eight.send(make(Command::NoteOn, 1, note, 1.f));
    const float a = peakOf(renderFrames(one, 20000));
    const float b = peakOf(renderFrames(eight, 20000));
    CHECK(b > 1.5f * a); // eight notes are much more sound than one
    CHECK(b <= 1.f);     // and the ceiling still holds
}

static void testDrumsAndSynthTogether() {
    Engine engine;
    engine.prepare(44100.f);
    engine.addSynthTrack(1);
    Command hit;
    hit.type = Command::Trigger;
    hit.track = 0;
    hit.a = 0;
    hit.value = 1.f;
    engine.send(hit);
    engine.send(make(Command::NoteOn, 1, 48, 1.f));
    renderFrames(engine, 4000);
    CHECK(engine.trackPeak(0) > 0.05f);
    CHECK(engine.trackPeak(1) > 0.f);
    // The track level reaches the synth too.
    Command level;
    level.type = Command::SetTrackLevel;
    level.track = 1;
    level.value = 0.f;
    engine.send(level);
    renderFrames(engine, 4000);
    CHECK(engine.trackPeak(1) == 0.f);
}

// Not a pass or fail: how much processor the synth takes, on this machine.
static void reportCost() {
    for (int notes : {1, 4, 8}) {
        Engine engine;
        engine.prepare(44100.f);
        engine.addSynthTrack(1);
        engine.send(make(Command::SelectPreset, 1, organPreset())); // a sound that holds, so every note keeps working
        const int chord[8] = {48, 55, 60, 64, 67, 71, 74, 79};
        for (int i = 0; i < notes; ++i) engine.send(make(Command::NoteOn, 1, chord[i], 1.f));
        std::vector<float> out(2 * 256);
        const int blocks = 44100 * 5 / 256;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i) engine.render(out.data(), 256);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("  cost: %d note%s held = %.1f%% of one processor core here\n", notes, notes == 1 ? "" : "s", 100.0 * seconds / 5.0);
    }
}

int main(int argc, char** argv) {
    testPresetsAreThere();
    testASynthTrackIsAddedOnce();
    testNotesPlayAndStop();
    testInTuneAtTheEnginesRate();
    testEveryPresetPlays();
    testPresetsSoundDifferent();
    testChords();
    testDrumsAndSynthTogether();
    if (argc > 1 && std::strcmp(argv[1], "--cost") == 0) reportCost();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("cz tests passed\n");
    return 0;
}
