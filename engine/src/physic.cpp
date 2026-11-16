// PHYSIC (docs/plans/physic-plan.md): the voices, the steal, the strike, the tick and the presets.
// What each part is and why: physic.hpp, physic_rule.hpp and engine/src/physic/.

// No multiply and add joined into one rounding (a compiler for the handheld's ARM does that by
// itself; the desk's does not): so the handheld plays the desk's bits.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include "apotheca/physic.hpp"

#include <cmath>
#include <cstring>
#include <vector>

#include "apotheca/slide.hpp"
#include "physic/body.hpp"
#include "physic/modes.hpp"
#include "physic/string.hpp"
#include "physic/strike.hpp"

namespace apotheca {

using namespace physic;

namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- the presets ------------------------------------------------------------------------------
// The plan's section 5. Preset 0 is MARIMBA, the new track's sound. The 24 of version 1 set nothing of
// batch 10 (each 0, off: their sound is pinned, physic_test testVersionOne); the 8 batch 10 added are
// at the end of their folders. Names and folders are the owner's call; nobody has heard any.
struct Preset {
    const char* name;
    const char* category;
    int value[kSettings];
};
// MODEL STRIKE HARD POS  DECAY MAT STRETCH REL  VEL OCT FINE WIDTH LEVEL  DICE DPOS DHARD DTUNE DDECAY  PAIR MOTOR BODY BMIX BUZZ
const Preset kPresets[] = {
    {"Marimba", "MALLETS", {kMarimba, 1, 45, 80, 58, 30, 50, 100, 70, 0, 0, 30, 0, 20, 30, 20, 0, 10}},
    {"Soft Marimba", "MALLETS", {kMarimba, 1, 15, 85, 60, 25, 50, 100, 70, 0, 0, 30, 0, 20, 30, 20, 0, 10}},
    {"Xylophone", "MALLETS", {kXylo, 1, 80, 75, 46, 35, 50, 100, 70, 1, 0, 30, 0, 15, 30, 20, 0, 10}},
    {"Vibes Still", "MALLETS", {kVibes, 1, 55, 75, 82, 90, 50, 64, 70, 0, 0, 30, -2, 10, 20, 10, 0, 0}},
    {"Glockenspiel", "MALLETS", {kGlock, 1, 90, 70, 72, 95, 50, 100, 70, 1, 0, 30, -3, 10, 20, 10, 0, 0}},
    // the motor at 4.5 Hz on the tubes; the damper bar lets go in 0.4 s
    {"Vibes", "MALLETS", {kVibes, 1, 55, 75, 82, 90, 50, 65, 70, 0, 0, 30, -2, 10, 20, 10, 0, 0, 0, 85, kBodyTubes, 60, 0}},
    // a glockenspiel's bars, a felt hammer, in a box; a damper
    {"Celesta", "MALLETS", {kGlock, 1, 70, 70, 60, 95, 50, 60, 70, 1, 0, 20, -3, 10, 20, 10, 0, 0, 3, 0, kBodyBox, 60, 0}},
    {"Steel", "STRINGS", {kString, 0, 75, 25, 62, 55, 15, 40, 70, 0, 0, 0, 0, 10, 30, 20, 10, 0}},
    {"Harp", "STRINGS", {kString, 0, 45, 60, 64, 40, 10, 100, 70, 0, 0, 0, 0, 10, 30, 20, 5, 0}},
    {"Koto", "STRINGS", {kString, 0, 85, 14, 55, 50, 20, 45, 70, 0, 0, 0, 0, 10, 20, 20, 10, 0}},
    {"Pizz", "STRINGS", {kString, 0, 40, 45, 35, 30, 10, 30, 70, 0, 0, 0, 0, 10, 30, 20, 10, 10}},
    {"Plucked Bass", "STRINGS", {kString, 0, 35, 30, 55, 35, 25, 35, 70, -1, 0, 0, 2, 10, 20, 20, 5, 0}},
    {"Nylon", "STRINGS", {kString, 0, 30, 20, 55, 20, 8, 45, 70, 0, 0, 0, 0, 10, 30, 20, 5, 0, 0, 0, kBodyGuitar, 70, 0}},
    // a mallet on courses of two strings, 5 cents apart, on a soundboard
    {"Dulcimer", "STRINGS", {kString, 1, 70, 15, 62, 45, 20, 100, 70, 0, 0, 20, -6, 10, 30, 20, 5, 0, 5, 0, kBodyBox, 40, 0}},
    {"Twelve", "STRINGS", {kString, 0, 80, 22, 60, 50, 15, 40, 70, 0, 0, 20, -2, 10, 30, 20, 10, 0, 7, 0, kBodyGuitar, 50, 0}},
    {"Plucked Bass Box", "STRINGS", {kString, 0, 35, 30, 55, 35, 25, 35, 70, -1, 0, 0, 0, 10, 20, 20, 5, 0, 0, 0, kBodyBox, 60, 0}},
    {"Music Box", "TINES", {kKalimba, 0, 90, 20, 62, 85, 50, 100, 70, 1, 0, 0, -2, 10, 20, 10, 5, 0}},
    // tines on a box, a little rattle on them (an mbira's caps)
    {"Kalimba", "TINES", {kKalimba, 0, 60, 15, 60, 70, 50, 100, 70, 0, 0, 20, -2, 15, 30, 20, 5, 0, 0, 0, kBodyBox, 70, 20}},
    {"Church Bell", "BELLS", {kBell, 1, 70, 30, 95, 100, 50, 100, 70, 0, 0, 0, -3, 0, 0, 0, 0, 0}},
    {"Handbell", "BELLS", {kBell, 1, 60, 40, 78, 95, 50, 75, 70, 0, 0, 0, -3, 10, 20, 10, 0, 0}},
    {"Tubular Bell", "BELLS", {kChime, 1, 85, 5, 88, 98, 50, 100, 70, 0, 0, 0, -3, 0, 0, 0, 0, 0}},
    {"Wine Glass", "BELLS", {kGlass, 1, 75, 20, 80, 100, 50, 100, 70, 0, 0, 0, -2, 0, 0, 0, 0, 0}},
    {"Crystal", "BELLS", {kGlass, 1, 85, 30, 76, 100, 75, 100, 70, 0, 0, 0, -3, 0, 0, 0, 0, 0}},
    {"Woodblock", "WOOD AND SKIN", {kWood, 1, 85, 50, 18, 5, 50, 100, 70, 0, 0, 0, 0, 10, 20, 10, 0, 0}},
    {"Temple Blocks", "WOOD AND SKIN", {kWood, 1, 70, 70, 28, 20, 40, 100, 70, 0, 0, 0, 0, 10, 20, 10, 0, 0}},
    {"Log Drum", "WOOD AND SKIN", {kWood, 1, 45, 60, 38, 15, 30, 100, 70, -1, 0, 0, 0, 10, 20, 10, 0, 0}},
    {"Tom", "WOOD AND SKIN", {kDrum, 1, 55, 30, 40, 40, 50, 100, 70, -1, 0, 0, 0, 15, 30, 20, 0, 10}},
    // a hand on a skin: a burst of noise 6 ms long, where and how long rolled a little each time
    {"Hand Drum", "WOOD AND SKIN", {kDrum, 2, 85, 35, 35, 40, 50, 100, 70, 0, 0, 0, 3, 30, 40, 40, 10, 10, 0, 0, kBodyNone, 0, 0}},
    {"Rain", "DICE", {kGlock, 1, 80, 60, 60, 90, 50, 100, 70, 1, 0, 50, -4, 80, 100, 100, 30, 100}},
    {"Wind Chime", "DICE", {kChime, 1, 80, 20, 85, 98, 50, 100, 70, 0, 0, 40, -5, 90, 100, 100, 100, 100}},
    {"Loose Strings", "DICE", {kString, 0, 70, 30, 60, 50, 15, 40, 70, 0, 0, 20, 0, 70, 80, 60, 60, 40}},
    {"Drunk Marimba", "DICE", {kMarimba, 1, 45, 80, 58, 30, 50, 100, 70, 0, 0, 30, 0, 100, 100, 100, 100, 100}},
};
const int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);

