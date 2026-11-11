#include "controls.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "ui.hpp"

namespace apotheca {

namespace {

// The order they are asked for. The ones that matter most come first.
const Button kOrder[] = {Button::Up, Button::Down, Button::Left, Button::Right, Button::A, Button::B, Button::X, Button::Y,
                         Button::L1, Button::R1, Button::L2, Button::R2, Button::Start, Button::Select, Button::Menu};
constexpr int kButtonSteps = sizeof kOrder / sizeof kOrder[0];
constexpr Uint32 kSkipAfterMs = 8000;
constexpr Uint32 kSettleMs = 600; // at the very start, what the axes say is where they rest
constexpr int kAxisPress = 16000, kAxisRelease = 9000; // of 32767, measured from where the axis rests

const char* kindWord(RawControl::Kind kind) {
    return kind == RawControl::JoyButton ? "button" : kind == RawControl::JoyHat ? "hat" : kind == RawControl::JoyAxis ? "axis" : kind == RawControl::Key ? "key" : "none";
}

void put(std::ostringstream& out, const char* name, const RawControl& c) {
    out << name << ' ' << kindWord(c.kind) << ' ' << c.index << ' ' << c.value << ' ' << c.rest << '\n';
}

} // namespace

std::string ControlMap::text() const {
    std::ostringstream out;
    out << "apotheca-controls 1\n";
    for (int i = 0; i < static_cast<int>(Button::Count); ++i) put(out, buttonName(static_cast<Button>(i)), button[i]);
    put(out, "STICK-RIGHT", stickRight);
    put(out, "STICK-DOWN", stickDown);
    return out.str();
}

bool ControlMap::parse(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 17, "apotheca-controls") != 0) return false;
    ControlMap made;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        std::string name, kind;
        RawControl c;
        if (!(in >> name >> kind >> c.index >> c.value >> c.rest)) continue;
        c.kind = kind == "button" ? RawControl::JoyButton : kind == "hat" ? RawControl::JoyHat : kind == "axis" ? RawControl::JoyAxis : kind == "key" ? RawControl::Key : RawControl::None;
        if (c.kind == RawControl::JoyAxis && c.value != 1 && c.value != -1) continue;
        if ((c.kind == RawControl::JoyAxis || c.kind == RawControl::JoyHat || c.kind == RawControl::JoyButton) && (c.index < 0 || c.index > 255)) continue;
        if (name == "STICK-RIGHT") made.stickRight = c;
        else if (name == "STICK-DOWN") made.stickDown = c;
        else
            for (int i = 0; i < static_cast<int>(Button::Count); ++i)
                if (name == buttonName(static_cast<Button>(i))) made.button[i] = c;
    }
    // No use without a way to move and the two buttons that act.
    for (Button needed : {Button::Up, Button::Down, Button::Left, Button::Right, Button::A, Button::B})
        if (made.button[static_cast<int>(needed)].kind == RawControl::None) return false;
    *this = made;
    return true;
}

// ---- the hold at the start ----------------------------------------------------------------

void RelearnHold::handle(const SDL_Event& e) {
    if (done_) return;
    int kind = 0, index = 0;
    bool down = false;
    if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) kind = 1, index = e.jbutton.button, down = e.type == SDL_JOYBUTTONDOWN;
    else if (e.type == SDL_JOYHATMOTION) kind = 2, index = e.jhat.hat, down = e.jhat.value != SDL_HAT_CENTERED;
    else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        if (e.type == SDL_KEYDOWN && e.key.repeat) return;
        kind = 3, index = e.key.keysym.sym, down = e.type == SDL_KEYDOWN;
    } else return;
    if (down && !holding_) {
        // Only a press that begins in the first seconds counts.
        if (began_ != 0 && now_ - began_ >= kWindowMs) return;
        wants_ = true;
        kind_ = kind;
        index_ = index;
    } else if (!down && kind == kind_ && index == index_) {
        holding_ = wants_ = false;
    }
}

bool RelearnHold::update(Uint32 nowMs) {
    if (began_ == 0) began_ = nowMs;
    now_ = nowMs;
    if (done_) return false;
    if (wants_ && !holding_) holding_ = true, since_ = nowMs; // the hold's clock starts here
    if (holding_ && nowMs - since_ >= kHoldMs) {
        done_ = true;
        holding_ = wants_ = false;
        return true;
    }
    return false;
}

// ---- learning ---------------------------------------------------------------------------

ControlLearner::ControlLearner() {
    for (int& rest : axisRest_) rest = 0;
}

RawControl* ControlLearner::target() {
    if (step_ < kButtonSteps) return &map_.button[static_cast<int>(kOrder[step_])];
    return step_ == kButtonSteps ? &map_.stickRight : &map_.stickDown;
}

