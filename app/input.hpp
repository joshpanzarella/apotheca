#pragma once
// The handheld's controls, and nothing else.
//
// The rest of the app only ever sees these buttons and the one stick, named the way
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
    L3, R3,                  // pressing the stick in (either SDL name)
    Count,
    None = Count
};

// One of the four on the D-pad. (The stick is turned into these too.)
inline bool isDirection(Button b) { return b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right; }

struct ButtonEvent {
    Button button = Button::None;
    bool down = false;
};

class Input {
public:
    // Feed every SDL event through here. Returns true and fills `out` when
    // the event was a handheld button going down or up.
    bool handle(const SDL_Event& event, ButtonEvent& out);

    // Use controls learned from this device (controls.hpp) in place of
    // SDL's own idea of a gamepad. The keyboard stand-ins still work.
    void useMap(const struct ControlMap* map) { map_ = map; }

    bool held(Button button) const { return held_[static_cast<int>(button)]; }
    // Does this device have the button at all? (MENU is kept by some firmwares, and then it was left out of the setup.)
    bool has(Button button) const;

    // THE stick. The RG40XXV has one. Each axis is -1..1 with a dead zone
    // already taken out; up and left are negative. Firmwares differ on
    // whether they report it as SDL's left or right stick, so both are read
    // and whichever is pushed further counts.
    float stickX() const { return shaped(further(axes_[0], axes_[2])); }
    float stickY() const { return shaped(further(axes_[1], axes_[3])); }

private:
    bool set(Button button, bool down, ButtonEvent& out);
    static float shaped(float v);
    static float further(float a, float b) { return (a < 0 ? -a : a) >= (b < 0 ? -b : b) ? a : b; }

    bool handleMapped(const SDL_Event& event, ButtonEvent& out);
    const struct ControlMap* map_ = nullptr;
    int hat_[8] = {};
    bool held_[static_cast<int>(Button::Count)] = {};
    float axes_[4] = {};
};

// What is printed on the handheld for a button, for hints on screen.
const char* buttonName(Button button);

} // namespace apotheca
