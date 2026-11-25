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
#include <functional>
#include <memory>

#include "samples.hpp"
#include "session_parts.hpp"
#include "presets.hpp"
#include "drum_voices.hpp"
#include "kit_screen.hpp"
#include "lane_edit.hpp"
#include "lane_feel.hpp"
#include "mod_screen.hpp"
#include "mods.hpp"
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
    // MORPH reads TABLE A, TABLE B (B on or off), UNISON and WIDTH at a key's strike: a key X holds
    // on this track is struck again, so the change is heard (wavetable-plan.md, call 13). It stays held.
    if (t.kind == TrackKind::Morph && (realId == morph::TableA || realId == morph::TableB || realId == morph::Unison || realId == morph::Width) && hearingHeld() &&
        heard_.lane < 0 && heard_.track == track)
        audition(track, heard_.pitch, kPlacedLevel);
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
    if (const UserPreset* own = holdsRecording(t.kind) ? listedPreset(t.kind, index) : nullptr)
        if (!own->recording.empty()) putPresetRecording(track, own->recording);
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

void Session::keepSound(int track) {
    kept_.track = -1;
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind == TrackKind::None) return;
    if (!kept_.was) kept_.was.reset(new TrackData); // (large: kept on the heap, made once)
    kept_.was->fieldsFrom(project.tracks[track]);
    kept_.memory = presetSounds_[track];
    const TrackData& t = project.tracks[track];
    kept_.lanes.clear(); // (its room kept: one A down after another makes nothing new)
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        if (!t.automation.has(scene)) continue; // (nothing written there)
        for (int lane = autoLaneOfEffect(0, 0); lane < autoLaneOfEffect(kTrackSlots, 0); ++lane)
            for (int step = 0; step < kAutoSteps; ++step) {
                const float v = t.automation[scene][lane][step];
                if (v >= 0.f) kept_.lanes.push_back({scene, lane, step, v});
            }
    }
    for (int slot = 0; slot < kTrackSlots; ++slot) kept_.types[slot] = typeMemory_[memorySlot(track, slot)];
    kept_.track = track;
}

// Each slot that holds the effect it held when A went down: its lanes in every scene as they were then, and the
// TYPEs it remembered. Only a whole preset passed over (setEffect) changes them while A is held, so most often
// nothing differs and nothing is done. Written into the song and the track sent once, whole (as FX's move of a
// line: a command for every step could fill the engine's queue); a step of undo with the rest.
void Session::putEffectLanesBack(int track) {
    TrackData& t = project.tracks[track];
    const TrackData& was = *kept_.was;
    bool slot[kTrackSlots];
    for (int s = 0; s < kTrackSlots; ++s) {
        slot[s] = t.effects[s].kind == was.effects[s].kind;
        if (slot[s]) typeMemory_[memorySlot(track, s)] = kept_.types[s];
    }
    // The same as kept? Every kept step there, and no more written than were kept.
    int now = 0, same = 0, kept = 0;
    for (const KeptPoint& p : kept_.lanes)
        if (slot[autoSlotOfLane(p.lane)]) {
            ++kept;
            same += t.automation.has(p.scene) && t.automation[p.scene][p.lane][p.step] == p.value ? 1 : 0;
        }
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        if (!t.automation.has(scene)) continue;
        for (int lane = autoLaneOfEffect(0, 0); lane < autoLaneOfEffect(kTrackSlots, 0); ++lane)
            if (slot[autoSlotOfLane(lane)])
                for (float v : t.automation[scene][lane]) now += v >= 0.f ? 1 : 0;
    }
    if (now == kept && same == kept) return;
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        if (!t.automation.has(scene)) continue;
        for (int lane = autoLaneOfEffect(0, 0); lane < autoLaneOfEffect(kTrackSlots, 0); ++lane)
            if (slot[autoSlotOfLane(lane)])
                for (float& v : t.automation[scene][lane]) v = kAutoNone;
    }
    for (const KeptPoint& p : kept_.lanes)
        if (slot[autoSlotOfLane(p.lane)]) t.automation[p.scene][p.lane][p.step] = p.value;
    noteEdit();
    sendTrackImage(track);
}

// Through the usual setters, each only where it differs: the kit's lanes as a kit is put on (putKit),
// the recording, the sampler's settings, a synth's patch as undo puts one back (sendSynthPatch: every
// setting and macro, whatever preset it was on), then the track sound (putTrackSound) and, last, what a
// whole preset had found, so a plain preset picked next does what it would have done without the trip.
bool Session::putSoundBack(int track) {
    if (track < 0 || track >= kNumTracks || track != kept_.track || !kept_.was) return false;
    TrackData& t = project.tracks[track];
    TrackData& was = *kept_.was;
    if (t.kind != was.kind || t.kind == TrackKind::None) return false;
    if (t.kind == TrackKind::Drums && kitText(t) != kitText(was)) putKit(track, was);
    if (holdsRecording(t.kind) && std::strcmp(t.sample, was.sample) != 0) setTrackSample(track, was.sample);
    if (t.kind == TrackKind::Sampler) {
        for (int id = 0; id < sampler::kSettings; ++id) setSamplerSetting(track, id, was.sampler[id]);
        if (t.samplerChop != was.samplerChop) setSamplerChop(track, was.samplerChop);
    }
    if (const SynthSpec* spec = specFor(t.kind)) {
        const std::size_t size = sizeof(int) * static_cast<std::size_t>(settingCount(t.kind));
        if (t.preset != was.preset || std::memcmp(spec->values(t), spec->values(was), size) != 0 || std::memcmp(t.macro, was.macro, sizeof t.macro) != 0) {
            t.preset = was.preset;
            std::memcpy(spec->values(t), spec->values(was), size);
            std::memcpy(t.macro, was.macro, sizeof t.macro);
            sendSynthPatch(track);
        }
    }
    putTrackSound(track, trackSoundOf(was));
    putEffectLanesBack(track);
    presetSounds_[track] = kept_.memory;
    return true;
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
// A picture's points as the page is given them: 0 to 1000 (a float file's may pass full: held there).
std::string peaksText(const std::vector<float>& peaks) {
    std::string out;
    char text[16];
    for (std::size_t i = 0; i < peaks.size(); ++i) {
        const float p = peaks[i];
        std::snprintf(text, sizeof text, i ? ",%d" : "%d", static_cast<int>(std::lround((p > 1.f ? 1.f : p) * 1000.f)));
        out += text;
    }
    return out;
}
// One of a KIT's settings, as every lane has it: a range (min to max) or stops (only those values),
// with every reading where a reading does not hang on the lane (START and END are a time into the
// lane's own file: "time", read on the page and given in the state).
std::string kitField(const char* name, int field, const char* rest) {
    char text[64];
    std::snprintf(text, sizeof text, ",\"field\":%d", field);
    return std::string("{\"name\":") + jsonText(name) + text + rest;
}
} // namespace

