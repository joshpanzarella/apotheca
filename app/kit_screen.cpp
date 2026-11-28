#include "kit_screen.hpp"
#include "drum_voices.hpp"
#include "samples.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace apotheca {

using namespace ui;

void laneFitText(int fit, int meter, char* text, std::size_t size) {
    if (meter < 1) meter = kSteps; // (as meterOf holds it)
    if (fit == 0) std::snprintf(text, size, "OFF");
    else if (fit % meter == 0) std::snprintf(text, size, "%d BAR%s", fit / meter, fit == meter ? "" : "S");
    else std::snprintf(text, size, "%d STEP%s", fit, fit == 1 ? "" : "S");
}

int laneFitFor(float seconds, float tempo, int meter) {
    if (!(seconds > 0.f) || !(tempo > 0.f) || meter <= 0) return 0;
    const double steps = static_cast<double>(seconds) * static_cast<double>(tempo) / 15.0;
    const int stops[] = {1, 2, 3, 4, 6, 8, 12, meter, 2 * meter, 4 * meter}; // (FIT's stops under a bar, then whole bars)
    const int under = 7;
    int best = 0;
    double bestOff = 0.0;
    for (int i = 0; i < under + 3; ++i) {
        if (i < under && stops[i] >= meter) continue;
        const double off = std::fabs(std::log(steps / stops[i]));
        if (best == 0 || off < bestOff) best = stops[i], bestOff = off;
    }
    return best;
}

float loopTempo(float seconds, float tempo, int meter, int by, int* bars) {
    if (!(seconds > 0.f) || meter <= 0) return 0.f;
    // A tempo the loop fills b bars at: b bars are b * meter steps of 15 / tempo seconds each. Whole, as the tempo is.
    float tempos[3];
    int counts[3], n = 0;
    for (int b : {1, 2, 4}) {
        const float t = std::round(static_cast<float>(b * meter) * 15.f / seconds);
        if (t >= kMinTempo && t <= kMaxTempo) tempos[n] = t, counts[n] = b, ++n;
    }
    if (n == 0) return 0.f;
    int at = -1, nearest = 0;
    for (int i = 0; i < n; ++i) {
        if (tempos[i] == tempo) at = i;
        if (std::fabs(std::log(tempos[i] / tempo)) < std::fabs(std::log(tempos[nearest] / tempo))) nearest = i;
    }
    int to = nearest;
    if (at >= 0 && by != 0) {
        to = at + (by > 0 ? 1 : -1);
        if (to < 0 || to >= n) return 0.f; // (the end: nothing changes)
    }
    if (bars) *bars = counts[to];
    return tempos[to];
}

const char* laneChokeText(int group) { return group == kChokeNone ? "NONE" : group == 1 ? "1" : "2"; }

// X held: it sounds until X is let go, or to its end (a tap: Session::kAuditionMs). Otherwise (a
// sound arrived on, by the list or A held) the whole of it, as a hit.
void KitScreen::hear(bool held) {
    hearOwed_ = false; // (whatever is heard now pays what a turn owed)
    ++hears_;
    // A line runs across the lane's picture while it sounds: for as long as the recording lasts
    // (a fitted one lasts its steps at the tempo), or until X is let go.
    const Session::LaneSound& sound = session_.laneSound(project_.track, lane_);
    const int fit = trk().laneFit[lane_];
    head_.start(lastMs_, fit > 0 && !sound.waiting ? static_cast<float>(fit) * 15.f / project_.tempo : sound.seconds);
    headLane_ = lane_;
    headHeld_ = held;
    if (held) {
        session_.hearLaneHeld(project_.track, lane_, kPlacedLevel, this);
        return;
    }
    Command c;
    c.type = Command::Trigger;
    c.track = project_.track;
    c.a = lane_;
    c.value = kPlacedLevel;
    session_.engine.send(c);
}

// The list of recordings: the preview, half of the recording at least (a fitted one: half its
// steps at the tempo), stopped as soon as the cursor moves on; X on it plays on to the end. The
// built-in drum is heard whole, as a tap of it always is (Session::letGoHeard).
void KitScreen::hearFromList(SamplePicker::Hear how) {
    if (how == SamplePicker::Hear::Held) return hear(true);
    if (how == SamplePicker::Hear::Quiet) {
        session_.stopHeard(this);
        if (headLane_ == lane_) head_.since = 0; // its line goes with it
        headHeld_ = false;
        return;
    }
    if (how == SamplePicker::Hear::Preview) hear(true); // begun as X begins it, and let go at once (below)
    const float whole = head_.wholeLength();
    const Uint32 ms = static_cast<Uint32>(1000.f * (how == SamplePicker::Hear::Preview ? whole / 2.f : whole));
    const Uint32 lasts = ms > Session::kAuditionMs ? ms : Session::kAuditionMs;
    session_.hearFor(lasts);
    headHeld_ = false;
    head_.lasts = how == SamplePicker::Hear::Preview ? static_cast<float>(lasts) / 1000.f : 0.f; // the line: to where it stops (Rest: to the end)
}

// The built-in drum in the middle: UP the recordings by name, DOWN the twelve synth models (KICK
// first, each at its usual knobs). So each is a press away, and the recordings are where they were.
void KitScreen::stepSound(int by) {
    if (session_.sampleFiles() != namesSeen_) { // the card changed mid-hold (WiFi): step through what is there now
        namesSeen_ = session_.sampleFiles();
        names_ = session_.sampleNames();
    }
    const int models = drumsynth::kModels - 1, count = static_cast<int>(names_.size());
    const drumsynth::Voice& voice = trk().laneSynth[lane_];
    int now = voice.on() ? -voice.model : 0; // -1 KICK ... -12 NOISE, 0 the drum, 1.. the recordings
    for (int i = 0; i < count && !voice.on(); ++i)
        if (names_[static_cast<std::size_t>(i)] == trk().laneSample[lane_]) now = i + 1;
    const int to = now + by;
    if (to < -models || to > count) return; // the ends stop
    if (to < 0) session_.setLaneSynth(project_.track, lane_, drumsynth::usualVoice(-to));
    else session_.setLaneSample(project_.track, lane_, to == 0 ? "" : names_[static_cast<std::size_t>(to - 1)]);
    if (!project_.playing) hear();
}

void KitScreen::stepFit(int by) {
    int now = 0;
    for (int i = 0; i < kLaneFitCount; ++i)
        if (kLaneFits[i] <= trk().laneFit[lane_]) now = i;
    // Between two stops (a tap's 12, 24 or 48 in 3/4, CATCH's 12): `now` is the stop below, so DOWN lands on it
    // (24 to 16, not 8) and UP on the stop above.
    const int to = by < 0 && kLaneFits[now] != trk().laneFit[lane_] ? now : now + by;
    if (to < 0 || to >= kLaneFitCount) return;
    session_.setLaneFit(project_.track, lane_, kLaneFits[to]);
}

// NONE, 1, 2: the ends stop.
void KitScreen::stepChoke(int by) {
    const int to = trk().laneChoke[lane_] + by;
    if (to < kChokeNone || to > kChokeGroups) return;
    session_.setLaneChoke(project_.track, lane_, to);
}

KitScreen::Source KitScreen::sourceOf(int lane) const {
    if (trk().laneSynth[lane].on()) return kSourceSynth;
    return trk().laneSample[lane][0] ? kSourceSample : kSourceBuiltIn;
}

const char* KitScreen::sourceText(Source source) { return source == kSourceSynth ? "SYNTH" : source == kSourceSample ? "SAMPLE" : "BUILT IN"; }

int KitScreen::sourceModel(int lane) {
    static const int models[kDrumLanes] = {drumsynth::kKick, drumsynth::kSnare, drumsynth::kClHat, drumsynth::kOpHat, drumsynth::kClap, drumsynth::kTom, drumsynth::kRim, drumsynth::kCymbal}; // (DrumKit::laneName's)
    return lane >= 0 && lane < kDrumLanes ? models[lane] : drumsynth::kKick;
}

