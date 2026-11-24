#pragma once
// The joysticks the program reads, kept open as they come and go (review 2026-11-23: they were opened
// once at the start, so a gamepad that dropped out and came back, a sleep and a wake, a driver's
// hiccup, was never read again: the song played on and no button answered).
//
// Raw (controls learned, or the setup asking for them): every joystick, opened as it arrives.
// Otherwise SDL's own reading of the first gamepad it knows. What arrives is opened once only: SDL
// says ADDED for every device already there when it starts, and those were opened then.

#include <SDL.h>

#include <algorithm>
#include <vector>

namespace apotheca {

struct Joysticks {
    std::vector<SDL_Joystick*> opened; // raw
    SDL_GameController* pad = nullptr; // SDL's own reading
    bool raw = true;

    enum class Change { None, Added, Removed };

    // A device arrived (`which`: its place in SDL's list) or left (`which`: its instance). Anything else: None.
    Change handle(const SDL_Event& e) {
        if (e.type == SDL_JOYDEVICEADDED) {
            const int index = e.jdevice.which;
            const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
            if (raw) {
                for (SDL_Joystick* stick : opened)
                    if (SDL_JoystickInstanceID(stick) == id) return Change::None; // (already open: one there at the start)
                SDL_Joystick* stick = SDL_JoystickOpen(index);
                if (!stick) return Change::None;
                opened.push_back(stick);
                return Change::Added;
            }
            if (pad || !SDL_IsGameController(index)) return Change::None;
            pad = SDL_GameControllerOpen(index);
            return pad ? Change::Added : Change::None;
        }
        if (e.type == SDL_JOYDEVICEREMOVED) {
            const SDL_JoystickID id = e.jdevice.which;
            const auto gone = std::find_if(opened.begin(), opened.end(), [&](SDL_Joystick* s) { return SDL_JoystickInstanceID(s) == id; });
            if (gone != opened.end()) {
                SDL_JoystickClose(*gone);
                opened.erase(gone);
                return Change::Removed;
            }
            if (pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == id) {
                SDL_GameControllerClose(pad);
                pad = nullptr;
                for (int i = 0; i < SDL_NumJoysticks() && !pad; ++i) // another there: read that one
                    if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
                return Change::Removed;
            }
        }
        return Change::None;
    }

    void closeAll() {
        for (SDL_Joystick* stick : opened) SDL_JoystickClose(stick);
        opened.clear();
        if (pad) SDL_GameControllerClose(pad);
        pad = nullptr;
    }
};

} // namespace apotheca
