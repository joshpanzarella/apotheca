// Session: recordings. The lanes' sounds, recording, catching, fitting to tempo, the WiFi page and updates.
#include "session.hpp"
#include "instruments.hpp"
#include "ui.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

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
constexpr int kPeaks = 512;      // points in a recording's outline (the slice view draws it 512 wide)
constexpr int kMaxFitSteps = 64; // four bars
const char* const kCatchFolder = "catches";    // where a caught bar is kept, inside the samples folder
const char* const kRecordedFolder = "recorded"; // and a take from a microphone

// A recording's file may end .wav or .WAV (the list shows both by the one name, the page takes
// both): whichever is there, as deleteSample does. On a card that tells them apart, one ending
// .WAV was listed and never played.
bool readRecording(const std::string& folder, const std::string& name, std::vector<float>& out) {
    return readWav(folder + "/" + name + ".wav", out) || readWav(folder + "/" + name + ".WAV", out);
}
bool recordingThere(const std::string& folder, const std::string& name) {
    return fileExists(folder + "/" + name + ".wav") || fileExists(folder + "/" + name + ".WAV");
}
}

std::vector<std::string> Session::sampleNames() const { return wavNames(sampleFolder_); }
std::vector<std::string> Session::sampleFolders() const { return wavFolders(sampleFolder_); }

int Session::sampleUses(const std::string& name) const {
    int uses = 0;
    if (name.empty()) return 0;
    for (int t = 0; t < kNumTracks; ++t) {
        const TrackData& track = project.tracks[t];
        for (int lane = 0; lane < kDrumLanes; ++lane) uses += track.kind == TrackKind::Drums && name == track.laneSample[lane];
        uses += holdsRecording(track.kind) && name == track.sample;
    }
    return uses;
}

bool Session::deleteSample(const std::string& name) {
    if (sampleFolder_.empty() || !sampleNameOk(name)) return false;
    bool there = false;
    for (const std::string& have : sampleNames()) there = there || have == name;
    if (!there) return false;
    // The file may end .wav or .WAV: whichever is there.
    bool gone = std::remove((sampleFolder_ + "/" + name + ".wav").c_str()) == 0;
    gone = std::remove((sampleFolder_ + "/" + name + ".WAV").c_str()) == 0 || gone;
    if (!gone) return false;
    for (int t = 0; t < kNumTracks; ++t) {
        TrackData& track = project.tracks[t];
        for (int lane = 0; lane < kDrumLanes; ++lane)
            if (track.kind == TrackKind::Drums && name == track.laneSample[lane]) setLaneSample(t, lane, "");
        if (holdsRecording(track.kind) && name == track.sample) setTrackSample(t, "");
    }
    return true;
}

