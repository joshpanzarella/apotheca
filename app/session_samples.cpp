// Session: recordings. The lanes' sounds, recording, catching, fitting to tempo, the WiFi page and updates.
#include "session.hpp"
#include "instruments.hpp"
#include "text.hpp"
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
#include <functional>
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
bool readRecording(const std::string& folder, const std::string& name, Clip& out) {
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
    // The lanes and tracks that played it go back to their drum, and that is no step of undo: undo
    // would only put back a name whose file is gone (it is deleted for good). Steps that name it
    // find it missing, as a song opened would.
    const bool unmarked = undoReady_ && soundMark() == soundMark_;
    for (int t = 0; t < kNumTracks; ++t) {
        TrackData& track = project.tracks[t];
        for (int lane = 0; lane < kDrumLanes; ++lane)
            if (track.kind == TrackKind::Drums && name == track.laneSample[lane]) setLaneSample(t, lane, "");
        if (holdsRecording(track.kind) && name == track.sample) setTrackSample(t, "");
    }
    if (unmarked) soundMark_ = soundMark();
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
    // One copy at a time, and a move asked meanwhile is told so before anything else: it is not looked at
    // (the copy under way stays under way until finishCarry, on this thread, so the answer is certain).
    if (carry_) return FileJob::Busy;
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
    // then it is written again where it goes, and the old one taken away once the new one is safely
    // there, on a thread of its own (seconds for a large one); finishCarry does the rest. Until then
    // nothing has changed: the old file is there, and the new one only appears whole (writeTextFile).
    if (Transfer::renameBroken || std::rename(file.c_str(), target.c_str()) != 0) { // (renameBroken: the tests' card that will not)
        carry_.reset(new CarryJob);
        CarryJob* job = carry_.get();
        job->from = from, job->to = to, job->file = file, job->target = target;
        job->thread = std::thread([job] {
            std::string contents;
            const bool copied = readTextFile(job->file, contents) && writeTextFile(job->target, contents);
            job->ok = copied && std::remove(job->file.c_str()) == 0;
            if (copied && !job->ok) std::remove(job->target.c_str()); // the old one would not go: do not leave two
            job->done.store(true);
        });
        return FileJob::Copying;
    }
    recordingChanged(from, to);
    const bool saved = saveNow(); // the open song now, not at the next autosave: its revs will have the new name
    followLater({{from, to}});    // the saved songs and revs, a few a frame (Moving until they are done)
    // The file HAS moved. A song that could not be written (a full card) still names the old place:
    // said, since opened later it is mended only if no other recording has that last name.
    return !saved ? FileJob::NotSaved : following() ? FileJob::Moving : FileJob::Done;
}

void Session::finishCarry() {
    if (!carry_ || !carry_->done.load()) return;
    if (carry_->thread.joinable()) carry_->thread.join();
    const std::string from = carry_->from, to = carry_->to;
    const bool ok = carry_->ok;
    carry_.reset();
    if (ok) {
        recordingChanged(from, to);
        const bool saved = saveNow();
        followLater({{from, to}});
        carryResult_ = !saved ? FileJob::NotSaved : following() ? FileJob::Moving : FileJob::Done;
    } else {
        carryResult_ = FileJob::Failed;
    }
    ++sampleFiles_; // open lists of recordings read the card again
    ++carriesDone_;
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

// Undo follows a recording renamed or moved (gesture audit G04): every step's text, the redo steps'
// and the one undo goes back to next (settled_) name it where it is now, as the saved songs do
// (renameRecordings). A step that named the old place would put back a name whose file is gone.
// Only a text that has the name in it is gone through (a plain search first, as followFile).
void Session::undoFollows(const std::vector<std::pair<std::string, std::string>>& renames) {
    if (renames.empty()) return;
    const auto follow = [&](std::string& text) {
        bool named = false;
        for (const auto& rename : renames) named = named || text.find(rename.first) != std::string::npos;
        if (named) renameRecordings(text, renames);
    };
    for (UndoStep& step : undo_) follow(step.text);
    for (UndoStep& step : redo_) follow(step.text);
    follow(settled_);
}

void Session::recordingChanged(const std::string& from, const std::string& to, bool stepsFollow) {
    if ((from.empty() && to.empty()) || (!from.empty() && !sampleNameOk(from)) || (!to.empty() && !sampleNameOk(to))) return;
    // The new name is no step of undo (nothing was done to the song), and the steps follow it.
    const bool unmarked = undoReady_ && soundMark() == soundMark_;
    if (stepsFollow && !from.empty() && !to.empty()) undoFollows({{from, to}});
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
    if (unmarked) soundMark_ = soundMark();
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
    // A recording found is no step of undo (the song names where it is now), and the steps follow it.
    const bool unmarked = undoReady_ && soundMark() == soundMark_;
    std::vector<std::pair<std::string, std::string>> renames;
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
            renames.push_back({was, at});
            continue;
        }
        std::snprintf(name, kSampleNameRoom, "%s", was.c_str()); // it would not read: as it was
        read();
    }
    undoFollows(renames);
    if (unmarked) soundMark_ = soundMark();
    return found;
}

void Session::install(int track, int lane, const Clip& samples) {
    std::unique_ptr<SampleData> data(new SampleData);
    data->data = samples.left();
    if (samples.stereo()) data->right = samples.right; // STEREO: both sides to the engine
    engine.setLaneSample(track, lane, std::move(data));
}

