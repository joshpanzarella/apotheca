// Tests for Twin Analog, the second synth: the Clairaudient oscillator from
// the Shapetaker Rack plugin, ported to stand alone with voices and an
// envelope.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "apotheca/engine.hpp"

using namespace apotheca;
using namespace apotheca::twin;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

struct Change { int id, value; };

// A Twin on its own, at a given rate, with settings applied over a preset.
struct Synth {
    Twin twin;
    float rate;
    explicit Synth(const std::vector<Change>& changes = {}, int preset = 0, float sampleRate = 48000.f) : rate(sampleRate) {
        twin.prepare(sampleRate);
        twin.selectPreset(preset);
        for (const Change& c : changes) twin.setSetting(c.id, c.value);
    }
    std::vector<float> render(int frames) { // interleaved L R
        std::vector<float> l(frames), r(frames), out(2 * frames);
        twin.render(l.data(), r.data(), frames);
        for (int i = 0; i < frames; ++i) { out[2 * i] = l[i]; out[2 * i + 1] = r[i]; }
        return out;
    }
};

static float peakOf(const std::vector<float>& v, int fromFrame = 0, int toFrame = -1) {
    float p = 0.f;
    const std::size_t end = toFrame < 0 ? v.size() : static_cast<std::size_t>(2 * toFrame);
    for (std::size_t i = 2 * static_cast<std::size_t>(fromFrame); i < end; ++i) p = std::fabs(v[i]) > p ? std::fabs(v[i]) : p;
    return p;
}
static double energyOf(const std::vector<float>& v, int fromFrame, int toFrame) {
    double sum = 0.0;
    for (int i = 2 * fromFrame; i < 2 * toFrame; ++i) sum += double(v[i]) * v[i];
    return sum;
}

// The length of one cycle, in frames (see phazed_test.cpp).
static double periodOf(const std::vector<float>& stereo, int minLag, int maxLag) {
    const int n = static_cast<int>(stereo.size() / 2), window = n - maxLag - 1;
    std::vector<double> score(maxLag + 2, 0.0);
    double best = -1e30;
    for (int lag = minLag; lag <= maxLag + 1; ++lag) {
        double sum = 0.0;
        for (int i = 0; i < window; ++i) sum += double(stereo[2 * i]) * stereo[2 * (i + lag)];
        score[lag] = sum;
    }
    int bestLag = minLag + 1;
    for (int lag = minLag + 1; lag <= maxLag; ++lag) best = score[lag] > best ? score[lag] : best;
    for (int lag = minLag + 1; lag <= maxLag; ++lag) {
        if (score[lag] >= 0.9 * best && score[lag] >= score[lag - 1] && score[lag] >= score[lag + 1]) { bestLag = lag; break; }
    }
    const double a = score[bestLag - 1], b = score[bestLag], c = score[bestLag + 1], d = a - 2.0 * b + c;
    return bestLag + (d != 0.0 ? 0.5 * (a - c) / d : 0.0);
}

// A plain, steady sound to measure on: one oscillator, nothing moving.
static std::vector<Change> plain(std::vector<Change> more = {}) {
    std::vector<Change> c = {{Crossfade, 0}, {VShape, 50}, {VFine, 0}, {ZFine, 0}, {FormantDepth, 0}, {Asymmetry, 0}, {Width, 50},
                             {Vintage, 0}, {Noise, 0}, {Attack, 0}, {Decay, 50}, {Sustain, 100}, {Release, 20}, {VOctave, 0}, {ZSemitone, 0},
                             {CrossSync, 0}, {ReverseSync, 0}, {Waveform, 0}, {CrossfadeMode, 0}, {Level, 0}};
    c.insert(c.end(), more.begin(), more.end());
    return c;
}

static double pitchOf(const std::vector<Change>& changes, int note, float rate = 48000.f) {
    Synth s(changes, 0, rate);
    s.twin.noteOn(note, 1.f);
    s.render(static_cast<int>(rate * 0.2f));
    return rate / periodOf(s.render(16384), 20, 1500);
}
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