// Every model's row, worked out once for the program and shared by every PHYSIC track.
struct Tables {
    ModelRow row[kModels];
    Tables() {
        for (int m = 0; m < kModels; ++m) row[m] = modelRow(m);
    }
};
const Tables kTables;

// ---- the voice's constants (SELF) ---------------------------------------------------------------
constexpr float kModalGain = 0.3f;            // the most a struck note's modes ring at, all added
constexpr float kPluckGain = 0.55f;           // a plucked string's noise
constexpr float kStringMalletGain = 0.9f;     // a mallet on a string: the most of its push's height
constexpr float kSleepPeak = 1e-6f;           // -120 dB over 64 samples: asleep
constexpr float kTakeOverPeak = 0.01f;        // -40 dB: a glide from a voice this loud takes it over
constexpr int kPeakTicks = 4;                 // the peak is read over 64 samples
constexpr double kStealFadeSeconds = 0.003;   // a stolen voice fades out over this
constexpr double kDampAllSeconds = 0.03;      // allNotesOff (Play, panic): the T60 every voice is damped to
constexpr double kReachSeconds = 0.005;       // a let go reaches its RELEASE over this
constexpr double kSettleSeconds = 0.02;       // DECAY and MATERIAL turned: a ringing note slides over this
constexpr double kRestrikeSeconds = 0.002;    // a ringing string struck again is damped this long first...
constexpr float kRestrikeKeep = 0.3f;         // ...down to this much of itself
constexpr double kLevelSeconds = 0.005;       // a take-over's level, LEVEL turned: slide over this
constexpr float kMostVoiceGain = 4.f;         // a re-strike's push is never more than this over the voice's level
constexpr int kQueue = 16;                    // strikes waiting for the top of render() (one moment)
constexpr double kMostMadeUp = 4.0;           // a soft mallet's darkening made up for, at most (12 dB)
constexpr double kNoiseModalGain = 0.18;      // NOISE into the modes: what the sizes they are LIKELY to ring at add up to
constexpr double kNoiseStringGain = 0.2;      // NOISE into a string: about as loud as a pluck
constexpr std::uint32_t kBuzzSalt = 0xB0771E5u; // BUZZ's noise is another hash of the note's seed than its strike's

double tiltOfPluck(double ratio, double hardness) { return std::pow(ratio < 1.0 ? 1.0 : ratio, -(1.0 - hardness / 100.0)); } // a finger: SELF

std::uint32_t liveSeed(std::uint32_t n) { return chordHash(n * 0x2545F491u + 0x6A09E667u); }

} // namespace

struct Physic::State {
    struct Voice {
        bool asleep = true, fading = false, released = false, string = false;
        int key = -1, model = kMarimba;
        std::uint32_t age = 0;  // samples since its strike: its ticks are counted from here
        std::uint32_t seed = 0;
        double pitch = 60.0;    // its sounding pitch (held), the glide not counted
        Slide slide;            // a glide: how far it still is from that pitch
        float off = 0.f;        // the glide's semitones, as last worked out (on its tick)
        float amp = 1.f, ampTo = 1.f, ampStep = 0.f; // its velocity's level (a take-over slides it)
        float panL = 0.7071068f, panR = 0.7071068f;
        int fadeLeft = 0;       // a stolen voice: samples of its fade left
        double decayScale = 1.0;                     // the dice's
        double aNow = 1.0, aTo = 1.0, bNow = 0.0, bTo = 0.0, relNow = 0.0, relTo = 0.0;
        int aTicks = 0, bTicks = 0, relTicks = 0;    // ticks of each slide left
        double stretch = 50.0;  // STRETCH as its modes were tuned
        unsigned generation = 0;
        // The modes.
        int modes = 0;
        double baseHz[kModesMost] = {};              // at its pitch, no glide
        float cw[kModesMost] = {}, sw[kModesMost] = {}, radius[kModesMost] = {};
        ModeBank bank;
        int src[kModesMost] = {};                    // which of the model's modes each is (PAIR's twins after the model's own)
        // Batch 10, each as the strike took it (PAIR and TUBES are how the object is made).
        double pairCents = 0.0;                      // PAIR: 0, or its two halves this far apart
        double tube = 0.0;                           // TUBES: 0, or BODY MIX's share
        bool motorOn = false;                        // MOTOR: its turn, the level at this tick's start and end
        double motorPhase = 0.0;
        float motorG0 = 1.f, motorG1 = 1.f;
        float buzzGap = 0.f, most = 0.f;             // BUZZ: its gap over the loudest so far (0: off), and that loudest
        // The string (and PAIR's second).
        StringLoop str, str2;
        double dispersion = 0.0;
        int dampTicks = 0;      // a re-strike's damping: ticks left
        // The strike.
        Push push;
        float peakRun = 0.f, peak = 0.f;             // the voice's loudest over this 64 samples, and the last
    };
    Voice voice[kSlots];
    std::vector<float> lines; // every slot's string line, one after another, then every slot's second (PAIR)
    int lineSize = 0;
    PhysicPatch patch;
    float macro[kMacros];
    float eff[kSettings];     // each setting as the macros bend it
    float sampleRate = 48000.f;
    unsigned generation = 1;  // goes up when a setting or a macro moves: a voice's tick looks
    float levelNow = 1.f, levelTo = 1.f, levelStep = 0.f;
    double motorHzNow = 0.0;  // MOTOR and BUZZ as the settings say (worked out when one is turned, not on a tick)
    float buzzGapNow = 0.f;
    BodyBank body;            // BODY's resonances (GUITAR, BOX), one for the track
    int bodyKind = kBodyNone;
    float bodyMixNow = 0.f, bodyMixTo = 0.f, bodyMixStep = 0.f;
    struct Queued {
        int note = 0, from = -1;
        float velocity = 0.f, seconds = 0.f;
        std::uint32_t seed = 0;
        float eff[kSettings]; // the settings as they were when it was played (a macro may move before render())
    };
    Queued queue[kQueue];
    int queued = 0;
    std::uint32_t nextSeed = 0, liveCount = 0;
    bool seedGiven = false;
    long ticks = 0, strikes = 0, tunings = 0;
    std::uint32_t lastSeed = 0;
    Rolled lastRolled;
    float scratch[kTick], scratch2[kTick], pushed[kTick];

