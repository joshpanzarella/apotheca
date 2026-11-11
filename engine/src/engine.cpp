#include "apotheca/engine.hpp"

#include <cmath>
#include <utility>

namespace apotheca {

namespace {
bool validTrack(int track) { return track >= 0 && track < kNumTracks; }
bool validSlot(int slot) { return slot >= 0 && slot < kEffectSlots; }
bool validScene(int scene) { return scene >= 0 && scene < kMaxScenes; }
// A bar of a scene, as the pattern commands name it. -1 if there is no such bar.
int slotFor(const Command& c) { return validScene(c.scene) && c.bar >= 0 && c.bar < kMaxBars ? c.scene * kMaxBars + c.bar : -1; }
bool validCell(int lane, int step) { return lane >= 0 && lane < kDrumLanes && step >= 0 && step < kSteps; }

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
        for (int slot = 0; slot < kPatternSlots; ++slot)
            for (auto& lane : track.drumScenes[slot].chance)
                for (float& c : lane) c = 1.f;
    for (int& r : repeats_) r = 1;
    caught_.reset(new std::atomic<float>[kCatchFrames]);
    for (int i = 0; i < kCatchFrames; ++i) caught_[i].store(0.f, std::memory_order_relaxed);
    for (auto& s : scope_) s.store(0.f, std::memory_order_relaxed);
    tracks_[0].hasDrums = true;
    claimed_[0] = true;
    claimedDrums_[0] = true;
    prepare(48000.f);
}

Engine::~Engine() {
    // Anything sent but never taken up (no render() since) is still ours.
    Command command;
    while (commands_.pop(command)) {
        if (command.type == Command::InstallSynth) delete static_cast<Instrument*>(command.pointer);
        if (command.type == Command::SetTrackImage) delete static_cast<TrackImage*>(command.pointer);
        if (command.type == Command::SetLaneSample) delete static_cast<SampleData*>(command.pointer);
        if (command.type == Command::InstallEffect) delete static_cast<Effect*>(command.pointer);
        if (command.type == Command::SetTrackSample) delete static_cast<SampleData*>(command.pointer);
    }
    collectGarbage();
    for (Track& track : tracks_)
        for (SampleData* sample : track.laneSamples) delete sample;
    for (Track& track : tracks_) delete track.trackSample;
}

void Engine::destroy(const Trash& trash) {
    if (trash.isEffect) delete static_cast<Effect*>(trash.pointer);
    else if (trash.isSample) delete static_cast<SampleData*>(trash.pointer);
    else if (trash.isImage) delete static_cast<TrackImage*>(trash.pointer);
    else delete static_cast<Instrument*>(trash.pointer);
}

void Engine::collectGarbage() {
    Trash trash;
    while (trash_.pop(trash)) destroy(trash);
}

bool Engine::setEffect(int track, int slot, EffectKind kind) {
    if ((track != kMasterTrack && (track < 0 || track >= kNumTracks)) || slot < 0 || slot >= kEffectSlots) return false;
    std::unique_ptr<Effect> effect = makeEffect(kind, sampleRate_, 120.f); // the audio thread tells it the real tempo
    Command c;
    c.type = Command::InstallEffect;
    c.track = track;
    c.a = slot;
    c.pointer = effect.get();
    if (!commands_.push(c)) return false;
    effect.release(); // the audio thread hands the old one back through the trash
    return true;
}

bool Engine::setTrackSample(int track, std::unique_ptr<SampleData> sample) {
    if (track < 0 || track >= kNumTracks || !claimed_[track] || claimedDrums_[track]) return false;
    Command c;
    c.type = Command::SetTrackSample;
    c.track = track;
    c.pointer = sample.get();
    if (!commands_.push(c)) return false;
    sample.release();
    return true;
}

bool Engine::setLaneSample(int track, int lane, std::unique_ptr<SampleData> sample) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || !claimedDrums_[track]) return false;
    Command c;
    c.type = Command::SetLaneSample;
    c.track = track;
    c.a = lane;
    c.pointer = sample.get();
    if (!commands_.push(c)) return false;
    sample.release(); // the audio thread hands the old one back through the trash
    return true;
}

