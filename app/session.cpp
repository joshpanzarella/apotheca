#include "session.hpp"
#include "ui.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <memory>

#include "samples.hpp"
#include "song_file.hpp"
#include "stretch.hpp"
#include "synth_spec.hpp"

namespace apotheca {

namespace {
constexpr Uint32 kAuditionMs = 350;
constexpr Uint32 kSaveEveryMs = 2000;

// New songs are named with two words, so nobody has to type on a D-pad.
constexpr Uint32 kTempoSettleMs = 250;
constexpr int kPeaks = 240;      // points in a recording's outline
constexpr int kMaxFitSteps = 64; // four bars

const char* const kFirstWords[] = {"brass", "salt", "amber", "ash", "copper", "glass", "iron", "lunar", "pale", "quiet", "red", "silver",
                                   "slow", "smoke", "tin", "velvet", "wax", "black", "green", "hollow", "mercury", "night", "old", "vitriol"};
const char* const kSecondWords[] = {"moth", "lantern", "furnace", "garden", "mirror", "river", "vessel", "key", "crow", "flask", "orchard", "tower",
                                    "engine", "lion", "salt", "window", "road", "bell", "serpent", "star", "well", "ember", "hour", "stone"};

bool fileExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}
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

bool Session::addTrack(TrackKind kind) {
    const int track = project.trackCount();
    if (track >= kNumTracks || kind == TrackKind::None) return false;
    if (kind == TrackKind::Drums) {
        if (!engine.addDrumTrack(track)) return false;
    } else {
        const SynthKind synth = kind == TrackKind::Twin ? SynthKind::Twin : kind == TrackKind::Sampler ? SynthKind::Sampler : SynthKind::Phazed;
        if (!engine.addSynthTrack(track, synth)) return false;
        project.tracks[track].preset = 0;
        if (const SynthSpec* spec = specFor(kind)) spec->loadPreset(project.tracks[track], 0);
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
    engine.collectGarbage();
    project.playScene = engine.playScene();
    project.playStep = engine.playStep();
    project.playRepeat = engine.playRepeat();
    project.playBar = engine.playBar();
    project.silenced = engine.silenced();

    hearMidi();
    serveDesigner();

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

void Session::setCellBars(int track, int scene, int bars) {
    if (track < 0 || track >= kNumTracks || scene < 0 || scene >= kMaxScenes) return;
    bars = bars < 2 ? 1 : (bars < 4 ? 2 : (bars < 8 ? 4 : 8));
    project.tracks[track].bars[scene] = bars;
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
void Session::sendDrumPattern(int track, int scene) {
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
            }
        }
    }
}

void Session::sendNotePattern(int track, int scene) {
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

} // namespace apotheca

// ---- songs on disk -----------------------------------------------------------------

namespace apotheca {

std::string Session::pathOf(const std::string& name) const { return folder_ + "/" + name + ".song"; }

std::string Session::freshName() {
    const int firsts = sizeof kFirstWords / sizeof kFirstWords[0], seconds = sizeof kSecondWords / sizeof kSecondWords[0];
    for (int attempt = 0; attempt < 2000; ++attempt) {
        nameSeed_ = nameSeed_ * 1664525u + 1013904223u;
        std::string name = std::string(kFirstWords[(nameSeed_ >> 8) % firsts]) + " " + kSecondWords[(nameSeed_ >> 16) % seconds];
        if (attempt >= 1000) name += " " + std::to_string(attempt - 999); // every pair taken: number them
        if (folder_.empty() || !fileExists(pathOf(name))) return name;
    }
    return "untitled";
}

void Session::useSongFolder(const std::string& folder, std::uint32_t seed) {
    folder_ = folder;
    nameSeed_ = seed | 1u;
    mkdir(folder_.c_str(), 0755); // fine if it is already there
    std::string last;
    if (readTextFile(folder_ + "/last.txt", last)) {
        while (!last.empty() && (last.back() == '\n' || last.back() == '\r')) last.pop_back();
        std::string text;
        if (!last.empty() && readTextFile(pathOf(last), text) && parseSong(text, project)) {
            songName_ = last;
            savedText_ = songText(project);
            applyProject();
            return;
        }
    }
    songName_ = freshName();
    savedText_.clear();
}

std::vector<std::string> Session::songNames() const {
    std::vector<std::pair<long long, std::string>> found;
    if (DIR* dir = folder_.empty() ? nullptr : opendir(folder_.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() <= 5 || file.compare(file.size() - 5, 5, ".song") != 0) continue;
            struct stat info;
            if (stat((folder_ + "/" + file).c_str(), &info) != 0) continue;
            found.push_back({static_cast<long long>(info.st_mtime), file.substr(0, file.size() - 5)});
        }
        closedir(dir);
    }
    // Newest first; the song that is open leads when times tie.
    std::sort(found.begin(), found.end(), [this](const std::pair<long long, std::string>& a, const std::pair<long long, std::string>& b) {
        if (a.first != b.first) return a.first > b.first;
        if ((a.second == songName_) != (b.second == songName_)) return a.second == songName_;
        return a.second < b.second;
    });
    std::vector<std::string> names;
    for (const auto& song : found) names.push_back(song.second);
    return names;
}

void Session::saveNow() {
    if (folder_.empty()) return;
    const std::string text = songText(project);
    if (text == savedText_) return;
    // A song nobody has put anything in is not worth a file.
    static const std::string empty = [] { std::unique_ptr<Project> fresh(new Project); return songText(*fresh); }();
    if (text == empty && !fileExists(pathOf(songName_))) return;
    if (!writeTextFile(pathOf(songName_), text)) return; // try again next time
    savedText_ = text;
    writeTextFile(folder_ + "/last.txt", songName_ + "\n");
}

void Session::newSong() {
    saveNow();
    std::unique_ptr<Project> fresh(new Project);
    project = *fresh;
    songName_ = freshName();
    savedText_.clear();
    applyProject();
}

bool Session::openSong(const std::string& name) {
    saveNow();
    std::string text;
    if (folder_.empty() || !readTextFile(pathOf(name), text) || !parseSong(text, project)) return false;
    songName_ = name;
    savedText_ = songText(project);
    writeTextFile(folder_ + "/last.txt", songName_ + "\n");
    applyProject();
    return true;
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
    if (track < 0 || track >= kNumTracks || macro < 0 || macro >= phazed::kMacros || value != value) return;
    TrackData& t = project.tracks[track];
    if (!specFor(t.kind)) return;
    value = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
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
        return;
    }
    // One of the player's own: the engine is told every setting, and the macros it was saved with.
    const std::vector<float>& macros = userPresets(t.kind)[own].macros;
    for (int m = 0; m < phazed::kMacros && m < static_cast<int>(macros.size()); ++m) t.macro[m] = macros[m] < 0.f ? 0.f : (macros[m] > 1.f ? 1.f : macros[m]);
    const int* values = spec->values(t);
    c.type = Command::SetSetting;
    for (int id = 0; id < settingCount(t.kind); ++id) {
        c.a = id;
        c.b = values[id];
        engine.send(c);
    }
    c.type = Command::SetMacro;
    for (int m = 0; m < phazed::kMacros; ++m) {
        c.a = m;
        c.value = t.macro[m];
        engine.send(c);
    }
}