namespace {
// An outline of a sound, kPeaks points of its loudest (of either side, a stereo one's: STEREO keeps the one picture).
void outlineOf(const std::vector<float>& sound, std::vector<float>& peaks, const std::vector<float>* right = nullptr) {
    peaks.assign(kPeaks, 0.f);
    if (right && right->size() != sound.size()) right = nullptr;
    for (std::size_t i = 0; i < sound.size(); ++i) {
        float& peak = peaks[i * kPeaks / sound.size()];
        float v = std::fabs(sound[i]);
        if (right && std::fabs((*right)[i]) > v) v = std::fabs((*right)[i]);
        if (v > peak) peak = v;
    }
}
void outlineOf(const Clip& sound, std::vector<float>& peaks) { outlineOf(sound.left(), peaks, sound.stereo() ? &sound.right : nullptr); }
void outlineOf(const SampleData& sound, std::vector<float>& peaks) { outlineOf(sound.data, peaks, sound.stereo() ? &sound.right : nullptr); }
// The built-in drums as Waves (mono), made once and kept for as long as the program runs, as the kit is.
const Clip& builtInDrum(int lane) {
    static const Clip* const* drums = [] {
        static const Clip* made[kDrumLanes];
        for (int l = 0; l < kDrumLanes; ++l) made[l] = new Clip(DrumKit::builtIn(kSampleRate).samples[l]);
        return made;
    }();
    return *drums[lane];
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
        const Clip& drum = builtInDrum(lane);
        sound.source = std::shared_ptr<const Clip>(&drum, [](const Clip*) {}); // (kept for as long as the program runs)
    } else {
        std::shared_ptr<Clip> recording(new Clip);
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
    sound.recording = sound.source = std::make_shared<const Clip>(std::move(made));
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
    sound.shaped = edit.shapes() ? std::make_shared<const Clip>(makeShape(*sound.source, edit, kSampleRate)) : sound.source;
    sound.seconds = static_cast<float>(sound.shaped->size()) / kSampleRate;
    sound.fitted.reset();
    sound.fittedTempo = 0.f;
    sound.fittedSteps = 0;
    sound.waiting = sound.recording && project.tracks[track].laneFit[lane] > 0;
    installLane(track, lane);
}

void Session::installLane(int track, int lane) {
    LaneSound& sound = sounds_[track][lane];
    const std::shared_ptr<const Clip>& made = sound.fitted ? sound.fitted : sound.shaped;
    if (!made) return;
    const LaneEdit& edit = project.tracks[track].laneEdit[lane];
    if (edit.levelAtRest()) { // (MONO alone is a shape: its sides mixed, and no guard)
        if (edit.atRest() && !sound.recording) { // the built-in drum as it is: the kit's, in the engine too
            sound.peaks.clear();
            engine.setLaneSample(track, lane, nullptr);
            return;
        }
        outlineOf(*made, sound.peaks);
        install(track, lane, *made); // as recorded (or fitted), to the bit
        return;
    }
    Clip level;
    makeLevel(*made, edit, kSampleRate, level);
    outlineOf(level, sound.peaks);
    install(track, lane, level);
}

void Session::setLaneSample(int track, int lane, const std::string& name) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return;
    TrackData& t = project.tracks[track];
    std::snprintf(t.laneSample[lane], sizeof t.laneSample[lane], "%s", name.c_str());
    // Another sound: START and END were parts of the last one, and go back to the whole. The rest of
    // the lane's edits stay. A synth voice goes with it (a recording or the drum in its place). All of
    // it is one step of undo with the name (soundMark sees the name: gesture audit G04), so undo puts
    // the old sound back with its own cut and voice, never the old cut on the new sound.
    LaneEdit& edit = t.laneEdit[lane];
    edit.start = 0;
    edit.end = kEditUnits;
    t.laneSynth[lane] = drumsynth::Voice();
    // Its own cuts were the last sound's: EVEN again (SLICES AT THE HITS; in the same step of undo, as the cut).
    setLaneCuts(track, lane, LaneCuts());
    loadLane(track, lane);
}

void Session::setLaneSynth(int track, int lane, const drumsynth::Voice& wanted) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return;
    TrackData& t = project.tracks[track];
    const drumsynth::Voice now = drumsynth::voiceHeld(wanted), was = t.laneSynth[lane];
    if (now == was && (now.on() || !t.laneSample[lane][0])) return;
    t.laneSynth[lane] = now;
    t.laneSample[lane][0] = 0; // a synth, or the built-in drum: no recording
    setLaneCuts(track, lane, LaneCuts()); // (no recording, no slices: SLICES AT THE HITS)
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
    const std::shared_ptr<const Clip>& made = sound.fitted ? sound.fitted : sound.shaped;
    int gain = 0;
    if (!made || !normalGain(*made, gain)) return false;
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
    addressAsked_ = false;        // another port and PIN: the address is asked again at once
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
    if (!transfer_.hasLettering()) { // the pages' lettering: read from the card once, the first time the page goes on
        const std::vector<unsigned char> ttf = loadFontFile();
        transfer_.setLettering(std::string(ttf.begin(), ttf.end()));
    }
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
    keepSettingsNow(); // (what is owed to the folder before)
    settingsKept_.clear(), volumeKept_.clear(); // what this one has is not known yet (loadSettings, loadVolume)
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

