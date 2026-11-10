#include "apotheca/engine.hpp"

#include <cmath>
#include <utility>

namespace apotheca {

namespace {
constexpr float kTwoPi = 6.2831853f;

bool validTrack(int track) { return track >= 0 && track < kNumTracks; }
bool validSlot(int slot) { return slot >= 0 && slot < kEffectSlots; }

// Per-sample multiplier that falls to 1/1000 (-60 dB) in `seconds`.
float decayFor(float seconds, float sampleRate) {
    return std::exp(-6.9077553f / (seconds * sampleRate));
}
} // namespace

Engine::Engine() { prepare(48000.f); }

void Engine::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    heldDecay_ = decayFor(2.0f, sampleRate);
    releaseDecay_ = decayFor(0.08f, sampleRate);
    for (Track& track : tracks_) {
        track.voice = Voice();
        for (auto& effect : track.effects) {
            if (effect) {
                effect->prepare(sampleRate);
                effect->reset();
            }
        }
    }
    for (auto& effect : masterEffects_) {
        if (effect) {
            effect->prepare(sampleRate);
            effect->reset();
        }
    }
}

void Engine::setTrackEffect(int track, int slot, std::unique_ptr<Effect> effect) {
    if (!validTrack(track) || !validSlot(slot)) return;
    if (effect) effect->prepare(sampleRate_);
    tracks_[track].effects[slot] = std::move(effect);
}

void Engine::setMasterEffect(int slot, std::unique_ptr<Effect> effect) {
    if (!validSlot(slot)) return;
    if (effect) effect->prepare(sampleRate_);
    masterEffects_[slot] = std::move(effect);
}

float Engine::trackPeak(int track) const {
    return validTrack(track) ? tracks_[track].peak.load(std::memory_order_relaxed) : 0.f;
}

void Engine::apply(const Command& c) {
    switch (c.type) {
    case Command::NoteOn:
        if (validTrack(c.track)) {
            Voice& v = tracks_[c.track].voice;
            const float hz = 440.f * std::pow(2.f, (static_cast<float>(c.a) - 69.f) / 12.f);
            v.phase = 0.f;
            v.increment = hz / sampleRate_;
            v.amplitude = std::isfinite(c.value) ? c.value : 0.f;
            v.decay = heldDecay_;
            v.held = true;
        }
        break;
    case Command::NoteOff:
        if (validTrack(c.track)) {
            tracks_[c.track].voice.held = false;
            tracks_[c.track].voice.decay = releaseDecay_;
        }
        break;
    case Command::SetTrackLevel:
        if (validTrack(c.track) && std::isfinite(c.value)) tracks_[c.track].level = c.value;
        break;
    case Command::SetEffectParam:
        if (!validSlot(c.a)) break;
        if (c.track == kMasterTrack) {
            if (masterEffects_[c.a]) masterEffects_[c.a]->setParam(c.b, c.value);
        } else if (validTrack(c.track)) {
            if (tracks_[c.track].effects[c.a]) tracks_[c.track].effects[c.a]->setParam(c.b, c.value);
        }
        break;
    }
}

void Engine::render(float* interleaved, int frames) {
    Command command;
    while (commands_.pop(command)) apply(command);

    float peaks[kNumTracks] = {};
    while (frames > 0) {
        const int chunk = frames < kMaxBlock ? frames : kMaxBlock;
        renderChunk(interleaved, chunk, peaks);
        interleaved += 2 * chunk;
        frames -= chunk;
    }
    for (int t = 0; t < kNumTracks; ++t) tracks_[t].peak.store(peaks[t], std::memory_order_relaxed);
}

void Engine::renderChunk(float* interleaved, int frames, float* peaks) {
    float mixL[kMaxBlock] = {};
    float mixR[kMaxBlock] = {};
    float left[kMaxBlock];
    float right[kMaxBlock];

    for (int t = 0; t < kNumTracks; ++t) {
        Track& track = tracks_[t];
        Voice& v = track.voice;

        for (int i = 0; i < frames; ++i) {
            const float s = v.amplitude * std::sin(kTwoPi * v.phase);
            left[i] = right[i] = s;
            v.phase += v.increment;
            if (v.phase >= 1.f) v.phase -= 1.f;
            v.amplitude *= v.decay;
        }
        if (v.amplitude < 1e-6f) v.amplitude = 0.f; // also keeps denormals out

        for (auto& effect : track.effects) {
            if (effect) effect->process(left, right, frames);
        }

        float peak = peaks[t];
        for (int i = 0; i < frames; ++i) {
            const float l = left[i] * track.level;
            const float r = right[i] * track.level;
            mixL[i] += l;
            mixR[i] += r;
            const float a = std::fabs(l) > std::fabs(r) ? std::fabs(l) : std::fabs(r);
            if (a > peak) peak = a;
        }
        peaks[t] = peak;
    }

    for (auto& effect : masterEffects_) {
        if (effect) effect->process(mixL, mixR, frames);
    }

    for (int i = 0; i < frames; ++i) {
        interleaved[2 * i] = mixL[i];
        interleaved[2 * i + 1] = mixR[i];
    }
}

} // namespace apotheca
