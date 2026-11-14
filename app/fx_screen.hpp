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
// An effect is a TYPE of one of seven FAMILIES (apotheca/effects.hpp): DELAY
// has the types CLEAN, TAPE and PING, LEVEL has COMP and DUCK. The list of
// effects (A held) is the families, each line naming its types. Nothing here
// spells an effect out.
//
//   in the list      up / down: the next line (round the ends). Right: into
//                    the line's settings, at the one last used there.
//                    Hold A: the list of effects opens on the right. With A
//                    held, up / down steps through it (a bus offers only its
//                    own family) and left / right through the TYPEs. What the
//                    cursor is on is HEARD at once, but only TRIED: the slot's
//                    own effect, its settings and its automation are kept
//                    aside. Let go of A and what the cursor is on goes in
//                    (another family at its usual settings, its automation
//                    gone; another type of the same family with the settings
//                    the two share, its automation moved); let go on the
//                    effect it started on, or press B, or leave the screen or
//                    the track, and the effect that was there is back as it
//                    was, every setting and all its automation.
//                    Hold B on an effect's line: a bar fills, and when it
//                    is full (a second) the effect is deleted, with its
//                    settings and its automation. It cannot be undone, and
//                    the heading says so while the bar fills. Let go sooner
//                    and nothing goes. (On SENDS a tap of B: nothing sent.)
//   in the settings  up / down: the next setting. Right: the slot's next TYPE (COMP to DUCK); left: the
//                    type before, except on the first setting, where left is back to the list (and on
//                    a family of one, or the sends, left is back to the list from any setting). The ends
//                    stop. The cursor stays on its row. A type switched back to comes back with the
//                    settings it was left with (switchEffectType).
//                    Hold A + stick up / down turns it; the D-pad moves it a
//                    twentieth, or to the next choice. B: its usual value
//                    (a send: nothing sent).
//   L1 / R1          the track before / after (it stops at the first and the
//                    last, as the SONG screen's columns do); the cursor stays
//   X                hear the track (a recording on it, on lane 1 or the sampler:
//                    for as long as X is held; a tap for a moment)
//   START            play / stop
//   (Y does nothing here.)

#include "screen.hpp"
#include <SDL.h>

