// The JX-3P's chorus (plan batch 5, engine/src/jx3p/chorus.hpp): its table, its
// two delays and their sweep found by impulses through it, its 10 kHz filters,
// off as the dry sound, the fade, its sleep and its waking, blocks, and Play
// from the top through the synth.
//
// Each test says FAITHFUL (held to a published figure: its source named) or
// SELF (held to the plan's own guess). The Service Notes give the BBD's clock
// (40 to 200 kHz), the 180 degrees and the 10 kHz filters (p.7); nothing gives
// the sweep's rate, its shape, the mix or the filters' poles: those are SELF.
//
//   jx3p_chorus_test                  all of them
//   jx3p_chorus_test testSweep ...    just those
//   JX3P_PRINT=1 jx3p_chorus_test     with the figures

#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/jx3p.hpp"
#include "jx3p/chorus.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::jx3p;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static bool printing() { return failures || std::getenv("JX3P_PRINT"); }

// The chorus alone at 48 kHz, on (or off), where its switch is.
static std::unique_ptr<BbdChorus> chorus(bool on = true, double rate = 48000.0) {
    std::unique_ptr<BbdChorus> c(new BbdChorus);
    c->on = on;
    c->prepare(rate);
    return c;
}
// `in` through it from its sample `clock`, in blocks of `block` (0: at random): left and right.
static void through(BbdChorus& c, const std::vector<float>& in, std::vector<float>& l, std::vector<float>& r, int block = 64, std::uint64_t clock = 0) {
    l = in;
    r.assign(in.size(), 99.f);
    std::uint32_t seed = 5u;
    for (std::size_t done = 0; done < in.size();) {
        std::size_t n = static_cast<std::size_t>(block);
        if (block == 0) {
            seed = seed * 1664525u + 1013904223u;
            n = 1 + (seed >> 8) % 600u;
        }
        if (n > in.size() - done) n = in.size() - done;
        c.run(&l[done], &r[done], static_cast<int>(n), clock + done);
        done += n;
    }
}
// What a side's BBD gave, taken out of the side (on at full: dry x kChorusDry plus wet x kChorusWet).
static std::vector<float> wetOf(const std::vector<float>& side, const std::vector<float>& in) {
    std::vector<float> w(side.size());
    for (std::size_t i = 0; i < w.size(); ++i) w[i] = (side[i] - kChorusDry * in[i]) / kChorusWet;
    return w;
}
static std::vector<float> noise(std::size_t n, std::uint32_t seed = 3u, float amp = 0.5f) {
    std::vector<float> v(n);
    for (float& x : v) {
        seed = seed * 1664525u + 1013904223u;
        x = amp * (static_cast<float>(seed >> 8) / 8388608.f - 1.f);
    }
    return v;
}

// THE TABLE: an MN3009's 256 stages at 40 and 200 kHz are 3.2 and 0.64 ms (FAITHFUL: SN p.7's clock,
// the MN3009's stages); at 48 kHz 153.6 and 30.72 samples, the line long enough for the longest read;
// the fade 20 ms (960 samples) and the sweep's cycle 2 s (SELF).
static void testTheTable() {
    CHECK(std::fabs(bbdDelaySeconds(kBbdClockLowHz) - 3.2e-3) < 1e-12 && std::fabs(bbdDelaySeconds(kBbdClockHighHz) - 0.64e-3) < 1e-12);
    std::unique_ptr<BbdChorus> c = chorus();
    CHECK(std::fabs(c->delayHigh - 153.6f) < 1e-3f && std::fabs(c->delayLow - 30.72f) < 1e-3f);
    CHECK(c->reach == 157 && c->reach < kChorusLine && c->fadeLength == 960 && c->period == 96000u);
    std::unique_ptr<BbdChorus> d = chorus(true, 44100.0);
    CHECK(d->fadeLength == 882 && d->period == 88200u && std::fabs(d->delayHigh - 141.12f) < 1e-3f);
}