std::vector<float> Engine::lastBar() const {
    const std::uint64_t now = caughtCount_.load(std::memory_order_relaxed);
    std::uint64_t end = barEndedAt_.load(std::memory_order_relaxed);
    int frames = barFrames_.load(std::memory_order_relaxed);
    // No whole bar, or one so long ago that the ring has gone round past it:
    // the last bar's worth up to now.
    if (frames <= 0 || now - (end - static_cast<std::uint64_t>(frames)) > static_cast<std::uint64_t>(kCatchFrames)) {
        frames = barFramesNow_.load(std::memory_order_relaxed);
        if (frames > kCatchFrames) frames = kCatchFrames;
        end = now;
    }
    std::vector<float> out(static_cast<std::size_t>(frames), 0.f);
    for (int i = 0; i < frames; ++i) {
        const std::uint64_t back = static_cast<std::uint64_t>(frames - i);
        if (back > end) continue; // before the engine began: silence
        out[static_cast<std::size_t>(i)] = caught_[(end - back) % kCatchFrames].load(std::memory_order_relaxed);
    }
    return out;
}

bool Engine::removeTrack(int track) {
    if (!validTrack(track) || !claimed_[track]) return false;
    Command c;
    c.type = Command::RemoveInstrument;
    c.track = track;
    if (!send(c)) return false;
    claimed_[track] = false;
    claimedDrums_[track] = false;
    return true;
}

bool Engine::setTrackImage(int track, std::unique_ptr<TrackImage> image) {
    if (!validTrack(track) || !image) return false;
    Command c;
    c.type = Command::SetTrackImage;
    c.track = track;
    c.pointer = image.get();
    if (!send(c)) return false;
    image.release(); // the audio thread hands it back through the trash
    return true;
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

bool Engine::addDrumTrack(int track) {
    if (!validTrack(track) || claimed_[track]) return false;
    Command c;
    c.type = Command::InstallDrums;
    c.track = track;
    if (!send(c)) return false;
    claimed_[track] = true;
    claimedDrums_[track] = true;
    return true;
}

bool Engine::addSynthTrack(int track, SynthKind kind) {
    if (!validTrack(track) || claimed_[track]) return false;
    std::unique_ptr<Instrument> synth;
    if (kind == SynthKind::Twin) synth.reset(new Twin);
    else if (kind == SynthKind::Sampler) synth.reset(new Sampler);
    else synth.reset(new Phazed);
    if (!synth->ok()) return false;
    synth->prepare(sampleRate_);
    Command c;
    c.type = Command::InstallSynth;
    c.track = track;
    c.pointer = synth.get();
    if (!send(c)) return false;
    synth.release(); // the audio thread owns it from here
    claimed_[track] = true;
    return true;
}

float Engine::trackPeak(int track) const {
    return validTrack(track) ? tracks_[track].peak.load(std::memory_order_relaxed) : 0.f;
}

void Engine::readScope(float* out, int count) const {
    if (count > kScopeSize) count = kScopeSize;
    const unsigned written = scopeWritten_.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        out[i] = scope_[(written - static_cast<unsigned>(count) + static_cast<unsigned>(i)) % kScopeSize].load(std::memory_order_relaxed);
    }
}

void Engine::letGoOfNotes() {
    for (Track& track : tracks_) {
        if (!track.synth) continue;
        track.synth->allNotesOff();
        for (double& off : track.offIn) off = kNoNote;
    }
}

// Play a pattern's note now, or set it waiting.
void Engine::startNote(Track& track, const Note& note, double delaySamples) {
    if (delaySamples < 1.0) {
        if (track.offIn[note.pitch] < kNoNote) track.synth->noteOff(note.pitch); // the same key again
        track.synth->noteOn(note.pitch, note.velocity);
        track.offIn[note.pitch] = static_cast<double>(note.length) * samplesPerStep_;
        return;
    }
    for (auto& p : track.pendingNotes) {
        if (p.velocity > 0.f) continue;
        p.samplesLeft = delaySamples;
        p.pitch = note.pitch;
        p.velocity = note.velocity;
        p.length = note.length;
        return;
    }
}

