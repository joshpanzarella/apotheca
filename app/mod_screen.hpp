#pragma once
// The MOD screen (a tab after FX: docs/plans/modulation-plan.md, section 7, batch 5): a track's two
// modulators, laid out as the FX screen. Two columns, everything in view.
//
//   LEFT    a list of four lines, always there, under two headings:
//             MOD 1        what it is (SINE ... DRIFT, STEPPER, or OFF), what it moves, how much and
//                          how fast, and a small picture of its shape with a dot at now
//             STEPS        MOD 1's row of steps (a STEPPER's), the one playing lit
//             MOD 2, STEPS the same for MOD 2
//   RIGHT   the line the cursor is on, large: a heading (whose, which, the shape picture), then
//           its settings as rows. A MOD: TARGET, SHAPE, DEPTH, RATE, PHASE (an LFO) or LENGTH (a
//           STEPPER). Its STEPS: the row of sixteen, then LENGTH, SLEW and CHANCE.
//
//   in the list      up / down: the next line (round the ends). Right: into the line's settings,
//                    at the one last used there. Hold A on a MOD's line: the list of targets (as
//                    on TARGET). Hold B on either of a MOD's lines: a bar fills, and at a second
//                    the MOD is deleted (SEL + B brings it back). Y: the dice (a MOD's line: a
//                    STEPPER's row, an LFO's shape and rate; STEPS: the row).
//   in the settings  up / down: the next setting (the ends stop); left: back to the list. On the
//                    row of steps left / right go along it (left from the first: the list), down
//                    to LENGTH, and up from LENGTH back to the row.
//                    Hold A + UP / DOWN turns it: DEPTH by 1%, a step by 5%, SLEW and CHANCE by 5%;
//                    SHAPE, RATE, PHASE and LENGTH click once a push; the stick turns DEPTH, a step,
//                    SLEW and CHANCE smoothly and clicks the rest. With A held on the row, LEFT /
//                    RIGHT go along it, so a row is written in one grip.
//                    Hold A on TARGET: the list of targets (MACROS, FX 1 to 3, SENDS: only what a
//                    MOD may move, mods.hpp). UP / DOWN a line, LEFT / RIGHT along it; let go and
//                    it is aimed there (a MOD that was OFF starts as the knob's first: LFO, SINE,
//                    1 BAR, +25%); B with A held: as it was. Nothing is heard or kept while A is held.
//                    B: a setting back to its usual (DEPTH +25%, SHAPE SINE, RATE 1 BAR, PHASE 0,
//                    LENGTH 16, SLEW and CHANCE 0, a step 0). TARGET has none: hold B deletes.
//   L1 / R1          the track before / after (it stops at the first and the last); the cursor stays
//   X                hear the track (held: until let go). Held on a MOD's line while the song is
//                    stopped, that MOD runs (Session::hearMods), with a synth's note held, as X held
//                    on its knob does
//   START            play / stop
//
// Everything here is a step of undo through Session::setMod (soundMark sees it).

#include <SDL.h>