void Session::usePresetFolder(const std::string& folder) {
    presetFolder_ = folder;
    kits_.clear(); // (read again when next asked for: the folder's own kits)
    samplerPresets_.clear(); // (the sampler's: the same)
    samplerPresetsRead_ = false;
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
            preset.file = name;
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
    if (undoReady_) soundMark_ = soundMark(); // retargeting the number is not an edit (a file saved)
    return true;
}

bool Session::deleteSynthPreset(int track) {
    if (track < 0 || track >= kNumTracks || presetFolder_.empty()) return false;
    const TrackKind kind = project.tracks[track].kind;
    const int first = builtInPresets(kind), gone = project.tracks[track].preset;
    const std::vector<UserPreset>& mine = userPresets(kind);
    const int own = gone - first;
    if (!specFor(kind) || own < 0 || own >= static_cast<int>(mine.size()) || mine[static_cast<std::size_t>(own)].file.empty()) return false;
    if (std::remove((presetFolder_ + "/" + mine[static_cast<std::size_t>(own)].file).c_str()) != 0) return false;
    usePresetFolder(presetFolder_); // the list read again: the ones after it are a place earlier now
    // Every track of this synth stays on its preset: past it, a place earlier. A track on the one deleted keeps
    // its sound (nothing is sent: nothing changed), named by the preset before it, and that sound is its own
    // now (as savePreset's): a plain preset picked next leaves its effects.
    for (int i = 0; i < kNumTracks; ++i) {
        TrackData& t = project.tracks[i];
        if (t.kind != kind || t.preset < gone) continue;
        if (t.preset == gone) presetSounds_[i] = PresetSoundMemory();
        --t.preset;
    }
    if (undoReady_) soundMark_ = soundMark(); // retargeting the numbers is not an edit (a file deleted)
    return true;
}

// MORPH's sixteen tables as the page draws them (wavetable-plan.md, batch 4): every frame of each at
// kPagePoints points across one cycle, -100 to 100 (a frame's peak is 1), read straight from the largest
// size as MAIN's picture reads it. Made once: the tables never change. About 110 KB of text, sent with
// the spec, never with the state, so a turned POSITION or TABLE changes no spec.
namespace {
constexpr int kPagePoints = 64;
const std::string& morphTablesText() {
    static const std::string text = [] {
        Morph::makeTables();
        std::string out = ",\"tables\":{\"points\":" + std::to_string(kPagePoints) + ",\"names\":[";
        for (int t = 0; t < morph::kTables; ++t) out += (t ? "," : "") + jsonText(morph::tableName(t));
        out += "],\"frames\":[";
        for (int t = 0; t < morph::kTables; ++t) out += (t ? "," : "") + std::to_string(Morph::tableFrames(t));
        out += "],\"waves\":[";
        char n[16];
        for (int t = 0; t < morph::kTables; ++t) {
            out += t ? ",[" : "[";
            const int frames = Morph::tableFrames(t);
            for (int f = 0; f < frames; ++f)
                for (int i = 0; i < kPagePoints; ++i) {
                    const float v = Morph::tableRead(t, f, static_cast<double>(i) / kPagePoints);
                    std::snprintf(n, sizeof n, "%s%d", f || i ? "," : "", static_cast<int>(std::lround(std::max(-1.f, std::min(1.f, v)) * 100.f)));
                    out += n;
                }
            out += "]";
        }
        return out + "]}";
    }();
    return text;
}
} // namespace