std::string Session::recordingLine() const {
    char text[64] = {};
    if (rec_ == Rec::Waiting) std::snprintf(text, sizeof text, "WAITING FOR SOUND...  R1 TO CANCEL");
    else if (rec_ == Rec::Recording) std::snprintf(text, sizeof text, "RECORDING  %.1f S OF %d  R1 TO STOP", static_cast<double>(recordSeconds()), static_cast<int>(Recorder::kMaxSeconds));
    return text;
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

// A multisample (sampler.hpp): the files beside the picked one that spell notes in their names (sampleNote), each
// round its own note, and the layers of a note (sampleLayer: "soft", "ff", "v2"), each played by its share of the
// velocities, softest first. A file that spells no note but a layer, and is the picked one's name otherwise
// ("Snare hard" beside "Snare soft", sampleLayerStem), is a layer of the picked one's note. Null where that is the
// picked one alone; else `main` has been moved into it. The picked one plays at its spelled note, or where ROOT says
// where it spells none (its root is 60, which ROOT moves as it moves the set). At most kMaxZoneNotes notes, the
// nearest to the picked one's; as many layers on each as kMaxZones leaves room for, up to kMaxLayers, spread from
// the softest to the loudest; the picked one always among them.
std::unique_ptr<sampler::SampleZoneSet> Session::multisampleOf(const std::string& name, Clip& main) {
    const std::string folder = sampleFolderOf(name), stem = sampleLayerStem(name);
    const int mainNote = sampleNote(name), mainRoot = mainNote >= 0 ? mainNote : 60;
    // A layer's place among a note's: its number; none spelled sits with MF (between SOFT and HARD).
    const auto layerOf = [](const std::string& n) {
        const int layer = sampleLayer(n);
        return layer >= 0 ? layer : 5;
    };
    struct Candidate {
        std::string name;
        int note, layer;
    };
    std::vector<Candidate> files{{name, mainRoot, layerOf(name)}}; // the picked one first: it wins a tie
    for (const std::string& have : sampleNames()) {
        if (have == name || sampleFolderOf(have) != folder) continue;
        const int note = sampleNote(have);
        if (note >= 0 && note <= 127) files.push_back({have, note, layerOf(have)});
        else if (note < 0 && mainNote < 0 && sampleLayer(have) >= 0 && sampleLayerStem(have) == stem)
            files.push_back({have, mainRoot, layerOf(have)});
    }
    // By note, a note's layers softest first; one note and layer twice keeps the first (the picked one, else the
    // first by name).
    std::stable_sort(files.begin() + 1, files.end(), [](const Candidate& a, const Candidate& b) { return a.name < b.name; });
    std::stable_sort(files.begin(), files.end(), [](const Candidate& a, const Candidate& b) {
        return a.note != b.note ? a.note < b.note : a.layer < b.layer;
    });
    std::vector<std::vector<Candidate>> notes; // each note's layers
    for (const Candidate& c : files) {
        if (!notes.empty() && notes.back().front().note == c.note) {
            if (notes.back().back().layer != c.layer) notes.back().push_back(c); // (the picked one sorts first of a pair)
        } else {
            notes.push_back({c});
        }
    }
    if (notes.size() == 1 && notes.front().size() == 1) return nullptr; // the picked one alone
    // The nearest notes to the picked one's (a lower one first where two are as near), then by note again.
    std::stable_sort(notes.begin(), notes.end(), [&](const std::vector<Candidate>& a, const std::vector<Candidate>& b) {
        const int da = std::abs(a.front().note - mainRoot), db = std::abs(b.front().note - mainRoot);
        return da != db ? da < db : a.front().note < b.front().note;
    });
    if (notes.size() > static_cast<std::size_t>(sampler::kMaxZoneNotes)) notes.resize(sampler::kMaxZoneNotes);
    std::stable_sort(notes.begin(), notes.end(), [](const std::vector<Candidate>& a, const std::vector<Candidate>& b) { return a.front().note < b.front().note; });
    int room = sampler::kMaxZones / static_cast<int>(notes.size());
    room = room < 1 ? 1 : (room > sampler::kMaxLayers ? sampler::kMaxLayers : room);
    std::unique_ptr<sampler::SampleZoneSet> set(new sampler::SampleZoneSet);
    for (std::vector<Candidate>& layers : notes) {
        const int n = static_cast<int>(layers.size());
        if (n > room) { // spread from the softest to the loudest, the picked one kept in place of the nearest to it
            std::vector<Candidate> kept;
            for (int k = 0; k < room; ++k) kept.push_back(layers[static_cast<std::size_t>(room == 1 ? n - 1 : (k * (n - 1) + (room - 1) / 2) / (room - 1))]);
            for (int k = 0; k < n; ++k) {
                if (layers[static_cast<std::size_t>(k)].name != name) continue;
                std::size_t nearest = 0;
                for (std::size_t q = 1; q < kept.size(); ++q)
                    if (std::abs(kept[q].layer - layers[static_cast<std::size_t>(k)].layer) < std::abs(kept[nearest].layer - layers[static_cast<std::size_t>(k)].layer)) nearest = q;
                kept[nearest] = layers[static_cast<std::size_t>(k)];
            }
            layers = std::move(kept);
        }
        std::vector<sampler::SampleZone> read;
        for (const Candidate& c : layers) {
            Clip data;
            if (c.name == name) data = std::move(main); // (given back below if it ends up alone)
            else if (!readRecording(sampleFolder_, c.name, data) || data.empty()) continue; // gone or broken: skipped
            read.push_back(sampler::SampleZone());
            if (data.stereo()) read.back().right = std::move(data.right); // STEREO: each note as its file has it
            read.back().data = std::move(data.left());
            read.back().root = c.note;
        }
        for (std::size_t k = 0; k < read.size(); ++k) {
            read[k].top = k + 1 == read.size() ? 1.f : static_cast<float>(k + 1) / static_cast<float>(read.size()); // an even share each
            set->zones.push_back(std::move(read[k]));
        }
    }
    if (set->zones.size() < 2) { // one after all (nothing readable beside it): the picked one alone, given back
        if (!set->zones.empty()) main = Clip(std::move(set->zones.front().data), std::move(set->zones.front().right));
        return nullptr;
    }
    // Each note's keys reach halfway to its neighbours'; its layers share them.
    std::vector<int> roots;
    for (const sampler::SampleZone& z : set->zones)
        if (roots.empty() || roots.back() != z.root) roots.push_back(z.root);
    for (sampler::SampleZone& z : set->zones) {
        const std::size_t i = static_cast<std::size_t>(std::find(roots.begin(), roots.end(), z.root) - roots.begin());
        z.lo = i == 0 ? 0 : (roots[i - 1] + z.root) / 2 + 1;
        z.hi = i + 1 == roots.size() ? 127 : (z.root + roots[i + 1]) / 2;
    }
    return set;
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
    Clip main;
    if (sampleFolder_.empty() || !readRecording(sampleFolder_, name, main) || main.empty()) {
        sound.missing = true;
        engine.setTrackSample(track, nullptr);
        return;
    }
    sound.seconds = static_cast<float>(main.size()) / kSampleRate;
    outlineOf(main, sound.peaks);
    if (project.tracks[track].kind == TrackKind::Sampler) {
        std::unique_ptr<sampler::SampleZoneSet> set = multisampleOf(name, main);
        if (set) {
            sound.zones = static_cast<int>(set->zones.size());
            sound.layers = 1;
            for (std::size_t i = 0, run = 1; i < set->zones.size(); ++i) {
                run = i > 0 && set->zones[i].root == set->zones[i - 1].root ? run + 1 : 1;
                if (static_cast<int>(run) > sound.layers) sound.layers = static_cast<int>(run);
            }
            engine.setTrackZones(track, std::move(set));
            return;
        }
    }
    if (project.tracks[track].kind == TrackKind::Sampler) {
        // One recording: EDIT works on it (S4). Kept as it came off the card, and made from that.
        sound.recording = sound.source = std::make_shared<const Clip>(std::move(main));
        sound.sourceSeconds = sound.seconds;
        sound.sourcePeaks = sound.peaks;
        shapeSampler(track);
        return;
    }
    std::unique_ptr<SampleData> data(new SampleData); // (a CLOUD: the file as it is, stereo or not)
    if (main.stereo()) data->right = std::move(main.right);
    data->data = std::move(main.left());
    engine.setTrackSample(track, std::move(data));
}

void Session::shapeSampler(int track) {
    LaneSound& sound = sounds_[track][0];
    if (!sound.source) return;
    const LaneEdit& edit = project.tracks[track].samplerEdit;
    sound.shaped = edit.shapes() ? std::make_shared<const Clip>(makeShape(*sound.source, edit, kSampleRate)) : sound.source;
    installSampler(track);
}

void Session::installSampler(int track) {
    LaneSound& sound = sounds_[track][0];
    if (!sound.shaped) return;
    const LaneEdit& edit = project.tracks[track].samplerEdit;
    std::unique_ptr<SampleData> data(new SampleData);
    if (edit.levelAtRest()) {
        data->data = sound.shaped->left(); // the file as it is, to the bit (MONO alone: its sides mixed)
        if (sound.shaped->stereo()) data->right = sound.shaped->right;
    } else {
        Clip level;
        makeLevel(*sound.shaped, edit, kSampleRate, level);
        if (level.stereo()) data->right = std::move(level.right);
        data->data = std::move(level.left());
    }
    sound.seconds = static_cast<float>(data->data.size()) / kSampleRate;
    outlineOf(*data, sound.peaks);
    engine.setTrackSample(track, std::move(data));
}

bool Session::samplerEdits(int track) const {
    return track >= 0 && track < kNumTracks && project.tracks[track].kind == TrackKind::Sampler && sounds_[track][0].source != nullptr;
}

void Session::setSamplerEdit(int track, const LaneEdit& wanted) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Sampler) return;
    LaneEdit& edit = project.tracks[track].samplerEdit;
    LaneEdit now = laneEditHeld(wanted, sounds_[track][0].sourceSeconds);
    now.pitch = now.fine = 0; // (the page's PITCH and TUNE do that)
    const LaneEdit was = edit;
    if (now == was) return;
    edit = now;
    LaneEdit shapeWas = was, shapeNow = now; // what is cut or turned round: made again
    shapeWas.gain = shapeNow.gain = shapeWas.fadeIn = shapeNow.fadeIn = shapeWas.fadeOut = shapeNow.fadeOut = 0;
    if (shapeWas != shapeNow) shapeSampler(track);
    else installSampler(track); // the fades and GAIN alone: on what is made already
    ++sounds_[track][0].version; // (its picture changed: the designer page draws it again)
}

