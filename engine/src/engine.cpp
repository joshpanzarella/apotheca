#include "apotheca/engine.hpp"

#include <cmath>
#include <utility>

namespace apotheca {

namespace {
bool validTrack(int track) { return track >= 0 && track < kNumTracks; }
bool validSlot(int slot) { return slot >= 0 && slot < kEffectSlots; }

// The ceiling: untouched up to 0.8, then rounded off toward 1.0.
float ceiling(float x) {
    const float knee = 0.8f;
    const float a = std::fabs(x);
    if (a <= knee) return x;
    const float y = knee + (1.f - knee) * std::tanh((a - knee) / (1.f - knee));
    return x < 0.f ? -y : y;
}
} // namespace

Engine::Engine() {
    for (Track& track : tracks_)
        for (auto& lane : track.chance)
            for (float& c : lane) c = 1.f;
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
        if (track.synth) track.synth->prepare(sampleRate);
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

bool Engine::addSynthTrack(int track) {
    if (!validTrack(track) || tracks_[track].hasInstrument()) return false;
    std::unique_ptr<CzSynth> synth(new CzSynth);
    if (!synth->ok()) return false;
    synth->prepare(sampleRate_);
    tracks_[track].synth = std::move(synth);
    return true;
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
    case Command::SetStepNudge:
        if (validTrack(c.track) && c.a >= 0 && c.a < kDrumLanes && c.b >= 0 && c.b < kSteps && std::isfinite(c.value)) {
            tracks_[c.track].nudge[c.a][c.b] = c.value < -0.5f ? -0.5f : (c.value > 0.5f ? 0.5f : c.value);
        }
        break;
    case Command::SetStepChance:
        if (validTrack(c.track) && c.a >= 0 && c.a < kDrumLanes && c.b >= 0 && c.b < kSteps && std::isfinite(c.value)) {
            tracks_[c.track].chance[c.a][c.b] = c.value < 0.f ? 0.f : (c.value > 1.f ? 1.f : c.value);
        }
        break;
    case Command::NoteOn:
        if (validTrack(c.track) && tracks_[c.track].synth) tracks_[c.track].synth->noteOn(c.a, c.value);
        break;
    case Command::NoteOff:
        if (validTrack(c.track) && tracks_[c.track].synth) tracks_[c.track].synth->noteOff(c.a);
        break;
    case Command::SelectPreset:
        if (validTrack(c.track) && tracks_[c.track].synth) tracks_[c.track].synth->selectPreset(c.a);
        break;
    case Command::Play:
        playing_ = true;
        nextStep_ = 0;
        untilStep_ = 0.0;
        clearPending();
        break;
    case Command::Stop:
        playing_ = false; // waiting hits only play while playing, and Play clears them
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
    case Command::SetLaneLevel:
        if (validTrack(c.track)) tracks_[c.track].drums.setLaneLevel(c.a, c.value);
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

void Engine::clearPending() {
    for (Track& track : tracks_)
        for (auto& lane : track.pending)
            for (auto& p : lane) p = Track::Pending();
}

// Roll for the hit, then play it now or put it in a waiting slot.
void Engine::schedule(Track& track, int lane, int step, double delaySamples) {
    const float velocity = track.pattern[lane][step];
    const float chance = track.chance[lane][step];
    if (chance < 1.f) {
        dice_ = dice_ * 1664525u + 1013904223u;
        if (static_cast<float>(dice_ >> 8) * (1.f / 16777216.f) >= chance) return;
    }
    if (delaySamples < 1.0) {
        track.drums.trigger(lane, velocity);
        return;
    }
    for (auto& p : track.pending[lane]) {
        if (p.velocity <= 0.f) {
            p.samplesLeft = delaySamples;
            p.velocity = velocity;
            return;
        }
    }
}

void Engine::firePending() {
    for (Track& track : tracks_) {
        if (!track.hasDrums) continue;
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            for (auto& p : track.pending[lane]) {
                if (p.velocity > 0.f && p.samplesLeft < 1.0) {
                    track.drums.trigger(lane, p.velocity);
                    p = Track::Pending();
                }
            }
        }
    }
}

// Called on the first sample of step `nextStep_`. Hits on the grid play now.
// A late hit of this step, and an early hit of the next step, are both
// somewhere inside the step that is starting, so they are set to wait.
void Engine::fireStep() {
    const int following = (nextStep_ + 1) % kSteps;
    for (Track& track : tracks_) {
        if (!track.hasDrums) continue;
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            if (track.pattern[lane][nextStep_] > 0.f && track.nudge[lane][nextStep_] >= 0.f) {
                schedule(track, lane, nextStep_, track.nudge[lane][nextStep_] * samplesPerStep_);
            }
            if (track.pattern[lane][following] > 0.f && track.nudge[lane][following] < 0.f) {
                schedule(track, lane, following, (1.0 + track.nudge[lane][following]) * samplesPerStep_);
            }
        }
    }
    playStep_.store(nextStep_, std::memory_order_relaxed);
    nextStep_ = following;
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
            firePending();
            if (untilStep_ <= 0.0) {
                fireStep();
                untilStep_ += samplesPerStep_;
            }
            // Stop the chunk at whichever comes first: the next step, or the
            // next waiting hit.
            double next = untilStep_;
            for (Track& track : tracks_) {
                if (!track.hasDrums) continue;
                for (auto& lane : track.pending)
                    for (auto& p : lane)
                        if (p.velocity > 0.f && p.samplesLeft < next) next = p.samplesLeft;
            }
            const int toNext = static_cast<int>(std::ceil(next));
            if (toNext < chunk) chunk = toNext < 1 ? 1 : toNext;
            untilStep_ -= chunk;
            for (Track& track : tracks_) {
                if (!track.hasDrums) continue;
                for (auto& lane : track.pending)
                    for (auto& p : lane)
                        if (p.velocity > 0.f) p.samplesLeft -= chunk;
            }
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
        if (!track.hasInstrument()) continue; // an empty track costs nothing

        if (track.synth) track.synth->render(left, right, frames);
        else track.drums.render(left, right, frames);
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
        interleaved[2 * i] = ceiling(mixL[i]);
        interleaved[2 * i + 1] = ceiling(mixR[i]);
    }
}

} // namespace apotheca