namespace {
// A folder of the samples folder that is really there, as itself: no part of it a link. "" is the samples folder.
bool realFolder(const std::string& folder, const std::string& name) {
    struct stat info;
    for (std::size_t slash = name.find('/'); !name.empty(); slash = name.find('/', slash + 1)) {
        if (lstat((folder + "/" + name.substr(0, slash)).c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) return false;
        if (slash == std::string::npos) break;
    }
    return true;
}
std::string lowered(std::string text) {
    for (char& c : text) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return text;
}
}

Session::FileJob Session::moveSample(const std::string& from, const std::string& to) {
    if (sampleFolder_.empty() || !sampleNameOk(from)) return FileJob::Failed;
    if (from == to) return FileJob::Same;
    if (to.size() >= static_cast<std::size_t>(kSampleNameRoom)) return FileJob::TooLong;
    if (!sampleNameOk(to) || !realFolder(sampleFolder_, sampleFolderOf(from)) || !realFolder(sampleFolder_, sampleFolderOf(to))) return FileJob::Failed;
    // Its file, whichever way it ends; it keeps that ending.
    struct stat info;
    const char* ending = nullptr;
    for (const char* end : {".wav", ".WAV"})
        if (!ending && lstat((sampleFolder_ + "/" + from + end).c_str(), &info) == 0 && S_ISREG(info.st_mode)) ending = end;
    if (!ending) return FileJob::Failed;
    // Nothing is written over, and no two names that differ only in capitals (a card may not tell them apart).
    for (const std::string& have : sampleNames())
        if (have != from && lowered(have) == lowered(to)) return FileJob::Taken;
    const std::string file = sampleFolder_ + "/" + from + ending, target = sampleFolder_ + "/" + to + ending;
    if (lstat(target.c_str(), &info) == 0) return FileJob::Taken;
    // Some cards will not rename a file from one folder into another (the handheld's own refused):
    // then it is written again where it goes, and the old one taken away once the new one is safely there.
    if (Transfer::renameBroken || std::rename(file.c_str(), target.c_str()) != 0) { // (renameBroken: the tests' card that will not)
        std::string contents;
        const bool copied = readTextFile(file, contents) && writeTextFile(target, contents);
        if (!copied || std::remove(file.c_str()) != 0) {
            if (copied) std::remove(target.c_str()); // the old one would not go: do not leave two
            return FileJob::Failed;
        }
    }
    recordingChanged(from, to);
    const bool saved = saveNow(); // the open song now, not at the next autosave: its revs will have the new name
    followLater({{from, to}});    // the saved songs and revs, a few a frame (Moving until they are done)
    // The file HAS moved. A song that could not be written (a full card) still names the old place:
    // said, since opened later it is mended only if no other recording has that last name.
    return !saved ? FileJob::NotSaved : following() ? FileJob::Moving : FileJob::Done;
}

Session::FileJob Session::makeSampleFolder(const std::string& name) {
    if (sampleFolder_.empty() || name.empty()) return FileJob::Failed;
    if (name.size() + 2 >= static_cast<std::size_t>(kSampleNameRoom)) return FileJob::TooLong; // no recording's name would fit in it
    if (!sampleNameOk(name + "/x") || !realFolder(sampleFolder_, sampleFolderOf(name))) return FileJob::Failed;
    for (const std::string& have : sampleFolders())
        if (lowered(have) == lowered(name)) return FileJob::Taken;
    struct stat info;
    if (lstat((sampleFolder_ + "/" + name).c_str(), &info) == 0) return FileJob::Taken;
    return mkdir((sampleFolder_ + "/" + name).c_str(), 0755) == 0 ? FileJob::Done : FileJob::Failed;
}

Session::FileJob Session::deleteSampleFolder(const std::string& name) {
    if (sampleFolder_.empty() || !sampleNameOk(name + "/x") || !realFolder(sampleFolder_, name)) return FileJob::Failed;
    if (rmdir((sampleFolder_ + "/" + name).c_str()) == 0) return FileJob::Done; // (it goes only if nothing is in it)
    return errno == ENOTEMPTY || errno == EEXIST ? FileJob::NotEmpty : FileJob::Failed;
}

void Session::recordingChanged(const std::string& from, const std::string& to) {
    if ((from.empty() && to.empty()) || (!from.empty() && !sampleNameOk(from)) || (!to.empty() && !sampleNameOk(to))) return;
    for (int t = 0; t < kNumTracks; ++t) {
        TrackData& track = project.tracks[t];
        const bool sampler = holdsRecording(track.kind); // (a sampler or a CLOUD: one recording)
        if (!sampler && track.kind != TrackKind::Drums) continue;
        for (int lane = 0; lane < (sampler ? 1 : kDrumLanes); ++lane) { // a sampler has one recording, kept where lane 0's sound is
            char* name = sampler ? track.sample : track.laneSample[lane];
            const auto read = [&] { sampler ? loadTrackSample(t) : loadLane(t, lane); };
            if (!from.empty() && from == name) {
                if (to.empty()) {
                    read(); // its file has gone: it is missing from now on, not from the next time the song is opened
                    continue;
                }
                std::snprintf(name, kSampleNameRoom, "%s", to.c_str()); // the sound in the engine is the same one: it plays on
            }
            // The file it was waiting for is here; or one was sent from the page in place of the one it
            // plays, and the new sound plays from now on (a rename leaves the sound as it is: the same file).
            if (!to.empty() && to == name && (from.empty() || sounds_[t][lane].missing)) read();
        }
    }
    if (!to.empty()) findMissing(sampleLabel(to)); // or one of its base name, in another folder
}

int Session::findMissing(const std::string& label) {
    struct Lost { int track, lane; };
    std::vector<Lost> lost;
    for (int t = 0; t < kNumTracks; ++t) {
        const TrackData& track = project.tracks[t];
        const bool sampler = holdsRecording(track.kind); // (a sampler or a CLOUD: one recording)
        if (!sampler && track.kind != TrackKind::Drums) continue;
        for (int lane = 0; lane < (sampler ? 1 : kDrumLanes); ++lane) {
            const char* name = sampler ? track.sample : track.laneSample[lane];
            if (sounds_[t][lane].missing && sampleNameOk(name) && (label.empty() || sampleLabel(name) == label)) lost.push_back({t, lane});
        }
    }
    if (lost.empty() || sampleFolder_.empty()) return 0; // nothing missing: the folder is not looked at
    ++lookups_;
    const std::vector<std::string> have = wavNames(sampleFolder_); // once, however many are missing
    int found = 0;
    for (const Lost& one : lost) {
        const bool sampler = holdsRecording(project.tracks[one.track].kind);
        char* name = sampler ? project.tracks[one.track].sample : project.tracks[one.track].laneSample[one.lane];
        const auto read = [&] { sampler ? loadTrackSample(one.track) : loadLane(one.track, one.lane); };
        const std::string was = name, base = sampleLabel(was);
        std::string at;
        int count = 0;
        bool there = false; // a file of its own name that will not read: it is not swapped for another
        for (const std::string& other : have) {
            if (other == was) there = true;
            else if (sampleLabel(other) == base && sampleNameOk(other) && ++count == 1) at = other;
        }
        if (there || count != 1) continue; // none, or two: never a guess
        std::snprintf(name, kSampleNameRoom, "%s", at.c_str());
        read();
        if (!sounds_[one.track][one.lane].missing) {
            ++found;
            continue;
        }
        std::snprintf(name, kSampleNameRoom, "%s", was.c_str()); // it would not read: as it was
        read();
    }
    return found;
}

void Session::install(int track, int lane, const std::vector<float>& samples) {
    std::unique_ptr<SampleData> data(new SampleData);
    data->data = samples;
    engine.setLaneSample(track, lane, std::move(data));
}

namespace {
// An outline of a sound, kPeaks points of its loudest.
void outlineOf(const std::vector<float>& sound, std::vector<float>& peaks) {
    peaks.assign(kPeaks, 0.f);
    for (std::size_t i = 0; i < sound.size(); ++i) {
        float& peak = peaks[i * kPeaks / sound.size()];
        const float v = std::fabs(sound[i]);
        if (v > peak) peak = v;
    }
}
}

// Read the lane's file (or take the built-in drum) and make the lane's sound from it: as recorded,
// or as its edits make it. If the lane is fitted, update() will follow with the stretched version.
void Session::loadLane(int track, int lane) {
    LaneSound& sound = sounds_[track][lane];
    const unsigned version = sound.version + 1;
    sound = LaneSound();
    sound.version = version;
    const char* name = project.tracks[track].laneSample[lane];
    const drumsynth::Voice& voice = project.tracks[track].laneSynth[lane];
    if (voice.on()) {
        // DRUM SYNTH: the voice made, and from then on it is the lane's recording (FIT, EDIT, the picture).
        // Too costly for a frame, it is made on the helper thread, and the lane plays what it had till then.
        if (drumsynth::work(voice, kSampleRate) > drumsynth::kWorkInAFrame) {
            sound.making = sound.shaping = sound.waiting = true;
            return;
        }
        madeLane(track, lane, drumsynth::make(voice, kSampleRate));
        return;
    }
    if (!name[0]) {
        // The built-in drum: the kit's own sound, never copied unless an edit makes something of it.
        const std::vector<float>& drum = DrumKit::builtIn(kSampleRate).samples[lane];
        sound.source = std::shared_ptr<const std::vector<float>>(&drum, [](const std::vector<float>*) {}); // (kept for as long as the program runs)
    } else {
        std::shared_ptr<std::vector<float>> recording(new std::vector<float>);
        if (sampleFolder_.empty() || !readRecording(sampleFolder_, name, *recording) || recording->empty()) {
            sound.missing = true; // the drum plays in its place, and the song keeps the name
            engine.setLaneSample(track, lane, nullptr);
            return;
        }
        sound.recording = sound.source = recording;
    }
    sound.sourceSeconds = static_cast<float>(sound.source->size()) / kSampleRate;
    outlineOf(*sound.source, sound.sourcePeaks);
    shapeLane(track, lane);
}

void Session::madeLane(int track, int lane, std::vector<float> made) {
    LaneSound& sound = sounds_[track][lane];
    sound.making = sound.shaping = sound.waiting = false;
    sound.recording = sound.source = std::make_shared<const std::vector<float>>(std::move(made));
    sound.sourceSeconds = static_cast<float>(sound.source->size()) / kSampleRate;
    outlineOf(*sound.source, sound.sourcePeaks);
    shapeLane(track, lane);
}

void Session::shapeLane(int track, int lane) {
    LaneSound& sound = sounds_[track][lane];
    if (!sound.source) return; // missing: the drum plays, and nothing is made
    const LaneEdit& edit = project.tracks[track].laneEdit[lane];
    ++sound.version; // a stretch on its way is for the sound as it was
    // PITCH on a long sound is too much for one frame of the handheld (a 10 s file two octaves down
    // is 0.1 s on the desk): it is made on the helper thread, as a FIT is, and until it is ready the
    // lane plays what it had (or, freshly read, the file as it is).
    if (shapeCost(sound.source->size(), edit) > kShapeInAFrame) {
        sound.shaping = sound.waiting = true;
        if (!sound.shaped) {
            sound.shaped = sound.source;
            sound.seconds = static_cast<float>(sound.shaped->size()) / kSampleRate;
        }
        installLane(track, lane); // (the level, made at once, on what it has)
        return;
    }
    sound.shaping = false;
    sound.shaped = edit.shapes() ? std::make_shared<const std::vector<float>>(makeShape(*sound.source, edit, kSampleRate)) : sound.source;
    sound.seconds = static_cast<float>(sound.shaped->size()) / kSampleRate;
    sound.fitted.reset();
    sound.fittedTempo = 0.f;
    sound.fittedSteps = 0;
    sound.waiting = sound.recording && project.tracks[track].laneFit[lane] > 0;
    installLane(track, lane);
}

void Session::installLane(int track, int lane) {
    LaneSound& sound = sounds_[track][lane];
    const std::shared_ptr<const std::vector<float>>& made = sound.fitted ? sound.fitted : sound.shaped;
    if (!made) return;
    const LaneEdit& edit = project.tracks[track].laneEdit[lane];
    if (edit.atRest()) {
        if (!sound.recording) { // the built-in drum as it is: the kit's, in the engine too
            sound.peaks.clear();
            engine.setLaneSample(track, lane, nullptr);
            return;
        }
        outlineOf(*made, sound.peaks);
        install(track, lane, *made); // as recorded (or fitted), to the bit
        return;
    }
    std::unique_ptr<SampleData> data(new SampleData);
    data->data.resize(made->size());
    makeLevel(made->data(), static_cast<int>(made->size()), edit, kSampleRate, data->data.data());
    outlineOf(data->data, sound.peaks);
    engine.setLaneSample(track, lane, std::move(data));
}

void Session::setLaneSample(int track, int lane, const std::string& name) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return;
    TrackData& t = project.tracks[track];
    std::snprintf(t.laneSample[lane], sizeof t.laneSample[lane], "%s", name.c_str());
    // Another sound: START and END were parts of the last one, and go back to the whole. The rest of
    // the lane's edits stay. Not a step of undo by itself (as the sound is not): undo would put the
    // old cut on the new sound. A synth voice goes with it (a recording or the drum in its place).
    LaneEdit& edit = t.laneEdit[lane];
    if (edit.start != 0 || edit.end != kEditUnits || t.laneSynth[lane].on()) {
        const bool unmarked = undoReady_ && soundMark() == soundMark_;
        edit.start = 0;
        edit.end = kEditUnits;
        t.laneSynth[lane] = drumsynth::Voice();
        if (unmarked) soundMark_ = soundMark();
    }
    loadLane(track, lane);
}

