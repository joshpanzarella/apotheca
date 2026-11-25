// PITCH: a pitch shifter, a TYPE of MOD (owner, 2026-11-24: "Let's do ... pitch shifter"). The sound again,
// higher or lower, beside itself or instead of itself: an octave down under a bass, a fifth over a lead.
//
//   PITCH  -12 to +12 semitones, a click each (a choice: the D-pad steps it).
//   FINE   -50 to +50 cents on top: a little either way thickens it (a doubler).
//   GRAIN  how long each piece read is, 20 to 120 ms: short is quick and buzzes on low notes; long is smooth
//          and blurs fast playing.
//   MIX    0 is the sound untouched, to the sample; full is the shifted sound alone.
//
// How: the sound goes into a short line, and a head reads it back at the new speed (read between samples: the
// cubic, glide.hpp). Shifting up, the head catches up with the sound; down, it falls behind. When it has gone a
// GRAIN it is spliced: the second head starts a GRAIN back (or on), the two cross in 5 ms (kFadeSeconds, a smooth
// S whose two halves add to one), and the second plays on alone.
//
// MATCHED SPLICES (2026-11-24, after 0.877 shipped two heads half a GRAIN apart with triangle fades): where the
// second head starts is chosen up to 25 ms past the GRAIN (kReachSeconds, a cycle of 40 Hz), at the place whose
// sound best matches what the first is playing, so the two cross in step. Fixed half a GRAIN apart they crossed out
// of step on some notes: 35 of 216 held notes (eight shifts, 80 Hz to 1.6 kHz) dipped more than 6 dB, 17 to 33 times
// a second, the worst 17.8 dB at 1016 Hz, and 784 Hz an octave down 19.2 (testPitchMatched; 440 Hz, the one testPitch
// played, was a lucky note). Now every one is within 0.05 dB, GRAIN turned under it too. A chord or a bell spreads
// less off its notes: an octave up C E G B was -1.0 dB of it off them, now -16.2; a bell's four parts -4.0, now -12.3.
//   The match is a correlation (each place's sound times the first head's, added up, over the place's own size),
// in two passes on left and right added: rough, on the sound summed in eights (kGroup) over 10.7 ms, every eighth
// place of the reach; then fine, every place within 4 of the rough best, over 5.3 ms. It matches the sound the first
// head plays some 5 to 11 ms before the splice. IT IS SPREAD OVER TICKS: it starts 32 ticks before the splice and
// does a 32nd of it a tick, three 32nds at most (in one go it is 10.8 us on the desk: 3 to 4% of a handheld block).
// Late (the speed or GRAIN turned under it), shifting down the head waits for it, and for the place it found to be
// behind the newest sample (GRAIN turned shorter); up it cannot, and takes the best found so far. A SEARCH IS KEPT
// ONLY WHILE ITS SPLICE IS NEAR (review, 2026-11-25): sent more than twice as far off as a search starts (GRAIN
// turned longer, the speed slowed), or the head stopped (PITCH and FINE at 0), it is dropped and done again nearer.
// What it found matched the sound then: kept, a note played since crossed out of step at the next splice (220 Hz to
// a fifth up, 10 ms after GRAIN went from 20 ms to 120 at -12: a 9.4 dB dip 0.2 to 0.3 s on; stopped, 9.9), and one
// caught half done by the stop went on, after it, reading a line written over since (11.8; on a held note, 1.2).
// Nothing to match (silence, a line still empty since waking, or a sound all in its sides, left the right upside down):
// the plain GRAIN. The heads keep the place they were given, so the delay is up to 25 ms longer than a GRAIN.
//
// SHIFTING UP, what goes on the line is low-passed first (4 poles): what was over half the rate over the speed would
// come out over the top and fold back down, out of tune. Its corner is just under that (0.5 / speed - 0.025 x (speed
// - 1) of the rate: 10.8 kHz an octave up), and the top of the band at no shift, where it is the sound as it is: it
// comes on with no step (started at 0.45 of the rate, PITCH turned up through 0 read a corner 5 times the steady one).
// An octave-up saw: the loudest fold-back under 16 kHz was -24 to -33 dB (1300, 660, 440 Hz), now -52 to -57. On the
// wet sound only: MIX 0 is untouched. Off at 0 and down; its state snapped to zero under kTiny on the tick.
//
// At PITCH 0 and FINE 0 the head stands still, so the wet sound is the dry one late: half a GRAIN and 2 samples from
// waking (1442 samples at 60 ms), or where it stood when a glide passed 0. Mixed with the dry it is a comb, as any
// shifter's is there.
//
// What glides is worked out on the TICK (16 samples counted from the start): the speed (in semitones, so a step of
// PITCH is a slide of a few ms, not a jump) and the low-pass; GRAIN and MIX glide every sample. Nothing under kQuiet
// goes on the line. The heads run on whether there is sound or not, so it sleeps only after kRestSeconds of nothing
// (its line all zeros, the low-pass at rest), and wakes as a fresh one: the same render however long the silence
// was, and however the sound is cut into blocks.