bool Session::normalizeSampler(int track) {
    if (!samplerEdits(track)) return false;
    const LaneSound& sound = sounds_[track][0];
    int gain = 0;
    if (!sound.shaped || !normalGain(*sound.shaped, gain)) return false;
    LaneEdit edit = project.tracks[track].samplerEdit;
    edit.gain = gain;
    setSamplerEdit(track, edit);
    return true;
}

bool Session::trimSamplerSilence(int track) {
    if (!samplerEdits(track)) return false;
    LaneEdit edit = project.tracks[track].samplerEdit;
    if (!silenceEnds(*sounds_[track][0].source, kSampleRate, edit.start, edit.end)) return false;
    setSamplerEdit(track, edit);
    return true;
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
    // Another recording: EDIT's START and END were parts of the last one, and go back to the whole; the rest stay
    // (a lane's rule). One step of undo with the name, as a lane's.
    project.tracks[track].samplerEdit.start = 0;
    project.tracks[track].samplerEdit.end = kEditUnits;
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
    const int from = catchFrom();
    const Engine::Caught caught = engine.lastBars(catchBars_, from);
    caughtBars_ = caught.bars, caughtFrom_ = from;
    // One track with no whole bar heard since it was chosen: what there is is only that since (a step, or half a
    // bar), not a bar; caught, it was fitted to a step or two and said as a bar (review fix, 2026-11-25). Nothing,
    // and the line says to let a bar go by. (The mix is always heard: no whole bar there is a bar's worth, as ever.)
    caughtNoBar_ = from >= 0 && caught.bars == 0;
    if (caughtNoBar_) return "";
    Clip bar(caught.sound, caught.right); // STEREO: both sides, as heard (a WAV of two channels)...
    bar.oneIfSame();                      // ...or one, where nothing was off the middle (the same sound, half the file)
    const int barSteps = caught.steps;
    float peak = 0.f;
    for (float v : bar) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    for (float v : bar.right) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
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
        setLaneFit(track, lane, barSteps); // it is its bars' steps long (12 a bar in 3/4: review 2026-11-23, it was always 16), and stays so when the tempo moves
    }
    return name;
}