static void testPresets() {
    CHECK(Twin::presetCount() == 16);
    int folders = 0;
    const char* last = "";
    for (int i = 0; i < Twin::presetCount(); ++i) {
        CHECK(Twin::presetName(i)[0] != '\0' && Twin::presetCategory(i)[0] != '\0');
        if (std::strcmp(Twin::presetCategory(i), last) != 0) ++folders;
        last = Twin::presetCategory(i);
        const TwinPatch patch = Twin::presetPatch(i);
        for (int id = 0; id < kSettings; ++id) CHECK(patch.value[id] >= settingMin(id) && patch.value[id] <= settingMax(id));
        for (int j = 0; j < i; ++j) CHECK(std::strcmp(Twin::presetName(i), Twin::presetName(j)) != 0);
    }
    CHECK(folders == 5); // each folder's presets sit together
    CHECK(Twin::presetName(-1)[0] == '\0' && Twin::presetName(Twin::presetCount())[0] == '\0');
    CHECK(settingMin(VOctave) == -2 && settingMax(VOctave) == 2);
    CHECK(settingMin(ZSemitone) == -24 && settingMax(ZSemitone) == 24);
    CHECK(settingMax(ReverseSync) == 2 && settingMax(FormantRatio) == 7 && settingMax(Oversample) == 3);
    CHECK(settingMin(Level) == -24 && settingMax(Level) == 6);
}

static void testEveryPresetPlays() {
    for (int preset = 0; preset < Twin::presetCount(); ++preset) {
        Synth s({}, preset);
        const int chord[4] = {48, 55, 60, 64};
        for (int note : chord) s.twin.noteOn(note, 1.f);
        const std::vector<float> out = s.render(24000);
        bool finite = true;
        for (float v : out) finite = finite && std::isfinite(v);
        const float peak = peakOf(out);
        if (!finite || peak < 0.02f || peak > 1.f) std::printf("  preset %d \"%s\": peak %.3f\n", preset, Twin::presetName(preset), peak);
        CHECK(finite);
        CHECK(peak > 0.02f);
        CHECK(peak <= 1.f);
    }
}

static void testInTune() {
    // A above middle C is 440 Hz, at any engine rate.
    CHECK(std::fabs(cents(pitchOf(plain(), 69), 440.0)) < 3.0);
    CHECK(std::fabs(cents(pitchOf(plain(), 69, 44100.f), 440.0)) < 3.0);
    CHECK(std::fabs(cents(pitchOf(plain(), 57), 220.0)) < 3.0);
    // V's octave setting.
    CHECK(std::fabs(cents(pitchOf(plain({{VOctave, 1}}), 69), 880.0)) < 3.0);
    CHECK(std::fabs(cents(pitchOf(plain({{VOctave, -2}}), 69), 110.0)) < 3.0);
    // Z follows the same key, offset by its semitones. All the way to Z:
    CHECK(std::fabs(cents(pitchOf(plain({{Crossfade, 100}}), 69), 440.0)) < 3.0);
    CHECK(std::fabs(cents(pitchOf(plain({{Crossfade, 100}, {ZSemitone, 7}}), 69), 440.0 * std::pow(2.0, 7.0 / 12.0))) < 3.0);
    CHECK(std::fabs(cents(pitchOf(plain({{Crossfade, 100}, {ZSemitone, -12}}), 69), 220.0)) < 3.0);
    // The oversampling setting does not move the pitch.
    for (int os = 0; os <= 3; ++os) CHECK(std::fabs(cents(pitchOf(plain({{Oversample, os}}), 69), 440.0)) < 3.0);
}

