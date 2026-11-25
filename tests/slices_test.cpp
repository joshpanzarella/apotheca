// SLICES AT THE HITS (apotheca/slice_rule.hpp, batch S2): the hit finder on drum loops built by hand,
// whose hits are known to the sample, and the engine playing a lane's own cuts. Measured, not
// listened to.
//
//   1. a clean loop: a cut just before every hit (within 2 ms before its first sample), none between
//   2. the same loop under noise: the same cuts, and nothing in the noise
//   3. a soft hit after a loud one: found, at a higher SENSITIVITY than the loud ones
//   4. SENSITIVITY: more never finds fewer; the lowest finds only what rises out of near silence
//   5. two hits closer than 30 ms are one, the stronger; one within 30 ms of the top is the top's
//   6. at most 64 slices, the strongest kept; silence and a steady tone are one slice
//   7. the helpers: a start's slice, where a slice begins, what ONE SLICE plays, up and down, EVEN
//      as it always was, cuts from outside held
//   8. the engine: ONE SLICE plays to the next cut, backwards too; RUNS ON is untouched; no cuts is
//      the sound as it was to the bit; a track taken away keeps none
//
// `slices_test --measure` times the finder on a 20 s loop (measured.md).

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/slice_rule.hpp"

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

const float kRate = 48000.f;

// The same noise every run.
struct Noise {
    std::uint32_t state = 12345u;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (2.f / 16777216.f) - 1.f;
    }
};

enum class Drum { Kick, Snare, Hat };

// One drum, added in at sample `at` with peak `gain`: a kick a falling sine, a snare noise and a tone,
// a hat noise with its lows taken out. Each starts at full at once (as a sampled drum does).
void put(std::vector<float>& out, Drum kind, int at, float gain, Noise& noise) {
    const int n = kind == Drum::Kick ? 24000 : kind == Drum::Snare ? 14000 : 4000;
    double phase = 0.0;
    float before = 0.f;
    for (int i = 0; i < n && at + i < static_cast<int>(out.size()); ++i) {
        const float t = static_cast<float>(i) / kRate;
        float v = 0.f;
        if (kind == Drum::Kick) {
            phase += 2.0 * 3.14159265358979 * (50.0 + 100.0 * std::exp(-t * 30.0)) / kRate;
            v = static_cast<float>(std::cos(phase)) * std::exp(-t * 8.f);
        } else if (kind == Drum::Snare) {
            v = (0.6f * noise.next() + 0.4f * std::cos(6.2831853f * 190.f * t)) * std::exp(-t * 20.f);
        } else {
            const float w = noise.next();
            v = 0.7f * (w - before) * std::exp(-t * 60.f);
            before = w;
        }
        out[static_cast<std::size_t>(at + i)] += gain * v;
    }
}

struct Hit {
    Drum kind;
    double seconds;
    float gain;
};

std::vector<float> loop(double seconds, const std::vector<Hit>& hits, float noiseLevel = 0.f) {
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate), 0.f);
    Noise noise;
    for (const Hit& h : hits) put(out, h.kind, static_cast<int>(h.seconds * kRate), h.gain, noise);
    if (noiseLevel > 0.f) {
        Noise hiss;
        hiss.state = 777u;
        for (float& v : out) v += noiseLevel * hiss.next();
    }
    return out;
}

// A break, two seconds: kicks, snares, hats on the eighths; none on the top but the kick.
const std::vector<Hit> kBreak = {
    {Drum::Kick, 0.0, 0.9f},   {Drum::Hat, 0.25, 0.5f},   {Drum::Snare, 0.5, 0.8f}, {Drum::Hat, 0.75, 0.5f},
    {Drum::Kick, 1.0, 0.9f},   {Drum::Kick, 1.375, 0.8f}, {Drum::Snare, 1.5, 0.8f}, {Drum::Hat, 1.75, 0.5f},
};

LaneCuts cutsOf(const std::vector<float>& sound, int sense) {
    const std::vector<slices::Found> found = slices::findHits(sound.data(), static_cast<int>(sound.size()), kRate);
    return slices::cutsAt(found, static_cast<int>(sound.size()), sense, kRate);
}