namespace {
const char* presetEnding(TrackKind kind) { return kind == TrackKind::Twin ? ".twin" : ".phazed"; }

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
    for (TrackKind kind : {TrackKind::Phazed, TrackKind::Twin}) {
        const std::string ending = presetEnding(kind);
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
    if (!spec || name.empty()) return false;
    std::string text = "apotheca-preset 1\nsynth ";
    text += t.kind == TrackKind::Twin ? "twin" : "phazed";
    text += "\nmacros";
    char number[32];
    for (float m : t.macro) std::snprintf(number, sizeof number, " %.6g", m), text += number;
    text += "\nsettings";
    const int* values = spec->values(t);
    for (int id = 0; id < settingCount(t.kind); ++id) std::snprintf(number, sizeof number, " %d", values[id]), text += number;
    text += "\n";
    if (!writeTextFile(presetFolder_ + "/" + name + presetEnding(t.kind), text)) return false;
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
    std::string out = "{\"name\":" + jsonText(spec->name) + ",\"macros\":[";
    for (int m = 0; m < phazed::kMacros; ++m) out += (m ? "," : "") + jsonText(spec->macroNames[m]);
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
    std::snprintf(text, sizeof text, ",\"presets\":%d,\"preset\":%d,\"values\":[", spec->presetCount(), t.preset);
    out += text;
    const int* values = spec->values(const_cast<TrackData&>(t));
    for (int id = 0; id < settingCount(t.kind); ++id) std::snprintf(text, sizeof text, id ? ",%d" : "%d", values[id]), out += text;
    out += "],\"macros\":[";
    for (int m = 0; m < phazed::kMacros; ++m) std::snprintf(text, sizeof text, m ? ",%.4g" : "%.4g", t.macro[m]), out += text;
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
        const int scene = engine.playScene(), step = engine.playStep();
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
            sendDrumPattern(project.track, scene);
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

float Session::ui_stickSpeed() { return ui::gStickSpeed; }

void Session::keepSettings() const {
    if (programFolder_.empty()) return;
    writeTextFile(programFolder_ + "/settings.txt", "stick " + std::to_string(static_cast<int>(std::round(ui::gStickSpeed * 100.f))) + "\noctaves " +
                                                        std::to_string(octaves_) + "\nmidirec " + (midiRecord_ ? "1" : "0") + "\n");
}

void Session::setStickSpeed(float speed) {
    if (speed != speed) return;
    speed = std::round(speed * 20.f) / 20.f; // in twentieths
    ui::gStickSpeed = speed < kStickSlowest ? kStickSlowest : (speed > kStickFastest ? kStickFastest : speed);
    keepSettings();
}

void Session::setOctavesShown(int octaves) {
    octaves_ = octaves < 1 ? 1 : (octaves > 3 ? 3 : octaves);
    keepSettings();
}

void Session::loadSettings() {
    std::string text;
    if (programFolder_.empty() || !readTextFile(programFolder_ + "/settings.txt", text)) return;
    std::istringstream lines(text);
    std::string name;
    int value = 0;
    const std::string folder = programFolder_;
    programFolder_.clear(); // reading is not a change worth writing down
    while (lines >> name >> value) {
        if (name == "stick" && value >= 25 && value <= 150) setStickSpeed(static_cast<float>(value) / 100.f);
        if (name == "octaves" && value >= 1 && value <= 3) setOctavesShown(value);
        if (name == "midirec" && (value == 0 || value == 1)) setMidiRecording(value == 1);
    }
    programFolder_ = folder;
}

void Session::loadVolume(float ifNone) {
    std::string text;
    float volume = ifNone;
    if (!programFolder_.empty() && readTextFile(programFolder_ + "/volume.txt", text) && !text.empty()) {
        const int percent = std::atoi(text.c_str());
        if (percent >= 0 && percent <= 100) volume = static_cast<float>(percent) / 100.f;
    }
    const std::string folder = programFolder_;
    programFolder_.clear(); // setting it now is not a change worth writing down
    setVolume(volume);
    programFolder_ = folder;
}

void Session::setTempo(float bpm) {
    bpm = std::round(bpm != bpm ? project.tempo : bpm);
    bpm = bpm < kMinTempo ? kMinTempo : (bpm > kMaxTempo ? kMaxTempo : bpm);
    if (bpm == project.tempo) return;
    project.tempo = bpm;
    Command c;
    c.type = Command::SetTempo;
    c.value = bpm;
    engine.send(c);
}

std::string Session::renameSong() {
    if (folder_.empty()) return songName_;
    saveNow();
    const std::vector<int> have = revs(); // under the old name
    const std::string old = songName_, fresh = freshName();
    if (fileExists(pathOf(old)) && std::rename(pathOf(old).c_str(), pathOf(fresh).c_str()) != 0) return songName_;
    for (int rev : have) {
        const std::string from = revPath(rev);
        songName_ = fresh;
        std::rename(from.c_str(), revPath(rev).c_str());
        songName_ = old;
    }
    songName_ = fresh;
    writeTextFile(folder_ + "/last.txt", songName_ + "\n");
    return songName_;
}

bool Session::deleteSong(const std::string& name) {
    if (folder_.empty() || name.empty() || name.find('/') != std::string::npos || !fileExists(pathOf(name))) return false;
    const std::string bin = folder_ + "/deleted";
    mkdir(bin.c_str(), 0755); // fine if it is already there
    // The song and everything of its: "<name>.song" and "<name>.<n>.rev".
    std::vector<std::string> files;
    if (DIR* dir = opendir(folder_.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            const bool song = file == name + ".song";
            const bool rev = file.size() > name.size() + 5 && file.compare(0, name.size() + 1, name + ".") == 0 &&
                             file.compare(file.size() - 4, 4, ".rev") == 0 &&
                             file.find_first_not_of("0123456789", name.size() + 1) == file.size() - 4;
            if (song || rev) files.push_back(file);
        }
        closedir(dir);
    }
    bool moved = true;
    for (const std::string& file : files) moved = std::rename((folder_ + "/" + file).c_str(), (bin + "/" + file).c_str()) == 0 && moved;
    if (name == songName_) {
        // The open one: a new song takes its place. Nothing is saved over what was moved.
        std::unique_ptr<Project> fresh(new Project);
        project = *fresh;
        songName_ = freshName();
        savedText_.clear();
        applyProject();
    }
    return moved;
}

// ---- revs ---------------------------------------------------------------------------
// Kept as "<song name>.<number>.rev" in the song folder: the same text as a
// song file, under a name the song list does not pick up.

std::string Session::revPath(int rev) const { return folder_ + "/" + songName_ + "." + std::to_string(rev) + ".rev"; }

std::vector<int> Session::revs() const {
    std::vector<int> found;
    const std::string front = songName_ + ".";
    if (DIR* dir = folder_.empty() ? nullptr : opendir(folder_.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() <= front.size() + 4 || file.compare(0, front.size(), front) != 0) continue;
            if (file.compare(file.size() - 4, 4, ".rev") != 0) continue;
            const std::string number = file.substr(front.size(), file.size() - front.size() - 4);
            if (number.empty() || number.size() > 6 || number.find_first_not_of("0123456789") != std::string::npos) continue;
            const int rev = std::atoi(number.c_str());
            if (rev >= 1) found.push_back(rev);
        }
        closedir(dir);
    }
    std::sort(found.begin(), found.end(), [](int a, int b) { return a > b; });
    return found;
}

int Session::saveRev() {
    if (folder_.empty()) return 0;
    saveNow();
    if (!fileExists(pathOf(songName_))) return 0; // an empty song that was never written: nothing to keep
    const std::vector<int> have = revs();
    const int rev = have.empty() ? 1 : have.front() + 1;
    return writeTextFile(revPath(rev), songText(project)) ? rev : 0;
}

std::string Session::revWhen(int rev) const {
    struct stat info;
    if (folder_.empty() || stat(revPath(rev).c_str(), &info) != 0) return "";
    const std::time_t at = info.st_mtime;
    char text[32] = "";
    if (const std::tm* local = std::localtime(&at)) std::strftime(text, sizeof text, "%d %b %H:%M", local);
    for (char& c : text) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return text;
}

bool Session::openRev(int rev) {
    std::string text;
    std::unique_ptr<Project> back(new Project);
    if (folder_.empty() || !readTextFile(revPath(rev), text) || !parseSong(text, *back)) return false;
    // Keep where the song is now, unless a rev already holds exactly that.
    const std::string now = songText(project);
    bool kept = false; // the rev being opened is one of these, so "already there" is covered
    for (int other : revs()) {
        std::string theirs;
        if (kept) break;
        kept = readTextFile(revPath(other), theirs) && theirs == now;
    }
    if (!kept && saveRev() == 0) return false; // could not keep it: do not throw it away
    project = *back;
    applyProject();
    saveNow();
    return true;
}

// ---- effects ------------------------------------------------------------------------

void Session::setEffect(int track, int slot, EffectKind kind) {
    if ((track != kMasterTrack && (track < 0 || track >= kNumTracks)) || slot < 0 || slot >= kEffectSlots) return;
    if (static_cast<int>(kind) < 0 || static_cast<int>(kind) >= kEffectKinds) kind = EffectKind::None;
    if (!engine.setEffect(track, slot, kind)) return;
    EffectData& data = effectData(track, slot);
    data.kind = static_cast<int>(kind);
    for (int i = 0; i < kEffectParams; ++i) data.param[i] = effectInfo(kind).defaults[i];
}

void Session::setEffectParam(int track, int slot, int index, float value) {
    if ((track != kMasterTrack && (track < 0 || track >= kNumTracks)) || slot < 0 || slot >= kEffectSlots) return;
    EffectData& data = effectData(track, slot);
    if (index < 0 || index >= effectInfo(static_cast<EffectKind>(data.kind)).params) return;
    value = value != value ? 0.f : (value < 0.f ? 0.f : (value > 1.f ? 1.f : value));
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

// ---- recordings on drum lanes -------------------------------------------------------

Session::~Session() {
    if (input_ && rec_ != Rec::Idle && rec_ != Rec::NoInput) input_->stop();
    if (job_ && job_->thread.joinable()) job_->thread.join();
}

std::vector<std::string> Session::sampleNames() const { return wavNames(sampleFolder_); }

void Session::install(int track, int lane, const std::vector<float>& samples) {
    std::unique_ptr<SampleData> data(new SampleData);
    data->data = samples;
    engine.setLaneSample(track, lane, std::move(data));
}

// Read the lane's file and put it in the engine as recorded. If the lane is
// fitted, update() will follow with the stretched version.
void Session::loadLane(int track, int lane) {
    LaneSound& sound = sounds_[track][lane];
    const unsigned version = sound.version + 1;
    sound = LaneSound();
    sound.version = version;
    const char* name = project.tracks[track].laneSample[lane];
    if (!name[0]) {
        engine.setLaneSample(track, lane, nullptr);
        return;
    }
    std::shared_ptr<std::vector<float>> recording(new std::vector<float>);
    if (sampleFolder_.empty() || !readWav(sampleFolder_ + "/" + name + ".wav", *recording) || recording->empty()) {
        sound.missing = true; // the drum plays in its place, and the song keeps the name
        engine.setLaneSample(track, lane, nullptr);
        return;
    }
    sound.seconds = static_cast<float>(recording->size()) / kSampleRate;
    sound.peaks.assign(kPeaks, 0.f);
    for (std::size_t i = 0; i < recording->size(); ++i) {
        float& peak = sound.peaks[i * kPeaks / recording->size()];
        const float v = std::fabs((*recording)[i]);
        if (v > peak) peak = v;
    }
    sound.recording = recording;
    sound.waiting = project.tracks[track].laneFit[lane] > 0;
    install(track, lane, *recording);
}

void Session::setLaneSample(int track, int lane, const std::string& name) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return;
    std::snprintf(project.tracks[track].laneSample[lane], sizeof project.tracks[track].laneSample[lane], "%s", name.c_str());
    loadLane(track, lane);
}

// ---- files over WiFi --------------------------------------------------------------------

bool Session::toggleTransfer(unsigned seed) {
    if (transfer_.running()) {
        transfer_.stop();
        discardUpdate(); // an update nobody answered goes when WiFi is turned off
        return false;
    }
    if (folder_.empty() || sampleFolder_.empty()) return false; // nowhere for files to go
    const std::string chords = chordFolder_.empty() ? folder_ + "/../chords" : chordFolder_;
    // 8080 if it is free (easy to type), else whatever is.
    transfer_.setDesk(&designer_);
    const bool on = transfer_.start(sampleFolder_, folder_, chords, 8080, seed) || transfer_.start(sampleFolder_, folder_, chords, 0, seed);
    if (on) std::printf("files over wifi: http://%s\n", transfer_.address().c_str()); // in the log too, for whoever is setting it up
    return on;
}

// ---- updating Apotheca over WiFi ---------------------------------------------------------

void Session::useProgramFolder(const std::string& folder) {
    programFolder_ = folder;
    transfer_.setProgramFolder(folder);
    // One that was sent but never answered, last time: it does not linger.
    std::remove((folder + "/apotheca.pending").c_str());
}

bool Session::acceptUpdate() {
    if (!updateWaiting_) return false;
    updateWaiting_ = false;
    return std::rename((programFolder_ + "/apotheca.pending").c_str(), (programFolder_ + "/apotheca.new").c_str()) == 0;
}

void Session::discardUpdate() {
    if (!updateWaiting_) return;
    updateWaiting_ = false;
    std::remove((programFolder_ + "/apotheca.pending").c_str());
}

void Session::startedFine() {
    if (!programFolder_.empty()) std::remove((programFolder_ + "/update-trying").c_str());
}

// ---- recording ------------------------------------------------------------------------

Session::Rec Session::toggleRecording(int track, int lane) {
    if (rec_ == Rec::Waiting || rec_ == Rec::Recording) {
        finishRecording();
        return rec_;
    }
    rec_ = Rec::Idle;
    lastRecorded_.clear();
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes) return rec_;
    const TrackKind kind = project.tracks[track].kind;
    if (kind != TrackKind::Drums && kind != TrackKind::Sampler) return rec_;
    if (sampleFolder_.empty() || !input_ || !input_->start()) return rec_ = Rec::NoInput;
    recTrack_ = track;
    recLane_ = lane;
    recorder_.arm(kSampleRate);
    return rec_ = Rec::Waiting;
}