static void testEnvelope() {
    // Attack: a slow one has barely started after 20 ms; a fast one is there.
    Synth fast(plain()), slow(plain({{Attack, 70}}));
    fast.twin.noteOn(60, 1.f);
    slow.twin.noteOn(60, 1.f);
    const float f = peakOf(fast.render(960)), s = peakOf(slow.render(960));
    CHECK(f > 0.1f);
    CHECK(s < 0.2f * f);
    CHECK(peakOf(slow.render(96000)) > 0.5f * f); // but it gets there

    // Sustain: held at that share of full level, for as long as the key is.
    Synth half(plain({{Sustain, 50}, {Decay, 20}}));
    half.twin.noteOn(60, 1.f);
    half.render(48000);
    const float held = peakOf(half.render(4800));
    fast.render(48000);
    const float full = peakOf(fast.render(4800));
    CHECK(std::fabs(held / full - 0.5f) < 0.05f);
    CHECK(peakOf(half.render(48000 * 3)) > 0.9f * held); // still there three seconds on

    // Sustain at nothing: the note dies while the key is still down.
    Synth pluck(plain({{Sustain, 0}, {Decay, 30}}));
    pluck.twin.noteOn(60, 1.f);
    CHECK(peakOf(pluck.render(2400)) > 0.1f);
    pluck.render(48000);
    CHECK(peakOf(pluck.render(4800)) < 0.002f);

    // Release: short ends soon after the key is let go, long does not, and
    // both end in exact silence.
    Synth quick(plain({{Release, 10}})), lingering(plain({{Release, 90}}));
    for (Synth* x : {&quick, &lingering}) {
        x->twin.noteOn(60, 1.f);
        x->render(9600);
        x->twin.noteOff(60);
        x->render(9600);
    }
    CHECK(peakOf(quick.render(4800)) < 0.001f);
    CHECK(peakOf(lingering.render(4800)) > 0.05f);
    lingering.render(48000 * 12);
    CHECK(peakOf(lingering.render(4800)) == 0.f);
    quick.render(48000);
    CHECK(peakOf(quick.render(4800)) == 0.f);

    // Velocity is the note's level.
    Synth loud(plain()), soft(plain());
    loud.twin.noteOn(60, 1.f);
    soft.twin.noteOn(60, 0.5f);
    loud.render(9600);
    soft.render(9600);
    CHECK(std::fabs(peakOf(soft.render(4800)) / peakOf(loud.render(4800)) - 0.5f) < 0.03f);
}

static void testVoices() {
    // Eight notes are eight voices. A ninth takes the oldest, and nothing breaks.
    Synth one(plain()), eight(plain()), nine(plain());
    one.twin.noteOn(48, 1.f);
    for (int i = 0; i < 8; ++i) eight.twin.noteOn(48 + 2 * i, 1.f);
    for (int i = 0; i < 9; ++i) nine.twin.noteOn(48 + 2 * i, 1.f);
    one.render(9600);
    eight.render(9600);
    const double e1 = energyOf(one.render(24000), 0, 24000), e8 = energyOf(eight.render(24000), 0, 24000);
    CHECK(e8 > 5.0 * e1);
    const std::vector<float> out = nine.render(48000);
    bool finite = true;
    for (float v : out) finite = finite && std::isfinite(v);
    CHECK(finite);
    // The oldest (the lowest note here) was the one taken: letting go of the
    // other eight leaves silence, since note 48's voice now plays note 64.
    for (int i = 1; i < 9; ++i) nine.twin.noteOff(48 + 2 * i);
    nine.render(48000 * 2);
    CHECK(peakOf(nine.render(4800)) == 0.f);

    // The same key twice uses one voice, not two. Fill all eight, play the
    // newest key again, then let go of every key but the oldest: the oldest
    // must still be sounding, which it would not be had the repeat taken it.
    Synth again(plain());
    for (int i = 0; i < 8; ++i) again.twin.noteOn(48 + 2 * i, 1.f);
    again.render(4800);
    again.twin.noteOn(48 + 14, 1.f);
    again.render(4800);
    for (int i = 1; i < 8; ++i) again.twin.noteOff(48 + 2 * i);
    again.render(48000);
    CHECK(peakOf(again.render(4800)) > 0.05f);
    again.twin.noteOff(48);
    again.render(48000 * 2);
    CHECK(peakOf(again.render(4800)) == 0.f);

    // Nonsense is ignored.
    Synth odd(plain());
    odd.twin.noteOn(-1, 1.f);
    odd.twin.noteOn(128, 1.f);
    odd.twin.noteOn(60, 0.f);
    odd.twin.noteOn(60, std::nanf(""));
    odd.twin.noteOff(999);
    CHECK(peakOf(odd.render(4800)) == 0.f);
}

