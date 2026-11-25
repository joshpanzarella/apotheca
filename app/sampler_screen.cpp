#include "sampler_screen.hpp"
#include "designer.hpp"
#include "samples.hpp"

#include "apotheca/envelope.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
const char* const kNames[sampler::kSettings] = {"PITCH", "TUNE", "START", "ATTACK", "RELEASE", "LOOP", "LEVEL", "END"};
// The order the page shows them in: END beside START (it is the last id: ids are appended).
const int kShown[sampler::kSettings] = {sampler::Root, sampler::Tune, sampler::Start, sampler::End, sampler::Attack, sampler::Release, sampler::Loop, sampler::Level};
}

const char* samplerSettingName(int id) { return id >= 0 && id < sampler::kSettings ? kNames[id] : ""; }

int SamplerScreen::settingAt(int place) { return place >= 0 && place < sampler::kSettings ? kShown[place] : -1; }

void samplerSettingText(int id, int value, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    // Shown as a pitch: semitones above or below the recording as it was made. Kept as the "root key"
    // it always was (the key that plays it as made: middle C less the pitch), so old songs are unchanged.
    if (id == sampler::Root) {
        if (value == kHearKey) std::snprintf(out, n, "0");
        else std::snprintf(out, n, "%+d", kHearKey - value);
    }
    else if (id == sampler::Tune) std::snprintf(out, n, "%+d CENTS", value);
    else if (id == sampler::Attack) {
        const float ms = 1000.f * envelope::attackSeconds(static_cast<float>(value));
        if (ms >= 1000.f) std::snprintf(out, n, "%.1f S", ms / 1000.f);
        else std::snprintf(out, n, "%d MS", static_cast<int>(std::round(ms)));
    } else if (id == sampler::Release) {
        const float ms = 1000.f * envelope::fallSeconds(static_cast<float>(value));
        if (ms >= 1000.f) std::snprintf(out, n, "%.1f S", ms / 1000.f);
        else std::snprintf(out, n, "%d MS", static_cast<int>(std::round(ms)));
    } else if (id == sampler::Loop) std::snprintf(out, n, "%s", value ? "ON" : "OFF");
    else std::snprintf(out, n, "%d%%", value);
}

// Heard at middle C, always: the key ROOT is set to to begin with. So the
// recording is heard at its own pitch until ROOT or TUNE is moved, and then
// the move is heard. (It used to be played at the root key, whatever that
// was, which is the one key where moving ROOT changes nothing.)
//
// X held: it sounds for as long as X is held (to the recording's end), and is let go with X; a tap
// sounds as long as it always did (Session::kAuditionMs). A setting turned while X holds it is
// heard on the held note.
void SamplerScreen::hear(bool held) {
    ++timesHeard_;
    // A line runs across the recording's picture while it sounds: from START, at the speed ROOT and
    // TUNE give middle C, until the key is let go and the release has run.
    const TrackData& t = trk();
    const bool chopped = t.samplerChop && session_.trackSound(project_.track).zones == 0;
    const int key = chopped ? sampler::kChopBase : kHearKey; // chopped: the first slice (middle C is past the last)
    const float speed = chopped ? std::pow(2.f, static_cast<float>(60 - t.sampler[sampler::Root]) / 12.f + static_cast<float>(t.sampler[sampler::Tune]) / 1200.f)
                                : std::pow(2.f, static_cast<float>(kHearKey - t.sampler[sampler::Root]) / 12.f + static_cast<float>(t.sampler[sampler::Tune]) / 1200.f);
    release_ = 0.002f * std::pow(10000.f, static_cast<float>(t.sampler[sampler::Release]) / 100.f);
    headHeld_ = held || (headHeld_ && session_.hearingHeld());
    head_.start(lastMs_, session_.trackSound(project_.track).seconds, static_cast<float>(t.sampler[sampler::Start]) / 100.f, speed,
                headHeld_ ? 0.f : static_cast<float>(Session::kAuditionMs) / 1000.f + release_); // held: until it is let go (update)
    head_.to = endPart(t);
    if (held) session_.auditionHeld(project_.track, key, kPlacedLevel, this);
    else session_.audition(project_.track, key, kPlacedLevel);
}

