// Session, part one of four (see session_parts.hpp for what is where).
#include "session.hpp"
#include "instruments.hpp"
#include "ui.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <memory>

#include "samples.hpp"
#include "session_parts.hpp"
#include "song_file.hpp"
#include "stretch.hpp"
#include "synth_spec.hpp"

namespace apotheca {

namespace {
constexpr Uint32 kAuditionMs = 350;
constexpr Uint32 kSaveEveryMs = 2000;
constexpr Uint32 kTempoSettleMs = 250;
}

void Session::togglePlay(bool song) {
    project.playing = !project.playing;
    Command c;
    c.type = project.playing ? Command::Play : Command::Stop;
    c.a = project.scene;
    c.b = song ? 0 : 1;
    engine.send(c);
    project.playLoop = !song;
}

void Session::panic(bool held) {
    Command c;
    c.type = Command::Panic;
    c.a = held ? 1 : 0;
    engine.send(c);
    if (!held) return;
    project.playing = false;
    project.silenced = true;
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

bool Session::removeTrack(int track) {
    const int count = project.trackCount();
    if (track < 0 || track >= count || count <= 1) return false;
    for (int t = track; t + 1 < kNumTracks; ++t) project.tracks[t] = project.tracks[t + 1];
    {
        std::unique_ptr<TrackData> blank(new TrackData); // large: not on the stack
        project.tracks[kNumTracks - 1] = *blank;
    }
    if (project.track > track || project.track >= count - 1) project.track = project.track > 0 ? project.track - 1 : 0;
    project.bar = 0;
    applyProject(); // every track told to the engine afresh, in its new place
    return true;
}

bool Session::addTrack(TrackKind kind) {
    const int track = project.trackCount();
    if (track >= kNumTracks || kind == TrackKind::None) return false;
    if (kind == TrackKind::Drums) {
        if (!engine.addDrumTrack(track)) return false;
    } else {
        if (!engine.addSynthTrack(track, instrument(kind)->synth)) return false;
        project.tracks[track].preset = 0;
        if (const SynthSpec* spec = specFor(kind)) spec->loadPreset(project.tracks[track], 0);
    }
    project.tracks[track].kind = kind;
    return true;
}

bool Session::setTrackKind(int track, TrackKind kind) {
    if (track < 0 || track >= project.trackCount() || kind == TrackKind::None || project.tracks[track].kind == kind) return false;
    TrackData& t = project.tracks[track];
    t.kind = kind;
    t.preset = 0;
    for (float& m : t.macro) m = 0.5f;
    if (const SynthSpec* spec = specFor(kind)) spec->loadPreset(t, 0);
    for (auto& scene : t.automation)
        for (int lane = 0; lane < kSynthMacros; ++lane) // the macros' lanes; the effects' are still the same effects
            for (float& v : scene[lane]) v = kAutoNone;
    applyProject(); // the engine is given the song afresh, with the new instrument in its place
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
    engine.collectGarbage();
    project.playScene = engine.playScene();
    project.playStep = engine.playStep();
    project.playRepeat = engine.playRepeat();
    project.playBar = engine.playBar();
    project.silenced = engine.silenced();
    project.busy = engine.qualityLimit() < 3;

    // A full queue loses commands, and then the engine and the song no longer
    // agree (a note on the screen that never plays). It takes the audio thread
    // standing still for seconds, so it is rare; when it has happened, wait
    // for the queue to empty and send the whole song again. If that overflows
    // too, dropped() moves again and this comes round once more.
    if (const unsigned lost = engine.dropped(); lost != droppedSeen_ && engine.caughtUp()) {
        droppedSeen_ = lost;
        applyProject();
        ++resyncs_;
    }

    hearMidi();
    serveDesigner();

    // Undo's bookkeeping. The cell being worked on is watched directly (the
    // grids change it hit by hit); everything else that changes a pattern
    // says so itself (noteEdit). When things have been still for a while,
    // that stillness is what the next step of undo will return to.
    if (!undoReady_) {
        undoReady_ = true;
        settled_ = patternsNow();
        cellMark_ = cellMark();
    }
    if (const std::uint32_t mark = cellMark(); mark != cellMark_) {
        cellMark_ = mark;
        noteEdit();
    }
    if (editing_ && nowMs - editedAt_ >= 700) {
        editing_ = false;
        settled_ = patternsNow();
    }

    // A new program has arrived: ask whoever is holding the handheld.
    if (transfer_.updatesArrived() != seenUpdates_) {
        seenUpdates_ = transfer_.updatesArrived();
        struct stat info;
        updateWaiting_ = stat((programFolder_ + "/apotheca.pending").c_str(), &info) == 0;
        updateMegabytes_ = updateWaiting_ ? static_cast<float>(info.st_size) / (1024.f * 1024.f) : 0.f;
    }
    // A chord pack that has just arrived over WiFi is there to pick from at once.
    if (transfer_.received() != seenReceived_) {
        seenReceived_ = transfer_.received();
        if (!chordFolder_.empty()) packs = loadChordPacks(chordFolder_);
        // And a recording renamed, moved, deleted or sent from the page: the open song follows.
        // The songs that are shut follow a rename or a move too, on this thread: the one that autosaves.
        std::vector<std::pair<std::string, std::string>> renames;
        for (const Transfer::Change& change : transfer_.takeChanges()) {
            recordingChanged(change.from, change.to);
            renames.push_back({change.from, change.to}); // one sent or deleted has no name on one side, and is passed over there
        }
        savedSongsFollow(renames);
        // The open song is written now, not at the next autosave: its revs already have the new
        // name, and a flat battery in between would leave the song itself with the old one.
        for (const auto& rename : renames)
            if (!rename.first.empty() && !rename.second.empty()) { saveNow(); break; }
    }

    // What has come in from the input goes to the recorder.
    if (rec_ == Rec::Waiting || rec_ == Rec::Recording) {
        float block[2048];
        for (int got = input_->read(block, 2048); got > 0; got = input_->read(block, 2048)) recorder_.feed(block, got);
        if (recorder_.state() == Recorder::Recording) rec_ = Rec::Recording;
        if (recorder_.full()) finishRecording(); // ten seconds
    }

    // Fitted recordings follow the tempo, once it has stopped moving for a
    // quarter of a second: no point stretching for every number on the way.
    if (project.tempo != seenTempo_) {
        seenTempo_ = project.tempo;
        tempoMovedAt_ = nowMs;
    }
    if (nowMs - tempoMovedAt_ >= kTempoSettleMs) steadyTempo_ = seenTempo_;
    collectJob(false);
    if (!job_ && steadyTempo_ > 0.f) {
        for (int track = 0; track < kNumTracks && !job_; ++track)
            for (int lane = 0; lane < kDrumLanes && !job_; ++lane)
                if (wantsFitting(track, lane)) startJob(track, lane);
    }
    if (nowMs >= nextSaveAt_) {
        nextSaveAt_ = nowMs + kSaveEveryMs;
        saveNow();
    }
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

void Session::setSceneSteps(int scene, int steps) {
    if (scene < 0 || scene >= kMaxScenes) return;
    // Any number from one step to every step of its longest cell; "all of them" is kept as 0,
    // so the scene grows again if a cell in it is made longer.
    const int whole = project.sceneBars(scene) * kSteps;
    steps = steps < 1 ? 1 : (steps >= whole ? 0 : steps);
    if (project.steps[scene] == steps) return;
    project.steps[scene] = steps;
    noteEdit();
    Command c;
    c.type = Command::SetSceneSteps;
    c.scene = scene;
    c.a = steps;
    engine.send(c);
}

void Session::setCellBars(int track, int scene, int bars) {
    if (track < 0 || track >= kNumTracks || scene < 0 || scene >= kMaxScenes) return;
    bars = bars < 2 ? 1 : (bars < 4 ? 2 : (bars < 8 ? 4 : 8));
    TrackData& t = project.tracks[track];
    const int was = t.bars[scene] < 1 ? 1 : t.bars[scene];
    t.bars[scene] = bars;
    // Made longer: the new bars are filled with what the cell already plays, round and round, so a
    // one-bar loop made two bars long is the loop twice, ready to be changed in its second half.
    // Only bars with nothing in them: a bar that was written, then hidden by shortening the cell,
    // comes back as it was.
    bool filled = false;
    for (int bar = was; bar < bars && t.kind != TrackKind::None; ++bar) {
        const int from = bar % was;
        bool automated = false;
        for (int lane = 0; lane < kAutoLanes; ++lane)
            for (int step = 0; step < kSteps; ++step) automated = automated || t.automation[scene][lane][bar * kSteps + step] >= 0.f;
        if (!t.drums[scene][bar].empty() || !t.notes[scene][bar].empty() || automated) continue;
        t.drums[scene][bar] = t.drums[scene][from];
        t.notes[scene][bar] = t.notes[scene][from];
        for (int lane = 0; lane < kAutoLanes; ++lane)
            for (int step = 0; step < kSteps; ++step) t.automation[scene][lane][bar * kSteps + step] = t.automation[scene][lane][from * kSteps + step];
        filled = true;
    }
    noteEdit();
    if (filled) sendTrackImage(track); // every bar of it, in one piece
    if (track == project.track && scene == project.scene && project.bar >= bars) project.bar = bars - 1;
    Command c;
    c.type = Command::SetCellBars;
    c.track = track;
    c.scene = scene;
    c.a = bars;
    engine.send(c);
}

// A whole cell to the engine: emptied there in one command, then only what
// is in it. (Every step of eight bars would be four thousand commands.)
// A hit's step effects, as the engine is told them.
Command stepFxCommand(int track, int scene, int bar, int lane, int step, const StepFx& fx) {
    Command c;
    c.type = Command::SetStepFx;
    c.track = track;
    c.scene = scene;
    c.bar = bar;
    c.a = lane;
    c.b = step;
    c.c = fx.pitch;
    c.d = fx.ratchet;
    c.value = fx.reverse ? 1.f : 0.f;
    c.extra = fx.cutAndThrow();
    return c;
}

void Session::sendDrumPattern(int track, int scene) {
    noteEdit();
    Command c;
    c.track = track;
    c.scene = scene;
    c.type = Command::ClearCell;
    engine.send(c);
    c.type = Command::SetCellBars;
    c.a = project.tracks[track].bars[scene];
    engine.send(c);
    for (int bar = 0; bar < kMaxBars; ++bar) {
        const DrumPattern& p = project.tracks[track].drums[scene][bar];
        c.bar = bar;
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            for (int step = 0; step < kSteps; ++step) {
                if (p.level[lane][step] <= 0.f) continue;
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
                c.type = Command::SetStepStart;
                c.value = p.start[lane][step];
                engine.send(c);
                if (p.fx[lane][step].any()) engine.send(stepFxCommand(track, scene, bar, lane, step, p.fx[lane][step]));
            }
        }
    }
}

void Session::sendNotePattern(int track, int scene) {
    noteEdit();
    Command c;
    c.track = track;
    c.scene = scene;
    c.type = Command::ClearCell;
    engine.send(c);
    c.type = Command::SetCellBars;
    c.a = project.tracks[track].bars[scene];
    engine.send(c);
    for (int bar = 0; bar < kMaxBars; ++bar)
        if (!project.tracks[track].notes[scene][bar].empty()) sendNoteBar(track, scene, bar);
}

void Session::sendNoteBar(int track, int scene, int bar) {
    noteEdit();
    NoteEvent notes[kMaxNotes]; // chords written out as their notes
    project.tracks[track].notes[scene][bar].render(notes);
    Command c;
    c.type = Command::SetNote;
    c.track = track;
    c.scene = scene;
    c.bar = bar;
    for (int slot = 0; slot < kMaxNotes; ++slot) {
        c.a = slot;
        c.b = notes[slot].step;
        c.c = notes[slot].pitch;
        c.d = static_cast<int>(std::floor(notes[slot].length));
        c.extra = notes[slot].length - static_cast<float>(c.d);
        c.shift = notes[slot].nudge;
        c.value = notes[slot].velocity;
        c.e = (notes[slot].ratchet >= 2 ? notes[slot].ratchet : 0) + (notes[slot].glide ? 256 : 0) + ((notes[slot].throwTo & 3) << 9) + (notes[slot].glide >= 2 ? glideKind(notes[slot].glide) << 11 : 0);
        engine.send(c);
    }
}

int Session::packIndex() const {
    for (std::size_t i = 0; i < packs.size(); ++i)
        if (packs[i].name == project.chordPack) return static_cast<int>(i);
    return 0;
}

void Session::choosePack(int index) {
    const int count = static_cast<int>(packs.size());
    index = ((index % count) + count) % count;
    std::snprintf(project.chordPack, sizeof project.chordPack, "%s", packs[static_cast<std::size_t>(index)].name.c_str());
}



void Session::setVolume(float volume) {
    volume = std::round((volume != volume ? volume_ : volume) * 20.f) / 20.f; // in twentieths
    volume_ = volume < 0.f ? 0.f : (volume > 1.f ? 1.f : volume);
    Command c;
    c.type = Command::SetVolume;
    c.value = volume_ * volume_; // squared: even steps to the ear
    engine.send(c);
    if (!programFolder_.empty()) writeTextFile(programFolder_ + "/volume.txt", std::to_string(static_cast<int>(std::round(volume_ * 100.f))) + "\n");
}

// ---- automation ---------------------------------------------------------------------------

void Session::setAutomation(int track, int scene, int lane, int index, float value) {
    if (track < 0 || track >= kNumTracks || scene < 0 || scene >= kMaxScenes || lane < 0 || lane >= kAutoLanes || index < 0 || index >= kAutoSteps || value != value) return;
    value = value < 0.f ? kAutoNone : (value > 1.f ? 1.f : value);
    float& now = project.tracks[track].automation[scene][lane][index];
    if (now == value) return;
    now = value;
    noteEdit(); // a step of undo
    Command c;
    c.type = Command::SetAutomation;
    c.track = track;
    c.scene = scene;
    c.a = lane;
    c.b = index;
    c.value = value;
    engine.send(c);
}

void Session::clearAutomation(int track, int scene, int lane) {
    if (track < 0 || track >= kNumTracks || scene < 0 || scene >= kMaxScenes || lane >= kAutoLanes) return;
    bool any = false;
    for (int l = 0; l < kAutoLanes; ++l)
        if (lane < 0 || lane == l)
            for (float& v : project.tracks[track].automation[scene][l]) any = any || v >= 0.f, v = kAutoNone;
    if (!any) return;
    noteEdit();
    Command c;
    c.type = Command::ClearAutomation;
    c.track = track;
    c.scene = scene;
    c.a = lane < 0 ? -1 : lane;
    engine.send(c);
}

bool Session::hasAutomation(int track, int scene, int lane) const {
    if (track < 0 || track >= kNumTracks || scene < 0 || scene >= kMaxScenes || lane < 0 || lane >= kAutoLanes) return false;
    for (float v : project.tracks[track].automation[scene][lane])
        if (v >= 0.f) return true;
    return false;
}

float Session::automated(int track, int lane, float resting) const {
    const int scene = engine.playScene(), step = engine.playStep();
    if (!project.playing || scene < 0 || step < 0 || track < 0 || track >= kNumTracks || lane < 0 || lane >= kAutoLanes) return resting;
    const TrackData& t = project.tracks[track];
    const float written = t.automation[scene][lane][(engine.playBar() % t.bars[scene]) * kSteps + step];
    return written >= 0.f ? written : resting;
}

// A knob was moved. With WRITE on and the song playing, that is written into the pattern where the song is.
void Session::recordAutomation(int track, int lane, float value) {
    const int scene = engine.playScene(), step = engine.playStep();
    if (!project.writing || !project.playing || scene < 0 || step < 0) return;
    setAutomation(track, scene, lane, (engine.playBar() % project.tracks[track].bars[scene]) * kSteps + step, value);
}

// ---- undo -----------------------------------------------------------------------------------

// The patterns, as the song file writes them. (The whole song's text: only
// the patterns are taken back out of it.)
std::string Session::patternsNow() const { return songText(project); }

std::uint32_t Session::cellMark() const {
    const TrackData& t = project.current();
    std::uint32_t h = 2166136261u + static_cast<std::uint32_t>(project.track * 131 + project.scene);
    const auto mix = [&](const void* data, std::size_t size) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) h = (h ^ bytes[i]) * 16777619u;
    };
    if (t.kind == TrackKind::Drums) {
        for (int bar = 0; bar < kMaxBars; ++bar) {
            const DrumPattern& p = t.drums[project.scene][bar];
            mix(p.level, sizeof p.level);
            mix(p.nudge, sizeof p.nudge);
            mix(p.chance, sizeof p.chance);
            mix(p.start, sizeof p.start);
            mix(p.fx, sizeof p.fx);
        }
    } else if (isSynth(t.kind)) {
        for (int bar = 0; bar < kMaxBars; ++bar) {
            const NotePattern& p = t.notes[project.scene][bar];
            for (const NoteEvent& n : p.notes)
                if (n.velocity > 0.f) mix(&n, sizeof n);
            for (const ChordEvent& c : p.chords)
                if (c.velocity > 0.f) mix(&c, sizeof c);
        }
    }
    return h;
}

