#include "synth_screen.hpp"

#include "icons.hpp"

#include "mods.hpp"
#include "samples.hpp"

#include "apotheca/cloud.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace apotheca {

using namespace ui;
using namespace phazed;

namespace {

constexpr float kChoicesPerSecond = 5.f; // the stick held over on a choice
constexpr Uint32 kRepeatDelayMs = ui::kCursorDelayMs, kRepeatEveryMs = ui::kCursorEveryMs;
constexpr float kStickStopsPerSecond = 16.f;
constexpr float kMacroPerSecond = 2.f;    // stick fully over: bottom to top in half a second
constexpr float kPresetsPerSecond = 10.f;
constexpr Uint32 kListPreviewMs = 1500, kListRestMs = 4000; // CLOUD's list of recordings: a preview, and X on it


} // namespace

// ---- what is on each page --------------------------------------------------------

// (CLOUD's first page has RECORDING before its settings: SynthSpec::recording.)
int SynthScreen::itemCount() const { return page_ == 0 ? kMainStops : spec_.itemCount(page_) + (spec_.recording && page_ == 1 ? 1 : 0); }

SynthScreen::Item SynthScreen::item(int index) const {
    Item it = {0, ""};
    if (spec_.recording && page_ == 1) {
        if (index == 0) return {kRecordingId, "RECORDING"};
        --index;
    }
    spec_.item(page_, index, it.id, it.name);
    return it;
}

int SynthScreen::columns() const { return envelopePage() ? kEnvSteps : 4; }

int SynthScreen::settingHere() const { return page_ == 0 ? -1 : item(cursor_).id; }

void SynthScreen::goTo(int page, int cursor) {
    page_ = page < 0 ? 0 : (page >= spec_.pages ? spec_.pages - 1 : page);
    const int count = itemCount();
    cursor_ = cursor < 0 ? 0 : (cursor >= count ? count - 1 : cursor);
}

// L1 / R1: the next page, the cursor on the same row and column where it has them, else the nearest
// (its last column, its last stop); never onto CLOUD's RECORDING (firstStop).
void SynthScreen::turnPage(int to) {
    const int cols = columns(), row = cursor_ / cols, col = cursor_ % cols;
    goTo(to, 0);
    const int toCols = columns(), count = itemCount();
    int at = row * toCols + (col < toCols ? col : toCols - 1);
    if (at >= count) at = count - 1;
    cursor_ = at < firstStop(page_) ? firstStop(page_) : at;
}

void SynthScreen::keepPlace() {
    const int t = project_.track;
    if (page_ > 0 && t >= 0 && t < kNumTracks) placePage_[t] = page_, placeStop_[t] = cursor_;
}

// ---- the cursor --------------------------------------------------------------------

void SynthScreen::moveCursor(Button direction) {
    if (page_ == 0) {
        // Four rows: folder and preset; two rows of four macros; ADVANCED.
        const int row = cursor_ < kFirstMacro ? 0 : cursor_ == kAdvanced ? 3 : 1 + (cursor_ - kFirstMacro) / 4;
        const int col = cursor_ < kFirstMacro ? cursor_ * 2 : cursor_ == kAdvanced ? 0 : (cursor_ - kFirstMacro) % 4;
        int r = row, c = col;
        if (direction == Button::Up) r = row > 0 ? row - 1 : 3;
        if (direction == Button::Down) r = row < 3 ? row + 1 : 0;
        if (direction == Button::Left) c = col - 1;
        if (direction == Button::Right) c = col + 1;
        if (r == 0) {
            if (direction == Button::Left || direction == Button::Right) cursor_ = cursor_ == kFolder ? kPreset : kFolder;
            else cursor_ = col >= 2 ? kPreset : kFolder;
        } else if (r == 3) {
            cursor_ = kAdvanced;
        } else {
            c = ((c % 4) + 4) % 4;
            cursor_ = kFirstMacro + (r - 1) * 4 + c;
        }
        return;
    }
    const int count = itemCount(), cols = columns();
    int next = cursor_;
    if (direction == Button::Left) next = cursor_ - 1;
    if (direction == Button::Right) next = cursor_ + 1;
    if (direction == Button::Up) next = cursor_ - cols;
    if (direction == Button::Down) next = cursor_ + cols;
    if (envelopePage() && direction == Button::Down && cursor_ >= kEnvSteps && cursor_ < 2 * kEnvSteps) {
        next = cursor_ < kEnvSteps + 4 ? 2 * kEnvSteps : 2 * kEnvSteps + 1; // from the levels down to HOLD AT or LAST STEP
    }
    if (envelopePage() && direction == Button::Up && cursor_ >= 2 * kEnvSteps) {
        next = cursor_ == 2 * kEnvSteps ? kEnvSteps : 2 * kEnvSteps - 1;
    }
    if (next >= 0 && next < count) cursor_ = next;
}

// ---- naming a preset to save ------------------------------------------------------------

// B or START keeps what is typed: the preset is saved, under a name taken replacing that one (and saying
// so). B with nothing typed leaves; START with nothing typed does nothing (the keys say to name it).
void SynthScreen::typeButton(Button b) {
    const NameKeys::Asked asked = keys_.press(b);
    if (asked == NameKeys::Asked::Nothing) return;
    const std::string name = presetNameFrom(keys_.typed());
    if (keys_.typed().empty() && asked == NameKeys::Asked::Start) return;
    const bool taken = nameTaken();
    keys_.close();
    saved_ = 0.f;
    stickWasX_ = false; // the keys' stick is not the knobs': the next push starts over
    if (name.empty()) return; // (nothing typed: B leaves)
    if (!session_.savePreset(project_.track, name)) say("NOT SAVED", 2500);
    else say((taken ? "REPLACED " : "SAVED AS ") + name, 2500);
}

bool SynthScreen::nameTaken() const {
    const std::string name = presetNameFrom(keys_.typed());
    for (const UserPreset& p : userPresets(trk().kind))
        if (!name.empty() && p.name == name) return true;
    return false;
}