#include "input.hpp"
#include "screen.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class ModScreen : public Screen {
public:
    explicit ModScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // The lines: 0 MOD 1, 1 its STEPS, 2 MOD 2, 3 its STEPS. Column 0 is the line in the list; 1..
    // its settings. A MOD's line: 1 TARGET, 2 SHAPE, 3 DEPTH, 4 RATE, 5 PHASE / LENGTH. A STEPS
    // line: 1 to 16 the steps, 17 LENGTH, 18 SLEW, 19 CHANCE. A column the line does not have is
    // the nearest it has.
    static constexpr int kRows = 2 * kModsPerTrack;
    enum ModColumn { kTarget = 1, kShape, kDepth, kRate, kMore, kModColumns = kMore };
    enum StepsColumn { kFirstStep = 1, kLength = kFirstStep + kModSteps, kSlew, kChance, kStepsColumns = kChance };
    static int modOf(int row) { return row / 2; }
    static bool stepsLine(int row) { return row % 2 == 1; }
    void goTo(int row, int column);
    int cursorRow() const { return row_; }
    int cursorColumn() const { return column_; }
    int settings(int row) const; // how many columns the line has past the list (an OFF MOD: TARGET alone; its STEPS: none)
    int remembered(int row) const { return row < 0 || row >= kRows ? 1 : last_[row]; }
    const ModSettings& mod(int m) const { return session_.modData(project_.track, m); }

    // A held on TARGET (or on a MOD's line): the list of targets. Its lines, and what is on each.
    bool picking() const { return pickRow_ >= 0; }
    enum PickLine { kPickMacros = 0, kPickFx1, kPickFx2, kPickFx3, kPickSends, kPickLines };
    static void pickLineTargets(const TrackData& track, int line, int* out, int* count); // up to 8: what fits, in order
    int pickLine() const { return pickLine_; }
    int pickItem() const { return pickItem_; }
    int pickTarget() const; // the target under the list's cursor, or kModTargetNone
    // B held on a MOD's line: how far its bar has filled, 0..1. Full, the MOD is deleted.
    static constexpr Uint32 kDeleteHoldMs = 1000;
    float deletePart() const { return deletePart_; }

    // What a target is called on screen: "TONE", "CUTOFF, FX 2", "ECHO SEND"; "NOTHING" for none.
    static void targetText(const TrackData& track, int target, char* out, int size);

    // Where the MOD's dot is now, for the picture: the position its engine tick was at, while the
    // song plays or X runs it (else not a number: the picture is drawn with no dot).
    double nowPosition(int m) const;
    int nowPlace(int m) const; // a STEPPER's place playing, on the same terms (-1: none shown)
    // The shape picture: an LFO's cycle at `position` (or its first, with no dot, for not a number),
    // at full height, upside down for a negative DEPTH (what the target does); a STEPPER's row with
    // `place` lit (-1: none). `asWritten`: the row as the player writes it, never turned over (the
    // STEPS line in the list, beside the STEPS pane, which draws it so too).
    // `dot` comes back true where a dot was drawn, at (dotX, dotY).
    struct Dot { bool drawn = false; int x = 0, y = 0; };
    static Dot drawPicture(SDL_Renderer* r, int x, int y, int w, int h, const ModSettings& m, int track, int mod, double position, int place, bool cursor, bool asWritten = false);

    // The hints bar's words where the cursor is now (what draw() hands the bar): up to 6.
    int hintsNow(ui::Hint* list) const;

    // Where things are drawn (for the tests that read the screen). As the FX screen's: the page ends 7
    // above the hints bar; the list's four lines and the settings' rows each reach that foot.
    static constexpr int kPageBottom = ui::kScreenH - ui::kHintsH - 7;
    static constexpr int kListX = 8, kListW = 200, kLineGap = 3;
    static constexpr int kListTop = 33 + 19, kHeadGap = 5 + 19; // the first line's top; the second heading's room
    static constexpr int kLineH = (kPageBottom - kListTop - kHeadGap - (kRows - 1) * kLineGap) / kRows;
    static constexpr int lineY(int row) { return kListTop + row * (kLineH + kLineGap) + (row >= 2 ? kHeadGap : 0); }
    static constexpr int kPaneX = 222, kPaneW = ui::kScreenW - 8 - kPaneX, kPaneY = 34, kHeadH = 93;
    static constexpr int kSetY = kPaneY + kHeadH + 8, kSetGap = 6;
    static constexpr int kRowH = (kPageBottom - kSetY - (kModColumns - 1) * kSetGap) / kModColumns; // a setting's row
    static constexpr int kValueX = kPaneX + 12, kBarX = kPaneX + 168, kBarW = kPaneW - 168 - 14, kBarH = 10;
    static constexpr int kValueRoom = kBarX - kValueX - 8;
    static constexpr int rowY(int index) { return kSetY + index * (kRowH + kSetGap); } // the index'th row of a MOD's settings
    // The STEPS line: the row of steps in a box, then its three settings' rows.
    static constexpr int kStepsBoxH = kPageBottom - kSetY - 3 * kRowH - 3 * kSetGap;
    static constexpr int stepsRowY(int index) { return kSetY + kStepsBoxH + kSetGap + index * (kRowH + kSetGap); } // LENGTH, SLEW, CHANCE
    static constexpr int kStepX = kPaneX + 12, kStepW = (kPaneW - 24) / kModSteps, kStepBarW = kStepW - 4;
    static constexpr int kStepsTop = kSetY + 10, kStepsHalf = 38; // the bars' top; how far a full value reaches from the middle
    static constexpr int kStepsMiddle = kStepsTop + kStepsHalf;
    // The shape picture in the heading, and the small one on a MOD's line in the list.
    static constexpr int kPicW = 150, kPicH = 64, kPicX = kPaneX + kPaneW - 12 - kPicW, kPicY = kPaneY + (kHeadH - kPicH) / 2;
    static constexpr int kSmallPicW = 56, kSmallPicH = 30, kSmallPicX = kListX + kListW - 8 - kSmallPicW;
    // The list of targets: a line each, from under the heading to the page's foot.
    static constexpr int kPickY = kSetY - 2, kPickLineH = (kPageBottom - kPickY) / kPickLines;
    static constexpr int pickLineY(int line) { return kPickY + line * kPickLineH; }
    static constexpr int kPickNameX = kPaneX + 12, kPickItemsX = kPaneX + 100;
    static constexpr const char* kDeletingWords = "DELETING THIS MOD";