// SOURCE with A held (owner, 2026-11-28): SYNTH, BUILT IN, SAMPLE, the ends stop. Each is a change of the lane's sound
// as SOUND's are, so one step of undo each, and B with A held puts back what A's press found. SAMPLE needs a file: the
// list of recordings opens, and A is spent (its let go is no tap; the list takes the buttons from here).
void KitScreen::stepSource(int by) {
    const int to = static_cast<int>(sourceOf(lane_)) + by;
    if (to < kSourceSynth || to > kSourceSample) return;
    if (to == kSourceSynth) session_.setLaneSynth(project_.track, lane_, drumsynth::usualVoice(sourceModel(lane_)));
    else if (to == kSourceBuiltIn) session_.setLaneSample(project_.track, lane_, "");
    if (to == kSourceSample) {
        holding_ = false;
        repeating_ = Button::None;
        saved_ = 0.f;
        sourcePushed_ = false;
        openPicker(true);
        return;
    }
    if (!project_.playing) hear();
}

void KitScreen::catchBar() {
    const std::string name = session_.catchLastBar(project_.track, lane_);
    said_ = caughtLine(session_, name);
    saidUntil_ = lastMs_ + 2500;
}

// Record from the input: once to start (it waits for sound), again to stop.
void KitScreen::record() {
    const Session::Rec now = session_.toggleRecording(project_.track, lane_);
    if (now == Session::Rec::NoInput) said_ = "NO INPUT FOUND: PLUG IN A USB MICROPHONE OR INTERFACE";
    else if (now == Session::Rec::Idle && session_.lastRecorded().empty()) said_ = "NOTHING RECORDED: NO SOUND CAME IN";
    saidUntil_ = lastMs_ + 3000;
}

void KitScreen::openPicker(bool bySource) {
    const int track = project_.track, lane = lane_;
    SamplePicker::Jobs jobs;
    jobs.use = [this, track, lane](const std::string& name) { session_.setLaneSample(track, lane, name); };
    jobs.hear = [this](SamplePicker::Hear how) { hearFromList(how); };
    jobs.sounding = [this] { return session_.hearingOn(this) && head_.at(lastMs_) >= 0.f; }; // (its line still running: not ended by itself)
    jobs.sound = [this, track, lane] { return &session_.laneSound(track, lane); };
    jobs.remove = [this](const std::string& name) { return session_.deleteSample(name); };
    jobs.move = [this](const std::string& from, const std::string& to) { return session_.moveSample(from, to); };
    jobs.makeFolder = [this](const std::string& name) { return session_.makeSampleFolder(name); };
    jobs.removeFolder = [this](const std::string& name) { return session_.deleteSampleFolder(name); };
    jobs.following = [this] { return session_.following(); };
    jobs.copying = [this] { return session_.carryCopying(); };
    jobs.copied = [this] { return session_.carryResult(); };
    jobs.followFailures = [this] { return session_.followFailures(); };
    jobs.playhead = [this] { return &head_; };
    jobs.catchBar = [this] { catchBar(); }; // (an empty list: as Y and R1 on the lane)
    jobs.record = [this] { record(); };
    // B: the lane's cut back with its recording. Every recording put on gives the lane the whole (setLaneSample),
    // the one it had as well when the cursor comes back to it, so the name alone lost the cut, and the sound's
    // mark then differed: a step of undo, and the redo steps gone (review of G04). Nothing is done where the cut
    // is as it was. A recording deleted meanwhile takes its cut with it: the drum goes back as it is.
    // (No voice to put back: on a DRUM SYNTH lane A's tap opens its knobs, never this list.)
    // Its own cuts likewise (SLICES AT THE HITS: every recording put on makes them EVEN).
    const LaneEdit edit = trk().laneEdit[lane_];
    const LaneCuts cuts = trk().laneCuts[lane_];
    const bool recorded = trk().laneSample[lane_][0] != 0;
    // Opened by SOURCE (A held, a synth voice perhaps stepped past on the way): B puts back what A's press found,
    // the voice too, as B with A held does (Session::putSoundBack: one step of undo, where it differs).
    jobs.putBack = [this, track, lane, edit, cuts, recorded, bySource](const std::string& original) {
        if (bySource && session_.putSoundBack(track)) return;
        if (original.empty() && recorded) return;
        if (project_.tracks[track].laneEdit[lane] != edit) session_.setLaneEdit(track, lane, edit);
        if (project_.tracks[track].laneCuts[lane] != cuts) session_.setLaneCuts(track, lane, cuts);
    };
    char title[48];
    std::snprintf(title, sizeof title, "LANE %d: PICK ITS SOUND", lane + 1);
    jobs.used = [this, track, lane](const std::string& name) { return session_.sampleUses(name) - (name == project_.tracks[track].laneSample[lane] ? 1 : 0); };
    picker_.open(session_.sampleNames(), session_.sampleFolders(), trk().laneSample[lane_], title, "THE BUILT-IN DRUM", jobs);
    sampleFilesSeen_ = session_.sampleFiles();
}

// The kit after or before in the list, put on at once. From a kit of the player's own making (in no
// list) UP is the first; the ends stop. After the kits, the samples folders as kits (batch S1: loadFolderKit),
// as they were when A went down (`folders_`).
void KitScreen::stepKit(int by) {
    if (session_.sampleFiles() != foldersSeen_) { // the card changed mid-hold (WiFi): step through what is there now
        foldersSeen_ = session_.sampleFiles();
        folders_ = session_.kitFolders();
    }
    const int kits = static_cast<int>(session_.kits().size()), folders = static_cast<int>(folders_.size());
    int now = session_.kitOf(project_.track);
    const std::string folder = now < 0 ? session_.folderKitOf(project_.track) : "";
    for (int i = 0; i < folders && !folder.empty(); ++i)
        if (folders_[static_cast<std::size_t>(i)] == folder) now = kits + i;
    const int to = now < 0 ? (by > 0 ? 0 : -1) : now + by;
    if (to < 0 || to >= kits + folders) return; // (the ends stop)
    if (to < kits ? !session_.loadKit(project_.track, to) : !session_.loadFolderKit(project_.track, folders_[static_cast<std::size_t>(to - kits)])) return;
    if (!project_.playing) hear(false); // (heard: its first lane, where the stop is reached from)
}

// B with A held on SOUND or the KIT stop (gesture audit G12): the track's sound as it was when A went down,
// heard as each step is. A is spent, as FX's and MOD's call-off: the D-pad and the stick are the cursor's
// again, and its let go is no tap (no list, no VOICE view).
// The stick still pushed then waits until it comes back to the middle (as a MOD let go): it was stepping the
// sounds, and would move the cursor off the stop at once.
void KitScreen::putBack(const Input& input) {
    if (session_.putSoundBack(project_.track) && !project_.playing) hear();
    holding_ = false;
    repeating_ = Button::None;
    saved_ = 0.f;
    stickWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
}

void KitScreen::callOffDelete() {
    kitDeleting_ = false;
    kitDeletePart_ = 0.f;
    deleting_ = bBack_ = false;
    deletePart_ = 0.f;
    edit_.callOffDelete();
    picker_.callOffDelete();
}

// A built-in drum, a DRUM SYNTH voice (a hit, not a loop: it has no laneSample) or a file not found has no
// length to fit by (and the bar keeps HOLD A fit to steps).
bool KitScreen::fitsByTap() const {
    return trk().laneSample[lane_][0] && !session_.laneSound(project_.track, lane_).missing;
}

// A tap of A on FIT (gesture audit G45): the bars what the lane plays unfitted (its cut) fills at the
// tempo of the scene being worked on (its own, else the song's), rounded to FIT's stops in its meter. A step
// of undo, as FIT is. (The scene's: a TRACK TO SAMPLES bounce is made at it, so a 4-bar bounce of a 90 BPM
// scene in a 120 BPM song is 4 bars, not 5 and a third rounded to 4 or 8: review fix, 2026-11-25.)
void KitScreen::fitToItsBars() {
    if (!fitsByTap()) return;
    const float own = project_.sceneTempo[project_.scene], tempo = own > 0.f ? own : project_.tempo;
    const int fit = laneFitFor(session_.laneSound(project_.track, lane_).seconds, tempo, project_.meterOf(project_.scene));
    if (fit > 0) session_.setLaneFit(project_.track, lane_, fit);
}