bool SynthScreen::onOwnPreset() const {
    const int own = trk().preset - builtInPresets(trk().kind);
    return own >= 0 && own < static_cast<int>(userPresets(trk().kind).size());
}

// ---- changing the sound ----------------------------------------------------------------

void SynthScreen::hear() {
    if (!project_.playing) session_.audition(project_.track, 60, kPlacedLevel);
}

void SynthScreen::choosePreset(int index) {
    const int count = spec_.presetCount();
    index = ((index % count) + count) % count;
    if (index == trk().preset) return;
    session_.chooseSynthPreset(project_.track, index);
    hear();
}

void SynthScreen::stepPreset(int by) { choosePreset(trk().preset + by); }

// A preset's place in its folder, from 1, and how many the folder holds: the run of presets side
// by side with its folder's name.
int SynthScreen::placeInFolder(int preset, int& of) const {
    const int count = spec_.presetCount();
    of = 0;
    if (preset < 0 || preset >= count) return 0;
    const char* here = spec_.presetCategory(preset);
    int first = preset, last = preset;
    while (first > 0 && std::strcmp(spec_.presetCategory(first - 1), here) == 0) --first;
    while (last + 1 < count && std::strcmp(spec_.presetCategory(last + 1), here) == 0) ++last;
    of = last - first + 1;
    return preset - first + 1;
}

// To the first preset of the next or the previous folder.
void SynthScreen::stepFolder(int by) {
    const int count = spec_.presetCount();
    const char* here = spec_.presetCategory(trk().preset);
    int i = trk().preset;
    // One folder holds every preset (FM-6's ROM 1A, with none of the player's own): nowhere to go.
    // Without this the walks below went round for good and the program froze (owner, 2026-11-15).
    bool oneFolder = true;
    for (int p = 0; p < count && oneFolder; ++p) oneFolder = std::strcmp(spec_.presetCategory(p), here) == 0;
    if (oneFolder) return;
    if (by > 0) {
        while (std::strcmp(spec_.presetCategory(i), here) == 0) i = (i + 1) % count;
    } else {
        // Back past the start of this folder, then to the start of the one before.
        while (std::strcmp(spec_.presetCategory((i + count - 1) % count), here) == 0) i = (i + count - 1) % count;
        i = (i + count - 1) % count;
        const char* before = spec_.presetCategory(i);
        while (std::strcmp(spec_.presetCategory((i + count - 1) % count), before) == 0) i = (i + count - 1) % count;
    }
    choosePreset(i);
}

// Through the session, so that with WRITE on and the song playing the move is recorded into the pattern.
void SynthScreen::setMacro(int macro, float value) { session_.setSynthMacro(project_.track, macro, value); }

// (Through settingWrite: an FM-6 operator's ON is a bit of the synth's OPERATORS ON.)
void SynthScreen::setSetting(int id, int value) {
    int realId = id, realValue = value;
    settingWrite(spec_, values(), id, value, realId, realValue);
    if (realValue == values()[realId]) return;
    values()[realId] = realValue;
    Command c;
    c.type = Command::SetSetting;
    c.track = project_.track;
    c.a = realId;
    c.b = realValue;
    session_.engine.send(c);
}

int SynthScreen::valueOf(int id) const { return settingValue(spec_, values(), id); }

bool SynthScreen::isChoice(int id) const { return id >= 0 && spec_.settingMax(id) - spec_.settingMin(id) <= kChoiceRange; }

// ---- CLOUD's recording (SynthSpec::recording) -------------------------------------------------

void SynthScreen::say(const std::string& words, Uint32 ms) {
    said_ = words;
    for (char& c : said_) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    saidUntil_ = lastMs_ + ms;
}

// The next or the one before in the list of recordings ("none", the TONE, first), heard at once when
// the song is stopped. The ends stop.
void SynthScreen::stepRecording(int by) {
    stepped_ = true;
    if (session_.sampleFiles() != namesSeen_) { // the card changed mid-hold (WiFi): step through what is there now
        namesSeen_ = session_.sampleFiles();
        names_ = session_.sampleNames();
    }
    const int count = static_cast<int>(names_.size()) + 1;
    int now = 0;
    for (int i = 0; i < count - 1; ++i)
        if (names_[static_cast<std::size_t>(i)] == trk().sample) now = i + 1;
    const int to = now + by;
    if (to < 0 || to >= count) return;
    session_.setTrackSample(project_.track, to == 0 ? "" : names_[static_cast<std::size_t>(to - 1)]);
    hear();
}

// The list's preview: the cloud at middle C for a moment (Session::kAuditionMs and more), stopped as
// soon as the cursor moves on; X held, as long as X is; X while it plays, on a while.
void SynthScreen::hearFromList(SamplePicker::Hear how) {
    if (how == SamplePicker::Hear::Quiet) return session_.stopHeard(this);
    if (how == SamplePicker::Hear::Rest) return session_.hearFor(kListRestMs);
    session_.auditionHeld(project_.track, 60, kPlacedLevel, this);
    if (how == SamplePicker::Hear::Preview) session_.hearFor(kListPreviewMs);
}

void SynthScreen::openPicker() {
    const int track = project_.track;
    SamplePicker::Jobs jobs;
    jobs.use = [this, track](const std::string& name) { session_.setTrackSample(track, name); };
    jobs.hear = [this](SamplePicker::Hear how) { hearFromList(how); };
    jobs.sounding = [this] { return session_.hearingOn(this); };
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
    picker_.open(session_.sampleNames(), session_.sampleFolders(), trk().sample, "CLOUD: PICK ITS RECORDING", "THE BUILT-IN TONE", jobs);
    sampleFilesSeen_ = session_.sampleFiles();
}

