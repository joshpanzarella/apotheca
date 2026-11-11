#include "apotheca/engine.hpp"
#include "apotheca/denormals.hpp"

#include <chrono>
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
    for (int& n : steps_) n = 0;
    caught_.reset(new std::atomic<float>[kCatchFrames]);
    for (int i = 0; i < kCatchFrames; ++i) caught_[i].store(0.f, std::memory_order_relaxed);
    for (auto& s : scope_) s.store(0.f, std::memory_order_relaxed);
    tracks_[0].hasDrums = true;
    for (Track& track : tracks_) {
        for (int i = 0; i < kMaxScenes * kAutoLanes * kAutoSteps; ++i) track.automation[i] = kAutoNone;
        for (int lane = 0; lane < kAutoLanes; ++lane) {
            track.autoBase[lane] = lane < 8 ? 0.5f : kAutoNone; // macros start in the middle; an effect says when it arrives
            track.autoNow[lane] = track.autoFrom[lane] = track.autoTo[lane] = track.autoBase[lane];
        }
    }
    claimed_[0] = true;
    claimedDrums_[0] = true;
    prepare(48000.f);
}

Engine::~Engine() {
    useWorkers(0);
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
    // Its settings as it starts, said out loud: automation needs to know where each knob rests.
    if (track != kMasterTrack && kind != EffectKind::None) {
        Command p;
        p.type = Command::SetEffectParam;
        p.track = track;
        p.a = slot;
        for (int i = 0; i < effectInfo(kind).params; ++i) {
            p.b = i;
            p.value = effectInfo(kind).defaults[i];
            commands_.push(p);
        }
    }
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
    const DrumKit& kit = DrumKit::builtIn(sampleRate); // one for every engine at this rate
    for (Track& track : tracks_) {
        track.drums.setKit(&kit);
        track.drums.prepare(sampleRate);
        if (track.synth) track.synth->prepare(sampleRate);
        for (auto& effect : track.effects) {
            if (effect) {
                effect->prepare(sampleRate);
                effect->reset();
            }
        }
        for (SlotFade& fade : track.fades) fade = SlotFade(); // a setup call: free to free
    }
    for (auto& effect : masterEffects_) {
        if (effect) {
            effect->prepare(sampleRate);
            effect->reset();
        }
    }
    for (SlotFade& fade : masterFades_) fade = SlotFade();
}

void Engine::setTrackEffect(int track, int slot, std::unique_ptr<Effect> effect) {
    if (!validTrack(track) || !validSlot(slot)) return;
    if (effect) effect->prepare(sampleRate_);
    tracks_[track].effects[slot] = std::move(effect);
    tracks_[track].fades[slot] = SlotFade();
}

void Engine::setMasterEffect(int slot, std::unique_ptr<Effect> effect) {
    if (!validSlot(slot)) return;
    if (effect) effect->prepare(sampleRate_);
    masterEffects_[slot] = std::move(effect);
    masterFades_[slot] = SlotFade();
}

// ---- an effect goes into a slot, or out of it, without a click ----------------------------

// On the audio thread, between chunks. The one in the slot becomes the one
// that is leaving, and a fade starts. Nothing is freed here: the leaving one
// is handed to the trash when its fade is done (binLeaving).
void Engine::swapEffect(std::unique_ptr<Effect>& place, SlotFade& fade, Effect* incoming) {
    if (fade.leaving) {
        // Swapped again inside the 10 ms of the last swap. The one that was
        // still fading out goes now; the fade starts over from the one in the slot.
        Trash trash;
        trash.isEffect = true;
        trash.pointer = fade.leaving.get();
        if (trash_.push(trash)) fade.leaving.release(); // if full it stays ours, and is freed below or with the engine
    }
    if (fade.leaving) {
        // Nowhere to hand it: the incoming one cannot wait either, so the one
        // in the slot goes at once (as every swap did before there was a fade).
        Trash trash;
        trash.isEffect = true;
        trash.pointer = place.release();
        if (trash.pointer) trash_.push(trash); // if full, it is leaked rather than freed here
        place.reset(incoming);
        return;
    }
    if (!place && !incoming) return; // empty, and still empty
    fade.leaving = std::move(place);
    place.reset(incoming);
    fade.total = static_cast<int>(kSwapFadeSeconds * sampleRate_);
    if (fade.total < 1) fade.total = 1;
    fade.left = fade.total;
}