const char* ControlLearner::asking() const {
    if (step_ < kButtonSteps) return buttonName(kOrder[step_]);
    return step_ == kButtonSteps ? "THE STICK, TO THE RIGHT" : step_ == kButtonSteps + 1 ? "THE STICK, DOWN" : "";
}

// MENU is often kept by the firmware for itself, and a device may have no stick.
bool ControlLearner::canSkip() const { return step_ >= kButtonSteps - 1 && step_ < kSteps; }

float ControlLearner::skipIn() const {
    const Uint32 gone = now_ - stepBegan_;
    return gone >= kSkipAfterMs ? 0.f : static_cast<float>(kSkipAfterMs - gone) / 1000.f;
}

// Was this event something being pressed (as against let go, or an axis at rest)?
bool ControlLearner::pressedNow(const SDL_Event& e, RawControl& out) {
    char text[64] = "";
    bool pressed = false;
    if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) {
        out.kind = RawControl::JoyButton;
        out.index = e.jbutton.button;
        pressed = e.type == SDL_JOYBUTTONDOWN;
        std::snprintf(text, sizeof text, "button %d %s", out.index, pressed ? "down" : "up");
    } else if (e.type == SDL_JOYHATMOTION) {
        out.kind = RawControl::JoyHat;
        out.index = e.jhat.hat;
        out.value = e.jhat.value;
        // One direction at a time: a diagonal is not an answer.
        pressed = out.value == SDL_HAT_UP || out.value == SDL_HAT_DOWN || out.value == SDL_HAT_LEFT || out.value == SDL_HAT_RIGHT;
        std::snprintf(text, sizeof text, "hat %d = %d", out.index, out.value);
    } else if (e.type == SDL_JOYAXISMOTION) {
        const int axis = e.jaxis.axis;
        if (axis < 0 || axis >= 32) return false;
        const int v = e.jaxis.value;
        const auto snap = [](int value) { return value < -24000 ? -32768 : (value > 24000 ? 32767 : 0); };
        // Where does this axis rest? A stick rests in the middle, but a
        // trigger rests at one end of its travel, and getting that wrong
        // makes the button read as held for ever. Three ways to know:
        //   - it was told (setAxisRest), or the axis spoke in the first
        //     moments, before anything was asked;
        //   - its first word is part-way along: it is moving away from rest,
        //     so rest is the nearest of the three places it could be;
        //   - its first word is hard against an end: it jumped there (a
        //     digital trigger, or a D-pad reporting as an axis). That end is
        //     "pressed"; rest is wherever it goes next, when it is let go.
        if (now_ - began_ < kSettleMs) {
            axisRest_[axis] = snap(v);
            axisKnown_[axis] = true;
            return false;
        }
        if (!axisKnown_[axis] && step_ >= kButtonSteps) {
            // The stick is being asked for, and a stick rests in the middle.
            axisRest_[axis] = 0;
            axisKnown_[axis] = true;
        }
        if (!axisKnown_[axis]) {
            if (!axisJumped_[axis] && (v <= -32767 || v >= 32767)) {
                axisJumped_[axis] = true;
                axisJump_[axis] = v;
                std::snprintf(text, sizeof text, "axis %d jumped to an end", axis);
                saw_ = text;
                return false; // not an answer yet: wait to see where it rests
            }
            if (axisJumped_[axis]) {
                if (std::abs(v - axisJump_[axis]) < kAxisPress) return false; // still at that end
                // Let go: now it is known. The press it has just finished is the answer.
                axisRest_[axis] = snap(v);
                axisKnown_[axis] = true;
                out.kind = RawControl::JoyAxis;
                out.index = axis;
                out.value = axisJump_[axis] > axisRest_[axis] ? 1 : -1;
                out.rest = axisRest_[axis];
                std::snprintf(text, sizeof text, "axis %d %s (rests at %d)", axis, out.value > 0 ? "+" : "-", out.rest);
                saw_ = text;
                finishedPress_ = true; // already let go: nothing to wait for
                return true;
            }
            axisRest_[axis] = snap(v);
            axisKnown_[axis] = true;
        }
        const int moved = v - axisRest_[axis];
        const bool out_ = std::abs(moved) > kAxisPress;
        if (std::abs(moved) < kAxisRelease) axisOut_[axis] = false;
        pressed = out_ && !axisOut_[axis];
        if (out_) axisOut_[axis] = true;
        out.kind = RawControl::JoyAxis;
        out.index = axis;
        out.value = moved > 0 ? 1 : -1;
        out.rest = axisRest_[axis];
        if (pressed) std::snprintf(text, sizeof text, "axis %d %s (rests at %d)", axis, moved > 0 ? "+" : "-", out.rest);
    } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        if (e.key.repeat) return false;
        out.kind = RawControl::Key;
        out.index = static_cast<int>(e.key.keysym.sym);
        pressed = e.type == SDL_KEYDOWN;
        std::snprintf(text, sizeof text, "key %d %s", out.index, pressed ? "down" : "up");
    } else {
        return false;
    }
    if (text[0]) saw_ = text;
    return pressed;
}

