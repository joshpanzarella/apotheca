#include "session.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "song_file.hpp"
#include "synth_spec.hpp"

namespace apotheca {

namespace {
constexpr Uint32 kAuditionMs = 350;
constexpr Uint32 kSaveEveryMs = 2000;

// New songs are named with two words, so nobody has to type on a D-pad.
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
        if (!engine.addSynthTrack(track, kind == TrackKind::Twin ? SynthKind::Twin : SynthKind::Phazed)) return false;
        project.tracks[track].preset = 0;
        specFor(kind)->loadPreset(project.tracks[track], 0);
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
    NoteEvent notes[kMaxNotes]; // chords written out as their notes
    project.tracks[track].notes[scene].render(notes);
    Command c;
    c.type = Command::SetNote;
    c.track = track;
    c.scene = scene;
    for (int slot = 0; slot < kMaxNotes; ++slot) {
        c.a = slot;
        c.b = notes[slot].step;
        c.c = notes[slot].pitch;
        c.d = notes[slot].length;
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

void Session::applyProject() {
    Command c;
    c.type = Command::Stop;
    engine.send(c);
    project.playing = false;
    for (Sounding& sounding : sounding_) sounding.track = -1;

    for (int track = 0; track < kNumTracks; ++track) engine.removeTrack(track); // those with nothing say so; fine
    for (int t = 0; t < project.trackCount(); ++t) {
        const TrackData& track = project.tracks[t];
        if (track.kind == TrackKind::Drums) engine.addDrumTrack(t);
        else engine.addSynthTrack(t, track.kind == TrackKind::Twin ? SynthKind::Twin : SynthKind::Phazed);

        // Every pattern on the track, in one piece.
        std::unique_ptr<TrackImage> image(new TrackImage);
        for (int scene = 0; scene < kMaxScenes; ++scene) {
            for (int lane = 0; lane < kDrumLanes; ++lane)
                for (int step = 0; step < kSteps; ++step) {
                    image->level[scene][lane][step] = track.drums[scene].level[lane][step];
                    image->nudge[scene][lane][step] = track.drums[scene].nudge[lane][step];
                    image->chance[scene][lane][step] = track.drums[scene].chance[lane][step];
                }
            NoteEvent notes[kMaxNotes];
            track.notes[scene].render(notes);
            for (int slot = 0; slot < kMaxNotes; ++slot) {
                const NoteEvent& n = notes[slot];
                if (n.velocity <= 0.f) continue;
                image->notes[scene][slot].step = static_cast<std::uint8_t>(n.step);
                image->notes[scene][slot].pitch = static_cast<std::uint8_t>(n.pitch);
                image->notes[scene][slot].length = static_cast<std::uint8_t>(n.length);
                image->notes[scene][slot].velocity = n.velocity;
            }
        }
        for (int lane = 0; lane < kDrumLanes; ++lane) image->laneLevel[lane] = track.laneLevel[lane];
        engine.setTrackImage(t, std::move(image));

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