// Is there a cut at most 2 ms before `seconds` (and not after it)?
bool cutBefore(const LaneCuts& c, int n, double seconds) {
    for (int k = 1; k < c.count; ++k) {
        const double at = static_cast<double>(c.at[k]) * n / kRate;
        if (at <= seconds && at >= seconds - 0.002) return true;
    }
    return false;
}

void testACleanLoop() {
    const std::vector<float> sound = loop(2.0, kBreak);
    const int n = static_cast<int>(sound.size());
    for (int sense = 1; sense <= kSenseMost; ++sense) {
        const LaneCuts c = cutsOf(sound, sense);
        CHECK(c.count == 8 && c.sense == sense && c.at[0] == 0.f);
        for (const Hit& h : kBreak)
            if (h.seconds > 0.0) CHECK(cutBefore(c, n, h.seconds));
        for (int k = 1; k < c.count; ++k) CHECK(c.at[k] > c.at[k - 1] && c.at[k] < 1.f);
        if (c.count != 8) std::printf("    a clean break at SENSITIVITY %d: %d slices, not 8\n", sense, c.count);
    }
    // Each cut is the 1 ms lead before the first loud sample: on a hit that starts at full at once, 48
    // samples before it (give or take the few its noise starts quiet for).
    const LaneCuts c = cutsOf(sound, 5);
    const long snare = std::lround(static_cast<double>(c.at[2]) * n) - (static_cast<long>(0.5 * kRate) - 48);
    CHECK(snare >= -8 && snare <= 4);
    // A hit over a loud tail (a snare 5 dB under a snare 60 ms before it, still ringing): found where it starts,
    // not once it stands four times over the tail: the same lead before it.
    const std::vector<float> tail = loop(1.0, {{Drum::Snare, 0.1, 0.9f}, {Drum::Snare, 0.16, 0.5f}});
    const LaneCuts t = cutsOf(tail, kSenseMost);
    const long second = t.count == 3 ? std::lround(static_cast<double>(t.at[2]) * static_cast<double>(tail.size())) - static_cast<long>(0.16 * kRate) : 9999;
    CHECK(t.count == 3 && second >= -48 - 8 && second <= -48 + 4);
}

void testUnderNoise() {
    for (float hiss : {0.003f, 0.01f, 0.03f}) { // -50, -40 and -30 dB under the loudest
        const std::vector<float> sound = loop(2.0, kBreak, hiss);
        const int n = static_cast<int>(sound.size());
        for (int sense = 2; sense <= kSenseMost; ++sense) {
            const LaneCuts c = cutsOf(sound, sense);
            CHECK(c.count == 8);
            for (const Hit& h : kBreak)
                if (h.seconds > 0.0) CHECK(cutBefore(c, n, h.seconds));
            if (c.count != 8) std::printf("    noise %.3f at SENSITIVITY %d: %d slices, not 8\n", static_cast<double>(hiss), sense, c.count);
        }
    }
    // Noise and nothing else, twenty seconds of it: nothing found but the top, however sensitive.
    std::vector<float> hiss(960000);
    Noise noise;
    for (float& v : hiss) v = 0.2f * noise.next();
    CHECK(cutsOf(hiss, kSenseMost).count == 1);
}