void Session::setLaneSynth(int track, int lane, const drumsynth::Voice& wanted) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return;
    TrackData& t = project.tracks[track];
    const drumsynth::Voice now = drumsynth::voiceHeld(wanted), was = t.laneSynth[lane];
    if (now == was && (now.on() || !t.laneSample[lane][0])) return;
    t.laneSynth[lane] = now;
    t.laneSample[lane][0] = 0; // a synth, or the built-in drum: no recording
    if (now.model != was.model) { // another sound: START and END go back to the whole (the rest of EDIT stays)
        t.laneEdit[lane].start = 0;
        t.laneEdit[lane].end = kEditUnits;
    }
    loadLane(track, lane);
}

void Session::setLaneEdit(int track, int lane, const LaneEdit& wanted) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return;
    readyLane(track, lane);
    LaneEdit& edit = project.tracks[track].laneEdit[lane];
    const LaneSound& sound = sounds_[track][lane];
    const LaneEdit was = edit, now = laneEditHeld(wanted, sound.sourceSeconds);
    if (now == was) return;
    edit = now;
    LaneEdit shapeWas = was, shapeNow = now; // what is cut, turned round or pitched: made again, and fitted again
    shapeWas.gain = shapeNow.gain = shapeWas.fadeIn = shapeNow.fadeIn = shapeWas.fadeOut = shapeNow.fadeOut = 0;
    if (shapeWas != shapeNow) shapeLane(track, lane);
    else installLane(track, lane); // the fades and GAIN alone: on what is made already
}