// Hold A + LEFT / RIGHT on FIT (batch S1, 2026-11-25): THE TEMPO FROM THIS LOOP. The song's tempo set so the lane's
// recording, as it plays unfitted (its cut), fills 1, 2 or 4 bars of the scene's meter: the nearest first, then
// RIGHT twice the bars (faster) and LEFT half (slower). FIT goes to those bars with it, so the loop is not
// stretched and stays in time if the tempo moves later. One step of undo, which puts the tempo back too.
void KitScreen::tempoFromLoop(int by) {
    if (!fitsByTap()) return; // (a built-in drum, a voice, a file not found: a hit, not a loop, as A's tap)
    const float seconds = session_.laneSound(project_.track, lane_).seconds;
    const int meter = project_.meterOf(project_.scene);
    // Steps from where the gesture has been only: the song at the nearest tempo but FIT not at its bars (a loop
    // at the song's tempo, loaded with FIT OFF) is not there yet, so the first press gives the nearest.
    int nearBars = 0;
    const float nearBpm = loopTempo(seconds, project_.tempo, meter, 0, &nearBars);
    const bool reached = nearBpm > 0.f && nearBpm == project_.tempo && trk().laneFit[lane_] == nearBars * meter;
    int bars = 0;
    float bpm = loopTempo(seconds, project_.tempo, meter, reached ? by : 0, &bars);
    if (bpm <= 0.f) bpm = loopTempo(seconds, project_.tempo, meter, 0, &bars); // at the end already: where it is (FIT to it)
    char text[80];
    if (bpm <= 0.f) {
        std::snprintf(text, sizeof text, "NO TEMPO FITS IT IN 1, 2 OR 4 BARS"); // (from kMinTempo to kMaxTempo: the words fit the heads' room)
    } else {
        session_.setTempoFromLoop(project_.track, lane_, bpm, bars);
        std::snprintf(text, sizeof text, "TEMPO %d: IT FILLS %d BAR%s", static_cast<int>(project_.tempo), bars, bars == 1 ? "" : "S");
    }
    said_ = text;
    saidUntil_ = lastMs_ + 2500;
}

// R1 in the EDIT view (batch S1, 2026-11-25): what the lane plays saved as a new recording (Session::saveLaneSound).
void KitScreen::saveSound() {
    std::string name;
    const Session::SaveSound how = session_.saveLaneSound(project_.track, lane_, 0.f, 1.f, name);
    said_ = Session::savedLine(how, name);
    saidUntil_ = lastMs_ + 2500;
}

void KitScreen::press(Button b) {
    if (onKit_) {
        if (holding_ && (b == Button::Up || b == Button::Down)) {
            stepKit(b == Button::Up ? 1 : -1), stepped_ = true;
            return;
        }
        if (b == Button::Left || b == Button::Right) { // the kits, a choice: one a press (owner, 2026-11-28: the stop is a box with arrows)
            if (!holding_) folders_ = session_.kitFolders(), foldersSeen_ = session_.sampleFiles(); // (as A going down reads them)
            stepKit(b == Button::Right ? 1 : -1);
            stepped_ = stepped_ || holding_;
            return;
        }
        if (b == Button::Up) onKit_ = false, lane_ = kDrumLanes - 1; // (round, as the lanes go)
        if (b == Button::Down) onKit_ = false, lane_ = 0;
        return;
    }
    if (holding_) {
        if (column_ == kFit && (b == Button::Left || b == Button::Right)) { // THE TEMPO FROM THIS LOOP (batch S1)
            stepped_ = true;
            tempoFromLoop(b == Button::Right ? 1 : -1);
            return;
        }
        if (b != Button::Up && b != Button::Down) return;
        stepped_ = true;
        if (column_ == kSound) stepSound(b == Button::Up ? 1 : -1);
        else if (column_ == kFit) stepFit(b == Button::Up ? 1 : -1);
        else if (column_ == kChoke) stepChoke(b == Button::Up ? 1 : -1);
        else if (column_ == kSource) stepSource(b == Button::Up ? 1 : -1);
        return;
    }
    if (b == Button::Up) {
        if (lane_ == 0) onKit_ = true; // the KIT stop is above lane 1
        else --lane_;
    }
    if (b == Button::Down) lane_ = (lane_ + 1) % kDrumLanes;
    if (b == Button::Left) column_ = (column_ + kColumns - 1) % kColumns; // round the ends, as the lanes go
    if (b == Button::Right) column_ = (column_ + 1) % kColumns;
}

void KitScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (picker_.isOpen()) {
        if (b == Button::Start && event.down && !input.held(Button::Select) && !picker_.typing()) session_.togglePlay(false); // to hear it in the beat
        else picker_.onButton(event, project_.playing);
        return;
    }
    edit_.closeIfMoved();
    if (edit_.isOpen()) return edit_.onButton(event, input);
    if (voicing_) return voiceButton(event, input);
    // While a take waits or runs, R1 stops it from any lane or column and from the KIT stop, A held or
    // not (gesture audit G34: the line says R1 TO STOP, and on the KIT stop R1 did nothing).
    if (b == Button::R1 && event.down && session_.recordingNow()) {
        kitDeleting_ = false; // (any other button calls a delete off)
        session_.toggleRecording(project_.track, lane_);
        return;
    }
    if (onKit_) {
        // The KIT stop: hold A + up / down the kits, B with A held the kit as it was, Y save, hold B delete.
        if (event.down && b != Button::B) kitDeleting_ = false; // (any other button calls a delete off)
        if (b == Button::A) {
            holding_ = event.down;
            stepped_ = false;
            saved_ = 0.f;
            if (event.down) session_.keepSound(project_.track); // (what B with A held puts back)
            if (event.down) folders_ = session_.kitFolders(), foldersSeen_ = session_.sampleFiles(); // (the folders after the kits: batch S1)
            return;
        }
        if (b == Button::B) {
            if (event.down && holding_) {
                putBack(input);
            } else if (event.down) {
                const int now = session_.kitOf(project_.track);
                kitDeleting_ = now >= 0 && session_.kits()[static_cast<std::size_t>(now)].own;
                kitDeleteTimed_ = false;
            } else {
                kitDeleting_ = false;
            }
            return;
        }
        if (!event.down) {
            if (b == repeating_) repeating_ = Button::None;
            return;
        }
        if (isDirection(b)) {
            press(b);
            repeating_ = b;
            repeat_.restart();
        } else if (b == Button::Y && !holding_) {
            const int now = session_.kitOf(project_.track);
            const std::string name = now >= 0 ? "" : session_.saveKit(project_.track);
            said_ = now >= 0 ? "IT IS " + session_.kits()[static_cast<std::size_t>(now)].name + " ALREADY" : name.empty() ? "NOT SAVED: THE CARD REFUSED IT" : "SAVED AS " + name;
            saidUntil_ = lastMs_ + 2500;
        } else if (b == Button::Start && !input.held(Button::Select)) {
            session_.togglePlay(false);
        }
        return;
    }
    // CATCH's Y (catch_key.hpp): a tap catches on its let go; held, the directions choose the bars and what.
    if (b == Button::Y && !event.down) {
        if (catchKey_.up()) catchBar();
        return;
    }
    if (event.down && b != Button::Y) {
        if (catchKey_.held() && isDirection(b)) {
            catchKey_.direction(b, session_);
            return; // (the cursor stays, and nothing repeats)
        }
        catchKey_.spoil();
    }
    if (b == Button::A) {
        // Held, with up / down: one at a time. A plain tap on SOUND: the list; on the picture: EDIT.
        if (!event.down && holding_ && !stepped_ && column_ == kSound) {
            if (trk().laneSynth[lane_].on()) { // a synth lane: its knobs
                openVoice();
                holding_ = false;
                repeating_ = Button::None;
                return;
            }
            openPicker();
        }
        if (!event.down && holding_ && !stepped_ && column_ == kFit) fitToItsBars();
        if (!event.down && holding_ && !stepped_ && column_ == kEdit) {
            openEdit(edit_.row());
            holding_ = false;
            repeating_ = Button::None;
            return;
        }
        holding_ = event.down;
        saved_ = 0.f;
        if (event.down) names_ = session_.sampleNames(), namesSeen_ = session_.sampleFiles(), stepped_ = false;
        if (event.down && (column_ == kSound || column_ == kSource)) session_.keepSound(project_.track); // (what B with A held puts back)
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (isDirection(b)) {
        const bool choice = holding_ && column_ == kSource; // (SOURCE's three: a click a push, never run on)
        press(b);
        repeating_ = choice ? Button::None : b;
        repeat_.restart();
    } else if (b == Button::B && holding_) {
        stepped_ = true; // (A's let go after it is no tap: on FIT no fit to its bars)
        if (column_ == kSound || column_ == kSource) putBack(input);
    } else if (b == Button::B && !holding_) {
        if (column_ == kSound || column_ == kSource) session_.setLaneSample(project_.track, lane_, "");
        else if (column_ == kFit) session_.setLaneFit(project_.track, lane_, 0);
        else if (column_ == kChoke) session_.setLaneChoke(project_.track, lane_, kChokeNone);
    } else if (b == Button::Y && !holding_) {
        catchKey_.down(); // (CATCH on its let go, if nothing was chosen meanwhile)
    } else if (b == Button::R1 && !holding_) {
        record();
    } else if (b == Button::L1 && !holding_ && !input.held(Button::Select)) {
        // Straight to this lane cut into slices, on the PATTERN tab (pattern_screen.hpp).
        if (trk().laneSample[lane_][0] && !session_.laneSound(project_.track, lane_).missing) {
            session_.wantSlices = lane_;
            session_.view = View::Pattern;
        } else {
            said_ = "SLICES ARE FOR A RECORDING: PUT ONE ON THIS LANE FIRST";
            saidUntil_ = lastMs_ + 2500;
        }
    } else if (b == Button::X) {
        hear(true);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void KitScreen::update(const Input& input, Uint32 nowMs) {
    edit_.closeIfMoved(); // (EDIT opened on another track, or the tracks changed under it, while another tab was up)
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (picker_.isOpen() && session_.sampleFiles() != sampleFilesSeen_) { // the card's lists changed (WiFi): read them again, the cursor kept
        sampleFilesSeen_ = session_.sampleFiles();
        picker_.rescan(session_.sampleNames(), session_.sampleFolders());
    }
    if (!input.held(Button::A)) holding_ = false;
    if (!holding_) sourcePushed_ = false;
    if (!input.held(Button::B) || !onKit_ || input.held(Button::A)) kitDeleting_ = false;
    // B kept held on a kit of the player's own: its file is deleted when the bar is full.
    kitDeletePart_ = 0.f;
    if (kitDeleting_) {
        if (!kitDeleteTimed_) kitDeleteTimed_ = true, kitDeleteSince_ = nowMs;
        kitDeletePart_ = static_cast<float>(nowMs - kitDeleteSince_) / static_cast<float>(kKitDeleteMs);
        if (nowMs - kitDeleteSince_ >= kKitDeleteMs) {
            kitDeleting_ = false;
            kitDeletePart_ = 0.f;
            const int now = session_.kitOf(project_.track);
            const std::string name = now >= 0 ? session_.kits()[static_cast<std::size_t>(now)].name : "";
            said_ = now >= 0 && session_.deleteKit(now) ? name + " DELETED. THE TRACK KEEPS ITS SOUND" : "NOT DELETED";
            saidUntil_ = nowMs + 2500;
        }
    }

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    if (headHeld_ && !session_.hearingHeld()) {
        headHeld_ = false; // X let go (App told the Session): the line stops where the sound does
        const float lasts = session_.heardEnds() > head_.since ? static_cast<float>(session_.heardEnds() - head_.since) / 1000.f : 0.f;
        head_.lasts = lasts > 0.f ? lasts : 1e-3f; // (0 would be "to the end")
    }
    picker_.update(input, nowMs, project_.playing);
    if (picker_.isOpen()) return;
    if (!voicing_) hearOwed_ = false; // (left the view: the turn is not heard)
    if (edit_.isOpen() || voicing_) {
        if (!said_.empty() && nowMs >= saidUntil_) said_.clear();
        if (edit_.isOpen()) return edit_.update(input, nowMs, dt);
        hearWhenDone(input, nowMs);
        return voiceUpdate(input, nowMs, dt);
    }
    // While recording, the line says how it is going.
    if (session_.recordingNow()) {
        said_ = session_.recordingLine();
        wasRecording_ = true;
        saidUntil_ = nowMs + 3000;
    } else if (wasRecording_) {
        wasRecording_ = false; // it has just stopped, by R1 or at kMaxSeconds (20 s)
        if (!session_.lastRecorded().empty()) {
            said_ = "RECORDED AS \"" + session_.lastRecorded() + "\"";
            for (char& c : said_) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        } else {
            said_ = "NOTHING RECORDED: NO SOUND CAME IN";
        }
        saidUntil_ = nowMs + 3000;
    }
    if (!said_.empty() && nowMs >= saidUntil_) said_.clear();
    // One move at once, then at twice the held D-pad's pace however far it is pushed: the lanes and columns a
    // cursor's, with A held (the sounds, FIT, CHOKE, the kits) a list's. With A held only up / down counts, and not
    // by a sideways push that drifts (ui::stickAxes).
    if (stickWait_ && input.stickX() == 0.f && input.stickY() == 0.f) stickWait_ = false; // (back in the middle after a put-back)
    const bool choosing = catchKey_.stick(input, session_, nowMs); // Y held: the stick chooses CATCH's bars and what, a push at a time
    const float x = holding_ || stickWait_ || choosing ? 0.f : input.stickX();
    const float y = stickWait_ || choosing ? 0.f : holding_ ? stickAxes(input.stickX(), input.stickY()).y : input.stickY();
    const bool sideways = std::fabs(x) > std::fabs(y);
    const float push = sideways ? x : y;
    // On the KIT stop LEFT / RIGHT step the kits, A held or not: a list's pace held, and the stick once a push (a choice).
    const bool kitSideways = onKit_ && (repeating_ == Button::Left || repeating_ == Button::Right);
    const Pace runs = holding_ || kitSideways ? Pace::List : Pace::Cursor;
    const bool kitPush = onKit_ && !holding_ && sideways && x != 0.f;
    if (kitPush) {
        if (!kitPushed_) press(x > 0.f ? Button::Right : Button::Left);
        saved_ = 0.f;
        stickWas_ = false;
    } else if (holding_ && column_ == kSource) { // SOURCE's three, a choice: once a push, however far it is pushed
        if (y != 0.f && !sourcePushed_) press(y > 0.f ? Button::Down : Button::Up);
        sourcePushed_ = y != 0.f;
        saved_ = 0.f;
        stickWas_ = false;
    } else if (push == 0.f) {
        saved_ = 0.f;
        stickWas_ = false;
    } else {
        saved_ += stickWas_ ? stickPerSecond(runs) * dt : 1.f;
        stickWas_ = true;
        for (; saved_ >= 1.f; saved_ -= 1.f)
            press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
    }
    // A held D-pad: the lanes and columns are a cursor (gesture audit G08: they ran at the list's pace); with A
    // held it steps the sounds, FIT and CHOKE (and on the KIT stop the kits) at the list's, to hear each.
    kitPushed_ = kitPush;
    while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), runs)) press(repeating_);
}

// ---- EDIT ------------------------------------------------------------------------------

