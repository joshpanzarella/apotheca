#pragma once
// The rest at the end of a pass (gesture audit G06, 2026-11-24).
//
// main.cpp goes round every 16 ms when nothing happens (owner, 2026-11-15: save the battery), and it
// still does. Before, a pass ended in one SDL_Delay(16), so a press that came during the drawing or the
// rest waited all of it before it was even read. Now the 16 ms is slept in four naps of 4 ms, and
// before each nap the events that came are taken off SDL's queue and looked at: a button pressed or
// let go, or a volume key, ends the rest there and then, and the next pass reads it straight away.
//
// "A button" is what Input reads as one, on a copy of the program's Input moved on by each event: a
// trigger or a D-pad that the device sends as an axis counts when it crosses its threshold. Nothing
// else ends the rest. A stick's motion does not (a resting stick that jitters sends a stream of it),
// nor a key's repeat, nor the window's events: with no press, a pass is the 16 ms rest it always was.
// So a press costs at most one pass more than before (one more picture), and holding a button none.
//
// Nothing taken is lost: the next pass reads the events the rest took first, in the order they
// came, then SDL's own (next()). SDL2 stamps an event with SDL_GetTicks when it puts it on its queue,
// which is when it is pumped: pumping every 4 ms also gives a press its time (ButtonEvent::ms, for TAP
// TEMPO) to within a nap while the program rests; one during the pass's work is stamped as the work
// ends. The pass's clock was up to a whole pass out.
//
// Only SDL calls every SDL2 has (SDL_PumpEvents, SDL_PeepEvents, SDL_Delay): the handheld runs its
// firmware's own libSDL2, of an age not known here. (SDL_WaitEventTimeout was not used: it wakes on
// any event, the stick's included.)

#include <SDL.h>

#include <cstddef>
#include <vector>

#include "input.hpp"

namespace apotheca {

class PassWait {
public:
    static constexpr Uint32 kRestMs = 16; // a pass's rest when nothing is pressed: as it always was
    static constexpr int kNaps = 4;
    static constexpr Uint32 kNapMs = kRestMs / kNaps;

    PassWait() { kept_.reserve(256); }

    // Does this event press or let go of a button? Read on `probe`, a copy of the program's Input that
    // each event moves on, as the real one will be. A volume key (main.cpp's, read before Input) counts.
    static bool presses(Input& probe, const SDL_Event& event) {
        if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_VOLUMEUP || event.key.keysym.sym == SDLK_VOLUMEDOWN)) return true;
        ButtonEvent ignored;
        return probe.handle(event, ignored);
    }

    // The rest. `nap(ms)` sleeps; `take(events)` puts every event that came since on the end of
    // `events`. Before each nap what came is looked at, and a press ends the rest there. Returns the
    // naps slept: kNaps when nothing was pressed, fewer when a press cut it short (0: one came while
    // the pass was at work, and the next pass starts at once).
    template <class Nap, class Take>
    int rest(const Input& input, Nap nap, Take take) {
        Input probe = input;
        std::size_t seen = given_; // (any the pass did not read yet are read by the probe too)
        for (int naps = 0; naps < kNaps; ++naps) {
            take(kept_);
            for (; seen < kept_.size(); ++seen)
                if (presses(probe, kept_[seen])) return naps;
            nap(kNapMs);
        }
        return kNaps; // (what came in the last nap the pass reads itself)
    }

    // The program's rest: SDL_Delay, and SDL's queue emptied into ours.
    int rest(const Input& input) {
        return rest(input, [](Uint32 ms) { SDL_Delay(ms); }, [](std::vector<SDL_Event>& into) {
            SDL_PumpEvents();
            SDL_Event event;
            while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1) into.push_back(event);
        });
    }

    // The next event for a pass to read: those the rest took, in order, then SDL's own. False when
    // there is none left.
    bool next(SDL_Event& event) {
        if (given_ < kept_.size()) {
            event = kept_[given_++];
            return true;
        }
        kept_.clear();
        given_ = 0;
        return SDL_PollEvent(&event) == 1;
    }

    // For the tests: the events taken and not yet read.
    std::size_t waiting() const { return kept_.size() - given_; }

private:
    std::vector<SDL_Event> kept_;
    std::size_t given_ = 0;
};

} // namespace apotheca