// One slot's work on a chunk. With no fade under way this is the effect's own
// process() and nothing else, so a song sounds as it always did.
void Engine::runSlot(std::unique_ptr<Effect>& place, SlotFade& fade, float* left, float* right, int frames) {
    if (fade.left <= 0) {
        if (place) place->process(left, right, frames);
        return;
    }
    // Both run on the same sound coming in: `was` is the slot as it was.
    float wasL[kMaxBlock], wasR[kMaxBlock];
    for (int i = 0; i < frames; ++i) {
        wasL[i] = left[i];
        wasR[i] = right[i];
    }
    if (fade.leaving) fade.leaving->process(wasL, wasR, frames);
    if (place) place->process(left, right, frames);
    // A straight slide: the two are mostly the same sound, so they add in step.
    const float each = 1.f / static_cast<float>(fade.total);
    for (int i = 0; i < frames && fade.left > 0; ++i) {
        --fade.left;
        const float old = static_cast<float>(fade.left) * each; // 1 down to 0
        left[i] += old * (wasL[i] - left[i]);
        right[i] += old * (wasR[i] - right[i]);
    }
}

// A fade that has finished: its old effect goes to the trash, to be freed off
// the audio thread. Only the audio thread calls this and only when no helper
// is rendering, because the trash queue takes from one thread.
void Engine::binLeaving(SlotFade& fade) {
    if (fade.left > 0 || !fade.leaving) return;
    Trash trash;
    trash.isEffect = true;
    trash.pointer = fade.leaving.get();
    if (trash_.push(trash)) fade.leaving.release(); // if full: kept, and tried again next chunk
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
    else if (kind == SynthKind::Qmod) synth.reset(new Qmod);
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

namespace {
constexpr std::uint8_t kLaterPiece = 255; // Note::glide of a ratchet's second, third or fourth note
}

// Play a pattern's note now, or set it waiting.
void Engine::startNote(Track& track, const Note& note, double delaySamples) {
    // A ratchet: the note several times over, evenly, inside its first step. Each is let go just
    // before the next, so it is heard to start again; the last is held for what is left of the
    // note. Only the first of them glides.
    if (note.ratchet >= 2) {
        const int hits = note.ratchet > kMaxRatchet ? kMaxRatchet : note.ratchet;
        const float apart = 1.f / static_cast<float>(hits); // in steps
        for (int k = 0; k < hits; ++k) {
            Note one = note;
            one.ratchet = 0;
            // The first glides, and is there before the second begins. The rest are marked as pieces.
            one.glide = k > 0 ? kLaterPiece : note.glide ? static_cast<std::uint8_t>(hits) : 0;
            const float left = note.length - static_cast<float>(k) * apart;
            if (left < kShortestNote) break; // a note shorter than its ratchet: as many as fit
            one.length = k + 1 < hits && left > apart * 0.85f ? apart * 0.85f : left;
            startNote(track, one, delaySamples + static_cast<double>(k) * apart * samplesPerStep_);
        }
        return;
    }
    if (delaySamples < 1.0) {
        // A ratchet's later piece finds its key held: another note has begun there since (the one
        // before it of the ratchet was let go in time). That note keeps the key, whole.
        if (note.glide == kLaterPiece && track.offIn[note.pitch] < kNoNote) return;
        if (track.offIn[note.pitch] < kNoNote) track.synth->noteOff(note.pitch); // the same key again
        // A glide: from the key the track last began, over half a step, or over the first piece of
        // a ratchet. (The first note after Play has none to come from.)
        const bool glides = note.glide != 0 && note.glide != kLaterPiece;
        if (glides && track.lastPitch >= 0 && track.lastPitch != note.pitch) {
            const double steps = note.glide >= 2 ? 0.85 / note.glide : 0.5;
            track.synth->noteOnFrom(note.pitch, note.velocity, track.lastPitch, static_cast<float>(steps * samplesPerStep_ / sampleRate_));
        } else {
            track.synth->noteOn(note.pitch, note.velocity);
        }
        track.lastPitch = note.pitch;
        track.offIn[note.pitch] = static_cast<double>(note.length) * samplesPerStep_;
        return;
    }
    for (auto& p : track.pendingNotes) {
        if (p.velocity > 0.f) continue;
        p.samplesLeft = delaySamples;
        p.pitch = note.pitch;
        p.glide = note.glide;
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
            note.glide = p.glide;
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
        if (validTrack(c.track) && tracks_[c.track].hasDrums && c.a >= 0 && c.a < kDrumLanes) {
            StepFx fx;
            fx.pitch = static_cast<std::int8_t>(c.d < -kMaxStepPitch ? -kMaxStepPitch : (c.d > kMaxStepPitch ? kMaxStepPitch : c.d));
            fx.ratchet = static_cast<std::uint8_t>(c.bar < 0 ? 0 : (c.bar > kMaxRatchet ? kMaxRatchet : c.bar));
            fx.reverse = c.scene == 1;
            fx.cut = static_cast<std::uint8_t>(c.extra >= 1.f && c.extra < kCutChoices ? c.extra : 0.f);
            playHit(tracks_[c.track], c.a, c.value, static_cast<float>(c.b) * 1e-4f, c.c > 0 ? static_cast<float>(c.c) * 1e-4f : -1.f, fx, 0.0);
        }
        break;
    case Command::SetStepFx:
        if (validTrack(c.track) && slotFor(c) >= 0 && validCell(c.a, c.b)) {
            StepFx& fx = tracks_[c.track].drumScenes[slotFor(c)].fx[c.a][c.b];
            fx.pitch = static_cast<std::int8_t>(c.c < -kMaxStepPitch ? -kMaxStepPitch : (c.c > kMaxStepPitch ? kMaxStepPitch : c.c));
            fx.ratchet = static_cast<std::uint8_t>(c.d < 0 ? 0 : (c.d > kMaxRatchet ? kMaxRatchet : c.d));
            fx.reverse = c.value == 1.f;
            fx.cut = static_cast<std::uint8_t>(c.extra >= 1.f && c.extra < kCutChoices ? c.extra : 0.f);
        }
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
        if (c.track != kMasterTrack) {
            // Another effect in the slot: where its knobs rest is not known until it says.
            for (int i = 0; i < 4; ++i) {
                const int lane = autoLaneOfEffect(c.a, i);
                tracks_[c.track].autoBase[lane] = tracks_[c.track].autoNow[lane] = kAutoNone;
                tracks_[c.track].autoGliding[lane] = false;
            }
        }
        swapEffect(place, c.track == kMasterTrack ? masterFades_[c.a] : tracks_[c.track].fades[c.a], static_cast<Effect*>(c.pointer));
        if (place) place->setTempo(tempo_);
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
            const int hits = c.e & 0xFF;
            note.ratchet = static_cast<std::uint8_t>(sound && hits >= 2 ? (hits > kMaxRatchet ? kMaxRatchet : hits) : 0);
            note.glide = sound && (c.e & 256) != 0 ? 1 : 0;
        }
        break;
    case Command::SetSceneCount:
        sceneCount_ = c.a < 1 ? 1 : (c.a > kMaxScenes ? kMaxScenes : c.a);
        break;
    case Command::SetSceneRepeats:
        if (validScene(c.scene)) repeats_[c.scene] = c.a < 1 ? 1 : (c.a > kMaxRepeats ? kMaxRepeats : c.a);
        break;
    case Command::SetSceneSteps:
        if (validScene(c.scene)) steps_[c.scene] = c.a < 0 ? 0 : (c.a > kMaxBars * kSteps ? kMaxBars * kSteps : c.a);
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
                        scene.fx[lane][step] = StepFx();
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
        for (Track& track : tracks_) track.lastPitch = -1; // the song's first note has nothing to glide from
        break;
    case Command::Stop:
        playing_ = false;
        clearPending(); // a hit that was waiting for its moment does not come after the song has stopped
        letGoOfNotes();
        settleAutomation(); // every automated setting back to where its knob was left
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
    case Command::SetTrackHeard:
        if (validTrack(c.track)) tracks_[c.track].heard = c.a != 0;
        break;
    case Command::SetLaneLevel:
        if (validTrack(c.track)) tracks_[c.track].drums.setLaneLevel(c.a, c.value);
        break;
    case Command::SetLanePart:
        if (validTrack(c.track)) tracks_[c.track].drums.setLanePart(c.a, c.value);
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
        if (validTrack(c.track) && tracks_[c.track].synth) {
            Track& track = tracks_[c.track];
            track.synth->setMacro(c.a, c.value);
            if (c.a >= 0 && c.a < 8) { // the knob itself: where the setting rests when nothing automates it
                track.autoBase[c.a] = track.autoNow[c.a] = c.value;
                track.autoGliding[c.a] = false;
            }
        }
        break;
    case Command::SetAutomation:
        if (validTrack(c.track) && validScene(c.scene) && c.a >= 0 && c.a < kAutoLanes && c.b >= 0 && c.b < kAutoSteps && c.value == c.value)
            tracks_[c.track].autoAt(c.scene, c.a)[c.b] = c.value < 0.f ? kAutoNone : (c.value > 1.f ? 1.f : c.value);
        break;
    case Command::ClearAutomation:
        if (validTrack(c.track) && validScene(c.scene))
            for (int lane = 0; lane < kAutoLanes; ++lane)
                if (c.a < 0 || c.a == lane)
                    for (int i = 0; i < kAutoSteps; ++i) tracks_[c.track].autoAt(c.scene, lane)[i] = kAutoNone;
        break;
    case Command::SetSetting:
        if (validTrack(c.track) && tracks_[c.track].synth) tracks_[c.track].synth->setSetting(c.a, c.b);
        break;
    case Command::InstallSynth:
        // addSynthTrack() only sends this for a track it found empty.
        if (validTrack(c.track) && !tracks_[c.track].hasInstrument()) {
            tracks_[c.track].synth.reset(static_cast<Instrument*>(c.pointer));
            tracks_[c.track].synth->setQualityLimit(qualityLimit_.load(std::memory_order_relaxed)); // a new synth joins under the same limit
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
                        scene.fx[lane][step] = StepFx();
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
                            track.drumScenes[scene].fx[lane][step] = image.fx[scene][lane][step];
                        }
                    for (int slot = 0; slot < kMaxNotes; ++slot) {
                        const TrackImage::Note& from = image.notes[scene][slot];
                        Note& to = track.notes[scene][slot];
                        to.step = from.step;
                        to.pitch = from.pitch;
                        to.length = from.length;
                        to.nudge = from.nudge;
                        to.velocity = from.velocity;
                        to.ratchet = from.ratchet;
                        to.glide = from.glide;
                    }
                }
                for (int i = 0; i < kMaxScenes * kAutoLanes * kAutoSteps; ++i) track.automation[i] = image.automation ? image.automation[i] : kAutoNone;
                for (int lane = 0; lane < kDrumLanes; ++lane) {
                    track.drums.setLaneLevel(lane, image.laneLevel[lane]);
                    track.drums.setLanePart(lane, image.lanePart[lane]);
                }
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
            Track& track = tracks_[c.track];
            if (track.effects[c.a]) track.effects[c.a]->setParam(c.b, c.value);
            if (c.b >= 0 && c.b < 4) {
                const int lane = autoLaneOfEffect(c.a, c.b);
                track.autoBase[lane] = track.autoNow[lane] = c.value;
                track.autoGliding[lane] = false;
            }
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
    const float chance = scene.chance[lane][step];
    if (chance < 1.f) {
        dice_ = dice_ * 1664525u + 1013904223u;
        if (static_cast<float>(dice_ >> 8) * (1.f / 16777216.f) >= chance) return;
    }
    playHit(track, lane, scene.level[lane][step], scene.start[lane][step], -1.f, scene.fx[lane][step], delaySamples);
}