void SynthScreen::turn(float amount) {
    if (amount == 0.f) return;
    if (page_ == 0 || onRecording()) {
        if (page_ == 0 && cursor_ >= kFirstMacro && cursor_ < kAdvanced) {
            setMacro(cursor_ - kFirstMacro, trk().macro[cursor_ - kFirstMacro] + amount * kMacroPerSecond);
            return;
        }
        if (onRecording()) { // CLOUD's RECORDING: one at a time, as the presets
            saved_ += amount * kPresetsPerSecond;
            for (; saved_ >= 1.f; saved_ -= 1.f) stepRecording(1);
            for (; saved_ <= -1.f; saved_ += 1.f) stepRecording(-1);
            return;
        }
        if (cursor_ == kAdvanced) return;
        // Folder and preset step one at a time; up is the next one.
        saved_ += amount * kPresetsPerSecond;
        for (; saved_ >= 1.f; saved_ -= 1.f) cursor_ == kFolder ? stepFolder(1) : stepPreset(1);
        for (; saved_ <= -1.f; saved_ += 1.f) cursor_ == kFolder ? stepFolder(-1) : stepPreset(-1);
        return;
    }
    const int id = item(cursor_).id;
    // A setting with a handful of values (a wave, a type, on / off) is a choice: it clicks to the
    // next one at each push of the stick, and then steadily while the stick is held over.
    if (isChoice(id)) {
        if (!choicePushed_) {
            choicePushed_ = true;
            saved_ = 0.f;
            setSetting(id, valueOf(id) + (amount > 0.f ? 1 : -1));
            return;
        }
        saved_ += amount * kChoicesPerSecond;
        const int steps = static_cast<int>(saved_);
        if (steps != 0) {
            saved_ -= static_cast<float>(steps);
            setSetting(id, valueOf(id) + steps);
        }
        return;
    }
    // Any other setting: its whole range in about two thirds of a second.
    saved_ += amount * 1.5f * static_cast<float>(spec_.settingMax(id) - spec_.settingMin(id) + 1);
    const int whole = static_cast<int>(saved_);
    if (whole != 0) {
        saved_ -= static_cast<float>(whole);
        setSetting(id, valueOf(id) + whole);
    }
}

// The D-pad with A held. Up and down: one. Left and right: one as well, but on PRESET eight at
// a time, as in the list of recordings: there are dozens of presets and one at a time is slow.
void SynthScreen::stepHeld(Button direction) {
    const int way = direction == Button::Up || direction == Button::Right ? 1 : -1;
    const bool sideways = direction == Button::Left || direction == Button::Right;
    if (page_ == 0 && cursor_ == kPreset && sideways) {
        const int to = trk().preset + way * kPresetJump, last = spec_.presetCount() - 1;
        choosePreset(to < 0 ? 0 : (to > last ? last : to)); // the ends stop
    } else {
        stepExact(way);
    }
}

void SynthScreen::stepExact(int by) {
    if (page_ == 0) {
        if (cursor_ == kFolder) stepFolder(by);
        else if (cursor_ == kPreset) stepPreset(by);
        else if (cursor_ < kAdvanced) {
            // Twentieths, landing on them, so the middle is easy to find again.
            const float now = trk().macro[cursor_ - kFirstMacro] * 20.f;
            setMacro(cursor_ - kFirstMacro, (by > 0 ? std::floor(now + 1.001f) : std::ceil(now - 1.001f)) / 20.f);
        }
        return;
    }
    const int id = item(cursor_).id;
    if (id == kRecordingId) return stepRecording(by);
    setSetting(id, valueOf(id) + by);
}

// ---- buttons ----------------------------------------------------------------------------

void SynthScreen::endMod(const Input& input) {
    if (!modKnob_.held()) return;
    modKnob_.letGo();
    stickWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
    repeating_ = Button::None;
}

void SynthScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (keys_.isOpen()) { // naming a preset to save: every button is the keys'.
        if (event.down) typeButton(b);
        else keys_.letGo(b);
        return;
    }
    if (event.down && b != Button::B) presetDeleting_ = false; // (any other button calls a delete off)
    if (picker_.isOpen()) { // CLOUD's list of recordings: every button is its own but START
        if (b == Button::Start && event.down && !input.held(Button::Select) && !picker_.typing()) session_.togglePlay(false);
        else picker_.onButton(event, project_.playing);
        return;
    }
    // CLOUD: while a take waits or runs, R1 stops it from any page and stop, before SAVE on PRESET and a page
    // turned (gesture audit G34: off RECORDING R1 turned the page and the take ran on).
    if (b == Button::R1 && event.down && spec_.recording && session_.recordingNow()) {
        session_.toggleRecording(project_.track, 0);
        return;
    }
    // X: hear the sound. Held on a macro (MAIN, A not held), it is also the macro's MOD: the X-held
    // check comes before every other button's own job (Y's roll, B's middle, the D-pad's cursor).
    if (b == Button::X) {
        if (!event.down) {
            endMod(input);
            return;
        }
        const bool onMacro = page_ == 0 && cursor_ >= kFirstMacro && cursor_ < kAdvanced && !holding_;
        if (onMacro && modKnob_.hold(project_.track, kModFirstMacro + (cursor_ - kFirstMacro), input)) repeating_ = Button::None;
        if (onMacro && !project_.playing) session_.auditionHeld(project_.track, 60, kPlacedLevel, this); // heard as long as X is (a tap as ever)
        else hear();
        return;
    }
    if (modKnob_.held() && modKnob_.onButton(event, lastMs_)) return;
    // A tap of A on CLOUD's RECORDING: the list of recordings. Held, it turns to the next one.
    if (b == Button::A && onRecording()) {
        if (!event.down && holding_ && !stepped_) openPicker();
        if (event.down) stepped_ = false, names_ = session_.sampleNames(), namesSeen_ = session_.sampleFiles();
        if (event.down) session_.keepSound(project_.track); // (what B with A held puts back)
        holding_ = event.down;
        saved_ = 0.f;
        return;
    }
    if (b == Button::A) {
        if (event.down && page_ == 0 && cursor_ == kAdvanced) {
            // The page and stop this track was last on; the first time, page 1 at its first stop.
            const int t = project_.track;
            if (t >= 0 && t < kNumTracks && placePage_[t] > 0) goTo(placePage_[t], placeStop_[t]);
            else goTo(1, firstStop(1));
            return; // A opened the pages; it is not holding anything
        }
        if (event.down && page_ == 0 && (cursor_ == kFolder || cursor_ == kPreset)) session_.keepSound(project_.track); // (what B with A held puts back)
        holding_ = event.down;
        saved_ = 0.f;
        return;
    }
    // Y on MAIN: a new sound from the dice, on a synth that has them (PHYSIC: SynthSpec::roll). At once,
    // and it is heard. Not with A held (turning), and on the pages nothing.
    // Y on CLOUD's first page: CATCH, the last bar the song played as this track's recording.
    if (b == Button::Y && event.down && spec_.recording && page_ == 1 && !holding_) {
        const std::string name = session_.catchLastBar(project_.track, 0);
        say(name.empty() ? std::string("NOTHING TO CATCH: PLAY SOMETHING FIRST") : "CAUGHT THE LAST BAR AS \"" + name + "\"", 2500);
        return;
    }
    if (b == Button::Y && event.down && spec_.roll && page_ == 0 && !holding_) {
        if (session_.rollSynth(project_.track, lastMs_ * 2654435761u + (++rolls_) * 40503u)) hear();
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        if (b == Button::B) presetDeleting_ = false;
        return;
    }
    if (isDirection(b)) {
        if (holding_) {
            stepHeld(b);
        } else {
            moveCursor(b);
        }
        repeating_ = b;
        repeatAt_ = 0;
        return;
    }
    if (b == Button::B && holding_) {
        // B with A held on FOLDER, PRESET or CLOUD's RECORDING (gesture audit G12): the sound as it was when
        // A went down (its settings, macros, effects and recording), heard. A is spent: the D-pad and the
        // stick are the cursor's again, and its let go is no tap (no list).
        if ((page_ == 0 && (cursor_ == kFolder || cursor_ == kPreset)) || onRecording()) {
            if (session_.putSoundBack(project_.track)) hear();
            holding_ = false;
            repeating_ = Button::None;
            saved_ = 0.f;
        }
        return;
    }
    if (b == Button::B && !holding_) {
        if (page_ > 0) keepPlace(), goTo(0, kAdvanced); // back to MAIN, on the way in (the place kept for A)
        else if (cursor_ == kPreset) { // one of the player's own: B kept held deletes its file (as a kit's)
            presetDeleting_ = onOwnPreset();
            presetDeleteTimed_ = false;
            presetDeleteTrack_ = project_.track;
        } else if (cursor_ >= kFirstMacro && cursor_ < kAdvanced) {
            // Back to the middle, and whatever automation it had in this cell goes.
            const bool writing = session_.writing();
            session_.setWriting(false); // this is a reset, not a move to record
            session_.clearAutomation(project_.track, project_.scene, cursor_ - kFirstMacro);
            setMacro(cursor_ - kFirstMacro, 0.5f);
            session_.setWriting(writing);
        }
    } else if (b == Button::R1 && onRecording() && !holding_) {
        // RECORD on CLOUD's RECORDING (as the sampler's R1): once to start (it waits for sound), again to stop.
        const Session::Rec now = session_.toggleRecording(project_.track, 0);
        if (now == Session::Rec::NoInput) say("NO INPUT FOUND: PLUG IN A USB MICROPHONE OR INTERFACE", 3000);
        else if (now == Session::Rec::Idle && session_.lastRecorded().empty()) say("NOTHING RECORDED: NO SOUND CAME IN", 3000);
    } else if (b == Button::R1 && page_ == 0 && cursor_ == kPreset && !holding_) {
        // Save the sound: the keys come up to name it. On one of the player's own its name is typed in, so
        // R1 then B saves over it.
        keys_.open(onOwnPreset() ? spec_.presetName(trk().preset) : "", kPresetNameLength);
    } else if (b == Button::R1 && page_ > 0) {
        turnPage(page_ == spec_.pages - 1 ? 1 : page_ + 1);
    } else if (b == Button::L1 && page_ > 0) {
        turnPage(page_ == 1 ? spec_.pages - 1 : page_ - 1);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void SynthScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (picker_.isOpen() && session_.sampleFiles() != sampleFilesSeen_) { // the card's lists changed (WiFi): read them again, the cursor kept
        sampleFilesSeen_ = session_.sampleFiles();
        picker_.rescan(session_.sampleNames(), session_.sampleFolders());
    }
    if (!input.held(Button::A)) holding_ = false;
    keepPlace(); // (on an advanced page: where A on ADVANCED comes back to)
    // B kept held on one of the player's own presets: its file is deleted when the bar is full. (Let go, A
    // held, off PRESET or another track up: called off.)
    if (!input.held(Button::B) || input.held(Button::A) || page_ != 0 || cursor_ != kPreset || project_.track != presetDeleteTrack_ || keys_.isOpen()) presetDeleting_ = false;
    presetDeletePart_ = 0.f;
    if (presetDeleting_) {
        if (!presetDeleteTimed_) presetDeleteTimed_ = true, presetDeleteSince_ = nowMs;
        presetDeletePart_ = static_cast<float>(nowMs - presetDeleteSince_) / static_cast<float>(kPresetDeleteMs);
        if (nowMs - presetDeleteSince_ >= kPresetDeleteMs) {
            presetDeleting_ = false;
            presetDeletePart_ = 0.f;
            const std::string name = onOwnPreset() ? spec_.presetName(trk().preset) : "";
            say(!name.empty() && session_.deleteSynthPreset(project_.track) ? name + " DELETED. THE TRACK KEEPS ITS SOUND" : "NOT DELETED", 2500);
        }
    }
    // Let go when X is up, off MAIN, or when another track is up (two tracks of one synth share this
    // screen; picked on SONG with X still held): the gesture is the knob's on the track shown.
    if (!input.held(Button::X) || page_ != 0 || project_.track != modKnob_.track()) endMod(input);

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    const float x = input.stickX(), y = input.stickY();
    if (keys_.isOpen()) {
        keys_.update(input, nowMs, dt);
        return;
    }
    if (modKnob_.held()) {
        modKnob_.update(input, nowMs, dt); // the stick and the D-pad are the MOD's; the cursor stays on the knob
        return;
    }
    if (spec_.recording) {
        picker_.update(input, nowMs, project_.playing);
        if (picker_.isOpen()) return;
        // While recording, the line says how it is going (as the sampler's).
        if (session_.recordingNow()) {
            say(session_.recordingLine(), 3000);
            wasRecording_ = true;
        } else if (wasRecording_) {
            wasRecording_ = false; // it has just stopped, by R1 or at kMaxSeconds (20 s)
            say(session_.lastRecorded().empty() ? std::string("NOTHING RECORDED: NO SOUND CAME IN") : "RECORDED AS \"" + session_.lastRecorded() + "\"", 3000);
        }
    }
    if (!said_.empty() && nowMs >= saidUntil_) said_.clear(); // every synth says things (R1 saves); not only ones that record
    if (stickWait_ && x == 0.f && y == 0.f) stickWait_ = false;

    if (stickWait_) {
        // (the stick was pushed for the MOD: it moves nothing until it has come back)
    } else if (holding_) {
        const float push = -y;
        turn(push * std::fabs(push) * dt);
        if (push == 0.f) choicePushed_ = false; // back in the middle: the next push is a new click
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else {
        choicePushed_ = false;
        if (x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(x) * kStickStopsPerSecond * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) moveCursor(x > 0.f ? Button::Right : Button::Left);
        }
        if (y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? std::fabs(y) * kStickStopsPerSecond * dt : 1.f;
            stickWasY_ = true;
            for (; stickY_ >= 1.f; stickY_ -= 1.f) moveCursor(y > 0.f ? Button::Down : Button::Up);
        }
    }

    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        if (holding_) stepHeld(repeating_);
        else moveCursor(repeating_);
    }
}