void Session::readyLane(int track, int lane) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return;
    const LaneSound& sound = sounds_[track][lane];
    if (!sound.source && !sound.missing && !sound.making) loadLane(track, lane); // (a synth voice on its way has no source yet: read again, it was never let land) // (a synth voice on its way has no source yet: read again, it was never let land)
}

bool Session::normalizeLane(int track, int lane) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return false;
    const LaneSound& sound = sounds_[track][lane];
    const std::shared_ptr<const std::vector<float>>& made = sound.fitted ? sound.fitted : sound.shaped;
    int gain = 0;
    if (!made || !normalGain(made->data(), static_cast<int>(made->size()), gain)) return false;
    LaneEdit edit = project.tracks[track].laneEdit[lane];
    edit.gain = gain;
    setLaneEdit(track, lane, edit);
    return true;
}

bool Session::trimLaneSilence(int track, int lane) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return false;
    const LaneSound& sound = sounds_[track][lane];
    LaneEdit edit = project.tracks[track].laneEdit[lane];
    if (!sound.source || !silenceEnds(*sound.source, kSampleRate, edit.start, edit.end)) return false;
    setLaneEdit(track, lane, edit);
    return true;
}

// ---- files over WiFi --------------------------------------------------------------------

std::string Session::transferName() const {
    if (!transferOn() || !wifi_.here()) return "";
    const WifiStatus status = wifi_.now().status;
    if ((status.state != WifiState::Joined && status.state != WifiState::Hotspot) || status.hostname.empty()) return "";
    const std::string address = transferAddress();
    const std::size_t tail = address.find_first_of(":/"); // (no colon on port 80: from the number's slash)
    return status.hostname + (tail == std::string::npos ? "" : address.substr(tail));
}