// The EDIT view (edit_view.hpp), on this lane.
void KitScreen::openEdit(int row) {
    session_.readyLane(project_.track, lane_);
    const int track = project_.track, lane = lane_;
    EditView::Target t;
    t.edit = [this, track, lane]() -> const LaneEdit& { return project_.tracks[track].laneEdit[lane]; };
    t.set = [this, track, lane](const LaneEdit& e) { session_.setLaneEdit(track, lane, e); };
    t.sound = [this, track, lane]() -> const Session::LaneSound& { return session_.laneSound(track, lane); };
    t.trim = [this, track, lane] { return session_.trimLaneSilence(track, lane); };
    t.normalize = [this, track, lane] { return session_.normalizeLane(track, lane); };
    t.hear = [this](bool held) { hear(held); };
    t.heardAt = [this, lane] { return headLane_ == lane ? head_.at(lastMs_) : -1.f; };
    t.say = [this](const std::string& words) { said_ = words; saidUntil_ = lastMs_ + 2500; };
    t.said = [this]() -> const std::string& { return said_; };
    t.save = [this] { saveSound(); };
    char text[80];
    std::snprintf(text, sizeof text, "LANE %d. THE FILE ITSELF IS NEVER CHANGED", lane + 1);
    t.quiet = text;
    const char* file = trk().laneSample[lane];
    if (!file[0]) std::snprintf(text, sizeof text, "%s, BUILT IN", DrumKit::laneName(lane));
    else if (sampleFolderOf(file).empty()) std::snprintf(text, sizeof text, "%s", file);
    else std::snprintf(text, sizeof text, "%s  /  %s", sampleFolderOf(file).c_str(), sampleLabel(file).c_str());
    for (char* c = text; *c; ++c) *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    t.title = text;
    t.missing = session_.laneSound(project_.track, lane).noMemory ? "NOT LOADED: NOT ENOUGH MEMORY. THE DRUM PLAYS, AS IT IS."
                                                                   : "NOT IN THE SAMPLES FOLDER. THE DRUM PLAYS, AS IT IS.";
    edit_.open(t, row);
}

void KitScreen::hearWhenDone(const Input& input, Uint32 nowMs) {
    if (!hearOwed_) return;
    if (project_.playing) { hearOwed_ = false; return; } // the song is heard instead
    const bool turning = input.held(Button::A) || input.stickX() != 0.f || input.stickY() != 0.f ||
                         (repeating_ != Button::None && input.held(repeating_));
    const Session::LaneSound& sound = session_.laneSound(project_.track, lane_);
    if (turning || sound.making || sound.shaping || nowMs - changedAt_ < kHearAfterMs) return;
    hearOwed_ = false;
    hear();
}

// ---- VOICE ------------------------------------------------------------------------------

namespace {
// The shown knob nearest `column` in a row of four (the column itself first, then one either side,
// the left one first), or -1.
int shownNear(int model, int row, int column) {
    for (int off = 0; off < 4; ++off)
        for (int c : {column - off, column + off})
            if (c >= 0 && c < 4 && drumsynth::shown(model, row * 4 + c)) return row * 4 + c;
    return -1;
}
}

void KitScreen::openVoice(int place) {
    session_.readyLane(project_.track, lane_);
    voicing_ = true;
    edit_.close();
    yJust_ = false;
    hearOwed_ = false;
    const int model = trk().laneSynth[lane_].model, named = voicePlaceOf(trk().laneSynth[lane_]);
    voicePlace_ = drumsynth::shown(model, place) ? place : kVoiceModel;
    voicePicked_ = named >= 0 ? named : 0; // (this lane's: another lane's pick means nothing here)
    holding_ = deleting_ = bBack_ = false;
    deletePart_ = 0.f;
    repeating_ = Button::None;
}

void KitScreen::setVoice(const drumsynth::Voice& voice) {
    const drumsynth::Voice before = trk().laneSynth[lane_];
    session_.setLaneSynth(project_.track, lane_, voice);
    if (trk().laneSynth[lane_] == before || project_.playing) return;
    hearOwed_ = true;
    changedAt_ = lastMs_;
}

void KitScreen::voicePress(Button b) {
    drumsynth::Voice v = trk().laneSynth[lane_];
    if (!v.on()) return;
    if (!holding_) {
        // The MODEL on top; under it two rows of four, the empty places passed by. The ends stop.
        if (voicePlace_ == kVoiceModel) {
            if (b == Button::Down) {
                const int first = shownNear(v.model, 0, 0);
                voicePlace_ = first >= 0 ? first : shownNear(v.model, 1, 0);
            }
            return;
        }
        const int row = voicePlace_ / 4, column = voicePlace_ % 4;
        if (b == Button::Left || b == Button::Right) {
            const int by = b == Button::Left ? -1 : 1;
            for (int c = column + by; c >= 0 && c < 4; c += by)
                if (drumsynth::shown(v.model, row * 4 + c)) {
                    voicePlace_ = row * 4 + c;
                    break;
                }
        }
        if (b == Button::Up) voicePlace_ = row == 0 ? kVoiceModel : (shownNear(v.model, 0, column) >= 0 ? shownNear(v.model, 0, column) : kVoiceModel);
        if (b == Button::Down && row == 0 && shownNear(v.model, 1, column) >= 0) voicePlace_ = shownNear(v.model, 1, column);
        return;
    }
    if (voicePlace_ == kVoiceModel && (b == Button::Left || b == Button::Right)) {
        // The model's named voices (drum_voices.hpp): the one before / after the voice it is, or, turned
        // by hand, the one before / after the last picked. The ends stop.
        const int now = voicePlaceOf(v), from = now >= 0 ? now : voicePicked_;
        const int to = from + (b == Button::Right ? 1 : -1);
        if (to < 0 || to >= voiceCount(v.model)) return;
        voicePicked_ = to;
        setVoice(voiceAt(v.model, to));
        return;
    }
    if (b != Button::Up && b != Button::Down) return;
    const int by = b == Button::Up ? 1 : -1;
    if (voicePlace_ == kVoiceModel) {
        const int to = v.model + by;
        if (to <= drumsynth::kNone || to >= drumsynth::kModels) return; // the ends stop
        setVoice(drumsynth::usualVoice(to));
        return;
    }
    const int to = v.dial[voicePlace_] + by;
    if (to < 0 || to > drumsynth::kDialMost) return;
    v.dial[voicePlace_] = static_cast<std::uint8_t>(to);
    setVoice(v);
}

