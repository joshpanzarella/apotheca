#include "apotheca/drums.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>

namespace apotheca {

namespace {

constexpr float kTwoPi = 6.2831853f;

// Small repeatable noise source, so the kit is the same on every start.
struct Noise {
    std::uint32_t state;
    explicit Noise(std::uint32_t seed) : state(seed) {}
    float next() { // -1..1
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (2.f / 16777216.f) - 1.f;
    }
};

// One-pole highpass and lowpass, enough to shape noise.
struct OnePole {
    float coeff = 1.f, low = 0.f;
    OnePole(float hz, float sr) { coeff = 1.f - std::exp(-kTwoPi * hz / sr); }
    float lowpass(float x) { low += coeff * (x - low); return low; }
    float highpass(float x) { return x - lowpass(x); }
};

float decay(float t, float seconds) { return std::exp(-6.9077553f * t / seconds); } // -60 dB at `seconds`

// Six detuned square waves: the metallic part of a drum machine cymbal.
float metal(float t) {
    static const float hz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
    float sum = 0.f;
    for (float f : hz) sum += std::fmod(t * f * 8.f, 1.f) < 0.5f ? 1.f : -1.f;
    return sum / 6.f;
}

// Scale to a fixed peak and fade the last 5 ms, so no sample ends on a click.
void finish(std::vector<float>& s, float sr, float peak) {
    float loudest = 0.f;
    for (float v : s) loudest = std::fabs(v) > loudest ? std::fabs(v) : loudest;
    const float scale = loudest > 0.f ? peak / loudest : 0.f;
    const int n = static_cast<int>(s.size());
    const int tail = static_cast<int>(0.005f * sr);
    for (int i = 0; i < n; ++i) {
        float g = scale;
        if (i >= n - tail) g *= static_cast<float>(n - 1 - i) / static_cast<float>(tail);
        s[i] *= g;
    }
}

template <typename F>
std::vector<float> make(float sr, float seconds, float peak, F sampleAt) {
    std::vector<float> s(static_cast<std::size_t>(seconds * sr));
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = sampleAt(static_cast<float>(i) / sr);
    finish(s, sr, peak);
    return s;
}

// A sine whose pitch falls from `from` to `to`; returns the phase at time t.
float sweepPhase(float t, float from, float to, float seconds) {
    // integral of to + (from - to) * exp(-t / tau)
    const float tau = seconds / 5.f;
    return to * t + (from - to) * tau * (1.f - std::exp(-t / tau));
}

} // namespace

const char* DrumKit::laneName(int lane) {
    static const char* names[kDrumLanes] = {"KICK", "SNARE", "CL HAT", "OP HAT", "CLAP", "TOM", "RIM", "CRASH"};
    return lane >= 0 && lane < kDrumLanes ? names[lane] : "";
}

// Drive: pushes a sound into a soft ceiling. The peaks flatten and the body
// comes up, which is what makes a drum sound loud at the same peak level.
static float drive(float x, float amount) { return std::tanh(amount * x); }

const DrumKit& DrumKit::builtIn(float sampleRate) {
    struct Kept { float rate; DrumKit kit; };
    static std::mutex mutex;
    // Never taken apart: an engine may still be playing from one as the program ends.
    static std::vector<std::unique_ptr<Kept>>& kept = *new std::vector<std::unique_ptr<Kept>>;
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& k : kept)
        if (k->rate == sampleRate) return k->kit;
    kept.emplace_back(new Kept{sampleRate, {}});
    kept.back()->kit.buildBuiltIn(sampleRate);
    return kept.back()->kit;
}