// What the track has, for the page to build its controls from: a synth's pages and presets, or a
// sampler's settings and recording (samplerSpec). "page" says which; the page asks for this again
// when it changes (the state's "spec" number: serveDesigner).
std::string Session::designSpec(int track) const {
    const TrackKind kind = project.tracks[track].kind;
    if (kind == TrackKind::Sampler) return samplerSpec(track);
    if (kind == TrackKind::Drums) return kitSpec(track);
    const SynthSpec* spec = specFor(kind);
    if (!spec) return "{\"name\":\"\"}";
    std::string out = "{\"name\":" + jsonText(spec->name) + (spec->roll ? ",\"roll\":true" : "") + ",\"macros\":[";
    for (int m = 0; m < kSynthMacros; ++m) out += (m ? "," : "") + jsonText(spec->macroNames[m]);
    out += "],\"pages\":[";
    char text[128];
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
    out += "]";
    if (spec->recording) {
        // CLOUD: its recording (or the TONE's outline, "tone"), the list to pick one from, the key a PLAY
        // holds, and its picture's band: POSITION a mark, SPRAY the lit width about it (both settings;
        // the page reads their values from the state, as the device draws them, the macros left out).
        const LaneSound& sound = sounds_[track][0];
        const bool tone = !project.tracks[track].sample[0] || sound.missing || sound.peaks.empty();
        std::snprintf(text, sizeof text, ",\"hear\":60,\"waves\":[{\"seconds\":%.3f,\"missing\":%s,\"tone\":%s,\"recording\":", tone ? cloud::kToneSeconds : static_cast<double>(sound.seconds),
                      sound.missing ? "true" : "false", tone ? "true" : "false");
        out += text + jsonText(project.tracks[track].sample);
        std::snprintf(text, sizeof text, ",\"marks\":[],\"band\":[%d,%d],\"peaks\":[", cloud::Position, cloud::Spray);
        out += text + peaksText(tone ? cloudToneOutline() : sound.peaks) + "]}],\"recordings\":[";
        for (std::size_t i = 0; i < designRecordings_.size(); ++i) out += (i ? "," : "") + jsonText(designRecordings_[i].c_str());
        out += "]";
    }
    if (spec->table) {
        // MORPH: its tables, and which settings pick and place each oscillator's (B's 0 is OFF, then the
        // tables); the page reads their values from the state and draws the frame POSITION reads.
        char oscs[200];
        std::snprintf(oscs, sizeof oscs, ",\"oscs\":[{\"name\":\"A\",\"table\":%d,\"position\":%d,\"off\":false},{\"name\":\"B\",\"table\":%d,\"position\":%d,\"off\":true}]",
                      morph::TableA, morph::PositionA, morph::TableB, morph::PositionB);
        out += morphTablesText() + oscs;
    }
    out += designFxSpec(track) + designModsSpec(track);
    return out + ",\"page\":\"synth\"}";
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
    out += text + peaksText(sound.peaks) + "]}],\"recordings\":[";
    for (std::size_t i = 0; i < designRecordings_.size(); ++i) out += (i ? "," : "") + jsonText(designRecordings_[i].c_str());
    out += "]" + designFxSpec(track) + designModsSpec(track);
    return out + "}";
}


// A drum KIT's page (owner, 2026-11-16: "I think the kit could also be helpful"): its eight lanes, each
// with its sound (the built-in drum or a recording), the picture the KIT page's EDIT view draws (the
// lane's whole file, or the built-in drum: LaneSound::sourcePeaks, made on this thread when it was
// read) with START and END on it, and its settings. "fields" is each setting once, as every lane has
// it; a lane's own is the id lane * "stride" + field (designer.hpp: KitField). "waves" is one picture a
// lane, as the sampler's, its marks the lane's START and END in parts of the file (kEditUnits), never
// closer than "least" (leastSpan: kLeastSpanMs of that file).
std::string Session::kitSpec(int track) const {
    const TrackData& t = project.tracks[track];
    char text[160];
    std::string out = "{\"name\":\"KIT\",\"macros\":[],\"pages\":[],\"presets\":[],\"page\":\"kit\"";
    std::snprintf(text, sizeof text, ",\"stride\":%d,\"fields\":[", kKitStride);
    out += text;
    const auto texts = [&](int lo, int hi, const std::function<void(int, char*)>& read) {
        std::string list = ",\"texts\":[";
        for (int v = lo; v <= hi; ++v) {
            read(v, text);
            list += (v > lo ? "," : "") + jsonText(text);
        }
        return list + "]";
    };
    const auto stops = [&](const int* values, int n, const std::function<void(int, char*)>& read) {
        std::string list = ",\"stops\":[";
        for (int i = 0; i < n; ++i) list += (i ? "," : "") + std::to_string(values[i]);
        list += "],\"texts\":[";
        for (int i = 0; i < n; ++i) {
            read(values[i], text);
            list += (i ? "," : "") + jsonText(text);
        }
        return list + "]";
    };
    for (int row = 0; row < kEditRows; ++row) {
        const auto read = [&](int v, char* into) { laneEditText(laneEditWith(LaneEdit(), row, v), row, 1.f, into, 64); };
        std::string rest;
        if (row == kEditStart || row == kEditEnd) {
            std::snprintf(text, sizeof text, ",\"min\":0,\"max\":%d,\"time\":true}", kEditUnits);
            rest = text;
        } else if (row == kEditFadeIn || row == kEditFadeOut) {
            rest = stops(kFadeStops, kFadeStopCount, read) + "}";
        } else {
            const int lo = row == kEditGain ? -kGainMost : row == kEditPitch ? -kPitchMost : row == kEditFine ? -kFineMost : 0;
            const int hi = row == kEditReverse ? 1 : -lo;
            std::snprintf(text, sizeof text, ",\"min\":%d,\"max\":%d", lo, hi);
            rest = text; // (before texts(), which writes in `text` too)
            rest += texts(lo, hi, read) + "}";
        }
        out += (row ? "," : "") + kitField(laneEditName(row), row, rest.c_str());
    }
    const int meter = project.meterOf(project.scene); // (FIT reads bars of the scene's meter, as the KIT page's box)
    out += "," + kitField("FIT TO", kKitFit, (stops(kLaneFits, kLaneFitCount, [meter](int v, char* into) { laneFitText(v, meter, into, 64); }) + "}").c_str());
    out += "," + kitField("CHOKE", kKitChoke, (",\"min\":0,\"max\":2" + texts(kChokeNone, kChokeGroups, [](int v, char* into) { std::snprintf(into, 64, "%s", laneChokeText(v)); }) + "}").c_str());
    out += "," + kitField("VOLUME", kKitVolume, (",\"min\":0,\"max\":100" + texts(0, 100, [](int v, char* into) { std::snprintf(into, 64, "%d%%", v); }) + "}").c_str());
    const auto feel = [](int v, char* into) { feelAmountText(v, into, 64); };
    out += "," + kitField("HUMAN TIME", kKitHumanTime, (stops(kAmountAt, kAmountClicks, feel) + "}").c_str());
    out += "," + kitField("HUMAN LEVEL", kKitHumanLevel, (stops(kAmountAt, kAmountClicks, feel) + "}").c_str());
    out += "," + kitField("SWING", kKitSwing, (stops(kAmountAt, kAmountClicks, feel) + "}").c_str());
    // DRUM SYNTH: a synth lane's knob P is kKitKnobs + lane * 8 + P (designer.hpp), 0..100, its name in the
    // wave's "synth", its reading in the state. And the models, for the SOUND choice.
    std::snprintf(text, sizeof text, "],\"knobs\":%d", kKitKnobs);
    out += text;
    out += ",\"models\":[";
    for (int m = drumsynth::kNone + 1; m < drumsynth::kModels; ++m)
        out += std::string(m > 1 ? "," : "") + "{\"word\":" + jsonText(drumsynth::modelWord(m)) + ",\"name\":" + jsonText(drumsynth::modelName(m)) + "}";
    out += "],\"waves\":[";
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const LaneSound& sound = sounds_[track][lane];
        std::snprintf(text, sizeof text, "%s{\"lane\":%d,\"drum\":", lane ? "," : "", lane);
        out += text + jsonText(DrumKit::laneName(lane));
        // A synth lane: its model, its places' names and its named voices. Its picture, its length and the
        // least gap of START and END move with every knob, so they come in the state (lanes[].synth), and
        // a knob turned changes no spec (the page would be built again under the slider being dragged).
        const drumsynth::Voice& voice = t.laneSynth[lane];
        const float seconds = voice.on() ? 0.f : sound.sourceSeconds;
        std::snprintf(text, sizeof text, ",\"seconds\":%.3f,\"missing\":%s,\"recording\":", seconds, sound.missing ? "true" : "false");
        out += text + jsonText(t.laneSample[lane]);
        if (voice.on()) {
            out += ",\"synth\":{\"word\":" + jsonText(drumsynth::modelWord(voice.model)) + ",\"name\":" + jsonText(drumsynth::modelName(voice.model)) + ",\"places\":[";
            for (int p = 0; p < drumsynth::kPlaces; ++p) out += std::string(p ? "," : "") + (drumsynth::shown(voice.model, p) ? jsonText(drumsynth::knob(voice.model, p).name) : "null");
            out += "],\"voices\":[";
            for (int v = 0; v < voiceCount(voice.model); ++v) out += std::string(v ? "," : "") + jsonText(voiceName(voice.model, v));
            out += "]}";
        }
        std::snprintf(text, sizeof text, ",\"marks\":[%d,%d],\"least\":%d,\"units\":%d,\"peaks\":[", lane * kKitStride + kEditStart, lane * kKitStride + kEditEnd, voice.on() ? 1 : leastSpan(sound.sourceSeconds), kEditUnits);
        out += text + (voice.on() ? std::string() : peaksText(sound.sourcePeaks)) + "]}";
    }
    out += "],\"recordings\":[";
    for (std::size_t i = 0; i < designRecordings_.size(); ++i) out += (i ? "," : "") + jsonText(designRecordings_[i].c_str());
    out += "]" + designFxSpec(track) + designModsSpec(track);
    return out + "}";
}