// THE SWEEP, AS WORKED OUT (FAITHFUL, SN p.7: the clock from 40 to 200 kHz, the other side 180 degrees
// out): over one cycle each side's delay stays within 0.64 to 3.2 ms and reaches both ends; the two
// clocks always add to 240 kHz; the left is longest where the right is shortest. The shape (a
// triangle of the clock) and the rate are SELF.
static void testSweep() {
    std::unique_ptr<BbdChorus> c = chorus();
    float lo = 1e9f, hi = 0.f, worstSum = 0.f;
    std::uint32_t longestL = 0, shortestR = 0;
    float bestL = 0.f, bestR = 1e9f;
    for (std::uint32_t at = 0; at < c->period; ++at) {
        float l, r;
        c->delaysAt(at, l, r);
        lo = std::fmin(lo, std::fmin(l, r)), hi = std::fmax(hi, std::fmax(l, r));
        worstSum = std::fmax(worstSum, std::fabs(c->delayK / l + c->delayK / r - 240000.f) / 240000.f);
        if (l > bestL) bestL = l, longestL = at;
        if (r < bestR) bestR = r, shortestR = at;
    }
    if (printing()) std::printf("  sweep: %.3f to %.3f samples (%.4f to %.4f ms), clocks add to 240 kHz within %.2g\n", lo, hi, lo / 48.0, hi / 48.0, worstSum);
    CHECK(lo >= 30.72f - 1e-3f && hi <= 153.6f + 1e-3f && lo < 30.72f + 1e-3f && hi > 153.6f - 1e-3f);
    CHECK(worstSum < 1e-6f);
    CHECK(longestL == shortestR && longestL == 0u);
    // A triangle of the clock (SELF): a quarter of the way round the left's clock is half way, 120 kHz.
    float l, r;
    c->delaysAt(c->period / 4, l, r);
    CHECK(std::fabs(c->delayK / l - 120000.f) < 1.f && std::fabs(l - r) < 1e-3f);
}

// THE DELAYS, FOUND BY IMPULSES THROUGH IT (FAITHFUL, SN p.7, as testSweep; the rate SELF): an impulse
// every 2400 samples for 2 s and a half, each side's wet peak found (between samples, a parabola),
// less the two filters' own lateness, is the delay worked out for the sample it comes OUT on within a
// quarter of a sample (a BBD's delay is its clock's when the sound leaves it: at 40 kHz it shortens
// 0.0128 of a sample a sample, so an impulse in at 3.2 ms comes out 3.16 ms later); the left at its
// longest about 0 and 2 s and its shortest about 1 s, the right the other way round.
static void testImpulses() {
    // The two filters' own lateness: an impulse through one and then another.
    std::unique_ptr<BbdChorus> c = chorus();
    LowPass2 a, b;
    std::vector<float> f(64);
    for (int i = 0; i < 64; ++i) f[static_cast<std::size_t>(i)] = b.run(a.run(i == 0 ? 1.f : 0.f, c->coef), c->coef);
    const auto peakAt = [](const std::vector<float>& v, std::size_t from, std::size_t to) {
        std::size_t best = from;
        for (std::size_t i = from; i < to; ++i)
            if (v[i] > v[best]) best = i;
        const double y0 = v[best - 1], y1 = v[best], y2 = v[best + 1];
        return static_cast<double>(best) + 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
    };
    const double late = peakAt(f, 1, 60);
    const std::size_t n = 120000, gap = 2400;
    std::vector<float> in(n, 0.f), l, r;
    for (std::size_t t = 0; t < n; t += gap) in[t] = 1.f;
    through(*c, in, l, r);
    const std::vector<float> wl = wetOf(l, in), wr = wetOf(r, in);
    std::unique_ptr<BbdChorus> ref = chorus();
    double worst = 0.0;
    for (std::size_t t = 0; t + gap <= n; t += gap) {
        const double gotL = peakAt(wl, t + 20, t + 200) - static_cast<double>(t) - late;
        const double gotR = peakAt(wr, t + 20, t + 200) - static_cast<double>(t) - late;
        float dl, dr, unused;
        ref->delaysAt(static_cast<std::uint32_t>(static_cast<std::size_t>(std::lround(t + gotL)) % ref->period), dl, unused);
        ref->delaysAt(static_cast<std::uint32_t>(static_cast<std::size_t>(std::lround(t + gotR)) % ref->period), unused, dr);
        worst = std::fmax(worst, std::fmax(std::fabs(gotL - dl), std::fabs(gotR - dr)));
        if (t == 0 || t == 48000 || t == 96000) {
            if (printing()) std::printf("  at %.1f s: left %.2f ms, right %.2f ms (by impulse)\n", t / 48000.0, gotL / 48.0, gotR / 48.0);
            const bool longLeft = t != 48000;
            CHECK((longLeft ? gotL : gotR) > 150.0 && (longLeft ? gotR : gotL) < 31.0);
        }
    }
    if (printing()) std::printf("  delays by impulse against those worked out: %.3f samples at worst (the filters %.2f late)\n", worst, late);
    CHECK(worst < 0.25);
}

