// Session: a synth's sound (settings, macros, presets), the sound designer page, a USB keyboard.
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
#include "presets.hpp"
#include "sampler_screen.hpp"
#include "song_file.hpp"
#include "stretch.hpp"
#include "synth_spec.hpp"

namespace apotheca {

// ---- a synth's sound, and the sound designer ----------------------------------------------

void Session::setSynthSetting(int track, int id, int value) {
    if (track < 0 || track >= kNumTracks) return;
    TrackData& t = project.tracks[track];
    const SynthSpec* spec = specFor(t.kind);
    if (!spec || id < 0 || id >= shownSettingCount(t.kind)) return;
    int realId = id, realValue = value; // (an FM-6 operator's ON is a bit of its OPERATORS ON)
    settingWrite(*spec, spec->values(t), id, value, realId, realValue);
    if (spec->values(t)[realId] == realValue) return;
    spec->values(t)[realId] = realValue;
    Command c;
    c.type = Command::SetSetting;
    c.track = track;
    c.a = realId;
    c.b = realValue;
    engine.send(c);
}

void Session::setSynthMacro(int track, int macro, float value) {
    if (track < 0 || track >= kNumTracks || macro < 0 || macro >= kSynthMacros || value != value) return;
    TrackData& t = project.tracks[track];
    if (!specFor(t.kind)) return;
    value = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
    recordAutomation(track, macro, value); // with WRITE on and the song playing: into the pattern too
    if (t.macro[macro] == value) return;
    t.macro[macro] = value;
    Command c;
    c.type = Command::SetMacro;
    c.track = track;
    c.a = macro;
    c.value = value;
    engine.send(c);
}

void Session::chooseSynthPreset(int track, int index) {
    if (track < 0 || track >= kNumTracks) return;
    TrackData& t = project.tracks[track];
    const SynthSpec* spec = specFor(t.kind);
    if (!spec || index < 0 || index >= spec->presetCount()) return;
    t.preset = index;
    spec->loadPreset(t, index);
    for (float& m : t.macro) m = 0.5f; // a new sound starts as it was made
    Command c;
    c.track = track;
    if (index < enginePresets(t.kind)) {
        c.type = Command::SelectPreset; // the engine has these itself, and puts its macros back
        c.a = index;
        engine.send(c);
        // ...and is told the knobs too, as sendSong tells them: the engine is always told the
        // project's macros, never left to agree with them by itself.
        for (int m = 0; m < kSynthMacros; ++m) {
            Command k;
            k.type = Command::SetMacro;
            k.track = track;
            k.a = m;
            k.value = t.macro[m];
            engine.send(k);
        }
    } else {
        // One the app holds (an added built-in, the player's own): the engine is told every setting,
        // and the macros it was made with.
        const std::vector<float>& macros = listedPreset(t.kind, index)->macros;
        for (int m = 0; m < kSynthMacros && m < static_cast<int>(macros.size()); ++m) t.macro[m] = macros[m] < 0.f ? 0.f : (macros[m] > 1.f ? 1.f : macros[m]);
        sendSynthPatch(track);
    }
    putPresetSound(track, presetSound(t.kind, index));
}

// The track's sound past its synth as a preset picked leaves it (presets-plan.md). A whole preset
// puts its own in; a plain one leaves the track's alone, but for one thing: where the track still
// holds just what a whole preset put in a moment ago (nothing turned since), the plain one puts
// back what the track had before that preset came, so passing over a whole preset on the way to
// a plain one does not leave its effects behind.
void Session::putPresetSound(int track, const TrackSound* sound) {
    PresetSoundMemory& memory = presetSounds_[track];
    const TrackData& t = project.tracks[track];
    const bool untouched = memory.held && trackSoundText(t.kind, trackSoundOf(t)) == memory.put;
    if (sound) {
        if (!untouched) memory.before = trackSoundOf(t);
        memory.held = true;
        putTrackSound(track, *sound);
        memory.put = trackSoundText(t.kind, trackSoundOf(t));
        return;
    }
    if (untouched) putTrackSound(track, memory.before);
    memory = PresetSoundMemory();
}

// A new sound from the dice (PHYSIC's Y). A change of the sound like any other: undo sees it (soundMark).
bool Session::rollSynth(int track, unsigned seed) {
    if (track < 0 || track >= kNumTracks) return false;
    TrackData& t = project.tracks[track];
    const SynthSpec* spec = specFor(t.kind);
    if (!spec || !spec->roll) return false;
    spec->roll(t, seed);
    sendSynthPatch(track);
    return true;
}

// Tell the engine every setting and macro of a synth track, as the project has them.
void Session::sendSynthPatch(int track) {
    TrackData& t = project.tracks[track];
    const SynthSpec* spec = specFor(t.kind);
    if (!spec) return;
    Command c;
    c.track = track;
    const int* values = spec->values(t);
    c.type = Command::SetSetting;
    for (int id = 0; id < settingCount(t.kind); ++id) {
        c.a = id;
        c.b = values[id];
        engine.send(c);
    }
    c.type = Command::SetMacro;
    for (int m = 0; m < kSynthMacros; ++m) {
        c.a = m;
        c.value = t.macro[m];
        engine.send(c);
    }
}

namespace {
std::string jsonText(const char* text) {
    std::string out = "\"";
    for (const char* c = text; *c; ++c) {
        if (*c == '"' || *c == '\\') out += '\\';
        if (static_cast<unsigned char>(*c) >= 32) out += *c;
    }
    return out + "\"";
}
} // namespace

void Session::usePresetFolder(const std::string& folder) {
    presetFolder_ = folder;
    mkdir(folder.c_str(), 0755);
    for (int i = 0; i < kInstrumentCount; ++i) {
        if (!kInstruments[i].presetEnding) continue; // it keeps no presets
        const TrackKind kind = kInstruments[i].kind;
        const std::string ending = kInstruments[i].presetEnding;
        std::vector<std::string> names;
        if (DIR* dir = opendir(folder.c_str())) {
            while (const dirent* entry = readdir(dir)) {
                const std::string name = entry->d_name;
                if (name.size() > ending.size() && name.compare(name.size() - ending.size(), ending.size(), ending) == 0 && name[0] != '.') names.push_back(name);
            }
            closedir(dir);
        }
        std::sort(names.begin(), names.end());
        std::vector<UserPreset> presets;
        for (const std::string& name : names) {
            UserPreset preset;
            std::string text;
            preset.name = presetNameFrom(name.substr(0, name.size() - ending.size()));
            if (!preset.name.empty() && readTextFile(folder + "/" + name, text) && readPresetFile(kind, text, preset)) presets.push_back(preset);
        }
        setUserPresets(kind, presets);
    }
}

bool Session::savePreset(int track, const std::string& typed) {
    if (track < 0 || track >= kNumTracks || presetFolder_.empty()) return false;
    TrackData& t = project.tracks[track];
    const SynthSpec* spec = specFor(t.kind);
    const std::string name = presetNameFrom(typed);
    const InstrumentInfo* info = instrument(t.kind);
    if (!spec || !info || !info->presetEnding || name.empty()) return false;
    // The synth's sound and the track's whole sound past it: effects, sends, GLIDE FROM, MODs.
    if (!writeTextFile(presetFolder_ + "/" + name + info->presetEnding, presetFileText(t))) return false;
    // It is in the list now; the track is on it. The sound itself has not changed, and it is the
    // track's own now: a plain preset picked next leaves it.
    usePresetFolder(presetFolder_);
    presetSounds_[track] = PresetSoundMemory();
    const std::vector<UserPreset>& mine = userPresets(t.kind);
    for (std::size_t i = 0; i < mine.size(); ++i)
        if (mine[i].name == name) t.preset = builtInPresets(t.kind) + static_cast<int>(i);
    return true;
}

// What the track has, for the page to build its controls from: a synth's pages and presets, or a
// sampler's settings and recording (samplerSpec). "page" says which; the page asks for this again
// when it changes (the state's "spec" number: serveDesigner).
std::string Session::designSpec(int track) const {
    const TrackKind kind = project.tracks[track].kind;
    if (kind == TrackKind::Sampler) return samplerSpec(track);
    const SynthSpec* spec = specFor(kind);
    if (!spec) return "{\"name\":\"\"}";
    std::string out = "{\"name\":" + jsonText(spec->name) + (spec->roll ? ",\"roll\":true" : "") + ",\"macros\":[";
    for (int m = 0; m < kSynthMacros; ++m) out += (m ? "," : "") + jsonText(spec->macroNames[m]);
    out += "],\"pages\":[";
    char text[48];
    for (int page = 1; page < spec->pages; ++page) {
        out += (page > 1 ? ",{\"name\":" : "{\"name\":") + jsonText(spec->pageNames[page]) + ",\"env\":" + (spec->envelopePage(page) ? "true" : "false") + ",\"items\":[";
        for (int i = 0; i < spec->itemCount(page); ++i) {
            int id = 0;
            const char* name = "";
            spec->item(page, i, id, name);
            const int lo = spec->settingMin(id), hi = spec->settingMax(id);
            std::snprintf(text, sizeof text, ",\"id\":%d,\"min\":%d,\"max\":%d", id, lo, hi);
            out += (i ? ",{\"name\":" : "{\"name\":") + jsonText(name) + text;
            // A setting with few values is a choice: the page is given their names. JAX-EP's knobs
            // too (pageReadsAsDevice): the page then shows 7.0 where the device does, not 70.
            if (hi - lo <= 40 || spec->pageReadsAsDevice) {
                out += ",\"texts\":[";
                for (int v = lo; v <= hi; ++v) {
                    spec->settingText(id, v, text, sizeof text);
                    out += (v > lo ? "," : "") + jsonText(text);
                }
                out += "]";
            }
            out += "}";
        }
        out += "]}";
    }
    out += "],\"presets\":[";
    for (int i = 0; i < spec->presetCount(); ++i)
        out += (i ? ",{\"name\":" : "{\"name\":") + jsonText(spec->presetName(i)) + ",\"folder\":" + jsonText(spec->presetCategory(i)) + "}";
    return out + "],\"page\":\"synth\"}";
}

// A SAMPLER's page: its eight settings in the order its screen shows them, each with every reading
// the screen gives (PITCH is kept as the root key, so it is marked "flip": the page's slider goes up
// as the pitch does), and its recording as a picture with START and END on it.
//
// "waves" is a list of pictures, one here; the KIT's page will have one a lane. A picture is the
// recording's outline (its 512 loudest points, 0 to 1000, made on this thread when the recording
// was read: LaneSound::peaks), and the settings that mark it ("marks": a setting's id, at 0 to 100%
// of the way along), which never come within "least" of each other (sampler::heldApart).
std::string Session::samplerSpec(int track) const {
    const TrackData& t = project.tracks[track];
    const LaneSound& sound = sounds_[track][0];
    char text[128];
    std::string out = "{\"name\":\"SAMPLER\",\"macros\":[],\"pages\":[{\"name\":\"SETTINGS\",\"env\":false,\"items\":[";
    for (int place = 0; place < sampler::kSettings; ++place) {
        const int id = SamplerScreen::settingAt(place), lo = sampler::settingMin(id), hi = sampler::settingMax(id);
        std::snprintf(text, sizeof text, ",\"id\":%d,\"min\":%d,\"max\":%d", id, lo, hi);
        out += (place ? ",{\"name\":" : "{\"name\":") + jsonText(samplerSettingName(id)) + text + (id == sampler::Root ? ",\"flip\":true" : "") + ",\"texts\":[";
        for (int v = lo; v <= hi; ++v) {
            samplerSettingText(id, v, text, sizeof text);
            out += (v > lo ? "," : "") + jsonText(text);
        }
        out += "]}";
    }
    out += "]}],\"presets\":[],\"page\":\"sampler\"";
    std::snprintf(text, sizeof text, ",\"hear\":%d,\"waves\":[{\"seconds\":%.3f,\"missing\":%s,\"recording\":", kHearKey, sound.seconds, sound.missing ? "true" : "false");
    out += text + jsonText(t.sample);
    std::snprintf(text, sizeof text, ",\"marks\":[%d,%d],\"least\":%d,\"peaks\":[", sampler::Start, sampler::End, sampler::kLeastSpan);
    out += text;
    for (std::size_t i = 0; i < sound.peaks.size(); ++i) {
        const float p = sound.peaks[i]; // (loudest, so never under 0; a float file's may pass 1)
        std::snprintf(text, sizeof text, i ? ",%d" : "%d", static_cast<int>(std::lround((p > 1.f ? 1.f : p) * 1000.f)));
        out += text;
    }
    out += "]}],\"recordings\":[";
    for (std::size_t i = 0; i < designRecordings_.size(); ++i) out += (i ? "," : "") + jsonText(designRecordings_[i].c_str());
    return out + "]}";
}

std::string Session::designState() const {
    const TrackData& t = project.tracks[project.track];
    const SynthSpec* spec = specFor(t.kind);
    char text[96];
    if (t.kind == TrackKind::Sampler) {
        std::snprintf(text, sizeof text, "{\"track\":%d,\"name\":\"SAMPLER\",\"spec\":%u,\"called\":", project.track + 1, designSpecSerial_);
        std::string out = text + jsonText(t.name) + ",\"presets\":0,\"preset\":-1,\"macros\":[],\"values\":[";
        for (int id = 0; id < sampler::kSettings; ++id) std::snprintf(text, sizeof text, id ? ",%d" : "%d", t.sampler[id]), out += text;
        return out + "]}";
    }
    if (!spec) {
        std::snprintf(text, sizeof text, "{\"track\":%d,\"name\":\"\",\"spec\":%u}", project.track + 1, designSpecSerial_);
        return text;
    }
    std::snprintf(text, sizeof text, "{\"track\":%d,\"name\":", project.track + 1);
    std::string out = text + jsonText(spec->name);
    std::snprintf(text, sizeof text, ",\"spec\":%u", designSpecSerial_);
    out += text;
    out += ",\"called\":" + jsonText(t.name);
    std::snprintf(text, sizeof text, ",\"presets\":%d,\"preset\":%d,\"values\":[", spec->presetCount(), t.preset);
    out += text;
    const int* values = spec->values(const_cast<TrackData&>(t));
    // Every setting a page shows, by its id: an FM-6 operator's ON (past the synth's own) is its bit.
    for (int id = 0; id < shownSettingCount(t.kind); ++id) std::snprintf(text, sizeof text, id ? ",%d" : "%d", settingValue(*spec, values, id)), out += text;
    out += "],\"macros\":[";
    for (int m = 0; m < kSynthMacros; ++m) std::snprintf(text, sizeof text, m ? ",%.4g" : "%.4g", t.macro[m]), out += text;
    return out + "]}";
}

namespace {
constexpr Uint32 kDesignListEveryMs = 5000; // a sampler's page sees a recording put on the card by other ways this soon
}

void Session::serveDesigner() {
    const auto letGo = [&] {
        Command c;
        c.type = Command::NoteOff;
        c.track = designHeldTrack_;
        for (int key = 0; key < 128; ++key) {
            if (!designHeld_[key]) continue;
            designHeld_[key] = false;
            c.a = key;
            engine.send(c);
        }
    };
    if (!transfer_.running()) return letGo();
    const TrackKind kind = project.current().kind;
    const bool sampler = kind == TrackKind::Sampler;
    for (const DesignRequest& r : designer_.take()) {
        if (!specFor(kind) && !sampler) continue; // no synth or sampler on this track: nothing to do it to
        switch (r.kind) {
        case DesignRequest::Set:
            if (sampler) { // as its screen turns it: START and END never cross (an id it has not: nothing)
                const int* v = project.current().sampler;
                setSamplerSetting(project.track, r.a, sampler::heldApart(r.a, r.b, v[sampler::Start], v[sampler::End]));
            } else {
                setSynthSetting(project.track, r.a, r.b);
            }
            break;
        case DesignRequest::Sample: // only a recording the page was offered (it was on the card), or none; a sampler's only
            if (r.name.empty() || std::find(designRecordings_.begin(), designRecordings_.end(), r.name) != designRecordings_.end()) setTrackSample(project.track, r.name);
            break;
        case DesignRequest::Macro: setSynthMacro(project.track, r.a, r.value); break;
        case DesignRequest::Preset: chooseSynthPreset(project.track, r.a); break;
        case DesignRequest::Save: savePreset(project.track, r.name); break;
        case DesignRequest::Name: setTrackName(project.track, r.name); break;
        case DesignRequest::Roll: rollSynth(project.track, now_ * 2654435761u + static_cast<unsigned>(r.a)); break;
        case DesignRequest::Note: {
            if (designHeldTrack_ != project.track) letGo(); // another track now: the old keys come up first
            designHeldTrack_ = project.track;
            if (designHeld_[r.a] == (r.b != 0)) break;
            designHeld_[r.a] = r.b != 0;
            Command c;
            c.type = r.b ? Command::NoteOn : Command::NoteOff;
            c.track = project.track;
            c.a = r.a;
            c.value = kPlacedLevel;
            engine.send(c);
            break;
        }
        }
    }
    // The page asks how things are about once a second. When it stops (the
    // tab was closed, the WiFi dropped) any key it was holding is let go.
    if (designer_.looks() != designLooks_) designLooks_ = designer_.looks(), designSeenAt_ = now_;
    else if (now_ - designSeenAt_ > 3000) letGo();

    // The recordings a sampler's page offers: read from the card here, never by the server's thread.
    const bool looking = now_ - designSeenAt_ <= 3000 && designLooks_ != 0;
    bool listed = false;
    if (sampler && looking && (!designListed_ || now_ - designListAt_ >= kDesignListEveryMs || transfer_.received() != designReceived_)) {
        designRecordings_ = sampleNames();
        designListAt_ = now_;
        designReceived_ = transfer_.received();
        designListed_ = listed = true;
    }
    const SynthSpec* spec = specFor(kind);
    const int presets = spec ? spec->presetCount() : 0;
    const unsigned sound = sounds_[project.track][0].version; // (a sampler's recording read again: its picture)
    if (kind != designSpecKind_ || presets != designSpecPresets_ || project.track != designSpecTrack_ || sound != designSpecSound_ || listed) {
        designSpecKind_ = kind;
        designSpecPresets_ = presets;
        designSpecTrack_ = project.track;
        designSpecSound_ = sound;
        std::string text = designSpec(project.track);
        if (text != designSpecText_) designSpecText_.swap(text), ++designSpecSerial_;
    }
    designer_.show(designSpecText_, designState());
}

void Session::setMidiRecording(bool on) {
    midiRecord_ = on;
    keepSettings();
}

// What the USB keyboard played since the last frame. The sound has already
// been made (the keyboard's own thread saw to that); this is for the cursor
// and for recording.
void Session::hearMidi() {
    const TrackKind kind = project.current().kind;
    midi_.setTarget(project.track, kind == TrackKind::Drums ? MidiInput::Drums : isSynth(kind) ? MidiInput::Synth : MidiInput::Nothing);
    MidiEvent event;
    while (midi_.poll(event)) {
        ++midiHeard_;
        if (event.kind == MidiEvent::NoteOn) midiKey_ = event.note;
        const int scene = playScene(), step = engine.playStep();
        // Let go: the recorded note is as long as it was held, in whole steps, to the end of its bar at most.
        Recording& rec = recordingNote_[event.note];
        if (event.kind == MidiEvent::NoteOff && rec.on) {
            rec.on = false;
            TrackData& track = project.tracks[rec.track];
            NoteEvent& note = track.notes[rec.scene][rec.bar].notes[rec.slot];
            if (note.velocity > 0.f && note.pitch == event.note) {
                const int bars = track.bars[rec.scene];
                const int all = bars * kSteps, now = scene == rec.scene && step >= 0 ? (engine.playBar() % bars) * kSteps + step : rec.startedAt;
                int held = ((now - rec.startedAt) % all + all) % all;
                if (held < 1) held = 1;
                if (held > kSteps - note.step) held = kSteps - note.step;
                note.length = static_cast<float>(held);
                sendNoteBar(rec.track, rec.scene, rec.bar);
            }
            continue;
        }
        if (event.kind != MidiEvent::NoteOn || !midiRecord_ || !project.playing || scene < 0 || step < 0) continue;
        TrackData& track = project.current();
        const int bar = engine.playBar() % track.bars[scene];
        if (kind == TrackKind::Drums) {
            DrumPattern& pattern = track.drums[scene][bar];
            const int lane = midiDrumLane(event.note);
            pattern.level[lane][step] = event.velocity < 0.05f ? 0.05f : event.velocity;
            pattern.fromDice[lane][step] = false; // played by hand: the player's own
            sendDrumHit(project.track, scene, bar, lane, step); // the one hit: the whole cell for each filled the queue
        } else if (isSynth(kind)) {
            NotePattern& pattern = track.notes[scene][bar];
            int slot = pattern.at(event.note, step);
            if (slot < 0) slot = pattern.used() < kMaxNotes ? pattern.freeSlot() : -1;
            if (slot < 0) continue; // the bar is full
            NoteEvent& note = pattern.notes[slot];
            note.step = step;
            note.pitch = event.note;
            note.length = 1.f;
            note.nudge = 0.f;
            note.velocity = event.velocity < 0.05f ? 0.05f : event.velocity;
            rec.on = true;
            rec.track = project.track;
            rec.scene = scene;
            rec.bar = bar;
            rec.slot = slot;
            rec.startedAt = bar * kSteps + step;
            sendNoteBar(project.track, scene, bar);
        }
    }
}

} // namespace apotheca