// One hit, with its step effects, now or after a wait. A ratchet is the same
// hit several times across the step; each of its hits is cut by the next, and
// a hit that is CUT stops when its time is up, or when the next of the ratchet
// comes, whichever is first.
void Engine::playHit(Track& track, int lane, float velocity, float start, float part, const StepFx& fx, double delaySamples) {
    const int hits = fx.hits();
    const double gap = samplesPerStep_ / hits;
    HitPlay how;
    how.rate = fx.pitch == 0 ? 1.f : std::exp2(static_cast<float>(fx.pitch) / 12.f);
    how.reverse = fx.reverse != 0;
    if (fx.cut != 0) {
        const double life = static_cast<double>(fx.cutSteps()) * samplesPerStep_;
        how.life = static_cast<int>(life < gap ? life : gap);
        if (how.life < 1) how.life = 1;
    }
    int more = hits - 1;
    if (delaySamples < 1.0) {
        track.drums.trigger(lane, velocity, start, part, how);
        if (more == 0) return;
        delaySamples += gap; // the rest of the ratchet waits its turn
        --more;
    }
    for (auto& p : track.pending[lane]) {
        if (p.velocity <= 0.f) {
            p.samplesLeft = delaySamples;
            p.gap = gap;
            p.more = more;
            p.velocity = velocity;
            p.start = start;
            p.part = part;
            p.how = how;
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
                    track.drums.trigger(lane, p.velocity, p.start, p.part, p.how);
                    if (p.more > 0) { // a ratchet: the same again, a part of a step on
                        --p.more;
                        p.samplesLeft += p.gap;
                    } else {
                        p = Track::Pending();
                    }
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

// How many steps a scene lasts: every step of its longest cell, or fewer if it has been cut short.
int Engine::sceneSteps(int scene) const {
    const int whole = sceneBars(scene) * kSteps;
    return steps_[scene] > 0 && steps_[scene] < whole ? steps_[scene] : whole;
}

Engine::Position Engine::afterThisBar() const {
    if ((bar_ + 1) * kSteps < sceneSteps(scene_)) return {scene_, repeat_, bar_ + 1}; // further into the scene
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
    // The very first step after Play. A hit or note on step 1 that is pushed
    // EARLY would have started just before the bar, and there was no
    // "before": so it starts now, at the top, and is not missed.
    const bool veryFirst = firstStep_; // (only ever set for step 1)
    firstStep_ = false;

    // A scene may be cut short, to any number of steps: its last bar then ends early.
    const int left = sceneSteps(scene_) - bar_ * kSteps; // steps this bar has (at least one: the bar would not have begun otherwise)
    const int following = (nextStep_ + 1) % (left >= kSteps ? kSteps : (left < 1 ? 1 : left));
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
                } else if (veryFirst && now.level[lane][0] > 0.f) {
                    schedule(track, lane, now, 0, 0.0);
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
                else if (veryFirst && note.velocity > 0.f && note.step == 0) startNote(track, note, 0.0);
            }
            for (const Note& note : track.notes[slotOf(track, followingScene, followingBar)]) {
                if (note.velocity > 0.f && note.step == following && note.nudge < 0.f) startNote(track, note, (1.0 + note.nudge) * samplesPerStep_);
            }
        }
    }
    // Automation: where each lane is to be by the middle of this step.
    autoPhase_ = 0.0;
    for (Track& track : tracks_) {
        if (!track.hasInstrument()) continue;
        const int at = (slotOf(track, scene_, bar_) % kMaxBars) * kSteps + nextStep_;
        for (int lane = 0; lane < kAutoLanes; ++lane) {
            const float written = track.autoAt(scene_, lane)[at];
            const float to = written >= 0.f ? written : track.autoBase[lane];
            if (to < 0.f || to == track.autoNow[lane]) {
                track.autoGliding[lane] = false;
                continue;
            }
            track.autoFrom[lane] = track.autoNow[lane] < 0.f ? to : track.autoNow[lane];
            track.autoTo[lane] = to;
            track.autoGliding[lane] = true;
        }
    }
    playStep_.store(nextStep_, std::memory_order_relaxed);
    playScene_.store(scene_, std::memory_order_relaxed);
    playRepeat_.store(repeat_, std::memory_order_relaxed);
    playBar_.store(bar_, std::memory_order_relaxed);
    nextStep_ = following;
}