static std::vector<float> note(const std::vector<Change>& changes, const std::vector<std::pair<int, float>>& macros = {}, int key = 57) {
    Synth s(changes);
    for (const auto& m : macros) s.twin.setMacro(m.first, m.second);
    s.twin.noteOn(key, 1.f);
    return s.render(12000);
}

static void testEverySettingReachesTheSound() {
    // A patch where everything is in play: two detuned oscillators, formant
    // on, some vintage. Then each setting moved, one at a time.
    const std::vector<Change> base = {{VFine, 5}, {ZFine, 7}, {ZSemitone, 7}, {FormantDepth, 50}, {Asymmetry, 35}, {Vintage, 50},
                                      {Attack, 10}, {Decay, 30}, {Sustain, 60}, {Noise, 0}, {Oversample, 1}};
    const std::vector<float> plainSound = note(base);
    CHECK(peakOf(plainSound) > 0.02f);
    const Change moved[] = {{VOctave, 1}, {VFine, 15}, {VShape, 80}, {ZSemitone, 12}, {ZFine, 15}, {ZShape, 20}, {Crossfade, 20},
                            {CrossSync, 1}, {ReverseSync, 1}, {ReverseSync, 2}, {Waveform, 1}, {CrossfadeMode, 1}, {FormantDepth, 90},
                            {FormantRatio, 6}, {Asymmetry, 80}, {Width, 90}, {Vintage, 100}, {Noise, 50}, {Oversample, 3},
                            {Attack, 40}, {Decay, 5}, {Sustain, 10}, {Level, 3}};
    bool covered[kSettings] = {};
    for (const Change& change : moved) {
        std::vector<Change> with = base;
        with.push_back(change);
        if (note(with) == plainSound) std::printf("  setting %d at %d changed nothing\n", change.id, change.value);
        CHECK(note(with) != plainSound);
        covered[change.id] = true;
    }
    // Release only shows after the key is let go.
    const auto tail = [&](int release) {
        std::vector<Change> with = base;
        with.push_back({Release, release});
        Synth s(with);
        s.twin.noteOn(57, 1.f);
        s.render(9600);
        s.twin.noteOff(57);
        return s.render(9600);
    };
    CHECK(tail(10) != tail(60));
    covered[Release] = true;
    for (bool c : covered) CHECK(c); // no setting left untested

    // Out-of-range values are held to the limits; unknown settings are ignored.
    std::vector<Change> over = base, top = base, unknown = base;
    over.push_back({Level, 999});
    top.push_back({Level, 6});
    unknown.push_back({-1, 5});
    unknown.push_back({kSettings, 5});
    CHECK(note(over) == note(top));
    CHECK(note(unknown) == plainSound);
    // The same notes on the same patch are the same sound, every time.
    CHECK(note(base) == plainSound);
}

static void testReverseSyncAlwaysFlips() {
    // The module's CHANCE control is left out: reverse sync is not random.
    // Two runs match exactly, and it is a different sound from sync off.
    const std::vector<Change> on = plain({{ReverseSync, 1}, {Crossfade, 100}, {ZSemitone, 7}});
    CHECK(note(on) == note(on));
    CHECK(note(on) != note(plain({{Crossfade, 100}, {ZSemitone, 7}})));
    CHECK(note(plain({{ReverseSync, 2}, {Crossfade, 50}, {ZSemitone, 7}})) != note(plain({{ReverseSync, 1}, {Crossfade, 50}, {ZSemitone, 7}})));
    // It turns both of Z's copies: the left side changes as well as the right.
    const std::vector<float> flipped = note(on), straight = note(plain({{Crossfade, 100}, {ZSemitone, 7}}));
    bool leftDiffers = false, rightDiffers = false;
    for (std::size_t i = 0; i < flipped.size(); i += 2) {
        leftDiffers = leftDiffers || flipped[i] != straight[i];
        rightDiffers = rightDiffers || flipped[i + 1] != straight[i + 1];
    }
    CHECK(leftDiffers && rightDiffers);
    // Cross sync wins when both are on, as on the module.
    CHECK(note(plain({{CrossSync, 1}, {ReverseSync, 2}, {Crossfade, 100}, {ZSemitone, 7}})) == note(plain({{CrossSync, 1}, {Crossfade, 100}, {ZSemitone, 7}})));
}

