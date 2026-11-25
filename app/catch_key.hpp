#pragma once
// CATCH's Y (batch S3, 2026-11-25; sampling-gaps.md 4), the same on the kit's lanes, the sampler's SOUND and
// CLOUD's first page:
//   a tap of Y            CATCH, on its LET GO (it was on the press): the last bars played, as chosen below
//   hold Y + LEFT / RIGHT  how many bars: 1, 2 or 4 (the ends stop)
//   hold Y + UP / DOWN     of what: THE MIX (everything, as it was heard), or a track alone, 1 to the last
//                          (the ends stop). The engine remembers that track from when it is chosen.
// While Y is held the screen's line says what a let go would catch ("CATCH THE LAST 2 BARS OF THE MIX"); a
// let go after a choice, or after any other button, catches nothing (as L1's own tap on the grids). The
// stick, with Y held, makes one choice a push. The choice is the session's (Session::catchBars, catchFrom).

#include <cmath>
#include <string>

#include "input.hpp"
#include "session.hpp"

namespace apotheca {

class CatchKey {
public:
    void down() { held_ = true, chose_ = false, stickOut_ = false; }
    // Y let go: true if it was a tap (nothing chosen and nothing else pressed while it was held).
    bool up() {
        const bool tap = held_ && !chose_;
        held_ = false;
        return tap;
    }
    bool held() const { return held_; }
    void spoil() { chose_ = true; } // another button went down while Y was held: its let go catches nothing
    // A direction while Y is held: the choice. True if it was taken (Y held).
    bool direction(Button b, Session& session) {
        if (!held_) return false;
        chose_ = true;
        if (b == Button::Left || b == Button::Right) session.stepCatchBars(b == Button::Right ? 1 : -1);
        else if (b == Button::Up || b == Button::Down) session.stepCatchFrom(b == Button::Down ? 1 : -1);
        return true;
    }
    // The stick, every frame: with Y held, one choice a push (back near the middle before the next).
    // Returns true while Y is held (the screen's cursor does not move meanwhile).
    bool stick(const Input& input, Session& session) {
        if (!input.held(Button::Y)) held_ = false; // (let go while another screen was up: no catch)
        if (!held_) return false;
        const float x = input.stickX(), y = input.stickY();
        const float push = std::fabs(x) > std::fabs(y) ? std::fabs(x) : std::fabs(y);
        if (push < 0.3f) stickOut_ = false;
        else if (!stickOut_ && push >= 0.6f) {
            stickOut_ = true;
            direction(std::fabs(x) > std::fabs(y) ? (x > 0.f ? Button::Right : Button::Left) : (y > 0.f ? Button::Down : Button::Up), session);
        }
        return true;
    }

private:
    bool held_ = false, chose_ = false, stickOut_ = false;
};

// "CATCH THE LAST 2 BARS OF THE MIX", "CATCH THE LAST BAR OF 3 BASS": what a let go of Y would catch.
std::string catchChoiceLine(const Session& session);
// What a catch came to: "CAUGHT THE LAST 2 BARS AS "CATCH 3"" (of a track: "CAUGHT 2 BARS OF 3 BASS AS..."; fewer
// than asked: "CAUGHT ONLY 1 BAR..."; no whole bar yet: "CAUGHT 2 BARS' WORTH..."), or why nothing was. Upper case.
std::string caughtLine(const Session& session, const std::string& name);

} // namespace apotheca