// Falling behind: two blocks in a row that took more than 85% of their time.
// Then every synth is told to do plainer work, one notch. It is let back up a
// notch only after ten seconds with plenty to spare (under 35%: a notch up
// about doubles the cost, and it must fit), so it does not swing to and fro.
void Engine::noteLoad(float part, int frames) {
    if (!(part >= 0.f)) return;
    load_.store(part, std::memory_order_relaxed);
    int limit = qualityLimit_.load(std::memory_order_relaxed);
    const int before = limit;
    busyBlocks_ = part > 0.85f ? busyBlocks_ + 1 : 0;
    calmFrames_ = part < 0.35f ? calmFrames_ + frames : 0;
    if (busyBlocks_ >= 2 && limit > 0) {
        --limit;
        busyBlocks_ = 0;
        calmFrames_ = 0;
    } else if (calmFrames_ >= static_cast<long long>(10.f * sampleRate_) && limit < 3) {
        ++limit;
        calmFrames_ = 0;
    }
    if (limit == before) return;
    qualityLimit_.store(limit, std::memory_order_relaxed);
    for (Track& track : tracks_)
        if (track.synth) track.synth->setQualityLimit(limit);
}

void Engine::render(float* interleaved, int frames) {
    const FlushDenormals flushed(flush_.load(std::memory_order_relaxed)); // if asked for: on for this call, then the thread is as it was
    const auto began = watchLoad_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    const int allFrames = frames;
    struct Timed {
        Engine& engine;
        std::chrono::steady_clock::time_point began;
        int frames;
        ~Timed() {
            if (!engine.watchLoad_ || frames <= 0) return;
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            engine.noteLoad(static_cast<float>(took * engine.sampleRate_ / frames), frames);
        }
    } timed{*this, began, allFrames};
    Command command;
    while (commands_.pop(command)) apply(command);
    while (live_.pop(command)) apply(command);

    float peaks[kNumTracks] = {};
    while (frames > 0) {
        int chunk = frames < kMaxBlock ? frames : kMaxBlock;
        // A step starts on its exact sample: the chunk ends where the next
        // step begins. Drum hits that are waiting come on theirs whether
        // the song is playing or not (a ratchet heard by hand has its later
        // hits waiting).
        firePending();
        double next = 1e18;
        if (playing_) {
            fireNoteOffs(); // before the step, so a note that ends where the next begins is let go first
            firePendingNotes();
            if (untilStep_ <= 0.0) {
                fireStep();
                untilStep_ += samplesPerStep_;
            }
            next = untilStep_;
        }
        // Stop the chunk at whichever comes first: the next step, the next
        // waiting hit, or the next note to be let go.
        for (Track& track : tracks_) {
            if (track.synth && playing_) {
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
        if (next < chunk) {
            const int toNext = static_cast<int>(std::ceil(next));
            if (toNext < chunk) chunk = toNext < 1 ? 1 : toNext;
        }
        if (playing_) untilStep_ -= chunk;
        for (Track& track : tracks_) {
            if (track.synth && playing_) {
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
        renderChunk(interleaved, chunk, peaks);
        interleaved += 2 * chunk;
        frames -= chunk;
    }
    for (int t = 0; t < kNumTracks; ++t) tracks_[t].peak.store(peaks[t], std::memory_order_relaxed);
}

// Give a lane's value to what it belongs to: a macro of the synth, or a setting of an effect.
void Engine::applyAuto(Track& track, int lane, float value) {
    track.autoNow[lane] = value;
    if (lane < 8) {
        if (track.synth) track.synth->setMacro(lane, value);
    } else if (track.effects[(lane - 8) / 4]) {
        track.effects[(lane - 8) / 4]->setParam((lane - 8) % 4, value);
    }
}

// Move every gliding lane on. The glide takes the first half of a step, in
// eight moves: smooth to the ear, and few enough that a synth which works a
// lot out again at each change (Phazed) is not asked to too often.
void Engine::glideAutomation(int frames) {
    const double before = autoPhase_;
    autoPhase_ += frames / samplesPerStep_;
    const int was = static_cast<int>(before * 16.0), now = static_cast<int>(autoPhase_ * 16.0);
    if (now == was && before > 0.0) return;
    const float part = autoPhase_ >= 0.5 ? 1.f : static_cast<float>((now < 8 ? now : 8)) / 8.f;
    for (Track& track : tracks_) {
        if (!track.hasInstrument()) continue;
        for (int lane = 0; lane < kAutoLanes; ++lane) {
            if (!track.autoGliding[lane]) continue;
            applyAuto(track, lane, track.autoFrom[lane] + (track.autoTo[lane] - track.autoFrom[lane]) * part);
            if (part >= 1.f) track.autoGliding[lane] = false;
        }
    }
}

// Stopped: every setting that automation had moved goes back to its knob.
void Engine::settleAutomation() {
    for (Track& track : tracks_)
        for (int lane = 0; lane < kAutoLanes; ++lane) {
            track.autoGliding[lane] = false;
            if (track.autoBase[lane] >= 0.f && track.autoNow[lane] != track.autoBase[lane]) applyAuto(track, lane, track.autoBase[lane]);
        }
}

// One track's part of the chunk: its instrument, its effects, its level and its mute, into the
// track's own buffers. It touches nothing but that track, so several can run at once.
void Engine::renderTrack(int t) {
    Track& track = tracks_[t];
    const int frames = chunkFrames_;
    float* left = trackL_[t];
    float* right = trackR_[t];
    if (track.synth) track.synth->render(left, right, frames);
    else track.drums.render(left, right, frames);
    for (int slot = 0; slot < kEffectSlots; ++slot) runSlot(track.effects[slot], track.fades[slot], left, right, frames);
    if (t == chunkScope_) {
        unsigned written = scopeWritten_.load(std::memory_order_relaxed);
        for (int i = 0; i < frames; ++i) {
            scope_[written++ % kScopeSize].store(0.5f * (left[i] + right[i]) * track.level, std::memory_order_relaxed);
        }
        scopeWritten_.store(written, std::memory_order_relaxed);
    }
    float peak = chunkPeaks_[t];
    const float gateTo = track.heard ? 1.f : 0.f, gateStep = 1.f / (0.005f * sampleRate_);
    for (int i = 0; i < frames; ++i) {
        if (track.gate != gateTo) {
            track.gate += track.gate < gateTo ? gateStep : -gateStep;
            if ((track.gate > gateTo) == track.heard) track.gate = gateTo; // arrived (or stepped past)
        }
        left[i] *= track.level * track.gate;
        right[i] *= track.level * track.gate;
        const float a = std::fabs(left[i]) > std::fabs(right[i]) ? std::fabs(left[i]) : std::fabs(right[i]);
        if (a > peak) peak = a;
    }
    chunkPeaks_[t] = peak;
}

void Engine::shareOutTracks() {
    for (;;) {
        const int i = nextActive_.fetch_add(1, std::memory_order_acq_rel);
        if (i >= activeCount_.load(std::memory_order_acquire)) return;
        renderTrack(active_[i]);
        finished_.fetch_add(1, std::memory_order_release);
    }
}

void Engine::workerLoop(void (*atStart)()) {
    if (atStart) atStart();
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(wakeMutex_);
            wake_.wait(lock, [this] { return wakeups_ > 0 || quitWorkers_.load(); });
            if (quitWorkers_.load()) return;
            --wakeups_;
        }
        flushDenormals(flush_.load(std::memory_order_relaxed)); // this thread only renders: as the engine is set, each time
        shareOutTracks();
    }
}

void Engine::useWorkers(int count, void (*atStart)()) {
    // Stop the ones there are. The flag is set with the lock held: a helper
    // checks it with the lock held and then goes to sleep, and a flag set in
    // between (with no lock) is a wake-up it never hears. It then sleeps for
    // good, and join() below waits for it for good. (That happened: about one
    // run in a few hundred of the test below, and it would have been a quit
    // that hangs on the handheld.)
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        quitWorkers_.store(true);
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) worker.join();
    workers_.clear();
    quitWorkers_.store(false);
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeups_ = 0;
    }
    count = count < 0 ? 0 : (count > kNumTracks - 1 ? kNumTracks - 1 : count);
    for (int i = 0; i < count; ++i) workers_.emplace_back([this, atStart] { workerLoop(atStart); });
}

