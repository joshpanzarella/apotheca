// What every screen is: something that takes button presses, moves on once a
// frame, and draws itself. The app picks one screen for the tab and the
// track's instrument, and talks to it through this.
//
// A new screen derives from Screen and is added to App::screenNow().
//
// A rule every screen keeps: update() reads which buttons are held from
// `input` EVERY frame and drops its own "held" flags when they are not. A
// screen that only trusted the release event got stuck with A "held" when
// the release went to another screen.
#pragma once

#include <SDL.h>

#include "input.hpp"

namespace apotheca {

struct Fonts;

class Screen {
public:
    virtual ~Screen() = default;
    virtual void onButton(const ButtonEvent& event, const Input& input) = 0;
    virtual void update(const Input& input, Uint32 nowMs) = 0; // once per frame
    virtual void draw(SDL_Renderer* renderer, const Fonts& fonts) const = 0;
};

} // namespace apotheca