void testASoftHitAfterALoudOne() {
    // A loud kick with a long tail; a hat 24 dB under it, then a snare 18 dB under it, in the tail.
    const std::vector<Hit> hits = {{Drum::Kick, 0.1, 0.9f}, {Drum::Hat, 0.25, 0.9f * 0.063f}, {Drum::Snare, 0.4, 0.9f * 0.126f}};
    const std::vector<float> sound = loop(1.0, hits);
    const int n = static_cast<int>(sound.size());
    const LaneCuts c = cutsOf(sound, 7);
    CHECK(c.count == 4 && cutBefore(c, n, 0.1) && cutBefore(c, n, 0.25) && cutBefore(c, n, 0.4));
    // A ghost snare 15 dB under a loud one, a sixteenth after it at 120 BPM, while it still rings: found
    // at the most SENSITIVITY, not at the least.
    const std::vector<float> ghost = loop(1.0, {{Drum::Snare, 0.1, 0.9f}, {Drum::Snare, 0.225, 0.16f}});
    CHECK(cutBefore(cutsOf(ghost, kSenseMost), static_cast<int>(ghost.size()), 0.225));
    CHECK(cutsOf(ghost, 1).count == 2 && !cutBefore(cutsOf(ghost, 1), static_cast<int>(ghost.size()), 0.225));
    // A kick 24 dB under a hat that still rings: its lows rise though the whole sound hardly does.
    const std::vector<float> two = loop(0.6, {{Drum::Hat, 0.05, 0.9f}, {Drum::Kick, 0.09, 0.06f}});
    const LaneCuts k = cutsOf(two, 7);
    CHECK(k.count == 3 && cutBefore(k, static_cast<int>(two.size()), 0.09));
    // ...found in the lows, whose filter lags the sound: the lag is taken off, so the cut is still the 1 ms lead
    // (or a little more) before the kick, never on it.
    CHECK(k.at[2] * static_cast<float>(two.size()) <= 0.09f * kRate - 48.f);
}

void testSensitivity() {
    // Snares over a steady hiss, each louder than the last, rising about 6 to 30 dB out of it: each step
    // of SENSITIVITY finds at least what the one below found, the least only the two loudest, the most all.
    std::vector<Hit> hits;
    const float gains[] = {0.03f, 0.05f, 0.08f, 0.13f, 0.2f, 0.35f, 0.6f, 0.95f};
    for (int i = 0; i < 8; ++i) hits.push_back({Drum::Snare, 0.25 + i * 0.25, gains[i]});
    const std::vector<float> sound = loop(2.25, hits, 0.02f);
    int before = 0;
    for (int sense = 1; sense <= kSenseMost; ++sense) {
        const LaneCuts c = cutsOf(sound, sense);
        CHECK(c.count >= before);
        before = c.count;
    }
    CHECK(cutsOf(sound, 1).count == 3 && cutsOf(sound, kSenseMost).count == 9 && cutsOf(sound, 5).count == 6);
    CHECK(slices::senseRise(1) == 24.f && slices::senseRise(kSenseMost) == 3.f && slices::senseRise(0) == 24.f && slices::senseRise(99) == 3.f);
    for (int s = 2; s <= kSenseMost; ++s) CHECK(slices::senseRise(s) < slices::senseRise(s - 1));
}