// EFFECTS for the page (every instrument's carries it): the track's three slots, its sends, the two
// buses, the master's two slots, the mixer's LEVEL and PAN. A slot says which effects it may hold
// (NONE, then every family's types, as the FX page's list holds them: a bus its own short list) and
// the settings of the one it holds now (their names, and every reading of a setting that is a choice).
// A slot given another effect is a new spec (serveDesigner: designSpecFx_).
std::string Session::designFxSpec(int track) const {
    char text[128];
    const auto kindsText = [&](int bus) {
        std::string list = "[{\"id\":0,\"name\":\"NONE\"}";
        for (int place = 1; place < familyListSize(bus); ++place) {
            const EffectKind first = familyAt(place, bus);
            for (int t = 0; t < effectTypes(first); ++t) {
                const EffectKind kind = effectTypeAt(first, t);
                std::snprintf(text, sizeof text, ",{\"id\":%d,\"name\":", static_cast<int>(kind));
                list += text;
                const char* shown = effectTypeShown(kind);
                if (shown[0]) {
                    char both[64];
                    std::snprintf(both, sizeof both, "%s %s", effectFamily(kind), shown);
                    list += jsonText(both);
                } else list += jsonText(effectFamily(kind));
                list += "}";
            }
        }
        return list + "]";
    };
    const auto slotText = [&](const char* name, int bus, EffectKind kind) {
        // The name first, on a line of its own: it may be `text` itself, which kindsText writes into, and
        // the order of a + b + c is the compiler's (GCC took kindsText first: SLOT 1 came out a family name).
        std::string out = "{\"name\":" + jsonText(name);
        out += ",\"kinds\":" + kindsText(bus) + ",\"params\":[";
        const EffectInfo& info = effectInfo(kind);
        for (int i = 0; i < info.params; ++i) {
            std::snprintf(text, sizeof text, "%s{\"name\":", i ? "," : "");
            // On a bus the MIX reads LEVEL: how loud the bus comes back (fx_screen.cpp).
            out += text + jsonText(bus >= 0 && i == effectMixParam(kind) ? "LEVEL" : info.paramNames[i]);
            const int choices = effectChoices(kind, i);
            std::snprintf(text, sizeof text, ",\"choices\":%d", choices);
            out += text;
            if (choices > 0) {
                out += ",\"texts\":[";
                for (int c = 0; c < choices; ++c) {
                    effectParamText(kind, i, (static_cast<float>(c) + 0.5f) / static_cast<float>(choices), text, sizeof text);
                    out += (c ? "," : "") + jsonText(text);
                }
                out += "]";
            }
            out += "}";
        }
        return out + "]}";
    };
    const TrackData& t = project.tracks[track];
    std::string out = ",\"fx\":{\"slots\":[";
    for (int s = 0; s < kTrackSlots; ++s) {
        std::snprintf(text, sizeof text, "SLOT %d", s + 1);
        out += (s ? "," : "") + slotText(text, -1, static_cast<EffectKind>(t.effects[s].kind));
    }
    out += "],\"buses\":[";
    const char* const busNames[kBuses] = {"ECHO", "SPACE"};
    for (int b = 0; b < kBuses; ++b) out += (b ? "," : "") + slotText(busNames[b], b, static_cast<EffectKind>(project.busEffects[b].kind));
    out += "],\"master\":[";
    for (int s = 0; s < kMasterSlots; ++s) {
        std::snprintf(text, sizeof text, "MASTER %d", s + 1);
        out += (s ? "," : "") + slotText(text, -1, static_cast<EffectKind>(project.masterEffects[s].kind));
    }
    std::snprintf(text, sizeof text, "],\"sends\":[{\"name\":\"ECHO\"},{\"name\":\"SPACE\"}],\"mix\":{\"dbmin\":%d,\"dbmax\":%d,\"pan\":%d}}", kMixLeastDb, kMixMostDb, kPanMost);
    return out + text;
}