// Called when a pattern has just changed. The first change after a still
// moment begins a step: what there was at that still moment goes on the pile.
void Session::noteEdit() {
    if (restoring_ || !undoReady_) return;
    if (!editing_) {
        editing_ = true;
        undo_.push_back(settled_);
        if (static_cast<int>(undo_.size()) > kUndoLevels) undo_.erase(undo_.begin());
        redo_.clear(); // a new change: there is no "forward" any more
    }
    editedAt_ = now_;
}

int Session::undoSteps() const { return static_cast<int>(undo_.size()); }

void Session::restorePatterns(const std::string& text) {
    std::unique_ptr<Project> was(new Project);
    if (!parseSong(text, *was)) return;
    restoring_ = true;
    for (int t = 0; t < kNumTracks; ++t) {
        TrackData& now = project.tracks[t];
        const TrackData& then = was->tracks[t];
        if (now.kind == TrackKind::None || now.kind != then.kind) continue; // a track added since stays as it is
        // A track that is the same then as now is left alone. Most are: an edit is
        // to one track, and sending a track's every pattern again is the costly part.
        if (std::memcmp(now.bars, then.bars, sizeof now.bars) == 0 && std::memcmp(now.drums, then.drums, sizeof now.drums) == 0 &&
            std::memcmp(now.notes, then.notes, sizeof now.notes) == 0 && std::memcmp(now.automation, then.automation, sizeof now.automation) == 0) {
            continue;
        }
        for (int scene = 0; scene < kMaxScenes; ++scene) {
            now.bars[scene] = then.bars[scene];
            for (int bar = 0; bar < kMaxBars; ++bar) {
                now.drums[scene][bar] = then.drums[scene][bar];
                now.notes[scene][bar] = then.notes[scene][bar];
            }
        }
        std::memcpy(now.automation, then.automation, sizeof now.automation);
        sendTrackImage(t);
    }
    Command c;
    c.type = Command::SetSceneRepeats;
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        project.repeats[scene] = was->repeats[scene];
        c.scene = scene;
        c.a = project.repeats[scene];
        engine.send(c);
    }
    c.type = Command::SetSceneSteps;
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        project.steps[scene] = was->steps[scene];
        c.scene = scene;
        c.a = project.steps[scene];
        engine.send(c);
    }
    if (project.bar >= project.current().bars[project.scene]) project.bar = 0;
    syncSong();
    restoring_ = false;
    settled_ = text;
    editing_ = false;
    cellMark_ = cellMark();
}