void KitScreen::voiceButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    const bool afterY = event.down && b == Button::B && bLeavesAfterY(voicePlace_);
    if (event.down) yJust_ = false; // (any press: Y is no longer the last)
    if (b == Button::A) {
        holding_ = event.down;
        turnedVoice_ = 0.f;
        saved_ = 0.f;
        stickWas_ = reversePushed_ = false;
        return;
    }
    if (event.down && b != Button::B) deleting_ = bBack_ = false; // any other button calls a held B off
    if (!event.down) {
        if (b == Button::B) {
            if (deleting_ && bBack_) voicing_ = false; // a tap on MODEL, on a knob at its usual value, or just after Y: back to the kit
            deleting_ = bBack_ = false;
        }
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    drumsynth::Voice v = trk().laneSynth[lane_];
    if (!v.on()) { // (it was taken off from elsewhere: the page's view, an undo)
        voicing_ = false;
        return;
    }
    if (isDirection(b)) {
        voicePress(b);
        repeating_ = b;
        repeat_.restart();
    } else if (b == Button::B && !holding_) {
        // The knob to its usual value at once; held on, the bar fills and every knob goes back (update).
        if (voicePlace_ == kVoiceModel || afterY) {
            bBack_ = true; // (straight after Y's roll: the roll kept, G16)
        } else {
            const int usual = drumsynth::usualVoice(v.model).dial[voicePlace_];
            bBack_ = v.dial[voicePlace_] == usual;
            v.dial[voicePlace_] = static_cast<std::uint8_t>(usual);
            if (!bBack_) setVoice(v);
        }
        deleting_ = true;
        deleteTimed_ = false;
        deletePart_ = 0.f;
    } else if (b == Button::Y && !holding_) {
        // ROLL: each knob shown somewhere in the middle of its range, by a hash of the voice and a count.
        const std::uint32_t seed = drumsynth::seedOf(v) ^ (++rolls_ * 0x9E3779B9u);
        const drumsynth::Voice was = v;
        for (std::uint32_t turn = 0; v == was && turn < 8; ++turn)
            for (int p = 0; p < drumsynth::kPlaces; ++p)
                if (drumsynth::shown(v.model, p)) {
                    const float unit = (drumsynth::noiseAt(seed + turn, static_cast<std::uint32_t>(p)) + 1.f) * 0.5f; // 0..1
                    const int dial = kRollLow + static_cast<int>(unit * static_cast<float>(kRollHigh - kRollLow + 1));
                    v.dial[p] = static_cast<std::uint8_t>(dial > kRollHigh ? kRollHigh : dial);
                }
        setVoice(v);
        yJust_ = true, yBox_ = voicePlace_;
    } else if ((b == Button::L1 || b == Button::R1) && !holding_ && !input.held(Button::Select)) {
        // The synth lane before / after; the ends stop.
        const int by = b == Button::L1 ? -1 : 1;
        for (int lane = lane_ + by; lane >= 0 && lane < kDrumLanes; lane += by)
            if (trk().laneSynth[lane].on()) {
                lane_ = lane;
                openVoice(voicePlace_);
                break;
            }
    } else if (b == Button::X) {
        hear(true);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void KitScreen::voiceUpdate(const Input& input, Uint32 nowMs, float dt) {
    const drumsynth::Voice& v = trk().laneSynth[lane_];
    if (!v.on()) { // taken off from elsewhere (an undo, the page): back to the kit
        voicing_ = false;
        return;
    }
    // B held: the bar fills for half a second (G29), then every knob goes back to the model's usual.
    if (!input.held(Button::B) || holding_) deleting_ = bBack_ = false;
    if (!deleting_) {
        deletePart_ = 0.f;
    } else {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        if (deletePart_ >= 1.f) {
            deleting_ = bBack_ = false; // (let go now, B does not go back)
            deletePart_ = 0.f;
            setVoice(drumsynth::usualVoice(v.model));
        }
    }
    const float x = input.stickX(), y = input.stickY();
    if (holding_) {
        const float upDown = stickAxes(x, y).y; // only up / down counts, and not by a sideways push that drifts
        if (voicePlace_ == kVoiceModel) {
            // The models (up / down), a choice: once a push, however far it is pushed (CLAUDE.md: choices click once per
            // push; the owner's stick rule, 2026-11-25, took out the "past half"), as EDIT's REVERSE.
            if (upDown != 0.f && !reversePushed_) voicePress(upDown < 0.f ? Button::Up : Button::Down);
            reversePushed_ = upDown != 0.f;
        } else {
            // A knob: the stick turns it through its whole range in about a second (times STICK SPEED), however far it
            // is pushed (owner, 2026-11-25; it was by the square of the push).
            turnedVoice_ += -stickWay(upDown) * static_cast<float>(drumsynth::kDialMost) * dt;
            const int whole = static_cast<int>(turnedVoice_);
            if (whole != 0) {
                turnedVoice_ -= static_cast<float>(whole);
                drumsynth::Voice to = v;
                const int dial = to.dial[voicePlace_] + whole;
                to.dial[voicePlace_] = static_cast<std::uint8_t>(dial < 0 ? 0 : dial > drumsynth::kDialMost ? drumsynth::kDialMost : dial);
                setVoice(to);
            }
            if (upDown == 0.f) turnedVoice_ = 0.f;
        }
    } else {
        // The boxes: one at once, then at twice the held D-pad's pace (the cursor's), however far it is pushed.
        const bool sideways = std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? stickPerSecond(Pace::Cursor) * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f) {
                deleting_ = bBack_ = false;
                voicePress(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
            }
        }
    }
    // A held D-pad: the boxes are a cursor, and a knob with A held a value turned, 0 to 100 (gesture audit G08: it
    // ran at the list's pace, eleven seconds end to end); MODEL with A held steps the models and the named voices,
    // a list, at the list's.
    while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), holding_ && voicePlace_ == kVoiceModel ? Pace::List : Pace::Cursor))
        voicePress(repeating_);
}