// The list of recordings: the preview, half of what a note plays at least (START to END at middle
// C), stopped as soon as the cursor moves on; X on it plays on to the end (to END: with LOOP on,
// once round).
void SamplerScreen::hearFromList(SamplePicker::Hear how) {
    if (how == SamplePicker::Hear::Held) return hear(true);
    if (how == SamplePicker::Hear::Quiet) {
        session_.stopHeard(this);
        head_.since = 0; // its line goes with it
        headHeld_ = false;
        return;
    }
    if (how == SamplePicker::Hear::Preview) hear(true); // begun as X begins it, and let go at once (below)
    const float whole = head_.wholeLength();
    const Uint32 ms = static_cast<Uint32>(1000.f * (how == SamplePicker::Hear::Preview ? whole / 2.f : whole));
    const Uint32 lasts = ms > Session::kAuditionMs ? ms : Session::kAuditionMs;
    session_.hearFor(lasts);
    headHeld_ = false;
    head_.lasts = how == SamplePicker::Hear::Preview ? static_cast<float>(lasts) / 1000.f + release_ : 0.f; // (Rest: to END)
}

// A setting, changed: kept, and heard if the song is not playing. Not more
// often than a few times a second, however fast the stick turns it.
void SamplerScreen::setSetting(int id, int value) {
    const int before = trk().sampler[id];
    // START and END never cross: each stops kLeastSpan short of the other.
    value = sampler::heldApart(id, value, trk().sampler[sampler::Start], trk().sampler[sampler::End]);
    session_.setSamplerSetting(project_.track, id, value);
    if (trk().sampler[id] == before || project_.playing || lastMs_ - heardAt_ < kHearEveryMs) return;
    heardAt_ = lastMs_;
    hear();
}

void SamplerScreen::stepSound(int by) {
    if (session_.sampleFiles() != namesSeen_) { // the card changed mid-hold (WiFi): step through what is there now
        namesSeen_ = session_.sampleFiles();
        names_ = session_.sampleNames();
    }
    const int count = static_cast<int>(names_.size()) + 1; // "none" first
    int now = 0;
    for (int i = 0; i < count - 1; ++i)
        if (names_[static_cast<std::size_t>(i)] == trk().sample) now = i + 1;
    const int to = now + by;
    if (to < 0 || to >= count) return;
    session_.setTrackSample(project_.track, to == 0 ? "" : names_[static_cast<std::size_t>(to - 1)]);
    if (!project_.playing) hear();
}

void SamplerScreen::press(Button b) {
    if (!holding_) {
        // SOUND on top; the settings in two rows of four under it; PRESET below those.
        if (stop_ == kPresetStop) {
            if (b == Button::Up) goTo(7);
            return; // (left, right and down: the ends stop)
        }
        if (b == Button::Left) goTo(stop_ - 1);
        if (b == Button::Right) goTo(stop_ + 1);
        if (b == Button::Up) goTo(stop_ == 0 ? 0 : (stop_ <= 4 ? 0 : stop_ - 4));
        if (b == Button::Down) goTo(stop_ == 0 ? 1 : (stop_ > 4 ? kPresetStop : (stop_ + 4 > sampler::kSettings ? sampler::kSettings : stop_ + 4)));
        return;
    }
    if (b != Button::Up && b != Button::Down) return;
    stepped_ = true;
    const int by = b == Button::Up ? 1 : -1;
    if (stop_ == kPresetStop) {
        stepPreset(by);
        return;
    }
    if (stop_ == 0) {
        stepSound(by);
        return;
    }
    const int id = settingAt(stop_ - 1);
    if (id < 0) return; // (a stop that is no setting: settingAt says -1)
    setSetting(id, trk().sampler[id] + (id == sampler::Root ? -by : by)); // PITCH up is the root key down
}