#include <algorithm>
#include <cmath>
#include <vector>

#include "glide.hpp"
#include "svf.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr int kSemis = 25;               // PITCH's choices: -12 to +12
constexpr float kShortest = 0.02f, kLongest = 0.12f; // GRAIN, seconds
constexpr float kRestSeconds = 4.f;
constexpr float kSettle = 0.02f;
constexpr int kLeast = 3; // the heads read at least this far back (the cubic reads two either side)
constexpr float kReachSeconds = 0.025f; // a splice lands up to this far past a GRAIN: one cycle of 40 Hz
constexpr float kFadeSeconds = 0.005f;  // the crossing at a splice
constexpr int kGroup = 8;               // the rough pass reads the sound summed in eights
constexpr int kRoughWindow = 64;        // groups: 512 samples, 10.7 ms
constexpr int kFineWindow = 256;        // samples, 5.3 ms
constexpr int kFineReach = 4;           // the fine pass: this far either side of the rough best
constexpr int kFineCost = 6;            // a fine place costs about six rough ones
constexpr int kSearchTicks = 32;        // the search's share a tick: a 32nd of it...
constexpr int kMostShares = 3;          // ...and never more than three shares, however late it is
constexpr int kDropTicks = 2 * (kSearchTicks + 2); // a search whose splice is sent further off is dropped (twice: a
                                                   // speed halved under it keeps it)
// The 4-pole Butterworth low-pass (two state-variable stages, k = 1 / Q).
constexpr float kButterK[2] = {1.8477591f, 0.7653669f};

int semisOf(float v) {
    const int i = static_cast<int>(clamp01(v) * kSemis);
    return (i >= kSemis ? kSemis - 1 : i) - 12;
}
float centsOf(float v) { return 100.f * clamp01(v) - 50.f; }
float grainOf(float v) { return kShortest + (kLongest - kShortest) * clamp01(v); }

// a . b and b . b over `n` samples, `a` in order, `b` the `n` places of a ring of `mask + 1` from `from` on
// (b[i] = ring[i] + other[i] when `other` is given). Four sums, so it runs at the speed of the loads, not of
// one sum waiting on the last; the same order every time.
inline void dotRing(const float* a, const float* ring, const float* other, int mask, int from, int n, float& cross, float& energy) {
    float c[4] = {0.f, 0.f, 0.f, 0.f}, e[4] = {0.f, 0.f, 0.f, 0.f};
    int k = 0;
    while (k < n) {
        const int at = (from + k) & mask, run = std::min(n - k, mask + 1 - at); // up to the ring's end
        const float* r = ring + at;
        const float* o = other ? other + at : nullptr;
        const float* x = a + k;
        int j = 0;
        if (o) {
            for (; j + 4 <= run; j += 4)
                for (int u = 0; u < 4; ++u) {
                    const float b = r[j + u] + o[j + u];
                    c[u] += x[j + u] * b, e[u] += b * b;
                }
            for (; j < run; ++j) {
                const float b = r[j] + o[j];
                c[0] += x[j] * b, e[0] += b * b;
            }
        } else {
            for (; j + 4 <= run; j += 4)
                for (int u = 0; u < 4; ++u) c[u] += x[j + u] * r[j + u], e[u] += r[j + u] * r[j + u];
            for (; j < run; ++j) c[0] += x[j] * r[j], e[0] += r[j] * r[j];
        }
        k += run;
    }
    cross = (c[0] + c[1]) + (c[2] + c[3]);
    energy = (e[0] + e[1]) + (e[2] + e[3]);
}