void DrumKit::buildBuiltIn(float sr) {
    // The kit is built to hit hard: fast pitch drops, a sharp click on the
    // front of each drum, and every sound driven before it is levelled.

    // Kick: a sine that drops from 250 Hz to 47 Hz in a few milliseconds,
    // driven hard, with a click in front.
    {
        Noise noise(1);
        OnePole clickTone(5000.f, sr);
        samples[0] = make(sr, 0.6f, 0.98f, [&](float t) {
            const float body = std::sin(kTwoPi * sweepPhase(t, 250.f, 47.f, 0.075f)) * decay(t, 0.7f);
            const float click = clickTone.lowpass(noise.next()) * decay(t, 0.006f)
                              + 0.6f * std::sin(kTwoPi * 2900.f * t) * decay(t, 0.004f);
            return drive(drive(body, 3.5f) + 0.5f * click, 1.4f);
        });
    }
    // Snare: two falling tones for the shell, bright noise for the wires,
    // and a crack of full-band noise on the front.
    {
        Noise noise(2);
        OnePole hp(1400.f, sr);
        samples[1] = make(sr, 0.34f, 0.98f, [&](float t) {
            const float shell = std::sin(kTwoPi * sweepPhase(t, 340.f, 188.f, 0.04f)) * decay(t, 0.13f)
                              + 0.5f * std::sin(kTwoPi * sweepPhase(t, 560.f, 290.f, 0.03f)) * decay(t, 0.08f);
            const float n = noise.next();
            const float wires = hp.highpass(n) * decay(t, 0.3f);
            const float crack = n * decay(t, 0.012f);
            return drive(0.8f * shell + 0.9f * wires + 0.9f * crack, 3.f);
        });
    }
    // Hats: metal and noise above 8 kHz, driven so they cut. Closed is a
    // tick, open rings.
    {
        Noise noise(3);
        OnePole hp(8000.f, sr);
        samples[2] = make(sr, 0.07f, 0.85f, [&](float t) {
            return drive(hp.highpass(0.6f * metal(t) + 0.6f * noise.next()) * decay(t, 0.05f), 2.5f);
        });
    }
    {
        Noise noise(4);
        OnePole hp(8000.f, sr);
        samples[3] = make(sr, 0.55f, 0.85f, [&](float t) {
            return drive(hp.highpass(0.6f * metal(t) + 0.6f * noise.next()) * decay(t, 0.55f), 2.5f);
        });
    }
    // Clap: four tight bursts of mid noise, then a wide tail.
    {
        Noise noise(5);
        OnePole hp(700.f, sr), lp(5500.f, sr);
        samples[4] = make(sr, 0.42f, 0.95f, [&](float t) {
            float env = decay(t > 0.036f ? t - 0.036f : 0.f, 0.36f);
            if (t < 0.036f) env = decay(std::fmod(t, 0.009f), 0.011f);
            return drive(lp.lowpass(hp.highpass(noise.next())) * env, 3.5f);
        });
    }
    // Tom: the kick's recipe, higher.
    {
        Noise noise(6);
        OnePole clickTone(4000.f, sr);
        samples[5] = make(sr, 0.45f, 0.95f, [&](float t) {
            const float body = std::sin(kTwoPi * sweepPhase(t, 300.f, 112.f, 0.07f)) * decay(t, 0.45f);
            return drive(drive(body, 2.5f) + 0.3f * clickTone.lowpass(noise.next()) * decay(t, 0.005f), 1.3f);
        });
    }
    // Rim: two high tones and a click, very short and very loud.
    {
        Noise noise(7);
        samples[6] = make(sr, 0.06f, 0.95f, [&](float t) {
            const float tones = std::sin(kTwoPi * 1720.f * t) + 0.7f * std::sin(kTwoPi * 487.f * t);
            return drive(tones * decay(t, 0.04f) + 0.8f * noise.next() * decay(t, 0.003f), 3.f);
        });
    }
    // Crash: the hat recipe, darker and long.
    {
        Noise noise(8);
        OnePole hp(3500.f, sr);
        samples[7] = make(sr, 1.8f, 0.85f, [&](float t) {
            const float wash = hp.highpass(0.5f * metal(t * 1.31f) + 0.7f * noise.next());
            return drive(wash * (decay(t, 1.7f) + 0.6f * decay(t, 0.05f)), 2.2f);
        });
    }
}

void DrumMachine::prepare(float sampleRate) {
    chokeStep_ = 1.f / (0.003f * sampleRate);
    joinFrames_ = static_cast<int>(0.0015f * sampleRate);
    reset();
}

void DrumMachine::setLaneLevel(int lane, float level) {
    if (lane < 0 || lane >= kDrumLanes || !(level >= 0.f)) return; // also drops NaN
    laneLevel_[lane] = level > 1.f ? 1.f : level;
}

void DrumMachine::setLanePart(int lane, float part) {
    if (lane < 0 || lane >= kDrumLanes) return;
    lanePart_[lane] = part; // trigger() takes anything that is not a share of the sound as "all of it"
}

void DrumMachine::setLaneCuts(int lane, int count, int from, int places, const float* at) {
    if (lane < 0 || lane >= kDrumLanes) return;
    cutCount_[lane] = count <= 0 ? 0 : (count > kMaxCuts ? kMaxCuts : count);
    cuts_[lane][0] = 0.f; // (the first is always the top)
    for (int i = 0; i < places && i < 3 && at; ++i) {
        const int k = from + i;
        if (k <= 0 || k >= kMaxCuts) continue;
        const float v = at[i];
        cuts_[lane][k] = v > 0.f && v < 1.f ? v : 1.f; // (nonsense, NaN too: past the end, so nothing ends there)
    }
}