void SamplerScreen::stepPreset(int by) {
    const int count = static_cast<int>(session_.samplerPresets().size());
    if (!count) return;
    const int now = session_.samplerPresetOf(project_.track);
    const int to = now < 0 ? (by > 0 ? 0 : count - 1) : now + by;
    if (to < 0 || to >= count) return; // the ends stop
    // Heard as it is put on, when the song is stopped, as a synth's preset and a kit are (gesture audit G22:
    // stepping the sampler's was silent, so a preset had to be stepped to, then X'd).
    if (session_.loadSamplerPreset(project_.track, to) && !project_.playing) hear();
}

void SamplerScreen::catchBar() {
    const std::string name = session_.catchLastBar(project_.track, 0);
    said_ = name.empty() ? "NOTHING TO CATCH: PLAY SOMETHING FIRST" : "CAUGHT THE LAST BAR AS \"" + name + "\"";
    for (char& c : said_) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    saidUntil_ = lastMs_ + 2500;
}

// Record from the input: once to start (it waits for sound), again to stop.
void SamplerScreen::record() {
    const Session::Rec now = session_.toggleRecording(project_.track, 0);
    if (now == Session::Rec::NoInput) said_ = "NO INPUT FOUND: PLUG IN A USB MICROPHONE OR INTERFACE";
    else if (now == Session::Rec::Idle && session_.lastRecorded().empty()) said_ = "NOTHING RECORDED: NO SOUND CAME IN";
    saidUntil_ = lastMs_ + 3000;
}

void SamplerScreen::openPicker() {
    const int track = project_.track;
    SamplePicker::Jobs jobs;
    jobs.use = [this, track](const std::string& name) { session_.setTrackSample(track, name); };
    jobs.hear = [this](SamplePicker::Hear how) { hearFromList(how); };
    jobs.sounding = [this] { return session_.hearingOn(this) && head_.at(lastMs_) >= 0.f; }; // (its line still running: not ended by itself)
    jobs.playhead = [this] { return &head_; };
    jobs.sound = [this, track] { return &session_.trackSound(track); };
    jobs.remove = [this](const std::string& name) { return session_.deleteSample(name); };
    jobs.move = [this](const std::string& from, const std::string& to) { return session_.moveSample(from, to); };
    jobs.makeFolder = [this](const std::string& name) { return session_.makeSampleFolder(name); };
    jobs.removeFolder = [this](const std::string& name) { return session_.deleteSampleFolder(name); };
    jobs.following = [this] { return session_.following(); };
    jobs.copying = [this] { return session_.carryCopying(); };
    jobs.copied = [this] { return session_.carryResult(); };
    jobs.followFailures = [this] { return session_.followFailures(); };
    jobs.used = [this, track](const std::string& name) { return session_.sampleUses(name) - (name == project_.tracks[track].sample ? 1 : 0); };
    jobs.catchBar = [this] { catchBar(); }; // (an empty list: as Y and R1 on SOUND)
    jobs.record = [this] { record(); };
    picker_.open(session_.sampleNames(), session_.sampleFolders(), trk().sample, "THE SAMPLER: PICK ITS RECORDING", "NO RECORDING", jobs);
    sampleFilesSeen_ = session_.sampleFiles();
}

void SamplerScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (keys_.isOpen()) { // naming a preset to save: every button is the keys'.
        if (event.down) typeButton(b);
        else keys_.letGo(b);
        return;
    }
    if (picker_.isOpen()) {
        if (b == Button::Start && event.down && !input.held(Button::Select) && !picker_.typing()) session_.togglePlay(false);
        else picker_.onButton(event, project_.playing);
        return;
    }
    if (event.down && b != Button::B) presetDeleting_ = false; // (any other button calls a delete off)
    // While a take waits or runs, R1 stops it from any stop, before PRESET's SAVE (gesture audit G34: there
    // R1 opened the name keys and the take ran on).
    if (b == Button::R1 && event.down && session_.recordingNow()) {
        session_.toggleRecording(project_.track, 0);
        return;
    }
    if (b == Button::A) {
        if (!event.down && holding_ && !stepped_ && stop_ == 0) openPicker(); // a plain tap on SOUND: the list
        if (event.down) stepped_ = false;
        holding_ = event.down;
        saved_ = turned_ = 0.f;
        stickWas_ = false;
        loopPush_ = 0;
        if (event.down) names_ = session_.sampleNames(), namesSeen_ = session_.sampleFiles();
        if (event.down && (stop_ == 0 || stop_ == kPresetStop)) session_.keepSound(project_.track); // (what B with A held puts back)
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        if (b == Button::B) presetDeleting_ = false;
        return;
    }
    if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeat_.restart();
    } else if (b == Button::B && holding_) {
        // B with A held on SOUND or PRESET (gesture audit G12): the sound as it was when A went down, its
        // effects too, heard. A is spent: the D-pad and the stick are the cursor's again, its let go no tap.
        if (stop_ == 0 || stop_ == kPresetStop) {
            if (session_.putSoundBack(project_.track) && !project_.playing) hear();
            holding_ = false;
            repeating_ = Button::None;
            saved_ = turned_ = 0.f;
            stickWait_ = input.stickX() != 0.f || input.stickY() != 0.f; // (it was stepping: not off the stop at once)
        }
    } else if (b == Button::B && !holding_) {
        if (stop_ == 0) session_.setTrackSample(project_.track, "");
        else if (stop_ == kPresetStop) { // one's own sound on the track: B kept held deletes its file
            presetDeleting_ = session_.samplerPresetOf(project_.track) >= 0;
            presetDeleteTimed_ = false;
        } else setSetting(settingAt(stop_ - 1), sampler::settingDefault(settingAt(stop_ - 1)));
    } else if (b == Button::Y && !holding_) {
        catchBar();
    } else if (b == Button::R1 && !holding_) {
        if (stop_ == kPresetStop) {
            // Save the sound: the keys come up to name it, on one of the player's own with its name typed in
            // (R1 then B saves over it).
            const int now = session_.samplerPresetOf(project_.track);
            keys_.open(now >= 0 ? session_.samplerPresets()[static_cast<std::size_t>(now)].name : "", kPresetNameLength);
            return;
        }
        record();
    } else if (b == Button::L1 && !holding_) {
        const bool on = !project_.tracks[project_.track].samplerChop;
        session_.setSamplerChop(project_.track, on);
        said_ = on ? "CHOPPED: 16 SLICES FROM C1 UP" : "CHOP OFF: THE WHOLE RECORDING";
        saidUntil_ = lastMs_ + 3000;
    } else if (b == Button::X) {
        hear(true);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

// ---- naming a preset to save -----------------------------------------------------------

// B or START keeps what is typed: the preset is saved, under a name taken replacing that one (and saying
// so). B with nothing typed leaves; START with nothing typed does nothing (the keys say to name it).
void SamplerScreen::typeButton(Button b) {
    const NameKeys::Asked asked = keys_.press(b);
    if (asked == NameKeys::Asked::Nothing) return;
    if (keys_.typed().empty() && asked == NameKeys::Asked::Start) return;
    const std::string name = presetNameFrom(keys_.typed());
    const bool taken = nameTaken();
    keys_.close();
    saved_ = 0.f;
    stickWas_ = false; // the keys' stick is not the knobs': the next push starts over
    if (name.empty()) return; // (nothing typed: B leaves)
    said_ = !session_.saveSamplerPreset(project_.track, name) ? "NOT SAVED" : (taken ? "REPLACED " : "SAVED AS ") + name;
    saidUntil_ = lastMs_ + 2500;
}

void SamplerScreen::closeKeys() {
    if (!keys_.isOpen()) return;
    keys_.close();
    saved_ = 0.f;
    stickWas_ = false; // (as after B or START)
}

void SamplerScreen::callOffDelete() {
    presetDeleting_ = false;
    presetDeletePart_ = 0.f;
    picker_.callOffDelete();
}

bool SamplerScreen::nameTaken() const {
    const std::string name = presetNameFrom(keys_.typed());
    for (const Session::SamplerPreset& p : const_cast<Session&>(session_).samplerPresets())
        if (!name.empty() && p.name == name) return true;
    return false;
}

void SamplerScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (picker_.isOpen() && session_.sampleFiles() != sampleFilesSeen_) { // the card's lists changed (WiFi): read them again, the cursor kept
        sampleFilesSeen_ = session_.sampleFiles();
        picker_.rescan(session_.sampleNames(), session_.sampleFolders());
    }
    if (!input.held(Button::A)) holding_ = false;
    if (!input.held(Button::B) || stop_ != kPresetStop || input.held(Button::A)) presetDeleting_ = false;
    // B kept held on a preset of the player's own: its file is deleted when the bar is full.
    presetDeletePart_ = 0.f;
    if (presetDeleting_) {
        if (!presetDeleteTimed_) presetDeleteTimed_ = true, presetDeleteSince_ = nowMs;
        presetDeletePart_ = static_cast<float>(nowMs - presetDeleteSince_) / static_cast<float>(kPresetDeleteMs);
        if (nowMs - presetDeleteSince_ >= kPresetDeleteMs) {
            presetDeleting_ = false;
            presetDeletePart_ = 0.f;
            const int now = session_.samplerPresetOf(project_.track);
            const std::string name = now >= 0 ? session_.samplerPresets()[static_cast<std::size_t>(now)].name : "";
            said_ = now >= 0 && session_.deleteSamplerPreset(now) ? name + " DELETED. THE TRACK KEEPS ITS SOUND" : "NOT DELETED";
            saidUntil_ = nowMs + 2500;
        }
    }

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    if (headHeld_ && !session_.hearingHeld()) {
        // X let go (App told the Session): the line goes on for what is still heard, and the release.
        headHeld_ = false;
        head_.lasts = (session_.heardEnds() > head_.since ? static_cast<float>(session_.heardEnds() - head_.since) / 1000.f : 0.f) + release_; // (release_ is above 0: never "to the end")
    }
    picker_.update(input, nowMs, project_.playing);
    if (picker_.isOpen()) return;
    if (keys_.isOpen()) {
        keys_.update(input, nowMs, dt);
        return;
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
    if (stickWait_ && input.stickX() == 0.f && input.stickY() == 0.f) stickWait_ = false;
    const float x = stickWait_ ? 0.f : input.stickX(), y = stickWait_ ? 0.f : input.stickY();

    // A held D-pad (and the stick, at twice it): SOUND's recordings with A held at the list's pace, after the cursor's
    // wait they always had; the rest at the cursor's, PRESET and the settings with A held too (gesture audit G08: no
    // held run was made slower than it was).
    const Pace runs = holding_ && stop_ == 0 ? Pace::List : Pace::Cursor;
    if (holding_ && stop_ > 0 && settingAt(stop_ - 1) == sampler::Loop) {
        // LOOP, a choice (off, on): it clicks once a push, up ON and down OFF, as the D-pad does (gesture audit G08:
        // it was turned as a range of one, about 1.3 s of push to flip it). However far it is pushed (owner,
        // 2026-11-25; it was past half), but not by a sideways push that drifts (ui::stickAxes). Back in the middle,
        // or pushed the other way (a flick across can pass the middle between two frames), the next push clicks.
        const float upDown = stickAxes(x, y).y;
        const int way = upDown < 0.f ? 1 : (upDown > 0.f ? -1 : 0);
        if (way != 0 && way != loopPush_) setSetting(sampler::Loop, way > 0 ? 1 : 0);
        loopPush_ = way;
    } else if (holding_ && stop_ > 0 && settingAt(stop_ - 1) >= 0) {
        // A setting: the stick turns it through its whole range in about a second (times STICK SPEED), however far
        // it is pushed (the D-pad for exact steps).
        const int id = settingAt(stop_ - 1);
        const float range = static_cast<float>(sampler::settingMax(id) - sampler::settingMin(id));
        turned_ += -stickWay(y) * range * dt;
        const int whole = static_cast<int>(turned_);
        if (whole != 0) {
            turned_ -= static_cast<float>(whole);
            setSetting(id, trk().sampler[id] + (id == sampler::Root ? -whole : whole));
        }
        if (y == 0.f) turned_ = 0.f;
    } else {
        // One at once, then at twice the held D-pad's pace, however far it is pushed.
        const bool sideways = !holding_ && std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? stickPerSecond(runs) * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f)
                press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
        }
    }
    while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), Pace::Cursor, runs))
        press(repeating_);
}