bool Session::toggleTransfer(unsigned seed) {
    hotspotOwnsTransfer_ = false; // a hand on the WIFI PAGE: HOTSPOT owns nothing now (setHotspot says when it does)
    if (transfer_.running()) {
        transfer_.stop();
        discardUpdate(); // an update nobody answered goes when WiFi is turned off
        wifi_.down();    // (our firmware: the radio off. On its own thread; nothing where there is no such program)
        return false;
    }
    if (folder_.empty() || sampleFolder_.empty()) return false; // nowhere for files to go
    const std::string chords = chordFolder_.empty() ? folder_ + "/../chords" : chordFolder_;
    // 80 if it binds (no port to type; needs root: our firmware), else 8080 if it is free, else whatever is.
    transfer_.setDesk(&designer_);
    transfer_.setBounceFolder(bounceFolder_);
    transfer_.setOpenSong(songName_); // (and again from update() whenever it changes)
    toldOpenSong_ = songName_;
    const bool on = transfer_.start(sampleFolder_, folder_, chords, 80, seed) || transfer_.start(sampleFolder_, folder_, chords, 8080, seed) ||
                    transfer_.start(sampleFolder_, folder_, chords, 0, seed);
    if (on) std::printf("files over wifi: http://%s\n", transfer_.address().c_str()); // in the log too, for whoever is setting it up
    if (on) wifi_.up(); // (our firmware: the radio on, and a saved network joined by itself)
    return on;
}

