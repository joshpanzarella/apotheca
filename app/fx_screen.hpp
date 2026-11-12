#pragma once
// The FX screen. Two pages of four rows; Y goes from one to the other, and so
// does moving down off the last row (or up off the first).
//
//   TRACK page   the three effects on the track being worked on, one after
//                the other, then its SENDS: how much of it goes to the ECHO
//                bus and how much to the SPACE bus.
//   ALL page     what the ECHO bus holds (a delay) and what the SPACE bus
//                holds (a reverb): one each, shared by every track, their MIX
//                reading LEVEL. Then the two effects on the master
//                (everything, after the tracks and the buses are mixed).
//
// In each row: the effect, then its settings.
//
//   the stick or D-pad moves the cursor
//   on an effect     hold A + up / down: the next or the one before in the
//                    list. Hold A + left / right: the first of the next group
//                    in the list, or of the one before. It goes in at its
//                    usual settings. B: none. (A bus offers only its own kind.)
//   on a setting     hold A + stick up / down turns it; the D-pad moves it a
//                    twentieth, or to the next choice (the filter's type, the
//                    delay's time). B: back to its usual value.
//   on a send        the same as a setting. B: nothing sent.
//   Y                the other page
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

    // For tests and stills. Rows run through both pages: 0 to 2 the track's
    // slots, 3 its sends; 4 the ECHO bus, 5 the SPACE bus, 6 and 7 the master's
    // slots. Column 0 is the effect, 1.. its settings; on the sends row, 0 is
    // ECHO and 1 is SPACE.
    void goTo(int row, int column);
    int cursorRow() const { return row_; }
    int cursorColumn() const { return column_; }
    int page() const { return row_ / kPageRows; } // 0 the track's, 1 the buses and the master
    static constexpr int kPageRows = 4, kPages = 2, kRows = kPages * kPageRows;
    static constexpr int kSendsRow = kTrackSlots, kBusRow = kPageRows, kMasterRow = kPageRows + kBuses;

private:
    static_assert(kTrackSlots + 1 == kPageRows && kBuses + kMasterSlots == kPageRows, "each page is four rows");
    static bool sends(int row) { return row == kSendsRow; }
    static int busOf(int row) { return row >= kBusRow && row < kMasterRow ? row - kBusRow : -1; } // -1: not a bus's row
    int trackOf(int row) const { return row < kPageRows ? project_.track : (row < kMasterRow ? busTrack(row - kBusRow) : kMasterTrack); }
    static int slotOf(int row) { return row < kTrackSlots ? row : (row >= kMasterRow ? row - kMasterRow : 0); }
    // What a slot's row holds. (Not for the sends row, which is not a slot.)
    const EffectData& data(int row) const { return session_.effectData(trackOf(row), slotOf(row)); }
    int columns(int row) const { return sends(row) ? kBuses : effectInfo(static_cast<EffectKind>(data(row).kind)).params + 1; }
    float send(int bus) const { return project_.current().send[bus]; }
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
