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
#include "song_file.hpp"
#include "stretch.hpp"
#include "synth_spec.hpp"

namespace apotheca {

// ---- a synth's sound, and the sound designer ----------------------------------------------

void Session::setSynthSetting(int track, int id, int value) {
    if (track < 0 || track >= kNumTracks) return;
    TrackData& t = project.tracks[track];
    const SynthSpec* spec = specFor(t.kind);
    if (!spec || id < 0 || id >= settingCount(t.kind)) return;
    value = value < spec->settingMin(id) ? spec->settingMin(id) : (value > spec->settingMax(id) ? spec->settingMax(id) : value);
    if (spec->values(t)[id] == value) return;
    spec->values(t)[id] = value;
    Command c;
    c.type = Command::SetSetting;
    c.track = track;
    c.a = id;
    c.b = value;
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
    const int own = index - builtInPresets(t.kind);
    if (own < 0) {
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
        return;
    }
    // One of the player's own: the engine is told every setting, and the macros it was saved with.
    const std::vector<float>& macros = userPresets(t.kind)[own].macros;
    for (int m = 0; m < kSynthMacros && m < static_cast<int>(macros.size()); ++m) t.macro[m] = macros[m] < 0.f ? 0.f : (macros[m] > 1.f ? 1.f : macros[m]);
    sendSynthPatch(track);
}

// A new sound from the dice, for a synth that generates its sounds (QMOD).
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
// A preset file: "apotheca-preset 1", then "macros" and "settings" lines.
bool readPreset(const std::string& text, UserPreset& out) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 15, "apotheca-preset") != 0) return false;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        std::string word;
        in >> word;
        double v = 0.0;
        if (word == "macros") while (in >> v) out.macros.push_back(std::isfinite(v) ? static_cast<float>(v) : 0.5f);
        if (word == "settings") while (in >> v) out.values.push_back(std::isfinite(v) ? static_cast<int>(v) : 0);
    }
    return !out.values.empty();
}

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
            if (!preset.name.empty() && readTextFile(folder + "/" + name, text) && readPreset(text, preset)) presets.push_back(preset);
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
    std::string text = "apotheca-preset 1\nsynth ";
    text += info->word;
    text += "\nmacros";
    char number[32];
    for (float m : t.macro) std::snprintf(number, sizeof number, " %.6g", m), text += number;
    text += "\nsettings";
    const int* values = spec->values(t);
    for (int id = 0; id < settingCount(t.kind); ++id) std::snprintf(number, sizeof number, " %d", values[id]), text += number;
    text += "\n";
    if (!writeTextFile(presetFolder_ + "/" + name + info->presetEnding, text)) return false;
    // It is in the list now; the track is on it. The sound itself has not changed.
    usePresetFolder(presetFolder_);
    const std::vector<UserPreset>& mine = userPresets(t.kind);
    for (std::size_t i = 0; i < mine.size(); ++i)
        if (mine[i].name == name) t.preset = builtInPresets(t.kind) + static_cast<int>(i);
    return true;
}

// What the synth has, for the page to build its controls from. It only
// changes when the synth does, or a preset is saved.
std::string Session::designSpec(TrackKind kind) const {
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
            // A setting with few values is a choice: the page is given their names.
            if (hi - lo <= 40) {
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
    return out + "]}";
}

std::string Session::designState() const {
    const TrackData& t = project.tracks[project.track];
    const SynthSpec* spec = specFor(t.kind);
    char text[64];
    if (!spec) {
        std::snprintf(text, sizeof text, "{\"track\":%d,\"name\":\"\"}", project.track + 1);
        return text;
    }
    std::snprintf(text, sizeof text, "{\"track\":%d,\"name\":", project.track + 1);
    std::string out = text + jsonText(spec->name);
    out += ",\"called\":" + jsonText(t.name);
    std::snprintf(text, sizeof text, ",\"presets\":%d,\"preset\":%d,\"values\":[", spec->presetCount(), t.preset);
    out += text;
    const int* values = spec->values(const_cast<TrackData&>(t));
    for (int id = 0; id < settingCount(t.kind); ++id) std::snprintf(text, sizeof text, id ? ",%d" : "%d", values[id]), out += text;
    out += "],\"macros\":[";
    for (int m = 0; m < kSynthMacros; ++m) std::snprintf(text, sizeof text, m ? ",%.4g" : "%.4g", t.macro[m]), out += text;
    return out + "]}";
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
    for (const DesignRequest& r : designer_.take()) {
        if (!specFor(kind)) continue; // no synth on this track: nothing to do it to
        switch (r.kind) {
        case DesignRequest::Set: setSynthSetting(project.track, r.a, r.b); break;
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
            c.value = 0.8f;
            engine.send(c);
            break;
        }
        }
    }
    // The page asks how things are about once a second. When it stops (the
    // tab was closed, the WiFi dropped) any key it was holding is let go.
    if (designer_.looks() != designLooks_) designLooks_ = designer_.looks(), designSeenAt_ = now_;
    else if (now_ - designSeenAt_ > 3000) letGo();

    const SynthSpec* spec = specFor(kind);
    const int presets = spec ? spec->presetCount() : 0;
    if (kind != designSpecKind_ || presets != designSpecPresets_) {
        designSpecKind_ = kind;
        designSpecPresets_ = presets;
        designSpecText_ = designSpec(kind);
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