// The track's MODs for the page: every TARGET it may be aimed at (NOTHING, then what fits, in the MOD
// page's order: macros, the slots' settings, sends, the mixer), the RATEs, SHAPEs and PHASEs.
std::string Session::designModsSpec(int track) const {
    const TrackData& t = project.tracks[track];
    std::string out = ",\"mods\":{\"targets\":[{\"id\":-1,\"name\":\"NOTHING\"}";
    char text[64];
    for (int line = 0; line < ModScreen::kPickLines; ++line) {
        int targets[8], count = 0;
        ModScreen::pickLineTargets(t, line, targets, &count);
        for (int i = 0; i < count; ++i) {
            ModScreen::targetText(t, targets[i], text, sizeof text);
            out += ",{\"id\":" + std::to_string(targets[i]) + ",\"name\":" + jsonText(text) + "}";
        }
    }
    out += "],\"rates\":[";
    for (int r = 0; r < kModRates; ++r) out += (r ? "," : "") + jsonText(modRateInfo(r).name);
    out += "],\"shapes\":[";
    for (int s = 0; s < kModShapes; ++s) out += (s ? "," : "") + jsonText(modShapeName(s));
    // A PHASE in quarters, as the MOD page reads it (ModKnob::moreText).
    return out + "],\"phases\":[\"0\",\"1/4\",\"1/2\",\"3/4\"]}";
}

std::string Session::designFxState(int track) const {
    char text[128];
    const TrackData& t = project.tracks[track];
    // Which effect each slot holds: the track's three, the buses', the master's two. Then every
    // setting's value in the fxset ids' order (designer.hpp), then what each reads.
    std::snprintf(text, sizeof text, ",\"fxkinds\":[%d,%d,%d,%d,%d,%d,%d],\"fx\":[", t.effects[0].kind, t.effects[1].kind, t.effects[2].kind,
                  project.busEffects[0].kind, project.busEffects[1].kind, project.masterEffects[0].kind, project.masterEffects[1].kind);
    std::string out = text;
    float values[kFxIds - 2];
    for (int s = 0; s < kTrackSlots; ++s)
        for (int p = 0; p < kEffectParams; ++p) values[s * kEffectParams + p] = t.effects[s].param[p];
    values[kFxSendIds] = t.send[0];
    values[kFxSendIds + 1] = t.send[1];
    for (int b = 0; b < kBuses; ++b)
        for (int p = 0; p < kEffectParams; ++p) values[kFxBusIds + b * kEffectParams + p] = project.busEffects[b].param[p];
    for (int s = 0; s < kMasterSlots; ++s)
        for (int p = 0; p < kEffectParams; ++p) values[kFxMasterIds + s * kEffectParams + p] = project.masterEffects[s].param[p];
    for (int i = 0; i < kFxIds - 2; ++i) std::snprintf(text, sizeof text, i ? ",%.4g" : "%.4g", values[i]), out += text;
    out += "],\"fxreadings\":[";
    bool first = true;
    const auto reading = [&](EffectKind kind, int p, const float* all, int i) {
        // Past what the effect has (NONE has nothing): nothing to read.
        if (p < effectInfo(kind).params) effectParamText(kind, p, values[i], text, sizeof text, all);
        else text[0] = 0;
        out += (first ? "" : ",") + jsonText(text);
        first = false;
    };
    for (int s = 0; s < kTrackSlots; ++s) {
        const EffectData& d = t.effects[s];
        for (int p = 0; p < kEffectParams; ++p) reading(static_cast<EffectKind>(d.kind), p, d.param, s * kEffectParams + p);
    }
    for (int b = 0; b < kBuses; ++b) {
        std::snprintf(text, sizeof text, "%s\"%d%%\"", first ? "" : ",", static_cast<int>(t.send[b] * 100.f + 0.5f));
        out += text;
        first = false;
    }
    for (int b = 0; b < kBuses; ++b) {
        const EffectData& d = project.busEffects[b];
        for (int p = 0; p < kEffectParams; ++p) reading(static_cast<EffectKind>(d.kind), p, d.param, kFxBusIds + b * kEffectParams + p);
    }
    for (int s = 0; s < kMasterSlots; ++s) {
        const EffectData& d = project.masterEffects[s];
        for (int p = 0; p < kEffectParams; ++p) reading(static_cast<EffectKind>(d.kind), p, d.param, kFxMasterIds + s * kEffectParams + p);
    }
    std::snprintf(text, sizeof text, "],\"mix\":[%d,%d]", t.mixDb, t.mixPan);
    return out + text;
}