bool Session::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(patternsNow()); // as things are, to come forward to again
    const std::string back = undo_.back();
    undo_.pop_back();
    restorePatterns(back);
    return true;
}

bool Session::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(patternsNow());
    const std::string forward = redo_.back();
    redo_.pop_back();
    restorePatterns(forward);
    return true;
}

// ---- mute, solo, names ---------------------------------------------------------------------

bool Session::heard(int track) const {
    if (track < 0 || track >= kNumTracks) return false;
    bool anySolo = false;
    for (const TrackData& t : project.tracks) anySolo = anySolo || (t.kind != TrackKind::None && t.solo);
    return anySolo ? project.tracks[track].solo : !project.tracks[track].muted;
}

void Session::sendMutes() {
    Command c;
    c.type = Command::SetTrackHeard;
    for (int t = 0; t < kNumTracks; ++t) {
        c.track = t;
        c.a = heard(t) ? 1 : 0;
        engine.send(c);
    }
}

void Session::setMuted(int track, bool muted) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind == TrackKind::None) return;
    project.tracks[track].muted = muted;
    sendMutes();
}

void Session::setSolo(int track, bool solo) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind == TrackKind::None) return;
    project.tracks[track].solo = solo;
    sendMutes();
}

void Session::hearTrack(int track) {
    if (track < 0 || track >= kNumTracks) return;
    const TrackKind kind = project.tracks[track].kind;
    if (isSynth(kind)) {
        audition(track, 60, 0.8f);
    } else if (kind == TrackKind::Drums) {
        Command c;
        c.type = Command::Trigger;
        c.track = track;
        c.a = 0;
        c.value = 0.8f;
        engine.send(c);
    }
}

