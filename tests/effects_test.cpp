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
// No framework: each CHECK prints its line on failure and the program exits
// non-zero if any failed.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--print") {
        printPins();
        const std::vector<float> song = effectsSong(0);
        Sum sum;
        sum.add(song);
        std::printf("song: 0x%016llxull, rms %.9g\n", static_cast<unsigned long long>(sum.h), rmsOf(song, 0, song.size() - 1));
        return 0;
    }
    testPinnedSound();
    testWholeSong();
    if (failures == 0) std::printf("effects_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