// THE 10 KHZ FILTERS (FAITHFUL, SN p.7: "both have cutoff frequency at 10kHz"; their two poles SELF):
// each is 3 dB down at 10 kHz and flat at 1 kHz; the wet path, through the one before the line and
// the one after, is 6 dB down at 10 kHz against 1 kHz within 1 dB (the cubic read takes a little
// more, averaged over a whole sweep), and falls on above it.
static void testTenKilohertz() {
    std::unique_ptr<BbdChorus> c = chorus();
    const auto alone = [&](double hz) {
        LowPass2 f;
        const std::vector<float> in = sineOf(hz, 0.5, 48000);
        std::vector<float> out(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) out[i] = f.run(in[i], c->coef);
        return dB(rmsOf(out, 4800) / rmsOf(in, 4800));
    };
    const double at1 = alone(1000.0), at10 = alone(10000.0), at20 = alone(20000.0);
    if (printing()) std::printf("  one filter: %.3f dB at 1 kHz, %.3f at 10 kHz, %.2f at 20 kHz\n", at1, at10, at20);
    CHECK(std::fabs(at1) < 0.05 && std::fabs(at10 + 3.01) < 0.05 && at20 < -15.0);
    const auto wet = [&](double hz) {
        std::unique_ptr<BbdChorus> w = chorus();
        const std::vector<float> in = sineOf(hz, 0.5, 96000 + 4800);
        std::vector<float> l, r;
        through(*w, in, l, r);
        return std::make_pair(rmsOf(wetOf(l, in), 4800), rmsOf(wetOf(r, in), 4800));
    };
    const auto w1 = wet(1000.0), w10 = wet(10000.0), w15 = wet(15000.0);
    const double left = dB(w10.first / w1.first), right = dB(w10.second / w1.second), top = dB(w15.first / w1.first);
    if (printing()) std::printf("  the wet path at 10 kHz against 1 kHz: %.2f dB left, %.2f right; at 15 kHz %.2f\n", left, right, top);
    CHECK(std::fabs(left + 6.02) < 1.0 && std::fabs(right + 6.02) < 1.0 && top < left - 6.0);
    // At 1 kHz, each side less kChorusDry of the dry sound is kChorusWet of it again, delayed: as loud.
    const double amp = 0.5 / std::sqrt(2.0);
    if (printing()) std::printf("  the wet at 1 kHz: %.3f dB left, %.3f right, against what went in\n", dB(w1.first / amp), dB(w1.second / amp));
    CHECK(std::fabs(dB(w1.first / amp)) < 0.1 && std::fabs(dB(w1.second / amp)) < 0.1);
}

// OFF IS THE DRY SOUND, MONO (SELF): a synth with CHORUS off gives the same left and right, to the bit;
// on, they differ. And the chorus faded out gives back its input untouched.
static void testOff() {
    for (int on : {0, 1}) {
        std::unique_ptr<Jx3p> j(new Jx3p);
        j->setSetting(Chorus, on);
        for (int k : {48, 55, 60, 64}) j->noteOn(k, 1.f);
        std::vector<float> l(48000), r(48000);
        j->render(l.data(), r.data(), 48000);
        CHECK(peakOf(l) > 0.05f && (l == r) == (on == 0));
    }
    std::unique_ptr<BbdChorus> c = chorus();
    const std::vector<float> in = noise(4000);
    std::vector<float> l, r;
    through(*c, in, l, r);
    c->on = false;
    through(*c, in, l, r, 64, 4000); // its fade out, then dry
    CHECK(!c->active() && c->idle());
    CHECK(std::vector<float>(l.begin() + 960, l.end()) == std::vector<float>(in.begin() + 960, in.end()));
    CHECK(std::vector<float>(r.begin() + 960, r.end()) == std::vector<float>(in.begin() + 960, in.end()));
    CHECK(std::vector<float>(l.begin(), l.begin() + 959) != std::vector<float>(in.begin(), in.begin() + 959));
}