void KitScreen::drawVoice(SDL_Renderer* r, const Fonts& fonts) const {
    char text[80];
    const drumsynth::Voice& v = trk().laneSynth[lane_];
    const Session::LaneSound& sound = session_.laneSound(project_.track, lane_);

    // ---- the model and its sound --------------------------------------------------
    const int sx = 8, sy = kEditY, sw = 624, sh = kEditH;
    box(r, sx, sy, sw, sh, kPanel, kLine);
    if (deletePart_ > 0.f) {
        fill(r, sx + 1, sy + sh - 6, static_cast<int>((sw - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), 5, faded(kStop, 150));
        fonts.within(&Fonts::small, kVoiceDeletingWords, sw - 120.f).draw(r, sx + 7.f, sy + 7.f, kVoiceDeletingWords, kText);
    } else if (!said_.empty()) {
        fonts.within(&Fonts::small, said_.c_str(), sw - 120.f).draw(r, sx + 7.f, sy + 7.f, said_.c_str(), kBrass);
    } else {
        std::snprintf(text, sizeof text, "LANE %d. A SOUND MADE FROM ITS KNOBS", lane_ + 1);
        fonts.within(&Fonts::small, text, sw - 120.f).draw(r, sx + 7.f, sy + 7.f, text, kDim);
    }
    // The model, and the named voice its dials are (brass), or SYNTH when they are turned by hand.
    const int named = voicePlaceOf(v);
    std::snprintf(text, sizeof text, "%s,", drumsynth::modelName(v.model));
    const char* second = named >= 0 ? voiceName(v.model, named) : "SYNTH";
    char both[sizeof text + 40]; // (the model's name and a voice's: far shorter; the room is what the compiler can prove)
    std::snprintf(both, sizeof both, "%s %s", text, second);
    const Font& big = fonts.within(&Fonts::large, both, sw - 14.f);
    big.draw(r, sx + 7.f, sy + 23.f, text, kText);
    big.draw(r, sx + 7.f + big.width(text) + big.width(" "), sy + 23.f, second, named >= 0 ? kBrass : kDim);
    if (sound.making || sound.shaping) std::snprintf(text, sizeof text, "MAKING IT...");
    else std::snprintf(text, sizeof text, "PLAYS %.2f S", static_cast<double>(sound.seconds));
    fonts.small.drawRight(r, sx + sw - 8.f, sy + 7.f, text, sound.making || sound.shaping ? kBrass : kDim);
    if (!sound.sourcePeaks.empty()) {
        const int wx = sx + 7, wy = kEditWaveY, ww = sw - 14, wh = kEditWaveH;
        wavePicture(r, sound.sourcePeaks, wx, wy, ww, wh, 0.f, 1.f);
        const float at = headLane_ == lane_ ? head_.at(lastMs_) : -1.f;
        if (at >= 0.f) fill(r, wx + static_cast<int>((at > 1.f ? 1.f : at) * static_cast<float>(ww - 2)), wy - 2, 2, wh + 4, kBrass);
    }
    if (voicePlace_ == kVoiceModel) outline(r, sx - 1, sy - 1, sw + 2, sh + 2, holding_ ? kBrass : kTeal, 2);

    // ---- the knobs ----------------------------------------------------------------
    for (int p = 0; p < drumsynth::kPlaces; ++p) {
        const int x = kEditBoxX + (p % 4) * kEditBoxStep, y = kEditRowY + (p / 4) * (kEditRowH + kEditRowGap);
        if (!drumsynth::shown(v.model, p)) {
            box(r, x, y, kEditBoxW, kEditRowH, kPanel, kLine); // a place this model does not use
            continue;
        }
        drumsynth::knobText(v.model, p, v.dial[p], text, sizeof text);
        valueBox(r, fonts, x, y, kEditBoxW, kEditRowH, drumsynth::knob(v.model, p).name, text, static_cast<float>(v.dial[p]) / drumsynth::kDialMost, p == voicePlace_, holding_);
    }
}

void KitScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    if (edit_.isOpen()) {
        std::snprintf(text, sizeof text, "%s / LANE %d EDIT", name, lane_ + 1);
        topBar(r, fonts, View::Device, text, project_);
        edit_.draw(r, fonts);
        scopeStrip(r, fonts, session_.engine);
        return;
    }
    if (voicing_) {
        drawVoicePage(r, fonts, name);
        return;
    }
    std::snprintf(text, sizeof text, "%s / KIT", name);
    topBar(r, fonts, View::Device, text, project_);
    const SDL_Color cursorColor = holding_ ? kBrass : kTeal;
    const int soundX = 8, soundW = 250, fitX = kFitX, chokeX = kChokeX, waveX = kWaveX, waveW = kWaveW;

    // The KIT stop, where SOUND's heading stood: a box like a lane's (owner, 2026-11-28: "it's not very apparent
    // that you can select or change a kit"; it was KIT and the name as a heading, a line under it on the stop), with
    // KIT, the kit the track's is (one of the list's, or YOUR OWN) and the arrows LEFT / RIGHT step it by.
    {
        const int now = const_cast<Session&>(session_).kitOf(project_.track);
        const std::vector<Session::Kit>& all = const_cast<Session&>(session_).kits();
        // A folder put on as a kit (batch S1) reads FOLDER and its name while the kit is as it was put on.
        const std::string folder = now < 0 ? session_.folderKitOf(project_.track) : "";
        std::string name = now >= 0 ? all[static_cast<std::size_t>(now)].name : folder.empty() ? "YOUR OWN" : "FOLDER " + folder;
        for (char& c : name) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        const int y = kKitBoxY, h = kKitBoxH, arrowsW = 26;
        box(r, soundX, y, soundW, h, kPanel2, kLine);
        const float room = static_cast<float>(soundW - 14 - arrowsW);
        std::string whole = std::string("KIT ") + name;
        const Font& f = fonts.within(&Fonts::small, whole.c_str(), room);
        while (name.size() > 1 && f.width(("KIT " + name).c_str()) > room) name.pop_back(); // (a long folder: cut, as a lane's name is)
        const float textY = y + (h - f.capHeight()) / 2.f;
        f.draw(r, soundX + 7.f, textY, "KIT", kDim);
        f.draw(r, soundX + 7.f + f.width("KIT") + 8.f, textY, name.c_str(), onKit_ ? cursorColor : now >= 0 || !folder.empty() ? kText : kDim);
        // The arrows: it steps, a kit a press. In the cursor's colour on the stop.
        const SDL_Color arrows = onKit_ ? cursorColor : kDim;
        const int mid = y + h / 2, right = soundX + soundW - 8;
        for (int i = 0; i < 5; ++i) {
            fill(r, right - arrowsW + 6 + i, mid - i, 1, 2 * i + 1, arrows); // LEFT
            fill(r, right - i, mid - i, 1, 2 * i + 1, arrows);               // RIGHT
        }
        if (onKit_) outline(r, soundX - 1, y - 1, soundW + 2, h + 2, cursorColor, 2);
        if (kitDeletePart_ > 0.f) fill(r, soundX, y + h - 3, static_cast<int>(soundW * (kitDeletePart_ > 1.f ? 1.f : kitDeletePart_)), 3, kStop);
    }
    const std::string said = catchKey_.held() ? catchChoiceLine(session_) : said_; // (Y held: what its let go catches)
    if (said.empty()) {
        fonts.small.draw(r, fitX + 2.f, 34.f, "FIT TO", kDim);
        fonts.small.draw(r, chokeX + 2.f, 34.f, "CHOKE", kDim);
        fonts.small.draw(r, kSourceX + 2.f, 34.f, "SOURCE", kDim);
        fonts.small.draw(r, waveX + 2.f, 34.f, "EDIT", kDim); // (THE RECORDING until SOURCE took its room: A edits it)
    } else { // over the heads of the three columns at the right, for a moment
        fonts.within(&Fonts::small, said.c_str(), waveX + waveW - fitX - 4.f).draw(r, fitX + 2.f, 34.f, said.c_str(), kBrass);
    }
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        drawLaneRow(r, fonts, lane, cursorColor);
    }
    scopeStrip(r, fonts, session_.engine);

    drawHints(r, fonts);
}

