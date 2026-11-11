#include "kit_screen.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace apotheca {

using namespace ui;

namespace {
const int kFits[] = {0, 1, 2, 4, 8, 16, 32, 64};
constexpr int kFitCount = sizeof kFits / sizeof kFits[0];
constexpr Uint32 kRepeatDelayMs = 170, kRepeatEveryMs = 110;
constexpr float kStepsPerSecond = 8.f; // the stick with A held: slow enough to hear each sound
bool isDirection(Button b) { return b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right; }
}

void KitScreen::hear() {
    Command c;
    c.type = Command::Trigger;
    c.track = project_.track;
    c.a = lane_;
    c.value = 0.8f;
    session_.engine.send(c);
}

// The built-in drum is the first choice; the recordings follow by name.
void KitScreen::stepSound(int by) {
    const int count = static_cast<int>(names_.size()) + 1;
    int now = 0;
    for (int i = 0; i < count - 1; ++i)
        if (names_[static_cast<std::size_t>(i)] == trk().laneSample[lane_]) now = i + 1;
    const int to = now + by;
    if (to < 0 || to >= count) return; // the ends stop
    session_.setLaneSample(project_.track, lane_, to == 0 ? "" : names_[static_cast<std::size_t>(to - 1)]);
    if (!project_.playing) hear();
}

void KitScreen::stepFit(int by) {
    int now = 0;
    for (int i = 0; i < kFitCount; ++i)
        if (kFits[i] <= trk().laneFit[lane_]) now = i;
    const int to = now + by;
    if (to < 0 || to >= kFitCount) return;
    session_.setLaneFit(project_.track, lane_, kFits[to]);
}

void KitScreen::openPicker() {
    const int track = project_.track, lane = lane_;
    SamplePicker::Jobs jobs;
    jobs.use = [this, track, lane](const std::string& name) { session_.setLaneSample(track, lane, name); };
    jobs.hear = [this] { hear(); };
    jobs.sound = [this, track, lane] { return &session_.laneSound(track, lane); };
    char title[48];
    std::snprintf(title, sizeof title, "LANE %d: PICK ITS SOUND", lane + 1);
    picker_.open(session_.sampleNames(), trk().laneSample[lane_], title, "THE BUILT-IN DRUM", jobs);
}

void KitScreen::press(Button b) {
    if (holding_) {
        if (b != Button::Up && b != Button::Down) return;
        stepped_ = true;
        if (column_ == kSound) stepSound(b == Button::Up ? 1 : -1);
        else stepFit(b == Button::Up ? 1 : -1);
        return;
    }
    if (b == Button::Up) lane_ = (lane_ + kDrumLanes - 1) % kDrumLanes;
    if (b == Button::Down) lane_ = (lane_ + 1) % kDrumLanes;
    if (b == Button::Left || b == Button::Right) column_ = column_ == kSound ? kFit : kSound;
}

void KitScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (picker_.isOpen()) {
        if (b == Button::Start && event.down && !input.held(Button::Select)) session_.togglePlay(false); // to hear it in the beat
        else picker_.onButton(event, project_.playing);
        return;
    }
    if (b == Button::A) {
        // Held, with up / down: one at a time. A plain tap on SOUND: the list.
        if (!event.down && holding_ && !stepped_ && column_ == kSound) openPicker();
        holding_ = event.down;
        saved_ = 0.f;
        if (event.down) names_ = session_.sampleNames(), stepped_ = false;
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
        else session_.setLaneFit(project_.track, lane_, 0);
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
    } else if (b == Button::X) {
        hear();
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void KitScreen::update(const Input& input, Uint32 nowMs) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
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

void KitScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / KIT", name);
    topBar(r, fonts, View::Device, text, project_);
    const SDL_Color cursorColor = holding_ ? kBrass : kTeal;
    const int rowH = 37, top = 50, soundX = 8, soundW = 250, fitX = 262, fitW = 96, waveX = 366, waveW = 266;

    fonts.small.draw(r, soundX + 2.f, 34.f, "SOUND", kDim);
    fonts.small.draw(r, fitX + 2.f, 34.f, "FIT TO", kDim);
    if (said_.empty()) fonts.small.draw(r, waveX + 2.f, 34.f, "THE RECORDING", kDim);
    else fonts.small.draw(r, waveX + 2.f, 34.f, said_.c_str(), kBrass);
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int y = top + lane * rowH, h = rowH - 4;
        const Session::LaneSound& sound = session_.laneSound(project_.track, lane);
        const char* file = trk().laneSample[lane];
        const bool here = lane == lane_;
        const float textY = y + (h - fonts.medium.capHeight()) / 2.f;

        box(r, soundX, y, soundW, h, file[0] ? kPanel2 : kPanel, kLine);
        std::snprintf(text, sizeof text, "%d", lane + 1);
        fonts.small.draw(r, soundX + 7.f, y + (h - fonts.small.capHeight()) / 2.f, text, kDim);
        if (file[0]) {
            char upper[40];
            std::snprintf(upper, sizeof upper, "%.24s", file);
            for (char* c = upper; *c; ++c) *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
            fonts.medium.draw(r, soundX + 26.f, textY, upper, sound.missing ? kDim : kText);
        } else {
            fonts.medium.draw(r, soundX + 26.f, textY, DrumKit::laneName(lane), kDim);
            fonts.small.drawRight(r, soundX + soundW - 8.f, y + (h - fonts.small.capHeight()) / 2.f, "BUILT IN", kLine);
        }
        if (here && column_ == kSound) outline(r, soundX - 1, y - 1, soundW + 2, h + 2, cursorColor, 2);

        const int fit = trk().laneFit[lane];
        box(r, fitX, y, fitW, h, kPanel, kLine);
        if (!file[0]) std::snprintf(text, sizeof text, "-");
        else if (fit == 0) std::snprintf(text, sizeof text, "OFF");
        else if (fit % 16 == 0) std::snprintf(text, sizeof text, "%d BAR%s", fit / 16, fit == 16 ? "" : "S");
        else std::snprintf(text, sizeof text, "%d STEP%s", fit, fit == 1 ? "" : "S");
        fonts.medium.draw(r, fitX + 9.f, textY, text, fit > 0 && file[0] ? kPurple : kDim);
        if (here && column_ == kFit) outline(r, fitX - 1, y - 1, fitW + 2, h + 2, cursorColor, 2);

        // The recording's outline, and what is being done with it.
        if (sound.missing) {
            fonts.small.draw(r, waveX + 4.f, y + (h - fonts.small.capHeight()) / 2.f, "NOT IN THE SAMPLES FOLDER. THE DRUM PLAYS.", kBrass);
        } else if (!sound.peaks.empty()) {
            const int count = static_cast<int>(sound.peaks.size()), ww = waveW - 70;
            for (int i = 0; i < count; ++i) {
                const int ph = 1 + static_cast<int>(sound.peaks[static_cast<std::size_t>(i)] * (h - 3));
                fill(r, waveX + ww * i / count, y + (h - ph) / 2, 1, ph, here ? kText : kDim);
            }
            if (sound.waiting) std::snprintf(text, sizeof text, "FITTING");
            else std::snprintf(text, sizeof text, "%.1f S", sound.seconds);
            fonts.small.drawRight(r, waveX + waveW - 2.f, y + (h - fonts.small.capHeight()) / 2.f, text, sound.waiting ? kBrass : kDim);
        }
    }
    const int below = top + kDrumLanes * rowH + 4;
    scope(r, session_.engine, 8, below, 624, kScreenH - kHintsH - below - 6);

    Hint list[8];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (holding_) {
        hint("STICK UP / DOWN", column_ == kSound ? "next / previous sound" : "longer / shorter");
    } else {
        hint(column_ == kSound ? "A" : "HOLD A", column_ == kSound ? "pick from the list" : "fit to steps");
        hint("B", column_ == kSound ? "the drum back" : "fit off");
        hint("Y", "catch");
        hint("R1", "record");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count, true);
    picker_.draw(r, fonts); // over everything, when it is open
}

} // namespace apotheca