void testGapsAndTheTop() {
    const int gap = static_cast<int>(std::lround(slices::kGapSeconds * kRate));
    // A flam: a soft snare 10 ms before a loud one. One cut, the first's: it rose out of silence, the most.
    const std::vector<float> flam = loop(1.0, {{Drum::Snare, 0.3, 0.2f}, {Drum::Snare, 0.31, 0.9f}});
    LaneCuts c = cutsOf(flam, kSenseMost);
    CHECK(c.count == 2 && cutBefore(c, static_cast<int>(flam.size()), 0.3));
    // Hits further apart than the gap are two: a hat, and a kick 40 ms after it.
    const std::vector<float> apart = loop(1.0, {{Drum::Hat, 0.3, 0.9f}, {Drum::Kick, 0.34, 0.9f}});
    CHECK(cutsOf(apart, 5).count == 3);
    // A hit within the gap of the top is the top's; one just past it is a cut of its own.
    const std::vector<float> near = loop(1.0, {{Drum::Kick, 0.01, 0.9f}, {Drum::Snare, 0.5, 0.9f}});
    c = cutsOf(near, 5);
    CHECK(c.count == 2 && c.at[0] == 0.f && cutBefore(c, static_cast<int>(near.size()), 0.5));
    // ...by the candidates' own rule: one at exactly the gap counts, one a sample short does not.
    std::vector<slices::Found> found(2);
    found[0].at = gap - 1, found[0].rise = 30.f;
    found[1].at = gap + 4800, found[1].rise = 30.f;
    CHECK(slices::cutsAt(found, 48000, 5, kRate).count == 2);
    found[0].at = gap;
    CHECK(slices::cutsAt(found, 48000, 5, kRate).count == 3);
    // Two exactly the gap apart are two; a sample nearer, one.
    found[0].at = 10000, found[1].at = 10000 + gap;
    CHECK(slices::cutsAt(found, 48000, 5, kRate).count == 3);
    found[1].at = 10000 + gap - 1;
    CHECK(slices::cutsAt(found, 48000, 5, kRate).count == 2);
    // Two at once: the stronger, and the earlier on a tie.
    found[0].at = 10000, found[0].rise = 10.f;
    found[1].at = 10100, found[1].rise = 20.f;
    c = slices::cutsAt(found, 48000, 5, kRate);
    CHECK(c.count == 2 && c.at[1] == 10100.f / 48000.f);
    found[1].rise = 10.f;
    c = slices::cutsAt(found, 48000, 5, kRate);
    CHECK(c.count == 2 && c.at[1] == 10000.f / 48000.f);
    // A rise just under the SENSITIVITY's is not a hit; at it, it is.
    found.resize(1);
    found[0].at = 20000, found[0].rise = slices::senseRise(5) - 0.01f;
    CHECK(slices::cutsAt(found, 48000, 5, kRate).count == 1);
    found[0].rise = slices::senseRise(5);
    CHECK(slices::cutsAt(found, 48000, 5, kRate).count == 2);
    // A candidate past the end is no cut.
    found[0].at = 48000;
    CHECK(slices::cutsAt(found, 48000, 5, kRate).count == 1);
    // A cut added by hand: at the strongest candidate inside, each more than 5 ms (240 samples) in; none: -1.
    found.assign(4, slices::Found());
    found[0].at = 10000, found[0].rise = 8.f;
    found[1].at = 10100, found[1].rise = 30.f; // (the strongest, but in the first check too near the slice's start)
    found[2].at = 14000, found[2].rise = 12.f;
    found[3].at = 30000, found[3].rise = 40.f; // (outside)
    CHECK(slices::strongestBetween(found, 48000, 10000.f / 48000.f - 0.002f, 20000.f / 48000.f, kRate) == 14000.f / 48000.f);
    CHECK(slices::strongestBetween(found, 48000, 9000.f / 48000.f, 20000.f / 48000.f, kRate) == 10100.f / 48000.f); // (all three inside: the strongest)
    CHECK(slices::strongestBetween(found, 48000, 15000.f / 48000.f, 25000.f / 48000.f, kRate) == -1.f && slices::strongestBetween(found, 0, 0.f, 1.f, kRate) == -1.f);
}

void testTheMostAndNothing() {
    // A hit every 150 ms for 20 s: 133 of them, the louder every fourth. 64 slices, the strongest kept.
    std::vector<Hit> hits;
    for (int i = 1; i * 0.15 < 20.0; ++i) hits.push_back({Drum::Snare, i * 0.15, i % 4 == 0 ? 0.9f : 0.3f});
    const std::vector<float> sound = loop(20.0, hits);
    const int n = static_cast<int>(sound.size());
    const LaneCuts c = cutsOf(sound, kSenseMost);
    CHECK(c.count == kMaxLaneCuts);
    for (int k = 1; k < c.count; ++k) CHECK(c.at[k] > c.at[k - 1]);
    int loud = 0;
    for (int i = 4; i * 0.15 < 20.0; i += 4) loud += cutBefore(c, n, i * 0.15) ? 1 : 0;
    CHECK(loud == 33); // every loud one (the 33 of them) is among the 63 kept
    // Silence, a steady tone, nothing at all: one slice.
    CHECK(cutsOf(std::vector<float>(48000, 0.f), 5).count == 1);
    std::vector<float> tone(96000);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * std::sin(6.2831853f * 220.f * static_cast<float>(i) / kRate);
    CHECK(cutsOf(tone, kSenseMost).count == 1);
    CHECK(slices::findHits(nullptr, 1000, kRate).empty() && slices::findHits(tone.data(), 100, kRate).empty());
    const std::vector<slices::Found> none;
    const LaneCuts empty = slices::cutsAt(none, 0, 3, kRate);
    CHECK(empty.count == 1 && empty.sense == 3);
}