    void refresh();
    void makeLines(float rate);
    int settleTicks(double seconds) const {
        const int t = static_cast<int>(std::lround(seconds * sampleRate / kTick));
        return t < 1 ? 1 : t;
    }
    // A strike's or a voice's pieces.
    void settle();
    Voice* pick(int key, bool& again);
    void strike(Voice& v, const Queued& q, bool again);
    void takeOver(Voice& v, const Queued& q);
    void letGo(Voice& v, double t60);
    void sleep(Voice& v);
    void tick(Voice& v);
    void tuneModes(Voice& v, bool exact, bool decay = true);
    void tuneString(Voice& v, bool filter, bool stiff, int which = 0);
    void strikeGains(Voice& v, double q, double along, double hardness, bool pluck, double noiseLength);
    void modesHz(Voice& v);
    void tuneOne(Voice& v, StringLoop& str, double& dispersion, double cents, bool filter, bool stiff);
    void motorTick(Voice& v);
    double pitchOf(int key, double tuneCents) const;
};

void Physic::State::refresh() {
    const int* p = patch.value;
    for (int id = 0; id < kSettings; ++id) eff[id] = static_cast<float>(p[id]);
    const auto bent = [&](int id, int m) { eff[id] = macroBent(static_cast<float>(p[id]), macro[m], static_cast<float>(settingMin(id)), static_cast<float>(settingMax(id))); };
    bent(Hardness, MacroTone);
    bent(Position, MacroPosition);
    bent(Decay, MacroDecay);
    bent(Material, MacroMaterial);
    bent(Stretch, MacroStretch);
    bent(Release, MacroRelease);
    bent(Dice, MacroDice);
    eff[Octave] = static_cast<float>(clampi(p[Octave] + macroOctave(macro[MacroOctave]), -3, 3));
    levelTo = std::pow(10.f, static_cast<float>(p[Level]) / 20.f);
    if (levelTo != levelNow) levelStep = std::fabs(levelTo - levelNow) / static_cast<float>(kLevelSeconds * sampleRate);
    motorHzNow = motorHz(p[Motor]);
    buzzGapNow = static_cast<float>(buzzGap(p[Buzz]));
    // BODY: a box's resonances are set when it changes; NONE or TUBES lets the last box's ring go to
    // nothing over the level's slide, then stops running it.
    const int bk = clampi(p[Body], 0, kBodies - 1);
    const bool box = bk == kBodyGuitar || bk == kBodyBox;
    if (box && bk != bodyKind) {
        body.set(bk, sampleRate);
        bodyKind = bk;
    }
    bodyMixTo = box ? static_cast<float>(p[BodyMix]) / 100.f : 0.f;
    if (bodyMixTo != bodyMixNow) bodyMixStep = std::fabs(bodyMixTo - bodyMixNow) / static_cast<float>(kLevelSeconds * sampleRate);
    ++generation;
}

void Physic::State::makeLines(float rate) {
    // Long enough for the lowest string (kStringLowest) at this rate, a power of two.
    const double longest = rate / hzOf(kStringLowest) + 64.0;
    int size = 64;
    while (size < longest) size *= 2;
    if (size == lineSize) return;
    lineSize = size;
    lines.assign(static_cast<std::size_t>(size) * kSlots * 2, 0.f);
    for (int i = 0; i < kSlots; ++i) {
        voice[i].str.line = lines.data() + static_cast<std::size_t>(size) * i;
        voice[i].str.mask = size - 1;
        voice[i].str2.line = lines.data() + static_cast<std::size_t>(size) * (kSlots + i);
        voice[i].str2.mask = size - 1;
    }
}

double Physic::State::pitchOf(int key, double tuneCents) const {
    return heldPitch(key + 12.0 * eff[Octave] + eff[Fine] / 100.0 + tuneCents / 100.0, static_cast<int>(eff[Model]) == kString);
}

// ---- the strike ---------------------------------------------------------------------------------

Physic::State::Voice* Physic::State::pick(int key, bool& again) {
    again = false;
    for (Voice& v : voice)
        if (!v.asleep && !v.fading && v.key == key) {
            again = true;
            return &v;
        }
    int sounding = 0;
    for (Voice& v : voice) sounding += !v.asleep && !v.fading;
    if (sounding < kVoices)
        for (Voice& v : voice)
            if (v.asleep) return &v;
    // Eight sound: the quietest gives way (never one inside its strike, unless all eight are). It fades.
    Voice* victim = nullptr;
    for (int pass = 0; pass < 2 && !victim; ++pass)
        for (Voice& v : voice) {
            if (v.asleep || v.fading) continue;
            if (pass == 0 && v.push.on(v.age)) continue;
            if (!victim || v.peak < victim->peak) victim = &v;
        }
    victim->fading = true;
    victim->fadeLeft = static_cast<int>(std::lround(kStealFadeSeconds * sampleRate));
    for (Voice& v : voice)
        if (v.asleep) return &v;
    // Both fade slots are fading: the one furthest on is cut short.
    Voice* oldest = nullptr;
    for (Voice& v : voice)
        if (v.fading && &v != victim && (!oldest || v.fadeLeft < oldest->fadeLeft)) oldest = &v;
    sleep(*oldest);
    return oldest;
}

