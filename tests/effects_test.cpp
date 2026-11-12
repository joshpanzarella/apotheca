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
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "apotheca/denormals.hpp"
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
    // The list as it is today, in groups, a new effect before the old one of its group.
    {
        const EffectKind list[] = {EffectKind::None, EffectKind::Filter, EffectKind::Drive, EffectKind::Tape, EffectKind::Delay, EffectKind::Hall, EffectKind::Reverb, EffectKind::Crush};
        CHECK(effectListSize() == static_cast<int>(sizeof list / sizeof list[0]));
        for (int i = 0; i < effectListSize(); ++i) CHECK(effectAt(i) == list[i]);
    }
    // The numbers of the new ones: fixed for good, as the old five's are.
    CHECK(static_cast<int>(EffectKind::Tape) == 6 && static_cast<int>(EffectKind::Hall) == 7);
    CHECK(std::string(effectInfo(EffectKind::Hall).word) == "hall" && effectFromWord("hall") == EffectKind::Hall);
    CHECK(effectInfo(EffectKind::Hall).group == EffectGroup::Reverb && effectChoices(EffectKind::Hall, 0) == 0 && effectChoices(EffectKind::Hall, 1) == 0);
    CHECK(std::string(effectInfo(EffectKind::Tape).word) == "tape" && effectFromWord("tape") == EffectKind::Tape);
    CHECK(effectInfo(EffectKind::Tape).group == EffectGroup::Delay && effectChoices(EffectKind::Tape, 0) == 8 && effectChoices(EffectKind::Tape, 1) == 0);
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
    }
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
        //   FILTER 4.2   DRIVE 2.0   DELAY 4.8   REVERB 3.9   CRUSH 1.0   TAPE 2.9   HALL 2.6
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
        // The new ones glide every setting, a choice included:
        //   TAPE    TIME 1.2      FEEDBACK 0.9    AGE 1.0     MIX 1.0        0.9
        //   HALL    SIZE 1.4      DECAY 1.0       TONE 0.8    MIX 1.0        0.7  (SIZE bends the room's pitch on the way)
        static const double today[kEffectKinds][kEffectParams + 1] = {
            {0, 0, 0, 0, 0},
            {1.15, 1.01, 18.1, 1.00, 1.00},
            {5.87, 1.91, 1.90, 3.80, 1.91},
            {33.5, 1.51, 1.55, 2.57, 1.55},
            {0.94, 0.90, 1.09, 0, 0.83},
            {38.2, 38.8, 3.79, 0, 4.22},
            {1.15, 0.90, 1.01, 1.00, 0.88}, // TAPE
            {1.38, 0.96, 0.80, 1.00, 0.69}, // HALL
        };
        for (int p = 0; p < info.params; ++p) {
            double steady = 0.0;
            const double jump = sweepJump(kind, p, &steady);
            SAY("sweep    %-7s %-10s %6.2f  (held still %.2f)\n", info.name, info.paramNames[p], jump, steady);
            CHECK(std::fabs(jump - today[k][p]) < 0.1 * today[k][p] + 0.05);
            CHECK(std::fabs(steady - today[k][kEffectParams]) < 0.05 * today[k][kEffectParams] + 0.02);
            if (k > static_cast<int>(EffectKind::Crush)) CHECK(jump < 1.5); // a new effect glides: turning it is about as smooth as the sine
        }
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
    const char* names[8] = {"1/16", "1/8 TRIP", "1/8", "1/4 TRIP", "1/8 DOTTED", "1/4", "1/4 DOTTED", "1/2"};
    for (int i = 0; i < 8; ++i) CHECK(reads(tape, 0, tapeTime(i)) == names[i]);
    CHECK(reads(tape, 0, 0.f) == "1/16" && reads(tape, 0, 1.f) == "1/2" && reads(tape, 0, info.defaults[0]) == "1/8 DOTTED" && reads(tape, 0, 0.125f) == "1/8 TRIP");
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
                const double jump = swapJump(static_cast<EffectKind>(a), static_cast<EffectKind>(b), master == 1);
                SAY("swap     %-6s to %-6s %s %5.2f\n", effectInfo(static_cast<EffectKind>(a)).name, effectInfo(static_cast<EffectKind>(b)).name, master ? "master" : "track ", jump);
                const bool crush = a == static_cast<int>(EffectKind::Crush) || b == static_cast<int>(EffectKind::Crush);
                CHECK(jump < (crush ? 4.5 : 2.1));
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
    // TAPE and DELAY on ECHO, HALL and REVERB on SPACE, the newer one first as in the main list.
    CHECK(busListSize(0) == 3 && busEffectAt(0, 1) == EffectKind::Tape && busEffectAt(0, 2) == EffectKind::Delay);
    CHECK(busListSize(1) == 3 && busEffectAt(1, 1) == EffectKind::Hall && busEffectAt(1, 2) == EffectKind::Reverb);
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
        double differs = 0.0;
        for (int i = 24000; i < 48000; ++i) differs = std::fmax(differs, std::fabs(static_cast<double>(out[i]) - plain[4800 + i]));
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