void Engine::firePendingNotes() {
    for (Track& track : tracks_) {
        if (!track.synth) continue;
        for (auto& p : track.pendingNotes) {
            if (p.velocity <= 0.f || p.samplesLeft >= 1.0) continue;
            Note note;
            note.pitch = p.pitch;
            note.velocity = p.velocity;
            note.length = p.length;
            p = Track::PendingNote();
            startNote(track, note, 0.0);
        }
    }
}

// Let go of the pattern's notes whose moment has come.
void Engine::fireNoteOffs() {
    for (Track& track : tracks_) {
        if (!track.synth) continue;
        for (int pitch = 0; pitch < 128; ++pitch) {
            if (track.offIn[pitch] < 1.0) {
                track.synth->noteOff(pitch);
                track.offIn[pitch] = kNoNote;
            }
        }
    }
}

void Engine::apply(const Command& c) {
    // After a panic, with the buttons let go, the next thing asked for is heard.
    // Catch gives the last whole bar the song played. That bar stops being
    // "the last thing heard" when the song starts again, when something is
    // played by hand after it stopped, or on a panic.
    if (c.type == Command::Play || c.type == Command::Panic ||
        (!playing_ && (c.type == Command::Trigger || c.type == Command::NoteOn))) {
        barFrames_.store(0, std::memory_order_relaxed);
    }
    if (gate_ == Gate::Shut && !panicHeld_ && !cutEverything_ &&
        (c.type == Command::Play || c.type == Command::Trigger || c.type == Command::NoteOn)) {
        gate_ = Gate::Opening;
    }
    switch (c.type) {
    case Command::Trigger:
        if (validTrack(c.track) && tracks_[c.track].hasDrums) tracks_[c.track].drums.trigger(c.a, c.value, static_cast<float>(c.b) * 1e-4f);
        break;
    case Command::SetStepStart:
        if (validTrack(c.track) && slotFor(c) >= 0 && validCell(c.a, c.b) && std::isfinite(c.value)) {
            tracks_[c.track].drumScenes[slotFor(c)].start[c.a][c.b] = c.value < 0.f ? 0.f : (c.value > 1.f ? 1.f : c.value);
        }
        break;
    case Command::InstallEffect: {
        Trash trash;
        trash.isEffect = true;
        if ((c.track != kMasterTrack && !validTrack(c.track)) || !validSlot(c.a)) {
            trash.pointer = c.pointer; // nowhere to put it
            if (trash.pointer) trash_.push(trash);
            break;
        }
        std::unique_ptr<Effect>& place = c.track == kMasterTrack ? masterEffects_[c.a] : tracks_[c.track].effects[c.a];
        trash.pointer = place.release();
        place.reset(static_cast<Effect*>(c.pointer));
        if (place) place->setTempo(tempo_);
        if (trash.pointer) trash_.push(trash); // if full, it is leaked rather than freed here
        break;
    }
    case Command::SetTrackSample: {
        SampleData* incoming = static_cast<SampleData*>(c.pointer);
        Trash trash;
        trash.isSample = true;
        if (validTrack(c.track) && tracks_[c.track].synth) {
            Track& track = tracks_[c.track];
            trash.pointer = track.trackSample;
            track.trackSample = incoming;
            track.synth->setSample(incoming ? incoming->data.data() : nullptr, incoming ? static_cast<int>(incoming->data.size()) : 0);
        } else {
            trash.pointer = incoming;
        }
        if (trash.pointer) trash_.push(trash);
        break;
    }
    case Command::SetLaneSample: {
        SampleData* incoming = static_cast<SampleData*>(c.pointer);
        Trash trash;
        trash.isSample = true;
        if (validTrack(c.track) && tracks_[c.track].hasDrums && c.a >= 0 && c.a < kDrumLanes) {
            Track& track = tracks_[c.track];
            trash.pointer = track.laneSamples[c.a];
            track.laneSamples[c.a] = incoming;
            track.drums.setLaneSample(c.a, incoming ? incoming->data.data() : nullptr, incoming ? static_cast<int>(incoming->data.size()) : 0);
        } else {
            trash.pointer = incoming; // nowhere to put it
        }
        if (trash.pointer) trash_.push(trash); // if full, it is leaked rather than freed here
        break;
    }
    case Command::SetStep:
        if (validTrack(c.track) && slotFor(c) >= 0 && validCell(c.a, c.b) && std::isfinite(c.value)) {
            tracks_[c.track].drumScenes[slotFor(c)].level[c.a][c.b] = c.value < 0.f ? 0.f : (c.value > 1.f ? 1.f : c.value);
        }
        break;
    case Command::SetStepNudge:
        if (validTrack(c.track) && slotFor(c) >= 0 && validCell(c.a, c.b) && std::isfinite(c.value)) {
            tracks_[c.track].drumScenes[slotFor(c)].nudge[c.a][c.b] = c.value < -0.5f ? -0.5f : (c.value > 0.5f ? 0.5f : c.value);
        }
        break;
    case Command::SetStepChance:
        if (validTrack(c.track) && slotFor(c) >= 0 && validCell(c.a, c.b) && std::isfinite(c.value)) {
            tracks_[c.track].drumScenes[slotFor(c)].chance[c.a][c.b] = c.value < 0.f ? 0.f : (c.value > 1.f ? 1.f : c.value);
        }
        break;
    case Command::SetNote:
        if (validTrack(c.track) && slotFor(c) >= 0 && c.a >= 0 && c.a < kMaxNotes && std::isfinite(c.value)) {
            Note& note = tracks_[c.track].notes[slotFor(c)][c.a];
            float length = static_cast<float>(c.d) + (std::isfinite(c.extra) ? c.extra : 0.f);
            if (length > 255.f) length = 255.f;
            const bool sound = c.value > 0.f && c.b >= 0 && c.b < kSteps && c.c >= 0 && c.c <= 127 && c.d >= 0 && length >= kShortestNote;
            note.velocity = sound ? (c.value > 1.f ? 1.f : c.value) : 0.f;
            note.step = static_cast<std::uint8_t>(sound ? c.b : 0);
            note.pitch = static_cast<std::uint8_t>(sound ? c.c : 0);
            note.length = sound ? length : 0.f;
            note.nudge = sound && std::isfinite(c.shift) ? (c.shift < -0.5f ? -0.5f : (c.shift > 0.5f ? 0.5f : c.shift)) : 0.f;
        }
        break;
    case Command::SetSceneCount:
        sceneCount_ = c.a < 1 ? 1 : (c.a > kMaxScenes ? kMaxScenes : c.a);
        break;
    case Command::SetSceneRepeats:
        if (validScene(c.scene)) repeats_[c.scene] = c.a < 1 ? 1 : (c.a > kMaxRepeats ? kMaxRepeats : c.a);
        break;
    case Command::ClearCell:
        if (validTrack(c.track) && validScene(c.scene)) {
            Track& track = tracks_[c.track];
            for (int slot = c.scene * kMaxBars; slot < (c.scene + 1) * kMaxBars; ++slot) {
                DrumScene& scene = track.drumScenes[slot];
                for (int lane = 0; lane < kDrumLanes; ++lane)
                    for (int step = 0; step < kSteps; ++step) {
                        scene.level[lane][step] = scene.nudge[lane][step] = scene.start[lane][step] = 0.f;
                        scene.chance[lane][step] = 1.f;
                    }
                for (Note& note : track.notes[slot]) note = Note();
            }
        }
        break;
    case Command::SetCellBars:
        if (validTrack(c.track) && validScene(c.scene)) tracks_[c.track].bars[c.scene] = static_cast<std::uint8_t>(c.a < 1 ? 1 : (c.a > kMaxBars ? kMaxBars : c.a));
        break;
    case Command::Play:
        playing_ = true;
        loopScene_ = c.b != 0;
        scene_ = validScene(c.a) ? c.a : 0;
        if (!loopScene_ && scene_ >= sceneCount_) scene_ = 0;
        repeat_ = 0;
        bar_ = 0;
        nextStep_ = 0;
        untilStep_ = 0.0;
        firstStep_ = true;
        clearPending();
        letGoOfNotes();
        break;
    case Command::Stop:
        playing_ = false; // waiting hits only play while playing, and Play clears them
        letGoOfNotes();
        playStep_.store(-1, std::memory_order_relaxed);
        playScene_.store(-1, std::memory_order_relaxed);
        break;
    case Command::Panic:
        panicHeld_ = c.a != 0;
        if (!panicHeld_) break;
        playing_ = false;
        clearPending();
        letGoOfNotes();
        playStep_.store(-1, std::memory_order_relaxed);
        playScene_.store(-1, std::memory_order_relaxed);
        if (gate_ != Gate::Shut) gate_ = Gate::Closing;
        silenced_.store(true, std::memory_order_relaxed);
        break;
    case Command::SetVolume:
        if (std::isfinite(c.value)) volume_ = c.value < 0.f ? 0.f : (c.value > 1.f ? 1.f : c.value);
        break;
    case Command::SetTempo:
        if (std::isfinite(c.value)) {
            tempo_ = c.value < kMinTempo ? kMinTempo : (c.value > kMaxTempo ? kMaxTempo : c.value);
            samplesPerStep_ = static_cast<double>(sampleRate_) * 60.0 / tempo_ / 4.0;
            // Effects that keep time (the delay) follow.
            for (Track& track : tracks_)
                for (auto& effect : track.effects)
                    if (effect) effect->setTempo(tempo_);
            for (auto& effect : masterEffects_)
                if (effect) effect->setTempo(tempo_);
        }
        break;
    case Command::SetTrackLevel:
        if (validTrack(c.track) && std::isfinite(c.value)) tracks_[c.track].level = c.value;
        break;
    case Command::SetLaneLevel:
        if (validTrack(c.track)) tracks_[c.track].drums.setLaneLevel(c.a, c.value);
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
    case Command::SetMacro:
        if (validTrack(c.track) && tracks_[c.track].synth) tracks_[c.track].synth->setMacro(c.a, c.value);
        break;
    case Command::SetSetting:
        if (validTrack(c.track) && tracks_[c.track].synth) tracks_[c.track].synth->setSetting(c.a, c.b);
        break;
    case Command::InstallSynth:
        // addSynthTrack() only sends this for a track it found empty.
        if (validTrack(c.track) && !tracks_[c.track].hasInstrument()) {
            tracks_[c.track].synth.reset(static_cast<Instrument*>(c.pointer));
        }
        break;
    case Command::InstallDrums:
        if (validTrack(c.track) && !tracks_[c.track].hasInstrument()) tracks_[c.track].hasDrums = true;
        break;
    case Command::RemoveInstrument:
        if (validTrack(c.track)) {
            Track& track = tracks_[c.track];
            if (track.synth) {
                // Freeing is not for this thread: hand it back. If the trash is
                // full (it never should be) the synth is kept rather than lost.
                Trash trash;
                trash.pointer = track.synth.get();
                if (trash_.push(trash)) track.synth.release();
                else break;
            }
            if (track.trackSample) { // the synth that read it has gone
                Trash trash;
                trash.isSample = true;
                trash.pointer = track.trackSample;
                track.trackSample = nullptr;
                trash_.push(trash);
            }
            track.hasDrums = false;
            track.drums.reset();
            for (int lane = 0; lane < kDrumLanes; ++lane) {
                if (!track.laneSamples[lane]) continue;
                Trash trash;
                trash.isSample = true;
                trash.pointer = track.laneSamples[lane];
                track.drums.setLaneSample(lane, nullptr, 0);
                track.laneSamples[lane] = nullptr;
                trash_.push(trash);
            }
            for (int lane = 0; lane < kDrumLanes; ++lane) track.drums.setLaneLevel(lane, 1.f);
            for (int slot = 0; slot < kPatternSlots; ++slot) {
                DrumScene& scene = track.drumScenes[slot];
                for (int lane = 0; lane < kDrumLanes; ++lane)
                    for (int step = 0; step < kSteps; ++step) {
                        scene.level[lane][step] = scene.nudge[lane][step] = scene.start[lane][step] = 0.f;
                        scene.chance[lane][step] = 1.f;
                    }
            }
            for (int slot = 0; slot < kPatternSlots; ++slot)
                for (Note& note : track.notes[slot]) note = Note();
            for (auto& b : track.bars) b = 1;
            for (double& off : track.offIn) off = kNoNote;
            for (auto& p : track.pendingNotes) p = Track::PendingNote();
            for (auto& lane : track.pending)
                for (auto& p : lane) p = Track::Pending();
        }
        break;
    case Command::SetTrackImage:
        if (c.pointer) {
            const TrackImage& image = *static_cast<TrackImage*>(c.pointer);
            if (validTrack(c.track)) {
                Track& track = tracks_[c.track];
                for (int scene = 0; scene < kMaxScenes; ++scene) track.bars[scene] = static_cast<std::uint8_t>(image.bars[scene] < 1 ? 1 : (image.bars[scene] > kMaxBars ? kMaxBars : image.bars[scene]));
                for (int scene = 0; scene < kPatternSlots; ++scene) {
                    for (int lane = 0; lane < kDrumLanes; ++lane)
                        for (int step = 0; step < kSteps; ++step) {
                            track.drumScenes[scene].level[lane][step] = image.level[scene][lane][step];
                            track.drumScenes[scene].nudge[lane][step] = image.nudge[scene][lane][step];
                            track.drumScenes[scene].chance[lane][step] = image.chance[scene][lane][step];
                            track.drumScenes[scene].start[lane][step] = image.start[scene][lane][step];
                        }
                    for (int slot = 0; slot < kMaxNotes; ++slot) {
                        const TrackImage::Note& from = image.notes[scene][slot];
                        Note& to = track.notes[scene][slot];
                        to.step = from.step;
                        to.pitch = from.pitch;
                        to.length = from.length;
                        to.nudge = from.nudge;
                        to.velocity = from.velocity;
                    }
                }
                for (int lane = 0; lane < kDrumLanes; ++lane) track.drums.setLaneLevel(lane, image.laneLevel[lane]);
            }
            Trash trash;
            trash.isImage = true;
            trash.pointer = c.pointer;
            trash_.push(trash); // if full, the image is leaked rather than freed here
        }
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
        for (auto& p : track.pendingNotes) p = Track::PendingNote();
    for (Track& track : tracks_)
        for (auto& lane : track.pending)
            for (auto& p : lane) p = Track::Pending();
}

// Roll for the hit, then play it now or put it in a waiting slot.
void Engine::schedule(Track& track, int lane, const DrumScene& scene, int step, double delaySamples) {
    const float velocity = scene.level[lane][step];
    const float chance = scene.chance[lane][step];
    if (chance < 1.f) {
        dice_ = dice_ * 1664525u + 1013904223u;
        if (static_cast<float>(dice_ >> 8) * (1.f / 16777216.f) >= chance) return;
    }
    const float start = scene.start[lane][step];
    if (delaySamples < 1.0) {
        track.drums.trigger(lane, velocity, start);
        return;
    }
    for (auto& p : track.pending[lane]) {
        if (p.velocity <= 0.f) {
            p.samplesLeft = delaySamples;
            p.velocity = velocity;
            p.start = start;
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
                    track.drums.trigger(lane, p.velocity, p.start);
                    p = Track::Pending();
                }
            }
        }
    }
}