// Each mode's share of a strike: its shape where it is struck, its own weight, the push's
// spectrum; then all of them set so the sizes the modes ring at add up to kModalGain (a strike's
// place and its mallet move the colour, not how loud it is, and a voice never peaks over
// kModalGain, however its modes line up).
//
// NOISE (`noiseLength` samples, else 0): a mode fed noise rings at a size that is a matter of luck,
// so the sizes it is LIKELY to ring at are what add up, to kNoiseModalGain: a mode of gain g and
// radius r after N samples of noise of power 1/3 under a half-sine (half its power) is likely
// g sqrt(sum of r^2n over the N, / 6) (a random walk that dies away as it goes).
void Physic::State::strikeGains(Voice& v, double q, double along, double hardness, bool pluck, double noiseLength) {
    const ModelRow& row = kTables.row[v.model];
    const double glide = std::pow(2.0, v.off / 12.0);
    const int paired = v.modes - row.modes; // PAIR: the first `paired` modes and their twins share a gain
    const int tubed = row.played >= 0 ? row.played : 0;
    double gain[kModesMost] = {}, sum = 0.0, plain = 0.0;
    for (int k = 0; k < v.modes; ++k) {
        const int m = v.src[k];
        double shape = 1.0;
        switch (row.shape) {
        case kShapeFreeBar: shape = freeBarShape(kFreeBar[row.beam[m] < 6 ? row.beam[m] : 5], along); break;
        case kShapeTine: shape = tineShape(kTineBar[row.beam[m] < 4 ? row.beam[m] : 3], along); break;
        case kShapeSkin: shape = skinShape(row.beam[m], row.zero[m], along); break;
        default: shape = tiltShape(row.ratio[m], along); break;
        }
        double g = shape * row.weight[m];
        if (pluck) g *= tiltOfPluck(row.ratio[m], hardness);
        if (m < paired) g *= 0.5;
        if (v.tube > 0.0 && m == tubed) g *= 1.0 + kTubeLouder * v.tube;
        gain[k] = g;
        if (noiseLength > 0.0) {
            const double r2 = static_cast<double>(v.radius[k]) * v.radius[k];
            const double walk = r2 < 1.0 ? (1.0 - std::pow(r2, noiseLength)) / (1.0 - r2) : noiseLength;
            sum += std::fabs(g * v.bank.out[k]) * std::sqrt(walk / 6.0);
        } else {
            sum += std::fabs(g * bumpSpectrum(q, 6.283185307179586 * v.baseHz[k] * glide / sampleRate) * v.bank.out[k]);
        }
        plain += std::fabs(g * v.bank.out[k]);
    }
    if (noiseLength > 0.0) {
        const double norm = sum > 1e-12 ? kNoiseModalGain / sum : 0.0;
        for (int k = 0; k < kModesMost; ++k) v.bank.in[k] = k < v.modes && v.bank.out[k] > 0.f ? static_cast<float>(gain[k] * norm) : 0.f;
        return;
    }
    // A soft mallet's darkening is made up for, but by kMostMadeUp at most: a yarn mallet on a high
    // bar is dull AND quiet.
    const double heard = sum > plain / kMostMadeUp ? sum : plain / kMostMadeUp;
    const double norm = heard > 1e-9 ? kModalGain / heard : 0.0;
    for (int k = 0; k < kModesMost; ++k) v.bank.in[k] = k < v.modes && v.bank.out[k] > 0.f ? static_cast<float>(gain[k] * norm) : 0.f;
}

// The modes' frequencies at the voice's pitch (no glide): the model's own, the first kPairedModes of
// them split PAIR's cents apart, their twins after the model's own (`src` says which is which).
void Physic::State::modesHz(Voice& v) {
    const ModelRow& row = kTables.row[v.model];
    const double hz = hzOf(v.pitch);
    const int paired = v.pairCents > 0.0 ? (row.modes < kPairedModes ? row.modes : kPairedModes) : 0;
    v.modes = row.modes + paired;
    for (int k = 0; k < row.modes; ++k) {
        v.src[k] = k;
        v.baseHz[k] = hz * stretched(row.ratio[k], v.stretch);
        if (k < paired) v.baseHz[k] *= pairRatio(v.pairCents, 0);
    }
    for (int j = 0; j < paired; ++j) {
        v.src[row.modes + j] = j;
        v.baseHz[row.modes + j] = hz * stretched(row.ratio[j], v.stretch) * pairRatio(v.pairCents, 1);
    }
}

