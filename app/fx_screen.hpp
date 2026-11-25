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
//                    is full (half a second, G29) the effect is deleted, with its
//                    settings and its automation. SEL + B brings it back (G10);
//                    the heading says DELETING THIS EFFECT while the bar fills.
//                    Let go sooner and nothing goes. (On SENDS a tap of B: nothing sent.)
//                    Hold Y on one of the track's own lines: up / down moves
//                    it there, settings, automation and MODs with it. The
//                    cursor goes with it; the ends stop. One step of undo.
//                    (The buses' and the master's never move.)
//   in the settings  up / down: the next setting. Left: back to the list, from any setting; right
//                    does nothing (owner, 2026-11-14: LEFT doing two jobs was awkward).
//                    Hold A + left / right: the slot's TYPE before / after (COMP to DUCK; stepType).
//                    The ends stop. The cursor stays on its row. A type switched back to comes back
//                    with the settings it was left with (switchEffectType).
//                    Hold A + stick up / down turns it; the D-pad moves it a
//                    twentieth (BAND's FREQ a thirtieth: a third of an octave),
//                    or to the next choice. B: its usual value (a send: nothing sent).
//                    Hold Y on BAND's FREQ, GAIN or Q: left / right is FREQ and
//                    up / down GAIN (on Q, Q), the stick and the D-pad alike,
//                    both at once on the stick (bandGrip). The cursor stays.
//   L1 / R1          the track before / after (it stops at the first and the
//                    last, as the SONG screen's columns do); the cursor stays
//   X                hear the track (lane 1 on drums; a synth or the sampler: middle C),
//                    for as long as X is held, over the song too; a tap for a moment. Held
//                    on a track's slot setting or a send: its MOD (mod_knob.hpp), its fields
//                    in the heading's place (on SENDS, in the other send's row) while X is
//                    held
//   START            play / stop

#include "fx_picture.hpp"
#include "screen.hpp"
#include <SDL.h>

#include <string>
#include <vector>

#include "input.hpp"
#include "mod_knob.hpp"
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

// One of a track's own effects a slot up or down (the buses' and the master's never move):
// kind, settings, automation, MODs aimed at it and remembered types, all with it. The ends
// stop. False, and nothing done, off a track's three slots or past an end.
bool moveEffectSlot(Session& session, int track, int slot, int by);
// The same, as the player switches TYPE (LEFT / RIGHT in a slot's settings, or let go on another type in the
// list of effects): the slot remembers the type it leaves, its settings as they are (Session::rememberType),
// and a type it has held before comes back with ITS settings as it was left, to the bit, not with ones carried
// from the type it leaves. Its automation is what changeEffectType does with it: the lanes of the settings the
// two share move to their new places, the rest are cleared (nothing of a remembered type's automation is kept).
bool switchEffectType(Session& session, int track, int slot, EffectKind to);

class FxScreen : public Screen {
public:
    explicit FxScreen(Session& session) : session_(session), project_(session.project), modKnob_(session, this) {}

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
    // Y held on one of BAND's FREQ, GAIN or Q dials (A not held; gesture audit G18) moves the bell two ways at
    // once: LEFT / RIGHT (the stick across) FREQ, UP / DOWN GAIN, or Q on the Q dial. The setting up / down
    // turns (kBandGain or kBandQ), or -1 where Y is not that grip.
    static constexpr int kBandFreq = 0, kBandGain = 1, kBandQ = 2; // BAND's settings (its row in the effect table)
    int bandGrip() const;
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
    static constexpr Uint32 kDeleteHoldMs = ui::kHoldUndoneMs; // (undo brings an effect back: G29)
    // In the heading while the bar fills. Effects are in undo (since 2026-11-14: Session::soundMark), so
    // SEL + B brings one back: the MOD screen's words, not CANNOT BE UNDONE (gesture audit G10).
    static constexpr const char* kDeletingWords = "DELETING THIS EFFECT";
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

    // X held on a track's slot setting or a send: its MOD (for the tests). The knob under the cursor as
    // an automation lane, or -1 (the list, a bus, the master).
    const ModKnob& modKnob() const { return modKnob_; }
    int knobHere() const;
    // Where the MOD's fields are drawn while X is held: the heading's place; on SENDS the other send's row.
    int modPanelY() const { return !sends(row_) ? kPaneY : (column_ == 1 ? kSendY + kSendH + kSendGap : kSendY); }
    mutable int liveStep_ = -1;     // the picture's live line: the step it last saw, and when (SDL ticks)
    mutable Uint32 liveAt_ = 0;
    mutable float liveLevel_ = 0.f; // the track's level as the picture shows it, held and falling
    int modPanelH() const { return sends(row_) ? kSendH : kHeadH; }

    static constexpr int kRows = kTrackSlots + 1 + kBuses + kMasterSlots;
    static constexpr int kSendsRow = kTrackSlots, kBusRow = kSendsRow + 1, kMasterRow = kBusRow + kBuses;

