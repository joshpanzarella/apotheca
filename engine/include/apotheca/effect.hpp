#pragma once
// An effect processes a stereo block in place. Every track has three slots and
// the master has two; an empty slot passes the sound through untouched. Each
// of the two send buses holds one, which is told so (setOnBus).
//
// Rules for anything that implements Effect:
//   - process() runs on the realtime audio thread: no allocation, no locks,
//     no file or screen work.
//   - prepare() is the place to allocate. It is called off the audio thread.

#include <cmath>

namespace apotheca {

class Effect {
public:
    virtual ~Effect() = default;
    virtual void prepare(float sampleRate) { (void)sampleRate; }
    virtual void reset() {}
    virtual void setTempo(float bpm) { (void)bpm; } // for effects that keep time. Realtime safe.
    // "You are on a send bus": give back only what you add (the echoes, the
    // room), none of the sound that came in, and read MIX as how loud that is.
    // Every delay and reverb must honour it (effects_test checks each one in
    // those groups); the engine puts nothing else on a bus. Not realtime: it
    // is said once, before the effect goes in.
    virtual void setOnBus(bool on) { (void)on; }
    // "These lanes of the first drum track are hit on the first sample of the
    // block you are about to be given": bit 0 is lane 1. Only DUCK listens.
    // Realtime safe; called by whichever thread is about to call process().
    virtual void drumHits(unsigned lanes) { (void)lanes; }
    virtual void setParam(int index, float value) = 0;
    virtual void process(float* left, float* right, int frames) = 0;
    // What it has to show beside its settings, as of the last process():
    // for COMP, how many dB it is turning the sound down. 0 for an effect
    // with nothing to show. The audio thread reads it after process() and
    // keeps it where the screen can ask (Engine::effectMeter): nothing else
    // may call it while the effect is in a slot.
    virtual float meter() const { return 0.f; }
    // The EffectKind makeEffect made it as (0: one made by hand). Set before it goes in a slot and never
    // changed there; the audio thread keeps it where any thread can ask (Engine::effectKindIn).
    int madeAs = 0;
};

// Plain volume. Param 0: linear gain (1 = unchanged).
class GainEffect : public Effect {
public:
    void setParam(int index, float value) override {
        if (index == 0 && std::isfinite(value)) gain_ = value;
    }
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) {
            left[i] *= gain_;
            right[i] *= gain_;
        }
    }

private:
    float gain_ = 1.f;
};

// One-pole lowpass: rolls off the top end at 6 dB per octave.
// Param 0: cutoff in Hz.
class LowpassEffect : public Effect {
public:
    void prepare(float sampleRate) override {
        sampleRate_ = sampleRate;
        update();
    }
    void reset() override { stateL_ = stateR_ = 0.f; }
    void setParam(int index, float value) override {
        if (index == 0 && std::isfinite(value)) {
            cutoffHz_ = value;
            update();
        }
    }
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) {
            stateL_ += coeff_ * (left[i] - stateL_);
            stateR_ += coeff_ * (right[i] - stateR_);
            left[i] = stateL_;
            right[i] = stateR_;
        }
    }

private:
    void update() {
        float hz = cutoffHz_;
        if (hz < 10.f) hz = 10.f;
        if (hz > 0.45f * sampleRate_) hz = 0.45f * sampleRate_;
        coeff_ = 1.f - std::exp(-6.2831853f * hz / sampleRate_);
    }

    float sampleRate_ = 48000.f;
    float cutoffHz_ = 20000.f;
    float coeff_ = 1.f;
    float stateL_ = 0.f;
    float stateR_ = 0.f;
};

} // namespace apotheca