void Physic::State::strike(Voice& v, const Queued& q, bool again) {
    ++strikes;
    lastSeed = q.seed;
    const int model = clampi(static_cast<int>(eff[Model]), 0, kModels - 1);
    const double dice = eff[Dice] / 100.0;
    Rolled r;
    r.position = clampf(eff[Position] + static_cast<float>(diceRoll(q.seed, kRollsPosition) * kDicePositionMost * dice * eff[DicePosition] / 100.0), 0.f, 100.f);
    r.hardness = clampf(eff[Hardness] + static_cast<float>(diceRoll(q.seed, kRollsHardness) * kDiceHardnessMost * dice * eff[DiceHardness] / 100.0), 0.f, 100.f);
    r.tuneCents = diceRoll(q.seed, kRollsTune) * kDiceTuneCents * dice * eff[DiceTune] / 100.0;
    r.decayScale = 1.0 + diceRoll(q.seed, kRollsDecay) * kDiceDecayMost * dice * eff[DiceDecay] / 100.0;
    lastRolled = r;
    const double velocityAmount = eff[Velocity] / 100.0;
    const float level = static_cast<float>(velocityLevel(q.velocity, velocityAmount));
    // A ringing key struck again keeps its voice: the new hit is added to the ringing (a string is
    // damped first), at the new hit's own level. Otherwise the voice starts afresh.
    const double pair = eff[Pair];
    again = again && v.model == model && v.pairCents == pair;
    float push = 1.f;
    if (again) {
        push = clampf(level / (v.amp > 1e-3f ? v.amp : 1e-3f), 0.f, kMostVoiceGain);
    } else {
        if (v.string) v.str.clear();
        if (v.string && v.pairCents > 0.0) v.str2.clear();
        v.bank.clear();
        v.pairCents = pair;
        v.motorOn = false;
        v.motorPhase = 0.0;
        v.motorG0 = v.motorG1 = 1.f;
        v.most = 0.f;
        v.model = model;
        v.string = model == kString;
        v.amp = v.ampTo = level;
        v.ampStep = 0.f;
        v.peak = v.peakRun = 0.f;
        // WIDTH: low keys left, high keys right, as a marimba stands (equal power).
        const double pan = clampf(eff[Width] / 100.f * static_cast<float>(q.note - 60) / 36.f, -1.f, 1.f);
        v.panL = static_cast<float>(std::cos((pan + 1.0) * 0.7853981633974483));
        v.panR = static_cast<float>(std::sin((pan + 1.0) * 0.7853981633974483));
    }
    v.asleep = v.fading = false;
    v.released = false;
    v.relNow = v.relTo = 0.0;
    v.relTicks = 0;
    v.key = q.note;
    v.seed = q.seed;
    v.age = 0;
    v.decayScale = r.decayScale;
    v.aNow = v.aTo = rateOfT60(decaySeconds(eff[Decay]) * r.decayScale);
    v.bNow = v.bTo = materialB(eff[Material]);
    v.aTicks = v.bTicks = 0;
    v.stretch = eff[Stretch];
    v.generation = generation;
    if (!again) {
        v.pitch = pitchOf(q.note, r.tuneCents);
        v.slide.stop();
        v.off = 0.f;
    }
    if (!again && q.from >= 0) { // a glide: struck at `from`, and slides to its own pitch
        const double from = pitchOf(q.from, 0.0);
        if (from != v.pitch && q.seconds > 0.f) {
            v.slide.semitones = static_cast<float>(from - v.pitch);
            v.slide.perFrame = std::fabs(v.slide.semitones) / (q.seconds * sampleRate);
            v.off = v.slide.semitones;
        }
    }
    const int strikeKind = clampi(static_cast<int>(std::lround(eff[Strike])), 0, kStrikes - 1);
    const bool pluck = strikeKind == kPluck;
    const double noiseLength = strikeKind == kNoise ? std::fmax(1.0, std::round(noiseSeconds(r.hardness) * sampleRate)) : 0.0;
    const double along = positionAlong(r.position);
    if (!again) v.tube = clampi(static_cast<int>(eff[Body]), 0, kBodies - 1) == kBodyTubes ? eff[BodyMix] / 100.0 : 0.0;
    if (!v.string) {
        if (!again) modesHz(v);
        tuneModes(v, v.off == 0.f);
        v.push = Push();
        if (noiseLength > 0.0) { // NOISE: a burst under a half-sine, as loud as its gains say
            strikeGains(v, 0.0, along, r.hardness, false, noiseLength);
            v.push.kind = Push::Burst;
            v.push.seed = q.seed;
            v.push.length = static_cast<float>(noiseLength);
            v.push.perLength = 1.f / v.push.length;
            v.push.size = push;
            v.push.from = 0;
            v.push.to = static_cast<std::uint32_t>(noiseLength) + 1u;
        } else {
            const double contact = pluck ? kPluckContact : contactSeconds(r.hardness, q.velocity, velocityAmount);
            double tau = contact * sampleRate / kBumpOfContact; // in samples
            if (tau < 0.25) tau = 0.25;
            const double bq = std::exp(-1.0 / tau);
            strikeGains(v, bq, along, r.hardness, pluck, 0.0);
            v.push.kind = Push::Bump;
            v.push.q = static_cast<float>(bq);
            v.push.size = static_cast<float>(push * (1.0 - bq) * (1.0 - bq) / bq); // an area of 1: a mode rings at its gain times the bump's spectrum
            v.push.from = 0;
            v.push.to = static_cast<std::uint32_t>(std::ceil(kBumpLong * tau)) + 1u;
        }
        v.bank.groups = (v.modes + 3) / 4;
    } else {
        if (!again) v.dampTicks = 0;
        tuneString(v, true, true);
        const double period = sampleRate / hzOf(v.pitch + v.off);
        v.push = Push();
        v.push.seed = q.seed;
        v.push.comb = static_cast<float>(along * period);
        if (noiseLength > 0.0) {
            // NOISE into the loop: each pass round adds to what is there, a random walk that dies by the
            // loop's keep a pass, so it is made as loud as a pluck by how many passes it lasts.
            const double keep = std::exp(-v.aNow / hzOf(v.pitch)), keep2 = keep * keep, passes = noiseLength / period;
            const double walk = keep2 < 1.0 ? (1.0 - std::pow(keep2, passes)) / (1.0 - keep2) : passes;
            v.push.kind = Push::Burst;
            v.push.length = static_cast<float>(noiseLength);
            v.push.perLength = 1.f / v.push.length;
            v.push.size = static_cast<float>(kNoiseStringGain / std::sqrt(walk > 1.0 ? walk : 1.0)) * push;
        } else if (pluck) {
            v.push.kind = Push::Noise;
            v.push.length = static_cast<float>(period);
            // HARDNESS: a finger's soft pluck is noise held a few samples at a time; a plectrum's, every sample.
            const double soft = 1.0 - r.hardness / 100.0;
            v.push.grain = static_cast<float>(1.0 + 7.0 * soft * soft);
            v.push.size = kPluckGain * push;
        } else {
            const double contact = contactSeconds(r.hardness, q.velocity, velocityAmount) * sampleRate;
            v.push.kind = Push::Pulse;
            v.push.length = static_cast<float>(contact < 1.0 ? 1.0 : contact);
            v.push.size = static_cast<float>(kStringMalletGain * std::sqrt(std::fmin(1.0, period / (4.0 * contact)))) * push;
        }
        if (v.pairCents > 0.0) v.push.size *= 0.5f; // PAIR: each string gets half, so the two begin as loud as one
        // Struck again while it rings: damped kRestrikeSeconds first, then plucked.
        std::uint32_t wait = 0;
        if (again) {
            v.dampTicks = settleTicks(kRestrikeSeconds);
            wait = static_cast<std::uint32_t>(v.dampTicks * kTick);
            v.str.settle();
            v.str.damp = std::pow(kRestrikeKeep, 1.f / static_cast<float>(wait));
            if (v.pairCents > 0.0) {
                v.str2.settle();
                v.str2.damp = v.str.damp;
            }
        }
        v.push.from = wait;
        v.push.to = wait + static_cast<std::uint32_t>(std::ceil(v.push.length + v.push.comb)) + 1u;
    }
}

// A lone glide from a voice that rings loud: the voice slides to the new key, with no new strike.
void Physic::State::takeOver(Voice& v, const Queued& q) {
    const double velocityAmount = eff[Velocity] / 100.0;
    const double tune = diceRoll(q.seed, kRollsTune) * kDiceTuneCents * eff[Dice] / 100.0 * eff[DiceTune] / 100.0;
    const double now = v.pitch + v.slide.semitones;
    v.key = q.note;
    v.seed = q.seed;
    v.pitch = heldPitch(q.note + 12.0 * eff[Octave] + eff[Fine] / 100.0 + tune / 100.0, v.string);
    v.slide.semitones = static_cast<float>(now - v.pitch);
    v.slide.perFrame = q.seconds > 0.f ? std::fabs(v.slide.semitones) / (q.seconds * sampleRate) : std::fabs(v.slide.semitones);
    if (!v.string) modesHz(v);
    v.released = false; // a new note: the damper is lifted
    v.relNow = v.relTo = 0.0;
    v.relTicks = 0;
    v.ampTo = static_cast<float>(velocityLevel(q.velocity, velocityAmount));
    v.ampStep = std::fabs(v.ampTo - v.amp) / static_cast<float>(kLevelSeconds * sampleRate);
    v.generation = 0; // worked out again on its next tick, the glide's first place with it
}

// Strikes waiting since the last render() are one moment: a glide takes a voice over only when it
// is the one note of its moment to name that `from` (a chord under GLIDE FROM TOP names one key for
// every note: then every key strikes and slides, and none is a silent take-over).
void Physic::State::settle() {
    const int n = queued;
    queued = 0;
    float now[kSettings];
    std::memcpy(now, eff, sizeof now);
    for (int i = 0; i < n; ++i) {
        const Queued& q = queue[i];
        std::memcpy(eff, q.eff, sizeof eff); // (each strike with the settings it was played with)
        if (q.from >= 0) {
            int naming = 0;
            for (int j = 0; j < n; ++j) naming += queue[j].from == q.from;
            Voice* ringing = nullptr;
            for (Voice& v : voice)
                if (!v.asleep && !v.fading && v.key == q.from) ringing = &v;
            if (naming == 1 && ringing && ringing->peak > kTakeOverPeak && !ringing->push.on(ringing->age)) {
                takeOver(*ringing, q);
                continue;
            }
        }
        bool again = false;
        Voice* v = pick(q.note, again);
        strike(*v, q, again);
    }
    std::memcpy(eff, now, sizeof eff);
}