void ControlLearner::handle(const SDL_Event& event) {
    if (done()) return;
    if (began_ != 0 && now_ - began_ < kSettleMs && event.type != SDL_JOYAXISMOTION) return; // not listening for answers yet
    RawControl got;
    finishedPress_ = false;
    if (!pressedNow(event, got)) {
        // Something was let go: the next press is a new answer.
        if (event.type == SDL_JOYBUTTONUP || event.type == SDL_KEYUP || (event.type == SDL_JOYHATMOTION && event.jhat.value == SDL_HAT_CENTERED) ||
            event.type == SDL_JOYAXISMOTION)
            waitingForRelease_ = false;
        return;
    }
    if (waitingForRelease_) return; // still the press that answered the step before
    const bool stickStep = step_ >= kButtonSteps;
    if (stickStep && got.kind != RawControl::JoyAxis) {
        complaint_ = "THAT IS A BUTTON. PUSH THE STICK.";
        return;
    }
    // Each control can mean one thing only.
    for (int i = 0; i < step_ && i < kButtonSteps; ++i) {
        if (map_.button[static_cast<int>(kOrder[i])].same(got)) {
            complaint_ = "THAT ONE IS ALREADY TAKEN. PRESS A DIFFERENT ONE.";
            return;
        }
    }
    if (step_ == kButtonSteps + 1 && map_.stickRight.kind == RawControl::JoyAxis && map_.stickRight.index == got.index) {
        complaint_ = "THAT IS THE SAME WAY AS BEFORE. PUSH IT DOWN.";
        return;
    }
    *target() = got;
    complaint_ = "";
    ++step_;
    stepBegan_ = now_;
    waitingForRelease_ = !finishedPress_;
    finishedPress_ = false;
}

void ControlLearner::setAxisRest(int axis, int value) {
    if (axis < 0 || axis >= 32) return;
    axisRest_[axis] = value < -24000 ? -32768 : (value > 24000 ? 32767 : 0);
    axisKnown_[axis] = true;
}

void ControlLearner::update(Uint32 nowMs) {
    if (began_ == 0) began_ = now_ = nowMs;
    if (stepBegan_ == 0) stepBegan_ = nowMs;
    now_ = nowMs;
    if (!done() && canSkip() && nowMs - stepBegan_ >= kSkipAfterMs) {
        // Left unset: the device does not have it, or keeps it for itself.
        // (Skipping "stick right" skips "stick down" too: there is no stick.)
        step_ = step_ == kButtonSteps ? kSteps : step_ + 1;
        stepBegan_ = nowMs;
        complaint_ = "";
    }
}

void ControlLearner::draw(SDL_Renderer* r, const Fonts& fonts) const {
    using namespace ui;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, 0, 0, kScreenW, kScreenH, kBg);
    fonts.medium.draw(r, 40.f, 40.f, "SETTING UP THE CONTROLS", kDim);
    fonts.small.draw(r, 40.f, 64.f, "Once only. Apotheca learns what each of this handheld's buttons sends.", kDim);
    char text[96];
    std::snprintf(text, sizeof text, "%d OF %d", step_ + 1 > kSteps ? kSteps : step_ + 1, kSteps);
    fonts.small.drawRight(r, kScreenW - 40.f, 40.f, text, kDim);

    const bool stick = step_ >= kButtonSteps;
    fonts.large.draw(r, 40.f, 150.f, stick ? "PUSH" : "PRESS", kText);
    box(r, 40, 180, kScreenW - 80, 70, kPanel2, kTeal);
    outline(r, 40, 180, kScreenW - 80, 70, kTeal, 2);
    fonts.large.drawCentered(r, kScreenW / 2.f, 180.f + (70.f - fonts.large.capHeight()) / 2.f, asking(), kTeal);
    if (complaint_[0]) fonts.medium.draw(r, 40.f, 270.f, complaint_, kBrass);
    if (canSkip()) {
        std::snprintf(text, sizeof text, "If this handheld has none, or nothing happens: wait %d seconds and it is left out.", static_cast<int>(skipIn()) + 1);
        fonts.small.draw(r, 40.f, 300.f, text, kDim);
    }
    // What the device is sending, in its own terms: proof that it is being heard.
    fonts.small.draw(r, 40.f, kScreenH - 60.f, "THE HANDHELD LAST SENT:", kDim);
    fonts.small.draw(r, 220.f, kScreenH - 60.f, saw_.empty() ? "nothing yet" : saw_.c_str(), saw_.empty() ? kBrass : kText);
    fonts.small.draw(r, 40.f, kScreenH - 40.f, "To do this again later: the SETUP screen, or hold any button for two seconds just after Apotheca starts.", kDim);
}

} // namespace apotheca
