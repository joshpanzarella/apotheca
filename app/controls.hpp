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
    // controls.txt's first line says which setup made it. 2: the setup that asks for L3 and R3 (the
    // stick pressed in). A map from before was never asked them: with no L3 it is asked for L3 alone,
    // once (neverAskedL3), and written again as a 2, whatever the answer.
    static constexpr int kVersion = 2;
    int version = kVersion;
    bool neverAskedL3() const { return version < 2 && button[static_cast<int>(Button::L3)].kind == RawControl::None; }

    std::string text() const;            // as kept in controls.txt
    bool parse(const std::string& text); // false if it is not one, or has no way to move or act
};

// Asks for each control in turn and builds a ControlMap from the answers.
class ControlLearner {
public:
    ControlLearner();
    // Ask for this one button alone, onto a map already learned (an old map with no L3: main.cpp).
    // The axes' rests already told are kept.
    void askOnlyFor(const ControlMap& kept, Button button);
    // Feed every SDL event here. `nowMs` is for the steps that can be skipped by waiting.
    void handle(const SDL_Event& event);
    void update(Uint32 nowMs);
    // Where an axis sits untouched, if the device can say (SDL can for most).
    void setAxisRest(int axis, int value);
    bool done() const { return step_ >= count_; }
    const ControlMap& map() const { return map_; }

    // For the screen.
    const char* asking() const;        // "A", "THE STICK, TO THE RIGHT"...
    bool canSkip() const;              // this one may be missing on the device (MENU, the stick, L3, R3)
    float skipIn() const;              // seconds left before it is skipped
    const char* complaint() const { return complaint_; } // why the last press was not taken
    const std::string& saw() const { return saw_; }      // what the device last sent, in its own terms
    int step() const { return step_; }
    int steps() const { return count_; } // kSteps, or 1 (askOnlyFor)
    // Fifteen buttons, the stick twice, then the stick pressed in (L3, and R3 for a second stick).
    static constexpr int kSteps = 19;
    void draw(SDL_Renderer* renderer, const struct Fonts& fonts) const;

private:
    // What a step asks for: a Button, or the stick's two ways.
    enum { kAskStickRight = -1, kAskStickDown = -2 };
    int ask() const { return step_ < count_ ? asks_[step_] : 0; }
    bool pressedNow(const SDL_Event& event, RawControl& out);
    RawControl* target();
    ControlMap map_;
    int asks_[kSteps];
    int count_ = kSteps;
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