void Physic::State::letGo(Voice& v, double t60) {
    v.released = true;
    v.relTo = rateOfT60(t60);
    v.relTicks = settleTicks(kReachSeconds);
}

void Physic::State::sleep(Voice& v) {
    v.asleep = true;
    v.fading = false;
    v.key = -1;
    v.bank.clear();
    if (v.string) v.str.clear();
    if (v.string && v.pairCents > 0.0) v.str2.clear();
    v.push = Push();
    v.slide.stop();
    v.peak = v.peakRun = 0.f;
}

// ---- the tick -----------------------------------------------------------------------------------

// The modes' turn and shrink a sample, from their frequencies now (with the glide) and the damping
// law. Exact (double) at rest, so a note is in tune; from polynomials while a glide moves. `decay`:
// the law's numbers moved (else the shrink is worked out again only where it hangs on the
// frequency: b over 0).
void Physic::State::tuneModes(Voice& v, bool exact, bool decay) {
    ++tunings;
    const bool shrink = decay || v.bNow > 0.0;
    const double rate = sampleRate, twoPi = 6.283185307179586;
    const double q = v.off == 0.f ? 1.0 : std::pow(2.0, v.off / 12.0);
    const int played = kTables.row[v.model].played, tubed = played >= 0 ? played : 0;
    for (int k = 0; k < kModesMost; ++k) {
        if (k >= v.modes) {
            v.bank.c[k] = v.bank.s[k] = v.bank.out[k] = 0.f;
            continue;
        }
        const double hz = v.baseHz[k] * q;
        const double fade = nyquistFade(hz, rate);
        if (fade == 0.0) { // above 0.45 of the sample rate: silent, and not run on
            v.bank.c[k] = v.bank.s[k] = v.bank.out[k] = v.bank.in[k] = 0.f;
            v.bank.x[k] = v.bank.y[k] = 0.f;
            continue;
        }
        const double w = twoPi * hz / rate;
        if (exact) {
            v.cw[k] = static_cast<float>(std::cos(w));
            v.sw[k] = static_cast<float>(std::sin(w));
        } else {
            sinCos(static_cast<float>(w), v.sw[k], v.cw[k]);
        }
        if (shrink || v.radius[k] == 0.f) {
            double r = v.aNow + v.bNow * hz * hz;
            if (v.tube > 0.0 && v.src[k] == tubed) r /= 1.0 + kTubeLonger * v.tube; // TUBES: the played mode rings longer
            if (v.relNow > r) r = v.relNow;
            v.radius[k] = static_cast<float>(radiusOf(r, rate));
        }
        v.bank.c[k] = v.radius[k] * v.cw[k];
        v.bank.s[k] = v.radius[k] * v.sw[k];
        v.bank.out[k] = static_cast<float>(fade);
    }
}

// The string's loop at its pitch now: the filter from the law (`filter`), the stiffness (`stiff`),
// the tuning. (A glide moves the tuning only: the filter and the stiffness are worked out again when
// it lands.)
// `which`: PAIR's first string alone (1) or its second (2), else both. (A glide's tick between its
// start and its landing retunes them by turns, a tick each, so two strings cost about one there.)
void Physic::State::tuneString(Voice& v, bool filter, bool stiff, int which) {
    ++tunings;
    if (v.pairCents > 0.0) { // PAIR: two strings, half its cents either side of the note
        if (which != 2) tuneOne(v, v.str, v.dispersion, -0.5 * v.pairCents, filter, stiff);
        double second = 0.0;
        if (which != 1) tuneOne(v, v.str2, second, 0.5 * v.pairCents, filter, stiff);
    } else {
        tuneOne(v, v.str, v.dispersion, 0.0, filter, stiff);
    }
}
void Physic::State::tuneOne(Voice& v, StringLoop& str, double& dispersion, double cents, bool filter, bool stiff) {
    const double f0 = hzOf(cents == 0.0 ? v.pitch + v.off : v.pitch + v.off + cents / 100.0);
    double a = v.aNow, b = v.bNow;
    if (v.tube > 0.0) {
        // TUBES: the fundamental rings longer; the partial the loop filter is fitted at dies as it did.
        const double longer = a / (1.0 + kTubeLonger * v.tube);
        double f1 = kLoopFitHarmonic * f0;
        if (f1 > 0.4 * sampleRate) f1 = 0.4 * sampleRate;
        if (f1 > f0 * 1.01) b += (a - longer) / (f1 * f1 - f0 * f0);
        a = longer;
    }
    if (v.relNow > a) a = v.relNow;
    if (filter) str.setFilter(loopFilterOf(a, b, f0, sampleRate), dcPole(f0, sampleRate)); // (a glide's tick keeps the filter: its tuning alone)
    if (stiff) {
        dispersion = dispersionOf(stiffnessOf(v.stretch), f0, sampleRate);
        str.disp = static_cast<float>(dispersion);
    }
    str.retune(tuningOf(f0, sampleRate, str.pole, str.disp));
}

// MOTOR on the tick: the level at the tick's start and end (a sample at a time between). Its turn
// starts at the strike, or where MOTOR is turned on; turned off, the level goes back to full over a tick.
void Physic::State::motorTick(Voice& v) {
    const double hz = motorHzNow;
    const auto levelAt = [](double turn) { // 1 - D (1 - cos 2 pi t) / 2 = 1 - D sin^2 (pi t)
        const float sn = sinHalfTurn(static_cast<float>(turn));
        return 1.f - static_cast<float>(kMotorDepth) * sn * sn;
    };
    if (hz > 0.0) {
        if (!v.motorOn) v.motorPhase = 0.0;
        v.motorG0 = v.motorOn ? v.motorG1 : 1.f; // (where the last tick ended)
        v.motorPhase += hz * kTick / sampleRate;
        if (v.motorPhase >= 1.0) v.motorPhase -= 1.0;
        v.motorG1 = levelAt(v.motorPhase);
        v.motorOn = true;
    } else if (v.motorOn) {
        v.motorG0 = v.motorG1;
        v.motorG1 = 1.f;
        v.motorOn = v.motorG0 != 1.f;
    }
}