// ---- drawing ----------------------------------------------------------------------------


// CLOUD's picture on MAIN: the recording's outline (the TONE's, dim, with none), lit within SPRAY of
// POSITION (both as the knobs have them now, macros on), POSITION a brass line, and each grain
// sounding a teal tick at its place, top and bottom (Engine::grainPlaces, as of the last chunk).
void SynthScreen::drawTyping(SDL_Renderer* r, const Fonts& fonts) const {
    // B saves (under a name taken: replaces it, and the bar says so first); with nothing typed it leaves.
    keys_.draw(r, fonts, "SAVE AS PRESET", kDim, "(empty: name it to save)", keys_.typed().empty() ? "leave" : nameTaken() ? "replace" : "save", nullptr);
}

void SynthScreen::drawPicture(SDL_Renderer* r, const Fonts& fonts) const {
    const int x = 8, y = kPictureY, w = kScreenW - 16, h = kPictureH;
    box(r, x, y, w, h, kPanel, kLine);
    const TrackData& t = trk();
    const Session::LaneSound& sound = session_.trackSound(project_.track);
    const bool tone = !t.sample[0] || sound.missing || sound.peaks.empty();
    const std::vector<float>& peaks = tone ? cloudToneOutline() : sound.peaks;
    const float position = static_cast<float>(cloud::positionShare(cloud::macroBent(static_cast<float>(t.cloud.value[cloud::Position]), session_.automated(project_.track, cloud::MacroPosition, t.macro[cloud::MacroPosition]), 0.f, 100.f)));
    const float spray = static_cast<float>(cloud::sprayShare(cloud::macroBent(static_cast<float>(t.cloud.value[cloud::Spray]), session_.automated(project_.track, cloud::MacroSpray, t.macro[cloud::MacroSpray]), 0.f, 100.f)));
    const int px = x + 2, py = y + 2 + kTickH, pw = w - 4, ph = h - 4 - 2 * kTickH;
    const int count = static_cast<int>(peaks.size());
    for (int i = 0; i < count; ++i) {
        const float at = (i + 0.5f) / count;
        float off = at - position;
        if (off > 0.5f) off -= 1.f; // the recording has no edges: the band goes round
        if (off < -0.5f) off += 1.f;
        const bool lit = std::fabs(off) <= spray + 0.5f / count;
        const int colH = 1 + static_cast<int>(peaks[static_cast<std::size_t>(i)] * (ph - 1));
        fill(r, px + pw * i / count, py + (ph - colH) / 2, pw / count > 0 ? pw / count : 1, colH, lit && !tone ? kText : (lit ? kDim : kLine));
    }
    fill(r, px + static_cast<int>(position * (pw - 2)), py - 2, 2, ph + 4, kBrass); // POSITION
    float places[kGrainsShown];
    const int grains = session_.engine.grainPlaces(project_.track, places, kGrainsShown);
    for (int g = 0; g < grains; ++g) {
        const int gx = px + static_cast<int>(places[g] * (pw - 2));
        fill(r, gx, y + 2, 2, kTickH - 1, kTeal);
        fill(r, gx, y + h - 1 - kTickH, 2, kTickH - 1, kTeal);
    }
    std::string label = !t.sample[0] ? std::string("TONE (BUILT IN)") : sound.missing ? std::string("FILE NOT FOUND: THE TONE PLAYS") : sampleLabel(t.sample);
    for (char& c : label) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    fonts.fixed().small.draw(r, x + 8.f, y + 2.f + kTickH, label.c_str(), tone ? kDim : kText); // (a text over a picture: the tight row's lettering)
}

