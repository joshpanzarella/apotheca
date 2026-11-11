// Tests for Phazed, the synth, inside Apotheca's engine. Its engine has its own tests
// for the sound itself (in its own repository); these check the joins: that
// it is in tune at the engine's sample rate, that every preset plays, and
// that the engine's commands reach it.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "apotheca/engine.hpp"

using namespace apotheca;

static std::atomic<int> failures{0}; // checks run on several threads (forEachPreset)
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

static float peakOf(const std::vector<float>& buffer, int fromFrame = 0) {
    float peak = 0.f;
    for (std::size_t i = 2 * static_cast<std::size_t>(fromFrame); i < buffer.size(); ++i) peak = std::fabs(buffer[i]) > peak ? std::fabs(buffer[i]) : peak;
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
    for (int i = 0; i < Phazed::presetCount(); ++i) {
        if (std::strcmp(Phazed::presetCategory(i), "Organs") == 0) return i;
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
    CHECK(Phazed::presetCount() >= 60);
    int folders = 0;
    const char* last = "";
    for (int i = 0; i < Phazed::presetCount(); ++i) {
        CHECK(Phazed::presetName(i)[0] != '\0');
        CHECK(Phazed::presetCategory(i)[0] != '\0');
        if (std::strcmp(Phazed::presetCategory(i), last) != 0) ++folders;
        last = Phazed::presetCategory(i);
    }
    CHECK(folders == 7); // each folder's presets sit together
    // The player only ever sees "Phazed": no name or folder carries the
    // initials or the maker of the instrument its engine was modelled on.
    bool renamed = false;
    for (int i = 0; i < Phazed::presetCount(); ++i) {
        for (const char* text : {Phazed::presetName(i), Phazed::presetCategory(i)}) {
            std::string lower = text;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            CHECK(lower.find("cz") == std::string::npos);
            CHECK(lower.find("casio") == std::string::npos);
        }
        if (std::strcmp(Phazed::presetName(i), "Phazed Strings") == 0) renamed = true;
    }
    CHECK(renamed);
    CHECK(Phazed::presetName(-1)[0] == '\0' && Phazed::presetName(Phazed::presetCount())[0] == '\0');
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
    for (int preset = 0; preset < Phazed::presetCount(); ++preset) {
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
            std::printf("  preset %d \"%s\": peak %.4f%s\n", preset, Phazed::presetName(preset), peak, finite ? "" : ", not finite");
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
    for (int preset = 0; preset < Phazed::presetCount(); ++preset) {
        if (std::strcmp(Phazed::presetCategory(preset), last) == 0) continue;
        last = Phazed::presetCategory(preset);
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

// ---- patches, settings and macros ---------------------------------------------------

static Command setting(int id, int value) {
    Command c = make(Command::SetSetting, 1, id);
    c.b = value;
    return c;
}
static Command macro(int which, float value) { return make(Command::SetMacro, 1, which, value); }

// One note of a preset, with whatever was sent first. Returns the audio.
struct Played {
    std::vector<float> held, tail;
};
static Played playNote(int preset, const std::vector<Command>& before, int note = 60) {
    Engine engine;
    engine.prepare(44100.f);
    engine.addSynthTrack(1);
    engine.send(make(Command::SelectPreset, 1, preset));
    for (const Command& c : before) engine.send(c);
    renderFrames(engine, 22050); // let the preset change and any chorus fade settle
    engine.send(make(Command::NoteOn, 1, note, 1.f));
    Played p;
    p.held = renderFrames(engine, 22050);
    engine.send(make(Command::NoteOff, 1, note));
    p.tail = renderFrames(engine, 22050);
    return p;
}
static double energyOf(const std::vector<float>& v, int fromFrame, int toFrame) {
    double sum = 0.0;
    for (int i = 2 * fromFrame; i < 2 * toFrame; ++i) sum += double(v[i]) * v[i];
    return sum;
}
// How bright: the energy of the sample-to-sample changes over the energy itself.
static double brightnessOf(const std::vector<float>& v) {
    double changes = 0.0, total = 1e-12;
    for (std::size_t i = 2; i < v.size(); i += 2) {
        changes += double(v[i] - v[i - 2]) * (v[i] - v[i - 2]);
        total += double(v[i]) * v[i];
    }
    return changes / total;
}
static int firstPresetWhere(bool (*wanted)(const PhazedPatch&)) {
    for (int i = 0; i < Phazed::presetCount(); ++i) {
        if (wanted(Phazed::presetPatch(i))) return i;
    }
    return -1;
}

static void testPatchesAreInRange() {
    using namespace phazed;
    CHECK(kSettings == 129);
    CHECK(settingMin(Octave) == -1 && settingMax(Octave) == 1);
    CHECK(settingMin(Level) == -24 && settingMax(Level) == 6);
    CHECK(settingMin(lineSetting(0, Wave1)) == 1 && settingMin(lineSetting(1, Wave1)) == 0);
    CHECK(settingMax(lineSetting(1, Wave2)) == 8 && settingMax(lineSetting(0, AmpKeyFollow)) == 9);
    CHECK(settingMax(envRate(1, PitchEnv, 7)) == 99 && settingMax(envLevel(0, AmpEnv, 0)) == 99);
    CHECK(settingMin(envSustain(0, WaveEnv)) == 0 && settingMax(envSustain(0, WaveEnv)) == 8);
    CHECK(settingMin(envEnd(1, AmpEnv)) == 1 && settingMax(envEnd(1, AmpEnv)) == 8);
    CHECK(envEnd(1, PitchEnv) == kSettings - 1); // the numbering fills the patch exactly
    for (int preset = 0; preset < Phazed::presetCount(); ++preset) {
        const PhazedPatch patch = Phazed::presetPatch(preset);
        for (int id = 0; id < kSettings; ++id) {
            CHECK(patch.value[id] >= settingMin(id) && patch.value[id] <= settingMax(id));
        }
    }
}

static void testAPatchIsTheWholeSound() {
    // Start on one preset and set every setting to another preset's values,
    // one at a time. The result must be that other preset, exactly.
    const int from = 0, to = organPreset();
    const PhazedPatch target = Phazed::presetPatch(to);
    std::vector<Command> edits;
    for (int id = 0; id < phazed::kSettings; ++id) edits.push_back(setting(id, target.value[id]));
    const Played built = playNote(from, edits);
    const Played chosen = playNote(to, {});
    CHECK(built.held == chosen.held);
    CHECK(built.tail == chosen.tail);
    CHECK(peakOf(chosen.held) > 0.01f);
    CHECK(playNote(from, {}).held != chosen.held);
}

static void testVibratoSettingsAllReachTheSound() {
    using namespace phazed;
    const int preset = organPreset();
    const auto with = [&](int wave, int delay, int rate, int depth) {
        return playNote(preset, {setting(VibratoWave, wave), setting(VibratoDelay, delay), setting(VibratoRate, rate), setting(VibratoDepth, depth)}).held;
    };
    const std::vector<float> base = with(0, 0, 40, 50);
    CHECK(with(3, 0, 40, 50) != base); // wave
    CHECK(with(0, 60, 40, 50) != base); // delay
    CHECK(with(0, 0, 80, 50) != base); // rate
    CHECK(with(0, 0, 40, 90) != base); // depth
}

static void testSettingsAreHeldToTheirLimits() {
    const int preset = organPreset();
    CHECK(playNote(preset, {setting(phazed::Level, 999)}).held == playNote(preset, {setting(phazed::Level, 6)}).held);
    CHECK(playNote(preset, {setting(-1, 5), setting(phazed::kSettings, 5)}).held == playNote(preset, {}).held);
    // The level is the loudness, in decibels: 12 dB down is a quarter.
    const float loud = peakOf(playNote(preset, {setting(phazed::Level, 0)}).held, 0);
    const float soft = peakOf(playNote(preset, {setting(phazed::Level, -12)}).held, 0);
    CHECK(std::fabs(soft / loud - 0.251f) < 0.02f);
}

static void testMacrosInTheMiddleChangeNothing() {
    std::vector<Command> middle;
    for (int m = 0; m < phazed::kMacros; ++m) middle.push_back(macro(m, 0.5f));
    for (int preset : {0, organPreset(), Phazed::presetCount() - 1}) {
        CHECK(playNote(preset, middle).held == playNote(preset, {}).held);
    }
}

// A check made on every preset, shared out over the machine's cores: each
// note played is its own engine, so they do not get in each other's way.
// (One after the other this was 23 of the test's 25 seconds.)
template <typename Body>
static void forEachPreset(Body body) {
    std::atomic<int> next{0};
    std::vector<std::thread> crew;
    const unsigned count = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    for (unsigned i = 0; i < count; ++i) {
        crew.emplace_back([&] {
            for (int preset = next.fetch_add(1); preset < Phazed::presetCount(); preset = next.fetch_add(1)) body(preset);
        });
    }
    for (std::thread& worker : crew) worker.join();
}

static void testEachMacroDoesItsJob() {
    using namespace phazed;
    // A preset that holds its note, with two lines, and one with a moving tone.
    const int organ = organPreset();
    const int twoLine = firstPresetWhere([](const PhazedPatch& p) { return p.value[LineSelect] >= 2 && p.value[envSustain(0, AmpEnv)] > 0; });
    CHECK(twoLine >= 0);
    const Played plain = playNote(twoLine, {});

    // TONE: up is brighter, down is duller.
    const double dull = brightnessOf(playNote(twoLine, {macro(Tone, 0.f)}).held);
    const double bright = brightnessOf(playNote(twoLine, {macro(Tone, 1.f)}).held);
    CHECK(bright > 1.2 * dull);

    // MOTION changes how the tone moves, so the note is a different sound.
    CHECK(playNote(twoLine, {macro(Motion, 1.f)}).held != plain.held);
    CHECK(playNote(twoLine, {macro(Motion, 0.f)}).held != plain.held);

    // ATTACK: up is a slower start, so less sound in the first 30 ms.
    const double fast = energyOf(playNote(organ, {macro(Attack, 0.f)}).held, 0, 1323);
    const double slow = energyOf(playNote(organ, {macro(Attack, 1.f)}).held, 0, 1323);
    CHECK(slow < 0.5 * fast);

    // RELEASE: up is a longer tail after the key is let go.
    const double shortTail = energyOf(playNote(organ, {macro(Release, 0.f)}).tail, 4410, 22050);
    const double longTail = energyOf(playNote(organ, {macro(Release, 1.f)}).tail, 4410, 22050);
    CHECK(longTail > 4.0 * shortTail + 1e-9);

    // DETUNE: the second line moves against the first.
    CHECK(playNote(twoLine, {macro(Detune, 1.f)}).held != plain.held);
    // ...and both ways: down is as much a change as up, and a different one.
    // (It used to do nothing below the middle on a sound with no detune.)
    CHECK(playNote(twoLine, {macro(Detune, 0.f)}).held != plain.held);
    CHECK(playNote(twoLine, {macro(Detune, 0.f)}).held != playNote(twoLine, {macro(Detune, 1.f)}).held);
    CHECK(playNote(twoLine, {macro(Detune, 0.25f)}).held != plain.held);
    CHECK(playNote(twoLine, {macro(Detune, 0.25f)}).held != playNote(twoLine, {macro(Detune, 0.f)}).held);
    CHECK(playNote(twoLine, {macro(Detune, 0.5f)}).held == plain.held); // the middle leaves the preset alone
    // On every two-line preset, not just this one.
    std::atomic<int> checked{0};
    forEachPreset([&](int preset) {
        if (Phazed::presetPatch(preset).value[LineSelect] < 2) return; // one line: there is nothing to detune against
        ++checked;
        const auto as = playNote(preset, {}).held;
        const auto down = playNote(preset, {macro(Detune, 0.f)}).held, up = playNote(preset, {macro(Detune, 1.f)}).held;
        CHECK(down != as);
        CHECK(up != as);
        CHECK(down != up); // flat is not sharp
    });
    CHECK(checked >= 3);
    // A sound with ONE line has nothing to detune against. Off the middle it
    // is given a twin of its line, so the macro does something there too:
    // both ways, differently, and at about the loudness it had.
    std::atomic<int> ones{0};
    forEachPreset([&](int preset) {
        if (Phazed::presetPatch(preset).value[LineSelect] >= 2) return;
        ++ones;
        const auto as = playNote(preset, {}).held;
        const auto down = playNote(preset, {macro(Detune, 0.f)}).held, up = playNote(preset, {macro(Detune, 1.f)}).held;
        CHECK(down != as && up != as && down != up);
        CHECK(playNote(preset, {macro(Detune, 0.5f)}).held == as); // the middle: the preset, untouched
        const double before = energyOf(as, 0, static_cast<int>(as.size() / 2));
        for (float at : {0.4f, 0.75f, 1.f}) {
            const auto bent = at == 1.f ? up : playNote(preset, {macro(Detune, at)}).held;
            const double after = energyOf(bent, 0, static_cast<int>(bent.size() / 2));
            if (before > 1e-9 && (after < 0.4 * before || after > 2.5 * before)) std::printf("preset %d at %.2f: %.2f times the energy\n", preset, at, after / before);
            CHECK(before <= 1e-9 || (after > 0.4 * before && after < 2.5 * before)); // within 4 dB
        }
    });
    CHECK(ones >= 10);

    // VIBRATO: up makes the pitch wobble, even on a sound that had none.
    const int still = firstPresetWhere([](const PhazedPatch& p) { return p.value[VibratoDepth] == 0 && p.value[envSustain(0, AmpEnv)] > 0; });
    CHECK(still >= 0);
    CHECK(playNote(still, {macro(Vibrato, 1.f)}).held != playNote(still, {}).held);
    CHECK(playNote(still, {macro(Vibrato, 0.f)}).held == playNote(still, {}).held); // none to take away
    // Even when the sound's vibrato speed is set to nothing: the macro gives it one.
    const std::vector<Command> noVibrato = {setting(VibratoRate, 0), setting(VibratoDepth, 0)};
    std::vector<Command> turnedUp = noVibrato;
    turnedUp.push_back(macro(Vibrato, 1.f));
    CHECK(playNote(organ, turnedUp).held != playNote(organ, noVibrato).held);

    // CHORUS: top is on (the two sides differ), bottom is off (they match).
    const auto sidesDiffer = [](const std::vector<float>& v) {
        for (std::size_t i = 0; i < v.size(); i += 2) if (v[i] != v[i + 1]) return true;
        return false;
    };
    CHECK(sidesDiffer(playNote(organ, {setting(ChorusOn, 0), macro(Chorus, 1.f)}).held));
    CHECK(!sidesDiffer(playNote(organ, {setting(ChorusOn, 1), macro(Chorus, 0.f)}).held));
    // In the middle it is whatever the patch says.
    CHECK(sidesDiffer(playNote(organ, {setting(ChorusOn, 1)}).held));
    CHECK(!sidesDiffer(playNote(organ, {setting(ChorusOn, 0)}).held));

    // OCTAVE: top is an octave up, bottom an octave down.
    const int level = firstPresetWhere([](const PhazedPatch& p) { return p.value[Octave] == 0 && p.value[envSustain(0, AmpEnv)] > 0 && p.value[LineSelect] == 0 && p.value[VibratoDepth] == 0; });
    CHECK(level >= 0);
    const auto hz = [&](float octaveMacro) {
        const Played p = playNote(level, {macro(MacroOctave, octaveMacro)}, 69);
        return 44100.0 / periodOf(std::vector<float>(p.held.begin() + 2 * 8000, p.held.end()), 20, 1200);
    };
    const double middle = hz(0.5f);
    CHECK(std::fabs(1200.0 * std::log2(hz(1.f) / middle) - 1200.0) < 10.0);
    CHECK(std::fabs(1200.0 * std::log2(hz(0.f) / middle) + 1200.0) < 10.0);
}

static void testChoosingAPresetClearsTheMacros() {
    const int preset = organPreset();
    Engine engine;
    engine.prepare(44100.f);
    engine.addSynthTrack(1);
    engine.send(make(Command::SelectPreset, 1, preset));
    engine.send(macro(phazed::Attack, 1.f));
    engine.send(macro(phazed::Tone, 0.f));
    engine.send(make(Command::SelectPreset, 1, preset)); // choose it again
    renderFrames(engine, 22050);
    engine.send(make(Command::NoteOn, 1, 60, 1.f));
    CHECK(renderFrames(engine, 22050) == playNote(preset, {}).held);
    // Nonsense macros are ignored.
    CHECK(playNote(preset, {macro(-1, 1.f), macro(phazed::kMacros, 1.f), macro(phazed::Tone, std::nanf(""))}).held == playNote(preset, {}).held);
}

// ---- GLIDE on Phazed (the CZ's own portamento, switched on a note at a time) -------------------
// How often the sound crosses zero going up, in a stretch of it: its pitch, near enough.
static int crossings(const std::vector<float>& left, int from, int frames) {
    int ups = 0;
    for (int i = from + 1; i < from + frames; ++i) ups += left[static_cast<std::size_t>(i - 1)] <= 0.f && left[static_cast<std::size_t>(i)] > 0.f;
    return ups;
}
static std::vector<float> renderSynth(Phazed& synth, int frames) {
    std::vector<float> left(static_cast<std::size_t>(frames)), right(static_cast<std::size_t>(frames));
    synth.render(left.data(), right.data(), frames);
    return left;
}
static int presetNamed(const char* name) {
    for (int i = 0; i < Phazed::presetCount(); ++i)
        if (std::strcmp(Phazed::presetName(i), name) == 0) return i;
    return -1;
}

// The glide comes from the key the engine names, not from whatever key was last played by hand.
static void testAGlideComesFromTheKeyItIsGiven() {
    const auto early = [](int handKey) { // crossings from 10 to 40 ms into a note gliding up from A3 to A4
        Phazed synth;
        synth.prepare(44100.f);
        synth.selectPreset(organPreset());
        synth.noteOn(57, 1.f);
        renderSynth(synth, 13230);
        synth.noteOff(57);
        renderSynth(synth, 4410);
        if (handKey >= 0) {
            synth.noteOn(handKey, 1.f); // a key on the USB keyboard, or X on the piano roll
            renderSynth(synth, 2205);
            synth.noteOff(handKey);
            renderSynth(synth, 8820);
        }
        synth.noteOnFrom(69, 1.f, 57, 0.06f);
        const std::vector<float> out = renderSynth(synth, 13230);
        return std::pair<int, int>(crossings(out, 441, 1323), crossings(out, 8820, 1323));
    };
    const std::pair<int, int> alone = early(-1), afterHigh = early(93), afterSame = early(69);
    CHECK(alone.second > 20);                       // A4, settled: some 26 in 30 ms
    CHECK(alone.first < 0.9 * alone.second);        // it comes up from below
    CHECK(afterHigh.first < 0.9 * afterHigh.second); // and still from below after a high key by hand: not down from that key
    CHECK(afterSame.first < 0.9 * afterSame.second); // and after its own key by hand (the CZ would not have glided at all)
    if (failures) std::printf("  glide from: alone %d/%d, after a high key %d/%d, after its own %d/%d\n", alone.first, alone.second, afterHigh.first, afterHigh.second, afterSame.first, afterSame.second);
}

// A gliding note that is given a slot which is still ringing starts a moment later, from the
// CZ's render. It still glides: on a sound with a long tail, every note of a run, not the first seven.
// And a plain note that starts late in the same way stays plain.
static void testAGlideSurvivesAWaitForItsSlot() {
    const int harp = presetNamed("Harp");
    CHECK(harp >= 0);
    if (harp < 0) return;
    const int kNotes = 24, kApart = 5512; // sixteenths at 120
    const auto run = [&](bool glide, bool lastPlain) {
        Phazed synth;
        synth.prepare(44100.f);
        synth.selectPreset(harp);
        std::vector<int> early;
        for (int n = 0; n < kNotes; ++n) {
            const int key = n % 2 ? 81 : 57, before = n % 2 ? 57 : 81;
            if (glide && n > 0 && !(lastPlain && n >= kNotes - 4)) synth.noteOnFrom(key, 1.f, before, 0.06f);
            else synth.noteOn(key, 1.f);
            const std::vector<float> a = renderSynth(synth, kApart * 95 / 100);
            synth.noteOff(key);
            renderSynth(synth, kApart - kApart * 95 / 100);
            early.push_back(crossings(a, 441, 1764));
        }
        return early;
    };
    const std::vector<int> plain = run(false, false), glided = run(true, false), mixed = run(true, true);
    int slid = 0;
    for (int n = kNotes - 12; n < kNotes - 4; ++n) slid += std::abs(glided[static_cast<std::size_t>(n)] - plain[static_cast<std::size_t>(n)]) >= 3;
    CHECK(slid == 8); // every one of them, long after the silent slots ran out
    for (int n = kNotes - 3; n < kNotes; ++n) CHECK(std::abs(mixed[static_cast<std::size_t>(n)] - plain[static_cast<std::size_t>(n)]) < 8 && std::abs(mixed[static_cast<std::size_t>(n)] - glided[static_cast<std::size_t>(n)]) > 12); // plain notes after gliding ones: plain
    if (failures) {
        for (int n = 0; n < kNotes; ++n) std::printf("  note %d: plain %d, glided %d, mixed %d\n", n, plain[static_cast<std::size_t>(n)], glided[static_cast<std::size_t>(n)], mixed[static_cast<std::size_t>(n)]);
    }
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
    testPatchesAreInRange();
    testAPatchIsTheWholeSound();
    testVibratoSettingsAllReachTheSound();
    testSettingsAreHeldToTheirLimits();
    testMacrosInTheMiddleChangeNothing();
    testEachMacroDoesItsJob();
    testChoosingAPresetClearsTheMacros();
    testAGlideComesFromTheKeyItIsGiven();
    testAGlideSurvivesAWaitForItsSlot();
    if (argc > 1 && std::strcmp(argv[1], "--cost") == 0) reportCost();
    if (failures) {
        std::printf("%d check(s) failed\n", failures.load());
        return 1;
    }
    std::printf("phazed tests passed\n");
    return 0;
}