class Pitch : public Unit {
public:
    Pitch() : Unit(kPitchInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        reach_ = static_cast<int>(kReachSeconds * sampleRate);
        fade_ = fadeLength_ = std::max(1, static_cast<int>(kFadeSeconds * sampleRate));
        // The line holds the furthest a head reads (a GRAIN, the reach, a crossing's travel at an octave and a
        // half step) and the furthest the search reads (a GRAIN, the reach, its window) for as long as it takes.
        const int grain = static_cast<int>((kLongest + kReachSeconds) * sampleRate);
        const int heads = grain + 2 * fadeLength_ + 2 * kLeast + 8, search = grain + kRoughWindow * kGroup + kFineReach + (kSearchTicks + 2) * kTick + 8;
        int size = 64;
        while (size < std::max(heads, search)) size *= 2;
        for (auto& line : line_) line.assign(static_cast<std::size_t>(size), 0.f);
        rough_.assign(static_cast<std::size_t>(size / kGroup), 0.f);
        mask_ = size - 1;
        most_ = static_cast<float>(size - 4); // tapRead: `whole` at most size - 3
        perTick_ = (reach_ / kGroup + 2 + (2 * kFineReach + 1) * kFineCost + kSearchTicks - 1) / kSearchTicks;
        rest_ = static_cast<int>(kRestSeconds * sampleRate);
        changed();
        reset();
    }
    void reset() override {
        for (auto& line : line_) std::fill(line.begin(), line.end(), 0.f);
        std::fill(rough_.begin(), rough_.end(), 0.f);
        for (auto& stage : low_)
            for (Svf& s : stage) s.reset();
        at_ = 0;
        quietIn_ = 0;
        asleep_ = true;
        up_ = false;
        shapedFor_ = 0.f;
        semis_.snap(), grain_.snap(), mix_.snap();
        startHeads();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0 || line_[0].empty()) return;
        soundOnly(left, right, frames);
        float* io[2] = {left, right};
        float* const lines[2] = {line_[0].data(), line_[1].data()};
        float* const rough = rough_.data();
        const int size = mask_ + 1;
        for (int i = 0; i < frames; ++i) {
            const float x[2] = {std::fabs(left[i]) < kQuiet ? 0.f : left[i], std::fabs(right[i]) < kQuiet ? 0.f : right[i]};
            const bool none = x[0] == 0.f && x[1] == 0.f;
            if (asleep_) {
                if (none) {
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick(x);
            const float mix = mix_.next();
            const float grain = grain_.next() * sr_;
            // In: shifting up, through the low-pass first (what would fold back over the top).
            for (int c = 0; c < 2; ++c) {
                float v = x[c];
                if (up_) {
                    float v1, v2;
                    low_[0][c].step(shape_[0], v, v1, v2);
                    low_[1][c].step(shape_[1], v2, v1, v2);
                    v = v2;
                }
                lines[c][at_] = std::fabs(v) < kQuiet ? 0.f : v;
            }
            if ((at_ & (kGroup - 1)) == kGroup - 1) {
                const float* l = lines[0] + at_ - (kGroup - 1);
                const float* r = lines[1] + at_ - (kGroup - 1);
                float sum = 0.f;
                for (int k = 0; k < kGroup; ++k) sum += l[k] + r[k];
                rough[at_ / kGroup] = sum;
            }
            // The heads: they fall behind (lower) or catch up (higher) by 1 - speed a sample.
            const bool fading = fade_ < fadeLength_;
            delay_[active_] = std::min(most_, std::max(static_cast<float>(kLeast), delay_[active_] + turn_));
            if (fading) delay_[1 - active_] = std::min(most_, std::max(static_cast<float>(kLeast), delay_[1 - active_] + turn_));
            else if (turn_ < 0.f ? delay_[active_] <= upEnd() : turn_ > 0.f && delay_[active_] >= downEnd(grain) && (ready() || delay_[active_] >= downLast()))
                splice(grain);
            float wet[2] = {0.f, 0.f};
            const int from = (at_ + 1) & mask_;
            if (fade_ < fadeLength_) {
                const float t = static_cast<float>(fade_ + 1) * fadeStep_, in = t * t * (3.f - 2.f * t); // the S: in + out is one
                read(lines, size, from, delay_[active_], in, wet);
                read(lines, size, from, delay_[1 - active_], 1.f - in, wet);
                ++fade_;
            } else {
                read(lines, size, from, delay_[active_], 1.f, wet);
            }
            at_ = (at_ + 1) & mask_;
            if (sinceWake_ < size) ++sinceWake_;
            for (int c = 0; c < 2; ++c)
                if (mix != 0.f) io[c][i] = x[c] + mix * (wet[c] - x[c]);
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick(size);
        }
    }

protected:
    void changed() override {
        semis_.to = static_cast<float>(semisOf(p_[0])) + centsOf(p_[1]) / 100.f;
        grain_.to = grainOf(p_[2]);
        mix_.to = p_[3];
    }

private:
    // The search for where the next head starts. `dir` +1: shifting up (the next head starts further back), -1 down.
    enum { kNone, kRough, kFine, kDone };
    struct Search {
        int state = kNone, dir = 0;
        int end = 0, roughEnd = 0; // the first head's sound it matches: its newest sample, and group
        int next = 0, last = 0;    // the places still to try: rough in groups, fine in samples, both signed
        int lo = 0, hi = 0;        // where a head may start, in samples from the first (signed)
        float best = 0.f;
        int bestAt = 0, found = 0;
    };

    void read(float* const* lines, int size, int from, float back, float gain, float* wet) const {
        const int whole = static_cast<int>(back);
        float w[4];
        cubicWeights(back - static_cast<float>(whole), w);
        for (int c = 0; c < 2; ++c) wet[c] += gain * tapRead(lines[c], size, from, whole, w);
    }
    // Shifting up a head is spliced where it can still cross in time; down, a GRAIN and the reach on, or later (GRAIN
    // turned shorter under it) when its search is not done or the place it found is not yet behind the newest sample,
    // but where it can still cross inside the line.
    float upEnd() const { return static_cast<float>(kLeast) + static_cast<float>(fadeLength_) * -turn_; }
    float downEnd(float grain) const { return static_cast<float>(kLeast) + grain + static_cast<float>(reach_); }
    float downLast() const { return most_ - static_cast<float>(fadeLength_) * turn_ - 1.f; }
    bool ready() const { return search_.state == kDone && search_.dir < 0 && delay_[active_] + static_cast<float>(search_.found) >= static_cast<float>(kLeast); }

    void startHeads() {
        active_ = 0;
        delay_[0] = delay_[1] = static_cast<float>(kLeast) + 0.5f * grain_.now * sr_;
        fade_ = fadeLength_;
        fadeStep_ = 1.f / static_cast<float>(fadeLength_ + 1);
        sinceWake_ = 0;
        search_ = Search();
    }
    // The speed and the low-pass for where PITCH and FINE are going by this tick's end (sliding, the steps are a
    // few hundredths of a semitone: not heard). True when the low-pass has just come on.
    bool aim() {
        const float speed = std::exp2(semis_.end * (1.f / 12.f));
        turn_ = 1.f - speed;
        const bool up = speed > 1.f, on = up && !up_;
        if (up && speed != shapedFor_) {
            // The corner, over the rate: just under where the sound would come out over the top (0.225 an octave
            // up: 10.8 kHz), and the top itself at no shift, where it is the sound as it is: so it comes on with no step.
            const double edge = std::min(0.4999, 0.5 / speed - 0.025 * (speed - 1.0));
            const float g = static_cast<float>(std::tan(3.141592653589793 * edge));
            shape_[0].set(g, kButterK[0]), shape_[1].set(g, kButterK[1]);
            shapedFor_ = speed;
        }
        if (!up && up_)
            for (auto& stage : low_)
                for (Svf& s : stage) s.reset();
        up_ = up;
        return on;
    }
    void beginTick(const float* x) {
        const float k = glideK(kTick, kSettle, sr_);
        semis_.begin(kTick, k), grain_.begin(kTick, k), mix_.begin(kTick, k);
        if (aim())
            for (int c = 0; c < 2; ++c) // on from the sound as it is: a held value goes straight through
                for (auto& stage : low_) stage[c].ic1 = 0.f, stage[c].ic2 = x[c];
        searchTick();
    }
    void endTick(int size) {
        tick_ = 0;
        semis_.done(), grain_.done(), mix_.done();
        bool still = true;
        for (auto& stage : low_)
            for (Svf& s : stage) s.settle(), still = still && s.ic1 == 0.f && s.ic2 == 0.f;
        // Asleep: nothing for the rest time, nothing in the line (every place written as an exact zero since), and
        // the low-pass at rest.
        if (quietIn_ >= rest_ && quietIn_ >= size && still) {
            for (auto& line : line_) std::fill(line.begin(), line.end(), 0.f); // (zeros already: kept exact)
            std::fill(rough_.begin(), rough_.end(), 0.f);
            asleep_ = true;
        }
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        at_ = 0;
        semis_.snap(), grain_.snap(), mix_.snap();
        aim(); // (the low-pass is at rest: it starts from nothing, as a fresh one's)
        startHeads();
    }

    // ---- the splice and its search ----

    // Once a tick: start the search when the head is some 32 ticks from its splice, and do this tick's share: what
    // is left over the ticks left, at least a 32nd of the whole and at most three 32nds. Late (the speed or GRAIN
    // turned under it), shifting down it waits for the search; up it cannot, and takes the best found so far. Sent
    // far off or stopped, the search is dropped.
    void searchTick() {
        if (turn_ == 0.f) { // standing still: by its splice, what a search found would be old sound's
            search_ = Search();
            return;
        }
        const int dir = turn_ < 0.f ? 1 : -1;
        if (search_.state != kNone && search_.dir != dir) search_ = Search(); // turned round: start again
        const float grain = grain_.now * sr_, move = std::fabs(turn_) * static_cast<float>(kTick);
        const float toPlace = std::min((dir > 0 ? delay_[active_] - upEnd() : downEnd(grain) - delay_[active_]) / move, 1e6f);
        // Sent more than twice as far off as a search starts (GRAIN turned longer, the speed nearer 1 or slowing to
        // a stop): what it found matched the sound then, and a note played since is out of step with it. Again, nearer.
        if (search_.state != kNone && toPlace > static_cast<float>(kDropTicks)) search_ = Search();
        if (search_.state == kDone) return;
        if (search_.state == kNone) {
            if (toPlace > static_cast<float>(kSearchTicks + 2)) return;
            begin(dir, static_cast<int>(grain));
            if (search_.state == kDone) return;
        }
        // The ticks it has: to its place; past it (down), to where it must splice to cross inside the line.
        const float ticks = dir < 0 && toPlace < 2.f ? std::min((downLast() - delay_[active_]) / move, 1e6f) : toPlace;
        const int left = toDo(), within = static_cast<int>(ticks) - 1;
        work(within < 1 ? kMostShares * perTick_ : std::min(kMostShares * perTick_, std::max(perTick_, (left + within - 1) / within)));
    }
    void begin(int dir, int grain) {
        Search& s = search_;
        s = Search();
        s.dir = dir;
        s.state = kDone;
        s.found = dir * grain;
        s.lo = dir > 0 ? grain : -(grain + reach_);
        s.hi = dir > 0 ? grain + reach_ : -grain;
        // Shifting up, the next head reads older sound than the first: what it matches is the newest (and if that
        // is not yet older than a GRAIN since waking, every place is before the sound began: nothing to match).
        // Down, it reads newer: what it matches is older by as much, so every place tried has been written.
        const int newest = (at_ - 1) & mask_;
        if (dir > 0 && sinceWake_ <= grain) return;
        s.end = dir > 0 ? newest : (newest - grain - reach_ - kFineReach - 1) & mask_;
        const int groups = (mask_ + 1) / kGroup, gmask = groups - 1;
        s.roughEnd = (((s.end + 1) / kGroup) - 1) & gmask;
        float energy = 0.f;
        for (int n = 0; n < kRoughWindow; ++n) {
            const float v = rough_[static_cast<std::size_t>((s.roughEnd - kRoughWindow + 1 + n) & gmask)];
            roughRef_[n] = v, energy += v * v;
        }
        if (energy == 0.f) return; // nothing to match: the plain GRAIN
        for (int n = 0; n < kFineWindow; ++n) {
            const int at = (s.end - kFineWindow + 1 + n) & mask_;
            fineRef_[n] = line_[0][static_cast<std::size_t>(at)] + line_[1][static_cast<std::size_t>(at)];
        }
        // Rough places: every whole group in reach, by the size of the jump, smallest first (a GRAIN to a GRAIN and
        // the reach, signed by dir).
        s.next = dir * ((grain + kGroup - 1) / kGroup);
        s.last = dir * ((grain + reach_) / kGroup);
        s.best = -1e30f;
        s.bestAt = s.found / kGroup;
        s.state = kRough;
    }
    int toDo() const {
        const Search& s = search_;
        if (s.state == kRough) return std::abs(s.last - s.next) + 1 + (2 * kFineReach + 1) * kFineCost;
        if (s.state == kFine) return (std::abs(s.last - s.next) + 1) * kFineCost;
        return 0;
    }
    void work(int budget) {
        Search& s = search_;
        const int groups = (mask_ + 1) / kGroup, gmask = groups - 1;
        while (budget > 0 && (s.state == kRough || s.state == kFine)) {
            float cross, energy;
            if (s.state == kRough) {
                dotRing(roughRef_, rough_.data(), nullptr, gmask, (s.roughEnd - s.next - kRoughWindow + 1) & gmask, kRoughWindow, cross, energy);
                --budget;
            } else {
                dotRing(fineRef_, line_[0].data(), line_[1].data(), mask_, (s.end - s.next - kFineWindow + 1) & mask_, kFineWindow, cross, energy);
                budget -= kFineCost;
            }
            // How alike: the correlation squared (over the place's own size), its sign kept.
            const float score = energy > 0.f ? cross * std::fabs(cross) / energy : -1e30f;
            if (score > s.best) s.best = score, s.bestAt = s.next;
            if (s.next != s.last) {
                s.next += s.dir;
                continue;
            }
            if (s.state == kRough) {
                // Fine: every place within kFineReach of the rough best, in reach.
                const int centre = s.bestAt * kGroup;
                s.next = std::max(s.lo, std::min(s.hi, centre - kFineReach * s.dir));
                s.last = std::max(s.lo, std::min(s.hi, centre + kFineReach * s.dir));
                s.best = -1e30f;
                s.bestAt = centre;
                s.state = kFine;
            } else {
                s.found = s.bestAt;
                s.state = kDone;
            }
        }
    }
    // Where the next head starts, from the first: the search's place; not done, the best it has found so far; not
    // started (turned round this tick), the plain GRAIN.
    int jumpFor(int dir, float grain) const {
        const Search& s = search_;
        if (s.state == kNone || s.dir != dir || (s.state == kRough && s.best <= -1e30f)) return dir * static_cast<int>(grain);
        return s.state == kDone ? s.found : s.state == kRough ? s.bestAt * kGroup : s.bestAt;
    }
    void splice(float grain) {
        const int dir = turn_ < 0.f ? 1 : -1;
        const int jump = jumpFor(dir, grain);
        const int next = 1 - active_;
        delay_[next] = std::min(most_, std::max(static_cast<float>(kLeast), delay_[active_] + static_cast<float>(jump)));
        active_ = next;
        fade_ = 0;
        search_ = Search();
    }

    std::vector<float> line_[2], rough_;
    float roughRef_[kRoughWindow] = {}, fineRef_[kFineWindow] = {};
    int mask_ = 0, at_ = 0, rest_ = 0, reach_ = 0, perTick_ = 1;
    float most_ = 0.f, turn_ = 0.f;
    // The heads: how far back each reads, which one is playing, and how far into a crossing (fadeLength_: none).
    float delay_[2] = {0.f, 0.f}, fadeStep_ = 0.f;
    int active_ = 0, fade_ = 0, fadeLength_ = 1, sinceWake_ = 0;
    Search search_;
    // The low-pass shifting up: two stages a side.
    Svf low_[2][2];
    SvfShape shape_[2];
    float shapedFor_ = 0.f;
    bool up_ = false;
    Glide semis_, grain_, mix_;
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int index, float v, const float*, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (index == 0) {
        const int semis = semisOf(v);
        if (semis == 0) std::snprintf(out, n, "0 ST");
        else std::snprintf(out, n, "%+d ST", semis);
    } else if (index == 1) {
        const int cents = static_cast<int>(std::round(centsOf(v)));
        if (cents == 0) std::snprintf(out, n, "0 CENTS");
        else std::snprintf(out, n, "%+d CENTS", cents);
    } else if (index == 2) {
        std::snprintf(out, n, "%.0f MS", static_cast<double>(1000.f * grainOf(v)));
    } else {
        readPercent(v, out, size);
    }
}

Effect* create() { return new Pitch; }

} // namespace

const EffectInfo kPitchInfo = {
    "PITCH", 4, {"PITCH", "FINE", "GRAIN", "MIX"}, {(24.f + 0.5f) / kSemis, 0.5f, 0.4f, 0.5f}, // an octave up, beside the sound
    "pitch", EffectFamily::Mod, {kSemis, 0, 0, 0}, reading, create,
    "higher or lower: an octave, a fifth",
};

} // namespace fx
} // namespace apotheca
