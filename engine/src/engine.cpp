#include "apotheca/engine.hpp"

#include <cmath>
#include <utility>

namespace apotheca {

namespace {
bool validTrack(int track) { return track >= 0 && track < kNumTracks; }
bool validSlot(int slot) { return slot >= 0 && slot < kEffectSlots; }
} // namespace

Engine::Engine() {
    tracks_[0].hasDrums = true;
    prepare(48000.f);
}

void Engine::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    samplesPerStep_ = static_cast<double>(sampleRate_) * 60.0 / tempo_ / 4.0;
    builtInKit_.buildBuiltIn(sampleRate);
    for (Track& track : tracks_) {
        track.drums.setKit(&builtInKit_);
        track.drums.prepare(sampleRate);
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
    case Command::Trigger:
        if (validTrack(c.track) && tracks_[c.track].hasDrums) tracks_[c.track].drums.trigger(c.a, c.value);
        break;
    case Command::SetStep:
        if (validTrack(c.track) && c.a >= 0 && c.a < kDrumLanes && c.b >= 0 && c.b < kSteps && std::isfinite(c.value)) {
            tracks_[c.track].pattern[c.a][c.b] = c.value < 0.f ? 0.f : (c.value > 1.f ? 1.f : c.value);
        }
        break;
    case Command::Play:
        playing_ = true;
        nextStep_ = 0;
        untilStep_ = 0.0;
        break;
    case Command::Stop:
        playing_ = false;
        playStep_.store(-1, std::memory_order_relaxed);
        break;
    case Command::SetTempo:
        if (std::isfinite(c.value)) {
            tempo_ = c.value < kMinTempo ? kMinTempo : (c.value > kMaxTempo ? kMaxTempo : c.value);
            samplesPerStep_ = static_cast<double>(sampleRate_) * 60.0 / tempo_ / 4.0;
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

void Engine::fireStep() {
    for (Track& track : tracks_) {
        if (!track.hasDrums) continue;
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            const float velocity = track.pattern[lane][nextStep_];
            if (velocity > 0.f) track.drums.trigger(lane, velocity);
        }
    }
    playStep_.store(nextStep_, std::memory_order_relaxed);
    nextStep_ = (nextStep_ + 1) % kSteps;
}

void Engine::render(float* interleaved, int frames) {
    Command command;
    while (commands_.pop(command)) apply(command);

    float peaks[kNumTracks] = {};
    while (frames > 0) {
        int chunk = frames < kMaxBlock ? frames : kMaxBlock;
        if (playing_) {
            // A step starts on its exact sample: the chunk ends where the
            // next step begins.
            if (untilStep_ <= 0.0) {
                fireStep();
                untilStep_ += samplesPerStep_;
            }
            const int toStep = static_cast<int>(std::ceil(untilStep_));
            if (toStep < chunk) chunk = toStep < 1 ? 1 : toStep;
            untilStep_ -= chunk;
        }
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
        if (!track.hasDrums) continue; // an empty track costs nothing

        track.drums.render(left, right, frames);
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