void DrumMachine::setLaneChoke(int lane, int group) {
    if (lane < 0 || lane >= kDrumLanes) return;
    choke_[lane] = chokeGroupOf(group);
}

// CHOKE: the hit on `lane` cuts every other lane of its group, as a lane's own next hit cuts it (the
// same 3 ms). Not a voice that has not been heard yet: it started on this very sample, and both hits
// were written. Nothing at all for a lane in no group.
void DrumMachine::chokeOthers(int lane) {
    const int group = choke_[lane];
    if (group == kChokeNone) return;
    for (int other = 0; other < kDrumLanes; ++other) {
        if (other == lane || choke_[other] != group) continue;
        for (Voice& v : voices_[other])
            if (v.position >= 0.0 && v.fadeStep == 0.f && v.born != renders_) v.fadeStep = chokeStep_;
    }
}

void DrumMachine::reset() {
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        voices_[lane][0] = Voice();
        voices_[lane][1] = Voice();
        next_[lane] = 0;
    }
}

void DrumMachine::setLaneSample(int lane, const float* data, int length) {
    if (lane < 0 || lane >= kDrumLanes) return;
    own_[lane] = length > 0 ? data : nullptr;
    ownLength_[lane] = own_[lane] ? length : 0;
    voices_[lane][0] = Voice(); // they were reading the old sound
    voices_[lane][1] = Voice();
}

void DrumMachine::trigger(int lane, float velocity, float start, float part, const HitPlay& how) {
    if (!kit_ || lane < 0 || lane >= kDrumLanes) return;
    if (!(velocity > 0.f)) return; // also drops NaN
    if (velocity > 1.f) velocity = 1.f;
    const int length = own_[lane] ? ownLength_[lane] : static_cast<int>(kit_->samples[lane].size());
    if (length == 0) return;
    if (!(start > 0.f)) start = 0.f; // also NaN
    const int from = static_cast<int>(start * static_cast<float>(length)); // at or past the end: nothing plays
    const bool lanes = !(part >= 0.f);
    if (lanes) part = lanePart_[lane]; // below zero (or NaN): as the lane is set
    int end = part > 0.f && part < 1.f ? from + static_cast<int>(part * static_cast<float>(length)) : length;
    if (lanes && part > 0.f && cutCount_[lane] > 0) {
        // ONE SLICE with the lane's own cuts (SLICES AT THE HITS): to the next cut past where it starts. A
        // cut within the carry-on's millisecond and a half of the start is the one it starts on (a start
        // heard by hand comes in ten-thousandths of the sound: a few samples short of its cut).
        end = length;
        for (int k = 1; k < cutCount_[lane]; ++k) {
            const int at = static_cast<int>(cuts_[lane][k] * static_cast<float>(length));
            if (at > from + joinFrames_) {
                end = at;
                break;
            }
        }
    }
    const double rate = how.rate > 0.f ? static_cast<double>(how.rate) : 1.0; // nonsense (NaN too): as it is. The engine keeps it within an octave.

    Voice& fresh = voices_[lane][next_[lane]];
    Voice& old = voices_[lane][1 - next_[lane]];
    // The sound is already at this very place: it carries on. (If it had just
    // begun to fade at the end of its own part, it comes back up.) Not a hit
    // from the very top, which always starts the sound again; and not a hit
    // that plays backwards, which has its own place to start. (A sound that
    // was running backwards is turned round by it: its speed is the hit's.)
    if (old.position >= 0.0 && from > 0 && !how.reverse && std::fabs(old.position - from) <= joinFrames_) {
        old.end = end;
        old.rate = rate;
        old.life = how.life;
        old.gainTo = velocity;
        old.fadeStep = 0.f;
        old.audition = how.audition; // the sound is this hit's now
        for (int bus = 0; bus < 2; ++bus) old.thrownTo[bus] = (how.throwTo >> bus) & 1 ? 1.f : 0.f; // it slides there (render)
        if (old.fade < 1.f) { // back up from where the fade had got to
            old.rise *= old.fade;
            old.riseStep = chokeStep_ * 3.f;
            old.fade = 1.f;
        }
        chokeOthers(lane); // (a hit all the same)
        return;
    }
    // Nothing there to play: leave the lane's voices alone (before the wipe below).
    if (from >= length) return; // at or past the end
    if (how.reverse && (end < length ? end : length) - 1 < from) return; // an empty span
    if (old.position >= 0.0 && old.fadeStep == 0.f) old.fadeStep = chokeStep_;
    fresh = Voice();
    fresh.end = end;
    fresh.life = how.life > 0 ? how.life : 0;
    fresh.gain = fresh.gainTo = velocity;
    fresh.audition = how.audition;
    for (int bus = 0; bus < 2; ++bus) fresh.thrown[bus] = fresh.thrownTo[bus] = (how.throwTo >> bus) & 1 ? 1.f : 0.f;
    if (how.reverse) {
        // From the last sample it would have played, back to the first.
        const int last = (end < length ? end : length) - 1; // (an empty span returned above)
        fresh.position = last;
        fresh.rate = -rate;
        fresh.stop = from;
        fresh.rise = 0.f; // it comes in from the middle of the sound, or from its tail
        fresh.riseStep = chokeStep_ * 3.f;
    } else {
        fresh.position = from;
        fresh.rate = rate;
        fresh.rise = from > 0 ? 0.f : 1.f;
        fresh.riseStep = from > 0 ? chokeStep_ * 3.f : 0.f; // a millisecond
    }
    fresh.born = renders_;
    next_[lane] = 1 - next_[lane];
    chokeOthers(lane);
}