void Session::finishRecording() {
    if (input_) input_->stop();
    rec_ = Rec::Idle;
    lastRecorded_.clear();
    const std::vector<float> take = recorder_.finish();
    if (take.empty()) return; // no sound came
    mkdir(sampleFolder_.c_str(), 0755);
    std::string name;
    for (int number = 1; number < 10000; ++number) {
        name = "rec " + std::to_string(number);
        if (!fileExists(sampleFolder_ + "/" + name + ".wav")) break;
    }
    if (!writeWav(sampleFolder_ + "/" + name + ".wav", take, static_cast<int>(kSampleRate))) return;
    lastRecorded_ = name;
    if (project.tracks[recTrack_].kind == TrackKind::Sampler) setTrackSample(recTrack_, name);
    else if (project.tracks[recTrack_].kind == TrackKind::Drums) setLaneSample(recTrack_, recLane_, name);
}

// A sampler track's recording, read from its file and handed to the engine.
void Session::loadTrackSample(int track) {
    LaneSound& sound = sounds_[track][0];
    const unsigned version = sound.version + 1;
    sound = LaneSound();
    sound.version = version;
    const char* name = project.tracks[track].sample;
    if (!name[0]) {
        engine.setTrackSample(track, nullptr);
        return;
    }
    std::unique_ptr<SampleData> data(new SampleData);
    if (sampleFolder_.empty() || !readWav(sampleFolder_ + "/" + name + ".wav", data->data) || data->data.empty()) {
        sound.missing = true;
        engine.setTrackSample(track, nullptr);
        return;
    }
    sound.seconds = static_cast<float>(data->data.size()) / kSampleRate;
    sound.peaks.assign(kPeaks, 0.f);
    for (std::size_t i = 0; i < data->data.size(); ++i) {
        float& peak = sound.peaks[i * kPeaks / data->data.size()];
        const float v = std::fabs(data->data[i]);
        if (v > peak) peak = v;
    }
    engine.setTrackSample(track, std::move(data));
}

