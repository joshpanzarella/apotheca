#pragma once
// The FX screen: the two effects on the track being worked on, and the two
// on the master (everything, after the tracks are mixed).
//
// Four rows, one to a slot. In each: the effect, then its settings.
//
//   the stick or D-pad moves the cursor
//   on an effect     hold A + up / down: the next or the one before in the
//                    list (NONE, FILTER, DRIVE, DELAY, REVERB, CRUSH). It goes
//                    in at its usual settings. B: none.
//   on a setting     hold A + stick up / down turns it; the D-pad moves it a
//                    twentieth, or to the next choice (the filter's type, the
//                    delay's time). B: back to its usual value.
//   X                hear the track
//   START            play / stop

#include "screen.hpp"
#include <SDL.h>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

// (How a setting reads, effectParamText(), is in apotheca/effects.hpp with the rest of the effect table.)

class FxScreen : public Screen {
public:
    explicit FxScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // For tests and stills. Rows: 0, 1 the track's slots; 2, 3 the master's.
    // Column 0 is the effect; 1.. are its settings.
    void goTo(int row, int column);
    int cursorRow() const { return row_; }
    int cursorColumn() const { return column_; }
    static constexpr int kRows = 2 * kEffectSlots;

private:
    int trackOf(int row) const { return row < kEffectSlots ? project_.track : kMasterTrack; }
    int slotOf(int row) const { return row % kEffectSlots; }
    const EffectData& data(int row) const { return session_.effectData(trackOf(row), slotOf(row)); }
    void press(Button direction);
    void turn(float amount);
    void hear();

    Session& session_;
    Project& project_;
    int row_ = 0, column_ = 0;
    bool holding_ = false;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
};

} // namespace apotheca