std::string Session::designModsState(int track) const {
    char text[256]; // (every number at its longest: more than 128)
    const TrackData& t = project.tracks[track];
    std::string out = ",\"mods\":[";
    for (int m = 0; m < kModsPerTrack; ++m) {
        const ModSettings& mod = t.mods[m];
        std::snprintf(text, sizeof text, "%s{\"kind\":%d,\"target\":%d,\"depth\":%.4g,\"rate\":%d,\"shape\":%d,\"phase\":%d,\"length\":%d,\"slew\":%.4g,\"chance\":%.4g,\"speed\":%.4g,\"steps\":[",
                      m ? "," : "", mod.kind, mod.target, mod.depth, mod.rate, mod.shape, mod.phase, mod.length, mod.slew, mod.chance, mod.speed);
        out += text;
        for (int s = 0; s < kModSteps; ++s) std::snprintf(text, sizeof text, s ? ",%.4g" : "%.4g", mod.steps[s]), out += text;
        out += "]}";
    }
    return out + "]";
}

std::string Session::designState() const {
    const TrackData& t = project.tracks[project.track];
    const SynthSpec* spec = specFor(t.kind);
    char text[96];
    if (t.kind == TrackKind::Sampler) {
        std::snprintf(text, sizeof text, "{\"track\":%d,\"name\":\"SAMPLER\",\"spec\":%u,\"called\":", project.track + 1, designSpecSerial_);
        std::string out = text + jsonText(t.name) + ",\"presets\":0,\"preset\":-1,\"macros\":[],\"values\":[";
        for (int id = 0; id < sampler::kSettings; ++id) std::snprintf(text, sizeof text, id ? ",%d" : "%d", t.sampler[id]), out += text;
        return out + "]" + designFxState(project.track) + designModsState(project.track) + "}";
    }
    if (t.kind == TrackKind::Drums) {
        // Every lane's settings by their ids (kKitStride a lane), and what the handheld reads for each
        // EDIT row (START and END are times into the lane's own file), and how long it plays.
        std::snprintf(text, sizeof text, "{\"track\":%d,\"name\":\"KIT\",\"spec\":%u,\"called\":", project.track + 1, designSpecSerial_);
        std::string out = text + jsonText(t.name) + ",\"presets\":0,\"preset\":-1,\"macros\":[],\"values\":[";
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            int v[kKitStride] = {};
            for (int row = 0; row < kEditRows; ++row) v[row] = laneEditValue(t.laneEdit[lane], row);
            v[kKitFit] = t.laneFit[lane];
            v[kKitChoke] = t.laneChoke[lane];
            v[kKitVolume] = static_cast<int>(std::lround(t.laneLevel[lane] * 100.f));
            v[kKitHumanTime] = t.laneFeel[lane].time;
            v[kKitHumanLevel] = t.laneFeel[lane].level;
            v[kKitSwing] = t.laneSwing[lane];
            for (int f = 0; f < kKitStride; ++f) std::snprintf(text, sizeof text, lane || f ? ",%d" : "%d", v[f]), out += text;
        }
        for (int lane = 0; lane < kDrumLanes; ++lane) // a synth lane's knobs, from kKitKnobs (0 where it is none)
            for (int p = 0; p < drumsynth::kPlaces; ++p) std::snprintf(text, sizeof text, ",%d", t.laneSynth[lane].dial[p]), out += text;
        out += "],\"lanes\":[";
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            const LaneSound& sound = sounds_[project.track][lane];
            out += lane ? ",{\"readings\":[" : "{\"readings\":[";
            for (int row = 0; row < kEditRows; ++row) {
                laneEditText(t.laneEdit[lane], row, sound.sourceSeconds, text, sizeof text);
                out += (row ? "," : "") + jsonText(text);
            }
            std::snprintf(text, sizeof text, "],\"plays\":%.2f,\"making\":%s,\"fitting\":%s", sound.seconds, sound.shaping ? "true" : "false", sound.waiting && !sound.shaping ? "true" : "false");
            out += text;
            // A synth lane: what each knob reads, the named voice its dials are (-1: turned by hand), and
            // its picture, length and START / END's least gap (in the spec for any other lane).
            const drumsynth::Voice& voice = t.laneSynth[lane];
            if (voice.on()) {
                out += ",\"synth\":{\"readings\":[";
                for (int p = 0; p < drumsynth::kPlaces; ++p) {
                    drumsynth::knobText(voice.model, p, voice.dial[p], text, sizeof text);
                    out += (p ? "," : "") + jsonText(text);
                }
                std::snprintf(text, sizeof text, "],\"voice\":%d,\"seconds\":%.3f,\"least\":%d,\"peaks\":[", voicePlaceOf(voice), sound.sourceSeconds, leastSpan(sound.sourceSeconds));
                out += text + peaksText(sound.sourcePeaks) + "]}";
            }
            out += "}";
        }
        return out + "]" + designFxState(project.track) + designModsState(project.track) + "}";
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
    return out + "]" + designFxState(project.track) + designModsState(project.track) + "}";
}

namespace {
constexpr Uint32 kDesignListEveryMs = 5000; // a sampler's page sees a recording put on the card by other ways this soon
}