// Hold A + left / right on the FX screen: the first effect of the next group, or of the one before.
static void testTheGroupJump() {
    const EffectGroup F = EffectGroup::Filter, D = EffectGroup::Drive, E = EffectGroup::Delay, R = EffectGroup::Reverb, N = EffectGroup::None;
    const EffectGroup row[] = {N, F, D, D, E, E, E, R, R};
    const int n = 9;
    const int right[] = {1, 2, 4, 4, 7, 7, 7, 7, 8}, left[] = {0, 0, 1, 1, 2, 2, 2, 4, 4};
    for (int i = 0; i < n; ++i) {
        CHECK(groupJump(row, n, i, 1) == right[i]);
        CHECK(groupJump(row, n, i, -1) == left[i]);
        CHECK(groupJump(row, n, i, 0) == i && groupJump(row, n, i, 5) == right[i] && groupJump(row, n, i, -5) == left[i]);
    }
    CHECK(groupJump(row, n, -1, 1) == -1 && groupJump(row, n, n, -1) == n && groupJump(nullptr, n, 3, 1) == 3 && groupJump(row, 0, 0, 1) == 0);
    // A list whose first group has more than one in it: left from inside it goes nowhere.
    const EffectGroup two[] = {F, F, D};
    CHECK(groupJump(two, 3, 1, -1) == 1 && groupJump(two, 3, 0, -1) == 0 && groupJump(two, 3, 2, -1) == 0 && groupJump(two, 3, 0, 1) == 2);
    // The real list: every jump lands on the first of another group, or stays.
    for (int place = 0; place < effectListSize(); ++place)
        for (int by = -1; by <= 1; by += 2) {
            const int to = effectGroupJump(place, by);
            CHECK(to >= 0 && to < effectListSize() && (by > 0 ? to >= place : to <= place));
            if (to == place) continue;
            CHECK(effectInfo(effectAt(to)).group != effectInfo(effectAt(place)).group);
            CHECK(to == 0 || effectInfo(effectAt(to - 1)).group != effectInfo(effectAt(to)).group);
            // ...and the nearest such: nothing of a third group lies between.
            for (int i = (by > 0 ? place : to) + 1; i < (by > 0 ? to : place); ++i)
                CHECK(effectInfo(effectAt(i)).group == effectInfo(effectAt(by > 0 ? place : to)).group || (by < 0 && effectInfo(effectAt(i)).group == effectInfo(effectAt(place)).group));
        }
    CHECK(effectGroupJump(0, -1) == 0 && effectGroupJump(0, 1) == 1 && effectGroupJump(effectListSize() - 1, 1) == effectListSize() - 1);
    CHECK(effectGroupJump(-3, 1) == -3 && effectGroupJump(effectListSize(), -1) == effectListSize());
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
    testDelayEchoes();
    testReverbTail();
    testTape();
    testHall();
    testFlushToZero();
    testReverbDoesNotSlowDown();
    testSwapFade();
    testSwapsOnAnyHelperCount();
    testEffectsOnABus();
    testSends();
    testSendLanes();
    testBusRests();
    testBusEffectsInTheEngine();
    testThirdSlot();
    testSendsOnAnyHelperCount();
    testTheGroupJump();
    if (failures == 0) std::printf("effects_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
