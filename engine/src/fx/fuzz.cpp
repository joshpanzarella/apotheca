// FUZZ: a fuzz pedal, a TYPE of DRIVE. The sound is turned up so far that it
// comes out nearly square, one side clipped harder and higher than the other
// (as a pedal's transistor does), and a GATE makes quiet sound sputter and
// cut out instead of hissing on.
//
//   FUZZ   how far it is turned up before it clips: 6 to 48 dB. Its own name
//          and scale: a fuzz is meant to be pushed far past where DRIVE and
//          SATURATE go (their DRIVE at 35% would be a weak fuzz), so it starts
//          at its usual 70% (35 dB) when TYPE is changed to it.
//   TONE   a pedal's tone (the one a Big Muff has): down, a low-pass (dark,
//          woolly); up, a high-pass (thin, cutting); in the middle both,
//          with a dip between them (the "scooped" sound). Named as the other
//          types' TONE: up is brighter there too. What it takes away from a
//          fuzzed note is made up again (worked out for a square at 220 Hz),
//          so it changes the colour and not the loudness.
//   GATE   the starved, dying-battery sputter: the sound has to get past a
//          step before anything comes out, so a note fades by breaking up
//          into a crackle and then stops dead. 0 is no gate. The step is
//          after FUZZ, so the more FUZZ the quieter a note gets before it
//          breaks up.
//   MIX    0 is the sound untouched, to the sample.
//
// The clip: the upper side straight to 0.75, flat at 1 (SATURATE's HARD); the
// lower side soft and lower, at 0.6. Lopsided, so even overtones as well as
// odd (most at middling FUZZ: pushed hard both sides are flat and it is all
// but a square, the 2nd 46 dB down at 70%), and the wave is brought back on
// centre after it. Worked out at twice
// the sample rate (bend.hpp, halfband.hpp), the plain sound made as late to
// match. The loudness is held level as FUZZ and GATE move (a sine at half
// scale comes out as loud as it went in), so FUZZ changes the character, not
// the volume. Every setting glides on the TICK (svf.hpp); asleep with nothing
// coming in, as FOLD.

#include <complex>
#include <initializer_list>

#include "bend.hpp"
#include "unit.hpp"

