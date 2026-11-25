#pragma once
// CATCH's Y (batch S3, 2026-11-25; sampling-gaps.md 4), the same on the kit's lanes, the sampler's SOUND and
// CLOUD's first page:
//   a tap of Y            CATCH, on its LET GO (it was on the press): the last bars played, as chosen below
//   hold Y + LEFT / RIGHT  how many bars: 1, 2 or 4 (the ends stop)
//   hold Y + UP / DOWN     of what: THE MIX (everything, as it was heard), or a track alone, 1 to the last
//                          (the ends stop; held, it runs on at a list's pace). The engine remembers that track
//                          from when it is chosen.
// While Y is held the screen's line says what a let go would catch ("CATCH THE LAST 2 BARS OF THE MIX"); a
// let go after a choice, or after any other button, catches nothing (as L1's own tap on the grids). The
// stick, with Y held, makes one choice a push. The choice is the session's (Session::catchBars, catchFrom).

#include <cmath>
#include <string>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class CatchKey {
public:
    void down() { held_ = true, chose_ = false, stickOut_ = false, runWay_ = Button::None; }
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
        if (b == Button::Left || b == Button::Right) {
            session.stepCatchBars(b == Button::Right ? 1 : -1);
        } else if (b == Button::Up || b == Button::Down) {
            session.stepCatchFrom(b == Button::Down ? 1 : -1);
            runWay_ = b; // held, it runs on (stick, below)
            run_.restart();
        }
        return true;
    }
    // Every frame: with Y held, a held UP / DOWN runs on through the mix and the tracks at a list's pace (up to
    // thirteen of them: a held run, as every list; review fix, 2026-11-25), stopping at the ends; LEFT / RIGHT's
    // three stops are a press each. The stick, one choice a push, any push past the dead zone (back in the middle
    // before the next; a choice clicks once a push, however far: the owner's stick, 2026-11-25), the larger way.
    // Returns true while Y is held (the screen's cursor does not move meanwhile).
    bool stick(const Input& input, Session& session, Uint32 nowMs) {
        if (!input.held(Button::Y)) held_ = false; // (let go while another screen was up: no catch)
        if (!held_ || (runWay_ != Button::None && !input.held(runWay_))) runWay_ = Button::None;
        if (!held_) return false;
        if (runWay_ != Button::None)
            while (run_.due(nowMs, true, ui::Pace::List)) session.stepCatchFrom(runWay_ == Button::Down ? 1 : -1);
        const float x = input.stickX(), y = input.stickY();
        const float push = std::fabs(x) > std::fabs(y) ? std::fabs(x) : std::fabs(y);
        if (push == 0.f) stickOut_ = false; // (the dead zone is already taken out: input.cpp)
        else if (!stickOut_) {
            stickOut_ = true;
            direction(std::fabs(x) > std::fabs(y) ? (x > 0.f ? Button::Right : Button::Left) : (y > 0.f ? Button::Down : Button::Up), session);
        }
        return true;
    }

private:
    bool held_ = false, chose_ = false, stickOut_ = false;
    Button runWay_ = Button::None; // UP or DOWN held with Y: running on
    ui::Repeat run_;
};

// "CATCH THE LAST 2 BARS OF THE MIX", "CATCH THE LAST BAR OF 3 BASS": what a let go of Y would catch.
std::string catchChoiceLine(const Session& session);
// What a catch came to: "CAUGHT THE LAST 2 BARS AS "CATCH 3"" (of a track: "CAUGHT 2 BARS OF 3 BASS AS..."; fewer
// than asked: "CAUGHT ONLY 1 BAR..."; no whole bar of the mix yet: "CAUGHT 2 BARS' WORTH..."), or why nothing was
// (one track with no whole bar since it was chosen: "NO WHOLE BAR OF 3 BASS YET: LET ONE GO BY"). Upper case.
std::string caughtLine(const Session& session, const std::string& name);

} // namespace apotheca
