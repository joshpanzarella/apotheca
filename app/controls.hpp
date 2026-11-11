#pragma once
// Learning the handheld's controls.
//
// Firmwares differ in how they present the buttons: as a gamepad SDL knows,
// as a bare joystick with numbered buttons, with the D-pad as a "hat" or as
// two axes, the triggers as buttons or as axes, or the lot as keyboard keys.
// So Apotheca does not guess. The first time it starts on a device with a
// joystick it asks for each button in turn, remembers what the device sent,
// and keeps that in "controls.txt" beside the program. Delete that file to
// be asked again.

#include <SDL.h>

#include <string>

#include "input.hpp"

namespace apotheca {

// One thing a device can send that means "pressed".
struct RawControl {
    enum Kind { None, JoyButton, JoyHat, JoyAxis, Key } kind = None;
    int index = 0; // which button, hat or axis; or the key's code
    int value = 0; // a hat's direction (SDL_HAT_UP...); an axis's way, +1 or -1
    int rest = 0;  // where an axis sits untouched (triggers often rest at one end)
    bool same(const RawControl& o) const { return kind == o.kind && index == o.index && (kind == JoyButton || kind == Key || value == o.value); }
};

struct ControlMap {
    RawControl button[static_cast<int>(Button::Count)];
    RawControl stickRight, stickDown; // the stick's two axes, and which way is right and down

    std::string text() const;            // as kept in controls.txt
    bool parse(const std::string& text); // false if it is not one, or has no way to move or act
};

// A way back into the setup that needs no button to be understood: in the
// first few seconds after starting, hold ANY button down for two seconds.
// It reads what the device sends raw, so it works when the learned controls
// are wrong in every way.
class RelearnHold {
public:
    static constexpr Uint32 kWindowMs = 5000, kHoldMs = 2000;
    void handle(const SDL_Event& event);
    // True once, when a press that began in the window has lasted two seconds.
    bool update(Uint32 nowMs);
    bool offering() const { return began_ == 0 || now_ - began_ < kWindowMs || holding_; } // show the notice
    float part() const { return holding_ ? static_cast<float>(now_ - since_) / kHoldMs : 0.f; }

private:
    Uint32 began_ = 0, now_ = 0, since_ = 0;
    bool holding_ = false, wants_ = false, done_ = false;
    int kind_ = 0, index_ = 0; // what is being held, so only ITS letting go ends the hold
};

// Asks for each control in turn and builds a ControlMap from the answers.
class ControlLearner {
public:
    ControlLearner();
    // Feed every SDL event here. `nowMs` is for the steps that can be skipped by waiting.
    void handle(const SDL_Event& event);
    void update(Uint32 nowMs);
    // Where an axis sits untouched, if the device can say (SDL can for most).
    void setAxisRest(int axis, int value);
    bool done() const { return step_ >= kSteps; }
    const ControlMap& map() const { return map_; }

    // For the screen.
    const char* asking() const;        // "A", "THE STICK, TO THE RIGHT"...
    bool canSkip() const;              // this one may be missing on the device
    float skipIn() const;              // seconds left before it is skipped
    const char* complaint() const { return complaint_; } // why the last press was not taken
    const std::string& saw() const { return saw_; }      // what the device last sent, in its own terms
    int step() const { return step_; }
    static constexpr int kSteps = 17; // fifteen buttons, then the stick twice
    void draw(SDL_Renderer* renderer, const struct Fonts& fonts) const;

private:
    bool pressedNow(const SDL_Event& event, RawControl& out);
    RawControl* target();
    ControlMap map_;
    int step_ = 0;
    bool waitingForRelease_ = false;
    int axisRest_[32];
    bool axisOut_[32] = {};
    bool axisKnown_[32] = {};  // where it rests is known
    bool axisJumped_[32] = {}; // its first word was hard against an end
    int axisJump_[32] = {};
    bool finishedPress_ = false;
    Uint32 now_ = 0, stepBegan_ = 0, began_ = 0;
    const char* complaint_ = "";
    std::string saw_;
};

} // namespace apotheca