void Session::serveDesigner() {
    const auto letGo = [&] {
        if (hearingHeld() && heard_.owner == &designer_) letGoHeard(); // a kit's lane the page was holding
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
    const bool sampler = kind == TrackKind::Sampler, kit = kind == TrackKind::Drums;
    for (const DesignRequest& r : designer_.take()) {
        if (!specFor(kind) && !sampler && !kit) continue; // no synth, sampler or kit on this track: nothing to do it to
        if (r.kind == DesignRequest::FxSet || r.kind == DesignRequest::FxType) { // every instrument's (a kit's too)
            serveFx(r);
            continue;
        }
        if (r.kind == DesignRequest::ModSet) {
            serveMod(r);
            continue;
        }
        if (kit && r.kind != DesignRequest::Name) { // a kit's own (a synth's or a sampler's asks do nothing to it)
            serveKit(r);
            continue;
        }
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
        case DesignRequest::LaneSample: case DesignRequest::Hear: case DesignRequest::Normalize: case DesignRequest::Trim:
        case DesignRequest::LaneSynth: case DesignRequest::LaneVoice: break; // (a kit's: above)
        case DesignRequest::FxSet: case DesignRequest::FxType: case DesignRequest::ModSet: break; // (every instrument's: above)
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

    // The recordings a sampler's or a kit's page offers: read from the card here, never by the server's thread.
    const bool looking = now_ - designSeenAt_ <= 3000 && designLooks_ != 0;
    bool listed = false;
    if ((sampler || kit || holdsRecording(kind)) && looking && (!designListed_ || now_ - designListAt_ >= kDesignListEveryMs || transfer_.received() != designReceived_)) {
        designRecordings_ = sampleNames();
        designListAt_ = now_;
        designReceived_ = transfer_.received();
        designListed_ = listed = true;
    }
    // A kit's lanes are read for its pictures, as the KIT page's EDIT view reads one (a built-in drum's
    // sound is the kit's own: nothing is copied).
    if (kit && looking)
        for (int lane = 0; lane < kDrumLanes; ++lane) readyLane(project.track, lane);
    const SynthSpec* spec = specFor(kind);
    const int presets = spec ? spec->presetCount() : 0;
    // A sampler's recording read again, or any of a kit's lanes given another sound or edited: its
    // pictures may have changed. (Versions only go up, so their sum moves whenever one does.)
    unsigned sound = sounds_[project.track][0].version;
    if (kit)
        for (int lane = 1; lane < kDrumLanes; ++lane) sound += sounds_[project.track][lane].version;
    // The slots', buses' and master's effects, hashed in order (a swap is a new spec too).
    unsigned fx = 0;
    for (int s = 0; s < kTrackSlots; ++s) fx = fx * 31u + static_cast<unsigned>(project.tracks[project.track].effects[s].kind);
    for (int b = 0; b < kBuses; ++b) fx = fx * 31u + static_cast<unsigned>(project.busEffects[b].kind);
    for (int s = 0; s < kMasterSlots; ++s) fx = fx * 31u + static_cast<unsigned>(project.masterEffects[s].kind);
    const int meter = project.meterOf(project.scene); // (a kit's FIT TO reads bars of it: batch S1)
    if (kind != designSpecKind_ || presets != designSpecPresets_ || project.track != designSpecTrack_ || sound != designSpecSound_ || fx != designSpecFx_ || meter != designSpecMeter_ || listed) {
        designSpecMeter_ = meter;
        designSpecKind_ = kind;
        designSpecPresets_ = presets;
        designSpecTrack_ = project.track;
        designSpecSound_ = sound;
        designSpecFx_ = fx;
        std::string text = designSpec(project.track);
        if (text != designSpecText_) designSpecText_.swap(text), ++designSpecSerial_;
    }
    designer_.show(designSpecText_, designState());
}

// A KIT's requests, on the lanes of the track being worked on, through the calls the KIT page and the
// drum grid make (so undo, the song file, the engine and the handheld's screens follow as they do).
void Session::serveKit(const DesignRequest& r) {
    const int track = project.track;
    const TrackData& t = project.tracks[track];
    switch (r.kind) {
    case DesignRequest::Set: {
        if (r.a >= kKitKnobs && r.a < kKitKnobs + kDrumLanes * drumsynth::kPlaces) { // a synth lane's knob
            const int lane = (r.a - kKitKnobs) / drumsynth::kPlaces, place = (r.a - kKitKnobs) % drumsynth::kPlaces;
            if (!t.laneSynth[lane].on()) return; // (a place its model does not use is put back to 0 there: voiceHeld)
            drumsynth::Voice v = t.laneSynth[lane];
            v.dial[place] = static_cast<std::uint8_t>(r.b < 0 ? 0 : r.b > drumsynth::kDialMost ? drumsynth::kDialMost : r.b);
            setLaneSynth(track, lane, v);
            return;
        }
        if (r.a < 0 || r.a >= kDrumLanes * kKitStride) return;
        const int lane = r.a / kKitStride, field = r.a % kKitStride;
        if (field < kEditRows) setLaneEdit(track, lane, laneEditWith(t.laneEdit[lane], field, r.b)); // (held to its limits there)
        else if (field == kKitFit) setLaneFit(track, lane, r.b);
        else if (field == kKitChoke) setLaneChoke(track, lane, r.b);
        else if (field == kKitVolume) setLaneLevel(track, lane, static_cast<float>(r.b) / 100.f); // (held to 0..1 there)
        else if (field == kKitHumanTime) setLaneFeel(track, lane, r.b, t.laneFeel[lane].level);
        else if (field == kKitHumanLevel) setLaneFeel(track, lane, t.laneFeel[lane].time, r.b);
        else if (field == kKitSwing) setLaneSwing(track, lane, r.b);
        return;
    }
    case DesignRequest::LaneSample: // only a recording the page was offered, or none (the built-in drum)
        if (r.name.empty() || std::find(designRecordings_.begin(), designRecordings_.end(), r.name) != designRecordings_.end()) setLaneSample(track, r.a, r.name);
        return;
    case DesignRequest::Hear: // as X held on the KIT page: heard until let go (a tap: as a tap is)
        if (r.b) hearLaneHeld(track, r.a, kPlacedLevel, &designer_);
        else if (hearingHeld() && heard_.owner == &designer_ && heard_.lane == r.a) letGoHeard();
        return;
    case DesignRequest::LaneSynth: setLaneSynth(track, r.a, drumsynth::usualVoice(drumsynth::modelFromWord(r.name.c_str()))); return;
    case DesignRequest::LaneVoice: // a named voice of the lane's model (none on a lane that is no synth)
        if (t.laneSynth[r.a].on() && r.b < voiceCount(t.laneSynth[r.a].model)) setLaneSynth(track, r.a, voiceAt(t.laneSynth[r.a].model, r.b));
        return;
    case DesignRequest::Normalize: normalizeLane(track, r.a); return;
    case DesignRequest::Trim: trimLaneSilence(track, r.a); return;
    default: return; // a synth's or a sampler's: nothing on a kit
    }
}

// EFFECTS from the page, through the calls the FX page makes (so undo, the song file, the engine and
// automation follow as they do): a setting by its id (designer.hpp), or which effect a slot holds.
void Session::serveFx(const DesignRequest& r) {
    const int track = project.track;
    if (r.kind == DesignRequest::FxType) { // (a bus takes NONE or its own: setEffect says so)
        const EffectKind kind = static_cast<EffectKind>(r.b);
        if (r.a < kTrackSlots) setEffect(track, r.a, kind);
        else if (r.a < kTrackSlots + kBuses) setEffect(r.a == kTrackSlots ? kEchoBus : kSpaceBus, 0, kind);
        else setEffect(kMasterTrack, r.a - kTrackSlots - kBuses, kind);
        return;
    }
    const int id = r.a;
    if (id < kFxSendIds) setEffectParam(track, id / kEffectParams, id % kEffectParams, r.value);
    else if (id < kFxBusIds) setSend(track, id - kFxSendIds, r.value);
    else if (id < kFxMasterIds) {
        const int b = id - kFxBusIds;
        setEffectParam(b / kEffectParams == 0 ? kEchoBus : kSpaceBus, 0, b % kEffectParams, r.value);
    } else if (id < kFxMixIds) {
        const int s = id - kFxMasterIds;
        setEffectParam(kMasterTrack, s / kEffectParams, s % kEffectParams, r.value);
    } else { // the mixer: whole dB and pan, the other kept (held first: past long's end it is no int)
        const TrackData& t = project.tracks[track];
        const float db = !(r.value >= kMixLeastDb) ? kMixLeastDb : (r.value > kMixMostDb ? kMixMostDb : r.value);
        const float pan = !(r.value >= -kPanMost) ? -kPanMost : (r.value > kPanMost ? kPanMost : r.value);
        setMix(track, id == kFxMixDb ? static_cast<int>(std::lround(db)) : t.mixDb, id == kFxMixPan ? static_cast<int>(std::lround(pan)) : t.mixPan);
    }
}

// A MOD from the page: one field, or one step of a STEPPER's row, through setMod (which holds every
// field and sends what changed, as the MOD page's turns do). A TARGET that does not fit is nothing:
// setMod would take the whole MOD for OFF, and the page may hold yesterday's slots.
void Session::serveMod(const DesignRequest& r) {
    const int track = project.track;
    const TrackData& t = project.tracks[track];
    if (t.kind == TrackKind::None) return;
    if (r.b == kModFieldTarget) {
        const int asked = modWhole(r.value);
        if (asked != kModTargetNone && (!modTargetKnown(asked) || !modTargetFits(t, asked))) return;
    }
    ModSettings m = t.mods[r.a];
    if (r.b < kModFields) modSet(m, r.b, r.value);
    else m.steps[r.b - kModFields] = r.value != r.value ? 0.f : (r.value < -1.f ? -1.f : (r.value > 1.f ? 1.f : r.value));
    // Aiming an OFF MOD at something wakes it as an LFO (ModKnob::fresh: the MOD page's new one, and
    // the only way field-by-field asks can make one: setMod blanks a kind with no target).
    if (r.b == kModFieldTarget && m.target != kModTargetNone && m.kind == kModOff) m.kind = kModLfo;
    setMod(track, r.a, m);
}

void Session::setMidiRecording(bool on) {
    midiRecord_ = on;
    keepSettings();
}

void Session::setClick(int mode) {
    mode = mode < kClickOff ? kClickOff : (mode >= kClickModes ? kClickModes - 1 : mode); // a choice with ends
    if (mode == click_) return;
    click_ = mode;
    keepSettings();
}

void Session::setCountIn(bool on) {
    if (on == countIn_) return;
    countIn_ = on;
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
        const int scene = project.playScene, step = project.playStep;
        // Let go: the recorded note is as long as it was held, in whole steps, to the end of its bar at most.
        Recording& rec = recordingNote_[event.note];
        if (event.kind == MidiEvent::NoteOff && rec.on) {
            rec.on = false;
            TrackData& track = project.tracks[rec.track];
            NoteEvent& note = track.notes[rec.scene][rec.bar].notes[rec.slot];
            if (note.velocity > 0.f && note.pitch == event.note) {
                const int bars = track.bars[rec.scene];
                const int all = bars * kSteps, now = scene == rec.scene && step >= 0 ? (project.playBar % bars) * kSteps + step : rec.startedAt;
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
        const int bar = project.playBar % track.bars[scene];
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