bool Session::setHotspot(bool on, unsigned seed) {
    if (on) {
        const bool was = transferOn();
        if (!was && !toggleTransfer(seed)) return false; // nowhere for files to go: the radio stays off
        if (!transfer_.running()) return false;          // (a still: the page is only pretended on)
        if (!was) hotspotOwnsTransfer_ = true;
        wifi_.hotspot();
        return true;
    }
    if (hotspotOwnsTransfer_) {
        if (transferOn()) toggleTransfer(seed); // the radio off with it: down stops the hotspot too
    } else if (transferOn()) {
        wifi_.down();
        wifi_.up(); // back to joining (a saved network rejoins by itself)
    }
    return true;
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
    if (std::rename((programFolder_ + "/apotheca.pending").c_str(), (programFolder_ + "/apotheca.new").c_str()) != 0) return false;
    updateReady_ = true;
    return true;
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
    if (kind != TrackKind::Drums && !holdsRecording(kind)) return rec_;
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
    // Takes go in a folder of their own, so they do not pile up among everything else.
    mkdir(sampleFolder_.c_str(), 0755);
    mkdir((sampleFolder_ + "/" + kRecordedFolder).c_str(), 0755);
    std::string name;
    for (int number = 1; number < 10000; ++number) {
        name = std::string(kRecordedFolder) + "/rec " + std::to_string(number);
        if (!recordingThere(sampleFolder_, name)) break;
    }
    if (!writeWav(sampleFolder_ + "/" + name + ".wav", take, static_cast<int>(kSampleRate))) return;
    lastRecorded_ = name;
    if (holdsRecording(project.tracks[recTrack_].kind)) setTrackSample(recTrack_, name);
    else if (project.tracks[recTrack_].kind == TrackKind::Drums) setLaneSample(recTrack_, recLane_, name);
}

// A sampler track's recording, read from its file and handed to the engine.
// A multisample: where the files beside it spell notes in their names, they
// play too, each round its own note (sampler.hpp); the song still names only
// the one picked, and the set is found again whenever it is read.
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
    std::vector<float> main;
    if (sampleFolder_.empty() || !readRecording(sampleFolder_, name, main) || main.empty()) {
        sound.missing = true;
        engine.setTrackSample(track, nullptr);
        return;
    }
    sound.seconds = static_cast<float>(main.size()) / kSampleRate;
    sound.peaks.assign(kPeaks, 0.f);
    for (std::size_t i = 0; i < main.size(); ++i) {
        float& peak = sound.peaks[i * kPeaks / main.size()];
        const float v = std::fabs(main[i]);
        if (v > peak) peak = v;
    }
    if (project.tracks[track].kind == TrackKind::Sampler) {
        // The files beside it with a note in the name. The picked one plays
        // at its spelled note, or at ROOT where it spells none.
        const std::string folder = sampleFolderOf(name);
        const int mainNote = sampleNote(name);
        const int mainRoot = mainNote >= 0 ? mainNote : project.tracks[track].sampler[sampler::Root];
        struct Candidate {
            std::string name;
            int note;
        };
        std::vector<Candidate> beside;
        for (const std::string& have : sampleNames()) {
            if (have == name || sampleFolderOf(have) != folder) continue;
            const int note = sampleNote(have);
            if (note >= 0 && note <= 127) beside.push_back({have, note});
        }
        if (!beside.empty()) {
            // The nearest notes to the picked one (a lower one first where two
            // are as near), the picked one always among them.
            std::stable_sort(beside.begin(), beside.end(), [&](const Candidate& a, const Candidate& b) {
                const int da = std::abs(a.note - mainRoot), db = std::abs(b.note - mainRoot);
                return da != db ? da < db : a.note < b.note;
            });
            if (beside.size() > static_cast<std::size_t>(sampler::kMaxZones - 1)) beside.resize(sampler::kMaxZones - 1);
            std::unique_ptr<sampler::SampleZoneSet> set(new sampler::SampleZoneSet);
            set->zones.push_back(sampler::SampleZone());
            set->zones.back().data = std::move(main);
            set->zones.back().root = mainRoot;
            for (const Candidate& c : beside) {
                std::vector<float> data;
                if (!readRecording(sampleFolder_, c.name, data) || data.empty()) continue; // gone or broken: skipped
                set->zones.push_back(sampler::SampleZone());
                set->zones.back().data = std::move(data);
                set->zones.back().root = c.note;
            }
            if (set->zones.size() > 1) {
                // By root, lowest first; one note twice keeps the picked one.
                // Each plays up to the middle with its neighbours.
                std::stable_sort(set->zones.begin(), set->zones.end(),
                                 [](const sampler::SampleZone& a, const sampler::SampleZone& b) { return a.root < b.root; });
                std::vector<sampler::SampleZone> kept;
                for (sampler::SampleZone& z : set->zones) {
                    if (!kept.empty() && kept.back().root == z.root) continue;
                    kept.push_back(std::move(z));
                }
                set->zones = std::move(kept);
                if (set->zones.size() > 1) {
                    for (std::size_t i = 0; i < set->zones.size(); ++i) {
                        sampler::SampleZone& z = set->zones[i];
                        z.lo = i == 0 ? 0 : (set->zones[i - 1].root + z.root) / 2 + 1;
                        z.hi = i + 1 == set->zones.size() ? 127 : (z.root + set->zones[i + 1].root) / 2;
                    }
                    sound.zones = static_cast<int>(set->zones.size());
                    engine.setTrackZones(track, std::move(set));
                    return;
                }
            }
            // One zone after all (nothing readable beside it, or all one note):
            // the picked one alone, below. (The survivor is always it: it went
            // first, and one note twice keeps the first.)
            main = std::move(set->zones.front().data);
        }
    }
    std::unique_ptr<SampleData> data(new SampleData);
    data->data = std::move(main);
    engine.setTrackSample(track, std::move(data));
}