void testTheHelpers() {
    LaneCuts even;
    // EVEN: as the slice view always had it.
    CHECK(slices::count(even, 16) == 16 && slices::cutStart(even, 16, 5) == 5.f / 16.f && slices::cutStart(even, 16, 21) == 5.f / 16.f && slices::cutStart(even, 16, -1) == 15.f / 16.f);
    CHECK(slices::sliceAt(even, 16, 5.f / 16.f) == 5 && slices::sliceAt(even, 16, 0.4f) == 6 && slices::sliceAt(even, 16, 1.f) == 15 && slices::sliceAt(even, 16, -0.1f) == 0);
    CHECK(slices::partAt(even, 16, 0.4f) == 1.f / 16.f && slices::partAt(even, 32, 0.f) == 1.f / 32.f);
    CHECK(slices::stepCut(even, 16, 0.4f, 1) == 7.f / 16.f && slices::stepCut(even, 16, 0.4f, -1) == 6.f / 16.f && slices::stepCut(even, 16, 15.f / 16.f, 1) == 0.f && slices::stepCut(even, 16, 0.f, -1) == 15.f / 16.f);
    CHECK(slices::onCut(even, 16, 5.f / 16.f) && !slices::onCut(even, 16, 0.4f));
    // Its own: uneven.
    LaneCuts c;
    c.count = 4;
    c.at[1] = 0.1f, c.at[2] = 0.5f, c.at[3] = 0.55f;
    CHECK(slices::count(c, 16) == 4 && slices::cutStart(c, 16, 2) == 0.5f && slices::cutStart(c, 16, 6) == 0.5f && slices::cutStart(c, 16, -1) == 0.55f);
    CHECK(slices::sliceAt(c, 16, 0.f) == 0 && slices::sliceAt(c, 16, 0.0999f) == 0 && slices::sliceAt(c, 16, 0.1f) == 1 && slices::sliceAt(c, 16, 0.3f) == 1 && slices::sliceAt(c, 16, 0.9f) == 3);
    CHECK(slices::sliceAt(c, 16, 0.5f - 1e-6f) == 2); // a hair short of a cut is on it
    CHECK(slices::partAt(c, 16, 0.1f) == 0.5f - 0.1f && slices::partAt(c, 16, 0.3f) == 0.5f - 0.3f && slices::partAt(c, 16, 0.55f) == 1.f - 0.55f);
    CHECK(slices::stepCut(c, 16, 0.1f, 1) == 0.5f && slices::stepCut(c, 16, 0.3f, 1) == 0.5f && slices::stepCut(c, 16, 0.3f, -1) == 0.1f && slices::stepCut(c, 16, 0.55f, 1) == 0.f && slices::stepCut(c, 16, 0.f, -1) == 0.55f);
    CHECK(slices::onCut(c, 16, 0.5f) && !slices::onCut(c, 16, 0.52f));
    // EVEN made the lane's own: the same places, as cuts.
    const LaneCuts own = slices::ownCuts(even, 16);
    CHECK(own.count == 16 && own.sense == 0 && own.at[5] == 5.f / 16.f && slices::ownCuts(c, 16) == c);
    CHECK(slices::ownCuts(even, 128).count == kMaxLaneCuts);
    // From outside: held rising and inside, the first the top, the rest 0.
    LaneCuts in;
    in.count = 5, in.sense = 4;
    in.at[0] = 0.3f, in.at[1] = 0.2f, in.at[2] = 0.4f, in.at[3] = 0.3f, in.at[4] = 0.9f;
    LaneCuts held = slices::cutsHeld(in);
    CHECK(held.count == 3 && held.sense == 4 && held.at[0] == 0.f && held.at[1] == 0.2f && held.at[2] == 0.4f && held.at[3] == 0.f && held.at[4] == 0.f);
    in.at[2] = 0.2f; // the same place twice: ended there
    CHECK(slices::cutsHeld(in).count == 2);
    in.at[1] = 1.f;
    CHECK(slices::cutsHeld(in).count == 1);
    in.at[1] = std::nanf("");
    CHECK(slices::cutsHeld(in).count == 1);
    in.sense = 11;
    CHECK(slices::cutsHeld(in).sense == 0);
    in.count = 0, in.sense = 4;
    CHECK(slices::cutsHeld(in).count == 0 && slices::cutsHeld(in).sense == 0);
    // Alike is alike past `count` too: what lies beyond is not looked at.
    LaneCuts a = c, b = c;
    b.at[10] = 0.7f;
    CHECK(a == b);
    b.at[2] = 0.51f;
    CHECK(a != b);
}

