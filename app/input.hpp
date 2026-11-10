#pragma once
// The handheld's controls, and nothing else.
//
// The rest of the app only ever sees these buttons and sticks, named the way
// they are printed on the Anbernic RG40XXV. The keyboard is a stand-in for
// testing on a desk: each key presses one handheld button, so the keyboard
// can do nothing the handheld cannot.

#include <SDL.h>

namespace apotheca {

enum class Button {
    Up, Down, Left, Right,   // D-pad
    A, B, X, Y,              // face buttons, by their printed letters
    L1, R1, L2, R2,          // shoulder buttons
    Start, Select, Menu,
    L3, R3,                  // pressing the sticks in
    Count,
    None = Count
};

struct ButtonEvent {
    Button button = Button::None;
    bool down = false;
};

class Input {
public:
    // Feed every SDL event through here. Returns true and fills `out` when
    // the event was a handheld button going down or up.
    bool handle(const SDL_Event& event, ButtonEvent& out);

    bool held(Button button) const { return held_[static_cast<int>(button)]; }

    // Sticks, each axis -1..1 with a dead zone already taken out.
    // Up and left are negative.
    float leftX() const { return shaped(axes_[0]); }
    float leftY() const { return shaped(axes_[1]); }
    float rightX() const { return shaped(axes_[2]); }
    float rightY() const { return shaped(axes_[3]); }

private:
    bool set(Button button, bool down, ButtonEvent& out);
    static float shaped(float v);

    bool held_[static_cast<int>(Button::Count)] = {};
    float axes_[4] = {};
};

// What is printed on the handheld for a button, for hints on screen.
const char* buttonName(Button button);

} // namespace apotheca