private:
    int track() const { return project_.track; }
    void set(int m, const ModSettings& s) { session_.setMod(track(), m, s); }
    // MOD m as it is now: its DEPTH where its DEPTH lane has it while the song plays (the picture
    // turns over by it).
    ModSettings asNow(int m) const {
        ModSettings now = session_.modData(track(), m);
        now.depth = session_.modDepthShown(track(), m);
        return now;
    }
    static constexpr const char* kMovesWords = "moves with the pattern";
    void press(Button b);      // a direction, A held or not
    void turnBy(int by);       // A held + UP / DOWN on a setting: a click or a notch
    void turnSmooth(float by); // the stick with A held on an amount
    void toUsual();            // B on a setting
    void dice(Uint32 nowMs);
    void stepTrack(int by);
    void openPick();
    void pickMove(Button b);
    void pickLetGo(); // A let go on the list of targets: aimed there
    void hear(bool held);
    bool amount() const; // the cursor's setting is turned smoothly by the stick (DEPTH, a step, SLEW, CHANCE)
    void drawList(SDL_Renderer* r, const Fonts& fonts) const;
    void drawModPane(SDL_Renderer* r, const Fonts& fonts) const;
    void drawStepsPane(SDL_Renderer* r, const Fonts& fonts) const;
    void drawPick(SDL_Renderer* r, const Fonts& fonts) const;
    void drawRow(SDL_Renderer* r, const Fonts& fonts, int y, const char* name, const char* value, float part, int choices, int which, bool centre, bool cursor) const;

    Session& session_;
    Project& project_;
    int row_ = 0, column_ = 0;
    int last_[kRows] = {1, 1, 1, 1};
    int stepLast_[kRows] = {kFirstStep, kFirstStep, kFirstStep, kFirstStep}; // the step UP from LENGTH goes back to
    bool holding_ = false;
    int pickRow_ = -1, pickLine_ = 0, pickItem_ = 0, pickTrack_ = -1;
    bool deleting_ = false, deleteTimed_ = false;
    int deleteRow_ = -1;
    Uint32 deleteSince_ = 0;
    float deletePart_ = 0.f;
    bool xHeld_ = false;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
    unsigned rolls_ = 0;
};

// The page reaches its foot: the list's last line, a MOD's last row, the last row under the steps.
static_assert(ModScreen::lineY(ModScreen::kRows - 1) + ModScreen::kLineH <= ModScreen::kPageBottom &&
                  ModScreen::lineY(ModScreen::kRows - 1) + ModScreen::kLineH + 4 > ModScreen::kPageBottom,
              "the list ends at the page's foot");
static_assert(ModScreen::rowY(ModScreen::kModColumns - 1) + ModScreen::kRowH <= ModScreen::kPageBottom &&
                  ModScreen::rowY(ModScreen::kModColumns - 1) + ModScreen::kRowH + 5 > ModScreen::kPageBottom,
              "a MOD's settings end at the page's foot");
static_assert(ModScreen::stepsRowY(2) + ModScreen::kRowH == ModScreen::kPageBottom, "the steps' settings end at the page's foot");

} // namespace apotheca
