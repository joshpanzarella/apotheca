#include "session.hpp"

namespace apotheca {

namespace {
constexpr Uint32 kAuditionMs = 350;
}

void Session::togglePlay(bool song) {
    project.playing = !project.playing;
    Command c;
    c.type = project.playing ? Command::Play : Command::Stop;
    c.a = project.scene;
    c.b = song ? 0 : 1;
    engine.send(c);
}

void Session::syncSong() {
    const int count = project.sceneCount();
    if (count == sentSceneCount_) return;
    sentSceneCount_ = count;
    Command c;
    c.type = Command::SetSceneCount;
    c.a = count;
    engine.send(c);
}

bool Session::addTrack(TrackKind kind) {
    const int track = project.trackCount();
    if (track >= kNumTracks || kind == TrackKind::None) return false;
    if (kind == TrackKind::Drums) {
        if (!engine.addDrumTrack(track)) return false;
    } else {
        if (!engine.addSynthTrack(track)) return false;
        project.tracks[track].preset = 0;
        project.tracks[track].patch = Phazed::presetPatch(0);
    }
    project.tracks[track].kind = kind;
    return true;
}

void Session::audition(int track, int pitch, float velocity) {
    // Reuse the slot already holding this key, else the one that ends soonest.
    Sounding* slot = &sounding_[0];
    for (Sounding& s : sounding_) {
        if (s.track == track && s.pitch == pitch) { slot = &s; break; }
        if (s.track < 0 || (slot->track >= 0 && s.until < slot->until)) slot = &s;
    }
    Command c;
    c.track = slot->track;
    c.a = slot->pitch;
    c.type = Command::NoteOff;
    if (slot->track >= 0) engine.send(c);
    c.type = Command::NoteOn;
    c.track = track;
    c.a = pitch;
    c.value = velocity;
    engine.send(c);
    slot->track = track;
    slot->pitch = pitch;
    slot->until = now_ + kAuditionMs;
}

void Session::update(Uint32 nowMs) {
    now_ = nowMs;
    for (Sounding& s : sounding_) {
        if (s.track < 0 || nowMs < s.until) continue;
        Command c;
        c.type = Command::NoteOff;
        c.track = s.track;
        c.a = s.pitch;
        engine.send(c);
        s.track = -1;
    }
    syncSong();
}

void Session::sendDrumPattern(int track, int scene) {
    const DrumPattern& p = project.tracks[track].drums[scene];
    Command c;
    c.track = track;
    c.scene = scene;
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        for (int step = 0; step < kSteps; ++step) {
            c.a = lane;
            c.b = step;
            c.type = Command::SetStep;
            c.value = p.level[lane][step];
            engine.send(c);
            c.type = Command::SetStepNudge;
            c.value = p.nudge[lane][step];
            engine.send(c);
            c.type = Command::SetStepChance;
            c.value = p.chance[lane][step];
            engine.send(c);
        }
    }
}

void Session::sendNotePattern(int track, int scene) {
    const NotePattern& p = project.tracks[track].notes[scene];
    Command c;
    c.type = Command::SetNote;
    c.track = track;
    c.scene = scene;
    for (int slot = 0; slot < kMaxNotes; ++slot) {
        c.a = slot;
        c.b = p.notes[slot].step;
        c.c = p.notes[slot].pitch;
        c.d = p.notes[slot].length;
        c.value = p.notes[slot].velocity;
        engine.send(c);
    }
}

} // namespace apotheca
