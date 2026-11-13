#pragma once
// The FX screen: two columns, everything in view.
//
//   LEFT    a list of eight lines, always there:
//             THIS TRACK   its three effects, one after the other, then its
//                          SENDS (how much of it goes to the ECHO bus and how
//                          much to the SPACE bus)
//             BUSES        what the ECHO bus holds (a delay) and what the
//                          SPACE bus holds (a reverb): one each, shared by
//                          every track
//             MASTER       the two effects on everything
//   RIGHT   the line the cursor is on, large: a heading (where it is, the
//           effect, its TYPE if it has types, what it is doing just now),
//           then its settings as wide rows. For the SENDS line: the two sends.
//
// An effect is a FAMILY (apotheca/effects.hpp): DELAY has the types CLEAN and
// TAPE, REVERB has ROOM and HALL. Nothing here spells an effect out.
//
//   in the list      up / down: the next line (round the ends). Right: into
//                    the line's settings, at the one last used there.
//                    Hold A: the list of effects opens on the right. With A
//                    held, up / down steps through it (the effect goes in at
//                    once, at its usual settings; a bus offers only its own
//                    family) and left / right changes the TYPE, keeping the
//                    settings the two types share.
//                    B: empties the line (on SENDS: nothing sent).
//   in the settings  up / down: the next setting. Left: back to the list.
//                    Hold A + stick up / down turns it; the D-pad moves it a
//                    twentieth, or to the next choice. B: its usual value
//                    (a send: nothing sent).
//   X                hear the track
//   START            play / stop
//   (Y does nothing here.)

#include "screen.hpp"
#include <SDL.h>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

// (How a setting reads, effectParamText(), is in apotheca/effects.hpp with the rest of the effect table.)

// Another TYPE of the same family into a slot (TAPE where CLEAN was). The
// settings the two share by name are kept as they READ (effectCarry), the
// rest start at their usual values. Automation written for a setting that is
// kept goes with it, in every scene (to its new place, read the same way);
// automation for a setting the new type does not have is cleared. Nothing is
// written by WRITE. False, and nothing done, if `to` is not another type of
// what the slot holds.
bool changeEffectType(Session& session, int track, int slot, EffectKind to);

class FxScreen : public Screen {
public:
    explicit FxScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // For tests and stills. The lines: 0 to 2 the track's slots, 3 its sends;
    // 4 the ECHO bus, 5 the SPACE bus; 6 and 7 the master's slots. Column 0 is
    // the line itself, in the list; 1.. its settings (on the sends line, 1 is
    // ECHO and 2 is SPACE). A column the line does not have is the nearest it has.
    void goTo(int row, int column);
    int cursorRow() const { return row_; }
    int cursorColumn() const { return column_; }
    // How many settings a line has (the sends line: two; an empty slot: none).
    int settings(int row) const { return sends(row) ? kBuses : effectInfo(static_cast<EffectKind>(data(row).kind)).params; }
    // The setting last used on a line: where RIGHT goes back to. 1 if none was.
    int remembered(int row) const { return row < 0 || row >= kRows ? 1 : last_[row]; }
    // A is held on a slot's line: the list of effects is open on the right.
    bool picking() const { return holding_ && column_ == 0 && !sends(row_); }
    // What the effect on a line is showing just now (COMP: the dB it is turning
    // the sound down), from the engine; 0 for a line with nothing to show. And
    // how long the strip that draws it is, on a bar `width` wide: nothing at
    // 0 dB, the whole width at kMeterFullDb and beyond.
    float meterOf(int row) const;
    static int meterPixels(float dB, int width);
    // What a meter says in the heading: "TURNING DOWN 7 DB" (COMP), "DIPPING 7 DB"
    // (DUCK). Empty under half a dB, and for an effect with no meter.
    static void meterWords(EffectKind kind, float dB, char* out, int size);

    static constexpr int kRows = kTrackSlots + 1 + kBuses + kMasterSlots;
    static constexpr int kSendsRow = kTrackSlots, kBusRow = kSendsRow + 1, kMasterRow = kBusRow + kBuses;

    // Where things are drawn (for the tests that read the screen).
    static constexpr int kListX = 8, kListW = 200, kLineH = 36, kLineGap = 3;
    static constexpr int kPaneX = 222, kPaneW = ui::kScreenW - 8 - kPaneX, kPaneY = 34, kHeadH = 92;
    static constexpr int kSetY = kPaneY + kHeadH + 8, kSetH = 66, kSetGap = 6;  // a setting's row
    static constexpr int kValueX = kPaneX + 12, kBarX = kPaneX + 168, kBarW = kPaneW - 168 - 14, kBarH = 10; // in it: the value, the bar
    static constexpr int kSendY = kPaneY + 20, kSendH = 128, kSendGap = 8;       // a send's row
    static constexpr int kPickY = kSetY - 2, kPickLineH = 31;                    // the list of effects
    static constexpr int kPickNameX = kPaneX + 86, kPickSaysX = kPaneX + 200;
    static int lineY(int row); // the top of a line in the list
    static constexpr int kNameX = kListX + 62, kMarkX = kListX + kListW - 12; // in a line: the effect's name; its level mark
    // The room a value has, in a setting's row and in a send's.
    static constexpr int kValueRoom = kBarX - kValueX - 8;

private:
    static bool sends(int row) { return row == kSendsRow; }
    static int busOf(int row) { return row >= kBusRow && row < kMasterRow ? row - kBusRow : -1; } // -1: not a bus's line
    int trackOf(int row) const { return row < kBusRow ? project_.track : (row < kMasterRow ? busTrack(row - kBusRow) : kMasterTrack); }
    static int slotOf(int row) { return row < kTrackSlots ? row : (row >= kMasterRow ? row - kMasterRow : 0); }
    // What a slot's line holds. (Not for the sends line, which is not a slot.)
    const EffectData& data(int row) const { return session_.effectData(trackOf(row), slotOf(row)); }
    float send(int bus) const { return project_.current().send[bus]; }
    void press(Button direction);
    void turn(float amount);
    void hear();
    void emptySend(int bus);
    void drawList(SDL_Renderer* r, const Fonts& fonts) const;
    void drawSends(SDL_Renderer* r, const Fonts& fonts) const;
    void drawSlot(SDL_Renderer* r, const Fonts& fonts) const;

    Session& session_;
    Project& project_;
    int row_ = 0, column_ = 0;
    int last_[kRows] = {1, 1, 1, 1, 1, 1, 1, 1};
    static_assert(kRows == 8, "last_ has a place for each line");
    bool holding_ = false;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
};

} // namespace apotheca