// Where the song is once the bar now playing ends.
// A scene is as long as the longest cell in it.
int Engine::sceneBars(int scene) const {
    int bars = 1;
    for (const Track& track : tracks_)
        if (track.hasInstrument() && track.bars[scene] > bars) bars = track.bars[scene];
    return bars;
}

Engine::Position Engine::afterThisBar() const {
    if (bar_ + 1 < sceneBars(scene_)) return {scene_, repeat_, bar_ + 1}; // further into the scene
    if (loopScene_) return {scene_, repeat_, 0};
    if (repeat_ + 1 < repeats_[scene_]) return {scene_, repeat_ + 1, 0};
    return {(scene_ + 1) % sceneCount_, 0, 0};
}

// Called on the first sample of step `nextStep_`. Hits on the grid play now.
// A late hit of this step, and an early hit of the next step, are both
// somewhere inside the step that is starting, so they are set to wait.
void Engine::fireStep() {
    // Step 1 of a new bar: move on through the song (not on the very first
    // step after Play, which is where the song starts).
    // Bar lines, for Catch: where the bar that has just ended began and ended.
    if (nextStep_ == 0) {
        const std::uint64_t now = caughtCount_.load(std::memory_order_relaxed);
        if (!firstStep_ && now - barBeganAt_ <= static_cast<std::uint64_t>(kCatchFrames)) {
            barFrames_.store(static_cast<int>(now - barBeganAt_), std::memory_order_relaxed);
            barEndedAt_.store(now, std::memory_order_relaxed);
        }
        barBeganAt_ = now;
    }
    if (nextStep_ == 0 && !firstStep_) {
        const Position next = afterThisBar();
        scene_ = next.scene;
        repeat_ = next.repeat;
        bar_ = next.bar;
    }
    firstStep_ = false;

    const int following = (nextStep_ + 1) % kSteps;
    // An early hit on step 1 belongs to whichever scene the next bar plays.
    const Position after = afterThisBar();
    const int followingScene = following == 0 ? after.scene : scene_;
    const int followingBar = following == 0 ? after.bar : bar_;

    for (Track& track : tracks_) {
        if (track.hasDrums) {
            const DrumScene& now = track.drumScenes[slotOf(track, scene_, bar_)];
            const DrumScene& then = track.drumScenes[slotOf(track, followingScene, followingBar)];
            for (int lane = 0; lane < kDrumLanes; ++lane) {
                if (now.level[lane][nextStep_] > 0.f && now.nudge[lane][nextStep_] >= 0.f) {
                    schedule(track, lane, now, nextStep_, now.nudge[lane][nextStep_] * samplesPerStep_);
                }
                if (then.level[lane][following] > 0.f && then.nudge[lane][following] < 0.f) {
                    schedule(track, lane, then, following, (1.0 + then.nudge[lane][following]) * samplesPerStep_);
                }
            }
        } else if (track.synth) {
            // The notes whose time was up have been let go already
            // (fireNoteOffs). Start this step's.
            // A late note waits; an early one belongs to the step after
            // this and is set waiting now, as a drum hit is.
            for (const Note& note : track.notes[slotOf(track, scene_, bar_)]) {
                if (note.velocity > 0.f && note.step == nextStep_ && note.nudge >= 0.f) startNote(track, note, note.nudge * samplesPerStep_);
            }
            for (const Note& note : track.notes[slotOf(track, followingScene, followingBar)]) {
                if (note.velocity > 0.f && note.step == following && note.nudge < 0.f) startNote(track, note, (1.0 + note.nudge) * samplesPerStep_);
            }
        }
    }
    playStep_.store(nextStep_, std::memory_order_relaxed);
    playScene_.store(scene_, std::memory_order_relaxed);
    playRepeat_.store(repeat_, std::memory_order_relaxed);
    playBar_.store(bar_, std::memory_order_relaxed);
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
            fireNoteOffs(); // before the step, so a note that ends where the next begins is let go first
            firePendingNotes();
            if (untilStep_ <= 0.0) {
                fireStep();
                untilStep_ += samplesPerStep_;
            }
            // Stop the chunk at whichever comes first: the next step, the
            // next waiting hit, or the next note to be let go.
            double next = untilStep_;
            for (Track& track : tracks_) {
                if (track.synth) {
                    for (double off : track.offIn)
                        if (off < next) next = off;
                    for (const auto& p : track.pendingNotes)
                        if (p.velocity > 0.f && p.samplesLeft < next) next = p.samplesLeft;
                }
                if (!track.hasDrums) continue;
                for (auto& lane : track.pending)
                    for (auto& p : lane)
                        if (p.velocity > 0.f && p.samplesLeft < next) next = p.samplesLeft;
            }
            const int toNext = static_cast<int>(std::ceil(next));
            if (toNext < chunk) chunk = toNext < 1 ? 1 : toNext;
            untilStep_ -= chunk;
            for (Track& track : tracks_) {
                if (track.synth) {
                    for (double& off : track.offIn)
                        if (off < kNoNote) off -= chunk;
                    for (auto& p : track.pendingNotes)
                        if (p.velocity > 0.f) p.samplesLeft -= chunk;
                }
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
    const int scopeTrack = scopeTrack_.load(std::memory_order_relaxed);

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

        if (t == scopeTrack) {
            unsigned written = scopeWritten_.load(std::memory_order_relaxed);
            for (int i = 0; i < frames; ++i) {
                scope_[written++ % kScopeSize].store(0.5f * (left[i] + right[i]) * track.level, std::memory_order_relaxed);
            }
            scopeWritten_.store(written, std::memory_order_relaxed);
        }
    }

    for (auto& effect : masterEffects_) {
        if (effect) effect->process(mixL, mixR, frames);
    }

    // Remember what goes out, for Catch.
    // The volume: after everything, Catch included. It glides to a new
    // setting over a few milliseconds.
    const auto turnDown = [&] {
        if (volume_ == 1.f && volumeNow_ == 1.f) return;
        for (int i = 0; i < frames; ++i) {
            volumeNow_ += (volume_ - volumeNow_) * 0.005f;
            if (std::fabs(volume_ - volumeNow_) < 1e-5f) volumeNow_ = volume_;
            interleaved[2 * i] *= volumeNow_;
            interleaved[2 * i + 1] *= volumeNow_;
        }
    };
    const auto remember = [&] {
        std::uint64_t at = caughtCount_.load(std::memory_order_relaxed);
        for (int i = 0; i < frames; ++i, ++at) {
            caught_[at % kCatchFrames].store(0.5f * (interleaved[2 * i] + interleaved[2 * i + 1]), std::memory_order_relaxed);
        }
        caughtCount_.store(at, std::memory_order_relaxed);
        barFramesNow_.store(static_cast<int>(samplesPerStep_ * kSteps), std::memory_order_relaxed);
    };
    if (gate_ == Gate::Open) {
        for (int i = 0; i < frames; ++i) {
            interleaved[2 * i] = ceiling(mixL[i]);
            interleaved[2 * i + 1] = ceiling(mixR[i]);
        }
        remember();
        turnDown();
        return;
    }
    // The panic gate: shut in 5 ms, so even a wall of noise does not click;
    // open again in 30 ms, once what was sounding has died away (or after
    // three seconds whatever is left).
    const float closeStep = 1.f / (0.005f * sampleRate_), openStep = 1.f / (0.03f * sampleRate_);
    for (int i = 0; i < frames; ++i) {
        if (gate_ == Gate::Closing) {
            gateGain_ -= closeStep;
            if (gateGain_ <= 0.f) {
                gateGain_ = 0.f;
                gate_ = Gate::Shut;
                cutEverything_ = true;
                shutFrames_ = quietFrames_ = 0;
            }
        } else if (gate_ == Gate::Shut) {
            const float loud = std::fabs(mixL[i]) > std::fabs(mixR[i]) ? std::fabs(mixL[i]) : std::fabs(mixR[i]);
            quietFrames_ = loud < 1e-3f ? quietFrames_ + 1 : 0;
            ++shutFrames_;
            const bool died = static_cast<float>(quietFrames_) >= 0.1f * sampleRate_ || static_cast<float>(shutFrames_) >= 3.f * sampleRate_;
            if (!panicHeld_ && !cutEverything_ && died) gate_ = Gate::Opening;
        } else if (gate_ == Gate::Opening) {
            gateGain_ += openStep;
            if (gateGain_ >= 1.f) {
                gateGain_ = 1.f;
                gate_ = Gate::Open;
                silenced_.store(false, std::memory_order_relaxed);
            }
        }
        interleaved[2 * i] = ceiling(mixL[i]) * gateGain_;
        interleaved[2 * i + 1] = ceiling(mixR[i]) * gateGain_;
    }
    remember();
    turnDown();
    if (cutEverything_) {
        // Behind the shut gate: stop what rings by itself.
        cutEverything_ = false;
        for (Track& track : tracks_) {
            track.drums.reset();
            for (auto& effect : track.effects)
                if (effect) effect->reset();
        }
        for (auto& effect : masterEffects_)
            if (effect) effect->reset();
    }
}

} // namespace apotheca