// A lane's synth voice (its VOICE view), with its hints.
void KitScreen::drawVoicePage(SDL_Renderer* r, const Fonts& fonts, const char* name) const {
    char text[64];
    std::snprintf(text, sizeof text, "%s / LANE %d VOICE", name, lane_ + 1);
    topBar(r, fonts, View::Device, text, project_);
    drawVoice(r, fonts);
    scopeStrip(r, fonts, session_.engine);
    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    const drumsynth::Voice& v = trk().laneSynth[lane_];
    const bool onModel = voicePlace_ == kVoiceModel;
    if (holding_) {
        hint("UP / DOWN", onModel ? "model" : "turn it");
        if (onModel) hint("LEFT / RIGHT", "voice");
    } else {
        hint("HOLD A", onModel ? "model" : "turn");
        hint("B", onModel || bLeavesAfterY(voicePlace_) || v.dial[voicePlace_] == drumsynth::usualVoice(v.model).dial[voicePlace_] ? "back" : "default");
        hint("Y", "roll");
        hint("L1 / R1", "lane");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

// A lane's row: its sound, FIT TO, CHOKE and the recording's picture.
void KitScreen::drawLaneRow(SDL_Renderer* r, const Fonts& fonts, int lane, SDL_Color cursorColor) const {
    char text[64];
    const int rowH = kLaneRowH, top = kLaneTop, soundX = 8, soundW = 250, fitX = kFitX, fitW = kFitW, chokeX = kChokeX, chokeW = kChokeW, waveX = kWaveX, waveW = kWaveW;
    const int y = top + lane * rowH, h = rowH - kLaneGap;
    const Session::LaneSound& sound = session_.laneSound(project_.track, lane);
    const char* file = trk().laneSample[lane];
    const bool here = lane == lane_;
    const float textY = y + (h - fonts.medium.capHeight()) / 2.f;

    const drumsynth::Voice& voice = trk().laneSynth[lane];
    box(r, soundX, y, soundW, h, file[0] || voice.on() ? kPanel2 : kPanel, kLine);
    std::snprintf(text, sizeof text, "%d", lane + 1);
    fonts.small.draw(r, soundX + 7.f, y + (h - fonts.small.capHeight()) / 2.f, text, kDim);
    // (SYNTH and BUILT IN were said faint at the box's right: SOURCE says it now, in its colour.)
    if (voice.on()) { // a synth voice: its model's name
        fonts.medium.draw(r, soundX + 26.f, textY, drumsynth::modelName(voice.model), kText);
    } else if (file[0]) {
        // Its own name, and the folder it is in (if any) small on the right.
        std::string upper = sampleLabel(file), folder = sampleLabel(sampleFolderOf(file)); // (the folder it is in: of two, the inner one)
        for (char& c : upper) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        for (char& c : folder) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        // (At XL a name that does not fit whole is held at LARGE, and cut as it is cut at LARGE: never shorter.)
        const Font& folderFont = fonts.within(&Fonts::small, folder.c_str(), 70.f);
        while (!folder.empty() && folderFont.width(folder.c_str()) > 70) folder.pop_back();
        const float room = soundW - 34.f - (folder.empty() ? 0.f : folderFont.width(folder.c_str()) + 14.f);
        const Font& nameFont = fonts.within(&Fonts::medium, upper.c_str(), room);
        while (!upper.empty() && nameFont.width(upper.c_str()) > room) upper.pop_back();
        nameFont.draw(r, soundX + 26.f, y + (h - nameFont.capHeight()) / 2.f, upper.c_str(), sound.missing ? kDim : kText);
        if (!folder.empty()) folderFont.drawRight(r, soundX + soundW - 8.f, y + (h - folderFont.capHeight()) / 2.f, folder.c_str(), kDim);
    } else {
        fonts.medium.draw(r, soundX + 26.f, textY, DrumKit::laneName(lane), kDim);
    }
    if (here && !onKit_ && column_ == kSound) outline(r, soundX - 1, y - 1, soundW + 2, h + 2, cursorColor, 2);

    const int fit = trk().laneFit[lane];
    box(r, fitX, y, fitW, h, kPanel, kLine);
    const bool fits = file[0] || voice.on(); // (a synth voice is fitted as a recording; the built-in drum is not)
    if (!fits) std::snprintf(text, sizeof text, "-");
    else laneFitText(fit, project_.meterOf(project_.scene), text, sizeof text);
    fonts.medium.draw(r, fitX + 9.f, textY, text, fit > 0 && fits ? kPurple : kDim);
    if (here && !onKit_ && column_ == kFit) outline(r, fitX - 1, y - 1, fitW + 2, h + 2, cursorColor, 2);

    // CHOKE: NONE, or the group, in the colour of the mark on the drum grid's lane name.
    const int group = trk().laneChoke[lane];
    box(r, chokeX, y, chokeW, h, kPanel, kLine);
    fonts.medium.draw(r, chokeX + 9.f, textY, laneChokeText(group), group == kChokeNone ? kDim : kBrass);
    if (here && !onKit_ && column_ == kChoke) outline(r, chokeX - 1, y - 1, chokeW + 2, h + 2, cursorColor, 2);

    // SOURCE: BUILT IN, SYNTH or SAMPLE, each in its own colour (owner, 2026-11-28).
    {
        const Source source = sourceOf(lane);
        const char* word = sourceText(source);
        box(r, kSourceX, y, kSourceW, h, kPanel, kLine);
        const Font& f = fonts.within(&Fonts::medium, word, kSourceW - 12.f); // (BUILT IN at XL: held at LARGE; 7 in, not CHOKE's 9: BUILT IN stands in the middle)
        f.draw(r, kSourceX + 7.f, y + (h - f.capHeight()) / 2.f, word, source == kSourceSynth ? kTeal : source == kSourceSample ? kPurple : kBrass);
        if (here && !onKit_ && column_ == kSource) outline(r, kSourceX - 1, y - 1, kSourceW + 2, h + 2, cursorColor, 2);
    }

    // The recording's outline, and what is being done with it.
    if (sound.missing) { // (on two short lines: the column is narrower since CHOKE, and again since SOURCE)
        const char* gone = sound.noMemory ? "NO MEMORY:" : "NOT FOUND:"; // (NOT LOADED: there was not the memory for it)
        const char* plays = "DRUM PLAYS";
        const Font& goneFont = fonts.within(&Fonts::small, plays, waveW - 6.f); // (the longer: both lines in its lettering)
        const float lineGap = 4.f, linesH = 2.f * goneFont.capHeight() + lineGap;
        goneFont.draw(r, waveX + 4.f, y + (h - linesH) / 2.f, gone, kBrass);
        goneFont.draw(r, waveX + 4.f, y + (h - linesH) / 2.f + goneFont.capHeight() + lineGap, plays, kBrass);
    } else if (!sound.peaks.empty()) {
        if (sound.shaping) std::snprintf(text, sizeof text, "MAKING");
        else if (sound.waiting) std::snprintf(text, sizeof text, "FITTING");
        else std::snprintf(text, sizeof text, "%.1f S", sound.seconds);
        // The picture in what the seconds leave (it had 70 pixels kept for them; the column is narrower since SOURCE).
        const int count = static_cast<int>(sound.peaks.size()), ww = waveW - 2 - static_cast<int>(fonts.small.width(text)) - 6;
        for (int i = 0; i < count; ++i) {
            const int ph = 1 + static_cast<int>(sound.peaks[static_cast<std::size_t>(i)] * (h - 3));
            fill(r, waveX + ww * i / count, y + (h - ph) / 2, 1, ph, here ? kText : kDim);
        }
        if (lane == headLane_) drawPlayhead(r, head_, lastMs_, waveX, y + 1, ww, h - 2);
        const bool edited = !trk().laneEdit[lane].atRest(); // (its seconds purple, as a FIT that is on)
        fonts.small.drawRight(r, waveX + waveW - 2.f, y + (h - fonts.small.capHeight()) / 2.f, text, sound.waiting ? kBrass : edited ? kPurple : kDim);
    }
    if (here && !onKit_ && column_ == kEdit) outline(r, waveX - 1, y - 1, waveW + 2, h + 2, cursorColor, 2);
}

void KitScreen::drawHints(SDL_Renderer* r, const Fonts& fonts) const {
    Hint list[8];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (onKit_) {
        const int now = const_cast<Session&>(session_).kitOf(project_.track);
        if (kitDeletePart_ > 0.f) {
            hint("HOLD B", kKitDeletingWords);
        } else if (holding_) {
            hint("UP / DOWN", folders_.empty() ? "next / previous kit" : "kits, then folders");
            hint("B", "as it was"); // (Session::putSoundBack, as SOUND's)
        } else {
            hint("LEFT / RIGHT", "kits"); // (owner, 2026-11-28: the arrows on the box)
            hint("HOLD A", "try kits"); // (UP / DOWN too, and B puts the kit back)
            if (now < 0) hint("Y", "save this kit");
            else if (const_cast<Session&>(session_).kits()[static_cast<std::size_t>(now)].own) hint("HOLD B", "delete this kit");
            hint("START", project_.playing ? "stop" : "play");
        }
        hints(r, fonts, list, count);
        picker_.draw(r, fonts);
        return;
    }
    if (catchKey_.held()) {
        hint("LEFT / RIGHT", "bars");
        hint("UP / DOWN", "mix or a track");
        hint("Y", "catch"); // (on its let go)
    } else if (holding_ && column_ == kEdit) {
        hint("A", "edit"); // (opens when it is let go)
    } else if (holding_ && column_ == kSource) {
        // From BUILT IN either way; from either end, the one way there is.
        const Source now = sourceOf(lane_);
        if (now == kSourceBuiltIn) hint("UP / DOWN", "sample / synth");
        else hint(now == kSourceSynth ? "UP" : "DOWN", "built in");
        hint("B", "as it was");
    } else if (holding_) {
        hint("UP / DOWN", column_ == kSound ? "recordings / synths" : column_ == kFit ? "longer / shorter" : "next / previous group");
        if (column_ == kSound) hint("B", "as it was");
        if (column_ == kFit && fitsByTap()) hint("LEFT / RIGHT", "tempo from it");
    } else if (column_ == kEdit) {
        hint("A", "edit");
    } else if (column_ == kSource) {
        hint("HOLD A", "source");
        hint("B", "built in");
    } else if (column_ == kFit && fitsByTap()) {
        hint("A", "fit to its bars"); // (HOLD A steps it: the help card says so)
        hint("B", "fit off");
    } else {
        // On the name A picks within the lane's SOURCE, and says which (owner, 2026-11-28): a synth's knobs, else the list.
        // (CHOKE's "choke group" and SOUND's B "default drum" were shortened for lane 1's UP kit, owner 2026-11-28:
        // the column's heading says CHOKE, and the manual's word for B there is "the drum back".)
        hint(column_ == kSound ? "A" : "HOLD A", column_ == kSound ? (trk().laneSynth[lane_].on() ? "synth knobs" : "sample list") : column_ == kFit ? "fit to steps" : "choke");
        hint("B", column_ == kSound ? "drum back" : column_ == kFit ? "fit off" : "no choke");
    }
    if (!holding_ && !catchKey_.held()) {
        hint("Y", "catch");
        hint("R1", session_.recordingNow() ? "stop" : "record");
        // A recording on the lane has all six it can hold: START yields to the slices (it is on
        // every other bar, and the top bar shows playing).
        const bool slices = trk().laneSample[lane_][0] != 0;
        if (slices) hint("L1", "slices");
        if (!slices) hint("START", project_.playing ? "stop" : "play");
        if (lane_ == 0) hint("UP", "kit"); // the KIT stop is above lane 1 (owner, 2026-11-28: it was not seen)
    }
    hints(r, fonts, list, count);
    picker_.draw(r, fonts); // over everything, when it is open
}

} // namespace apotheca