void Session::setTrackName(int track, const std::string& name) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind == TrackKind::None) return;
    std::string safe = presetNameFrom(name).substr(0, sizeof project.tracks[track].name - 1);
    while (!safe.empty() && safe.back() == ' ') safe.pop_back(); // the cut may have landed after a space
    std::snprintf(project.tracks[track].name, sizeof project.tracks[track].name, "%s", safe.c_str());
}


// ---- effects ------------------------------------------------------------------------

void Session::setEffect(int track, int slot, EffectKind kind) {
    if (slot < 0 || slot >= slotsOf(track)) return;
    if (static_cast<int>(kind) < 0 || static_cast<int>(kind) >= kEffectKinds) kind = EffectKind::None;
    const int bus = busOfTrack(track);
    if (bus >= 0 && kind != EffectKind::None && effectBus(kind) != bus) return; // ECHO holds a delay, SPACE a reverb
    if (!engine.setEffect(track, slot, kind)) return;
    // Another effect in the slot: automation written for the old one's settings means nothing now.
    if (track >= 0)
        for (int scene = 0; scene < kMaxScenes; ++scene)
            for (int p = 0; p < 4; ++p) clearAutomation(track, scene, autoLaneOfEffect(slot, p));
    EffectData& data = effectData(track, slot);
    data.kind = static_cast<int>(kind);
    for (int i = 0; i < kEffectParams; ++i) data.param[i] = effectDefault(kind, i, bus >= 0); // on a bus, LEVEL at full
}