namespace apotheca {
namespace fx {

namespace {

constexpr float kLow = 0.6f;         // the lower side's top
constexpr float kMostGate = 2.f;     // GATE at full: the step, where the clip is 1
constexpr float kDarkHz = 600.f, kBrightHz = 1500.f; // TONE's low-pass and high-pass
constexpr float kSettle = 0.012f, kMixSettle = 0.02f;
constexpr float kToneReferenceHz = 220.f; // TONE's loss is made up for a square at this pitch
enum { kPush, kGate, kLevel, kTilt, kMakeUp, kShapes };

inline float soft(float x) { // tanh, nearly; flat from 3
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
// The pedal: past the gate's step, then the lopsided clip.
inline float fuzz(float x, float gate) {
    const float a = std::fabs(x) - gate;
    if (!(a > 0.f)) return 0.f;
    if (x > 0.f) {
        if (a <= 0.75f) return a;
        return a >= 1.25f ? 1.f : a - (a - 0.75f) * (a - 0.75f);
    }
    return -kLow * soft(a * (1.f / kLow));
}
float pushOf(float v) { return std::pow(10.f, (6.f + 42.f * clamp01(v)) / 20.f); }
float gateOf(float v) { return kMostGate * clamp01(v); }

class Fuzz : public Unit {
public:
    Fuzz() : Unit(kFuzzInfo) {}
    void prepare(float sampleRate) override {
        sr_ = sampleRate;
        keep_ = dcKeep(sr_);
        dark_ = onePole(kDarkHz, sr_);
        bright_ = onePole(kBrightHz, sr_);
        toneSums();
        changed();
        reset();
    }
    void reset() override {
        for (BendSide& s : side_) s.reset();
        for (int c = 0; c < 2; ++c) low_[c] = high_[c] = 0.f;
        quietIn_ = 0;
        asleep_ = true;
        arrive();
    }
    void process(float* left, float* right, int frames) override {
        if (frames <= 0) return;
        soundOnly(left, right, frames);
        float* io[2] = {left, right};
        for (int i = 0; i < frames; ++i) {
            const float x[2] = {heardIn(left[i]), heardIn(right[i])};
            const bool none = x[0] == 0.f && x[1] == 0.f;
            if (asleep_) {
                if (none) {
                    tick_ = (tick_ + 1) % kTick;
                    continue;
                }
                wake();
            }
            if (tick_ == 0) beginTick();
            const float mix = mix_.next(), late = late_.next();
            shape_.next();
            const float* const n = shape_.now;
            const float push = n[kPush], gate = n[kGate], level = n[kLevel], tilt = n[kTilt], makeUp = n[kMakeUp];
            const auto bend = [push, gate, level](float v) { return level * fuzz(v * push, gate); };
            for (int c = 0; c < 2; ++c) {
                const float bent = side_[c].twice(x[c], bend, keep_);
                low_[c] += dark_ * (bent - low_[c]);
                high_[c] += bright_ * (bent - high_[c]);
                const float wet = makeUp * (low_[c] + tilt * ((bent - high_[c]) - low_[c]));
                const float plain = side_[c].plain(x[c]);
                const float dry = x[c] + late * (plain - x[c]);
                if (mix != 0.f || late != 0.f) io[c][i] += (dry - x[c]) + mix * (wet - dry);
            }
            quietIn_ = none ? quietIn_ + 1 : 0;
            if (++tick_ == kTick) endTick();
        }
    }

protected:
    void changed() override {
        fuzz_.first.to = p_[0];
        tone_.first.to = p_[1];
        gate_.first.to = p_[2];
        mix_.to = p_[3];
        late_.to = p_[3] > 0.f ? 1.f : 0.f;
    }

private:
    void beginTick() {
        const float k = glideK(kTick, kMixSettle, sr_);
        mix_.begin(kTick, k);
        late_.begin(kTick, k);
        if (!(fuzz_.moving() || tone_.moving() || gate_.moving())) return;
        const float g = glideK(kTick, kSettle, sr_);
        for (Gentle* s : {&fuzz_, &tone_, &gate_}) s->tick(g);
        shape();
        shape_.aim();
    }
    void endTick() {
        tick_ = 0;
        mix_.done();
        late_.done();
        shape_.land();
        float loudest = 0.f;
        for (int c = 0; c < 2; ++c) {
            side_[c].settle();
            if (std::fabs(low_[c]) < kTiny) low_[c] = 0.f;
            if (std::fabs(high_[c]) < kTiny) high_[c] = 0.f;
            // (low_ and high_ both follow the side's dcOut down, so leaving either out changes nothing seen.)
            loudest = std::fmax(loudest, std::fmax(side_[c].loudest(), std::fmax(std::fabs(low_[c]), std::fabs(high_[c]))));
        }
        if (quietIn_ >= kTick && loudest < kQuiet) {
            for (BendSide& s : side_) s.reset();
            for (int c = 0; c < 2; ++c) low_[c] = high_[c] = 0.f;
            asleep_ = true;
        }
    }
    void shape() {
        const float push = pushOf(fuzz_.now()), gate = gateOf(gate_.now());
        float* const to = shape_.to;
        to[kPush] = push, to[kGate] = gate;
        to[kLevel] = bendLevel([push, gate](float v) { return fuzz(v * push, gate); });
        to[kTilt] = clamp01(tone_.now());
        to[kMakeUp] = makeUpFor(to[kTilt]);
    }
    // What TONE takes from a fuzzed note (a square at kToneReferenceHz: every odd harmonic, a third, a fifth...
    // as loud, to the top), given back: TONE changes the colour, not the loudness. What is kept is
    // |dark + t (thin - dark)|^2 summed over the harmonics, a square in t: its three numbers are summed once
    // (toneSums, from prepare), so a tick while TONE moves costs a square root.
    void toneSums() {
        double plain = 0.0, a0 = 0.0, a1 = 0.0, a2 = 0.0;
        for (int k = 1; static_cast<float>(k) * kToneReferenceHz < 0.5f * sr_; k += 2) {
            const double w = 6.283185307179586 * k * kToneReferenceHz / sr_;
            const std::complex<double> back(std::cos(w), -std::sin(w)); // a sample late
            const double dark1 = dark_, bright1 = bright_;
            const std::complex<double> dark = dark1 / (1.0 - (1.0 - dark1) * back), thin = 1.0 - bright1 / (1.0 - (1.0 - bright1) * back);
            const std::complex<double> d = thin - dark;
            const double size = 1.0 / (static_cast<double>(k) * k);
            plain += size;
            a0 += size * std::norm(dark);
            a1 += size * 2.0 * (dark.real() * d.real() + dark.imag() * d.imag());
            a2 += size * std::norm(d);
        }
        tonePlain_ = plain, toneA0_ = a0, toneA1_ = a1, toneA2_ = a2;
    }
    float makeUpFor(float tilt) const {
        const double t = tilt, kept = toneA0_ + t * (toneA1_ + t * toneA2_);
        return kept > 0.0 ? static_cast<float>(std::sqrt(tonePlain_ / kept)) : 1.f;
    }
    void wake() {
        asleep_ = false;
        quietIn_ = 0;
        arrive();
    }
    void arrive() {
        fuzz_.snap(), tone_.snap(), gate_.snap(), mix_.snap(), late_.snap();
        shape();
        shape_.snap();
    }

    BendSide side_[2];
    float low_[2] = {}, high_[2] = {}, keep_ = 1.f, dark_ = 1.f, bright_ = 1.f;
    double tonePlain_ = 1.0, toneA0_ = 1.0, toneA1_ = 0.0, toneA2_ = 0.0; // toneSums()
    Slide<kShapes> shape_;
    Gentle fuzz_, tone_, gate_;
    Glide mix_, late_;
    int tick_ = 0, quietIn_ = 0;
    bool asleep_ = true;
};

void reading(int, float v, const float*, char* out, int size) { readPercent(v, out, size); }

Effect* create() { return new Fuzz; }

} // namespace

const EffectInfo kFuzzInfo = {
    "FUZZ", 4, {"FUZZ", "TONE", "GATE", "MIX"}, {0.7f, 0.5f, 0.2f, 1.f},
    "fuzz", EffectFamily::Drive, {0, 0, 0, 0}, reading, create, "a fuzz pedal",
};

float fuzzCurve(float x, float gate) { return fuzz(x, gate); }

} // namespace fx
} // namespace apotheca