static void testWidth() {
    // Width at nothing is mono: both sides the same. (Vintage off, so the
    // voices' small pan differences are off too.)
    const std::vector<float> mono = note(plain({{Width, 0}, {Crossfade, 50}, {VFine, 10}, {ZFine, 10}}));
    float apart = 0.f;
    for (std::size_t i = 0; i < mono.size(); i += 2) apart = std::fmax(apart, std::fabs(mono[i] - mono[i + 1]));
    CHECK(apart < 1e-5f);
    CHECK(peakOf(mono) > 0.05f);
    const std::vector<float> wide = note(plain({{Width, 100}, {Crossfade, 50}, {VFine, 10}, {ZFine, 10}}));
    float spread = 0.f;
    for (std::size_t i = 0; i < wide.size(); i += 2) spread = std::fmax(spread, std::fabs(wide[i] - wide[i + 1]));
    CHECK(spread > 0.05f);
}

static void testMacros() {
    const std::vector<Change> base = {{VFine, 5}, {ZFine, 7}, {ZSemitone, 12}, {FormantDepth, 40}, {Vintage, 30}, {Attack, 10}, {Sustain, 70}};
    const std::vector<float> plainSound = note(base);
    // In the middle, nothing changes.
    std::vector<std::pair<int, float>> middle;
    for (int m = 0; m < kMacros; ++m) middle.push_back({m, 0.5f});
    CHECK(note(base, middle) == plainSound);
    // Each one changes the sound at either end.
    for (int m = 0; m < kMacros; ++m) {
        if (m == MacroRelease) continue; // heard only after the key is let go; below
        CHECK(note(base, {{m, 1.f}}) != plainSound);
        CHECK(note(base, {{m, 0.f}}) != plainSound);
    }
    // BLEND: bottom is all V, top is all Z, which here is an octave up.
    const auto hz = [&](float blend) {
        Synth s(plain({{ZSemitone, 12}, {Crossfade, 50}}));
        s.twin.setMacro(Blend, blend);
        s.twin.noteOn(69, 1.f);
        s.render(9600);
        return 48000.0 / periodOf(s.render(16384), 20, 1500);
    };
    CHECK(std::fabs(cents(hz(0.f), 440.0)) < 3.0);
    CHECK(std::fabs(cents(hz(1.f), 880.0)) < 3.0);
    // ATTACK: up is a slower start.
    const double quick = energyOf(note(base, {{MacroAttack, 0.f}}), 0, 480), slow = energyOf(note(base, {{MacroAttack, 1.f}}), 0, 480);
    CHECK(slow < 0.1 * quick);
    // RELEASE: up is a longer tail.
    const auto tail = [&](float release) {
        Synth s(base);
        s.twin.setMacro(MacroRelease, release);
        s.twin.noteOn(57, 1.f);
        s.render(9600);
        s.twin.noteOff(57);
        s.render(4800);
        return energyOf(s.render(9600), 0, 9600);
    };
    CHECK(tail(1.f) > 10.0 * tail(0.f) + 1e-9);
    // WIDTH: bottom is mono.
    const std::vector<float> narrow = note(plain({{VFine, 10}, {ZFine, 10}, {Crossfade, 50}}), {{MacroWidth, 0.f}});
    float apart = 0.f;
    for (std::size_t i = 0; i < narrow.size(); i += 2) apart = std::fmax(apart, std::fabs(narrow[i] - narrow[i + 1]));
    CHECK(apart < 1e-5f);

    // Choosing a preset clears the macros; nonsense is ignored.
    Synth s({}, 3);
    s.twin.setMacro(Shape, 1.f);
    s.twin.setMacro(MacroAttack, 1.f);
    s.twin.selectPreset(3);
    s.twin.noteOn(57, 1.f);
    Synth fresh({}, 3);
    fresh.twin.noteOn(57, 1.f);
    CHECK(s.render(12000) == fresh.render(12000));
    CHECK(note(base, {{-1, 1.f}, {kMacros, 1.f}, {Shape, std::nanf("")}}) == plainSound);
}