bool Session::putPresetRecording(int track, const std::string& name) {
    if (track < 0 || track >= kNumTracks || !holdsRecording(project.tracks[track].kind) || !sampleNameOk(name)) return false;
    if (name == project.tracks[track].sample) return true;
    std::string found;
    int alike = 0;
    for (const std::string& have : sampleNames()) {
        if (have == name) {
            found = have;
            alike = 1;
            break;
        }
        if (sampleLabel(have) == sampleLabel(name) && ++alike == 1) found = have;
    }
    if (alike != 1) { // none, or two of its base name: never a guess
        say("RECORDING NOT FOUND");
        return false;
    }
    setTrackSample(track, found);
    return true;
}

void Session::setTrackSample(int track, const std::string& name) {
    if (track < 0 || track >= kNumTracks || !holdsRecording(project.tracks[track].kind)) return;
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
    const bool sampler = holdsRecording(project.tracks[track].kind); // its one recording; the lane does not matter
    if (!sampler && project.tracks[track].kind != TrackKind::Drums) return "";
    const std::vector<float> bar = engine.lastBar();
    float peak = 0.f;
    for (float v : bar) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    if (peak < 1e-4f) return ""; // nothing was playing
    // Catches go in a folder of their own, so they do not pile up among everything else.
    mkdir(sampleFolder_.c_str(), 0755); // fine if it is already there
    mkdir((sampleFolder_ + "/" + kCatchFolder).c_str(), 0755);
    std::string name;
    for (int number = 1; number < 10000; ++number) {
        name = std::string(kCatchFolder) + "/catch " + std::to_string(number);
        if (!recordingThere(sampleFolder_, name)) break;
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

int Session::laneSlices(int track, int lane) const {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes) return kSteps;
    const int fit = project.tracks[track].laneFit[lane];
    return fit > kSteps ? fit : kSteps;
}

void Session::sendLanePart(int track, int lane) {
    Command c;
    c.type = Command::SetLanePart;
    c.track = track;
    c.a = lane;
    c.value = project.tracks[track].laneOneSlice[lane] ? 1.f / static_cast<float>(laneSlices(track, lane)) : 0.f;
    engine.send(c);
}

void Session::setLaneOneSlice(int track, int lane, bool on) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return;
    project.tracks[track].laneOneSlice[lane] = on;
    sendLanePart(track, lane);
}