void Engine::renderChunk(float* interleaved, int frames, float* peaks) {
    if (playing_) glideAutomation(frames);
    float mixL[kMaxBlock] = {};
    float mixR[kMaxBlock] = {};
    // Which tracks there are to work out, and what the workers need for this chunk.
    int count = 0;
    for (int t = 0; t < kNumTracks; ++t)
        if (tracks_[t].hasInstrument()) active_[count++] = t; // an empty track costs nothing
    chunkFrames_ = frames;
    chunkPeaks_ = peaks;
    chunkScope_ = scopeTrack_.load(std::memory_order_relaxed);
    if (workers_.empty() || count < 2) {
        for (int i = 0; i < count; ++i) renderTrack(active_[i]);
    } else {
        // Shared out. Everything above is set before the list is opened (nextActive_ = 0), so a
        // helper arriving late for the chunk before finds nothing, or this chunk whole.
        finished_.store(0, std::memory_order_relaxed);
        activeCount_.store(count, std::memory_order_release);
        nextActive_.store(0, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(wakeMutex_);
            wakeups_ = static_cast<int>(workers_.size()) < count - 1 ? static_cast<int>(workers_.size()) : count - 1;
        }
        wake_.notify_all();
        shareOutTracks(); // the audio thread does its share
        while (finished_.load(std::memory_order_acquire) < count) std::this_thread::yield();
        nextActive_.store(kNumTracks, std::memory_order_release); // the list is shut again
    }
    // Mixed in track order, always, so the sum is the same however the work was shared.
    for (int i = 0; i < count; ++i) {
        const int t = active_[i];
        for (int f = 0; f < frames; ++f) {
            mixL[f] += trackL_[t][f];
            mixR[f] += trackR_[t][f];
        }
    }

    for (int slot = 0; slot < kEffectSlots; ++slot) runSlot(masterEffects_[slot], masterFades_[slot], mixL, mixR, frames);
    // Effects whose fade out has ended go to the trash. Here, not in
    // renderTrack: the helpers are done, and the trash takes from this thread alone.
    for (int i = 0; i < count; ++i)
        for (SlotFade& fade : tracks_[active_[i]].fades) binLeaving(fade);
    for (SlotFade& fade : masterFades_) binLeaving(fade);

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
        // (An effect part way through its 10 ms fade out is left alone: the
        // gate stays shut far longer than that, so it is never heard again.)
    }
}

} // namespace apotheca