static void testOversamplingKeepsTheLevel() {
    // Whatever the factor, a note comes out at the same loudness: the filter
    // that brings the rate back down neither adds nor takes away.
    double level[4];
    for (int os = 0; os <= 3; ++os) {
        Synth s(plain({{Oversample, os}, {VShape, 20}}));
        s.twin.noteOn(48, 1.f);
        s.render(9600);
        level[os] = std::sqrt(energyOf(s.render(24000), 0, 24000));
        CHECK(level[os] > 0.1);
    }
    for (int os = 1; os <= 3; ++os) CHECK(std::fabs(level[os] / level[0] - 1.0) < 0.05);
}

static void testInsideTheEngine() {
    Engine engine;
    CHECK(engine.addSynthTrack(1, SynthKind::Twin));
    CHECK(engine.addSynthTrack(2, SynthKind::Phazed));
    Command c;
    c.type = Command::NoteOn;
    c.track = 1;
    c.a = 60;
    c.value = 1.f;
    engine.send(c);
    std::vector<float> out(2 * 9600);
    engine.render(out.data(), 9600);
    CHECK(engine.trackPeak(1) > 0.01f);
    CHECK(engine.trackPeak(2) == 0.f && engine.trackPeak(0) == 0.f);
    // Settings, macros and presets reach it through the engine's commands.
    const float before = engine.trackPeak(1);
    c.type = Command::SetSetting;
    c.a = Level;
    c.b = -24;
    engine.send(c);
    engine.render(out.data(), 9600);
    engine.render(out.data(), 9600);
    CHECK(engine.trackPeak(1) < 0.2f * before);
    c.type = Command::SelectPreset;
    c.a = 8; // a pad: slow attack, long release
    engine.send(c);
    c.type = Command::SetMacro;
    c.a = MacroRelease;
    c.value = 0.f;
    engine.send(c);
    c.type = Command::NoteOff;
    c.a = 60;
    engine.send(c);
    engine.render(out.data(), 9600);
    for (int i = 0; i < 20; ++i) engine.render(out.data(), 9600);
    CHECK(engine.trackPeak(1) == 0.f);
}

// Not a pass or fail: what it costs, on this machine.
static void reportCost() {
    for (int depth : {0, 50})
    for (int os = 0; os <= 3; ++os) {
        Synth s({{Oversample, os}, {FormantDepth, depth}});
        for (int i = 0; i < 8; ++i) s.twin.noteOn(48 + 3 * i, 1.f);
        std::vector<float> l(256), r(256);
        const int blocks = 48000 * 3 / 256;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i) s.twin.render(l.data(), r.data(), 256);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("  cost: 8 notes, formant %s, x%d oversampling = %.1f%% of one processor core here\n", depth ? "on " : "off", 1 << os, 100.0 * seconds / 3.0);
    }
}

int main(int argc, char** argv) {
    testPresets();
    testEveryPresetPlays();
    testInTune();
    testEnvelope();
    testVoices();
    testEverySettingReachesTheSound();
    testReverseSyncAlwaysFlips();
    testWidth();
    testMacros();
    testOversamplingKeepsTheLevel();
    testInsideTheEngine();
    if (argc > 1 && std::strcmp(argv[1], "--cost") == 0) reportCost();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("twin tests passed\n");
    return 0;
}