void Session::setEffectParam(int track, int slot, int index, float value) {
    if (slot < 0 || slot >= slotsOf(track)) return;
    EffectData& data = effectData(track, slot);
    if (index < 0 || index >= effectInfo(static_cast<EffectKind>(data.kind)).params) return;
    value = value != value ? 0.f : (value < 0.f ? 0.f : (value > 1.f ? 1.f : value));
    if (track >= 0) recordAutomation(track, autoLaneOfEffect(slot, index), value);
    if (value == data.param[index]) return;
    data.param[index] = value;
    Command c;
    c.type = Command::SetEffectParam;
    c.track = track;
    c.a = slot;
    c.b = index;
    c.value = value;
    engine.send(c);
}

void Session::setSend(int track, int bus, float value) {
    if (track < 0 || track >= kNumTracks || bus < 0 || bus >= kBuses) return;
    value = value != value ? 0.f : (value < 0.f ? 0.f : (value > 1.f ? 1.f : value));
    recordAutomation(track, autoLaneOfSend(bus), value);
    float& amount = project.tracks[track].send[bus];
    if (value == amount) return;
    amount = value;
    Command c;
    c.type = Command::SetSend;
    c.track = track;
    c.a = bus;
    c.value = value;
    engine.send(c);
}

// ---- recordings on drum lanes -------------------------------------------------------

