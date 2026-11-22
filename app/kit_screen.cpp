#include "kit_screen.hpp"
#include "drum_voices.hpp"
#include "samples.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace apotheca {

using namespace ui;

void laneFitText(int fit, char* text, std::size_t size) {
    if (fit == 0) std::snprintf(text, size, "OFF");
    else if (fit % 16 == 0) std::snprintf(text, size, "%d BAR%s", fit / 16, fit == 16 ? "" : "S");
    else std::snprintf(text, size, "%d STEP%s", fit, fit == 1 ? "" : "S");
}

const char* laneChokeText(int group) { return group == kChokeNone ? "NONE" : group == 1 ? "1" : "2"; }

namespace {
constexpr Uint32 kRepeatDelayMs = ui::kListDelayMs, kRepeatEveryMs = ui::kListEveryMs; // eight rows, and sounds stepped through
constexpr float kStepsPerSecond = 8.f; // the stick with A held: slow enough to hear each sound
}

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
    const int to = now + by;
    if (to < 0 || to >= kLaneFitCount) return;
    session_.setLaneFit(project_.track, lane_, kLaneFits[to]);
}

// NONE, 1, 2: the ends stop.
void KitScreen::stepChoke(int by) {
    const int to = trk().laneChoke[lane_] + by;
    if (to < kChokeNone || to > kChokeGroups) return;
    session_.setLaneChoke(project_.track, lane_, to);
}

void KitScreen::openPicker() {
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
    jobs.followFailures = [this] { return session_.followFailures(); };
    jobs.playhead = [this] { return &head_; };
    char title[48];
    std::snprintf(title, sizeof title, "LANE %d: PICK ITS SOUND", lane + 1);
    jobs.used = [this, track, lane](const std::string& name) { return session_.sampleUses(name) - (name == project_.tracks[track].laneSample[lane] ? 1 : 0); };
    picker_.open(session_.sampleNames(), session_.sampleFolders(), trk().laneSample[lane_], title, "THE BUILT-IN DRUM", jobs);
    sampleFilesSeen_ = session_.sampleFiles();
}

// The kit after or before in the list, put on at once. From a kit of the player's own making (in no
// list) UP is the first; the ends stop.
void KitScreen::stepKit(int by) {
    const int now = session_.kitOf(project_.track);
    const int to = now < 0 ? (by > 0 ? 0 : -1) : now + by;
    if (to < 0) return; // (past the last: loadKit refuses)
    if (!session_.loadKit(project_.track, to)) return;
    if (!project_.playing) hear(false); // (heard: its first lane, where the stop is reached from)
}