void DrumMachine::letGo(int lane) {
    if (lane < 0 || lane >= kDrumLanes) return;
    for (Voice& v : voices_[lane])
        if (v.audition && v.position >= 0.0 && v.fadeStep == 0.f) v.fadeStep = chokeStep_;
}

void DrumMachine::chokeAll() {
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (Voice& v : voices_[lane])
            if (v.position >= 0.0 && v.fadeStep == 0.f) v.fadeStep = chokeStep_;
}

// RUNS (speed plan, batch 2). For a stretch of samples nothing about a voice moves but its place:
// forward at speed 1, its level at rest (no glide, no rise, no fade), its THROW at rest, and none of
// the things that change it falls inside (the end of the sound or of its part, where its fade begins,
// or the sample its time is up on). That stretch is a plain loop with the same sum written the same
// way, so the sound is the same to the bit; every other sample goes the long way, one at a time, as
// before. 0: the next sample is not plain.
namespace {
bool gRuns = true; // setRunsForTests
}
void DrumMachine::setRunsForTests(bool on) { gRuns = on; }

template <bool kThrown>
int DrumMachine::plainRun(const Voice& v, int length, int most) {
    if (v.rate != 1.0 || v.gain != v.gainTo || v.rise < 1.f || v.fadeStep != 0.f) return 0;
    if (kThrown && (v.thrown[0] != v.thrownTo[0] || v.thrown[1] != v.thrownTo[1])) return 0;
    int n = most;
    if (v.life > 0 && v.life - 1 < n) n = v.life - 1; // (the sample it reaches 0 on starts the fade)
    // The samples whose place is still short of the sound's end and of its part's (places p, p + 1...:
    // exact in a double, as the long way's adding of 1 is).
    const double room = static_cast<double>(v.end < length ? v.end : length) - v.position;
    if (!(room > 0.0)) return 0;
    if (room < static_cast<double>(n)) n = static_cast<int>(std::ceil(room));
    return n;
}