Session::~Session() {
    cancelBounce();
    if (input_ && rec_ != Rec::Idle && rec_ != Rec::NoInput) input_->stop();
    if (job_ && job_->thread.joinable()) job_->thread.join();
}


void Session::sendTrackImage(int t) {
    const TrackData& track = project.tracks[t];
    std::unique_ptr<TrackImage> image(new TrackImage);
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        image->bars[scene] = track.bars[scene];
        for (int bar = 0; bar < kMaxBars; ++bar) {
            const int slot = scene * kMaxBars + bar;
            const DrumPattern& drums = track.drums[scene][bar];
            for (int lane = 0; lane < kDrumLanes; ++lane)
                for (int step = 0; step < kSteps; ++step) {
                    image->level[slot][lane][step] = drums.level[lane][step];
                    image->nudge[slot][lane][step] = drums.nudge[lane][step];
                    image->chance[slot][lane][step] = drums.chance[lane][step];
                    image->start[slot][lane][step] = drums.start[lane][step];
                    image->fx[slot][lane][step] = drums.fx[lane][step];
                }
            NoteEvent notes[kMaxNotes];
            track.notes[scene][bar].render(notes);
            for (int i = 0; i < kMaxNotes; ++i) {
                const NoteEvent& n = notes[i];
                if (n.velocity <= 0.f) continue;
                image->notes[slot][i].step = static_cast<std::uint8_t>(n.step);
                image->notes[slot][i].pitch = static_cast<std::uint8_t>(n.pitch);
                image->notes[slot][i].length = n.length;
                image->notes[slot][i].nudge = n.nudge;
                image->notes[slot][i].velocity = n.velocity;
                image->notes[slot][i].ratchet = n.ratchet;
                image->notes[slot][i].glide = n.glide;
                image->notes[slot][i].throwTo = n.throwTo;
            }
        }
    }
    bool automated = false;
    for (const auto& scene : track.automation)
        for (const auto& lane : scene)
            for (float v : lane) automated = automated || v >= 0.f;
    if (automated) {
        image->automation.reset(new float[kMaxScenes * kAutoLanes * kAutoSteps]);
        std::memcpy(image->automation.get(), track.automation, sizeof track.automation);
    }
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        image->laneLevel[lane] = track.laneLevel[lane];
        image->lanePart[lane] = track.laneOneSlice[lane] ? 1.f / static_cast<float>(laneSlices(t, lane)) : 0.f;
    }
    engine.setTrackImage(t, std::move(image));
}