// THE SWITCH FADES OVER 20 MS AND LANDS EXACTLY (SELF): on, the wet comes in over 960 samples, then
// the side is the steady mix; switched off and on, a sine through it has no step larger than a
// chorused sine's own. Through the synth: before its first sample a switch is there at once.
static void testFade() {
    std::unique_ptr<BbdChorus> c = chorus(false);
    c->on = true;
    CHECK(c->fading() && c->fade == 0);
    const std::vector<float> in = sineOf(440.0, 0.5, 48000);
    std::vector<float> l, r;
    std::vector<float> part(in.begin(), in.begin() + 959);
    through(*c, part, l, r);
    CHECK(c->fading() && c->fade == 959);
    part.assign(in.begin() + 959, in.begin() + 960);
    through(*c, part, l, r, 64, 959);
    CHECK(!c->fading() && c->fade == c->fadeLength);
    // Off and on again while a sine plays: no click.
    std::unique_ptr<BbdChorus> d = chorus();
    std::vector<float> out, ls, rs;
    for (int k = 0; k < 6; ++k) {
        d->on = k % 2 == 0;
        part.assign(in.begin() + k * 6000, in.begin() + (k + 1) * 6000);
        through(*d, part, ls, rs, 64, static_cast<std::uint64_t>(k) * 6000u);
        out.insert(out.end(), ls.begin(), ls.end());
    }
    std::unique_ptr<BbdChorus> steady = chorus();
    through(*steady, std::vector<float>(in.begin(), in.begin() + 36000), ls, rs);
    const float own = largestJump(ls, 200), jump = largestJump(out, 200);
    if (printing()) std::printf("  switched while a sine plays: largest step %.4f (the chorused sine's own %.4f)\n", jump, own);
    CHECK(jump <= own * 1.05f);
    // The synth: CHORUS turned off before its first sample is off from it (no wet fading out).
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->setSetting(Chorus, 0);
    j->noteOn(60, 1.f);
    std::vector<float> jl(2000), jr(2000);
    j->render(jl.data(), jr.data(), 2000);
    CHECK(jl == jr);
}

// IT SLEEPS, AND WAKES AS A FRESH ONE (SELF, the house's rules): after a sound, fed silence, it is
// idle within its longest read and its filters' ring, puts out exact zeros, and makes no number too
// small to hold (flush-to-zero off). One that heard a sound long ago gives the same bits for a new
// sound as one that never heard anything (its sweep runs from the clock, slept or not).
static void testSleep() {
    std::unique_ptr<BbdChorus> c = chorus();
    std::vector<float> in = noise(4800);
    in.resize(4800 + 4800, 0.f);
    std::vector<float> l, r;
    std::feclearexcept(FE_UNDERFLOW);
    through(*c, in, l, r, 1);
    CHECK(!std::fetestexcept(FE_UNDERFLOW));
    int idleAt = -1;
    {
        std::unique_ptr<BbdChorus> d = chorus();
        for (std::size_t i = 0; i < in.size() && idleAt < 0; ++i) {
            float x = in[i], y;
            d->run(&x, &y, 1, i);
            if (i >= 4800 && d->idle()) idleAt = static_cast<int>(i) - 4800;
        }
    }
    if (printing()) std::printf("  asleep %d samples after the last sound\n", idleAt);
    CHECK(c->idle() && idleAt > 150 && idleAt < 400);
    CHECK(peakOf(std::vector<float>(l.begin() + 4800 + idleAt, l.end())) == 0.f && peakOf(std::vector<float>(r.begin() + 4800 + idleAt, r.end())) == 0.f);
    // Woken: the same bits as one that heard nothing.
    const std::vector<float> later = noise(9600, 11u);
    std::unique_ptr<BbdChorus> fresh = chorus();
    std::vector<float> a, ar, b, br;
    through(*c, later, a, ar, 64, 30000);
    through(*fresh, later, b, br, 64, 30000);
    CHECK(a == b && ar == br);
}