void Session::stepCatchBars(int by) {
    static const int stops[3] = {1, 2, 4};
    int at = catchBars_ >= 4 ? 2 : catchBars_ - 1;
    at += by > 0 ? 1 : (by < 0 ? -1 : 0);
    catchBars_ = stops[at < 0 ? 0 : (at > 2 ? 2 : at)];
}

void Session::stepCatchFrom(int by) {
    const int to = catchFrom() + (by > 0 ? 1 : (by < 0 ? -1 : 0));
    catchFrom_ = to < -1 ? -1 : (to >= project.trackCount() ? project.trackCount() - 1 : to);
    engine.setCatchTrack(catchFrom()); // (and every frame, update)
}

namespace {
// "breaks/amen" and 2: "breaks/amen 2", its name cut where the whole would not be a name (sampleNameOk: under 64).
std::string numbered(const std::string& folder, std::string stem, int number) {
    const std::string tail = " " + std::to_string(number), head = folder.empty() ? "" : folder + "/";
    const std::size_t room = static_cast<std::size_t>(kSampleNameRoom - 1) - head.size() - tail.size();
    if (stem.size() > room) stem.resize(room);
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    return head + (stem.empty() ? "sound" : stem) + tail;
}
}

Session::SaveSound Session::saveLaneSound(int track, int lane, float start, float part, std::string& name) {
    name.clear();
    if (sampleFolder_.empty() || track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return SaveSound::Failed;
    readyLane(track, lane);
    const TrackData& t = project.tracks[track];
    const LaneSound& sound = sounds_[track][lane];
    if (sound.missing) return SaveSound::Missing;
    // What the engine plays is still on its way (a shape or a voice on the helper thread, a FIT for this tempo or
    // this length not made yet): saved now, it would be what the lane played before.
    const int fit = t.laneFit[lane];
    const bool fitOwed = sound.recording && fit > 0 && (!sound.fitted || sound.fittedTempo != project.tempo || sound.fittedSteps != fit);
    if (sound.waiting || sound.shaping || sound.making || fitOwed) return SaveSound::Making;
    const std::shared_ptr<const Clip>& made = sound.fitted ? sound.fitted : sound.shaped;
    if (!made || made->empty()) return SaveSound::Failed;
    // As installLane hands it to the engine: at rest the sound itself (the built-in drum's is the kit's), else
    // with its fades, GAIN and guard. STEREO: as it plays, two sides or one (MONO mixed).
    Clip plays;
    if (t.laneEdit[lane].levelAtRest()) plays = *made;
    else makeLevel(*made, t.laneEdit[lane], kSampleRate, plays);
    // A part of it, as DrumMachine::trigger takes one: from start * length, part * length long.
    const int length = static_cast<int>(plays.size());
    if (!(start > 0.f)) start = 0.f;
    const int from = start < 1.f ? static_cast<int>(start * static_cast<float>(length)) : length;
    int end = part > 0.f && part < 1.f ? from + static_cast<int>(part * static_cast<float>(length)) : length;
    if (end > length) end = length;
    if (from >= end) return SaveSound::Failed;
    Clip out(std::vector<float>(plays.begin() + from, plays.begin() + end),
             plays.stereo() ? std::vector<float>(plays.right.begin() + from, plays.right.begin() + end) : std::vector<float>());
    if (end < length) { // cut short: it ends with the quick fade the engine ends a part with, so the file never clicks
        const int fade = std::min(static_cast<int>(kEdgeMs * kSampleRate / 1000.f), static_cast<int>(out.size()));
        for (int i = 0; i < fade; ++i) {
            const float g = static_cast<float>(i) / static_cast<float>(fade);
            out[out.size() - 1 - static_cast<std::size_t>(i)] *= g;
            if (out.stereo()) out.right[out.size() - 1 - static_cast<std::size_t>(i)] *= g;
        }
    }
    // Its name: after its source, in its folder; a voice or the built-in drum in the samples folder.
    std::string folder, stem;
    if (t.laneSample[lane][0]) folder = sampleFolderOf(t.laneSample[lane]), stem = sampleLabel(t.laneSample[lane]);
    else stem = t.laneSynth[lane].on() ? drumsynth::modelName(t.laneSynth[lane].model) : DrumKit::laneName(lane);
    for (char& c : stem) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); // (a model's or a drum's name is in capitals)
    std::vector<std::string> taken = sampleNames(); // capitals not counted: a card may not tell them apart
    for (std::string& have : taken) have = lowered(have);
    for (int number = 1; number < 10000; ++number) {
        const std::string candidate = numbered(folder, stem, number);
        if (!sampleNameOk(candidate) || recordingThere(sampleFolder_, candidate)) continue;
        if (std::find(taken.begin(), taken.end(), lowered(candidate)) != taken.end()) continue;
        if (!writeWav(sampleFolder_ + "/" + candidate + ".wav", out, static_cast<int>(kSampleRate))) return SaveSound::Failed;
        name = candidate;
        return SaveSound::Saved;
    }
    return SaveSound::Failed;
}