void SamplerScreen::drawTyping(SDL_Renderer* r, const Fonts& fonts) const {
    // B saves (under a name taken: replaces it, and the bar says so first); with nothing typed it leaves.
    const bool taken = nameTaken(); // (its file is written over: CLAUDE.md, what cannot be undone says so where it is confirmed)
    keys_.draw(r, fonts, taken ? kPresetReplacingWords : "SAVE AS PRESET", taken ? kBrass : kDim, "(empty: name it to save)", keys_.typed().empty() ? "leave" : taken ? "replace" : "save", nullptr);
}

void SamplerScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / SOUND", name);
    topBar(r, fonts, View::Device, text, project_);
    if (keys_.isOpen()) {
        drawTyping(r, fonts); // naming a preset to save: the keys instead of the rows
        return;
    }
    const SDL_Color accent = holding_ ? kBrass : kTeal;
    const TrackData& t = trk();
    const Session::LaneSound& sound = session_.trackSound(project_.track);

    // ---- the recording ------------------------------------------------------------
    const int sx = 8, sy = kSoundY, sw = 624, sh = kSoundH;
    box(r, sx, sy, sw, sh, kPanel, kLine);
    const char* top = said_.empty() ? "SOUND" : said_.c_str(); // (a preset's name at its longest in what is said: held at LARGE at XL)
    fonts.within(&Fonts::small, top, sw - 14.f).draw(r, sx + 7.f, sy + 7.f, top, said_.empty() ? kDim : kBrass);
    char upper[80];
    if (!t.sample[0]) std::snprintf(upper, sizeof upper, "NONE");
    else if (sampleFolderOf(t.sample).empty()) std::snprintf(upper, sizeof upper, "%s", t.sample);
    else std::snprintf(upper, sizeof upper, "%s  /  %s", sampleFolderOf(t.sample).c_str(), sampleLabel(t.sample).c_str()); // the folder it is in, then its name
    for (char* c = upper; *c; ++c) *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    fonts.within(&Fonts::large, upper, sw - 14.f).draw( // (a name that just fits the box at LARGE: held at LARGE at XL)
        r, sx + 7.f, sy + 23.f, upper, stop_ == 0 ? accent : (t.sample[0] ? kText : kDim));
    // With no picture, what is said stands in the middle of the picture's room (not at its top, with
    // the room under it left empty).
    const float wordsY = kWaveY + (kWaveH - fonts.small.capHeight()) / 2.f;
    if (sound.missing) {
        fonts.small.draw(r, sx + 7.f, wordsY, "NOT IN THE SAMPLES FOLDER. THIS TRACK IS SILENT UNTIL IT IS.", kBrass);
    } else if (!t.sample[0]) {
        fonts.small.draw(r, sx + 7.f, wordsY, "Hold A and push up to pick a recording, or press Y to catch the last bar played.", kDim);
    } else if (!sound.peaks.empty()) {
        const int wx = sx + 7, wy = kWaveY, ww = sw - 14, wh = kWaveH;
        wavePicture(r, sound.peaks, wx, wy, ww, wh, static_cast<float>(t.sampler[sampler::Start]) / 100.f, endPart(t)); // what a note plays, lit
        if (t.samplerChop && sound.zones == 0)
            for (int i = 1; i < sampler::kChopSlices; ++i) box(r, wx + ww * i / sampler::kChopSlices, wy, 1, wh, kLine, kLine); // its slices
        drawPlayhead(r, head_, lastMs_, wx, wy - 2, ww, wh + 4);
        if (sound.zones > 0 && sound.layers > 1) std::snprintf(text, sizeof text, "%.1f S  %d ZONES  %d LAYERS", sound.seconds, sound.zones, sound.layers);
        else if (sound.zones > 0) std::snprintf(text, sizeof text, "%.1f S  %d ZONES", sound.seconds, sound.zones);
        else if (t.samplerChop) std::snprintf(text, sizeof text, "%.1f S  CHOP", sound.seconds);
        else std::snprintf(text, sizeof text, "%.1f S", sound.seconds);
        fonts.small.drawRight(r, sx + sw - 8.f, sy + 7.f, text, kDim);
    }
    if (stop_ == 0) outline(r, sx - 1, sy - 1, sw + 2, sh + 2, accent, 2);

    // ---- the settings -------------------------------------------------------------
    for (int place = 0; place < sampler::kSettings; ++place) {
        const int id = settingAt(place);
        const int x = 8 + (place % 4) * 157, y = kSettingY + (place / 4) * (kSettingH + kSettingGap);
        samplerSettingText(id, t.sampler[id], text, sizeof text);
        const float range = static_cast<float>(sampler::settingMax(id) - sampler::settingMin(id));
        valueBox(r, fonts, x, y, 153, kSettingH, kNames[id], text, (id == sampler::Root ? static_cast<float>(sampler::settingMax(id) - t.sampler[id]) : static_cast<float>(t.sampler[id] - sampler::settingMin(id))) / range, stop_ == place + 1,
                 holding_, id == sampler::Tune);
    }

    // ---- the preset -----------------------------------------------------------------
    const int px = 8, py = kPresetY, pw = 624, ph = kPresetH;
    box(r, px, py, pw, ph, kPanel, kLine);
    const int presetNow = const_cast<Session&>(session_).samplerPresetOf(project_.track);
    const char* presetName = presetNow >= 0 ? const_cast<Session&>(session_).samplerPresets()[static_cast<std::size_t>(presetNow)].name.c_str() : "-";
    fonts.small.draw(r, px + 7.f, py + (ph - fonts.small.capHeight()) / 2.f, "PRESET", kDim);
    fonts.within(&Fonts::large, presetName, pw - 170.f).drawCentered( // (held at LARGE when it runs past the box)
        r, px + pw / 2.f, py + (ph - fonts.large.capHeight()) / 2.f, presetName, stop_ == kPresetStop ? accent : (presetNow >= 0 ? kText : kDim));
    if (presetDeletePart_ > 0.f) fill(r, px, py + ph - 2, static_cast<int>(pw * (presetDeletePart_ > 1.f ? 1.f : presetDeletePart_)), 2, kStop);
    if (stop_ == kPresetStop) outline(r, px - 1, py - 1, pw + 2, ph + 2, accent, 2);
    scopeStrip(r, fonts, session_.engine);

    Hint list[8];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (presetDeletePart_ > 0.f) {
        hint("HOLD B", kPresetDeletingWords); // (a file, gone for good: the kit's words)
    } else if (holding_) {
        hint("UP / DOWN", stop_ == 0 ? "next / previous recording" : (stop_ == kPresetStop ? "next / previous preset" : "turn it"));
        if (stop_ == 0 || stop_ == kPresetStop) hint("B", "as it was");
    } else if (stop_ == kPresetStop) {
        hint("HOLD A", "pick one");
        // With an own preset the bar holds five: START yields to the delete (it is on every
        // other bar, and the top bar shows playing).
        const bool own = const_cast<Session&>(session_).samplerPresetOf(project_.track) >= 0;
        if (own) hint("HOLD B", "delete this preset");
        hint("Y", "catch");
        hint("R1", session_.recordingNow() ? "stop" : "save");
        hint("L1", "chop");
        if (!own) hint("START", project_.playing ? "stop" : "play");
    } else {
        hint("HOLD A", stop_ == 0 ? "pick one" : "turn");
        hint("B", stop_ == 0 ? "none" : "default");
        hint("Y", "catch");
        hint("R1", session_.recordingNow() ? "stop" : "record");
        hint("L1", "chop");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
    picker_.draw(r, fonts);
}

} // namespace apotheca