// NOTHING UNDER kChorusQuiet GOES ON THE LINE (SELF, the house's rule for a delay, as PING's): a
// sound 280 dB down comes out as its dry part alone, the line stays empty (exact zeros), and so
// nothing of it is delayed.
static void testUnderQuiet() {
    std::unique_ptr<BbdChorus> c = chorus();
    const std::vector<float> in = noise(4800, 9u, 1e-14f);
    std::vector<float> l, r;
    through(*c, in, l, r);
    bool dryOnly = true;
    for (std::size_t i = 0; i < in.size(); ++i) dryOnly = dryOnly && l[i] == kChorusDry * in[i] && r[i] == kChorusDry * in[i];
    CHECK(dryOnly && c->quietRun == c->reach);
    for (float x : c->line) CHECK(x == 0.f);
}

// A FADE LANDED AT ONCE (snap: a fresh synth, allNotesOff) EMPTIES A CHORUS SWITCHED OFF (SELF): one
// cut off half way through its fade out and then switched on again gives the same bits as a fresh
// one switched on. Through the synth: allNotesOff half way through a fade out leaves it off at once
// (the same left and right from that sample).
static void testSnap() {
    std::unique_ptr<BbdChorus> c = chorus(), fresh = chorus(false);
    const std::vector<float> in = noise(4800, 21u), next = noise(4800, 22u);
    std::vector<float> l, r, a, ar, b, br;
    through(*c, in, l, r);
    c->on = false;
    through(*c, std::vector<float>(in.begin(), in.begin() + 300), l, r, 64, 4800);
    c->snap();
    CHECK(!c->active());
    c->on = true, fresh->on = true;
    through(*c, next, a, ar, 64, 9000);
    through(*fresh, next, b, br, 64, 9000);
    CHECK(a == b && ar == br);
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->setSetting(Release, 0);
    j->noteOn(60, 1.f);
    std::vector<float> jl(4800), jr(4800);
    j->render(jl.data(), jr.data(), 4800);
    j->setSetting(Chorus, 0);
    j->render(jl.data(), jr.data(), 300); // half way through its fade out
    j->allNotesOff();
    j->noteOn(64, 1.f);
    j->render(jl.data(), jr.data(), 4800);
    CHECK(jl == jr && peakOf(jl) > 0.05f);
}

// THE SWITCH FADES THROUGH THE SYNTH, AND A SWITCH IN SILENCE IS DONE BY THE NEXT SOUND (SELF): a held
// note, CHORUS off: the two sides differ for the 20 ms of the fade and are one from its end. And a
// chorus switched off while the synth is silent (asleep) has faded out before the next key: that
// key is mono from its first sample, however long the silence was cut into blocks.
static void testSwitchInTheSynth() {
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->setSetting(Sustain, 100);
    j->noteOn(57, 1.f);
    std::vector<float> l(4800), r(4800);
    j->render(l.data(), r.data(), 4800);
    j->setSetting(Chorus, 0);
    j->render(l.data(), r.data(), 4800);
    CHECK(std::vector<float>(l.begin(), l.begin() + 900) != std::vector<float>(r.begin(), r.begin() + 900));
    CHECK(std::vector<float>(l.begin() + 960, l.end()) == std::vector<float>(r.begin() + 960, r.end()));
    for (int block : {1, 64, 2000}) {
        std::unique_ptr<Jx3p> k(new Jx3p);
        k->setSetting(Release, 0);
        k->noteOn(60, 1.f);
        std::vector<float> kl(48000), kr(48000);
        k->render(kl.data(), kr.data(), 4800);
        k->noteOff(60);
        k->render(kl.data(), kr.data(), 48000); // rung out, the chorus asleep
        CHECK(peakOf(std::vector<float>(kl.end() - 4800, kl.end())) == 0.f);
        k->setSetting(Chorus, 0);
        for (int done = 0; done < 2000; done += block) k->render(kl.data(), kr.data(), block);
        k->noteOn(64, 1.f);
        k->render(kl.data(), kr.data(), 4800);
        CHECK(std::vector<float>(kl.begin(), kl.begin() + 4800) == std::vector<float>(kr.begin(), kr.begin() + 4800));
    }
}