std::string Session::freeSampleName(const std::string& folder, const std::string& stem) const {
    if (sampleFolder_.empty()) return "";
    std::vector<std::string> taken = sampleNames(); // capitals not counted: a card may not tell them apart
    for (std::string& have : taken) have = lowered(have);
    for (int number = 1; number < 10000; ++number) {
        const std::string candidate = numbered(folder, lowered(stem), number);
        if (!sampleNameOk(candidate) || recordingThere(sampleFolder_, candidate)) continue;
        if (std::find(taken.begin(), taken.end(), lowered(candidate)) != taken.end()) continue;
        return candidate;
    }
    return "";
}

std::string Session::savedLine(SaveSound how, const std::string& name) {
    std::string said;
    if (how == SaveSound::Saved) said = "SAVED \"" + sampleLabel(name) + "\""; // (in its source's folder: the name is enough)
    else if (how == SaveSound::Missing) said = "NOT SAVED: ITS FILE IS NOT FOUND";
    else if (how == SaveSound::Making) said = "NOT SAVED: IT IS STILL BEING MADE. AGAIN IN A MOMENT";
    else said = "NOT SAVED: THE CARD REFUSED IT";
    for (char& c : said) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return said;
}

int Session::evenSlices(int track, int lane) const {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes) return kSteps;
    // A slice a step of the fitted sound once FIT is over half a bar of 16: a 3/4 bar (12) is cut into 12, not 16
    // (integration review, 2026-11-25: at 16 a 3/4 break painted in order never reached slices 13 to 16). Under
    // that, 16, as it always was.
    const int fit = project.tracks[track].laneFit[lane];
    return fit > kSteps / 2 ? fit : kSteps;
}

int Session::laneSlices(int track, int lane) const {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes) return kSteps;
    return slices::count(project.tracks[track].laneCuts[lane], evenSlices(track, lane));
}

// ---- SLICES AT THE HITS (batch S2; slice_rule.hpp) ----------------------------------------------

namespace {
bool laneThere(int track, int lane) { return track >= 0 && track < kNumTracks && lane >= 0 && lane < kDrumLanes; }
// The hits in a sound, found on its two sides mixed to one where it has two (STEREO: one set of cuts for both).
std::vector<slices::Found> hitsIn(const Clip& sound) {
    if (!sound.stereo()) return slices::findHits(sound.data(), static_cast<int>(sound.size()), kSampleRate);
    const std::vector<float> mixed = sound.mono();
    return slices::findHits(mixed.data(), static_cast<int>(mixed.size()), kSampleRate);
}
} // namespace

float Session::laneCutAt(int track, int lane, int k) const {
    if (!laneThere(track, lane)) return 0.f;
    return slices::cutStart(project.tracks[track].laneCuts[lane], evenSlices(track, lane), k);
}

int Session::laneSliceAt(int track, int lane, float start) const {
    if (!laneThere(track, lane)) return 0;
    return slices::sliceAt(project.tracks[track].laneCuts[lane], evenSlices(track, lane), start);
}

float Session::lanePartAt(int track, int lane, float start) const {
    if (!laneThere(track, lane)) return 1.f / kSteps;
    return slices::partAt(project.tracks[track].laneCuts[lane], evenSlices(track, lane), start);
}

float Session::laneCutStep(int track, int lane, float start, int by) const {
    if (!laneThere(track, lane)) return start;
    return slices::stepCut(project.tracks[track].laneCuts[lane], evenSlices(track, lane), start, by);
}

// The cuts to the engine, three a command (Command::SetLaneCuts); none is one command saying so.
void Session::sendLaneCuts(int track, int lane) {
    const LaneCuts& cuts = project.tracks[track].laneCuts[lane];
    Command c;
    c.type = Command::SetLaneCuts;
    c.track = track;
    c.a = lane;
    c.c = cuts.count;
    if (cuts.count == 0) {
        engine.send(c);
        return;
    }
    for (int from = 0; from < cuts.count; from += 3) {
        c.b = from;
        c.d = cuts.count - from < 3 ? cuts.count - from : 3;
        c.value = cuts.at[from];
        c.extra = c.d > 1 ? cuts.at[from + 1] : 0.f;
        c.shift = c.d > 2 ? cuts.at[from + 2] : 0.f;
        engine.send(c);
    }
}

void Session::setLaneCuts(int track, int lane, const LaneCuts& cuts) {
    if (!laneThere(track, lane) || project.tracks[track].kind != TrackKind::Drums) return;
    const LaneCuts held = slices::cutsHeld(cuts); // (rising, the rest 0: alike byte for byte)
    LaneCuts& now = project.tracks[track].laneCuts[lane];
    if (now == held && std::memcmp(&now, &held, sizeof held) == 0) return;
    now = held;
    sendLaneCuts(track, lane);
    sendLanePart(track, lane); // ONE SLICE's share goes by how many there are
}