// One voice's part of a block, added to `left`. Written once and built twice: `kThrown` false is
// a hit as it always was, with nothing of THROW in its loop, so a hit that is not thrown costs
// what it did; true also writes the voice out for the buses. Returns `wrote` with the bits of
// the ways out it wrote added.
template <bool kThrown>
int DrumMachine::renderVoice(Voice& v, const float* sample, int length, float laneLevel, float* left, int frames, float* const* thrownOut, int wrote) {
    const float quick = chokeStep_ * 3.f; // a fade of a millisecond
    // Backwards, the fade must be over when it reaches its start (the front of a drum is its
    // loudest part): it begins this far short of it.
    const double fadeRoom = v.rate < 0.0 ? -v.rate / static_cast<double>(quick) : 0.0;
    int i = 0;
    while (i < frames) {
        const int run = gRuns ? plainRun<kThrown>(v, length, frames - i) : 0;
        if (run > 0) {
            // The long way's sum, word for word, with what does not move taken once.
            const float gain = v.gain, fade = v.fade, rise = v.rise;
            const float* from = sample + static_cast<int>(v.position);
            float* to = left + i;
            for (int k = 0; k < run; ++k) to[k] += from[k] * gain * fade * rise * laneLevel;
            if (kThrown)
                for (int bus = 0; bus < 2; ++bus) {
                    if (v.thrown[bus] == 0.f && v.thrownTo[bus] == 0.f) continue;
                    if (!thrownOut[bus]) continue;
                    if (!(wrote & (1 << bus))) {
                        for (int k = 0; k < frames; ++k) thrownOut[bus][k] = 0.f;
                        wrote |= 1 << bus;
                    }
                    const float thrown = v.thrown[bus];
                    float* out = thrownOut[bus] + i;
                    for (int k = 0; k < run; ++k) out[k] += from[k] * gain * fade * rise * laneLevel * thrown;
                }
            v.position += run;
            if (v.life > 0) v.life -= run;
            i += run;
            continue;
        }
        if (v.position >= length || (v.rate < 0.0 && v.position < v.stop)) { v.position = -1.0; break; }
        // The end of its part: a quick fade, so the next slice's first moment is not heard as a tick.
        if (v.fadeStep == 0.f && (v.rate > 0.0 ? v.position >= v.end : v.position <= v.stop + fadeRoom)) v.fadeStep = quick;
        // Cut: its time is up.
        if (v.life > 0 && --v.life == 0 && v.fadeStep == 0.f) v.fadeStep = quick;
        const int at = static_cast<int>(v.position);
        float value = sample[at];
        if (v.rate != 1.0) { // between two samples: a straight line from one to the next
            const float part = static_cast<float>(v.position - at);
            value += part * ((at + 1 < length ? sample[at + 1] : 0.f) - value);
        }
        v.position += v.rate;
        left[i] += value * v.gain * v.fade * v.rise * laneLevel;
        if constexpr (kThrown) {
            // THROW: the same sample a second time, for the bus. (Worked out again, not kept
            // from the line above: that line stays as it was, to the bit.)
            for (int bus = 0; bus < 2; ++bus) {
                if (v.thrown[bus] == 0.f && v.thrownTo[bus] == 0.f) continue;
                if (!thrownOut[bus]) continue;
                if (!(wrote & (1 << bus))) {
                    for (int k = 0; k < frames; ++k) thrownOut[bus][k] = 0.f;
                    wrote |= 1 << bus;
                }
                thrownOut[bus][i] += value * v.gain * v.fade * v.rise * laneLevel * v.thrown[bus];
                if (v.thrown[bus] != v.thrownTo[bus]) // a hit that carried the sound on changed it: a millisecond
                    v.thrown[bus] = v.thrown[bus] < v.thrownTo[bus] ? (v.thrown[bus] + quick > 1.f ? 1.f : v.thrown[bus] + quick) : (v.thrown[bus] - quick < 0.f ? 0.f : v.thrown[bus] - quick);
            }
        }
        if (v.gain != v.gainTo) { // a level that changes mid-sound moves there over a millisecond
            v.gain = v.gain < v.gainTo ? (v.gain + quick > v.gainTo ? v.gainTo : v.gain + quick) : (v.gain - quick < v.gainTo ? v.gainTo : v.gain - quick);
        }
        if (v.rise < 1.f) {
            v.rise += v.riseStep;
            if (v.rise > 1.f) v.rise = 1.f;
        }
        if (v.fadeStep > 0.f) {
            v.fade -= v.fadeStep;
            if (v.fade <= 0.f) { v.position = -1.0; break; }
        }
        ++i;
    }
    return wrote;
}

int DrumMachine::render(float* left, float* right, int frames, float* echo, float* space) {
    for (int i = 0; i < frames; ++i) left[i] = 0.f;
    float* const thrownOut[2] = {echo, space};
    int wrote = 0;
    if (kit_) {
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            const float* sample = own_[lane] ? own_[lane] : kit_->samples[lane].data();
            const int length = own_[lane] ? ownLength_[lane] : static_cast<int>(kit_->samples[lane].size());
            const float laneLevel = laneLevel_[lane];
            for (Voice& v : voices_[lane]) {
                if (v.position < 0.0) continue;
                // THROW: is this voice written out for a bus as well? Asked once for the block, not
                // on every sample. (Nothing sets a voice's THROW while this runs.)
                const bool throwing = (echo || space) && (v.thrown[0] != 0.f || v.thrownTo[0] != 0.f || v.thrown[1] != 0.f || v.thrownTo[1] != 0.f);
                if (throwing) wrote = renderVoice<true>(v, sample, length, laneLevel, left, frames, thrownOut, wrote);
                else renderVoice<false>(v, sample, length, laneLevel, left, frames, thrownOut, wrote);
            }
        }
    }
    for (int i = 0; i < frames; ++i) right[i] = left[i];
    if (frames > 0) ++renders_; // (every voice started before this render has been heard now)
    return wrote;
}

} // namespace apotheca