void KitScreen::press(Button b) {
    if (onKit_) {
        if (holding_) {
            if (b == Button::Up || b == Button::Down) stepKit(b == Button::Up ? 1 : -1), stepped_ = true;
            return;
        }
        if (b == Button::Up) onKit_ = false, lane_ = kDrumLanes - 1; // (round, as the lanes go)
        if (b == Button::Down) onKit_ = false, lane_ = 0;
        return;
    }
    if (holding_) {
        if (b != Button::Up && b != Button::Down) return;
        stepped_ = true;
        if (column_ == kSound) stepSound(b == Button::Up ? 1 : -1);
        else if (column_ == kFit) stepFit(b == Button::Up ? 1 : -1);
        else if (column_ == kChoke) stepChoke(b == Button::Up ? 1 : -1);
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
    if (editing_) return editButton(event, input);
    if (voicing_) return voiceButton(event, input);
    if (onKit_) {
        // The KIT stop: hold A + up / down the kits, B with A held the kit as it was, Y save, hold B delete.
        if (event.down && b != Button::B) kitDeleting_ = false; // (any other button calls a delete off)
        if (b == Button::A) {
            holding_ = event.down;
            stepped_ = false;
            saved_ = 0.f;
            if (event.down) {
                kitBefore_.reset(new TrackData);
                copyKit(trk(), *kitBefore_);
            }
            return;
        }
        if (b == Button::B) {
            if (event.down && holding_) {
                if (kitBefore_) session_.putKit(project_.track, *kitBefore_);
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
            repeatAt_ = 0;
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
        if (!event.down && holding_ && !stepped_ && column_ == kEdit) {
            openEdit(editRow_);
            holding_ = false;
            repeating_ = Button::None;
            return;
        }
        holding_ = event.down;
        saved_ = 0.f;
        if (event.down) names_ = session_.sampleNames(), namesSeen_ = session_.sampleFiles(), stepped_ = false;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::B && !holding_) {
        if (column_ == kSound) session_.setLaneSample(project_.track, lane_, "");
        else if (column_ == kFit) session_.setLaneFit(project_.track, lane_, 0);
        else if (column_ == kChoke) session_.setLaneChoke(project_.track, lane_, kChokeNone);
    } else if (b == Button::Y && !holding_) {
        const std::string name = session_.catchLastBar(project_.track, lane_);
        said_ = name.empty() ? "NOTHING TO CATCH: PLAY SOMETHING FIRST" : "CAUGHT THE LAST BAR AS \"" + name + "\"";
        for (char& c : said_) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        saidUntil_ = lastMs_ + 2500;
    } else if (b == Button::R1 && !holding_) {
        // Record from the input: once to start (it waits for sound), again to stop.
        const Session::Rec now = session_.toggleRecording(project_.track, lane_);
        if (now == Session::Rec::NoInput) said_ = "NO INPUT FOUND: PLUG IN A USB MICROPHONE OR INTERFACE";
        else if (now == Session::Rec::Idle && session_.lastRecorded().empty()) said_ = "NOTHING RECORDED: NO SOUND CAME IN";
        saidUntil_ = lastMs_ + 3000;
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
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (picker_.isOpen() && session_.sampleFiles() != sampleFilesSeen_) { // the card's lists changed (WiFi): read them again, the cursor kept
        sampleFilesSeen_ = session_.sampleFiles();
        picker_.rescan(session_.sampleNames(), session_.sampleFolders());
    }
    if (!input.held(Button::A)) holding_ = false;
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
    if (!editing_ && !voicing_) hearOwed_ = false; // (left the view: the turn is not heard)
    if (editing_ || voicing_) {
        if (!said_.empty() && nowMs >= saidUntil_) said_.clear();
        hearWhenDone(input, nowMs);
        return editing_ ? editUpdate(input, nowMs, dt) : voiceUpdate(input, nowMs, dt);
    }
    // While recording, the line says how it is going.
    const Session::Rec rec = session_.recording();
    if (rec == Session::Rec::Waiting || rec == Session::Rec::Recording) {
        char text[64];
        if (rec == Session::Rec::Waiting) std::snprintf(text, sizeof text, "WAITING FOR SOUND...  R1 TO CANCEL");
        else std::snprintf(text, sizeof text, "RECORDING  %.1f S OF 10  R1 TO STOP", static_cast<double>(session_.recordSeconds()));
        said_ = text;
        wasRecording_ = true;
        saidUntil_ = nowMs + 3000;
    } else if (wasRecording_) {
        wasRecording_ = false; // it has just stopped, by R1 or at ten seconds
        if (!session_.lastRecorded().empty()) {
            said_ = "RECORDED AS \"" + session_.lastRecorded() + "\"";
            for (char& c : said_) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        } else {
            said_ = "NOTHING RECORDED: NO SOUND CAME IN";
        }
        saidUntil_ = nowMs + 3000;
    }
    if (!said_.empty() && nowMs >= saidUntil_) said_.clear();
    // One move at once, then at a steady rate. With A held only up / down counts.
    const float x = holding_ ? 0.f : input.stickX(), y = input.stickY();
    const bool sideways = std::fabs(x) > std::fabs(y);
    const float push = sideways ? x : y;
    if (push == 0.f) {
        saved_ = 0.f;
        stickWas_ = false;
    } else {
        saved_ += stickWas_ ? std::fabs(push) * kStepsPerSecond * (holding_ ? 1.f : 2.f) * dt : 1.f;
        stickWas_ = true;
        for (; saved_ >= 1.f; saved_ -= 1.f)
            press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        press(repeating_);
    }
}

// ---- EDIT ------------------------------------------------------------------------------

namespace {
constexpr float kEditMovesPerSecond = 14.f; // the stick among the boxes, as on the sampler's page
// How far along its bar a box stands (below 0: no bar), and whether the bar grows from the middle.
float editPart(const LaneEdit& e, int row) {
    const int v = laneEditValue(e, row);
    switch (row) {
    case kEditStart: case kEditEnd: return static_cast<float>(v) / kEditUnits;
    case kEditFadeIn: case kEditFadeOut: return static_cast<float>(fadeStopPlace(v)) / (kFadeStopCount - 1);
    case kEditGain: return static_cast<float>(v + kGainMost) / (2 * kGainMost);
    case kEditPitch: return static_cast<float>(v + kPitchMost) / (2 * kPitchMost);
    case kEditFine: return static_cast<float>(v + kFineMost) / (2 * kFineMost);
    default: return -1.f;
    }
}
bool editFromMiddle(int row) { return row == kEditGain || row == kEditPitch || row == kEditFine; }
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

void KitScreen::setEdit(const LaneEdit& edit) {
    const LaneEdit before = trk().laneEdit[lane_];
    session_.setLaneEdit(project_.track, lane_, edit);
    if (trk().laneEdit[lane_] == before || project_.playing) return;
    hearOwed_ = true;
    changedAt_ = lastMs_;
}

void KitScreen::editPress(Button b) {
    if (!holding_) {
        // Two rows of four: left / right along a row, up / down between the rows. The ends stop.
        if (b == Button::Left && editRow_ % 4 > 0) --editRow_;
        if (b == Button::Right && editRow_ % 4 < 3) ++editRow_;
        if (b == Button::Up && editRow_ >= 4) editRow_ -= 4;
        if (b == Button::Down && editRow_ < 4) editRow_ += 4;
        return;
    }
    if (b != Button::Up && b != Button::Down) return;
    setEdit(laneEditStepped(trk().laneEdit[lane_], editRow_, b == Button::Up ? 1 : -1, session_.laneSound(project_.track, lane_).sourceSeconds));
}

void KitScreen::editButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (b == Button::A) {
        holding_ = event.down;
        turned_ = 0.f;
        saved_ = 0.f;
        stickWas_ = reversePushed_ = false;
        return;
    }
    if (event.down && b != Button::B) deleting_ = bBack_ = false; // any other button calls a held B off
    if (!event.down) {
        if (b == Button::B) {
            if (deleting_ && bBack_) editing_ = false; // a tap on a box at its usual value: back to the kit
            deleting_ = bBack_ = false;
        }
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    const LaneEdit& e = trk().laneEdit[lane_];
    if (isDirection(b)) {
        editPress(b);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::B && !holding_) {
        // The box to its usual value at once; held on, the bar fills and every edit goes (update).
        const int usual = laneEditValue(LaneEdit(), editRow_);
        bBack_ = laneEditValue(e, editRow_) == usual;
        if (!bBack_) setEdit(laneEditWith(e, editRow_, usual));
        deleting_ = true;
        deleteTimed_ = false;
        deletePart_ = 0.f;
    } else if (b == Button::Y && !holding_) {
        char text[64];
        if (editRow_ == kEditStart || editRow_ == kEditEnd) {
            if (session_.trimLaneSilence(project_.track, lane_)) said_ = "START AND END ON THE FIRST AND LAST SOUND";
            else said_ = "NOTHING TO TRIM TO: IT IS SILENT";
        } else if (session_.normalizeLane(project_.track, lane_)) {
            laneEditText(trk().laneEdit[lane_], kEditGain, 0.f, text, sizeof text);
            said_ = std::string("NORMALIZED: GAIN ") + text;
        } else {
            said_ = "NOTHING TO NORMALIZE: IT IS SILENT";
        }
        saidUntil_ = lastMs_ + 2500;
        if (!project_.playing) { hearOwed_ = true; changedAt_ = lastMs_ - kHearAfterMs; } // heard once it is made
    } else if (b == Button::X) {
        hear(true);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void KitScreen::editUpdate(const Input& input, Uint32 nowMs, float dt) {
    // B held: the bar fills for a second, then every edit of the lane goes (undo brings them back).
    if (!input.held(Button::B) || holding_) deleting_ = bBack_ = false;
    if (!deleting_) {
        deletePart_ = 0.f;
    } else {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        if (deletePart_ >= 1.f) {
            deleting_ = bBack_ = false; // (let go now, B does not go back)
            deletePart_ = 0.f;
            setEdit(LaneEdit());
        }
    }
    const float x = input.stickX(), y = input.stickY();
    const float seconds = session_.laneSound(project_.track, lane_).sourceSeconds;
    if (holding_) {
        // A box: the stick turns it through its whole range in about a second, by the square of the push.
        const LaneEdit& e = trk().laneEdit[lane_];
        if (editRow_ == kEditReverse) { // a choice: once a push
            if (std::fabs(y) > 0.5f && !reversePushed_) setEdit(laneEditWith(e, kEditReverse, y < 0.f ? 1 : 0));
            reversePushed_ = std::fabs(y) > 0.5f;
        } else {
            turned_ += -y * std::fabs(y) * static_cast<float>(laneEditRange(editRow_)) * dt;
            const int whole = static_cast<int>(turned_);
            if (whole != 0) {
                turned_ -= static_cast<float>(whole);
                const int v = laneEditValue(e, editRow_);
                if (editRow_ == kEditFadeIn || editRow_ == kEditFadeOut) {
                    int place = fadeStopPlace(v) + whole;
                    place = place < 0 ? 0 : (place >= kFadeStopCount ? kFadeStopCount - 1 : place);
                    setEdit(laneEditWith(e, editRow_, kFadeStops[place]));
                } else {
                    setEdit(laneEditHeld(laneEditWith(e, editRow_, v + whole), seconds));
                }
            }
            if (y == 0.f) turned_ = 0.f;
        }
    } else {
        const bool sideways = std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * kEditMovesPerSecond * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f) {
                deleting_ = bBack_ = false;
                editPress(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
            }
        }
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + ui::kCursorDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + ui::kCursorEveryMs;
        editPress(repeating_);
    }
}

void KitScreen::drawEdit(SDL_Renderer* r, const Fonts& fonts) const {
    char text[80];
    const TrackData& t = trk();
    const LaneEdit& e = t.laneEdit[lane_];
    const Session::LaneSound& sound = session_.laneSound(project_.track, lane_);

    // ---- the sound ----------------------------------------------------------------
    const int sx = 8, sy = kEditY, sw = 624, sh = kEditH;
    box(r, sx, sy, sw, sh, kPanel, kLine);
    if (deletePart_ > 0.f) {
        fill(r, sx + 1, sy + sh - 6, static_cast<int>((sw - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), 5, faded(kStop, 150));
        fonts.within(&Fonts::small, kDeletingWords, sw - 120.f).draw(r, sx + 7.f, sy + 7.f, kDeletingWords, kText);
    } else if (!said_.empty()) {
        fonts.within(&Fonts::small, said_.c_str(), sw - 120.f).draw(r, sx + 7.f, sy + 7.f, said_.c_str(), kBrass);
    } else {
        std::snprintf(text, sizeof text, "LANE %d. THE FILE ITSELF IS NEVER CHANGED", lane_ + 1);
        fonts.within(&Fonts::small, text, sw - 120.f).draw(r, sx + 7.f, sy + 7.f, text, kDim);
    }
    const char* file = t.laneSample[lane_];
    if (!file[0]) std::snprintf(text, sizeof text, "%s, BUILT IN", DrumKit::laneName(lane_));
    else if (sampleFolderOf(file).empty()) std::snprintf(text, sizeof text, "%s", file);
    else std::snprintf(text, sizeof text, "%s  /  %s", sampleFolderOf(file).c_str(), sampleLabel(file).c_str());
    for (char* c = text; *c; ++c) *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    fonts.within(&Fonts::large, text, sw - 14.f).draw(r, sx + 7.f, sy + 23.f, text, sound.missing ? kDim : kText);
    const float wordsY = kEditWaveY + (kEditWaveH - fonts.small.capHeight()) / 2.f;
    if (sound.missing) {
        fonts.small.draw(r, sx + 7.f, wordsY, "NOT IN THE SAMPLES FOLDER. THE DRUM PLAYS, AS IT IS.", kBrass);
    } else if (!sound.sourcePeaks.empty()) {
        const int wx = sx + 7, wy = kEditWaveY, ww = sw - 14, wh = kEditWaveH;
        // The whole file, the part played lit, each point as loud as GAIN and the fades make it.
        std::vector<float> lit(sound.sourcePeaks.size());
        const int count = static_cast<int>(lit.size());
        for (int i = 0; i < count; ++i)
            lit[static_cast<std::size_t>(i)] = guarded(sound.sourcePeaks[static_cast<std::size_t>(i)] * laneEditLevelAt(e, static_cast<float>(i) / count, sound.sourceSeconds));
        wavePicture(r, sound.sourcePeaks, wx, wy, ww, wh, static_cast<float>(e.start) / kEditUnits, static_cast<float>(e.end) / kEditUnits, &lit);
        // The line while it is heard, through the part played (backwards with REVERSE).
        const float at = headLane_ == lane_ ? head_.at(lastMs_) : -1.f;
        if (at >= 0.f) fill(r, wx + static_cast<int>(laneEditPlaceOf(e, at > 1.f ? 1.f : at) * static_cast<float>(ww - 2)), wy - 2, 2, wh + 4, kBrass);
        if (sound.shaping) std::snprintf(text, sizeof text, "MAKING IT...");
        else std::snprintf(text, sizeof text, "PLAYS %.2f S", static_cast<double>(sound.seconds));
        fonts.small.drawRight(r, sx + sw - 8.f, sy + 7.f, text, sound.shaping ? kBrass : e.atRest() ? kDim : kPurple);
    }

    // ---- the boxes ----------------------------------------------------------------
    for (int row = 0; row < kEditRows; ++row) {
        const int x = kEditBoxX + (row % 4) * kEditBoxStep, y = kEditRowY + (row / 4) * (kEditRowH + kEditRowGap);
        laneEditText(e, row, sound.sourceSeconds, text, sizeof text);
        valueBox(r, fonts, x, y, kEditBoxW, kEditRowH, laneEditName(row), text, editPart(e, row), row == editRow_, holding_, editFromMiddle(row));
    }
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
    editing_ = false;
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
            if (deleting_ && bBack_) voicing_ = false; // a tap on MODEL, or on a knob at its usual value: back to the kit
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
        repeatAt_ = 0;
    } else if (b == Button::B && !holding_) {
        // The knob to its usual value at once; held on, the bar fills and every knob goes back (update).
        if (voicePlace_ == kVoiceModel) {
            bBack_ = true;
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
    // B held: the bar fills for a second, then every knob goes back to the model's usual.
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
        if (voicePlace_ == kVoiceModel) { // a choice: once a push
            if (std::fabs(y) > 0.5f && !reversePushed_) voicePress(y < 0.f ? Button::Up : Button::Down);
            reversePushed_ = std::fabs(y) > 0.5f;
        } else {
            // A knob: the stick turns it through its whole range in about a second, by the square of the push.
            turnedVoice_ += -y * std::fabs(y) * static_cast<float>(drumsynth::kDialMost) * dt;
            const int whole = static_cast<int>(turnedVoice_);
            if (whole != 0) {
                turnedVoice_ -= static_cast<float>(whole);
                drumsynth::Voice to = v;
                const int dial = to.dial[voicePlace_] + whole;
                to.dial[voicePlace_] = static_cast<std::uint8_t>(dial < 0 ? 0 : dial > drumsynth::kDialMost ? drumsynth::kDialMost : dial);
                setVoice(to);
            }
            if (y == 0.f) turnedVoice_ = 0.f;
        }
    } else {
        const bool sideways = std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * kEditMovesPerSecond * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f) {
                deleting_ = bBack_ = false;
                voicePress(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
            }
        }
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + (holding_ ? ui::kListDelayMs : ui::kCursorDelayMs);
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + (holding_ ? ui::kListEveryMs : ui::kCursorEveryMs);
        voicePress(repeating_);
    }
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
    char both[80];
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
    if (editing_) {
        std::snprintf(text, sizeof text, "%s / LANE %d EDIT", name, lane_ + 1);
        topBar(r, fonts, View::Device, text, project_);
        drawEdit(r, fonts);
        scopeStrip(r, fonts, session_.engine);
        Hint list[6];
        int count = 0;
        const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
        if (holding_) {
            hint("UP / DOWN", editRow_ == kEditReverse ? "on / off" : "turn it");
        } else {
            hint("HOLD A", "turn");
            hint("B", laneEditValue(trk().laneEdit[lane_], editRow_) == laneEditValue(LaneEdit(), editRow_) ? "back" : "default");
            hint("Y", editRow_ == kEditStart || editRow_ == kEditEnd ? "trim silence" : "normalize");
            hint("START", project_.playing ? "stop" : "play");
        }
        hints(r, fonts, list, count);
        return;
    }
    if (voicing_) {
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
            hint("B", onModel || v.dial[voicePlace_] == drumsynth::usualVoice(v.model).dial[voicePlace_] ? "back" : "default");
            hint("Y", "roll");
            hint("L1 / R1", "lane");
            hint("START", project_.playing ? "stop" : "play");
        }
        hints(r, fonts, list, count);
        return;
    }
    std::snprintf(text, sizeof text, "%s / KIT", name);
    topBar(r, fonts, View::Device, text, project_);
    const SDL_Color cursorColor = holding_ ? kBrass : kTeal;
    const int rowH = kLaneRowH, top = kLaneTop, soundX = 8, soundW = 250, fitX = kFitX, fitW = kFitW, chokeX = kChokeX, chokeW = kChokeW, waveX = kWaveX, waveW = kWaveW;

    // The KIT stop, where SOUND's heading stood: KIT and the kit the track's is (one of the list's, or YOUR OWN).
    {
        const int now = const_cast<Session&>(session_).kitOf(project_.track);
        const std::vector<Session::Kit>& all = const_cast<Session&>(session_).kits();
        const char* name = now >= 0 ? all[static_cast<std::size_t>(now)].name.c_str() : "YOUR OWN";
        const std::string whole = std::string("KIT ") + name;
        const Font& f = fonts.within(&Fonts::small, whole.c_str(), static_cast<float>(soundW) - 12.f);
        f.draw(r, soundX + 2.f, 34.f, "KIT", kDim);
        f.draw(r, soundX + 2.f + f.width("KIT") + 8.f, 34.f, name, onKit_ ? cursorColor : now >= 0 ? kText : kDim);
        // (Its line stands under the tallest lettering, XL's, and above lane 1.)
        if (kitDeletePart_ > 0.f) fill(r, soundX, 48, static_cast<int>(soundW * (kitDeletePart_ > 1.f ? 1.f : kitDeletePart_)), 1, kStop);
        else if (onKit_) fill(r, soundX, 48, soundW, 1, cursorColor);
    }
    if (said_.empty()) {
        fonts.small.draw(r, fitX + 2.f, 34.f, "FIT TO", kDim);
        fonts.small.draw(r, chokeX + 2.f, 34.f, "CHOKE", kDim);
        fonts.small.draw(r, waveX + 2.f, 34.f, "THE RECORDING", kDim);
    } else { // over the heads of the three columns at the right, for a moment
        fonts.within(&Fonts::small, said_.c_str(), waveX + waveW - fitX - 4.f).draw(r, fitX + 2.f, 34.f, said_.c_str(), kBrass);
    }
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int y = top + lane * rowH, h = rowH - kLaneGap;
        const Session::LaneSound& sound = session_.laneSound(project_.track, lane);
        const char* file = trk().laneSample[lane];
        const bool here = lane == lane_;
        const float textY = y + (h - fonts.medium.capHeight()) / 2.f;

        const drumsynth::Voice& voice = trk().laneSynth[lane];
        box(r, soundX, y, soundW, h, file[0] || voice.on() ? kPanel2 : kPanel, kLine);
        std::snprintf(text, sizeof text, "%d", lane + 1);
        fonts.small.draw(r, soundX + 7.f, y + (h - fonts.small.capHeight()) / 2.f, text, kDim);
        if (voice.on()) { // a synth voice: its model's name, and SYNTH where a built-in drum says BUILT IN
            fonts.medium.draw(r, soundX + 26.f, textY, drumsynth::modelName(voice.model), kText);
            fonts.small.drawRight(r, soundX + soundW - 8.f, y + (h - fonts.small.capHeight()) / 2.f, "SYNTH", kDim);
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
            fonts.small.drawRight(r, soundX + soundW - 8.f, y + (h - fonts.small.capHeight()) / 2.f, "BUILT IN", kLine);
        }
        if (here && !onKit_ && column_ == kSound) outline(r, soundX - 1, y - 1, soundW + 2, h + 2, cursorColor, 2);

        const int fit = trk().laneFit[lane];
        box(r, fitX, y, fitW, h, kPanel, kLine);
        const bool fits = file[0] || voice.on(); // (a synth voice is fitted as a recording; the built-in drum is not)
        if (!fits) std::snprintf(text, sizeof text, "-");
        else laneFitText(fit, text, sizeof text);
        fonts.medium.draw(r, fitX + 9.f, textY, text, fit > 0 && fits ? kPurple : kDim);
        if (here && !onKit_ && column_ == kFit) outline(r, fitX - 1, y - 1, fitW + 2, h + 2, cursorColor, 2);

        // CHOKE: NONE, or the group, in the colour of the mark on the drum grid's lane name.
        const int group = trk().laneChoke[lane];
        box(r, chokeX, y, chokeW, h, kPanel, kLine);
        fonts.medium.draw(r, chokeX + 9.f, textY, laneChokeText(group), group == kChokeNone ? kDim : kBrass);
        if (here && !onKit_ && column_ == kChoke) outline(r, chokeX - 1, y - 1, chokeW + 2, h + 2, cursorColor, 2);

        // The recording's outline, and what is being done with it.
        if (sound.missing) { // (on two short lines: the column is narrower since CHOKE)
            const char* gone = "FILE NOT FOUND.";
            const char* plays = "THE DRUM PLAYS.";
            const Font& goneFont = fonts.within(&Fonts::small, plays, waveW - 6.f); // (the longer: both lines in its lettering)
            const float lineGap = 4.f, linesH = 2.f * goneFont.capHeight() + lineGap;
            goneFont.draw(r, waveX + 4.f, y + (h - linesH) / 2.f, gone, kBrass);
            goneFont.draw(r, waveX + 4.f, y + (h - linesH) / 2.f + goneFont.capHeight() + lineGap, plays, kBrass);
        } else if (!sound.peaks.empty()) {
            const int count = static_cast<int>(sound.peaks.size()), ww = waveW - 70;
            for (int i = 0; i < count; ++i) {
                const int ph = 1 + static_cast<int>(sound.peaks[static_cast<std::size_t>(i)] * (h - 3));
                fill(r, waveX + ww * i / count, y + (h - ph) / 2, 1, ph, here ? kText : kDim);
            }
            if (lane == headLane_) drawPlayhead(r, head_, lastMs_, waveX, y + 1, ww, h - 2);
            if (sound.shaping) std::snprintf(text, sizeof text, "MAKING");
            else if (sound.waiting) std::snprintf(text, sizeof text, "FITTING");
            else std::snprintf(text, sizeof text, "%.1f S", sound.seconds);
            const bool edited = !trk().laneEdit[lane].atRest(); // (its seconds purple, as a FIT that is on)
            fonts.small.drawRight(r, waveX + waveW - 2.f, y + (h - fonts.small.capHeight()) / 2.f, text, sound.waiting ? kBrass : edited ? kPurple : kDim);
        }
        if (here && !onKit_ && column_ == kEdit) outline(r, waveX - 1, y - 1, waveW + 2, h + 2, cursorColor, 2);
    }
    scopeStrip(r, fonts, session_.engine);

    Hint list[8];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (onKit_) {
        const int now = const_cast<Session&>(session_).kitOf(project_.track);
        if (kitDeletePart_ > 0.f) {
            hint("HOLD B", kKitDeletingWords);
        } else if (holding_) {
            hint("UP / DOWN", "next / previous kit");
            hint("B", "as it was");
        } else {
            hint("HOLD A", "kits");
            if (now < 0) hint("Y", "save this kit");
            else if (const_cast<Session&>(session_).kits()[static_cast<std::size_t>(now)].own) hint("HOLD B", "delete this kit");
            hint("START", project_.playing ? "stop" : "play");
        }
        hints(r, fonts, list, count);
        picker_.draw(r, fonts);
        return;
    }
    if (holding_ && column_ == kEdit) {
        hint("A", "edit"); // (opens when it is let go)
    } else if (holding_) {
        hint("UP / DOWN", column_ == kSound ? "recordings / synths" : column_ == kFit ? "longer / shorter" : "next / previous group");
    } else if (column_ == kEdit) {
        hint("A", "edit");
    } else {
        hint(column_ == kSound ? "A" : "HOLD A", column_ == kSound ? (trk().laneSynth[lane_].on() ? "knobs" : "pick from list") : column_ == kFit ? "fit to steps" : "choke group");
        hint("B", column_ == kSound ? "default drum" : column_ == kFit ? "fit off" : "no choke");
    }
    if (!holding_) {
        hint("Y", "catch");
        hint("R1", "record");
        // A recording on the lane has all six it can hold: START yields to the slices (it is on
        // every other bar, and the top bar shows playing).
        const bool slices = trk().laneSample[lane_][0] != 0;
        if (slices) hint("L1", "slices");
        if (!slices) hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
    picker_.draw(r, fonts); // over everything, when it is open
}

} // namespace apotheca
