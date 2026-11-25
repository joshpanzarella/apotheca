#include "controls.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "ui.hpp"

namespace apotheca {

namespace {

// The order they are asked for. The ones that matter most come first; the stick's two ways come
// after MENU, then the stick pressed in (L3, and R3 where there is a second stick).
const Button kOrder[] = {Button::Up, Button::Down, Button::Left, Button::Right, Button::A, Button::B, Button::X, Button::Y,
                         Button::L1, Button::R1, Button::L2, Button::R2, Button::Start, Button::Select, Button::Menu};
constexpr int kButtonSteps = sizeof kOrder / sizeof kOrder[0];
static_assert(kButtonSteps + 4 == ControlLearner::kSteps, "the buttons, the stick's two ways, L3 and R3");
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
    out << "apotheca-controls " << version << '\n';
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
    made.version = std::atoi(line.c_str() + 17) >= 2 ? 2 : 1; // (no number, or 1: from before L3 was asked for)
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

// ---- learning ---------------------------------------------------------------------------

ControlLearner::ControlLearner() {
    for (int& rest : axisRest_) rest = 0;
    for (int i = 0; i < kButtonSteps; ++i) asks_[i] = static_cast<int>(kOrder[i]);
    asks_[kButtonSteps] = kAskStickRight;
    asks_[kButtonSteps + 1] = kAskStickDown;
    asks_[kButtonSteps + 2] = static_cast<int>(Button::L3);
    asks_[kButtonSteps + 3] = static_cast<int>(Button::R3);
}

void ControlLearner::askOnlyFor(const ControlMap& kept, Button button) {
    map_ = kept;
    map_.version = ControlMap::kVersion; // asked now: never again, whatever the answer
    asks_[0] = static_cast<int>(button);
    count_ = 1;
    step_ = 0;
    stepBegan_ = now_; // (its wait starts now; 0 before the first update: from that)
    complaint_ = "";
}

RawControl* ControlLearner::target() {
    const int a = ask();
    if (a >= 0) return &map_.button[a];
    return a == kAskStickRight ? &map_.stickRight : &map_.stickDown;
}

const char* ControlLearner::asking() const {
    if (done()) return "";
    const int a = ask();
    if (a == static_cast<int>(Button::L3)) return "L3: THE STICK, PRESSED IN";
    if (a == static_cast<int>(Button::R3)) return "R3: A SECOND STICK, PRESSED IN";
    if (a >= 0) return buttonLabel(static_cast<Button>(a));
    return a == kAskStickRight ? "THE STICK, TO THE RIGHT" : "THE STICK, DOWN";
}

// MENU is often kept by the firmware for itself, a device may have no stick, and whether a firmware
// passes the stick pressed in is not known (the stock one's: nobody knows yet).
bool ControlLearner::canSkip() const {
    if (done()) return false;
    const int a = ask();
    return a < 0 || a == static_cast<int>(Button::Menu) || a == static_cast<int>(Button::L3) || a == static_cast<int>(Button::R3);
}

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
        if (!axisKnown_[axis] && ask() < 0) {
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
    const int a = ask();
    if (a < 0 && got.kind != RawControl::JoyAxis) {
        complaint_ = "THAT IS A BUTTON. PUSH THE STICK.";
        return;
    }
    // The stick pressed in is a button: a way the stick moves is not it (that axis would then press
    // L3 whenever the stick was pushed).
    if ((a == static_cast<int>(Button::L3) || a == static_cast<int>(Button::R3)) && got.kind == RawControl::JoyAxis) {
        complaint_ = "THAT IS THE STICK MOVING. PRESS IT IN.";
        return;
    }
    // Each control can mean one thing only (the buttons asked for already, or kept from before: askOnlyFor).
    for (int i = 0; i < static_cast<int>(Button::Count); ++i) {
        if (i != a && map_.button[i].kind != RawControl::None && map_.button[i].same(got)) {
            complaint_ = "THAT ONE IS ALREADY TAKEN. PRESS A DIFFERENT ONE.";
            return;
        }
    }
    if (a == kAskStickDown && map_.stickRight.kind == RawControl::JoyAxis && map_.stickRight.index == got.index) {
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
        // (Skipping "stick right" skips the rest too: with no stick, no stick to press in.)
        step_ = ask() == kAskStickRight ? count_ : step_ + 1;
        stepBegan_ = nowMs;
        complaint_ = "";
    }
}

void ControlLearner::draw(SDL_Renderer* r, const Fonts& chosen) const {
    const Fonts& fonts = chosen.fixed(); // its lines run the screen's width: always at LARGE, whatever TEXT SIZE is
    using namespace ui;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, 0, 0, kScreenW, kScreenH, kBg);
    fonts.medium.draw(r, 40.f, 40.f, "SETTING UP THE CONTROLS", kDim);
    fonts.small.draw(r, 40.f, 64.f, count_ == 1 ? "One button is new since this handheld was set up. Once only."
                                                : "Once only. Apotheca learns what each of this handheld's buttons sends.", kDim);
    char text[96];
    std::snprintf(text, sizeof text, "%d OF %d", step_ + 1 > count_ ? count_ : step_ + 1, count_);
    fonts.small.drawRight(r, kScreenW - 40.f, 40.f, text, kDim);

    const bool stick = !done() && ask() < 0;
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
    fonts.small.draw(r, 40.f, kScreenH - 40.f, "To do this again later: the SET tab, CONTROLS row, hold A for a second.", kDim);
}

} // namespace apotheca