// ---- the engine -----------------------------------------------------------------------------------

Command make(Command::Type type, int a = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.a = a;
    c.value = value;
    return c;
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    std::vector<float> data; // lane 0's recording: each sample its own place, so a sound says where it was read
    Rig() {
        data.resize(48000);
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = 0.1f + 0.8f * static_cast<float>(i) / 48000.f;
        std::unique_ptr<SampleData> sample(new SampleData);
        sample->data = data;
        engine->setLaneSample(0, 0, std::move(sample));
    }
    void send(const Command& c) { CHECK(engine->send(c)); }
    void cuts(const std::vector<float>& at) { // through the command, three a go
        const int n = static_cast<int>(at.size());
        Command c = make(Command::SetLaneCuts, 0);
        c.c = n;
        if (n == 0) send(c);
        for (int from = 0; from < n; from += 3) {
            c.b = from;
            c.d = n - from < 3 ? n - from : 3;
            c.value = at[static_cast<std::size_t>(from)];
            c.extra = from + 1 < n ? at[static_cast<std::size_t>(from + 1)] : 0.f;
            c.shift = from + 2 < n ? at[static_cast<std::size_t>(from + 2)] : 0.f;
            send(c);
        }
    }
    std::vector<float> hit(float start, int reverse = 0, int frames = 60000) {
        Command t = make(Command::Trigger, 0, 1.f);
        t.b = static_cast<int>(std::lround(start * 10000.f));
        t.c = -10000; // (the lane's own part)
        t.scene = reverse;
        send(t);
        std::vector<float> both(2 * static_cast<std::size_t>(frames)), left(static_cast<std::size_t>(frames));
        engine->render(both.data(), frames);
        for (int i = 0; i < frames; ++i) left[static_cast<std::size_t>(i)] = both[2 * static_cast<std::size_t>(i)];
        return left;
    }
};

int heard(const std::vector<float>& out) {
    int last = -1;
    for (std::size_t i = 0; i < out.size(); ++i)
        if (out[i] != 0.f) last = static_cast<int>(i);
    return last + 1;
}