    // Where things are drawn (for the tests that read the screen). The page goes down to kPageBottom, 7 above
    // the hints bar as its top is 7 under the top bar: the list's last line, the fourth setting, the second send
    // and the full list of effects all end there (owner, 2026-11-13: it once stopped 40 short).
    static constexpr int kPageBottom = ui::kScreenH - ui::kHintsH - 7;
    static constexpr int kListX = 8, kListW = 200, kLineH = 41, kLineGap = 3;
    static constexpr int kPaneX = 222, kPaneW = ui::kScreenW - 8 - kPaneX, kPaneY = 34, kHeadH = 93;
    // Under the heading (owner, 2026-11-24, design B): the picture of what the effect does, then its settings as four
    // dials in a row, a dial a setting, each in its box: its name at the top, the ring, its value under it, a meter's
    // strip (COMP) and the purple line of automation at the foot.
    static constexpr int kSetY = kPaneY + kHeadH + 8;
    static constexpr int kPicY = kSetY, kPicH = 140;
    static constexpr int kDialGap = 6, kDialY = kPicY + kPicH + kDialGap, kDialH = kPageBottom - kDialY, kDialW = (kPaneW - 3 * kDialGap) / 4;
    static constexpr int dialX(int p) { return kPaneX + p * (kDialW + kDialGap); }
    static constexpr int dialCX(int p) { return dialX(p) + kDialW / 2; }
    static constexpr int kDialR = 30, kDialRing = 6, kDialCY = kDialY + 74; // the ring: its radius, how thick, its centre
    static constexpr int kDialNameY = kDialY + 10, kDialValueY = kDialY + 116; // the name's top, the value's top
    static constexpr int kMeterX = 10, kMeterW = kDialW - 20, kMeterY = kDialY + kDialH - 16, kMeterH = 4; // (x from the dial's left)
    static constexpr int kDialIconY = kDialY + 136; // its icon, under the value
    static constexpr int kAutoY = kDialY + kDialH - 5; // the purple line: the setting moves with the pattern
    // A pixel in the middle of dial p's ring, `part` of the way round (for the tests that read a dial).
    static SDL_Point dialAt(int p, float part) { return dialPoint(dialCX(p), kDialCY, kDialR - kDialRing / 2.f, part); }
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
    // The room a value (and a name) has in a dial.
    static constexpr int kValueRoom = kDialW - 10;

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
    // The key that switches TYPE from where the cursor is, in a slot's settings with A not held, or nullptr where
    // neither LEFT nor RIGHT switches it. It names only what does something: no RIGHT on the family's last type,
    // no LEFT on its first, nor on the first setting (there LEFT is the list).
    const char* typeKey() const;
    // The same, asked with numbers (a test can ask for a family of one, which no family is today): the cursor's
    // column (0 the list), how many types the family has, and which of them the slot is. Below the first setting
    // it is nullptr only for a family of one, and that is what sends LEFT back to the list from there.
    static const char* typeKeyFor(int column, int types, int at);

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
    void turnSetting(int index, float amount); // a slot's setting `index` (not the cursor's), by `amount`
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
    bool yHeld_ = false; // Y is held: on one of the track's lines, up / down moves it
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
    ui::StickClimb climb_; // a full push held on the cursor climbs (G07)
    Button repeating_ = Button::None;
    ui::Repeat repeat_; // the held direction's repeats (ui.hpp)
    Uint32 lastMs_ = 0;
    ModKnob modKnob_;
    bool stickWait_ = false; // X let go off a MOD with the stick still pushed: it moves nothing until it comes back
    void endMod(const Input& input);
    // A direction moved the cursor onto BAND's dials with Y held (press). It was a move, not a turn (review of G18,
    // as the roll's X): the held D-pad stops and the stick waits until it has come back (moved()).
    bool gripEntered_ = false;
    void moved(const Input& input);
    void drawModPanel(SDL_Renderer* r, const Fonts& fonts) const;
};

// The page reaches its foot exactly: the list's last line, the fourth setting and the second send.
static_assert(FxScreen::lineY(FxScreen::kRows - 1) + FxScreen::kLineH == FxScreen::kPageBottom, "the list ends at the page's foot");
static_assert(FxScreen::kDialY + FxScreen::kDialH == FxScreen::kPageBottom && FxScreen::dialX(3) + FxScreen::kDialW <= FxScreen::kPaneX + FxScreen::kPaneW, "the dials end at the page's foot and the pane's edge");
static_assert(FxScreen::kDialCY + FxScreen::kDialR < FxScreen::kDialValueY - 4 && FxScreen::kDialCY - FxScreen::kDialR > FxScreen::kDialNameY + 16, "the ring clears the name and the value");
static_assert(FxScreen::kSendY + 2 * FxScreen::kSendH + FxScreen::kSendGap == FxScreen::kPageBottom, "the sends end at the page's foot");
static_assert(FxScreen::kPickY + 2 * FxScreen::kPickPad + FxScreen::kPickLines * FxScreen::kPickLineH == FxScreen::kPageBottom, "the list of effects ends at the page's foot");

} // namespace apotheca