void Physic::State::tick(Voice& v) {
    ++ticks;
    bool decay = false, pitch = false, stiffer = false;
    if (v.generation != generation) { // a setting or a macro has moved since its last tick
        v.generation = generation;
        const double aTo = rateOfT60(decaySeconds(eff[Decay]) * v.decayScale), bTo = materialB(eff[Material]);
        if (aTo != v.aTo) v.aTo = aTo, v.aTicks = settleTicks(kSettleSeconds);
        if (bTo != v.bTo) v.bTo = bTo, v.bTicks = settleTicks(kSettleSeconds);
        if (eff[Stretch] != v.stretch) { // the modes retune at once (and the string's stiffness)
            stiffer = true;
            v.stretch = eff[Stretch];
            if (!v.string) modesHz(v);
            pitch = true;
        }
        decay = true;
    }
    // The slides: each lands exactly on its last tick.
    const auto slide = [](double& now, double to, int& left) {
        if (left <= 0) return false;
        now = --left == 0 ? to : now + (to - now) / (left + 1);
        return true;
    };
    decay = slide(v.aNow, v.aTo, v.aTicks) | decay;
    decay = slide(v.bNow, v.bTo, v.bTicks) | decay;
    decay = slide(v.relNow, v.relTo, v.relTicks) | decay;
    const float off = v.slide.take(kTick);
    if (off != v.off) {
        v.off = off;
        pitch = true;
    }
    motorTick(v);
    v.buzzGap = buzzGapNow;
    if (v.string) {
        const bool pair = v.pairCents > 0.0;
        if (v.dampTicks > 0 && v.age > 0 && --v.dampTicks == 0) {
            v.str.settle();
            if (pair) v.str2.settle();
        }
        const bool landed = pitch && v.off == 0.f;
        const int which = pair && !decay && !landed && !stiffer ? 1 + static_cast<int>((v.age / kTick) % 2) : 0;
        if (pitch || decay) tuneString(v, decay || landed, stiffer || landed, which);
        v.str.snap();
        if (pair) v.str2.snap();
    } else {
        if (pitch || decay) tuneModes(v, v.off == 0.f, decay);
        v.bank.snap(v.modes, v.push.on(v.age) || v.push.on(v.age + kTick - 1));
    }
    // Asleep: 64 samples counted from its strike all under -120 dB, its strike done (and a string's
    // whole line as quiet).
    if (v.age % (kTick * kPeakTicks) == 0 && v.age > 0) {
        v.peak = v.peakRun;
        v.peakRun = 0.f;
        if (!v.push.on(v.age) && v.peak < kSleepPeak && (!v.string || (v.str.loudest() < kSleepPeak && (v.pairCents == 0.0 || v.str2.loudest() < kSleepPeak)))) sleep(v);
    }
}

// ---- the synth ----------------------------------------------------------------------------------

Physic::Physic() : state_(new State) {
    for (float& m : state_->macro) m = 0.5f;
    state_->patch = presetPatch(0);
    state_->makeLines(state_->sampleRate);
    state_->refresh();
    state_->levelNow = state_->levelTo;
    state_->bodyMixNow = state_->bodyMixTo;
}
Physic::~Physic() = default;

int Physic::presetCount() { return kPresetCount; }
const char* Physic::presetName(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].name; }
const char* Physic::presetCategory(int i) { return kPresets[clampi(i, 0, kPresetCount - 1)].category; }
PhysicPatch Physic::presetPatch(int i) {
    PhysicPatch patch;
    const Preset& preset = kPresets[clampi(i, 0, kPresetCount - 1)];
    for (int id = 0; id < kSettings; ++id) patch.value[id] = clampi(preset.value[id], settingMin(id), settingMax(id));
    return patch;
}

void Physic::prepare(float sampleRate) {
    State& s = *state_;
    if (!(sampleRate > 1000.f)) return;
    s.sampleRate = sampleRate;
    for (State::Voice& v : s.voice) s.sleep(v);
    s.makeLines(sampleRate);
    s.queued = 0;
    s.bodyKind = kBodyNone; // (its resonances worked out again at this rate)
    s.body.clear();
    s.refresh();
    s.levelNow = s.levelTo;
    s.bodyMixNow = s.bodyMixTo;
}

void Physic::selectPreset(int index) {
    if (index < 0 || index >= kPresetCount) return;
    state_->patch = presetPatch(index);
    for (float& m : state_->macro) m = 0.5f;
    state_->refresh();
}

void Physic::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value = clampi(value, settingMin(id), settingMax(id));
    if (state_->patch.value[id] == value) return;
    state_->patch.value[id] = value;
    state_->refresh();
}

// Stores the number: nothing is worked out until each voice's next tick (a MOD gives values often).
void Physic::setMacro(int macro, float value) {
    if (macro < 0 || macro >= kMacros || !(value == value)) return;
    value = clampf(value, 0.f, 1.f);
    if (state_->macro[macro] == value) return;
    state_->macro[macro] = value;
    state_->refresh();
}

void Physic::noteSeed(std::uint32_t seed) {
    state_->nextSeed = seed;
    state_->seedGiven = true;
}

void Physic::noteOn(int note, float velocity) { noteOnFrom(note, velocity, -1, 0.f); }

void Physic::noteOnFrom(int note, float velocity, int from, float seconds) {
    State& s = *state_;
    const std::uint32_t seed = s.seedGiven ? s.nextSeed : liveSeed(s.liveCount++);
    s.seedGiven = false;
    if (note < 0 || note > 127 || !(velocity == velocity)) return;
    if (s.queued == kQueue) s.settle();
    State::Queued& q = s.queue[s.queued++];
    q.note = note;
    q.velocity = clampf(velocity, 0.f, 1.f);
    q.from = from >= 0 && from <= 127 && from != note && seconds > 0.f && seconds < 1e6f ? from : -1;
    q.seconds = q.from >= 0 ? seconds : 0.f;
    q.seed = seed;
    std::memcpy(q.eff, s.eff, sizeof q.eff);
}

void Physic::noteOff(int note) {
    State& s = *state_;
    for (int i = 0; i < s.queued; ++i)
        if (s.queue[i].note == note) s.settle(); // (a strike let go before it has sounded: struck, then let go)
    const float release = s.eff[Release];
    if (releaseRings(release)) return;
    for (State::Voice& v : s.voice)
        if (!v.asleep && !v.fading && !v.released && v.key == note) s.letGo(v, releaseSeconds(release));
}

void Physic::stopNotes() {
    State& s = *state_;
    s.settle();
    const float release = s.eff[Release];
    s.seedGiven = false;
    if (releaseRings(release)) return;
    for (State::Voice& v : s.voice)
        if (!v.asleep && !v.fading && !v.released) s.letGo(v, releaseSeconds(release));
}

void Physic::allNotesOff() {
    State& s = *state_;
    s.settle();
    for (State::Voice& v : s.voice)
        if (!v.asleep && !v.fading) s.letGo(v, kDampAllSeconds);
    s.liveCount = 0;
    s.seedGiven = false;
}

