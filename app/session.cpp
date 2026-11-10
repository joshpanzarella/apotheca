#include "session.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
            c.type = Command::SetStepStart;
            c.value = p.start[lane][step];
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

// ---- recordings on drum lanes -------------------------------------------------------

Session::~Session() {
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
        job->out = stretchTo(*job->in, job->frames);
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
        else engine.addSynthTrack(t, track.kind == TrackKind::Twin ? SynthKind::Twin : SynthKind::Phazed);

        // Every pattern on the track, in one piece.
        std::unique_ptr<TrackImage> image(new TrackImage);
        for (int scene = 0; scene < kMaxScenes; ++scene) {
            for (int lane = 0; lane < kDrumLanes; ++lane)
                for (int step = 0; step < kSteps; ++step) {
                    image->level[scene][lane][step] = track.drums[scene].level[lane][step];
                    image->nudge[scene][lane][step] = track.drums[scene].nudge[lane][step];
                    image->chance[scene][lane][step] = track.drums[scene].chance[lane][step];
                    image->start[scene][lane][step] = track.drums[scene].start[lane][step];
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
        if (track.kind == TrackKind::Drums)
            for (int lane = 0; lane < kDrumLanes; ++lane) loadLane(t, lane);

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