void testTheEngine() {
    // How the audition's part is read: a Trigger's c is the part in ten-thousandths, below zero the lane's.
    {
        Rig rig;
        const int all = heard(rig.hit(0.f));
        CHECK(all >= 47990 && all <= 48000); // the whole recording
    }
    Rig rig;
    rig.send(make(Command::SetLanePart, 0, 1.f / 16.f)); // ONE SLICE, even: a sixteenth
    int frames = heard(rig.hit(0.25f));
    CHECK(frames >= 3000 && frames <= 3000 + 160);
    // The lane's own cuts: from 0.25 to the next, 0.6 (16800 frames), then its fade.
    rig.cuts({0.f, 0.1f, 0.25f, 0.6f});
    frames = heard(rig.hit(0.25f));
    CHECK(frames >= 16800 && frames <= 16800 + 160);
    // From between two cuts: to the next one. From the last: to the end.
    frames = heard(rig.hit(0.3f));
    CHECK(frames >= 14400 && frames <= 14400 + 160);
    frames = heard(rig.hit(0.6f));
    CHECK(frames >= 19190 && frames <= 19201); // (a start heard by hand is in ten-thousandths: a sample short)
    // A start a few samples short of a cut is on it (as one heard by hand comes): to the cut after.
    {
        Command t = make(Command::Trigger, 0, 1.f);
        t.b = 2500;
        t.c = -10000;
        rig.send(t);
        std::vector<float> both(120000);
        rig.engine->render(both.data(), 60000);
        int last = -1;
        for (int i = 0; i < 60000; ++i)
            if (both[2 * static_cast<std::size_t>(i)] != 0.f) last = i;
        CHECK(last + 1 >= 16790 && last + 1 <= 16800 + 160);
    }
    // ...but one 2 ms short of a cut ends on it.
    frames = heard(rig.hit(0.6f - 0.002f));
    CHECK(frames >= 96 && frames <= 96 + 160);
    // From the top: to the first cut past it.
    frames = heard(rig.hit(0.f));
    CHECK(frames >= 4800 && frames <= 4800 + 160);
    // Backwards: from the end of its slice to its start (the fade before it).
    const std::vector<float> back = rig.hit(0.25f, 1);
    CHECK(heard(back) >= 16800 - 160 && heard(back) <= 16800 + 160);
    // ...read from the slice's end down: two samples of it stand as the recording's do.
    CHECK(std::fabs(back[200] / back[1200] - rig.data[static_cast<std::size_t>(28800 - 1 - 200)] / rig.data[static_cast<std::size_t>(28800 - 1 - 1200)]) < 1e-4f);
    // RUNS ON: the cuts change nothing (the hit plays on to the end).
    rig.send(make(Command::SetLanePart, 0, 0.f));
    frames = heard(rig.hit(0.25f));
    CHECK(frames >= 35990 && frames <= 36000);
    // A part given by the hit (an audition) wins over the cuts.
    rig.send(make(Command::SetLanePart, 0, 1.f / 4.f));
    Command t = make(Command::Trigger, 0, 1.f);
    t.b = 2500;
    t.c = 500; // a twentieth
    rig.send(t);
    {
        std::vector<float> both(120000);
        rig.engine->render(both.data(), 60000);
        int last = -1;
        for (int i = 0; i < 60000; ++i)
            if (both[2 * static_cast<std::size_t>(i)] != 0.f) last = i;
        CHECK(last + 1 >= 2400 && last + 1 <= 2400 + 160);
    }
    // Cuts taken away: the even share again.
    rig.cuts({});
    frames = heard(rig.hit(0.25f));
    CHECK(frames >= 12000 && frames <= 12000 + 160);
    // Nonsense in a command: out of order or out of range ends nothing early; a lane out of range is nothing.
    Command bad = make(Command::SetLaneCuts, 0);
    bad.c = 3, bad.b = 1, bad.d = 2, bad.value = std::nanf(""), bad.extra = 2.f;
    rig.send(bad);
    frames = heard(rig.hit(0.25f));
    CHECK(frames >= 35990 && frames <= 36000);
    bad.a = 9;
    rig.send(bad);
    bad.a = -1;
    rig.send(bad);
    DrumMachine machine;
    machine.setLaneCuts(-1, 3, 0, 3, nullptr);
    machine.setLaneCuts(8, 3, 0, 3, nullptr);
}

// No cuts: the sound as it was, to the bit, through a pattern of ONE SLICE hits.
void testTheSameToTheBit() {
    std::vector<float> outs[2];
    for (int run = 0; run < 2; ++run) {
        Rig rig;
        rig.send(make(Command::SetLanePart, 0, 1.f / 16.f));
        if (run == 1) rig.cuts({}); // told "none", as a song sent is
        Command tempo = make(Command::SetTempo, 0, 120.f);
        rig.send(tempo);
        for (int step = 0; step < 16; step += 3) {
            Command c = make(Command::SetStep, 0, 0.9f);
            c.b = step;
            rig.send(c);
            Command s = make(Command::SetStepStart, 0, static_cast<float>((step * 5) % 16) / 16.f);
            s.b = step;
            rig.send(s);
        }
        rig.send(make(Command::Play));
        std::vector<float> both(2 * 100000);
        rig.engine->render(both.data(), 100000);
        outs[run] = both;
    }
    CHECK(outs[0] == outs[1]);
    CHECK(heard(outs[0]) > 0);
}