void Session::moveLaneHits(int t, int lane, const std::function<float(int, int, int, float)>& to) {
    TrackData& track = project.tracks[t];
    struct Moved {
        int scene, bar, step;
        float start;
    };
    std::vector<Moved> moved;
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        if (!track.drums.has(scene)) continue; // (a scene with nothing written: no hit, and no piece made for it)
        for (int bar = 0; bar < kMaxBars; ++bar) {
            DrumPattern& p = track.drums[scene][bar];
            for (int step = 0; step < kSteps; ++step) {
                if (!(p.level[lane][step] > 0.f)) continue;
                const float was = p.start[lane][step], now = to(scene, bar, step, was);
                if (now == was) continue;
                p.start[lane][step] = now;
                moved.push_back({scene, bar, step, now});
            }
        }
    }
    if (moved.empty()) return;
    noteEdit();
    if (moved.size() > 64) { // (a command a hit could fill the queue: the track's image, once)
        sendTrackImage(t);
        return;
    }
    for (const Moved& m : moved) {
        Command c;
        c.type = Command::SetStepStart;
        c.track = t;
        c.scene = m.scene;
        c.bar = m.bar;
        c.a = lane;
        c.b = m.step;
        c.value = m.start;
        engine.send(c);
    }
}

// The lane's hits as found in the shape it plays now: kept, or looked at now when `atOnce` and short enough
// for a frame (kFindInAFrame), or when `atOnce` is "whatever it takes".
const std::vector<slices::Found>* Session::foundHits(int track, int lane, bool atOnce) {
    LaneSound& sound = sounds_[track][lane];
    if (!sound.shaped || sound.shaping) return nullptr;
    if (sound.hitsOf == sound.shaped) return &sound.hits;
    if (hits_ && hits_->track == track && hits_->lane == lane) return nullptr; // (on its way)
    if (!atOnce || static_cast<int>(sound.shaped->size()) > kFindInAFrame) return nullptr;
    sound.hits = hitsIn(*sound.shaped);
    sound.hitsOf = sound.shaped;
    return &sound.hits;
}

bool Session::findingHits(int track, int lane) const { return hits_ && hits_->track == track && hits_->lane == lane; }

int Session::senseShown(int track, int lane) const {
    if (findingHits(track, lane)) return hitsSense_;
    return laneThere(track, lane) ? project.tracks[track].laneCuts[lane].sense : 0;
}

Session::Cut Session::cutLaneAtHits(int track, int lane, int sense) {
    if (!laneThere(track, lane) || project.tracks[track].kind != TrackKind::Drums) return Cut::NoSound;
    const LaneSound& sound = sounds_[track][lane];
    if (!sound.recording || sound.missing || !sound.shaped || sound.shaping) return Cut::NoSound;
    sense = sense < 0 ? 0 : (sense > kSenseMost ? kSenseMost : sense);
    // EVEN asked while this lane's hits are still being found: the job, when it lands, only keeps the hits (review
    // fix, 2026-11-25: it put on the SENSITIVITY asked before, over the EVEN asked after).
    if (sense == 0 && findingHits(track, lane)) hitsSense_ = 0;
    const LaneCuts was = project.tracks[track].laneCuts[lane];
    const int even = evenSlices(track, lane);
    LaneCuts now; // EVEN
    if (sense > 0) {
        if (hits_ && !(hits_->track == track && hits_->lane == lane)) finishHits(true); // (another lane's on its way: let it land first)
        const std::vector<slices::Found>* found = foundHits(track, lane, true);
        if (!found) {
            // Too long for a frame: looked at on a thread of its own, and put on when it lands (finishHits).
            hitsSense_ = sense;
            if (!hits_) {
                hits_.reset(new HitsJob);
                HitsJob* job = hits_.get();
                job->track = track;
                job->lane = lane;
                job->of = sound.shaped;
                job->thread = std::thread([job] {
                    job->found = hitsIn(*job->of);
                    job->done.store(true);
                });
            }
            return Cut::Finding;
        }
        now = slices::cutsAt(*found, static_cast<int>(sound.shaped->size()), sense, kSampleRate);
    }
    if (now == was) return Cut::Same;
    // A hit in order stays in order (a break painted in order: each step its own slice, now on a hit); any other
    // to the cut at or before where it starts (it plays from the hit it was in). In order is by the hit's place in
    // the lane's FIT (`even` steps: a bar, or 2 or 4), so every bar painted in order is the same bar: the whole
    // break hit at the top of bar 2, 3 or 4 is slice 1 there, not slice 17, 33 or 49 (review fix, 2026-11-25:
    // round past the count, those came to a cut in the middle of the break).
    // Counted in the scene's meter (a 3/4 bar is 12 steps: bar 2 of a 2-bar break at FIT 24 is slices 13 to 24),
    // and a FILL by the bar it plays instead of, as the slice view lays it out (PatternScreen::ownStart; integration
    // review, 2026-11-25: the stored eighth bar put a 1-bar cell's FILL at FIT 32 on slices 17 to 32).
    moveLaneHits(track, lane, [&](int scene, int bar, int step, float start) {
        const int ownBar = project.ownBarOf(track, scene, bar);
        const int own = (ownBar * project.meterOf(scene) + step) % even;
        if (std::fabs(start - slices::cutStart(was, even, own)) <= slices::kHair) return slices::cutStart(now, even, own);
        return slices::cutStart(now, even, slices::sliceAt(now, even, start));
    });
    setLaneCuts(track, lane, now);
    return Cut::Done;
}

void Session::finishHits(bool wait) {
    if (!hits_ || (!wait && !hits_->done.load())) return;
    if (hits_->thread.joinable()) hits_->thread.join();
    std::unique_ptr<HitsJob> job = std::move(hits_);
    LaneSound& sound = sounds_[job->track][job->lane];
    if (sound.shaped != job->of) return; // the lane has moved on: looked at again when asked
    sound.hits = std::move(job->found);
    sound.hitsOf = job->of;
    const int sense = hitsSense_;
    hitsSense_ = 0;
    if (sense > 0) cutLaneAtHits(job->track, job->lane, sense); // (0: EVEN or an undo came after the ask: kept only)
}

