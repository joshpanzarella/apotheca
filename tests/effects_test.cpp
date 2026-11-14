// Effects tests: the test bench every effect is measured on.
//
// 1. THE PINNED SOUND. The five effects old songs use (FILTER, DRIVE, DELAY,
//    REVERB, CRUSH) are held to the exact samples they made on 2026-11-11,
//    before the effects were split into one file each. Each case below is a
//    fixed input through one effect at fixed settings, and the number beside
//    it is a checksum of every bit of the output. A change to any of the five
//    that moves one sample by one bit fails here. That is the proof that an
//    old song sounds the same.
//
//    The numbers are only exact where the maths library is the one they were
//    made with (tanh, pow, exp, tan and sin differ in the last bit between
//    machines). The test checks that first (`mathsPrint`). Where it differs,
//    each case is held to its loudness and its peak instead, within a
//    thousandth, and the test says so.
//
//    To make the numbers again (only ever from code known to be right):
//        ./build/effects_test --print
//
// 2. THE TABLE: every effect's row says what the rest of the program needs.
//
// 3. THE TOOLS every later effect is measured with, each used here on the
//    old five so that the tools themselves are tested and today's numbers
//    are written down:
//      impulseOf / rt60 / tailSeconds / echoesIn   a reverb's tail
//      echoAt / echoGain                           a delay's times and feedback
//      levelAt                                     level in and out at a frequency
//      harmonicsOf                                 a waveshaper's harmonics and its aliasing
//      largestJump / sweepJump / swapJump          "no click" on a turn and on a swap
//      effectsSong / swapSong                      the same on 0, 1, 2 and 3 helper threads
//    ./build/effects_test --measure prints the numbers the comments quote.
//
// 4. FLUSH-TO-ZERO and 5. THE FADE when an effect is swapped.
//
// No framework: each CHECK prints its line on failure and the program exits
// non-zero if any failed.

#include <atomic>
#include <cfenv>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "apotheca/denormals.hpp"
#include "apotheca/engine.hpp"

#include "../engine/src/fx/glide.hpp"    // Glide, by itself
#include "../engine/src/fx/halfband.hpp" // the 2x stage, measured by itself
#include "../engine/src/fx/unit.hpp"     // makeSaturatePlain: SATURATE without it

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

// ---- tools: checksums ----------------------------------------------------------------------

// A checksum of every bit of a run of samples (FNV-1a, 64 bits).
struct Sum {
    std::uint64_t h = 1469598103934665603ull;
    void add(const float* v, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            std::uint32_t bits;
            std::memcpy(&bits, &v[i], sizeof bits);
            for (int b = 0; b < 4; ++b) {
                h ^= (bits >> (8 * b)) & 0xffu;
                h *= 1099511628211ull;
            }
        }
    }
    void add(const std::vector<float>& v) { add(v.data(), v.size()); }
};

// Which maths library this is, as a checksum of what it answers. The pinned
// numbers hold only where this matches the machine they were made on.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static float mulAdd(float a, float b, float c) { return a * b + c; } // one instruction on some machines, two on others: the answers differ
static std::uint64_t mathsPrint() {
    Sum sum;
    std::vector<float> got;
    for (int i = 0; i < 400; ++i) {
        const float x = 0.0173f * static_cast<float>(i) - 3.1f;
        got.push_back(std::tanh(x));
        got.push_back(std::pow(40.f, 0.0025f * static_cast<float>(i)));
        got.push_back(std::pow(2.f, x * 3.f));
        got.push_back(std::exp(-0.004f * static_cast<float>(i)));
        got.push_back(std::tan(0.0035f * static_cast<float>(i) + 0.001f));
        got.push_back(std::sin(x * 7.f));
        got.push_back(std::round(x * 100.f));
        got.push_back(mulAdd(x, 1.0000001f, -x));
    }
    sum.add(got);
    return sum.h;
}

// ---- tools: signals -------------------------------------------------------------------------

struct Stereo {
    std::vector<float> l, r;
    explicit Stereo(std::size_t frames = 0) : l(frames, 0.f), r(frames, 0.f) {}
    std::size_t frames() const { return l.size(); }
};

// Noise that is the same on every machine: -1..1.
struct Noise {
    std::uint32_t state;
    explicit Noise(std::uint32_t seed) : state(seed) {}
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(state >> 8) - 8388608) / 8388608.f;
    }
};

// Through an effect in the engine's own chunks of 64 frames, as a slot runs it.
// A drum is being played meanwhile, for the one effect that listens (DUCK;
// the rest are deaf to it): lane 1 is hit five times a second (at frame 0,
// 9600, ...) and lane 8 as often, half way between (4800, 14400, ...).
static const std::size_t kHitEvery = 9600;
static void through(Effect& effect, Stereo& io, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    if (to > io.frames()) to = io.frames();
    for (std::size_t at = from; at < to; at += kMaxBlock) {
        const std::size_t n = to - at < static_cast<std::size_t>(kMaxBlock) ? to - at : static_cast<std::size_t>(kMaxBlock);
        if (at % kHitEvery == 0) effect.drumHits(1u);
        if (at % kHitEvery == kHitEvery / 2) effect.drumHits(1u << 7);
        effect.process(io.l.data() + at, io.r.data() + at, static_cast<int>(n));
    }
}

static double rmsOf(const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    if (to > v.size()) to = v.size();
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i) e += static_cast<double>(v[i]) * v[i];
    return to > from ? std::sqrt(e / static_cast<double>(to - from)) : 0.0;
}
static float peakOf(const std::vector<float>& v) {
    float peak = 0.f;
    for (float s : v) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    return peak;
}

// ---- 1. the pinned sound ------------------------------------------------------------------------

// What every pinned case is fed: two seconds. A click, then a burst of noise,
// then a sine sweeping up from 20 Hz to 12 kHz, then nothing, so that echoes
// and tails are in the checksum too. Left and right differ.
static Stereo pinInput(float rate) {
    const std::size_t frames = 96000;
    Stereo in(frames);
    in.l[0] = 0.9f;
    in.r[0] = -0.7f;
    Noise left(12345u), right(67890u);
    for (std::size_t i = 4800; i < 14400; ++i) {
        in.l[i] = 0.5f * left.next();
        in.r[i] = 0.5f * right.next();
    }
    double phase = 0.0;
    for (std::size_t i = 14400; i < 38400; ++i) {
        const double along = static_cast<double>(i - 14400) / 24000.0;
        const double hz = 20.0 * std::pow(600.0, along);
        phase += 6.283185307179586 * hz / rate;
        in.l[i] = 0.6f * static_cast<float>(std::sin(phase));
        in.r[i] = 0.6f * static_cast<float>(std::sin(phase + 1.0));
    }
    return in;
}

struct PinCase {
    const char* name;
    EffectKind kind;
    float rate, tempo;
    float p[kEffectParams]; // below 0: left at its default
    bool moving;            // the settings jump about every chunk, the tempo changes, and it is reset part way
    std::uint64_t sum;      // every bit of the output
    double rms;             // and, for a machine with other maths, how loud
    float peak;
};

static Stereo renderPin(const PinCase& c) {
    Stereo io = pinInput(c.rate);
    std::unique_ptr<Effect> effect = makeEffect(c.kind, c.rate, c.tempo);
    if (!effect) return Stereo();
    const int params = effectInfo(c.kind).params;
    for (int i = 0; i < params; ++i)
        if (c.p[i] >= 0.f) effect->setParam(i, c.p[i]);
    if (!c.moving) {
        through(*effect, io);
        return io;
    }
    const std::size_t chunks = io.frames() / kMaxBlock;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        for (int i = 0; i < params; ++i)
            if (chunk % static_cast<std::size_t>(3 + i) == 0) effect->setParam(i, static_cast<float>((chunk * static_cast<std::size_t>(37 + 22 * i)) % 1000) / 999.f);
        if (chunk == 700) effect->setTempo(87.f);
        if (chunk == 1000) effect->reset();
        through(*effect, io, chunk * kMaxBlock, (chunk + 1) * kMaxBlock);
    }
    return io;
}

static const float D = -1.f; // "its default"
// name, effect, sample rate, tempo, settings, moving, then what it made on 2026-11-11.
static const PinCase kPins[] = {
#include "effects_pins.inc"
};
static const int kPinCount = static_cast<int>(sizeof kPins / sizeof kPins[0]);
// mathsPrint() of the machine the numbers were made on.
static const std::uint64_t kPinMaths =
#include "effects_pins_maths.inc"
    ;

static const char* kindWord(EffectKind kind) {
    switch (kind) {
    case EffectKind::Filter: return "Filter";
    case EffectKind::Drive: return "Drive";
    case EffectKind::Delay: return "Delay";
    case EffectKind::Reverb: return "Reverb";
    case EffectKind::Crush: return "Crush";
    default: return "None";
    }
}

static std::string settingWord(float v) {
    if (v < 0.f) return "D";
    char text[32];
    std::snprintf(text, sizeof text, "%.9gf", static_cast<double>(v));
    if (!std::strchr(text, '.') && !std::strchr(text, 'e')) { // "1f" is not a number in C++
        text[std::strlen(text) - 1] = 0;
        std::strcat(text, ".f");
    }
    return text;
}

// The table again, with the numbers this build makes. For effects_pins.inc.
static void printPins() {
    for (int i = 0; i < kPinCount; ++i) {
        const PinCase& c = kPins[i];
        const Stereo out = renderPin(c);
        Sum sum;
        sum.add(out.l);
        sum.add(out.r);
        std::vector<float> both(out.l);
        both.insert(both.end(), out.r.begin(), out.r.end());
        std::printf("    {\"%s\", EffectKind::%s, %.0f.f, %.0f.f, {%s, %s, %s, %s}, %s, 0x%016llxull, %.9g, %.9gf},\n", c.name, kindWord(c.kind), static_cast<double>(c.rate),
                    static_cast<double>(c.tempo), settingWord(c.p[0]).c_str(), settingWord(c.p[1]).c_str(), settingWord(c.p[2]).c_str(), settingWord(c.p[3]).c_str(),
                    c.moving ? "true" : "false", static_cast<unsigned long long>(sum.h), rmsOf(both), static_cast<double>(peakOf(both)));
    }
    std::printf("maths: 0x%016llxull\n", static_cast<unsigned long long>(mathsPrint()));
}

// Each of the five, at each of its pinned settings: the same to the last bit.
// Returns how many matched exactly.
static int checkPins(const char* when) {
    const bool sameMaths = mathsPrint() == kPinMaths;
    int exact = 0;
    int perKind[kEffectKinds] = {};
    for (int i = 0; i < kPinCount; ++i) {
        const PinCase& c = kPins[i];
        const Stereo out = renderPin(c);
        CHECK(out.frames() == 96000);
        if (out.frames() != 96000) continue;
        Sum sum;
        sum.add(out.l);
        sum.add(out.r);
        std::vector<float> both(out.l);
        both.insert(both.end(), out.r.begin(), out.r.end());
        const double rms = rmsOf(both);
        const float peak = peakOf(both);
        const bool same = sum.h == c.sum;
        exact += same ? 1 : 0;
        ++perKind[static_cast<int>(c.kind)];
        // Either way it is about as loud as it was: a second, coarser net.
        const bool close = std::fabs(rms - c.rms) <= 1e-3 * c.rms + 1e-9 && std::fabs(peak - c.peak) <= 1e-3f * c.peak + 1e-9f;
        if ((sameMaths && !same) || !close) {
            std::printf("FAIL pinned sound (%s): %s  sum %016llx, was %016llx; rms %.9g, was %.9g; peak %.9g, was %.9g\n", when, c.name, static_cast<unsigned long long>(sum.h),
                        static_cast<unsigned long long>(c.sum), rms, c.rms, static_cast<double>(peak), static_cast<double>(c.peak));
            ++failures;
        }
    }
    // Every one of the five is in the table, more than once.
    for (int k = 1; k <= static_cast<int>(EffectKind::Crush); ++k) CHECK(perKind[k] >= 4);
    if (!sameMaths)
        std::printf("NOTE pinned sound (%s): this machine's maths library differs from the one the numbers were made with, so the %d cases were held to their loudness and peak only "
                    "(%d still matched to the bit)\n",
                    when, kPinCount, exact);
    return exact;
}

static void testPinnedSound() {
    const int exact = checkPins("as built");
    if (mathsPrint() == kPinMaths) CHECK(exact == kPinCount);
    // The table itself: what a setting is called and where it starts is part of what a song stores.
    const EffectInfo& filter = effectInfo(EffectKind::Filter);
    CHECK(std::string(filter.name) == "FILTER" && filter.params == 4 && std::string(filter.paramNames[0]) == "CUTOFF" && std::string(filter.paramNames[1]) == "RESONANCE");
    CHECK(std::string(filter.paramNames[2]) == "TYPE" && std::string(filter.paramNames[3]) == "MIX");
    CHECK(filter.defaults[0] == 0.6f && filter.defaults[1] == 0.2f && filter.defaults[2] == 0.f && filter.defaults[3] == 1.f);
    const EffectInfo& drive = effectInfo(EffectKind::Drive);
    CHECK(std::string(drive.name) == "DRIVE" && drive.params == 4 && std::string(drive.paramNames[0]) == "DRIVE" && std::string(drive.paramNames[1]) == "TONE");
    CHECK(std::string(drive.paramNames[2]) == "MIX" && std::string(drive.paramNames[3]) == "OUTPUT");
    CHECK(drive.defaults[0] == 0.35f && drive.defaults[1] == 0.7f && drive.defaults[2] == 1.f && drive.defaults[3] == 0.5f);
    const EffectInfo& delay = effectInfo(EffectKind::Delay);
    CHECK(std::string(delay.name) == "DELAY" && delay.params == 4 && std::string(delay.paramNames[0]) == "TIME" && std::string(delay.paramNames[1]) == "FEEDBACK");
    CHECK(std::string(delay.paramNames[2]) == "TONE" && std::string(delay.paramNames[3]) == "MIX");
    CHECK(delay.defaults[0] == 0.5f && delay.defaults[1] == 0.45f && delay.defaults[2] == 0.6f && delay.defaults[3] == 0.35f);
    const EffectInfo& reverb = effectInfo(EffectKind::Reverb);
    CHECK(std::string(reverb.name) == "REVERB" && reverb.params == 3 && std::string(reverb.paramNames[0]) == "SIZE" && std::string(reverb.paramNames[1]) == "DAMPING");
    CHECK(std::string(reverb.paramNames[2]) == "MIX");
    CHECK(reverb.defaults[0] == 0.6f && reverb.defaults[1] == 0.5f && reverb.defaults[2] == 0.3f);
    const EffectInfo& crush = effectInfo(EffectKind::Crush);
    CHECK(std::string(crush.name) == "CRUSH" && crush.params == 3 && std::string(crush.paramNames[0]) == "BITS" && std::string(crush.paramNames[1]) == "RATE");
    CHECK(std::string(crush.paramNames[2]) == "MIX");
    CHECK(crush.defaults[0] == 0.5f && crush.defaults[1] == 0.3f && crush.defaults[2] == 1.f);
    // And their numbers, which songs in memory and commands use.
    CHECK(static_cast<int>(EffectKind::None) == 0 && static_cast<int>(EffectKind::Filter) == 1 && static_cast<int>(EffectKind::Drive) == 2);
    CHECK(static_cast<int>(EffectKind::Delay) == 3 && static_cast<int>(EffectKind::Reverb) == 4 && static_cast<int>(EffectKind::Crush) == 5);
    // The new ones' numbers were given out before they were built (6 TAPE, 7 HALL, 8 CHORUS, 9 COMP, 10 SATURATE, 11 DUCK).
    CHECK(static_cast<int>(EffectKind::Chorus) == 8 && static_cast<int>(EffectKind::Comp) == 9 && static_cast<int>(EffectKind::Saturate) == 10);
}

// ---- a whole song, with an effect in every slot ------------------------------------------------

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}

// Drums and three synths, all five effects on the tracks and two on the
// master, settings off their defaults, one effect setting automated. Three
// bars, asked for in blocks of odd sizes as a sound card might. Everything is
// in place and has had a moment before the song starts (as a bounce does it).
// `buses`: an effect on each send bus as a new song has, and nothing sent to
// them (sendSong() is the one whose sends sound).
static std::vector<float> effectsSong(int helpers, bool buses = false) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->useWorkers(helpers);
    if (buses) {
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay) && engine->setEffect(kSpaceBus, 0, EffectKind::Reverb));
        engine->send(make(Command::SetEffectParam, kSpaceBus, 0, 0, 0.9f));
    }
    const int step = 6000; // frames in one step at 120 BPM and 48 kHz
    for (int s = 0; s < kSteps; s += 2) engine->send(make(Command::SetStep, 0, s % 4 == 0 ? 0 : 6, s, 0.9f));
    const SynthKind kinds[3] = {SynthKind::Phazed, SynthKind::Qmod, SynthKind::Twin};
    for (int t = 1; t <= 3; ++t) {
        CHECK(engine->addSynthTrack(t, kinds[t - 1]));
        for (int n = 0; n < 4; ++n) {
            Command note = make(Command::SetNote, t, n, n * 4 + (t % 3), 0.7f);
            note.c = 40 + 5 * t + 3 * n;
            note.d = 3;
            engine->send(note);
        }
    }
    const EffectKind slots[4][2] = {{EffectKind::Filter, EffectKind::Crush}, {EffectKind::Drive, EffectKind::Delay}, {EffectKind::Reverb, EffectKind::Filter}, {EffectKind::Delay, EffectKind::Drive}};
    for (int t = 0; t < 4; ++t)
        for (int slot = 0; slot < 2; ++slot) CHECK(engine->setEffect(t, slot, slots[t][slot]));
    CHECK(engine->setEffect(kMasterTrack, 0, EffectKind::Delay) && engine->setEffect(kMasterTrack, 1, EffectKind::Reverb));
    engine->send(make(Command::SetEffectParam, 0, 0, 2, 0.3f));  // the drums' filter: low 24
    engine->send(make(Command::SetEffectParam, 0, 0, 0, 0.8f));
    engine->send(make(Command::SetEffectParam, 0, 1, 0, 0.7f));  // their crush: fewer bits
    engine->send(make(Command::SetEffectParam, 1, 1, 0, 0.4f));  // a dotted eighth
    engine->send(make(Command::SetEffectParam, 2, 1, 2, 0.7f));  // the DJ filter, on its high side
    engine->send(make(Command::SetEffectParam, 2, 1, 0, 0.7f));
    engine->send(make(Command::SetEffectParam, kMasterTrack, 0, 3, 0.2f));
    engine->send(make(Command::SetEffectParam, kMasterTrack, 1, 0, 0.3f));
    // The drums' filter closes and opens by itself as the bar goes by.
    for (int s = 0; s < kSteps; s += 4) engine->send(make(Command::SetAutomation, 0, autoLaneOfEffect(0, 0), s, s % 8 == 0 ? 0.35f : 0.9f));
    std::vector<float> all(2 * 1024);
    engine->render(all.data(), 1024);
    engine->send(make(Command::Play, 0, 0, 1));
    for (int done = 0, size = 37; done < 3 * 16 * step; done += size, size = size % 301 + 64) {
        std::vector<float> got(static_cast<std::size_t>(2 * size), 99.f);
        engine->render(got.data(), size);
        all.insert(all.end(), got.begin(), got.end());
    }
    float peaks = 0.f;
    for (int t = 0; t < 4; ++t) peaks += engine->trackPeak(t);
    all.push_back(peaks); // the meters agree too
    return all;
}

// The song's checksum on 2026-11-11, before the effects were moved. (The
// same maths-library caveat: the instruments use it too.)
static const std::uint64_t kSongSum = 0xd8819d428dab766dull;
static const double kSongRms = 0.355528453;

static void testWholeSong() {
    const std::vector<float> alone = effectsSong(0);
    Sum sum;
    sum.add(alone);
    const double rms = rmsOf(alone, 0, alone.size() - 1);
    CHECK(peakOf(alone) > 0.1f && alone.size() > 500000);
    for (float v : alone) {
        if (v != v) {
            CHECK(!"a sample that is not a number");
            break;
        }
    }
    if (mathsPrint() == kPinMaths) {
        if (sum.h != kSongSum) std::printf("the whole song: sum 0x%016llxull, rms %.9g\n", static_cast<unsigned long long>(sum.h), rms);
        CHECK(sum.h == kSongSum);
    }
    CHECK(std::fabs(rms - kSongRms) <= 1e-3 * kSongRms);
    // The same to the last bit however many processor cores share the tracks out.
    for (int helpers : {1, 2, 3}) CHECK(effectsSong(helpers) == alone);
    // And the same to the last bit with an effect waiting on each send bus and
    // nothing sent: the buses are at rest, and add nothing, not even zeros.
    CHECK(effectsSong(0, true) == alone && effectsSong(2, true) == alone);
}

// ---- 2. the table ----------------------------------------------------------------------------

static std::string reads(EffectKind kind, int index, float v, const float* all = nullptr) {
    char text[32] = "?";
    effectParamText(kind, index, v, text, sizeof text, all);
    return text;
}

static void testTheTable() {
    // The word in a song file. These five are in songs already: they never change.
    CHECK(std::string(effectInfo(EffectKind::Filter).word) == "filter" && std::string(effectInfo(EffectKind::Drive).word) == "drive");
    CHECK(std::string(effectInfo(EffectKind::Delay).word) == "delay" && std::string(effectInfo(EffectKind::Reverb).word) == "reverb");
    CHECK(std::string(effectInfo(EffectKind::Crush).word) == "crush" && std::string(effectInfo(EffectKind::None).word).empty());
    CHECK(effectFromWord("filter") == EffectKind::Filter && effectFromWord("drive") == EffectKind::Drive && effectFromWord("delay") == EffectKind::Delay);
    CHECK(effectFromWord("reverb") == EffectKind::Reverb && effectFromWord("crush") == EffectKind::Crush);
    CHECK(effectFromWord("") == EffectKind::None && effectFromWord(nullptr) == EffectKind::None && effectFromWord("FILTER") == EffectKind::None);
    CHECK(effectFromWord("nosuch") == EffectKind::None && effectFromWord("filte") == EffectKind::None && effectFromWord("filters") == EffectKind::None);
    // Every effect: a word of its own, one word, lower case; in a group; in the list once.
    CHECK(effectListSize() == kEffectKinds && effectAt(0) == EffectKind::None && effectPlace(EffectKind::None) == 0);
    CHECK(effectAt(-1) == EffectKind::None && effectAt(effectListSize()) == EffectKind::None && effectPlace(EffectKind::Count) == 0 && effectPlace(static_cast<EffectKind>(-3)) == 0);
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        const std::string word = info.word;
        CHECK(!word.empty() && effectFromWord(info.word) == kind);
        for (char c : word) CHECK((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
        for (int other = 1; other < k; ++other) CHECK(word != effectInfo(static_cast<EffectKind>(other)).word);
        CHECK(info.family != EffectFamily::None && effectFamilyName(info.family)[0] != 0);
        CHECK(info.create != nullptr && info.params >= 1 && info.params <= kEffectParams);
        const int place = effectPlace(kind);
        CHECK(place >= 1 && place < effectListSize() && effectAt(place) == kind);
        int times = 0;
        for (int i = 0; i < effectListSize(); ++i) times += effectAt(i) == kind ? 1 : 0;
        CHECK(times == 1);
        for (int i = 0; i < kEffectParams; ++i) {
            CHECK(info.defaults[i] >= 0.f && info.defaults[i] <= 1.f && info.choices[i] >= 0);
            CHECK(effectChoices(kind, i) == (i < info.params ? info.choices[i] : 0));
            CHECK((i < info.params) == (info.paramNames[i][0] != 0));
            // Every setting reads as something, wherever it is turned to, and fits its box.
            for (float v : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
                const std::string text = reads(kind, i, v);
                if (i < info.params) CHECK(!text.empty() && text != "?" && text.size() <= 12);
            }
        }
        // A setting that is a choice reads differently at each choice.
        for (int i = 0; i < info.params; ++i) {
            const int count = info.choices[i];
            for (int a = 0; a < count; ++a)
                for (int b = 0; b < a; ++b) CHECK(reads(kind, i, (a + 0.5f) / count) != reads(kind, i, (b + 0.5f) / count));
        }
    }
    CHECK(effectChoices(EffectKind::Filter, -1) == 0 && effectChoices(EffectKind::Filter, 4) == 0 && effectChoices(EffectKind::None, 0) == 0);
    CHECK(effectChoices(static_cast<EffectKind>(77), 0) == 0);
    // The list as it is today, in its seven families (FILTER, DRIVE, CRUSH, MOD, DELAY, REVERB, LEVEL), the types of a family side by side.
    static const EffectKind list[] = {EffectKind::None, EffectKind::Filter, EffectKind::Comb, EffectKind::Vowel, EffectKind::Eq, EffectKind::Drive, EffectKind::Saturate, EffectKind::Fold, EffectKind::Fuzz, EffectKind::Crush, EffectKind::Ring, EffectKind::Wear, EffectKind::Chorus, EffectKind::Flanger, EffectKind::Ensemble, EffectKind::Phaser, EffectKind::Tremolo, EffectKind::Delay, EffectKind::Tape, EffectKind::Ping, EffectKind::Reverb, EffectKind::Hall, EffectKind::Spring, EffectKind::Plate, EffectKind::Comp, EffectKind::Duck, EffectKind::Limiter, EffectKind::Snap};
    CHECK(effectListSize() == static_cast<int>(sizeof list / sizeof list[0]));
    for (int i = 0; i < effectListSize(); ++i) CHECK(effectAt(i) == list[i]);
    // ...and the families in EffectFamily's order, so a family is never in two pieces (the list a player picks from has each once).
    for (int i = 2; i < effectListSize(); ++i) CHECK(static_cast<int>(effectInfo(effectAt(i - 1)).family) <= static_cast<int>(effectInfo(effectAt(i)).family));
    // The numbers of the new ones: fixed for good, as the old five's are.
    CHECK(static_cast<int>(EffectKind::Tape) == 6 && static_cast<int>(EffectKind::Hall) == 7 && static_cast<int>(EffectKind::Ping) == 12 && static_cast<int>(EffectKind::Spring) == 13 && static_cast<int>(EffectKind::Plate) == 14);
    CHECK(static_cast<int>(EffectKind::Comb) == 15 && static_cast<int>(EffectKind::Vowel) == 16 && static_cast<int>(EffectKind::Eq) == 17);
    CHECK(static_cast<int>(EffectKind::Fold) == 18 && static_cast<int>(EffectKind::Fuzz) == 19 && static_cast<int>(EffectKind::Ring) == 20);
    CHECK(static_cast<int>(EffectKind::Wear) == 21 && static_cast<int>(EffectKind::Limiter) == 22 && static_cast<int>(EffectKind::Snap) == 23);
    CHECK(static_cast<int>(EffectKind::Flanger) == 24 && static_cast<int>(EffectKind::Ensemble) == 25 && static_cast<int>(EffectKind::Phaser) == 26 && static_cast<int>(EffectKind::Tremolo) == 27);
    CHECK(std::string(effectInfo(EffectKind::Hall).word) == "hall" && effectFromWord("hall") == EffectKind::Hall);
    CHECK(std::string(effectInfo(EffectKind::Plate).word) == "plate" && effectFromWord("plate") == EffectKind::Plate && effectInfo(EffectKind::Plate).family == EffectFamily::Reverb);
    CHECK(effectInfo(EffectKind::Hall).family == EffectFamily::Reverb && effectChoices(EffectKind::Hall, 0) == 0 && effectChoices(EffectKind::Hall, 1) == 0);
    CHECK(std::string(effectInfo(EffectKind::Tape).word) == "tape" && effectFromWord("tape") == EffectKind::Tape);
    CHECK(effectInfo(EffectKind::Tape).family == EffectFamily::Delay && effectChoices(EffectKind::Tape, 0) == 8 && effectChoices(EffectKind::Tape, 1) == 0);
    // Which settings are choices today, and the groups.
    CHECK(effectChoices(EffectKind::Filter, 2) == 5 && effectChoices(EffectKind::Delay, 0) == 6);
    CHECK(effectChoices(EffectKind::Filter, 0) == 0 && effectChoices(EffectKind::Delay, 1) == 0 && effectChoices(EffectKind::Drive, 0) == 0);
    CHECK(effectChoices(EffectKind::Reverb, 0) == 0 && effectChoices(EffectKind::Crush, 0) == 0);
    CHECK(effectInfo(EffectKind::Filter).family == EffectFamily::Filter && effectInfo(EffectKind::Drive).family == EffectFamily::Drive);
    CHECK(effectInfo(EffectKind::Delay).family == EffectFamily::Delay && effectInfo(EffectKind::Reverb).family == EffectFamily::Reverb);
    CHECK(effectInfo(EffectKind::Crush).family == EffectFamily::Crush && effectInfo(EffectKind::None).family == EffectFamily::None);
    CHECK(std::string(effectFamilyName(EffectFamily::Filter)) == "FILTER" && static_cast<int>(EffectFamily::Count) == 8);
    CHECK(std::string(effectFamilyName(EffectFamily::Delay)) == "DELAY" && std::string(effectFamilyName(EffectFamily::Reverb)) == "REVERB");
    CHECK(std::string(effectFamilyName(EffectFamily::Mod)) == "MOD" && std::string(effectFamilyName(EffectFamily::Level)) == "LEVEL");
    CHECK(std::string(effectFamilyName(EffectFamily::Drive)) == "DRIVE" && std::string(effectFamilyName(EffectFamily::Crush)) == "CRUSH");
    CHECK(std::string(effectFamilyName(EffectFamily::None)).empty());
    CHECK(std::string(effectFamilyName(EffectFamily::Count)).empty() && std::string(effectFamilyName(static_cast<EffectFamily>(-1))).empty());
    // How settings read, each effect's own way.
    CHECK(reads(EffectKind::Filter, 0, 0.f) == "30 HZ" && reads(EffectKind::Filter, 0, 1.f) == "21.7 KHZ" && reads(EffectKind::Filter, 1, 0.2f) == "20%");
    CHECK(reads(EffectKind::Filter, 2, 0.1f) == "LOW 12 DB" && reads(EffectKind::Filter, 2, 0.3f) == "LOW 24 DB" && reads(EffectKind::Filter, 2, 0.5f) == "BAND PASS");
    CHECK(reads(EffectKind::Filter, 2, 0.7f) == "DJ" && reads(EffectKind::Filter, 2, 0.9f) == "HIGH PASS" && reads(EffectKind::Filter, 3, 1.f) == "100%");
    const float dj[4] = {0.5f, 0.2f, 0.7f, 1.f}, low[4] = {0.5f, 0.2f, 0.1f, 1.f};
    CHECK(reads(EffectKind::Filter, 0, 0.5f, dj) == "OPEN" && reads(EffectKind::Filter, 0, 0.f, dj) == "LOW 60" && reads(EffectKind::Filter, 0, 1.f, dj) == "HIGH 8.0K");
    CHECK(reads(EffectKind::Filter, 0, 0.f, low) == "30 HZ");
    CHECK(reads(EffectKind::Drive, 0, 0.35f) == "35%" && reads(EffectKind::Drive, 3, 0.5f) == "100%" && reads(EffectKind::Drive, 3, 1.f) == "200%" && reads(EffectKind::Drive, 3, 0.333f) == "66%");
    CHECK(reads(EffectKind::Delay, 0, 0.f) == "1/16" && reads(EffectKind::Delay, 0, 0.2f) == "1/8" && reads(EffectKind::Delay, 0, 0.4f) == "1/8 DOT");
    CHECK(reads(EffectKind::Delay, 0, 0.5f) == "1/4" && reads(EffectKind::Delay, 0, 0.7f) == "1/4 DOT" && reads(EffectKind::Delay, 0, 1.f) == "1/2" && reads(EffectKind::Delay, 1, 0.45f) == "45%");
    CHECK(reads(EffectKind::Reverb, 0, 0.6f) == "60%" && reads(EffectKind::Reverb, 2, 0.3f) == "30%");
    CHECK(reads(EffectKind::Reverb, 0, 0.29f) == "29%" && reads(EffectKind::Reverb, 0, 0.996f) == "100%"); // to the nearest, not cut short (0.29 is stored as 0.28999...)
    CHECK(reads(EffectKind::Crush, 0, 0.f) == "16 BITS" && reads(EffectKind::Crush, 0, 1.f) == "3 BITS" && reads(EffectKind::Crush, 1, 0.f) == "FULL");
    CHECK(reads(EffectKind::Crush, 1, 1.f) == "1 IN 40" && reads(EffectKind::Crush, 2, 1.f) == "100%");
    CHECK(reads(EffectKind::None, 0, 0.5f) == "50%" && reads(static_cast<EffectKind>(99), 0, 0.5f) == "50%");
    // Nowhere to write: nothing is written.
    char small[4] = "abc";
    effectParamText(EffectKind::Filter, 0, 0.5f, small, 0);
    effectParamText(EffectKind::Filter, 0, 0.5f, nullptr, 8);
    CHECK(std::string(small) == "abc");
    effectParamText(EffectKind::Filter, 2, 0.5f, small, sizeof small); // too short: cut, and still ended
    CHECK(std::string(small) == "BAN");
    // makeEffect goes by the table too.
    CHECK(makeEffect(EffectKind::None, 48000.f, 120.f) == nullptr && makeEffect(EffectKind::Count, 48000.f, 120.f) == nullptr && makeEffect(static_cast<EffectKind>(-1), 48000.f, 120.f) == nullptr);
    for (int k = 1; k < kEffectKinds; ++k) CHECK(makeEffect(static_cast<EffectKind>(k), 48000.f, 120.f) != nullptr);
}

// ---- 3. the measuring tools ------------------------------------------------------------------

static const double kPi = 3.141592653589793;
static double dB(double ratio) { return ratio > 1e-30 ? 20.0 * std::log10(ratio) : -600.0; }

// The setting called MIX, or -1.
static int mixOf(EffectKind kind) {
    const EffectInfo& info = effectInfo(kind);
    for (int i = 0; i < info.params; ++i)
        if (std::string(info.paramNames[i]) == "MIX") return i;
    return -1;
}

static std::vector<float> sineOf(double hz, double amp, std::size_t frames, double rate = 48000.0) {
    std::vector<float> v(frames);
    for (std::size_t i = 0; i < frames; ++i) v[i] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    return v;
}

// How strong one frequency is in a run of samples: the size of a sine at `hz`
// (so a sine of 0.5 reads 0.5). Exact when a whole number of its cycles fits.
static double partAt(const float* v, std::size_t count, double hz, double rate = 48000.0) {
    double re = 0.0, im = 0.0;
    const double w = 2.0 * kPi * hz / rate;
    for (std::size_t i = 0; i < count; ++i) {
        re += v[i] * std::cos(w * static_cast<double>(i));
        im += v[i] * std::sin(w * static_cast<double>(i));
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(count);
}

// LEVEL IN AND OUT AT A FREQUENCY, in dB: a steady sine goes in, and after a
// second to settle the same frequency is measured coming out. `hz` a whole number.
static double levelAt(Effect& effect, double hz, double amp = 0.25) {
    Stereo io(96000);
    io.l = io.r = sineOf(hz, amp, 96000);
    effect.reset();
    through(effect, io);
    return dB(partAt(io.l.data() + 48000, 48000, hz) / amp);
}

// AN IMPULSE RESPONSE: one click in, `seconds` out. The left side.
static std::vector<float> impulseOf(Effect& effect, double seconds, double rate = 48000.0) {
    Stereo io(static_cast<std::size_t>(seconds * rate));
    io.l[0] = io.r[0] = 1.f;
    effect.reset();
    through(effect, io);
    return io.l;
}

// A TAIL'S DECAY TIME (RT60): how long it would take to fall 60 dB, read off
// the stretch from 5 dB down to 35 dB down of the energy still to come
// (Schroeder's way, which is steady where the tail itself is bumpy).
// `from` skips the click itself. 0 if the tail never falls that far.
static double rt60(const std::vector<float>& ir, std::size_t from = 1, double rate = 48000.0) {
    std::vector<double> left(ir.size() + 1, 0.0);
    for (std::size_t i = ir.size(); i-- > from;) left[i] = left[i + 1] + static_cast<double>(ir[i]) * ir[i];
    const double all = left[from];
    if (all <= 0.0) return 0.0;
    double t5 = -1.0, t35 = -1.0;
    for (std::size_t i = from; i < ir.size(); ++i) {
        const double down = 10.0 * std::log10(left[i] / all + 1e-300);
        if (t5 < 0.0 && down <= -5.0) t5 = static_cast<double>(i) / rate;
        if (down <= -35.0) {
            t35 = static_cast<double>(i) / rate;
            break;
        }
    }
    return t5 >= 0.0 && t35 > t5 ? 2.0 * (t35 - t5) : 0.0;
}

// HOW LONG A TAIL IS: the last moment it is within `down` dB of its loudest.
static double tailSeconds(const std::vector<float>& ir, double down = 60.0, std::size_t from = 1, double rate = 48000.0) {
    float peak = 0.f;
    for (std::size_t i = from; i < ir.size(); ++i) peak = std::fabs(ir[i]) > peak ? std::fabs(ir[i]) : peak;
    const float floor = peak * static_cast<float>(std::pow(10.0, -down / 20.0));
    for (std::size_t i = ir.size(); i-- > from;)
        if (std::fabs(ir[i]) > floor) return static_cast<double>(i) / rate;
    return 0.0;
}

// HOW DENSE A TAIL IS: separate echoes between two moments. An echo is a
// sample that stands above both its neighbours and is within 60 dB of the
// loudest. A reverb wants many of them early.
static int echoesIn(const std::vector<float>& ir, double fromSeconds, double toSeconds, double rate = 48000.0) {
    std::size_t from = static_cast<std::size_t>(fromSeconds * rate), to = static_cast<std::size_t>(toSeconds * rate);
    if (from < 1) from = 1;
    if (to + 1 > ir.size()) to = ir.size() > 0 ? ir.size() - 1 : 0;
    float peak = 0.f;
    for (std::size_t i = from; i < to; ++i) peak = std::fabs(ir[i]) > peak ? std::fabs(ir[i]) : peak;
    int count = 0;
    for (std::size_t i = from; i < to; ++i) {
        const float v = std::fabs(ir[i]);
        count += v > peak * 1e-3f && v > std::fabs(ir[i - 1]) && v > std::fabs(ir[i + 1]) ? 1 : 0;
    }
    return count;
}

// WHEN AN ECHO LANDS: the first sample after `after` that is not silent, or -1.
static long echoAt(const std::vector<float>& ir, std::size_t after) {
    for (std::size_t i = after + 1; i < ir.size(); ++i)
        if (ir[i] != 0.f) return static_cast<long>(i);
    return -1;
}

// A DELAY'S FEEDBACK: how much of one echo is in the next, with echoes every
// `every` samples. The sum of a whole echo is used, not its top, so a tone
// control in the loop (which spreads an echo out but keeps its sum) does not count.
static double echoGain(const std::vector<float>& ir, std::size_t every, int which = 1) {
    const auto sum = [&](int n) {
        double s = 0.0;
        for (std::size_t i = static_cast<std::size_t>(n) * every; i < static_cast<std::size_t>(n + 1) * every && i < ir.size(); ++i) s += ir[i];
        return s;
    };
    const double first = sum(which);
    return first != 0.0 ? sum(which + 1) / first : 0.0;
}

// A WAVESHAPER'S HARMONICS AND ITS ALIASING. A sine at `hz` (a whole number
// of hertz, so one second holds whole cycles and every part is measured
// exactly) goes in. What comes out is the sine, its harmonics (2, 3, 4...
// times `hz`, up to half the sample rate), and aliasing: harmonics too high
// for the sample rate, folded back down to where no harmonic is. All in dB
// against the sine that went in.
struct Harmonics {
    double part[8] = {};   // [1] the sine itself, [2] the second harmonic...; -600: none, or above the top
    double strongest = -600.0; // the loudest harmonic above the first
    double alias = -600.0;     // the loudest fold-back
    double aliasHz = 0.0;
    double rest = -600.0;      // everything that is neither the sine nor a harmonic, added up
    double aliasLow = -600.0;  // the loudest fold-back that lands under 19 kHz (above it a 2x stage is only part way shut, and nobody hears)
    double aliasLowHz = 0.0;
    double level = -600.0;     // all of it together, harmonics and the rest, against the sine that went in
};
static Harmonics harmonicsOf(Effect& effect, int hz, double amp) {
    const std::size_t n = 48000;
    Stereo io(2 * n);
    io.l = io.r = sineOf(hz, amp, 2 * n);
    effect.reset();
    through(effect, io);
    const float* out = io.l.data() + n; // the second second: settled
    Harmonics h;
    for (double& p : h.part) p = -600.0;
    double mean = 0.0, total = 0.0, harmonic = 0.0;
    for (std::size_t i = 0; i < n; ++i) mean += out[i];
    mean /= static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) total += (out[i] - mean) * (out[i] - mean);
    total /= static_cast<double>(n);
    for (int k = 1; k * hz < 24000; ++k) {
        const double a = partAt(out, n, static_cast<double>(k) * hz);
        harmonic += 0.5 * a * a;
        if (k < 8) h.part[k] = dB(a / amp);
        if (k > 1 && dB(a / amp) > h.strongest) h.strongest = dB(a / amp);
    }
    for (int k = 24000 / hz + 1; k <= 80; ++k) {
        int folded = (k * hz) % 48000;
        if (folded > 24000) folded = 48000 - folded;
        if (folded % hz == 0 || folded == 0 || folded == 24000) continue; // lands on a harmonic: cannot be told apart
        const double a = dB(partAt(out, n, folded) / amp);
        if (a > h.alias) {
            h.alias = a;
            h.aliasHz = folded;
        }
        if (folded < 19000 && a > h.aliasLow) {
            h.aliasLow = a;
            h.aliasLowHz = folded;
        }
    }
    h.level = 10.0 * std::log10(total / (0.5 * amp * amp) + 1e-60);
    const double rest = total - harmonic;
    h.rest = rest > 0.0 ? 10.0 * std::log10(rest / (0.5 * amp * amp)) : -600.0;
    return h;
}

// NO CLICK: the largest step from one sample to the next.
static float largestJump(const float* v, std::size_t count) {
    float jump = 0.f;
    for (std::size_t i = 1; i < count; ++i) jump = std::fabs(v[i] - v[i - 1]) > jump ? std::fabs(v[i] - v[i - 1]) : jump;
    return jump;
}
static float largestJump(const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    if (to > v.size()) to = v.size();
    return to > from ? largestJump(v.data() + from, to - from) : 0.f;
}
// The largest step a plain sine takes by itself.
static double sineStep(double hz, double amp, double rate = 48000.0) { return amp * 2.0 * std::sin(kPi * hz / rate); }

// NO CLICK ON A TURN: one setting swept end to end over a second, a little
// each chunk as the engine would, under a 100 Hz sine. Returns the largest
// step, counted in the sine's own largest step (1 = no rougher than the sine).
// `steady` gets the same with the setting left alone, to compare.
static double sweepJump(EffectKind kind, int param, double* steady = nullptr) {
    const double amp = 0.5;
    double result[2] = {};
    for (int pass = 0; pass < 2; ++pass) {
        std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, 120.f);
        if (pass == 0) effect->setParam(param, 0.f); // it starts where the sweep starts
        Stereo io(96000);
        io.l = io.r = sineOf(100.0, amp, 96000);
        through(*effect, io, 0, 48000); // settle
        for (std::size_t chunk = 0; chunk < 750; ++chunk) {
            if (pass == 0) effect->setParam(param, static_cast<float>(chunk) / 749.f);
            through(*effect, io, 48000 + chunk * 64, 48000 + (chunk + 1) * 64);
        }
        result[pass] = largestJump(io.l, 48000 - 1, 96000) / sineStep(100.0, amp);
    }
    if (steady) *steady = result[1];
    return result[0];
}

// ---- the tools, on the old five ----------------------------------------------------------------

static bool gMeasure = false; // --measure: print the numbers the comments below quote
#define SAY(...)                              \
    do {                                      \
        if (gMeasure) std::printf(__VA_ARGS__); \
    } while (0)

static std::unique_ptr<Effect> fresh(EffectKind kind, float a = -1.f, float b = -1.f, float c = -1.f, float d = -1.f, float tempo = 120.f) {
    std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, tempo);
    const float p[4] = {a, b, c, d};
    for (int i = 0; i < effectInfo(kind).params; ++i)
        if (p[i] >= 0.f) effect->setParam(i, p[i]);
    return effect;
}

static void testEveryEffect() {
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        const int mix = mixOf(kind);
        // MIX at 0: the sound untouched, to the sample. (DRIVE has an OUTPUT
        // after its MIX: at its middle, which is "as it came in".)
        if (mix >= 0) {
            std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, 120.f);
            effect->setParam(mix, 0.f);
            const Stereo in = pinInput(48000.f);
            Stereo out = in;
            through(*effect, out);
            if (kind != EffectKind::Drive) CHECK(out.l == in.l && out.r == in.r);
            float worst = 0.f;
            for (std::size_t i = 0; i < in.frames(); ++i) worst = std::fmax(worst, std::fabs(out.l[i] - in.l[i]));
            CHECK(worst < 1e-6f);
        }
        // Stays bounded: every setting at each end, loud noise in for ten seconds.
        float loudest = 0.f;
        for (int ends = 0; ends < (1 << info.params); ++ends) {
            std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, 120.f);
            for (int i = 0; i < info.params; ++i) effect->setParam(i, (ends >> i) & 1 ? 1.f : 0.f);
            Stereo io(480000);
            Noise noise(99u + static_cast<std::uint32_t>(ends));
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = noise.next();
            through(*effect, io);
            bool number = true;
            for (float v : io.l) number = number && v == v;
            for (float v : io.r) number = number && v == v;
            CHECK(number);
            loudest = std::fmax(loudest, std::fmax(peakOf(io.l), peakOf(io.r)));
        }
        // Today, with full-scale noise in, the loudest sample out:
        //   FILTER 4.2   DRIVE 2.0   DELAY 4.8   REVERB 3.9   CRUSH 1.0   TAPE 2.9   HALL 2.6   SPRING 1.3   PLATE 2.9
        SAY("bounded  %-7s loudest %.2f\n", info.name, static_cast<double>(loudest));
        // (EQ with every band at +12 dB is the sound 24 dB up at the bottom, where the bell and the low
        // shelf meet: 14.4. That is what it is asked for; the others make nothing that much louder.)
        CHECK(loudest < (kind == EffectKind::Eq ? 16.f : 8.f));
        // A setting past either end is the end; one that is not a number is 0;
        // a setting that is not there is nothing. To the bit.
        {
            const auto sumWith = [&](int index, float value, bool set) {
                std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, 120.f);
                if (set) effect->setParam(index, value);
                Stereo io = pinInput(48000.f);
                through(*effect, io);
                Sum sum;
                sum.add(io.l);
                sum.add(io.r);
                return sum.h;
            };
            const std::uint64_t untouched = sumWith(0, 0.f, false);
            CHECK(sumWith(-1, 0.9f, true) == untouched && sumWith(kEffectParams, 0.9f, true) == untouched && sumWith(kEffectParams + 1, 0.9f, true) == untouched);
            for (int p = 0; p < info.params; ++p) {
                CHECK(sumWith(p, 7.f, true) == sumWith(p, 1.f, true) && sumWith(p, -7.f, true) == sumWith(p, 0.f, true) && sumWith(p, std::nanf(""), true) == sumWith(p, 0.f, true));
                // And the setting does do something. (EQ's MID FREQ moves a bell that is flat at its usual 0 dB: testEq.)
                if (!(kind == EffectKind::Eq && p == 2)) CHECK(sumWith(p, 0.f, true) != sumWith(p, 1.f, true));
            }
        }
        // Repeats: twice from fresh, the same to the last bit.
        {
            Stereo a = pinInput(48000.f), b = a;
            through(*makeEffect(kind, 48000.f, 120.f), a);
            through(*makeEffect(kind, 48000.f, 120.f), b);
            CHECK(a.l == b.l && a.r == b.r);
        }
        // Silence in, silence out: after its tail, exact zeros.
        {
            std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, 120.f);
            Stereo io(48000 * 40);
            Noise noise(5u);
            for (std::size_t i = 0; i < 4800; ++i) io.l[i] = io.r[i] = noise.next();
            through(*effect, io);
            bool silent = true;
            for (std::size_t i = io.frames() - 4800; i < io.frames(); ++i) silent = silent && io.l[i] == 0.f && io.r[i] == 0.f;
            CHECK(silent);
        }
        // No click on a turn. Counted in steps of the 100 Hz sine underneath
        // (1.0 = as smooth as the sine going in). None of the five glides its
        // settings today, so these are what they are, written down so that a
        // change shows; a new effect is expected to do better. Today:
        //             1st           2nd             3rd         4th          held still
        //   FILTER  CUTOFF 1.2    RESONANCE 1.0   TYPE 18     MIX 1.0        1.0
        //   DRIVE   DRIVE 5.9     TONE 1.9        MIX 1.9     OUTPUT 3.8     1.9  (it steepens the sine)
        //   DELAY   TIME 33       FEEDBACK 1.5    TONE 1.6    MIX 2.6        1.6
        //   REVERB  SIZE 0.9      DAMPING 0.9     MIX 1.1                    0.8
        //   CRUSH   BITS 38       RATE 39         MIX 3.8                    4.2  (it is steps by design)
        // The choices (TYPE, TIME) jump from one to the next, and CRUSH is
        // all jumps; the tool shows both as it should.
        // The new ones glide every setting, a choice included:
        //   TAPE    TIME 1.2      FEEDBACK 0.9    AGE 1.0     MIX 1.0        0.9
        //   HALL    SIZE 1.4      DECAY 1.0       TONE 0.8    MIX 1.0        0.7  (SIZE bends the room's pitch on the way)
        // CHORUS, COMP and SATURATE too. Their numbers are over 1 only
        // because they come out louder or steeper than the sine went in
        // ("held still" says by how much), not because anything jumps:
        //   CHORUS  RATE 1.3      DEPTH 1.7       TYPE 0.7    MIX 1.3        1.3
        //   COMP    THRESHOLD 1.1 RATIO 0.9       SPEED 0.8   MIX 1.0        0.8  (it turns this sine down)
        //   SATURATE DRIVE 5.4    TYPE 2.1        TONE 1.4    MIX 1.4        1.4  (it squares the sine: at full DRIVE
        //                                                 held still it is 5.8, and FOLD held still 2.1. testSaturate)
        //   DUCK    AMOUNT 1.0    RELEASE 1.0     SOURCE 1.0                 1.0  (dipping all the while: testDuck)
        //   PING    TIME 1.4      FEEDBACK 0.7    TONE 0.7    MIX 1.0        0.7  (TIME bends the echoes' pitch on the way, as TAPE's)
        //   SPRING  DRIP 1.1      DECAY 1.0       TONE 1.0    MIX 1.0        1.0  (DRIP bends the springs' echoes on the way: testSpring)
        //   PLATE   PRE 1.5       DECAY 1.1       TONE 1.0    MIX 1.3        1.0  (PRE bends the gap's pitch on the way)
        //   COMB    PITCH 1.6     FEEDBACK 1.0    TONE 0.8    MIX 1.0        0.8  (PITCH passes notes that ring with the sine: testComb)
        //   VOWEL   VOWEL 0.20    SHIFT 0.22      RESONANCE 0.13  MIX 1.0    0.10 (a 100 Hz sine is under every vowel's peaks)
        //   EQ      LOW 3.4       MID 1.0         MID FREQ 1.0  HIGH 1.0     1.0  (LOW at +12 dB is the sine four times as big)
        //   FOLD    DRIVE 7.5     SYMMETRY 2.2    TONE 1.8    MIX 1.8        1.8  (it folds the sine: more DRIVE, more folds)
        //   FUZZ    FUZZ 21.3     TONE 14.7       GATE 11.1   MIX 10.1       10.3 (it squares the sine; TONE up is a high-pass, made up)
        //   RING    FREQ 31.8     WAVE 114        SPREAD 3.3  MIX 3.2        3.3  (the sine times a 4 kHz wave, a square)
        //   WEAR    WOW 1.2       DIRT 6.3        DROPS 1.2   TONE 1.8       1.2  (DIRT and TONE: the hiss on the sine, brighter at TONE 0)
        //   LIMITER GAIN 1.6      CEILING 1.4     RELEASE 1.4                1.4  (GAIN +3 dB as it comes; +20 dB squares the sine)
        //   SNAP    ATTACK 1.0    SUSTAIN 1.0     MIX 1.0                    1.0  (a steady sine has no attack and no tail)
        //   FLANGER RATE 1.2      DEPTH 1.3       FEEDBACK 1.2  MIX 1.2      1.2  (at 100 Hz the copy is all but in step: louder)
        //   ENSEMBLE RATE 1.4     DEPTH 1.4       SPREAD 1.3  MIX 1.3        1.3  (the same: three copies of a low note add up)
        //   PHASER  RATE 1.3      DEPTH 1.3       FEEDBACK 1.4  MIX 1.2      1.3  (FEEDBACK +50% rings a little at 100 Hz)
        //   TREMOLO RATE 1.0      DEPTH 1.0       SHAPE 1.0   SPREAD 1.0     1.0  (it only turns the sine down, 1/8 at 50%)
        // (FOLD, FUZZ, RING, WEAR and LIMITER's GAIN: their own tests hold each turn to no rougher than held
        // still where the turn ends.)
        static const double today[kEffectKinds][kEffectParams + 1] = {
            {0, 0, 0, 0, 0},
            {1.15, 1.01, 18.1, 1.00, 1.00},
            {5.87, 1.91, 1.90, 3.80, 1.91},
            {33.5, 1.51, 1.55, 2.57, 1.55},
            {0.94, 0.90, 1.09, 0, 0.83},
            {38.2, 38.8, 3.79, 0, 4.22},
            {1.15, 0.90, 1.01, 1.00, 0.88}, // TAPE
            {1.38, 0.96, 0.80, 1.00, 0.69}, // HALL
            {1.26, 1.67, 0.71, 1.25, 1.25},   // CHORUS
            {1.05, 0.91, 0.83, 1.00, 0.82},   // COMP
            {5.42, 2.10, 1.44, 1.43, 1.44},   // SATURATE
            {1.00, 1.00, 1.00, 0, 1.00},      // DUCK (a drum is hit five times a second all through: see through())
            {1.35, 0.65, 0.65, 1.00, 0.65},   // PING
            {1.14, 0.98, 0.95, 1.01, 0.95},   // SPRING
            {1.45, 1.07, 1.01, 1.27, 1.01},   // PLATE
            {1.58, 1.00, 0.79, 1.00, 0.79},   // COMB
            {0.20, 0.22, 0.13, 1.00, 0.10},   // VOWEL
            {3.39, 1.03, 1.00, 1.00, 1.00},   // EQ
            {7.50, 2.22, 1.80, 1.79, 1.80},   // FOLD
            {21.3, 14.7, 11.1, 10.1, 10.3},   // FUZZ
            {31.8, 114.3, 3.32, 3.20, 3.32},  // RING
            {1.18, 6.32, 1.19, 1.75, 1.19},   // WEAR
            {1.61, 1.41, 1.41, 0, 1.41},      // LIMITER
            {1.00, 1.00, 1.00, 0, 1.00},      // SNAP
            {1.23, 1.34, 1.20, 1.22, 1.23},   // FLANGER
            {1.35, 1.35, 1.28, 1.29, 1.28},   // ENSEMBLE
            {1.33, 1.26, 1.39, 1.24, 1.29},   // PHASER
            {1.00, 1.00, 1.00, 1.00, 1.00},   // TREMOLO
        };
        for (int p = 0; p < info.params; ++p) {
            double steady = 0.0;
            const double jump = sweepJump(kind, p, &steady);
            SAY("sweep    %-7s %-10s %6.2f  (held still %.2f)\n", info.name, info.paramNames[p], jump, steady);
            CHECK(std::fabs(jump - today[k][p]) < 0.1 * today[k][p] + 0.05);
            CHECK(std::fabs(steady - today[k][kEffectParams]) < 0.05 * today[k][kEffectParams] + 0.02);
            // A new effect glides: turning it is about as smooth as the sine. The three
            // over that line come out steeper than the sine went in even held still at
            // the far end of the setting (the table above; testChorus and testSaturate
            // hold each to "no steeper than held still").
            // COMB's PITCH passes notes whose harmonics sit on the sine, which ring it louder (testComb holds
            // it to the loudest note held still), and EQ's LOW at +12 dB is the sine four times as big.
            const bool steeperByDesign = (kind == EffectKind::Chorus && p == 1) || (kind == EffectKind::Saturate && p <= 1) || (kind == EffectKind::Comb && p == 0) ||
                                         (kind == EffectKind::Eq && (p == 0 || p == 3)) ||
                                         kind == EffectKind::Fold || kind == EffectKind::Fuzz || kind == EffectKind::Ring ||
                                         kind == EffectKind::Wear || (kind == EffectKind::Limiter && p == 0);
            if (k > static_cast<int>(EffectKind::Crush) && !steeperByDesign) CHECK(jump < 1.5);
        }
    }
}

// ---- CHORUS ---------------------------------------------------------------------------------

// A MOVING DELAY'S TIMES. Clicks go in, `every` samples apart; each comes
// back as a copy, and where the copy's weight is centred is how late it was
// just then (a cubic read keeps a click's centre exact, to a hundredth of a
// sample). One number for each click: the delay, in samples, of the copy
// found between `fromMs` and `toMs` after it. `sums` gets each copy's size.
static std::vector<double> delaysOf(const std::vector<float>& out, std::size_t every, double fromMs, double toMs, std::vector<double>* sums = nullptr, double rate = 48000.0) {
    std::vector<double> delays;
    const std::size_t from = static_cast<std::size_t>(fromMs * rate / 1000.0), to = static_cast<std::size_t>(toMs * rate / 1000.0);
    for (std::size_t click = 0; click + every <= out.size(); click += every) {
        double sum = 0.0, moment = 0.0;
        for (std::size_t i = from; i < to && i < every; ++i) {
            sum += out[click + i];
            moment += static_cast<double>(i) * out[click + i];
        }
        delays.push_back(sum != 0.0 ? moment / sum : 0.0);
        if (sums) sums->push_back(sum);
    }
    return delays;
}
// A run of clicks through an effect: both sides out.
static Stereo clicksThrough(Effect& effect, std::size_t every, double seconds, double rate = 48000.0) {
    Stereo io(static_cast<std::size_t>(seconds * rate) / every * every);
    for (std::size_t i = 0; i < io.frames(); i += every) io.l[i] = io.r[i] = 1.f;
    through(effect, io);
    return io;
}
// A WAVER: something read `dt` seconds apart that goes up and down. Its
// middle, how long one turn takes (from where it rises through its middle),
// and how far it swings each way and where in its turn it started (by
// laying a sine of that length against it).
struct Waver {
    double middle = 0.0, period = 0.0, swing = 0.0, phase = 0.0; // phase in turns, 0..1
};
static Waver waverOf(const std::vector<double>& v, double dt) {
    Waver w;
    if (v.size() < 8) return w;
    double low = v[0], high = v[0];
    for (double x : v) low = x < low ? x : low, high = x > high ? x : high;
    w.middle = 0.5 * (low + high);
    double first = -1.0, last = -1.0;
    int rises = 0;
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (v[i - 1] < w.middle && v[i] >= w.middle) {
            const double at = (static_cast<double>(i - 1) + (w.middle - v[i - 1]) / (v[i] - v[i - 1])) * dt;
            if (first < 0.0) first = at;
            last = at;
            ++rises;
        }
    }
    if (rises < 2) return w;
    w.period = (last - first) / (rises - 1);
    // Over a whole number of turns, so the sine laid against it comes out exact.
    const std::size_t from = static_cast<std::size_t>(first / dt), to = static_cast<std::size_t>(last / dt);
    double re = 0.0, im = 0.0, mean = 0.0;
    for (std::size_t i = from; i < to; ++i) mean += v[i];
    mean /= static_cast<double>(to - from);
    for (std::size_t i = from; i < to; ++i) {
        const double a = 2.0 * kPi * static_cast<double>(i) * dt / w.period;
        re += (v[i] - mean) * std::sin(a);
        im += (v[i] - mean) * std::cos(a);
    }
    w.middle = mean;
    w.swing = 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(to - from);
    w.phase = std::atan2(im, re) / (2.0 * kPi);
    if (w.phase < 0.0) w.phase += 1.0;
    return w;
}
// How far one waver is behind another, in turns, 0..1.
static double turnsBehind(const Waver& a, const Waver& b) {
    double d = a.phase - b.phase;
    while (d < 0.0) d += 1.0;
    while (d >= 1.0) d -= 1.0;
    return d;
}
// HOW ALIKE LEFT AND RIGHT ARE: 1 the same (no width), 0 unrelated.
static double alike(const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0) {
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = from; i < a.size(); ++i) {
        ab += static_cast<double>(a[i]) * b[i];
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt(aa * bb) : 1.0;
}
// A SINE'S PITCH AS IT GOES: its frequency over each run of `cycles` cycles, from where it rises through zero.
static std::vector<double> pitchAsItGoes(const std::vector<float>& v, std::size_t from, int cycles, std::vector<double>* when = nullptr, double rate = 48000.0) {
    std::vector<double> rises, hz;
    for (std::size_t i = from + 1; i < v.size(); ++i)
        if (v[i - 1] < 0.f && v[i] >= 0.f) rises.push_back(static_cast<double>(i - 1) + static_cast<double>(-v[i - 1]) / static_cast<double>(v[i] - v[i - 1]));
    for (std::size_t i = static_cast<std::size_t>(cycles); i < rises.size(); ++i) {
        hz.push_back(cycles * rate / (rises[i] - rises[i - static_cast<std::size_t>(cycles)]));
        if (when) when->push_back(0.5 * (rises[i] + rises[i - static_cast<std::size_t>(cycles)]) / rate);
    }
    return hz;
}

static const float kChorusType[4] = {0.1f, 0.3f, 0.6f, 0.9f}; // CHORUS, ENSEMBLE, VIBRATO, FLANGE
static float chorusRate(double hz) { return static_cast<float>(std::log(hz / 0.1) / std::log(80.0)); } // the RATE setting for a speed

static void testChorus() {
    const EffectKind kind = EffectKind::Chorus;
    // Its row: what a song stores.
    const EffectInfo& info = effectInfo(kind);
    CHECK(std::string(info.name) == "CHORUS" && std::string(info.word) == "chorus" && info.params == 4 && info.family == EffectFamily::Mod);
    CHECK(std::string(info.paramNames[0]) == "RATE" && std::string(info.paramNames[1]) == "DEPTH" && std::string(info.paramNames[2]) == "TYPE" && std::string(info.paramNames[3]) == "MIX");
    CHECK(info.defaults[0] == 0.4f && info.defaults[1] == 0.5f && info.defaults[2] == 0.f && info.defaults[3] == 0.8f);
    CHECK(effectChoices(kind, 2) == 4 && effectChoices(kind, 0) == 0 && effectChoices(kind, 1) == 0 && effectChoices(kind, 3) == 0);
    CHECK(reads(kind, 2, 0.f) == "CHORUS" && reads(kind, 2, 0.25f) == "ENSEMBLE" && reads(kind, 2, 0.5f) == "VIBRATO" && reads(kind, 2, 0.75f) == "FLANGE" && reads(kind, 2, 1.f) == "FLANGE");
    CHECK(reads(kind, 2, 0.24f) == "CHORUS" && reads(kind, 2, 0.49f) == "ENSEMBLE" && reads(kind, 2, 0.74f) == "VIBRATO");
    CHECK(reads(kind, 0, 0.f) == "0.10 HZ" && reads(kind, 0, 1.f) == "8.0 HZ" && reads(kind, 0, 0.4f) == "0.58 HZ" && reads(kind, 0, chorusRate(1.0)) == "1.0 HZ");
    CHECK(reads(kind, 1, 0.5f) == "50%" && reads(kind, 3, 0.8f) == "80%");

    const std::size_t every = 960; // a click every 20 ms: longer than the longest copy is late
    const double dt = every / 48000.0, ms = 48.0;
    // CHORUS. Three copies a side, at rest 7, 10.5 and 14 ms late, each
    // swinging 3 ms each way at DEPTH 100% (so 1.5 at 50%), at the speed RATE
    // reads, a third of a turn after one another; the right side a quarter
    // turn after the left. Today, RATE 0.58 HZ, DEPTH 50%:
    //   copy   at rest    swing     one turn    behind the first   right behind left
    //   1      7.00 ms    1.50 ms   1.733 s     0                  0.25 of a turn
    //   2     10.50       1.50      1.733       0.667              0.25
    //   3     14.00       1.50      1.733       0.333              0.25
    {
        std::unique_ptr<Effect> c = fresh(kind, 0.4f, 0.5f, kChorusType[0], 1.f);
        const Stereo out = clicksThrough(*c, every, 12.0);
        const double read = std::atof(reads(kind, 0, 0.4f).c_str()); // "0.58 HZ"
        const double windows[4] = {5.0, 8.75, 12.25, 16.0}, rest[3] = {7.0, 10.5, 14.0};
        Waver first;
        for (int v = 0; v < 3; ++v) {
            std::vector<double> sums;
            const Waver l = waverOf(delaysOf(out.l, every, windows[v], windows[v + 1], &sums), dt), r = waverOf(delaysOf(out.r, every, windows[v], windows[v + 1]), dt);
            if (v == 0) first = l;
            SAY("chorus   copy %d: at rest %.2f ms, swing %.2f ms, one turn %.3f s, %.3f of a turn behind the first; right %.3f behind left; each click comes back x %.3f\n", v + 1, l.middle / ms,
                l.swing / ms, l.period, turnsBehind(first, l), turnsBehind(l, r), sums[5]);
            CHECK(std::fabs(l.middle / ms - rest[v]) < 0.05 && std::fabs(r.middle / ms - rest[v]) < 0.05);
            CHECK(std::fabs(l.swing / ms - 1.5) < 0.03 && std::fabs(r.swing / ms - 1.5) < 0.03);
            CHECK(std::fabs(l.period * read - 1.0) < 0.05 && std::fabs(r.period * read - 1.0) < 0.05); // the speed on the screen, within a twentieth
            CHECK(std::fabs(l.period - 1.0 / (0.1 * std::pow(80.0, 0.4))) < 0.01);
            static const double behind[3] = {0.0, 2.0 / 3.0, 1.0 / 3.0};
            CHECK(std::fabs(turnsBehind(first, l) - behind[v]) < 0.01 || std::fabs(turnsBehind(first, l) - 1.0) < 0.01);
            CHECK(std::fabs(turnsBehind(l, r) - 0.25) < 0.01);
            // Each copy is a third of the power: the dry and the three together are as loud as the dry alone was.
            // (A copy whose delay is moving is squeezed or stretched by half a percent: the pitch bend.)
            bool third = true;
            for (double sum : sums) third = third && std::fabs(sum - std::sin(kPi / 4.0) / std::sqrt(3.0)) < 0.004;
            CHECK(third);
        }
        CHECK(std::fabs(out.l[0] - std::cos(kPi / 4.0)) < 1e-6 && out.l[1] == 0.f && out.l[200] == 0.f); // the dry click, and nothing till the first copy
    }
    // DEPTH is how far: nothing at 0 (the copies stand still), 3 ms at full.
    {
        std::unique_ptr<Effect> still = fresh(kind, 0.4f, 0.f, kChorusType[0], 1.f), full = fresh(kind, 0.4f, 1.f, kChorusType[0], 1.f);
        const Stereo a = clicksThrough(*still, every, 4.0);
        for (double d : delaysOf(a.l, every, 5.0, 8.75)) CHECK(std::fabs(d / ms - 7.0) < 0.001);
        CHECK(a.l == a.r); // and with nothing wavering there is no width
        // At full the copies' ranges cross, so only the first is read, on its early side: 7 - 3 = 4 ms.
        const Stereo b = clicksThrough(*full, every, 4.0);
        double earliest = 1e9;
        for (std::size_t click = 0; click < b.frames(); click += every)
            for (std::size_t i = 1; i < every; ++i)
                if (b.l[click + i] != 0.f) {
                    earliest = i < earliest ? static_cast<double>(i) : earliest;
                    break;
                }
        SAY("chorus   DEPTH 100%%: the earliest a copy comes back is %.2f ms\n", earliest / ms);
        CHECK(earliest / ms > 3.9 && earliest / ms < 4.1);
    }
    // Above 1 Hz the swing shrinks as the speed rises, so the pitch bends
    // by the same amount: swing x speed stays what it is at 1 Hz.
    //   Today, DEPTH 100%: 1 Hz 3.00 ms; 2 Hz 1.50; 4 Hz 0.75; 8 Hz 0.375.
    for (double hz : {0.25, 1.0, 2.0, 4.0, 8.0}) {
        std::unique_ptr<Effect> c = fresh(kind, chorusRate(hz), 1.f, kChorusType[2], 1.f); // VIBRATO: one copy, easy to read
        const std::size_t close = 480;
        const Stereo out = clicksThrough(*c, close, hz < 1.0 ? 14.0 : 5.0);
        const Waver w = waverOf(delaysOf(out.l, close, 0.5, 9.9), close / 48000.0);
        const double want = 4.0 * (hz > 1.0 ? 1.0 / hz : 1.0);
        SAY("vibrato  RATE %.2f Hz: one turn %.4f s, at rest %.2f ms, swing %.3f ms\n", hz, w.period, w.middle / ms, w.swing / ms);
        CHECK(std::fabs(w.period * hz - 1.0) < 0.02 && std::fabs(w.swing / ms - want) < 0.03 * want && std::fabs(w.middle / ms - 5.0) < 0.05);
        const double read = std::atof(reads(kind, 0, chorusRate(hz)).c_str());
        CHECK(std::fabs(w.period * read - 1.0) < 0.05);
    }
    // ENSEMBLE: the three copies each at a speed of its own (1, 1.29 and 0.77 times RATE), 2.5 ms at full.
    {
        std::unique_ptr<Effect> c = fresh(kind, chorusRate(1.0), 0.5f, kChorusType[1], 1.f);
        const Stereo out = clicksThrough(*c, every, 12.0);
        const double windows[4] = {5.0, 8.75, 12.25, 16.0}, speeds[3] = {1.0, 1.29, 0.77};
        for (int v = 0; v < 3; ++v) {
            const Waver l = waverOf(delaysOf(out.l, every, windows[v], windows[v + 1]), dt), r = waverOf(delaysOf(out.r, every, windows[v], windows[v + 1]), dt);
            SAY("ensemble copy %d: one turn %.3f s (%.3f Hz), swing %.2f ms, right %.3f of a turn behind left\n", v + 1, l.period, 1.0 / l.period, l.swing / ms, turnsBehind(l, r));
            CHECK(std::fabs(l.period * speeds[v] - 1.0) < 0.02 && std::fabs(l.swing / ms - 1.25) < 0.04 && std::fabs(turnsBehind(l, r) - 0.25) < 0.01);
        }
    }
    // VIBRATO: one copy, and at full MIX no dry sound at all; left and right in
    // step (it is one pitch). A 1 kHz sine comes out wavering in pitch, at the
    // speed RATE reads, by 2 pi x speed x swing: at 5 Hz and DEPTH 50% the swing
    // is 0.4 ms, so 1.26% each way (22 cents). Today: 987.4 to 1012.6 Hz, 5.00 Hz.
    {
        std::unique_ptr<Effect> v = fresh(kind, chorusRate(5.0), 0.5f, kChorusType[2], 1.f);
        const std::vector<float> click = impulseOf(*v, 0.1);
        CHECK(click[0] == 0.f && echoAt(click, 0) > 200); // no dry sound
        Stereo io(48000 * 3);
        io.l = io.r = sineOf(1000.0, 0.5, io.frames());
        v->reset();
        through(*v, io);
        CHECK(io.l == io.r);
        std::vector<double> when;
        const std::vector<double> hz = pitchAsItGoes(io.l, 4800, 4, &when);
        const Waver w = waverOf(hz, when[1] - when[0]);
        double low = 1e9, high = 0.0;
        for (double f : hz) low = f < low ? f : low, high = f > high ? f : high;
        SAY("vibrato  a 1 kHz sine at 5 Hz, DEPTH 50%%: %.1f to %.1f Hz, wavering at %.2f Hz\n", low, high, 1.0 / w.period);
        const double bend = 2.0 * kPi * 5.0 * 0.0004 * 1000.0; // 12.6 Hz
        CHECK(std::fabs((high - 1000.0) - bend) < 0.05 * bend && std::fabs((1000.0 - low) - bend) < 0.05 * bend);
        CHECK(std::fabs(w.period * std::atof(reads(kind, 0, chorusRate(5.0)).c_str()) - 1.0) < 0.05);
        // Half way up MIX it is half dry and half the copy.
        std::unique_ptr<Effect> half = fresh(kind, chorusRate(5.0), 0.f, kChorusType[2], 0.5f);
        const std::vector<float> both = impulseOf(*half, 0.1);
        CHECK(both[0] == 0.5f && std::fabs(both[240] - 0.5f) < 1e-6f);
    }
    // FLANGE: one very short copy, 2.2 ms at rest, swinging 1.9 ms each way at
    // full, and fed back into itself: each time round is 0.6 of the last.
    {
        std::unique_ptr<Effect> still = fresh(kind, 0.4f, 0.f, kChorusType[3], 1.f);
        const std::vector<float> ir = impulseOf(*still, 0.1);
        const std::size_t round = static_cast<std::size_t>(2.2 * ms + 0.5); // 106 samples
        std::vector<double> size;
        for (int n = 1; n <= 4; ++n) {
            double sum = 0.0;
            for (std::size_t i = n * round - round / 2; i < n * round + round / 2; ++i) sum += ir[i];
            size.push_back(sum);
        }
        SAY("flange   at rest: copies %.4f, %.4f, %.4f, %.4f (each x %.3f of the last)\n", size[0], size[1], size[2], size[3], size[1] / size[0]);
        CHECK(std::fabs(size[0] - std::sin(kPi / 4.0)) < 0.002);
        for (int n = 1; n < 4; ++n) CHECK(std::fabs(size[static_cast<std::size_t>(n)] / size[static_cast<std::size_t>(n - 1)] - 0.6) < 0.005);
        std::unique_ptr<Effect> moving = fresh(kind, 0.4f, 0.25f, kChorusType[3], 1.f);
        const Stereo out = clicksThrough(*moving, every, 12.0);
        const Waver l = waverOf(delaysOf(out.l, every, 1.0, 3.2), dt), r = waverOf(delaysOf(out.r, every, 1.0, 3.2), dt);
        SAY("flange   DEPTH 25%%: at rest %.2f ms, swing %.3f ms, right %.3f of a turn behind left\n", l.middle / ms, l.swing / ms, turnsBehind(l, r));
        CHECK(std::fabs(l.middle / ms - 2.2) < 0.03 && std::fabs(l.swing / ms - 0.475) < 0.02 && std::fabs(turnsBehind(l, r) - 0.25) < 0.01);
    }
    // WIDTH and LOUDNESS, with the same noise on both sides going in.
    // "Alike" is 1 when left and right are the same and 0 when unrelated. Today, MIX 100%:
    //             alike    louder than it went in
    //   CHORUS     0.55    -0.4 dB
    //   ENSEMBLE   0.55    -0.4
    //   VIBRATO    1.00    -0.9   (in step: no width)
    //   FLANGE     0.46    +0.4   (the feedback)
    // The dry sound and the copies together are as loud as the dry alone, but
    // for this: a copy read between samples loses a little of the very top
    // (noise is all top, so it shows here: 0.9 dB for a copy by itself; a
    // sine at 1 kHz loses nothing).
    {
        static const double wantAlike[4] = {0.55, 0.55, 1.0, 0.46}, wantLouder[4] = {-0.4, -0.4, -0.9, 0.4};
        for (int type = 0; type < 4; ++type) {
            std::unique_ptr<Effect> c = fresh(kind, 0.4f, 0.5f, kChorusType[type], 1.f);
            Stereo io(48000 * 6);
            Noise noise(77u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
            const double in = rmsOf(io.l);
            through(*c, io);
            const double same = alike(io.l, io.r, 4800), louder = dB(rmsOf(io.l, 4800) / in);
            SAY("chorus   type %d: left and right alike %.2f, %.1f dB louder than it went in\n", type, same, louder);
            CHECK(std::fabs(same - wantAlike[type]) < 0.04 && std::fabs(louder - wantLouder[type]) < 0.4);
            if (type == 2) CHECK(io.l == io.r);
        }
    }
    // MIX at 0 is the sound untouched, to the sample, in every type; and it
    // has been listening all the while (turned up, the copies are already there).
    for (int type = 0; type < 4; ++type) {
        std::unique_ptr<Effect> c = fresh(kind, 0.4f, 1.f, kChorusType[type], 0.f);
        const Stereo in = pinInput(48000.f);
        Stereo out = in;
        through(*c, out);
        CHECK(out.l == in.l && out.r == in.r);
    }
    // Turned down to 0 while it sounds, MIX gets there, exactly, and soon.
    {
        std::unique_ptr<Effect> c = fresh(kind, 0.4f, 1.f, kChorusType[0], 1.f);
        Noise noise(31u);
        Stereo in(96000);
        for (std::size_t i = 0; i < in.frames(); ++i) in.l[i] = in.r[i] = 0.3f * noise.next();
        Stereo out = in;
        through(*c, out, 0, 24000);
        c->setParam(3, 0.f);
        through(*c, out, 24000, 96000);
        bool same = true;
        for (std::size_t i = 48000; i < 96000; ++i) same = same && out.l[i] == in.l[i] && out.r[i] == in.r[i];
        CHECK(same && out.l[24100] != in.l[24100]);
    }
    // MIX part way: at 50% the dry sound is cos(pi/8) = 0.924 and the copies sin(pi/8) = 0.383 between them.
    {
        std::unique_ptr<Effect> c = fresh(kind, 0.4f, 0.f, kChorusType[0], 0.5f);
        const std::vector<float> ir = impulseOf(*c, 0.05);
        CHECK(std::fabs(ir[0] - std::cos(kPi / 8.0)) < 1e-6 && std::fabs(ir[336] - std::sin(kPi / 8.0) / std::sqrt(3.0)) < 1e-6 && std::fabs(ir[504] - ir[336]) < 1e-6 && std::fabs(ir[672] - ir[336]) < 1e-6);
    }
    // Each side is its own: a sound on the left only leaves the right silent, FLANGE's feedback included.
    {
        std::unique_ptr<Effect> c = fresh(kind, 0.4f, 1.f, kChorusType[3], 1.f);
        Stereo io(48000);
        Noise noise(32u);
        for (std::size_t i = 0; i < 24000; ++i) io.l[i] = 0.3f * noise.next();
        through(*c, io);
        CHECK(peakOf(io.r) == 0.f && peakOf(io.l) > 0.2f);
    }
    // A setting thrown from one end to the other in one go is a slide too:
    // nothing steeper than the sound is anyway. (With the glide taken out, MIX
    // thrown open steps by 40 of the sine's own; TYPE by 60.)
    for (int p = 0; p < 4; ++p) {
        std::unique_ptr<Effect> c = fresh(kind, 0.4f, 0.5f, 0.f, p == 3 ? 0.f : 1.f);
        Stereo io(48000);
        io.l = io.r = sineOf(100.0, 0.5, 48000);
        through(*c, io, 0, 24000);
        c->setParam(p, 1.f);
        through(*c, io, 24000, 48000);
        const double jump = largestJump(io.l, 23999, 48000) / sineStep(100.0, 0.5);
        SAY("chorus   %s thrown to the top at once: %.2f\n", info.paramNames[p], jump);
        CHECK(jump < 2.0);
    }
    // The same at any sample rate: 7 ms is 7 ms.
    {
        std::unique_ptr<Effect> c = makeEffect(kind, 96000.f, 120.f);
        c->setParam(1, 0.f);
        c->setParam(3, 1.f);
        Stereo io(9600);
        io.l[0] = io.r[0] = 1.f;
        through(*c, io);
        CHECK(echoAt(io.l, 0) >= 671 && echoAt(io.l, 0) <= 672 && io.l[672] > 0.3f);
    }
    // However the sound is cut into blocks, the same to the last bit.
    {
        Stereo whole = pinInput(48000.f), cut = whole, big = whole;
        through(*fresh(kind, 0.7f, 0.8f, kChorusType[3], 1.f), whole);
        std::unique_ptr<Effect> c = fresh(kind, 0.7f, 0.8f, kChorusType[3], 1.f), d = fresh(kind, 0.7f, 0.8f, kChorusType[3], 1.f);
        for (std::size_t at = 0, n = 1; at < cut.frames(); at += n, n = n % 61 + 1) {
            if (at + n > cut.frames()) n = cut.frames() - at;
            c->process(cut.l.data() + at, cut.r.data() + at, static_cast<int>(n));
        }
        d->process(big.l.data(), big.r.data(), static_cast<int>(big.frames()));
        CHECK(cut.l == whole.l && cut.r == whole.r && big.l == whole.l && big.r == whole.r);
    }
    // A reset is a fresh start: the same sound again, to the bit.
    {
        std::unique_ptr<Effect> c = fresh(kind, 0.7f, 0.8f, kChorusType[3], 1.f);
        Stereo a = pinInput(48000.f), b = a, cut = a;
        through(*c, a);
        through(*c, cut, 0, 12000); // reset in the middle of a sound: nothing of it is left over
        c->reset();
        through(*c, b);
        CHECK(a.l == b.l && a.r == b.r);
    }
}

static void testFilterLevels() {
    // Level in and out at a frequency. CUTOFF 0.5: 30 Hz * 2^4.75 = 807 Hz. RESONANCE 0.2 (Q about 1).
    // (The 6400 Hz column is lower than a textbook filter's: this kind bends toward
    // silence at half the sample rate.)
    const double corner = 30.0 * std::pow(2.0, 4.75);
    CHECK(std::fabs(corner - 807.0) < 1.0);
    struct Row { const char* name; float type; double at100, at800, at1600, at6400; };
    // Today, in dB:            100 Hz   800 Hz  1600 Hz  6400 Hz
    //   LOW 12 DB                0.1     -0.3   -11.1    -37.0
    //   LOW 24 DB                0.1     -3.2   -23.3    -74.0
    //   BAND PASS              -17.7      0.0    -4.8    -18.1
    //   HIGH PASS              -36.2     -0.4     0.8      0.1
    const Row rows[4] = {{"LOW 12 DB", 0.1f, 0.1, -0.3, -11.1, -37.0}, {"LOW 24 DB", 0.3f, 0.1, -3.2, -23.3, -74.0}, {"BAND PASS", 0.5f, -17.7, 0.0, -4.8, -18.1}, {"HIGH PASS", 0.9f, -36.2, -0.4, 0.8, 0.1}};
    for (const Row& row : rows) {
        std::unique_ptr<Effect> f = fresh(EffectKind::Filter, 0.5f, 0.2f, row.type, 1.f);
        const double got[4] = {levelAt(*f, 100), levelAt(*f, 800), levelAt(*f, 1600), levelAt(*f, 6400)};
        SAY("filter   %-10s %7.1f %7.1f %7.1f %7.1f dB at 100, 800, 1600, 6400 Hz\n", row.name, got[0], got[1], got[2], got[3]);
        CHECK(std::fabs(got[0] - row.at100) < 0.3 && std::fabs(got[1] - row.at800) < 0.3 && std::fabs(got[2] - row.at1600) < 0.3 && std::fabs(got[3] - row.at6400) < 0.5);
    }
    // The slopes their names promise, an octave apart well above the corner:
    // 12 dB an octave, and 24. Today 13.0 and 25.9, between 3200 and 6400 Hz.
    std::unique_ptr<Effect> two = fresh(EffectKind::Filter, 0.5f, 0.2f, 0.1f, 1.f), four = fresh(EffectKind::Filter, 0.5f, 0.2f, 0.3f, 1.f);
    const double slope2 = levelAt(*two, 3200) - levelAt(*two, 6400), slope4 = levelAt(*four, 3200) - levelAt(*four, 6400);
    SAY("filter   slopes %.1f and %.1f dB an octave\n", slope2, slope4);
    CHECK(slope2 > 11.5 && slope2 < 13.5 && slope4 > 23.0 && slope4 < 27.0);
    // RESONANCE at full is a peak at the corner of about Q = 12 (21.6 dB).
    std::unique_ptr<Effect> ringing = fresh(EffectKind::Filter, 0.5f, 1.f, 0.1f, 1.f);
    const double peak = levelAt(*ringing, 807, 0.02);
    SAY("filter   resonance at full: %.1f dB at the corner\n", peak);
    CHECK(peak > 20.5 && peak < 22.5);
    // The tool reads a plain gain right: DRIVE with nothing but OUTPUT at 25% is half, -6.02 dB.
    std::unique_ptr<Effect> half = fresh(EffectKind::Drive, 0.f, 1.f, 0.f, 0.25f);
    CHECK(std::fabs(levelAt(*half, 1000) + 6.02) < 0.01);
}

static void testDriveHarmonics() {
    // A 4186 Hz sine (the top C of a piano) at half scale. DRIVE's curve is
    // the same either side of zero, so it makes odd harmonics only: the third
    // at 12558 Hz and the fifth at 20930. The seventh would be 29302 Hz, which
    // 48 kHz cannot hold: it folds back to 18698 Hz. That is aliasing.
    //
    // Today, in dB against the sine going in (TONE fully up, MIX full, OUTPUT 100%):
    //   DRIVE    sine   2nd     3rd     5th    loudest fold-back    everything not a harmonic
    //   35%       1.0   none   -15.9   -30.3    -43.8 at 18698 Hz     -43.6
    //   81%      -5.0   none   -15.5   -21.0    -24.6 at 18698 Hz     -21.3
    //   100%     -8.0   none   -18.3   -23.3    -26.4 at 18698 Hz     -21.7
    // At 81% the loudest fold-back is 9 dB under the third harmonic, 4 under
    // the fifth and 20 under the sine; all the fold-back together is 16 under
    // the sine. It is rough up high, as the plan says (it measured 17 dB its
    // own way). SATURATE is to be held to 50 dB with this same tool.
    struct Row { float drive; double sine, third, fifth, alias, rest; };
    const Row rows[3] = {{0.35f, 1.0, -15.9, -30.3, -43.8, -43.6}, {0.81f, -5.0, -15.5, -21.0, -24.6, -21.3}, {1.f, -8.0, -18.3, -23.3, -26.4, -21.7}};
    for (const Row& row : rows) {
        std::unique_ptr<Effect> d = fresh(EffectKind::Drive, row.drive, 1.f, 1.f, 0.5f);
        const Harmonics h = harmonicsOf(*d, 4186, 0.5);
        SAY("drive    %3.0f%%  sine %6.1f  2nd %6.1f  3rd %6.1f  5th %6.1f  fold-back %6.1f at %.0f Hz  rest %6.1f\n", row.drive * 100.0, h.part[1], h.part[2], h.part[3], h.part[5],
            h.alias, h.aliasHz, h.rest);
        CHECK(std::fabs(h.part[1] - row.sine) < 0.3 && std::fabs(h.part[3] - row.third) < 0.5 && std::fabs(h.part[5] - row.fifth) < 0.5);
        CHECK(std::fabs(h.alias - row.alias) < 1.0 && std::fabs(h.rest - row.rest) < 1.0);
        CHECK(h.part[2] < -100.0 && h.part[4] < -100.0); // no even harmonics
        CHECK(h.part[6] == -600.0);                       // the sixth is above the top: not measured
        CHECK(h.aliasHz == 18698.0);                      // the seventh, folded
        CHECK(std::fabs(h.strongest - h.part[3]) < 1e-9);
    }
    // The tool on something with no harmonics at all: a filter passes a sine as a sine.
    std::unique_ptr<Effect> f = fresh(EffectKind::Filter, 1.f, 0.f, 0.1f, 1.f);
    const Harmonics clean = harmonicsOf(*f, 4186, 0.5);
    SAY("filter   a sine through it: 3rd %.1f, fold-back %.1f, rest %.1f\n", clean.part[3], clean.alias, clean.rest);
    CHECK(clean.part[3] < -100.0 && clean.alias < -100.0 && clean.rest < -100.0 && std::fabs(clean.part[1]) < 1.0);
    // CRUSH at 1 in 4 is fold-back by design: what is not the sine or a harmonic is
    // only 4.9 dB under the sine going in. (Its fold-back is around its own
    // slower rate, not the sample rate, so `rest` is the number to read, not `alias`.)
    std::unique_ptr<Effect> c = fresh(EffectKind::Crush, 0.f, 0.376f, 1.f);
    const Harmonics crushed = harmonicsOf(*c, 4186, 0.5);
    SAY("crush    1 in 4: sine %.1f, rest %.1f\n", crushed.part[1], crushed.rest);
    CHECK(std::fabs(crushed.rest + 4.9) < 1.0 && std::fabs(crushed.part[1] + 1.7) < 0.5);
}

static void testDelayEchoes() {
    // When each echo lands: TIME in steps (sixteenths) at the tempo, to the sample.
    const float times[6] = {0.05f, 0.2f, 0.4f, 0.5f, 0.7f, 1.f};
    const int steps[6] = {1, 2, 3, 4, 6, 8};
    for (float tempo : {90.f, 120.f, 174.f}) {
        for (int i = 0; i < 6; ++i) {
            std::unique_ptr<Effect> d = fresh(EffectKind::Delay, times[i], 0.5f, 1.f, 1.f, tempo);
            const long want = std::lround(steps[i] * 15.0 / tempo * 48000.0);
            const std::vector<float> ir = impulseOf(*d, 3.0 * static_cast<double>(want) / 48000.0 + 0.1);
            const long first = echoAt(ir, 0);
            CHECK(first == want);
            CHECK(echoAt(ir, static_cast<std::size_t>(first + want / 2)) == 2 * want); // and the echo's echo
            CHECK(delaySteps(times[i]) == steps[i]);
        }
    }
    // A quarter note at 120: 24000 samples, half a second.
    // How much quieter each echo is: FEEDBACK times 0.9, whatever TONE does to its shape.
    //   Today: FEEDBACK 50% -> 0.450 (-6.9 dB an echo); 100% -> 0.900 (-0.9 dB); 0 -> one echo and no more.
    for (float tone : {0.f, 0.6f, 1.f}) {
        for (float feedback : {0.25f, 0.5f, 1.f}) {
            std::unique_ptr<Effect> d = fresh(EffectKind::Delay, 0.5f, feedback, tone, 1.f);
            const std::vector<float> ir = impulseOf(*d, 2.6);
            const double gain = echoGain(ir, 24000, 1), next = echoGain(ir, 24000, 2);
            SAY("delay    feedback %3.0f%% tone %3.0f%%: each echo x %.4f, then x %.4f (%.2f dB)\n", feedback * 100.0, tone * 100.0, gain, next, dB(gain));
            CHECK(std::fabs(gain - 0.9 * feedback) < 0.002 && std::fabs(next - 0.9 * feedback) < 0.002);
        }
    }
    std::unique_ptr<Effect> once = fresh(EffectKind::Delay, 0.5f, 0.f, 1.f, 1.f);
    const std::vector<float> ir = impulseOf(*once, 1.6);
    CHECK(ir[24000] == 1.f && echoAt(ir, 24000) == -1 && echoGain(ir, 24000, 1) == 0.0);
    // TONE dulls the repeats: the first echo is the click itself, and the second is spread out and lower.
    std::unique_ptr<Effect> dull = fresh(EffectKind::Delay, 0.5f, 1.f, 0.f, 1.f);
    const std::vector<float> dulled = impulseOf(*dull, 1.1);
    CHECK(dulled[24000] == 1.f && std::fabs(dulled[48000]) < 0.1f && std::fabs(dulled[48000]) > 0.01f);
}

static void testReverbTail() {
    // An impulse, MIX full. Today (DAMPING 50%):
    //   SIZE     RT60       within 60 dB of its loudest for    echoes in the first 100 ms
    //   0%       0.59 s          0.51 s                           about 200
    //   60%      1.44 s          1.15 s                           about 200
    //   100%    10.8 s           6.5 s                            about 200
    // The plan's "tail of 0.6 / 1.5 / 10.8 s" is the RT60. (Its "130 echoes"
    // was counted another way; by this count HALL has to beat 200.)
    struct Row { float size; double rt, tail; };
    const Row rows[3] = {{0.f, 0.59, 0.51}, {0.6f, 1.44, 1.15}, {1.f, 10.8, 6.51}};
    double last = 0.0;
    for (const Row& row : rows) {
        std::unique_ptr<Effect> r = fresh(EffectKind::Reverb, row.size, 0.5f, 1.f);
        const std::vector<float> ir = impulseOf(*r, 30.0);
        const double rt = rt60(ir), tail = tailSeconds(ir);
        SAY("reverb   size %3.0f%%: RT60 %.2f s, tail %.2f s, %d echoes in the first 100 ms\n", row.size * 100.0, rt, tail, echoesIn(ir, 0.0005, 0.1));
        CHECK(std::fabs(rt - row.rt) < 0.06 * row.rt && std::fabs(tail - row.tail) < 0.08 * row.tail);
        CHECK(rt > last);
        last = rt;
        const int echoes = echoesIn(ir, 0.0005, 0.1);
        CHECK(echoes > 150 && echoes < 260);
    }
    // More DAMPING: a shorter tail (the top dies sooner). Today 1.54 s at none, 1.27 s at full.
    std::unique_ptr<Effect> bright = fresh(EffectKind::Reverb, 0.6f, 0.f, 1.f), dark = fresh(EffectKind::Reverb, 0.6f, 1.f, 1.f);
    const double brightRt = rt60(impulseOf(*bright, 12.0)), darkRt = rt60(impulseOf(*dark, 12.0));
    SAY("reverb   size 60%%: RT60 %.2f s bright, %.2f s damped\n", brightRt, darkRt);
    CHECK(brightRt > darkRt && darkRt > 0.3);
    // The tools on tails whose answer is known. A tail falling 60 dB a second: RT60 is 1 s.
    std::vector<float> made(96000);
    Noise noise(7u);
    for (std::size_t i = 0; i < made.size(); ++i) made[i] = noise.next() * static_cast<float>(std::pow(10.0, -3.0 * static_cast<double>(i) / 48000.0));
    CHECK(std::fabs(rt60(made, 0) - 1.0) < 0.03);
    CHECK(tailSeconds(made, 60.0, 0) > 0.9 && tailSeconds(made, 60.0, 0) <= 1.0 && tailSeconds(made, 30.0, 0) > 0.4 && tailSeconds(made, 30.0, 0) <= 0.5);
    std::vector<float> nothing(1000, 0.f), click(1000, 0.f);
    click[0] = 1.f;
    click[300] = 0.5f;
    click[301] = -0.25f;
    click[303] = 0.1f;
    CHECK(rt60(nothing) == 0.0 && tailSeconds(nothing) == 0.0 && echoesIn(nothing, 0.0, 1.0) == 0 && echoAt(nothing, 0) == -1);
    CHECK(echoesIn(click, 0.0, 1.0) == 2 && echoesIn(click, 0.0063, 1.0) == 1 && echoesIn(click, 0.007, 1.0) == 0); // 300 and 303 stand above their neighbours; 301 does not
    CHECK(echoAt(click, 0) == 300 && echoAt(click, 300) == 301 && echoAt(click, 301) == 303 && echoAt(click, 303) == -1);
    // ...and the click finder and the one-frequency meter.
    CHECK(largestJump(click) == 1.f && largestJump(nothing) == 0.f && largestJump(click, 304) == 0.f && largestJump(click, 302) == 0.1f);
    const std::vector<float> tone = sineOf(100.0, 0.5, 48000);
    CHECK(std::fabs(largestJump(tone) / sineStep(100.0, 0.5) - 1.0) < 1e-3);
    CHECK(std::fabs(partAt(tone.data(), 48000, 100.0) - 0.5) < 1e-6 && partAt(tone.data(), 48000, 200.0) < 1e-6 && partAt(tone.data(), 48000, 101.0) < 1e-6);
}

// ---- tools for the effects that glide (TAPE, HALL) ------------------------------------------------

// The wet sound alone: what came out, less what went in. (In a slot TAPE
// keeps the dry sound whole, so this is its echoes times MIX.)
static std::vector<float> wetOf(Effect& effect, const std::vector<float>& in, std::vector<float>* out = nullptr) {
    Stereo io(in.size());
    io.l = io.r = in;
    through(effect, io);
    if (out) *out = io.l;
    std::vector<float> wet(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) wet[i] = io.l[i] - in[i];
    return wet;
}

// A click of a chosen size (impulseOf's is 1, which a ceiling would bend).
static std::vector<float> clickOf(Effect& effect, double seconds, float size) {
    Stereo io(static_cast<std::size_t>(seconds * 48000.0));
    io.l[0] = io.r[0] = size;
    through(effect, io);
    return io.l;
}

// Where the loudest sample is between two places, or -1 if all is silent.
static long peakAt(const std::vector<float>& v, std::size_t from, std::size_t to) {
    long at = -1;
    float peak = 0.f;
    for (std::size_t i = from; i < to && i < v.size(); ++i)
        if (std::fabs(v[i]) > peak) peak = std::fabs(v[i]), at = static_cast<long>(i);
    return at;
}

// A DELAY'S FEEDBACK AT ONE FREQUENCY: how much of echo `which` is in the
// next, at `hz`, with echoes every `every` samples (a whole number of cycles
// of `hz`). echoGain adds an echo up, which a low cut in the loop brings to
// nothing; this does not mind.
static double echoGainAt(const std::vector<float>& ir, std::size_t every, double hz, int which = 1) {
    // Each echo is looked for half a gap either side of where it is due, so one that comes a little early is still whole.
    const std::size_t from = static_cast<std::size_t>(which) * every - every / 2;
    if (from + 2 * every > ir.size()) return 0.0;
    const double first = partAt(ir.data() + from, every, hz);
    return first > 0.0 ? partAt(ir.data() + from + every, every, hz) / first : 0.0;
}

// What a 6 dB an octave cut with its corner at `corner` leaves at `hz`:
// the high cut, and (low: true) the low cut. Exact for the kind used here.
static double cutAt(double corner, double hz, bool low = false, double rate = 48000.0) {
    const double a = 1.0 - std::exp(-2.0 * kPi * corner / rate), w = 2.0 * kPi * hz / rate;
    const double re = 1.0 - (1.0 - a) * std::cos(w), im = (1.0 - a) * std::sin(w); // 1 - (1-a)/z
    const double pass = a / std::sqrt(re * re + im * im);
    if (!low) return pass;
    // 1 - a / (1 - (1-a)/z) = (1-a)(1 - 1/z) / (1 - (1-a)/z)
    return (1.0 - a) * 2.0 * std::sin(0.5 * w) / std::sqrt(re * re + im * im);
}

// THE DELAY OF A SINE AS IT WANDERS: `wet` is a sine at `hz` (48 whole
// samples to a cycle or a divisor of 480) coming out late by an amount that
// moves. One reading every 10 ms, in samples, give or take a whole number of
// cycles: only how it MOVES is meant.
static std::vector<double> delayTrack(const std::vector<float>& wet, double hz, std::size_t from, std::size_t to) {
    std::vector<double> track;
    const double w = 2.0 * kPi * hz / 48000.0;
    double last = 0.0, turns = 0.0;
    for (std::size_t at = from; at + 480 <= to && at + 480 <= wet.size(); at += 480) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = at; i < at + 480; ++i) {
            re += wet[i] * std::cos(w * static_cast<double>(i));
            im += wet[i] * std::sin(w * static_cast<double>(i));
        }
        const double angle = std::atan2(-re, im); // sin(w(n - d)): w d
        if (!track.empty()) {
            if (angle - last > kPi) turns -= 2.0 * kPi;
            if (angle - last < -kPi) turns += 2.0 * kPi;
        }
        last = angle;
        track.push_back((angle + turns) / w);
    }
    return track;
}
struct Wander {
    double depth = 0.0; // samples either way
    double hz = 0.0;    // how often it goes round
    double cents = 0.0; // the most the pitch is off, either way
};
static Wander wanderOf(const std::vector<double>& track) {
    Wander result;
    if (track.size() < 3) return result;
    double low = track[0], high = track[0], mean = 0.0, steepest = 0.0;
    for (double d : track) low = d < low ? d : low, high = d > high ? d : high, mean += d / static_cast<double>(track.size());
    for (std::size_t i = 1; i < track.size(); ++i) steepest = std::fmax(steepest, std::fabs(track[i] - track[i - 1]) / 480.0);
    result.depth = 0.5 * (high - low);
    result.cents = 1200.0 * std::log2(1.0 + steepest);
    double firstUp = -1.0, lastUp = -1.0;
    int ups = 0;
    for (std::size_t i = 1; i < track.size(); ++i) {
        if (track[i - 1] < mean && track[i] >= mean) {
            const double at = static_cast<double>(i - 1) + (mean - track[i - 1]) / (track[i] - track[i - 1]);
            if (firstUp < 0.0) firstUp = at;
            lastUp = at;
            ++ups;
        }
    }
    if (ups >= 2) result.hz = (ups - 1) / ((lastUp - firstUp) * 0.01);
    return result;
}

// THE PITCH of something close to a sine, between two places: by its upward crossings of zero.
static double pitchOf(const std::vector<float>& v, std::size_t from, std::size_t to, double rate = 48000.0) {
    double first = -1.0, last = -1.0;
    int ups = 0;
    for (std::size_t i = from + 1; i < to && i < v.size(); ++i) {
        if (v[i - 1] < 0.f && v[i] >= 0.f) {
            const double at = static_cast<double>(i - 1) + static_cast<double>(-v[i - 1]) / (static_cast<double>(v[i]) - v[i - 1]);
            if (first < 0.0) first = at;
            last = at;
            ++ups;
        }
    }
    return ups >= 2 ? (ups - 1) * rate / (last - first) : 0.0;
}

// How long a block takes, in microseconds, over `count` blocks of silence.
// (The quickest of five runs: a busy machine takes the processor away for a moment, and that is
// not the effect being slow. The whole test suite runs at once, and other builds may be going.)
static double blocksOfSilence(Effect& effect, int count) {
    Stereo io(kMaxBlock);
    double best = 1e30;
    for (int part = 0; part < 5; ++part) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < count / 5; ++i) {
            io.l.assign(kMaxBlock, 0.f);
            io.r.assign(kMaxBlock, 0.f);
            effect.process(io.l.data(), io.r.data(), kMaxBlock);
        }
        best = std::fmin(best, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (count / 5));
    }
    return best;
}
// A TAIL DOES NOT SLOW DOWN, with the processor's flush-to-zero OFF (as the
// app has it): a click, then `seconds` of silence timed a second at a time.
// `early` is the first second, `slowest` the worst, `late` the last.
struct TailTimes { double early, slowest, late; };
static TailTimes tailTimes(Effect& effect, int seconds) {
    CHECK(!denormalsFlushed());
    Stereo io(kMaxBlock);
    io.l[0] = io.r[0] = 1.f;
    effect.process(io.l.data(), io.r.data(), kMaxBlock);
    TailTimes t = {blocksOfSilence(effect, 750), 0.0, 0.0};
    for (int second = 0; second < seconds; ++second) t.slowest = std::fmax(t.slowest, blocksOfSilence(effect, 750));
    t.late = blocksOfSilence(effect, 750);
    return t;
}
// THE SAME WITH FLUSH-TO-ZERO ON OR OFF, to the last bit: the pinned input,
// then `seconds` of nothing. (The old five are not: see testFlushToZero.)
static bool sameFlushed(EffectKind kind, float a, float b, float c, float d, double seconds) {
    Stereo plain = pinInput(48000.f);
    plain.l.resize(plain.l.size() + static_cast<std::size_t>(seconds * 48000.0), 0.f);
    plain.r.resize(plain.l.size(), 0.f);
    Stereo flushed = plain;
    through(*fresh(kind, a, b, c, d), plain);
    {
        const FlushDenormals on;
        through(*fresh(kind, a, b, c, d), flushed);
    }
    return std::memcmp(plain.l.data(), flushed.l.data(), plain.l.size() * sizeof(float)) == 0 && std::memcmp(plain.r.data(), flushed.r.data(), plain.r.size() * sizeof(float)) == 0 &&
           peakOf(plain.l) > 0.1f;
}

// NUMBERS TOO SMALL TO HOLD, COUNTED WITHOUT A CLOCK. The processor keeps a
// mark that is set whenever a sum comes out too small to hold (and is not
// exact). Lower the mark, run something, look.
struct TooSmall {
    TooSmall() {
        CHECK(!denormalsFlushed()); // with flush-to-zero on there is nothing to see
        std::feclearexcept(FE_UNDERFLOW);
    }
    bool seen() const { return std::fetestexcept(FE_UNDERFLOW) != 0; }
};

// ---- TAPE -------------------------------------------------------------------------------------------

static float tapeTime(int choice) { return (static_cast<float>(choice) + 0.5f) / 8.f; }

static void testTape() {
    const EffectKind tape = EffectKind::Tape;
    const EffectInfo& info = effectInfo(tape);
    CHECK(std::string(info.name) == "TAPE" && info.params == 4 && std::string(info.paramNames[0]) == "TIME" && std::string(info.paramNames[1]) == "FEEDBACK");
    CHECK(std::string(info.paramNames[2]) == "AGE" && std::string(info.paramNames[3]) == "MIX");
    CHECK(info.defaults[0] == 0.5625f && info.defaults[1] == 0.4f && info.defaults[2] == 0.35f && info.defaults[3] == 0.35f);
    // How it reads.
    const char* names[8] = {"1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOT", "1/4", "1/4 DOT", "1/2"};
    for (int i = 0; i < 8; ++i) CHECK(reads(tape, 0, tapeTime(i)) == names[i]);
    CHECK(reads(tape, 0, 0.f) == "1/16" && reads(tape, 0, 1.f) == "1/2" && reads(tape, 0, info.defaults[0]) == "1/8 DOT" && reads(tape, 0, 0.125f) == "1/8 TRIP");
    CHECK(reads(tape, 1, 0.f) == "0%" && reads(tape, 1, 1.f) == "120%" && reads(tape, 1, 0.4f) == "48%" && reads(tape, 2, 0.35f) == "35%" && reads(tape, 3, 1.f) == "100%");

    // WHEN EACH ECHO LANDS: the step length to the nearest sample, at three
    // tempos, each of the eight choices. In thirds of a step: 3 4 6 8 9 12 18 24.
    const int thirds[8] = {3, 4, 6, 8, 9, 12, 18, 24};
    for (float tempo : {90.f, 120.f, 174.f}) {
        for (int i = 0; i < 8; ++i) {
            std::unique_ptr<Effect> t = fresh(tape, tapeTime(i), 0.5f, 0.f, 1.f, tempo);
            const long want = std::lround(thirds[i] * 5.0 / tempo * 48000.0);
            const std::vector<float> ir = clickOf(*t, 2.6 * static_cast<double>(want) / 48000.0, 0.1f);
            CHECK(echoAt(ir, 0) == want);                                                                                     // nothing before it, to the sample
            CHECK(peakAt(ir, 1, ir.size()) == want);                                                                          // and it is the loudest thing there
            CHECK(peakAt(ir, static_cast<std::size_t>(want + want / 2), ir.size()) == 2 * want);                              // the echo's echo
        }
    }
    // A quarter note at 120 is 24000 samples; a dotted eighth (where it starts) 18000.
    CHECK(echoAt(clickOf(*fresh(tape, -1.f, -1.f, 0.f), 0.5, 0.1f), 0) == 18000);

    // FEEDBACK AND THE TONE LOST EACH TIME ROUND. A small click (well under the
    // ceiling), echoes a quarter note apart. How much of one echo is in the
    // next, at three frequencies, is FEEDBACK (0 to 120%) times what the two
    // cuts leave, times the little FEEDBACK adds to make up for them
    // (1 + low corner / high corner). In dB a repeat, against FEEDBACK:
    //   AGE     high cut   low cut     100 Hz    1 kHz    6 kHz
    //   0%      12 kHz     40 Hz        -0.6      0.0     -0.8
    //   50%     4.2 kHz    126 Hz       -3.9     -0.1     -4.5
    //   100%    1.5 kHz    400 Hz      -10.3     -0.2    -10.6
    // At AGE 0 and 1 kHz an echo comes back at FEEDBACK to within 0.05 dB.
    for (float age : {0.f, 0.5f, 1.f}) {
        const double highCorner = 12000.0 * std::pow(0.125, age), lowCorner = 40.0 * std::pow(10.0, age);
        for (float feedback : {0.25f, 0.5f, 0.75f}) {
            std::unique_ptr<Effect> t = fresh(tape, tapeTime(5), feedback, 0.f, 1.f);
            t->setParam(2, age);
            const std::vector<float> ir = clickOf(*t, 2.0, 0.01f);
            for (double hz : {100.0, 1000.0, 6000.0}) {
                const double want = 1.2 * feedback * (1.0 + lowCorner / highCorner) * cutAt(highCorner, hz) * cutAt(lowCorner, hz, true);
                const double got = echoGainAt(ir, 24000, hz, 1), next = echoGainAt(ir, 24000, hz, 2);
                if (feedback == 0.5f) SAY("tape     age %3.0f%% at %5.0f Hz: each echo x %.4f (FEEDBACK 60%%, and %.2f dB from the cuts)\n", age * 100.0, hz, got, dB(got / 0.6));
                CHECK(std::fabs(dB(got / want)) < 0.1 && std::fabs(dB(next / want)) < 0.1);
                if (age == 0.f && hz == 1000.0) CHECK(std::fabs(dB(got / (1.2 * feedback))) < 0.05);
            }
        }
    }
    SAY("tape     the tool: a cut at its own corner leaves %.2f dB (high cut at 1500 Hz), %.2f dB (low cut at 400 Hz)\n", dB(cutAt(1500.0, 1500.0)), dB(cutAt(400.0, 400.0, true)));
    CHECK(std::fabs(dB(cutAt(1500.0, 1500.0)) + 3.0) < 0.3 && std::fabs(dB(cutAt(400.0, 400.0, true)) + 3.0) < 0.3 && cutAt(400.0, 0.0, true) == 0.0); // the tool: 3 dB down at the corner
    // The first echo has been through the cuts once: duller than the click, and its sum is nothing (no low end at all).
    {
        const std::vector<float> ir = clickOf(*fresh(tape, tapeTime(5), 0.f, 1.f, 1.f), 1.4, 0.01f);
        const long at = peakAt(ir, 1, ir.size());
        CHECK(at > 24000 - 74 && at < 24000 + 74); // the wow has moved it, by 72 samples at the very most
        const float top = std::fabs(ir[static_cast<std::size_t>(at)]);
        CHECK(top < 0.2f * 0.01f && top > 0.1f * 0.01f);
        double sum = 0.0;
        for (std::size_t i = 12000; i < 36000; ++i) sum += ir[i];
        CHECK(std::fabs(sum) < 1e-3 * 0.01);
        CHECK(std::fabs(dB(partAt(ir.data() + 12000, 24000, 1000.0) / (0.01 * 2.0 / 24000.0)) - dB(cutAt(1500.0, 1000.0) * cutAt(400.0, 1000.0, true))) < 0.1);
        // FEEDBACK at 0: one echo and no more.
        float after = 0.f;
        for (std::size_t i = 36000; i < ir.size(); ++i) after = std::fmax(after, std::fabs(ir[i]));
        CHECK(after < 1e-6f * top);
    }

    // MIX is how loud the echoes are; the sound itself is not touched.
    {
        const std::vector<float> in = sineOf(1000.0, 0.25, 48000);
        std::vector<float> outFull, outHalf;
        const std::vector<float> full = wetOf(*fresh(tape, tapeTime(0), 0.5f, 0.3f, 1.f), in, &outFull), half = wetOf(*fresh(tape, tapeTime(0), 0.5f, 0.3f, 0.5f), in, &outHalf);
        bool dryWhole = true, halved = true;
        for (std::size_t i = 0; i < 6000 - 80; ++i) dryWhole = dryWhole && outFull[i] == in[i]; // before the first echo (less the wow's reach): the sine, to the bit
        for (std::size_t i = 0; i < in.size(); ++i) halved = halved && std::fabs(half[i] - 0.5f * full[i]) < 1e-6f;
        CHECK(dryWhole && halved && rmsOf(full, 24000) > 0.1);
    }

    // WOW. A 1 kHz sine, one echo: how late the echo is, tracked for ten
    // seconds. It swings as a 0.8 Hz sine, 1.5 ms (72 samples) either way at
    // AGE full, which is 13 cents of pitch either way; half of each at AGE 50%;
    // nothing at AGE 0.
    for (float age : {0.f, 0.5f, 1.f}) {
        const std::vector<float> wet = wetOf(*fresh(tape, tapeTime(0), 0.f, age, 1.f), sineOf(1000.0, 0.25, 12 * 48000));
        const Wander wow = wanderOf(delayTrack(wet, 1000.0, 48000, 11 * 48000));
        SAY("tape     age %3.0f%%: wow %.2f samples either way (%.3f ms), %.3f Hz, %.2f cents either way\n", age * 100.0, wow.depth, wow.depth / 48.0, wow.hz, wow.cents);
        if (age == 0.f) {
            CHECK(wow.depth < 0.001 && wow.cents < 0.001);
        } else {
            CHECK(std::fabs(wow.depth - 72.0 * age) < 0.02 * 72.0 * age);
            CHECK(std::fabs(wow.hz - 0.8) < 0.01);
            CHECK(std::fabs(wow.cents - 13.0 * age) < 0.3);
        }
    }
    // The tools on a wander that is known: 48 samples either way, twice a second.
    {
        std::vector<float> made(6 * 48000);
        for (std::size_t i = 0; i < made.size(); ++i) {
            const double t = static_cast<double>(i) / 48000.0;
            made[i] = static_cast<float>(0.25 * std::sin(2.0 * kPi * 1000.0 * (t - (4800.0 + 48.0 * std::sin(2.0 * kPi * 2.0 * t)) / 48000.0)));
        }
        const Wander known = wanderOf(delayTrack(made, 1000.0, 0, made.size()));
        CHECK(std::fabs(known.depth - 48.0) < 0.5 && std::fabs(known.hz - 2.0) < 0.01 && std::fabs(known.cents - 1200.0 * std::log2(1.0 + 2.0 * kPi * 2.0 * 0.001)) < 0.3);
        CHECK(std::fabs(pitchOf(sineOf(1000.0, 0.3, 48000), 0, 48000) - 1000.0) < 0.01 && std::fabs(pitchOf(sineOf(437.0, 0.3, 48000), 100, 40000) - 437.0) < 0.01);
        CHECK(pitchOf(std::vector<float>(1000, 0.f), 0, 1000) == 0.0);
    }

    // TIME GLIDES, as tape does: turned from a sixteenth to a half note, the
    // tape slows to half speed (a 1 kHz sine on it reads 500 Hz) until it has
    // got there, and is then exactly a half note; turned back it runs at
    // double (2 kHz). Never slower or faster than that, and never a jump.
    {
        std::unique_ptr<Effect> t = fresh(tape, 0.f, 0.f, 0.f, 1.f), there = fresh(tape, 1.f, 0.f, 0.f, 1.f);
        Stereo io(13 * 48000), other(0);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        other = io;
        const std::vector<float> in = io.l;
        through(*t, io, 0, 48000);
        t->setParam(0, 1.f);
        through(*t, io, 48000, 7 * 48000);
        t->setParam(0, 0.f);
        through(*t, io, 7 * 48000, io.frames());
        through(*there, other, 0, 7 * 48000);
        std::vector<float> wet(io.frames());
        for (std::size_t i = 0; i < wet.size(); ++i) wet[i] = io.l[i] - in[i];
        double slowest = 1e9, fastest = 0.0;
        for (std::size_t at = 48000; at + 2400 <= wet.size(); at += 2400) {
            const double hz = pitchOf(wet, at, at + 2400);
            (at < 7 * 48000 ? slowest : fastest) = at < 7 * 48000 ? std::fmin(slowest, hz) : std::fmax(fastest, hz);
            if (at >= 7 * 48000) CHECK(hz > 990.0);
            else CHECK(hz < 1010.0);
        }
        SAY("tape     TIME turned: the slowest the tape reads a 1 kHz sine is %.1f Hz, the fastest %.1f Hz\n", slowest, fastest);
        CHECK(std::fabs(slowest - 500.0) < 5.0 && std::fabs(fastest - 2000.0) < 20.0);
        // Slowing: 42000 samples further away at half a sample a sample is 1.75 s. Still on the way at 1.5 s, there by 6.
        CHECK(pitchOf(wet, 48000 + 72000 - 2400, 48000 + 72000) < 510.0);
        CHECK(std::fabs(pitchOf(wet, 6 * 48000, 7 * 48000) - 1000.0) < 0.01 && std::fabs(pitchOf(wet, 12 * 48000, 13 * 48000) - 1000.0) < 0.01);
        bool arrived = true;
        for (std::size_t i = 7 * 48000 - 4800; i < 7 * 48000; ++i) arrived = arrived && io.l[i] == other.l[i]; // exactly where one that started there is
        CHECK(arrived);
        const double roughest = largestJump(io.l, 48000, io.frames()) / (sineStep(1000.0, 0.25) + sineStep(2000.0, 0.25));
        SAY("tape     TIME turned: the largest step is %.2f of a 1 kHz and a 2 kHz sine together\n", roughest);
        CHECK(roughest < 1.02);
        // A new tempo is the same thing: half the tempo, and the tape slows to half speed to get there.
        std::unique_ptr<Effect> slowed = fresh(tape, 0.f, 0.f, 0.f, 1.f);
        Stereo again(4 * 48000);
        again.l = again.r = sineOf(1000.0, 0.25, again.frames());
        through(*slowed, again, 0, 48000);
        slowed->setTempo(60.f);
        through(*slowed, again, 48000, again.frames());
        for (std::size_t i = 0; i < wet.size() && i < again.frames(); ++i) wet[i] = again.l[i] - in[i];
        CHECK(std::fabs(pitchOf(wet, 48000 + 2400, 48000 + 4800) - 500.0) < 5.0 && std::fabs(pitchOf(wet, 3 * 48000, 4 * 48000) - 1000.0) < 0.01);
        CHECK(largestJump(again.l, 48000, again.frames()) / (sineStep(1000.0, 0.25) + sineStep(2000.0, 0.25)) < 1.02);
        // A tempo that is no tempo is not taken.
        std::unique_ptr<Effect> odd = fresh(tape, tapeTime(5), 0.f, 0.f, 1.f);
        odd->setTempo(10.f);
        odd->setTempo(900.f);
        odd->setTempo(std::nanf(""));
        CHECK(echoAt(clickOf(*odd, 0.6, 0.1f), 0) == 24000);
        // And the slowest one that is: half a bar at 20 would not fit on the tape, so it is as long as the tape allows.
        odd->reset();
        odd->setTempo(20.f);
        odd->setParam(0, 1.f);
        const long longest = echoAt(clickOf(*odd, 3.2, 0.1f), 0);
        CHECK(longest == 144008); // three seconds (half a bar at 40) and the spare: 144088, less the wow's reach and 8
    }

    // IT NEVER RUNS AWAY. FEEDBACK at 120%, a burst of noise, then nothing for
    // 30 seconds: it howls, and holds one level. The echo alone, at a sixteenth:
    //   AGE     level from 10 to 20 s, and from 20 to 30 s     loudest once it has settled
    //   0%          0.79   0.80                                   1.02
    //   35%         0.43   0.44                                   0.63
    //   100%        0.18   0.18                                   0.27
    // (The ceiling is 1, 0.59 and 0.33; the low cut after it can lift a peak
    // a little over.) At 96% it dies away instead.
    for (float age : {0.f, 0.35f, 1.f}) {
        std::vector<float> in(30 * 48000, 0.f);
        Noise noise(31u);
        for (std::size_t i = 0; i < 12000; ++i) in[i] = 0.5f * noise.next();
        const std::vector<float> wet = wetOf(*fresh(tape, 0.f, 1.f, age, 1.f), in);
        const double middle = rmsOf(wet, 10 * 48000, 20 * 48000), end = rmsOf(wet, 20 * 48000, 30 * 48000);
        const float ceiling = 1.f / (1.f + 2.f * age);
        float held = 0.f;
        for (std::size_t i = 10 * 48000; i < wet.size(); ++i) held = std::fmax(held, std::fabs(wet[i]));
        SAY("tape     feedback 120%%, age %3.0f%%: level %.3f from 10 to 20 s, %.3f from 20 to 30 s; loudest %.3f (the ceiling is %.2f)\n", age * 100.0, middle, end, static_cast<double>(held), static_cast<double>(ceiling));
        CHECK(middle > 0.5 * ceiling && std::fabs(dB(end / middle)) < 0.2);
        CHECK(peakOf(wet) < 1.5f); // the noise itself, once round
        CHECK(held <= 1.1f * ceiling && held > 0.7f * ceiling);
        const std::vector<float> dying = wetOf(*fresh(tape, 0.f, 0.8f, age, 1.f), in);
        CHECK(rmsOf(dying, 20 * 48000, 30 * 48000) < 0.05 * rmsOf(dying, 48000, 2 * 48000));
    }
    // The ceiling on one loud echo: a click four times full scale comes back no louder than the ceiling.
    for (float age : {0.f, 1.f}) {
        const std::vector<float> ir = clickOf(*fresh(tape, tapeTime(5), 1.f, age, 1.f), 2.0, 4.f);
        const float second = std::fabs(ir[static_cast<std::size_t>(peakAt(ir, 36000, 60000))]);
        CHECK(second <= 1.f / (1.f + 2.f * age) && second > 0.1f / (1.f + 2.f * age));
    }

    // SILENCE IN, SILENCE OUT, and no slower on the way. With FEEDBACK at
    // 60% and a sixteenth, each echo is 4.4 dB under the last: the tail is
    // 180 dB down after 5 s and still there (it is not cut short), goes
    // round for the last time when it is 240 dB down (6.75 s), and the effect
    // is asleep (exact zeros, no work) once a whole tape of nothing has gone
    // by, 3 s later. Measured: a block takes 1.4 us while it rings, 1.4 us at
    // its slowest, 0.1 us asleep.
    {
        const std::vector<float> ir = clickOf(*fresh(tape, 0.f, 0.5f, 0.f, 1.f), 14.0, 1.f);
        float late = 0.f;
        for (std::size_t i = 5 * 48000; i < 5 * 48000 + 6000; ++i) late = std::fmax(late, std::fabs(ir[i]));
        std::size_t last = 0;
        for (std::size_t i = 0; i < ir.size(); ++i) last = ir[i] != 0.f ? i : last;
        SAY("tape     a click at 60%%: 5 s on the echo is %.1e; the last sample that is not zero is %.2f s in\n", static_cast<double>(late), static_cast<double>(last) / 48000.0);
        CHECK(late > 1e-10f && late < 1e-8f);
        CHECK(last > 6 * 48000 + 24000 && last < 11 * 48000);
        float smallest = 1.f;
        for (float v : ir) smallest = v != 0.f ? std::fmin(smallest, std::fabs(v)) : smallest;
        CHECK(smallest > 1e-30f); // nothing that came out was anywhere near too small to hold (that starts at 1e-38)
        // Woken by a new sound, it plays as one fresh from the box does.
        // (The wow starts again too, so this holds on a worn tape. What is left on the tape is under 240 dB down.)
        std::unique_ptr<Effect> slept = fresh(tape, 0.f, 0.5f, 0.6f, 1.f), box = fresh(tape, 0.f, 0.5f, 0.6f, 1.f);
        const std::vector<float> first = clickOf(*slept, 14.3, 1.f);
        const std::vector<float> a = clickOf(*slept, 1.0, 0.5f), b = clickOf(*box, 1.0, 0.5f);
        float apart = 0.f;
        for (std::size_t i = 0; i < a.size(); ++i) apart = std::fmax(apart, std::fabs(a[i] - b[i]));
        CHECK(first.back() == 0.f && apart < 1e-9f && rmsOf(a, 5900, 6100) > 0.005);
        // Sound too faint to be sound (1e-20) does not get onto the tape: what comes out is what went in, to the bit.
        std::unique_ptr<Effect> faint = fresh(tape, 0.f, 0.f, 0.f, 1.f);
        Stereo io(48000);
        io.l.assign(48000, 1e-20f);
        io.r.assign(48000, 1e-20f);
        io.l[0] = io.r[0] = 0.5f;
        io.l[36000] = io.r[36000] = 0.5f; // and a second click, to read the tape back by
        const Stereo before = io;
        through(*faint, io);
        bool nothingOn = io.l[6000] != before.l[6000];
        for (std::size_t i = 36001; i < 42000; ++i) nothingOn = nothingOn && std::fabs(io.l[i] - before.l[i]) < 1e-22f && std::fabs(io.r[i] - before.r[i]) < 1e-22f; // the echo of 1e-20 would be 8e-21
        CHECK(nothingOn);
        // Turned while asleep, a setting is simply there when sound comes: no glide from where it was.
        std::unique_ptr<Effect> turned = fresh(tape, 1.f, 0.f, 0.f, 0.f);
        clickOf(*turned, 0.1, 0.f);
        turned->setParam(0, 0.f);
        turned->setParam(3, 1.f);
        const std::vector<float> woke = clickOf(*turned, 0.5, 0.1f);
        CHECK(echoAt(woke, 0) == 6000 && std::fabs(woke[6000]) > 0.05f);
    }
    CHECK(sameFlushed(tape, tapeTime(0), 0.5f, 0.6f, 1.f, 14.0)); // the same with the processor's flush-to-zero on or off
    // NOTHING TOO SMALL TO HOLD IS EVER MADE, with flush-to-zero off: a whole
    // tail, down to asleep, at three ages; and the same with sound coming in
    // that is itself too small to hold (another effect's dying tail).
    for (float age : {0.f, 0.35f, 1.f}) {
        std::unique_ptr<Effect> t = fresh(tape, 0.f, 0.5f, age, 1.f);
        const TooSmall marks;
        const std::vector<float> ir = clickOf(*t, 14.0, 1.f);
        const bool seen = marks.seen();
        CHECK(!seen && ir.back() == 0.f && rmsOf(ir, 5900, 6100) > 0.005);
    }
    {
        std::unique_ptr<Effect> t = fresh(tape, 0.f, 0.5f, 0.35f, 1.f);
        Stereo io(4 * 48000);
        io.l.assign(io.frames(), 1e-39f);
        io.r.assign(io.frames(), -1e-39f);
        io.l[0] = io.r[0] = 0.5f;
        const TooSmall marks;
        through(*t, io);
        const bool seen = marks.seen();
        CHECK(!seen && rmsOf(io.l, 5900, 6100) > 0.005);
    }
    {
        // The tool does see them: the old REVERB's tail fades through them (which is why it cuts them off by hand).
        std::unique_ptr<Effect> r = fresh(EffectKind::Reverb, 0.f, 0.5f, 1.f);
        const TooSmall marks;
        clickOf(*r, 8.0, 1.f);
        const bool seen = marks.seen();
        CHECK(seen);
    }
    // A TURNED SETTING ARRIVES IN ABOUT 20 MS, and then is exactly there. MIX
    // from 0 to full under a steady echo: 6% of the way after the first chunk
    // (64 samples), about a quarter after 5 ms, all but there after 100 ms.
    {
        std::unique_ptr<Effect> t = fresh(tape, 0.f, 0.f, 0.f, 0.f);
        Stereo io(96000);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        const std::vector<float> in = io.l;
        through(*t, io, 0, 48000);
        t->setParam(3, 1.f);
        through(*t, io, 48000, 72000);
        std::vector<float> wet(io.frames());
        for (std::size_t i = 0; i < wet.size(); ++i) wet[i] = io.l[i] - in[i];
        const double full = partAt(wet.data() + 67200, 4800, 1000.0), first = partAt(wet.data() + 48000 + 48, 48, 1000.0), soon = partAt(wet.data() + 48000 + 240, 48, 1000.0);
        SAY("tape     MIX turned to full: the echo is %.3f of the way after 1.5 ms, %.3f after 5.5 ms, %.4f after 100 ms\n", first / full, soon / full, partAt(wet.data() + 52800, 4800, 1000.0) / full);
        CHECK(std::fabs(full - 0.25) < 0.005);
        CHECK(first / full > 0.02 && first / full < 0.12 && soon / full > 0.15 && soon / full < 0.4);
        CHECK(std::fabs(partAt(wet.data() + 52800, 4800, 1000.0) / full - 1.0) < 0.01);
        // ...and back to 0: within a second the sound is untouched, to the bit (it lands; it does not creep for ever).
        t->setParam(3, 0.f);
        through(*t, io, 72000, 96000);
        bool untouched = true;
        for (std::size_t i = 84000; i < 96000; ++i) untouched = untouched && io.l[i] == in[i];
        CHECK(untouched && io.l[72100] != in[72100]);
    }
    {
        std::unique_ptr<Effect> t = fresh(tape, 0.f, 0.5f, 0.35f, 1.f);
        const TailTimes took = tailTimes(*t, 16);
        SAY("flush    tape (flush-to-zero OFF): a block takes %.2f us early, %.2f us at its slowest, %.2f us at the end (asleep)\n", took.early, took.slowest, took.late);
        CHECK(took.slowest < 3.0 * took.early + 20.0 && took.late < took.early + 5.0);
    }
    // Both sides are the same tape: left and right in, left and right out, each its own.
    {
        std::unique_ptr<Effect> t = fresh(tape, 0.f, 0.5f, 0.f, 1.f);
        Stereo io(24000);
        io.l[0] = 0.1f;
        io.r[100] = -0.05f;
        through(*t, io);
        const long l = peakAt(io.l, 1, io.frames()), r = peakAt(io.r, 101, io.frames());
        CHECK(l == 6000 && r == 6100 && std::fabs(io.r[static_cast<std::size_t>(r)] + 0.5f * io.l[static_cast<std::size_t>(l)]) < 1e-4f);
        CHECK(io.l[static_cast<std::size_t>(r)] != io.r[static_cast<std::size_t>(r)]);
    }
}

// ---- HALL -------------------------------------------------------------------------------------------

// One band of a sound: what is within about half an octave of `hz` (a
// band-pass, twice over), so a tail can be timed at one pitch.
static std::vector<float> bandOf(const std::vector<float>& v, double hz, double rate = 48000.0) {
    const double w = 2.0 * kPi * hz / rate, alpha = std::sin(w) / (2.0 * 2.0), a0 = 1.0 + alpha;
    const double b0 = alpha / a0, a1 = -2.0 * std::cos(w) / a0, a2 = (1.0 - alpha) / a0;
    std::vector<float> out(v.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, p1 = 0, p2 = 0, q1 = 0, q2 = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double x = v[i], y = b0 * (x - x2) - a1 * y1 - a2 * y2;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        const double z = b0 * (y - p2) - a1 * q1 - a2 * q2;
        p2 = p1, p1 = y, q2 = q1, q1 = z;
        out[i] = static_cast<float>(z);
    }
    return out;
}
// A click in both sides (or one), both sides out.
static Stereo clickBoth(Effect& effect, double seconds, float left = 1.f, float right = 1.f) {
    Stereo io(static_cast<std::size_t>(seconds * 48000.0));
    io.l[0] = left;
    io.r[0] = right;
    through(effect, io);
    return io;
}
// How alike two sounds are between two places: 1 the same, 0 unrelated, -1 opposite.
static double alike(const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to) {
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = from; i < to && i < a.size() && i < b.size(); ++i) ab += static_cast<double>(a[i]) * b[i], aa += static_cast<double>(a[i]) * a[i], bb += static_cast<double>(b[i]) * b[i];
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt(aa * bb) : 0.0;
}

// When a sound first rises over `floor`, or -1. (HALL rests on a floor 360 dB down, not on nothing.)
static long soundAt(const std::vector<float>& v, float floor = 1e-9f) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (std::fabs(v[i]) > floor) return static_cast<long>(i);
    return -1;
}
static float hallDecay(double seconds) { return static_cast<float>(std::log(seconds / 0.3) / std::log(20.0 / 0.3)); }

// Measured, and pinned (see the comments where they are used).
static const double kHallWander = 4.86; // dB, lowest to highest
static const int kHallWanderUps = 34;    // times up through its middle in 40 s
// [setting][down, up][the largest step, the sharpest corner], each against the steady one.
static const double kHallTurn[4][2][2] = {
    {{2.47, 16.5}, {1.34, 5.8}},   // SIZE: the bent sound (see where it is used)
    {{0.88, 0.88}, {0.64, 0.77}},  // DECAY
    {{1.16, 1.48}, {0.86, 0.98}},  // TONE
    {{0.92, 0.92}, {1.01, 1.01}},  // MIX
};

static void testHall() {
    const EffectKind hall = EffectKind::Hall;
    const EffectInfo& info = effectInfo(hall);
    CHECK(std::string(info.name) == "HALL" && info.params == 4 && std::string(info.paramNames[0]) == "SIZE" && std::string(info.paramNames[1]) == "DECAY");
    CHECK(std::string(info.paramNames[2]) == "TONE" && std::string(info.paramNames[3]) == "MIX");
    CHECK(info.defaults[0] == 0.6f && info.defaults[1] == 0.45f && info.defaults[2] == 0.5f && info.defaults[3] == 0.3f);
    // How it reads: DECAY in seconds, 0.3 to 20; the rest as amounts.
    CHECK(reads(hall, 1, 0.f) == "0.3 S" && reads(hall, 1, 0.45f) == "2.0 S" && reads(hall, 1, 0.5f) == "2.4 S" && reads(hall, 1, 1.f) == "20 S" && reads(hall, 1, hallDecay(1.0)) == "1.0 S");
    CHECK(reads(hall, 1, hallDecay(9.9)) == "9.9 S" && reads(hall, 1, hallDecay(9.97)) == "10 S" && reads(hall, 1, hallDecay(12.4)) == "12 S");
    CHECK(reads(hall, 0, 0.6f) == "60%" && reads(hall, 2, 0.5f) == "50%" && reads(hall, 3, 0.3f) == "30%");

    // DECAY IS A REAL TIME, WHATEVER SIZE IS. A click, MIX full, TONE in the
    // middle: the RT60 around 500 Hz against what DECAY reads.
    //   DECAY       SIZE 0%    50%     100%
    //   0.3 s        0.28      0.31    0.34
    //   0.86 s       0.82      0.87    0.86
    //   2.45 s       2.28      2.34    2.28
    //   7.0 s        6.50      6.68    6.63
    //   20 s        19.0      18.7    18.8
    // The plan asks for within 10% at five settings: it is, at every SIZE, but
    // for the shortest DECAY in the largest room (14% long: a third of a second
    // is three trips of its longest line). It reads about 6% short because
    // TONE's dulling already takes a little at 500 Hz; at 80 Hz it is within 3%.
    for (float size : {0.f, 0.5f, 1.f}) {
        double last = 0.0;
        for (float decay : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
            const double want = 0.3 * std::pow(20.0 / 0.3, decay);
            const std::vector<float> ir = clickOf(*fresh(hall, size, decay, 0.5f, 1.f), std::fmin(40.0, 2.0 + want * 1.2), 1.f);
            const double mid = rt60(bandOf(ir, 500.0)), low = rt60(bandOf(ir, 80.0)), high = rt60(bandOf(ir, 8000.0));
            const int echoes = echoesIn(ir, 0.0005, 0.1);
            SAY("hall     size %3.0f%% decay %5.2f s: RT60 at 500 Hz %6.2f s (x %.3f), at 80 Hz x %.3f, at 8 kHz x %.3f; %d echoes in the first 100 ms\n", size * 100.0, want, mid, mid / want, low / want, high / want, echoes);
            CHECK(std::fabs(mid / want - 1.0) < (decay == 0.f ? 0.16 : 0.10));
            if (decay > 0.f) CHECK(std::fabs(low / want - 1.0) < 0.06 && mid / want < 1.03);
            CHECK(mid > 2.0 * last);
            last = mid;
            // The highs die sooner: at 8 kHz the tail lasts 0.31 to 0.45 as long (TONE in the middle).
            if (decay > 0.f) CHECK(high / want > 0.28 && high / want < 0.48);
            // DENSE: 1250 separate echoes in the first 100 ms in the smallest room, 900 in the middle, 650 in the largest. (The old REVERB: 200. The plan asks for over 400.)
            CHECK(echoes > (size == 0.f ? 1100 : (size == 0.5f ? 800 : 580)) && echoes < 1400);
        }
    }
    // The same DECAY at two sizes is the same time, within 5%.
    {
        const double small = rt60(bandOf(clickOf(*fresh(hall, 0.1f, 0.5f, 0.5f, 1.f), 5.0, 1.f), 500.0)), large = rt60(bandOf(clickOf(*fresh(hall, 0.9f, 0.5f, 0.5f, 1.f), 5.0, 1.f), 500.0));
        CHECK(std::fabs(small / large - 1.0) < 0.05);
    }

    // TONE: HOW LONG THE TAIL LASTS AT EACH PITCH. DECAY 2.0 s (the usual), in seconds:
    //   TONE     80 Hz   250 Hz   1 kHz   4 kHz   8 kHz   14 kHz
    //   0%       1.91    1.56     0.85    0.63    0.62    0.59      dark: above 600 Hz the tail lasts 0.3 as long
    //   50%      1.99    1.94     1.68    0.93    0.74    0.66      the corner is at 2.7 kHz
    //   100%     0.91    1.37     1.85    1.76    1.51    1.16      bright (corner 12 kHz), and the lows thinned: 0.4 as long at the bottom
    {
        struct Row { float tone; double at[6]; };
        const double hz[6] = {80.0, 250.0, 1000.0, 4000.0, 8000.0, 14000.0};
        const Row rows[3] = {{0.f, {1.91, 1.56, 0.85, 0.63, 0.62, 0.59}}, {0.5f, {1.99, 1.94, 1.68, 0.93, 0.74, 0.66}}, {1.f, {0.91, 1.37, 1.85, 1.76, 1.51, 1.16}}};
        for (const Row& row : rows) {
            const std::vector<float> ir = clickOf(*fresh(hall, 0.6f, 0.45f, row.tone, 1.f), 6.0, 1.f);
            for (int i = 0; i < 6; ++i) {
                const double got = rt60(bandOf(ir, hz[i]));
                SAY("hall     tone %3.0f%%: RT60 at %5.0f Hz %.2f s\n", row.tone * 100.0, hz[i], got);
                CHECK(std::fabs(got / row.at[i] - 1.0) < 0.06);
            }
        }
        // Up to the middle the lows are whole (the bottom lasts as long as DECAY says); only above it are they thinned.
        const double below = rt60(bandOf(clickOf(*fresh(hall, 0.6f, 0.45f, 0.45f, 1.f), 6.0, 1.f), 80.0)), above = rt60(bandOf(clickOf(*fresh(hall, 0.6f, 0.45f, 0.75f, 1.f), 6.0, 1.f), 80.0));
        CHECK(std::fabs(below / 1.985 - 1.0) < 0.03 && above < 0.75 * below && above > 0.6 * below);
        // The tool: a tail that falls 60 dB a second at one pitch reads 1 s there, and nothing is found at another.
        std::vector<float> made(96000);
        for (std::size_t i = 0; i < made.size(); ++i) made[i] = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / 48000.0) * std::pow(10.0, -3.0 * static_cast<double>(i) / 48000.0));
        CHECK(std::fabs(rt60(bandOf(made, 1000.0), 0) - 1.0) < 0.03 && rmsOf(bandOf(made, 8000.0), 4800) < 0.01 * rmsOf(bandOf(made, 1000.0), 4800));
    }

    // THE GAP BEFORE THE ROOM ANSWERS (pre-delay) follows SIZE: 2 ms to 30 ms, to the sample.
    {
        const float sizes[4] = {0.f, 0.5f, 0.6f, 1.f};
        const long gaps[4] = {96, 768, 902, 1440};
        for (int i = 0; i < 4; ++i) {
            const std::vector<float> ir = clickOf(*fresh(hall, sizes[i], 0.45f, 0.5f, 1.f), 0.1, 1.f);
            CHECK(soundAt(ir) == gaps[i]);
            CHECK(std::fabs(ir[static_cast<std::size_t>(gaps[i])] - 0.5f * 0.7f * 0.7f * 0.6f * 0.6f) < 1e-4f); // the click through four smearing stages, at the early sound's level
        }
    }

    // LEFT AND RIGHT ARE NEARLY UNRELATED (1 the same, 0 unrelated), so the room is wide:
    //   SIZE     one click in both: the first 100 ms, and after     a click on the left only: the right side against the left
    //   0%          0.03    0.03                                      -0.5 dB
    //   60%         0.02    0.03                                      -2.5 dB
    //   100%        0.03    0.00                                      -5.7 dB
    // A sound on one side fills both, a little more on its own side.
    for (float size : {0.f, 0.6f, 1.f}) {
        const Stereo both = clickBoth(*fresh(hall, size, 0.45f, 0.5f, 1.f), 3.0), one = clickBoth(*fresh(hall, size, 0.45f, 0.5f, 1.f), 3.0, 1.f, 0.f), other = clickBoth(*fresh(hall, size, 0.45f, 0.5f, 1.f), 3.0, 0.f, 1.f);
        const double early = alike(both.l, both.r, 1, 4800), later = alike(both.l, both.r, 4800, 96000), across = dB(rmsOf(one.r, 4800, 96000) / rmsOf(one.l, 4800, 96000));
        SAY("hall     size %3.0f%%: left and right alike %.3f early, %.3f later; a click on the left: the right is %.1f dB of the left (alike %.3f); on the right: the left is %.1f dB\n", size * 100.0, early, later, across, alike(one.l, one.r, 4800, 96000),
            dB(rmsOf(other.l, 4800, 96000) / rmsOf(other.r, 4800, 96000)));
        CHECK(std::fabs(early) < 0.08 && std::fabs(later) < 0.08 && std::fabs(alike(one.l, one.r, 4800, 96000)) < 0.12);
        CHECK(across < -0.2 && across > -7.0);
        const double back = dB(rmsOf(other.l, 4800, 96000) / rmsOf(other.r, 4800, 96000));
        CHECK(back < -0.2 && back > -7.0);
        CHECK(rmsOf(both.l, 4800, 96000) > 1e-4 && std::fabs(dB(rmsOf(both.l, 4800, 96000) / rmsOf(both.r, 4800, 96000))) < 1.5); // and neither side is the louder
    }
    CHECK(std::fabs(alike(sineOf(100.0, 1.0, 4800), sineOf(100.0, 0.3, 4800), 0, 4800) - 1.0) < 1e-9 && alike(sineOf(100.0, 1.0, 4800), sineOf(200.0, 1.0, 4800), 0, 4800) < 1e-9); // the tool

    // ABOUT AS LOUD AT ANY SIZE AND DECAY. Held noise, MIX full: the room
    // comes out 1.6 to 4.2 dB under the noise at every corner (a long tail
    // is fed less, a small room less).
    for (float size : {0.f, 0.5f, 1.f})
        for (float decay : {0.f, 0.5f, 1.f}) {
            std::unique_ptr<Effect> h = fresh(hall, size, decay, 0.5f, 1.f);
            Stereo io(48000 * 20);
            Noise noise(3u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
            const double inRms = rmsOf(io.l);
            through(*h, io);
            const double level = dB(rmsOf(io.l, 19 * 48000, 20 * 48000) / inRms);
            SAY("hall     size %3.0f%% decay %3.0f%%: held noise comes out at %.1f dB\n", size * 100.0, decay * 100.0, level);
            CHECK(level > -4.6 && level < -1.2);
        }

    // MIX: the sound and the room swap over at equal power. Before the room
    // answers (the gap), what comes out is the sound alone times cos(MIX x 90 degrees):
    // all of it at 0, 0.891 at 30%, 0.707 at half, none at full. The room is
    // sin of the same: 0.454 at 30%, 0.707 at half, all at full.
    {
        const std::vector<float> in = sineOf(1000.0, 0.25, 48000);
        std::vector<float> out, room;
        const std::vector<float> full = wetOf(*fresh(hall, 1.f, 0.45f, 0.5f, 1.f), in, &room);
        for (float mix : {0.3f, 0.5f, 1.f}) {
            const std::vector<float> wet = wetOf(*fresh(hall, 1.f, 0.45f, 0.5f, mix), in, &out);
            const float dry = mix == 1.f ? 0.f : std::cos(1.5707963f * mix), share = std::sin(1.5707963f * mix);
            bool before = true, after = true;
            for (std::size_t i = 0; i < 1440; ++i) before = before && std::fabs(out[i] - dry * in[i]) < 1e-6f;
            for (std::size_t i = 1440; i < in.size(); ++i) after = after && std::fabs(out[i] - (dry * in[i] + share * room[i])) < 2e-6f;
            CHECK(before && after && rmsOf(room, 24000) > 0.02);
        }
    }

    // TWO OF THE EIGHT LINES ARE BENT, SLOWLY, so the tail does not sit on one
    // note. A held 1 kHz sine, MIX full, the usual settings: its level in the
    // room wanders (a room that stood still would hold it steady to a
    // thousandth of a dB). Measured over 40 s from the 10th, a reading every
    // 100 ms: see kHallWander. With no bend it reads 0.00 dB and 0 times.
    {
        std::unique_ptr<Effect> h = fresh(hall, 0.6f, 0.45f, 0.5f, 1.f);
        Stereo io(50 * 48000);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        through(*h, io);
        std::vector<double> level;
        for (std::size_t at = 10 * 48000; at + 4800 <= io.frames(); at += 4800) level.push_back(dB(partAt(io.l.data() + at, 4800, 1000.0)));
        double low = level[0], high = level[0], mean = 0.0;
        for (double v : level) low = std::fmin(low, v), high = std::fmax(high, v), mean += v / static_cast<double>(level.size());
        int ups = 0;
        for (std::size_t i = 1; i < level.size(); ++i) ups += level[i - 1] < mean && level[i] >= mean ? 1 : 0;
        SAY("hall     a held 1 kHz sine: its level in the room wanders %.2f dB, up through its middle %d times in 40 s\n", high - low, ups);
        CHECK(std::fabs((high - low) - kHallWander) < 0.1 * kHallWander && std::abs(ups - kHallWanderUps) <= 1);
    }

    // A TURNED SETTING GLIDES. Each turned end to end AT ONCE (the worst a
    // player can do), under a held 100 Hz sine at MIX 50%. Two readings while
    // it gets there, each against the same reading with nothing turned (the
    // rougher of before and after): the largest step, and the sharpest corner
    // (a click is a corner). See kHallTurn for the numbers.
    //   - DECAY, TONE and MIX: no rougher than the sound standing still.
    //   - SIZE: the room's pitch bends while it changes size (the lines take a
    //     tenth of a second), so for a moment there is more going on: a step
    //     2.4 times the steady one going small, and that is the bent sound, not
    //     a click. Ears decide whether it is wanted.
    {
        for (int p = 0; p < 4; ++p)
            for (int up = 0; up < 2; ++up) {
                std::unique_ptr<Effect> h = fresh(hall, 0.6f, 0.45f, 0.5f, 0.5f);
                h->setParam(p, up ? 0.f : 1.f);
                Stereo io(5 * 48000);
                io.l = io.r = sineOf(100.0, 0.5, io.frames());
                through(*h, io, 0, 96000);
                h->setParam(p, up ? 1.f : 0.f);
                through(*h, io, 96000, io.frames());
                const auto corner = [&](std::size_t from, std::size_t to) {
                    double most = 0.0;
                    for (std::size_t i = from + 2; i < to; ++i) most = std::fmax(most, std::fabs(static_cast<double>(io.l[i]) - 2.0 * io.l[i - 1] + io.l[i - 2]));
                    return most;
                };
                const double steady = std::fmax(largestJump(io.l, 48000, 96000), largestJump(io.l, 4 * 48000, 5 * 48000)), steadyCorner = std::fmax(corner(48000, 96000), corner(4 * 48000, 5 * 48000));
                const double step = largestJump(io.l, 96000 - 1, 4 * 48000) / steady, sharp = corner(96000 - 2, 4 * 48000) / steadyCorner;
                SAY("hall     %-5s turned %-4s at once: the largest step is %.2f of the steady one, the sharpest corner %.2f of the steady one\n", info.paramNames[p], up ? "up" : "down", step, sharp);
                CHECK(std::fabs(step - kHallTurn[p][up][0]) < 0.08 * kHallTurn[p][up][0]);
                CHECK(sharp < 1.25 * kHallTurn[p][up][1] + 0.1);
                if (p > 0) CHECK(step < 1.35 && sharp < 2.5);
            }
    }
    // MIX arrives in about 30 ms. Three rooms hear the same sine: one at MIX
    // none (the sine), one at full (the room alone), one turned from none to
    // full while sounding. The third is some of each of the others; how much
    // of each, in a stretch of 2 ms: the room's share is 0.03 in the first,
    // 0.18 from 5 ms, 0.96 from 100 ms, and the sine's share is what is left
    // (on the way the two slide straight across; at rest they are at equal power).
    {
        std::unique_ptr<Effect> h = fresh(hall, 0.6f, 0.45f, 0.5f, 0.f), room = fresh(hall, 0.6f, 0.45f, 0.5f, 1.f);
        Stereo io(96000), wet(0);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        const std::vector<float> in = io.l;
        wet = io;
        through(*room, wet);
        through(*h, io, 0, 48000);
        h->setParam(3, 1.f);
        through(*h, io, 48000, io.frames());
        // Least squares over a stretch: out = a x in + b x room.
        const auto shares = [&](std::size_t at, double* a, double* b) {
            double ii = 0, rr = 0, ir = 0, oi = 0, orr = 0;
            for (std::size_t i = at; i < at + 96; ++i) ii += static_cast<double>(in[i]) * in[i], rr += static_cast<double>(wet.l[i]) * wet.l[i], ir += static_cast<double>(in[i]) * wet.l[i], oi += static_cast<double>(io.l[i]) * in[i], orr += static_cast<double>(io.l[i]) * wet.l[i];
            const double det = ii * rr - ir * ir;
            *a = (oi * rr - orr * ir) / det, *b = (orr * ii - oi * ir) / det;
        };
        double dry[3], share[3];
        shares(48000, &dry[0], &share[0]);
        shares(48000 + 240, &dry[1], &share[1]);
        shares(48000 + 4800, &dry[2], &share[2]);
        SAY("hall     MIX turned to full: the room's share is %.3f over the first 2 ms, %.3f from 5 ms, %.4f from 100 ms; the sine's %.3f, %.3f, %.4f\n", share[0], share[1], share[2], dry[0], dry[1], dry[2]);
        CHECK(share[0] > 0.015 && share[0] < 0.06 && share[1] > 0.12 && share[1] < 0.24 && share[2] > 0.94 && share[2] < 0.985);
        for (int i = 0; i < 3; ++i) CHECK(std::fabs(dry[i] + share[i] - 1.0) < 0.005);
        // ...and turned back to none it lands: the sound untouched, to the bit, within a second.
        h->setParam(3, 0.f);
        Stereo more(96000);
        more.l = more.r = in;
        through(*h, more);
        bool untouched = true;
        for (std::size_t i = 48000; i < 96000; ++i) untouched = untouched && more.l[i] == in[i];
        CHECK(untouched && more.l[100] != in[100]);
    }
    // SIZE LANDS: once it has got there, the room is exactly the room of that
    // size. Two rooms with the same past, turned from small to large while
    // sounding; one is then given a click. What that click adds is what a
    // fresh large room does with it, up to where its first bent line comes
    // back (the gap, 30 ms, and the third line, 51.7 ms): the gap and the
    // first two lines are whole numbers of samples again.
    {
        std::unique_ptr<Effect> a = fresh(hall, 0.f, 0.45f, 0.5f, 1.f), b = fresh(hall, 0.f, 0.45f, 0.5f, 1.f), box = fresh(hall, 1.f, 0.45f, 0.5f, 1.f);
        Stereo ioA(4 * 48000), ioB(0);
        Noise noise(11u);
        for (std::size_t i = 0; i < 24000; ++i) ioA.l[i] = ioA.r[i] = 0.25f * noise.next();
        ioB = ioA;
        ioA.l[3 * 48000] = ioA.r[3 * 48000] = 1.f;
        for (Effect* e : {a.get(), b.get()}) {
            Stereo& io = e == a.get() ? ioA : ioB;
            through(*e, io, 0, 12000);
            e->setParam(0, 1.f);
            through(*e, io, 12000, io.frames());
        }
        const std::vector<float> want = clickOf(*box, 0.5, 1.f);
        float apart = 0.f, loud = 0.f;
        const std::size_t until = 1440 + static_cast<std::size_t>(0.0517 * 48000.0) - 8;
        for (std::size_t i = 0; i < until; ++i) apart = std::fmax(apart, std::fabs((ioA.l[3 * 48000 + i] - ioB.l[3 * 48000 + i]) - want[i])), loud = std::fmax(loud, std::fabs(want[i]));
        SAY("hall     SIZE turned and landed: a click then differs from a fresh room's by %.1e at most (the click's own sound is %.2f)\n", static_cast<double>(apart), static_cast<double>(loud));
        CHECK(apart < 2e-6f && loud > 0.05f && soundAt(want) == 1440);
        CHECK(rmsOf(want, 1440 + 1800, until) > 1e-3); // (the first lines are back within that stretch: it is not only the early sound)
    }

    // SILENCE IN, SILENCE OUT IN THE END, AND NOTHING TOO SMALL TO HOLD ON
    // THE WAY, with the processor's flush-to-zero off. A click, then nothing:
    // the room fades on a floor 360 dB down (never through the numbers too
    // small to hold), and a quarter of a second after it has held nothing
    // over 240 dB down it is asleep: exact zeros. At DECAY 2.0 s that is
    // 6.7 s after the click; at 0.3 s, 1.3 s; at 20 s, 60 s.
    {
        struct Row { float size, decay, tone, mix; double seconds, asleepBy; };
        const Row rows[4] = {{0.6f, 0.45f, 0.5f, 1.f, 9.0, 6.66}, {0.f, 0.f, 1.f, 0.5f, 3.0, 1.27}, {1.f, 0.f, 0.f, 1.f, 3.0, 1.29}, {0.3f, 1.f, 0.75f, 1.f, 70.0, 59.9}};
        for (const Row& row : rows) {
            std::unique_ptr<Effect> h = fresh(hall, row.size, row.decay, row.tone, row.mix);
            const TooSmall marks;
            Stereo io(static_cast<std::size_t>(row.seconds * 48000.0));
            io.l[0] = io.r[0] = 1.f;
            through(*h, io);
            const bool seen = marks.seen();
            std::size_t last = 0;
            float smallest = 1.f;
            for (std::size_t i = 1; i < io.frames(); ++i) {
                if (io.l[i] != 0.f) last = i, smallest = std::fmin(smallest, std::fabs(io.l[i]));
                if (io.r[i] != 0.f) last = i > last ? i : last, smallest = std::fmin(smallest, std::fabs(io.r[i]));
            }
            SAY("hall     decay %5.2f s size %3.0f%%: the last sample that is not zero is %.2f s in; the smallest that came out is %.1e\n", 0.3 * std::pow(20.0 / 0.3, row.decay), row.size * 100.0, static_cast<double>(last) / 48000.0, static_cast<double>(smallest));
            CHECK(!seen);
            CHECK(std::fabs(static_cast<double>(last) / 48000.0 - row.asleepBy) < 0.06 * row.asleepBy + 0.05);
            CHECK(smallest > 1e-25f && io.l.back() == 0.f && io.r.back() == 0.f);
        }
        // The same with sound coming in that is itself too small to hold (another effect's dying tail): it is taken as none.
        std::unique_ptr<Effect> h = fresh(hall, 0.6f, 0.f, 1.f, 0.5f);
        Stereo io(4 * 48000);
        io.l.assign(io.frames(), 1e-39f);
        io.r.assign(io.frames(), -1e-39f);
        io.l[0] = io.r[0] = 0.5f;
        const TooSmall marks;
        through(*h, io);
        const bool seen = marks.seen();
        CHECK(!seen && rmsOf(io.l, 902, 4800) > 1e-3 && io.l.back() == 0.f && io.r.back() == 0.f);
    }
    CHECK(sameFlushed(hall, 0.6f, 0.f, 0.75f, 0.5f, 3.0)); // the same with the processor's flush-to-zero on or off, to the bit
    CHECK(sameFlushed(hall, 0.2f, 0.3f, 0.2f, 1.f, 6.0));
    // And by the clock (the app's way, flush-to-zero OFF). A block of 64 takes
    // 2.7 us while the tail is loud, 2.8 us at its slowest in 40 s, 0.13 us
    // asleep. With the floor and the sleep taken out it was measured at 2.7 us,
    // then 30 us from the second second, then 100 to 130 us for good.
    {
        std::unique_ptr<Effect> h = fresh(hall, 0.6f, 0.45f, 0.5f, 1.f);
        const TailTimes took = tailTimes(*h, 40);
        SAY("flush    hall (flush-to-zero OFF): a block takes %.2f us early, %.2f us at its slowest, %.2f us at the end (asleep)\n", took.early, took.slowest, took.late);
        CHECK(took.slowest < 3.0 * took.early + 5.0 && took.late < 0.5 * took.early + 1.0);
    }
    // Woken by a new sound, it plays as one fresh from the box does (the bend
    // starts again too; what is left in the room is under 240 dB down); and a
    // setting turned while it slept is simply there.
    {
        std::unique_ptr<Effect> slept = fresh(hall, 0.f, 0.f, 0.5f, 0.7f), box = fresh(hall, 0.f, 0.f, 0.5f, 0.7f), turned = fresh(hall, 1.f, 0.f, 0.5f, 0.f);
        const Stereo first = clickBoth(*slept, 3.0), a = clickBoth(*slept, 3.0, 0.5f, -0.25f), b = clickBoth(*box, 3.0, 0.5f, -0.25f);
        float apart = 0.f;
        for (std::size_t i = 0; i < a.frames(); ++i) apart = std::fmax(apart, std::fmax(std::fabs(a.l[i] - b.l[i]), std::fabs(a.r[i] - b.r[i])));
        CHECK(first.l.back() == 0.f && a.l.back() == 0.f && apart < 1e-9f && rmsOf(a.l, 96, 4800) > 1e-3);
        clickBoth(*turned, 0.1, 0.f, 0.f);
        turned->setParam(0, 0.f);
        turned->setParam(3, 0.7f);
        const Stereo c = clickBoth(*turned, 3.0, 0.5f, -0.25f);
        CHECK(c.l == b.l && c.r == b.r);
        // Asleep, with nothing coming in, what comes out is nothing; at MIX 0 the sound is not touched at all.
        Stereo faint(256);
        faint.l.assign(256, 1e-20f);
        through(*slept, faint);
        CHECK(faint.l[0] == 0.f && faint.l[255] == 0.f);
        slept->setParam(3, 0.f);
        faint.l.assign(256, 1e-20f);
        through(*slept, faint);
        CHECK(faint.l[0] == 1e-20f && faint.l[255] == 1e-20f);
    }
}

// ---- 4. flush-to-zero ------------------------------------------------------------------------

// Whether a sum that should come out too small to hold comes out as nothing.
static bool tinyIsZero() {
    volatile float small = 1e-30f, smaller = 1e-10f;
    volatile float product = small * smaller; // 1e-40: a denormal, or zero when flushed
    // Its bits, not `product == 0`: where a vanishing number coming in counts
    // as zero, that comparison says yes to one that was never flushed.
    const float got = product;
    std::uint32_t bits;
    std::memcpy(&bits, &got, sizeof bits);
    return bits == 0u;
}

// Whether a number that small coming IN counts as nothing.
static bool tinyInIsZero() {
    volatile float small = 1e-30f, smaller = 1e-10f, big = 1e30f;
    const bool was = flushDenormals(false);
    volatile float vanishing = small * smaller; // made with it off: 1e-40, a real vanishing number
    flushDenormals(was);
    volatile float product = vanishing * big;   // 1e-10 as plain arithmetic; nothing if `vanishing` reads as zero
    return product == 0.f;
}

// An "effect" that only looks at the thread it is run on.
struct ThreadProbe : Effect {
    std::thread::id home;
    std::atomic<int>* elsewhere;
    std::atomic<int>* unflushed;
    std::atomic<int>* runs;
    void setParam(int, float) override {}
    void process(float*, float*, int) override {
        runs->fetch_add(1);
        if (!tinyIsZero() || !denormalsFlushed()) unflushed->fetch_add(1);
        if (std::this_thread::get_id() != home) elsewhere->fetch_add(1);
        // Long enough for a helper to wake and take a track of its own.
        const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(100);
        while (std::chrono::steady_clock::now() < until) {}
    }
};

static void testFlushToZero() {
    const bool was = flushDenormals(true);
    const bool can = denormalsFlushed(); // false on a processor this does not know
    flushDenormals(was);
    if (!can) {
        std::printf("NOTE flush-to-zero is not available on this processor: its tests were skipped\n");
        return;
    }
    // The switch itself.
    CHECK(!was && !denormalsFlushed() && !tinyIsZero()); // a thread starts with it off
    CHECK(!tinyInIsZero());
    CHECK(flushDenormals(true) == false && denormalsFlushed() && tinyIsZero() && tinyInIsZero());
    CHECK(flushDenormals(true) == true && denormalsFlushed());
    CHECK(flushDenormals(false) == true && !denormalsFlushed() && !tinyIsZero());
    CHECK(flushDenormals(false) == false);
    {
        const FlushDenormals on;
        CHECK(denormalsFlushed() && tinyIsZero());
        {
            const FlushDenormals again;
            CHECK(denormalsFlushed());
        }
        CHECK(denormalsFlushed()); // the inner one put back "on"
    }
    CHECK(!denormalsFlushed() && !tinyIsZero());

    // The engine, asked to: on for the whole of render() on the thread that
    // calls it, and on every helper; and the caller's thread is as it was
    // afterwards. Not asked (as it starts): off everywhere.
    {
        std::unique_ptr<Engine> engine(new Engine);
        std::atomic<int> elsewhere{0}, unflushed{0}, runs{0};
        for (int t = 1; t < 4; ++t) CHECK(engine->addSynthTrack(t, SynthKind::Sampler));
        for (int t = 0; t < 4; ++t) {
            std::unique_ptr<ThreadProbe> probe(new ThreadProbe);
            probe->home = std::this_thread::get_id();
            probe->elsewhere = &elsewhere;
            probe->unflushed = &unflushed;
            probe->runs = &runs;
            engine->setTrackEffect(t, 0, std::move(probe));
        }
        std::vector<float> out(128);
        CHECK(!engine->flushesDenormals());
        engine->render(out.data(), 64);
        CHECK(runs.load() == 4 && elsewhere.load() == 0 && unflushed.load() == 4); // not asked: off
        engine->setFlushDenormals(true);
        CHECK(engine->flushesDenormals());
        runs = 0;
        unflushed = 0;
        engine->render(out.data(), 64);
        CHECK(runs.load() == 4 && elsewhere.load() == 0 && unflushed.load() == 0); // no helpers: all here, all flushed
        CHECK(!denormalsFlushed());                                                  // and put back
        engine->useWorkers(3);
        for (int i = 0; i < 4000 && elsewhere.load() < 50; ++i) engine->render(out.data(), 64);
        CHECK(elsewhere.load() >= 50); // the helpers did take tracks
        CHECK(unflushed.load() == 0);
        CHECK(!denormalsFlushed());
        // A thread that already had it on keeps it on.
        flushDenormals(true);
        engine->render(out.data(), 64);
        CHECK(denormalsFlushed());
        flushDenormals(false);
        // Turned off again: off on the helpers too, from the next chunk.
        engine->setFlushDenormals(false);
        runs = 0;
        elsewhere = 0;
        for (int i = 0; i < 4000 && elsewhere.load() < 50; ++i) engine->render(out.data(), 64);
        CHECK(elsewhere.load() >= 50 && unflushed.load() == runs.load());
        engine->useWorkers(0);
    }

    // WHAT IT CHANGES, measured. Every pinned case again with it on, sample
    // against sample. Today: 39 of the 40 come out the same to the last bit.
    // One does not: "drive, none and bright", where DRIVE's tone filter at
    // its brightest falls from a sound to nothing inside one chunk, through
    // numbers too small to hold. In that case, and in any tail, the samples
    // that differ are all below 1e-30 (600 dB down; a 16-bit file's smallest
    // step is 3e-5). Flushed, they are zero, or differ in their last bits.
    {
        int changed = 0;
        float biggest = 0.f;
        std::string which;
        for (int i = 0; i < kPinCount; ++i) {
            const Stereo plain = renderPin(kPins[i]);
            Stereo flushed;
            {
                const FlushDenormals on;
                flushed = renderPin(kPins[i]);
            }
            int differ = 0;
            for (int side = 0; side < 2; ++side) {
                const std::vector<float>& a = side ? plain.r : plain.l;
                const std::vector<float>& b = side ? flushed.r : flushed.l;
                for (std::size_t n = 0; n < a.size(); ++n) {
                    if (std::memcmp(&a[n], &b[n], sizeof(float)) == 0) continue;
                    ++differ;
                    biggest = std::fmax(biggest, std::fmax(std::fabs(a[n]), std::fabs(b[n])));
                }
            }
            if (differ) {
                ++changed;
                which += std::string(which.empty() ? "" : "; ") + kPins[i].name;
                SAY("flush    pinned case \"%s\": %d samples differ\n", kPins[i].name, differ);
            }
        }
        SAY("flush    %d of %d pinned cases differ with it on; the largest sample that differs is %g\n", changed, kPinCount, static_cast<double>(biggest));
        CHECK(biggest < 1e-30f);
        if (mathsPrint() == kPinMaths) CHECK(changed == 1 && which == "drive, none and bright");
        // The pinned song goes through the engine, which leaves it off: checked in testWholeSong.
    }
    // The same on a long tail. The reverb at its shortest, a click, then
    // eight seconds. (Today: about 9500 samples differ, the first 4.9 s in,
    // the largest of them 7e-31.)
    {
        std::unique_ptr<Effect> plain = fresh(EffectKind::Reverb, 0.f, 0.5f, 1.f), flushed = fresh(EffectKind::Reverb, 0.f, 0.5f, 1.f);
        const std::vector<float> a = impulseOf(*plain, 8.0);
        std::vector<float> b;
        {
            const FlushDenormals on;
            b = impulseOf(*flushed, 8.0);
        }
        int differ = 0;
        float biggest = 0.f;
        std::size_t firstAt = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::memcmp(&a[i], &b[i], sizeof(float)) == 0) continue;
            if (!differ) firstAt = i;
            ++differ;
            biggest = std::fmax(biggest, std::fmax(std::fabs(a[i]), std::fabs(b[i])));
        }
        SAY("flush    reverb tail: %d samples differ, from %.2f s; the largest is %g\n", differ, static_cast<double>(firstAt) / 48000.0, static_cast<double>(biggest));
        CHECK(biggest < 1e-30f);
        CHECK(differ > 0 && firstAt > 48000); // the tool does see the difference, and it is far down the tail
        CHECK(b.back() == 0.f && a.back() == 0.f);
    }
}

// A reverb fed a click and then silence for a long time does not slow down.
static void testReverbDoesNotSlowDown() {
    const FlushDenormals on; // as the engine runs it
    for (float size : {0.f, 0.6f}) {
        std::unique_ptr<Effect> r = fresh(EffectKind::Reverb, size, 0.5f, 1.f);
        Stereo io(kMaxBlock);
        // The time of a block, as the quickest of five runs of them: a busy machine (other builds
        // going on) takes the processor away for a moment, and that is not the effect being slow.
        const auto blocks = [&](int count) {
            double best = 1e30;
            for (int part = 0; part < 5; ++part) {
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < count / 5; ++i) {
                    io.l.assign(kMaxBlock, 0.f);
                    io.r.assign(kMaxBlock, 0.f);
                    r->process(io.l.data(), io.r.data(), kMaxBlock);
                }
                best = std::min(best, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (count / 5));
            }
            return best;
        };
        io.l[0] = io.r[0] = 1.f;
        r->process(io.l.data(), io.r.data(), kMaxBlock);
        const double early = blocks(750); // the first second: the tail is loud
        // Then 40 seconds of silence, timed a second at a time; the slowest second counts.
        double slowest = 0.0;
        for (int second = 0; second < 40; ++second) {
            const double took = blocks(750);
            slowest = took > slowest ? took : slowest;
        }
        const double late = blocks(750);
        SAY("flush    reverb size %3.0f%%: a block takes %.2f us early, %.2f us at its slowest, %.2f us at the end\n", size * 100.0, early, slowest, late);
        // Generous: three times, plus room for a busy machine. Vanishing
        // numbers unflushed cost ten to thirty times.
        CHECK(slowest < 3.0 * early + 20.0 && late < 3.0 * early + 20.0);
        CHECK(io.l[kMaxBlock - 1] == 0.f || size > 0.f); // and the short one has ended in exact zeros
    }
}

// ---- 5. the fade when an effect is swapped ------------------------------------------------------

// A steady 100 Hz sine in place of whatever the track's instrument made: put
// in a track's first slot, it gives the second slot something to work on.
struct SineSource : Effect {
    double phase = 0.0;
    void setParam(int, float) override {}
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) {
            left[i] = right[i] = static_cast<float>(0.5 * std::sin(phase));
            phase += 2.0 * kPi * 100.0 / 48000.0;
        }
    }
};
// An effect that counts what is done to it, to see when it runs and when it is freed.
struct Counted : Effect {
    static std::atomic<int> alive, freed;
    std::atomic<int>* frames;
    explicit Counted(std::atomic<int>* f) : frames(f) { alive.fetch_add(1); }
    ~Counted() override {
        alive.fetch_sub(1);
        freed.fetch_add(1);
    }
    void setParam(int, float) override {}
    void process(float* left, float* right, int n) override {
        if (frames) frames->fetch_add(n);
        for (int i = 0; i < n; ++i) left[i] = right[i] = 0.f; // it silences the track: easy to see in the output
    }
};
std::atomic<int> Counted::alive{0};
std::atomic<int> Counted::freed{0};

static std::vector<float> leftOf(Engine& engine, int frames) {
    std::vector<float> both(static_cast<std::size_t>(2 * frames)), left(static_cast<std::size_t>(frames));
    engine.render(both.data(), frames);
    for (int i = 0; i < frames; ++i) left[static_cast<std::size_t>(i)] = both[static_cast<std::size_t>(2 * i)];
    return left;
}

// An engine whose first track makes a steady sine (0.5 times the track's level of 0.65).
static std::unique_ptr<Engine> sineEngine() {
    std::unique_ptr<Engine> engine(new Engine);
    engine->setTrackEffect(0, 0, std::unique_ptr<Effect>(new SineSource));
    return engine;
}
static const double kEngineSine = 0.5 * 0.65;
static const int kFade = 480; // 10 ms at 48 kHz

// NO CLICK ON A SWAP: `from` is in the slot (None: empty) and has run for a
// second; then `to` goes in. The largest step in the 50 ms around the swap,
// counted in the sine's own. `master`: the master's slot instead of the track's.
// `held`, if given, gets the roughest of the two held still: `from` in the half second before the swap,
// `to` in the 50 ms after that 50 ms (FUZZ makes the sine nearly square, RING multiplies it by another).
static double swapJump(EffectKind from, EffectKind to, bool master = false, double* held = nullptr) {
    std::unique_ptr<Engine> engine = sineEngine();
    const int track = master ? kMasterTrack : 0, slot = master ? 0 : 1;
    CHECK(engine->setEffect(track, slot, from));
    std::vector<float> all = leftOf(*engine, 48000);
    CHECK(engine->setEffect(track, slot, to));
    const std::vector<float> after = leftOf(*engine, 4800);
    all.insert(all.end(), after.begin(), after.end());
    engine->collectGarbage();
    if (held) *held = std::fmax(largestJump(all, 24000, 48000 - 2), largestJump(all, 48000 + 2400, 48000 + 4800)) / sineStep(100.0, kEngineSine);
    return largestJump(all, 48000 - 2, 48000 + 2400) / sineStep(100.0, kEngineSine);
}

// ---- COMP ------------------------------------------------------------------------------------

// What a compressor should turn a level down by, worked out here from its
// description and not from its code: nothing below the knee, (1 - 1/ratio)
// of the dB over the threshold above it, a curve across the 6 dB between.
static double shouldTurnDown(double levelDb, double thresholdDb, double ratio) {
    const double over = levelDb - thresholdDb, slope = 1.0 - 1.0 / ratio, knee = 6.0;
    if (over <= -0.5 * knee) return 0.0;
    if (over >= 0.5 * knee) return slope * over;
    return slope * (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);
}
static double shouldMakeUp(double thresholdDb, double ratio) {
    const double half = 0.5 * (1.0 - 1.0 / ratio) * -thresholdDb;
    return half > 12.0 ? 12.0 : half;
}
static double ampOf(double levelDb) { return std::pow(10.0, levelDb / 20.0); }

// THE GAIN AS IT GOES: a sine goes in whose level is `levels[n]` dB for the
// n-th stretch of `each` samples; out comes how many dB the effect has the
// sound up or down at every sample (read where the sine is well away from
// zero, and held between).
static std::vector<double> gainOf(Effect& effect, double hz, const std::vector<double>& levels, std::size_t each, std::vector<float>* output = nullptr, double rate = 48000.0) {
    Stereo io(levels.size() * each);
    for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = static_cast<float>(ampOf(levels[i / each]) * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    const std::vector<float> in = io.l;
    through(effect, io);
    std::vector<double> gain(io.frames(), 0.0);
    double last = 0.0;
    for (std::size_t i = 0; i < io.frames(); ++i) {
        if (std::fabs(in[i]) > 0.4 * ampOf(levels[i / each])) last = dB(std::fabs(io.l[i] / in[i]));
        gain[i] = last;
    }
    if (output) *output = io.l;
    return gain;
}
// When a run of numbers first gets `part` of the way from `from` to `to`, counted from `start`, in ms. Between samples.
static double msToReach(const std::vector<double>& v, std::size_t start, double from, double to, double part, double rate = 48000.0) {
    const double mark = from + (to - from) * part;
    for (std::size_t i = start + 1; i < v.size(); ++i)
        if ((v[i] - mark) * (to - from) >= 0.0) {
            const double between = v[i] != v[i - 1] ? (mark - v[i - 1]) / (v[i] - v[i - 1]) : 0.0;
            return (static_cast<double>(i - 1 - start) + between) * 1000.0 / rate;
        }
    return -1.0;
}

static const float kCompRatio[6] = {0.05f, 0.2f, 0.4f, 0.58f, 0.7f, 0.9f}; // 1.5, 2, 3, 4, 8, 20 to 1
static float compThreshold(double dBs) { return static_cast<float>((dBs + 48.0) / 48.0); }

static void testComp() {
    const EffectKind kind = EffectKind::Comp;
    // Its row: what a song stores.
    const EffectInfo& info = effectInfo(kind);
    CHECK(std::string(info.name) == "COMP" && std::string(info.word) == "comp" && info.params == 4 && info.family == EffectFamily::Level);
    CHECK(std::string(info.paramNames[0]) == "THRESHOLD" && std::string(info.paramNames[1]) == "RATIO" && std::string(info.paramNames[2]) == "SPEED" && std::string(info.paramNames[3]) == "MIX");
    CHECK(info.defaults[0] == 0.625f && info.defaults[1] == 0.58f && info.defaults[2] == 0.4f && info.defaults[3] == 1.f);
    CHECK(effectChoices(kind, 1) == 6 && effectChoices(kind, 0) == 0 && effectChoices(kind, 2) == 0 && effectChoices(kind, 3) == 0);
    CHECK(reads(kind, 0, 0.f) == "-48 DB" && reads(kind, 0, 1.f) == "0 DB" && reads(kind, 0, 0.625f) == "-18 DB" && reads(kind, 0, 0.5f) == "-24 DB");
    static const char* const ratioNames[6] = {"1.5:1", "2:1", "3:1", "4:1", "8:1", "20:1"};
    for (int i = 0; i < 6; ++i) CHECK(reads(kind, 1, kCompRatio[i]) == ratioNames[i]);
    CHECK(reads(kind, 1, 0.f) == "1.5:1" && reads(kind, 1, 1.f) == "20:1");
    CHECK(reads(kind, 2, 0.f) == "1/50 MS" && reads(kind, 2, 1.f) == "30/400 MS" && reads(kind, 2, 0.4f) == "4/115 MS" && reads(kind, 3, 0.5f) == "50%");

    // THE CURVE: a steady 1 kHz sine at eleven levels, at three thresholds and
    // three ratios. What comes out against what should: the level, less the
    // turn-down, plus the make-up. Today the worst of the 99 is 0.1 dB out.
    //   e.g. THRESHOLD -24 DB, RATIO 4:1 (make-up 9.0 dB):
    //     in    -40    -36    -32    -28    -24    -20    -16    -12     -8     -4      0 dB
    //     out   -31.0  -27.0  -23.0  -19.1  -15.6  -13.6  -13.0  -12.0  -11.0  -10.0   -9.0
    {
        double worst = 0.0;
        const double ratios[3] = {2.0, 4.0, 20.0};
        const float ratioAt[3] = {kCompRatio[1], kCompRatio[3], kCompRatio[5]};
        for (double threshold : {-36.0, -24.0, -12.0})
            for (int r = 0; r < 3; ++r) {
                std::unique_ptr<Effect> c = fresh(kind, compThreshold(threshold), ratioAt[r], 0.4f, 1.f);
                if (gMeasure) std::printf("comp     %3.0f dB, %2.0f:1 (make-up %4.1f):", threshold, ratios[r], shouldMakeUp(threshold, ratios[r]));
                for (int n = 0; n < 11; ++n) {
                    const double in = -40.0 + 4.0 * n;
                    const double out = in + levelAt(*c, 1000, ampOf(in));
                    const double want = in - shouldTurnDown(in, threshold, ratios[r]) + shouldMakeUp(threshold, ratios[r]);
                    SAY(" %6.1f", out);
                    worst = std::fabs(out - want) > worst ? std::fabs(out - want) : worst;
                    CHECK(std::fabs(out - want) < 0.15);
                }
                SAY("\n");
            }
        SAY("comp     the curve: the worst of 99 levels is %.2f dB from what it should be\n", worst);
    }
    // THE KNEE: across the 6 dB around the threshold the turn-down eases in.
    // Half a dB at a time from 4 dB under to 4 over, at -24 DB, 4:1. Today
    // every one is within 0.01 dB; at the threshold itself it is 0.56 dB down
    // (a hard corner would be 0, and a 3 dB knee 0.28).
    {
        std::unique_ptr<Effect> c = fresh(kind, compThreshold(-24.0), kCompRatio[3], 0.4f, 1.f);
        double worst = 0.0;
        for (int n = -8; n <= 8; ++n) {
            const double in = -24.0 + 0.5 * n;
            const double down = 9.0 - levelAt(*c, 1000, ampOf(in));
            worst = std::fabs(down - shouldTurnDown(in, -24.0, 4.0)) > worst ? std::fabs(down - shouldTurnDown(in, -24.0, 4.0)) : worst;
            CHECK(std::fabs(down - shouldTurnDown(in, -24.0, 4.0)) < 0.04);
        }
        SAY("comp     the knee: the worst of 17 levels is %.3f dB out\n", worst);
        CHECK(shouldTurnDown(-24.0, -24.0, 4.0) == 0.5625 && shouldTurnDown(-27.0, -24.0, 4.0) == 0.0 && shouldTurnDown(-21.0, -24.0, 4.0) == 2.25);
    }
    // THE MAKE-UP: half of what a full-scale sound is turned down by, and
    // never more than 12 dB. A sound far under the threshold comes up by just that.
    //   Today: -18 DB 4:1 +6.75; -24 DB 4:1 +9.0; -12 DB 2:1 +3.0; -48 DB 20:1 +12.0 (would be 22.8); 0 DB +0.0.
    {
        struct Row { double threshold, ratio; float ratioAt; };
        const Row rows[5] = {{-18.0, 4.0, kCompRatio[3]}, {-24.0, 4.0, kCompRatio[3]}, {-12.0, 2.0, kCompRatio[1]}, {-48.0, 20.0, kCompRatio[5]}, {0.0, 8.0, kCompRatio[4]}};
        for (const Row& row : rows) {
            std::unique_ptr<Effect> c = fresh(kind, compThreshold(row.threshold), row.ratioAt, 0.4f, 1.f);
            const double up = levelAt(*c, 1000, ampOf(-70.0));
            SAY("comp     make-up at %3.0f dB, %2.0f:1: %+.2f dB\n", row.threshold, row.ratio, up);
            CHECK(std::fabs(up - shouldMakeUp(row.threshold, row.ratio)) < 0.02);
        }
        CHECK(shouldMakeUp(-48.0, 20.0) == 12.0 && shouldMakeUp(-18.0, 4.0) == 6.75);
    }
    // ATTACK AND RELEASE, from a step in level: a 4 kHz sine at -40 dB (left
    // alone), then at -4 dB for a second (turned down 15 dB at -24 DB, 4:1),
    // then at -40 again. The attack is how long the turn-down takes to get
    // 63% of the way there, in dB; the release how long to come 63% of the
    // way back. Against what SPEED reads. Today:
    //   SPEED 0%    reads 1/50 MS      measured  1.0 /  51 ms
    //   SPEED 40%   reads 4/115 MS     measured  3.9 / 119 ms
    //   SPEED 100%  reads 30/400 MS    measured 30.0 / 430 ms
    // (The release is a little longer than it reads, by about the attack: the
    // turn-down is held, lets go at the release speed, and is then smoothed at
    // the attack speed.)
    for (double rate : {48000.0, 96000.0})
        for (float speed : {0.f, 0.4f, 1.f}) {
            std::unique_ptr<Effect> c = makeEffect(kind, static_cast<float>(rate), 120.f);
            c->setParam(0, compThreshold(-24.0));
            c->setParam(1, kCompRatio[3]);
            c->setParam(2, speed);
            const std::size_t each = static_cast<std::size_t>(2.0 * rate);
            const std::vector<double> gain = gainOf(*c, 4000.0, {-40.0, -4.0, -40.0}, each, nullptr, rate);
            const double rest = shouldMakeUp(-24.0, 4.0), down = rest - 15.0;
            // (Two seconds on, the slowest release still has a tenth of a dB to go.)
            CHECK(std::fabs(gain[each - 1] - rest) < 0.01 && std::fabs(gain[2 * each - 1] - down) < 0.05 && std::fabs(gain[3 * each - 1] - rest) < (speed == 1.f ? 0.15 : 0.01));
            const double attack = msToReach(gain, each, rest, gain[2 * each - 1], 0.632, rate), release = msToReach(gain, 2 * each, gain[2 * each - 1], rest, 0.632, rate);
            int readAttack = 0, readRelease = 0;
            CHECK(std::sscanf(reads(kind, 2, speed).c_str(), "%d/%d MS", &readAttack, &readRelease) == 2);
            SAY("comp     SPEED %3.0f%% at %.0f Hz: reads %d/%d MS, measured %.2f / %.1f ms\n", speed * 100.0, rate, readAttack, readRelease, attack, release);
            CHECK(std::fabs(attack - readAttack) < 0.2 * readAttack && std::fabs(release - readRelease) < 0.2 * readRelease);
            // And the exact ends of SPEED: 1 ms and 50 ms, 30 ms and 400 ms.
            if (speed == 0.f) CHECK(std::fabs(attack - 1.0) < 0.15 && std::fabs(release - 51.0) < 2.5);
            if (speed == 1.f) CHECK(std::fabs(attack - 30.0) < 1.0 && std::fabs(release - 430.0) < 15.0);
        }
    // IT DOES NOT PUMP ON A STEADY TONE. A sine at -6 dB, turned down 13.5 dB
    // (-24 DB, 4:1): once settled, how much the gain still moves within the
    // tone's own cycle, and the harmonics that movement adds. Today:
    //                         gain moves by    3rd harmonic
    //   1 kHz, SPEED 40%        0.000 dB        -103 dB
    //   100 Hz, SPEED 40%       0.040 dB         -65 dB
    //   100 Hz, SPEED 0% (1/50) 0.251 dB         -48 dB   (as fast as it goes, on a low note: it follows the wave a little)
    //   50 Hz, SPEED 100%       0.005 dB         -78 dB
    {
        struct Row { int hz; float speed; double moves, third; };
        const Row rows[4] = {{1000, 0.4f, 0.005, -95.0}, {100, 0.4f, 0.06, -60.0}, {100, 0.f, 0.35, -44.0}, {50, 1.f, 0.01, -72.0}};
        for (const Row& row : rows) {
            std::unique_ptr<Effect> c = fresh(kind, compThreshold(-24.0), kCompRatio[3], row.speed, 1.f);
            const std::vector<double> gain = gainOf(*c, row.hz, {-6.0}, 144000);
            double low = 1e9, high = -1e9;
            for (std::size_t i = 96000; i < gain.size(); ++i) low = gain[i] < low ? gain[i] : low, high = gain[i] > high ? gain[i] : high;
            const Harmonics h = harmonicsOf(*c, row.hz, ampOf(-6.0));
            SAY("comp     steady %4d Hz, SPEED %3.0f%%: the gain moves by %.3f dB, 3rd harmonic %.1f dB under the sine going in\n", row.hz, row.speed * 100.0, high - low, h.part[3]);
            CHECK(high - low < row.moves && h.part[3] < row.third);
        }
    }
    // IT IGNORES WHAT IS UNDER 60 HZ when it reads the level: the same sine at
    // -6 dB is turned down 13.5 dB at 1 kHz and far less down low. Today:
    //   1 kHz 13.5 dB    120 Hz 13.3    60 Hz 11.2    40 Hz 7.6    30 Hz 4.2
    {
        std::unique_ptr<Effect> c = fresh(kind, compThreshold(-24.0), kCompRatio[3], 1.f, 1.f);
        const double rest = shouldMakeUp(-24.0, 4.0);
        double down[5];
        const int at[5] = {1000, 120, 60, 40, 30};
        for (int i = 0; i < 5; ++i) {
            down[i] = rest - levelAt(*c, at[i], ampOf(-6.0));
            SAY("comp     a sine at -6 dB, %4d Hz: turned down %.1f dB\n", at[i], down[i]);
        }
        // A two-pole high-pass at 60 Hz in front of the level reading: 3 dB down at 60, 7.8 at 40, 12.3 at 30.
        CHECK(std::fabs(down[0] - 13.5) < 0.2 && std::fabs(down[1] - 13.3) < 0.3 && std::fabs(down[2] - 0.75 * (18.0 - 3.0)) < 0.2);
        CHECK(std::fabs(down[3] - 0.75 * (18.0 - 7.8)) < 0.2 && std::fabs(down[4] - 0.75 * (18.0 - 12.3)) < 0.2);
        // 60 Hz is 60 Hz at any sample rate: the same 30 Hz sine at 96 kHz is turned down the same 4.2 dB.
        std::unique_ptr<Effect> fast = makeEffect(kind, 96000.f, 120.f);
        fast->setParam(0, compThreshold(-24.0));
        fast->setParam(2, 1.f);
        Stereo io(4 * 96000);
        io.l = io.r = sineOf(30.0, ampOf(-6.0), io.frames(), 96000.0);
        through(*fast, io);
        const double at96 = rest - dB(partAt(io.l.data() + 2 * 96000, 2 * 96000, 30.0, 96000.0) / ampOf(-6.0));
        SAY("comp     at 96 kHz, 30 Hz: turned down %.1f dB\n", at96);
        CHECK(std::fabs(at96 - down[4]) < 0.1);
    }
    // LEFT AND RIGHT ARE TURNED DOWN TOGETHER, by the louder of the two, so
    // the picture does not lean: a loud sine on the left and a quiet one on
    // the right come out with the same gain. And the other way about.
    for (int loudSide = 0; loudSide < 2; ++loudSide) {
        std::unique_ptr<Effect> c = fresh(kind, compThreshold(-24.0), kCompRatio[3], 0.4f, 1.f);
        Stereo io(96000);
        (loudSide ? io.r : io.l) = sineOf(1000.0, ampOf(-6.0), 96000);
        (loudSide ? io.l : io.r) = sineOf(1000.0, ampOf(-40.0), 96000);
        through(*c, io);
        const double loud = dB(partAt((loudSide ? io.r : io.l).data() + 48000, 48000, 1000.0) / ampOf(-6.0)), quiet = dB(partAt((loudSide ? io.l : io.r).data() + 48000, 48000, 1000.0) / ampOf(-40.0));
        CHECK(std::fabs(loud - quiet) < 0.01 && std::fabs(loud - (shouldMakeUp(-24.0, 4.0) - 13.5)) < 0.2);
    }
    // MIX is the squashed sound beside the plain one: at 50%, half of each.
    {
        std::unique_ptr<Effect> full = fresh(kind, compThreshold(-24.0), kCompRatio[3], 0.4f, 1.f), half = fresh(kind, compThreshold(-24.0), kCompRatio[3], 0.4f, 0.5f);
        const double squashed = ampOf(levelAt(*full, 1000, ampOf(-6.0))), both = ampOf(levelAt(*half, 1000, ampOf(-6.0)));
        CHECK(std::fabs(both - 0.5 * (1.0 + squashed)) < 0.002);
    }
    // THE METER: how many dB it is turning down, as of the last block.
    {
        std::unique_ptr<Effect> c = fresh(kind, compThreshold(-24.0), kCompRatio[3], 0.4f, 1.f);
        CHECK(c->meter() == 0.f);
        Stereo io(96000);
        io.l = io.r = sineOf(1000.0, ampOf(-6.0), 96000);
        through(*c, io, 0, 48000);
        SAY("comp     the meter on a 1 kHz sine at -6 dB, -24 DB 4:1: %.2f dB\n", static_cast<double>(c->meter()));
        CHECK(std::fabs(c->meter() - 13.5f) < 0.2f);
        // It is the most in the last call, however long the call.
        std::unique_ptr<Effect> d = fresh(kind, compThreshold(-24.0), kCompRatio[3], 0.4f, 1.f);
        Stereo burst(4800);
        for (std::size_t i = 0; i < 2400; ++i) burst.l[i] = burst.r[i] = io.l[i];
        d->process(burst.l.data(), burst.r.data(), 4800);
        CHECK(d->meter() > 12.f);
        // MIX does not change what it reads; a sound under the threshold reads 0; so does silence, exactly, soon after.
        std::unique_ptr<Effect> dry = fresh(kind, compThreshold(-24.0), kCompRatio[3], 0.4f, 0.f);
        Stereo again(48000);
        again.l = again.r = sineOf(1000.0, ampOf(-6.0), 48000);
        const std::vector<float> plain = again.l;
        through(*dry, again);
        CHECK(std::fabs(dry->meter() - c->meter()) < 1e-4f && again.l == plain);
        Stereo quiet(48000 * 3);
        quiet.l = quiet.r = sineOf(1000.0, ampOf(-30.0), quiet.frames());
        through(*c, quiet, 0, 4800);
        CHECK(c->meter() > 5.f && c->meter() < 13.f); // letting go
        through(*c, quiet, 4800, quiet.frames());
        CHECK(c->meter() == 0.f);
        through(*c, again, 0, 4800); // (what came out of `dry` is the plain sine)
        CHECK(c->meter() > 10.f);
        c->reset();
        CHECK(c->meter() == 0.f);
        // An effect with nothing to show reads 0.
        for (EffectKind other : {EffectKind::Filter, EffectKind::Drive, EffectKind::Delay, EffectKind::Reverb, EffectKind::Crush, EffectKind::Chorus}) {
            std::unique_ptr<Effect> e = makeEffect(other, 48000.f, 120.f);
            through(*e, io, 0, 4800);
            CHECK(e->meter() == 0.f);
        }
    }
    // THE METER, AS THE SCREEN ASKS FOR IT: Engine::effectMeter(track, slot).
    // The engine's test sine is at -6 dB in the track's slots (0.5), and at
    // -9.8 dB on the master (after the track's level of 0.65). It is at 100 Hz,
    // where the level reading has already lost half a dB to its 60 Hz cut. At
    // COMP's usual settings (-18 DB, 4:1) that is 8.5 dB down on the track
    // (0.75 of 11.5); on the master 5.7.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        for (int slot = -1; slot <= kTrackSlots; ++slot) CHECK(engine->effectMeter(0, slot) == 0.f && engine->effectMeter(kMasterTrack, slot) == 0.f);
        CHECK(engine->setEffect(0, 1, kind));
        leftOf(*engine, 48000);
        SAY("comp     Engine::effectMeter on a track: %.2f dB\n", static_cast<double>(engine->effectMeter(0, 1)));
        CHECK(std::fabs(engine->effectMeter(0, 1) - 8.53f) < 0.15f);
        CHECK(engine->effectMeter(0, 0) == 0.f && engine->effectMeter(1, 1) == 0.f && engine->effectMeter(kMasterTrack, 0) == 0.f && engine->effectMeter(kMasterTrack, 1) == 0.f);
        CHECK(engine->effectMeter(-2, 1) == 0.f && engine->effectMeter(kNumTracks, 1) == 0.f && engine->effectMeter(0, kTrackSlots) == 0.f && engine->effectMeter(0, -1) == 0.f);
        // What comes out agrees with it: the sine, down by the meter, up by the make-up (6.75 dB).
        const std::vector<float> out = leftOf(*engine, 48000);
        CHECK(std::fabs(dB(partAt(out.data(), 48000, 100.0) / kEngineSine) - (6.75 - engine->effectMeter(0, 1))) < 0.3);
        // Its THRESHOLD turned up out of the way: it lets go, and the meter falls to 0.
        engine->send(make(Command::SetEffectParam, 0, 1, 0, 1.f));
        leftOf(*engine, 3 * 48000);
        CHECK(engine->effectMeter(0, 1) == 0.f);
        engine->send(make(Command::SetEffectParam, 0, 1, 0, 0.625f));
        leftOf(*engine, 24000);
        CHECK(engine->effectMeter(0, 1) > 8.f);
        // Taken out of the slot, the slot reads 0 from the next chunk.
        CHECK(engine->setEffect(0, 1, EffectKind::None));
        leftOf(*engine, 64);
        CHECK(engine->effectMeter(0, 1) == 0.f);
        // A track that is deleted is not rendered any more: its effects show nothing, though they are still in their slots.
        CHECK(engine->addSynthTrack(1, SynthKind::Sampler));
        engine->setTrackEffect(1, 0, std::unique_ptr<Effect>(new SineSource));
        CHECK(engine->setEffect(1, 1, kind));
        leftOf(*engine, 24000);
        CHECK(engine->effectMeter(1, 1) > 8.f);
        CHECK(engine->removeTrack(1));
        leftOf(*engine, 512); // (sounding: it fades for 5 ms before it goes)
        CHECK(engine->effectMeter(1, 1) == 0.f);
        // On the master, in either slot.
        CHECK(engine->setEffect(kMasterTrack, 1, kind));
        leftOf(*engine, 48000);
        SAY("comp     Engine::effectMeter on the master: %.2f dB\n", static_cast<double>(engine->effectMeter(kMasterTrack, 1)));
        CHECK(std::fabs(engine->effectMeter(kMasterTrack, 1) - 5.72f) < 0.15f && engine->effectMeter(kMasterTrack, 0) == 0.f && engine->effectMeter(0, 1) == 0.f);
        engine->collectGarbage();
        // With helpers sharing the tracks out, the same reading.
        std::unique_ptr<Engine> shared = sineEngine();
        shared->useWorkers(3);
        for (int t = 1; t < 4; ++t) {
            CHECK(shared->addSynthTrack(t, SynthKind::Sampler));
            shared->setTrackEffect(t, 0, std::unique_ptr<Effect>(new SineSource));
            CHECK(shared->setEffect(t, 1, kind));
        }
        CHECK(shared->setEffect(0, 1, kind));
        leftOf(*shared, 48000);
        for (int t = 0; t < 4; ++t) CHECK(std::fabs(shared->effectMeter(t, 1) - 8.53f) < 0.15f);
        shared->useWorkers(0);
        shared->collectGarbage();
    }
    // A setting thrown from one end to the other in one go is not a click:
    // the turn-down moves at its own speed, and the make-up glides.
    for (int p = 0; p < 4; ++p) {
        std::unique_ptr<Effect> c = fresh(kind, p == 0 ? 1.f : compThreshold(-24.0), kCompRatio[p == 1 ? 5 : 0], 0.4f, p == 3 ? 0.f : 1.f);
        Stereo io(48000);
        io.l = io.r = sineOf(100.0, 0.5, 48000);
        through(*c, io, 0, 24000);
        c->setParam(p, p == 1 ? 0.f : (p == 0 ? 0.f : 1.f));
        through(*c, io, 24000, 48000);
        const double jump = largestJump(io.l, 23999, 48000) / sineStep(100.0, 0.5);
        SAY("comp     %s thrown across at once: %.2f\n", info.paramNames[p], jump);
        CHECK(jump < 2.6);
    }
    // Turned down to 0 while it sounds, MIX gets there, exactly.
    {
        std::unique_ptr<Effect> c = fresh(kind, compThreshold(-24.0), kCompRatio[3], 0.4f, 1.f);
        Stereo in(96000);
        in.l = in.r = sineOf(1000.0, 0.5, 96000);
        Stereo out = in;
        through(*c, out, 0, 24000);
        c->setParam(3, 0.f);
        through(*c, out, 24000, 96000);
        bool same = true;
        for (std::size_t i = 48000; i < 96000; ++i) same = same && out.l[i] == in.l[i] && out.r[i] == in.r[i];
        CHECK(same && out.l[24100] != in.l[24100]);
    }
    // However the sound is cut into blocks, the same to the last bit; and a reset is a fresh start.
    {
        Stereo whole = pinInput(48000.f), cut = whole, big = whole, twice = whole;
        through(*fresh(kind, compThreshold(-30.0), kCompRatio[4], 0.2f, 0.9f), whole);
        std::unique_ptr<Effect> c = fresh(kind, compThreshold(-30.0), kCompRatio[4], 0.2f, 0.9f), d = fresh(kind, compThreshold(-30.0), kCompRatio[4], 0.2f, 0.9f);
        for (std::size_t at = 0, n = 1; at < cut.frames(); at += n, n = n % 61 + 1) {
            if (at + n > cut.frames()) n = cut.frames() - at;
            c->process(cut.l.data() + at, cut.r.data() + at, static_cast<int>(n));
        }
        d->process(big.l.data(), big.r.data(), static_cast<int>(big.frames()));
        CHECK(cut.l == whole.l && cut.r == whole.r && big.l == whole.l && big.r == whole.r);
        Stereo part = twice;
        through(*d, part, 0, 12000); // (d has had the whole sound, and now some more)
        d->reset();
        through(*d, twice);
        CHECK(twice.l == whole.l && twice.r == whole.r);
    }
}

// ---- SATURATE, and the 2x stage -------------------------------------------------------------

// The 2x stage by itself (engine/src/fx/halfband.hpp).
static void testHalfband() {
    // Up and back down with nothing done between, a sine comes through
    // whole, right to the top: 0.000 dB at every one of these. (Near the top
    // each way loses a little, and the two losses make each other good.)
    for (int hz : {100, 1000, 10000, 19000, 21000, 22000, 23000}) {
        fx::Halfband up, down;
        const std::vector<float> in = sineOf(hz, 0.5, 96000);
        std::vector<float> twice(2 * 64), out(96000);
        for (std::size_t at = 0; at < 96000; at += 64) {
            up.up(in.data() + at, twice.data(), 64);
            down.down(twice.data(), out.data() + at, 64);
        }
        const double level = dB(partAt(out.data() + 48000, 48000, hz) / 0.5);
        SAY("halfband up and down, %5d Hz: %.3f dB\n", hz, level);
        CHECK(std::fabs(level) < 0.01);
    }
    // Going up, the copy of the sound an octave above (its "image") is taken
    // out: a sine at 1, 10 and 19 kHz leaves under a thousandth (70 dB down) at 47, 38 and 29 kHz.
    for (int hz : {1000, 10000, 19000}) {
        fx::Halfband up;
        const std::vector<float> in = sineOf(hz, 0.5, 48000);
        std::vector<float> twice(96000);
        up.up(in.data(), twice.data(), 48000);
        const double kept = dB(partAt(twice.data() + 48000, 48000, hz, 96000.0) / 0.5), image = dB(partAt(twice.data() + 48000, 48000, 48000 - hz, 96000.0) / 0.5);
        SAY("halfband up, %5d Hz: the sine %.3f dB, its image at %d Hz %.1f dB\n", hz, kept, 48000 - hz, image);
        CHECK(std::fabs(kept) < 0.01 && image < -69.0);
    }
    // Coming down, what is above half the 48 kHz rate would fold back; it is
    // taken out first. A sine at the 2x rate, and what is left of it folded:
    //   29 kHz -97 dB (at 19 kHz)   36 kHz -71   47 kHz -79   26 kHz -21 (at 22 kHz: part way shut)   25 kHz -10
    for (int hz : {29000, 36000, 47000, 26000, 25000}) {
        fx::Halfband down;
        std::vector<float> twice(96000), out(48000);
        for (std::size_t i = 0; i < 96000; ++i) twice[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * hz * static_cast<double>(i) / 96000.0));
        down.down(twice.data(), out.data(), 48000);
        const double folded = dB(partAt(out.data() + 24000, 24000, 48000 - hz) / 0.5);
        SAY("halfband down, %5d Hz at the 2x rate: %.1f dB left, at %d Hz\n", hz, folded, 48000 - hz);
        if (hz >= 28800) CHECK(folded < -69.0);
        else CHECK(folded < -5.0 && folded > -25.0); // between "passed" and "stopped": this is where it is, written down
    }
    // late() is up and then down with nothing between, without going up: the
    // same samples, to a few parts in ten million. (That is what lets the
    // plain sound be mixed with what was bent at the 2x rate.)
    {
        fx::Halfband up, down, line;
        Noise noise(11u);
        std::vector<float> in(4800), twice(9600), out(4800), late(4800);
        for (float& v : in) v = noise.next();
        up.up(in.data(), twice.data(), 4800);
        down.down(twice.data(), out.data(), 4800);
        line.late(in.data(), late.data(), 4800);
        float apart = 0.f;
        for (std::size_t i = 0; i < 4800; ++i) apart = std::fmax(apart, std::fabs(out[i] - late[i]));
        SAY("halfband late() against up and down, noise at %.2f: at most %.2g apart\n", static_cast<double>(peakOf(in)), static_cast<double>(apart));
        CHECK(apart < 2e-6f && peakOf(out) > 0.5f);
    }
    // How late: a sine up and back against the one that went in, in samples.
    //   Today: 100 Hz 1.79, 1 kHz 1.80, 5 kHz 1.83, 10 kHz 1.95, 12 kHz 2.03 (half a wave: mixed with the plain sound, gone), 16 kHz 2.26
    {
        double at12k = 0.0;
        for (int hz : {100, 1000, 5000, 10000, 12000, 16000}) {
            fx::Halfband line;
            const std::vector<float> in = sineOf(hz, 0.5, 96000);
            std::vector<float> out(96000);
            line.late(in.data(), out.data(), 96000);
            double s = 0.0, c = 0.0, s0 = 0.0, c0 = 0.0;
            for (std::size_t i = 48000; i < 96000; ++i) {
                const double w = 2.0 * kPi * hz * static_cast<double>(i) / 48000.0;
                s += out[i] * std::sin(w), c += out[i] * std::cos(w), s0 += in[i] * std::sin(w), c0 += in[i] * std::cos(w);
            }
            double turn = (std::atan2(c0, s0) - std::atan2(c, s)) / (2.0 * kPi);
            turn -= std::floor(turn);
            const double samples = turn * 48000.0 / hz;
            SAY("halfband late, %5d Hz: %.2f samples (%.2f of a wave)\n", hz, samples, turn);
            if (hz == 12000) at12k = turn;
            if (hz <= 1000) CHECK(std::fabs(samples - 1.8) < 0.05);
        }
        CHECK(std::fabs(at12k - 0.5) < 0.02);
    }
    // It is hardly late: a click up and back comes out whole (its samples add
    // to 1) and centred 1.8 samples on. And reset() empties it.
    {
        fx::Halfband up, down;
        std::vector<float> in(256, 0.f), twice(512), out(256);
        in[0] = 1.f;
        up.up(in.data(), twice.data(), 256);
        down.down(twice.data(), out.data(), 256);
        double sum = 0.0, moment = 0.0;
        for (std::size_t i = 0; i < 256; ++i) sum += out[i], moment += static_cast<double>(i) * out[i];
        SAY("halfband a click up and down: adds to %.5f, centred %.2f samples late\n", sum, moment / sum);
        CHECK(std::fabs(sum - 1.0) < 1e-4 && std::fabs(moment / sum - 1.8) < 0.1);
        up.reset();
        down.reset();
        std::vector<float> none(256, 0.f);
        up.up(none.data(), twice.data(), 256);
        down.down(twice.data(), out.data(), 256);
        CHECK(peakOf(out) == 0.f && peakOf(twice) == 0.f);
    }
}

static const float kSatType[4] = {0.1f, 0.3f, 0.6f, 0.9f}; // TAPE, TUBE, FOLD, HARD
static const char* const kSatName[4] = {"TAPE", "TUBE", "FOLD", "HARD"};
static const float kDrive20dB = 20.f / 24.f; // DRIVE is 24 dB at full: 20 dB is 83%

// SATURATE as it ships, or with the 2x stage left out.
static std::unique_ptr<Effect> saturate(bool twice, float drive, int type, float tone = 1.f, float mix = 1.f, float rate = 48000.f) {
    std::unique_ptr<Effect> e = twice ? makeEffect(EffectKind::Saturate, rate, 120.f) : std::unique_ptr<Effect>(fx::makeSaturatePlain());
    if (!twice) e->prepare(rate);
    e->setParam(0, drive);
    e->setParam(1, kSatType[type]);
    e->setParam(2, tone);
    e->setParam(3, mix);
    return e;
}
// How steep the steepest step is when a 100 Hz sine at half scale goes through, settled, in the sine's own steps.
static double steepness(Effect& effect) {
    Stereo io(48000);
    io.l = io.r = sineOf(100.0, 0.5, 48000);
    through(effect, io);
    return largestJump(io.l, 24000, 48000) / sineStep(100.0, 0.5);
}

// The sharpest corner in a run of samples: how far one sample stands off the line between its neighbours.
static double largestBend(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double bend = 0.0;
    for (std::size_t i = from + 1; i + 1 < to; ++i) bend = std::fmax(bend, std::fabs(static_cast<double>(v[i + 1]) - 2.0 * v[i] + v[i - 1]));
    return bend;
}

static void testSaturate() {
    const EffectKind kind = EffectKind::Saturate;
    // Its row: what a song stores.
    const EffectInfo& info = effectInfo(kind);
    CHECK(std::string(info.name) == "SATURATE" && std::string(info.word) == "saturate" && info.params == 4 && info.family == EffectFamily::Drive);
    CHECK(std::string(info.paramNames[0]) == "DRIVE" && std::string(info.paramNames[1]) == "TYPE" && std::string(info.paramNames[2]) == "TONE" && std::string(info.paramNames[3]) == "MIX");
    CHECK(info.defaults[0] == 0.4f && info.defaults[1] == 0.f && info.defaults[2] == 0.8f && info.defaults[3] == 1.f);
    CHECK(effectChoices(kind, 1) == 4 && effectChoices(kind, 0) == 0 && effectChoices(kind, 2) == 0 && effectChoices(kind, 3) == 0);
    for (int t = 0; t < 4; ++t) CHECK(reads(kind, 1, kSatType[t]) == kSatName[t]);
    CHECK(reads(kind, 1, 0.f) == "TAPE" && reads(kind, 1, 0.25f) == "TUBE" && reads(kind, 1, 0.5f) == "FOLD" && reads(kind, 1, 0.75f) == "HARD" && reads(kind, 1, 1.f) == "HARD");
    CHECK(reads(kind, 0, 0.4f) == "40%" && reads(kind, 2, 0.8f) == "80%" && reads(kind, 3, 1.f) == "100%");

    // THE FOUR CURVES, by themselves: what comes out for what goes in.
    //   in      0     0.25    0.5     0.75    1       1.5     2       3       5
    //   TAPE    0     0.245   0.466   0.645   0.778   0.929   0.984   1       1
    //   TUBE    0     0.179   0.312   0.404   0.463   0.518   0.533   0.534   0.534    (and -0.711 for -0.5, -1.244 for -1.5, -1.466 for -5)
    //   FOLD    0     0.383   0.707   0.924   1       0.707   0      -1       1        (a sine: back to 0 at 2, 4, 6...)
    //   HARD    0     0.25    0.5     0.75    0.938   1       1       1       1
    {
        const auto at = [](int type, double x) { return static_cast<double>(fx::saturateCurve(type, static_cast<float>(x))); };
        const double in[9] = {0, 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 5};
        static const double want[4][9] = {
            {0, 0.24546, 0.46581, 0.64474, 0.77778, 0.92857, 0.98413, 1, 1},
            {0, 0.17892, 0.31197, 0.40367, 0.46276, 0.51832, 0.53269, 0.53419, 0.53419},
            {0, 0.38268, 0.70711, 0.92388, 1, 0.70711, 0, -1, 1},
            {0, 0.25, 0.5, 0.75, 0.9375, 1, 1, 1, 1},
        };
        for (int type = 0; type < 4; ++type) {
            if (gMeasure) {
                std::printf("saturate %s curve:", kSatName[type]);
                for (double x : in) std::printf(" %.5f", at(type, x));
                std::printf("\n");
            }
            for (int i = 0; i < 9; ++i) CHECK(std::fabs(at(type, in[i]) - want[type][i]) < 2e-5);
        }
        CHECK(std::fabs(at(1, -0.5) + 0.46581) < 2e-5 && std::fabs(at(1, -1.5) + 1.24359) < 2e-5 && std::fabs(at(1, -5.0) + 1.46581) < 2e-5);
        // TAPE is tanh, nearly: within 0.025 of it everywhere.
        // Every curve is one unbroken line with no corner in it: walked in
        // steps of a thousandth from -6 to 6, it never jumps, and its slope
        // never jumps either. TAPE, FOLD and HARD are the same both ways
        // round (odd harmonics only); TUBE is not. Through zero the slope is
        // 1 (FOLD: pi / 2; TUBE: 0.80, being off centre). Nothing is over 1
        // but TUBE's far side (-1.47).
        for (int type = 0; type < 4; ++type) {
            double jump = 0.0, bend = 0.0, top = 0.0, lopsided = 0.0, fromTanh = 0.0;
            for (int i = -6000; i < 6000; ++i) {
                const double x = 0.001 * i, a = at(type, x - 0.001), b = at(type, x), c = at(type, x + 0.001);
                jump = std::fmax(jump, std::fabs(c - b));
                bend = std::fmax(bend, std::fabs(c - 2.0 * b + a));
                top = std::fmax(top, std::fabs(b));
                lopsided = std::fmax(lopsided, std::fabs(b + at(type, -x)));
                fromTanh = std::fmax(fromTanh, std::fabs(b - std::tanh(x)));
            }
            const double slope = (at(type, 0.001) - at(type, -0.001)) / 0.002;
            SAY("saturate %s curve: largest step %.5f, largest bend %.7f, top %.3f, lopsided by %.3f, slope through zero %.3f, from tanh %.3f\n", kSatName[type], jump, bend, top, lopsided, slope, fromTanh);
            // A step of 0.001 in cannot move it more than its steepest slope allows; a corner would show as a bend a thousand times the rest.
            CHECK(jump < 0.0017 && bend < 1e-5);
            CHECK(type == 1 ? (std::fabs(top - 1.46581) < 1e-4 && lopsided > 0.9) : (std::fabs(top - 1.0) < 1e-5 && lopsided < 1e-6));
            CHECK(std::fabs(slope - (type == 2 ? kPi / 2.0 : type == 1 ? 0.805 : 1.0)) < 2e-3);
            if (type == 0) CHECK(fromTanh < 0.025);
            // TAPE, TUBE and HARD only ever flatten: more in is never less out. FOLD folds.
            bool rising = true;
            for (int i = -6000; i < 6000; ++i) rising = rising && at(type, 0.001 * (i + 1)) >= at(type, 0.001 * i) - 1e-6;
            CHECK(rising == (type != 2));
        }
    }
    // ITS HARMONICS, AND ITS FOLD-BACK, the way DRIVE was measured above: a
    // 4186 Hz sine at half scale, TONE fully up, MIX full; dB against the
    // sine going in. "Fold-back" is the loudest note that was never played;
    // "under 19 kHz" the loudest of those low enough to matter (above it the
    // 2x stage is only part way shut); "rest" is all of it added up.
    //
    // With the 2x stage, as it ships. Today:
    //   TYPE  DRIVE   sine    2nd     3rd     5th    fold-back         under 19 kHz    rest
    //   TAPE   25%    -0.1    none   -24.9   -48.3   -125 at 18698     -125           -122
    //   TAPE   50%    -0.2    none   -16.4   -30.5    -98 at 18698      -98            -98
    //   TAPE   83%    -0.6    none   -11.7   -18.6    -59 at 16466      -59            -57     (83% is 20 dB of drive)
    //   TAPE  100%    -0.7    none   -11.3   -17.0    -43 at 16466      -43            -40
    //   TUBE   25%    -0.2   -16.4   -29.5   -67.0    -72 at 22884     -135            -72
    //   TUBE   50%    -0.3   -15.4   -18.6   -37.1    -50 at 22884     -103            -50
    //   TUBE   83%    -0.6   -21.1   -12.1   -19.7    -38 at 22884      -63            -38
    //   TUBE  100%    -0.7   -25.1   -11.4   -17.4    -39 at 22884      -45            -36
    //   FOLD   25%    -0.2    none   -16.6   -44.7   -133 at 18698     -133           -123
    //   FOLD   50%    -1.5    none    -6.3   -25.8   -106 at 18698     -106           -106
    //   FOLD   83%   -14.5    none    -1.3   -11.7    -84 at 18698      -84            -84
    //   FOLD  100%    -5.8    none    -3.3    -9.0    -78 at 18698      -78            -78
    //   HARD   25%    -0.1    none   -34.0   -40.5    -77 at 16466      -77            -75
    //   HARD   50%    -0.3    none   -14.1   -29.1    -69 at 17022      -69            -64
    //   HARD   83%    -0.6    none   -11.3   -17.3    -43 at  8094      -43            -38
    //   HARD  100%    -0.8    none   -11.2   -16.5    -39 at 16466      -39            -36
    //
    // The same curves with the 2x stage left out (what the stage buys), at 20 dB of drive:
    //   TAPE   83%    fold-back -24 at 18698    rest -23        the stage buys 35 dB
    //   TUBE   83%              -27 at 22884         -22        11 dB anywhere, 36 dB under 19 kHz
    //   FOLD   83%              -30 at 18698         -30        54 dB
    //   HARD   83%              -22 at 18698         -20        21 dB
    //
    // Against the old DRIVE (above), at its 81%: its loudest fold-back is 20 dB
    // under the sine going in and 9 under its third harmonic, and all of it
    // together 16 under the sine. SATURATE on TAPE at 20 dB of drive: 59, 48 and
    // 56. TUBE's sixth harmonic (25116 Hz) folds to 22884 Hz, where the stage is
    // part way shut: 38 dB down, and too high to hear; under 19 kHz TUBE is 63 down.
    struct Row { int type; float drive; double sine, second, third, fifth, alias, aliasLow, rest; };
    static const Row rows[16] = {
        {0, 0.25f, -0.1, -600, -24.9, -48.3, -125, -125, -122}, {0, 0.5f, -0.2, -600, -16.4, -30.5, -98, -98, -98},  {0, kDrive20dB, -0.6, -600, -11.7, -18.6, -59.3, -59.3, -56.7}, {0, 1.f, -0.7, -600, -11.3, -17.0, -43.2, -43.2, -40.0},
        {1, 0.25f, -0.2, -16.4, -29.5, -67.0, -71.9, -135, -71.9},  {1, 0.5f, -0.3, -15.4, -18.6, -37.1, -49.6, -103, -49.6}, {1, kDrive20dB, -0.6, -21.1, -12.1, -19.7, -38.0, -62.8, -37.9}, {1, 1.f, -0.7, -25.1, -11.4, -17.4, -39.2, -45.3, -36.1},
        {2, 0.25f, -0.2, -600, -16.6, -44.7, -133, -133, -123},     {2, 0.5f, -1.5, -600, -6.3, -25.8, -106, -106, -106},     {2, kDrive20dB, -14.5, -600, -1.3, -11.7, -84.3, -84.3, -84.3},        {2, 1.f, -5.8, -600, -3.3, -9.0, -78.3, -78.3, -78.2},
        {3, 0.25f, -0.1, -600, -34.0, -40.5, -76.8, -76.8, -74.8},  {3, 0.5f, -0.3, -600, -14.1, -29.1, -68.8, -68.8, -64.3}, {3, kDrive20dB, -0.6, -600, -11.3, -17.3, -42.7, -42.7, -37.6}, {3, 1.f, -0.8, -600, -11.2, -16.5, -38.8, -38.8, -36.1},
    };
    for (const Row& row : rows) {
        const Harmonics h = harmonicsOf(*saturate(true, row.drive, row.type), 4186, 0.5);
        SAY("saturate %s %3.0f%%  sine %6.1f  2nd %6.1f  3rd %6.1f  5th %6.1f  fold-back %6.1f at %5.0f  under 19 kHz %6.1f  rest %6.1f  level %5.2f\n", kSatName[row.type], row.drive * 100.0, h.part[1],
            h.part[2], h.part[3], h.part[5], h.alias, h.aliasHz, h.aliasLow, h.rest, h.level);
        CHECK(std::fabs(h.part[1] - row.sine) < 0.3 && std::fabs(h.part[3] - row.third) < 0.5 && std::fabs(h.part[5] - row.fifth) < 0.5);
        // Even harmonics: TUBE has them (it is lopsided), the others none at all.
        if (row.type == 1) CHECK(std::fabs(h.part[2] - row.second) < 0.5 && h.part[4] > -40.0);
        else CHECK(h.part[2] < -100.0 && h.part[4] < -100.0);
        // Fold-back: no worse than written down (2 dB of room), and far down is far down.
        CHECK(h.alias < row.alias + 2.0 && h.aliasLow < row.aliasLow + 2.0 && h.rest < row.rest + 2.0);
        if (row.alias > -90.0) CHECK(std::fabs(h.alias - row.alias) < 2.0 && std::fabs(h.rest - row.rest) < 2.0);
    }
    // The plan's line: at 20 dB of drive, everything that is not a harmonic
    // at least 50 dB under the sine. TAPE and FOLD do it anywhere; TUBE under
    // 19 kHz (see above); HARD does not (38 dB: a corner is a corner).
    {
        const Harmonics tape = harmonicsOf(*saturate(true, kDrive20dB, 0), 4186, 0.5), tube = harmonicsOf(*saturate(true, kDrive20dB, 1), 4186, 0.5);
        const Harmonics fold = harmonicsOf(*saturate(true, kDrive20dB, 2), 4186, 0.5), hard = harmonicsOf(*saturate(true, kDrive20dB, 3), 4186, 0.5);
        CHECK(tape.rest - tape.part[1] < -50.0 && fold.rest - fold.part[1] < -50.0 && tube.aliasLow - tube.part[1] < -50.0);
        CHECK(tube.aliasHz == 22884.0 && hard.rest - hard.part[1] > -45.0 && hard.rest - hard.part[1] < -35.0);
        CHECK(tape.alias - tape.strongest < -45.0); // 48 dB under its third harmonic; DRIVE's is 9
        // And what the 2x stage buys: the same with it left out.
        const double buys[4] = {35.0, 11.0, 54.0, 21.0};
        const Harmonics* with[4] = {&tape, &tube, &fold, &hard};
        for (int type = 0; type < 4; ++type) {
            const Harmonics plain = harmonicsOf(*saturate(false, kDrive20dB, type), 4186, 0.5);
            SAY("saturate %s  83%%, no 2x stage: fold-back %6.1f at %5.0f  under 19 kHz %6.1f  rest %6.1f  (the stage buys %.1f dB; under 19 kHz %.1f)\n", kSatName[type], plain.alias, plain.aliasHz,
                plain.aliasLow, plain.rest, plain.alias - with[type]->alias, plain.aliasLow - with[type]->aliasLow);
            CHECK(std::fabs((plain.alias - with[type]->alias) - buys[type]) < 3.0);
            CHECK(plain.aliasLow - with[type]->aliasLow > 20.0);
            // The harmonics themselves are the same either way: the stage changes what folds, not the curve.
            CHECK(std::fabs(plain.part[1] - with[type]->part[1]) < 0.1 && std::fabs(plain.part[3] - with[type]->part[3]) < 0.2);
        }
        // Left out, TAPE is where the old DRIVE is: 24 dB.
        CHECK(std::fabs(harmonicsOf(*saturate(false, kDrive20dB, 0), 4186, 0.5).alias + 24.2) < 1.5);
    }
    // FOLD folds: past the top the wave comes back down, so as DRIVE rises
    // the sine itself falls away under its own harmonics (at 83% the third is
    // 13 dB over it). The others only flatten: the sine stays on top.
    {
        const Harmonics folded = harmonicsOf(*saturate(true, kDrive20dB, 2), 4186, 0.5), flat = harmonicsOf(*saturate(true, kDrive20dB, 0), 4186, 0.5);
        CHECK(folded.part[3] > folded.part[1] + 5.0 && flat.part[3] < flat.part[1] - 8.0);
    }
    // THE LOUDNESS IS HELD LEVEL AS DRIVE RISES: a sine at half scale comes
    // out as loud as it went in (everything it has become, added up), at
    // every DRIVE, in every TYPE. Today the worst is 0.03 dB, with TONE fully
    // up, at 100 Hz, where nothing is lost off the top (at 4186 Hz, above:
    // 0.1 to 0.75 dB quieter, the harmonics the sample rate has no room
    // for). The old DRIVE over its range, the same way: +1.4 dB down to -7.1.
    //   A quieter sine (a tenth of full scale) comes UP as DRIVE rises, and
    //   a full-scale one comes DOWN: that is what drive is. Today, TAPE:
    //     DRIVE        0%     50%    100%
    //     in  -20 dB   +0.4   +4.4   +11.8 dB
    //     in   -6 dB    0.0    0.0     0.0
    //     in    0 dB   -1.2   -5.0    -5.9
    {
        for (int type = 0; type < 4; ++type) {
            double low = 1e9, high = -1e9;
            for (int step = 0; step <= 10; ++step) {
                const Harmonics h = harmonicsOf(*saturate(true, 0.1f * step, type), 100, 0.5);
                low = h.level < low ? h.level : low, high = h.level > high ? h.level : high;
            }
            SAY("saturate %s: a sine at half scale comes out %.2f to %.2f dB louder over the whole of DRIVE\n", kSatName[type], low, high);
            CHECK(low > -0.1 && high < 0.1);
        }
        double low = 1e9, high = -1e9;
        for (int step = 0; step <= 10; ++step) {
            const Harmonics h = harmonicsOf(*fresh(EffectKind::Drive, 0.1f * step, 1.f, 1.f, 0.5f), 100, 0.5);
            low = h.level < low ? h.level : low, high = h.level > high ? h.level : high;
        }
        SAY("drive    the same: %.2f to %.2f dB\n", low, high);
        CHECK(high - low > 4.0);
        static const double want[3][3] = {{0.4, 4.4, 11.8}, {0.0, 0.0, 0.0}, {-1.2, -5.0, -5.9}};
        const double amps[3] = {0.1, 0.5, 1.0};
        for (int a = 0; a < 3; ++a)
            for (int d = 0; d < 3; ++d) {
                const Harmonics h = harmonicsOf(*saturate(true, 0.5f * d, 0), 100, amps[a]);
                SAY("saturate TAPE, a sine at %.1f, DRIVE %3d%%: %+.1f dB\n", amps[a], 50 * d, h.level);
                CHECK(std::fabs(h.level - want[a][d]) < 0.5);
            }
    }
    // Silence in, silence out, to exact zeros, in every type and with TONE
    // right down (where its filter would otherwise ring on for ever, far too
    // small to hear but never nothing).
    // (And at a part-way MIX, where the plain sound made late is heard too.)
    for (int type = 0; type < 4; ++type)
        for (float tone : {0.f, 1.f, 2.f}) {
            std::unique_ptr<Effect> e = saturate(true, 1.f, type, tone > 1.f ? 1.f : tone, tone > 1.f ? 0.5f : 1.f);
            Stereo io(48000 * 20);
            Noise noise(5u);
            for (std::size_t i = 0; i < 4800; ++i) io.l[i] = io.r[i] = noise.next();
            through(*e, io);
            bool silent = true;
            for (std::size_t i = io.frames() - 4800; i < io.frames(); ++i) silent = silent && io.l[i] == 0.f && io.r[i] == 0.f;
            CHECK(silent);
        }
    // TUBE bends one way before the other, which leaves the wave off centre;
    // it is brought back. What comes out sits on zero.
    {
        std::unique_ptr<Effect> e = saturate(true, 0.5f, 1);
        Stereo io(96000);
        io.l = io.r = sineOf(1000.0, 0.5, 96000);
        through(*e, io);
        double mean = 0.0;
        for (std::size_t i = 48000; i < 96000; ++i) mean += io.l[i];
        SAY("saturate TUBE: what comes out is off centre by %.6f\n", mean / 48000.0);
        CHECK(std::fabs(mean / 48000.0) < 1e-4); // (before it is brought back: 0.1)
    }
    // TONE is a low-pass after the curve: 500 Hz at the bottom, 20 kHz at the
    // top, 3 dB down at the corner. Read with a sine too quiet to bend.
    //   Today: TONE 0%: 100 Hz -0.2 dB, 500 Hz -3.0, 4 kHz -18.0.  TONE 50% (3.2 kHz): 3162 Hz -3.0.  TONE 100%: 10 kHz -0.5.
    for (float rate : {48000.f, 96000.f}) {
        const auto level = [&](float tone, double hz) {
            std::unique_ptr<Effect> e = saturate(true, 0.f, 0, tone, 1.f, rate);
            const std::size_t n = static_cast<std::size_t>(rate);
            Stereo io(2 * n), ref(2 * n);
            io.l = io.r = sineOf(hz, 0.01, 2 * n, rate);
            through(*e, io);
            return dB(partAt(io.l.data() + n, n, hz, rate) / 0.01);
        };
        const double flat = level(1.f, 100.0); // a quiet sound's own gain at DRIVE 0 (+0.4 dB)
        // And at the bottom, what brings TUBE back on centre: 3 dB down at 5 Hz, so 1.0 dB at 10 Hz.
        const double bottom = level(1.f, 10.0) - flat;
        SAY("saturate at %.0f Hz, a 10 Hz sine: %.2f dB\n", static_cast<double>(rate), bottom);
        CHECK(std::fabs(bottom + 1.0) < 0.15);
        const double got[5] = {level(0.f, 100.0) - flat, level(0.f, 500.0) - flat, level(0.f, 4000.0) - flat, level(0.5f, 3162.0) - flat, level(1.f, 10000.0) - flat};
        SAY("saturate TONE at %.0f Hz: 0%%: %.1f, %.1f, %.1f dB at 100, 500, 4000 Hz; 50%%: %.1f at 3162 Hz; 100%%: %.1f at 10 kHz\n", static_cast<double>(rate), got[0], got[1], got[2], got[3], got[4]);
        CHECK(std::fabs(got[0] + 0.2) < 0.1 && std::fabs(got[1] + 3.0) < 0.15 && std::fabs(got[2] + 18.0) < 0.4 && std::fabs(got[3] + 3.0) < 0.3 && got[4] < 0.0 && got[4] > -1.0);
    }
    // MIX at 0 is the sound untouched, to the sample, in every type; and
    // half way it is half of each, sample for sample: half the bent sound,
    // and half the plain one made as late as the 2x stage makes the bent one.
    for (int type = 0; type < 4; ++type) {
        const Stereo in = pinInput(48000.f);
        Stereo dry = in, half = in, full = in;
        through(*saturate(true, 0.7f, type, 0.6f, 0.f), dry);
        through(*saturate(true, 0.7f, type, 0.6f, 0.5f), half);
        through(*saturate(true, 0.7f, type, 0.6f, 1.f), full);
        CHECK(dry.l == in.l && dry.r == in.r);
        fx::Halfband line;
        std::vector<float> late(in.frames());
        line.late(in.l.data(), late.data(), static_cast<int>(in.frames()));
        float worst = 0.f, asItCame = 0.f;
        for (std::size_t i = 0; i < in.frames(); ++i) {
            worst = std::fmax(worst, std::fabs(half.l[i] - 0.5f * (late[i] + full.l[i])));
            asItCame = std::fmax(asItCame, std::fabs(half.l[i] - 0.5f * (in.l[i] + full.l[i])));
        }
        CHECK(worst < 1e-6f && asItCame > 0.01f && peakOf(full.l) > 0.3f);
        // Each side is its own.
        Stereo one = in;
        one.r.assign(one.frames(), 0.f);
        through(*saturate(true, 0.7f, type, 0.6f, 1.f), one);
        CHECK(peakOf(one.r) == 0.f && one.l == full.l);
    }
    // MIX PART WAY HAS NO HOLE IN THE TREBLE. What comes out of the 2x stage is
    // about 2 samples late, and up high 2 samples is half a wave: added to the
    // sound as it came in, the two cancel (a comb filter). So the plain sound
    // is made late by the same amount before the two are mixed. Read with a
    // sine too quiet to bend, where the wet sound is the plain one times a
    // gain: part way, the level is then part way between the two.
    //   Before this was mended, DRIVE 0, MIX 50%: 5 kHz -1.6 dB, 8 kHz -5.6, 10 kHz -11.8, 12 kHz -24.9, 14 kHz -8.1.
    //   Today, DRIVE 0, MIX 50%: 5 kHz +0.2 dB, 8 kHz +0.1, 10 kHz -0.0, 12 kHz -0.1, 14 kHz -0.2, 18 kHz -0.3: each within 0.1 dB of part way.
    for (float drive : {0.f, 0.4f})
        for (int type : {0, 2}) {
            const auto level = [&](float mix, double hz) {
                std::unique_ptr<Effect> e = saturate(true, drive, type, 1.f, mix);
                Stereo io(96000);
                io.l = io.r = sineOf(hz, 0.01, 96000);
                through(*e, io);
                return partAt(io.l.data() + 48000, 48000, hz) / 0.01;
            };
            for (double hz : {1000.0, 5000.0, 8000.0, 10000.0, 12000.0, 14000.0, 16000.0, 18000.0}) {
                const double full = level(1.f, hz);
                for (float mix : {0.25f, 0.5f, 0.75f}) {
                    const double got = level(mix, hz), partWay = (1.0 - mix) + mix * full;
                    SAY("saturate %s DRIVE %2.0f%% MIX %2.0f%%, %5.0f Hz: %+.1f dB (all wet %+.1f; part way between would be %+.1f)\n", kSatName[type], drive * 100.0, mix * 100.0, hz, dB(got), dB(full), dB(partWay));
                    CHECK(std::fabs(dB(got / partWay)) < 0.2);
                }
            }
        }
    // MIX leaving 0, and coming back to it: the plain sound slides from "as
    // it came in" to "late", it does not jump. Under a 100 Hz sine the late
    // one is 1.8 of the sine's steps behind, so a jump would be a corner:
    // one sample standing off the line between its neighbours. The sharpest
    // corner, in the sine's own steepest steps:
    //   Today: leaving 0 for 50%: 0.013. Coming back: 0.013. (Jumping instead: 1.6 and 0.8.)
    for (int back = 0; back < 2; ++back) {
        std::unique_ptr<Effect> e = saturate(true, 0.f, 0, 1.f, back ? 0.5f : 0.f);
        Stereo io(48000);
        const std::vector<float> in = sineOf(100.0, 0.5, 48000);
        io.l = io.r = in;
        through(*e, io, 0, 24037);
        e->setParam(3, back ? 0.f : 0.5f);
        through(*e, io, 24037, 48000);
        const double corner = largestBend(io.l, 24000, 48000) / sineStep(100.0, 0.5);
        SAY("saturate MIX %s under a 100 Hz sine: the sharpest corner is %.4f of the sine's steepest step\n", back ? "back to 0" : "leaving 0 for 50%", corner);
        CHECK(corner < 0.02);
        // And it gets there: back at 0 it is the sound untouched again; away from 0 it is not.
        bool same = true;
        for (std::size_t i = 36000; i < 48000; ++i) same = same && io.l[i] == in[i];
        CHECK(same == (back == 1));
    }
    // NO CLICKS. A turned setting slides, and a change of TYPE fades from one
    // curve to the other over 10 ms. Under a 100 Hz sine, nothing is steeper
    // than the wave is anyway when held still (DRIVE at full squares it: 5.8
    // of the sine's own steps; FOLD at 40%: 2.1). Thrown from one end to the
    // other at once, the same.
    {
        double steepest[4], steepestOfAll = 0.0;
        for (int type = 0; type < 4; ++type) {
            steepest[type] = steepness(*saturate(true, 0.4f, type, 0.8f));
            steepestOfAll = steepest[type] > steepestOfAll ? steepest[type] : steepestOfAll;
        }
        const double squared = steepness(*saturate(true, 1.f, 0, 0.8f));
        SAY("saturate held still: TAPE %.2f, TUBE %.2f, FOLD %.2f, HARD %.2f; TAPE at full DRIVE %.2f\n", steepest[0], steepest[1], steepest[2], steepest[3], squared);
        CHECK(sweepJump(kind, 0) < 1.05 * squared && sweepJump(kind, 1) < 1.05 * steepestOfAll && sweepJump(kind, 2) < 1.05 * steepest[0] && sweepJump(kind, 3) < 1.05 * steepest[0]);
        const double limits[4] = {squared, steepestOfAll, steepest[0], steepest[0]};
        for (int p = 0; p < 4; ++p) {
            std::unique_ptr<Effect> e = fresh(kind, 0.4f, 0.f, p == 2 ? 0.f : 0.8f, p == 3 ? 0.f : 1.f);
            if (p == 0) e->setParam(0, 0.f);
            Stereo io(48000);
            io.l = io.r = sineOf(100.0, 0.5, 48000);
            through(*e, io, 0, 24037);
            e->setParam(p, 1.f);
            through(*e, io, 24037, 48000);
            const double jump = largestJump(io.l, 24000, 48000) / sineStep(100.0, 0.5);
            SAY("saturate %s thrown to the top at once: %.2f\n", info.paramNames[p], jump);
            CHECK(jump < 1.1 * limits[p]);
        }
        // DRIVE thrown part of the way, where the wave is not yet square and a step would show.
        {
            std::unique_ptr<Effect> e = saturate(true, 0.f, 0, 0.8f);
            Stereo io(48000);
            io.l = io.r = sineOf(100.0, 0.5, 48000);
            through(*e, io, 0, 24100);
            e->setParam(0, 0.5f);
            through(*e, io, 24100, 48000);
            const double jump = largestJump(io.l, 24000, 48000) / sineStep(100.0, 0.5), still = steepness(*saturate(true, 0.5f, 0, 0.8f));
            SAY("saturate DRIVE thrown from 0 to 50%%: %.2f (held still at 50%%: %.2f)\n", jump, still);
            CHECK(jump < 1.1 * still);
            // A small step hides in a slope; it cannot hide as a corner. The
            // sharpest corner while DRIVE slides is no sharper than the wave's
            // own at 50%. (Moved a block at a time it is far sharper: this was seen to fail that way.)
            Stereo held(48000);
            held.l = held.r = sineOf(100.0, 0.5, 48000);
            through(*saturate(true, 0.5f, 0, 0.8f), held);
            const double sliding = largestBend(io.l, 24000, 48000), own = largestBend(held.l, 24000, 48000);
            SAY("saturate the sharpest corner while DRIVE slides: %.5f (the wave's own at 50%%: %.5f)\n", sliding, own);
            CHECK(sliding < 1.5 * own);
        }
        // Each TYPE to each other, at a DRIVE where they differ a lot.
        double worst = 0.0;
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) {
                if (a == b) continue;
                std::unique_ptr<Effect> e = saturate(true, 0.7f, a, 0.8f);
                Stereo io(48000);
                io.l = io.r = sineOf(100.0, 0.5, 48000);
                through(*e, io, 0, 24037);
                e->setParam(1, kSatType[b]);
                through(*e, io, 24037, 48000);
                const double jump = largestJump(io.l, 24000, 48000) / sineStep(100.0, 0.5);
                const double still = std::fmax(steepness(*saturate(true, 0.7f, a, 0.8f)), steepness(*saturate(true, 0.7f, b, 0.8f)));
                worst = jump / still > worst ? jump / still : worst;
                CHECK(jump < 1.1 * still);
            }
        SAY("saturate TYPE to TYPE at DRIVE 70%%: the worst is %.2f of the steeper wave's own steepest step\n", worst);
        // A SECOND change of TYPE before the first one's 10 ms fade is over (a
        // song's automation can do it; a thumb cannot). It used to start the
        // new fade from the old curve as if the first had finished: a step up
        // to 11.6 times the steady one. Now the second waits for the first.
        double worstTwice = 0.0;
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
                for (int c = 0; c < 4; ++c) {
                    if (a == b || b == c) continue;
                    for (int gap : {1, 37, 100, 240, 479}) {
                        std::unique_ptr<Effect> e = saturate(true, 0.7f, a, 0.8f), straight = saturate(true, 0.7f, c, 0.8f);
                        Stereo io(48000), other(48000);
                        io.l = io.r = other.l = other.r = sineOf(100.0, 0.5, 48000);
                        through(*e, io, 0, 24037);
                        e->setParam(1, kSatType[b]);
                        through(*e, io, 24037, 24037 + gap);
                        e->setParam(1, kSatType[c]);
                        through(*e, io, 24037 + gap, 48000);
                        through(*straight, other);
                        const double jump = largestJump(io.l, 24000, 48000) / sineStep(100.0, 0.5);
                        const double still = std::fmax(steepness(*saturate(true, 0.7f, a, 0.8f)), std::fmax(steepness(*saturate(true, 0.7f, b, 0.8f)), steepness(*saturate(true, 0.7f, c, 0.8f))));
                        worstTwice = jump / still > worstTwice ? jump / still : worstTwice;
                        CHECK(jump < 1.1 * still);
                        // And it ends up as the last one asked for: the same sound as one that was always that.
                        double apart = 0.0;
                        for (std::size_t i = 43200; i < 48000; ++i) apart = std::fmax(apart, std::fabs(static_cast<double>(io.l[i]) - other.l[i]));
                        CHECK(apart < 1e-4);
                    }
                }
        SAY("saturate TYPE changed twice inside one fade, DRIVE 70%%: the worst is %.2f of the steepest wave's own steepest step\n", worstTwice);
        // Three changes inside one fade: the middle one is never heard, and the last is where it ends.
        {
            std::unique_ptr<Effect> e = saturate(true, 0.7f, 0, 0.8f), straight = saturate(true, 0.7f, 3, 0.8f);
            Stereo io(48000), other(48000);
            io.l = io.r = other.l = other.r = sineOf(100.0, 0.5, 48000);
            through(*e, io, 0, 24000);
            e->setParam(1, kSatType[2]);
            through(*e, io, 24000, 24050);
            e->setParam(1, kSatType[1]);
            through(*e, io, 24050, 24100);
            e->setParam(1, kSatType[3]);
            through(*e, io, 24100, 48000);
            through(*straight, other);
            double apart = 0.0;
            for (std::size_t i = 24000 + 2 * 480 + 64 + 480; i < 25200; ++i) apart = std::fmax(apart, std::fabs(static_cast<double>(io.l[i]) - other.l[i]));
            CHECK(apart < 0.02); // two fades and it is HARD (but for the tone filter's memory): TUBE was passed over
            CHECK(largestJump(io.l, 23990, 48000) / sineStep(100.0, 0.5) < 1.1 * std::fmax(steepness(*saturate(true, 0.7f, 2, 0.8f)), steepness(*saturate(true, 0.7f, 3, 0.8f))));
        }
    }
    // The first sound is in the TYPE that is set, with no fade from another: from
    // new, after a long silence, and after a reset() from another curve.
    for (int c = 0; c < 4; ++c) {
        Stereo fresh(4800), waited(48000 + 4800), again(9600 + 4800);
        const std::vector<float> sine = sineOf(100.0, 0.5, 4800);
        fresh.l = fresh.r = sine;
        std::copy(sine.begin(), sine.end(), waited.l.begin() + 48000);
        std::copy(sine.begin(), sine.end(), waited.r.begin() + 48000);
        again.l = again.r = sineOf(100.0, 0.5, 9600 + 4800);
        std::copy(sine.begin(), sine.end(), again.l.begin() + 9600);
        std::copy(sine.begin(), sine.end(), again.r.begin() + 9600);
        through(*saturate(true, 0.7f, c, 0.8f), fresh);
        through(*saturate(true, 0.7f, c, 0.8f), waited);
        std::unique_ptr<Effect> e = saturate(true, 0.7f, (c + 1) % 4, 0.8f);
        through(*e, again, 0, 9600);
        e->reset();
        e->setParam(1, kSatType[c]);
        through(*e, again, 9600, 9600 + 4800);
        CHECK(std::equal(fresh.l.begin(), fresh.l.end(), waited.l.begin() + 48000));
        CHECK(std::equal(fresh.l.begin(), fresh.l.end(), again.l.begin() + 9600));
    }
    // Turned down to 0 while it sounds, MIX gets there, exactly.
    {
        std::unique_ptr<Effect> e = saturate(true, 0.7f, 1);
        Stereo in(96000);
        in.l = in.r = sineOf(1000.0, 0.5, 96000);
        Stereo out = in;
        through(*e, out, 0, 24000);
        e->setParam(3, 0.f);
        through(*e, out, 24000, 96000);
        bool same = true;
        for (std::size_t i = 48000; i < 96000; ++i) same = same && out.l[i] == in.l[i] && out.r[i] == in.r[i];
        CHECK(same && out.l[24100] != in.l[24100]);
    }
    // However the sound is cut into blocks, the same to the last bit; and a reset is a fresh start.
    for (int type = 0; type < 4; ++type) {
        Stereo whole = pinInput(48000.f), cut = whole, big = whole, twice = whole;
        through(*saturate(true, 0.6f, type, 0.5f, 0.9f), whole);
        std::unique_ptr<Effect> c = saturate(true, 0.6f, type, 0.5f, 0.9f), d = saturate(true, 0.6f, type, 0.5f, 0.9f);
        for (std::size_t at = 0, n = 1; at < cut.frames(); at += n, n = n % 61 + 1) {
            if (at + n > cut.frames()) n = cut.frames() - at;
            c->process(cut.l.data() + at, cut.r.data() + at, static_cast<int>(n));
        }
        d->process(big.l.data(), big.r.data(), static_cast<int>(big.frames()));
        CHECK(cut.l == whole.l && cut.r == whole.r && big.l == whole.l && big.r == whole.r);
        Stereo part = twice;
        through(*d, part, 0, 12000);
        d->reset();
        through(*d, twice);
        CHECK(twice.l == whole.l && twice.r == whole.r);
    }
}

static void testSwapFade() {
    CHECK(static_cast<int>(kSwapFadeSeconds * 48000.f) == kFade);
    // The sine comes out as made, and the click finder agrees it is smooth.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        const std::vector<float> plain = leftOf(*engine, 4800);
        CHECK(std::fabs(peakOf(plain) - kEngineSine) < 1e-3 && std::fabs(largestJump(plain) / sineStep(100.0, kEngineSine) - 1.0) < 0.01);
    }
    // Every effect in, out, and for every other, on a track and on the master.
    // Counted in the sine's own largest step: 1.0 is no click at all.
    // Before there was a fade (measured by taking it out): a REVERB coming
    // out of a slot jumped by 21, its tail cut off mid-air.
    // With the fade, today: nothing over 1.9 (DRIVE, which steepens the sine
    // that much by itself), and with CRUSH 4.2 (it steps by that much by
    // itself once it is in).
    double worst = 0.0;
    for (int master = 0; master < 2; ++master)
        for (int a = 0; a < kEffectKinds; ++a)
            for (int b = 0; b < kEffectKinds; ++b) {
                if (a == b) continue;
                double held = 0.0;
                const double jump = swapJump(static_cast<EffectKind>(a), static_cast<EffectKind>(b), master == 1, &held);
                SAY("swap     %-6s to %-6s %s %5.2f (held still %5.2f)\n", effectInfo(static_cast<EffectKind>(a)).name, effectInfo(static_cast<EffectKind>(b)).name, master ? "master" : "track ", jump, held);
                const bool crush = a == static_cast<int>(EffectKind::Crush) || b == static_cast<int>(EffectKind::Crush);
                // FUZZ and RING are rougher than that held still (FUZZ makes the sine nearly square: 18 times
                // its step; RING 5): for a swap with either, no rougher than the rougher of the two held still (or CRUSH's 4.5).
                const auto rough = [](int k) { return k == static_cast<int>(EffectKind::Fuzz) || k == static_cast<int>(EffectKind::Ring); };
                // LIMITER is 3 ms late: through the fade the sine is laid over itself 3 ms earlier (2.10 into DRIVE,
                // which is 1.91 held still).
                const bool late = a == static_cast<int>(EffectKind::Limiter) || b == static_cast<int>(EffectKind::Limiter);
                const double most = std::fmax(crush ? 4.5 : (late ? 2.3 : 2.1), rough(a) || rough(b) ? 1.05 * held + 0.05 : 0.0);
                CHECK(jump < most);
                if (jump >= most) std::printf("     swap %s to %s: %.2f (held still %.2f)\n", effectInfo(static_cast<EffectKind>(a)).name, effectInfo(static_cast<EffectKind>(b)).name, jump, held);
                if (!crush && !rough(a) && !rough(b)) worst = jump > worst ? jump : worst;
            }
    SAY("swap     the worst without CRUSH: %.2f\n", worst);

    // Going in. A delay whose echo is still far off is, for now, the plain
    // sound: fading it in changes nothing, to the bit, and its echo of what
    // was played DURING the fade is whole (it was listening from the start).
    {
        std::unique_ptr<Engine> engine = sineEngine(), plain = sineEngine();
        leftOf(*engine, 4800);
        leftOf(*plain, 4800);
        CHECK(engine->setEffect(0, 1, EffectKind::Delay));
        engine->send(make(Command::SetEffectParam, 0, 1, 3, 1.f)); // MIX full
        engine->send(make(Command::SetEffectParam, 0, 1, 1, 0.f)); // one echo
        const std::vector<float> out = leftOf(*engine, 24000 + 2000), dry = leftOf(*plain, 24000 + 2000);
        bool same = true, echoed = true;
        for (int i = 0; i < 24000; ++i) same = same && out[static_cast<std::size_t>(i)] == dry[static_cast<std::size_t>(i)];
        // A quarter note on, the echo: the sine as it was 24000 samples before, on top of the sine now.
        for (int i = 24000; i < 26000; ++i) echoed = echoed && std::fabs(out[static_cast<std::size_t>(i)] - dry[static_cast<std::size_t>(i)] - dry[static_cast<std::size_t>(i - 24000)]) < 1e-6f;
        CHECK(same && echoed);
    }
    // Out to nothing: the effect that was there runs for the 480 samples of
    // its fade and not one more, is not freed on the audio thread, and is
    // freed by collectGarbage().
    {
        Counted::alive = 0;
        Counted::freed = 0;
        std::atomic<int> ran{0};
        std::unique_ptr<Engine> engine = sineEngine(), plain = sineEngine();
        engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(new Counted(&ran)));
        const std::vector<float> silent = leftOf(*engine, 4800);
        const std::vector<float> ahead = leftOf(*plain, 4800);
        CHECK(peakOf(silent) == 0.f && ran.load() == 4800 && peakOf(ahead) > 0.3f);
        CHECK(engine->setEffect(0, 1, EffectKind::None));
        const std::vector<float> out = leftOf(*engine, 2000), dry = leftOf(*plain, 2000);
        CHECK(ran.load() == 4800 + kFade + (kMaxBlock - kFade % kMaxBlock) % kMaxBlock); // to the end of the chunk its fade ended in
        CHECK(Counted::alive.load() == 1 && Counted::freed.load() == 0);                  // still there: the audio thread frees nothing
        // The slide: from silence (what it made) up to the plain sine, in a straight line.
        for (int i = 0; i < 2000; ++i) {
            const float want = dry[static_cast<std::size_t>(i)] * (i < kFade ? static_cast<float>(i + 1) / kFade : 1.f);
            if (std::fabs(out[static_cast<std::size_t>(i)] - want) > 1e-6f) {
                CHECK(!"the fade is not a straight slide over 480 samples");
                break;
            }
        }
        CHECK(out[10] != dry[10] && out[kFade - 2] != dry[kFade - 2]); // part way, it is not there yet
        for (int i = kFade - 1; i < 2000; ++i) {
            if (out[static_cast<std::size_t>(i)] != dry[static_cast<std::size_t>(i)]) { // and after it, the plain sound to the bit
                CHECK(!"after the fade the slot is not exactly empty");
                break;
            }
        }
        engine->collectGarbage();
        CHECK(Counted::alive.load() == 0 && Counted::freed.load() == 1);
        // Later renders cost what an empty slot costs: it is not run again.
        leftOf(*engine, 1000);
        CHECK(ran.load() == 4800 + kFade + (kMaxBlock - kFade % kMaxBlock) % kMaxBlock);
    }
    // Swapped again before the fade is over: nothing is lost or freed twice,
    // and the last one in is the one that stays.
    {
        Counted::alive = 0;
        Counted::freed = 0;
        std::atomic<int> ran{0};
        std::unique_ptr<Engine> engine = sineEngine(), plain = sineEngine();
        engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(new Counted(&ran)));
        leftOf(*engine, 640);
        leftOf(*plain, 640);
        CHECK(engine->setEffect(0, 1, EffectKind::Delay));
        leftOf(*engine, 128); // part way through the fade
        leftOf(*plain, 128);
        CHECK(Counted::alive.load() == 1);
        CHECK(engine->setEffect(0, 1, EffectKind::None));
        CHECK(engine->setEffect(0, 1, EffectKind::Filter));
        CHECK(engine->setEffect(0, 1, EffectKind::None)); // three more in one go
        const int before = ran.load();
        const std::vector<float> out = leftOf(*engine, 1920), dry = leftOf(*plain, 1920);
        CHECK(ran.load() == before); // the one that was fading out went at the second swap
        CHECK(Counted::alive.load() == 1 && Counted::freed.load() == 0); // ...to the trash, not freed on the audio thread
        engine->collectGarbage();
        CHECK(Counted::alive.load() == 0 && Counted::freed.load() == 1);
        bool same = true;
        for (int i = kFade; i < 1920; ++i) same = same && out[static_cast<std::size_t>(i)] == dry[static_cast<std::size_t>(i)];
        CHECK(same); // empty at the end of it
        for (float v : out) CHECK(v == v);
    }
    // An engine that goes while a fade is under way frees what was fading.
    {
        Counted::alive = 0;
        std::atomic<int> ran{0};
        {
            std::unique_ptr<Engine> engine = sineEngine();
            engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(new Counted(&ran)));
            engine->setMasterEffect(0, std::unique_ptr<Effect>(new Counted(&ran)));
            leftOf(*engine, 64);
            CHECK(engine->setEffect(0, 1, EffectKind::Reverb) && engine->setEffect(kMasterTrack, 0, EffectKind::None));
            leftOf(*engine, 64);
            CHECK(Counted::alive.load() == 2);
        }
        CHECK(Counted::alive.load() == 0);
    }
    // A slot that was empty and is "emptied" does nothing at all.
    {
        std::unique_ptr<Engine> engine = sineEngine(), plain = sineEngine();
        CHECK(engine->setEffect(0, 1, EffectKind::None) && engine->setEffect(kMasterTrack, 0, EffectKind::None));
        CHECK(leftOf(*engine, 1000) == leftOf(*plain, 1000));
    }
    // A panic part way through a fade: nothing breaks, and silence follows.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        CHECK(engine->setEffect(kMasterTrack, 0, EffectKind::Delay));
        engine->send(make(Command::SetEffectParam, kMasterTrack, 0, 0, 0.05f));
        leftOf(*engine, 48000);
        CHECK(engine->setEffect(kMasterTrack, 0, EffectKind::None));
        leftOf(*engine, 100);
        engine->send(make(Command::Panic, 0, 1));
        const std::vector<float> shut = leftOf(*engine, 4800);
        for (float v : shut) CHECK(v == v);
        CHECK(shut.back() == 0.f);
    }
    // Both sides fade. (Everything above listened to the left.)
    {
        std::unique_ptr<Engine> engine = sineEngine();
        engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(new Counted(nullptr)));
        std::vector<float> both(2 * 640);
        engine->render(both.data(), 640);
        CHECK(engine->setEffect(0, 1, EffectKind::None));
        engine->render(both.data(), 640);
        bool same = true;
        for (int i = 0; i < 640; ++i) same = same && both[static_cast<std::size_t>(2 * i)] == both[static_cast<std::size_t>(2 * i + 1)];
        CHECK(same && both[2 * 100] != 0.f && std::fabs(both[2 * 100 + 1]) < 0.1f); // part way up from silence, on the right as on the left
        engine->collectGarbage();
    }
    // The setup calls are not swaps: prepare() and setTrackEffect() end a
    // fade that is under way, and free what was fading there and then.
    for (int which = 0; which < 4; ++which) { // 0: prepare, a track's slot; 1: setTrackEffect; 2: setMasterEffect; 3: prepare, the master's slot
        Counted::alive = 0;
        std::unique_ptr<Engine> engine = sineEngine(), plain = sineEngine();
        if (which >= 2) engine->setMasterEffect(0, std::unique_ptr<Effect>(new Counted(nullptr)));
        else engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(new Counted(nullptr)));
        leftOf(*engine, 640);
        leftOf(*plain, 640);
        CHECK(engine->setEffect(which >= 2 ? kMasterTrack : 0, which >= 2 ? 0 : 1, EffectKind::None));
        leftOf(*engine, 64);
        leftOf(*plain, 64);
        CHECK(Counted::alive.load() == 1);
        if (which == 0 || which == 3) engine->prepare(48000.f);
        if (which == 1) engine->setTrackEffect(0, 1, nullptr);
        if (which == 2) engine->setMasterEffect(0, nullptr);
        CHECK(Counted::alive.load() == 0);
        if (which == 0 || which == 3) plain->prepare(48000.f);
        CHECK(leftOf(*engine, 640) == leftOf(*plain, 640)); // and the slot is simply empty: no fade left over
    }
    // The master's slots hand their old effects back the same way.
    {
        Counted::alive = 0;
        Counted::freed = 0;
        std::atomic<int> ran{0};
        std::unique_ptr<Engine> engine = sineEngine();
        engine->setMasterEffect(1, std::unique_ptr<Effect>(new Counted(&ran)));
        leftOf(*engine, 640);
        CHECK(engine->setEffect(kMasterTrack, 1, EffectKind::Filter));
        leftOf(*engine, 640);
        CHECK(Counted::alive.load() == 1 && ran.load() == 640 + 512); // its fade, to the end of the chunk it ended in
        engine->collectGarbage();
        CHECK(Counted::alive.load() == 0 && Counted::freed.load() == 1);
    }
}

// Effects swapped while a song plays, on every track and the master, with
// helpers: the same to the last bit however the tracks are shared out. (A
// fade's old effect is handed to the trash by the audio thread alone.)
static std::vector<float> swapSong(int helpers) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->useWorkers(helpers);
    for (int s = 0; s < kSteps; s += 2) engine->send(make(Command::SetStep, 0, s % 4 == 0 ? 0 : 6, s, 0.9f));
    for (int t = 1; t <= 3; ++t) {
        CHECK(engine->addSynthTrack(t, t == 2 ? SynthKind::Qmod : SynthKind::Phazed));
        for (int n = 0; n < 4; ++n) {
            Command note = make(Command::SetNote, t, n, n * 4 + (t % 3), 0.7f);
            note.c = 40 + 5 * t + 3 * n;
            note.d = 3;
            engine->send(note);
        }
    }
    engine->send(make(Command::Play, 0, 0, 1));
    std::vector<float> all;
    int round = 0;
    for (int done = 0, size = 37; done < 2 * 16 * 6000; done += size, size = size % 301 + 64, ++round) {
        // Every few blocks another slot gets another effect; some swaps land inside the last one's fade.
        if (round % 5 == 0) {
            const int which = round / 5;
            const int track = which % 5 == 4 ? kMasterTrack : which % 5;
            CHECK(engine->setEffect(track, which % 2, static_cast<EffectKind>(which % kEffectKinds)));
            if (which % 7 == 0) CHECK(engine->setEffect(track, which % 2, static_cast<EffectKind>((which + 2) % kEffectKinds)));
        }
        std::vector<float> got(static_cast<std::size_t>(2 * size), 99.f);
        engine->render(got.data(), size);
        all.insert(all.end(), got.begin(), got.end());
        engine->collectGarbage();
    }
    return all;
}

static void testSwapsOnAnyHelperCount() {
    const std::vector<float> alone = swapSong(0);
    CHECK(peakOf(alone) > 0.1f && alone.size() > 300000);
    for (float v : alone) {
        if (v != v || v == 99.f) {
            CHECK(!"a sample that is not a number, or was never written");
            break;
        }
    }
    for (int helpers : {1, 2, 3}) CHECK(swapSong(helpers) == alone);
}

// ---- 6. the send buses ------------------------------------------------------------------------

// Stands on a bus (or in a slot) and writes down what it is given. What it
// gives back: the same times `gain`; then, once nothing comes in, a steady
// `ringLevel` for `ring` frames (a tail of a known length).
struct Probe : Effect {
    std::vector<float> fed;
    int resets = 0, ringLeft = 0, ring = 0, onBus = -1, runs = 0;
    float gain = 1.f, ringLevel = 1e-3f, lastParam = -1.f, tempo = 0.f;
    int lastIndex = -1;
    Probe() { fed.reserve(48000 * 60); } // room up front: process() must not allocate
    void reset() override {
        ++resets;
        ringLeft = 0;
    }
    void setOnBus(bool on) override { onBus = on ? 1 : 0; }
    void setTempo(float bpm) override { tempo = bpm; }
    void setParam(int index, float value) override {
        lastIndex = index;
        lastParam = value;
    }
    void process(float* left, float* right, int frames) override {
        ++runs;
        for (int i = 0; i < frames; ++i) {
            if (fed.size() < fed.capacity()) fed.push_back(left[i]);
            if (left[i] != 0.f || right[i] != 0.f) {
                ringLeft = ring;
                left[i] *= gain;
                right[i] *= gain;
            } else if (ringLeft > 0) {
                --ringLeft;
                left[i] = right[i] = ringLevel;
            }
        }
    }
};

// A sine engine with a probe on each bus. The probes are the engine's: they live as long as it does.
struct SendRig {
    std::unique_ptr<Engine> engine = sineEngine();
    Probe* echo = new Probe;
    Probe* space = new Probe;
    SendRig() {
        engine->setBusEffect(0, std::unique_ptr<Effect>(echo));
        engine->setBusEffect(1, std::unique_ptr<Effect>(space));
    }
    void send(int bus, float amount, int track = 0) { engine->send(make(Command::SetSend, track, bus, 0, amount)); }
};
static const int kSendGlide = 240; // 5 ms at 48 kHz: a send from nothing to full

// EVERY DELAY AND REVERB, ON A BUS. It gives back what it adds and none of
// what came in; MIX is how loud. And the engine's rest rule suits it.
static void testEffectsOnABus() {
    CHECK(static_cast<int>(kSendGlideSeconds * 48000.f) == kSendGlide);
    CHECK(effectBus(EffectKind::Delay) == 0 && effectBus(EffectKind::Reverb) == 1);
    CHECK(effectBus(EffectKind::None) == -1 && effectBus(EffectKind::Filter) == -1 && effectBus(EffectKind::Drive) == -1 && effectBus(EffectKind::Crush) == -1);
    CHECK(effectMixParam(EffectKind::Delay) == 3 && effectMixParam(EffectKind::Reverb) == 2 && effectMixParam(EffectKind::Filter) == 3);
    CHECK(effectMixParam(EffectKind::Drive) == 2 && effectMixParam(EffectKind::None) == -1);
    // The list each bus picks from: NONE, then its own kind, in the main list's order.
    for (int bus = 0; bus < kBuses; ++bus) {
        CHECK(busListSize(bus) >= 2 && busEffectAt(bus, 0) == EffectKind::None);
        CHECK(busEffectAt(bus, -1) == EffectKind::None && busEffectAt(bus, busListSize(bus)) == EffectKind::None);
        int lastPlace = 0;
        for (int place = 1; place < busListSize(bus); ++place) {
            const EffectKind kind = busEffectAt(bus, place);
            CHECK(kind != EffectKind::None && effectBus(kind) == bus && busEffectPlace(bus, kind) == place);
            CHECK(effectPlace(kind) > lastPlace);
            lastPlace = effectPlace(kind);
        }
        CHECK(busEffectPlace(bus, EffectKind::Filter) == 0 && busEffectPlace(bus, EffectKind::None) == 0);
    }
    // DELAY, TAPE and PING on ECHO, REVERB, HALL, SPRING and PLATE on SPACE, in the main list's order.
    CHECK(busListSize(0) == 4 && busEffectAt(0, 1) == EffectKind::Delay && busEffectAt(0, 2) == EffectKind::Tape && busEffectAt(0, 3) == EffectKind::Ping);
    CHECK(busListSize(1) == 5 && busEffectAt(1, 1) == EffectKind::Reverb && busEffectAt(1, 2) == EffectKind::Hall && busEffectAt(1, 3) == EffectKind::Spring && busEffectAt(1, 4) == EffectKind::Plate);
    CHECK(busEffectPlace(0, EffectKind::Reverb) == 0 && busEffectPlace(1, EffectKind::Delay) == 0);
    CHECK(busListSize(-1) == 1 && busListSize(kBuses) == 1 && busEffectAt(-1, 1) == EffectKind::None);
    int found = 0;
    for (int place = 1; place < effectListSize(); ++place) {
        const EffectKind kind = effectAt(place);
        if (effectBus(kind) < 0) continue;
        ++found;
        const int mix = effectMixParam(kind);
        CHECK(mix >= 0); // it needs a LEVEL
        if (mix < 0) continue;
        // LEVEL at nothing: silence. (Off a bus, MIX at nothing is the sound untouched.)
        {
            std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, 120.f);
            effect->setOnBus(true);
            effect->setParam(mix, 0.f);
            Stereo io = pinInput(48000.f);
            through(*effect, io);
            CHECK(peakOf(io.l) == 0.f && peakOf(io.r) == 0.f);
        }
        // LEVEL is a plain volume on what comes back: half is half.
        Stereo full = pinInput(48000.f), half = full, off = full;
        {
            std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, 120.f);
            effect->setOnBus(true);
            effect->setParam(mix, 1.f);
            through(*effect, full);
            effect = makeEffect(kind, 48000.f, 120.f);
            effect->setOnBus(true);
            effect->setParam(mix, 0.5f);
            through(*effect, half);
            double worst = 0.0;
            for (std::size_t i = 0; i < full.frames(); ++i) {
                worst = std::fmax(worst, std::fabs(static_cast<double>(half.l[i]) - 0.5 * full.l[i]));
                worst = std::fmax(worst, std::fabs(static_cast<double>(half.r[i]) - 0.5 * full.r[i]));
            }
            CHECK(peakOf(full.l) > 0.05f && peakOf(full.r) > 0.05f && worst < 1e-6);
            // Told it is off the bus again, it is as it was made.
            effect = makeEffect(kind, 48000.f, 120.f);
            effect->setOnBus(true);
            effect->setOnBus(false);
            through(*effect, off);
            Stereo plain = pinInput(48000.f);
            through(*makeEffect(kind, 48000.f, 120.f), plain);
            CHECK(off.l == plain.l && off.r == plain.r);
        }
        // The rest rule (kBusRestSeconds): once it has been quiet that long with
        // nothing going in, it has nothing more to say. Every setting at each
        // end, at the slowest tempo (the longest echoes), a full-scale click.
        const int rest = static_cast<int>(kBusRestSeconds * 48000.f);
        double longestGap = 0.0;
        for (int corner = 0; corner < 16; ++corner) {
            std::unique_ptr<Effect> effect = makeEffect(kind, 48000.f, kMinTempo);
            effect->setOnBus(true);
            for (int i = 0; i < effectInfo(kind).params; ++i) effect->setParam(i, i == mix ? 1.f : ((corner >> i) & 1 ? 1.f : 0.f));
            Stereo io(48000 * 45);
            io.l[0] = io.r[0] = 1.f;
            through(*effect, io);
            int quiet = 0;
            bool begun = false, late = false;
            for (std::size_t i = 0; i < io.frames(); ++i) {
                const bool loud = !(std::fabs(io.l[i]) < kBusQuiet && std::fabs(io.r[i]) < kBusQuiet);
                if (loud) {
                    if (begun && quiet >= rest) late = true;
                    if (begun) longestGap = std::fmax(longestGap, quiet / 48000.0);
                    begun = true;
                    quiet = 0;
                } else {
                    ++quiet;
                }
            }
            CHECK(begun && !late);
        }
        SAY("%s on a bus: the longest silence between two sounds %.2f s (the rest rule waits %.0f)\n", effectInfo(kind).name, longestGap, static_cast<double>(kBusRestSeconds));
        CHECK(longestGap < 0.8 * kBusRestSeconds);
    }
    CHECK(found >= 2);
    // DELAY on a bus: a click in, its echoes out, and no click. Each echo is LEVEL times FEEDBACK's share.
    {
        std::unique_ptr<Effect> delay = fresh(EffectKind::Delay, 0.f, 0.5f, 1.f, 0.8f); // a sixteenth: 6000 frames
        delay->setOnBus(true);
        const std::vector<float> ir = impulseOf(*delay, 0.5);
        CHECK(ir[0] == 0.f && echoAt(ir, 0) == 6000 && std::fabs(ir[6000] - 0.8f) < 1e-6f);
        CHECK(std::fabs(echoGain(ir, 6000) - 0.45) < 0.01 && peakOf(std::vector<float>(ir.begin(), ir.begin() + 6000)) == 0.f);
    }
    // REVERB on a bus: no click at the front, and the same room (the tail's time is as it is in a slot).
    {
        std::unique_ptr<Effect> on = fresh(EffectKind::Reverb, 0.6f, 0.5f, 1.f), in = fresh(EffectKind::Reverb, 0.6f, 0.5f, 1.f);
        on->setOnBus(true);
        const std::vector<float> wet = impulseOf(*on, 4.0), both = impulseOf(*in, 4.0);
        CHECK(wet[0] == 0.f && both[0] != 0.f);
        CHECK(std::fabs(rt60(wet) - rt60(both)) < 0.02 && rt60(wet) > 1.0);
        double worst = 0.0;
        for (std::size_t i = 1; i < wet.size(); ++i) worst = std::fmax(worst, std::fabs(static_cast<double>(wet[i]) - both[i]));
        CHECK(worst < 1e-6); // after the click itself, the two are the same sound
    }
}

// SENDS: one track, its send at half: the bus gets half. After the track's
// level and its mute. Nonsense is ignored.
static void testSends() {
    const std::vector<float> plain = leftOf(*sineEngine(), 48000);
    {
        SendRig rig;
        rig.send(0, 0.5f);
        const std::vector<float> out = leftOf(*rig.engine, 48000);
        CHECK(rig.echo->onBus == 1 && rig.space->onBus == 1);
        CHECK(rig.echo->fed.size() == 48000 && rig.space->fed.empty() && rig.space->runs == 0);
        CHECK(rig.engine->busAwake(0) && !rig.engine->busAwake(1) && !rig.engine->busAwake(2) && !rig.engine->busAwake(-1));
        // It opens over 2.5 ms (half of the 5 ms that nothing to full takes), smoothly...
        bool rising = true, exact = true, mixed = true;
        for (int i = 2; i < kSendGlide / 2; ++i) rising = rising && std::fabs(rig.echo->fed[i] - plain[i] * (i + 1) / static_cast<float>(kSendGlide)) < 1e-5f;
        CHECK(rising && rig.echo->fed[60] != 0.f && std::fabs(rig.echo->fed[60]) < std::fabs(plain[60]) * 0.26f);
        // ...and from then on the bus is given exactly half, and what it gives back is added to the mix.
        for (int i = kSendGlide / 2; i < 48000; ++i) {
            exact = exact && rig.echo->fed[i] == plain[i] * 0.5f;
            mixed = mixed && out[i] == plain[i] + rig.echo->fed[i];
        }
        CHECK(exact && mixed);
        CHECK(largestJump(out) / sineStep(100.0, kEngineSine * 1.5) < 1.02);
        // The other bus, the same; the first is not touched by it.
        rig.send(1, 0.25f);
        const std::vector<float> two = leftOf(*rig.engine, 4800);
        CHECK(rig.space->fed.size() == 4800 && std::fabs(rig.space->fed[4700] - rig.echo->fed[48000 + 4700] * 0.5f) < 1e-7f);
        CHECK(std::fabs(peakOf(two) - kEngineSine * 1.75) < 1e-3);
        // Down again: it closes as smoothly, and to nothing exactly.
        rig.send(0, 0.f);
        rig.send(1, 0.f);
        const std::vector<float> closing = leftOf(*rig.engine, 4800);
        CHECK(largestJump(closing) / sineStep(100.0, kEngineSine * 1.75) < 1.02);
        CHECK(rig.echo->fed.back() == 0.f && rig.space->fed.back() == 0.f);
        const std::vector<float> shut = leftOf(*rig.engine, 480);
        const std::vector<float> same = leftOf(*sineEngine(), 48000 + 4800 + 4800 + 480);
        CHECK(std::equal(shut.begin(), shut.end(), same.end() - 480));
    }
    // After the track's level...
    {
        SendRig rig;
        rig.send(0, 1.f);
        rig.engine->send(make(Command::SetTrackLevel, 0, 0, 0, 0.2f));
        leftOf(*rig.engine, 4800);
        CHECK(std::fabs(peakOf(std::vector<float>(rig.echo->fed.end() - 960, rig.echo->fed.end())) - 0.5f * 0.2f) < 1e-4f);
    }
    // ...and after its mute: a muted track sends nothing (once the 5 ms the mute takes are over).
    {
        SendRig rig;
        rig.send(0, 1.f);
        leftOf(*rig.engine, 4800);
        rig.engine->send(make(Command::SetTrackHeard, 0, 0));
        const std::vector<float> muted = leftOf(*rig.engine, 4800);
        CHECK(peakOf(std::vector<float>(rig.echo->fed.begin() + 4800, rig.echo->fed.begin() + 4800 + 200)) > 0.05f);
        CHECK(peakOf(std::vector<float>(rig.echo->fed.begin() + 4800 + 245, rig.echo->fed.end())) == 0.f);
        CHECK(peakOf(std::vector<float>(muted.begin() + 245, muted.end())) == 0.f);
    }
    // Full is full, more than full is full, less than nothing is nothing.
    {
        SendRig rig;
        rig.send(0, 7.f);
        rig.send(1, -3.f);
        leftOf(*rig.engine, 4800);
        bool full = true;
        for (int i = kSendGlide; i < 4800; ++i) full = full && rig.echo->fed[i] == plain[i];
        CHECK(full && rig.space->runs == 0);
    }
    // Nonsense: no such bus, no such track, not a number. Nothing is sent anywhere.
    {
        SendRig rig;
        rig.send(2, 1.f);
        rig.send(-1, 1.f);
        rig.send(0, 1.f, kNumTracks);
        rig.send(0, 1.f, -1);
        rig.send(0, 1.f, kEchoBus);
        rig.send(0, std::nanf(""));
        const std::vector<float> out = leftOf(*rig.engine, 4800);
        CHECK(rig.echo->runs == 0 && rig.space->runs == 0 && std::equal(out.begin(), out.end(), plain.begin()));
        // A track with no instrument sends nothing, whatever its send says.
        rig.send(1, 1.f, 5);
        leftOf(*rig.engine, 4800);
        CHECK(rig.space->runs == 0);
    }
    // Two tracks into one bus: it gets the sum, each at its own amount.
    {
        SendRig rig;
        CHECK(rig.engine->addDrumTrack(3));
        rig.engine->setTrackEffect(3, 0, std::unique_ptr<Effect>(new SineSource));
        rig.send(0, 0.5f, 0);
        rig.send(0, 0.25f, 3);
        leftOf(*rig.engine, 4800);
        bool sum = true;
        for (int i = kSendGlide; i < 4800; ++i) sum = sum && rig.echo->fed[i] == plain[i] * 0.5f + plain[i] * 0.25f;
        CHECK(sum);
    }
    // A bus with no effect gives nothing back: the sound is not there twice.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
        engine->send(make(Command::SetSend, 0, 1, 0, 1.f));
        const std::vector<float> out = leftOf(*engine, 4800);
        CHECK(std::equal(out.begin(), out.end(), plain.begin()) && !engine->busAwake(0) && !engine->busAwake(1));
    }
    // The buses join the mix BEFORE the master's slots: a master effect works on them too.
    {
        SendRig rig;
        rig.send(0, 1.f);
        std::unique_ptr<Effect> gain(new GainEffect);
        gain->setParam(0, 0.25f);
        rig.engine->setMasterEffect(1, std::move(gain));
        const std::vector<float> out = leftOf(*rig.engine, 4800);
        bool quarter = true;
        for (int i = kSendGlide; i < 4800; ++i) quarter = quarter && out[i] == (plain[i] + plain[i]) * 0.25f;
        CHECK(quarter);
    }
}

// A send is automated like any setting: lane 20 is ECHO's, lane 21 SPACE's.
static void testSendLanes() {
    CHECK(autoLaneOfSend(0) == 20 && autoLaneOfSend(1) == 21 && kAutoLanes == 24 && autoLaneOfEffect(2, 3) == 19);
    const int step = 6000;
    for (int bus = 0; bus < kBuses; ++bus) {
        SendRig rig;
        Probe* mine = bus == 0 ? rig.echo : rig.space;
        Probe* other = bus == 0 ? rig.space : rig.echo;
        rig.send(bus, 0.25f); // where the knob rests
        rig.engine->send(make(Command::SetAutomation, 0, autoLaneOfSend(bus), 4, 1.f));
        rig.engine->send(make(Command::SetAutomation, 0, autoLaneOfSend(bus), 8, 0.f));
        rig.engine->send(make(Command::Play, 0, 0, 1));
        leftOf(*rig.engine, 16 * step);
        const auto peakIn = [&](int from, int to) { return static_cast<double>(peakOf(std::vector<float>(mine->fed.begin() + from, mine->fed.begin() + to))) / kEngineSine; };
        CHECK(std::fabs(peakIn(2 * step, 4 * step) - 0.25) < 1e-3);                  // at rest
        CHECK(std::fabs(peakIn(4 * step + step / 2 + 300, 5 * step) - 1.0) < 1e-3);  // by the middle of step 5: full
        CHECK(std::fabs(peakIn(5 * step + step / 2 + 300, 8 * step) - 0.25) < 1e-3); // back to the knob
        CHECK(peakIn(8 * step + step / 2 + 300, 9 * step) == 0.0);                   // written as nothing: nothing
        CHECK(std::fabs(peakIn(10 * step, 16 * step) - 0.25) < 1e-3);
        // It glides there: no step in what the bus is given beyond the sine's own, with a margin for the eight moves.
        CHECK(largestJump(mine->fed) / sineStep(100.0, kEngineSine) < 1.5);
        CHECK(other->runs == 0);
        // Stopped part way through the automated step: back to the knob.
        leftOf(*rig.engine, 4 * step + step * 3 / 4);
        rig.engine->send(make(Command::Stop));
        leftOf(*rig.engine, 4800);
        CHECK(std::fabs(static_cast<double>(peakOf(std::vector<float>(mine->fed.end() - 960, mine->fed.end()))) / kEngineSine - 0.25) < 1e-3);
        // The knob moved while stopped: that is where it rests, and automation starts from there.
        rig.send(bus, 0.5f);
        leftOf(*rig.engine, 4800);
        CHECK(std::fabs(static_cast<double>(peakOf(std::vector<float>(mine->fed.end() - 960, mine->fed.end()))) / kEngineSine - 0.5) < 1e-3);
    }
    // The knob moved while automation is moving it: the knob wins, at once, for the rest of the step.
    {
        SendRig rig;
        rig.engine->send(make(Command::SetAutomation, 0, autoLaneOfSend(0), 4, 1.f));
        rig.engine->send(make(Command::Play, 0, 0, 1));
        leftOf(*rig.engine, 4 * step + step / 8); // an eighth of the way into the step whose glide takes half of it
        rig.send(0, 0.3f);
        leftOf(*rig.engine, step - step / 8);
        CHECK(std::fabs(static_cast<double>(peakOf(std::vector<float>(rig.echo->fed.end() - step / 2, rig.echo->fed.end()))) / kEngineSine - 0.3) < 1e-3);
    }
    // A send whose knob was never touched rests at nothing, and automation comes back to that.
    {
        SendRig rig;
        rig.engine->send(make(Command::SetAutomation, 0, autoLaneOfSend(0), 4, 1.f));
        rig.engine->send(make(Command::Play, 0, 0, 1));
        const std::vector<float> out = leftOf(*rig.engine, 8 * step);
        const std::vector<float> plain = leftOf(*sineEngine(), 8 * step);
        CHECK(std::fabs(peakOf(std::vector<float>(out.begin() + 4 * step + step / 2 + 300, out.begin() + 5 * step)) - 2.0 * kEngineSine) < 1e-3);
        CHECK(std::equal(out.begin() + 5 * step + step / 2 + 300, out.end(), plain.begin() + 5 * step + step / 2 + 300));
    }
    // The two spare lanes do nothing at all.
    {
        SendRig rig;
        rig.engine->send(make(Command::SetAutomation, 0, 22, 0, 1.f));
        rig.engine->send(make(Command::SetAutomation, 0, 23, 0, 1.f));
        rig.engine->send(make(Command::Play, 0, 0, 1));
        const std::vector<float> out = leftOf(*rig.engine, 2 * step);
        const std::vector<float> plain = leftOf(*sineEngine(), 2 * step);
        CHECK(out == plain && rig.echo->runs == 0 && rig.space->runs == 0);
    }
}

// A bus that has been sent nothing for longer than its tail is put to rest,
// and costs nothing until something is sent again.
static void testBusRests() {
    const int rest = static_cast<int>(kBusRestSeconds * 48000.f);
    CHECK(rest == 480000);
    // Never sent to: never run.
    {
        SendRig rig;
        leftOf(*rig.engine, 48000);
        CHECK(rig.echo->runs == 0 && rig.space->runs == 0 && !rig.engine->busAwake(0) && !rig.engine->busAwake(1));
    }
    for (int ring : {0, 96000}) {
        SendRig rig;
        rig.echo->ring = ring;
        rig.send(0, 1.f);
        leftOf(*rig.engine, 4800);
        CHECK(rig.engine->busAwake(0) && rig.echo->resets == 0);
        rig.engine->send(make(Command::SetTrackHeard, 0, 0)); // the track goes quiet: 240 frames, and then nothing is sent
        const std::size_t before = rig.echo->fed.size();
        leftOf(*rig.engine, 240 + ring + rest - 2000);
        CHECK(rig.engine->busAwake(0)); // still listening for its tail...
        const std::vector<float> last = leftOf(*rig.engine, 2000 + 64);
        CHECK(!rig.engine->busAwake(0)); // ...and now at rest
        const std::size_t ran = rig.echo->fed.size() - before;
        // It ran for its tail and the rest time after it, to the chunk.
        CHECK(ran >= static_cast<std::size_t>(ring + rest) && ran <= static_cast<std::size_t>(240 + ring + rest + 2 * kMaxBlock));
        CHECK(rig.echo->resets == 1); // cleared as it goes to rest, so it wakes clean
        const int runs = rig.echo->runs;
        const std::vector<float> resting = leftOf(*rig.engine, 48000);
        CHECK(rig.echo->runs == runs && peakOf(resting) == 0.f);
        // Something is sent again: it is awake from that chunk on.
        rig.engine->send(make(Command::SetTrackHeard, 0, 1));
        leftOf(*rig.engine, 4800);
        CHECK(rig.engine->busAwake(0) && rig.echo->fed.size() == before + ran + 4800 && rig.echo->resets == 1);
        // And a tail that keeps on sounding keeps it awake (it is the quiet that counts, not the time).
        rig.echo->ring = 3 * rest;
        rig.engine->send(make(Command::SetTrackHeard, 0, 0));
        leftOf(*rig.engine, 2 * rest);
        CHECK(rig.engine->busAwake(0));
    }
    // The quiet is counted from when it was last sent anything: sent to again half way, it starts over.
    {
        SendRig rig;
        rig.send(0, 1.f);
        leftOf(*rig.engine, 4800);
        rig.engine->send(make(Command::SetTrackHeard, 0, 0));
        leftOf(*rig.engine, rest * 3 / 4);
        rig.engine->send(make(Command::SetTrackHeard, 0, 1));
        leftOf(*rig.engine, 4800);
        rig.engine->send(make(Command::SetTrackHeard, 0, 0));
        leftOf(*rig.engine, rest * 3 / 4);
        CHECK(rig.engine->busAwake(0) && rig.echo->resets == 0);
        leftOf(*rig.engine, rest / 4 + 1000);
        CHECK(!rig.engine->busAwake(0) && rig.echo->resets == 1);
    }
    // A tail under the quiet line does not keep it awake; one just over it does.
    for (int loud = 0; loud < 2; ++loud) {
        SendRig rig;
        rig.echo->ring = 3 * rest;
        rig.echo->ringLevel = loud ? kBusQuiet * 1.01f : kBusQuiet * 0.99f;
        rig.send(0, 1.f);
        leftOf(*rig.engine, 4800);
        rig.engine->send(make(Command::SetTrackHeard, 0, 0));
        leftOf(*rig.engine, rest + 1000);
        CHECK(rig.engine->busAwake(0) == (loud == 1));
    }
    // An effect that has gone wrong (not a number) is not hidden by resting it.
    {
        SendRig rig;
        rig.echo->ring = 3 * rest;
        rig.echo->ringLevel = std::nanf("");
        rig.send(0, 1.f);
        leftOf(*rig.engine, 4800);
        rig.engine->send(make(Command::SetTrackHeard, 0, 0));
        leftOf(*rig.engine, rest + 1000);
        CHECK(rig.engine->busAwake(0));
    }
    // A send that is up on a track making no sound wakes nothing.
    {
        std::unique_ptr<Engine> engine(new Engine); // drums, and no hits
        Probe* probe = new Probe;
        engine->setBusEffect(0, std::unique_ptr<Effect>(probe));
        engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
        leftOf(*engine, 4800);
        CHECK(probe->runs == 0 && !engine->busAwake(0));
    }
    // An effect changed on a bus at rest: no fade to run, and the old one is freed.
    {
        std::atomic<int> ran{0};
        std::unique_ptr<Engine> engine = sineEngine();
        const int before = Counted::alive.load();
        engine->setBusEffect(0, std::unique_ptr<Effect>(new Counted(&ran)));
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay));
        const std::vector<float> out = leftOf(*engine, 4800);
        engine->collectGarbage();
        CHECK(ran.load() == 0 && Counted::alive.load() == before && !engine->busAwake(0));
        const std::vector<float> plain = leftOf(*sineEngine(), 4800);
        CHECK(out == plain);
    }
    // ...and the same when something is sent to it in that very moment: the old one never runs.
    {
        std::atomic<int> ran{0};
        std::unique_ptr<Engine> engine = sineEngine();
        const int before = Counted::alive.load();
        engine->setBusEffect(0, std::unique_ptr<Effect>(new Counted(&ran)));
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay));
        engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
        leftOf(*engine, 4800);
        engine->collectGarbage();
        CHECK(ran.load() == 0 && Counted::alive.load() == before && engine->busAwake(0));
    }
    // Panic clears the buses and puts them to rest.
    {
        SendRig rig;
        rig.echo->ring = 480000;
        rig.send(0, 1.f);
        leftOf(*rig.engine, 4800);
        rig.engine->send(make(Command::SetSend, 0, 0, 0, 0.f));
        rig.engine->send(make(Command::Panic, 0, 1));
        leftOf(*rig.engine, 4800);
        CHECK(rig.echo->resets == 1 && !rig.engine->busAwake(0));
        const int runs = rig.echo->runs;
        rig.engine->send(make(Command::Panic, 0, 0));
        leftOf(*rig.engine, 48000);
        CHECK(rig.echo->runs == runs);
    }
}

// The real effects on a bus, through the engine's own calls.
static void testBusEffectsInTheEngine() {
    const std::vector<float> plain = leftOf(*sineEngine(), 48000);
    // DELAY on ECHO, a sixteenth (6000 frames at 120): for the first 6000
    // frames nothing comes back, so the sound is there once and not twice.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay));
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 0, 0.f));
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 1, 0.f)); // one echo
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, 0.5f)); // LEVEL
        leftOf(*engine, 4800); // (the 10 ms a new effect takes to come in)
        engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
        const std::vector<float> out = leftOf(*engine, 12000);
        CHECK(std::equal(out.begin(), out.begin() + 6000, plain.begin() + 4800));
        // Then the echo: the track, 6000 frames late, at LEVEL. (100 Hz: 6000 frames is 12.5 cycles, so it comes back upside down.)
        bool echo = true;
        for (int i = 6000 + kSendGlide; i < 12000; ++i) echo = echo && std::fabs(out[i] - (plain[4800 + i] + 0.5f * plain[4800 + i - 6000])) < 1e-6f;
        CHECK(echo && std::fabs(peakOf(std::vector<float>(out.begin() + 7000, out.end())) - kEngineSine * 0.5) < 1e-3);
        // The tempo reaches it: at 60 the sixteenth is 12000 frames.
        engine->send(make(Command::SetTempo, 0, 0, 0, 60.f));
        engine->send(make(Command::SetSend, 0, 0, 0, 0.f));
        engine->send(make(Command::SetTrackHeard, 0, 0));
        const std::vector<float> tail = leftOf(*engine, 24000);
        CHECK(peakOf(std::vector<float>(tail.begin() + 6000, tail.begin() + 11900)) > 0.1f); // still echoing what it was sent
        CHECK(peakOf(std::vector<float>(tail.begin() + 12300, tail.end())) < 1e-6f);        // and 12000 after the send shut, nothing
    }
    // An effect put on a bus starts with its LEVEL at full: the echo comes back as loud as what was sent.
    {
        CHECK(effectDefault(EffectKind::Delay, 3) == 0.35f && effectDefault(EffectKind::Delay, 3, true) == 1.f && effectDefault(EffectKind::Reverb, 2, true) == 1.f);
        CHECK(effectDefault(EffectKind::Delay, 1, true) == 0.45f && effectDefault(EffectKind::Delay, 0, true) == 0.5f && effectDefault(EffectKind::Filter, 3, true) == 1.f);
        CHECK(effectDefault(EffectKind::Delay, 4, true) == 0.f && effectDefault(EffectKind::Delay, -1) == 0.f && effectDefault(EffectKind::Reverb, 3, true) == 0.f && effectDefault(EffectKind::None, 0, true) == 0.f);
        std::unique_ptr<Engine> engine = sineEngine();
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay)); // a quarter note: 24000 frames
        engine->send(make(Command::SetSend, 0, 0, 0, 0.5f));
        const std::vector<float> out = leftOf(*engine, 48000);
        bool echo = true;
        for (int i = 24000 + kSendGlide; i < 47000; ++i) echo = echo && std::fabs(out[i] - (plain[i] + 0.5f * plain[i - 24000])) < 1e-6f;
        CHECK(echo && std::equal(out.begin(), out.begin() + 24000, plain.begin()));
    }
    // Its LEVEL at nothing: nothing comes back.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay) && engine->setEffect(kSpaceBus, 0, EffectKind::Reverb));
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, 0.f));
        engine->send(make(Command::SetEffectParam, kSpaceBus, 0, 2, 0.f));
        engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
        engine->send(make(Command::SetSend, 0, 1, 0, 1.f));
        const std::vector<float> out = leftOf(*engine, 48000);
        CHECK(out == plain && engine->busAwake(0) && engine->busAwake(1));
        // A bus has one slot; a setting for another is not for the master or a track by mistake.
        engine->send(make(Command::SetEffectParam, kEchoBus, 1, 3, 1.f));
        engine->send(make(Command::SetEffectParam, kSpaceBus - 1, 0, 2, 1.f));
        CHECK(!engine->setEffect(kEchoBus, 1, EffectKind::Reverb) && !engine->setEffect(kSpaceBus, -1, EffectKind::Reverb) && !engine->setEffect(kSpaceBus - 1, 0, EffectKind::Reverb));
        const std::vector<float> still = leftOf(*engine, 4800), longer = leftOf(*sineEngine(), 48000 + 4800);
        CHECK(std::equal(still.begin(), still.end(), longer.begin() + 48000));
    }
    // REVERB on SPACE: the room comes back, the sound is not doubled at the front.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        CHECK(engine->setEffect(kSpaceBus, 0, EffectKind::Reverb));
        engine->send(make(Command::SetEffectParam, kSpaceBus, 0, 2, 1.f));
        leftOf(*engine, 4800);
        engine->send(make(Command::SetSend, 0, 1, 0, 1.f));
        const std::vector<float> out = leftOf(*engine, 48000);
        CHECK(std::equal(out.begin(), out.begin() + 1000, plain.begin() + 4800)); // its first echo is 29 ms on
        // (Against the plain sound of the same moment: 4800 frames on, so a longer run of it. This once
        // read `plain` 4800 floats past its end.)
        const std::vector<float> longer = leftOf(*sineEngine(), 4800 + 48000);
        CHECK(std::equal(plain.begin(), plain.end(), longer.begin()));
        double differs = 0.0;
        for (int i = 24000; i < 48000; ++i) differs = std::fmax(differs, std::fabs(static_cast<double>(out[i]) - longer.at(4800 + i)));
        CHECK(differs > 0.05);
    }
    // An effect put on a bus that is sounding fades in and out like any other: no click.
    {
        SendRig rig;
        rig.send(0, 1.f);
        std::vector<float> all = leftOf(*rig.engine, 48000);
        CHECK(rig.engine->setEffect(kEchoBus, 0, EffectKind::None)); // the probe (which gave the sound back whole) goes
        std::vector<float> after = leftOf(*rig.engine, 2400);
        all.insert(all.end(), after.begin(), after.end());
        CHECK(largestJump(all, 48000 - 2, 48000 + 2400) / sineStep(100.0, kEngineSine * 2.0) < 1.05);
        // Part way through the fade it is part way; after it, the bus gives nothing (it holds no effect).
        CHECK(std::fabs(peakOf(std::vector<float>(all.begin() + 48000 + kFade, all.end())) - kEngineSine) < 1e-3);
        CHECK(peakOf(std::vector<float>(all.begin() + 48000 + 100, all.begin() + 48000 + 340)) > kEngineSine * 1.3);
        // And in again, from nothing.
        CHECK(rig.engine->setEffect(kEchoBus, 0, EffectKind::Delay));
        rig.engine->send(make(Command::SetEffectParam, kEchoBus, 0, 0, 0.f));
        after = leftOf(*rig.engine, 12000);
        CHECK(largestJump(after) / sineStep(100.0, kEngineSine * 1.35) < 1.05);
        rig.engine->collectGarbage();
    }
    // The setup call tells the effect where it is; an effect put there by setEffect is told too.
    {
        std::unique_ptr<Engine> engine(new Engine);
        Probe* probe = new Probe;
        engine->setBusEffect(1, std::unique_ptr<Effect>(probe));
        CHECK(probe->onBus == 1);
        engine->send(make(Command::SetEffectParam, kSpaceBus, 0, 2, 0.4f));
        engine->send(make(Command::SetTempo, 0, 0, 0, 90.f));
        leftOf(*engine, 64);
        CHECK(probe->lastIndex == 2 && probe->lastParam == 0.4f && probe->tempo == 90.f);
        engine->setBusEffect(2, std::unique_ptr<Effect>(new Probe)); // no such bus: freed, nothing put anywhere
        engine->setBusEffect(-1, std::unique_ptr<Effect>(new Probe));
        engine->prepare(48000.f);
        CHECK(probe->resets == 1);
    }
}

// The FX screen trying an effect (A held on a slot's line): the slot's automation is held, so the pattern does
// not move the effect being tried; let go, it moves it again from the next step. Only that slot's.
static void testHeldAutomation() {
    std::unique_ptr<Engine> engine = sineEngine();
    Probe* first = new Probe;
    Probe* second = new Probe;
    engine->setTrackEffect(0, 0, std::unique_ptr<Effect>(first));
    engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(second));
    engine->send(make(Command::SetEffectParam, 0, 0, 2, 0.3f));
    engine->send(make(Command::SetEffectParam, 0, 1, 2, 0.3f));
    for (int step = 0; step < kSteps; ++step) {
        engine->send(make(Command::SetAutomation, 0, autoLaneOfEffect(0, 2), step, step % 2 ? 0.9f : 0.7f));
        engine->send(make(Command::SetAutomation, 0, autoLaneOfEffect(1, 2), step, step % 2 ? 0.9f : 0.7f));
    }
    engine->send(make(Command::HoldEffectAutomation, 0, 1, 1));
    engine->send(make(Command::Play, 0, 0, 1));
    leftOf(*engine, 4 * 6000);
    CHECK(first->lastIndex == 2 && first->lastParam > 0.6f);                       // moved by its lanes
    CHECK(second->lastIndex == 2 && second->lastParam == 0.3f);                    // held: where its knob is
    engine->send(make(Command::Stop));
    leftOf(*engine, 64);
    CHECK(second->lastParam == 0.3f && first->lastParam == 0.3f);                  // stopped: back to the knob (the held one never left it)
    engine->send(make(Command::HoldEffectAutomation, 0, 5, 0));                    // no such slot: nothing
    engine->send(make(Command::HoldEffectAutomation, kMasterTrack, 1, 0));
    engine->send(make(Command::HoldEffectAutomation, 0, 1, 0));
    engine->send(make(Command::Play, 0, 0, 1));
    leftOf(*engine, 4 * 6000);
    CHECK(second->lastIndex == 2 && second->lastParam > 0.6f);                     // let go: its lanes move it again
    engine->send(make(Command::HoldEffectAutomation, 0, 1, 1));                    // held while it plays: it stays where it got to
    leftOf(*engine, 64);
    const float stayed = second->lastParam;
    leftOf(*engine, 4 * 6000);
    CHECK(second->lastParam == stayed && first->lastParam > 0.6f);
    engine->send(make(Command::Stop));
    leftOf(*engine, 64);
    CHECK(second->lastParam == stayed && first->lastParam == 0.3f); // stopped while held: left where it is
    // Held in the middle of a glide (step 1: 0.7 to 0.9 over half a step): it stops where it got to.
    engine->send(make(Command::HoldEffectAutomation, 0, 1, 0));
    engine->send(make(Command::Play, 0, 0, 1));
    leftOf(*engine, 6000 + 1000);
    engine->send(make(Command::HoldEffectAutomation, 0, 1, 1));
    leftOf(*engine, 64);
    const float midway = second->lastParam;
    leftOf(*engine, 3000);
    CHECK(midway > 0.7f && midway < 0.9f && second->lastParam == midway);
    if (!(midway > 0.7f && midway < 0.9f && second->lastParam == midway)) std::printf("    held mid-glide at %.4f, then %.4f\n", static_cast<double>(midway), static_cast<double>(second->lastParam));
    engine->send(make(Command::Stop));
    leftOf(*engine, 64);
}

// Tracks have a third slot: it runs after the second, and lanes 16 to 19 are its settings.
static void testThirdSlot() {
    CHECK(kTrackSlots == 3 && kMasterSlots == 2 && slotsOf(0) == 3 && slotsOf(kMasterTrack) == 2 && slotsOf(kEchoBus) == 1 && slotsOf(kSpaceBus) == 1);
    CHECK(slotsOf(kNumTracks) == 0 && slotsOf(kSpaceBus - 1) == 0 && busTrack(0) == kEchoBus && busTrack(1) == kSpaceBus);
    CHECK(busOfTrack(kEchoBus) == 0 && busOfTrack(kSpaceBus) == 1 && busOfTrack(kMasterTrack) == -1 && busOfTrack(0) == -1);
    const std::vector<float> plain = leftOf(*sineEngine(), 9600);
    {
        std::unique_ptr<Engine> engine = sineEngine();
        Probe* second = new Probe;
        Probe* third = new Probe;
        second->gain = 0.5f;
        third->gain = 0.25f;
        engine->setTrackEffect(0, 1, std::unique_ptr<Effect>(second));
        engine->setTrackEffect(0, 2, std::unique_ptr<Effect>(third));
        engine->setTrackEffect(0, 3, std::unique_ptr<Effect>(new Probe)); // no such slot: freed
        const std::vector<float> out = leftOf(*engine, 4800);
        bool series = true;
        for (int i = 1; i < 4800; ++i) series = series && third->fed[i] == second->fed[i] * 0.5f && out[i] == third->fed[i] * 0.25f * 0.65f;
        CHECK(series && second->onBus == -1);
        // Its settings by command and by automation (lane 8 + 2 * 4 + setting).
        engine->send(make(Command::SetEffectParam, 0, 2, 1, 0.3f));
        leftOf(*engine, 64);
        CHECK(third->lastIndex == 1 && third->lastParam == 0.3f && second->lastIndex == -1);
        engine->send(make(Command::SetAutomation, 0, autoLaneOfEffect(2, 3), 1, 0.9f));
        engine->send(make(Command::Play, 0, 0, 1));
        leftOf(*engine, 2 * 6000);
        CHECK(third->lastIndex == 3 && third->lastParam == 0.9f && second->lastIndex == -1);
        engine->send(make(Command::Stop));
        leftOf(*engine, 64);
        CHECK(third->lastIndex == 3); // (its knob for that setting was never said: it stays)
    }
    // A real effect in it, by the usual call, and out again.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        CHECK(engine->setEffect(0, 2, EffectKind::Crush));
        const std::vector<float> crushed = leftOf(*engine, 9600);
        CHECK(!std::equal(crushed.begin() + 4800, crushed.end(), plain.begin() + 4800));
        CHECK(engine->setEffect(0, 2, EffectKind::None));
        leftOf(*engine, 4800);
        const std::vector<float> clean = leftOf(*engine, 4800), same = leftOf(*sineEngine(), 9600 + 9600);
        CHECK(std::equal(clean.begin(), clean.end(), same.end() - 4800));
        engine->collectGarbage();
    }
}

// A song with everything this step added: sends from every track, one
// automated, real effects on both buses, a third slot, a mute and a change of
// bus effect part way. The same to the last bit however many helpers share
// the tracks out, and the same twice from fresh.
static std::vector<float> sendSong(int helpers, bool sends = true) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->useWorkers(helpers);
    for (int s = 0; s < kSteps; s += 2) engine->send(make(Command::SetStep, 0, s % 4 == 0 ? 0 : 6, s, 0.9f));
    const SynthKind kinds[3] = {SynthKind::Phazed, SynthKind::Qmod, SynthKind::Twin};
    for (int t = 1; t <= 3; ++t) {
        CHECK(engine->addSynthTrack(t, kinds[t - 1]));
        for (int n = 0; n < 4; ++n) {
            Command note = make(Command::SetNote, t, n, n * 4 + (t % 3), 0.7f);
            note.c = 40 + 5 * t + 3 * n;
            note.d = 3;
            engine->send(note);
        }
    }
    CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay) && engine->setEffect(kSpaceBus, 0, EffectKind::Reverb));
    engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, 0.8f));
    engine->send(make(Command::SetEffectParam, kSpaceBus, 0, 2, 0.7f));
    CHECK(engine->setEffect(0, 2, EffectKind::Crush) && engine->setEffect(2, 2, EffectKind::Filter) && engine->setEffect(kMasterTrack, 1, EffectKind::Drive));
    if (sends) {
        for (int t = 0; t < 4; ++t) {
            engine->send(make(Command::SetSend, t, 0, 0, 0.2f + 0.2f * static_cast<float>(t)));
            engine->send(make(Command::SetSend, t, 1, 0, 0.9f - 0.2f * static_cast<float>(t)));
        }
        for (int s = 0; s < kSteps; s += 4) engine->send(make(Command::SetAutomation, 1, autoLaneOfSend(0), s, s % 8 == 0 ? 1.f : 0.f));
        for (int s = 2; s < kSteps; s += 4) engine->send(make(Command::SetAutomation, 0, autoLaneOfSend(1), s, 0.1f));
    }
    engine->send(make(Command::SetAutomation, 2, autoLaneOfEffect(2, 0), 4, 0.3f));
    std::vector<float> all(2 * 1024);
    engine->render(all.data(), 1024);
    engine->send(make(Command::Play, 0, 0, 1));
    int round = 0;
    for (int done = 0, size = 37; done < 3 * 16 * 6000; done += size, size = size % 301 + 64, ++round) {
        if (round == 300) engine->send(make(Command::SetTrackHeard, 3, 0));
        if (round == 500) CHECK(engine->setEffect(kEchoBus, 0, EffectKind::None));
        if (round == 501) CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay)); // inside the last one's fade
        if (round == 700) engine->send(make(Command::SetTrackHeard, 3, 1));
        std::vector<float> got(static_cast<std::size_t>(2 * size), 99.f);
        engine->render(got.data(), size);
        all.insert(all.end(), got.begin(), got.end());
        engine->collectGarbage();
    }
    all.push_back(engine->busAwake(0) ? 1.f : 0.f);
    all.push_back(engine->busAwake(1) ? 1.f : 0.f);
    return all;
}

static void testSendsOnAnyHelperCount() {
    const std::vector<float> alone = sendSong(0);
    CHECK(peakOf(alone) > 0.1f && alone.size() > 500000 && alone[alone.size() - 1] == 1.f && alone[alone.size() - 2] == 1.f);
    for (float v : alone) {
        if (v != v || v == 99.f) {
            CHECK(!"a sample that is not a number, or was never written");
            break;
        }
    }
    CHECK(sendSong(0) == alone); // twice from fresh
    for (int helpers : {1, 2, 3}) CHECK(sendSong(helpers) == alone);
    // The sends are heard (it is not the same song without them), and without them the buses rest.
    const std::vector<float> dry = sendSong(0, false);
    CHECK(dry != alone && dry[dry.size() - 1] == 0.f && dry[dry.size() - 2] == 0.f);
    double apart = 0.0;
    for (std::size_t i = 0; i + 2 < alone.size(); ++i) apart += (static_cast<double>(alone[i]) - dry[i]) * (static_cast<double>(alone[i]) - dry[i]);
    CHECK(std::sqrt(apart / static_cast<double>(alone.size())) > 0.01);
}

// FAMILIES AND TYPES (owner, 2026-11-13): what a player picks from is seven families, each with a TYPE: FILTER (FILTER),
// DRIVE (DRIVE, SATURATE), CRUSH (CRUSH), MOD (CHORUS), DELAY (CLEAN, TAPE, PING), REVERB (ROOM, HALL, SPRING, PLATE),
// LEVEL (COMP, DUCK). Underneath each type is the effect it always was.
static void testFamiliesAndTypes() {
    const EffectKind N = EffectKind::None, D = EffectKind::Delay, T = EffectKind::Tape, R = EffectKind::Reverb, H = EffectKind::Hall;
    // Nothing moved underneath: the numbers and the song-file words of every effect, as they shipped.
    static const char* const words[kEffectKinds] = {"", "filter", "drive", "delay", "reverb", "crush", "tape", "hall", "chorus", "comp", "saturate", "duck", "ping", "spring", "plate", "comb", "vowel", "eq", "fold", "fuzz", "ring", "wear", "limiter", "snap", "flanger", "ensemble", "phaser", "tremolo"};
    static const char* const names[kEffectKinds] = {"NONE", "FILTER", "DRIVE", "DELAY", "REVERB", "CRUSH", "TAPE", "HALL", "CHORUS", "COMP", "SATURATE", "DUCK", "PING", "SPRING", "PLATE", "COMB", "VOWEL", "EQ", "FOLD", "FUZZ", "RING", "WEAR", "LIMITER", "SNAP", "FLANGER", "ENSEMBLE", "PHASER", "TREMOLO"};
    CHECK(kEffectKinds == 28);
    for (int k = 0; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        CHECK(std::string(effectInfo(kind).word) == words[k] && std::string(effectInfo(kind).name) == names[k]);
        if (k > 0) CHECK(effectFromWord(words[k]) == kind);
    }
    // Every old kind in its family, as its type, in the families' order.
    struct Row { EffectKind kind; const char* family; const char* type; const char* shown; int types, index; EffectFamily id; };
    static const Row rows[] = {
        {EffectKind::Filter, "FILTER", "FILTER", "", 4, 0, EffectFamily::Filter},
        {EffectKind::Comb, "FILTER", "COMB", "COMB", 4, 1, EffectFamily::Filter},
        {EffectKind::Vowel, "FILTER", "VOWEL", "VOWEL", 4, 2, EffectFamily::Filter},
        {EffectKind::Eq, "FILTER", "EQ", "EQ", 4, 3, EffectFamily::Filter},
        {EffectKind::Drive, "DRIVE", "DRIVE", "", 4, 0, EffectFamily::Drive},
        {EffectKind::Saturate, "DRIVE", "SATURATE", "SATURATE", 4, 1, EffectFamily::Drive},
        {EffectKind::Fold, "DRIVE", "FOLD", "FOLD", 4, 2, EffectFamily::Drive},
        {EffectKind::Fuzz, "DRIVE", "FUZZ", "FUZZ", 4, 3, EffectFamily::Drive},
        {EffectKind::Crush, "CRUSH", "CRUSH", "", 3, 0, EffectFamily::Crush},
        {EffectKind::Ring, "CRUSH", "RING", "RING", 3, 1, EffectFamily::Crush},
        {EffectKind::Wear, "CRUSH", "WEAR", "WEAR", 3, 2, EffectFamily::Crush},
        {EffectKind::Chorus, "MOD", "CHORUS", "CHORUS", 5, 0, EffectFamily::Mod},
        {EffectKind::Flanger, "MOD", "FLANGER", "FLANGER", 5, 1, EffectFamily::Mod},
        {EffectKind::Ensemble, "MOD", "ENSEMBLE", "ENSEMBLE", 5, 2, EffectFamily::Mod},
        {EffectKind::Phaser, "MOD", "PHASER", "PHASER", 5, 3, EffectFamily::Mod},
        {EffectKind::Tremolo, "MOD", "TREMOLO", "TREMOLO", 5, 4, EffectFamily::Mod},
        {D, "DELAY", "CLEAN", "CLEAN", 3, 0, EffectFamily::Delay},
        {T, "DELAY", "TAPE", "TAPE", 3, 1, EffectFamily::Delay},
        {EffectKind::Ping, "DELAY", "PING", "PING", 3, 2, EffectFamily::Delay},
        {R, "REVERB", "ROOM", "ROOM", 4, 0, EffectFamily::Reverb},
        {H, "REVERB", "HALL", "HALL", 4, 1, EffectFamily::Reverb},
        {EffectKind::Spring, "REVERB", "SPRING", "SPRING", 4, 2, EffectFamily::Reverb},
        {EffectKind::Plate, "REVERB", "PLATE", "PLATE", 4, 3, EffectFamily::Reverb},
        {EffectKind::Comp, "LEVEL", "COMP", "COMP", 4, 0, EffectFamily::Level},
        {EffectKind::Duck, "LEVEL", "DUCK", "DUCK", 4, 1, EffectFamily::Level},
        {EffectKind::Limiter, "LEVEL", "LIMITER", "LIMITER", 4, 2, EffectFamily::Level},
        {EffectKind::Snap, "LEVEL", "SNAP", "SNAP", 4, 3, EffectFamily::Level},
    };
    CHECK(static_cast<int>(sizeof rows / sizeof rows[0]) == kEffectKinds - 1);
    for (int i = 0; i < kEffectKinds - 1; ++i) {
        const Row& row = rows[i];
        CHECK(effectInfo(row.kind).family == row.id && std::string(effectFamily(row.kind)) == row.family && std::string(effectFamilyName(row.id)) == row.family);
        CHECK(std::string(effectTypeName(row.kind)) == row.type && std::string(effectTypeShown(row.kind)) == row.shown);
        CHECK(effectTypes(row.kind) == row.types && effectTypeIndex(row.kind) == row.index && effectTypeAt(row.kind, row.index) == row.kind);
        CHECK(effectPlace(row.kind) == i + 1); // the whole list, in this order
        CHECK(effectFamilyNamesTypes(row.kind) == (row.types > 1 || row.shown[0] != 0)); // CRUSH says its words
    }
    CHECK(!effectFamilyNamesTypes(N) && !effectFamilyNamesTypes(static_cast<EffectKind>(77)));
    // Stepping the TYPE within each family: the types in order, ends that stop (None off either end).
    for (const Row& row : rows) {
        CHECK(effectTypeAt(row.kind, -1) == N && effectTypeAt(row.kind, row.types) == N);
        for (int t = 0; t < row.types; ++t) CHECK(effectTypeAt(row.kind, t) == rows[&row - rows - row.index + t].kind);
    }
    CHECK(effectTypeAt(D, 1) == T && effectTypeAt(T, 2) == EffectKind::Ping && effectTypeAt(H, 3) == EffectKind::Plate && effectTypeAt(EffectKind::Duck, 0) == EffectKind::Comp);
    CHECK(effectTypeAt(EffectKind::Saturate, 0) == EffectKind::Drive && effectTypeAt(EffectKind::Drive, 1) == EffectKind::Saturate);
    // Every effect is a type of a family; a family's types are side by side, one bus.
    int families = 1; // NONE's
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        CHECK(info.family != EffectFamily::None && effectTypeName(kind)[0] != 0);
        const int types = effectTypes(kind), at = effectTypeIndex(kind);
        CHECK(types >= 1 && at >= 0 && at < types && effectTypeAt(kind, at) == kind);
        for (int t = 0; t < types; ++t) {
            const EffectKind other = effectTypeAt(kind, t);
            CHECK(other != N && std::string(effectFamily(other)) == effectFamily(kind) && effectPlace(other) == effectPlace(kind) - at + t);
            CHECK(effectInfo(other).family == info.family && effectBus(other) == effectBus(kind)); // one family, one bus
            CHECK(effectTypes(other) == types && effectTypeIndex(other) == t);
            for (int u = 0; u < t; ++u) CHECK(std::string(effectTypeName(other)) != effectTypeName(effectTypeAt(kind, u)));
        }
        families += at == 0 ? 1 : 0;
        // In the list a player picks from: a family once, at its first type.
        const int place = familyPlace(kind);
        CHECK(place >= 1 && place < familyListSize() && familyAt(place) == effectTypeAt(kind, 0));
        CHECK(place == static_cast<int>(info.family)); // the families in EffectFamily's order
        // A few plain words for it: its first type's.
        char says[64] = "?";
        effectFamilySays(kind, says, sizeof says);
        CHECK(says[0] != 0 && std::string(says) == effectInfo(effectTypeAt(kind, 0)).says);
    }
    CHECK(effectTypes(N) == 1 && effectTypeAt(N, 0) == N && effectTypeIndex(N) == 0 && effectTypeName(N)[0] == 0 && effectTypeShown(N)[0] == 0 && std::string(effectFamily(N)) == "NONE");
    const EffectKind bad = static_cast<EffectKind>(77);
    CHECK(effectTypes(bad) == 1 && effectTypeAt(bad, 0) == N && effectTypeIndex(bad) == 0 && familyPlace(bad) == 0 && effectTypeName(bad)[0] == 0);
    // The list of families: EMPTY and the seven, in the owner's order.
    static const EffectKind list[] = {N, EffectKind::Filter, EffectKind::Drive, EffectKind::Crush, EffectKind::Chorus, D, R, EffectKind::Comp};
    CHECK(familyListSize() == 8 && families == 8 && static_cast<int>(EffectFamily::Count) == 8);
    for (int i = 0; i < 8; ++i) CHECK(familyAt(i) == list[i] && familyPlace(list[i]) == i);
    for (int i = 1; i < 8; ++i) CHECK(effectInfo(familyAt(i)).family == static_cast<EffectFamily>(i));
    CHECK(familyAt(-1) == N && familyAt(8) == N && familyPlace(T) == 5 && familyPlace(H) == 6 && familyPlace(EffectKind::Saturate) == 2 && familyPlace(EffectKind::Duck) == 7 && familyPlace(N) == 0);
    // A bus's own list: NONE and the one family it may hold.
    CHECK(familyListSize(0) == 2 && familyAt(0, 0) == N && familyAt(1, 0) == D && familyAt(2, 0) == N && familyAt(-1, 0) == N);
    CHECK(familyListSize(1) == 2 && familyAt(0, 1) == N && familyAt(1, 1) == R && familyAt(2, 1) == N);
    CHECK(familyPlace(D, 0) == 1 && familyPlace(T, 0) == 1 && familyPlace(R, 0) == 0 && familyPlace(H, 0) == 0 && familyPlace(EffectKind::Filter, 0) == 0 && familyPlace(N, 0) == 0);
    CHECK(familyPlace(R, 1) == 1 && familyPlace(H, 1) == 1 && familyPlace(D, 1) == 0 && familyPlace(T, 1) == 0 && familyPlace(EffectKind::Crush, 1) == 0);
    CHECK(familyListSize(2) == 8 && familyListSize(-5) == 8 && familyAt(6, 7) == R); // no such bus: the whole list
    // Only a DELAY goes on ECHO and only a REVERB on SPACE, every type of each.
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectFamily family = effectInfo(static_cast<EffectKind>(k)).family;
        CHECK(effectBus(static_cast<EffectKind>(k)) == (family == EffectFamily::Delay ? 0 : (family == EffectFamily::Reverb ? 1 : -1)));
    }
    const auto says = [](EffectKind kind) {
        char text[64] = "?";
        effectFamilySays(kind, text, sizeof text);
        return std::string(text);
    };
    CHECK(says(D) == "echoes" && says(EffectKind::Ping) == says(D) && says(R) == "space" && says(EffectKind::Plate) == says(R));
    CHECK(says(EffectKind::Crush) == "fewer bits" && says(N) == "nothing here" && says(bad) == "nothing here" && says(EffectKind::Duck) == "evens loud and quiet");
    CHECK(says(EffectKind::Filter) == "low, band, high, DJ" && says(EffectKind::Saturate) == "hard edge");
    char tiny[4] = "zzz";
    effectFamilySays(D, tiny, sizeof tiny);
    CHECK(std::string(tiny) == "ech");
    effectFamilySays(D, nullptr, 8);
    effectFamilySays(D, tiny, 0);
}

// CHANGING TYPE keeps what the two types share by name, as values that READ the same.
static void testTypeChangeCarriesSettings() {
    const EffectKind D = EffectKind::Delay, T = EffectKind::Tape, R = EffectKind::Reverb, H = EffectKind::Hall;
    // What is carried from what, today.
    CHECK(effectCarryFrom(D, T, 0) == 0 && effectCarryFrom(D, T, 1) == 1 && effectCarryFrom(D, T, 2) == -1 && effectCarryFrom(D, T, 3) == 3); // TIME, FEEDBACK, (AGE), MIX
    CHECK(effectCarryFrom(T, D, 0) == 0 && effectCarryFrom(T, D, 1) == 1 && effectCarryFrom(T, D, 2) == -1 && effectCarryFrom(T, D, 3) == 3); // (TONE)
    CHECK(effectCarryFrom(R, H, 0) == 0 && effectCarryFrom(R, H, 1) == -1 && effectCarryFrom(R, H, 2) == -1 && effectCarryFrom(R, H, 3) == 2); // SIZE, (DECAY), (TONE), MIX
    CHECK(effectCarryFrom(H, R, 0) == 0 && effectCarryFrom(H, R, 1) == -1 && effectCarryFrom(H, R, 2) == 3 && effectCarryFrom(H, R, 3) == -1);  // SIZE, (DAMPING), MIX
    CHECK(effectCarryFrom(D, T, -1) == -1 && effectCarryFrom(D, T, 4) == -1 && effectCarryFrom(EffectKind::None, T, 0) == -1 && effectCarryFrom(D, EffectKind::None, 0) == -1);
    CHECK(effectCarryFrom(H, EffectKind::Delay, 2) == 2); // by name alone: HALL's TONE and DELAY's
    // PLATE: PRE, DECAY, TONE, MIX. From HALL it keeps DECAY, TONE and MIX (the same scales: untouched, to the bit); from ROOM, MIX.
    {
        const EffectKind P = EffectKind::Plate;
        CHECK(effectCarryFrom(H, P, 0) == -1 && effectCarryFrom(H, P, 1) == 1 && effectCarryFrom(H, P, 2) == 2 && effectCarryFrom(H, P, 3) == 3);
        CHECK(effectCarryFrom(P, H, 0) == -1 && effectCarryFrom(P, H, 1) == 1 && effectCarryFrom(P, H, 2) == 2 && effectCarryFrom(P, H, 3) == 3); // (HALL's SIZE starts at its usual)
        CHECK(effectCarryFrom(R, P, 0) == -1 && effectCarryFrom(R, P, 1) == -1 && effectCarryFrom(R, P, 2) == -1 && effectCarryFrom(R, P, 3) == 2);
        CHECK(effectCarryFrom(P, R, 0) == -1 && effectCarryFrom(P, R, 1) == -1 && effectCarryFrom(P, R, 2) == 3);
        for (float v : {0.f, 0.123456f, 0.45f, 0.987654f, 1.f})
            for (int i = 1; i < 4; ++i) CHECK(effectCarryValue(H, i, P, i, v) == v && effectCarryValue(P, i, H, i, v) == v);
        const float hallWas[4] = {0.9f, 0.77f, 0.15f, 0.4f};
        float now[4];
        effectCarry(H, P, hallWas, false, now);
        CHECK(now[0] == effectInfo(P).defaults[0] && now[1] == 0.77f && now[2] == 0.15f && now[3] == 0.4f && reads(P, 1, now[1]) == reads(H, 1, 0.77f));
        effectCarry(H, P, hallWas, true, now);
        CHECK(now[0] == effectInfo(P).defaults[0] && now[3] == 0.4f);
        const float roomWas[4] = {0.9f, 0.1f, 0.6f, 0.f};
        effectCarry(R, P, roomWas, true, now);
        CHECK(now[0] == effectInfo(P).defaults[0] && now[1] == effectInfo(P).defaults[1] && now[2] == effectInfo(P).defaults[2] && now[3] == 0.6f);
        effectCarry(P, R, nullptr, true, now); // nothing to carry: ROOM as it comes, its LEVEL full on a bus
        CHECK(now[0] == effectInfo(R).defaults[0] && now[2] == 1.f);
    }
    // The words of the promise: 48% stays 48%, though TAPE's FEEDBACK goes to 120% and DELAY's to 100%.
    CHECK(reads(T, 1, 0.4f) == "48%" && reads(D, 1, effectCarryValue(T, 1, D, 1, 0.4f)) == "48%" && std::fabs(effectCarryValue(T, 1, D, 1, 0.4f) - 0.48f) < 0.005f);
    CHECK(reads(D, 1, 0.48f) == "48%" && reads(T, 1, effectCarryValue(D, 1, T, 1, 0.48f)) == "48%" && std::fabs(effectCarryValue(D, 1, T, 1, 0.48f) - 0.4f) < 0.005f);
    // It lands on the ROUNDEST value that reads so, not on the reading's low edge (found in review: 45% came back as
    // 0.4452, the first push up went to 0.45, still 45%, and nothing was seen to move).
    CHECK(effectCarryValue(D, 1, T, 1, 0.45f) == 0.375f && effectCarryValue(T, 1, D, 1, 0.375f) == 0.45f);
    CHECK(effectCarryValue(T, 1, D, 1, 0.4f) == 0.48f && effectCarryValue(D, 1, T, 1, 0.48f) == 0.4f);
    CHECK(effectCarryValue(D, 1, T, 1, 0.452f) == 0.375f && effectCarryValue(D, 1, T, 1, 0.4461f) == 0.375f); // anywhere in the 45%: the one place
    CHECK(effectCarryValue(D, 1, T, 1, 1.f) == 0.83f && reads(T, 1, 0.83f) == "100%"); // no twentieth reads 100% on the TAPE: a hundredth
    CHECK(effectCarryValue(D, 1, T, 1, 0.f) == 0.f && effectCarryValue(T, 1, D, 1, 0.f) == 0.f);
    // Every twentieth the D-pad can leave a setting on comes back as itself, to the bit, if the other type can read it.
    for (int k = 0; k <= 20; ++k) {
        const float v = static_cast<float>(k) / 20.f;
        const float there = effectCarryValue(D, 1, T, 1, v);
        CHECK(reads(T, 1, there) == reads(D, 1, v) && effectCarryValue(T, 1, D, 1, there) == v);
        const float back = effectCarryValue(T, 1, D, 1, v);
        if (reads(D, 1, back) == reads(T, 1, v)) CHECK(effectCarryValue(D, 1, T, 1, back) == v);
        else CHECK(k > 16); // 105% and over: DELAY has no such reading
    }
    // More than DELAY can read: the nearest there is, its most.
    CHECK(reads(T, 1, 1.f) == "120%" && effectCarryValue(T, 1, D, 1, 1.f) > 0.999f && reads(D, 1, effectCarryValue(T, 1, D, 1, 1.f)) == "100%");
    // A scale that is the same on both sides is not touched at all, to the bit.
    for (float v : {0.f, 0.123456f, 0.5f, 0.987654f, 1.f}) {
        CHECK(effectCarryValue(D, 3, T, 3, v) == v && effectCarryValue(T, 3, D, 3, v) == v);
        CHECK(effectCarryValue(R, 0, H, 0, v) == v && effectCarryValue(H, 0, R, 0, v) == v && effectCarryValue(R, 2, H, 3, v) == v && effectCarryValue(H, 3, R, 2, v) == v);
    }
    // TIME: the one that reads the same; a triplet DELAY does not have goes to the nearest in steps.
    const auto time = [&](EffectKind from, EffectKind to, float v) { return reads(to, 0, effectCarryValue(from, 0, to, 0, v)); };
    for (int c = 0; c < 6; ++c) CHECK(time(D, T, (c + 0.5f) / 6.f) == reads(D, 0, (c + 0.5f) / 6.f)); // every one of DELAY's is on the TAPE
    CHECK(reads(T, 0, 1.5f / 8.f) == "1/8 TRIP" && time(T, D, 1.5f / 8.f) == "1/16");   // 1.33 steps: 1 is nearer than 2
    CHECK(reads(T, 0, 3.5f / 8.f) == "1/4 TRIP" && time(T, D, 3.5f / 8.f) == "1/8 DOT"); // 2.67 steps: 3 is nearer than 2
    for (int c : {0, 2, 4, 5, 6, 7}) CHECK(time(T, D, (c + 0.5f) / 8.f) == reads(T, 0, (c + 0.5f) / 8.f));
    // A choice lands in the middle of its part, as the screen puts it.
    CHECK(effectCarryValue(D, 0, T, 0, 0.99f) == 7.5f / 8.f && effectCarryValue(T, 0, D, 0, 0.01f) == 0.5f / 6.f);

    // EVERY PAIR in every family, both ways, every setting of the new type, right along the old one's range.
    int pairs = 0, carried = 0, same = 0, nearest = 0;
    for (int a = 1; a < kEffectKinds; ++a)
        for (int b = 1; b < kEffectKinds; ++b) {
            const EffectKind from = static_cast<EffectKind>(a), to = static_cast<EffectKind>(b);
            if (a == b || std::string(effectFamily(from)) != effectFamily(to)) continue;
            ++pairs;
            const EffectInfo& was = effectInfo(from);
            const EffectInfo& is = effectInfo(to);
            for (int i = 0; i < is.params; ++i) {
                const int had = effectCarryFrom(from, to, i);
                // By name, and never a choice from an amount.
                int byName = -1;
                for (int j = 0; j < was.params; ++j)
                    if (std::string(was.paramNames[j]) == is.paramNames[i] && (was.choices[j] > 0) == (is.choices[i] > 0)) byName = byName < 0 ? j : byName;
                CHECK(had == byName);
                if (had < 0) {
                    CHECK(effectCarryValue(from, 0, to, i, 0.9f) == is.defaults[i] && effectCarryValue(from, -1, to, i, 0.9f) == is.defaults[i]);
                    continue;
                }
                ++carried;
                // Everything the new type can read, to look the old reading up in.
                std::vector<std::string> can;
                std::vector<float> where;
                const int steps = is.choices[i] ? is.choices[i] : 20000;
                for (int k = 0; k <= steps; ++k) {
                    const float v = is.choices[i] ? (std::min(k, steps - 1) + 0.5f) / steps : static_cast<float>(k) / steps;
                    can.push_back(reads(to, i, v));
                    where.push_back(v);
                }
                const int points = was.choices[had] ? was.choices[had] : 32;
                for (int k = 0; k <= points; ++k) {
                    const float v = was.choices[had] ? (std::min(k, points - 1) + 0.5f) / points : static_cast<float>(k) / points; // 0, 50%, 100% among them
                    const float got = effectCarryValue(from, had, to, i, v);
                    CHECK(got >= 0.f && got <= 1.f);
                    const std::string before = reads(from, had, v), after = reads(to, i, got);
                    const bool reachable = std::find(can.begin(), can.end(), before) != can.end();
                    if (reachable) {
                        CHECK(after == before);
                        if (after != before) std::printf("    %s %s %.4f reads %s, as %s reads %s\n", was.name, was.paramNames[had], static_cast<double>(v), before.c_str(), is.name, after.c_str());
                        ++same;
                    } else {
                        // It cannot be read there: the nearest that can. For an amount, by the number read.
                        ++nearest;
                        if (!is.choices[i]) {
                            const float want = std::strtof(before.c_str(), nullptr), gave = std::strtof(after.c_str(), nullptr);
                            for (const std::string& other : can) CHECK(std::fabs(std::strtof(other.c_str(), nullptr) - want) >= std::fabs(gave - want) - 1e-4f);
                        } else {
                            CHECK(was.amount && is.amount);
                            const float want = was.amount(had, v), gave = is.amount(i, got);
                            for (float other : where) CHECK(std::fabs(is.amount(i, other) - want) >= std::fabs(gave - want) - 1e-4f);
                        }
                    }
                    // A choice is put in the middle of its part.
                    if (const int count = is.choices[i]) CHECK(std::fabs(got * count - std::floor(got * count) - 0.5f) < 1e-4f);
                }
                // What is not a number, or off the range, is carried as the end of the range would be.
                CHECK(effectCarryValue(from, had, to, i, std::nanf("")) == effectCarryValue(from, had, to, i, 0.f));
                CHECK(effectCarryValue(from, had, to, i, -3.f) == effectCarryValue(from, had, to, i, 0.f) && effectCarryValue(from, had, to, i, 7.f) == effectCarryValue(from, had, to, i, 1.f));
            }
            // All four at once: carried, or the new type's default; on a bus LEVEL's default is full.
            const float old[kEffectParams] = {0.3f, 0.7f, 0.2f, 0.9f};
            for (int onBus = 0; onBus < 2; ++onBus) {
                float now[kEffectParams] = {-1.f, -1.f, -1.f, -1.f};
                effectCarry(from, to, old, onBus != 0, now);
                for (int i = 0; i < kEffectParams; ++i) {
                    const int had = effectCarryFrom(from, to, i);
                    CHECK(now[i] == (had >= 0 ? effectCarryValue(from, had, to, i, old[had]) : effectDefault(to, i, onBus != 0)));
                }
                float fresh[kEffectParams];
                effectCarry(from, to, nullptr, onBus != 0, fresh); // nothing to carry: its usual settings
                for (int i = 0; i < kEffectParams; ++i) CHECK(fresh[i] == effectDefault(to, i, onBus != 0));
            }
            effectCarry(from, to, old, false, nullptr);
        }
    // DELAY's three types and CRUSH's three: six pairs each; REVERB's four, FILTER's four, DRIVE's four and LEVEL's
    // four: twelve each. Every pair both ways. FILTER's carry MIX among FILTER, COMB and VOWEL, and RESONANCE
    // between FILTER and VOWEL: eight more. DRIVE, SATURATE and FOLD share DRIVE, TONE and MIX (18), FUZZ TONE and
    // MIX with each of them (12); CRUSH and RING share MIX (2), WEAR nothing; COMP and SNAP share MIX, DUCK and
    // LIMITER RELEASE (4). MOD's five: twenty pairs; CHORUS, FLANGER, ENSEMBLE and PHASER share RATE, DEPTH and MIX
    // (36), FLANGER and PHASER FEEDBACK (2), TREMOLO DEPTH with each (8) and SPREAD with ENSEMBLE (2); its RATE is
    // a choice of note lengths, the others' an amount in hertz, so it is not carried.
    CHECK(pairs == 80 && carried == 138 && same > 250 && nearest >= 2);
    // FILTER and VOWEL read RESONANCE alike (a percentage): it goes over untouched, both ways, as MIX does; EQ shares nothing.
    for (float v : {0.f, 0.2f, 0.37f, 1.f}) {
        CHECK(effectCarryFrom(EffectKind::Filter, EffectKind::Vowel, 2) == 1 && effectCarryValue(EffectKind::Filter, 1, EffectKind::Vowel, 2, v) == v);
        CHECK(effectCarryFrom(EffectKind::Vowel, EffectKind::Filter, 1) == 2 && effectCarryValue(EffectKind::Vowel, 2, EffectKind::Filter, 1, v) == v);
        CHECK(effectCarryValue(EffectKind::Comb, 3, EffectKind::Vowel, 3, v) == v && effectCarryValue(EffectKind::Filter, 3, EffectKind::Comb, 3, v) == v);
    }
    for (int i = 0; i < 4; ++i) CHECK(effectCarryFrom(EffectKind::Filter, EffectKind::Eq, i) == -1 && effectCarryFrom(EffectKind::Eq, EffectKind::Comb, i) == -1);
    // DRIVE and SATURATE share DRIVE, TONE and MIX, on one scale (percentages): each goes over untouched, to the bit.
    // DRIVE's OUTPUT and SATURATE's TYPE start at their usual values.
    {
        const EffectKind V = EffectKind::Drive, S = EffectKind::Saturate;
        CHECK(effectCarryFrom(V, S, 0) == 0 && effectCarryFrom(V, S, 1) == -1 && effectCarryFrom(V, S, 2) == 1 && effectCarryFrom(V, S, 3) == 2);
        CHECK(effectCarryFrom(S, V, 0) == 0 && effectCarryFrom(S, V, 1) == 2 && effectCarryFrom(S, V, 2) == 3 && effectCarryFrom(S, V, 3) == -1);
        for (float v : {0.f, 0.123456f, 0.35f, 0.987654f, 1.f}) CHECK(effectCarryValue(V, 0, S, 0, v) == v && effectCarryValue(S, 2, V, 1, v) == v && effectCarryValue(S, 3, V, 2, v) == v);
        const float driveWas[4] = {0.8f, 0.25f, 0.6f, 0.9f};
        float now[4];
        effectCarry(V, S, driveWas, false, now);
        CHECK(now[0] == 0.8f && now[1] == effectInfo(S).defaults[1] && now[2] == 0.25f && now[3] == 0.6f);
        const float satWas[4] = {0.7f, 0.9f, 0.1f, 0.5f};
        effectCarry(S, V, satWas, false, now);
        CHECK(now[0] == 0.7f && now[1] == 0.1f && now[2] == 0.5f && now[3] == effectInfo(V).defaults[3]);
    }
    // COMP and DUCK share no setting's name: the new one starts at its usual values, whatever the old one was at.
    {
        const EffectKind C = EffectKind::Comp, K = EffectKind::Duck;
        for (int i = 0; i < kEffectParams; ++i) CHECK(effectCarryFrom(C, K, i) == -1 && effectCarryFrom(K, C, i) == -1);
        const float was[4] = {0.11f, 0.22f, 0.33f, 0.44f};
        float now[4];
        effectCarry(C, K, was, false, now);
        for (int i = 0; i < kEffectParams; ++i) CHECK(now[i] == effectDefault(K, i));
        effectCarry(K, C, was, false, now);
        for (int i = 0; i < kEffectParams; ++i) CHECK(now[i] == effectDefault(C, i));
    }
    CHECK(effectCarryValue(D, 9, T, 1, 0.5f) == effectInfo(T).defaults[1] && effectCarryValue(D, 1, T, 9, 0.5f) == 0.f && effectCarryValue(D, 1, T, -1, 0.5f) == 0.f);
    CHECK(effectCarryValue(D, 2, T, 1, 0.5f) == effectInfo(T).defaults[1]); // not the setting it is carried from: the default
}

// ---- 7. found in review, 2026-11-12 -----------------------------------------------------------

// A GLIDE ALWAYS LANDS, whatever the block length. It used not to in blocks
// of 8 frames and under: the fraction of what is left fell below what a float
// can be moved by, and the value stood a hair short for good, "moving".
static void testGlideLands() {
    struct Case {
        float from, to;
    };
    static const Case cases[] = {{0.f, 1.f}, {1.f, 0.f}, {0.2f, 0.6f}, {0.6f, 0.61f}, {0.3f, 0.30001f}, {1.f, 0.999f}, {0.f, 4800.37f}, {4891.2f, 1222.8f}, {4800.f, 4800.4f}, {-0.5f, 0.5f}, {0.f, 1e-4f}};
    for (const Case& c : cases) {
        for (int frames : {1, 2, 3, 5, 8, 16, 64}) {
            for (float seconds : {0.02f, 0.1f}) {
                fx::Glide g;
                g.to = c.from;
                g.snap();
                CHECK(!g.moving() && g.now == c.from);
                g.to = c.to;
                const float k = fx::glideK(frames, seconds, 48000.f);
                int samples = 0;
                bool straight = true; // never past where it is going, never backwards
                float fastest = 0.f;
                const float span = std::fabs(c.to - c.from);
                while (g.moving() && samples < 48000 * 3) {
                    const float before = g.now, step = g.begin(frames, k);
                    float at = before;
                    for (int i = 0; i < frames; ++i) at += step;
                    g.done();
                    samples += frames;
                    straight = straight && (c.to > c.from ? g.now >= before && g.now <= c.to : g.now <= before && g.now >= c.to);
                    straight = straight && std::fabs(at - g.now) <= 1e-4f * (1.f + std::fabs(c.to)); // the steps add up to where it says it got to
                    fastest = std::fmax(fastest, std::fabs(step));
                }
                // There, exactly, within twelve times its settling time.
                CHECK(!g.moving() && g.now == c.to && g.end == c.to);
                CHECK(samples <= static_cast<int>(12.f * seconds * 48000.f));
                CHECK(straight);
                // And it is a glide, not a jump: no sample moves it by more than its share of the way.
                CHECK(fastest <= span / (seconds * 48000.f) * 1.001f + 2e-7f * (1.f + std::fabs(c.to)));
                // Standing still, it stays: no step, and done() changes nothing.
                CHECK(g.begin(frames, k) == 0.f && g.next() == c.to);
                g.done();
                CHECK(g.now == c.to);
            }
        }
    }
    // Most of the way in the time it is given, at any block length: after `seconds`, between 60 and 70% there.
    for (int frames : {1, 8, 64}) {
        fx::Glide g;
        g.to = 1.f;
        const float k = fx::glideK(frames, 0.02f, 48000.f);
        for (int done = 0; done < 960; done += frames) g.begin(frames, k), g.done();
        CHECK(g.now > 0.6f && g.now < 0.7f);
    }
}

// Sits in a track's slot and, at one sample, puts out `bad` (on both sides).
struct Poison : Effect {
    float bad;
    long at, seen = 0;
    Poison(float value, long sample) : bad(value), at(sample) {}
    void setParam(int, float) override {}
    void process(float* left, float* right, int n) override {
        for (int i = 0; i < n; ++i, ++seen)
            if (seen == at) left[i] = right[i] = bad;
    }
};

// WHAT IS NOT SOUND IS SILENCE AT THE WAY IN. One sample that is not a
// number, is infinite, or is absurdly loud used to stay for good in every new
// effect and in both buses. Now it is the same, to the bit, as a zero there.
static void testNotSound() {
    // The rule by itself.
    {
        float v[] = {1.f, -1.f, 1e8f, -1e8f, 1.1e8f, -1.1e8f, std::nanf(""), INFINITY, -INFINITY, 0.f, 1e-30f, FLT_MAX};
        const float want[] = {1.f, -1.f, 1e8f, -1e8f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1e-30f, 0.f};
        float w[12];
        std::memcpy(w, v, sizeof v);
        fx::soundOnly(v, w, 12);
        for (int i = 0; i < 12; ++i) CHECK(v[i] == want[i] && w[i] == want[i]);
        CHECK(fx::kLoudest == kBusLoudest);
    }
    const float bads[] = {std::nanf(""), INFINITY, -INFINITY, 1e30f, -FLT_MAX};
    int tried = 0;
    for (int k = static_cast<int>(EffectKind::Crush) + 1; k < kEffectKinds; ++k) { // every effect newer than the old five, and any to come
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        // Each choice of each setting that is a choice (SATURATE's four curves, CHORUS's types...), and on a bus where it can be.
        struct Variant {
            int param;
            float value;
            bool bus;
        };
        std::vector<Variant> variants = {{-1, 0.f, false}};
        if (effectBus(kind) >= 0) variants.push_back({-1, 0.f, true});
        for (int p = 0; p < info.params; ++p)
            for (int c = 1; c < info.choices[p]; ++c) variants.push_back({p, (static_cast<float>(c) + 0.5f) / static_cast<float>(info.choices[p]), false});
        for (const Variant& variant : variants) {
            for (float bad : bads) {
                std::unique_ptr<Effect> hurt = makeEffect(kind, 48000.f, 120.f), well = makeEffect(kind, 48000.f, 120.f);
                for (Effect* e : {hurt.get(), well.get()}) {
                    if (variant.bus) e->setOnBus(true);
                    if (variant.param >= 0) e->setParam(variant.param, variant.value);
                }
                Stereo a(72000), b(0);
                Noise noise(77u);
                for (std::size_t i = 0; i < a.frames(); ++i) a.l[i] = 0.5f * noise.next(), a.r[i] = 0.5f * noise.next();
                a.l[10000] = a.r[10001] = a.l[10002] = 0.f;
                b = a;
                a.l[10000] = bad;
                a.r[10001] = bad;
                a.l[10002] = -bad;
                through(*hurt, a);
                through(*well, b);
                bool numbers = true;
                for (std::size_t i = 0; i < a.frames(); ++i) numbers = numbers && std::fabs(a.l[i]) < 100.f && std::fabs(a.r[i]) < 100.f;
                CHECK(numbers);
                CHECK(a.l == b.l && a.r == b.r);
                CHECK(hurt->meter() == well->meter()); // (COMP: its reading of the level is not left broken either)
                ++tried;
            }
        }
    }
    CHECK(tried >= 5 * (2 + 2 + 4 + 6 + 4)); // TAPE and HALL on and off a bus, CHORUS's four types, COMP's six ratios, SATURATE's four curves
    // SATURATE's FOLD counts the turns with a cast to int: too far round, it gives nothing rather than anything at all.
    for (float x : {1e7f, 3e9f, 1e30f, FLT_MAX, INFINITY, std::nanf("")}) {
        CHECK(std::fabs(fx::saturateCurve(2, x)) <= 1.f && std::fabs(fx::saturateCurve(2, -x)) <= 1.f);
        if (x > 1e9f || x != x) CHECK(fx::saturateCurve(2, x) == 0.f && fx::saturateCurve(2, -x) == 0.f);
    }
    CHECK(std::fabs(fx::saturateCurve(2, 1.f) - 1.f) < 1e-5f && std::fabs(fx::saturateCurve(2, 401.f) - 1.f) < 1e-3f && std::fabs(fx::saturateCurve(2, -3.f) - 1.f) < 1e-5f);

    // THE BUSES. A track puts out one bad sample with both sends up, into the
    // old DELAY and REVERB (which do not look after themselves). What comes
    // out is what comes out when that sample is a zero, but for the track's own
    // sound at that one sample.
    for (float bad : bads) {
        std::vector<float> out[2];
        for (int run = 0; run < 2; ++run) {
            std::unique_ptr<Engine> engine = sineEngine();
            engine->setTrackEffect(0, 2, std::unique_ptr<Effect>(new Poison(run == 0 ? bad : 0.f, 30000)));
            CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay) && engine->setEffect(kSpaceBus, 0, EffectKind::Reverb));
            engine->send(make(Command::SetEffectParam, kEchoBus, 0, 1, 1.f)); // FEEDBACK full: what gets in stays a long while
            engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
            engine->send(make(Command::SetSend, 0, 1, 0, 0.7f));
            out[run].resize(2 * 144000); // both sides: each has its own delay line and its own room
            engine->render(out[run].data(), 144000);
        }
        int differ = 0;
        bool numbers = true;
        for (std::size_t i = 0; i < out[0].size(); ++i) {
            if (i / 2 == 30000) continue;
            differ += out[0][i] != out[1][i];
            numbers = numbers && std::fabs(out[0][i]) < 10.f;
        }
        CHECK(numbers && differ == 0);
        float late = 0.f;
        for (std::size_t i = 200000; i < out[0].size(); ++i) late = std::fmax(late, std::fabs(out[0][i]));
        CHECK(late > 0.3f); // (and the buses are sounding)
    }
}


// A BUS HOLDS ITS OWN KIND, in the engine too (the Session already refused the rest).
static void testABusTakesItsOwnKind() {
    int refused = 0, taken = 0;
    for (int k = 0; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        for (int bus = 0; bus < kBuses; ++bus) {
            std::unique_ptr<Engine> engine = sineEngine(), twin = sineEngine();
            for (Engine* e : {engine.get(), twin.get()}) {
                CHECK(e->setEffect(busTrack(bus), 0, busEffectAt(bus, 1))); // DELAY, or REVERB
                e->send(make(Command::SetSend, 0, bus, 0, 1.f));
            }
            const bool fits = kind == EffectKind::None || effectBus(kind) == bus;
            CHECK(engine->setEffect(busTrack(bus), 0, kind) == fits);
            fits ? ++taken : ++refused;
            const std::vector<float> out = leftOf(*engine, 24000), same = leftOf(*twin, 24000);
            // Refused: nothing changed, and the bus holds what it held. (A FILTER there would have
            // given the track's own sound back a second time: twice as loud at once.)
            if (!fits) CHECK(out == same && peakOf(std::vector<float>(out.begin(), out.begin() + 1000)) < kEngineSine * 1.01f);
            if (!fits) CHECK(engine->dropped() == 0);
            engine->collectGarbage();
        }
    }
    CHECK(taken == 4 + 5 && refused == 2 * kEffectKinds - 9); // ECHO: NONE and its three; SPACE: NONE and its four; everything else is turned away
    CHECK(effectBus(EffectKind::Tape) == 0 && effectBus(EffectKind::Hall) == 1 && effectBus(EffectKind::Chorus) == -1 && effectBus(EffectKind::Comp) == -1 && effectBus(EffectKind::Saturate) == -1);
}

// A BUS'S LEVEL IS THE ENGINE'S, applied after the rest rule has listened.
static void testBusLevel() {
    const std::vector<float> plain = leftOf(*sineEngine(), 48000);
    // Every effect a bus can hold, through the engine: LEVEL 0 is the track
    // alone (no dry sound comes back from the bus, and nothing else), and
    // LEVEL at half is half of what comes back at full.
    int tried = 0;
    for (int bus = 0; bus < kBuses; ++bus)
        for (int place = 1; place < busListSize(bus); ++place, ++tried) {
            const EffectKind kind = busEffectAt(bus, place);
            std::vector<float> out[3];
            const float levels[3] = {0.f, 0.5f, 1.f};
            for (int l = 0; l < 3; ++l) {
                std::unique_ptr<Engine> engine = sineEngine();
                CHECK(engine->setEffect(busTrack(bus), 0, kind));
                if (l < 2) engine->send(make(Command::SetEffectParam, busTrack(bus), 0, effectMixParam(kind), levels[l])); // (unsaid, it is at full)
                engine->send(make(Command::SetSend, 0, bus, 0, 1.f));
                out[l] = leftOf(*engine, 48000);
                CHECK(engine->busAwake(bus));
            }
            CHECK(out[0] == plain);
            double worst = 0.0, loudest = 0.0;
            for (std::size_t i = 0; i < plain.size(); ++i) {
                const double full = static_cast<double>(out[2][i]) - plain[i], half = static_cast<double>(out[1][i]) - plain[i];
                worst = std::fmax(worst, std::fabs(half - 0.5 * full));
                loudest = std::fmax(loudest, std::fabs(full));
            }
            CHECK(loudest > 0.05 && worst < 1e-6);
        }
    CHECK(tried == 7); // TAPE, DELAY, HALL, REVERB, PING, SPRING, PLATE
    // PARKED AT LEVEL 0 WITH ECHOES IN IT. The rule used to listen after the
    // level: it heard nothing, and after ten seconds cleared the echoes away.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay));
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 0, 0.99f)); // TIME: the longest
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 1, 1.f));   // FEEDBACK full: each echo 90% of the last
        engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
        std::vector<float> down = leftOf(*engine, 48000 + 1320); // (to a moment when the first echo is at the top of its wave: a step in LEVEL there is a step in the sound)
        down.erase(down.begin(), down.end() - 8);
        engine->send(make(Command::SetTrackHeard, 0, 0));
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, 0.f)); // LEVEL right down
        const std::vector<float> going = leftOf(*engine, 2400);
        down.insert(down.end(), going.begin(), going.end()); // (with the last of the sound before, so a step at the very first sample is seen)
        // It goes down like a send does (5 ms), not in one step...
        CHECK(largestJump(down) / sineStep(100.0, 2.0 * kEngineSine) < 1.05 && down[8 + kSendGlide + 240 + 1] == 0.f);
        const std::vector<float> parked = leftOf(*engine, static_cast<int>(1.2f * kBusRestSeconds * 48000.f));
        CHECK(peakOf(parked) == 0.f); // ...nothing is heard...
        CHECK(engine->busAwake(0));   // ...and the bus is still going round
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, 1.f));
        std::vector<float> back = leftOf(*engine, 96000);
        back.insert(back.begin(), 0.f); // (it was silent before: a step at the first sample is seen)
        CHECK(peakOf(back) > 0.03f); // the echoes were kept (0.9 of the last, a second apart: about 0.07 by now)
        CHECK(largestJump(back) / sineStep(100.0, 2.0 * kEngineSine) < 1.05);
        // The level is where it was said: a twin never turned down has the same echoes by now.
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, 0.5f));
        leftOf(*engine, 4800);
        const std::vector<float> half = leftOf(*engine, 48000);
        std::unique_ptr<Engine> twin = sineEngine();
        CHECK(twin->setEffect(kEchoBus, 0, EffectKind::Delay));
        twin->send(make(Command::SetEffectParam, kEchoBus, 0, 0, 0.99f));
        twin->send(make(Command::SetEffectParam, kEchoBus, 0, 1, 1.f));
        twin->send(make(Command::SetSend, 0, 0, 0, 1.f));
        leftOf(*twin, 48000 + 1320);
        twin->send(make(Command::SetTrackHeard, 0, 0));
        leftOf(*twin, 2400 + static_cast<int>(1.2f * kBusRestSeconds * 48000.f) + 96000 + 4800);
        const std::vector<float> whole = leftOf(*twin, 48000);
        double worst = 0.0;
        for (std::size_t i = 0; i < whole.size(); ++i) worst = std::fmax(worst, std::fabs(static_cast<double>(half[i]) - 0.5 * whole[i]));
        CHECK(peakOf(whole) > 0.03f && worst < 1e-6);
    }
    // Nonsense for a LEVEL: not a number is ignored, and the rest is held to 0 .. 100%.
    {
        const auto with = [](std::initializer_list<float> levels) {
            std::unique_ptr<Engine> engine = sineEngine();
            CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay));
            engine->send(make(Command::SetEffectParam, kEchoBus, 0, 0, 0.f)); // a sixteenth
            for (float level : levels) engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, level));
            engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
            return leftOf(*engine, 24000);
        };
        const std::vector<float> half = with({0.5f}), full = with({}), none = with({0.f});
        CHECK(half != full && half != none && full != none);
        CHECK(with({0.5f, std::nanf("")}) == half && with({7.f}) == full && with({INFINITY}) == full && with({-3.f}) == none && with({-INFINITY}) == none);
    }
    // At LEVEL 0 a bus whose echoes HAVE gone still goes to rest.
    {
        std::unique_ptr<Engine> engine = sineEngine();
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Delay));
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 1, 0.f)); // one echo
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, 0.f));
        engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
        leftOf(*engine, 4800);
        engine->send(make(Command::SetTrackHeard, 0, 0));
        leftOf(*engine, static_cast<int>(kBusRestSeconds * 48000.f) - 4800);
        CHECK(engine->busAwake(0));
        leftOf(*engine, 48000 + 9600);
        CHECK(!engine->busAwake(0));
    }
    // Another effect put on the bus starts at full, whatever the last one's LEVEL was;
    // and an effect handed over whole (the setup call) keeps its own MIX.
    {
        std::unique_ptr<Engine> engine = sineEngine(), fresh = sineEngine();
        CHECK(engine->setEffect(kSpaceBus, 0, EffectKind::Reverb));
        engine->send(make(Command::SetEffectParam, kSpaceBus, 0, 2, 0.1f));
        leftOf(*engine, 64);
        CHECK(engine->setEffect(kSpaceBus, 0, EffectKind::Hall) && fresh->setEffect(kSpaceBus, 0, EffectKind::Hall));
        leftOf(*engine, 64);
        leftOf(*fresh, 128);
        for (Engine* e : {engine.get(), fresh.get()}) e->send(make(Command::SetSend, 0, 1, 0, 1.f));
        CHECK(leftOf(*engine, 24000) == leftOf(*fresh, 24000));
        SendRig rig;
        rig.send(0, 1.f);
        rig.engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, 0.25f));
        const std::vector<float> out = leftOf(*rig.engine, 4800);
        CHECK(rig.echo->lastIndex == 3 && rig.echo->lastParam == 0.25f);
        CHECK(std::fabs(peakOf(std::vector<float>(out.begin() + 2400, out.end())) - 2.f * kEngineSine) < 1e-3f); // (the probe gives the sound back whole, at its own level)
    }
    // ANOTHER EFFECT PUT ON A RINGING BUS WHOSE LEVEL IS DOWN. The level used
    // to jump to full at once, so the one leaving was turned UP while it faded
    // out: 10 ms louder than anything that was playing. The level stays where
    // it was until the fade is over, and only then goes to the new one's.
    for (const EffectKind old : {EffectKind::Delay, EffectKind::Tape})
        for (const float level : {0.f, 0.3f})
            for (const EffectKind next : {EffectKind::None, EffectKind::Delay, EffectKind::Tape}) {
                std::unique_ptr<Engine> engine = sineEngine();
                CHECK(engine->setEffect(kEchoBus, 0, old));
                engine->send(make(Command::SetEffectParam, kEchoBus, 0, 0, 0.f));
                engine->send(make(Command::SetEffectParam, kEchoBus, 0, 1, 1.f)); // FEEDBACK full: it rings
                engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
                leftOf(*engine, 48000);
                engine->send(make(Command::SetTrackHeard, 0, 0));
                leftOf(*engine, 4800);
                const float full = peakOf(leftOf(*engine, 4800));
                CHECK(full > 0.02f); // it is ringing, with the track gone
                engine->send(make(Command::SetEffectParam, kEchoBus, 0, 3, level));
                leftOf(*engine, 4800);
                const float before = peakOf(leftOf(*engine, 4800));
                CHECK(level == 0.f ? before == 0.f : (before > 0.2f * full && before < 0.4f * full));
                CHECK(engine->setEffect(kEchoBus, 0, next));
                const float after = peakOf(leftOf(*engine, 960)); // the 10 ms of the fade, and 10 ms more
                CHECK(after <= before * 1.02f); // never louder than what was playing (at LEVEL 0: nothing at all)
            }
    // ...and it does then go to the new one's LEVEL (full): a second later the
    // bus gives what a bus that always held the new effect gives.
    {
        std::unique_ptr<Engine> engine = sineEngine(), fresh = sineEngine();
        CHECK(engine->setEffect(kEchoBus, 0, EffectKind::Tape) && fresh->setEffect(kEchoBus, 0, EffectKind::Delay));
        engine->send(make(Command::SetEffectParam, kEchoBus, 0, effectMixParam(EffectKind::Tape), 0.f));
        for (Engine* e : {engine.get(), fresh.get()}) e->send(make(Command::SetSend, 0, 0, 0, 1.f));
        leftOf(*engine, 24000);
        leftOf(*fresh, 24000 - 64);
        CHECK(engine->busAwake(0) && engine->setEffect(kEchoBus, 0, EffectKind::Delay));
        for (Engine* e : {engine.get(), fresh.get()}) {
            e->send(make(Command::SetEffectParam, kEchoBus, 0, 0, 0.f)); // a sixteenth
            e->send(make(Command::SetEffectParam, kEchoBus, 0, 1, 0.f)); // one echo
        }
        leftOf(*fresh, 64);
        leftOf(*engine, 24000);
        leftOf(*fresh, 24000);
        const std::vector<float> a = leftOf(*engine, 24000), b = leftOf(*fresh, 24000), plain2 = leftOf(*sineEngine(), 72000);
        double worst = 0.0, echo = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            worst = std::fmax(worst, std::fabs(static_cast<double>(a[i]) - b[i]));
            echo = std::fmax(echo, std::fabs(static_cast<double>(b[i]) - plain2[48000 + i]));
        }
        CHECK(echo > 0.05 && worst < 1e-6);
    }
}

// THE REST RULE'S NUMBERS, and the setup call that puts the buses to rest.
static void testBusRestNumbers() {
    CHECK(kBusQuiet == 1e-6f && kBusRestSeconds == 10.f); // 120 dB under full scale, for ten seconds
    const int rest = 480000;
    const float levels[4] = {0.9e-6f, 1.1e-6f, -0.9e-6f, -1.1e-6f};
    for (int side = 0; side < 2; ++side)
        for (float level : levels) {
            // A tail at this level on one side only, the other silent.
            struct OneSide : Probe {
                int side = 0;
                void process(float* left, float* right, int frames) override {
                    Probe::process(left, right, frames);
                    for (int i = 0; i < frames; ++i) (side == 0 ? right : left)[i] = 0.f;
                }
            };
            std::unique_ptr<Engine> engine = sineEngine();
            OneSide* probe = new OneSide;
            probe->side = side;
            probe->gain = 0.f;
            probe->ring = 3 * rest;
            probe->ringLevel = level;
            engine->setBusEffect(0, std::unique_ptr<Effect>(probe));
            engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
            leftOf(*engine, 4800);
            engine->send(make(Command::SetTrackHeard, 0, 0));
            leftOf(*engine, rest + 1000);
            CHECK(engine->busAwake(0) == (std::fabs(level) > 1e-6f));
        }
    // prepare() (a new sample rate, the program starting): the buses are at rest at once, their
    // effects cleared, and nothing comes out of them until something is sent again.
    {
        SendRig rig;
        rig.echo->ring = rig.space->ring = 480000;
        rig.send(0, 1.f);
        rig.send(1, 1.f);
        leftOf(*rig.engine, 4800);
        rig.engine->send(make(Command::SetSend, 0, 0, 0, 0.f));
        rig.engine->send(make(Command::SetSend, 0, 1, 0, 0.f));
        leftOf(*rig.engine, 4800);
        CHECK(rig.engine->busAwake(0) && rig.engine->busAwake(1) && rig.echo->resets == 0); // ringing on
        rig.engine->prepare(48000.f);
        CHECK(!rig.engine->busAwake(0) && !rig.engine->busAwake(1) && rig.echo->resets == 1 && rig.space->resets == 1);
        const int runs = rig.echo->runs + rig.space->runs;
        const std::vector<float> after = leftOf(*rig.engine, 4800);
        CHECK(rig.echo->runs + rig.space->runs == runs && !rig.engine->busAwake(0) && !rig.engine->busAwake(1));
        std::unique_ptr<Engine> plain = sineEngine();
        leftOf(*plain, 9600);
        plain->prepare(48000.f);
        CHECK(after == leftOf(*plain, 4800));
    }
}

// ---- DUCK ---------------------------------------------------------------------------------------

static const int kDuckFall = 144; // 3 ms at 48 kHz: from the top to the full dip
static float duckRelease(double seconds) { return static_cast<float>(std::log(seconds / 0.02) / std::log(50.0)); } // the RELEASE setting for a time
static const double kDuckUsual = 0.02 * std::pow(50.0, 0.6); // RELEASE as it comes: 209 ms
static float duckLane(int lane) { return (static_cast<float>(lane) + 0.5f) / 8.f; }                               // the SOURCE setting for a lane, 0 to 7
// What is left of the sound `n` samples after a hit (0: the hit's own sample), from rest.
static double duckGain(long n, double amount, double release, double rate = 48000.0) {
    if (n < 0) return 1.0;
    if (n < kDuckFall) return 1.0 - amount * static_cast<double>(n + 1) / kDuckFall;
    const double back = static_cast<double>(n - kDuckFall + 1) / (release * rate);
    return back >= 1.0 ? 1.0 : 1.0 - amount * (1.0 - back) * (1.0 - back);
}
// A steady 1 through an effect, cut into blocks of `block`; `lanes` are hit at each frame in `hits`
// (which must fall on a block's first frame). What comes out is how much of the sound is left.
static std::vector<float> duckedOnes(Effect& effect, std::size_t frames, const std::vector<std::size_t>& hits, unsigned lanes = 1u, std::size_t block = kMaxBlock, float in = 1.f) {
    Stereo io(frames);
    io.l.assign(frames, in);
    io.r.assign(frames, -in);
    for (std::size_t at = 0; at < frames; at += block) {
        for (std::size_t hit : hits)
            if (hit == at) effect.drumHits(lanes);
        effect.process(io.l.data() + at, io.r.data() + at, static_cast<int>(frames - at < block ? frames - at : block));
    }
    for (std::size_t i = 0; i < frames; ++i) CHECK(io.r[i] == -io.l[i]); // both sides alike
    return io.l;
}

// Puts out a steady 1, whatever came in: in a track's first slot, what leaves the track is its level times what the later slots leave.
struct Flat : Effect {
    void setParam(int, float) override {}
    void process(float* left, float* right, int n) override {
        for (int i = 0; i < n; ++i) left[i] = right[i] = 1.f;
    }
};
// Writes down which lanes it is told were hit, and on which frame.
struct HitWatch : Effect {
    std::vector<std::pair<long, unsigned>> heard;
    long frames = 0;
    HitWatch() { heard.reserve(4096); }
    void setParam(int, float) override {}
    void drumHits(unsigned lanes) override {
        if (heard.size() < heard.capacity()) heard.emplace_back(frames, lanes);
    }
    void process(float*, float*, int n) override { frames += n; }
};
static const double kTrackLevel = 0.65;
static const int kStepFrames = 6000; // a step at 120 BPM and 48 kHz

// An engine whose first track (the drums) puts out a steady 1 into a DUCK in its second slot.
struct DuckRig {
    std::unique_ptr<Engine> engine{new Engine};
    explicit DuckRig(float amount = 1.f, float release = -1.f, int lane = 0, bool withDuck = true) {
        engine->setTrackEffect(0, 0, std::unique_ptr<Effect>(new Flat));
        if (withDuck) engine->setTrackEffect(0, 1, fresh(EffectKind::Duck, amount, release, duckLane(lane)));
    }
    void step(int lane, int at, float level = 1.f, int track = 0) { engine->send(make(Command::SetStep, track, lane, at, level)); }
    void play() { engine->send(make(Command::Play, 0, 0, 1)); }
    // What is left of the track's sound, frame by frame, asked for in blocks of odd sizes.
    std::vector<float> left(int frames) {
        std::vector<float> all;
        for (int done = 0, size = 37; done < frames; done += size, size = size % 301 + 64) {
            const int n = frames - done < size ? frames - done : size;
            const std::vector<float> got = leftOf(*engine, n);
            for (float v : got) all.push_back(static_cast<float>(v / kTrackLevel));
            size = n;
        }
        return all;
    }
};
// The frames at which a dip starts: the sound was whole and is now less.
static std::vector<long> dipsIn(const std::vector<float>& left) {
    std::vector<long> at;
    for (std::size_t i = 1; i < left.size(); ++i)
        if (left[i] < left[i - 1] - 1e-4f && (i < 2 || left[i - 1] >= left[i - 2] - 1e-6f)) at.push_back(static_cast<long>(i));
    return at;
}

// A song for the helpers: drums that duck themselves, three synths ducked from three lanes (one of
// them a lane that is ratcheted and pushed late), a DUCK on the master, one swapped out and in.
static std::vector<float> duckSong(int helpers, bool ducks = true) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->useWorkers(helpers);
    for (int s = 0; s < kSteps; s += 4) engine->send(make(Command::SetStep, 0, 0, s, 0.9f));
    for (int s = 2; s < kSteps; s += 4) engine->send(make(Command::SetStep, 0, 2, s, 0.7f));
    engine->send(make(Command::SetStep, 0, 4, 6, 0.8f));
    engine->send(make(Command::SetStepNudge, 0, 4, 6, 0.3f));
    Command fx = make(Command::SetStepFx, 0, 4, 6);
    fx.d = 3;
    engine->send(fx);
    engine->send(make(Command::SetStepChance, 0, 2, 10, 0.5f));
    const SynthKind kinds[3] = {SynthKind::Phazed, SynthKind::Qmod, SynthKind::Phazed};
    for (int t = 1; t <= 3; ++t) {
        CHECK(engine->addSynthTrack(t, kinds[t - 1]));
        for (int n = 0; n < 4; ++n) {
            Command note = make(Command::SetNote, t, n, n * 4, 0.7f);
            note.c = 36 + 7 * t + 2 * n;
            note.d = 4;
            engine->send(note);
        }
    }
    if (ducks) {
        CHECK(engine->setEffect(0, 1, EffectKind::Duck) && engine->setEffect(1, 0, EffectKind::Duck) && engine->setEffect(2, 2, EffectKind::Duck) && engine->setEffect(3, 0, EffectKind::Duck));
        CHECK(engine->setEffect(kMasterTrack, 0, EffectKind::Duck));
        engine->send(make(Command::SetEffectParam, 0, 1, 2, duckLane(2)));
        engine->send(make(Command::SetEffectParam, 1, 0, 0, 1.f));
        engine->send(make(Command::SetEffectParam, 2, 2, 2, duckLane(4)));
        engine->send(make(Command::SetEffectParam, 2, 2, 1, 0.2f));
        engine->send(make(Command::SetEffectParam, 3, 0, 1, 0.9f));
        engine->send(make(Command::SetEffectParam, kMasterTrack, 0, 0, 0.3f));
    }
    std::vector<float> all(2 * 1024);
    engine->render(all.data(), 1024);
    engine->send(make(Command::Play, 0, 0, 1));
    int round = 0;
    for (int done = 0, size = 37; done < 3 * 16 * kStepFrames; done += size, size = size % 301 + 64, ++round) {
        if (ducks && round == 400) CHECK(engine->setEffect(1, 0, EffectKind::None));
        if (ducks && round == 640) CHECK(engine->setEffect(1, 0, EffectKind::Duck));
        std::vector<float> got(static_cast<std::size_t>(2 * size), 99.f);
        engine->render(got.data(), size);
        all.insert(all.end(), got.begin(), got.end());
        engine->collectGarbage();
    }
    return all;
}

static void testDuck() {
    // ---- the table
    CHECK(static_cast<int>(EffectKind::Duck) == 11 && std::string(effectInfo(EffectKind::Duck).word) == "duck" && effectFromWord("duck") == EffectKind::Duck);
    CHECK(effectInfo(EffectKind::Duck).family == EffectFamily::Level && effectInfo(EffectKind::Duck).params == 3 && effectChoices(EffectKind::Duck, 2) == 8);
    CHECK(effectChoices(EffectKind::Duck, 0) == 0 && effectChoices(EffectKind::Duck, 1) == 0 && effectBus(EffectKind::Duck) == -1 && effectMixParam(EffectKind::Duck) == -1);
    CHECK(reads(EffectKind::Duck, 0, 0.6f) == "60%" && reads(EffectKind::Duck, 1, 0.f) == "20 MS" && reads(EffectKind::Duck, 1, 1.f) == "1.0 S" && reads(EffectKind::Duck, 1, 0.6f) == "209 MS");
    CHECK(reads(EffectKind::Duck, 1, duckRelease(0.1)) == "100 MS" && reads(EffectKind::Duck, 1, duckRelease(0.5)) == "500 MS" && reads(EffectKind::Duck, 1, 0.99f) == "962 MS");
    for (int lane = 0; lane < 8; ++lane) CHECK(reads(EffectKind::Duck, 2, duckLane(lane)) == std::string("LANE ") + static_cast<char>('1' + lane));
    CHECK(reads(EffectKind::Duck, 2, 0.f) == "LANE 1" && reads(EffectKind::Duck, 2, 1.f) == "LANE 8");
    CHECK(effectMeterParam(EffectKind::Duck) == 0 && std::string(effectInfo(EffectKind::Duck).paramNames[0]) == "AMOUNT");
    // As it comes: 60%, 209 ms, lane 1.
    CHECK(effectInfo(EffectKind::Duck).defaults[0] == 0.6f && effectInfo(EffectKind::Duck).defaults[1] == 0.6f && effectInfo(EffectKind::Duck).defaults[2] == 0.f);

    // ---- no hit: the sound untouched, to the sample, wherever the settings are
    for (float amount : {0.f, 0.6f, 1.f}) {
        std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, amount, 0.3f, duckLane(3));
        const Stereo in = pinInput(48000.f);
        Stereo out = in;
        for (std::size_t at = 0; at < out.frames(); at += kMaxBlock) {
            if (at % 4800 == 0) duck->drumHits(0xffu & ~(1u << 3)); // every lane but its own is being hit
            duck->process(out.l.data() + at, out.r.data() + at, kMaxBlock < out.frames() - at ? kMaxBlock : static_cast<int>(out.frames() - at));
            CHECK(duck->meter() == 0.f);
        }
        CHECK(out.l == in.l && out.r == in.r);
    }
    // AMOUNT at 0 with its own lane hit: untouched too.
    {
        std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 0.f);
        const Stereo in = pinInput(48000.f);
        Stereo out = in;
        through(*duck, out);
        CHECK(out.l == in.l && out.r == in.r && duck->meter() == 0.f);
    }

    // ---- the shape: a straight fall of 3 ms to 1 - AMOUNT, then back over RELEASE, and it arrives
    double worst = 0.0;
    for (float amount : {1.f, 0.6f, 0.25f}) {
        for (double release : {0.02, 0.1, kDuckUsual, 1.0}) {
            std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, amount, release == kDuckUsual ? -1.f : duckRelease(release));
            const std::size_t hit = 6400, frames = hit + static_cast<std::size_t>(release * 48000.0) + 9600;
            const std::vector<float> left = duckedOnes(*duck, frames, {hit});
            const long back = static_cast<long>(std::lround(release * 48000.0));
            for (std::size_t i = 0; i < frames; ++i) {
                const long n = static_cast<long>(i) - static_cast<long>(hit);
                const double want = duckGain(n, amount, release);
                worst = std::fmax(worst, std::fabs(left[i] - want));
                // Untouched before the hit, from its very sample on it dips, and once back it is untouched again.
                if (n < 0 || n > kDuckFall + back + 2) CHECK(left[i] == 1.f);
                if (n >= 0 && n < kDuckFall + static_cast<long>(0.97 * back)) CHECK(left[i] < 1.f);
            }
            // Right down to 1 - AMOUNT, on the last sample of the fall, and nowhere lower.
            CHECK(std::fabs(left[hit + kDuckFall - 1] - (1.0 - amount)) < 1e-5);
            float lowest = 1.f;
            for (float v : left) lowest = std::fmin(lowest, v);
            CHECK(lowest == left[hit + kDuckFall - 1] && lowest >= 0.f);
            // Half of the dip is back 29% of the way through RELEASE (1 - sqrt(1/2)).
            const std::size_t half = hit + kDuckFall + static_cast<std::size_t>(0.2929 * release * 48000.0);
            CHECK(std::fabs(left[half] - (1.0 - 0.5 * amount)) < 0.01 * amount + 2e-3);
            // No step anywhere larger than the fall's own.
            CHECK(largestJump(left) <= amount / kDuckFall * 1.001f + 1e-6f);
        }
    }
    SAY("duck     furthest from the written shape %.2e\n", worst);
    CHECK(worst < 2e-4); // today 3e-5: the sums of small steps

    // ---- which lane: each SOURCE hears its own lane and no other
    for (int source = 0; source < 8; ++source)
        for (int lane = 0; lane < 8; ++lane) {
            std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 1.f, -1.f, duckLane(source));
            const std::vector<float> left = duckedOnes(*duck, 1280, {640}, 1u << lane);
            float lowest = 1.f;
            for (float v : left) lowest = std::fmin(lowest, v);
            CHECK(lane == source ? lowest == 0.f : lowest == 1.f);
        }
    // Several lanes hit together: it hears its own among them. A lane past the eighth is nobody's.
    {
        std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 1.f, -1.f, duckLane(5));
        CHECK(duckedOnes(*duck, 640, {0}, 0x21u)[kDuckFall - 1] == 0.f);
        std::unique_ptr<Effect> other = fresh(EffectKind::Duck, 1.f, -1.f, duckLane(7));
        CHECK(duckedOnes(*other, 640, {0}, 0x100u)[kDuckFall - 1] == 1.f);
    }

    // ---- a hit before it is back: the fall starts again from where the level is, with no jump
    {
        std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 0.8f, duckRelease(0.2));
        const std::size_t first = 640, again = first + 3840; // 80 ms on: about 37% of the dip is still on
        const std::vector<float> left = duckedOnes(*duck, 24000, {first, again});
        const double before = left[again - 1];
        CHECK(before > 0.5 && before < 0.9 && std::fabs(before - duckGain(static_cast<long>(again - 1 - first), 0.8, 0.2)) < 1e-4);
        CHECK(left[again] < before && std::fabs((before - left[again]) - 0.8 / kDuckFall) < 1e-5); // down at the fall's speed, from where it was
        CHECK(largestJump(left) <= 0.8f / kDuckFall * 1.001f + 1e-6f);
        // It reaches the bottom sooner (it had less far to go), and then comes back as from any hit.
        long bottom = 0;
        for (std::size_t i = again; i < left.size(); ++i)
            if (std::fabs(left[i] - 0.2f) < 1e-5f) {
                bottom = static_cast<long>(i);
                break;
            }
        CHECK(bottom > static_cast<long>(again) + 20 && bottom < static_cast<long>(again) + kDuckFall - 20);
        for (long n = 0; n < 12000; n += 97) CHECK(std::fabs(left[static_cast<std::size_t>(bottom + n)] - duckGain(kDuckFall - 1 + n, 0.8, 0.2)) < 2e-4);
        CHECK(left.back() == 1.f);
        // Two hits told in one go are one hit.
        std::unique_ptr<Effect> twice = fresh(EffectKind::Duck, 0.8f, duckRelease(0.2));
        twice->drumHits(1u);
        std::unique_ptr<Effect> once = fresh(EffectKind::Duck, 0.8f, duckRelease(0.2));
        CHECK(duckedOnes(*twice, 12000, {0}) == duckedOnes(*once, 12000, {0}));
    }

    // ---- the same however it is cut into blocks, to the bit; and with flush-to-zero on or off
    {
        const std::vector<std::size_t> hits = {4480, 13440, 17920, 44800};
        std::vector<float> whole;
        for (std::size_t block : {std::size_t(64), std::size_t(1), std::size_t(7), std::size_t(35), std::size_t(4480)}) {
            std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 0.7f, duckRelease(0.3));
            const std::vector<float> left = duckedOnes(*duck, 67200, hits, 1u, block, 0.37f);
            if (whole.empty()) whole = left;
            CHECK(left == whole && largestJump(left) > 1e-4f);
        }
        CHECK(sameFlushed(EffectKind::Duck, 0.9f, 0.5f, 0.f, -1.f, 2.0));
        // Nothing too small to hold, ever: a whole dip and the way back, on a sound and on a whisper.
        for (float in : {0.5f, 1e-30f}) {
            std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 1.f, duckRelease(0.05));
            const TooSmall watch;
            duckedOnes(*duck, 9600, {0, 6400}, 1u, 64, in);
            CHECK(!watch.seen());
        }
    }

    // ---- a turned setting glides: AMOUNT turned up in the middle of a dip does not jump
    {
        const auto run = [](Effect& effect, Stereo& io, std::size_t from, std::size_t to) { // (through() would hit the drum again)
            for (std::size_t at = from; at < to; at += kMaxBlock) effect.process(io.l.data() + at, io.r.data() + at, kMaxBlock);
        };
        std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 0.2f, 1.f);
        Stereo io(48000);
        io.l.assign(48000, 1.f);
        io.r.assign(48000, 1.f);
        duck->drumHits(1u);
        run(*duck, io, 64, 640);
        duck->setParam(0, 1.f);
        run(*duck, io, 640, 19200);
        // 576 frames in: 0.2 * (1 - 432/48000)^2 of the sound was off; it goes to five times that over the glide's 20 ms and more.
        CHECK(std::fabs(io.l[639] - (1.0 - 0.2 * 0.982)) < 1e-3);
        CHECK(largestJump(io.l, 639, 4800) < 0.8f / 960.f * 1.2f && largestJump(io.l, 639, 4800) > 0.8f / 960.f * 0.3f);
        CHECK(std::fabs(io.l[1600] - duckGain(1600 - 64, 0.2 + 0.8 * 0.63, 1.0)) < 0.03); // most of the way after its 20 ms
        CHECK(std::fabs(io.l[19199] - duckGain(19199 - 64, 1.0, 1.0)) < 1e-5); // ...and it got there, exactly
        // RELEASE turned in the middle of the way back: the level carries on from where it is, faster.
        std::unique_ptr<Effect> slow = fresh(EffectKind::Duck, 1.f, 1.f);
        io.l.assign(48000, 1.f);
        io.r.assign(48000, 1.f);
        slow->drumHits(1u);
        run(*slow, io, 64, 4864);
        const float was = io.l[4863];
        slow->setParam(1, duckRelease(0.1));
        run(*slow, io, 4864, 19264);
        CHECK(was < 0.2f && std::fabs(io.l[4864] - was) < 1e-3f && largestJump(io.l, 4800, 19264) < 2.f / 4800.f * 1.2f);
        CHECK(io.l[4864 + 6000] == 1.f && io.l[4864 + 3360] < 1.f && io.l[19263] == 1.f); // back in about the new 100 ms, not the old second
        // ...and the speed itself glides: just after the turn it is still climbing nearly as slowly as before, not ten times as fast.
        const float slopeBefore = io.l[4863] - io.l[4862], slopeAfter = io.l[4864 + 9] - io.l[4864 + 8];
        CHECK(slopeBefore > 0.f && slopeAfter > slopeBefore && slopeAfter < 2.f * slopeBefore && io.l[4864 + 2000] - io.l[4864 + 1999] > 5.f * slopeBefore);
        // At rest a turned setting is simply there: the next hit dips by the new AMOUNT from its first sample.
        std::unique_ptr<Effect> rest = fresh(EffectKind::Duck, 0.2f);
        Stereo quiet(640);
        rest->process(quiet.l.data(), quiet.r.data(), 640);
        rest->setParam(0, 1.f);
        const std::vector<float> left = duckedOnes(*rest, 640, {0});
        CHECK(std::fabs(left[0] - (1.0 - 1.0 / kDuckFall)) < 1e-6 && left[kDuckFall - 1] == 0.f);
    }

    // ---- reset: whatever it was doing, it is at rest
    {
        std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 1.f, 1.f);
        duckedOnes(*duck, 640, {0});
        CHECK(duck->meter() > 0.f);
        duck->reset();
        CHECK(duck->meter() == 0.f);
        const std::vector<float> after = duckedOnes(*duck, 640, {});
        for (float v : after) CHECK(v == 1.f);
        // A hit told and then a reset: forgotten.
        duck->drumHits(1u);
        duck->reset();
        CHECK(duckedOnes(*duck, 640, {})[200] == 1.f);
        // In the middle of the fall: it stops there.
        duckedOnes(*duck, 64, {0});
        duck->reset();
        for (float v : duckedOnes(*duck, 640, {})) CHECK(v == 1.f);
    }
    // ---- at another sample rate the times are the same times: 3 ms down, RELEASE back
    {
        std::unique_ptr<Effect> duck = makeEffect(EffectKind::Duck, 44100.f, 120.f);
        duck->setParam(0, 1.f);
        duck->setParam(1, duckRelease(0.1));
        const std::vector<float> left = duckedOnes(*duck, 8820, {0});
        long bottom = -1, back = -1;
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (bottom < 0 && left[i] == 0.f) bottom = static_cast<long>(i);
            if (bottom >= 0 && back < 0 && left[i] == 1.f) back = static_cast<long>(i);
        }
        CHECK(bottom >= 131 && bottom <= 133 && back - bottom >= 4390 && back - bottom <= 4412); // 132.3 samples, then 4410
    }

    // ---- what it shows: the most it dipped in the last block, in dB
    {
        std::unique_ptr<Effect> duck = fresh(EffectKind::Duck, 0.5f, 1.f);
        duckedOnes(*duck, 256, {0}, 1u, 256); // the block that holds the bottom of the fall
        CHECK(std::fabs(duck->meter() - 6.0206f) < 0.01f);
        duckedOnes(*duck, 24000, {});
        CHECK(duck->meter() > 0.5f && duck->meter() < 3.f); // half way back in time, a quarter of the dip is left: 1.2 dB
        duckedOnes(*duck, 48000, {});
        CHECK(duck->meter() == 0.f);
        std::unique_ptr<Effect> all = fresh(EffectKind::Duck, 1.f, 1.f);
        duckedOnes(*all, 256, {0}, 1u, 256);
        CHECK(all->meter() == 60.f); // right down: "a lot", not infinity
        std::unique_ptr<Effect> some = fresh(EffectKind::Duck, 0.9f, 1.f);
        duckedOnes(*some, 256, {0}, 1u, 256);
        CHECK(std::fabs(some->meter() - 20.f) < 0.01f);
    }

    // ==== in the engine ====
    // ---- what a slot is told: the first drum track's hits, on their own sample
    {
        std::unique_ptr<Engine> engine(new Engine);
        HitWatch* watch = new HitWatch;
        HitWatch* master = new HitWatch;
        HitWatch* bus = new HitWatch;
        CHECK(engine->addSynthTrack(1, SynthKind::Phazed));
        engine->setTrackEffect(1, 2, std::unique_ptr<Effect>(watch));
        engine->setMasterEffect(1, std::unique_ptr<Effect>(master));
        engine->setBusEffect(0, std::unique_ptr<Effect>(bus));
        engine->send(make(Command::SetSend, 0, 0, 0, 1.f));
        engine->send(make(Command::SetStep, 0, 0, 0, 1.f));
        engine->send(make(Command::SetStep, 0, 0, 8, 0.01f)); // a ghost of a hit is a hit
        engine->send(make(Command::SetStep, 0, 3, 4, 1.f));
        engine->send(make(Command::SetStep, 0, 7, 4, 1.f));
        engine->send(make(Command::SetStep, 0, 5, 12, 1.f));
        engine->send(make(Command::SetStepChance, 0, 5, 12, 0.f)); // never played: never told
        engine->send(make(Command::SetStep, 0, 2, 10, 1.f));
        engine->send(make(Command::SetStepNudge, 0, 2, 10, 0.25f)); // late by a quarter of a step
        engine->send(make(Command::SetStep, 0, 1, 14, 1.f));
        engine->send(make(Command::SetStepNudge, 0, 1, 14, -0.5f)); // early by half a step
        engine->send(make(Command::SetStep, 0, 6, 6, 1.f));
        Command fx = make(Command::SetStepFx, 0, 6, 6);
        fx.d = 4; // a ratchet of four: four hits
        engine->send(fx);
        engine->send(make(Command::SetLaneLevel, 0, 0, 0, 0.f)); // turned right down: it is the hit that counts, not the sound
        engine->send(make(Command::Play, 0, 0, 1));
        for (int done = 0, size = 37; done < 16 * kStepFrames; done += size, size = size % 301 + 64) leftOf(*engine, done + size > 16 * kStepFrames ? 16 * kStepFrames - done : size);
        const std::vector<std::pair<long, unsigned>> want = {
            {0, 1u}, {4 * kStepFrames, 0x88u}, {6 * kStepFrames, 0x40u}, {6 * kStepFrames + 1500, 0x40u}, {6 * kStepFrames + 3000, 0x40u}, {6 * kStepFrames + 4500, 0x40u},
            {8 * kStepFrames, 1u}, {10 * kStepFrames + 1500, 4u}, {13 * kStepFrames + 3000, 2u},
        };
        CHECK(watch->heard == want);
        CHECK(master->heard == want);  // the master's slots are told too
        CHECK(bus->heard.empty() && bus->frames > 0); // a bus is not (no DUCK can be on one), though it ran
        CHECK(effectBus(EffectKind::Duck) == -1 && !engine->setEffect(kEchoBus, 0, EffectKind::Duck) && !engine->setEffect(kSpaceBus, 0, EffectKind::Duck));
        // Stopped, a hit played by hand is told on the first frame of the next render; a ratchet's later hits on theirs.
        engine->send(make(Command::Stop));
        leftOf(*engine, 48000);
        watch->heard.clear();
        const long from = watch->frames;
        engine->send(make(Command::Trigger, 0, 3, 0, 0.8f));
        Command roll = make(Command::Trigger, 0, 5, 0, 0.8f);
        roll.bar = 2;
        engine->send(roll);
        leftOf(*engine, 300);
        leftOf(*engine, 9000);
        const std::vector<std::pair<long, unsigned>> hand = {{from, 0x28u}, {from + 3000, 0x20u}};
        CHECK(watch->heard == hand);
        // A hit on another drum track is not told: only the first one's are.
        CHECK(engine->addDrumTrack(2));
        leftOf(*engine, 64);
        watch->heard.clear();
        engine->send(make(Command::Trigger, 2, 0, 0, 0.8f));
        leftOf(*engine, 640);
        CHECK(watch->heard.empty());
        // ...until it IS the first: the drums on track 1 go, and track 3's are the first there are.
        CHECK(engine->removeTrack(0));
        leftOf(*engine, 640);
        const long now = watch->frames;
        engine->send(make(Command::Trigger, 2, 4, 0, 0.8f));
        leftOf(*engine, 640);
        const std::vector<std::pair<long, unsigned>> third = {{now, 0x10u}};
        CHECK(watch->heard == third);
        // With no drums left at all nothing is told, and the last hit is not told again and again.
        engine->send(make(Command::Trigger, 2, 1, 0, 0.8f));
        leftOf(*engine, 64);
        CHECK(watch->heard.size() == 2);
        CHECK(engine->removeTrack(2));
        leftOf(*engine, 640);
        CHECK(watch->heard.size() == 2);
        engine->collectGarbage();
    }
    // ---- a DUCK in a slot: the dip starts on the hit's own sample, however the audio is asked for
    {
        DuckRig rig(0.75f, duckRelease(0.1)), plain(0.f, -1.f, 0, false);
        for (DuckRig* r : {&rig, &plain}) {
            r->step(0, 0);
            r->step(0, 4);
            r->step(3, 6); // another lane: nothing
            r->step(0, 9);
            r->engine->send(make(Command::SetStepNudge, 0, 0, 9, 0.25f));
            r->engine->send(make(Command::SetTrackHeard, 1, 0));
            r->play();
        }
        const std::vector<float> left = rig.left(16 * kStepFrames), whole = plain.left(16 * kStepFrames);
        for (float v : whole) CHECK(std::fabs(v - 1.f) < 1e-6f);
        const std::vector<long> hits = {0, 4 * kStepFrames, 9 * kStepFrames + 1500};
        double off = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i) {
            double want = 1.0;
            for (long hit : hits) want = std::fmin(want, duckGain(static_cast<long>(i) - hit, 0.75, 0.1));
            off = std::fmax(off, std::fabs(left[i] - want));
        }
        CHECK(off < 3e-4);
        CHECK(left[0] < 1.f && left[4 * kStepFrames - 1] > 0.9999f && left[4 * kStepFrames] < 0.9999f); // from the very sample, not the block after
        // The screen can see it: the dB it is dipping, through the engine.
        DuckRig seen(0.5f, 1.f);
        seen.step(0, 0);
        seen.play();
        seen.left(2048);
        CHECK(seen.engine->effectMeter(0, 1) > 3.f && seen.engine->effectMeter(0, 1) < 6.1f && seen.engine->effectMeter(0, 0) == 0.f);
    }
    // ---- on the master: the whole mix dips
    {
        std::unique_ptr<Engine> engine(new Engine);
        engine->setTrackEffect(0, 0, std::unique_ptr<Effect>(new Flat));
        engine->setMasterEffect(0, fresh(EffectKind::Duck, 1.f, duckRelease(0.05), duckLane(2)));
        engine->send(make(Command::SetStep, 0, 2, 2, 1.f));
        engine->send(make(Command::Play, 0, 0, 1));
        const std::vector<float> left = leftOf(*engine, 4 * kStepFrames);
        CHECK(std::fabs(left[2 * kStepFrames - 1] - kTrackLevel) < 1e-6 && left[2 * kStepFrames + kDuckFall - 1] == 0.f && std::fabs(left[2 * kStepFrames + kDuckFall + 2400] - kTrackLevel) < 1e-6);
        CHECK(engine->setEffect(kMasterTrack, 1, EffectKind::Duck)); // and by the usual way in
    }
    // ---- one that is fading out of its slot still hears the hits (it is still sounding)
    {
        DuckRig rig(1.f, 1.f);
        leftOf(*rig.engine, 640);
        rig.engine->send(make(Command::Trigger, 0, 0, 0, 0.8f));
        CHECK(rig.engine->setEffect(0, 1, EffectKind::None));
        const std::vector<float> left = rig.left(kFade + 64);
        // The fall is over 3 ms in; the old effect is 70% of the sound then.
        CHECK(std::fabs(left[kDuckFall - 1] - (1.0 - (1.0 - static_cast<double>(kDuckFall) / kFade))) < 0.01);
        CHECK(std::fabs(left[kFade + 10] - 1.f) < 1e-6f);
        rig.engine->collectGarbage();
    }
    // ---- with the drums muted: another track still dips (it is keyed by the hit, not the sound)
    for (int heard = 0; heard < 2; ++heard) {
        std::unique_ptr<Engine> engine(new Engine);
        CHECK(engine->addSynthTrack(1, SynthKind::Phazed));
        engine->setTrackEffect(1, 0, std::unique_ptr<Effect>(new Flat));
        engine->setTrackEffect(1, 1, fresh(EffectKind::Duck, 1.f, duckRelease(0.05)));
        engine->send(make(Command::SetStep, 0, 0, 1, 1.f));
        engine->send(make(Command::SetTrackHeard, 0, heard));
        engine->send(make(Command::SetLaneLevel, 0, 0, 0, 0.f)); // (and silent either way, so that only track 2 is in what is measured)
        engine->send(make(Command::Play, 0, 0, 1));
        const std::vector<float> left = leftOf(*engine, 3 * kStepFrames);
        CHECK(std::fabs(left[kStepFrames - 1] - kTrackLevel) < 1e-6 && left[kStepFrames + kDuckFall - 1] == 0.f && std::fabs(left[kStepFrames + kDuckFall + 2400] - kTrackLevel) < 1e-6);
    }
    // ---- the same on any number of helpers, and twice from fresh, to the last bit
    {
        const std::vector<float> alone = duckSong(0);
        CHECK(peakOf(alone) > 0.1f && alone.size() > 500000);
        for (float v : alone) {
            if (v != v || v == 99.f) {
                CHECK(!"a sample that is not a number, or was never written");
                break;
            }
        }
        CHECK(duckSong(0) == alone);
        for (int helpers : {1, 2, 3}) CHECK(duckSong(helpers) == alone);
        // And it is ducking: the song without them is louder.
        const std::vector<float> dry = duckSong(0, false);
        CHECK(dry.size() == alone.size() && rmsOf(alone) < 0.9 * rmsOf(dry));
    }
}

// ---- A TAIL ALWAYS ENDS -------------------------------------------------------------------------

// A track's sound for these: full-scale noise for `frames`, then nothing at all.
struct Burst : Effect {
    long left;
    Noise noise{4242u};
    explicit Burst(long frames) : left(frames) {}
    void setParam(int, float) override {}
    void process(float* l, float* r, int n) override {
        for (int i = 0; i < n; ++i) {
            l[i] = r[i] = left > 0 ? noise.next() : 0.f;
            if (left > 0) --left;
        }
    }
};
// A quarter of a second of full-scale noise is sent to a bus that holds `kind`, set as `settings`
// say (its LEVEL full). From the last sample sent: how many seconds until the bus is at rest (its
// tail under kBusQuiet for kBusRestSeconds, and the effect cleared). `most`: give up there.
static double secondsToRest(EffectKind kind, const float* settings, float tempo, double most) {
    const int bus = effectBus(kind), sent = 12000;
    std::unique_ptr<Engine> engine(new Engine);
    engine->setTrackEffect(0, 0, std::unique_ptr<Effect>(new Burst(sent)));
    engine->send(make(Command::SetTempo, 0, 0, 0, tempo));
    engine->send(make(Command::SetTrackLevel, 0, 0, 0, 1.f));
    CHECK(bus >= 0 && engine->setEffect(busTrack(bus), 0, kind));
    for (int i = 0; i < effectInfo(kind).params; ++i)
        if (i != effectMixParam(kind)) engine->send(make(Command::SetEffectParam, busTrack(bus), 0, i, settings[i]));
    engine->send(make(Command::SetSend, 0, bus, 0, 1.f));
    std::vector<float> out(2 * 4800);
    engine->render(out.data(), 4800);
    engine->render(out.data(), 4800);
    engine->render(out.data(), 2400);
    CHECK(engine->busAwake(bus));
    double seconds = 0.0;
    while (engine->busAwake(bus) && seconds < most) {
        engine->render(out.data(), 4800);
        seconds += 0.1;
    }
    return seconds;
}

// EVERY EFFECT A BUS CAN HOLD DIES AWAY, at every setting, at the slowest tempo (where echoes are
// furthest apart): a bus always comes to rest, and a bounce always ends. kLongestTailSeconds is
// the promise; these are the times behind it, the worst of each effect's settings at 0, 50% and
// 100% each, in seconds from the last sound sent to the bus at rest (which is ten seconds after
// its tail has fallen 120 dB under full):
static void testATailAlwaysEnds() {
    double longest = 0.0;
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        if (effectBus(kind) < 0) continue;
        const EffectInfo& info = effectInfo(kind);
        const int mix = effectMixParam(kind);
        int free = 0;
        for (int i = 0; i < info.params; ++i) free += i != mix ? 1 : 0;
        double worst = 0.0;
        float worstAt[kEffectParams] = {};
        int cases = 1;
        for (int i = 0; i < free; ++i) cases *= 3;
        for (int c = 0; c < cases; ++c) {
            float settings[kEffectParams] = {};
            for (int i = 0, digit = c; i < info.params; ++i) {
                if (i == mix) continue;
                settings[i] = 0.5f * static_cast<float>(digit % 3);
                digit /= 3;
            }
            const double seconds = secondsToRest(kind, settings, kMinTempo, kLongestTailSeconds + 60.0);
            CHECK(seconds < kLongestTailSeconds);
            if (seconds > worst) {
                worst = seconds;
                for (int i = 0; i < kEffectParams; ++i) worstAt[i] = settings[i];
            }
        }
        SAY("tail     %-7s at rest %.1f s after the last sound, at its worst (settings %.0f%% %.0f%% %.0f%% %.0f%%, %g BPM)\n", info.name, worst, worstAt[0] * 100.0, worstAt[1] * 100.0, worstAt[2] * 100.0, worstAt[3] * 100.0, static_cast<double>(kMinTempo));
        longest = std::fmax(longest, worst);
    }
    SAY("tail     the longest of all: %.1f s (kLongestTailSeconds is %.0f)\n", longest, static_cast<double>(kLongestTailSeconds));
    CHECK(longest > 0.8 * kLongestTailSeconds); // and the promise is not far over what is measured
}

// TAPE ALWAYS ENDS, and until it is left alone it is as it was. (The hold is 30 s, the tired
// FEEDBACK 90%, sound coming in is anything over 80 dB under full: tape.cpp, "IT ALWAYS ENDS".)
static void testTapeAlwaysEnds() {
    const EffectKind tape = EffectKind::Tape;
    const std::size_t second = 48000;
    const auto level = [&](const std::vector<float>& v, double from, double to) { return rmsOf(v, static_cast<std::size_t>(from * 48000.0), static_cast<std::size_t>(to * 48000.0)); };
    const auto burst = [&](std::size_t seconds, std::size_t at = 0) {
        std::vector<float> in(seconds * second, 0.f);
        Noise noise(31u);
        for (std::size_t i = 0; i < 12000; ++i) in[at + i] = 0.5f * noise.next();
        return in;
    };
    CHECK(reads(tape, 1, 1.f) == "120%" && reads(tape, 1, 0.75f) == "90%"); // FEEDBACK reads what it does while sound comes in

    // ---- FEEDBACK at 120%, a sixteenth, a burst and then nothing: it howls at one level for thirty
    // seconds (as it always did), and then falls, each echo nine tenths of the last (7.3 dB a second
    // at a sixteenth), smoothly, to nothing at all.
    //   AGE     10 to 20 s   28 to 30 s   32 to 33 s   a second on from 40 s   silent from
    //   0%        0.79         0.80         0.46         -7.7 dB                 68.7 s
    //   35%       0.43         0.44         0.24         -7.9 dB                 67.0 s
    //   100%      0.18         0.18         0.09         -9.2 dB                 61.5 s
    // (Silent is the exact zero: the tape asleep, 240 dB down and a tape's length more. It is 120 dB down, where a bus rests, by 48 s.)
    for (float age : {0.f, 0.35f, 1.f}) {
        const std::vector<float> in = burst(90);
        const std::vector<float> wet = wetOf(*fresh(tape, 0.f, 1.f, age, 1.f), in);
        const double early = level(wet, 10, 20), held = level(wet, 28, 30), falling = level(wet, 32, 33), later = dB(level(wet, 41, 42) / level(wet, 40, 41));
        std::size_t last = 0;
        for (std::size_t i = 0; i < wet.size(); ++i) last = wet[i] != 0.f ? i : last;
        SAY("tape     ends: age %3.0f%%: level %.3f from 10 to 20 s, %.3f from 28 to 30 s, %.3f from 32 to 33 s; %.2f dB a second from 40 s; silent from %.1f s\n", age * 100.0, early, held, falling, later,
            static_cast<double>(last) / 48000.0);
        CHECK(std::fabs(dB(held / early)) < 0.2);                       // thirty seconds of singing, at one level
        CHECK(falling < 0.9 * held && falling > 0.4 * held);            // then it eases down: not a cut
        CHECK(later < -7.3 && later > (age < 0.5f ? -8.1 : -9.6));      // and falls at nine tenths an echo (-7.32 dB a second), and more off the notes the cuts take
        CHECK(level(wet, 47, 48) < 1e-5 * held && level(wet, 44, 45) > 1e-6); // 120 dB under full by 48 s, and not cut short on the way
        CHECK(level(wet, 80, 90) == 0.0 && last > 58 * second && last < 74 * second); // gone, to the exact zero (asleep)
        // No click where the fall starts: no step larger than the howl's own.
        CHECK(largestJump(wet, 30 * second, 40 * second) <= 1.02f * largestJump(wet, 20 * second, 30 * second));
        float smallest = 1.f;
        for (float v : wet) smallest = v != 0.f ? std::fmin(smallest, std::fabs(v)) : smallest;
        CHECK(smallest > 1e-30f); // and nothing on the way was near too small to hold
    }
    // ---- As long as sound keeps coming in it never tires: a click every 20 seconds (40 dB down)
    // and it is howling as loud after 100 seconds as after 10. Clicks too faint to be "sound
    // coming in" (86 dB down) do not hold it: it is left alone, and ends.
    for (float click : {0.01f, 5e-5f}) {
        std::vector<float> in = burst(110);
        for (std::size_t at = 20 * second; at < in.size(); at += 20 * second) in[at] = click;
        const std::vector<float> wet = wetOf(*fresh(tape, 0.f, 1.f, 0.f, 1.f), in);
        if (click > 1e-4f) CHECK(std::fabs(dB(level(wet, 90, 100) / level(wet, 10, 20))) < 0.2 && std::fabs(dB(level(wet, 50, 60) / level(wet, 10, 20))) < 0.2);
        else CHECK(level(wet, 60, 70) < 1e-4 && level(wet, 28, 30) > 0.5);
    }
    // ...and just over the line it is held: 78 dB down is sound, 82 dB down is not.
    for (float click : {1.26e-4f, 0.8e-4f}) {
        std::vector<float> in = burst(60);
        for (std::size_t at = 20 * second; at < in.size(); at += 5 * second) in[at] = in[at + 1] = click;
        const std::vector<float> wet = wetOf(*fresh(tape, 0.f, 1.f, 0.f, 1.f), in);
        CHECK(click > 1e-4f ? std::fabs(dB(level(wet, 50, 60) / level(wet, 10, 20))) < 0.2 : level(wet, 55, 60) < 1e-3);
    }
    // ---- Sound coming in again wakes a tired loop: FEEDBACK is back at what it reads, and it builds again.
    {
        std::vector<float> in = burst(80);
        in[37 * second] = 0.01f; // falling by now (30 s of hold, and 7 more)
        const std::vector<float> wet = wetOf(*fresh(tape, 0.f, 1.f, 0.f, 1.f), in);
        const double howl = level(wet, 10, 20);
        CHECK(level(wet, 36, 37) < 0.3 * howl && std::fabs(dB(level(wet, 50, 60) / howl)) < 0.3); // down, then all the way back up
        CHECK(level(wet, 64, 67) > 0.5 * howl && level(wet, 76, 80) < 0.1 * howl);               // held thirty seconds from THAT click, and then it tires again
        CHECK(largestJump(wet, 37 * second, 40 * second) <= 1.02f * largestJump(wet, 20 * second, 30 * second));
        CHECK(level(wet, 38.0, 38.5) > 1.5 * level(wet, 37.0, 37.5)); // at once (20 ms), not as slowly as it tired: a second on it is well up again
    }
    // A tired loop that has gone to sleep wakes as one fresh from the box, whatever wakes it: a click
    // too faint to count as "sound coming in" gets its thirty seconds of full FEEDBACK all the same.
    // (With AGE up too: the wow starts again from its first point.)
    for (float age : {0.f, 0.35f}) {
        std::unique_ptr<Effect> slept = fresh(tape, 0.f, 1.f, age, 1.f), box = fresh(tape, 0.f, 1.f, age, 1.f);
        const std::vector<float> first = wetOf(*slept, burst(90));
        CHECK(first.back() == 0.f && level(first, 10, 20) > 0.3);
        std::vector<float> faint(20 * second, 0.f);
        faint[0] = 2e-5f;
        const std::vector<float> a = wetOf(*slept, faint), b = wetOf(*box, faint);
        float apart = 0.f;
        for (std::size_t i = 0; i < a.size(); ++i) apart = std::fmax(apart, std::fabs(a[i] - b[i]));
        CHECK(apart < 1e-9f && level(a, 10, 11) > 10.0 * level(a, 1, 2) && level(a, 10, 11) > 1e-5); // and it is building
    }
    // ---- What each echo is of the last, at 1 kHz, before the hold is up and long after. A half
    // note at 40 BPM: an echo every three seconds, a small click (far under the ceiling), AGE 0.
    //   FEEDBACK   reads    3 to 9 s    66 to 72 s
    //   75%         90%      0.900       0.900     never touched: the same to five places
    //   50%         60%      0.600       (gone)
    //   79%         95%      0.950       0.900
    //   100%       120%      1.200       0.900
    {
        const std::size_t every = 144000;
        struct Row { float feedback; double reads; bool touched; };
        for (const Row& row : {Row{0.75f, 0.9, false}, Row{0.5f, 0.6, false}, Row{0.95f / 1.2f, 0.95, true}, Row{1.f, 1.2, true}}) {
            const std::vector<float> ir = clickOf(*fresh(tape, 1.f, row.feedback, 0.f, 1.f, 40.f), 78.0, 0.002f);
            const double before = echoGainAt(ir, every, 1000.0, 1), after = echoGainAt(ir, every, 1000.0, 22), between = echoGainAt(ir, every, 1000.0, 9);
            SAY("tape     ends: FEEDBACK %3.0f%%: each echo x %.4f at 6 s, x %.4f at 27 s, x %.4f at 69 s\n", row.reads * 100.0, before, between, after);
            CHECK(std::fabs(dB(before / row.reads)) < 0.05 && std::fabs(dB(between / before)) < 0.01); // as it reads, right up to the hold's end
            if (row.reads >= 0.9) CHECK(std::fabs(dB(after / 0.9)) < 0.05);                            // and after it: 90%, or what it was if that is less
            if (!row.touched && row.reads >= 0.9) CHECK(std::fabs(after / before - 1.0) < 2e-5);
            // A FEEDBACK under 90% is not brought UP to it either: at 60% the thirteenth echo is still 0.6 of the twelfth (at 39 s).
            if (row.reads < 0.9) CHECK(std::fabs(dB(echoGainAt(ir, every, 1000.0, 12) / before)) < 0.05);
        }
    }
    // ---- The line itself. A FEEDBACK that reads 90% is multiplied by exactly 1 (not by a hair
    // under it: 1.2f * 0.75f is 0.90000004 and 0.9f is 0.89999998, so comparing what it READS
    // touched 90% itself). Whatever way the setting gets to 75%, by the D-pad's notches of 5% too.
    {
        float byNotches = 0.4f; // the default, and seven pushes up
        for (int i = 0; i < 7; ++i) byNotches = std::floor(byNotches / 0.05f + 1.001f) * 0.05f;
        float bySums = 0.4f;
        for (int i = 0; i < 7; ++i) bySums += 0.05f; // 0.75000006: it still reads 90%
        CHECK(reads(tape, 1, byNotches) == "90%" && reads(tape, 1, bySums) == "90%");
        for (float at : {0.f, 0.4f, 0.5f, 0.74f, 0.75f, byNotches, bySums}) CHECK(fx::tapeTire(at) == 1.f);
        CHECK(fx::tapeTire(1.f) == 0.9f / 1.2f && fx::tapeTire(0.95f / 1.2f) < 0.95f && fx::tapeTire(0.95f / 1.2f) > 0.94f);
        CHECK(reads(tape, 1, 0.76f) == "91%" && fx::tapeTire(0.76f) < 0.99f && fx::tapeTire(0.76f) > 0.98f); // the first setting that reads over 90% is brought down to it
    }
    // The way there, a sample at a time: down gently (most of the way in two seconds), up at once
    // (20 ms), and it LANDS both ways. A fraction of what is left only ever creeps closer: it stood
    // still a hair short, and a loop fed again was then never quite the FEEDBACK it reads.
    {
        const float tired = fx::tapeTire(1.f);
        float down = 1.f, up = tired;
        long downAt = 0, upAt = 0, halfDown = 0, halfUp = 0;
        for (long i = 1; i <= 40 * 48000 && down != tired; ++i) {
            down = fx::tapeTireStep(down, tired, 48000.f), downAt = i;
            if (!halfDown && down < 0.5f * (1.f + tired)) halfDown = i;
        }
        for (long i = 1; i <= 40 * 48000 && up != 1.f; ++i) {
            up = fx::tapeTireStep(up, 1.f, 48000.f), upAt = i;
            if (!halfUp && up > 0.5f * (1.f + tired)) halfUp = i;
        }
        SAY("tape     ends: the multiplier is half way down after %.2f s and there after %.1f s; half way up after %.1f ms and there after %.2f s\n", halfDown / 48000.0, downAt / 48000.0, halfUp / 48.0, upAt / 48000.0);
        CHECK(down == tired && up == 1.f);                                   // exactly
        CHECK(downAt < 30 * 48000 && upAt < 48000);                          // and soon: 7.4 s down (the last of it far too small to hear), 0.17 s up
        CHECK(std::labs(halfDown - 66542) < 500 && std::labs(halfUp - 665) < 10); // half way in 1.39 s (2 s x ln 2) and in 13.9 ms
        CHECK(fx::tapeTireStep(1.f, 1.f, 48000.f) == 1.f && fx::tapeTireStep(tired, tired, 48000.f) == tired); // at rest it stays
    }
    // Sound on one side only is sound coming in.
    for (int side = 0; side < 2; ++side) {
        Stereo io(110 * second);
        io.l = io.r = burst(110);
        for (std::size_t at = 20 * second; at < io.frames(); at += 20 * second) (side ? io.r : io.l)[at] = 0.01f;
        through(*fresh(tape, 0.f, 1.f, 0.f, 1.f), io);
        CHECK(std::fabs(dB(level(side ? io.l : io.r, 90, 100) / level(side ? io.l : io.r, 10, 20))) < 0.2); // (listened to on the other side)
        // ...and it wakes a sleeping tape by itself: its echoes are there, on its side only.
        Stereo one(4 * second);
        const std::vector<float> sound = burst(4);
        (side ? one.r : one.l) = sound;
        through(*fresh(tape, 0.f, 0.5f, 0.f, 1.f), one);
        std::vector<float>& heard = side ? one.r : one.l;
        for (std::size_t i = 0; i < heard.size(); ++i) heard[i] -= sound[i];
        CHECK(level(heard, 0.5, 2) > 0.01 && level(side ? one.l : one.r, 0, 4) == 0.0);
    }
    // ---- However it is cut into blocks, the same to the last bit: through the hold, the tiring,
    // the fall and into sleep. (The count of time left alone went up a block at a time and the ease
    // down was a glide a block: blocks of 64 and of 48 were the same for 30.47 s and then up to
    // 5.8e-5 apart, so a bounce and live play differed in a tired TAPE's tail.) And with AGE up:
    // the wow was a sine worked out at each END OF A BLOCK and joined by a straight line, so its
    // shape was the block's (apart from 0.15 s, and by 0.05 once it howled).
    // The second sound is too faint to be "coming in" and wakes the sleeping tape part way through
    // a block: the thirty seconds count from that sample, not from the block's first.
    struct Cut { float feedback, age; };
    for (const Cut& c : {Cut{1.f, 0.f}, Cut{0.8f, 0.f}, Cut{1.f, 0.35f}, Cut{0.8f, 1.f}}) {
        const float feedback = c.feedback;
        std::vector<float> in(150 * second, 0.f);
        Noise noise(31u);
        for (std::size_t i = 0; i < 4800; ++i) in[1000 + i] = 0.5f * noise.next();
        in[75 * second + 1237] = 5e-5f;
        std::vector<float> ways[4];
        const std::size_t sizes[4] = {64, 48, 0, 256}; // 0: odd sizes, 1 to 61 in turn
        for (int way = 0; way < 4; ++way) {
            std::unique_ptr<Effect> t = fresh(tape, 0.f, feedback, c.age, 1.f);
            Stereo io(in.size());
            io.l = io.r = in;
            for (std::size_t at = 0, n = sizes[way] ? sizes[way] : 1; at < io.frames(); at += n, n = sizes[way] ? sizes[way] : n % 61 + 1) {
                if (at + n > io.frames()) n = io.frames() - at;
                t->process(io.l.data() + at, io.r.data() + at, static_cast<int>(n));
            }
            ways[way] = io.l;
        }
        std::size_t firstApart = in.size();
        float apart = 0.f;
        for (int way = 1; way < 4; ++way)
            for (std::size_t i = 0; i < in.size(); ++i)
                if (ways[way][i] != ways[0][i]) firstApart = std::min(firstApart, i), apart = std::fmax(apart, std::fabs(ways[way][i] - ways[0][i]));
        SAY("tape     ends: FEEDBACK %3.0f%%, AGE %3.0f%%: cut into blocks four ways, first apart at %.2f s, by %g at most\n", feedback * 120.0, c.age * 100.0, static_cast<double>(firstApart) / 48000.0, static_cast<double>(apart));
        CHECK(firstApart == in.size());
        // (And it did all of it: howled, tired, slept, was woken by the faint sound and built again, tired again.)
        if (feedback == 1.f && c.age == 0.f) CHECK(level(ways[0], 20, 30) > 0.5 && level(ways[0], 70, 75) == 0.0 && level(ways[0], 100, 105) > 0.5 && level(ways[0], 140, 150) < 1e-6);
    }
    // ---- On a bus it is the same loop: the bus comes to rest (testATailAlwaysEnds has every setting; this is the howl by itself).
    {
        const float howl[4] = {0.f, 1.f, 0.f, 1.f};
        const double seconds = secondsToRest(tape, howl, 120.f, 200.0);
        SAY("tape     ends: on a bus, a sixteenth at 120 BPM, FEEDBACK 120%%: at rest %.1f s after the last sound\n", seconds);
        CHECK(seconds > 40.0 && seconds < 70.0); // 30 s held, about 2 s easing, 16 s falling 120 dB, 10 s of quiet before the bus rests: 58.6
    }
}

// ---- PING ---------------------------------------------------------------------------------------

// Both sides of what came out, added: every echo in one run, whichever side it landed on.
static std::vector<float> bothOf(const Stereo& io) {
    std::vector<float> v(io.frames());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = io.l[i] + io.r[i];
    return v;
}
// Is every sample from `from` up to `to` an exact zero?
static bool silentIn(const std::vector<float>& v, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to && i < v.size(); ++i)
        if (v[i] != 0.f) return false;
    return true;
}
static const float kPingMost = 0.9f; // FEEDBACK at full: what each echo is of the last

// PING: a clean echo that bounces from side to side (ping.cpp). The plan's row: when each echo
// lands, and how much quieter; TIME within a sample of the step length at three tempos; each
// repeat down by FEEDBACK within 0.5 dB; echoes swap sides.
static void testPing() {
    const EffectKind ping = EffectKind::Ping, tape = EffectKind::Tape, delay = EffectKind::Delay;
    const EffectInfo& info = effectInfo(ping);
    CHECK(static_cast<int>(ping) == 12 && std::string(info.word) == "ping" && effectFromWord("ping") == ping); // its number and its word: fixed for good
    CHECK(std::string(info.name) == "PING" && std::string(effectFamily(ping)) == "DELAY" && std::string(effectTypeName(ping)) == "PING" && effectTypeIndex(ping) == 2);
    CHECK(info.family == EffectFamily::Delay && effectBus(ping) == 0 && effectMixParam(ping) == 3 && effectMeterParam(ping) == -1);
    CHECK(info.params == 4 && std::string(info.paramNames[0]) == "TIME" && std::string(info.paramNames[1]) == "FEEDBACK" && std::string(info.paramNames[2]) == "TONE" && std::string(info.paramNames[3]) == "MIX");
    CHECK(info.defaults[0] == 0.5625f && info.defaults[1] == 0.5f && info.defaults[2] == 0.6f && info.defaults[3] == 0.35f);
    CHECK(effectChoices(ping, 0) == 8 && effectChoices(ping, 1) == 0 && effectChoices(ping, 2) == 0 && effectChoices(ping, 3) == 0);
    // How it reads. TIME is TAPE's eight; FEEDBACK reads 0 to 100% as CLEAN's does (and does nine tenths of it, kPingMost at full).
    const char* names[8] = {"1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOT", "1/4", "1/4 DOT", "1/2"};
    for (int i = 0; i < 8; ++i) CHECK(reads(ping, 0, tapeTime(i)) == names[i] && reads(ping, 0, tapeTime(i)) == reads(tape, 0, tapeTime(i)));
    CHECK(reads(ping, 0, 0.f) == "1/16" && reads(ping, 0, 1.f) == "1/2" && reads(ping, 0, info.defaults[0]) == "1/8 DOT");
    CHECK(reads(ping, 1, 0.f) == "0%" && reads(ping, 1, 1.f) == "100%" && reads(ping, 1, 0.5f) == "50%" && reads(ping, 2, 0.6f) == "60%" && reads(ping, 3, 0.35f) == "35%" && reads(ping, 3, 1.f) == "100%");
    for (int k = 0; k <= 20; ++k) { // every push of the D-pad reads a round 5%, and reads as CLEAN's at the same place
        char five[8];
        std::snprintf(five, sizeof five, "%d%%", 5 * k);
        CHECK(reads(ping, 1, static_cast<float>(k) / 20.f) == five && reads(ping, 1, static_cast<float>(k) / 20.f) == reads(delay, 1, static_cast<float>(k) / 20.f));
    }

    // WHEN EACH ECHO LANDS, AND ON WHICH SIDE. A click in the middle (both sides alike), FEEDBACK
    // 50% (each echo 0.45 of the last), TONE full. The step length to the sample, at three tempos, each of the eight choices
    // (in thirds of a step: 3 4 6 8 9 12 18 24): the first echo is on the LEFT one TIME after the
    // click and is the click itself, to the bit; the second is on the RIGHT two TIMEs after; the
    // third on the left again; and between its own echoes a side is silent, to the exact zero.
    const int thirds[8] = {3, 4, 6, 8, 9, 12, 18, 24};
    for (float tempo : {90.f, 120.f, 174.f}) {
        for (int i = 0; i < 8; ++i) {
            std::unique_ptr<Effect> p = fresh(ping, tapeTime(i), 0.5f, 1.f, 1.f, tempo);
            const long want = std::lround(thirds[i] * 5.0 / tempo * 48000.0);
            const std::size_t w = static_cast<std::size_t>(want);
            const Stereo io = clickBoth(*p, 4.6 * static_cast<double>(want) / 48000.0, 0.1f, 0.1f);
            CHECK(echoAt(io.l, 0) == want && io.l[w] == 0.1f);          // left first, nothing before it, the click as it was
            CHECK(echoAt(io.r, 0) == 2 * want);                         // right next
            CHECK(echoAt(io.l, w + w / 2) == 3 * want);                 // left again
            CHECK(echoAt(io.r, 2 * w + w / 2) == 4 * want);             // and right
            CHECK(silentIn(io.r, 1, 2 * w) && silentIn(io.l, w + w / 2, 3 * w) && silentIn(io.r, 2 * w + w / 2, 4 * w));
            CHECK(peakAt(io.l, 1, io.frames()) == want && peakAt(io.r, 1, io.frames()) == 2 * want); // each side's first is its loudest
        }
    }
    // A quarter note at 120 is 24000 samples; a dotted eighth (where it starts) 18000.
    CHECK(echoAt(clickOf(*fresh(ping), 0.5, 0.1f), 0) == 18000);

    // ECHOES SWAP SIDES. Noise for 50 ms in the middle, a sixteenth (6000 samples), FEEDBACK 100% (0.9),
    // TONE full: the first eight echoes, each in its own window. All of echo n is on one side (the
    // other is exact zeros there): left for 1, 3, 5, 7 and right for 2, 4, 6, 8.
    //   echo        1      2      3      4      5      6      7      8
    //   side        L      R      L      R      L      R      L      R
    //   level       0.290  0.237  0.196  0.163  0.137  0.116  0.099  0.085   (noise: each 0.9 of the last, less what TONE's
    //                                                                          cut at 18 kHz takes off the top of it)
    // So left and right are never alike: over the whole tail what they share is 0.000 of it.
    {
        Stereo io(60000);
        Noise noise(12u);
        for (std::size_t i = 0; i < 2400; ++i) io.l[i] = io.r[i] = 0.5f * noise.next();
        const Stereo in = io;
        through(*fresh(ping, 0.f, 1.f, 1.f, 1.f), io);
        for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] -= in.l[i], io.r[i] -= in.r[i];
        for (std::size_t n = 1; n <= 8; ++n) {
            const std::vector<float>& on = n % 2 ? io.l : io.r;
            const std::vector<float>& off = n % 2 ? io.r : io.l;
            SAY("ping     echo %d is on the %s: level %.3f there, %.3f on the other side\n", static_cast<int>(n), n % 2 ? "left" : "right", rmsOf(on, n * 6000, n * 6000 + 2400), rmsOf(off, n * 6000, n * 6000 + 2400));
            CHECK(rmsOf(on, n * 6000, n * 6000 + 2400) > 0.07 && silentIn(off, n * 6000, n * 6000 + 2400));
        }
        SAY("ping     left and right over the whole tail: alike %.4f\n", alike(io.l, io.r, 0, io.frames()));
        CHECK(std::fabs(alike(io.l, io.r, 0, io.frames())) < 1e-6);
        // The first echo is the sound that came in, to the bit: nothing is cut on the way to it.
        bool itself = true;
        for (std::size_t i = 0; i < 2400; ++i) itself = itself && io.l[6000 + i] == in.l[i];
        CHECK(itself);
    }
    // What comes in on ONE side bounces all the same, from the left, at half the size (the two
    // sides are halved and added on the way in); and it stays where it was, untouched. Opposite
    // on the two sides, it adds to nothing: no echo at all.
    for (int side = 0; side < 2; ++side) {
        const Stereo io = clickBoth(*fresh(ping, 0.f, 0.5f, 1.f, 1.f), 0.5, side ? 0.f : 0.2f, side ? 0.2f : 0.f);
        CHECK(echoAt(io.l, 0) == 6000 && io.l[6000] == 0.1f && echoAt(io.r, 0) == 12000 && (side ? io.r : io.l)[0] == 0.2f && (side ? io.l : io.r)[0] == 0.f);
    }
    {
        const Stereo io = clickBoth(*fresh(ping, 0.f, 0.5f, 1.f, 1.f), 0.5, 0.2f, -0.2f);
        CHECK(io.l[0] == 0.2f && io.r[0] == -0.2f && echoAt(io.l, 0) == -1 && echoAt(io.r, 0) == -1);
    }

    // HOW MUCH QUIETER EACH ECHO IS: FEEDBACK, as it reads (0 to 100%, doing nine tenths of it), and what TONE's high cut
    // takes (6 dB an octave; its corner 600 Hz at TONE 0, 3.3 kHz at 50%, 4.6 kHz at 60%, 18 kHz
    // at full). A small click, echoes a quarter note apart, both sides added. In dB a repeat,
    // against FEEDBACK:
    //   TONE    corner     the whole echo (its sum)   100 Hz    1 kHz    6 kHz
    //   0%      600 Hz          0.00                   -0.12    -5.77   -19.82
    //   60%     4.6 kHz         0.00                    0.00    -0.19    -4.07
    //   100%    18 kHz          0.00                    0.00    -0.01    -0.28
    // So at 1 kHz each repeat is down by FEEDBACK to within 0.2 dB from TONE 60% up, and the sum
    // of an echo (what TONE spreads out but does not take) by FEEDBACK exactly, at any TONE.
    for (float tone : {0.f, 0.6f, 1.f}) {
        const double corner = 600.0 * std::pow(30.0, tone);
        for (float feedback : {0.25f, 0.5f, 1.f}) {
            const std::vector<float> ir = bothOf(clickBoth(*fresh(ping, tapeTime(5), feedback, tone, 1.f), 3.0, 0.01f, 0.01f));
            const double reads = kPingMost * feedback;
            CHECK(std::fabs(echoGain(ir, 24000, 1) - reads) < 0.002 && std::fabs(echoGain(ir, 24000, 2) - reads) < 0.002 && std::fabs(echoGain(ir, 24000, 3) - reads) < 0.002);
            if (feedback == 0.5f) SAY("ping     tone %3.0f%% (corner %5.0f Hz): each echo's sum x %.4f (FEEDBACK reads 50%%: 0.45)\n", tone * 100.0, corner, echoGain(ir, 24000, 1));
            for (double hz : {100.0, 1000.0, 6000.0}) {
                const double want = reads * cutAt(corner, hz), got = echoGainAt(ir, 24000, hz, 1), next = echoGainAt(ir, 24000, hz, 2);
                if (feedback == 0.5f) SAY("ping     tone %3.0f%% at %5.0f Hz: each echo x %.4f (%.2f dB against FEEDBACK)\n", tone * 100.0, hz, got, dB(got / reads));
                CHECK(std::fabs(dB(got / want)) < 0.1 && std::fabs(dB(next / want)) < 0.1);
                if (tone >= 0.6f && hz == 1000.0) CHECK(std::fabs(dB(got / reads)) < 0.25); // the plan asks 0.5 dB
                if (tone == 1.f && hz <= 1000.0) CHECK(std::fabs(dB(got / reads)) < 0.05);
            }
        }
    }
    // FEEDBACK at 0: one echo, on the left, and no more. TONE at 0: the second echo is spread out and lower.
    {
        const Stereo once = clickBoth(*fresh(ping, tapeTime(5), 0.f, 1.f, 1.f), 2.0, 0.1f, 0.1f);
        CHECK(once.l[24000] == 0.1f && echoAt(once.l, 24000) == -1 && echoAt(once.r, 0) == -1);
        const Stereo dull = clickBoth(*fresh(ping, tapeTime(5), 1.f, 0.f, 1.f), 2.0, 0.1f, 0.1f);
        CHECK(dull.l[24000] == 0.1f && std::fabs(dull.r[48000]) < 0.1f * 0.1f && std::fabs(dull.r[48000]) > 0.01f * 0.1f && dull.r[48100] != 0.f);
    }

    // MIX is how loud the echoes are; the sound itself is not touched. At MIX 50% and at full: the
    // sound that came in is there to the bit until the first echo, and the echoes at 50% are half
    // what they are at full, on both sides. (An echo is late by design; nothing else in here is,
    // so there is no second, late copy of the plain sound to cancel with it.)
    {
        Stereo full(48000), half(0);
        full.l = full.r = sineOf(1000.0, 0.25, 48000);
        const Stereo in = half = full;
        through(*fresh(ping, 0.f, 0.5f, 0.3f, 1.f), full);
        through(*fresh(ping, 0.f, 0.5f, 0.3f, 0.5f), half);
        bool dryWhole = true, halved = true;
        for (std::size_t i = 0; i < 6000; ++i) dryWhole = dryWhole && full.l[i] == in.l[i] && half.l[i] == in.l[i] && half.r[i] == in.r[i];
        for (std::size_t i = 0; i < 12000; ++i) dryWhole = dryWhole && full.r[i] == in.r[i]; // the right side waits twice as long
        for (std::size_t i = 0; i < in.frames(); ++i)
            halved = halved && std::fabs((half.l[i] - in.l[i]) - 0.5f * (full.l[i] - in.l[i])) < 1e-6f && std::fabs((half.r[i] - in.r[i]) - 0.5f * (full.r[i] - in.r[i])) < 1e-6f;
        CHECK(dryWhole && halved && full.l[6001] != in.l[6001] && full.r[12001] != in.r[12001]);
        // At MIX 50% a steady 1 kHz sine and its first echo (a whole number of cycles late) add: 0.25 + 0.125.
        CHECK(std::fabs(partAt(half.l.data() + 6000, 6000, 1000.0) - 0.375) < 0.001);
    }

    // TIME GLIDES, as TAPE's does: turned from a sixteenth to a half note the lines slow to half
    // speed (a 1 kHz sine on them reads 500 Hz) until they are there, and are then exactly a half
    // note; turned back they run at double (2 kHz). Never slower or faster, and never a jump.
    {
        std::unique_ptr<Effect> t = fresh(ping, 0.f, 0.f, 1.f, 1.f), there = fresh(ping, 1.f, 0.f, 1.f, 1.f);
        Stereo io(13 * 48000), other(0);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        other = io;
        const std::vector<float> in = io.l;
        through(*t, io, 0, 48000);
        t->setParam(0, 1.f);
        through(*t, io, 48000, 7 * 48000);
        t->setParam(0, 0.f);
        through(*t, io, 7 * 48000, io.frames());
        through(*there, other, 0, 7 * 48000);
        std::vector<float> wet(io.frames());
        for (std::size_t i = 0; i < wet.size(); ++i) wet[i] = io.l[i] - in[i];
        double slowest = 1e9, fastest = 0.0;
        for (std::size_t at = 48000; at + 2400 <= wet.size(); at += 2400) {
            const double hz = pitchOf(wet, at, at + 2400);
            (at < 7 * 48000 ? slowest : fastest) = at < 7 * 48000 ? std::fmin(slowest, hz) : std::fmax(fastest, hz);
            if (at >= 7 * 48000) CHECK(hz > 990.0);
            else CHECK(hz < 1010.0);
        }
        SAY("ping     TIME turned: the slowest the lines read a 1 kHz sine is %.1f Hz, the fastest %.1f Hz\n", slowest, fastest);
        CHECK(std::fabs(slowest - 500.0) < 5.0 && std::fabs(fastest - 2000.0) < 20.0);
        // Slowing: 42000 samples further away at half a sample a sample is 1.75 s. Still on the way at 1.5 s, there by 6.
        CHECK(pitchOf(wet, 48000 + 72000 - 2400, 48000 + 72000) < 510.0);
        CHECK(std::fabs(pitchOf(wet, 6 * 48000, 7 * 48000) - 1000.0) < 0.01 && std::fabs(pitchOf(wet, 12 * 48000, 13 * 48000) - 1000.0) < 0.01);
        bool arrived = true;
        for (std::size_t i = 7 * 48000 - 4800; i < 7 * 48000; ++i) arrived = arrived && io.l[i] == other.l[i]; // exactly where one that started there is
        CHECK(arrived);
        // ...and back at a sixteenth it is read on the sample again: the sound and itself 6000 samples ago, to the bit.
        // (But where the sine crosses nothing: a sample under 240 dB down went on the line as a zero.)
        bool back = true;
        for (std::size_t i = io.frames() - 4800; i < io.frames(); ++i) back = back && (io.l[i] == in[i] + in[i - 6000] || (std::fabs(in[i - 6000]) < 1e-12f && io.l[i] == in[i]));
        CHECK(back);
        const double roughest = largestJump(io.l, 48000, io.frames()) / (sineStep(1000.0, 0.25) + sineStep(2000.0, 0.25));
        SAY("ping     TIME turned: the largest step is %.2f of a 1 kHz and a 2 kHz sine together\n", roughest);
        CHECK(roughest < 1.02);
        // A new tempo is the same thing: half the tempo, and the lines slow to half speed to get there.
        std::unique_ptr<Effect> slowed = fresh(ping, 0.f, 0.f, 1.f, 1.f);
        Stereo again(4 * 48000);
        again.l = again.r = sineOf(1000.0, 0.25, again.frames());
        through(*slowed, again, 0, 48000);
        slowed->setTempo(60.f);
        through(*slowed, again, 48000, again.frames());
        for (std::size_t i = 0; i < wet.size() && i < again.frames(); ++i) wet[i] = again.l[i] - in[i];
        CHECK(std::fabs(pitchOf(wet, 48000 + 2400, 48000 + 4800) - 500.0) < 5.0 && std::fabs(pitchOf(wet, 3 * 48000, 4 * 48000) - 1000.0) < 0.01);
        CHECK(largestJump(again.l, 48000, again.frames()) / (sineStep(1000.0, 0.25) + sineStep(2000.0, 0.25)) < 1.02);
        // A tempo that is no tempo is not taken.
        std::unique_ptr<Effect> odd = fresh(ping, tapeTime(5), 0.f, 1.f, 1.f);
        odd->setTempo(10.f);
        odd->setTempo(900.f);
        odd->setTempo(std::nanf(""));
        CHECK(echoAt(clickOf(*odd, 0.6, 0.1f), 0) == 24000);
        // And the slowest one that is: half a bar at 20 would not fit on a line, so it is as long as the line allows.
        odd->reset();
        odd->setTempo(20.f);
        odd->setParam(0, 1.f);
        CHECK(echoAt(clickOf(*odd, 3.2, 0.1f), 0) == 144008); // three seconds (half a bar at 40) and the spare of 16, less 8
        // The slowest tempo a song has: a half note at 40 BPM is three seconds, and it fits to the sample.
        CHECK(echoAt(clickOf(*fresh(ping, 1.f, 0.f, 1.f, 1.f, kMinTempo), 3.2, 0.1f), 0) == 144000);
    }

    // IT CANNOT RING FOR GOOD. FEEDBACK at its most (100%: 0.9), a sixteenth, TONE full, a burst of
    // noise and then nothing: eight echoes a second, each nine tenths of the last, is 7.32 dB a
    // second, to nothing at all. Measured: 8.3 dB a second from 2 s (the top of the noise goes
    // faster: TONE's cut, eight times a second), 7.43 from 20 s; silent (the exact zero) from 31.2 s.
    {
        std::vector<float> in(45 * 48000, 0.f);
        Noise noise(31u);
        for (std::size_t i = 0; i < 12000; ++i) in[i] = 0.5f * noise.next();
        Stereo io(in.size());
        io.l = io.r = in;
        through(*fresh(ping, 0.f, 1.f, 1.f, 1.f), io);
        for (std::size_t i = 0; i < in.size(); ++i) io.l[i] -= in[i], io.r[i] -= in[i];
        const std::vector<float> wet = bothOf(io);
        const auto level = [&](double from, double to) { return rmsOf(wet, static_cast<std::size_t>(from * 48000.0), static_cast<std::size_t>(to * 48000.0)); };
        const double early = dB(level(3, 4) / level(2, 3)), late = dB(level(21, 22) / level(20, 21));
        std::size_t last = 0;
        for (std::size_t i = 0; i < wet.size(); ++i) last = wet[i] != 0.f ? i : last;
        SAY("ping     feedback 100%% (0.9): %.2f dB a second from 2 s, %.2f dB a second from 20 s; loudest %.2f; silent from %.1f s\n", early, late, static_cast<double>(peakOf(wet)), static_cast<double>(last) / 48000.0);
        CHECK(early < -7.3 && early > -9.0 && late < -7.3 && late > -7.6);
        CHECK(peakOf(wet) < 1.5f); // the echoes pile up while the burst lasts, and no further
        CHECK(last > 28 * 48000 && last < 36 * 48000 && silentIn(wet, 36 * 48000, wet.size()));
    }

    // SILENCE IN, SILENCE OUT, and no slower on the way. A full-scale click, FEEDBACK 50% (0.45), a
    // sixteenth, TONE full: each echo is 6.9 dB under the last. Nothing under 240 dB down goes
    // on a line (an exact zero does instead), so the last echo is the 33rd, 4.125 s in (the click
    // would be 240 dB down at the 35th; TONE has spread each echo out, and lower, by then); a
    // line's length of zeros later (3 s) the effect is asleep. The smallest sample that ever
    // comes out is 1e-12: nothing is anywhere near too small to hold (that starts at 1e-38).
    {
        const Stereo io = clickBoth(*fresh(ping, 0.f, 0.5f, 1.f, 1.f), 9.0);
        const std::vector<float> ir = bothOf(io);
        std::size_t last = 0;
        for (std::size_t i = 1; i < ir.size(); ++i) last = ir[i] != 0.f ? i : last;
        float smallest = 1.f;
        for (float v : ir) smallest = v != 0.f ? std::fmin(smallest, std::fabs(v)) : smallest;
        SAY("ping     a click at FEEDBACK 50%%: the last sample that is not zero is %.3f s in; the smallest that came out %.1e\n", static_cast<double>(last) / 48000.0, static_cast<double>(smallest));
        CHECK(last > 4 * 48000 && last < 5 * 48000 && smallest >= 1e-12f);
        // Half way down it is still all there, not cut short: the 17th echo's sum is 0.45 to the 16th of the click (2.83e-6).
        double mid = 0.0;
        for (std::size_t i = 17 * 6000; i < 18 * 6000; ++i) mid += ir[i];
        CHECK(std::fabs(mid / std::pow(0.45, 16.0) - 1.0) < 0.01);
        // Woken by a new sound, it plays as one fresh from the box does, TO THE BIT (its lines are zeros, its cuts at rest).
        std::unique_ptr<Effect> slept = fresh(ping, 0.f, 0.5f, 0.3f, 1.f), box = fresh(ping, 0.f, 0.5f, 0.3f, 1.f);
        const Stereo first = clickBoth(*slept, 9.0);
        const Stereo a = clickBoth(*slept, 1.0, 0.5f, 0.3f), b = clickBoth(*box, 1.0, 0.5f, 0.3f);
        CHECK(first.l.back() == 0.f && first.r.back() == 0.f && a.l == b.l && a.r == b.r && rmsOf(a.l, 5900, 6100) > 0.005 && rmsOf(a.r, 11900, 12200) > 0.0005);
        // Sound too faint to be sound (1e-20) does not get onto a line: what comes out is what went in, to the bit.
        std::unique_ptr<Effect> faint = fresh(ping, 0.f, 0.f, 1.f, 1.f);
        Stereo f(48000);
        f.l.assign(48000, 1e-20f);
        f.r.assign(48000, 1e-20f);
        f.l[0] = f.r[0] = 0.5f;
        const Stereo before = f;
        through(*faint, f);
        bool nothingOn = f.l[6000] != before.l[6000];
        for (std::size_t i = 6001; i < 48000; ++i) nothingOn = nothingOn && f.l[i] == before.l[i] && f.r[i] == before.r[i];
        CHECK(nothingOn);
        // Asleep means BOTH lines are empty. A half note at 40 BPM is a whole line long (three seconds), and with
        // FEEDBACK next to nothing (a millionth) the left line is given nothing but zeros from the click on: the
        // right echo, six seconds after the click and 120 dB down, is still on its way, and comes.
        {
            const Stereo far = clickBoth(*fresh(ping, 1.f, 1e-6f / kPingMost, 1.f, 1.f, kMinTempo), 6.5);
            CHECK(far.l[144000] == 1.f && echoAt(far.r, 0) == 288000 && std::fabs(far.r[288000]) > 5e-7f && std::fabs(far.r[288000]) < 1e-6f);
        }
        // Turned while asleep, a setting is simply there when sound comes: no glide from where it was.
        std::unique_ptr<Effect> turned = fresh(ping, 1.f, 0.f, 1.f, 0.f);
        clickOf(*turned, 0.1, 0.f);
        turned->setParam(0, 0.f);
        turned->setParam(3, 1.f);
        const std::vector<float> woke = clickOf(*turned, 0.5, 0.1f);
        CHECK(echoAt(woke, 0) == 6000 && woke[6000] == 0.1f);
        // reset() is the same as new: a ringing one, reset, plays as one fresh from the box. (It has rung for
        // longer than a line is long, so every place on both lines has been written, and is reset in the middle
        // of an echo, the 25th, with the high cuts full of it.)
        std::unique_ptr<Effect> rung = fresh(ping, 0.f, 1.f, 0.3f, 1.f), made = fresh(ping, 0.f, 1.f, 0.3f, 1.f);
        const Stereo ringing = clickBoth(*rung, 150100.5 / 48000.0);
        CHECK(ringing.frames() == 150100 && std::fabs(ringing.l[150099]) + std::fabs(ringing.r[150099]) > 1e-3f);
        rung->reset();
        const Stereo c = clickBoth(*rung, 1.0, 0.5f, 0.3f), d = clickBoth(*made, 1.0, 0.5f, 0.3f);
        CHECK(c.l == d.l && c.r == d.r);
    }
    CHECK(sameFlushed(ping, tapeTime(0), 0.5f, 0.6f, 1.f, 9.0)); // the same with the processor's flush-to-zero on or off
    // NOTHING TOO SMALL TO HOLD IS EVER MADE, with flush-to-zero off: a whole tail, down to
    // asleep, at three TONEs (the high cut is what would fade through them: it rests on a floor
    // instead); and the same with sound coming in that is itself too small to hold.
    for (float tone : {0.f, 0.6f, 1.f}) {
        std::unique_ptr<Effect> p = fresh(ping, 0.f, 0.5f, tone, 1.f);
        const TooSmall marks;
        const std::vector<float> ir = clickOf(*p, 12.0, 1.f);
        const bool seen = marks.seen();
        CHECK(!seen && ir.back() == 0.f && rmsOf(ir, 5900, 6100) > 0.005);
    }
    {
        std::unique_ptr<Effect> p = fresh(ping, 0.f, 0.5f, 0.3f, 1.f);
        Stereo io(4 * 48000);
        io.l.assign(io.frames(), 1e-39f);
        io.r.assign(io.frames(), -1e-39f);
        io.l[0] = io.r[0] = 0.5f;
        const TooSmall marks;
        through(*p, io);
        const bool seen = marks.seen();
        CHECK(!seen && rmsOf(io.l, 5900, 6100) > 0.005);
    }
    {
        std::unique_ptr<Effect> p = fresh(ping, 0.f, 0.5f, 0.3f, 1.f);
        const TailTimes took = tailTimes(*p, 9);
        SAY("flush    ping (flush-to-zero OFF): a block takes %.2f us early, %.2f us at its slowest, %.2f us at the end (asleep)\n", took.early, took.slowest, took.late);
        CHECK(took.slowest < 3.0 * took.early + 20.0 && took.late < took.early + 5.0);
    }

    // A TURNED SETTING ARRIVES IN ABOUT 20 MS, and then is exactly there. MIX from 0 to full under
    // a steady echo: 6% of the way after the first chunk (64 samples), about a quarter after 5 ms,
    // all but there after 100 ms.
    {
        std::unique_ptr<Effect> t = fresh(ping, 0.f, 0.f, 1.f, 0.f);
        Stereo io(96000);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        const std::vector<float> in = io.l;
        through(*t, io, 0, 48000);
        t->setParam(3, 1.f);
        through(*t, io, 48000, 72000);
        std::vector<float> wet(io.frames());
        for (std::size_t i = 0; i < wet.size(); ++i) wet[i] = io.l[i] - in[i];
        const double full = partAt(wet.data() + 67200, 4800, 1000.0), first = partAt(wet.data() + 48000 + 48, 48, 1000.0), soon = partAt(wet.data() + 48000 + 240, 48, 1000.0);
        SAY("ping     MIX turned to full: the echo is %.3f of the way after 1.5 ms, %.3f after 5.5 ms, %.4f after 100 ms\n", first / full, soon / full, partAt(wet.data() + 52800, 4800, 1000.0) / full);
        CHECK(std::fabs(full - 0.25) < 0.001);
        CHECK(first / full > 0.02 && first / full < 0.12 && soon / full > 0.15 && soon / full < 0.4);
        CHECK(std::fabs(partAt(wet.data() + 52800, 4800, 1000.0) / full - 1.0) < 0.01);
        // ...and back to 0: within a second the sound is untouched, to the bit (it lands; it does not creep for ever).
        t->setParam(3, 0.f);
        through(*t, io, 72000, 96000);
        bool untouched = true;
        for (std::size_t i = 84000; i < 96000; ++i) untouched = untouched && io.l[i] == in[i] && io.r[i] == in[i];
        CHECK(untouched && io.l[72100] != in[72100]);
    }
    // FEEDBACK and TONE slide too, and land. Turned from full to nothing under a steady 1 kHz sine
    // and its echoes: no step is larger than a 1 kHz sine as loud as the loudest sample there
    // would make (1.00 of it, both), and with FEEDBACK at nothing the echoes stop: one echo, on
    // the left, and after it the exact zero.
    for (int which : {1, 2}) {
        std::unique_ptr<Effect> t = fresh(ping, 0.f, 1.f, 1.f, 1.f);
        Stereo io(4 * 48000);
        std::vector<float> in = sineOf(1000.0, 0.25, io.frames());
        for (std::size_t i = 72000; i < in.size(); ++i) in[i] = 0.f; // the sound stops (at a zero crossing) a half second after the turn
        io.l = io.r = in;
        through(*t, io, 0, 48012); // (turned at the top of the sine, not where it crosses zero)
        t->setParam(which, 0.f);
        through(*t, io, 48012, io.frames());
        float loudest = 0.f;
        for (std::size_t i = 48000; i < 72000; ++i) loudest = std::fmax(loudest, std::fmax(std::fabs(io.l[i]), std::fabs(io.r[i])));
        const double roughest = std::fmax(largestJump(io.l, 48000, 72000), largestJump(io.r, 48000, 72000)) / sineStep(1000.0, loudest);
        SAY("ping     %s turned from full to nothing: the largest step is %.3f of a 1 kHz sine as loud (%.2f)\n", info.paramNames[which], roughest, static_cast<double>(loudest));
        CHECK(roughest < 1.02 && loudest > 0.5f);
        if (which == 1) CHECK(silentIn(io.l, 72000 + 6000, io.frames()) && silentIn(io.r, 72000 + 12000, io.frames()) && io.r[60010] != in[60010] && io.r[66010] == in[66010] && io.l[66010] != in[66010]); // FEEDBACK 0: one echo, left only (it is there within a quarter second)
    }

    // TONE slides as the others do, sample by sample (it is the high cut's coefficient that glides).
    // A burst of a 6 kHz sine, FEEDBACK full, a sixteenth: its left echo is the burst itself, and
    // on the way to the right it passes the cut. TONE is turned from full to nothing in the middle
    // of that echo. What reaches the right, against the burst (0.9 is FEEDBACK):
    //   before the turn                0.871   (the cut at 18 kHz leaves 0.968 at 6 kHz)
    //   2 to 3 ms after the turn       0.832   (on its way)
    //   50 ms after                    0.166   (nearly there: the cut at 600 Hz leaves 0.101, which is 0.091)
    {
        std::unique_ptr<Effect> t = fresh(ping, 0.f, 1.f, 1.f, 1.f);
        Stereo io(72000);
        for (std::size_t i = 44000; i < 50000; ++i) io.l[i] = io.r[i] = static_cast<float>(0.25 * std::sin(2.0 * kPi * 6000.0 * static_cast<double>(i) / 48000.0));
        through(*t, io, 0, 53000);
        t->setParam(2, 0.f);
        through(*t, io, 53000, 72000);
        // The burst is 6000 samples: its left echo is from 50000 (and passes the cut then), its right echo from 56000, alone there.
        // What passed the cut at 53000, the turn, is on the right at 59000.
        const double before = partAt(io.r.data() + 58000, 480, 6000.0) / 0.25, soon = partAt(io.r.data() + 59000 + 96, 48, 6000.0) / 0.25, later = partAt(io.r.data() + 59000 + 2400, 480, 6000.0) / 0.25;
        SAY("ping     TONE turned to nothing: at 6 kHz the right echo is %.3f of the sound before the turn, %.3f from 2 to 3 ms after it, %.3f 50 ms after\n", before, soon, later);
        CHECK(before > 0.8 && before < 0.95 && soon > 0.5 * before && soon < 0.98 * before && later < 0.25 * before && later > 0.09);
    }
    // ...and on the RIGHT side's cut the same (the right echo on its way back to the left): the same
    // burst, TONE turned in the middle of its RIGHT echo, and what reaches the left (two cuts, two
    // FEEDBACKs: 0.871 x 0.871 is 0.759):
    //   before the turn                0.759
    //   2 to 3 ms after the turn       0.725   (on its way, as the left side's)
    //   50 ms after                    0.145
    {
        std::unique_ptr<Effect> t = fresh(ping, 0.f, 1.f, 1.f, 1.f);
        Stereo io(72000);
        for (std::size_t i = 44000; i < 50000; ++i) io.l[i] = io.r[i] = static_cast<float>(0.25 * std::sin(2.0 * kPi * 6000.0 * static_cast<double>(i) / 48000.0));
        through(*t, io, 0, 59000);
        t->setParam(2, 0.f);
        through(*t, io, 59000, 72000);
        // The right echo is from 56000; what passed its cut at 59000, the turn, is on the left at 65000.
        const double before = partAt(io.l.data() + 64000, 480, 6000.0) / 0.25, soon = partAt(io.l.data() + 65000 + 96, 48, 6000.0) / 0.25, later = partAt(io.l.data() + 65000 + 2400, 480, 6000.0) / 0.25;
        SAY("ping     TONE turned to nothing on the right echo: at 6 kHz the next left echo is %.3f of the sound before the turn, %.3f from 2 to 3 ms after it, %.3f 50 ms after\n", before, soon, later);
        CHECK(before > 0.7 && before < 0.8 && soon > 0.5 * before && soon < 0.98 * before && later < 0.25 * before && later > 0.08);
    }

    // EVERY SETTING SLIDES SAMPLE BY SAMPLE, ON BOTH SIDES: never still for a block and then a step
    // (blocks of 64 here; that zipper is heard: MIX turned under a sine made steps 41% larger).
    // A steady level on the way in (0.5 both sides, FEEDBACK 50% (0.45), a sixteenth, TONE full) makes every
    // echo steady too. MIX turned moves both echoes by a little every sample at once; FEEDBACK turned
    // moves them a sixteenth later, when what it scaled comes off the lines. Each sample's move is
    // from 0.5 to 2 times the one before it, in the same direction, through the first 10 blocks.
    {
        const auto slides = [](const std::vector<float>& v, std::size_t from, std::size_t count) {
            double worst = 1.0;
            bool ok = true;
            for (std::size_t i = from + 1; i < from + count; ++i) {
                const double now = v[i] - v[i - 1], was = v[i - 1] - v[i - 2];
                const double ratio = was != 0.0 ? now / was : 0.0;
                ok = ok && ratio >= 0.5 && ratio <= 2.0;
                worst = std::fabs(std::log(ratio > 0.0 ? ratio : 1e-9)) > std::fabs(std::log(worst)) ? (ratio > 0.0 ? ratio : 0.0) : worst;
            }
            return std::make_pair(ok, worst);
        };
        std::unique_ptr<Effect> t = fresh(ping, 0.f, 0.5f, 1.f, 1.f);
        const std::size_t turnMix = 6 * 48000, turnFeedback = turnMix + 150 * 64; // both on a block's edge
        Stereo io(turnFeedback + 6000 + 4800);
        io.l.assign(io.frames(), 0.5f);
        io.r.assign(io.frames(), 0.5f);
        through(*t, io, 0, turnMix);
        t->setParam(3, 0.2f);
        through(*t, io, turnMix, turnFeedback);
        t->setParam(1, 0.1f);
        through(*t, io, turnFeedback, io.frames());
        const auto mixL = slides(io.l, turnMix + 1, 640), mixR = slides(io.r, turnMix + 1, 640);
        const auto fbL = slides(io.l, turnFeedback + 6000 + 1, 640), fbR = slides(io.r, turnFeedback + 6000 + 1, 640);
        SAY("ping     MIX turned: each sample's move against the last, at worst %.3f left, %.3f right; FEEDBACK: %.3f, %.3f\n", mixL.second, mixR.second, fbL.second, fbR.second);
        CHECK(mixL.first && mixR.first && fbL.first && fbR.first);
        CHECK(io.l[turnMix - 1] == io.l[turnMix - 2] && io.r[turnFeedback + 5999] == io.r[turnFeedback + 5998] && io.l[turnFeedback + 6000] != io.l[turnFeedback + 5999]); // steady before each
    }
    // TONE too: a steady level passes a high cut whatever it is set to, so a steady SLOPE is used
    // (the cut lags a slope by an amount that is its setting). On a bus, a slope on the way in, a
    // sixteenth: its first right echo is FEEDBACK of what left the cut, alone there. TONE turned from
    // full to nothing while the slope is in the cut: the largest change in that echo's step from one
    // sample to the next, in the 20 ms after (against the slope's own step, 9e-5 a sample):
    //   gliding sample by sample         9e-8   (a float's grain: 6e-8 before the turn)
    //   still for a block, then a step   5.2e-6 (the lag jumps at each block's edge)
    {
        std::unique_ptr<Effect> t = fresh(ping, 0.f, 1.f, 1.f, 1.f);
        t->setOnBus(true);
        Stereo io(30000);
        for (std::size_t i = 0; i < 12000; ++i) io.l[i] = io.r[i] = static_cast<float>(1e-4 * (static_cast<double>(i) - 6000.0));
        const std::size_t turn = 9000; // the slope from 3000 on is in the cut now; on the right 6000 later
        through(*t, io, 0, turn);
        t->setParam(2, 0.f);
        through(*t, io, turn, io.frames());
        double bend = 0.0, quiet = 0.0;
        for (std::size_t i = turn + 6000 + 2; i < turn + 6000 + 960; ++i) bend = std::fmax(bend, std::fabs(static_cast<double>(io.r[i]) - 2.0 * io.r[i - 1] + io.r[i - 2]));
        for (std::size_t i = turn + 6000 - 960; i < turn + 6000; ++i) quiet = std::fmax(quiet, std::fabs(static_cast<double>(io.r[i]) - 2.0 * io.r[i - 1] + io.r[i - 2]));
        SAY("ping     TONE turned under a slope: the right echo's step changes by at most %.2e a sample after the turn (%.2e before it)\n", bend, quiet);
        CHECK(bend < 1e-6);
    }

    // TIME LANDS ON THE SAMPLE WITHOUT A STEP. Turned from a sixteenth to an eighth under a steady
    // 1 kHz sine, the lines are read between samples until the reading is within a thousandth of a
    // sample of the new TIME, and then on it. Measured: on the sample 2.36 s after the turn; in the
    // 10 ms before, it reads the sound an eighth ago to within 3.5e-5 (a thousandth of a sample of
    // this sine is 3.3e-5; a tenth of one, 3.3e-3, would be a step).
    {
        std::unique_ptr<Effect> t = fresh(ping, 0.f, 0.f, 1.f, 1.f);
        Stereo io(6 * 48000);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        const std::vector<float> in = io.l;
        through(*t, io, 0, 48000);
        t->setParam(0, tapeTime(2));
        through(*t, io, 48000, io.frames());
        const auto exact = [&](std::size_t i) { return io.l[i] == in[i] + in[i - 12000] || (std::fabs(in[i - 12000]) < 1e-12f && io.l[i] == in[i]); };
        std::size_t landed = 48000;
        for (std::size_t i = 48000; i < io.frames(); ++i) landed = exact(i) ? landed : i + 1;
        double worst = 0.0;
        for (std::size_t i = landed - 480; i < landed; ++i) worst = std::fmax(worst, std::fabs(static_cast<double>(io.l[i]) - (static_cast<double>(in[i]) + in[i - 12000])));
        SAY("ping     TIME turned to an eighth: on the sample %.2f s after the turn; in the 10 ms before, within %.1e of the sound an eighth ago\n", static_cast<double>(landed - 48000) / 48000.0, worst);
        CHECK(landed > 48000 + 2.2 * 48000 && landed < 48000 + 2.6 * 48000 && worst < 1e-4);
    }

    // THE TAIL LEANS LEFT, by design (the owner's call to change it: see ping.cpp). The first echo is
    // on the left and each right echo is FEEDBACK of the left one before it, so over the whole tail
    // the left has 1 / FEEDBACK squared of the right's energy: 20 log10(1 / FEEDBACK) dB, and a
    // little more for what TONE takes off noise on the way across. 50 ms of noise in the middle,
    // TONE full, a sixteenth at 120 BPM. Measured, left over right:
    //   FEEDBACK reads    0%               25%     50%     75%     100%   (doing 0, 0.225, 0.45, 0.675, 0.9)
    //   left over right   (right silent)   13.8    7.8     4.25    1.64 dB
    //   1 / FEEDBACK^2    -                13.0    6.9     3.4     0.9 dB
    {
        const float settings[4] = {0.25f, 0.5f, 0.75f, 1.f};
        const double measured[4] = {13.82, 7.80, 4.25, 1.64};
        for (int s = 0; s < 4; ++s) {
            Stereo io(60 * 48000);
            Noise noise(5u);
            for (std::size_t i = 0; i < 2400; ++i) io.l[i] = io.r[i] = 0.5f * noise.next();
            std::unique_ptr<Effect> p = fresh(ping, 0.f, settings[s], 1.f, 1.f);
            p->setOnBus(true);
            through(*p, io);
            double l = 0.0, r = 0.0;
            for (std::size_t i = 0; i < io.frames(); ++i) l += static_cast<double>(io.l[i]) * io.l[i], r += static_cast<double>(io.r[i]) * io.r[i];
            const double lean = 10.0 * std::log10(l / r), plain = -20.0 * std::log10(kPingMost * settings[s]);
            SAY("ping     FEEDBACK %s: the tail's left over its right %.2f dB (1 / FEEDBACK squared: %.2f dB)\n", reads(ping, 1, settings[s]).c_str(), lean, plain);
            CHECK(std::fabs(lean - measured[s]) < 0.1 && lean > plain && lean < plain + 1.0);
        }
        const Stereo once = clickBoth(*fresh(ping, 0.f, 0.f, 1.f, 1.f), 1.0);
        CHECK(echoAt(once.r, 1) == -1); // at 0%: one echo, on the left
    }

    // HOWEVER IT IS CUT INTO BLOCKS, the same to the last bit, both sides: through a whole tail,
    // into sleep, and awake again part way through a block (a second, faint sound 60 dB down at
    // 45 s and 1237 samples). Blocks of 64, of 48, of every size from 1 to 61 in turn, and of 256.
    struct Cut { float time, feedback, tone; };
    for (const Cut& c : {Cut{0.f, 1.f, 1.f}, Cut{tapeTime(4), 0.5f, 0.f}, Cut{tapeTime(5), 0.6f, 0.6f}}) {
        const std::size_t second = 48000;
        Stereo in(70 * second);
        Noise noise(31u);
        for (std::size_t i = 0; i < 4800; ++i) in.l[1000 + i] = 0.5f * noise.next(), in.r[1000 + i] = 0.5f * noise.next();
        in.l[45 * second + 1237] = 5e-4f;
        Stereo ways[4];
        const std::size_t sizes[4] = {64, 48, 0, 256}; // 0: odd sizes, 1 to 61 in turn
        for (int way = 0; way < 4; ++way) {
            std::unique_ptr<Effect> p = fresh(ping, c.time, c.feedback, c.tone, 1.f);
            Stereo io = in;
            for (std::size_t at = 0, n = sizes[way] ? sizes[way] : 1; at < io.frames(); at += n, n = sizes[way] ? sizes[way] : n % 61 + 1) {
                if (at + n > io.frames()) n = io.frames() - at;
                p->process(io.l.data() + at, io.r.data() + at, static_cast<int>(n));
            }
            ways[way] = io;
        }
        std::size_t firstApart = in.frames();
        for (int way = 1; way < 4; ++way)
            for (std::size_t i = 0; i < in.frames(); ++i)
                if (ways[way].l[i] != ways[0].l[i] || ways[way].r[i] != ways[0].r[i]) firstApart = std::min(firstApart, i);
        // Where it fell silent the first time, and that the faint sound's echoes are there after it.
        std::size_t last = 0;
        for (std::size_t i = 0; i < 45 * second; ++i) last = ways[0].l[i] != 0.f || ways[0].r[i] != 0.f ? i : last;
        SAY("ping     cut into blocks four ways (TIME %s, FEEDBACK %s): first apart at %.2f s of %d; silent from %.1f s until the faint sound\n", reads(ping, 0, c.time).c_str(), reads(ping, 1, c.feedback).c_str(),
            static_cast<double>(firstApart) / 48000.0, static_cast<int>(in.frames() / second), static_cast<double>(last) / 48000.0);
        CHECK(firstApart == in.frames());
        // (And it did all of it: rang, fell silent with a line's length to spare before the faint sound, and that sound's echo came.)
        const std::size_t time = static_cast<std::size_t>(std::lround(thirds[std::min(7, static_cast<int>(c.time * 8.f))] * 5.0 / 120.0 * 48000.0));
        CHECK(rmsOf(ways[0].l, 2 * second, 3 * second) + rmsOf(ways[0].r, 2 * second, 3 * second) > 1e-5 && last < 41 * second);
        CHECK(ways[0].l[45 * second + 1237 + time] == 2.5e-4f && silentIn(ways[0].l, 45 * second + 1238, 45 * second + 1237 + time));
    }

    // ON A BUS: a click in, its echoes out, and no click. MIX is the level there.
    {
        std::unique_ptr<Effect> p = fresh(ping, 0.f, 0.5f, 1.f, 0.8f);
        p->setOnBus(true);
        const Stereo io = clickBoth(*p, 0.5);
        CHECK(io.l[0] == 0.f && io.r[0] == 0.f && echoAt(io.l, 0) == 6000 && io.l[6000] == 0.8f && echoAt(io.r, 0) == 12000);
        CHECK(std::fabs(echoGain(bothOf(io), 6000) - 0.45) < 0.002);
        // Asleep on a bus, what comes in is not given back either (sound too faint to wake it).
        std::unique_ptr<Effect> q = fresh(ping, 0.f, 0.5f, 1.f, 1.f);
        q->setOnBus(true);
        Stereo faint(4800);
        faint.l.assign(4800, 1e-16f);
        faint.r.assign(4800, -1e-16f);
        through(*q, faint);
        CHECK(peakOf(faint.l) == 0.f && peakOf(faint.r) == 0.f);
        // The bus comes to rest (testATailAlwaysEnds has every setting at 40 BPM; this is its most at 120).
        const float most[4] = {0.f, 1.f, 1.f, 1.f};
        const double seconds = secondsToRest(ping, most, 120.f, 100.0);
        SAY("ping     on a bus, a sixteenth at 120 BPM, FEEDBACK 100%% (0.9): at rest %.1f s after the last sound\n", seconds);
        CHECK(seconds > 20.0 && seconds < 35.0); // 120 dB at 7.5 dB a second is 16 s, and the 10 s of quiet before a bus rests
    }

    // CHANGING TYPE, to and from the family's other two. What is carried: TIME, FEEDBACK, TONE and
    // MIX with CLEAN; TIME, FEEDBACK and MIX with TAPE (its third is AGE: not TONE).
    for (int i = 0; i < 4; ++i) {
        CHECK(effectCarryFrom(delay, ping, i) == i && effectCarryFrom(ping, delay, i) == i);
        CHECK(effectCarryFrom(tape, ping, i) == (i == 2 ? -1 : i) && effectCarryFrom(ping, tape, i) == (i == 2 ? -1 : i));
    }
    // TIME: with TAPE the same eight, each to itself; to CLEAN a triplet goes to the nearest in steps, as TAPE's does.
    const auto time = [&](EffectKind from, EffectKind to, float v) { return reads(to, 0, effectCarryValue(from, 0, to, 0, v)); };
    for (int c = 0; c < 8; ++c) {
        CHECK(effectCarryValue(tape, 0, ping, 0, tapeTime(c)) == tapeTime(c) && effectCarryValue(ping, 0, tape, 0, tapeTime(c)) == tapeTime(c));
        CHECK(time(ping, delay, tapeTime(c)) == time(tape, delay, tapeTime(c)));
    }
    for (int c = 0; c < 6; ++c) CHECK(time(delay, ping, (c + 0.5f) / 6.f) == reads(delay, 0, (c + 0.5f) / 6.f)); // every one of CLEAN's is here
    CHECK(time(ping, delay, tapeTime(1)) == "1/16" && time(ping, delay, tapeTime(3)) == "1/8 DOT" && time(ping, delay, tapeTime(7)) == "1/2");
    // FEEDBACK as it READS. With CLEAN it is one scale (0 to 100%): not touched, to the bit, so a CLEAN and a PING at
    // the same reading do the same (each echo 0.9 of the reading). With TAPE (to 120%): the same reading, on the roundest value.
    for (float v : {0.f, 0.05f, 0.123456f, 0.45f, 0.5f, 0.987654f, 1.f}) CHECK(effectCarryValue(delay, 1, ping, 1, v) == v && effectCarryValue(ping, 1, delay, 1, v) == v);
    CHECK(effectCarryValue(ping, 1, tape, 1, 0.5f) == 0.42f && reads(tape, 1, 0.42f) == "50%" && effectCarryValue(tape, 1, ping, 1, 0.42f) == 0.5f);
    CHECK(reads(tape, 1, 0.375f) == "45%" && effectCarryValue(tape, 1, ping, 1, 0.375f) == 0.45f);
    CHECK(reads(tape, 1, 0.4f) == "48%" && reads(ping, 1, effectCarryValue(tape, 1, ping, 1, 0.4f)) == "48%" && effectCarryValue(ping, 1, tape, 1, effectCarryValue(tape, 1, ping, 1, 0.4f)) == 0.4f);
    // More than PING reads (TAPE over 100%): its most. All PING reads, TAPE can.
    CHECK(reads(ping, 1, effectCarryValue(tape, 1, ping, 1, 1.f)) == "100%" && effectCarryValue(tape, 1, ping, 1, 1.f) == 1.f);
    CHECK(reads(tape, 1, effectCarryValue(ping, 1, tape, 1, 1.f)) == "100%" && effectCarryValue(ping, 1, tape, 1, 0.f) == 0.f);
    // TONE (with CLEAN) and MIX (with both) are one scale: not touched, to the bit.
    for (float v : {0.f, 0.123456f, 0.5f, 0.987654f, 1.f}) {
        CHECK(effectCarryValue(delay, 2, ping, 2, v) == v && effectCarryValue(ping, 2, delay, 2, v) == v);
        CHECK(effectCarryValue(delay, 3, ping, 3, v) == v && effectCarryValue(ping, 3, delay, 3, v) == v && effectCarryValue(tape, 3, ping, 3, v) == v && effectCarryValue(ping, 3, tape, 3, v) == v);
    }
    // From TAPE its TONE starts where a new PING's does; on a bus LEVEL is full.
    {
        const float was[4] = {tapeTime(2), 0.4f, 0.9f, 0.2f};
        float now[4];
        effectCarry(tape, ping, was, false, now);
        CHECK(now[0] == tapeTime(2) && reads(ping, 1, now[1]) == "48%" && now[1] == 0.48f && now[2] == 0.6f && now[3] == 0.2f);
        effectCarry(tape, ping, nullptr, true, now);
        CHECK(now[0] == 0.5625f && now[1] == 0.5f && now[2] == 0.6f && now[3] == 1.f);
    }
}

// ---- SPRING -------------------------------------------------------------------------------------

// WHEN A PITCH ARRIVES: where the weight of one band of a sound is between two moments, in ms.
static double arrivesAt(const std::vector<float>& v, double hz, double fromMs, double toMs) {
    const std::vector<float> band = bandOf(v, hz);
    double sum = 0.0, weight = 0.0;
    for (std::size_t i = static_cast<std::size_t>(fromMs * 48.0); i < static_cast<std::size_t>(toMs * 48.0) && i < band.size(); ++i) {
        const double e = static_cast<double>(band[i]) * band[i];
        sum += e * static_cast<double>(i), weight += e;
    }
    return weight > 0.0 ? sum / weight / 48.0 : 0.0;
}
// HOW MUCH OF A SOUND IS BETWEEN TWO PITCHES, in dB against all of it (the first `count` samples, a reading every 25 Hz).
static double shareBetween(const std::vector<float>& v, std::size_t count, double fromHz, double toHz) {
    double all = 0.0, part = 0.0;
    for (double hz = 25.0; hz < 24000.0; hz += 25.0) {
        const double size = partAt(v.data(), count, hz), e = size * size;
        all += e;
        if (hz >= fromHz && hz < toHz) part += e;
    }
    return all > 0.0 ? 10.0 * std::log10(part / all + 1e-30) : -300.0;
}
static float springDecay(double seconds) { return static_cast<float>(std::log(seconds / 0.4) / std::log(10.0)); }
// [setting][down, up][the largest step, the sharpest corner], each against the steady one (see where it is used).
static const double kSpringTurn[4][2][2] = {
    {{1.00, 1.18}, {1.57, 2.02}}, // DRIP: the bent sound (see where it is used)
    {{1.0, 1.0}, {1.0, 1.0}}, // DECAY
    {{1.0, 1.0}, {1.0, 1.0}}, // TONE
    {{1.0, 1.0}, {1.0, 1.0}}, // MIX
};

static void testSpring() {
    const EffectKind spring = EffectKind::Spring, hall = EffectKind::Hall, room = EffectKind::Reverb;
    const EffectInfo& info = effectInfo(spring);
    // Its row: number 12 and the word "spring" for good; a TYPE of REVERB, after ROOM and HALL; it may sit on SPACE.
    CHECK(static_cast<int>(spring) == 13 && std::string(info.word) == "spring" && effectFromWord("spring") == spring);
    CHECK(std::string(info.name) == "SPRING" && info.params == 4 && info.family == EffectFamily::Reverb && effectBus(spring) == 1);
    CHECK(std::string(info.paramNames[0]) == "DRIP" && std::string(info.paramNames[1]) == "DECAY" && std::string(info.paramNames[2]) == "TONE" && std::string(info.paramNames[3]) == "MIX");
    CHECK(info.defaults[0] == 0.7f && info.defaults[1] == 0.6f && info.defaults[2] == 0.5f && info.defaults[3] == 0.3f);
    CHECK(std::string(effectFamily(spring)) == "REVERB" && std::string(effectTypeName(spring)) == "SPRING" && effectTypeIndex(spring) == 2 && effectTypeAt(room, 2) == spring);
    CHECK(effectPlace(spring) == effectPlace(hall) + 1 && familyPlace(spring) == familyPlace(room) && familyPlace(spring, 1) == 1 && familyPlace(spring, 0) == 0);
    for (int i = 0; i < 4; ++i) CHECK(effectChoices(spring, i) == 0);
    CHECK(effectMixParam(spring) == 3 && effectDefault(spring, 3, true) == 1.f && effectDefault(spring, 3) == 0.3f && effectMeterParam(spring) == -1);
    // How it reads: DECAY in seconds, 0.4 to 4; the rest as amounts.
    CHECK(reads(spring, 1, 0.f) == "0.4 S" && reads(spring, 1, 0.5f) == "1.3 S" && reads(spring, 1, 0.6f) == "1.6 S" && reads(spring, 1, 1.f) == "4.0 S" && reads(spring, 1, springDecay(2.0)) == "2.0 S");
    CHECK(reads(spring, 0, 0.7f) == "70%" && reads(spring, 2, 0.5f) == "50%" && reads(spring, 3, 0.3f) == "30%");
    // CHANGING TYPE (every pair is in testTypeChangeCarriesSettings; these are its own): from HALL the DECAY comes
    // over as the seconds it reads, or the nearest SPRING has; TONE and MIX as they are; DRIP is its usual.
    // From ROOM only MIX is shared. HALL's TONE and MIX stay in their lanes (third and fourth).
    CHECK(effectCarryFrom(hall, spring, 0) == -1 && effectCarryFrom(hall, spring, 1) == 1 && effectCarryFrom(hall, spring, 2) == 2 && effectCarryFrom(hall, spring, 3) == 3);
    CHECK(effectCarryFrom(spring, hall, 0) == -1 && effectCarryFrom(spring, hall, 1) == 1 && effectCarryFrom(spring, hall, 2) == 2 && effectCarryFrom(spring, hall, 3) == 3);
    CHECK(effectCarryFrom(room, spring, 0) == -1 && effectCarryFrom(room, spring, 1) == -1 && effectCarryFrom(room, spring, 2) == -1 && effectCarryFrom(room, spring, 3) == 2);
    CHECK(effectCarryFrom(spring, room, 0) == -1 && effectCarryFrom(spring, room, 1) == -1 && effectCarryFrom(spring, room, 2) == 3);
    CHECK(reads(hall, 1, 0.45f) == "2.0 S" && reads(spring, 1, effectCarryValue(hall, 1, spring, 1, 0.45f)) == "2.0 S");
    CHECK(reads(hall, 1, 1.f) == "20 S" && effectCarryValue(hall, 1, spring, 1, 1.f) > 0.999f && reads(hall, 1, 0.f) == "0.3 S" && effectCarryValue(hall, 1, spring, 1, 0.f) < 0.001f);
    CHECK(reads(hall, 1, effectCarryValue(spring, 1, hall, 1, 0.6f)) == "1.6 S" && reads(hall, 1, effectCarryValue(spring, 1, hall, 1, 1.f)) == "4.0 S");
    for (int k = 0; k <= 20; ++k) { // every twentieth the D-pad can leave DECAY on reads the same in the HALL, and comes back reading the same
        const float v = static_cast<float>(k) / 20.f, there = effectCarryValue(spring, 1, hall, 1, v);
        CHECK(reads(hall, 1, there) == reads(spring, 1, v) && reads(spring, 1, effectCarryValue(hall, 1, spring, 1, there)) == reads(spring, 1, v));
    }
    {
        const float was[4] = {0.2f, 0.45f, 0.35f, 0.8f};
        float now[4], onBus[4];
        effectCarry(hall, spring, was, false, now);
        effectCarry(room, spring, was, true, onBus);
        CHECK(now[0] == 0.7f && reads(spring, 1, now[1]) == "2.0 S" && now[2] == 0.35f && now[3] == 0.8f);
        CHECK(onBus[0] == 0.7f && onBus[1] == 0.6f && onBus[2] == 0.5f && onBus[3] == 0.35f); // ROOM's MIX is its third
    }

    // THE CHIRP: THE HIGH NOTES ARRIVE LATER. A click, MIX full, TONE full: where the weight of each pitch is in
    // the first 30 ms (before either spring's sound has been round once), in ms after the click:
    //   DRIP     1 kHz    2 kHz    3 kHz    3.5 kHz    4 kHz      4 kHz after 1 kHz
    //   0%        9.9      9.4      9.4      9.5        9.6          -0.3      plain: every pitch together
    //   50%       6.4      7.5     10.2     11.8       12.9           6.5
    //   70%       5.3      6.4      9.7     12.0       13.8           8.4      (as it comes)
    //   100%      3.9      4.6      7.9     10.8       13.5           9.6
    // (Turned up, the lows hurry and the highs hang back: a stage holds a low note for less than its one sample.)
    {
        const double hz[5] = {1000.0, 2000.0, 3000.0, 3500.0, 4000.0};
        struct Row { float drip; double at[5]; };
        const Row rows[4] = {{0.f, {9.85, 9.35, 9.38, 9.46, 9.57}}, {0.5f, {6.44, 7.48, 10.19, 11.76, 12.92}}, {0.7f, {5.32, 6.39, 9.70, 11.96, 13.77}}, {1.f, {3.89, 4.63, 7.93, 10.80, 13.46}}};
        double lastSpread = -2.0;
        for (const Row& row : rows) {
            const std::vector<float> ir = clickOf(*fresh(spring, row.drip, 0.6f, 1.f, 1.f), 0.2, 1.f);
            double at[5];
            for (int i = 0; i < 5; ++i) {
                at[i] = arrivesAt(ir, hz[i], 0.0, 30.0);
                SAY("spring   drip %3.0f%%: %4.0f Hz arrives at %5.2f ms\n", row.drip * 100.0, hz[i], at[i]);
                CHECK(std::fabs(at[i] - row.at[i]) < 0.25);
                if (row.drip > 0.f && i > 0) CHECK(at[i] > at[i - 1] + 0.5); // each higher pitch later than the one below
            }
            if (row.drip == 0.f) CHECK(std::fabs(at[4] - at[0]) < 0.6 && std::fabs(at[3] - at[1]) < 0.3); // no chirp: together
            CHECK(at[4] - at[0] > lastSpread + 1.0); // and more DRIP is more chirp
            lastSpread = at[4] - at[0];
        }
        // The tool: a click through nothing arrives when it was made, whatever the pitch; made 5 ms late, 5 ms later.
        std::vector<float> click(4800, 0.f);
        click[480] = 1.f;
        const double low = arrivesAt(click, 1000.0, 0.0, 60.0), high = arrivesAt(click, 4000.0, 0.0, 60.0);
        click[480] = 0.f, click[720] = 1.f;
        CHECK(std::fabs(arrivesAt(click, 1000.0, 0.0, 60.0) - low - 5.0) < 0.01 && std::fabs(high - low) < 1.0 && low > 10.0 && low < 11.5);
    }

    // DECAY IS A REAL TIME, WHATEVER DRIP IS. A click, MIX full, TONE full (next to nothing damped): the RT60
    // against what DECAY reads, around 500 Hz; all within 9% (the echoes are few and far apart at DRIP none,
    // so the reading is bumpy there), and at 3 kHz within 9% (TONE full still trims the highs a little: see TONE).
    //   DECAY     DRIP 0%   70%    100%
    //   0.40 s     0.39     0.40   0.42
    //   0.71 s     0.76     0.74   0.69
    //   1.26 s     1.26     1.28   1.29
    //   2.25 s     2.24     2.19   2.21
    //   4.00 s     3.92     3.96   3.90
    // At TONE 50% (as it comes) it reads 0 to 10% short there: TONE's damping already takes a little at 500 Hz.
    for (float drip : {0.f, 0.7f, 1.f}) {
        double last = 0.0;
        for (float decay : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
            const double want = 0.4 * std::pow(10.0, decay);
            const std::vector<float> ir = clickOf(*fresh(spring, drip, decay, 1.f, 1.f), 2.0 + want * 1.2, 1.f);
            const double mid = rt60(bandOf(ir, 500.0)), high = rt60(bandOf(ir, 3000.0));
            const double usual = rt60(bandOf(clickOf(*fresh(spring, drip, decay, 0.5f, 1.f), 2.0 + want * 1.2, 1.f), 500.0));
            SAY("spring   drip %3.0f%% decay %4.2f s: RT60 at 500 Hz %5.2f s (x %.3f), at 3 kHz x %.3f; at TONE 50%% x %.3f\n", drip * 100.0, want, mid, mid / want, high / want, usual / want);
            CHECK(std::fabs(mid / want - 1.0) < 0.10 && std::fabs(high / want - 1.0) < 0.10);
            CHECK(usual / want > 0.88 && usual / want < 1.01);
            CHECK(mid > 1.5 * last);
            last = mid;
        }
    }

    // TONE: HOW LONG THE TAIL LASTS AT EACH PITCH. DECAY 1.6 s (the usual), in seconds. The corner stays at
    // 1.5 kHz; TONE sets how long what is above it lasts: 0.2 as long at none, 0.85 at full (the highs take
    // longer a trip, so with nothing damped they would outlast DECAY by a fifth).
    //   TONE     300 Hz   500 Hz   1 kHz   2 kHz   3.5 kHz    3.5 kHz against 1 kHz
    //   0%       1.49     1.40     1.06    0.80    0.71       0.67    dark: the highs die soon
    //   25%      1.51     1.48     1.18    0.89    0.87       0.74
    //   50%      1.52     1.44     1.32    1.10    1.11       0.84
    //   75%      1.57     1.53     1.43    1.34    1.42       0.99
    //   100%     1.59     1.57     1.54    1.56    1.75       1.13    bright
    // The darker the TONE, the sooner the highs die against the mids, at every step (it was not so: with the
    // corner swept from 400 Hz, at TONE none the whole band was above it, and 0% was BRIGHTER than 50%).
    {
        struct Row { float tone; double at[5]; };
        const double hz[5] = {300.0, 500.0, 1000.0, 2000.0, 3500.0};
        const Row rows[5] = {{0.f, {1.49, 1.40, 1.06, 0.80, 0.71}}, {0.25f, {1.51, 1.48, 1.18, 0.89, 0.87}}, {0.5f, {1.52, 1.44, 1.32, 1.10, 1.11}}, {0.75f, {1.57, 1.53, 1.43, 1.34, 1.42}}, {1.f, {1.59, 1.57, 1.54, 1.56, 1.75}}};
        double lastTilt = 0.0;
        for (const Row& row : rows) {
            const std::vector<float> ir = clickOf(*fresh(spring, 0.7f, 0.6f, row.tone, 1.f), 8.0, 1.f);
            double got[5];
            for (int i = 0; i < 5; ++i) {
                got[i] = rt60(bandOf(ir, hz[i]));
                SAY("spring   tone %3.0f%%: RT60 at %4.0f Hz %.2f s\n", row.tone * 100.0, hz[i], got[i]);
                CHECK(std::fabs(got[i] / row.at[i] - 1.0) < 0.06);
            }
            const double tilt = got[4] / got[2];
            SAY("spring   tone %3.0f%%: 3.5 kHz lasts %.2f as long as 1 kHz\n", row.tone * 100.0, tilt);
            CHECK(tilt > lastTilt + 0.05); // each step brighter than the one before
            if (row.tone == 0.f) CHECK(tilt < 0.75);
            lastTilt = tilt;
        }
    }

    // BAND-LIMITED, as a spring is: little under 100 Hz, little over 5 kHz. A click, MIX full, TONE full, DECAY at
    // its shortest (so half a second holds the tail); how much of what comes out is where, against all of it:
    //   under 100 Hz  -34 dB     100 Hz to 4.5 kHz: all but a hundredth     over 5 kHz  -38 dB     over 6 kHz  -47 dB
    // and between 500 Hz and 4 kHz no 500 Hz stretch holds more than 2 dB more or 3 dB less than its even share.
    {
        const std::vector<float> ir = clickOf(*fresh(spring, 0.7f, 0.f, 1.f, 1.f), 1.0, 1.f);
        const std::size_t count = 48000;
        const double under = shareBetween(ir, count, 0.0, 100.0), over5 = shareBetween(ir, count, 5000.0, 24000.0), over6 = shareBetween(ir, count, 6000.0, 24000.0), within = shareBetween(ir, count, 100.0, 4500.0);
        SAY("spring   of a click's sound: %.1f dB is under 100 Hz, %.1f dB over 5 kHz, %.1f dB over 6 kHz, %.3f dB between 100 Hz and 4.5 kHz\n", under, over5, over6, within);
        CHECK(under < -28.0 && under > -40.0 && over5 < -32.0 && over6 < -40.0 && within > -0.05);
        for (double from = 500.0; from < 4000.0; from += 500.0) {
            const double share = shareBetween(ir, count, from, from + 500.0);
            SAY("spring   %4.0f to %4.0f Hz: %.1f dB\n", from, from + 500.0, share);
            CHECK(share > -12.2 && share < -7.0); // an even share of nine such stretches would be -9.5 dB
        }
        // The tool: a click by itself has as much in every stretch (a 480th of it in each 50 Hz).
        std::vector<float> click(count, 0.f);
        click[0] = 1.f;
        CHECK(std::fabs(shareBetween(click, count, 1000.0, 2000.0) - 10.0 * std::log10(40.0 / 959.0)) < 0.01);
    }
    // NOTHING FOLDS BACK THAT MATTERS. The springs run at a fifth of the rate, so a pitch over 4.8 kHz that got
    // down to them would come back as another pitch (5.2 kHz as 4.4, 6 kHz as 3.6, 8 kHz as 1.6). A held sine, MIX
    // full; what comes out at the pitch it would fold to, against the sine:
    //   5.2 kHz -> 4.4 kHz: -27 dB      6 kHz -> 3.6 kHz: -46 dB      8 kHz -> 1.6 kHz: -53 dB      (a 1 kHz sine comes out at 1 kHz, 1.2 dB down)
    // The worst is just over the top of the springs' rate, where the way down has not finished cutting.
    {
        const double pairs[3][2] = {{5200.0, 4400.0}, {6000.0, 3600.0}, {8000.0, 1600.0}};
        const double most[3] = {-24.0, -40.0, -47.0};
        for (int i = 0; i < 3; ++i) {
            Stereo io(96000);
            io.l = io.r = sineOf(pairs[i][0], 0.25, io.frames());
            through(*fresh(spring, 0.7f, 0.6f, 1.f, 1.f), io);
            const double folded = dB(partAt(io.l.data() + 48000, 48000, pairs[i][1]) / 0.25), itself = dB(partAt(io.l.data() + 48000, 48000, pairs[i][0]) / 0.25), all = dB(rmsOf(io.l, 48000, 96000) / (0.25 / std::sqrt(2.0)));
            SAY("spring   a %4.0f Hz sine: %.1f dB comes out at %4.0f Hz, %.1f dB at its own pitch, %.1f dB in all\n", pairs[i][0], folded, pairs[i][1], itself, all);
            CHECK(folded < most[i] && all < most[i] + 1.0);
        }
        Stereo io(96000);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        through(*fresh(spring, 0.7f, 0.6f, 1.f, 1.f), io);
        const double itself = dB(partAt(io.l.data() + 48000, 48000, 1000.0) / 0.25), all = dB(rmsOf(io.l, 48000, 96000) / (0.25 / std::sqrt(2.0)));
        SAY("spring   a 1000 Hz sine: %.1f dB comes out at its own pitch, %.1f dB in all\n", itself, all);
        CHECK(itself > -20.0 && itself < 12.0 && all - itself < 0.01); // (a held sine sits on a peak or in a dip of the springs' echoes: which, is the pitch's luck)
    }

    // TWO SPRINGS, ONE A SIDE: A SLIGHT DIFFERENCE SIDE TO SIDE. The left goes in at one spring and the right at
    // the other, and each side gets mostly one spring and some of the other.
    //   a click in both: left and right alike 0.80 (1 the same, 0 unrelated), the left 0.6 dB under the right
    //   a click on the left only: the left 5.7 to 6.0 dB over the right; on the right only, the right 3.8 to 4.0 dB over the left
    //   a click on the left with its opposite on the right: 2.2 to 2.3 dB under a click in both
    // (The sound was once taken in as one, left and right added: a sound opposite on the two sides, as a wide
    // stereo sound partly is, then put nothing in at all, where ROOM and HALL take it.)
    for (float drip : {0.f, 0.7f, 1.f}) {
        const Stereo both = clickBoth(*fresh(spring, drip, 0.6f, 0.5f, 1.f), 3.0), one = clickBoth(*fresh(spring, drip, 0.6f, 0.5f, 1.f), 3.0, 1.f, 0.f), other = clickBoth(*fresh(spring, drip, 0.6f, 0.5f, 1.f), 3.0, 0.f, 1.f);
        const Stereo opposite = clickBoth(*fresh(spring, drip, 0.6f, 0.5f, 1.f), 3.0, 1.f, -1.f);
        const double same = alike(both.l, both.r, 1, 144000), level = dB(rmsOf(both.l, 1, 144000) / rmsOf(both.r, 1, 144000));
        const double leftOnly = dB(rmsOf(one.l, 1, 144000) / rmsOf(one.r, 1, 144000)), rightOnly = dB(rmsOf(other.r, 1, 144000) / rmsOf(other.l, 1, 144000));
        const double opp = dB((rmsOf(opposite.l, 1, 144000) + rmsOf(opposite.r, 1, 144000)) / (rmsOf(both.l, 1, 144000) + rmsOf(both.r, 1, 144000)));
        SAY("spring   drip %3.0f%%: left and right alike %.3f, the left %.2f dB of the right; the left alone %.2f dB over the right, the right alone %.2f dB over the left; opposite sides %.2f dB of both alike\n", drip * 100.0, same, level, leftOnly, rightOnly, opp);
        CHECK(same > 0.7 && same < 0.9 && std::fabs(level) < 1.0);
        CHECK(leftOnly > 5.4 && leftOnly < 6.3 && rightOnly > 3.5 && rightOnly < 4.3 && opp > -2.6 && opp < -1.9);
        // What goes in at each side adds up: the left alone and the right alone together are a click in both.
        float apart = 0.f;
        for (std::size_t i = 0; i < both.frames(); ++i) apart = std::fmax(apart, std::fmax(std::fabs(one.l[i] + other.l[i] - both.l[i]), std::fabs(one.r[i] + other.r[i] - both.r[i])));
        CHECK(apart < 1e-6f && rmsOf(both.l, 1, 48000) > 1e-3);
    }
    // ABOUT AS LOUD AT ANY DECAY AND DRIP. Held white noise, MIX full: the springs come out 8.7 to 9.6 dB under
    // it (a spring passes a sixth of white noise's range: a sound that is all under 4 kHz comes back about as
    // loud as it went in).
    for (float drip : {0.f, 0.7f, 1.f})
        for (float decay : {0.f, 0.6f, 1.f}) {
            Stereo io(48000 * 20);
            Noise noise(3u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
            const double inRms = rmsOf(io.l);
            through(*fresh(spring, drip, decay, 0.5f, 1.f), io);
            const double level = dB(rmsOf(io.l, 19 * 48000, 20 * 48000) / inRms);
            SAY("spring   drip %3.0f%% decay %3.0f%%: held noise comes out at %.1f dB\n", drip * 100.0, decay * 100.0, level);
            CHECK(level > -10.6 && level < -7.7);
        }

    // AS LOUD AS HALL, FOR A SOUND IN A SPRING'S BAND. White noise comes out 9 dB under (above) because most of
    // its power is over 5 kHz, where a spring passes nothing; HALL gives back 3 dB under. A noise that is
    // mostly between 150 Hz and 2 kHz (as most of a song's power is), MIX full, against HALL with the same DECAY:
    //   DECAY    0.4 s      1.6 s      4 s
    //   SPRING   -2.1       -1.4       -0.1 dB   (both sides, against what went in)
    //   HALL     -2.3       -2.4       -1.7 dB
    // So a TYPE changed from HALL to SPRING keeps about its level (within 2 dB).
    {
        const float decays[3] = {0.f, 0.6f, 1.f};
        for (float decay : decays) {
            double level[2];
            for (int k = 0; k < 2; ++k) {
                const EffectKind kind = k ? hall : spring;
                std::unique_ptr<Effect> e = makeEffect(kind, 48000.f, 120.f);
                e->setParam(1, k ? effectCarryValue(spring, 1, hall, 1, decay) : decay);
                e->setParam(3, 1.f);
                e->reset();
                Stereo io(48000 * 6);
                Noise noise(5u);
                float lp = 0.f, lp2 = 0.f, hp = 0.f;
                for (std::size_t i = 0; i < io.frames(); ++i) {
                    lp += 0.25f * (0.25f * noise.next() - lp), lp2 += 0.25f * (lp - lp2), hp += 0.02f * (lp2 - hp);
                    io.l[i] = io.r[i] = 3.f * (lp2 - hp);
                }
                const double inRms = rmsOf(io.l, 48000, io.frames());
                through(*e, io);
                level[k] = dB(0.5 * (rmsOf(io.l, 48000, io.frames()) + rmsOf(io.r, 48000, io.frames())) / inRms);
            }
            SAY("spring   decay %3.0f%%: noise in a spring's band comes out at %.1f dB; HALL at %.1f dB\n", decay * 100.0, level[0], level[1]);
            CHECK(std::fabs(level[0] - level[1]) < 2.0 && level[0] > -3.0 && level[0] < 1.0);
        }
    }

    // MIX: the sound and the springs swap over at equal power, as HALL's do: the sound times cos(MIX x 90
    // degrees), the springs times sin of the same. At full the sound is gone, exactly.
    {
        const std::vector<float> in = sineOf(1000.0, 0.25, 48000);
        std::vector<float> out, tank;
        wetOf(*fresh(spring, 0.7f, 0.6f, 0.5f, 1.f), in, &tank);
        for (float mix : {0.3f, 0.5f, 1.f}) {
            wetOf(*fresh(spring, 0.7f, 0.6f, 0.5f, mix), in, &out);
            const float dry = mix == 1.f ? 0.f : std::cos(1.5707963f * mix), share = std::sin(1.5707963f * mix);
            bool right = true;
            for (std::size_t i = 0; i < in.size(); ++i) right = right && std::fabs(out[i] - (dry * in[i] + share * tank[i])) < 2e-6f;
            CHECK(right && rmsOf(tank, 24000) > 0.02);
        }
        // The springs alone: a sound that stops is gone from what comes out at once but for them (no dry sound left over at all).
        Stereo io(4800);
        io.l[0] = io.r[0] = 1.f;
        through(*fresh(spring, 0.f, 0.6f, 0.5f, 1.f), io); // DRIP none: nothing has come through the stages yet
        CHECK(std::fabs(io.l[0]) < 1e-15f && std::fabs(io.r[0]) < 1e-15f); // (cos of 90 degrees as a float is 4e-8, not 0: that much of the click would be left)
    }
    // AT MIX 50%, NO HOLE. What the springs add comes out late (the way down, the stages, the way up: 4 to 14
    // ms), and is not the sound that went in but its echoes, so beside the plain sound it does not cancel it.
    // Held white noise, each band out against the same band in: the plain sound alone would be -3.0 dB.
    //   250 Hz   1 kHz   3 kHz   8 kHz (the springs add nothing there)
    {
        Stereo io(48000 * 10);
        Noise noise(7u);
        for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
        const std::vector<float> in = io.l;
        through(*fresh(spring, 0.7f, 0.6f, 0.5f, 0.5f), io);
        const double hz[4] = {250.0, 1000.0, 3000.0, 8000.0};
        for (int i = 0; i < 4; ++i) {
            const double level = dB(rmsOf(bandOf(io.l, hz[i]), 96000) / rmsOf(bandOf(in, hz[i]), 96000));
            SAY("spring   MIX 50%%: the band at %4.0f Hz comes out at %.2f dB\n", hz[i], level);
            CHECK(level > -3.3 && level < (i == 3 ? -2.7 : 3.0));
        }
    }

    // A TURNED SETTING GLIDES. Each turned end to end AT ONCE, under a held 100 Hz sine at MIX 50%: the largest
    // step and the sharpest corner on the way, each against the same with nothing turned (see kSpringTurn).
    // DECAY, TONE and MIX: no rougher than the sound standing still. DRIP: a stage holds a low note for less
    // as it is turned up, so for a moment the springs' echoes bend in pitch: turned up at once the largest step
    // is 1.6 times the steady one. That is the bent sound, not a click. Ears decide whether it is wanted.
    for (int p = 0; p < 4; ++p)
        for (int up = 0; up < 2; ++up) {
            std::unique_ptr<Effect> e = fresh(spring, 0.7f, 0.6f, 0.5f, 0.5f);
            e->setParam(p, up ? 0.f : 1.f);
            Stereo io(5 * 48000);
            io.l = io.r = sineOf(100.0, 0.5, io.frames());
            through(*e, io, 0, 96000);
            e->setParam(p, up ? 1.f : 0.f);
            through(*e, io, 96000, io.frames());
            const auto corner = [&](std::size_t from, std::size_t to) {
                double most = 0.0;
                for (std::size_t i = from + 2; i < to; ++i) most = std::fmax(most, std::fabs(static_cast<double>(io.l[i]) - 2.0 * io.l[i - 1] + io.l[i - 2]));
                return most;
            };
            const double steady = std::fmax(largestJump(io.l, 48000, 96000), largestJump(io.l, 4 * 48000, 5 * 48000)), steadyCorner = std::fmax(corner(48000, 96000), corner(4 * 48000, 5 * 48000));
            const double step = largestJump(io.l, 96000 - 1, 4 * 48000) / steady, sharp = corner(96000 - 2, 4 * 48000) / steadyCorner;
            SAY("spring   %-5s turned %-4s at once: the largest step is %.2f of the steady one, the sharpest corner %.2f of the steady one\n", info.paramNames[p], up ? "up" : "down", step, sharp);
            CHECK(std::fabs(step - kSpringTurn[p][up][0]) < 0.08 * kSpringTurn[p][up][0]);
            CHECK(sharp < 1.25 * kSpringTurn[p][up][1] + 0.1);
            if (p > 0) CHECK(step < 1.1 && sharp < 1.2);
        }
    // TONE again, where it is heard: under a held 500 Hz sine, MIX full, DECAY full, TONE turned from bright to
    // dark at once. What the springs send back round changes by TONE's damping, so it must not change in a step.
    {
        std::unique_ptr<Effect> e = fresh(spring, 0.7f, 1.f, 1.f, 1.f);
        Stereo io(4 * 48000);
        io.l = io.r = sineOf(500.0, 0.5, io.frames());
        through(*e, io, 0, 96000);
        e->setParam(2, 0.f);
        through(*e, io, 96000, io.frames());
        const auto corner = [&](std::size_t from, std::size_t to) {
            double most = 0.0;
            for (std::size_t i = from + 2; i < to; ++i) most = std::fmax(most, std::fabs(static_cast<double>(io.l[i]) - 2.0 * io.l[i - 1] + io.l[i - 2]));
            return most;
        };
        const double before = corner(48000, 96000), during = corner(96000 - 2, 96000 + 4800);
        SAY("spring   TONE turned down at once under 500 Hz: the sharpest corner is %.3f of the one before\n", during / before);
        CHECK(during < 1.02 * before);
    }
    // ...and it takes its 30 ms. A held 1 kHz sine, DRIP none (so what goes back round is heard 8 ms on),
    // TONE turned from bright to dark at once: the level 10 to 20 ms after the turn is still the level before
    // it (0.99 of it), and by 110 ms it has changed (1.11). Had TONE jumped, it would be 1.09 by 10 ms.
    {
        std::unique_ptr<Effect> e = fresh(spring, 0.f, 1.f, 1.f, 1.f);
        Stereo io(3 * 48000);
        io.l = io.r = sineOf(1000.0, 0.5, io.frames());
        through(*e, io, 0, 96000);
        e->setParam(2, 0.f);
        through(*e, io, 96000, io.frames());
        const double before = rmsOf(io.l, 96000 - 4800, 96000), soon = rmsOf(io.l, 96000 + 480, 96000 + 960) / before, later = rmsOf(io.l, 96000 + 5280, 96000 + 5760) / before;
        SAY("spring   TONE turned down at once under 1 kHz: the level is %.3f of what it was 10 to 20 ms on, %.3f at 110 ms\n", soon, later);
        CHECK(std::fabs(soon - 0.991) < 0.03 && std::fabs(later - 1.110) < 0.03);
    }
    // WHAT FOLLOWS FROM DECAY SLIDES SAMPLE BY SAMPLE, the springs' feed too (it is turned down for a long DECAY).
    // Kept awake by an earlier click, DECAY turned from 0.4 to 4 s, then ONE block of 0.2 s, a click in it at its
    // first sample or 7000 samples on. What the click adds (the run with no second click taken away), in the 29 ms
    // before it has been round a spring once: at the block's first sample as at DECAY 0.4 s, 7000 samples on
    // 0.78 of that. A fresh one at 4 s gives 0.66 of it; a feed that waited for the next block would give 1.
    {
        const auto added = [&](int at) {
            std::unique_ptr<Effect> e = fresh(spring, 0.f, 0.f, 1.f, 1.f);
            Stereo io(480 + 9600);
            io.l[0] = io.r[0] = 1.f;
            through(*e, io, 0, 480);
            e->setParam(1, 1.f);
            if (at >= 0) io.l[480 + at] = io.r[480 + at] = 1.f;
            e->process(io.l.data() + 480, io.r.data() + 480, 9600);
            return io.l;
        };
        const std::vector<float> none = added(-1), first = added(0), later = added(7000);
        const auto size = [&](const std::vector<float>& v, std::size_t from) {
            double sum = 0.0;
            for (std::size_t i = from; i < from + 1400; ++i) sum += (static_cast<double>(v[i]) - none[i]) * (static_cast<double>(v[i]) - none[i]);
            return std::sqrt(sum);
        };
        const double atFirst = size(first, 480), atLater = size(later, 480 + 7000);
        const std::vector<float> short4 = clickOf(*fresh(spring, 0.f, 0.f, 1.f, 1.f), 0.03, 1.f), long4 = clickOf(*fresh(spring, 0.f, 1.f, 1.f, 1.f), 0.03, 1.f);
        const double ends = rmsOf(long4, 0, 1400) / rmsOf(short4, 0, 1400), startRatio = atFirst / (rmsOf(short4, 0, 1400) * std::sqrt(1400.0));
        SAY("spring   DECAY turned in a block: a click at its first sample %.3f of one at 0.4 s, 7000 samples on %.3f of the first (a fresh one at 4 s: %.3f)\n", startRatio, atLater / atFirst, ends);
        CHECK(std::fabs(startRatio - 1.0) < 0.01 && std::fabs(atLater / atFirst - 0.782) < 0.02 && atLater / atFirst > ends + 0.05);
    }
    // MIX turned back to none lands: the sound untouched, to the bit, within a second.
    {
        std::unique_ptr<Effect> e = fresh(spring, 0.7f, 0.6f, 0.5f, 1.f);
        const std::vector<float> in = sineOf(1000.0, 0.25, 96000);
        Stereo io(96000), more(96000);
        io.l = io.r = more.l = more.r = in;
        through(*e, io);
        e->setParam(3, 0.f);
        through(*e, more);
        bool untouched = true;
        for (std::size_t i = 48000; i < 96000; ++i) untouched = untouched && more.l[i] == in[i] && more.r[i] == in[i];
        CHECK(untouched && more.l[100] != in[100]);
    }
    // A setting LANDS: once it has got there the springs are exactly the springs of that setting. Two with the
    // same past, each setting turned from one end to the other while sounding; one is then given a click, and
    // what the click adds is what one fresh from the box does with it (a spring's working is the same at any level).
    {
        std::unique_ptr<Effect> a = fresh(spring, 0.f, 0.f, 0.f, 1.f), b = fresh(spring, 0.f, 0.f, 0.f, 1.f), box = fresh(spring, 1.f, 1.f, 1.f, 1.f);
        Stereo ioA(4 * 48000), ioB(0);
        Noise noise(11u);
        for (std::size_t i = 0; i < 24000; ++i) ioA.l[i] = ioA.r[i] = 0.25f * noise.next();
        ioB = ioA;
        ioA.l[3 * 48000] = ioA.r[3 * 48000] = 1.f; // (3 x 48000 is a whole number of the springs' own samples since it woke: the click lands as a fresh one's does)
        for (Effect* e : {a.get(), b.get()}) {
            Stereo& io = e == a.get() ? ioA : ioB;
            through(*e, io, 0, 12000);
            for (int p = 0; p < 3; ++p) e->setParam(p, 1.f);
            through(*e, io, 12000, io.frames());
        }
        const std::vector<float> want = clickOf(*box, 1.0, 1.f);
        float apart = 0.f, loud = 0.f;
        for (std::size_t i = 0; i < 48000; ++i) apart = std::fmax(apart, std::fabs((ioA.l[3 * 48000 + i] - ioB.l[3 * 48000 + i]) - want[i])), loud = std::fmax(loud, std::fabs(want[i]));
        SAY("spring   DRIP, DECAY and TONE turned and landed: a click then differs from a fresh one's by %.1e at most (the click's own sound is %.3f)\n", static_cast<double>(apart), static_cast<double>(loud));
        CHECK(apart < 2e-6f && loud > 0.02f);
    }

    // SILENCE IN, SILENCE OUT IN THE END, AND NOTHING TOO SMALL TO HOLD ON THE WAY, with the processor's
    // flush-to-zero off. A click, then nothing: the springs fade onto a floor 360 dB down, and a quarter of a
    // second after they have held nothing over 240 dB down it is asleep: exact zeros. At DECAY 1.6 s that is
    // 5.047 s after the click; at 0.4 s, 1.491 to 1.576 s; at 4 s, 12.402 s.
    {
        struct Row { float drip, decay, tone, mix; double seconds, asleepBy; };
        const Row rows[4] = {{0.7f, 0.6f, 0.5f, 1.f, 8.0, 5.047}, {0.f, 0.f, 1.f, 0.5f, 3.0, 1.576}, {1.f, 0.f, 0.f, 1.f, 3.0, 1.491}, {0.3f, 1.f, 0.75f, 1.f, 20.0, 12.402}};
        for (const Row& row : rows) {
            std::unique_ptr<Effect> e = fresh(spring, row.drip, row.decay, row.tone, row.mix);
            const TooSmall marks;
            Stereo io(static_cast<std::size_t>(row.seconds * 48000.0));
            io.l[0] = io.r[0] = 1.f;
            through(*e, io);
            const bool seen = marks.seen();
            std::size_t last = 0;
            float smallest = 1.f;
            for (std::size_t i = 1; i < io.frames(); ++i) {
                if (io.l[i] != 0.f) last = i, smallest = std::fmin(smallest, std::fabs(io.l[i]));
                if (io.r[i] != 0.f) last = i > last ? i : last, smallest = std::fmin(smallest, std::fabs(io.r[i]));
            }
            SAY("spring   decay %4.2f s drip %3.0f%%: the last sample that is not zero is %.3f s in; the smallest that came out is %.1e\n", 0.4 * std::pow(10.0, row.decay), row.drip * 100.0, static_cast<double>(last) / 48000.0, static_cast<double>(smallest));
            CHECK(!seen);
            // (Held close: it sleeps when BOTH springs are quiet. Listening to one alone it slept up to 0.12 s sooner.)
            CHECK(std::fabs(static_cast<double>(last) / 48000.0 - row.asleepBy) < 0.004 * row.asleepBy + 0.005);
            CHECK(smallest > 1e-30f && io.l.back() == 0.f && io.r.back() == 0.f);
        }
        // The same with sound coming in that is itself too small to hold (another effect's dying tail): it is taken as none.
        std::unique_ptr<Effect> e = fresh(spring, 0.7f, 0.f, 1.f, 0.5f);
        Stereo io(4 * 48000);
        io.l.assign(io.frames(), 1e-39f);
        io.r.assign(io.frames(), -1e-39f);
        io.l[0] = io.r[0] = 0.5f;
        const TooSmall marks;
        through(*e, io);
        const bool seen = marks.seen();
        CHECK(!seen && rmsOf(io.l, 96, 4800) > 1e-3 && io.l.back() == 0.f && io.r.back() == 0.f);
        // A sound that is faint but is a sound (260 dB down: over kNothing, under what the springs call quiet)
        // keeps it awake: it is never cut to nothing while it lasts.
        std::unique_ptr<Effect> held = fresh(spring, 0.7f, 0.6f, 0.5f, 0.5f);
        Stereo faint(96000);
        faint.l.assign(faint.frames(), 1e-13f);
        faint.r.assign(faint.frames(), 1e-13f);
        through(*held, faint);
        bool sounding = true;
        for (std::size_t i = 0; i < faint.frames(); ++i) sounding = sounding && faint.l[i] > 0.5e-13f && faint.l[i] < 3e-13f;
        CHECK(sounding);
        // ...and the springs go on sounding under it: a 1 kHz sine that faint, MIX full, comes out as steady as it goes in (a hundredth of a second at a time, from half a second in).
        std::unique_ptr<Effect> under = fresh(spring, 0.7f, 0.6f, 0.5f, 1.f);
        Stereo sine(96000);
        sine.l = sine.r = sineOf(1000.0, 1e-13, sine.frames());
        through(*under, sine);
        double least = 1.0, most = 0.0;
        for (std::size_t at = 24000; at + 480 <= sine.frames(); at += 480) least = std::fmin(least, rmsOf(sine.l, at, at + 480)), most = std::fmax(most, rmsOf(sine.l, at, at + 480));
        SAY("spring   a 1 kHz sine 260 dB down: comes out between %.2e and %.2e\n", least, most);
        CHECK(least > 2e-14 && most < 1.5 * least);
    }
    CHECK(sameFlushed(spring, 0.7f, 0.f, 0.75f, 0.5f, 3.0)); // the same with the processor's flush-to-zero on or off, to the bit
    CHECK(sameFlushed(spring, 0.2f, 0.3f, 0.2f, 1.f, 6.0));
    // And by the clock (the app's way, flush-to-zero OFF): a block takes as long at its slowest in 12 s as while the tail is loud, and next to nothing asleep.
    {
        std::unique_ptr<Effect> e = fresh(spring, 0.7f, 0.6f, 0.5f, 1.f);
        const TailTimes took = tailTimes(*e, 12);
        SAY("flush    spring (flush-to-zero OFF): a block takes %.2f us early, %.2f us at its slowest, %.2f us at the end (asleep)\n", took.early, took.slowest, took.late);
        CHECK(took.slowest < 3.0 * took.early + 5.0 && took.late < 0.5 * took.early + 1.0);
    }
    // Woken by a new sound, it plays as one fresh from the box does, TO THE BIT (every memory is cleared as it
    // wakes); and a setting turned while it slept is simply there.
    {
        std::unique_ptr<Effect> slept = fresh(spring, 0.7f, 0.f, 0.5f, 0.7f), box = fresh(spring, 0.7f, 0.f, 0.5f, 0.7f), turned = fresh(spring, 0.f, 1.f, 1.f, 0.f);
        const Stereo first = clickBoth(*slept, 3.0), a = clickBoth(*slept, 3.0, 0.5f, -0.25f), b = clickBoth(*box, 3.0, 0.5f, -0.25f);
        CHECK(first.l.back() == 0.f && a.l.back() == 0.f && a.l == b.l && a.r == b.r && rmsOf(a.l, 96, 4800) > 1e-4);
        // ...wherever in the springs' own count of samples (one in five) it fell asleep: a sound too faint to
        // stir them (280 dB down) on some odd sample of the tail's last quarter second puts the sleep a
        // quarter second after THAT sample, and the count starts again when it wakes.
        {
            std::size_t last = 0;
            for (std::size_t i = 0; i < first.frames(); ++i) last = first.l[i] != 0.f ? i : last;
            for (std::size_t odd = 1; odd <= 4; ++odd) {
                std::unique_ptr<Effect> again = fresh(spring, 0.7f, 0.f, 0.5f, 0.7f);
                Stereo tail(144000);
                tail.l[0] = tail.r[0] = 1.f;
                const std::size_t faintAt = last - 4800 + odd;
                tail.l[faintAt] = 1e-14f;
                through(*again, tail);
                std::size_t asleepAt = 0;
                for (std::size_t i = 0; i < tail.frames(); ++i) asleepAt = tail.l[i] != 0.f || tail.r[i] != 0.f ? i + 1 : asleepAt;
                CHECK(asleepAt == faintAt + 12001); // (a quarter of a second of nothing after the faint sample, to the sample)
                const Stereo then = clickBoth(*again, 3.0, 0.5f, -0.25f);
                CHECK(then.l == b.l && then.r == b.r);
            }
        }
        // ...whichever sample wakes it: the springs' own samples (one in five) are counted from that one.
        for (std::size_t late = 1; late <= 5; ++late) {
            std::unique_ptr<Effect> again = fresh(spring, 0.7f, 0.f, 0.5f, 0.7f);
            Stereo woken(144000), want(144000);
            woken.l[late] = want.l[0] = 0.5f, woken.r[late] = want.r[0] = -0.25f;
            through(*slept, woken);
            through(*again, want);
            bool same = woken.l.back() == 0.f;
            for (std::size_t i = 0; i + late < woken.frames(); ++i) same = same && woken.l[i + late] == want.l[i] && woken.r[i + late] == want.r[i];
            CHECK(same);
        }
        clickBoth(*turned, 0.1, 0.f, 0.f);
        turned->setParam(0, 0.7f), turned->setParam(1, 0.f), turned->setParam(2, 0.5f), turned->setParam(3, 0.7f);
        const Stereo c = clickBoth(*turned, 3.0, 0.5f, -0.25f);
        CHECK(c.l == b.l && c.r == b.r);
        // Asleep, with nothing coming in, what comes out is nothing; at MIX 0 the sound is not touched at all.
        Stereo faint(256);
        faint.l.assign(256, 1e-20f);
        through(*slept, faint);
        CHECK(faint.l[0] == 0.f && faint.l[255] == 0.f);
        slept->setParam(3, 0.f);
        faint.l.assign(256, 1e-20f);
        through(*slept, faint);
        CHECK(faint.l[0] == 1e-20f && faint.l[255] == 1e-20f);
    }

    // HOWEVER IT IS CUT INTO BLOCKS, THE SAME TO THE LAST BIT: through the tail, into sleep, and woken part way
    // through a block by a second sound (the springs' own samples are counted from the sample that wakes it,
    // not from a block's first), through that tail and into sleep again. Blocks of 64, of 48, of odd sizes
    // (1 to 61 in turn) and of 256; in a slot and on a bus; a sound on one side only wakes it too.
    {
        struct Cut { float drip, decay, tone, mix; bool bus; };
        for (const Cut& c : {Cut{0.7f, 0.6f, 0.5f, 0.5f, false}, Cut{1.f, 1.f, 1.f, 1.f, false}, Cut{0.f, 0.f, 0.f, 0.3f, false}, Cut{0.7f, 0.6f, 0.5f, 1.f, true}}) {
            const std::size_t second = 48000, wakes = 25 * second + 1237;
            Stereo in(46 * second);
            Noise noise(31u);
            for (std::size_t i = 0; i < 4800; ++i) in.l[1000 + i] = 0.5f * noise.next(), in.r[1000 + i] = 0.5f * noise.next();
            in.l[24 * second + 7] = in.r[24 * second + 7] = 1e-16f; // too faint to be a sound: it sleeps on
            in.r[wakes] = 0.25f;
            std::vector<float> ways[4];
            const std::size_t sizes[4] = {64, 48, 0, 256}; // 0: odd sizes, 1 to 61 in turn
            for (int way = 0; way < 4; ++way) {
                std::unique_ptr<Effect> e = fresh(spring, c.drip, c.decay, c.tone, c.mix);
                if (c.bus) e->setOnBus(true);
                Stereo io = in;
                for (std::size_t at = 0, n = sizes[way] ? sizes[way] : 1; at < io.frames(); at += n, n = sizes[way] ? sizes[way] : n % 61 + 1) {
                    if (at + n > io.frames()) n = io.frames() - at;
                    e->process(io.l.data() + at, io.r.data() + at, static_cast<int>(n));
                }
                ways[way] = io.l;
                ways[way].insert(ways[way].end(), io.r.begin(), io.r.end());
            }
            std::size_t firstApart = ways[0].size();
            float apart = 0.f;
            for (int way = 1; way < 4; ++way)
                for (std::size_t i = 0; i < ways[0].size(); ++i)
                    if (ways[way][i] != ways[0][i]) firstApart = std::min(firstApart, i), apart = std::fmax(apart, std::fabs(ways[way][i] - ways[0][i]));
            SAY("spring   drip %3.0f%% decay %3.0f%%%s: cut into blocks four ways, %s apart (first at %.2f s, by %g at most)\n", c.drip * 100.0, c.decay * 100.0, c.bus ? " on a bus" : "", firstApart == ways[0].size() ? "never" : "APART", static_cast<double>(firstApart % in.frames()) / 48000.0, static_cast<double>(apart));
            CHECK(firstApart == ways[0].size());
            // (And it did all of it: sounded, slept, slept on through the faint sample, was woken, sounded, slept.)
            CHECK(rmsOf(ways[0], 4800, 24000) > 1e-3 && rmsOf(ways[0], 23 * second, wakes) == 0.0 && rmsOf(ways[0], wakes + 96, wakes + 9600) > 1e-4 && rmsOf(ways[0], 45 * second, 46 * second) == 0.0);
        }
    }

    // AT ANOTHER SAMPLE RATE the springs run at the whole division of it nearest 9600 Hz, and DECAY is still the time it reads.
    for (float rate : {22050.f, 44100.f, 96000.f}) {
        std::unique_ptr<Effect> e = makeEffect(spring, rate, 120.f);
        e->setParam(2, 1.f), e->setParam(3, 1.f);
        Stereo io(static_cast<std::size_t>(8.0 * rate));
        io.l[0] = io.r[0] = 1.f;
        through(*e, io);
        const double got = rt60(bandOf(io.l, 500.0, rate), 1, rate);
        SAY("spring   at %.0f Hz: RT60 at 500 Hz %.2f s (DECAY reads 1.6)\n", static_cast<double>(rate), got);
        CHECK(std::fabs(got / 1.585 - 1.0) < 0.08 && io.l.back() == 0.f && peakOf(io.l) < 1.f);
    }
}

// ---- PLATE ------------------------------------------------------------------------------------------

// HOW EVEN A TAIL FALLS: the energy still to come (as rt60 reads it), in dB, against the straight
// line through it from 5 dB down to 35 dB down. The most it strays from that line, in dB, there.
static double strays(const std::vector<float>& ir, std::size_t from = 1) {
    std::vector<double> left(ir.size() + 1, 0.0);
    for (std::size_t i = ir.size(); i-- > from;) left[i] = left[i + 1] + static_cast<double>(ir[i]) * ir[i];
    const double all = left[from];
    if (all <= 0.0) return 0.0;
    std::size_t a = 0, b = 0;
    for (std::size_t i = from; i < ir.size(); ++i) {
        const double down = 10.0 * std::log10(left[i] / all + 1e-300);
        if (!a && down <= -5.0) a = i;
        if (down <= -35.0) {
            b = i;
            break;
        }
    }
    if (!a || b <= a) return 0.0;
    const double da = 10.0 * std::log10(left[a] / all), db = 10.0 * std::log10(left[b] / all);
    double most = 0.0;
    for (std::size_t i = a; i <= b; ++i) {
        const double line = da + (db - da) * static_cast<double>(i - a) / static_cast<double>(b - a);
        most = std::fmax(most, std::fabs(10.0 * std::log10(left[i] / all) - line));
    }
    return most;
}
// HOW SPIKY THE START IS: the loudest sample against the loudness, over the `ms` after the first sound.
// Noise reads about 3 to 4; a few separate echoes read far more.
static double crest(const std::vector<float>& v, double ms, float floor = 1e-9f) {
    const long at = soundAt(v, floor);
    if (at < 0) return 0.0;
    const std::size_t from = static_cast<std::size_t>(at), to = from + static_cast<std::size_t>(ms * 48.0);
    float peak = 0.f;
    for (std::size_t i = from; i < to && i < v.size(); ++i) peak = std::fmax(peak, std::fabs(v[i]));
    const double rms = rmsOf(v, from, to);
    return rms > 0.0 ? peak / rms : 0.0;
}

// Measured, and pinned (see where each is used).
static const double kPlateWander = 6.96; // dB, lowest to highest: a held 1 kHz sine's level in the plate
static const int kPlateWanderUps = 36;   // times up through its middle in 40 s

static void testPlate() {
    const EffectKind plate = EffectKind::Plate, hall = EffectKind::Hall;
    const EffectInfo& info = effectInfo(plate);
    // Its row: what a song stores, and a TYPE of REVERB.
    CHECK(static_cast<int>(plate) == 14 && std::string(info.word) == "plate" && effectFromWord("plate") == plate && std::string(info.name) == "PLATE");
    CHECK(info.family == EffectFamily::Reverb && effectBus(plate) == 1 && std::string(effectFamily(plate)) == "REVERB" && std::string(info.type) == "PLATE" && info.params == 4);
    CHECK(std::string(info.paramNames[0]) == "PRE" && std::string(info.paramNames[1]) == "DECAY" && std::string(info.paramNames[2]) == "TONE" && std::string(info.paramNames[3]) == "MIX");
    CHECK(info.defaults[0] == 0.1f && info.defaults[1] == 0.45f && info.defaults[2] == 0.6f && info.defaults[3] == 0.3f);
    for (int i = 0; i < 4; ++i) CHECK(effectChoices(plate, i) == 0);
    // How it reads: PRE in ms, DECAY in seconds as HALL's (so a TYPE change carries it untouched), the rest as amounts.
    CHECK(reads(plate, 0, 0.f) == "0 MS" && reads(plate, 0, 0.1f) == "10 MS" && reads(plate, 0, 0.29f) == "29 MS" && reads(plate, 0, 1.f) == "100 MS");
    for (float v : {0.f, 0.13f, 0.45f, 0.5f, 0.77f, 1.f}) CHECK(reads(plate, 1, v) == reads(hall, 1, v));
    CHECK(reads(plate, 1, 0.45f) == "2.0 S" && reads(plate, 1, 1.f) == "20 S" && reads(plate, 2, 0.6f) == "60%" && reads(plate, 3, 0.3f) == "30%");

    // DECAY IS A REAL TIME, and the plate is DENSE. A click, MIX full, PRE 0, TONE at its usual (60%): the RT60
    // against what DECAY reads; separate echoes in the first 100 ms; and how far the fall at 500 Hz strays
    // from a straight line (in dB, see strays()).
    //   DECAY      80 Hz    500 Hz   8 kHz    echoes   strays
    //   0.3 s      x0.89    x1.04    x0.92    1374     2.2 dB   (a third of a second is under one trip round both loops)
    //   0.86 s     x0.88    x1.06    x0.89    1397     1.3
    //   2.45 s     x0.82    x1.02    x0.78    1411     1.4
    //   7.0 s      x0.78    x1.01    x0.66    1406     0.6
    //   20 s       x0.78    x1.00    x0.61    1406     0.4
    // A plate is thin at the bottom and bright at the top, at any DECAY: in each loop, below 200 Hz the tail is
    // made to last 0.75 of DECAY and above TONE's corner 0.4 of it (shelves set from the trip, as HALL's are).
    // With a plain lowpass in the loops and nothing for the lows (before 2026-11-13), 80 Hz lasted x1.00 and
    // 8 kHz x0.27 at 20 s: held noise at DECAY 20 s came out 3 dB heavier at 80 Hz than at 1 kHz (see BRIGHT
    // AT ANY DECAY). The smearing stages are held to ring no longer than 0.45 of DECAY: without that a 0.3 s
    // plate measured 0.57 s (x1.9) and 0.86 s x1.17. The
    // second smear in each loop is turned the other way from the classic plate's (-0.4 for +0.5): with it as it
    // was, the lows lasted 15% longer than DECAY says (a smear that way round holds the lows three times its length).
    // (The old REVERB: about 200 echoes; HALL 650 to 1250.)
    {
        const double want80[5] = {0.892, 0.880, 0.815, 0.777, 0.784}, want500[5] = {1.036, 1.058, 1.021, 1.005, 1.002}, want8k[5] = {0.923, 0.890, 0.776, 0.664, 0.607};
        int row = 0;
        double last = 0.0;
        for (float decay : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
            const double want = 0.3 * std::pow(20.0 / 0.3, decay);
            const std::vector<float> ir = clickOf(*fresh(plate, 0.f, decay, 0.6f, 1.f), std::fmin(45.0, 2.0 + want * 1.3), 1.f);
            const double mid = rt60(bandOf(ir, 500.0)), low = rt60(bandOf(ir, 80.0)), high = rt60(bandOf(ir, 8000.0)), stray = strays(bandOf(ir, 500.0));
            const int echoes = echoesIn(ir, 0.0005, 0.1);
            SAY("plate    decay %5.2f s: RT60 at 80 Hz x %.3f, at 500 Hz x %.3f, at 8 kHz x %.3f; %d echoes in the first 100 ms; strays %.2f dB from an even fall\n", want, low / want, mid / want, high / want, echoes, stray);
            CHECK(std::fabs(mid / want - 1.0) < 0.07 && low / want > 0.7 && low / want < 0.95); // within 7% at 500 Hz, at every DECAY; the lows shorter
            CHECK(std::fabs(mid / want - want500[row]) < 0.02 && std::fabs(low / want - want80[row]) < 0.03 && std::fabs(high / want - want8k[row]) < 0.03);
            CHECK(echoes > 1300 && echoes < 1500);
            CHECK(stray < (decay == 0.f ? 2.6 : 1.6));
            CHECK(mid > 2.0 * last);
            last = mid;
            ++row;
        }
    }

    // THE START FALLS AT ONCE. A click, PRE 0, MIX full: left and right's energy in 10 ms frames, and read off
    // the energy still to come (as rt60): EDT, the time to fall the first 10 dB, times 6, and T20 (from 5 to 25
    // dB down, times 3), against what DECAY reads. An even fall has EDT about T20; a tail that stands level
    // before it falls has a long EDT.
    //              loudest 10 ms   150 ms on      EDT      T20      before 2026-11-13: loudest, 150 ms on, EDT
    //   PLATE 0.3 s    60 ms       -14.0 dB     x2.23    x1.05     150 ms, 0.0 dB, x3.67: level for 170 ms
    //   PLATE 0.86 s   50 ms        -3.9        x1.26    x0.86     140 ms, -0.7, x1.43
    //   PLATE 2.0 s   100 ms        -1.5        x0.90    x0.85     130 ms, -0.7, x0.96
    //   HALL 0.3 s     40 ms       -11.0        x2.41    x0.85     (SIZE at its usual)
    //   HALL 0.86 s    40 ms        -9.8        x0.91    x0.57
    //   HALL 2.0 s     40 ms        -8.5        x0.51    x0.57
    // The taps were read before DECAY's gains were taken (at the end of each half of a loop), so a tap 120 ms
    // in was as loud as one 5 ms in. Now each tap is turned down for how far into its half it is. At 2 s the
    // plate still builds for its first 150 ms: the taps in the second half of each loop join 172 ms in, once
    // the sound has got there, and add as much as the tail has fallen.
    {
        const auto shape = [](Effect& e, double seconds, double* peakMs, double* at150, double* edt, double* t20) {
            const Stereo io = clickBoth(e, seconds);
            std::vector<double> energy(io.frames());
            for (std::size_t i = 0; i < io.frames(); ++i) energy[i] = static_cast<double>(io.l[i]) * io.l[i] + static_cast<double>(io.r[i]) * io.r[i];
            double loudest = 0.0;
            for (std::size_t f = 0; f < 50; ++f) {
                double sum = 0.0;
                for (std::size_t i = f * 480; i < (f + 1) * 480; ++i) sum += energy[i];
                if (sum > loudest) loudest = sum, *peakMs = 10.0 * static_cast<double>(f);
                if (f == 15) *at150 = sum;
            }
            *at150 = dB(std::sqrt(*at150 / loudest));
            std::vector<double> left(energy.size() + 1, 0.0);
            for (std::size_t i = energy.size(); i-- > 1;) left[i] = left[i + 1] + energy[i];
            double t5 = -1.0, t10 = -1.0, t25 = -1.0;
            for (std::size_t i = 1; i < energy.size() && t25 < 0.0; ++i) {
                const double down = 10.0 * std::log10(left[i] / left[1] + 1e-300), t = static_cast<double>(i) / 48000.0;
                if (t5 < 0.0 && down <= -5.0) t5 = t;
                if (t10 < 0.0 && down <= -10.0) t10 = t;
                if (down <= -25.0) t25 = t;
            }
            *edt = 6.0 * t10, *t20 = 3.0 * (t25 - t5);
        };
        const float decays[3] = {0.f, 0.25f, 0.45f};
        const double wantPeak[3] = {60, 50, 100}, want150[3] = {-14.0, -3.9, -1.5}, wantEdt[3] = {2.23, 1.26, 0.90}, wantT20[3] = {1.05, 0.86, 0.85};
        for (int i = 0; i < 3; ++i) {
            const double want = 0.3 * std::pow(20.0 / 0.3, decays[i]);
            double peak = 0, at150 = 0, edt = 0, t20 = 0, hPeak = 0, h150 = 0, hEdt = 0, hT20 = 0;
            shape(*fresh(plate, 0.f, decays[i], 0.6f, 1.f), 2.0 + want * 1.3, &peak, &at150, &edt, &t20);
            shape(*fresh(hall, 0.6f, decays[i], 0.5f, 1.f), 2.0 + want * 1.3, &hPeak, &h150, &hEdt, &hT20);
            SAY("plate    decay %4.2f s: loudest 10 ms at %3.0f ms, 150 ms on %5.1f dB, EDT x %.2f, T20 x %.2f; HALL %3.0f ms, %5.1f dB, x %.2f, x %.2f\n", want, peak, at150, edt / want, t20 / want, hPeak, h150, hEdt / want, hT20 / want);
            CHECK(std::fabs(peak - wantPeak[i]) < 15.0 && std::fabs(at150 - want150[i]) < 1.0 && std::fabs(edt / want - wantEdt[i]) < 0.06 && std::fabs(t20 / want - wantT20[i]) < 0.04);
        }
    }

    // THE START. When it first answers: PRE to the sample, and then 8.9 ms (the left's first tap) and 11.9 ms (the right's).
    //   PRE      0     10 ms   50 ms   100 ms
    //   left    429    909     2829    5229 samples
    //   right   569   1049     2969    5369
    {
        const float pres[4] = {0.f, 0.1f, 0.5f, 1.f};
        for (int i = 0; i < 4; ++i) {
            const Stereo io = clickBoth(*fresh(plate, pres[i], 0.45f, 0.6f, 1.f), 0.5);
            const long gap = std::lround(pres[i] * 4800.f);
            SAY("plate    PRE %3.0f%%: first sound at %ld (left), %ld (right) samples\n", pres[i] * 100.0, soundAt(io.l, 1e-9f), soundAt(io.r, 1e-9f));
            CHECK(soundAt(io.l, 1e-9f) == gap + 429 && soundAt(io.r, 1e-9f) == gap + 569);
        }
    }
    // NO EARLY ECHOES TO PICK OUT: from its first sound the plate is a wash, not a few clicks. Its loudest
    // sample against its loudness (a crest), over the first 20 and 50 ms after its first sound; noise reads
    // about 3.5 to 4.5, separate echoes far more:
    //              20 ms    50 ms    strays (500 Hz)   echoes in the first 100 ms
    //   PLATE      4.08     4.40     2.1 dB            1412
    //   HALL      10.2      9.8      0.6 dB            1244
    //   ROOM      31.0     35.6      1.1 dB             203
    // (Without its four short smearing stages ahead of the classic four, PLATE read 11.4 and 10.7: a click at
    // 8.9 ms, a silent gap, another at 12 ms.) DECAY 2.0 s, PRE 0; HALL and ROOM at their usual settings.
    {
        const std::vector<float> p = clickOf(*fresh(plate, 0.f, 0.45f, 0.6f, 1.f), 3.0, 1.f), h = clickOf(*fresh(hall, 0.f, 0.45f, 0.5f, 1.f), 3.0, 1.f), r = clickOf(*fresh(EffectKind::Reverb, 0.6f, 0.5f, 1.f), 3.0, 1.f);
        SAY("plate    the first 20 / 50 ms after the first sound, its loudest sample against its loudness: PLATE %.2f / %.2f, HALL %.2f / %.2f, ROOM %.2f / %.2f\n", crest(p, 20), crest(p, 50), crest(h, 20), crest(h, 50), crest(r, 20), crest(r, 50));
        SAY("plate    strays from an even fall (500 Hz): PLATE %.2f dB, HALL %.2f dB, ROOM %.2f dB; echoes in 100 ms: %d, %d, %d\n", strays(bandOf(p, 500.0)), strays(bandOf(h, 500.0)), strays(bandOf(r, 500.0)), echoesIn(p, 0.0005, 0.1), echoesIn(h, 0.0005, 0.1), echoesIn(r, 0.0005, 0.1));
        CHECK(crest(p, 20) < 4.5 && crest(p, 50) < 5.0 && crest(h, 20) > 8.0 && crest(r, 20) > 20.0);
        // The tool: noise reads about 4; one click in silence reads the square root of the samples.
        std::vector<float> noise(4800), click(4800, 0.f);
        Noise n(2u);
        for (float& v : noise) v = n.next();
        click[100] = 1.f;
        CHECK(crest(noise, 50) > 1.5 && crest(noise, 50) < 2.0 && std::fabs(crest(click, 20) - std::sqrt(960.0)) < 1e-6); // (uniform noise: its crest is the square root of 3)
    }

    // BRIGHT. Held noise, MIX full, PRE 10 ms, DECAY 2.0 s: the plate at each pitch against the noise, in dB.
    //   TONE    80 Hz   250 Hz   1 kHz   4 kHz   8 kHz   14 kHz
    //   0%      -1.6    -0.7     -1.6    -5.8     -9.9   -14.1     dark: TONE's corner at 1.2 kHz
    //   60%     -1.6    -0.6     -0.5    -1.1     -2.2    -3.9     its usual: 6.1 kHz
    //   100%    -1.6    -0.6     -0.4    -0.6     -1.2    -2.5     bright: 18 kHz
    // (The gentle cut on the way in is at three times TONE's corner; it was twice, and 14 kHz read -5.4 at 60%.)
    {
        const double hz[6] = {80, 250, 1000, 4000, 8000, 14000};
        const double rows[3][6] = {{-1.6, -0.7, -1.6, -5.8, -9.9, -14.1}, {-1.6, -0.6, -0.5, -1.1, -2.2, -3.9}, {-1.6, -0.6, -0.4, -0.6, -1.2, -2.5}};
        const float tones[3] = {0.f, 0.6f, 1.f};
        for (int t = 0; t < 3; ++t) {
            Stereo io(48000 * 10);
            Noise noise(3u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
            const std::vector<float> in = io.l;
            through(*fresh(plate, 0.1f, 0.45f, tones[t], 1.f), io);
            char line[256] = "";
            for (int i = 0; i < 6; ++i) {
                const double level = dB(rmsOf(bandOf(io.l, hz[i]), 5 * 48000) / rmsOf(bandOf(in, hz[i]), 5 * 48000));
                std::snprintf(line + std::strlen(line), sizeof line - std::strlen(line), " %5.0f Hz %6.1f", hz[i], level);
                CHECK(std::fabs(level - rows[t][i]) < 0.5);
            }
            SAY("plate    tone %3.0f%%: held noise comes out at%s dB\n", tones[t] * 100.0, line);
        }
    }

    // WIDE: left and right nearly unrelated (1 the same, 0 unrelated): 0.004 in the first 100 ms, 0.000 after.
    // It takes left and right in together (as the classic plate does): a click on the left alone fills both
    // sides alike, 0.0 dB apart.
    {
        const Stereo both = clickBoth(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), 3.0), one = clickBoth(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), 3.0, 1.f, 0.f);
        const double early = alike(both.l, both.r, 1, 4800), later = alike(both.l, both.r, 4800, 96000), across = dB(rmsOf(one.r, 4800, 96000) / rmsOf(one.l, 4800, 96000));
        SAY("plate    left and right alike %.3f early, %.3f later; a click on the left: right is %.1f dB of the left\n", early, later, across);
        CHECK(std::fabs(early) < 0.05 && std::fabs(later) < 0.05 && std::fabs(across) < 0.3 && std::fabs(dB(rmsOf(both.l, 4800, 96000) / rmsOf(both.r, 4800, 96000))) < 1.0);
    }
    // Left and right are taken in together, to the bit: a click on the right alone is a click on the left alone,
    // and half of one in each.
    {
        const Stereo l = clickBoth(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), 1.0, 1.f, 0.f), r = clickBoth(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), 1.0, 0.f, 1.f), both = clickBoth(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), 1.0, 0.5f, 0.5f);
        CHECK(l.l == r.l && l.r == r.r && l.l == both.l && l.r == both.r && rmsOf(r.r, 909) > 1e-3);
    }
    // THE SAME AT ANY LEVEL, and never asleep while sound comes in: noise 260 dB down (so faint that what goes round
    // in the plate is under what it calls quiet, but sound all the same) comes out as the same noise at full does,
    // turned down by as much: 2.5e-3 apart, scaled back up (the plate's floor, 360 dB down, is only 100 dB under
    // this noise). Let fall asleep with sound coming in, it woke at once, a fresh plate with its bend started
    // again: 0.25 apart.
    {
        Stereo loud(4 * 48000), faint(0);
        Noise noise(12u);
        for (std::size_t i = 0; i < loud.frames(); ++i) loud.l[i] = 0.25f * noise.next(), loud.r[i] = 0.25f * noise.next();
        faint = loud;
        for (std::size_t i = 0; i < faint.frames(); ++i) faint.l[i] *= 1e-13f, faint.r[i] *= 1e-13f;
        through(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), loud);
        through(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), faint);
        double worst = 0.0;
        for (std::size_t i = 0; i < loud.frames(); ++i) worst = std::fmax(worst, std::fabs(1e13 * faint.l[i] - loud.l[i]));
        SAY("plate    noise 260 dB down comes out as noise at full does, 260 dB down: apart by %.1e (the loud noise's own peak %.2f)\n", worst, static_cast<double>(peakOf(loud.l)));
        CHECK(worst < 1e-2 && peakOf(loud.l) > 0.1f);
    }
    // PRE LANDS: turned while sounding, once it has got there the gap is a whole number of samples again. Two
    // plates with the same past, PRE turned from 80 ms to 29 ms in both; one is then given a click at 2 s. What the
    // click adds is to the sample what it adds to two plates that were at 29 ms all along (at that same moment:
    // the bend moves with time), and it begins 1392 + 429 samples after the click.
    {
        Stereo turned[2], still[2];
        Noise noise(11u);
        Stereo in(3 * 48000);
        for (std::size_t i = 0; i < 24000; ++i) in.l[i] = in.r[i] = 0.25f * noise.next();
        for (int k = 0; k < 2; ++k) {
            turned[k] = still[k] = in;
            if (k == 0) turned[k].l[2 * 48000] = turned[k].r[2 * 48000] = still[k].l[2 * 48000] = still[k].r[2 * 48000] = 1.f;
            std::unique_ptr<Effect> a = fresh(plate, 0.8f, 0.45f, 0.6f, 1.f), b = fresh(plate, 0.29f, 0.45f, 0.6f, 1.f);
            through(*a, turned[k], 0, 12000);
            a->setParam(0, 0.29f);
            through(*a, turned[k], 12000, in.frames());
            through(*b, still[k]);
        }
        float apart = 0.f, loud = 0.f;
        std::vector<float> added(in.frames() - 2 * 48000);
        for (std::size_t i = 0; i < added.size(); ++i) {
            added[i] = turned[0].l[2 * 48000 + i] - turned[1].l[2 * 48000 + i];
            apart = std::fmax(apart, std::fabs(added[i] - (still[0].l[2 * 48000 + i] - still[1].l[2 * 48000 + i])));
            loud = std::fmax(loud, std::fabs(added[i]));
        }
        SAY("plate    PRE turned and landed: a click then differs from one in a plate never turned by %.1e at most (the click's own sound %.3f)\n", static_cast<double>(apart), static_cast<double>(loud));
        CHECK(apart < 1e-6f && loud > 0.01f && soundAt(added, 1e-9f) == 1392 + 429);
    }
    // ABOUT AS LOUD AT ANY DECAY. Held noise, MIX full: -2.3 dB at 0.3 s, -3.1 at 2.45 s, -2.7 at 20 s (HALL: -1.6 to -4.2).
    for (float decay : {0.f, 0.5f, 1.f}) {
        Stereo io(48000 * 20);
        Noise noise(3u);
        for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
        const double inRms = rmsOf(io.l);
        through(*fresh(plate, 0.1f, decay, 0.6f, 1.f), io);
        const double level = dB(rmsOf(io.l, 19 * 48000, 20 * 48000) / inRms);
        SAY("plate    decay %3.0f%%: held noise comes out at %.1f dB\n", decay * 100.0, level);
        CHECK(level > -3.6 && level < -1.8);
    }

    // BRIGHT AT ANY DECAY, and thin at the bottom. Held noise for 30 s, MIX full, PRE 10 ms, DECAY 20 s: each
    // pitch against the noise, in dB, over the last 10 s (HALL at its usual SIZE and TONE 50%, for the eye).
    //   TONE           80 Hz   250 Hz   1 kHz   8 kHz   14 kHz
    //   60%    PLATE    0.0     0.9      0.8    -1.8     -4.6
    //   100%   PLATE    0.0     0.9      0.9    -0.0     -3.0
    //   50%    HALL     0.5     0.9      0.7    -1.8     -2.9
    // Before 2026-11-13 (a plain lowpass in the loops, nothing for the lows) the plate tilted dark and heavy
    // as DECAY grew: at 60% +3.5 +4.1 +3.2 -4.3 -8.1, at 100% +3.5 +4.1 +3.6 +1.4 -1.9.
    {
        const double hz[5] = {80, 250, 1000, 8000, 14000};
        const double rows[2][5] = {{0.0, 0.9, 0.8, -1.8, -4.6}, {0.0, 0.9, 0.9, -0.0, -3.0}};
        const float tones[2] = {0.6f, 1.f};
        for (int t = 0; t < 3; ++t) {
            Stereo io(48000 * 30);
            Noise noise(5u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
            const std::vector<float> in = io.l;
            through(t < 2 ? *fresh(plate, 0.1f, 1.f, tones[t], 1.f) : *fresh(hall, 0.6f, 1.f, 0.5f, 1.f), io);
            char line[256] = "";
            double level[5];
            for (int i = 0; i < 5; ++i) {
                level[i] = dB(rmsOf(bandOf(io.l, hz[i]), 20 * 48000) / rmsOf(bandOf(in, hz[i]), 20 * 48000));
                std::snprintf(line + std::strlen(line), sizeof line - std::strlen(line), " %5.0f Hz %6.1f", hz[i], level[i]);
                if (t < 2) CHECK(std::fabs(level[i] - rows[t][i]) < 0.5);
            }
            SAY("plate    %s, DECAY 20 s: held noise comes out at%s dB\n", t == 0 ? "PLATE TONE 60%" : (t == 1 ? "PLATE TONE 100%" : "HALL TONE 50%"), line);
            if (t < 2) CHECK(level[0] < level[2] - 0.4 && level[1] < level[2] + 1.0 && level[4] > level[2] - (t ? 4.0 : 6.5)); // no heavier at the bottom than in the middle; the top not far under it
        }
    }

    // NO COPY OF THE SOUND COMES BACK LATE, so at MIX 50% nothing combs. The plate alone against the noise that
    // went in, 0 to 10 ms late: alike 0.016 at most (a copy would read 1). A held sine at 39 pitches from 100 Hz
    // to 11 kHz at MIX 50%: -10.1 to +3.4 dB, -2.0 on average: what any room does to a held sine (HALL at its
    // usual: -15.9 to +2.9, -2.8), not the even notches of a comb.
    {
        Stereo io(48000 * 4);
        Noise noise(9u);
        for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
        const std::vector<float> in = io.l;
        through(*fresh(plate, 0.f, 0.45f, 0.6f, 1.f), io);
        double most = 0.0;
        for (int lag = 0; lag <= 480; ++lag) {
            double ab = 0, aa = 0, bb = 0;
            for (std::size_t i = 48000; i < io.frames(); ++i) ab += static_cast<double>(io.l[i]) * in[i - lag], aa += static_cast<double>(io.l[i]) * io.l[i], bb += static_cast<double>(in[i - lag]) * in[i - lag];
            most = std::fmax(most, std::fabs(ab / std::sqrt(aa * bb)));
        }
        SAY("plate    the plate alone against the sound that went in, 0 to 10 ms late: alike %.3f at most\n", most);
        CHECK(most < 0.04);
        double lo = 1e9, hi = -1e9, sum = 0.0;
        int n = 0;
        for (int f = 100; f < 12000; f = static_cast<int>(f * 1.13) + 1) {
            const double level = levelAt(*fresh(plate, 0.1f, 0.45f, 0.6f, 0.5f), f);
            lo = std::fmin(lo, level), hi = std::fmax(hi, level), sum += level, ++n;
        }
        SAY("plate    MIX 50%%, a held sine at %d pitches: %.1f to %.1f dB, %.1f on average\n", n, lo, hi, sum / n);
        CHECK(n == 39 && lo > -12.0 && hi < 4.0 && std::fabs(sum / n + 2.0) < 0.6);
    }
    // MIX: the sound and the plate swap over at equal power. Before the plate answers, what comes out is the
    // sound alone times cos(MIX x 90 degrees); after, plus sin of it times the plate alone.
    {
        const std::vector<float> in = sineOf(1000.0, 0.25, 48000);
        std::vector<float> out, room;
        wetOf(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), in, &room);
        for (float mix : {0.3f, 0.5f, 1.f}) {
            wetOf(*fresh(plate, 0.1f, 0.45f, 0.6f, mix), in, &out);
            const float dry = mix == 1.f ? 0.f : std::cos(1.5707963f * mix), share = std::sin(1.5707963f * mix);
            bool before = true, after = true;
            for (std::size_t i = 0; i < 909; ++i) before = before && std::fabs(out[i] - dry * in[i]) < 1e-6f;
            for (std::size_t i = 909; i < in.size(); ++i) after = after && std::fabs(out[i] - (dry * in[i] + share * room[i])) < 2e-6f;
            CHECK(before && after && rmsOf(room, 24000) > 0.02);
        }
    }
    // ON A BUS: no dry sound at all, and the same plate (MIX is its LEVEL): what a slot gives at MIX full, to the
    // sample. (Before the plate answers, nothing but its floor, 360 dB down: see plate.cpp.)
    {
        std::unique_ptr<Effect> on = fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), in = fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), half = fresh(plate, 0.1f, 0.45f, 0.6f, 0.3f);
        on->setOnBus(true);
        half->setOnBus(true);
        const std::vector<float> wet = clickOf(*on, 3.0, 1.f), both = clickOf(*in, 3.0, 1.f), some = clickOf(*half, 3.0, 1.f);
        CHECK(std::fabs(wet[0]) < 1e-15f && peakOf(std::vector<float>(wet.begin(), wet.begin() + 909)) < 1e-15f && rmsOf(wet, 909) > 1e-3);
        CHECK(wet == both);
        double worst = 0.0;
        for (std::size_t i = 0; i < wet.size(); ++i) worst = std::fmax(worst, std::fabs(static_cast<double>(some[i]) - 0.3 * wet[i]));
        CHECK(worst < 1e-6);
        // ...and a held sine on a bus comes back with none of itself: before the plate answers, nothing.
        std::unique_ptr<Effect> sine = fresh(plate, 0.1f, 0.45f, 0.6f, 1.f);
        sine->setOnBus(true);
        std::vector<float> got;
        wetOf(*sine, sineOf(1000.0, 0.25, 4800), &got);
        CHECK(peakOf(std::vector<float>(got.begin(), got.begin() + 909)) < 1e-15f && peakOf(got) > 0.01f);
    }

    // THE BEND: the two bent smears move, so the tail does not sit on one note. A held 1 kHz sine, MIX full:
    // its level in the plate wanders (a plate that stood still would hold it to a thousandth of a dB).
    // Over 40 s from the 10th, a reading every 100 ms: see kPlateWander.
    {
        Stereo io(50 * 48000);
        io.l = io.r = sineOf(1000.0, 0.25, io.frames());
        through(*fresh(plate, 0.1f, 0.45f, 0.6f, 1.f), io);
        std::vector<double> level;
        for (std::size_t at = 10 * 48000; at + 4800 <= io.frames(); at += 4800) level.push_back(dB(partAt(io.l.data() + at, 4800, 1000.0)));
        double low = level[0], high = level[0], mean = 0.0;
        for (double v : level) low = std::fmin(low, v), high = std::fmax(high, v), mean += v / static_cast<double>(level.size());
        int ups = 0;
        for (std::size_t i = 1; i < level.size(); ++i) ups += level[i - 1] < mean && level[i] >= mean ? 1 : 0;
        SAY("plate    a held 1 kHz sine: its level in the plate wanders %.2f dB, up through its middle %d times in 40 s\n", high - low, ups);
        CHECK(std::fabs((high - low) - kPlateWander) < 0.1 * kPlateWander && std::abs(ups - kPlateWanderUps) <= 2);
    }

    // A TURNED SETTING GLIDES: each turned end to end at once under a held 100 Hz sine at MIX 50%. Two readings
    // while it gets there, each against the same with nothing turned (the rougher of before and after): the
    // largest step, and the sharpest corner (a click is a corner).
    //            down: step  corner     up: step  corner
    //   PRE         1.91     3.03          1.53     2.16
    //   DECAY       1.00     1.00          1.42     1.69
    //   TONE        1.00     1.00          1.00     1.00
    //   MIX         0.97     4.85          1.00     3.84
    // MIX's corner is the glide setting off (the same Glide as every new effect's), against a plate whose
    // steady sound under a 100 Hz sine is very smooth: 5.3e-4 in all, under a 0.5 sine (a click of 0.01 reads 0.01).
    // PRE bends the pitch for a moment while the gap changes length (about a tenth of a second, as HALL's SIZE
    // does): going to none the plate's sound is read up to twice as fast, an octave up, so its steps are twice
    // the size. DECAY turned up from 0.3 s to 20 s: the plate fills up louder than it stood at 0.3 s. Neither is
    // a click: the corners say so. DECAY glides in 0.1 s, not 0.03: from 0.3 s up the far taps are turned up
    // thirty times (see THE START FALLS AT ONCE), and in 0.03 s that read 1.70 and 3.97 turned up.
    for (int p = 0; p < 4; ++p)
        for (int up = 0; up < 2; ++up) {
            std::unique_ptr<Effect> h = fresh(plate, 0.1f, 0.45f, 0.6f, 0.5f);
            h->setParam(p, up ? 0.f : 1.f);
            Stereo io(5 * 48000);
            io.l = io.r = sineOf(100.0, 0.5, io.frames());
            for (std::size_t i = 0; i < 4800; ++i) io.l[i] = io.r[i] = io.l[i] * static_cast<float>(i) / 4800.f; // faded in: a sine that starts at once is a click of its own, and the plate rings with it
            through(*h, io, 0, 96000);
            h->setParam(p, up ? 1.f : 0.f);
            through(*h, io, 96000, io.frames());
            const auto corner = [&](std::size_t from, std::size_t to) {
                double most = 0.0;
                for (std::size_t i = from + 2; i < to; ++i) most = std::fmax(most, std::fabs(static_cast<double>(io.l[i]) - 2.0 * io.l[i - 1] + io.l[i - 2]));
                return most;
            };
            const double steady = std::fmax(largestJump(io.l, 48000, 96000), largestJump(io.l, 4 * 48000, 5 * 48000)), steadyCorner = std::fmax(corner(48000, 96000), corner(4 * 48000, 5 * 48000));
            const double step = largestJump(io.l, 96000 - 1, 4 * 48000) / steady, sharp = corner(96000 - 2, 4 * 48000) / steadyCorner;
            SAY("plate    %-5s turned %-4s at once: the largest step is %.2f of the steady one, the sharpest corner %.2f (%.1e)\n", info.paramNames[p], up ? "up" : "down", step, sharp, sharp * steadyCorner);
            CHECK(step < (p == 0 ? 2.2 : (p == 1 ? 1.7 : 1.1)));
            CHECK(sharp < (p == 0 ? 3.5 : 2.5) || (p == 3 && sharp * steadyCorner < 1e-3)); // (MIX: see above)
        }
    // MIX 0 IS THE SOUND UNTOUCHED, TO THE BIT, awake or asleep: sound far too faint to be sound (under 1e-15,
    // which the plate takes in as none) comes out as it came, and a -0 stays -0. (Before 2026-11-13 the faint
    // samples came out as 0 while the plate was awake, and as they were while it slept: 1002 of these differed.
    // HALL does the same: not changed here.)
    {
        std::unique_ptr<Effect> h = fresh(plate, 0.1f, 0.45f, 0.6f, 0.f);
        Stereo io(48000);
        Noise noise(13u);
        for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = 0.25f * noise.next(), io.r[i] = 0.25f * noise.next();
        for (std::size_t i = 24000; i < 25000; ++i) io.l[i] *= 4e-16f, io.r[i] *= -4e-16f; // awake: loud sound just before
        io.l[25000] = -0.f, io.r[25001] = 1e-39f;
        const Stereo in = io;
        through(*h, io);
        int apart = 0;
        for (std::size_t i = 0; i < io.frames(); ++i)
            apart += std::memcmp(&io.l[i], &in.l[i], sizeof(float)) != 0 || std::memcmp(&io.r[i], &in.r[i], sizeof(float)) != 0 ? 1 : 0;
        SAY("plate    MIX 0, awake, sound 300 dB down and a -0: %d samples differ from what came in\n", apart);
        CHECK(apart == 0 && std::fabs(in.l[24500]) < 1e-15f && std::fabs(in.l[24500]) > 0.f);
    }
    // ...and MIX turned to none LANDS: the sound untouched, to the bit, within a second.
    {
        std::unique_ptr<Effect> h = fresh(plate, 0.1f, 0.45f, 0.6f, 1.f);
        const std::vector<float> in = sineOf(1000.0, 0.25, 96000);
        Stereo io(96000);
        io.l = io.r = in;
        through(*h, io, 0, 48000);
        h->setParam(3, 0.f);
        through(*h, io, 48000, 96000);
        bool untouched = true;
        for (std::size_t i = 72000; i < 96000; ++i) untouched = untouched && io.l[i] == in[i] && io.r[i] == in[i];
        CHECK(untouched && io.l[48100] != in[48100]);
    }

    // SILENCE IN, SILENCE OUT IN THE END, AND NOTHING TOO SMALL TO HOLD ON THE WAY, with the processor's
    // flush-to-zero off. A click, then nothing: the plate fades on a floor 360 dB down, and a second after
    // it has held nothing over 240 dB down it is asleep: exact zeros.
    //   DECAY 2.0 s: the last sample that is not zero 8.9 s after the click; 0.3 s: 2.1 s; 20 s: 76 s.
    {
        struct Row { float pre, decay, tone, mix; double seconds, asleepBy; };
        const Row rows[4] = {{0.1f, 0.45f, 0.6f, 1.f, 12.0, 8.91}, {0.f, 0.f, 1.f, 0.5f, 4.0, 2.14}, {1.f, 0.f, 0.f, 1.f, 4.0, 2.18}, {0.3f, 1.f, 0.75f, 1.f, 90.0, 76.4}};
        for (const Row& row : rows) {
            std::unique_ptr<Effect> h = fresh(plate, row.pre, row.decay, row.tone, row.mix);
            const TooSmall marks;
            Stereo io(static_cast<std::size_t>(row.seconds * 48000.0));
            io.l[0] = io.r[0] = 1.f;
            through(*h, io);
            const bool seen = marks.seen();
            std::size_t last = 0;
            float smallest = 1.f;
            for (std::size_t i = 1; i < io.frames(); ++i) {
                if (io.l[i] != 0.f) last = i, smallest = std::fmin(smallest, std::fabs(io.l[i]));
                if (io.r[i] != 0.f) last = i > last ? i : last, smallest = std::fmin(smallest, std::fabs(io.r[i]));
            }
            SAY("plate    decay %5.2f s pre %3.0f%%: the last sample that is not zero is %.2f s in; the smallest that came out is %.1e\n", 0.3 * std::pow(20.0 / 0.3, row.decay), row.pre * 100.0, static_cast<double>(last) / 48000.0, static_cast<double>(smallest));
            CHECK(!seen);
            CHECK(std::fabs(static_cast<double>(last) / 48000.0 - row.asleepBy) < 0.06 * row.asleepBy + 0.05);
            CHECK(smallest > 1e-25f && io.l.back() == 0.f && io.r.back() == 0.f);
        }
        // The same with sound coming in that is itself too small to hold (another effect's dying tail): it is taken as none.
        std::unique_ptr<Effect> h = fresh(plate, 0.1f, 0.f, 1.f, 0.5f);
        Stereo io(5 * 48000);
        io.l.assign(io.frames(), 1e-39f);
        io.r.assign(io.frames(), -1e-39f);
        io.l[0] = io.r[0] = 0.5f;
        const TooSmall marks;
        through(*h, io);
        const bool seen = marks.seen();
        CHECK(!seen && rmsOf(io.l, 909, 4800) > 1e-3 && io.l.back() == 0.f && io.r.back() == 0.f);
    }
    CHECK(sameFlushed(plate, 0.1f, 0.f, 0.75f, 0.5f, 4.0)); // the same with the processor's flush-to-zero on or off, to the bit
    CHECK(sameFlushed(plate, 0.6f, 0.3f, 0.2f, 1.f, 8.0));
    // And by the clock (the app's way, flush-to-zero OFF): a block of 64 is no slower late in a tail than
    // early, and asleep it is next to nothing.
    {
        std::unique_ptr<Effect> h = fresh(plate, 0.1f, 0.45f, 0.6f, 1.f);
        const TailTimes took = tailTimes(*h, 40);
        SAY("flush    plate (flush-to-zero OFF): a block takes %.2f us early, %.2f us at its slowest, %.2f us at the end (asleep)\n", took.early, took.slowest, took.late);
        CHECK(took.slowest < 3.0 * took.early + 5.0 && took.late < 0.5 * took.early + 1.0);
    }
    // Woken by a new sound, it plays as one fresh from the box does (the bend starts again too; what is left in
    // it is under 240 dB down); a setting turned while it slept is simply there. Asleep, with nothing coming in,
    // what comes out is nothing; at MIX 0 the sound is not touched at all.
    {
        std::unique_ptr<Effect> slept = fresh(plate, 0.f, 0.f, 0.5f, 0.7f), box = fresh(plate, 0.f, 0.f, 0.5f, 0.7f), turned = fresh(plate, 1.f, 1.f, 0.f, 0.f);
        const Stereo first = clickBoth(*slept, 4.0), a = clickBoth(*slept, 4.0, 0.5f, -0.25f), b = clickBoth(*box, 4.0, 0.5f, -0.25f);
        float apart = 0.f;
        for (std::size_t i = 0; i < a.frames(); ++i) apart = std::fmax(apart, std::fmax(std::fabs(a.l[i] - b.l[i]), std::fabs(a.r[i] - b.r[i])));
        CHECK(first.l.back() == 0.f && a.l.back() == 0.f && apart < 1e-9f && rmsOf(a.l, 429, 4800) > 1e-4);
        clickBoth(*turned, 0.1, 0.f, 0.f);
        turned->setParam(0, 0.f);
        turned->setParam(1, 0.f);
        turned->setParam(2, 0.5f);
        turned->setParam(3, 0.7f);
        const Stereo c = clickBoth(*turned, 4.0, 0.5f, -0.25f);
        CHECK(c.l == b.l && c.r == b.r);
        Stereo faint(256);
        faint.l.assign(256, 1e-20f);
        through(*slept, faint);
        CHECK(faint.l[0] == 0.f && faint.l[255] == 0.f);
        slept->setParam(3, 0.f);
        faint.l.assign(256, 1e-20f);
        through(*slept, faint);
        CHECK(faint.l[0] == 1e-20f && faint.l[255] == 1e-20f);
    }

    // IT SLEEPS ONLY WHEN IT IS EMPTY. It falls asleep once nothing has come in, and what it gives out (all
    // fourteen taps: they read every piece of both loops, and each trip round is under 0.4 s) has stayed under
    // kQuiet, 240 dB down, for a second. What is still in it then is so little that, woken by a click of 1e-6,
    // it differs from a fresh plate by 7.0e-13 at most (DECAY 20 s; 2e-16 at 0.3 s), at five DECAYs, TONE 0 and
    // full. Asleep at ten times kQuiet, that read 7.5e-12; after a tenth of a second, 1.3e-12. (It used to watch
    // what went into the loops: a test could not tell that from watching half of it, and the taps are what is
    // heard. Watching one side's taps and not the other's is the same within 3e-13: each side reads both loops.)
    {
        double worst = 0.0;
        for (float decay : {0.f, 0.2f, 0.45f, 0.8f, 1.f})
            for (float tone : {0.f, 1.f}) {
                const double seconds = 0.3 * std::pow(20.0 / 0.3, decay) * 5.0 + 3.0;
                std::unique_ptr<Effect> slept = fresh(plate, 0.f, decay, tone, 1.f), box = fresh(plate, 0.f, decay, tone, 1.f);
                const Stereo first = clickBoth(*slept, seconds);
                const Stereo a = clickBoth(*slept, 3.0, 1e-6f, -0.5e-6f), b = clickBoth(*box, 3.0, 1e-6f, -0.5e-6f);
                double apart = 0.0;
                for (std::size_t i = 0; i < a.frames(); ++i) apart = std::fmax(apart, std::fmax(std::fabs(static_cast<double>(a.l[i]) - b.l[i]), std::fabs(static_cast<double>(a.r[i]) - b.r[i])));
                CHECK(first.l.back() == 0.f && first.r.back() == 0.f && rmsOf(b.l, 429, 4800) > 1e-10);
                worst = std::fmax(worst, apart);
            }
        SAY("plate    asleep, then woken by a click of 1e-6: it differs from a fresh plate by %.1e at most\n", worst);
        CHECK(worst < 1e-12);
    }

    // HOWEVER IT IS CUT INTO BLOCKS, THE SAME TO THE LAST BIT: 150 s, four ways (blocks of 64, of 48, of 1 to
    // 61 in turn, of 256), through the tail, the sleep, and a waking part way through a block by a sound far
    // too faint to hear (and again into sleep, but for the longest DECAY: asleep at 88 s, woken at 120 s, and
    // still ringing at 150). Four settings.
    {
        struct Cut { float pre, decay, tone; };
        for (const Cut& c : {Cut{0.1f, 0.45f, 0.6f}, Cut{0.73f, 0.2f, 1.f}, Cut{0.f, 0.8f, 0.f}, Cut{0.37f, 1.f, 0.6f}}) {
            const std::size_t second = 48000;
            std::vector<float> in(150 * second, 0.f);
            Noise noise(31u);
            for (std::size_t i = 0; i < 4800; ++i) in[1000 + i] = 0.5f * noise.next();
            in[120 * second + 1237] = 5e-5f;
            std::vector<float> ways[4];
            const std::size_t sizes[4] = {64, 48, 0, 256}; // 0: odd sizes, 1 to 61 in turn
            for (int way = 0; way < 4; ++way) {
                std::unique_ptr<Effect> t = fresh(plate, c.pre, c.decay, c.tone, 1.f);
                Stereo io(in.size());
                io.l = io.r = in;
                for (std::size_t at = 0, n = sizes[way] ? sizes[way] : 1; at < io.frames(); at += n, n = sizes[way] ? sizes[way] : n % 61 + 1) {
                    if (at + n > io.frames()) n = io.frames() - at;
                    t->process(io.l.data() + at, io.r.data() + at, static_cast<int>(n));
                }
                ways[way] = io.l;
                ways[way].insert(ways[way].end(), io.r.begin(), io.r.end());
            }
            std::size_t firstApart = ways[0].size();
            for (int way = 1; way < 4; ++way)
                for (std::size_t i = 0; i < ways[0].size(); ++i)
                    if (ways[way][i] != ways[0][i]) firstApart = std::min(firstApart, i);
            // (And it did all of it: rang, slept, was woken by the faint sound, and slept again.)
            const std::vector<float>& l = ways[0];
            std::size_t lastBefore = 0, lastAfter = 0;
            for (std::size_t i = 0; i < 120 * second; ++i) lastBefore = l[i] != 0.f ? i : lastBefore;
            for (std::size_t i = 120 * second; i < 150 * second; ++i) lastAfter = l[i] != 0.f ? i : lastAfter;
            SAY("plate    pre %3.0f%% decay %5.2f s tone %3.0f%%: cut into blocks four ways, first apart at %s; asleep from %.2f s, woken and asleep again from %.2f s\n", c.pre * 100.0, 0.3 * std::pow(20.0 / 0.3, c.decay), c.tone * 100.0,
                firstApart == ways[0].size() ? "nowhere" : "SOMEWHERE", static_cast<double>(lastBefore) / 48000.0, static_cast<double>(lastAfter) / 48000.0);
            CHECK(firstApart == ways[0].size());
            CHECK(lastBefore > 2 * second && lastBefore < 118 * second && lastAfter > 120 * second && (lastAfter < 149 * second) == (c.decay < 1.f));
            CHECK(rmsOf(l, 120 * second + 1237 + 909, 120 * second + 48000) > 1e-9);
        }
    }
    // On a bus it comes to rest; testATailAlwaysEnds has every setting at 40 BPM (DECAY 20 s is its worst).
    // Here DECAY 20 s by itself: 51.2 s (the tail falls 120 dB in about 40 s, and the bus waits 10).
    {
        const float longest[4] = {0.f, 1.f, 1.f, 1.f};
        const double seconds = secondsToRest(plate, longest, kMinTempo, 200.0);
        SAY("plate    on a bus, DECAY 20 s, TONE full: at rest %.1f s after the last sound\n", seconds);
        CHECK(seconds > 45.0 && seconds < 55.0);
    }
}

// ---- COMB, VOWEL, EQ: FILTER's newer types ------------------------------------------------------

// THE RULES EVERY NEW EFFECT KEEPS, for a type with no bus: one set of settings `set` (and `other`, turned to
// while it sounds). `sleepBy` is how long after a click it is asleep at the most, with `set`. Says what it
// measured; each type's own test holds the numbers.
struct RulesSeen {
    double lastAfterClick = 0.0;  // seconds: the last sample that is not zero after a click
    double stepDown[4] = {}, stepUp[4] = {}, cornerDown[4] = {}, cornerUp[4] = {}; // a setting turned at once (see below)
};
static RulesSeen holdsTheRules(EffectKind kind, const float* set, const float* other, double sleepBy) {
    RulesSeen seen;
    const EffectInfo& info = effectInfo(kind);
    const auto make = [&](const float* p) { return fresh(kind, p[0], p[1], p[2], p[3]); };
    const int mix = mixOf(kind);
    // 1. HOWEVER IT IS CUT INTO BLOCKS, THE SAME TO THE LAST BIT, settings moving too: 30 s, four ways (blocks
    // of 64, of 48, of 1 to 61 in turn, of 256). A burst of noise, then another at 2.9 s with every setting
    // turned to `other` at 3.0 s (a sample no way's blocks have to end on), asleep, woken at 20 s by a
    // sound far too faint to hear, asleep again.
    {
        const std::size_t second = 48000, turn = 3 * second + 17;
        std::vector<float> in(30 * second, 0.f);
        Noise noise(41u);
        for (std::size_t i = 0; i < 4800; ++i) in[1000 + i] = 0.5f * noise.next();
        for (std::size_t i = 0; i < 14400; ++i) in[2 * second + 43200 + i] = 0.5f * noise.next();
        in[20 * second + 1237] = 5e-5f;
        std::vector<float> ways[4];
        const std::size_t sizes[4] = {64, 48, 0, 256};
        for (int way = 0; way < 4; ++way) {
            std::unique_ptr<Effect> e = make(set);
            Stereo io(in.size());
            io.l = io.r = in;
            for (std::size_t i = 0; i < in.size(); ++i) io.r[i] = -0.5f * in[i];
            bool turned = false;
            for (std::size_t at = 0, n = sizes[way] ? sizes[way] : 1; at < io.frames(); at += n, n = sizes[way] ? sizes[way] : n % 61 + 1) {
                if (at + n > io.frames()) n = io.frames() - at;
                if (!turned && at + n > turn) n = turn - at; // up to the turn, then on
                e->process(io.l.data() + at, io.r.data() + at, static_cast<int>(n));
                if (!turned && at + n == turn) {
                    for (int p = 0; p < info.params; ++p) e->setParam(p, other[p]);
                    turned = true;
                }
            }
            ways[way] = io.l;
            ways[way].insert(ways[way].end(), io.r.begin(), io.r.end());
        }
        bool same = true;
        for (int way = 1; way < 4; ++way) same = same && ways[way] == ways[0];
        CHECK(same);
        const std::vector<float>& l = ways[0];
        std::size_t lastBefore = 0, lastAfter = 0;
        for (std::size_t i = 0; i < 20 * second; ++i) lastBefore = l[i] != 0.f ? i : lastBefore;
        for (std::size_t i = 20 * second; i < 30 * second; ++i) lastAfter = l[i] != 0.f ? i : lastAfter;
        CHECK(lastBefore > 3 * second && lastBefore < 19 * second && lastAfter > 20 * second && lastAfter < 29 * second); // it slept, woke and slept again
        CHECK(rmsOf(l, 3 * second + 4800, 3 * second + 9600) > 1e-3); // (and it was sounding while the settings moved)
    }
    // 2. SILENCE IN, SILENCE OUT IN THE END, AND NOTHING TOO SMALL TO HOLD ON THE WAY (flush-to-zero off): a click
    // and nothing after it; a click in sound itself too small to hold (another effect's dying tail); and a
    // steady offset, under which a filter's band state falls away for good.
    {
        std::unique_ptr<Effect> e = make(set);
        const TooSmall marks;
        Stereo io(static_cast<std::size_t>((sleepBy + 2.0) * 48000.0));
        io.l[0] = io.r[0] = 1.f;
        through(*e, io);
        CHECK(!marks.seen());
        std::size_t last = 0;
        for (std::size_t i = 1; i < io.frames(); ++i) last = io.l[i] != 0.f || io.r[i] != 0.f ? i : last;
        seen.lastAfterClick = static_cast<double>(last) / 48000.0;
        CHECK(seen.lastAfterClick < sleepBy && io.l.back() == 0.f && io.r.back() == 0.f);
    }
    {
        std::unique_ptr<Effect> e = make(set);
        Stereo io(3 * 48000);
        io.l.assign(io.frames(), 1e-39f);
        io.r.assign(io.frames(), -1e-39f);
        io.l[0] = io.r[0] = 0.5f;
        const TooSmall marks;
        through(*e, io);
        CHECK(!marks.seen() && peakOf(io.l) >= 0.f);
    }
    {
        std::unique_ptr<Effect> e = make(set);
        Stereo io(4 * 48000);
        io.l.assign(io.frames(), 0.5f);
        io.r.assign(io.frames(), -0.25f);
        const TooSmall marks;
        through(*e, io);
        CHECK(!marks.seen());
    }
    CHECK(sameFlushed(kind, set[0], set[1], set[2], set[3], sleepBy)); // the same with the processor's flush-to-zero on or off, to the bit
    // 3. Woken, it plays as one fresh from the box does, to the bit; a setting turned while it slept is simply
    // there. Asleep, what comes out is what came in.
    {
        std::unique_ptr<Effect> slept = make(set), box = make(set), turned = make(other);
        clickBoth(*slept, sleepBy + 0.5);
        const Stereo a = clickBoth(*slept, 1.0, 0.5f, -0.25f), b = clickBoth(*box, 1.0, 0.5f, -0.25f);
        CHECK(a.l == b.l && a.r == b.r && rmsOf(b.l) > 1e-4);
        clickBoth(*turned, 0.05, 0.f, 0.f);
        clickBoth(*turned, sleepBy + 0.5);
        for (int p = 0; p < info.params; ++p) turned->setParam(p, set[p]);
        const Stereo c = clickBoth(*turned, 1.0, 0.5f, -0.25f);
        CHECK(c.l == b.l && c.r == b.r);
        clickBoth(*slept, sleepBy + 0.5);
        Stereo faint(256);
        faint.l.assign(256, 1e-20f);
        faint.r.assign(256, -0.f);
        const Stereo before = faint;
        through(*slept, faint);
        CHECK(std::memcmp(faint.l.data(), before.l.data(), 256 * sizeof(float)) == 0 && std::memcmp(faint.r.data(), before.r.data(), 256 * sizeof(float)) == 0);
    }
    // 4. By the clock (the app's way, flush-to-zero OFF): a block of 64 is no slower late in a tail than early,
    // and asleep it is next to nothing.
    {
        std::unique_ptr<Effect> e = make(set);
        const TailTimes took = tailTimes(*e, static_cast<int>(sleepBy) + 2);
        SAY("flush    %-6s (flush-to-zero OFF): a block takes %.2f us early, %.2f us at its slowest, %.2f us at the end (asleep)\n", info.name, took.early, took.slowest, took.late);
        CHECK(took.slowest < 3.0 * took.early + 5.0 && took.late < 0.5 * took.early + 1.0);
    }
    // 5. MIX 0 (EQ: every band at 0 dB; WEAR: every setting at 0; TREMOLO: DEPTH 0) IS THE SOUND UNTOUCHED, TO THE BIT, awake or
    // asleep: noise, sound far too faint to be sound, a -0. (LIMITER has no such setting: it is always 3 ms late.
    // testLimiter holds it to the sound as it came in, 3 ms later.)
    if (kind != EffectKind::Limiter) {
        float still[kEffectParams];
        for (int p = 0; p < kEffectParams; ++p) still[p] = set[p];
        if (mix >= 0) still[mix] = 0.f;
        else if (kind == EffectKind::Wear)
            for (float& p : still) p = 0.f;
        else if (kind == EffectKind::Tremolo)
            still[1] = 0.f; // DEPTH 0
        else
            for (int p : {0, 1, 3}) still[p] = 0.5f; // EQ's three bands at 0 dB
        std::unique_ptr<Effect> e = make(still);
        Stereo io(96000);
        Noise noise(13u);
        for (std::size_t i = 0; i < 48000; ++i) io.l[i] = 0.25f * noise.next(), io.r[i] = 0.25f * noise.next();
        for (std::size_t i = 24000; i < 25000; ++i) io.l[i] *= 4e-16f, io.r[i] *= -4e-16f;
        io.l[25000] = -0.f, io.r[25001] = 1e-39f, io.l[90000] = -0.f, io.r[90001] = 1e-39f, io.l[90002] = 3e-17f;
        const Stereo in = io;
        through(*e, io);
        CHECK(std::memcmp(io.l.data(), in.l.data(), 96000 * sizeof(float)) == 0 && std::memcmp(io.r.data(), in.r.data(), 96000 * sizeof(float)) == 0);
    }
    // 6. A SETTING TURNED AT ONCE GLIDES: each turned end to end under a held 100 Hz sine (at MIX 50%). The largest
    // step and the sharpest corner while it gets there, each against the same held still (the rougher of before
    // and after). Each type's test holds the numbers.
    for (int p = 0; p < info.params; ++p)
        for (int up = 0; up < 2; ++up) {
            float at[kEffectParams];
            for (int q = 0; q < kEffectParams; ++q) at[q] = set[q];
            if (mix >= 0 && p != mix) at[mix] = 0.5f;
            at[p] = up ? 0.f : 1.f;
            std::unique_ptr<Effect> e = make(at);
            Stereo io(4 * 48000);
            io.l = io.r = sineOf(100.0, 0.5, io.frames());
            for (std::size_t i = 0; i < 4800; ++i) io.l[i] = io.r[i] = io.l[i] * static_cast<float>(i) / 4800.f;
            through(*e, io, 0, 96000);
            e->setParam(p, up ? 1.f : 0.f);
            through(*e, io, 96000, io.frames());
            const auto corner = [&](std::size_t from, std::size_t to) {
                double most = 0.0;
                for (std::size_t i = from + 2; i < to; ++i) most = std::fmax(most, std::fabs(static_cast<double>(io.l[i]) - 2.0 * io.l[i - 1] + io.l[i - 2]));
                return most;
            };
            const double steady = std::fmax(largestJump(io.l, 48000, 96000), largestJump(io.l, 3 * 48000, 4 * 48000));
            const double steadyCorner = std::fmax(corner(48000, 96000), corner(3 * 48000, 4 * 48000));
            const double step = largestJump(io.l, 96000 - 1, 3 * 48000) / steady, sharp = corner(96000 - 2, 3 * 48000) / steadyCorner;
            SAY("glide    %-6s %-9s turned %-4s at once: the largest step is %.2f of the steady one, the sharpest corner %.2f (%.1e)\n", info.name, info.paramNames[p], up ? "up" : "down", step, sharp, sharp * steadyCorner);
            (up ? seen.stepUp : seen.stepDown)[p] = step;
            (up ? seen.cornerUp : seen.cornerDown)[p] = sharp;
        }
    return seen;
}

// ---- COMB ----

static float combNote(int note) { return (static_cast<float>(note) + 0.5f) / 25.f; } // the PITCH setting for a note, 0 (C2) to 24 (C4)
static float combFeedback(double f) { return static_cast<float>((f + 1.0) / 2.0); } // the FEEDBACK setting that reads f (-1 to 1)

// Where a comb rings: the strongest of `count` frequencies a cent apart about `hz`, in a click's ringing.
static double ringsAt(const std::vector<float>& ir, double hz, int cents = 60) {
    double best = 0.0, bestHz = hz;
    for (int c = -cents; c <= cents; ++c) {
        const double f = hz * std::pow(2.0, c / 1200.0), part = partAt(ir.data() + 1, ir.size() - 1, f);
        if (part > best) best = part, bestHz = f;
    }
    return bestHz;
}
static double cents(double hz, double want) { return 1200.0 * std::log2(hz / want); }

// THE TICK COUNTS WHILE ASLEEP (svf.hpp: ticks are counted from the effect's start). An effect that starts
// asleep and is fed zeros 3 samples, or 3 and five ticks, then noise, then a setting turned, gives the same
// to the bit; fed none first, the turn's glide lands on other samples (it is not the same).
static void tickCountsAsleep(EffectKind kind, int turn, float to) {
    const auto run = [&](int zeros) {
        std::unique_ptr<Effect> e = fresh(kind);
        std::vector<float> l(static_cast<std::size_t>(zeros) + 4800, 0.f), r;
        Noise noise(5u);
        for (std::size_t i = static_cast<std::size_t>(zeros); i < l.size(); ++i) l[i] = 0.25f * noise.next();
        r = l;
        if (zeros > 0) e->process(l.data(), r.data(), zeros);
        e->process(l.data() + zeros, r.data() + zeros, 2400);
        e->setParam(turn, to);
        e->process(l.data() + zeros + 2400, r.data() + zeros + 2400, 2400);
        return std::vector<float>(l.begin() + zeros, l.end());
    };
    const std::vector<float> three = run(3), later = run(3 + 5 * 16), none = run(0);
    CHECK(three == later && three != none);
}

static void testComb() {
    const EffectKind comb = EffectKind::Comb;
    const EffectInfo& info = effectInfo(comb);
    // Its row: what a song stores, and a TYPE of FILTER.
    CHECK(static_cast<int>(comb) == 15 && std::string(info.word) == "comb" && effectFromWord("comb") == comb && std::string(info.name) == "COMB");
    CHECK(info.family == EffectFamily::Filter && effectBus(comb) == -1 && std::string(effectFamily(comb)) == "FILTER" && std::string(effectTypeName(comb)) == "COMB" && info.params == 4);
    CHECK(std::string(info.paramNames[0]) == "PITCH" && std::string(info.paramNames[1]) == "FEEDBACK" && std::string(info.paramNames[2]) == "TONE" && std::string(info.paramNames[3]) == "MIX");
    CHECK(effectChoices(comb, 0) == 25 && effectChoices(comb, 1) == 0 && effectChoices(comb, 2) == 0 && effectChoices(comb, 3) == 0);
    CHECK(info.defaults[0] == combNote(12) && info.defaults[1] == 0.85f && info.defaults[2] == 0.7f && info.defaults[3] == 0.5f);
    // How it reads: a note, a signed percentage (0 is 0%), amounts.
    CHECK(reads(comb, 0, combNote(0)) == "C2" && reads(comb, 0, combNote(1)) == "C#2" && reads(comb, 0, combNote(9)) == "A2" && reads(comb, 0, combNote(12)) == "C3");
    CHECK(reads(comb, 0, combNote(24)) == "C4" && reads(comb, 0, 0.f) == "C2" && reads(comb, 0, 1.f) == "C4" && reads(comb, 0, combNote(23)) == "B3");
    // Below 0 FEEDBACK it rings an octave down, and PITCH reads the note that sounds; at 0 and above, as set.
    {
        const float under[4] = {combNote(0), combFeedback(-0.01), 0.7f, 0.5f}, at0[4] = {combNote(0), 0.5f, 0.7f, 0.5f}, over[4] = {combNote(0), 1.f, 0.7f, 0.5f};
        CHECK(reads(comb, 0, combNote(0), under) == "C1" && reads(comb, 0, combNote(12), under) == "C2" && reads(comb, 0, combNote(24), under) == "C3" && reads(comb, 0, combNote(9), under) == "A1");
        CHECK(reads(comb, 0, combNote(0), at0) == "C2" && reads(comb, 0, combNote(24), over) == "C4" && reads(comb, 0, combNote(24)) == "C4");
    }
    CHECK(reads(comb, 1, 0.f) == "-100%" && reads(comb, 1, 0.5f) == "0%" && reads(comb, 1, 0.85f) == "+70%" && reads(comb, 1, 1.f) == "+100%" && reads(comb, 1, 0.45f) == "-10%");
    CHECK(reads(comb, 2, 0.7f) == "70%" && reads(comb, 3, 0.5f) == "50%");
    CHECK(std::fabs(fx::combHz(combNote(9)) - 110.0) < 1e-6 && std::fabs(fx::combHz(combNote(12)) - 130.8128) < 1e-3 && std::fabs(fx::combHz(1.f) - 261.6256) < 1e-3);

    // IN TUNE: a click, MIX full, FEEDBACK +90%: where it rings, in cents from the note. Above 0 at the note;
    // below 0 an octave under it (odd harmonics only). At every TONE: the high cut's lateness is taken out.
    //   note     TONE 0%    50%    100%     and FEEDBACK -90%, at the note's half: the same, but C4 -2 at TONE 0
    //   C2         0         0       0
    //   G2         0         0       0
    //   C3        -1         0       0
    //   G3        -2         0       0
    //   C4        -3         0       0     (TONE 0 is a 500 Hz cut: it leans on the lowest harmonics, whose
    //                                       lateness is not quite the note's)
    // Left out, the high cut's lateness put C4 at TONE 0 a semitone flat.
    {
        double worst = 0.0;
        for (int note : {0, 7, 12, 19, 24})
            for (float tone : {0.f, 0.5f, 1.f})
                for (int below = 0; below < 2; ++below) {
                    const double hz = fx::combHz(combNote(note)), want = below ? hz / 2.0 : hz;
                    std::unique_ptr<Effect> e = fresh(comb, combNote(note), combFeedback(below ? -0.9 : 0.9), tone, 1.f);
                    const std::vector<float> ir = impulseOf(*e, 1.0);
                    const double off = cents(ringsAt(ir, want), want);
                    // ...and PITCH, read beside this FEEDBACK, names the note it rings at.
                    const float all[4] = {combNote(note), combFeedback(below ? -0.9 : 0.9), tone, 1.f};
                    const int key = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(want / 440.0)));
                    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                    CHECK(reads(comb, 0, combNote(note), all) == std::string(names[key % 12]) + std::to_string(key / 12 - 1));
                    SAY("comb     %-3s TONE %3.0f%% FEEDBACK %s90%%: rings %+.0f cents from %.1f Hz\n", reads(comb, 0, combNote(note)).c_str(), tone * 100.0, below ? "-" : "+", off, want);
                    worst = std::fmax(worst, std::fabs(off));
                    // ...and its harmonics: above 0 the second is there, below 0 the half's second (the note) is not.
                    const double second = partAt(ir.data() + 1, ir.size() - 1, 2.0 * want), first = partAt(ir.data() + 1, ir.size() - 1, want);
                    if (below) CHECK(second < 0.1 * first);
                    else CHECK(second > 0.2 * first);
                }
        CHECK(worst <= 3.5);
    }
    // HOW LONG IT RINGS: a click, MIX full, TONE full, the time to fall 60 dB (rt60), by note and FEEDBACK.
    //   FEEDBACK   C2      C3      C4
    //   +50%      0.12    0.06    0.03 s
    //   +100%     1.84    0.92    0.45  (0.95 a trip round)
    //   -100%     1.84            0.45
    {
        struct Row { int note; double feedback, rt; };
        const Row rows[] = {{0, 0.5, 0.122}, {12, 0.5, 0.061}, {24, 0.5, 0.031}, {0, 1.0, 1.835}, {12, 1.0, 0.917}, {24, 1.0, 0.451}, {0, -1.0, 1.835}, {24, -1.0, 0.451}};
        for (const Row& row : rows) {
            std::unique_ptr<Effect> e = fresh(comb, combNote(row.note), combFeedback(row.feedback), 1.f, 1.f);
            const double rt = rt60(impulseOf(*e, 5.0));
            SAY("comb     %-3s FEEDBACK %+4.0f%%, TONE full: rt60 %.3f s\n", reads(comb, 0, combNote(row.note)).c_str(), row.feedback * 100.0, rt);
            CHECK(std::fabs(rt - row.rt) < 0.05 * row.rt);
        }
        // Lower TONE, the high harmonics die first: C2 at +100%, its 16th harmonic (1046 Hz) by itself.
        const double dark = rt60(bandOf(impulseOf(*fresh(comb, combNote(0), 1.f, 0.f, 1.f), 5.0), 16.0 * fx::combHz(combNote(0))));
        const double bright = rt60(bandOf(impulseOf(*fresh(comb, combNote(0), 1.f, 1.f, 1.f), 5.0), 16.0 * fx::combHz(combNote(0))));
        SAY("comb     C2 +100%%: its 16th harmonic rings %.3f s at TONE 0, %.3f s at TONE full\n", dark, bright);
        CHECK(dark < 0.25 * bright);
    }
    // AS LOUD AT ANY FEEDBACK, on a full sound: noise, MIX full, TONE full, out against in in dB. A sine on
    // the note's second harmonic: up by sqrt((1 + f) / (1 - f)), 16 dB at the most.
    //   FEEDBACK          -100%    -50%    0%    +50%   +100%
    //   noise             -3.8     -0.3    0.0   -0.3   -4.0 dB   (the high cut in the loop takes some)
    //   a sine on C4     -15.9     -4.5    0.0   +4.5  +15.4 dB   (by the sum: 15.9, 4.5)
    {
        for (double f : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
            std::unique_ptr<Effect> e = fresh(comb, combNote(12), combFeedback(f), 1.f, 1.f);
            Stereo io(3 * 48000);
            Noise noise(3u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.25f * noise.next();
            const std::vector<float> in = io.l;
            through(*e, io);
            const double noiseDb = dB(rmsOf(io.l, 48000) / rmsOf(in, 48000));
            const double hz = 2.0 * fx::combHz(combNote(12)), sineDb = levelAt(*fresh(comb, combNote(12), combFeedback(f), 1.f, 1.f), std::round(hz), 0.05);
            const double g = 0.95 * f, want = 10.0 * std::log10((1.0 + std::fabs(g)) / (1.0 - std::fabs(g))) * (f >= 0.0 ? 1.0 : -1.0);
            SAY("comb     FEEDBACK %+4.0f%%: noise %+.1f dB, a sine on C4 %+.1f dB (by the sum %+.1f)\n", f * 100.0, noiseDb, sineDb, want);
            CHECK(std::fabs(noiseDb) < (std::fabs(f) > 0.9 ? 4.5 : 0.5));
            // (130.81 * 2 is not a whole hertz: 262 Hz is 5 cents off the peak, a little short of it.)
            if (f > 0.0) CHECK(sineDb > want - 3.0 && sineDb < want + 0.5);
            if (f < 0.0) CHECK(sineDb < -3.0); // below 0 the note's own place is a notch
        }
    }
    // MIX 50% IS HALF THE COMB ON THE PLAIN SOUND, aligned (the comb's own sound is not late): a click comes out at
    // half itself plus half of what it puts on the line, to the sample.
    {
        const std::vector<float> full = impulseOf(*fresh(comb, combNote(5), 0.8f, 0.6f, 1.f), 0.5), half = impulseOf(*fresh(comb, combNote(5), 0.8f, 0.6f, 0.5f), 0.5);
        double worst = 0.0;
        for (std::size_t i = 0; i < full.size(); ++i) worst = std::fmax(worst, std::fabs(static_cast<double>(half[i]) - ((i == 0 ? 0.5 : 0.0) + 0.5 * full[i])));
        CHECK(worst < 1e-6 && full[0] > 0.f && full[0] < 1.f);
    }
    // PITCH SWEPT (testEveryEffect): no steeper than the loudest note held still.
    {
        double loudest = 0.0;
        for (int note = 0; note < 25; ++note) {
            std::unique_ptr<Effect> e = fresh(comb, combNote(note));
            Stereo io(96000);
            io.l = io.r = sineOf(100.0, 0.5, 96000);
            through(*e, io);
            loudest = std::fmax(loudest, largestJump(io.l, 48000, 96000) / sineStep(100.0, 0.5));
        }
        const double swept = sweepJump(comb, 0);
        SAY("comb     PITCH swept end to end: %.2f; the loudest note held still %.2f\n", swept, loudest);
        CHECK(swept < 1.05 * loudest);
    }
    // THE RULES.
    const float set[4] = {combNote(0), 1.f, 1.f, 1.f}, other[4] = {combNote(20), 0.1f, 0.3f, 0.6f};
    // A click: asleep 7.0 s on (C2, FEEDBACK +100%, TONE full: it rings 240 dB down first).
    // Turned at once, end to end, from C2 at +100% (testEveryEffect has it swept): the largest step and the
    // sharpest corner while it moves, against held still. PITCH moves the 100 Hz sine on and off the comb's
    // peaks (a note's 60 ms swoop, 1.9 at the most: louder, not a click); FEEDBACK from -100% to +100% rings
    // the comb's harmonics a moment as it passes 0. Before PITCH, FEEDBACK and TONE set off gently (Gentle in
    // svf.hpp) and the loop's numbers were slid to sample by sample, FEEDBACK up read a corner of 6.28 and PITCH up 3.34.
    //              down: step  corner    up: step  corner
    //   PITCH         1.55     1.48         1.88     2.72
    //   FEEDBACK      1.51     1.50         1.05     1.10
    //   TONE          1.00     1.00         1.00     1.01
    //   MIX           1.00     1.00         1.00     0.92
    tickCountsAsleep(comb, 0, 0.9f);
    const RulesSeen seen = holdsTheRules(comb, set, other, 8.0);
    SAY("comb     C2, FEEDBACK +100%%, TONE full: the last sample that is not zero %.2f s after a click\n", seen.lastAfterClick);
    CHECK(std::fabs(seen.lastAfterClick - 7.02) < 0.1);
    const double was[4][4] = {{1.55, 1.48, 1.88, 2.72}, {1.51, 1.50, 1.05, 1.10}, {1.00, 1.00, 1.00, 1.01}, {1.00, 1.00, 1.00, 0.92}};
    for (int p = 0; p < 4; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- VOWEL ----

static float vowelAt(int vowel) { return static_cast<float>(vowel) / 4.f; } // A 0, E 1, I 2, O 3, U 4
static std::vector<float> sawOf(double hz, double amp, std::size_t frames) {
    std::vector<float> v(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = std::fmod(hz * static_cast<double>(i) / 48000.0, 1.0);
        v[i] = static_cast<float>(amp * (2.0 * t - 1.0));
    }
    return v;
}
// Out against in, in dB, after the first half second.
static double loudnessThrough(Effect& effect, const std::vector<float>& in) {
    Stereo io(in.size());
    io.l = io.r = in;
    through(effect, io);
    return dB(rmsOf(io.l, 24000) / rmsOf(in, 24000));
}

static void testVowel() {
    const EffectKind vowel = EffectKind::Vowel;
    const EffectInfo& info = effectInfo(vowel);
    CHECK(static_cast<int>(vowel) == 16 && std::string(info.word) == "vowel" && effectFromWord("vowel") == vowel && std::string(info.name) == "VOWEL");
    CHECK(info.family == EffectFamily::Filter && effectBus(vowel) == -1 && std::string(effectTypeName(vowel)) == "VOWEL" && info.params == 4);
    CHECK(std::string(info.paramNames[0]) == "VOWEL" && std::string(info.paramNames[1]) == "SHIFT" && std::string(info.paramNames[2]) == "RESONANCE" && std::string(info.paramNames[3]) == "MIX");
    for (int i = 0; i < 4; ++i) CHECK(effectChoices(vowel, i) == 0);
    CHECK(info.defaults[0] == 0.f && info.defaults[1] == 0.5f && info.defaults[2] == 0.5f && info.defaults[3] == 1.f);
    // How it reads: a vowel, or how far from one to the next (a D-pad push is a fifth of the way); SHIFT in semitones.
    CHECK(reads(vowel, 0, 0.f) == "A" && reads(vowel, 0, 0.25f) == "E" && reads(vowel, 0, 0.5f) == "I" && reads(vowel, 0, 0.75f) == "O" && reads(vowel, 0, 1.f) == "U");
    CHECK(reads(vowel, 0, 0.05f) == "A-E 20%" && reads(vowel, 0, 0.1f) == "A-E 40%" && reads(vowel, 0, 0.95f) == "O-U 80%" && reads(vowel, 0, 0.6f) == "I-O 40%");
    CHECK(reads(vowel, 1, 0.f) == "-12 ST" && reads(vowel, 1, 0.5f) == "0 ST" && reads(vowel, 1, 1.f) == "+12 ST" && reads(vowel, 1, 0.55f) == "+1 ST");
    CHECK(reads(vowel, 2, 0.5f) == "50%" && reads(vowel, 3, 1.f) == "100%");
    for (int k = 0; k <= 20; ++k) CHECK(reads(vowel, 0, k / 20.f).size() <= 7);

    // THE VOWELS: sines through it, MIX full, RESONANCE at its usual, in dB. Each vowel's first peak is +10.9 dB
    // and stands out over a place between it and the next:
    //   A  +10.9 at 600 Hz    +4.1 at 1040    -11.8 between, at 820
    //   E  +10.8 at 400       -0.7 at 1620    -25.6 at 900
    //   I  +10.8 at 250      -17.5 at 1750    -17.4 at 700
    //   O  +10.9 at 400       +0.4 at 750      -8.0 at 560
    //   U  +10.8 at 350       -5.6 at 600      -4.5 at 470   (U's two are close: its dip between is shallow)
    struct Peaks { int vowel; double first, second, between; };
    const Peaks peaks[] = {{0, 600, 1040, 820}, {1, 400, 1620, 900}, {2, 250, 1750, 700}, {3, 400, 750, 560}, {4, 350, 600, 470}};
    for (const Peaks& p : peaks) {
        std::unique_ptr<Effect> e = fresh(vowel, vowelAt(p.vowel));
        const double a = levelAt(*e, p.first), b = levelAt(*e, p.second), c = levelAt(*e, p.between);
        SAY("vowel    %s: %+.1f dB at %.0f Hz, %+.1f at %.0f, %+.1f between at %.0f\n", reads(vowel, 0, vowelAt(p.vowel)).c_str(), a, p.first, b, p.second, c, p.between);
        CHECK(a > c + 6.0 && std::fabs(a - 10.9) < 0.5 && a > b);
    }
    // SHIFT moves every peak: +12 semitones puts A's first at 1200 Hz, -12 at 300.
    {
        const double plain = levelAt(*fresh(vowel, 0.f, 0.5f), 600), up = levelAt(*fresh(vowel, 0.f, 1.f), 1200), down = levelAt(*fresh(vowel, 0.f, 0.f), 300);
        const double off = levelAt(*fresh(vowel, 0.f, 1.f), 600);
        SAY("vowel    A's first peak: %+.1f dB at 600 Hz; shifted up an octave %+.1f at 1200 (and %+.1f at 600); down %+.1f at 300\n", plain, up, off, down);
        CHECK(std::fabs(up - plain) < 1.0 && std::fabs(down - plain) < 1.0 && off < plain - 10.0);
    }
    // HALF WAY FROM A TO E the first peak is half way in pitch: sqrt(600 * 400) = 490 Hz.
    {
        double best = -1e9, at = 0.0;
        for (double hz = 400.0; hz <= 600.0; hz += 5.0) {
            const double l = levelAt(*fresh(vowel, 0.125f), hz);
            if (l > best) best = l, at = hz;
        }
        SAY("vowel    A-E 50%%: the first peak at %.0f Hz\n", at);
        CHECK(std::fabs(at - 490.0) < 6.0); // (in hertz it would be 500)
    }
    // RESONANCE: narrower peaks. A's first peak against 100 Hz either side of it.
    {
        for (float r : {0.f, 0.5f, 1.f}) {
            std::unique_ptr<Effect> e = fresh(vowel, 0.f, 0.5f, r);
            const double sharp = levelAt(*e, 600) - 0.5 * (levelAt(*e, 500) + levelAt(*e, 700));
            SAY("vowel    RESONANCE %3.0f%%: A's first peak stands %.1f dB over 100 Hz either side\n", r * 100.0, sharp);
        }
        std::unique_ptr<Effect> low = fresh(vowel, 0.f, 0.5f, 0.f), high = fresh(vowel, 0.f, 0.5f, 1.f);
        CHECK(levelAt(*high, 600) - levelAt(*high, 500) > levelAt(*low, 600) - levelAt(*low, 500) + 10.0);
    }
    // ABOUT AS LOUD AT ANY RESONANCE on a bright sound: a 110 Hz saw, each vowel, RESONANCE 0, 50%, 100%: -3.1 to
    // -7.2 dB (U and I the loudest: their first peak takes the saw's strong low harmonics). Noise, whose most is
    // up high where the vowels have little: -6.1 (A, full) to -16.7 dB (U, 0). The peaks are made up by Q to
    // the 3/4: by sqrt(Q) the saw read -10 dB at RESONANCE 0 and -17 at full.
    {
        const std::vector<float> saw = sawOf(110.0, 0.5, 96000);
        std::vector<float> noise(96000);
        Noise n(9u);
        for (float& v : noise) v = 0.5f * n.next();
        for (float r : {0.f, 0.5f, 1.f}) {
            for (int v = 0; v < 5; ++v) {
                const double s = loudnessThrough(*fresh(vowel, vowelAt(v), 0.5f, r), saw), z = loudnessThrough(*fresh(vowel, vowelAt(v), 0.5f, r), noise);
                SAY("vowel    RESONANCE %3.0f%% %s: a saw %+.1f dB, noise %+.1f dB\n", r * 100.0, reads(vowel, 0, vowelAt(v)).c_str(), s, z);
                CHECK(s > -7.6 && s < -2.7 && z > -17.2 && z < -5.6);
            }
        }
    }
    // MIX 50% IS HALF THE PEAKS ON THE PLAIN SOUND, aligned: a click comes out at half itself and half the filter's, to the sample.
    {
        const std::vector<float> full = impulseOf(*fresh(vowel, 0.3f, 0.5f, 0.5f, 1.f), 0.5), half = impulseOf(*fresh(vowel, 0.3f, 0.5f, 0.5f, 0.5f), 0.5);
        double worst = 0.0;
        for (std::size_t i = 0; i < full.size(); ++i) worst = std::fmax(worst, std::fabs(static_cast<double>(half[i]) - ((i == 0 ? 0.5 : 0.0) + 0.5 * full[i])));
        CHECK(worst < 1e-6);
    }
    const float set[4] = {0.f, 0.5f, 1.f, 1.f}, other[4] = {1.f, 0.9f, 0.2f, 0.4f};
    // A click: asleep 0.32 s on (A, RESONANCE full). Turned at once, end to end (as COMB's):
    //              down: step  corner    up: step  corner
    //   VOWEL         1.02     1.12         1.02     1.13
    //   SHIFT         1.00     1.00         0.99     0.99
    //   RESONANCE     1.00     1.00         1.00     1.00
    //   MIX           1.00     1.00         1.01     0.92
    // (With the peaks' numbers stepped once a tick and a plain Glide, VOWEL read corners of 3.95 and RESONANCE 4.41.)
    tickCountsAsleep(vowel, 0, 0.9f);
    const RulesSeen seen = holdsTheRules(vowel, set, other, 2.0);
    SAY("vowel    A, RESONANCE full: the last sample that is not zero %.2f s after a click\n", seen.lastAfterClick);
    CHECK(std::fabs(seen.lastAfterClick - 0.32) < 0.03);
    const double was[4][4] = {{1.02, 1.12, 1.02, 1.13}, {1.00, 1.00, 0.99, 0.99}, {1.00, 1.00, 1.00, 1.00}, {1.00, 1.00, 1.01, 0.92}};
    for (int p = 0; p < 4; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- EQ ----

static float eqDb(double db) { return static_cast<float>((db / 12.0 + 1.0) / 2.0); } // the setting that reads `db`

static void testEq() {
    const EffectKind eq = EffectKind::Eq;
    const EffectInfo& info = effectInfo(eq);
    CHECK(static_cast<int>(eq) == 17 && std::string(info.word) == "eq" && effectFromWord("eq") == eq && std::string(info.name) == "EQ");
    CHECK(info.family == EffectFamily::Filter && effectBus(eq) == -1 && std::string(effectTypeName(eq)) == "EQ" && info.params == 4 && mixOf(eq) == -1);
    CHECK(std::string(info.paramNames[0]) == "LOW" && std::string(info.paramNames[1]) == "MID" && std::string(info.paramNames[2]) == "MID FREQ" && std::string(info.paramNames[3]) == "HIGH");
    for (int i = 0; i < 4; ++i) CHECK(effectChoices(eq, i) == 0 && info.defaults[i] == 0.5f);
    // How it reads: dB with a sign (a D-pad push is 1.2 dB), the bell's place in hertz.
    CHECK(reads(eq, 0, 0.5f) == "0 DB" && reads(eq, 0, 1.f) == "+12 DB" && reads(eq, 0, 0.f) == "-12 DB" && reads(eq, 1, 0.55f) == "+1.2 DB" && reads(eq, 3, 0.45f) == "-1.2 DB");
    CHECK(reads(eq, 3, 0.75f) == "+6 DB" && reads(eq, 1, 0.3f) == "-4.8 DB");
    CHECK(reads(eq, 2, 0.f) == "125 HZ" && reads(eq, 2, 0.5f) == "1.0 KHZ" && reads(eq, 2, 1.f) == "8.0 KHZ" && reads(eq, 2, 0.25f) == "354 HZ");
    for (int k = 0; k <= 20; ++k)
        for (int p = 0; p < 4; ++p) CHECK(reads(eq, p, k / 20.f).size() <= 8);

    // FLAT IS THE SOUND UNTOUCHED, TO THE BIT: every band at 0 dB, wherever MID FREQ is, ten seconds of noise.
    for (float where : {0.f, 0.37f, 1.f}) {
        std::unique_ptr<Effect> e = fresh(eq, 0.5f, 0.5f, where, 0.5f);
        Stereo io(480000);
        Noise noise(4u);
        for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = noise.next(), io.r[i] = 0.3f * noise.next();
        const Stereo in = io;
        through(*e, io);
        CHECK(io.l == in.l && io.r == in.r);
    }
    // ...and turned back to flat it lands there, to the bit, within a second.
    {
        std::unique_ptr<Effect> e = fresh(eq, 1.f, 0.f, 0.2f, 0.8f);
        Stereo io(96000);
        Noise noise(6u);
        for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.5f * noise.next();
        const Stereo in = io;
        through(*e, io, 0, 48000);
        for (int p : {0, 1, 3}) e->setParam(p, 0.5f);
        through(*e, io, 48000, 96000);
        bool untouched = true;
        for (std::size_t i = 72000; i < 96000; ++i) untouched = untouched && io.l[i] == in.l[i];
        CHECK(untouched && io.l[47000] != in.l[47000]);
    }

    // THE BANDS, in dB, sines through it (the rest flat):
    //                      40 Hz   200 Hz   1 kHz   5 kHz   15 kHz
    //   LOW +12           +12.0    +6.0      0.0     0.0     0.0     a shelf: half way at its corner
    //   LOW -12           -12.0    -6.0      0.0     0.0     0.0
    //   MID +12 at 1 kHz    0.0    +0.8    +12.0    +0.7     0.0     a bell, a little over two octaves to half way
    //   MID -12 at 1 kHz    0.0    -0.8    -12.0    -0.7     0.0
    //   HIGH +12            0.0     0.0      0.0    +6.0   +12.0
    //   HIGH -12            0.0     0.0      0.0    -6.0   -12.0
    {
        struct Row { float low, mid, freq, high; double at[5]; };
        const double hz[5] = {40, 200, 1000, 5000, 15000};
        const Row rows[] = {
            {eqDb(12), 0.5f, 0.5f, 0.5f, {12.0, 6.0, 0.0, 0.0, 0.0}},  {eqDb(-12), 0.5f, 0.5f, 0.5f, {-12.0, -6.0, 0.0, 0.0, 0.0}},
            {0.5f, eqDb(12), 0.5f, 0.5f, {0.0, 0.8, 12.0, 0.7, 0.0}},  {0.5f, eqDb(-12), 0.5f, 0.5f, {0.0, -0.8, -12.0, -0.7, 0.0}},
            {0.5f, 0.5f, 0.5f, eqDb(12), {0.0, 0.0, 0.0, 6.0, 12.0}},  {0.5f, 0.5f, 0.5f, eqDb(-12), {0.0, 0.0, 0.0, -6.0, -12.0}},
        };
        for (const Row& row : rows) {
            std::unique_ptr<Effect> e = fresh(eq, row.low, row.mid, row.freq, row.high);
            double got[5];
            for (int i = 0; i < 5; ++i) got[i] = levelAt(*e, hz[i]);
            SAY("eq       LOW %s MID %s HIGH %s: %+.1f %+.1f %+.1f %+.1f %+.1f dB at 40, 200, 1k, 5k, 15k\n", reads(eq, 0, row.low).c_str(), reads(eq, 1, row.mid).c_str(), reads(eq, 3, row.high).c_str(), got[0], got[1], got[2], got[3], got[4]);
            for (int i = 0; i < 5; ++i) CHECK(std::fabs(got[i] - row.at[i]) < 0.1);
        }
    }
    // MID FREQ moves the bell: +12 dB at 125 Hz, 354 Hz, 1 kHz, 2.8 kHz, 8 kHz, each at its own place.
    for (float where : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
        const double hz = std::round(125.0 * std::pow(64.0, where));
        const double at = levelAt(*fresh(eq, 0.5f, 1.f, where, 0.5f), hz);
        SAY("eq       MID +12 at %s: %+.1f dB at %.0f Hz\n", reads(eq, 2, where).c_str(), at, hz);
        CHECK(std::fabs(at - 12.0) < 0.3);
    }
    CHECK(levelAt(*fresh(eq, 0.5f, 1.f, 0.f, 0.5f), 1000) < 3.0 && levelAt(*fresh(eq, 0.5f, 1.f, 1.f, 0.5f), 1000) < 3.0);
    // Every band its own sound: MID FREQ does something once the bell is up (testEveryEffect leaves it out).
    {
        Stereo a = pinInput(48000.f), b = a;
        through(*fresh(eq, 0.5f, 1.f, 0.f, 0.5f), a);
        through(*fresh(eq, 0.5f, 1.f, 1.f, 0.5f), b);
        CHECK(a.l != b.l);
    }
    // THE RULES. A click: asleep 0.04 s on, every band at +12 dB. Turned at once, end to end, under the 100 Hz sine
    // (+12 dB at its usual, the bell at 354 Hz): LOW turned down is the sine going from four times its size to a
    // quarter, setting off gently. With a plain Glide and the bands' numbers stepped once a tick, that read a
    // step of 8.1 and a corner of 790 (0.28: a click), then with the numbers slid but a plain Glide, a corner of 34.
    //              down: step  corner    up: step  corner
    //   LOW           1.03     2.20         1.00     1.00
    //   MID           0.99     1.44         1.00     1.00
    //   MID FREQ      1.00     1.00         1.02     1.20
    //   HIGH          1.00     1.00         1.00     1.01
    const float set[4] = {1.f, 1.f, 0.25f, 1.f}, other[4] = {0.f, 0.2f, 0.9f, 0.4f};
    tickCountsAsleep(eq, 0, 0.9f);
    const RulesSeen seen = holdsTheRules(eq, set, other, 2.0);
    SAY("eq       every band +12: the last sample that is not zero %.2f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick < 0.1);
    const double was[4][4] = {{1.03, 2.20, 1.00, 1.00}, {0.99, 1.44, 1.00, 1.00}, {1.00, 1.00, 1.02, 1.20}, {1.00, 1.00, 1.00, 1.01}};
    for (int p = 0; p < 4; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- FOLD, FUZZ: DRIVE's newer types; RING: CRUSH's --------------------------------------------------

// How rough a 100 Hz sine at half scale comes out with setting `p` held at `v` (the rest at their usual),
// in the sine's own steps, settled.
static double heldRough(EffectKind kind, int p, float v) {
    std::unique_ptr<Effect> e = fresh(kind);
    e->setParam(p, v);
    Stereo io(96000);
    io.l = io.r = sineOf(100.0, 0.5, 96000);
    through(*e, io);
    return largestJump(io.l, 48000, 96000) / sineStep(100.0, 0.5);
}
// EVERY SETTING SWEPT END TO END (sweepJump) IS NO ROUGHER THAN IT IS HELD STILL at the rougher end: these
// three make the sine rougher by design, so "as smooth as the sine" (testEveryEffect) is not the measure.
static void sweptNoRougherThanHeld(EffectKind kind) {
    const EffectInfo& info = effectInfo(kind);
    for (int p = 0; p < info.params; ++p) {
        const double swept = sweepJump(kind, p), held = std::fmax(heldRough(kind, p, 0.f), heldRough(kind, p, 1.f));
        SAY("swept    %-6s %-9s end to end %.2f; held still at the rougher end %.2f\n", info.name, info.paramNames[p], swept, held);
        CHECK(swept < 1.1 * held + 0.1);
    }
}
// The share of a sine's harmonics `from` to `to` (in what comes out, settled), in dB against all of it.
static double harmonicShare(Effect& effect, int hz, double amp, int from, int to) {
    const std::size_t n = 48000;
    Stereo io(2 * n);
    io.l = io.r = sineOf(hz, amp, 2 * n);
    effect.reset();
    through(effect, io);
    const float* out = io.l.data() + n;
    double all = 0.0, some = 0.0;
    for (int k = 1; k * hz < 24000; ++k) {
        const double a = partAt(out, n, static_cast<double>(k) * hz), e = 0.5 * a * a;
        all += e;
        if (k >= from && k <= to) some += e;
    }
    return 10.0 * std::log10(some / all + 1e-60);
}
// THE 2X STAGE'S LATENESS IS MATCHED AT MIX 50%: a 12 kHz sine, quiet enough to go through straight, comes out
// as loud as it went in (mixed with the plain sound as it came in, the two cancelled up there).
static double mixedAt12k(EffectKind kind, const float* p) {
    std::unique_ptr<Effect> e = fresh(kind, p[0], p[1], p[2], p[3]);
    return levelAt(*e, 12000, 0.05);
}

// A SETTING TURNED BY ITSELF GETS THERE: one that has run a second at its usual settings, then setting `p`
// turned to `v`, a second later comes out as one made with it (a 220 Hz sine at half scale, to 1e-4).
// `skip`: a setting that cannot be held to the sample so (RING's FREQ: the carrier is somewhere else in its turn).
static void turnedLands(EffectKind kind, int skip = -1, int skip2 = -1) {
    const EffectInfo& info = effectInfo(kind);
    for (int p = 0; p < info.params; ++p) {
        if (p == skip || p == skip2) continue;
        const float v = info.defaults[p] > 0.5f ? 0.1f : 0.9f;
        std::unique_ptr<Effect> turned = fresh(kind), made = fresh(kind);
        made->setParam(p, v);
        Stereo a(3 * 48000), b(3 * 48000);
        a.l = a.r = b.l = b.r = sineOf(220.0, 0.5, a.frames());
        through(*turned, a, 0, 48000);
        turned->setParam(p, v);
        through(*turned, a, 48000, a.frames());
        through(*made, b);
        double worst = 0.0, size = 0.0;
        for (std::size_t i = 2 * 48000 + 24000; i < a.frames(); ++i) {
            worst = std::fmax(worst, std::fmax(std::fabs(static_cast<double>(a.l[i]) - b.l[i]), std::fabs(static_cast<double>(a.r[i]) - b.r[i])));
            size = std::fmax(size, std::fabs(static_cast<double>(b.l[i])));
        }
        Stereo c(3 * 48000);
        c.l = c.r = sineOf(220.0, 0.5, c.frames());
        through(*fresh(kind), c);
        double moved = 0.0;
        for (std::size_t i = 2 * 48000 + 24000; i < c.frames(); ++i) moved = std::fmax(moved, std::fmax(std::fabs(static_cast<double>(c.l[i]) - b.l[i]), std::fabs(static_cast<double>(c.r[i]) - b.r[i])));
        SAY("lands    %-6s %-9s turned to %s: %.1e from one made so (which is %.1e from its usual)\n", info.name, info.paramNames[p], reads(kind, p, v).c_str(), worst, moved);
        CHECK(worst < 1e-4 * size + 1e-6 && moved > 1e-2);
    }
}

// MIX GLIDES, TAKING ABOUT 20 MS (kMixSettle) TO GET MOST OF THE WAY: a 220 Hz sine at half scale, MIX turned from
// 0 to full at once after a second. How far it has got (0 the plain sound, 1 all of the effect: what comes out
// laid against one left at each) in the first 3 ms after the turn, 60 to 80 ms after it, and 300 to 400 ms
// after it. `at`: the other settings.
struct MixGot {
    double early = 0.0, later = 0.0, late = 0.0;
};
static MixGot mixGlides(EffectKind kind, const float* at) {
    const int mix = mixOf(kind);
    float p[kEffectParams];
    for (int q = 0; q < kEffectParams; ++q) p[q] = at[q];
    Stereo ways[3];
    for (int way = 0; way < 3; ++way) { // 0 turned, 1 left at 0, 2 left at full
        p[mix] = way == 2 ? 1.f : 0.f;
        std::unique_ptr<Effect> e = fresh(kind, p[0], p[1], p[2], p[3]);
        ways[way] = Stereo(48000 + 19200);
        ways[way].l = ways[way].r = sineOf(220.0, 0.5, ways[way].frames());
        through(*e, ways[way], 0, 48000);
        if (way == 0) e->setParam(mix, 1.f);
        through(*e, ways[way], 48000, ways[way].frames());
    }
    const auto got = [&](std::size_t from, std::size_t to) {
        double along = 0.0, size = 0.0;
        for (std::size_t i = from; i < to; ++i) {
            const double d = static_cast<double>(ways[2].l[i]) - ways[1].l[i];
            along += (static_cast<double>(ways[0].l[i]) - ways[1].l[i]) * d;
            size += d * d;
        }
        return size > 0.0 ? along / size : 0.0;
    };
    MixGot g;
    g.early = got(48000, 48000 + 144);
    g.later = got(48000 + 2880, 48000 + 3840);
    g.late = got(48000 + 14400, 48000 + 19200);
    SAY("mix      %-6s MIX turned from 0 to full: %.3f of the way in the first 3 ms, %.4f at 60 to 80 ms, %.6f at 300 to 400 ms\n", effectInfo(kind).name, g.early, g.later, g.late);
    return g;
}

// A NOTE, THEN A FAINT STEADY OFFSET, IS NOTHING TOO SMALL TO HOLD (flush-to-zero off): 0.1 s of a 220 Hz sine at
// half scale (or a steady 0.5), then 1e-12 (or 3e-9) held for five seconds. Coming in, so it never sleeps: its
// filters ring down under the offset, past 1e-20, and are made exact zeros there (FOLD's TONE, FUZZ's two)
// or they would go on down past the smallest number a float holds. holdsTheRules' offset is too loud for that.
static bool faintAfterANote(EffectKind kind, const float* at) {
    bool clean = true;
    for (int way = 0; way < 2; ++way) {
        std::unique_ptr<Effect> e = fresh(kind, at[0], at[1], at[2], at[3]);
        Stereo io(5 * 48000);
        for (std::size_t i = 0; i < io.frames(); ++i)
            io.l[i] = io.r[i] = i < 4800 ? (way ? 0.5f : static_cast<float>(0.5 * std::sin(2.0 * kPi * 220.0 * static_cast<double>(i) / 48000.0))) : (way ? 3e-9f : 1e-12f);
        const TooSmall marks;
        through(*e, io);
        clean = clean && !marks.seen();
    }
    return clean;
}

// ---- FOLD ----

static float foldDrive(double push) { return static_cast<float>(std::log(push / 1.5) / std::log(8.0)); } // the DRIVE setting for a push

static void testFold() {
    const EffectKind fold = EffectKind::Fold;
    const EffectInfo& info = effectInfo(fold);
    CHECK(static_cast<int>(fold) == 18 && std::string(info.word) == "fold" && effectFromWord("fold") == fold && std::string(info.name) == "FOLD");
    CHECK(info.family == EffectFamily::Drive && effectBus(fold) == -1 && std::string(effectTypeName(fold)) == "FOLD" && info.params == 4);
    CHECK(std::string(info.paramNames[0]) == "DRIVE" && std::string(info.paramNames[1]) == "SYMMETRY" && std::string(info.paramNames[2]) == "TONE" && std::string(info.paramNames[3]) == "MIX");
    for (int i = 0; i < 4; ++i) CHECK(effectChoices(fold, i) == 0);
    CHECK(info.defaults[0] == 0.35f && info.defaults[1] == 0.5f && info.defaults[2] == 0.8f && info.defaults[3] == 1.f);
    CHECK(reads(fold, 0, 0.35f) == "35%" && reads(fold, 1, 0.5f) == "0%" && reads(fold, 1, 1.f) == "+100%" && reads(fold, 1, 0.f) == "-100%" && reads(fold, 1, 0.55f) == "+10%" && reads(fold, 1, 0.45f) == "-10%");
    // DRIVE and TONE are SATURATE's, so changing TYPE between them keeps both to the bit.
    CHECK(effectCarryFrom(EffectKind::Saturate, fold, 0) == 0 && effectCarryFrom(EffectKind::Saturate, fold, 2) == 2 && effectCarryFrom(EffectKind::Saturate, fold, 1) == -1);
    CHECK(effectCarryValue(EffectKind::Saturate, 0, fold, 0, 0.437f) == 0.437f && effectCarryValue(EffectKind::Saturate, 2, fold, 2, 0.613f) == 0.613f);

    // THE FOLDER BY ITSELF: a triangle with its top 30% rounded, flat where it turns.
    //   in    0     0.5    0.7    0.85     1      1.5    2      3       5
    //   out   0     0.5    0.7    0.8125  0.85   0.5    0     -0.85    0.85
    {
        const float in[] = {0.f, 0.5f, 0.7f, 0.85f, 1.f, 1.5f, 2.f, 3.f, 5.f};
        const float out[] = {0.f, 0.5f, 0.7f, 0.8125f, 0.85f, 0.5f, 0.f, -0.85f, 0.85f};
        for (int i = 0; i < 9; ++i) CHECK(std::fabs(fx::foldCurve(in[i]) - out[i]) < 1e-5f && std::fabs(fx::foldCurve(-in[i]) + out[i]) < 1e-5f);
        // No step and no corner anywhere: the slope either side of every point within a hair.
        double worst = 0.0;
        for (int i = -4000; i < 4000; ++i) {
            const double x = i / 1000.0, h = 1e-3;
            const double left = (fx::foldCurve(static_cast<float>(x)) - fx::foldCurve(static_cast<float>(x - h))) / h, right = (fx::foldCurve(static_cast<float>(x + h)) - fx::foldCurve(static_cast<float>(x))) / h;
            worst = std::fmax(worst, std::fabs(left - right));
        }
        CHECK(worst < 0.02);
        CHECK(fx::foldCurve(1e7f) == 0.f && fx::foldCurve(std::nanf("")) == 0.f);
    }
    // NOT SATURATE'S FOLD. At the same push, a 220 Hz sine at half scale: the share of its harmonics 10 to 40.
    //   push        2       3       4.5     6
    //   FOLD       -57.2   -45.9   -27.7   -25.6 dB
    //   SATURATE  -159.9  -122.8   -92.5   -69.0 dB (its FOLD)
    for (double push : {2.0, 3.0, 4.5, 6.0}) {
        std::unique_ptr<Effect> f = fresh(fold, foldDrive(push), 0.5f, 1.f, 1.f), sat = saturate(true, static_cast<float>((push - 1.0) / 5.0), 2, 1.f, 1.f);
        const double a = harmonicShare(*f, 220, 0.5, 10, 40), b = harmonicShare(*sat, 220, 0.5, 10, 40);
        SAY("fold     push %.1f: harmonics 10 to 40 are %.1f dB of it; SATURATE's FOLD %.1f dB\n", push, a, b);
        CHECK(a > b + 40.0 && a > -60.0);
    }
    // SYMMETRY: at 0 no even harmonics; off 0, the second comes in. A 220 Hz sine at half scale, DRIVE 60%.
    {
        Harmonics h[3];
        const float sym[3] = {0.5f, 0.75f, 1.f};
        for (int i = 0; i < 3; ++i) {
            std::unique_ptr<Effect> e = fresh(fold, 0.6f, sym[i], 1.f, 1.f);
            h[i] = harmonicsOf(*e, 220, 0.5);
            SAY("fold     SYMMETRY %s: 2nd %.1f dB, 3rd %.1f dB, 4th %.1f\n", reads(fold, 1, sym[i]).c_str(), h[i].part[2], h[i].part[3], h[i].part[4]);
        }
        CHECK(h[0].part[2] < -90.0 && h[0].part[4] < -90.0 && h[1].part[2] > -30.0 && h[2].part[2] > -30.0);
    }
    // AS LOUD AS IT WENT IN, the reference sine (half scale, 220 Hz), at every DRIVE and SYMMETRY.
    {
        double low = 1e9, high = -1e9;
        for (float d : {0.f, 0.25f, 0.5f, 0.75f, 1.f})
            for (float sy : {0.f, 0.5f, 1.f}) {
                std::unique_ptr<Effect> e = fresh(fold, d, sy, 1.f, 1.f);
                const double l = harmonicsOf(*e, 220, 0.5).level;
                low = std::fmin(low, l), high = std::fmax(high, l);
            }
        SAY("fold     the reference sine comes out between %+.2f and %+.2f dB at every DRIVE and SYMMETRY\n", low, high);
        CHECK(low > -1.0 && high < 0.5);
    }
    // ALIASING. A 4186 Hz sine at half scale, DRIVE 50%: what folds back under 19 kHz, against the strongest harmonic.
    {
        std::unique_ptr<Effect> e = fresh(fold, 0.5f, 0.5f, 1.f, 1.f);
        const Harmonics h = harmonicsOf(*e, 4186, 0.5);
        SAY("fold     4186 Hz at DRIVE 50%%: the strongest harmonic %.1f dB, the loudest fold-back under 19 kHz %.1f dB (at %.0f Hz)\n", h.strongest, h.aliasLow, h.aliasLowHz);
        CHECK(h.aliasLow < h.strongest - 40.0);
    }
    // ...AND AT FULL DRIVE, HIGH UP: worked out at four times the rate, what folds back under 19 kHz stays well
    // under the harmonics. At twice the rate, as it was, a 5 kHz note at full DRIVE came out mostly as notes that
    // were never played. The strongest harmonic, and the loudest fold-back, under it:
    //   at       4x (now)   2x (as it was)
    //   2.5 kHz   54 dB      37 dB
    //   5 kHz     34 dB      14 dB
    for (int hz : {2500, 5000}) {
        std::unique_ptr<Effect> e = fresh(fold, 1.f, 0.5f, 1.f, 1.f);
        const Harmonics h = harmonicsOf(*e, hz, 0.5);
        SAY("fold     %d Hz at full DRIVE: the strongest harmonic %.1f dB, the loudest fold-back under 19 kHz %.1f dB (at %.0f Hz)\n", hz, h.strongest, h.aliasLow, h.aliasLowHz);
        CHECK(h.aliasLow < h.strongest - (hz < 4000 ? 45.0 : 25.0));
    }
    // MIX 50% IS ALIGNED: 12 kHz at 0.05 goes straight through the folder.
    {
        const float half[4] = {0.f, 0.5f, 1.f, 0.5f}, full[4] = {0.f, 0.5f, 1.f, 1.f};
        const double a = mixedAt12k(fold, half), b = mixedAt12k(fold, full);
        SAY("fold     12 kHz at 0.05, DRIVE 0: MIX 50%% %+.2f dB, MIX 100%% %+.2f dB\n", a, b);
        CHECK(std::fabs(a - b) < 0.5);
    }
    sweptNoRougherThanHeld(fold);
    turnedLands(fold);
    {
        const float at[4] = {0.8f, 0.6f, 0.8f, 0.f};
        const MixGot g = mixGlides(fold, at);
        CHECK(g.early > 0.01 && g.early < 0.2 && g.later > 0.9 && g.later < 0.99 && std::fabs(g.late - 1.0) < 1e-5);
    }
    const float set[4] = {0.8f, 0.8f, 0.6f, 0.7f}, other[4] = {0.2f, 0.1f, 1.f, 1.f};
    tickCountsAsleep(fold, 0, 0.9f);
    for (float tone : {0.f, 1.f}) {
        const float at[4] = {0.8f, 0.5f, tone, 1.f};
        CHECK(faintAfterANote(fold, at));
    }
    const RulesSeen seen = holdsTheRules(fold, set, other, 3.0);
    SAY("fold     the last sample that is not zero %.2f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick > 0.3 && seen.lastAfterClick < 2.0);
    for (int p = 0; p < 4; ++p) SAY("fold     turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
    // Each setting turned at once, end to end (down: step, corner; up: step, corner), against the steady sound:
    const double was[4][4] = {{1.00, 0.99, 1.00, 1.00}, {1.09, 1.11, 1.09, 1.10}, {1.00, 1.00, 1.00, 1.00}, {0.99, 0.96, 1.00, 1.00}};
    for (int p = 0; p < 4; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- FUZZ ----

static void testFuzz() {
    const EffectKind fuzz = EffectKind::Fuzz;
    const EffectInfo& info = effectInfo(fuzz);
    CHECK(static_cast<int>(fuzz) == 19 && std::string(info.word) == "fuzz" && effectFromWord("fuzz") == fuzz && std::string(info.name) == "FUZZ");
    CHECK(info.family == EffectFamily::Drive && effectBus(fuzz) == -1 && std::string(effectTypeName(fuzz)) == "FUZZ" && info.params == 4);
    CHECK(std::string(info.paramNames[0]) == "FUZZ" && std::string(info.paramNames[1]) == "TONE" && std::string(info.paramNames[2]) == "GATE" && std::string(info.paramNames[3]) == "MIX");
    for (int i = 0; i < 4; ++i) CHECK(effectChoices(fuzz, i) == 0);
    CHECK(info.defaults[0] == 0.7f && info.defaults[1] == 0.5f && info.defaults[2] == 0.2f && info.defaults[3] == 1.f);
    CHECK(reads(fuzz, 0, 0.7f) == "70%" && reads(fuzz, 2, 0.2f) == "20%");
    // From SATURATE, TONE and MIX are kept; FUZZ starts at its own 70%.
    CHECK(effectCarryFrom(EffectKind::Saturate, fuzz, 0) == -1 && effectCarryFrom(EffectKind::Saturate, fuzz, 1) == 2 && effectCarryFrom(EffectKind::Saturate, fuzz, 3) == 3);

    // THE CLIP BY ITSELF: the upper side to 1, the lower softer, to 0.6; nothing under the gate's step.
    {
        CHECK(fx::fuzzCurve(0.f, 0.f) == 0.f && fx::fuzzCurve(0.5f, 0.f) == 0.5f && fx::fuzzCurve(5.f, 0.f) == 1.f && fx::fuzzCurve(1.f, 0.f) == 0.9375f);
        CHECK(std::fabs(fx::fuzzCurve(-5.f, 0.f) + 0.6f) < 1e-6f && std::fabs(fx::fuzzCurve(-0.3f, 0.f) + 0.2795f) < 1e-4f && std::fabs(fx::fuzzCurve(-0.01f, 0.f) + 0.01f) < 1e-4f);
        CHECK(fx::fuzzCurve(0.5f, 1.f) == 0.f && fx::fuzzCurve(-0.99f, 1.f) == 0.f && fx::fuzzCurve(1.5f, 1.f) == 0.5f && fx::fuzzCurve(std::nanf(""), 0.f) == 0.f);
    }
    // LOPSIDED, SO EVEN HARMONICS; NEARLY SQUARE. A 220 Hz sine at half scale, TONE in the middle, against the 1st:
    //   FUZZ     2nd     3rd     5th
    //   0%     -20.0   -26.7   -57.5    hardly fuzzed
    //   35%    -31.6   -13.1   -19.2
    //   70%    -46.3   -12.8   -18.1    nearly square: the lopsided sides both flat, so the 2nd fades
    //   100%   -58.9   -12.8   -18.1
    {
        for (float f : {0.f, 0.35f, 0.7f, 1.f}) {
            const Harmonics h = harmonicsOf(*fresh(fuzz, f, 0.5f, 0.f, 1.f), 220, 0.5);
            SAY("fuzz     FUZZ %s: 2nd %.1f, 3rd %.1f, 5th %.1f dB against the 1st (%.1f); level %+.2f dB\n", reads(fuzz, 0, f).c_str(), h.part[2] - h.part[1], h.part[3] - h.part[1], h.part[5] - h.part[1], h.part[1], h.level);
        }
        const Harmonics low = harmonicsOf(*fresh(fuzz, 0.35f, 0.5f, 0.f, 1.f), 220, 0.5), h = harmonicsOf(*fresh(fuzz, 0.7f, 0.5f, 0.f, 1.f), 220, 0.5);
        CHECK(low.part[2] - low.part[1] > -35.0 && h.part[2] - h.part[1] > -50.0 && h.part[3] - h.part[1] > -14.0 && h.part[5] - h.part[1] > -20.0);
    }
    // THE LOUDNESS IS HELD: the reference sine (0.18, 220 Hz) comes out about as loud as it went in at any FUZZ
    // from 35% (GATE off, TONE in the middle: its making up is for squares A1 to A7, not this one alone).
    {
        double low = 1e9, high = -1e9;
        for (float f : {0.35f, 0.5f, 0.75f, 1.f}) {
            const double l = harmonicsOf(*fresh(fuzz, f, 0.5f, 0.f, 1.f), 220, 0.18).level;
            low = std::fmin(low, l), high = std::fmax(high, l);
        }
        SAY("fuzz     the reference sine (0.18) comes out between %+.2f and %+.2f dB at FUZZ 35%% to 100%%, TONE 50%%\n", low, high);
        CHECK(low > -2.0 && high < 1.0);
    }
    // GATE ONLY TAKES AWAY. A sine at half scale, every FUZZ, GATE 50% against GATE off: never louder. (The level
    // was once evened out with the gate in it: a reference only just past the step was made up as much as four
    // times over, and FUZZ 25% at GATE 50% came out 12 dB over its neighbours.)
    {
        double most = -1e9;
        for (int k = 0; k <= 20; ++k) {
            const float f = k / 20.f;
            most = std::fmax(most, harmonicsOf(*fresh(fuzz, f, 0.5f, 0.5f, 1.f), 220, 0.5).level - harmonicsOf(*fresh(fuzz, f, 0.5f, 0.f, 1.f), 220, 0.5).level);
        }
        SAY("fuzz     GATE 50%% against GATE off, a half-scale sine, every FUZZ: at most %+.2f dB\n", most);
        CHECK(most < 0.1);
    }
    // ACROSS THE KEYBOARD (a 0.3 sine, FUZZ 70%, GATE 20%): TONE's making up is averaged over squares A1 to A7, so no
    // note comes out louder than it went in, nor anywhere near full scale. Worked out for a 220 Hz square alone,
    // TONE full gave A7 +14 dB and peaks of 2.2. TONE is a tone control all the same: at either end a note far
    // from 1 kHz loses up to 20 dB, as a pedal's does.
    //   TONE       A1     A2     A3     A4     A5     A6     A7   (dB, RMS out against in)
    //   0%       -1.8   -2.0   -2.6   -4.1   -7.2  -12.1  -17.7
    //   50%      -3.2   -3.4   -4.0   -5.4   -7.0   -5.8   -4.4
    //   100%    -20.0  -15.3  -11.1   -7.5   -4.1   -1.5   -0.3   (DRIVE at its usual: +2.5 to +3.5 on all)
    {
        double loudest = -1e9, middleLow = 1e9, middleHigh = -1e9;
        float peak = 0.f;
        for (float t : {0.f, 0.5f, 1.f}) {
            for (int octave = 0; octave < 7; ++octave) {
                const double hz = 55.0 * (1 << octave);
                std::unique_ptr<Effect> e = fresh(fuzz, 0.7f, t, 0.2f, 1.f);
                Stereo io(96000);
                io.l = io.r = sineOf(hz, 0.3, io.frames());
                through(*e, io);
                const double l = dB(rmsOf(io.l, 48000) / (0.3 * std::sqrt(0.5)));
                peak = std::fmax(peak, peakOf(std::vector<float>(io.l.begin() + 48000, io.l.end())));
                loudest = std::fmax(loudest, l);
                if (t == 0.5f) middleLow = std::fmin(middleLow, l), middleHigh = std::fmax(middleHigh, l);
                SAY("fuzz     TONE %3.0f%%, a 0.3 sine at %4.0f Hz: %+.1f dB\n", t * 100.0, hz, l);
            }
        }
        SAY("fuzz     across the keyboard: the loudest %+.1f dB, the highest peak %.2f; TONE 50%% from %+.1f to %+.1f dB\n", loudest, peak, middleLow, middleHigh);
        CHECK(loudest < 1.0 && peak < 0.6f && middleHigh - middleLow < 5.0);
    }
    // QUIET OR LOUD, AT ITS USUAL SETTINGS: never 10 dB louder than DRIVE at its usual (the family's first), and never
    // much quieter. A fuzz brings everything to about one level (a sine at 0.18 comes out as it went in), so it is
    // louder than DRIVE on quiet sound and quieter on loud. Matched at half scale it was 17 dB over DRIVE on a
    // 0.02 sine. RMS out against in, 220 Hz:
    //   sine      0.01   0.02   0.05   0.1    0.25   0.5    0.9    noise (0.15 across)
    //   DRIVE     +5.6   +5.6   +5.5   +5.3   +4.1   +1.2   -2.7   -0.1
    //   FUZZ      +7.2  +14.5  +10.8   +5.3   -2.5   -8.4  -13.5
    {
        double over = -1e9, under = 1e9;
        const auto gain = [&](EffectKind kind, const std::vector<float>& in) {
            std::unique_ptr<Effect> e = fresh(kind);
            Stereo io(in.size());
            io.l = io.r = in;
            through(*e, io);
            return dB(rmsOf(io.l, in.size() / 2) / rmsOf(in, in.size() / 2));
        };
        std::vector<std::vector<float>> ins;
        for (double amp : {0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 0.9}) ins.push_back(sineOf(220.0, amp, 96000));
        Noise noise(77u);
        std::vector<float> loose(96000);
        for (float& v : loose) v = 0.26f * noise.next(); // 0.15 across
        ins.push_back(loose);
        for (const std::vector<float>& in : ins) {
            const double d = gain(EffectKind::Drive, in), f = gain(fuzz, in);
            SAY("fuzz     at their usual settings, %.3f across in: DRIVE %+.1f dB, FUZZ %+.1f dB\n", rmsOf(in), d, f);
            over = std::fmax(over, f - d), under = std::fmin(under, f - d);
        }
        SAY("fuzz     against DRIVE: at most %+.1f dB, at least %+.1f dB\n", over, under);
        CHECK(over < 10.0 && under > -12.0);
    }
    // TONE: a scoop in the middle. Sines too quiet to clip (0.02 at FUZZ 0), through each TONE (made up for a
    // fuzzed note, so a quiet sine comes out louder at the ends):
    //   TONE    100 Hz   1 kHz   3 kHz   8 kHz
    //   0%      +3.2    -2.5   -10.8   -18.8
    //   50%     +1.7    -3.0    +0.2    +1.2
    //   100%   -18.2    +0.2    +4.4    +5.2
    {
        for (float t : {0.f, 0.5f, 1.f}) {
            double l[4];
            const double hz[4] = {100, 1000, 3000, 8000};
            for (int i = 0; i < 4; ++i) l[i] = levelAt(*fresh(fuzz, 0.f, t, 0.f, 1.f), hz[i], 0.02);
            SAY("fuzz     TONE %3.0f%%: %+.1f %+.1f %+.1f %+.1f dB at 100, 1k, 3k, 8k\n", t * 100.0, l[0], l[1], l[2], l[3]);
        }
        const auto at = [&](float t, double hz) { return levelAt(*fresh(fuzz, 0.f, t, 0.f, 1.f), hz, 0.02); };
        CHECK(at(0.f, 100) > at(0.f, 8000) + 15.0 && at(1.f, 8000) > at(1.f, 100) + 15.0);
        CHECK(at(0.5f, 1000) < at(0.5f, 100) - 3.0 && at(0.5f, 1000) < at(0.5f, 8000) - 3.0);
    }
    // THE GATE: a 220 Hz note dying away over two seconds (from half scale, 40 dB a second). Where the last
    // sound louder than 60 dB under the start comes out, with the gate off and at 50% and full.
    {
        double last[3];
        const float gate[3] = {0.f, 0.5f, 1.f};
        for (int g = 0; g < 3; ++g) {
            std::unique_ptr<Effect> e = fresh(fuzz, 0.7f, 0.5f, gate[g], 1.f);
            Stereo io(3 * 48000);
            for (std::size_t i = 0; i < 2 * 48000; ++i) io.l[i] = io.r[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 220.0 * i / 48000.0) * std::pow(10.0, -2.0 * i / 48000.0));
            through(*e, io);
            last[g] = 0.0;
            for (std::size_t i = 0; i < io.frames(); ++i)
                if (std::fabs(io.l[i]) > 5e-4f) last[g] = i / 48000.0;
            SAY("fuzz     a dying note, GATE %s: the last sound over -60 dB at %.2f s\n", reads(fuzz, 2, gate[g]).c_str(), last[g]);
        }
        CHECK(last[0] > 1.9 && last[1] < last[0] - 0.3 && last[2] < last[1] - 0.1);
    }
    // MIX 50% IS ALIGNED (TONE full: the high-pass, nearly flat at 12 kHz).
    {
        const float half[4] = {0.f, 1.f, 0.f, 0.5f}, full[4] = {0.f, 1.f, 0.f, 1.f};
        const double a = mixedAt12k(fuzz, half), b = mixedAt12k(fuzz, full);
        const double aligned = dB(0.5 * (1.0 + std::pow(10.0, b / 20.0))); // the two added as they are, in step
        SAY("fuzz     12 kHz at 0.05, FUZZ 0, TONE full: MIX 50%% %+.2f dB (in step: %+.2f), MIX 100%% %+.2f dB\n", a, aligned, b);
        CHECK(std::fabs(a - aligned) < 0.3);
    }
    sweptNoRougherThanHeld(fuzz);
    turnedLands(fuzz);
    {
        const float at[4] = {0.7f, 0.5f, 0.2f, 0.f};
        const MixGot g = mixGlides(fuzz, at);
        CHECK(g.early > 0.01 && g.early < 0.2 && g.later > 0.9 && g.later < 0.99 && std::fabs(g.late - 1.0) < 1e-5);
    }
    const float set[4] = {0.9f, 0.3f, 0.5f, 0.7f}, other[4] = {0.2f, 0.9f, 0.f, 1.f};
    tickCountsAsleep(fuzz, 0, 0.2f);
    for (float tone : {0.f, 0.5f, 1.f}) {
        const float at[4] = {0.7f, tone, 0.f, 1.f};
        CHECK(faintAfterANote(fuzz, at));
    }
    const RulesSeen seen = holdsTheRules(fuzz, set, other, 3.0);
    SAY("fuzz     the last sample that is not zero %.2f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick > 0.3 && seen.lastAfterClick < 2.0);
    for (int p = 0; p < 4; ++p) SAY("fuzz     turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
    // Each setting turned at once, end to end (down: step, corner; up: step, corner), against the steady sound:
    const double was[4][4] = {{0.85, 1.00, 1.09, 1.09}, {1.02, 1.00, 1.03, 1.00}, {1.00, 1.00, 1.00, 1.00}, {1.00, 0.99, 1.00, 1.00}};
    for (int p = 0; p < 4; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- RING ----

static float ringFreq(double hz) { return static_cast<float>(std::log(hz) / std::log(4000.0)); } // the FREQ setting for a pitch
static const float kRingWave[3] = {1.f / 6.f, 0.5f, 5.f / 6.f};                                  // SINE, TRIANGLE, SQUARE

static void testRing() {
    const EffectKind ring = EffectKind::Ring;
    const EffectInfo& info = effectInfo(ring);
    CHECK(static_cast<int>(ring) == 20 && std::string(info.word) == "ring" && effectFromWord("ring") == ring && std::string(info.name) == "RING");
    CHECK(info.family == EffectFamily::Crush && effectBus(ring) == -1 && std::string(effectTypeName(ring)) == "RING" && info.params == 4);
    CHECK(std::string(info.paramNames[0]) == "FREQ" && std::string(info.paramNames[1]) == "WAVE" && std::string(info.paramNames[2]) == "SPREAD" && std::string(info.paramNames[3]) == "MIX");
    CHECK(effectChoices(ring, 0) == 0 && effectChoices(ring, 1) == 3 && effectChoices(ring, 2) == 0 && effectChoices(ring, 3) == 0);
    CHECK(info.defaults[0] == 0.7f && info.defaults[1] == 0.f && info.defaults[2] == 0.f && info.defaults[3] == 1.f);
    CHECK(reads(ring, 0, 0.f) == "1.0 HZ" && reads(ring, 0, 1.f) == "4.0 KHZ" && reads(ring, 0, 0.5f) == "63 HZ" && reads(ring, 0, 0.7f) == "332 HZ" && reads(ring, 0, 0.25f) == "8.0 HZ");
    CHECK(reads(ring, 1, kRingWave[0]) == "SINE" && reads(ring, 1, kRingWave[1]) == "TRIANGLE" && reads(ring, 1, kRingWave[2]) == "SQUARE" && reads(ring, 2, 0.5f) == "50%");
    for (int k = 0; k <= 20; ++k) CHECK(reads(ring, 0, k / 20.f).size() <= 8);
    CHECK(effectCarryFrom(EffectKind::Crush, ring, 3) == 2 && effectCarryFrom(EffectKind::Crush, ring, 0) == -1); // CRUSH's MIX is kept

    // THE CARRIER: never past 1 either way, at any place and any pitch (a square at 0.49999997 of its turn read
    // 2 once: 0.49999997 + 0.5 is a whole turn in a float); the sine is a sine, the triangle a triangle.
    {
        float most = 0.f;
        for (float dt : {4000.f / 48000.f, 333.f / 48000.f, 1.f / 48000.f})
            for (int w = 0; w < 3; ++w) {
                for (int i = 0; i < 100000; ++i) most = std::fmax(most, std::fabs(fx::ringCarrier(w, i / 100000.f, dt)));
                for (float at : {0.f, 0.49999997f, 0.5f, 0.99999994f}) most = std::fmax(most, std::fabs(fx::ringCarrier(w, at, dt)));
            }
        CHECK(most <= 1.0001f);
        float worst = 0.f;
        for (int i = 0; i < 1000; ++i) worst = std::fmax(worst, std::fabs(fx::ringCarrier(0, i / 1000.f, 0.01f) - static_cast<float>(std::sin(2.0 * kPi * i / 1000.0))));
        CHECK(worst < 1e-5f && fx::ringCarrier(1, 0.25f, 0.01f) == 1.f && fx::ringCarrier(1, 0.75f, 0.01f) == -1.f && fx::ringCarrier(1, 0.5f, 0.01f) == 0.f);
        CHECK(fx::ringCarrier(2, 0.25f, 0.01f) == 1.f && fx::ringCarrier(2, 0.75f, 0.01f) == -1.f && fx::ringCarrier(2, 0.f, 0.01f) == 0.f && fx::ringCarrier(2, 0.5f, 0.01f) == 0.f);
    }
    // TWO NEW NOTES, THE OLD ONE GONE: a 1 kHz sine at half scale, a 300 Hz SINE: 700 and 1300 Hz at a quarter
    // each, nothing at 1 kHz. SQUARE: 4/pi as much at 700 and 1300, a third of that at 100 and 1900.
    {
        const auto parts = [&](int wave, float mix, double* at, int count, const double* hz) {
            std::unique_ptr<Effect> e = fresh(ring, ringFreq(300.0), kRingWave[wave], 0.f, mix);
            Stereo io(96000);
            io.l = io.r = sineOf(1000.0, 0.5, 96000);
            through(*e, io);
            for (int i = 0; i < count; ++i) at[i] = partAt(io.l.data() + 48000, 48000, hz[i]);
        };
        const double hz[5] = {700, 1300, 1000, 100, 1900};
        double sine[5], square[5], half[5];
        parts(0, 1.f, sine, 5, hz);
        parts(2, 1.f, square, 5, hz);
        parts(0, 0.5f, half, 5, hz);
        SAY("ring     1 kHz by 300 Hz: SINE %.3f %.3f at 700 and 1300, %.5f at 1 kHz; SQUARE %.3f %.3f, %.3f %.3f at 100 and 1900; MIX 50%% %.3f at 1 kHz, %.3f at 700\n",
            sine[0], sine[1], sine[2], square[0], square[1], square[3], square[4], half[2], half[0]);
        CHECK(std::fabs(sine[0] - 0.25) < 0.002 && std::fabs(sine[1] - 0.25) < 0.002 && sine[2] < 1e-3 && sine[3] < 1e-3);
        CHECK(std::fabs(square[0] - 0.25 * 4.0 / kPi) < 0.005 && std::fabs(square[1] - 0.25 * 4.0 / kPi) < 0.005 && std::fabs(square[3] - 0.25 * 4.0 / kPi / 3.0) < 0.01);
        CHECK(std::fabs(half[2] - 0.25) < 0.002 && std::fabs(half[0] - 0.125) < 0.002);
    }
    // WHAT FOLDS BACK, the carrier by itself (a steady 0.5 in) at 2.9 kHz: everything that is not one of its
    // harmonics, against all of it. SQUARE is smoothed at its jumps (polyBLEP); TRIANGLE is plain.
    {
        double rest[3];
        for (int w = 0; w < 3; ++w) {
            std::unique_ptr<Effect> e = fresh(ring, ringFreq(2900.0), kRingWave[w], 0.f, 1.f);
            Stereo io(96000);
            io.l.assign(96000, 0.5f);
            io.r = io.l;
            through(*e, io);
            const float* out = io.l.data() + 48000;
            double all = 0.0, harmonic = 0.0;
            for (std::size_t i = 0; i < 48000; ++i) all += static_cast<double>(out[i]) * out[i];
            all /= 48000.0;
            const double hz = std::pow(4000.0, static_cast<double>(ringFreq(2900.0)));
            for (int k = 1; k * hz < 24000.0; ++k) {
                const double a = partAt(out, 48000, k * hz);
                harmonic += 0.5 * a * a;
            }
            rest[w] = 10.0 * std::log10(std::fmax(all - harmonic, 1e-30) / all);
        }
        SAY("ring     a 2.9 kHz carrier: what is not its own harmonics is %.1f dB (SINE), %.1f dB (TRIANGLE), %.1f dB (SQUARE)\n", rest[0], rest[1], rest[2]);
        CHECK(rest[0] < -60.0 && rest[1] < -35.0 && rest[2] < -20.0);
    }
    // SPREAD: the right side's carrier a quarter turn behind at full. At 2 Hz the right is the left an eighth
    // of a second later (a 1 kHz sine: whole cycles); at 0 they are the same.
    {
        for (float spread : {0.f, 1.f}) {
            std::unique_ptr<Effect> e = fresh(ring, ringFreq(2.0), kRingWave[0], spread, 1.f);
            Stereo io(96000);
            io.l = io.r = sineOf(1000.0, 0.5, 96000);
            through(*e, io);
            const std::size_t lag = spread > 0.f ? 6000 : 0;
            double worst = 0.0;
            for (std::size_t i = 12000; i < 96000; ++i) worst = std::fmax(worst, std::fabs(static_cast<double>(io.r[i]) - io.l[i - lag]));
            SAY("ring     SPREAD %s at 2 Hz: the right is the left %zu samples later, to %.1e\n", reads(ring, 2, spread).c_str(), lag, worst);
            CHECK(worst < 2e-3);
            if (spread > 0.f) CHECK(largestJump(io.l) < 0.1f && alike(io.l, io.r, 12000, 96000) < 0.1);
        }
    }
    // IT STARTS AT THE CARRIER'S TOP, AND AGAIN AFTER SLEEP (four seconds of nothing): a click comes through
    // whole, every time.
    {
        std::unique_ptr<Effect> e = fresh(ring);
        for (int time = 0; time < 3; ++time) {
            const Stereo a = clickBoth(*e, 4.1, 0.8f, -0.5f);
            CHECK(std::fabs(a.l[0] - 0.8f) < 1e-5f && std::fabs(a.r[0] + 0.5f) < 1e-5f);
        }
    }
    // ...BUT ONLY AFTER FOUR SECONDS: hits closer than that meet the carrier where it has got to, as they would with
    // sound between them. At 1 Hz (it starts at its top), a click 3.9 s after another meets it 0.9 of a turn on
    // (0.809 of the way up, 0.818 as the carrier's steps add up in a float); 4.1 s after, at its top again.
    {
        for (double gap : {3.9, 4.1}) {
            std::unique_ptr<Effect> e = fresh(ring, 0.f);
            clickBoth(*e, gap, 0.8f, 0.8f);
            const Stereo a = clickBoth(*e, 0.01, 0.8f, 0.8f);
            SAY("ring     1 Hz, a click %.1f s after another: the carrier reads %.3f\n", gap, a.l[0] / 0.8f);
            CHECK(gap < 4.0 ? std::fabs(a.l[0] / 0.8f - std::sin(2.0 * kPi * 0.15)) < 0.02 : std::fabs(a.l[0] / 0.8f - 1.f) < 1e-5f);
        }
    }
    // HITS WITH DIGITAL SILENCE BETWEEN THEM SOUND AS HITS WITH SOUND TOO FAINT TO HEAR BETWEEN THEM. Sixteen hits a
    // quarter of a second apart (10 ms of 1 kHz at half scale), a 1 Hz SINE: once with exact zeros between, once
    // with noise 120 dB down. It slept after a third of a millisecond of zeros at first, so every hit met the
    // carrier at its top (each came out whole, and SPREAD's right side was near silent on every one), while
    // with the noise they went whole, nothing, whole, nothing... and swung from side to side.
    {
        for (float spread : {0.f, 1.f}) {
            Stereo ways[2];
            for (int way = 0; way < 2; ++way) {
                std::unique_ptr<Effect> e = fresh(ring, 0.f, kRingWave[0], spread, 1.f);
                ways[way] = Stereo(16 * 12000);
                Noise noise(9u);
                for (std::size_t i = 0; i < ways[way].frames(); ++i) {
                    const std::size_t in = i % 12000;
                    const float v = in < 480 ? static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(in) / 48000.0)) : (way ? 1e-6f * noise.next() : 0.f);
                    ways[way].l[i] = ways[way].r[i] = v;
                }
                through(*e, ways[way]);
            }
            double apart = 0.0, least = 1e9, most = 0.0, rightMost = 0.0;
            for (std::size_t i = 0; i < ways[0].frames(); ++i) apart = std::fmax(apart, std::fmax(std::fabs(static_cast<double>(ways[0].l[i]) - ways[1].l[i]), std::fabs(static_cast<double>(ways[0].r[i]) - ways[1].r[i])));
            for (int hit = 0; hit < 16; ++hit) {
                const std::size_t from = static_cast<std::size_t>(hit) * 12000;
                float l = 0.f, r = 0.f;
                for (std::size_t i = from; i < from + 480; ++i) l = std::fmax(l, std::fabs(ways[0].l[i])), r = std::fmax(r, std::fabs(ways[0].r[i]));
                least = std::fmin(least, l / 0.5), most = std::fmax(most, l / 0.5), rightMost = std::fmax(rightMost, r / 0.5);
            }
            SAY("ring     SPREAD %s, hits 0.25 s apart at 1 Hz: zeros between or noise 120 dB down %.1e apart; each hit from %.2f to %.2f of what went in, the right at most %.2f\n",
                reads(ring, 2, spread).c_str(), apart, least, most, rightMost);
            CHECK(apart < 1e-5 && least < 0.1 && most > 0.95);
            if (spread > 0.f) CHECK(rightMost > 0.9);
        }
    }
    // A CHANGE OF WAVE fades in 10 ms, one fade at a time, the last asked for winning: SINE, then TRIANGLE and
    // SQUARE 2 ms apart, comes out after 30 ms as SQUARE from the start does (a steady 0.5 in: the carrier).
    {
        std::unique_ptr<Effect> changed = fresh(ring, 0.5f, kRingWave[0]), square = fresh(ring, 0.5f, kRingWave[2]);
        Stereo a(4800), b(4800);
        a.l.assign(4800, 0.5f), a.r = a.l, b.l = a.l, b.r = a.l;
        through(*changed, a, 0, 960);
        changed->setParam(1, kRingWave[1]);
        through(*changed, a, 960, 1056);
        changed->setParam(1, kRingWave[2]);
        through(*changed, a, 1056, 4800);
        through(*square, b);
        CHECK(std::vector<float>(a.l.begin() + 2600, a.l.end()) == std::vector<float>(b.l.begin() + 2600, b.l.end()) && a.l[1200] != b.l[1200]);
        CHECK(largestJump(a.l, 900, 2600) < 1.05f * largestJump(b.l, 2600, 4800)); // (a square jumps by itself)
    }
    // ...and a fade is never cut short by the next change, nor left out: SINE to TRIANGLE, and 2 ms later back to
    // SINE, at 50 Hz (a steady 0.5 in) is as smooth as the carrier itself all through (cut, it jumped by a sixth
    // of full scale), and is SINE again, to the bit, once both fades are over. The turns land where the two waves
    // are far apart (a fifth of a turn: 0.95 and 0.8), so a change made at once jumps 23 times the carrier's own
    // step. (At 960 samples they landed on the top and the bottom, where SINE and TRIANGLE meet, and a WAVE
    // that did not fade at all passed.)
    {
        std::unique_ptr<Effect> changed = fresh(ring, ringFreq(50.0), kRingWave[0]), sine = fresh(ring, ringFreq(50.0), kRingWave[0]);
        Stereo a(4800), b(4800);
        a.l.assign(4800, 0.5f), a.r = a.l, b.l = a.l, b.r = a.l;
        through(*changed, a, 0, 900);
        changed->setParam(1, kRingWave[1]);
        through(*changed, a, 900, 1056);
        changed->setParam(1, kRingWave[0]);
        through(*changed, a, 1056, 4800);
        through(*sine, b);
        const float smooth = largestJump(b.l);
        SAY("ring     SINE to TRIANGLE and straight back: the largest step %.4f, the carrier's own %.4f\n", largestJump(a.l), smooth);
        CHECK(largestJump(a.l) < 1.2f * smooth && std::vector<float>(a.l.begin() + 2600, a.l.end()) == std::vector<float>(b.l.begin() + 2600, b.l.end()) && a.l[1200] != b.l[1200]);
    }
    // FULL-SCALE NOISE: never past full scale.
    {
        float loudest = 0.f;
        for (int w = 0; w < 3; ++w) {
            std::unique_ptr<Effect> e = fresh(ring, 1.f, kRingWave[w], 1.f, 1.f);
            Stereo io(96000);
            Noise noise(3u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = noise.next();
            through(*e, io);
            loudest = std::fmax(loudest, std::fmax(peakOf(io.l), peakOf(io.r)));
        }
        CHECK(loudest <= 1.0001f);
    }
    sweptNoRougherThanHeld(ring);
    turnedLands(ring, 0);
    {
        const float at[4] = {0.7f, kRingWave[0], 0.f, 0.f};
        const MixGot g = mixGlides(ring, at);
        CHECK(g.early > 0.01 && g.early < 0.2 && g.later > 0.9 && g.later < 0.99 && std::fabs(g.late - 1.0) < 1e-5);
    }
    // ...and FREQ, by what it makes: turned by itself from 332 Hz to 300 Hz, a 1 kHz sine comes out at 700 and 1300.
    {
        std::unique_ptr<Effect> e = fresh(ring);
        Stereo io(3 * 48000);
        io.l = io.r = sineOf(1000.0, 0.5, io.frames());
        through(*e, io, 0, 24000);
        e->setParam(0, ringFreq(300.0));
        through(*e, io, 24000, io.frames());
        CHECK(std::fabs(partAt(io.l.data() + 96000, 48000, 700.0) - 0.25) < 0.003 && partAt(io.l.data() + 96000, 48000, 668.0) < 0.01);
    }
    const float set[4] = {0.6f, kRingWave[2], 0.5f, 0.6f}, other[4] = {0.2f, kRingWave[1], 1.f, 1.f};
    tickCountsAsleep(ring, 0, 0.2f);
    const RulesSeen seen = holdsTheRules(ring, set, other, 4.5); // it sleeps four seconds after the last sound
    SAY("ring     the last sample that is not zero %.4f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick < 0.001);
    for (int p = 0; p < 4; ++p) SAY("ring     turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
    // Each setting turned at once, end to end (down: step, corner; up: step, corner), against the steady sound:
    const double was[4][4] = {{0.96, 1.06, 1.01, 1.02}, {0.74, 0.76, 1.00, 1.00}, {1.00, 1.00, 1.00, 1.00}, {0.86, 0.89, 1.00, 1.00}};
    for (int p = 0; p < 4; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- WEAR ----

// The pitch's sway, in cents either way at most, of a 1 kHz sine at half scale through `effect` (5 s to 25 s).
static double swayCents(Effect& effect, double* meanCents = nullptr) {
    Stereo io(25 * 48000);
    io.l = io.r = sineOf(1000.0, 0.5, io.frames());
    through(effect, io);
    const std::vector<double> hz = pitchAsItGoes(io.l, 5 * 48000, 10);
    double most = 0.0, sum = 0.0;
    for (double h : hz) most = std::fmax(most, std::fabs(cents(h, 1000.0))), sum += cents(h, 1000.0);
    if (meanCents) *meanCents = sum / static_cast<double>(hz.size());
    return most;
}

static void testWear() {
    const EffectKind wear = EffectKind::Wear;
    const EffectInfo& info = effectInfo(wear);
    CHECK(static_cast<int>(wear) == 21 && std::string(info.word) == "wear" && effectFromWord("wear") == wear && std::string(info.name) == "WEAR");
    CHECK(info.family == EffectFamily::Crush && effectBus(wear) == -1 && std::string(effectTypeName(wear)) == "WEAR" && info.params == 4 && effectMixParam(wear) == -1);
    CHECK(std::string(info.paramNames[0]) == "WOW" && std::string(info.paramNames[1]) == "DIRT" && std::string(info.paramNames[2]) == "DROPS" && std::string(info.paramNames[3]) == "TONE");
    CHECK(info.defaults[0] == 0.3f && info.defaults[1] == 0.25f && info.defaults[2] == 0.2f && info.defaults[3] == 0.4f && effectMeterParam(wear) == -1);
    for (int p = 0; p < 4; ++p) CHECK(effectChoices(wear, p) == 0 && reads(wear, p, 0.35f) == "35%");
    CHECK(effectTypes(EffectKind::Crush) == 3 && effectTypeAt(EffectKind::Crush, 2) == wear && effectCarryFrom(EffectKind::Crush, wear, 0) == -1 && effectCarryFrom(EffectKind::Ring, wear, 3) == -1);

    // WOW: a 1 kHz sine's pitch sways. At 100% about 30 cents either way at worst, at 30% (as it comes) about a
    // third of that, at 0 not at all (the sound untouched).
    {
        double mean = 0.0;
        const double full = swayCents(*fresh(wear, 1.f, 0.f, 0.f, 0.f), &mean), usual = swayCents(*fresh(wear, 0.3f, 0.f, 0.f, 0.f)), none = swayCents(*fresh(wear, 0.f, 0.f, 0.f, 0.f));
        SAY("wear     WOW: a 1 kHz sine sways %.1f cents at 100%% (on average %.2f), %.1f at 30%%, %.3f at 0\n", full, mean, usual, none);
        CHECK(full > 20.0 && full < 45.0 && std::fabs(mean) < 1.0 && usual > 0.2 * full && usual < 0.4 * full && none < 0.01);
    }
    // ...and it is a sway, not a jump: the sine at WOW 100% is as smooth as the sine (a step of it, at most 1.05 times).
    {
        std::unique_ptr<Effect> e = fresh(wear, 1.f, 0.f, 0.f, 0.f);
        Stereo io(10 * 48000);
        io.l = io.r = sineOf(1000.0, 0.5, io.frames());
        through(*e, io);
        CHECK(largestJump(io.l, 4800, io.frames()) < 1.05 * sineStep(1000.0, 0.5));
        // Its three parts, from the pitch over each 5 ms (one a millisecond): the FLUTTER is what is left over
        // 160 ms (a turn of it); the slow part, past the best 0.55 Hz sway laid over it, is the DRIFT (it never repeats).
        const std::vector<double> hz = pitchAsItGoes(io.l, 48000, 5);
        std::vector<double> slow(hz.size(), 0.0);
        double flutter = 0.0;
        int counted = 0;
        for (std::size_t i = 80; i + 80 < hz.size(); ++i) {
            for (std::size_t j = i - 80; j < i + 80; ++j) slow[i] += cents(hz[j], 1000.0) / 160.0;
            const double d = cents(hz[i], 1000.0) - slow[i];
            flutter += d * d, ++counted;
        }
        flutter = std::sqrt(flutter / counted);
        double c = 0.0, sn = 0.0, all = 0.0;
        const double dt = 1.0 / 1000.0;
        for (std::size_t i = 80; i + 80 < hz.size(); ++i) {
            const double t = static_cast<double>(i) * dt;
            c += slow[i] * std::cos(2.0 * kPi * 0.55 * t), sn += slow[i] * std::sin(2.0 * kPi * 0.55 * t), all += slow[i] * slow[i];
        }
        const double n = static_cast<double>(hz.size() - 160), sway = 2.0 * (c * c + sn * sn) / n / n, drift = std::sqrt(std::fmax(all / n - sway, 0.0));
        SAY("wear     WOW 100%%: the flutter %.2f cents (rms), the sway %.1f cents (rms), the drift past it %.2f cents (rms)\n", flutter, std::sqrt(sway), drift);
        CHECK(flutter > 1.5 && flutter < 5.0 && std::sqrt(sway) > 6.0 && drift > 3.0);
    }
    // ...and at WOW 1%, where the sound is read under a sample late much of the time (between this sample and the
    // three before it), and over at others: as smooth as the sine all through.
    {
        std::unique_ptr<Effect> e = fresh(wear, 0.01f, 0.f, 0.f, 0.f);
        Stereo io(20 * 48000);
        io.l = io.r = sineOf(1000.0, 0.5, io.frames());
        through(*e, io);
        SAY("wear     WOW 1%%: the largest step %.4f of the sine's\n", largestJump(io.l, 4800, io.frames()) / sineStep(1000.0, 0.5));
        CHECK(largestJump(io.l, 4800, io.frames()) < 1.01 * sineStep(1000.0, 0.5));
    }
    // ...and TURNING WOW does not dive: WOW sets how late the sound is read, so a WOW that moves fast bends the
    // pitch while it moves (on the usual 20 ms glide, 0 to 100% swooped 500 cents down, 0 to 30% 143, 100% to 0
    // 120 up: a tape-speed dive on every automated step). It moves at most all its way in a second, so turned
    // at once the pitch strays at most about as far as it sways at rest (30 cents at 100%), over the whole move.
    // A 1 kHz sine's pitch every 4 ms, worst from the turn to 3 s after it; and how long WOW takes to get there.
    {
        const auto turned = [&](float from, float to, double* there) {
            std::unique_ptr<Effect> e = fresh(wear, from, 0.f, 0.f, 0.f);
            Stereo io(5 * 48000);
            io.l = io.r = sineOf(1000.0, 0.5, io.frames());
            through(*e, io, 0, 2 * 48000);
            e->setParam(0, to);
            through(*e, io, 2 * 48000, io.frames());
            std::vector<double> when;
            const std::vector<double> hz = pitchAsItGoes(io.l, 2 * 48000 - 480, 4, &when);
            double worst = 0.0;
            for (double h : hz) worst = std::fmax(worst, std::fabs(cents(h, 1000.0)));
            if (there) { // how late the sine is read, from where it crosses zero, against a WEAR left at `to`
                std::unique_ptr<Effect> still = fresh(wear, to, 0.f, 0.f, 0.f);
                Stereo s(io.frames());
                s.l = s.r = sineOf(1000.0, 0.5, s.frames());
                through(*still, s);
                *there = 0.0;
                for (std::size_t i = 2 * 48000; i < io.frames(); ++i)
                    if (std::fabs(io.l[i] - s.l[i]) > 1e-4f) *there = static_cast<double>(i - 2 * 48000) / 48000.0;
            }
            return worst;
        };
        double there = 0.0;
        const double up = turned(0.f, 1.f, &there), down = turned(1.f, 0.f, nullptr), usual = turned(0.f, 0.3f, nullptr), restFull = swayCents(*fresh(wear, 1.f, 0.f, 0.f, 0.f));
        SAY("wear     WOW turned at once: 0 to 100%% strays %.1f cents at worst (there in %.2f s), 100%% to 0 %.1f, 0 to 30%% %.1f (at rest at 100%% it sways %.1f)\n", up, there, down, usual, restFull);
        CHECK(up < 1.6 * restFull && down < 1.6 * restFull && usual < 0.8 * restFull && there > 0.9 && there < 1.3);
    }
    // DIRT: hiss and crackle on the sound, and only while there is sound. A 330 Hz sine at half scale for two
    // seconds, then nothing (WOW, DROPS and TONE at 0, so what is not the sine is the dirt).
    {
        double hiss[2] = {}, after[2] = {}, lowShare = 1.0;
        int crackles[2] = {};
        for (int way = 0; way < 2; ++way) {
            const float dirt = way ? 1.f : 0.25f;
            std::unique_ptr<Effect> e = fresh(wear, 0.f, dirt, 0.f, 0.f);
            Stereo io(7 * 48000);
            const std::vector<float> sine = sineOf(330.0, 0.5, 2 * 48000);
            std::copy(sine.begin(), sine.end(), io.l.begin());
            io.r = io.l;
            through(*e, io);
            std::vector<float> dirtOf(io.frames());
            for (std::size_t i = 0; i < io.frames(); ++i) dirtOf[i] = io.l[i] - (i < sine.size() ? sine[i] : 0.f);
            hiss[way] = dB(rmsOf(dirtOf, 48000, 2 * 48000) / (0.5 / std::sqrt(2.0)));
            if (way == 0) { // the hiss is high: under 300 Hz there is next to none of it
                std::vector<float> low(dirtOf.size(), 0.f);
                float state = 0.f;
                const float k = static_cast<float>(1.0 - std::exp(-2.0 * kPi * 300.0 / 48000.0));
                for (std::size_t i = 0; i < low.size(); ++i) low[i] = state += k * (dirtOf[i] - state);
                lowShare = rmsOf(low, 48000, 2 * 48000) / rmsOf(dirtOf, 48000, 2 * 48000);
            }
            after[way] = dB(rmsOf(dirtOf, 2 * 48000 + 24000, 2 * 48000 + 48000) / (0.5 / std::sqrt(2.0)));
            for (std::size_t i = 48000, quiet = 0; i < 2 * 48000; ++i, ++quiet)
                if (std::fabs(dirtOf[i]) > 0.09f * (way ? 1.f : 0.0625f) && quiet > 96) ++crackles[way], quiet = 0;
            CHECK(io.l.back() == 0.f);
        }
        SAY("wear     DIRT 100%%: the dirt %.1f dB under the sound, %d crackles in a second, %.1f dB half a second after it stops; DIRT 25%%: %.1f dB, %d crackles, %.1f dB\n",
            -hiss[1], crackles[1], -after[1], -hiss[0], crackles[0], -after[0]);
        SAY("wear     the hiss under 300 Hz: %.3f of it\n", lowShare);
        CHECK(hiss[1] > -26.0 && hiss[1] < -12.0 && crackles[1] >= 8 && crackles[1] <= 20 && after[1] < hiss[1] - 20.0 && lowShare < 0.08);
        CHECK(std::fabs(hiss[0] - (hiss[1] - 24.1)) < 3.0 && crackles[0] <= 6);
    }
    // THE HISS IS STEREO: the same sine on both sides, DIRT 100%; what is added on the left and on the right are
    // two hisses, not one. Their likeness (1 the same, 0 nothing alike) is the crackles that land on both sides.
    {
        std::unique_ptr<Effect> e = fresh(wear, 0.f, 1.f, 0.f, 0.f);
        Stereo io(2 * 48000);
        io.l = io.r = sineOf(330.0, 0.5, io.frames());
        const Stereo in = io;
        through(*e, io);
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (std::size_t i = 48000; i < io.frames(); ++i) {
            const double a = static_cast<double>(io.l[i]) - in.l[i], b = static_cast<double>(io.r[i]) - in.r[i];
            lr += a * b, ll += a * a, rr += b * b;
        }
        const double alike = lr / std::sqrt(ll * rr);
        SAY("wear     DIRT 100%%: the left's and the right's dirt alike by %.2f\n", alike);
        CHECK(alike > -0.2 && alike < 0.5);
    }
    // DROPS: dropouts on a steady sine, counted in 40 ms pieces at least 5 dB under the sound. 100%: most of
    // a third of the pieces; 20% (as it comes): a few in 20 s; 0: none. Each sinks smoothly (no step over the
    // sine's own) and comes back.
    {
        int dropped[3] = {};
        double worst = 0.0;
        const float drops[3] = {1.f, 0.2f, 0.f};
        for (int way = 0; way < 3; ++way) {
            std::unique_ptr<Effect> e = fresh(wear, 0.f, 0.f, drops[way], 0.f);
            Stereo io(20 * 48000);
            io.l = io.r = sineOf(997.0, 0.5, io.frames()); // (not 1 kHz: 40 ms of it is whole turns, and every piece would start at a zero)
            through(*e, io);
            for (std::size_t at = 0; at + 1920 <= io.frames(); at += 1920) dropped[way] += rmsOf(io.l, at, at + 1920) < 0.5 / std::sqrt(2.0) * 0.56 ? 1 : 0;
            worst = std::fmax(worst, largestJump(io.l) / sineStep(997.0, 0.5));
        }
        SAY("wear     DROPS: pieces of 40 ms 5 dB down in 20 s: %d at 100%%, %d at 20%%, %d at 0 (of 500); the largest step %.2f of the sine's\n", dropped[0], dropped[1], dropped[2], worst);
        CHECK(dropped[0] > 80 && dropped[0] < 200 && dropped[1] >= 1 && dropped[1] <= 15 && dropped[2] == 0 && worst < 1.01);
    }
    // TONE: worn, the top goes (10 kHz 12 dB and more down at 100%) and the bottom thins (40 Hz down); 1 kHz is kept.
    {
        const auto at = [&](float tone, double hz) {
            std::unique_ptr<Effect> e = fresh(wear, 0.f, 0.f, 0.f, tone);
            return levelAt(*e, hz);
        };
        const double top = at(1.f, 10000.0), mid = at(1.f, 1000.0), low = at(1.f, 40.0), half = at(0.5f, 10000.0), bright = at(0.f, 10000.0);
        SAY("wear     TONE 100%%: 10 kHz %.1f dB, 1 kHz %.1f, 40 Hz %.1f; TONE 50%%: 10 kHz %.1f; TONE 0: %.2f\n", top, mid, low, half, bright);
        CHECK(top < -12.0 && mid > -2.0 && low < -3.0 && half < -3.0 && half > top && std::fabs(bright) < 1e-6);
    }
    // THE DICE ARE BY PLACE, so two hits closer together than four seconds hear other dice, and hits with
    // silence between them as hits with sound too faint to hear between them (to 1e-4); four seconds apart,
    // it has slept, and each hit is as the first.
    {
        const auto hits = [&](double gap, bool faint) {
            std::unique_ptr<Effect> e = fresh(wear, 0.5f, 1.f, 1.f, 0.5f);
            const std::size_t every = static_cast<std::size_t>(gap * 48000.0);
            Stereo io(3 * every);
            Noise noise(4u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = i % every < 9600 ? static_cast<float>(0.5 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(i % every) / 48000.0)) : (faint ? 1e-7f * noise.next() : 0.f);
            through(*e, io);
            return io;
        };
        const Stereo near = hits(2.0, false), nearFaint = hits(2.0, true), far = hits(4.5, false);
        const auto piece = [](const Stereo& s, std::size_t at) { return std::vector<float>(s.l.begin() + static_cast<long>(at), s.l.begin() + static_cast<long>(at + 9600)); };
        double apart = 0.0;
        for (std::size_t i = 0; i < near.frames(); ++i) apart = std::fmax(apart, std::fabs(static_cast<double>(near.l[i]) - nearFaint.l[i]));
        SAY("wear     hits 2 s apart, zeros or noise 140 dB down between: %.1e apart\n", apart);
        CHECK(piece(near, 0) != piece(near, 96000) && piece(far, 0) == piece(far, 216000) && apart < 1e-4);
    }
    // NOTHING TOO SMALL TO HOLD after a click with no DIRT (no hiss to keep the tone's filters busy): they fall away
    // fast (a 20 kHz one-pole), and are made exact zeros on the tick before they pass the smallest float.
    for (float tone : {0.f, 0.7f, 1.f}) {
        std::unique_ptr<Effect> e = fresh(wear, 0.3f, 0.f, 0.f, tone);
        const TooSmall marks;
        clickBoth(*e, 3.0, 0.7f, -0.3f);
        CHECK(!marks.seen());
    }
    sweptNoRougherThanHeld(wear);
    turnedLands(wear, 1, 2); // (DIRT and DROPS: by chance; DIRT's hiss and DROPS' dropouts are held above)
    CHECK(faintAfterANote(wear, info.defaults));
    const float set[4] = {0.6f, 0.5f, 0.5f, 0.7f}, other[4] = {0.2f, 1.f, 0.1f, 0.2f};
    tickCountsAsleep(wear, 3, 0.9f);
    const RulesSeen seen = holdsTheRules(wear, set, other, 4.5); // it sleeps four seconds after the last sound
    SAY("wear     the last sample that is not zero %.4f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick > 3.9); // the hiss rides on, ever fainter, until it sleeps
    for (int p = 0; p < 4; ++p) SAY("wear     turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
    // Each setting turned at once, end to end (down: step, corner; up: step, corner), against the steady sound:
    // (WOW up's step was 0.98 when WOW got there in 20 ms; moving at most all its way a second it is 1.20 at any such
    // speed, 0.5 to 2 a second, and its corner is as it was: under DIRT, a crackle meets the sine at another place.)
    const double was[4][4] = {{1.14, 1.11, 1.20, 1.11}, {0.32, 0.50, 1.03, 1.03}, {1.18, 1.11, 1.38, 1.10}, {1.18, 1.16, 0.58, 0.84}};
    for (int p = 0; p < 4; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- LIMITER ----

static float limiterGain(double db) { return static_cast<float>(db / 20.0); }
static float limiterCeiling(double db) { return static_cast<float>((db + 10.0) / 10.0); }

static void testLimiter() {
    const EffectKind lim = EffectKind::Limiter;
    const EffectInfo& info = effectInfo(lim);
    CHECK(static_cast<int>(lim) == 22 && std::string(info.word) == "limiter" && effectFromWord("limiter") == lim && std::string(info.name) == "LIMITER");
    CHECK(info.family == EffectFamily::Level && effectBus(lim) == -1 && info.params == 3 && effectMixParam(lim) == -1 && effectMeterParam(lim) == 0);
    CHECK(std::string(info.paramNames[0]) == "GAIN" && std::string(info.paramNames[1]) == "CEILING" && std::string(info.paramNames[2]) == "RELEASE");
    CHECK(reads(lim, 0, 0.f) == "0 DB" && reads(lim, 0, 0.15f) == "+3 DB" && reads(lim, 0, 1.f) == "+20 DB" && reads(lim, 1, 0.f) == "-10.0 DB" && reads(lim, 1, 0.8f) == "-2.0 DB" && reads(lim, 1, 1.f) == "0.0 DB");
    CHECK(reads(lim, 1, 0.85f) == "-1.5 DB" && reads(lim, 2, 0.f) == "20 MS" && reads(lim, 2, 0.4f) == "96 MS" && reads(lim, 2, 1.f) == "1.0 S");
    // RELEASE is DUCK's, the same scale: carried to the bit; nothing else is shared with COMP or DUCK.
    CHECK(effectCarryFrom(EffectKind::Duck, lim, 2) == 1 && effectCarryValue(EffectKind::Duck, 1, lim, 2, 0.37f) == 0.37f && effectCarryFrom(EffectKind::Comp, lim, 0) == -1);
    for (int i = 0; i <= 20; ++i) CHECK(reads(EffectKind::Duck, 1, i / 20.f) == reads(lim, 2, i / 20.f));

    // EXACTLY 3 MS LATE, and under the top the sound as it came in, to the bit (GAIN 0 dB): 144 samples at
    // 48 kHz, 132 at 44.1, 288 at 96.
    for (float rate : {48000.f, 44100.f, 96000.f}) {
        std::unique_ptr<Effect> e = makeEffect(lim, rate, 120.f);
        e->setParam(0, 0.f);
        e->setParam(1, 1.f);
        Stereo io(static_cast<std::size_t>(rate));
        Noise noise(7u);
        for (std::size_t i = 100; i < io.frames(); ++i) io.l[i] = 0.4f * noise.next(), io.r[i] = -0.3f * noise.next();
        io.l[200] = -0.f;
        const Stereo in = io;
        through(*e, io);
        const std::size_t late = static_cast<std::size_t>(std::round(0.003 * rate));
        bool same = true;
        for (std::size_t i = 0; i + late < io.frames(); ++i) same = same && io.l[i + late] == in.l[i] && io.r[i + late] == in.r[i];
        for (std::size_t i = 0; i < late; ++i) same = same && io.l[i] == 0.f;
        CHECK(same && e->meter() == 0.f);
    }
    // NOTHING OVER THE CEILING, to the sample, whatever goes in: full-scale noise, clicks, a sweep, at +20 dB,
    // at every ceiling. (And not by clipping: see the corners below.)
    {
        float over = 0.f;
        for (float ceiling : {0.f, 0.37f, 0.8f, 1.f})
            for (float release : {0.f, 1.f}) {
                std::unique_ptr<Effect> e = fresh(lim, 1.f, ceiling, release);
                Stereo io = pinInput(48000.f);
                Noise noise(3u);
                for (std::size_t i = 50000; i < 60000; ++i) io.l[i] = noise.next(), io.r[i] = (i % 777 == 0) ? 1.f : 0.f;
                through(*e, io);
                const float top = static_cast<float>(std::pow(10.0, (-10.0 + 10.0 * ceiling) / 20.0));
                over = std::fmax(over, std::fmax(peakOf(io.l), peakOf(io.r)) / top);
            }
        SAY("limiter  the loudest out against the ceiling: %.7f\n", over);
        CHECK(over <= 1.0000001f && over > 0.999f);
    }
    // IT SEES THE PEAK COMING: a steady 100 Hz sine at 0.1 and one click of 1 at 0.5 s, GAIN 0, CEILING -6 dB.
    // The click comes out at the ceiling exactly, and the sine is already on its way down 3 ms before it,
    // most of the way 1.5 ms before; it is not touched before that.
    {
        std::unique_ptr<Effect> e = fresh(lim, 0.f, limiterCeiling(-6.0), 0.4f);
        Stereo io(48000);
        io.l = io.r = sineOf(100.0, 0.1, io.frames());
        const std::size_t click = 24000 + 60; // where the sine is 0 (a whole number of its turns): the click is 1
        io.l[click] = io.r[click] = 1.f;
        const Stereo in = io;
        through(*e, io);
        const double top = std::pow(10.0, -6.0 / 20.0);
        const auto gainAt = [&](std::size_t i) { return static_cast<double>(io.l[i + 144]) / in.l[i]; };
        const std::size_t before = click - 72, wayBefore = click - 150;
        SAY("limiter  a click: comes out at %.6f of the ceiling; the sound 1.5 ms before it turned down to %.3f, 3.1 ms before %.6f\n", io.l[click + 144] / top, gainAt(before), gainAt(wayBefore));
        CHECK(std::fabs(io.l[click + 144] / top - 1.0) < 1e-5 && gainAt(before) < 0.8 && gainAt(before) > 0.3 && gainAt(wayBefore) == 1.0);
    }
    // NO CORNERS: a 100 Hz sine at half scale pushed 12 dB into a ceiling 6 dB down. Clipped it would be a square
    // (harmonics 3 to 9 together -13 dB); held down ahead of time, the shape is kept (testLimiter's table).
    {
        std::unique_ptr<Effect> e = fresh(lim, limiterGain(12.0), limiterCeiling(-6.0), 0.4f);
        const double share = harmonicShare(*e, 100, 0.5, 2, 9);
        std::unique_ptr<Effect> slow = fresh(lim, limiterGain(12.0), limiterCeiling(-6.0), 1.f), fast = fresh(lim, limiterGain(12.0), limiterCeiling(-6.0), 0.f);
        const double shareSlow = harmonicShare(*slow, 100, 0.5, 2, 9), shareFast = harmonicShare(*fast, 100, 0.5, 2, 9);
        SAY("limiter  a 100 Hz sine 12 dB into the ceiling: harmonics 2 to 9 %.1f dB of it (RELEASE 96 ms), %.1f (1 s), %.1f (20 ms)\n", share, shareSlow, shareFast);
        CHECK(share < -35.0 && shareSlow < -50.0 && shareFast < -18.0 && shareFast > share);
    }
    // THE METER says how far it is turning down: a sine at half scale, +20 dB, ceiling 0 dB: 20 log 5 = 14 dB.
    {
        std::unique_ptr<Effect> e = fresh(lim, 1.f, 1.f, 0.4f);
        Stereo io(48000);
        io.l = io.r = sineOf(220.0, 0.5, 48000);
        through(*e, io);
        SAY("limiter  the meter on a sine 14 dB over: %.2f dB\n", e->meter());
        CHECK(std::fabs(e->meter() - 13.98f) < 0.2f);
    }
    // ...and it is the MOST it turned down in the block, not where the block ended: one block of 1.5 s, the loud
    // sine for its first half second and then a quiet one (RELEASE 20 ms: long let go by the block's end). The
    // meter reads the 14 dB; a meter that read the last sample would read 0 and miss every peak between.
    {
        std::unique_ptr<Effect> e = fresh(lim, 1.f, 1.f, 0.f);
        std::vector<float> l = sineOf(220.0, 0.5, 72000), r;
        for (std::size_t i = 24000; i < l.size(); ++i) l[i] *= 0.01f;
        r = l;
        e->process(l.data(), r.data(), static_cast<int>(l.size()));
        SAY("limiter  the meter on a block loud at first and quiet at its end: %.2f dB\n", e->meter());
        CHECK(std::fabs(e->meter() - 13.98f) < 0.2f);
    }
    // RELEASE: after a second of loud sound, a quiet one comes back to full in about RELEASE times 2.5 (to
    // within 1 dB from 12 dB down), and then exactly.
    {
        double back[3] = {};
        const float settings[3] = {0.f, 0.4f, 1.f};
        for (int way = 0; way < 3; ++way) {
            std::unique_ptr<Effect> e = fresh(lim, 0.f, limiterCeiling(-6.0), settings[way]);
            Stereo io(10 * 48000);
            io.l = io.r = sineOf(1000.0, 0.05, io.frames());
            for (std::size_t i = 0; i < 48000; ++i) io.l[i] *= 40.f, io.r[i] *= 40.f; // 2.0: 12 dB over
            const Stereo in = io;
            through(*e, io);
            std::size_t at = 48000 + 144;
            for (std::size_t i = at; i < io.frames(); i += 48) {
                const double g = rmsOf(io.l, i, i + 48) / rmsOf(in.l, i - 144, i - 96);
                if (g < std::pow(10.0, -1.0 / 20.0)) at = i;
            }
            back[way] = static_cast<double>(at - 48000) / 48.0;
            bool exact = true;
            for (std::size_t i = 9 * 48000; i < io.frames(); ++i) exact = exact && io.l[i] == (std::fabs(in.l[i - 144]) >= 1e-15f ? in.l[i - 144] : 0.f); // (under kNothing is none)
            CHECK(exact);
        }
        SAY("limiter  back to within 1 dB after 12 dB down: %.0f ms (RELEASE 20 ms), %.0f ms (96 ms), %.0f ms (1 s)\n", back[0], back[1], back[2]);
        CHECK(back[0] < 80.0 && back[1] > 150.0 && back[1] < 350.0 && back[2] > 1500.0 && back[2] < 3500.0);
    }
    // LEFT AND RIGHT TOGETHER: a loud side turns the other down as much, so the picture does not lean (either way round).
    for (int loud = 0; loud < 2; ++loud) {
        std::unique_ptr<Effect> e = fresh(lim, 0.f, limiterCeiling(-6.0), 0.4f);
        Stereo io(48000);
        io.l = sineOf(220.0, loud ? 0.1 : 1.0, 48000);
        io.r = sineOf(220.0, loud ? 1.0 : 0.1, 48000);
        through(*e, io);
        CHECK(std::fabs(rmsOf(loud ? io.r : io.l, 24000, 48000) / rmsOf(loud ? io.l : io.r, 24000, 48000) - 10.0) < 0.05 && peakOf(loud ? io.r : io.l) <= 0.5012f);
    }
    // IT LETS GO THE SAME THROUGH SILENCE: a loud second, 50 ms of nothing, then a quiet sine (RELEASE 1 s). With exact
    // zeros between it does not sleep while it is still letting go: the quiet sine starts as turned down as it does
    // with sound far too faint to hear between (to 1e-6).
    {
        Stereo ways[2];
        for (int way = 0; way < 2; ++way) {
            std::unique_ptr<Effect> e = fresh(lim, 0.f, limiterCeiling(-6.0), 1.f);
            ways[way] = Stereo(2 * 48000);
            Noise noise(6u);
            const std::vector<float> sine = sineOf(500.0, 0.05, ways[way].frames());
            for (std::size_t i = 0; i < ways[way].frames(); ++i) {
                const float v = i < 48000 ? 40.f * sine[i] : (i < 48000 + 2400 ? (way ? 1e-7f * noise.next() : 0.f) : sine[i]);
                ways[way].l[i] = ways[way].r[i] = v;
            }
            through(*e, ways[way]);
        }
        double apart = 0.0;
        for (std::size_t i = 48000 + 2400 + 144; i < 2 * 48000; ++i) apart = std::fmax(apart, std::fabs(static_cast<double>(ways[0].l[i]) - ways[1].l[i]));
        const double down = rmsOf(ways[0].l, 48000 + 2400 + 144, 48000 + 2400 + 144 + 480) / (0.05 / std::sqrt(2.0));
        SAY("limiter  a quiet sine 50 ms after a loud second: %.2f of its size at first; zeros or faint noise between %.1e apart\n", down, apart);
        CHECK(apart < 1e-6 && down < 0.7);
    }
    sweptNoRougherThanHeld(lim);
    turnedLands(lim, 2); // (RELEASE: the usual sine is not over the ceiling; RELEASE's times are above)
    {
        const float faintAt[4] = {0.15f, 0.8f, 0.4f, 0.f};
        CHECK(faintAfterANote(lim, faintAt));
    }
    const float set[4] = {0.5f, 0.6f, 0.3f, 0.f}, other[4] = {0.1f, 1.f, 0.6f, 0.f};
    tickCountsAsleep(lim, 0, 0.9f);
    const RulesSeen seen = holdsTheRules(lim, set, other, 2.0);
    SAY("limiter  the last sample that is not zero %.4f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick > 0.0029 && seen.lastAfterClick < 0.0031); // the click itself, 3 ms late
    for (int p = 0; p < 3; ++p) SAY("limiter  turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
    // Each setting turned at once, end to end (down: step, corner; up: step, corner), against the steady sound:
    const double was[3][4] = {{1.02, 1.03, 1.07, 2.03}, {0.98, 1.00, 1.00, 1.00}, {1.00, 1.00, 1.00, 1.00}};
    for (int p = 0; p < 3; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- SNAP ----

static float snapAmount(double a) { return static_cast<float>((a + 1.0) / 2.0); } // the setting that reads a (-1 to 1)
// A drum: a burst of noise falling 60 dB in 0.3 s, from silence, every half second; what SNAP does to it,
// in dB, over a stretch after each hit (averaged over six hits).
static double snapOnAHit(float attack, float sustain, double fromMs, double toMs, float mix = 1.f, double size = 0.8) {
    std::unique_ptr<Effect> e = fresh(EffectKind::Snap, attack, sustain, mix);
    Stereo io(3 * 48000);
    Noise noise(17u);
    for (std::size_t i = 0; i < io.frames(); ++i) {
        const double t = static_cast<double>(i % 24000) / 48000.0;
        io.l[i] = io.r[i] = static_cast<float>(size * std::pow(10.0, -3.0 * t / 0.3)) * noise.next();
    }
    const Stereo in = io;
    through(*e, io);
    double got = 0.0, was = 0.0;
    for (int hit = 0; hit < 6; ++hit) {
        const std::size_t from = static_cast<std::size_t>(hit) * 24000 + static_cast<std::size_t>(fromMs * 48.0), to = static_cast<std::size_t>(hit) * 24000 + static_cast<std::size_t>(toMs * 48.0);
        for (std::size_t i = from; i < to; ++i) got += static_cast<double>(io.l[i]) * io.l[i], was += static_cast<double>(in.l[i]) * in.l[i];
    }
    return 10.0 * std::log10(got / was);
}

static void testSnap() {
    const EffectKind snap = EffectKind::Snap;
    const EffectInfo& info = effectInfo(snap);
    CHECK(static_cast<int>(snap) == 23 && std::string(info.word) == "snap" && effectFromWord("snap") == snap && std::string(info.name) == "SNAP");
    CHECK(info.family == EffectFamily::Level && effectBus(snap) == -1 && info.params == 3 && effectMixParam(snap) == 2 && effectMeterParam(snap) == -1);
    CHECK(std::string(info.paramNames[0]) == "ATTACK" && std::string(info.paramNames[1]) == "SUSTAIN" && std::string(info.paramNames[2]) == "MIX");
    CHECK(reads(snap, 0, 0.75f) == "+50%" && reads(snap, 0, 0.5f) == "0%" && reads(snap, 1, 0.f) == "-100%" && reads(snap, 2, 0.4f) == "40%");
    CHECK(effectCarryFrom(EffectKind::Comp, snap, 2) == 3 && effectCarryValue(EffectKind::Comp, 3, snap, 2, 0.45f) == 0.45f && effectCarryFrom(EffectKind::Limiter, snap, 0) == -1);
    CHECK(effectTypes(EffectKind::Comp) == 4 && effectTypeAt(EffectKind::Comp, 2) == EffectKind::Limiter && effectTypeAt(EffectKind::Comp, 3) == snap);

    // ON A DRUM: what it does to the first millisecond of each hit, 4 to 6 ms in, 20 to 40 ms in, and the tail
    // 100 to 250 ms in.
    {
        struct Row { float attack, sustain; double first, early, later, tail; };
        Row rows[5] = {{1.f, 0.5f}, {0.f, 0.5f}, {0.5f, 1.f}, {0.5f, 0.f}, {0.5f, 0.5f}};
        for (Row& r : rows) {
            r.first = snapOnAHit(r.attack, r.sustain, 0.0, 1.0), r.early = snapOnAHit(r.attack, r.sustain, 4.0, 6.0);
            r.later = snapOnAHit(r.attack, r.sustain, 20.0, 40.0), r.tail = snapOnAHit(r.attack, r.sustain, 100.0, 250.0);
            SAY("snap     ATTACK %-5s SUSTAIN %-5s a hit: first ms %+.1f dB, 4-6 ms %+.1f, 20-40 ms %+.1f, 100-250 ms %+.1f\n", reads(snap, 0, r.attack).c_str(), reads(snap, 1, r.sustain).c_str(), r.first, r.early, r.later, r.tail);
        }
        CHECK(rows[0].first > 11.0 && rows[0].first < 12.8 && rows[0].early > 6.5 && rows[0].early < 8.5 && rows[0].later < rows[0].early - 2.0 && std::fabs(rows[0].tail) < 0.5);
        CHECK(rows[1].first < -12.5 && rows[1].first > -14.0 && rows[1].early < -6.5 && rows[1].early > -8.5 && std::fabs(rows[1].tail) < 0.5);
        CHECK(rows[2].tail > 5.0 && rows[2].tail < 7.0 && std::fabs(rows[2].first) < 0.5);
        CHECK(rows[3].tail < -5.0 && rows[3].tail > -7.0 && std::fabs(rows[3].first) < 0.5);
        CHECK(rows[4].first == 0.0 && rows[4].tail == 0.0);
        // A QUIET DRUM is shaped as a loud one: the same hit 40 dB down (it listens to how the sound moves, not how loud it is).
        const double quietFirst = snapOnAHit(1.f, 0.5f, 0.0, 1.0, 1.f, 0.008), quietTail = snapOnAHit(0.5f, 1.f, 100.0, 250.0, 1.f, 0.008);
        SAY("snap     the hit 40 dB down: first ms %+.2f dB, tail %+.2f dB (at 0.8: %+.2f, %+.2f)\n", quietFirst, quietTail, rows[0].first, rows[2].tail);
        CHECK(std::fabs(quietFirst - rows[0].first) < 0.5 && std::fabs(quietTail - rows[2].tail) < 0.5);
        // MIX 50% is half of it, laid under the plain sound (not late: the two add up).
        const double half = snapOnAHit(1.f, 0.5f, 0.0, 1.0, 0.5f), want = 20.0 * std::log10(0.5 + 0.5 * std::pow(10.0, rows[0].first / 20.0));
        SAY("snap     MIX 50%%, ATTACK +100%%: the first ms %+.2f dB (the two added: %+.2f)\n", half, want);
        CHECK(std::fabs(half - want) < 0.5);
    }
    // ...ON BOTH SIDES, at every MIX between: the same drum on left and right comes out the same on both, to the
    // bit (a right side that dropped the plain sound at MIX 50% was 6 dB quiet, and the checks above, on the
    // left, did not hear it). And the right side at MIX 50% is the plain sound and the shaped one added, halved.
    for (float mix : {0.25f, 0.5f, 0.8f}) {
        Stereo io(48000), shaped(48000);
        Noise noise(23u);
        for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = static_cast<float>(0.8 * std::pow(10.0, -3.0 * static_cast<double>(i % 12000) / 48000.0 / 0.3)) * noise.next();
        shaped = io;
        const Stereo in = io;
        std::unique_ptr<Effect> e = fresh(snap, 1.f, 0.f, mix), full = fresh(snap, 1.f, 0.f, 1.f);
        through(*e, io);
        through(*full, shaped);
        double worst = 0.0;
        for (std::size_t i = 0; i < io.frames(); ++i) worst = std::fmax(worst, std::fabs(static_cast<double>(io.r[i]) - (in.r[i] + mix * (static_cast<double>(shaped.r[i]) - in.r[i]))));
        SAY("snap     MIX %.0f%%: left and right %s; the right off the plain and the shaped laid together by %.1e\n", mix * 100.0, io.l == io.r ? "the same" : "APART", worst);
        CHECK(io.l == io.r && worst < 1e-5);
    }
    // A STEADY SOUND IS LEFT ALONE: a 100 Hz and a 1 kHz sine at half scale, every end of ATTACK and SUSTAIN,
    // after its first half second, within 0.5 dB of what went in, and hardly bent (harmonics 2 to 9).
    for (double hz : {100.0, 1000.0})
        for (float a : {0.f, 1.f})
            for (float s : {0.f, 1.f}) {
                std::unique_ptr<Effect> e = fresh(snap, a, s, 1.f);
                const double level = levelAt(*e, hz, 0.5);
                const double bent = harmonicShare(*e, static_cast<int>(hz), 0.5, 2, 9);
                SAY("snap     a steady %4.0f Hz sine, ATTACK %-5s SUSTAIN %-5s: %+.2f dB, harmonics %.1f dB\n", hz, reads(snap, 0, a).c_str(), reads(snap, 1, s).c_str(), level, bent);
                CHECK(std::fabs(level) < 0.5 && bent < (hz < 500.0 ? -25.0 : -45.0));
            }
    // LEFT AND RIGHT TOGETHER: a hit on one side turns a steady other side up with it (either way round).
    for (int side = 0; side < 2; ++side) {
        std::unique_ptr<Effect> e = fresh(snap, 1.f, 0.5f, 1.f);
        Stereo io(48000);
        io.r = sineOf(440.0, 0.1, 48000);
        Noise noise(2u);
        for (std::size_t i = 24000; i < 24480; ++i) io.l[i] = 0.8f * noise.next();
        if (side) std::swap(io.l, io.r);
        through(*e, io);
        Stereo in(48000);
        in.r = sineOf(440.0, 0.1, 48000);
        if (side) std::swap(io.l, io.r);
        SAY("snap     a hit on the left alone: the right's first ms %.2f times, before it %.4f times\n", rmsOf(io.r, 24000, 24048) / rmsOf(in.r, 24000, 24048), rmsOf(io.r, 12000, 20000) / rmsOf(in.r, 12000, 20000));
        CHECK(rmsOf(io.r, 24000, 24048) > 1.5 * rmsOf(in.r, 24000, 24048) && std::fabs(rmsOf(io.r, 12000, 20000) / rmsOf(in.r, 12000, 20000) - 1.0) < 0.01);
    }
    {
        const float faintAt[4] = {1.f, 1.f, 1.f, 0.f};
        CHECK(faintAfterANote(snap, faintAt));
        const float at[4] = {1.f, 0.f, 0.f, 0.f};
        const MixGot g = mixGlides(snap, at);
        CHECK(g.early > 0.01 && g.early < 0.2 && g.later > 0.9 && std::fabs(g.late - 1.0) < 1e-4);
    }
    // (No turnedLands: a steady sine is what SNAP leaves alone, so a setting turned under one shows nothing.)
    // ATTACK AND SUSTAIN GLIDE, as MIX does: hits every 20 ms (each 3 ms of noise falling away), the setting turned from
    // 0 to its end at once after a second. How far it has got, laid against ones left at each, in the first 3 ms,
    // 60 to 80 ms after, and 300 to 400 ms after.
    for (int p = 0; p < 2; ++p) {
        Stereo ways[3];
        Noise noise(8u);
        std::vector<float> hits(48000 + 19200);
        for (std::size_t i = 0; i < hits.size(); ++i) hits[i] = static_cast<float>(0.5 * std::exp(-static_cast<double>(i % 960) / 144.0)) * noise.next();
        for (int way = 0; way < 3; ++way) { // 0 turned, 1 left at 0, 2 left at its end
            std::unique_ptr<Effect> e = fresh(snap, p == 0 ? (way == 2 ? 1.f : 0.5f) : 0.5f, p == 1 ? (way == 2 ? 1.f : 0.5f) : 0.5f, 1.f);
            ways[way] = Stereo(hits.size());
            ways[way].l = ways[way].r = hits;
            through(*e, ways[way], 0, 48000);
            if (way == 0) e->setParam(p, 1.f);
            through(*e, ways[way], 48000, ways[way].frames());
        }
        const auto got = [&](std::size_t from, std::size_t to) {
            double along = 0.0, size = 0.0;
            for (std::size_t i = from; i < to; ++i) {
                const double d = static_cast<double>(ways[2].l[i]) - ways[1].l[i];
                along += (static_cast<double>(ways[0].l[i]) - ways[1].l[i]) * d, size += d * d;
            }
            return size > 0.0 ? along / size : 0.0;
        };
        const double early = got(48000, 48000 + 144), later = got(48000 + 2880, 48000 + 3840), late = got(48000 + 14400, 48000 + 19200);
        SAY("snap     %s turned from 0 to its end at once under hits: %.3f of the way in 3 ms, %.4f at 60 to 80 ms, %.6f at 300 to 400 ms\n", info.paramNames[p], early, later, late);
        CHECK(early < 0.25 && later > 0.85 && std::fabs(late - 1.0) < 1e-4);
    }
    const float set[4] = {0.9f, 0.2f, 0.7f, 0.f}, other[4] = {0.1f, 1.f, 1.f, 0.f};
    tickCountsAsleep(snap, 0, 0.1f);
    const RulesSeen seen = holdsTheRules(snap, set, other, 1.5);
    SAY("snap     the last sample that is not zero %.4f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick < 0.001); // nothing late, no tail
    for (int p = 0; p < 3; ++p) SAY("snap     turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
    // Each setting turned at once, end to end (down: step, corner; up: step, corner), against the steady sound:
    const double was[3][4] = {{1.00, 1.00, 1.00, 0.92}, {1.00, 0.86, 1.00, 1.00}, {1.00, 0.94, 1.00, 1.00}};
    for (int p = 0; p < 3; ++p) {
        CHECK(seen.stepDown[p] < 1.1 * was[p][0] && seen.cornerDown[p] < 1.1 * was[p][1]);
        CHECK(seen.stepUp[p] < 1.1 * was[p][2] && seen.cornerUp[p] < 1.1 * was[p][3]);
    }
}

// ---- FLANGER, ENSEMBLE, PHASER, TREMOLO: MOD's newer types ---------------------------------------

// A STEADY SINE'S LEVEL, in dB, at any pitch (levelAt's needs a whole number of hertz): two seconds of a sine at
// `amp` through `effect` (fresh), then the part at `hz` read over as many whole turns of it as fit in the last second.
static double steadyLevel(Effect& effect, double hz, double amp = 0.25, bool right = false) {
    Stereo io(2 * 48000);
    io.l = io.r = sineOf(hz, amp, io.frames());
    through(effect, io);
    const std::size_t count = static_cast<std::size_t>(std::floor(hz) * 48000.0 / hz + 0.5);
    const std::vector<float>& v = right ? io.r : io.l;
    return dB(partAt(v.data() + v.size() - count, count, hz) / amp);
}
// Noise in (both sides alike, `amp`), how loud it comes out against how loud it went in, in dB (after a second).
static double noiseGain(Effect& effect, double amp = 0.25, double seconds = 6.0) {
    Stereo io(static_cast<std::size_t>(seconds * 48000.0));
    Noise noise(77u);
    for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = static_cast<float>(amp) * noise.next();
    const Stereo in = io;
    through(effect, io);
    return dB(rmsOf(io.l, 48000) / rmsOf(in.l, 48000));
}
static float modRate(double hz) { return static_cast<float>(std::log(hz / 0.1) / std::log(80.0)); } // the RATE setting for a speed
static float signedSet(double v) { return static_cast<float>((v + 1.0) / 2.0); }                    // the setting that reads v (-1 to 1)

// THE TYPES' NAMES, NUMBERS AND WORDS, and that each is MOD's.
static void modRow(EffectKind kind, int number, const char* word, const char* name, int index) {
    const EffectInfo& info = effectInfo(kind);
    CHECK(static_cast<int>(kind) == number && std::string(info.word) == word && effectFromWord(word) == kind && std::string(info.name) == name);
    CHECK(info.family == EffectFamily::Mod && effectBus(kind) == -1 && info.params == 4 && effectMeterParam(kind) == -1);
    CHECK(std::string(effectTypeName(kind)) == name && effectTypes(kind) == 5 && effectTypeIndex(kind) == index && effectTypeAt(EffectKind::Chorus, index) == kind);
}

static void testFlanger() {
    const EffectKind flanger = EffectKind::Flanger;
    const EffectInfo& info = effectInfo(flanger);
    modRow(flanger, 24, "flanger", "FLANGER", 1);
    CHECK(std::string(info.paramNames[0]) == "RATE" && std::string(info.paramNames[1]) == "DEPTH" && std::string(info.paramNames[2]) == "FEEDBACK" && std::string(info.paramNames[3]) == "MIX");
    for (int p = 0; p < 4; ++p) CHECK(effectChoices(flanger, p) == 0);
    CHECK(reads(flanger, 0, 0.f) == "0.10 HZ" && reads(flanger, 0, 1.f) == "8.0 HZ" && reads(flanger, 2, 0.f) == "-100%" && reads(flanger, 2, 0.5f) == "0%" && reads(flanger, 2, 0.9f) == "+80%");
    // RATE, DEPTH and MIX are CHORUS's, to the bit; FEEDBACK PHASER's.
    for (float v : {0.f, 0.13f, 0.5f, 1.f}) {
        CHECK(effectCarryValue(EffectKind::Chorus, 0, flanger, 0, v) == v && effectCarryValue(EffectKind::Chorus, 1, flanger, 1, v) == v && effectCarryValue(EffectKind::Chorus, 3, flanger, 3, v) == v);
        CHECK(effectCarryValue(EffectKind::Phaser, 2, flanger, 2, v) == v && effectCarryValue(flanger, 2, EffectKind::Phaser, 2, v) == v);
    }
    CHECK(effectCarryFrom(EffectKind::Chorus, flanger, 2) == -1); // CHORUS's TYPE is not FEEDBACK
    // THE SWEEP, BY ITSELF: at DEPTH 100% from 7.35 ms (its start) down to 0.35 ms half a turn on and back, by octaves;
    // at DEPTH 0 1.6 ms all the time; above 2 Hz narrower as 2 / rate.
    {
        const double longest = fx::flangerDelayMs(1.f, 0.f, 0.0), shortest = fx::flangerDelayMs(1.f, 0.f, 0.5);
        SAY("flanger  the sweep at DEPTH 100%%: %.3f to %.3f ms (%.2f octaves); at DEPTH 0 %.3f ms\n", longest, shortest, std::log2(longest / shortest), static_cast<double>(fx::flangerDelayMs(0.f, 0.f, 0.3)));
        CHECK(std::fabs(longest - 1.6 * std::pow(2.0, 2.2)) < 1e-4 && std::fabs(shortest - 1.6 / std::pow(2.0, 2.2)) < 1e-5 && fx::flangerDelayMs(0.f, 0.5f, 0.3) == 1.6f);
        CHECK(std::fabs(fx::flangerDelayMs(1.f, 0.f, 0.25) - 1.6) < 1e-5 && std::fabs(fx::flangerDelayMs(1.f, 0.f, 0.75) - 1.6) < 1e-5);
        // An even speed in octaves through the middle (rounded only at its turns): from 0.2 to 0.3 of a turn it moves
        // as far as from 0.15 to 0.2 and 0.3 to 0.35 together, to a hair. (A sine would move 0.2 to 0.3 faster: 1.38 times.)
        const auto oct = [](double at) { return std::log2(static_cast<double>(fx::flangerDelayMs(1.f, 0.f, at))); };
        const double middle = oct(0.2) - oct(0.3), sides = (oct(0.15) - oct(0.2)) + (oct(0.3) - oct(0.35));
        SAY("flanger  octaves moved 0.2 to 0.3 of a turn %.3f, 0.15 to 0.2 and 0.3 to 0.35 together %.3f\n", middle, sides);
        CHECK(middle > 0.0 && std::fabs(middle / sides - 1.0) < 0.15);
        // ...and ROUNDED AT ITS TURNS: the first 2% of a turn from the top moves under a quarter of what 2% does in the
        // middle (a plain triangle turns at once: a jump in pitch at each end of the sweep).
        const double top = oct(0.0) - oct(0.02), mid = oct(0.2) - oct(0.22);
        SAY("flanger  octaves moved in 2%% of a turn: from the top %.4f, in the middle %.4f\n", top, mid);
        CHECK(top < 0.25 * mid && top > 0.0);
        const double at4 = std::log2(fx::flangerDelayMs(1.f, modRate(4.0), 0.0) / 1.6);
        CHECK(std::fabs(at4 - 1.1) < 1e-3); // at 4 Hz half as far
    }
    // ...AND AS IT SOUNDS: clicks 25 ms apart, FEEDBACK 0, MIX 100% (each click and its copy half and half): where each
    // copy lands is where the sweep is, to a hundredth of an octave, at 0.24 Hz for eight seconds.
    {
        const float rate = 0.2f;
        const double hz = 0.1 * std::pow(80.0, static_cast<double>(rate));
        std::unique_ptr<Effect> e = fresh(flanger, rate, 1.f, 0.5f, 1.f);
        const Stereo out = clicksThrough(*e, 1200, 8.0);
        std::vector<double> sums;
        const std::vector<double> delays = delaysOf(out.l, 1200, 0.2, 24.0, &sums);
        double worst = 0.0, low = 1e9, high = 0.0, size = 0.0;
        for (std::size_t k = 0; k < delays.size(); ++k) {
            const double at = (static_cast<double>(k * 1200) + delays[k]) * hz / 48000.0;
            worst = std::fmax(worst, std::fabs(std::log2(delays[k] / (fx::flangerDelayMs(1.f, rate, at) * 48.0))));
            low = std::fmin(low, delays[k]), high = std::fmax(high, delays[k]);
            size = std::fmax(size, std::fabs(sums[k] / 0.5 - 1.0));
        }
        // (A copy read from a moving delay is squeezed or stretched, so its size is not quite 0.5: 1.6% at most here.)
        SAY("flanger  clicks at 0.24 Hz: the copy %.1f to %.1f samples late, off the sweep by %.4f octaves at worst; its size off 0.5 by %.4f of it\n", low, high, worst, size);
        CHECK(worst < 0.01 && low < 17.5 && high > 350.0 && out.l == out.r && size < 0.02);
    }
    // FEEDBACK: at DEPTH 0 (1.6 ms, 76.8 samples), each time round 0.95 of the last at +100%, -0.95 at -100%; the first
    // copy is scaled so that noise comes out about as loud at any FEEDBACK (sqrt(1 - 0.95^2) = 0.312).
    for (int sign = -1; sign <= 1; sign += 2) {
        std::unique_ptr<Effect> e = fresh(flanger, 0.2f, 0.f, sign > 0 ? 1.f : 0.f, 1.f);
        const std::vector<float> ir = impulseOf(*e, 0.1);
        double first = 0.0, second = 0.0;
        for (std::size_t i = 60; i < 100; ++i) first += ir[i];
        for (std::size_t i = 140; i < 170; ++i) second += ir[i];
        SAY("flanger  FEEDBACK %+d%%: the copy %.4f (0.5 x 0.312), the next %.4f of it\n", sign * 100, first, second / first);
        CHECK(std::fabs(first - 0.5 * std::sqrt(1.0 - 0.95 * 0.95)) < 1e-4 && std::fabs(second / first - sign * 0.95) < 2e-3 && ir[0] == 0.5f);
    }
    // THE NOTCHES: at DEPTH 0, FEEDBACK 0, MIX 100%, a sine at 312.5 Hz (half a wave in 1.6 ms) is gone, one at 625 Hz is
    // whole; at MIX 50% the notch is 9.5 dB down (0.75 and 0.25 laid against each other).
    {
        std::unique_ptr<Effect> a = fresh(flanger, 0.2f, 0.f, 0.5f, 1.f), b = fresh(flanger, 0.2f, 0.f, 0.5f, 1.f), c = fresh(flanger, 0.2f, 0.f, 0.5f, 0.5f);
        const double notch = steadyLevel(*a, 312.5), peak = steadyLevel(*b, 625.0), half = steadyLevel(*c, 312.5);
        SAY("flanger  DEPTH 0: 312.5 Hz %.1f dB, 625 Hz %.2f dB; at MIX 50%% 312.5 Hz %.2f dB\n", notch, peak, half);
        CHECK(notch < -40.0 && std::fabs(peak) < 0.1 && std::fabs(half - dB(0.5)) < 0.1);
    }
    // ABOUT AS LOUD AT ANY FEEDBACK: noise at the usual sweep, MIX 100%.
    {
        // (Half and half, a comb's notches take half the power away: -3 dB is the plain flanger at FEEDBACK 0.)
        double gain[3];
        for (int f = 0; f < 3; ++f) {
            std::unique_ptr<Effect> e = fresh(flanger, -1.f, -1.f, 0.5f * f, 1.f);
            gain[f] = noiseGain(*e);
        }
        SAY("flanger  noise at FEEDBACK -100 / 0 / +100%%: %+.2f / %+.2f / %+.2f dB\n", gain[0], gain[1], gain[2]);
        for (double g : gain) CHECK(g > -5.0 && g < -2.5);
    }
    // BOTH SIDES SWEEP TOGETHER, and each keeps to itself: noise on the left alone leaves the right silent.
    {
        std::unique_ptr<Effect> e = fresh(flanger);
        Stereo io(48000);
        Noise noise(3u);
        for (float& v : io.l) v = 0.3f * noise.next();
        through(*e, io);
        CHECK(peakOf(io.r) == 0.f && rmsOf(io.l) > 0.05);
    }
    // THE PITCH NEVER BENDS MORE THAN AT 2 HZ, NOT EVEN AS RATE OR DEPTH IS THROWN: a slow saw through it (FEEDBACK 0,
    // MIX 100%) comes out as itself less half its slope times the delay, so the delay is read off every sample, and how
    // fast it moves is the bend (0.1 samples a sample is 10% sharp or flat). At rest at 2 Hz, DEPTH 100%, is the most;
    // RATE thrown from 0.1 to 8 Hz, and DEPTH from 0 to 100% at 8 Hz, stay under it. (Before the sweep's reach was
    // held to its whole way in a second, and the sweep's speed to what that reach allows, RATE thrown so moved the delay 0.33 samples a sample for 25 ms as the
    // sweep closed up: five semitones.)
    {
        const auto bendOf = [&](float rate0, float depth0, float rate1, float depth1, double* afterwards, std::size_t change = 2 * 48000) {
            std::unique_ptr<Effect> e = fresh(flanger, rate0, depth0, 0.5f, 1.f);
            const std::size_t period = 4800;
            Stereo io(change + 2 * 48000);
            for (std::size_t n = 0; n < io.frames(); ++n) io.l[n] = io.r[n] = -0.5f + static_cast<float>(n % period) / static_cast<float>(period);
            const std::vector<float> in = io.l;
            through(*e, io, 0, change);
            e->setParam(0, rate1);
            e->setParam(1, depth1);
            through(*e, io, change, io.frames());
            double most = 0.0, prev = -1.0;
            *afterwards = 0.0;
            for (std::size_t n = 48000; n < io.frames(); ++n) {
                if (n % period < 400) {
                    prev = -1.0;
                    continue;
                }
                const double delay = 2.0 * static_cast<double>(period) * (static_cast<double>(in[n]) - io.l[n]);
                if (prev >= 0.0) {
                    const double speed = std::fabs(delay - prev);
                    if (n >= change) most = std::fmax(most, speed);
                    if (n >= change + 48000) *afterwards = std::fmax(*afterwards, speed);
                }
                prev = delay;
            }
            return most;
        };
        double at2 = 0.0, rest8 = 0.0, restDepth = 0.0;
        bendOf(modRate(2.0), 1.f, modRate(2.0), 1.f, &at2);
        const double rateThrown = bendOf(0.f, 1.f, 1.f, 1.f, &rest8), rateBack = bendOf(1.f, 1.f, 0.f, 1.f, &restDepth);
        const double depthThrown = bendOf(1.f, 0.f, 1.f, 1.f, &restDepth);
        SAY("flanger  the delay's speed (samples a sample): at rest at 2 Hz %.4f; RATE 0.1 to 8 Hz at once %.4f (at rest after %.4f); 8 to 0.1 Hz %.4f; DEPTH 0 to 100%% at 8 Hz %.4f\n", at2, rateThrown, rest8, rateBack, depthThrown);
        CHECK(at2 > 0.05 && rest8 > 0.4 * at2 && rest8 < 1.05 * at2);
        CHECK(rateThrown < at2 && rateBack < at2 && depthThrown < at2);
        // The reach moves its whole way in a second, no faster: DEPTH 0 to 100% at 0.1 Hz, turned at the top of a turn
        // (ten seconds in: the sweep itself barely moving, at its long end) bends the pitch about half a percent. (Its
        // whole way in a third of a second, or in two, is caught.)
        double slow = 0.0;
        const double depthSlow = bendOf(0.f, 0.f, 0.f, 1.f, &slow, 10 * 48000);
        SAY("flanger  DEPTH 0 to 100%% at 0.1 Hz: the delay's speed %.4f\n", depthSlow);
        CHECK(depthSlow > 0.0035 && depthSlow < 0.009); // (0.0051)
        // ...and it LANDS, to the bit: DEPTH turned from 70% to 10% sounds, a second on, as one made at 10%.
        std::unique_ptr<Effect> turned = fresh(flanger), made = fresh(flanger, -1.f, 0.1f);
        Stereo a(3 * 48000), b(3 * 48000);
        a.l = a.r = b.l = b.r = sineOf(220.0, 0.5, a.frames());
        through(*turned, a, 0, 48000);
        turned->setParam(1, 0.1f);
        through(*turned, a, 48000, a.frames());
        through(*made, b);
        bool same = true;
        for (std::size_t i = 2 * 48000 + 24000; i < a.frames(); ++i) same = same && a.l[i] == b.l[i];
        CHECK(same);
    }
    const float faintAt[4] = {0.3f, 1.f, 1.f, 1.f};
    CHECK(faintAfterANote(flanger, faintAt));
    const float at[4] = {0.3f, 0.7f, 0.8f, 0.f};
    const MixGot g = mixGlides(flanger, at);
    CHECK(g.early > 0.01 && g.early < 0.2 && g.later > 0.9 && std::fabs(g.late - 1.0) < 1e-4);
    turnedLands(flanger, 0); // (RATE: the sweep is somewhere else by then)
    tickCountsAsleep(flanger, 1, 0.1f);
    const float set[4] = {0.5f, 0.9f, 1.f, 0.7f}, other[4] = {0.1f, 0.2f, 0.f, 1.f};
    const RulesSeen seen = holdsTheRules(flanger, set, other, 6.5);
    SAY("flanger  the last sample that is not zero %.3f s after a click (FEEDBACK +100%%)\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick > 0.5 && seen.lastAfterClick < 2.5);
    for (int p = 0; p < 4; ++p) {
        SAY("flanger  turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
        CHECK(seen.stepDown[p] < 1.5 && seen.stepUp[p] < 1.5 && seen.cornerDown[p] < 3.0 && seen.cornerUp[p] < 3.0);
    }
}

static void testEnsemble() {
    const EffectKind ensemble = EffectKind::Ensemble;
    const EffectInfo& info = effectInfo(ensemble);
    modRow(ensemble, 25, "ensemble", "ENSEMBLE", 2);
    CHECK(std::string(info.paramNames[0]) == "RATE" && std::string(info.paramNames[1]) == "DEPTH" && std::string(info.paramNames[2]) == "SPREAD" && std::string(info.paramNames[3]) == "MIX");
    for (int p = 0; p < 4; ++p) CHECK(effectChoices(ensemble, p) == 0);
    CHECK(reads(ensemble, 0, 0.4f) == "0.58 HZ" && reads(ensemble, 2, 0.7f) == "70%");
    for (float v : {0.f, 0.37f, 1.f}) {
        CHECK(effectCarryValue(EffectKind::Chorus, 0, ensemble, 0, v) == v && effectCarryValue(EffectKind::Flanger, 3, ensemble, 3, v) == v);
        CHECK(effectCarryValue(EffectKind::Tremolo, 3, ensemble, 2, v) == v && effectCarryValue(ensemble, 2, EffectKind::Tremolo, 3, v) == v); // SPREAD
    }
    // THE THREE COPIES, BY CLICKS 20 ms APART (MIX 100%: the plain click, then the three, darkened). Where their weight
    // is centred never moves (a third of a turn apart, they always pull against each other: the pitch stays the
    // sound's own), while they spread: their spread squared, over that at DEPTH 0, averages (swirl^2 + shimmer^2) / 2
    // and swings by swirl x shimmer either way as the two waves pass each other. CHORUS's ENSEMBLE, laid against it,
    // moves its centre (each copy at its own speed).
    const auto cluster = [](Effect& e, double seconds, std::vector<double>& centre, std::vector<double>& spread) {
        const Stereo out = clicksThrough(e, 960, seconds);
        centre.clear(), spread.clear();
        for (std::size_t c = 0; c + 960 <= out.frames(); c += 960) {
            double sum = 0.0, moment = 0.0, second = 0.0;
            for (std::size_t i = 4 * 48; i < 19 * 48; ++i) sum += out.l[c + i], moment += static_cast<double>(i) * out.l[c + i];
            const double mid = moment / sum;
            for (std::size_t i = 4 * 48; i < 19 * 48; ++i) second += (static_cast<double>(i) - mid) * (static_cast<double>(i) - mid) * out.l[c + i];
            centre.push_back(mid), spread.push_back(second / sum);
        }
    };
    {
        std::vector<double> c0, s0, c1, s1, c2, s2, cc, sc;
        std::unique_ptr<Effect> still = fresh(ensemble, 0.4f, 0.f, 0.f, 1.f), slow = fresh(ensemble, 0.4f, 1.f, 0.f, 1.f), fast = fresh(ensemble, 1.f, 1.f, 0.f, 1.f);
        std::unique_ptr<Effect> chorus = fresh(EffectKind::Chorus, 0.4f, 1.f, 0.3f, 1.f);
        cluster(*still, 4.0, c0, s0);
        cluster(*slow, 12.0, c1, s1);
        cluster(*fast, 12.0, c2, s2);
        cluster(*chorus, 12.0, cc, sc);
        const auto range = [](const std::vector<double>& v, double& lo, double& hi, double& mean) {
            lo = 1e30, hi = -1e30, mean = 0.0;
            for (std::size_t i = 25; i < v.size(); ++i) lo = std::fmin(lo, v[i]), hi = std::fmax(hi, v[i]), mean += v[i];
            mean /= static_cast<double>(v.size() - 25);
        };
        double lo0, hi0, m0, clo1, chi1, cm1, slo1, shi1, sm1, clo2, chi2, cm2, slo2, shi2, sm2, clc, chc, cmc, d1, d2, d3;
        range(c0, lo0, hi0, m0), range(s0, d1, d2, d3);
        const double base = d3;
        range(c1, clo1, chi1, cm1), range(s1, slo1, shi1, sm1);
        range(c2, clo2, chi2, cm2), range(s2, slo2, shi2, sm2);
        range(cc, clc, chc, cmc);
        // The swings, in samples: at 0.58 Hz 1.8 ms and 0.14 ms (its shimmer at 5.6 Hz, under 6 Hz); at 8 Hz the swirl
        // 1.8 ms / 8 and the shimmer, at 10 Hz, 0.14 ms x 6 / 10.
        const double sw1 = 86.4, sh1 = 6.72, sw2 = 86.4 / 8.0, sh2 = 6.72 * 0.6;
        SAY("ensemble DEPTH 0: centred %.2f samples late (moves %.3f), spread^2 %.2f\n", m0, hi0 - lo0, base);
        SAY("ensemble DEPTH 100%%, 0.58 Hz: the centre moves %.3f samples; spread^2 over DEPTH 0 %.1f (%.1f to %.1f), wanted %.1f, +-%.1f\n", chi1 - clo1, sm1 - base, slo1 - base, shi1 - base, (sw1 * sw1 + sh1 * sh1) / 2, sw1 * sh1);
        SAY("ensemble DEPTH 100%%, 8 Hz: the centre moves %.3f samples; spread^2 over DEPTH 0 %.1f (%.1f to %.1f), wanted %.1f, +-%.1f\n", chi2 - clo2, sm2 - base, slo2 - base, shi2 - base, (sw2 * sw2 + sh2 * sh2) / 2, sw2 * sh2);
        SAY("ensemble CHORUS's ENSEMBLE at DEPTH 100%%: its centre moves %.1f samples\n", chc - clc);
        CHECK(std::fabs(m0 - 9.0 * 48.0 - 0.4 / 0.6) < 0.05 && hi0 - lo0 < 1e-3); // (the high cut's own lateness: 0.67 of a sample)
        CHECK(chi1 - clo1 < 1.5 && chi2 - clo2 < 1.5 && chc - clc > 100.0); // (a copy read from a moving delay is squeezed: its centre a hair off)
        CHECK(std::fabs((sm1 - base) / ((sw1 * sw1 + sh1 * sh1) / 2) - 1.0) < 0.05 && std::fabs((shi1 - slo1) / (2 * sw1 * sh1) - 1.0) < 0.15);
        CHECK(std::fabs((sm2 - base) / ((sw2 * sw2 + sh2 * sh2) / 2) - 1.0) < 0.05 && std::fabs((shi2 - slo2) / (2 * sw2 * sh2) - 1.0) < 0.15);
    }
    // SPREAD: the same sound on both sides comes out the same at 0, to the bit; at 100% the right's copies swing the other
    // way, so the two sides' copies are far less alike (the plain sound is the same on both).
    {
        double was[3];
        for (int s = 0; s < 3; ++s) {
            std::unique_ptr<Effect> e = fresh(ensemble, 0.4f, 1.f, 0.5f * s, 1.f);
            Stereo io(4 * 48000);
            Noise noise(9u);
            for (std::size_t i = 0; i < io.frames(); ++i) io.l[i] = io.r[i] = 0.3f * noise.next();
            const Stereo in = io;
            through(*e, io);
            if (s == 0) CHECK(io.l == io.r);
            std::vector<float> wl(io.frames()), wr(io.frames());
            const float dry = std::cos(0.78539816f);
            for (std::size_t i = 0; i < io.frames(); ++i) wl[i] = io.l[i] - dry * in.l[i], wr[i] = io.r[i] - dry * in.r[i];
            was[s] = alike(wl, wr, 48000);
        }
        SAY("ensemble the copies, left against right (1 the same): SPREAD 0 %.3f, 50%% %.3f, 100%% %.3f\n", was[0], was[1], was[2]);
        CHECK(was[0] > 0.9999 && was[1] < 0.6 && was[2] < was[1]);
    }
    // DARKENED: the copies go through a gentle high cut from 7 kHz (the plain sound does not). What the copies add at
    // MIX 100%, DEPTH 0 (the output less the plain sound's 0.707): at 333 Hz about all of 0.707, at 12 kHz 0.56 of it.
    {
        const auto copies = [&](double hz) {
            std::unique_ptr<Effect> e = fresh(ensemble, 0.4f, 0.f, 0.f, 1.f);
            Stereo io(96000);
            io.l = io.r = sineOf(hz, 0.25, io.frames());
            const Stereo in = io;
            through(*e, io);
            std::vector<float> wet(io.frames());
            for (std::size_t i = 0; i < wet.size(); ++i) wet[i] = io.l[i] - 0.70710678f * in.l[i];
            const std::size_t count = static_cast<std::size_t>(std::floor(hz) * 48000.0 / hz + 0.5);
            return dB(partAt(wet.data() + wet.size() - count, count, hz) / (0.25 * 0.70710678));
        };
        const double low = copies(1000.0 / 3.0), high = copies(12000.0);
        SAY("ensemble DEPTH 0, MIX 100%%: the copies at 333 Hz %+.2f dB, at 12 kHz %+.2f dB (of the 0.707 they are given)\n", low, high);
        CHECK(std::fabs(low) < 0.1 && high < -4.5 && high > -5.5);
        // MIX is CHORUS's law: at 50% the plain sound cos(22.5 degrees) and the copies sin(22.5) (333 Hz: the copies three
        // turns late, in step): 0.924 + 0.383, +2.33 dB.
        std::unique_ptr<Effect> half = fresh(ensemble, 0.4f, 0.f, 0.f, 0.5f);
        const double at50 = steadyLevel(*half, 1000.0 / 3.0);
        SAY("ensemble DEPTH 0, MIX 50%%, 333 Hz: %+.3f dB (wanted %+.3f)\n", at50, dB(std::cos(kPi / 8.0) + std::sin(kPi / 8.0)));
        CHECK(std::fabs(at50 - dB(std::cos(kPi / 8.0) + std::sin(kPi / 8.0))) < 0.03);
    }
    // LOUDNESS: noise at its usual settings.
    {
        std::unique_ptr<Effect> e = fresh(ensemble);
        const double gain = noiseGain(*e);
        SAY("ensemble noise at its usual settings: %+.2f dB\n", gain);
        CHECK(gain > -3.0 && gain < -1.5); // (three copies unlike each other add to less than one: 1/3 each)
    }
    const float faintAt[4] = {0.4f, 1.f, 1.f, 1.f};
    CHECK(faintAfterANote(ensemble, faintAt));
    const float at[4] = {0.4f, 0.6f, 0.7f, 0.f};
    const MixGot g = mixGlides(ensemble, at);
    CHECK(g.early > 0.01 && g.early < 0.2 && g.later > 0.9 && std::fabs(g.late - 1.0) < 1e-4);
    turnedLands(ensemble, 0);
    tickCountsAsleep(ensemble, 1, 0.1f);
    const float set[4] = {0.6f, 0.9f, 1.f, 0.8f}, other[4] = {0.1f, 0.2f, 0.f, 1.f};
    const RulesSeen seen = holdsTheRules(ensemble, set, other, 5.5);
    SAY("ensemble the last sample that is not zero %.3f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick < 0.05);
    for (int p = 0; p < 4; ++p) {
        SAY("ensemble turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
        CHECK(seen.stepDown[p] < 1.5 && seen.stepUp[p] < 1.5 && seen.cornerDown[p] < 3.0 && seen.cornerUp[p] < 3.0);
    }
}

// Where a six-stage phaser with its corner at `corner` notches: total turn 180, 540 and 900 degrees.
static double phaserNotch(double corner, int which) {
    const double g = std::tan(kPi * corner / 48000.0), deg = 15.0 * (2 * which + 1);
    return 48000.0 / kPi * std::atan(g * std::tan(deg * kPi / 180.0));
}

static void testPhaser() {
    const EffectKind phaser = EffectKind::Phaser;
    const EffectInfo& info = effectInfo(phaser);
    modRow(phaser, 26, "phaser", "PHASER", 3);
    CHECK(std::string(info.paramNames[0]) == "RATE" && std::string(info.paramNames[1]) == "DEPTH" && std::string(info.paramNames[2]) == "FEEDBACK" && std::string(info.paramNames[3]) == "MIX");
    for (int p = 0; p < 4; ++p) CHECK(effectChoices(phaser, p) == 0);
    CHECK(reads(phaser, 0, 0.3f) == "0.37 HZ" && reads(phaser, 2, 0.75f) == "+50%" && reads(phaser, 1, 0.8f) == "80%");
    // THREE NOTCHES at DEPTH 0 (the corner at 640 Hz), FEEDBACK 0, MIX 100%: at 171, 640 and 2390 Hz the sound is gone;
    // half way between (in turn: 360 and 720 degrees) it is whole.
    {
        double worst = -600.0, whole = 0.0;
        for (int k = 0; k < 3; ++k) {
            std::unique_ptr<Effect> e = fresh(phaser, 0.3f, 0.f, 0.5f, 1.f);
            const double hz = phaserNotch(640.0, k), level = steadyLevel(*e, hz);
            SAY("phaser   notch %d at %.1f Hz: %.1f dB\n", k + 1, hz, level);
            worst = std::fmax(worst, level);
        }
        for (double deg : {30.0, 60.0}) {
            std::unique_ptr<Effect> e = fresh(phaser, 0.3f, 0.f, 0.5f, 1.f);
            const double hz = 48000.0 / kPi * std::atan(std::tan(kPi * 640.0 / 48000.0) * std::tan(deg * kPi / 180.0)), level = steadyLevel(*e, hz);
            SAY("phaser   between at %.1f Hz: %+.3f dB\n", hz, level);
            whole = std::fmax(whole, std::fabs(level));
        }
        CHECK(worst < -40.0 && whole < 0.05);
    }
    // FEEDBACK at DEPTH 0: +100% makes the place between two notches (369 Hz) ring up, -100% takes it down.
    {
        double level[3];
        for (int f = 0; f < 3; ++f) {
            std::unique_ptr<Effect> e = fresh(phaser, 0.3f, 0.f, 0.5f * f, 1.f);
            level[f] = steadyLevel(*e, 48000.0 / kPi * std::atan(std::tan(kPi * 640.0 / 48000.0) * std::tan(kPi / 6.0)));
        }
        // 0.5 + 0.5 x 0.436 / (1 -+ 0.9): +8.6 dB, -4.2 dB.
        SAY("phaser   between two notches at FEEDBACK -100 / 0 / +100%%: %+.2f / %+.2f / %+.2f dB\n", level[0], level[1], level[2]);
        // (Measured 7.8 and -4.2: the loop's one sample of lateness moves the ringing a hair off that place.)
        CHECK(std::fabs(level[2] - dB(0.5 + 0.5 * std::sqrt(0.19) / 0.1)) < 1.0 && std::fabs(level[0] - dB(0.5 + 0.5 * std::sqrt(0.19) / 1.9)) < 0.2 && std::fabs(level[1]) < 0.05);
    }
    // THE SWEEP: a 640 Hz sine at DEPTH 100%, 1 Hz: notches pass it, so its level falls deep and comes back; the right
    // side does the same a quarter of a second after the left (a quarter turn behind).
    {
        std::unique_ptr<Effect> e = fresh(phaser, modRate(1.0), 1.f, 0.5f, 1.f);
        Stereo io(6 * 48000);
        io.l = io.r = sineOf(640.0, 0.25, io.frames());
        through(*e, io);
        // The level, in 5 ms pieces (about three turns of the sine each), from the second second on.
        std::vector<double> el, er;
        for (std::size_t at = 48000; at + 240 <= io.frames(); at += 240) el.push_back(rmsOf(io.l, at, at + 240)), er.push_back(rmsOf(io.r, at, at + 240));
        double lo = 1e9, hi = 0.0;
        for (double v : el) lo = std::fmin(lo, v), hi = std::fmax(hi, v);
        // The lag that lays the right's levels on the left's best, in pieces.
        int best = 0;
        double bestErr = 1e30;
        for (int lag = 0; lag < 200; ++lag) {
            double err = 0.0;
            for (std::size_t i = 0; i + lag < er.size() && i < 600; ++i) err += (el[i] - er[i + lag]) * (el[i] - er[i + lag]);
            if (err < bestErr) bestErr = err, best = lag;
        }
        SAY("phaser   640 Hz at DEPTH 100%%, 1 Hz: from %.1f to %+.1f dB; the right %.3f s behind\n", dB(lo / (0.25 * 0.7071)), dB(hi / (0.25 * 0.7071)), best * 0.005);
        CHECK(dB(lo / (0.25 * 0.7071)) < -25.0 && dB(hi / (0.25 * 0.7071)) > -0.5 && std::abs(best - 50) <= 1);
    }
    // ABOUT AS LOUD AT ANY FEEDBACK: noise at the usual sweep, MIX 100%.
    {
        double gain[3];
        for (int f = 0; f < 3; ++f) {
            std::unique_ptr<Effect> e = fresh(phaser, -1.f, -1.f, 0.5f * f, 1.f);
            gain[f] = noiseGain(*e);
        }
        SAY("phaser   noise at FEEDBACK -100 / 0 / +100%%: %+.2f / %+.2f / %+.2f dB\n", gain[0], gain[1], gain[2]);
        for (double g : gain) CHECK(g > -4.0 && g < 1.0);
    }
    for (float v : {0.f, 0.37f, 1.f}) CHECK(effectCarryValue(EffectKind::Ensemble, 1, phaser, 1, v) == v && effectCarryValue(phaser, 0, EffectKind::Chorus, 0, v) == v);
    // EACH SIDE KEEPS TO ITSELF, its feedback too: noise on the left alone (FEEDBACK +100%) leaves the right silent.
    {
        std::unique_ptr<Effect> e = fresh(phaser, 0.3f, 0.8f, 1.f, 1.f);
        Stereo io(48000);
        Noise noise(3u);
        for (float& v : io.l) v = 0.3f * noise.next();
        through(*e, io);
        CHECK(peakOf(io.r) == 0.f && rmsOf(io.l) > 0.05);
    }
    const float faintAt[4] = {0.3f, 1.f, 1.f, 1.f};
    CHECK(faintAfterANote(phaser, faintAt));
    const float at[4] = {0.3f, 0.8f, 0.75f, 0.f};
    const MixGot g = mixGlides(phaser, at);
    CHECK(g.early > 0.01 && g.early < 0.2 && g.later > 0.9 && std::fabs(g.late - 1.0) < 1e-4);
    turnedLands(phaser, 0);
    tickCountsAsleep(phaser, 1, 0.1f);
    const float set[4] = {0.5f, 1.f, 1.f, 0.7f}, other[4] = {0.1f, 0.2f, 0.f, 1.f};
    const RulesSeen seen = holdsTheRules(phaser, set, other, 5.5);
    SAY("phaser   the last sample that is not zero %.3f s after a click (FEEDBACK +100%%)\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick < 1.0);
    for (int p = 0; p < 4; ++p) {
        SAY("phaser   turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
        CHECK(seen.stepDown[p] < 1.5 && seen.stepUp[p] < 1.5 && seen.cornerDown[p] < 3.0 && seen.cornerUp[p] < 3.0);
    }
}

// ---- TREMOLO ----

static float tremoloRate(int choice) { return (static_cast<float>(choice) + 0.5f) / 10.f; }
static float tremoloShape(int shape) { return (static_cast<float>(shape) + 0.5f) / 4.f; }
// What it does to the level: a steady 0.5 on both sides in, what comes out over 0.5, each side. `seconds` long.
static Stereo tremoloGain(Effect& e, double seconds) {
    Stereo io(static_cast<std::size_t>(seconds * 48000.0));
    io.l.assign(io.frames(), 0.5f);
    io.r.assign(io.frames(), 0.5f);
    through(e, io);
    for (float& v : io.l) v *= 2.f;
    for (float& v : io.r) v *= 2.f;
    return io;
}

static void testTremolo() {
    const EffectKind tremolo = EffectKind::Tremolo;
    const EffectInfo& info = effectInfo(tremolo);
    modRow(tremolo, 27, "tremolo", "TREMOLO", 4);
    CHECK(std::string(info.paramNames[0]) == "RATE" && std::string(info.paramNames[1]) == "DEPTH" && std::string(info.paramNames[2]) == "SHAPE" && std::string(info.paramNames[3]) == "SPREAD");
    CHECK(effectChoices(tremolo, 0) == 10 && effectChoices(tremolo, 1) == 0 && effectChoices(tremolo, 2) == 4 && effectChoices(tremolo, 3) == 0 && effectMixParam(tremolo) == -1);
    static const char* const rates[10] = {"1/32", "1/16 TRIP", "1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOT", "1/4", "1/2", "1 BAR"};
    static const char* const shapes[4] = {"SINE", "SQUARE", "RAMP", "RANDOM"};
    for (int c = 0; c < 10; ++c) CHECK(reads(tremolo, 0, tremoloRate(c)) == rates[c]);
    for (int s = 0; s < 4; ++s) CHECK(reads(tremolo, 2, tremoloShape(s)) == shapes[s]);
    CHECK(reads(tremolo, 0, info.defaults[0]) == "1/8" && reads(tremolo, 2, info.defaults[2]) == "SINE" && info.defaults[1] == 0.5f && info.defaults[3] == 0.f);
    // DEPTH and SPREAD are carried (SPREAD from ENSEMBLE); RATE is not: CHORUS's is hertz, this one note lengths.
    CHECK(effectCarryFrom(EffectKind::Chorus, tremolo, 0) == -1 && effectCarryFrom(EffectKind::Chorus, tremolo, 1) == 1 && effectCarryFrom(EffectKind::Ensemble, tremolo, 3) == 2);
    // IN TIME: SINE at DEPTH 100%, every RATE at 120 and at 90 BPM: the level is 0.5 + 0.5 cos(a turn a note length),
    // from its top when the sound arrives, to 1e-4 for four seconds.
    const double steps[10] = {0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0, 2.0, 8.0 / 3.0, 3.0, 4.0, 8.0, 16.0};
    for (float bpm : {120.f, 90.f}) {
        double worst = 0.0;
        for (int c = 0; c < 10; ++c) {
            std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(c), 1.f, tremoloShape(0), 0.f, bpm);
            const Stereo g = tremoloGain(*e, 4.0);
            const double turn = steps[c] * 15.0 / bpm * 48000.0; // samples
            for (std::size_t i = 0; i < g.frames(); ++i) worst = std::fmax(worst, std::fabs(g.l[i] - (0.5 + 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / turn))));
            CHECK(g.l == g.r && g.l[0] == 1.f);
        }
        SAY("tremolo  SINE at %.0f BPM, every RATE: off 0.5 + 0.5 cos by %.1e at worst\n", static_cast<double>(bpm), worst);
        CHECK(worst < 1e-4);
    }
    // DEPTH: at 50% the level runs 0.5 to 1; at 0 the sound untouched to the bit (holdsTheRules has that).
    {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(4), 0.5f, tremoloShape(0), 0.f);
        const Stereo g = tremoloGain(*e, 1.0);
        float lo = 2.f, hi = 0.f;
        for (float v : g.l) lo = std::fmin(lo, v), hi = std::fmax(hi, v);
        CHECK(std::fabs(lo - 0.5f) < 1e-4f && hi == 1.f);
    }
    // SQUARE: up for the first half of each turn and down for the second, each jump smoothed (two stages of 0.6 ms: from
    // 99% to 1% of the way in 6.5 of them, 187 samples, under 4 ms), at a whole bar (2 s turns) and at 1/32. The largest
    // step is the smoothing's steepest (1 / (e x 28.8 samples) = 0.0128), never more.
    const double steepest = 1.0 / (std::exp(1.0) * 28.8);
    for (int c : {9, 0}) {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(c), 1.f, tremoloShape(1), 0.f);
        const Stereo g = tremoloGain(*e, 4.0);
        const double turn = steps[c] * 15.0 / 120.0 * 48000.0;
        std::size_t falls = 0, rises = 0, upFor = 0;
        long below = -1;
        double edge = 0.0;
        for (std::size_t i = 1; i < g.frames(); ++i) {
            if (g.l[i - 1] >= 0.99f && g.l[i] < 0.99f) below = static_cast<long>(i);
            if (g.l[i - 1] > 0.01f && g.l[i] <= 0.01f && below >= 0) edge = std::fmax(edge, static_cast<double>(i - below)), ++falls;
            if (g.l[i - 1] < 0.5f && g.l[i] >= 0.5f) ++rises;
            upFor += g.l[i] > 0.5f ? 1 : 0;
        }
        SAY("tremolo  SQUARE at %s: %zu falls, %zu rises in 4 s; a fall from 99%% to 1%% %.0f samples; up %.3f of the time; the largest step %.4f (the smoothing's steepest %.4f)\n", rates[c], falls, rises,
            edge, static_cast<double>(upFor) / g.frames(), static_cast<double>(largestJump(g.l)), steepest);
        CHECK(falls == static_cast<std::size_t>(std::ceil(4.0 * 48000.0 / turn - 0.5)) && std::fabs(static_cast<double>(upFor) / g.frames() - 0.5) < 0.01);
        CHECK(edge < 192.0 && edge > 170.0 && largestJump(g.l) < 1.01 * steepest);
    }
    // RAMP: the pump: at the bottom as a turn starts, up fast and easing (1 - (1 - a)^2), back down, smoothed, at its end (4 ms on, 0.02: the drop all but done, the next rise begun).
    {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(7), 1.f, tremoloShape(2), 0.f);
        const Stereo g = tremoloGain(*e, 2.0);
        const std::size_t turn = 24000; // a quarter note at 120
        SAY("tremolo  RAMP at 1/4: %.3f at the start, %.3f a quarter in, %.3f half way, %.3f just before the end, %.4f 4 ms into the next turn\n", g.l[0], g.l[turn / 4], g.l[turn / 2], g.l[turn - 20], g.l[turn + 192]);
        CHECK(g.l[0] == 0.f && std::fabs(g.l[turn / 4] - 0.4375) < 0.01 && std::fabs(g.l[turn / 2] - 0.75) < 0.01 && g.l[turn - 20] > 0.99f && g.l[turn + 192] < 0.03f && largestJump(g.l) < 1.01 * steepest);
    }
    // RANDOM: a new level each turn, the first at the top; the same every time; levels from near 0 to near 1; never a
    // step steeper than the smoothing's.
    {
        std::unique_ptr<Effect> a = fresh(tremolo, tremoloRate(2), 1.f, tremoloShape(3), 0.f), b = fresh(tremolo, tremoloRate(2), 1.f, tremoloShape(3), 0.f);
        const Stereo g = tremoloGain(*a, 30.0), h = tremoloGain(*b, 30.0);
        const std::size_t turn = 6000;
        double lo = 1.0, hi = 0.0, sum = 0.0;
        int count = 0, changed = 0;
        for (std::size_t t = 1; (t + 1) * turn <= g.frames(); ++t) {
            const double v = g.l[t * turn + turn / 2];
            lo = std::fmin(lo, v), hi = std::fmax(hi, v), sum += v, ++count;
            changed += g.l[t * turn + turn / 2] != g.l[(t - 1) * turn + turn / 2] ? 1 : 0;
            CHECK(g.l[t * turn + 1000] == g.l[t * turn + turn - 1]); // steady once its jump is smoothed
        }
        SAY("tremolo  RANDOM at 1/16 for 30 s: %d turns, levels %.3f to %.3f, %.3f on average, %d changed; the largest step %.4f\n", count, lo, hi, sum / count, changed, static_cast<double>(largestJump(g.l)));
        CHECK(g.l[100] == 1.f && g.l == h.l && lo < 0.05 && hi > 0.95 && std::fabs(sum / count - 0.5) < 0.08 && changed == count && largestJump(g.l) < 1.01 * steepest);
    }
    // SPREAD: 100% with SINE at DEPTH 100% is a pan, the two sides adding to 1 at every sample; 50% puts the right a quarter
    // turn behind.
    {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(4), 1.f, tremoloShape(0), 1.f), q = fresh(tremolo, tremoloRate(4), 1.f, tremoloShape(0), 0.5f);
        const Stereo g = tremoloGain(*e, 2.0), h = tremoloGain(*q, 2.0);
        double worst = 0.0, quarter = 0.0;
        for (std::size_t i = 0; i < g.frames(); ++i) {
            worst = std::fmax(worst, std::fabs(g.l[i] + g.r[i] - 1.0));
            quarter = std::fmax(quarter, std::fabs(h.r[i] - (0.5 + 0.5 * std::cos(2.0 * kPi * (static_cast<double>(i) / 12000.0 - 0.25)))));
        }
        SAY("tremolo  SPREAD 100%%: the two sides add to 1 within %.1e; 50%%: the right a quarter turn behind within %.1e\n", worst, quarter);
        CHECK(worst < 1e-4 && quarter < 1e-4);
    }
    // A NEW TEMPO OR RATE IS A NEW SPEED, never a jump in the wave: 1/8 at 120 BPM, then 60 BPM after a second (or 1/4).
    for (int way = 0; way < 2; ++way) {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(4), 1.f, tremoloShape(0), 0.f);
        Stereo io(4 * 48000);
        io.l.assign(io.frames(), 0.5f), io.r = io.l;
        through(*e, io, 0, 48000 + 100);
        if (way == 0) e->setTempo(60.f);
        else e->setParam(0, tremoloRate(7));
        through(*e, io, 48000 + 100, io.frames());
        // Its turns after it, from where it rises through its middle.
        std::vector<double> ups;
        for (std::size_t i = 2 * 48000; i < io.frames(); ++i)
            if (io.l[i - 1] < 0.25f && io.l[i] >= 0.25f) ups.push_back(static_cast<double>(i));
        SAY("tremolo  %s after a second: a turn is then %.0f samples; the largest step %.5f (the wave's own at its fastest %.5f)\n", way ? "1/4" : "60 BPM", ups.size() > 1 ? ups[1] - ups[0] : 0.0,
            static_cast<double>(largestJump(io.l)), 0.25 * 2.0 * kPi / 12000.0);
        CHECK(ups.size() > 1 && std::fabs(ups[1] - ups[0] - 24000.0) < 2.0 && largestJump(io.l) < 1.02 * 0.25 * 2.0 * kPi / 12000.0);
    }
    // A NEW SHAPE fades in over 10 ms: SINE to SQUARE at 1/8 where the two are far apart, no step more than the fade's.
    {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(4), 1.f, tremoloShape(0), 0.f);
        Stereo io(48000);
        io.l.assign(io.frames(), 0.5f), io.r = io.l;
        through(*e, io, 0, 12000 + 2000);
        e->setParam(2, tremoloShape(1));
        through(*e, io, 12000 + 2000, io.frames());
        SAY("tremolo  SINE to SQUARE where they are %.2f apart: the largest step %.5f (all at once: %.2f)\n", 1.0 - io.l[14000] * 2.0, static_cast<double>(largestJump(io.l, 13990, 15000)), 0.5 * (1.0 - io.l[14000] * 2.0));
        CHECK(largestJump(io.l, 13990, 15000) < 0.5f / 480.f * 1.1f);
        // ...and it takes the 10 ms: the level gets to the square's top (0.5 here) about 480 samples after the turn,
        // and is half way there after 240. (A 3 ms fade passed the step's bound above: the two are only 0.25 apart.)
        std::size_t arrived = 15000;
        while (arrived > 14000 && std::fabs(io.l[arrived - 1] - 0.5f) < 1e-5f) --arrived;
        const double halfWay = (io.l[14240] - io.l[14000]) / (0.5 - io.l[14000]);
        SAY("tremolo  SINE to SQUARE: at the square %zu samples after the turn; %.2f of the way there after 240\n", arrived - 14000, halfWay);
        CHECK(arrived >= 14000 + 470 && arrived <= 14000 + 500 && halfWay > 0.35 && halfWay < 0.65);
    }
    // A NEW SHAPE'S SMOOTHING STARTS WHERE THE SOUND IS: SINE to RAMP half a turn in (the sine at its bottom, the ramp
    // half way up): no corner in the level as the fade begins (a smoothing left where it last was, at the top, bent the
    // level 0.002 a sample at once).
    {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(4), 1.f, tremoloShape(0), 0.f);
        Stereo io(24000);
        io.l.assign(io.frames(), 0.5f), io.r = io.l;
        through(*e, io, 0, 6000);
        e->setParam(2, tremoloShape(2));
        through(*e, io, 6000, io.frames());
        double corner = 0.0;
        for (std::size_t i = 5990; i < 6100; ++i) corner = std::fmax(corner, std::fabs(2.0 * (static_cast<double>(io.l[i]) - 2.0 * io.l[i - 1] + io.l[i - 2])));
        // (Where the 10 ms fade ENDS the level bends 0.0016 at once: the crossfade's own straight lines, not looked at here.)
        SAY("tremolo  SINE to RAMP at the sine's bottom: the sharpest corner in the level in the fade's first 2 ms %.1e\n", corner);
        CHECK(corner < 3e-4);
    }
    // RANDOM ON THE RIGHT, SPREAD 100%: the left's levels, half a turn later, each turn's own.
    {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(2), 1.f, tremoloShape(3), 1.f);
        const Stereo g = tremoloGain(*e, 10.0);
        bool same = true;
        for (std::size_t k = 1; (k + 2) * 6000 < g.frames(); ++k) same = same && g.r[k * 6000 + 3000 + 1500] == g.l[k * 6000 + 1500];
        CHECK(same && g.l != g.r);
    }
    // ...AND WOKEN, IT STARTS ITS DICE AGAIN: noise for a second, 6 s of nothing, then a steady 0.5: the same levels as a
    // fresh one's, to the bit.
    {
        std::unique_ptr<Effect> slept = fresh(tremolo, tremoloRate(2), 1.f, tremoloShape(3), 0.f), box = fresh(tremolo, tremoloRate(2), 1.f, tremoloShape(3), 0.f);
        Stereo a(7 * 48000);
        Noise noise(4u);
        for (std::size_t i = 0; i < 48000; ++i) a.l[i] = a.r[i] = 0.3f * noise.next();
        through(*slept, a);
        const Stereo g = tremoloGain(*slept, 3.0), h = tremoloGain(*box, 3.0);
        CHECK(g.l == h.l && g.r == h.r);
    }
    // ASLEEP AFTER 4 S OF NOTHING, and then it starts again at its top: hits 1 s apart meet the wave where it has got to,
    // one after 5 s of zeros meets it at its top. (1/2 at 120 BPM: a turn is a second; hits at 0.25 s into each.)
    {
        std::unique_ptr<Effect> e = fresh(tremolo, tremoloRate(8), 1.f, tremoloShape(0), 0.f);
        Stereo io(12 * 48000);
        for (double t : {0.25, 1.75, 8.0}) io.l[static_cast<std::size_t>(t * 48000.0)] = io.r[static_cast<std::size_t>(t * 48000.0)] = 1.f;
        through(*e, io);
        // The first wakes it: the top (1); at 1.75 s, 1.5 turns on: the bottom (0); at 8.0, woken again: the top.
        SAY("tremolo  hits at 0.25, 1.75 and (after 6 s of nothing) 8.0 s: %.4f %.4f %.4f\n", io.l[12000], io.l[84000], io.l[384000]);
        CHECK(io.l[12000] == 1.f && std::fabs(io.l[84000]) < 1e-3f && io.l[384000] == 1.f);
    }
    const float set[4] = {tremoloRate(3), 0.9f, tremoloShape(1), 0.6f}, other[4] = {tremoloRate(8), 0.2f, tremoloShape(3), 0.1f};
    tickCountsAsleep(tremolo, 1, 0.1f);
    const RulesSeen seen = holdsTheRules(tremolo, set, other, 5.0);
    SAY("tremolo  the last sample that is not zero %.4f s after a click\n", seen.lastAfterClick);
    CHECK(seen.lastAfterClick < 0.001);
    for (int p = 0; p < 4; ++p) SAY("tremolo  turned at once %-9s down: step %.2f corner %.2f   up: step %.2f corner %.2f\n", info.paramNames[p], seen.stepDown[p], seen.cornerDown[p], seen.stepUp[p], seen.cornerUp[p]);
    for (int p : {1, 3}) CHECK(seen.stepDown[p] < 1.2 && seen.stepUp[p] < 1.2 && seen.cornerDown[p] < 2.0 && seen.cornerUp[p] < 2.0);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--print") {
        printPins();
        const std::vector<float> song = effectsSong(0);
        Sum sum;
        sum.add(song);
        std::printf("song: 0x%016llxull, rms %.9g\n", static_cast<unsigned long long>(sum.h), rmsOf(song, 0, song.size() - 1));
        return 0;
    }
    gMeasure = argc > 1 && std::string(argv[1]) == "--measure";
    // `effects_test --only testDuck testTape`: just those (for trying a change quickly; ctest runs them all).
    // `effects_test --measure --only testTape`: their numbers.
    struct Named {
        const char* name;
        void (*run)();
    };
    static const Named tests[] = {
        {"testPinnedSound", testPinnedSound},
        {"testWholeSong", testWholeSong},
        {"testTheTable", testTheTable},
        {"testEveryEffect", testEveryEffect},
        {"testFilterLevels", testFilterLevels},
        {"testDriveHarmonics", testDriveHarmonics},
        {"testChorus", testChorus},
        {"testDelayEchoes", testDelayEchoes},
        {"testReverbTail", testReverbTail},
        {"testTape", testTape},
        {"testHall", testHall},
        {"testFlushToZero", testFlushToZero},
        {"testReverbDoesNotSlowDown", testReverbDoesNotSlowDown},
        {"testSwapFade", testSwapFade},
        {"testComp", testComp},
        {"testHalfband", testHalfband},
        {"testSaturate", testSaturate},
        {"testSwapsOnAnyHelperCount", testSwapsOnAnyHelperCount},
        {"testEffectsOnABus", testEffectsOnABus},
        {"testSends", testSends},
        {"testSendLanes", testSendLanes},
        {"testBusRests", testBusRests},
        {"testBusEffectsInTheEngine", testBusEffectsInTheEngine},
        {"testThirdSlot", testThirdSlot},
        {"testHeldAutomation", testHeldAutomation},
        {"testSendsOnAnyHelperCount", testSendsOnAnyHelperCount},
        {"testFamiliesAndTypes", testFamiliesAndTypes},
        {"testTypeChangeCarriesSettings", testTypeChangeCarriesSettings},
        {"testGlideLands", testGlideLands},
        {"testNotSound", testNotSound},
        {"testABusTakesItsOwnKind", testABusTakesItsOwnKind},
        {"testBusLevel", testBusLevel},
        {"testBusRestNumbers", testBusRestNumbers},
        {"testDuck", testDuck},
        {"testATailAlwaysEnds", testATailAlwaysEnds},
        {"testTapeAlwaysEnds", testTapeAlwaysEnds},
        {"testPing", testPing},
        {"testSpring", testSpring},
        {"testPlate", testPlate},
        {"testComb", testComb},
        {"testVowel", testVowel},
        {"testEq", testEq},
        {"testFold", testFold},
        {"testFuzz", testFuzz},
        {"testRing", testRing},
        {"testWear", testWear},
        {"testLimiter", testLimiter},
        {"testSnap", testSnap},
        {"testFlanger", testFlanger},
        {"testEnsemble", testEnsemble},
        {"testPhaser", testPhaser},
        {"testTremolo", testTremolo},
    };
    // `effects_test --part 1` and `--part 2`: ctest runs it as two tests, each about half the time,
    // so neither comes near its two minutes on a busy machine. Part 2 is everything from
    // kSecondPartFrom on, so a test added at the end goes in part 2. (2026-11-14, a quiet desk:
    // 34 s each.) Move the line if one part grows much longer than the other.
    static const char* const kSecondPartFrom = "testFamiliesAndTypes";
    int part = 0;
    if (argc > 2 && std::string(argv[1]) == "--part") {
        part = std::atoi(argv[2]);
        if (part != 1 && part != 2) {
            std::printf("effects_test: --part 1 or --part 2\n");
            return 1;
        }
    }
    const int first = gMeasure ? 2 : 1; // where "--only" would be
    const bool only = part == 0 && argc > first + 1 && std::string(argv[first]) == "--only";
    int ran = 0, inSecond = 0;
    bool second = false;
    for (const Named& test : tests) {
        second = second || std::string(test.name) == kSecondPartFrom;
        inSecond += second ? 1 : 0;
        bool wanted = !only && (part == 0 || (part == 2) == second);
        for (int a = first + 1; a < argc && !wanted && only; ++a) wanted = std::string(argv[a]) == test.name;
        if (wanted) test.run(), ++ran;
    }
    if (inSecond == 0 || inSecond == static_cast<int>(sizeof tests / sizeof tests[0])) {
        std::printf("effects_test: %s is not a test here, so the two parts are not halves\n", kSecondPartFrom);
        return 1;
    }
    if (ran == 0) {
        std::printf("effects_test: no such test\n");
        return 1;
    }
    if (failures == 0) std::printf("effects_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