// THE SAME SOUND HOWEVER THE BLOCKS ARE CUT (SELF, a house rule): noise through it with the switch
// turned three times, cut 1, 64, 100 and at random: the same bits.
static void testBlocks() {
    const std::vector<float> in = noise(60000, 17u);
    const auto cut = [&](int block, std::vector<float>& r) {
        std::unique_ptr<BbdChorus> c = chorus();
        std::vector<float> all, allR, l, rr;
        for (int k = 0; k < 4; ++k) {
            c->on = k % 2 == 0;
            const std::vector<float> part(in.begin() + k * 15000, in.begin() + (k + 1) * 15000);
            through(*c, part, l, rr, block, static_cast<std::uint64_t>(k) * 15000u);
            all.insert(all.end(), l.begin(), l.end());
            allR.insert(allR.end(), rr.begin(), rr.end());
        }
        r = allR;
        return all;
    };
    std::vector<float> r0, r;
    const std::vector<float> whole = cut(64, r0);
    for (int block : {1, 100, 0}) CHECK(cut(block, r) == whole && r == r0);
}

// PLAY FROM THE TOP WITH THE CHORUS ON (SELF: plan section 2): six keys ringing with a long release,
// allNotesOff, two keys: once the tails are gone, the same bits as a fresh synth's two keys (the
// sweep starts again with the clock, and what the tails left on the line runs out). The tails
// themselves hear the sweep start again, as they hear the LFO (one chorus is the machine's).
static void testFromTheTop() {
    std::unique_ptr<Jx3p> a(new Jx3p), b(new Jx3p);
    for (Jx3p* j : {a.get(), b.get()}) j->setSetting(Release, 70);
    std::vector<float> l(4800), r(4800);
    for (int n = 0; n < 6; ++n) a->noteOn(50 + n, 0.8f);
    a->render(l.data(), r.data(), 4800);
    a->allNotesOff();
    CHECK(a->tailsRinging() == 6);
    for (Jx3p* j : {a.get(), b.get()}) j->noteOn(64, 1.f), j->noteOn(67, 1.f);
    const int n = 4 * 48000;
    std::vector<float> la(n), ra(n), lb(n), rb(n);
    a->render(la.data(), ra.data(), n);
    b->render(lb.data(), rb.data(), n);
    CHECK(a->tailsRinging() == 0 && la != lb);
    CHECK(std::vector<float>(la.end() - 48000, la.end()) == std::vector<float>(lb.end() - 48000, lb.end()));
    CHECK(std::vector<float>(ra.end() - 48000, ra.end()) == std::vector<float>(rb.end() - 48000, rb.end()));
}

// ABOUT AS LOUD ON AS OFF (SELF: kChorusDry and kChorusWet are a guess): a held chord of saws, each
// side on against off within 1.5 dB.
static void testLevel() {
    double rms[2] = {0.0, 0.0}, rmsR = 0.0;
    for (int on : {0, 1}) {
        std::unique_ptr<Jx3p> j(new Jx3p);
        j->setSetting(Chorus, on);
        j->setSetting(Sustain, 100);
        for (int k : {48, 55, 60, 64}) j->noteOn(k, 1.f);
        std::vector<float> l(96000), r(96000);
        j->render(l.data(), r.data(), 96000);
        rms[on] = rmsOf(l, 4800);
        if (on) rmsR = rmsOf(r, 4800);
    }
    if (printing()) std::printf("  a chord of saws on against off: left %.2f dB, right %.2f dB\n", dB(rms[1] / rms[0]), dB(rmsR / rms[0]));
    CHECK(std::fabs(dB(rms[1] / rms[0])) < 1.5 && std::fabs(dB(rmsR / rms[0])) < 1.5);
}

int main(int argc, char** argv) {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"testTheTable", testTheTable}, {"testSweep", testSweep}, {"testImpulses", testImpulses}, {"testTenKilohertz", testTenKilohertz},
        {"testOff", testOff}, {"testFade", testFade}, {"testSleep", testSleep}, {"testBlocks", testBlocks},
        {"testFromTheTop", testFromTheTop}, {"testLevel", testLevel}, {"testUnderQuiet", testUnderQuiet}, {"testSnap", testSnap},
        {"testSwitchInTheSynth", testSwitchInTheSynth},
    };
    bool any = false;
    for (int a = 1; a < argc; ++a)
        for (const Test& t : tests)
            if (std::string(argv[a]) == t.name) t.run(), any = true;
    if (!any)
        for (const Test& t : tests) t.run();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("jx3p chorus tests passed\n");
    return 0;
}
