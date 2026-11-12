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
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "apotheca/denormals.hpp"
#include "apotheca/engine.hpp"

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
static void through(Effect& effect, Stereo& io, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
    if (to > io.frames()) to = io.frames();
    for (std::size_t at = from; at < to; at += kMaxBlock) {
        const std::size_t n = to - at < static_cast<std::size_t>(kMaxBlock) ? to - at : static_cast<std::size_t>(kMaxBlock);
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
static std::vector<float> effectsSong(int helpers) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->useWorkers(helpers);
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
    CHECK(effectFromWord("tape") == EffectKind::None && effectFromWord("filte") == EffectKind::None && effectFromWord("filters") == EffectKind::None);
    // Every effect: a word of its own, one word, lower case; in a group; in the list once.
    int built = 0;
    for (int k = 1; k < kEffectKinds; ++k) built += effectExists(static_cast<EffectKind>(k)) ? 1 : 0;
    CHECK(effectListSize() == built + 1 && effectAt(0) == EffectKind::None && effectPlace(EffectKind::None) == 0);
    CHECK(!effectExists(EffectKind::None) && !effectExists(EffectKind::Count) && effectExists(EffectKind::Filter));
    CHECK(effectAt(-1) == EffectKind::None && effectAt(effectListSize()) == EffectKind::None && effectPlace(EffectKind::Count) == 0 && effectPlace(static_cast<EffectKind>(-3)) == 0);
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        if (!effectExists(kind)) {
            // A number kept for an effect that is not built yet: it is nothing, everywhere.
            CHECK(std::string(info.name) == "NONE" && effectPlace(kind) == 0 && makeEffect(kind, 48000.f, 120.f) == nullptr && effectChoices(kind, 0) == 0);
            continue;
        }
        const std::string word = info.word;
        CHECK(!word.empty() && effectFromWord(info.word) == kind);
        for (char c : word) CHECK((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
        for (int other = 1; other < k; ++other) CHECK(word != effectInfo(static_cast<EffectKind>(other)).word);
        CHECK(info.group != EffectGroup::None && effectGroupName(info.group)[0] != 0);
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
    // The list as it is today, in its groups: FILTER; DRIVE; DELAY; REVERB; MOVING; LEVEL; DIRT.
    static const EffectKind list[] = {EffectKind::None, EffectKind::Filter, EffectKind::Saturate, EffectKind::Drive, EffectKind::Delay, EffectKind::Reverb, EffectKind::Chorus, EffectKind::Comp, EffectKind::Crush};
    CHECK(effectListSize() == static_cast<int>(sizeof list / sizeof list[0]));
    for (int i = 0; i < effectListSize(); ++i) CHECK(effectAt(i) == list[i]);
    // ...and a group is never in two pieces (a whole group is stepped over at once).
    for (int i = 2; i < effectListSize(); ++i)
        for (int j = 1; j < i - 1; ++j) CHECK(effectInfo(effectAt(i)).group != effectInfo(effectAt(j)).group || effectInfo(effectAt(i - 1)).group == effectInfo(effectAt(i)).group);
    // Which settings are choices today, and the groups.
    CHECK(effectChoices(EffectKind::Filter, 2) == 5 && effectChoices(EffectKind::Delay, 0) == 6);
    CHECK(effectChoices(EffectKind::Filter, 0) == 0 && effectChoices(EffectKind::Delay, 1) == 0 && effectChoices(EffectKind::Drive, 0) == 0);
    CHECK(effectChoices(EffectKind::Reverb, 0) == 0 && effectChoices(EffectKind::Crush, 0) == 0);
    CHECK(effectInfo(EffectKind::Filter).group == EffectGroup::Filter && effectInfo(EffectKind::Drive).group == EffectGroup::Drive);
    CHECK(effectInfo(EffectKind::Delay).group == EffectGroup::Delay && effectInfo(EffectKind::Reverb).group == EffectGroup::Reverb);
    CHECK(effectInfo(EffectKind::Crush).group == EffectGroup::Dirt && effectInfo(EffectKind::None).group == EffectGroup::None);
    CHECK(std::string(effectGroupName(EffectGroup::Filter)) == "FILTER" && std::string(effectGroupName(EffectGroup::Drive)) == "DRIVE");
    CHECK(std::string(effectGroupName(EffectGroup::Delay)) == "DELAY" && std::string(effectGroupName(EffectGroup::Reverb)) == "REVERB");
    CHECK(std::string(effectGroupName(EffectGroup::Moving)) == "MOVING" && std::string(effectGroupName(EffectGroup::Level)) == "LEVEL");
    CHECK(std::string(effectGroupName(EffectGroup::Dirt)) == "DIRT" && std::string(effectGroupName(EffectGroup::None)).empty());
    CHECK(std::string(effectGroupName(EffectGroup::Count)).empty() && std::string(effectGroupName(static_cast<EffectGroup>(-1))).empty());
    // How settings read, each effect's own way.
    CHECK(reads(EffectKind::Filter, 0, 0.f) == "30 HZ" && reads(EffectKind::Filter, 0, 1.f) == "21.7 KHZ" && reads(EffectKind::Filter, 1, 0.2f) == "20%");
    CHECK(reads(EffectKind::Filter, 2, 0.1f) == "LOW 12 DB" && reads(EffectKind::Filter, 2, 0.3f) == "LOW 24 DB" && reads(EffectKind::Filter, 2, 0.5f) == "BAND PASS");
    CHECK(reads(EffectKind::Filter, 2, 0.7f) == "DJ" && reads(EffectKind::Filter, 2, 0.9f) == "HIGH PASS" && reads(EffectKind::Filter, 3, 1.f) == "100%");
    const float dj[4] = {0.5f, 0.2f, 0.7f, 1.f}, low[4] = {0.5f, 0.2f, 0.1f, 1.f};
    CHECK(reads(EffectKind::Filter, 0, 0.5f, dj) == "OPEN" && reads(EffectKind::Filter, 0, 0.f, dj) == "LOW 60 HZ" && reads(EffectKind::Filter, 0, 1.f, dj) == "HIGH 8.0 KHZ");
    CHECK(reads(EffectKind::Filter, 0, 0.f, low) == "30 HZ");
    CHECK(reads(EffectKind::Drive, 0, 0.35f) == "35%" && reads(EffectKind::Drive, 3, 0.5f) == "100%" && reads(EffectKind::Drive, 3, 1.f) == "200%" && reads(EffectKind::Drive, 3, 0.333f) == "66%");
    CHECK(reads(EffectKind::Delay, 0, 0.f) == "1/16" && reads(EffectKind::Delay, 0, 0.2f) == "1/8" && reads(EffectKind::Delay, 0, 0.4f) == "1/8 DOTTED");
    CHECK(reads(EffectKind::Delay, 0, 0.5f) == "1/4" && reads(EffectKind::Delay, 0, 0.7f) == "1/4 DOTTED" && reads(EffectKind::Delay, 0, 1.f) == "1/2" && reads(EffectKind::Delay, 1, 0.45f) == "45%");
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
    for (int k = 1; k < kEffectKinds; ++k) CHECK((makeEffect(static_cast<EffectKind>(k), 48000.f, 120.f) != nullptr) == effectExists(static_cast<EffectKind>(k)));
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
        if (!effectExists(kind)) continue;
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
        //   FILTER 4.2   DRIVE 2.0   DELAY 4.8   REVERB 3.9   CRUSH 1.0
        SAY("bounded  %-7s loudest %.2f\n", info.name, static_cast<double>(loudest));
        CHECK(loudest < 8.f);
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
                CHECK(sumWith(p, 0.f, true) != sumWith(p, 1.f, true)); // and the setting does do something
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
        // The new ones glide every setting. Their numbers are over 1 only
        // because they come out louder or steeper than the sine went in
        // ("held still" says by how much), not because anything jumps:
        //   CHORUS  RATE 1.3      DEPTH 1.7       TYPE 0.7    MIX 1.3        1.3
        //   COMP    THRESHOLD 1.1 RATIO 0.9       SPEED 0.8   MIX 1.0        0.8  (it turns this sine down)
        //   SATURATE DRIVE 5.4    TYPE 2.1        TONE 1.4    MIX 1.4        1.4  (it squares the sine: at full DRIVE
        //                                                 held still it is 5.8, and FOLD held still 2.1. testSaturate)
        static const double today[kEffectKinds][kEffectParams + 1] = {
            {0, 0, 0, 0, 0}, {1.15, 1.01, 18.1, 1.00, 1.00}, {5.87, 1.91, 1.90, 3.80, 1.91}, {33.5, 1.51, 1.55, 2.57, 1.55}, {0.94, 0.90, 1.09, 0, 0.83}, {38.2, 38.8, 3.79, 0, 4.22},
            {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, // kept for TAPE and HALL
            {1.26, 1.67, 0.71, 1.25, 1.25},   // CHORUS
            {1.05, 0.91, 0.83, 1.00, 0.82},   // COMP
            {5.42, 2.10, 1.44, 1.43, 1.44},   // SATURATE
        };
        for (int p = 0; p < info.params; ++p) {
            double steady = 0.0;
            const double jump = sweepJump(kind, p, &steady);
            SAY("sweep    %-7s %-10s %6.2f  (held still %.2f)\n", info.name, info.paramNames[p], jump, steady);
            CHECK(std::fabs(jump - today[k][p]) < 0.1 * today[k][p] + 0.05);
            CHECK(std::fabs(steady - today[k][kEffectParams]) < 0.05 * today[k][kEffectParams] + 0.02);
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
static std::vector<double> pitchOf(const std::vector<float>& v, std::size_t from, int cycles, std::vector<double>* when = nullptr, double rate = 48000.0) {
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
    CHECK(std::string(info.name) == "CHORUS" && std::string(info.word) == "chorus" && info.params == 4 && info.group == EffectGroup::Moving);
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
        const std::vector<double> hz = pitchOf(io.l, 4800, 4, &when);
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
        const auto blocks = [&](int count) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < count; ++i) {
                io.l.assign(kMaxBlock, 0.f);
                io.r.assign(kMaxBlock, 0.f);
                r->process(io.l.data(), io.r.data(), kMaxBlock);
            }
            return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / count;
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
static double swapJump(EffectKind from, EffectKind to, bool master = false) {
    std::unique_ptr<Engine> engine = sineEngine();
    const int track = master ? kMasterTrack : 0, slot = master ? 0 : 1;
    CHECK(engine->setEffect(track, slot, from));
    std::vector<float> all = leftOf(*engine, 48000);
    CHECK(engine->setEffect(track, slot, to));
    const std::vector<float> after = leftOf(*engine, 2400);
    all.insert(all.end(), after.begin(), after.end());
    engine->collectGarbage();
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
    CHECK(std::string(info.name) == "COMP" && std::string(info.word) == "comp" && info.params == 4 && info.group == EffectGroup::Level);
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
        for (int slot = -1; slot <= kEffectSlots; ++slot) CHECK(engine->effectMeter(0, slot) == 0.f && engine->effectMeter(kMasterTrack, slot) == 0.f);
        CHECK(engine->setEffect(0, 1, kind));
        leftOf(*engine, 48000);
        SAY("comp     Engine::effectMeter on a track: %.2f dB\n", static_cast<double>(engine->effectMeter(0, 1)));
        CHECK(std::fabs(engine->effectMeter(0, 1) - 8.53f) < 0.15f);
        CHECK(engine->effectMeter(0, 0) == 0.f && engine->effectMeter(1, 1) == 0.f && engine->effectMeter(kMasterTrack, 0) == 0.f && engine->effectMeter(kMasterTrack, 1) == 0.f);
        CHECK(engine->effectMeter(-2, 1) == 0.f && engine->effectMeter(kNumTracks, 1) == 0.f && engine->effectMeter(0, kEffectSlots) == 0.f && engine->effectMeter(0, -1) == 0.f);
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
        leftOf(*engine, 64);
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
    CHECK(std::string(info.name) == "SATURATE" && std::string(info.word) == "saturate" && info.params == 4 && info.group == EffectGroup::Drive);
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
    for (int type = 0; type < 4; ++type)
        for (float tone : {0.f, 1.f}) {
            std::unique_ptr<Effect> e = saturate(true, 1.f, type, tone);
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
    // half way it is half of each, sample for sample.
    for (int type = 0; type < 4; ++type) {
        const Stereo in = pinInput(48000.f);
        Stereo dry = in, half = in, full = in;
        through(*saturate(true, 0.7f, type, 0.6f, 0.f), dry);
        through(*saturate(true, 0.7f, type, 0.6f, 0.5f), half);
        through(*saturate(true, 0.7f, type, 0.6f, 1.f), full);
        CHECK(dry.l == in.l && dry.r == in.r);
        float worst = 0.f;
        for (std::size_t i = 0; i < in.frames(); ++i) worst = std::fmax(worst, std::fabs(half.l[i] - 0.5f * (in.l[i] + full.l[i])));
        CHECK(worst < 1e-6f && peakOf(full.l) > 0.3f);
        // Each side is its own.
        Stereo one = in;
        one.r.assign(one.frames(), 0.f);
        through(*saturate(true, 0.7f, type, 0.6f, 1.f), one);
        CHECK(peakOf(one.r) == 0.f && one.l == full.l);
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
                if (a == b || (a && !effectExists(static_cast<EffectKind>(a))) || (b && !effectExists(static_cast<EffectKind>(b)))) continue;
                const double jump = swapJump(static_cast<EffectKind>(a), static_cast<EffectKind>(b), master == 1);
                SAY("swap     %-6s to %-6s %s %5.2f\n", effectInfo(static_cast<EffectKind>(a)).name, effectInfo(static_cast<EffectKind>(b)).name, master ? "master" : "track ", jump);
                const bool crush = a == static_cast<int>(EffectKind::Crush) || b == static_cast<int>(EffectKind::Crush);
                CHECK(jump < (crush ? 4.5 : 2.1));
                if (jump >= (crush ? 4.5 : 2.1)) std::printf("     swap %s to %s: %.2f\n", effectInfo(static_cast<EffectKind>(a)).name, effectInfo(static_cast<EffectKind>(b)).name, jump);
                if (!crush) worst = jump > worst ? jump : worst;
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
    testPinnedSound();
    testWholeSong();
    testTheTable();
    testEveryEffect();
    testFilterLevels();
    testDriveHarmonics();
    testChorus();
    testDelayEchoes();
    testReverbTail();
    testFlushToZero();
    testReverbDoesNotSlowDown();
    testSwapFade();
    testComp();
    testHalfband();
    testSaturate();
    testSwapsOnAnyHelperCount();
    if (failures == 0) std::printf("effects_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