void Session::setTrackSample(int track, const std::string& name) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Sampler) return;
    std::snprintf(project.tracks[track].sample, sizeof project.tracks[track].sample, "%s", name.c_str());
    loadTrackSample(track);
}

void Session::setSamplerSetting(int track, int id, int value) {
    if (track < 0 || track >= kNumTracks || id < 0 || id >= sampler::kSettings || project.tracks[track].kind != TrackKind::Sampler) return;
    value = value < sampler::settingMin(id) ? sampler::settingMin(id) : (value > sampler::settingMax(id) ? sampler::settingMax(id) : value);
    if (value == project.tracks[track].sampler[id]) return;
    project.tracks[track].sampler[id] = value;
    Command c;
    c.type = Command::SetSetting;
    c.track = track;
    c.a = id;
    c.b = value;
    engine.send(c);
}

std::string Session::catchLastBar(int track, int lane) {
    if (sampleFolder_.empty() || track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes) return "";
    const bool sampler = project.tracks[track].kind == TrackKind::Sampler; // its one recording; the lane does not matter
    if (!sampler && project.tracks[track].kind != TrackKind::Drums) return "";
    const std::vector<float> bar = engine.lastBar();
    float peak = 0.f;
    for (float v : bar) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    if (peak < 1e-4f) return ""; // nothing was playing
    mkdir(sampleFolder_.c_str(), 0755); // fine if it is already there
    std::string name;
    for (int number = 1; number < 10000; ++number) {
        name = "catch " + std::to_string(number);
        if (!fileExists(sampleFolder_ + "/" + name + ".wav")) break;
    }
    if (!writeWav(sampleFolder_ + "/" + name + ".wav", bar, static_cast<int>(kSampleRate))) return "";
    if (sampler) {
        setTrackSample(track, name);
    } else {
        setLaneSample(track, lane, name);
        setLaneFit(track, lane, kSteps); // it is a bar long now, and stays one when the tempo moves
    }
    return name;
}