Session::Cut Session::moveLaneCut(int track, int lane, int k, float to) {
    if (!laneThere(track, lane) || project.tracks[track].kind != TrackKind::Drums) return Cut::NoSound;
    const LaneSound& sound = sounds_[track][lane];
    if (!(sound.seconds > 0.f)) return Cut::NoSound;
    LaneCuts own = slices::ownCuts(project.tracks[track].laneCuts[lane], evenSlices(track, lane));
    if (k <= 0) return Cut::Top;
    if (k >= own.count) return Cut::NoRoom;
    const float gap = static_cast<float>(slices::kHandGapSeconds / sound.seconds);
    const float low = own.at[k - 1] + gap, high = (k + 1 < own.count ? own.at[k + 1] : 1.f) - gap;
    if (!(low <= high)) return Cut::NoRoom;
    to = to < low ? low : (to > high ? high : to);
    const float was = own.at[k];
    if (to == was) return Cut::Same;
    own.at[k] = to;
    moveLaneHits(track, lane, [&](int, int, int, float start) { return std::fabs(start - was) <= slices::kHair ? to : start; });
    setLaneCuts(track, lane, own);
    return Cut::Done;
}

Session::Cut Session::addLaneCut(int track, int lane, int k) {
    if (!laneThere(track, lane) || project.tracks[track].kind != TrackKind::Drums) return Cut::NoSound;
    const LaneSound& sound = sounds_[track][lane];
    if (!(sound.seconds > 0.f) || !sound.shaped) return Cut::NoSound;
    LaneCuts own = slices::ownCuts(project.tracks[track].laneCuts[lane], evenSlices(track, lane));
    if (own.count >= kMaxLaneCuts) return Cut::Full;
    if (k < 0 || k >= own.count) return Cut::NoRoom;
    const float from = own.at[k], to = k + 1 < own.count ? own.at[k + 1] : 1.f;
    const float gap = static_cast<float>(slices::kHandGapSeconds / sound.seconds);
    if (!(to - from > 2.f * gap)) return Cut::NoRoom;
    // At the strongest hit found inside the slice (any SENSITIVITY: what a cut at the hits passed by), else its middle.
    float at = -1.f;
    if (const std::vector<slices::Found>* found = foundHits(track, lane, true)) at = slices::strongestBetween(*found, static_cast<int>(sound.shaped->size()), from, to, kSampleRate);
    if (at < 0.f) at = (from + to) / 2.f;
    for (int i = own.count; i > k + 1; --i) own.at[i] = own.at[i - 1];
    own.at[k + 1] = at;
    ++own.count;
    setLaneCuts(track, lane, own);
    return Cut::Done;
}

Session::Cut Session::deleteLaneCut(int track, int lane, int k) {
    if (!laneThere(track, lane) || project.tracks[track].kind != TrackKind::Drums) return Cut::NoSound;
    LaneCuts own = slices::ownCuts(project.tracks[track].laneCuts[lane], evenSlices(track, lane));
    if (k <= 0) return Cut::Top;
    if (k >= own.count) return Cut::NoRoom;
    const float was = own.at[k], before = own.at[k - 1];
    for (int i = k; i + 1 < own.count; ++i) own.at[i] = own.at[i + 1];
    own.at[--own.count] = 0.f;
    moveLaneHits(track, lane, [&](int, int, int, float start) { return std::fabs(start - was) <= slices::kHair ? before : start; });
    setLaneCuts(track, lane, own);
    return Cut::Done;
}

void Session::sendLanePart(int track, int lane) {
    Command c;
    c.type = Command::SetLanePart;
    c.track = track;
    c.a = lane;
    c.value = project.tracks[track].laneOneSlice[lane] ? 1.f / static_cast<float>(laneSlices(track, lane)) : 0.f; // (with cuts of its own: above 0 says ONE SLICE, the engine ends it at the next cut)
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

bool Session::setTempoFromLoop(int track, int lane, float bpm, int bars) {
    if (track < 0 || track >= kNumTracks || lane < 0 || lane >= kDrumLanes || project.tracks[track].kind != TrackKind::Drums) return false;
    if (!(bpm >= kMinTempo && bpm <= kMaxTempo) || bars < 1) return false;
    const int fit = bars * project.meterOf(project.scene);
    if (bpm == project.tempo && project.tracks[track].laneFit[lane] == fit) return true; // (as it is: no step)
    // Its own step: what was being edited just before is a step already, and nothing after joins this one. The
    // step keeps the tempo to put back (no other tempo change is undone: songs-and-undo.md); only FIT moving,
    // it keeps none, so its undo leaves the tempo where it is then.
    beginOwnUndoStep();
    if (undoReady_ && !undo_.empty() && bpm != project.tempo) undo_.back().tempo = project.tempo;
    setTempo(bpm);
    setLaneFit(track, lane, fit);
    endOwnUndoStep();
    return true;
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
            job->out = Clip(drumsynth::make(job->voice, kSampleRate));
            job->done.store(true);
            return;
        }
        if (job->shape) job->shaped = makeShape(*job->in, job->edit, kSampleRate);
        const Clip& from = job->shape ? job->shaped : *job->in; // (STEREO: both sides fitted together)
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
            sound.shaped = std::make_shared<const Clip>(std::move(job_->shaped));
            sound.seconds = static_cast<float>(sound.shaped->size()) / kSampleRate;
            sound.fitted.reset();
            sound.fittedTempo = 0.f;
            sound.fittedSteps = 0;
        }
        if (job_->frames > 0) {
            sound.fittedTempo = job_->tempo;
            sound.fittedSteps = project.tracks[track].laneFit[lane];
            sound.fitted = std::make_shared<const Clip>(std::move(job_->out));
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