void Session::setSamplerChop(int track, bool on) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Sampler) return;
    project.tracks[track].samplerChop = on;
    Command c;
    c.type = Command::SetTrackChop;
    c.track = track;
    c.a = on ? 1 : 0;
    engine.send(c);
}

void Session::setLaneFit(int track, int lane, int steps) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes) return;
    steps = steps < 0 ? 0 : (steps > kMaxFitSteps ? kMaxFitSteps : steps);
    if (project.tracks[track].laneFit[lane] == steps) return;
    project.tracks[track].laneFit[lane] = steps;
    sendLanePart(track, lane); // a slice is a different share of a recording fitted to a different length
    LaneSound& sound = sounds_[track][lane];
    if (!sound.recording) return;
    ++sound.version; // a stretch on its way is for the old length
    sound.waiting = steps > 0;
    if (steps == 0) {
        sound.fittedTempo = 0.f;
        sound.fittedSteps = 0;
        sound.fitted.reset();
        installLane(track, lane); // back to as recorded (and edited)
    }
}

// A lane wants work on the helper thread: a shape too costly for a frame (shapeLane), or its FIT
// for the tempo now.
bool Session::wantsFitting(int track, int lane) const {
    const LaneSound& sound = sounds_[track][lane];
    if (sound.shaping) return true; // (a synth voice to be made too)
    const int steps = project.tracks[track].laneFit[lane];
    return sound.recording && sound.shaped && steps > 0 && (sound.fittedTempo != steadyTempo_ || sound.fittedSteps != steps);
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
    job->make = sound.making;
    job->voice = project.tracks[track].laneSynth[lane];
    job->shape = sound.shaping && !sound.making;
    job->edit = project.tracks[track].laneEdit[lane];
    job->in = sound.shaping ? sound.source : sound.shaped; // the edited sound is what is fitted
    // A step is a sixteenth note: a quarter of a beat. (A shape is fitted in the same go once the tempo is known.)
    const int steps = sound.recording ? project.tracks[track].laneFit[lane] : 0;
    job->frames = steps > 0 && steadyTempo_ > 0.f ? static_cast<int>(std::lround(steps * 15.0 / steadyTempo_ * kSampleRate)) : 0;
    job->thread = std::thread([job] {
        if (job->make) { // a synth voice: made, and the rest is the app's (collectJob)
            job->out = drumsynth::make(job->voice, kSampleRate);
            job->done.store(true);
            return;
        }
        if (job->shape) job->shaped = makeShape(*job->in, job->edit, kSampleRate);
        const std::vector<float>& from = job->shape ? job->shaped : *job->in;
        // As good as the right length already (a bar caught at this tempo): leave it be.
        const int have = static_cast<int>(from.size());
        if (job->frames > 0) job->out = std::abs(have - job->frames) <= 8 ? from : fitTo(from, job->frames, kSampleRate);
        job->done.store(true);
    });
}

void Session::collectJob(bool wait) {
    if (!job_ || (!wait && !job_->done.load())) return;
    job_->thread.join();
    LaneSound& sound = sounds_[job_->track][job_->lane];
    const int track = job_->track, lane = job_->lane;
    if (job_->make) { // a synth voice made: the lane's recording, then shaped as any (it may want the thread again)
        if (sound.version == job_->version && sound.making && project.tracks[track].laneSynth[lane] == job_->voice) madeLane(track, lane, std::move(job_->out));
        job_.reset();
        return;
    }
    if (sound.version == job_->version && (job_->shape ? sound.source : sound.shaped) == job_->in) { // else the lane has moved on
        if (job_->shape) {
            sound.shaping = false;
            sound.shaped = std::make_shared<const std::vector<float>>(std::move(job_->shaped));
            sound.seconds = static_cast<float>(sound.shaped->size()) / kSampleRate;
            sound.fitted.reset();
            sound.fittedTempo = 0.f;
            sound.fittedSteps = 0;
        }
        if (job_->frames > 0) {
            sound.fittedTempo = job_->tempo;
            sound.fittedSteps = project.tracks[track].laneFit[lane];
            sound.fitted = std::make_shared<const std::vector<float>>(std::move(job_->out));
        }
        sound.waiting = false; // (a shape and its FIT are made in the one go: a job starts only once the tempo is known)
        installLane(track, lane);
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

} // namespace apotheca