void Physic::render(float* left, float* right, int frames) {
    State& s = *state_;
    for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
    if (s.queued) s.settle();
    const float fadeEvery = 1.f / static_cast<float>(std::lround(kStealFadeSeconds * s.sampleRate));
    for (State::Voice& v : s.voice) {
        if (v.asleep) continue;
        int pos = 0;
        while (pos < frames) {
            if (v.age % kTick == 0) {
                s.tick(v);
                if (v.asleep) break;
            }
            int n = kTick - static_cast<int>(v.age % kTick);
            if (n > frames - pos) n = frames - pos;
            const bool pushing = v.push.kind != Push::None && v.age + static_cast<std::uint32_t>(n) > v.push.from && v.age < v.push.to;
            if (pushing) v.push.fill(v.age, s.pushed, n);
            if (v.string) {
                v.str.run(pushing ? s.pushed : nullptr, s.scratch, n);
                if (v.pairCents > 0.0) { // PAIR: the second string, added
                    v.str2.run(pushing ? s.pushed : nullptr, s.scratch2, n);
                    for (int i = 0; i < n; ++i) s.scratch[i] += s.scratch2[i];
                }
            } else {
                v.bank.run(pushing ? s.pushed : nullptr, s.scratch, n);
            }
            float peak = v.peakRun;
            if (!v.motorOn && v.buzzGap == 0.f) { // (as in version 1, to the bit)
                for (int i = 0; i < n; ++i) {
                    if (v.amp != v.ampTo) {
                        v.amp = v.amp < v.ampTo ? (v.amp + v.ampStep >= v.ampTo ? v.ampTo : v.amp + v.ampStep)
                                                : (v.amp - v.ampStep <= v.ampTo ? v.ampTo : v.amp - v.ampStep);
                    }
                    float o = s.scratch[i] * v.amp;
                    if (v.fading) o *= static_cast<float>(v.fadeLeft > 0 ? v.fadeLeft - 1 : 0) * fadeEvery, --v.fadeLeft;
                    const float a = o < 0.f ? -o : o;
                    peak = a > peak ? a : peak;
                    left[pos + i] += o * v.panL;
                    right[pos + i] += o * v.panR;
                }
            } else { // MOTOR's level a sample at a time between its tick's two; BUZZ's rattle over its gap. (On locals.)
                const std::uint32_t into = v.age % kTick, age = v.age;
                const float g0 = v.motorG0, dg = v.motorG1 - v.motorG0, gap = v.buzzGap, ampTo = v.ampTo, ampStep = v.ampStep, panL = v.panL, panR = v.panR;
                const bool motor = v.motorOn, fading = v.fading;
                const std::uint32_t buzzSeed = v.seed ^ kBuzzSalt;
                float most = v.most, amp = v.amp;
                int fadeLeft = v.fadeLeft;
                float* const l = left + pos;
                float* const r = right + pos;
                for (int i = 0; i < n; ++i) {
                    if (amp != ampTo) amp = amp < ampTo ? (amp + ampStep >= ampTo ? ampTo : amp + ampStep) : (amp - ampStep <= ampTo ? ampTo : amp - ampStep);
                    float o = s.scratch[i] * amp;
                    if (motor) o *= g0 + dg * (static_cast<float>(into + static_cast<std::uint32_t>(i)) * (1.f / kTick));
                    if (gap > 0.f) {
                        const float a = o < 0.f ? -o : o;
                        most = a > most ? a : most;
                        const float over = a - gap * most;
                        if (over > 0.f) o += kBuzzSize * over * pluckNoise(buzzSeed, age + static_cast<std::uint32_t>(i));
                    }
                    if (fading) o *= static_cast<float>(fadeLeft > 0 ? fadeLeft - 1 : 0) * fadeEvery, --fadeLeft;
                    const float a = o < 0.f ? -o : o;
                    peak = a > peak ? a : peak;
                    l[i] += o * panL;
                    r[i] += o * panR;
                }
                v.most = most, v.amp = amp, v.fadeLeft = fadeLeft;
            }
            v.peakRun = peak;
            v.age += static_cast<std::uint32_t>(n);
            pos += n;
            if (v.fading && v.fadeLeft <= 0) {
                s.sleep(v);
                break;
            }
        }
    }
    // BODY (GUITAR, BOX): on the voices' sum, before LEVEL.
    if (s.body.count > 0) s.body.run(left, right, frames, s.bodyMixNow, s.bodyMixTo, s.bodyMixStep);
    // LEVEL, sliding a sample at a time when it is turned.
    for (int i = 0; i < frames; ++i) {
        if (s.levelNow != s.levelTo) {
            s.levelNow = s.levelNow < s.levelTo ? (s.levelNow + s.levelStep >= s.levelTo ? s.levelTo : s.levelNow + s.levelStep)
                                                : (s.levelNow - s.levelStep <= s.levelTo ? s.levelTo : s.levelNow - s.levelStep);
        }
        left[i] *= s.levelNow;
        right[i] *= s.levelNow;
    }
}

// ---- for the tests ------------------------------------------------------------------------------

int Physic::awake() const {
    int n = 0;
    for (const State::Voice& v : state_->voice) n += !v.asleep;
    return n;
}
long Physic::ticksWorked() const { return state_->ticks; }
long Physic::strikes() const { return state_->strikes; }
long Physic::tunings() const { return state_->tunings; }
std::uint32_t Physic::lastSeed() const { return state_->lastSeed; }
int Physic::keysSounding(int* keys) const {
    int n = 0;
    for (const State::Voice& v : state_->voice)
        if (!v.asleep && !v.fading) keys[n++] = v.key;
    return n;
}
Physic::Rolled Physic::lastRolled() const { return state_->lastRolled; }

float Physic::smallestState(bool lines) const {
    float least = 0.f;
    const auto look = [&least](float v) {
        const float a = v < 0.f ? -v : v;
        if (a > 0.f && (least == 0.f || a < least)) least = a;
    };
    for (const State::Voice& v : state_->voice) {
        for (int k = 0; k < kModesMost; ++k) look(v.bank.x[k]), look(v.bank.y[k]);
        for (const StringLoop* str : {&v.str, &v.str2}) {
            look(str->apIn), look(str->apOut), look(str->lp), look(str->dcIn), look(str->dcOut);
            for (int k = 0; k < kDispersion; ++k) look(str->dIn[k]), look(str->dOut[k]);
            if (str->line && lines)
                for (int i = 0; i <= str->mask; ++i) look(str->line[i]);
        }
    }
    for (int k = 0; k < kBodyModesMost; ++k) look(state_->body.x[k]), look(state_->body.y[k]);
    return least;
}

int Physic::modesOf(int slot, double* hz, float* heard) const {
    if (slot < 0 || slot >= kSlots) return -1;
    const State::Voice& v = state_->voice[slot];
    if (v.asleep || v.string) return -1;
    const double q = std::pow(2.0, v.off / 12.0);
    for (int k = 0; k < v.modes; ++k) hz[k] = v.baseHz[k] * q;
    if (heard)
        for (int k = 0; k < v.modes; ++k) heard[k] = v.bank.out[k];
    return v.modes;
}

} // namespace apotheca