#include <string>
#include <vector>

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
// The same, as the player switches TYPE (LEFT / RIGHT in a slot's settings, or let go on another type in the
// list of effects): the slot remembers the type it leaves, its settings as they are (Session::rememberType),
// and a type it has held before comes back with ITS settings as it was left, to the bit, not with ones carried
// from the type it leaves. Its automation is what changeEffectType does with it: the lanes of the settings the
// two share move to their new places, the rest are cleared (nothing of a remembered type's automation is kept).
bool switchEffectType(Session& session, int track, int slot, EffectKind to);

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
    // With A held there, what the slot is TRYING (heard, drawn on the right and lit in the list) while the
    // project keeps what it had: `trying()` is true once the engine has been sent another effect than the
    // project's. What the slot holds in the project is data(row) only when nothing is being tried there.
    bool trying() const { return tryRow_ >= 0 && away_; }
    const EffectData& tried() const { return tried_; }
    const EffectData& onLine(int row) const { return data(row); } // what a slot's line shows: what it tries, with A held
    // Nothing is tried any more and the slot sounds what the project holds (B with A held, another screen,
    // another track). The App calls it whenever this is not the screen up.
    void callOff();
    // B held on an effect's line: how far the bar has filled, 0..1. Full, the effect is deleted.
    static constexpr Uint32 kDeleteHoldMs = 1000;
    static constexpr const char* kDeletingWords = "DELETING: CANNOT BE UNDONE"; // in the heading while the bar fills
    float deletePart() const { return deletePart_; }
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

    // Where things are drawn (for the tests that read the screen). The page goes down to kPageBottom, 7 above
    // the hints bar as its top is 7 under the top bar: the list's last line, the fourth setting, the second send
    // and the full list of effects all end there (owner, 2026-11-13: it once stopped 40 short).
    static constexpr int kPageBottom = ui::kScreenH - ui::kHintsH - 7;
    static constexpr int kListX = 8, kListW = 200, kLineH = 41, kLineGap = 3;
    static constexpr int kPaneX = 222, kPaneW = ui::kScreenW - 8 - kPaneX, kPaneY = 34, kHeadH = 93;
    static constexpr int kSetY = kPaneY + kHeadH + 8, kSetGap = 6, kSetH = (kPageBottom - kSetY - 3 * kSetGap) / 4; // a setting's row
    static constexpr int kValueX = kPaneX + 12, kBarX = kPaneX + 168, kBarW = kPaneW - 168 - 14, kBarH = 10; // in it: the value, the bar
    static constexpr int kSendY = kPaneY + 20, kSendGap = 9, kSendH = (kPageBottom - kSendY - kSendGap) / 2; // a send's row
    // In a send's row, down from its top: the value; the bar, the purple line and its words (these three from its foot).
    static constexpr int kSendValueY = 40 + (kSendH - 128) / 2, kSendBarY = kSendH - 44, kSendLineY = kSendH - 24, kSendWordsY = kSendH - 18;
    // The list of effects: a line a family, EMPTY first. The full list fills the box to the page's foot.
    static constexpr int kPickY = kSetY - 2, kPickLines = 8, kPickLineH = (kPageBottom - kPickY) / kPickLines - 1;
    static constexpr int kPickPad = (kPageBottom - kPickY - kPickLines * kPickLineH) / 2; // above the first line and under the last
    static constexpr int kPickNameX = kPaneX + 12, kPickTypesX = kPaneX + 100; // in a line: the family; its types (or its words)
    static constexpr int pickLineY(int place) { return kPickY + kPickPad + place * kPickLineH; }
    static constexpr int lineY(int row) { // the top of a line in the list: three blocks, each under a heading
        return 33 + 19 + row * (kLineH + kLineGap) + (row >= kBusRow ? 5 + 19 : 0) + (row >= kMasterRow ? 5 + 19 : 0);
    }
    static constexpr int kNameX = kListX + 62, kMarkX = kListX + kListW - 12; // in a line: the effect's name; its level mark
    // In a line the type stands kTypeGap after its family. One that would then run into the mark at LARGE
    // (REVERB SPRING) keeps that gap and draws its family in the type's smaller lettering, at every size, to
    // end kNameClear short of the mark or sooner; where it does not fit (XL), the two are held at LARGE. (It
    // once closed the gap up instead, to 2.9: REVERB SPRING then read as one word.) A pair too long even so,
    // both small at LARGE (DRIVE SATURATE, 4 pixels over), is drawn as its type alone, in the family's
    // lettering: `typeAlone`. Which lettering, the gap, and whether the family is drawn small (the tests ask
    // this too).
    static constexpr int kTypeGap = 7, kNameClear = 3;
    static float nameEnd(EffectKind kind) { return static_cast<float>(kMarkX - (effectMeterParam(kind) >= 0 ? 7 : 0)); } // the mark, or the meter before it
    static const Fonts& lineFonts(const Fonts& fonts, EffectKind kind, float* gap, bool* familySmall, bool* typeAlone = nullptr) {
        const char* family = effectFamily(kind);
        const char* type = effectTypeShown(kind);
        const Fonts& large = fonts.fixed();
        *gap = static_cast<float>(kTypeGap);
        *familySmall = type[0] && kNameX + large.medium.width(family) + *gap + large.small.width(type) > nameEnd(kind) - kNameClear; // (REVERB BLOOM ended 1.4 into the clearance)
        const auto fits = [&](const Fonts& set) {
            return kNameX + (*familySmall ? set.small : set.medium).width(family) + *gap + set.small.width(type) <= nameEnd(kind) - (*familySmall ? kNameClear : 0);
        };
        const bool alone = type[0] && !fits(large);
        if (typeAlone) *typeAlone = alone;
        if (alone) return kNameX + fonts.medium.width(type) <= nameEnd(kind) - kNameClear ? fonts : large;
        return !type[0] || fits(fonts) ? fonts : large;
    }
    // The room a value has, in a setting's row and in a send's.
    static constexpr int kValueRoom = kBarX - kValueX - 8;

    // A family's types on its line in the list of effects, as they are written there: "clean", "tape", "ping",
    // with ", " between. In one row in the lettering chosen if they fit kPickTypesX to the box's right edge
    // less 12; if not, held at LARGE; if not even then, in two rows at LARGE (`split`: the first of the
    // second row). Asked with the names alone, so a test can try names no effect has yet.
    struct PickRows {
        const Fonts* set;
        int rows, split;
    };
    static constexpr int kPickTypesRoom = kPaneX + kPaneW - 12 - kPickTypesX, kPickRowGap = 4;
    static constexpr const char* kPickComma = ", ";
    static float typesWidth(const Font& font, const std::vector<std::string>& names, int from, int to);
    static PickRows pickRows(const Fonts& fonts, const std::vector<std::string>& names);
    static std::vector<std::string> typeWords(EffectKind kind); // a family's types, small: {"clean", "tape", "ping"}
    // The heading's TYPE row: its types 20 apart after "TYPE", the one it is on a plate. Those in view run from
    // `from` to before `to`: all where they fit `room`; else as many as fit about the one at `at`, with
    // kTypeMore standing for the rest at a cut end. The room is the same with A held or not (the key's is kept).
    static constexpr float kTypeGapX = 20.f;
    static constexpr const char* kTypeMore = "...";
    static constexpr int kTypeRowY = kPaneY + 64;
    static float typeRowRoom(const Fonts& fonts, int types);
    static float typeRowX(const Fonts& fonts) { return kPaneX + 12.f + fonts.small.width("TYPE") + 12.f; } // where the first type (or "...") stands
    static void typesInView(const Font& font, const std::vector<std::string>& names, int at, float room, int* from, int* to);
    // How wide the row is with the types `from` to before `to` in view (and "..." at a cut end).
    static float typeRowWidth(const Font& font, const std::vector<std::string>& names, int from, int to);
    // WHICH ARE IN VIEW IS LARGE'S AT EVERY SIZE (MOD's five do not all fit at LARGE: a smaller size showing more would
    // be other words): those LARGE shows (typesInView at LARGE); the set the row is drawn in is the one given if they
    // fit its room, else LARGE, the row's "TYPE" and key with it (XL).
    static const Fonts& typeRowView(const Fonts& fonts, const std::vector<std::string>& names, int at, int* from, int* to);
    // What the screen draws with, asked with names alone so a test can draw names no effect has yet (the fifteen
    // planned, and longer): a family's types on its line of the list of effects (its top at `y`, the one at `lit`
    // in brass, -1 none; a family's words are one name), and the heading's TYPE row (on `at`, the key with A held).
    static void drawPickTypes(SDL_Renderer* r, const Fonts& fonts, const std::vector<std::string>& names, int lit, int y);
    // `key`: the key drawn at the row's right end where it is not the list's ("RIGHT" on a setting's first row,
    // "LEFT / RIGHT" on the others: switching TYPE from the settings); with the list open it is "LEFT / RIGHT".
    static void drawTypeRow(SDL_Renderer* r, const Fonts& fonts, const std::vector<std::string>& names, int at, bool list, const char* key = nullptr);
    // The key that switches TYPE from where the cursor is, in a slot's settings with A not held ("RIGHT" on the
    // first setting, "LEFT / RIGHT" on the others), or nullptr where LEFT / RIGHT do not switch it.
    const char* typeKey() const;