// A new handheld's engine is a new song but for the two buses, which hold nothing until they are told.
Session::Session(Engine& engine, Project& project) : engine(engine), project(project), packs(1, builtInPack()) {
    for (int bus = 0; bus < kBuses; ++bus) sendEffects(busTrack(bus));
}

// Tell the engine what every slot of a track (or the master, or a bus) holds, and how each is set.
void Session::sendEffects(int track) {
    for (int slot = 0; slot < slotsOf(track); ++slot) {
        const EffectData data = effectData(track, slot);
        const EffectKind kind = static_cast<EffectKind>(data.kind);
        engine.setEffect(track, slot, kind);
        Command c;
        c.type = Command::SetEffectParam;
        c.track = track;
        c.a = slot;
        for (int i = 0; i < effectInfo(kind).params; ++i) {
            c.b = i;
            c.value = data.param[i];
            engine.send(c);
        }
    }
}

void Session::applyProject() {
    collectJob(true); // nothing half-made carries over
    Command c;
    c.type = Command::Stop;
    engine.send(c);
    project.playing = false;
    for (Sounding& sounding : sounding_) sounding.track = -1;

    for (int track = 0; track < kNumTracks; ++track) engine.removeTrack(track); // those with nothing say so; fine
    for (auto& track : sounds_)
        for (LaneSound& sound : track) {
            const unsigned version = sound.version + 1;
            sound = LaneSound();
            sound.version = version;
        }
    for (int t = 0; t < project.trackCount(); ++t) {
        const TrackData& track = project.tracks[t];
        if (track.kind == TrackKind::Drums) engine.addDrumTrack(t);
        else engine.addSynthTrack(t, instrument(track.kind)->synth);

        sendTrackImage(t); // every pattern on the track, in one piece
        if (track.kind == TrackKind::Drums)
            for (int lane = 0; lane < kDrumLanes; ++lane) loadLane(t, lane);
        if (track.kind == TrackKind::Sampler) {
            c = Command();
            c.type = Command::SetSetting;
            c.track = t;
            for (int id = 0; id < sampler::kSettings; ++id) {
                c.a = id;
                c.b = track.sampler[id];
                engine.send(c);
            }
            loadTrackSample(t);
        }

        if (const SynthSpec* spec = specFor(track.kind)) {
            c = Command();
            c.track = t;
            c.type = Command::SelectPreset;
            c.a = track.preset;
            engine.send(c);
            const int* values = spec->values(const_cast<TrackData&>(track));
            const int count = settingCount(track.kind);
            c.type = Command::SetSetting;
            for (int id = 0; id < count; ++id) {
                c.a = id;
                c.b = values[id];
                engine.send(c);
            }
            c.type = Command::SetMacro;
            for (int m = 0; m < kSynthMacros; ++m) {
                c.a = m;
                c.value = track.macro[m];
                engine.send(c);
            }
        }
    }
    // Every slot, the empty ones too: what the last song left there goes.
    // The two buses and the master are the "tracks" below zero.
    for (int track = busTrack(kBuses - 1); track < kNumTracks; ++track) sendEffects(track);
    // Every send, the ones at nothing too.
    for (int track = 0; track < kNumTracks; ++track)
        for (int bus = 0; bus < kBuses; ++bus) {
            c = Command();
            c.type = Command::SetSend;
            c.track = track;
            c.a = bus;
            c.value = project.tracks[track].send[bus];
            engine.send(c);
        }
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        c = Command();
        c.type = Command::SetSceneRepeats;
        c.scene = scene;
        c.a = project.repeats[scene];
        engine.send(c);
        c.type = Command::SetSceneSteps;
        c.a = project.steps[scene];
        engine.send(c);
    }
    c = Command();
    c.type = Command::SetTempo;
    c.value = project.tempo;
    engine.send(c);
    syncSong(); // the song's length, if it differs from the last one's
    sendMutes(); // the song's mutes; nothing is soloed in a song just opened
    // Another song: nothing of the last one to go back to.
    undo_.clear();
    redo_.clear();
    editing_ = false;
    undoReady_ = false;
}

} // namespace apotheca
