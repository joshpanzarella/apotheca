// Session: recordings. The lanes' sounds, recording, catching, fitting to tempo, the WiFi page and updates.
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
        if (track.kind == TrackKind::Sampler && name == track.sample) setTrackSample(t, "");
    }
    return true;
}

void Session::recordingChanged(const std::string& from, const std::string& to) {
    if ((from.empty() && to.empty()) || (!from.empty() && !sampleNameOk(from)) || (!to.empty() && !sampleNameOk(to))) return;
    for (int t = 0; t < kNumTracks; ++t) {
        TrackData& track = project.tracks[t];
        const bool sampler = track.kind == TrackKind::Sampler;
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
        const bool sampler = track.kind == TrackKind::Sampler;
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
        const bool sampler = project.tracks[one.track].kind == TrackKind::Sampler;
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
    if (sampleFolder_.empty() || !readRecording(sampleFolder_, name, *recording) || recording->empty()) {
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
    transfer_.setBounceFolder(bounceFolder_);
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
    if (sampleFolder_.empty() || !readRecording(sampleFolder_, name, data->data) || data->data.empty()) {
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

} // namespace apotheca