private:
    static bool sends(int row) { return row == kSendsRow; }
    static int busOf(int row) { return row >= kBusRow && row < kMasterRow ? row - kBusRow : -1; } // -1: not a bus's line
    int trackOf(int row) const { return row < kBusRow ? project_.track : (row < kMasterRow ? busTrack(row - kBusRow) : kMasterTrack); }
    static int slotOf(int row) { return row < kTrackSlots ? row : (row >= kMasterRow ? row - kMasterRow : 0); }
    // What a slot's line holds, or is trying with A held. (Not for the sends line, which is not a slot.)
    const EffectData& data(int row) const { return row == tryRow_ && away_ && trackOf(row) == tryTrack_ ? tried_ : session_.effectData(trackOf(row), slotOf(row)); }
    // The slot's line is trying something other than what the project holds: what the project's automation
    // and settings say is not what is heard.
    bool tryingOn(int row) const { return row == tryRow_ && away_; }
    void startTrying();
    void tryKind(EffectKind kind);   // the slot sounds `kind`, at the settings it would go in with
    void letGo();                    // A let go: what is tried goes in, or the slot is as it was
    void sendSlot(const EffectData& d); // the engine's slot to `d` (the project untouched)
    void holdAutomation(bool held);
    void stepTrack(int by);
    bool stepType(int by); // in a slot's settings: the slot's next TYPE (by 1) or the one before (-1); false at an end
    float send(int bus) const { return project_.current().send[bus]; }
    void press(Button direction);
    void turn(float amount);
    void hear(bool held = false); // held: X is down; a recording (a drum lane, the sampler) sounds until it is let go
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
    int tryRow_ = -1, tryTrack_ = 0; // A held on a slot's line: which, and whose
    int trySlot_ = 0;                // and its slot, kept after tryRow_ is let go (the engine's hold is let go by it)
    EffectData tried_;               // what it is trying
    bool away_ = false;              // the engine's slot holds tried_, not the project's
    bool held_ = false;              // the engine was told to hold the slot's automation
    bool deleting_ = false, deleteTimed_ = false; // B held on an effect's line
    int deleteRow_ = -1;
    Uint32 deleteSince_ = 0;
    float deletePart_ = 0.f;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
};

// The page reaches its foot exactly: the list's last line, the fourth setting and the second send.
static_assert(FxScreen::lineY(FxScreen::kRows - 1) + FxScreen::kLineH == FxScreen::kPageBottom, "the list ends at the page's foot");
static_assert(FxScreen::kSetY + 4 * FxScreen::kSetH + 3 * FxScreen::kSetGap == FxScreen::kPageBottom, "the settings end at the page's foot");
static_assert(FxScreen::kSendY + 2 * FxScreen::kSendH + FxScreen::kSendGap == FxScreen::kPageBottom, "the sends end at the page's foot");
static_assert(FxScreen::kPickY + 2 * FxScreen::kPickPad + FxScreen::kPickLines * FxScreen::kPickLineH == FxScreen::kPageBottom, "the list of effects ends at the page's foot");

} // namespace apotheca