void Session::setLaneFit(int track, int lane, int steps) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes) return;
    steps = steps < 0 ? 0 : (steps > kMaxFitSteps ? kMaxFitSteps : steps);
    if (project.tracks[track].laneFit[lane] == steps) return;
    project.tracks[track].laneFit[lane] = steps;
    LaneSound& sound = sounds_[track][lane];
    if (!sound.recording) return;
    ++sound.version; // a stretch on its way is for the old length
    sound.waiting = steps > 0;
    if (steps == 0) {
        sound.fittedTempo = 0.f;
        sound.fittedSteps = 0;
        install(track, lane, *sound.recording); // back to as recorded
    }
}

bool Session::wantsFitting(int track, int lane) const {
    const LaneSound& sound = sounds_[track][lane];
    const int steps = project.tracks[track].laneFit[lane];
    return sound.recording && steps > 0 && (sound.fittedTempo != steadyTempo_ || sound.fittedSteps != steps);
}

void Session::startJob(int track, int lane) {
    LaneSound& sound = sounds_[track][lane];
    sound.waiting = true;
    job_.reset(new Job);
    Job* job = job_.get();
    job->track = track;
    job->lane = lane;
    job->version = sound.version;
    job->tempo = steadyTempo_;
    job->in = sound.recording;
    // A step is a sixteenth note: a quarter of a beat.
    job->frames = static_cast<int>(std::lround(project.tracks[track].laneFit[lane] * 15.0 / steadyTempo_ * kSampleRate));
    job->thread = std::thread([job] {
        // As good as the right length already (a bar caught at this tempo): leave it be.
        const int have = static_cast<int>(job->in->size());
        job->out = std::abs(have - job->frames) <= 8 ? *job->in : fitTo(*job->in, job->frames, kSampleRate);
        job->done.store(true);
    });
}

