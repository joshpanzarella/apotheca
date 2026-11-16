#include "sampler_screen.hpp"
#include "samples.hpp"

#include "apotheca/envelope.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kRepeatDelayMs = ui::kCursorDelayMs, kRepeatEveryMs = ui::kCursorEveryMs;
constexpr float kMovesPerSecond = 14.f, kSoundsPerSecond = 8.f;
const char* const kNames[sampler::kSettings] = {"PITCH", "TUNE", "START", "ATTACK", "RELEASE", "LOOP", "LEVEL", "END"};
// The order the page shows them in: END beside START (it is the last id: ids are appended).
const int kShown[sampler::kSettings] = {sampler::Root, sampler::Tune, sampler::Start, sampler::End, sampler::Attack, sampler::Release, sampler::Loop, sampler::Level};
}

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
    const float speed = std::pow(2.f, static_cast<float>(kHearKey - t.sampler[sampler::Root]) / 12.f + static_cast<float>(t.sampler[sampler::Tune]) / 1200.f);
    release_ = 0.002f * std::pow(10000.f, static_cast<float>(t.sampler[sampler::Release]) / 100.f);
    headHeld_ = held || (headHeld_ && session_.hearingHeld());
    head_.start(lastMs_, session_.trackSound(project_.track).seconds, static_cast<float>(t.sampler[sampler::Start]) / 100.f, speed,
                headHeld_ ? 0.f : static_cast<float>(Session::kAuditionMs) / 1000.f + release_); // held: until it is let go (update)
    head_.to = endPart(t);
    if (held) session_.auditionHeld(project_.track, kHearKey, kPlacedLevel, this);
    else session_.audition(project_.track, kHearKey, kPlacedLevel);
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
    if (id == sampler::Start && value > trk().sampler[sampler::End] - sampler::kLeastSpan) value = trk().sampler[sampler::End] - sampler::kLeastSpan;
    if (id == sampler::End && value < trk().sampler[sampler::Start] + sampler::kLeastSpan) value = trk().sampler[sampler::Start] + sampler::kLeastSpan;
    session_.setSamplerSetting(project_.track, id, value);
    if (trk().sampler[id] == before || project_.playing || lastMs_ - heardAt_ < kHearEveryMs) return;
    heardAt_ = lastMs_;
    hear();
}

void SamplerScreen::stepSound(int by) {
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
        // SOUND on top; the settings in two rows of four under it.
        if (b == Button::Left) goTo(stop_ - 1);
        if (b == Button::Right) goTo(stop_ + 1);
        if (b == Button::Up) goTo(stop_ == 0 ? 0 : (stop_ <= 4 ? 0 : stop_ - 4));
        if (b == Button::Down) goTo(stop_ == 0 ? 1 : (stop_ > 4 ? stop_ : (stop_ + 4 > sampler::kSettings ? sampler::kSettings : stop_ + 4)));
        return;
    }
    if (b != Button::Up && b != Button::Down) return;
    stepped_ = true;
    const int by = b == Button::Up ? 1 : -1;
    const int id = settingAt(stop_ - 1);
    if (stop_ == 0) stepSound(by);
    else setSetting(id, trk().sampler[id] + (id == sampler::Root ? -by : by)); // PITCH up is the root key down
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
    jobs.followFailures = [this] { return session_.followFailures(); };
    jobs.used = [this, track](const std::string& name) { return session_.sampleUses(name) - (name == project_.tracks[track].sample ? 1 : 0); };
    picker_.open(session_.sampleNames(), session_.sampleFolders(), trk().sample, "THE SAMPLER: PICK ITS RECORDING", "NO RECORDING", jobs);
}

void SamplerScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (picker_.isOpen()) {
        if (b == Button::Start && event.down && !input.held(Button::Select) && !picker_.typing()) session_.togglePlay(false);
        else picker_.onButton(event, project_.playing);
        return;
    }
    if (b == Button::A) {
        if (!event.down && holding_ && !stepped_ && stop_ == 0) openPicker(); // a plain tap on SOUND: the list
        if (event.down) stepped_ = false;
        holding_ = event.down;
        saved_ = turned_ = 0.f;
        stickWas_ = false;
        if (event.down) names_ = session_.sampleNames();
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
        if (stop_ == 0) session_.setTrackSample(project_.track, "");
        else setSetting(settingAt(stop_ - 1), sampler::settingDefault(settingAt(stop_ - 1)));
    } else if (b == Button::Y && !holding_) {
        const std::string name = session_.catchLastBar(project_.track, 0);
        said_ = name.empty() ? "NOTHING TO CATCH: PLAY SOMETHING FIRST" : "CAUGHT THE LAST BAR AS \"" + name + "\"";
        for (char& c : said_) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        saidUntil_ = lastMs_ + 2500;
    } else if (b == Button::R1 && !holding_) {
        // Record from the input: once to start (it waits for sound), again to stop.
        const Session::Rec now = session_.toggleRecording(project_.track, 0);
        if (now == Session::Rec::NoInput) said_ = "NO INPUT FOUND: PLUG IN A USB MICROPHONE OR INTERFACE";
        else if (now == Session::Rec::Idle && session_.lastRecorded().empty()) said_ = "NOTHING RECORDED: NO SOUND CAME IN";
        saidUntil_ = lastMs_ + 3000;
    } else if (b == Button::X) {
        hear(true);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void SamplerScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) holding_ = false;

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
    const float x = input.stickX(), y = input.stickY();

    if (holding_ && stop_ > 0) {
        // A setting: the stick turns it through its whole range in about a second.
        const int id = settingAt(stop_ - 1);
        const float range = static_cast<float>(sampler::settingMax(id) - sampler::settingMin(id));
        turned_ += -y * std::fabs(y) * range * dt;
        const int whole = static_cast<int>(turned_);
        if (whole != 0) {
            turned_ -= static_cast<float>(whole);
            setSetting(id, trk().sampler[id] + (id == sampler::Root ? -whole : whole));
        }
        if (y == 0.f) turned_ = 0.f;
    } else {
        const bool sideways = !holding_ && std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * (holding_ ? kSoundsPerSecond : kMovesPerSecond) * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f)
                press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
        }
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + (holding_ && stop_ == 0 ? ui::kListEveryMs : kRepeatEveryMs);
        press(repeating_);
    }
}

void SamplerScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / SOUND", name);
    topBar(r, fonts, View::Device, text, project_);
    const SDL_Color accent = holding_ ? kBrass : kTeal;
    const TrackData& t = trk();
    const Session::LaneSound& sound = session_.trackSound(project_.track);

    // ---- the recording ------------------------------------------------------------
    const int sx = 8, sy = kSoundY, sw = 624, sh = kSoundH;
    box(r, sx, sy, sw, sh, kPanel, kLine);
    fonts.small.draw(r, sx + 7.f, sy + 7.f, said_.empty() ? "SOUND" : said_.c_str(), said_.empty() ? kDim : kBrass);
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
        const int count = static_cast<int>(sound.peaks.size());
        const float start = static_cast<float>(t.sampler[sampler::Start]) / 100.f, end = endPart(t);
        for (int i = 0; i < count; ++i) {
            const int ph = 1 + static_cast<int>(sound.peaks[static_cast<std::size_t>(i)] * (wh - 1));
            const float at = static_cast<float>(i) / count;
            fill(r, wx + ww * i / count, wy + (wh - ph) / 2, ww / count, ph, at >= start && at < end ? kText : kLine); // what a note plays, lit
        }
        fill(r, wx + static_cast<int>(start * ww), wy - 2, 2, wh + 4, kPurple);   // where a note begins
        fill(r, wx + endMarkX(end, ww), wy - 2, 2, wh + 4, kPurple);              // and where it ends
        drawPlayhead(r, head_, lastMs_, wx, wy - 2, ww, wh + 4);
        std::snprintf(text, sizeof text, "%.1f S", sound.seconds);
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
    scopeStrip(r, fonts, session_.engine);

    Hint list[8];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (holding_) {
        hint("UP / DOWN", stop_ == 0 ? "next / previous recording" : "turn it");
    } else {
        hint("HOLD A", stop_ == 0 ? "pick the recording" : "turn");
        hint("B", stop_ == 0 ? "none" : "default value");
        hint("Y", "catch");
        hint("R1", "record");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
    picker_.draw(r, fonts);
}

} // namespace apotheca