// MORPH's picture on MAIN (wavetable-plan.md, section 5): the wave POSITION A reads, teal, and B's
// behind it, purple, when B is on, each one cycle across the box; under them a strip with a mark a
// frame and POSITION A in brass (the knob as it is now, macro on). While a key sounds the waves are the
// ones the newest key reads (the MOD ENV and a MOD on POSITION move them: Engine::grainPlaces, MORPH's
// places), and its place is a teal tick on the strip: a player who cannot hear sees the morph.
// (The tables are made on a helper thread as the program starts; drawn before that is done, this
// waits for it, as a MORPH track added before then does.)
void SynthScreen::drawTablePicture(SDL_Renderer* r, const Fonts& fonts) const {
    const int x = 8, y = kPictureY, w = kScreenW - 16, h = kPictureH;
    box(r, x, y, w, h, kPanel, kLine);
    Morph::makeTables();
    const TrackData& t = trk();
    const int* v = t.morph.value;
    const int tableA = v[morph::TableA], tableB = morph::tableOfB(v[morph::TableB]);
    const int frames = Morph::tableFrames(tableA);
    const float macro = session_.automated(project_.track, morph::MacroPosition, t.macro[morph::MacroPosition]);
    const auto knob = [&](int id) { return morph::positionPlace(morph::macroBent(static_cast<float>(v[id]), macro, 0.f, 100.f), frames); };
    const double knobA = knob(morph::PositionA);
    double placeA = knobA, placeB = knob(morph::PositionB);
    float places[kGrainsShown];
    const int n = session_.engine.grainPlaces(project_.track, places, kGrainsShown);
    const bool sounding = n >= 2;
    if (sounding) {
        placeA = places[0] * (frames - 1);
        if (places[n / 2] >= 0.f) placeB = places[n / 2] * (frames - 1);
    }
    // The waves, each a line across the box.
    const int wx = x + 2, ww = w - 4, wy = y + 3, wh = h - 6 - kFrameStripH;
    fill(r, wx, wy + wh / 2, ww, 1, kLine);
    const auto wave = [&](int table, double place, SDL_Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        int lastX = wx, lastY = 0;
        for (int i = 0; i <= ww - 1; ++i) {
            const float s = Morph::tableRead(table, place, static_cast<double>(i) / (ww - 1));
            const int px = wx + i, py = wy + wh / 2 - static_cast<int>(std::lround(s * (wh / 2 - 1)));
            if (i > 0) SDL_RenderDrawLine(r, lastX, lastY, px, py);
            lastX = px, lastY = py;
        }
    };
    if (tableB >= 0) wave(tableB, placeB, kPurple);
    wave(tableA, placeA, kTeal);
    // The strip: a dim mark a frame, POSITION A in brass, the sounding place a teal tick.
    const int sx = x + 2, sw = w - 4, sy = y + h - 2 - kFrameStripH;
    fill(r, sx, sy, sw, 1, kLine);
    for (int f = 0; f < frames; ++f) fill(r, sx + (sw - 2) * f / (frames - 1), sy + 2, 1, kFrameStripH - 4, kDim);
    fill(r, sx + static_cast<int>(knobA / (frames - 1) * (sw - 2)), sy, 2, kFrameStripH, kBrass);
    if (sounding) fill(r, sx + static_cast<int>(places[0] * (sw - 2)), sy + 1, 2, kFrameStripH - 2, kTeal);
    char label[40];
    if (tableB >= 0) std::snprintf(label, sizeof label, "%s / %s", morph::tableName(tableA), morph::tableName(tableB));
    else std::snprintf(label, sizeof label, "%s", morph::tableName(tableA));
    fonts.fixed().small.draw(r, x + 8.f, y + 4.f, label, kText); // (a text over a picture: the tight row's lettering)
}

void SynthScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    drawScreen(r, fonts);
    if (picker_.isOpen()) picker_.draw(r, fonts); // CLOUD's list of recordings, over everything
}