void Session::collectJob(bool wait) {
    if (!job_ || (!wait && !job_->done.load())) return;
    job_->thread.join();
    LaneSound& sound = sounds_[job_->track][job_->lane];
    if (sound.version == job_->version && sound.recording == job_->in) { // else the lane has moved on
        sound.fittedTempo = job_->tempo;
        sound.fittedSteps = project.tracks[job_->track].laneFit[job_->lane];
        sound.waiting = false;
        install(job_->track, job_->lane, job_->out);
    }
    job_.reset();
}

bool Session::stretching() const {
    if (job_) return true;
    for (const auto& track : sounds_)
        for (const LaneSound& sound : track)
            if (sound.waiting) return true;
    return false;
}

void Session::finishStretching() {
    steadyTempo_ = seenTempo_ = project.tempo; // no waiting for it to settle
    for (;;) {
        collectJob(true);
        bool started = false;
        for (int track = 0; track < kNumTracks && !started; ++track)
            for (int lane = 0; lane < kDrumLanes && !started; ++lane)
                if (wantsFitting(track, lane)) { startJob(track, lane); started = true; }
        if (!started) return;
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
        else engine.addSynthTrack(t, track.kind == TrackKind::Twin ? SynthKind::Twin : track.kind == TrackKind::Sampler ? SynthKind::Sampler : SynthKind::Phazed);

        // Every pattern on the track, in one piece.
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
                }
            }
        }
        for (int lane = 0; lane < kDrumLanes; ++lane) image->laneLevel[lane] = track.laneLevel[lane];
        engine.setTrackImage(t, std::move(image));
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
            const int count = track.kind == TrackKind::Twin ? static_cast<int>(twin::kSettings) : static_cast<int>(phazed::kSettings);
            c.type = Command::SetSetting;
            for (int id = 0; id < count; ++id) {
                c.a = id;
                c.b = values[id];
                engine.send(c);
            }
            c.type = Command::SetMacro;
            for (int m = 0; m < phazed::kMacros; ++m) {
                c.a = m;
                c.value = track.macro[m];
                engine.send(c);
            }
        }
    }
    // Every slot, the empty ones too: what the last song left there goes.
    for (int track = kMasterTrack; track < kNumTracks; ++track) {
        for (int slot = 0; slot < kEffectSlots; ++slot) {
            const EffectData data = effectData(track, slot);
            const EffectKind kind = static_cast<EffectKind>(data.kind);
            engine.setEffect(track, slot, kind);
            c = Command();
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
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        c = Command();
        c.type = Command::SetSceneRepeats;
        c.scene = scene;
        c.a = project.repeats[scene];
        engine.send(c);
    }
    c = Command();
    c.type = Command::SetTempo;
    c.value = project.tempo;
    engine.send(c);
    syncSong(); // the song's length, if it differs from the last one's
}

} // namespace apotheca