void testATrackTakenAway() {
    Rig rig;
    CHECK(rig.engine->addDrumTrack(2));
    const auto onTwo = [&](Command c) { c.track = 2; return c; };
    const auto give = [&] {
        std::unique_ptr<SampleData> sample(new SampleData);
        sample->data = rig.data;
        CHECK(rig.engine->setLaneSample(2, 0, std::move(sample)));
        rig.send(onTwo(make(Command::SetLanePart, 0, 1.f / 16.f)));
    };
    const auto hitTwo = [&] {
        Command t = onTwo(make(Command::Trigger, 0, 1.f));
        t.c = -10000;
        rig.send(t);
        std::vector<float> both(2 * 60000), left(60000);
        rig.engine->render(both.data(), 60000);
        for (int i = 0; i < 60000; ++i) left[static_cast<std::size_t>(i)] = both[2 * static_cast<std::size_t>(i)];
        return heard(left);
    };
    give();
    Command c = onTwo(make(Command::SetLaneCuts, 0, 0.f));
    c.c = 2, c.b = 0, c.d = 2, c.value = 0.f, c.extra = 0.5f;
    rig.send(c);
    CHECK(hitTwo() >= 24000);
    CHECK(rig.engine->removeTrack(2));
    std::vector<float> both(2 * 4096);
    rig.engine->render(both.data(), 4096);
    rig.engine->collectGarbage();
    CHECK(rig.engine->addDrumTrack(2));
    give();
    const int frames = hitTwo();
    CHECK(frames >= 3000 && frames <= 3000 + 160); // the even share: the old cuts went with the track
}

void measure() {
    std::vector<Hit> hits;
    for (int bar = 0; bar < 10; ++bar)
        for (const Hit& h : kBreak) hits.push_back({h.kind, bar * 2.0 + h.seconds, h.gain});
    const std::vector<float> sound = loop(20.0, hits, 0.003f);
    double best = 1e9;
    std::size_t found = 0;
    for (int run = 0; run < 7; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const std::vector<slices::Found> f = slices::findHits(sound.data(), static_cast<int>(sound.size()), kRate);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        best = ms < best ? ms : best;
        found = f.size();
    }
    const std::vector<slices::Found> f = slices::findHits(sound.data(), static_cast<int>(sound.size()), kRate);
    double pick = 1e9;
    for (int run = 0; run < 7; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const LaneCuts c = slices::cutsAt(f, static_cast<int>(sound.size()), 5, kRate);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        pick = ms < pick ? ms : pick;
        if (run == 0) std::printf("a 20 s loop of %d hits: %d slices at SENSITIVITY 5\n", static_cast<int>(hits.size()), c.count);
    }
    std::printf("finding the hits in 20 s: %.2f ms (best of 7, %d candidates); a SENSITIVITY picked from them: %.3f ms\n", best, static_cast<int>(found), pick);
    // The audio thread's part: a hit of ONE SLICE on a lane of 64 cuts of its own (it looks for the next cut
    // past its start, here the last: the most looking there is) against the even share, a million hits each.
    std::vector<float> recording(960000, 0.1f);
    const DrumKit& kit = DrumKit::builtIn(48000.f);
    float at[3];
    double perHit[2] = {1e9, 1e9};
    for (int run = 0; run < 5; ++run)
        for (int cuts = 0; cuts < 2; ++cuts) {
            DrumMachine machine;
            machine.setKit(&kit);
            machine.prepare(48000.f);
            machine.setLaneSample(0, recording.data(), static_cast<int>(recording.size()));
            machine.setLanePart(0, 1.f / 64.f);
            if (cuts)
                for (int from = 0; from < 64; from += 3) {
                    for (int i = 0; i < 3; ++i) at[i] = static_cast<float>(from + i) / 64.f;
                    machine.setLaneCuts(0, 64, from, 64 - from < 3 ? 64 - from : 3, at);
                }
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 1000000; ++i) machine.trigger(0, 0.9f, (i & 1) ? 63.f / 64.f : 62.f / 64.f);
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 1e6;
            perHit[cuts] = ns < perHit[cuts] ? ns : perHit[cuts];
        }
    std::printf("a hit of ONE SLICE: %.1f ns even, %.1f ns to the next of 64 cuts (best of 5, a million hits each)\n", perHit[0], perHit[1]);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
        measure();
        return 0;
    }
    testACleanLoop();
    testUnderNoise();
    testASoftHitAfterALoudOne();
    testSensitivity();
    testGapsAndTheTop();
    testTheMostAndNothing();
    testTheHelpers();
    testTheEngine();
    testTheSameToTheBit();
    testATrackTakenAway();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("slices tests passed\n");
    return 0;
}