void SynthScreen::drawScreen(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / %s", name, spec_.pageNames[page_]);
    if (!said_.empty()) std::snprintf(text, sizeof text, "%s", said_.c_str()); // (CATCH, RECORD: for a moment)
    topBar(r, fonts, View::Device, text, project_);
    if (keys_.isOpen()) {
        drawTyping(r, fonts); // naming a preset to save: the keys instead of the rows
        return;
    }
    const TrackData& t = trk();
    const SDL_Color cursorColor = holding_ ? kBrass : kTeal;

    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };

    if (page_ == 0) {
        // ---- MAIN: folder, preset, eight macros, ADVANCED, and the scope ----------
        displayBox(r, fonts, kFolderX, kPresetY, kFolderW, kPresetH, "FOLDER", spec_.presetCategory(t.preset), cursor_ == kFolder, holding_);
        char place[24];
        std::snprintf(text, sizeof text, "%s", spec_.presetName(t.preset));
        int inFolder = 0;
        const int placeHere = placeInFolder(t.preset, inFolder);
        std::snprintf(place, sizeof place, "%d / %d", placeHere, inFolder);
        const float rowY = kPresetY + 7.f, countRight = kPresetX + kPresetW - 10.f;
        if (const char* note = folderNote()) {
            // PRESET, the folder's note and the count share the top row: one lettering for all three.
            const float room = countRight - (kPresetX + 7.f) - fonts.small.width("PRESET") - fonts.small.width(kCountLongest) - 2.f * kNoteGap;
            const Font& row = fonts.within(&Fonts::small, note, room);
            const float y = rowY; // (a row held at LARGE is where LARGE draws it)
            displayBox(r, fonts, kPresetX, kPresetY, kPresetW, kPresetH, "", text, cursor_ == kPreset, holding_);
            row.draw(r, kPresetX + 7.f, y, "PRESET", kDim);
            row.draw(r, kPresetX + 7.f + row.width("PRESET") + kNoteGap, y, note, kDim);
            row.drawRight(r, countRight, y, place, kDim);
        } else {
            displayBox(r, fonts, kPresetX, kPresetY, kPresetW, kPresetH, "PRESET", text, cursor_ == kPreset, holding_);
            fonts.small.drawRight(r, countRight, rowY, place, kDim);
        }
        // B held on one of the player's own: the bar along PRESET's foot fills (as the sampler's).
        if (presetDeletePart_ > 0.f) fill(r, kPresetX, kPresetY + kPresetH - 2, static_cast<int>(kPresetW * (presetDeletePart_ > 1.f ? 1.f : presetDeletePart_)), 2, kStop);

        for (int m = 0; m < kMacros; ++m) {
            const int x = 8 + (m % 4) * kMacroPitchX, y = macroY() + (m / 4) * (macroH() + kMacroGap);
            // While the song plays an automated macro shows where the pattern has it, not where its knob rests.
            const float v = session_.automated(project_.track, m, t.macro[m]);
            spec_.macroText(m, v, text, sizeof text);
            // A MOD on it runs: the brass tick across the dial where the macro is given this moment (batch 6).
            macroBox(r, fonts, x, y, kMacroW, macroH(), spec_.macroNames[m], text, v, cursor_ == kFirstMacro + m, holding_,
                     modMarkAt(session_, project_.track, kModFirstMacro + m, v));
            // A purple bar along the bottom: this macro has automation in the cell being worked on.
            if (session_.hasAutomation(project_.track, project_.scene, m)) fill(r, x + 1, y + macroH() - 3, kMacroW - 2, 2, kPurple);
        }
        if (spec_.recording) drawPicture(r, fonts);
        if (spec_.table) drawTablePicture(r, fonts);
        // ADVANCED at the left, down to the hints bar, and beside it SIGNAL as a large scope (design A: where the
        // words that said what ADVANCED holds were; the strip under them is the scope's foot now).
        const int advancedTop = advancedY(), advancedH = kScopeY + kScopeH - advancedTop;
        box(r, 8, advancedTop, kMacroW, advancedH, kPanel2, kLine);
        const float middle = advancedTop + advancedH / 2.f;
        fonts.medium.drawCentered(r, 8.f + kMacroW / 2.f, middle - fonts.medium.capHeight() - 3.f, "ADVANCED", cursor_ == kAdvanced ? kTeal : kText);
        std::snprintf(text, sizeof text, "%d PAGES  >", spec_.pages - 1);
        fonts.small.drawCentered(r, 8.f + kMacroW / 2.f, middle + 5.f, text, kDim);
        if (cursor_ == kAdvanced) outline(r, 7, advancedTop - 1, kMacroW + 2, advancedH + 2, kTeal, 2);
        if (modKnob_.held()) {
            // X held on a macro: its MOD's fields in SIGNAL's place, each button under its field.
            const int m = modKnob_.target() - kModFirstMacro;
            modKnob_.drawFields(r, fonts, kDeviceModPlaces, 0.f, kScopeX, kScopeY, kScopeW, kScopeH, spec_.macroNames[m]);
            modKnob_.drawHints(r, fonts, kDeviceModPlaces);
            return;
        }
        scopeBox(r, fonts, session_.engine, kMainScopeX, advancedY(), kMainScopeW, kScopeY + kScopeH - advancedY());

        if (presetDeletePart_ > 0.f) {
            hint("HOLD B", kPresetDeletingWords);
        } else if (holding_) {
            if (cursor_ == kFolder) hint("UP / DOWN", "next / previous folder");
            else if (cursor_ == kPreset) { hint("UP / DOWN", "next / previous preset"); hint("LEFT / RIGHT", "8 at a time"); }
            else hint("UP / DOWN", "turn it");
            if (cursor_ == kFolder || cursor_ == kPreset) hint("B", "as it was");
        } else {
            if (cursor_ == kAdvanced) hint("A", "open");
            else hint("HOLD A", cursor_ < kFirstMacro ? "change" : "turn");
            if (cursor_ == kPreset) hint("R1", spec_.recording && session_.recordingNow() ? "stop" : "save");
            if (cursor_ == kPreset && onOwnPreset()) hint("HOLD B", "delete this preset");
            if (cursor_ >= kFirstMacro && cursor_ < kAdvanced) hint("B", "back to the middle");
            // (Where the bar has room for it with nothing left out. The help card says it everywhere.)
            if (cursor_ >= kFirstMacro && cursor_ < kAdvanced && modTargetFits(t, kModFirstMacro + cursor_ - kFirstMacro)) hint("HOLD X", "mod");
            // PHYSIC's roll: said off the macros, where the bar has room (on a macro B and HOLD X fill it).
            if (spec_.roll && (cursor_ < kFirstMacro || cursor_ >= kAdvanced)) hint("Y", "new sound");
            // On one of the player's own START yields to the delete, as on the sampler's PRESET (it is on every
            // other bar, and the top bar shows playing): PHYSIC's bar ran 32 pixels over.
            if (!(cursor_ == kPreset && onOwnPreset())) hint("START", project_.playing ? "stop" : "play");
        }
        hints(r, fonts, list, count);
        return;
    }

    // ---- ADVANCED ----------------------------------------------------------------------
    std::snprintf(text, sizeof text, "PAGE %d OF %d", page_, spec_.pages - 1);
    fonts.small.drawRight(r, 632.f, 38.f, text, kDim);
    fonts.medium.draw(r, 8.f, 36.f, spec_.pageNames[page_], kText);

    if (!envelopePage()) {
        const int rows = (itemCount() + 3) / 4, rowH = settingH(rows);
        // A box too short for the lettering chosen (FM-6's six rows at XL: the value stood on its bar) is
        // drawn at LARGE, its name with it, and so is every box of the page: they are one height.
        const Fonts& boxFonts = boxFontsFor(fonts, rowH);
        for (int i = 0; i < itemCount(); ++i) {
            const Item it = item(i);
            const int x = 8 + (i % 4) * kMacroPitchX, y = settingY(rows, i / 4);
            if (it.id == kRecordingId) { // CLOUD's recording: its name, cut to the box (or the TONE)
                const Session::LaneSound& sound = session_.trackSound(project_.track);
                std::string shown = !trk().sample[0] ? std::string("TONE") : sound.missing ? std::string("NOT FOUND") : sampleLabel(trk().sample);
                for (char& c : shown) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
                const Font& valueFont = boxFonts.large;
                while (shown.size() > 1 && valueFont.width(shown.c_str()) > kMacroW - 14.f) shown.pop_back();
                valueBox(r, boxFonts, x, y, kMacroW, rowH, it.name, shown.c_str(), -1.f, i == cursor_, holding_, false, iconFor(it.name));
                continue;
            }
            const int value = valueOf(it.id);
            settingReading(spec_, values(), it.id, text, sizeof text);
            const int range = spec_.settingMax(it.id) - spec_.settingMin(it.id);
            const float part = isChoice(it.id) ? -1.f : range > 0 ? static_cast<float>(value - spec_.settingMin(it.id)) / range : 0.f; // a choice has no bar
            valueBox(r, boxFonts, x, y, kMacroW, rowH, it.name, text, part, i == cursor_, holding_, false, iconFor(it.name));
        }
        scopeStrip(r, fonts, session_.engine);
    } else {
        // An envelope: eight steps, each a speed and a level to reach.
        const int line = (page_ - 4) / 3, env = (page_ - 4) % 3;
        const int end = t.patch.value[envEnd(line, env)], sustain = t.patch.value[envSustain(line, env)];
        const int px = 8, py = 58, pw = 624, ph = 150;
        box(r, px, py, pw, ph, kPanel, kLine);
        // Each step is as wide as it is slow, so the picture is in time.
        float widths[kEnvSteps], total = 0.f;
        for (int s = 0; s < end; ++s) total += widths[s] = 6.f + 0.9f * (99 - t.patch.value[envRate(line, env, s)]);
        const float scale = total > 0.f ? (pw - 24) / total : 0.f;
        float x = px + 12.f;
        int lastX = px + 12, lastY = py + ph - 12;
        for (int s = 0; s < end; ++s) {
            x += widths[s] * scale;
            const int nx = static_cast<int>(x);
            const int ny = py + ph - 12 - (ph - 24) * t.patch.value[envLevel(line, env, s)] / 99;
            const bool here = cursor_ == s || cursor_ == kEnvSteps + s;
            SDL_SetRenderDrawColor(r, here ? cursorColor.r : kText.r, here ? cursorColor.g : kText.g, here ? cursorColor.b : kText.b, 255);
            SDL_RenderDrawLine(r, lastX, lastY, nx, ny);
            SDL_RenderDrawLine(r, lastX, lastY + 1, nx, ny + 1);
            fill(r, nx - 2, ny - 2, 5, 5, here ? cursorColor : kText);
            if (sustain == s + 1) {
                fill(r, nx, py + 6, 1, ph - 12, kBrass); // the note holds here until the key is let go
                fonts.small.draw(r, nx + 4.f, py + 8.f, "HOLD", kBrass);
            }
            std::snprintf(text, sizeof text, "%d", s + 1);
            fonts.small.drawCentered(r, static_cast<float>(nx), py + ph - 10.f, text, kDim);
            lastX = nx;
            lastY = ny;
        }
        // Two rows of eight: how fast each step moves, and where it ends up.
        fonts.small.draw(r, 8.f, 220.f, "SPEED OF EACH STEP", kDim);
        fonts.small.draw(r, 8.f, 300.f, "LEVEL EACH STEP REACHES", kDim);
        for (int i = 0; i < 2 * kEnvSteps; ++i) {
            const Item it = item(i);
            const int step = i % kEnvSteps;
            const int x2 = 8 + step * 78, y2 = i < kEnvSteps ? 236 : 316;
            const bool used = step < end;
            std::snprintf(text, sizeof text, "%d", t.patch.value[it.id]);
            box(r, x2, y2, 74, 54, used ? kPanel : kBg, kLine);
            fonts.small.draw(r, x2 + 6.f, y2 + 6.f, it.name, kDim);
            fonts.large.draw(r, x2 + 6.f, y2 + 22.f, text, i == cursor_ ? cursorColor : (used ? kText : kDim));
            fill(r, x2 + 6, y2 + 46, 62, 3, kLine);
            fill(r, x2 + 6, y2 + 46, 62 * t.patch.value[it.id] / 99, 3, i == cursor_ ? cursorColor : kDim);
            if (i == cursor_) outline(r, x2 - 1, y2 - 1, 76, 56, cursorColor, 2);
        }
        for (int i = 2 * kEnvSteps; i < kEnvSettings; ++i) {
            const Item it = item(i);
            spec_.settingText(it.id, t.patch.value[it.id], text, sizeof text);
            if (i == 2 * kEnvSteps + 1) std::snprintf(text, sizeof text, "STEP %d", t.patch.value[it.id]);
            valueBox(r, fonts, 8 + (i - 2 * kEnvSteps) * 312, 384, 308, 58, it.name, text, -1.f, i == cursor_, holding_);
        }
    }

    if (holding_) {
        hint("STICK UP / DOWN", onRecording() ? "next recording" : "change it");
        hint("D-PAD UP / DOWN", "by one");
        if (onRecording()) hint("B", "as it was");
    } else if (onRecording()) {
        hint("A", "list");
        hint("HOLD A", "next");
        hint("R1", session_.recordingNow() ? "stop" : "record");
        hint("Y", "catch");
        hint("B", "main");
    } else {
        hint("HOLD A", "change");
        if (spec_.recording && session_.recordingNow()) hint("L1", "page"), hint("R1", "stop"); // (R1 stops the take: G34)
        else hint("L1 R1", "page");
        hint("B", "main");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
