#pragma once
// The Pattern screen for the drum track: eight lanes by sixteen steps.
//
// The handheld has ONE stick. It does two jobs, and which one depends only on
// whether a button is held:
//
//   nothing held    the stick (or the D-pad) moves the cursor. Hold it and it runs.
//   A held          the stick shapes what is under the cursor
//   Y held          the stick sets how likely it is to play
//
// On a step, that reads as three pictures of one block:
//
//   hold A, stick up / down      LEVEL   the block fills or drains
//   hold A, stick left / right   NUDGE   the block slides early or late
//   hold A, L2 / R2              NUDGE   the same, a tenth of a step a press
//   hold Y, stick up / down      CHANCE  the block fades toward a ghost
//   hold Y, stick left / right   START   where in the drum's sound the hit
//                                        begins: a mark slides under the block,
//                                        and it is heard as it moves
//
// On a lane's name, hold Y and push right to roll a new start for every hit
// on the lane (on the sixteenths of the sound); left puts them all back. On
// a recording fitted to one bar that rearranges the beat and keeps it in time.
//
// Painting, for rows of hits in one sweep. Hold a shoulder button and drag
// the cursor with the stick or D-pad; every step it crosses gets a hit:
//
//   hold R1 and drag   every hit at ONE level: the level of the hit you
//                      started on, or 90% if you started on an empty step
//   ...and R2 as well  every hit at a RANDOM level, for as long as R2 is
//                      held too. (R2 by itself is "next screen", at once.)
//
// Hold B and drag: every hit the cursor crosses is deleted.
//
// Dragging over hits that are already there re-levels them and leaves their
// nudge and chance alone.
//
// Lane dice, for a whole pattern in a second. With the cursor on a lane's
// name:
//
//   hold Y, stick up     hits arrive on that lane, one after another, in a
//                        shuffled order that leans toward the beat
//   hold Y, stick down   they leave again, last in first out
//   tap Y                reshuffle: the same number of hits, somewhere new
//                        (from an empty lane, four hits)
//
// The dice only ever remove hits they placed. A hit you placed, or one of
// theirs that you have since shaped, painted or re-rolled a chance on, is
// yours and stays.
//
// Taps: A places a hit. Y places a "maybe" hit (50%), or on a hit that is
// there steps its chance 100 > 75 > 50 > 25. B removes. X hears the lane (on
// its name or an empty step: for as long as X is held, to the end of its
// recording; a tap for a moment).
// START plays and stops.
//
// A cell can be 1, 2, 4 or 8 bars long (set on the Song screen, or the BARS stop here).
// The grid shows one bar; running the cursor off the right of step 16 goes on to the next
// bar, and the top bar says which (BAR 2 OF 4).
//
// The cursor can also sit on a lane's name (A alone is silent there, X plays
// the drum; hold A + stick is that drum's volume), on the tempo (up from the top lane; hold A + stick),
// and on BARS (up from the tempo; left / right the bar before or after, round the ends,
// hold A + up / down the cell's count, more or fewer). With A or Y held, the D-pad
// nudges the same values in exact steps.
//
// Hold L1, R1 copies the bar shown and A pastes over it (as the piano roll's).
//
// STEP EFFECTS: what one hit does besides play. Hold X on a hit (X by itself
// still just hears it) and the stick or D-pad sets them, a click at a time,
// each heard as it changes:
//
//   hold X, up / down        PITCH    a semitone at a time, an octave either way
//   hold X, left / right     RATCHET  the hit played 2, 3 or 4 times inside its step
//   hold X, L1               REVERSE  backwards, on and off
//   hold X, R1               CUT      stopped after a step, half a step, a quarter; off
//   hold X, B                all of them (and THROW) emptied: the hit stays, plain. One step of undo
//
// They combine, and each hit has its own. Marks on the block: slits for a
// ratchet, an arrow for backwards, a red edge for a cut, and a dash beside
// it that sits higher or lower with the pitch.
//
// FEEL: a lane played by a hand. Hold X on a lane's NAME (X by itself still just hears the
// drum) and the stick or D-pad sets how much every hit of the lane is moved, a click at a time:
// OFF, 10, 25, 50, 75, 100%. It is live: nothing is written into the hits, every time round is
// another, and a hit on a main beat (steps 1, 5, 9, 13) moves half as much.
//
//   hold X, left / right     HUMAN TIME   each hit up to 12 ms early or late
//   hold X, up / down        HUMAN LEVEL  each hit up to 15% louder or quieter
//   hold X, Y                the dice: a small feel (10 or 25%)
//   hold X, A                this lane's feel onto every lane of the kit
//   hold X, B                this lane's feel and SWING back to OFF
//
// A lane with a feel wears a purple mark at the left of its name.
//
// SLICES, for chopping a break. L1 (tapped: it acts as it is let go) on a lane
// that plays a recording zooms the grid into that one lane (L1 again: back). The recording is drawn large, cut
// into slices: sixteen, or one to a step if it is fitted to more than a bar.
// Under it are the bar's sixteen steps, and a line from every hit to the
// slice it plays. Everything above works the same there, and:
//
//   up / down           on a hit: the next / previous slice, heard as it goes
//   A                   places a hit that plays the step's OWN slice (step 5:
//                       slice 5), so hits placed in a row are the break as it was
//   hold R1 and drag    lays the break out in order: a hit on every step
//                       crossed, each on its own slice
//   ...and R2 as well   each on a RANDOM slice: the break, shuffled
//   X                   hears one slice: the hit's, or the step's own
//   on the lane's name  up: a hit plays ONE SLICE and stops; down: it RUNS ON
//                       through the recording until the next hit.
//                       Hold Y + right rolls every hit's slice; left puts
//                       them back in order.
//
// Slices that follow each other in order play as one unbroken sound; only a
// slice out of order is heard as a cut (drums.hpp).
//
// Hold L1, here and in the slice view:
//
//   R1                  copy the bar on screen, every lane and every hit with all it is
//   A                   paste it over the bar on screen (another bar, cell, scene or drum track).
//                       One step of undo; on the same track its automation goes with it
//   up / down           the bar before or after, round the ends (as on the roll)
//   left / right        BARS: the cell's, more or fewer (1, 2, 4 or 8, as the
//                       Song screen's hold A). One step of undo

#include "redraw.hpp"
#include "follow_bar.hpp"
#include "screen.hpp"
#include <SDL.h>

#include <cstdint>

#include "apotheca/engine.hpp"
#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

// How fast the controls feel. One place, so it can become a setting later.
// The owner's rule: faster is better; nobody has ever asked for slower.
// (A held D-pad runs at ui.hpp's paces, as on every screen: ui::Repeat.)
struct Feel {
    float stickCellsPerSecond = 36.f; // stick held fully over, moving the cursor
    float fillPerSecond = 3.f;    // stick fully over: empty to full in a third of a second
    float tempoPerSecond = 90.f;  // stick fully over, in BPM
};

class PatternScreen : public Screen {
public:
    explicit PatternScreen(Session& session);

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override; // once per frame
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;
    unsigned stirring() const override { return said()[0] ? kStirScreenSaid : 0u; } // (redraw.hpp)

    // For the build check and for stills: writes through the same path the
    // buttons use.
    void setStep(int lane, int step, float level);
    void setNudge(int lane, int step, float nudge);   // -0.5..0.5 of a step
    void setChance(int lane, int step, float chance); // 0.05..1
    void setStart(int lane, int step, float start);   // 0..15/16 of the lane's sound
    float start(int lane, int step) const { return pat().start[lane][step]; }
    void moveCursorTo(int lane, int step); // step -1 is the lane's name
    float level(int lane, int step) const { return pat().level[lane][step]; }
    float nudge(int lane, int step) const { return pat().nudge[lane][step]; }
    float chance(int lane, int step) const { return pat().chance[lane][step]; }
    float laneLevel(int lane) const { return project_.current().laneLevel[lane]; }
    bool playing() const { return project_.playing; }
    int cursorLane() const { return lane_; }
    int cursorStep() const { return step_; }
    bool cursorOnTempo() const { return onTempo_; }
    bool cursorOnBars() const { return onBars_; }
    float tempo() const { return project_.tempo; }
    bool painting() const { return paint_ != Paint::None; }
    bool paintingRandom() const { return paint_ == Paint::Random; }
    int diceHits(int lane) const; // how many hits on the lane the dice placed and still own
    // Step effects (see the top of this file).
    void setFx(int lane, int step, StepFx fx);
    const StepFx& fx(int lane, int step) const { return pat().fx[lane][step]; }
    bool effecting() const { return fxHeld_; } // X is held on a hit
    // FEEL (see the top of this file): a lane's HUMAN TIME and HUMAN LEVEL.
    bool feeling() const { return feelHeld_; } // X is held on a lane's name
    LaneFeel laneFeel(int lane) const { return project_.current().laneFeel[lane]; }
    static void drawFeelMark(SDL_Renderer* renderer, int nameY, LaneFeel feel); // the mark on a lane's name (both views)
    static void drawSwingMark(SDL_Renderer* renderer, int nameY, int swing); // SWING: its mark on a lane's name (both views)
    static void drawChokeMark(SDL_Renderer* renderer, int nameY, int group); // CHOKE: the lane's group, on its name (both views)
    // The slice view (see the top of this file).
    bool slicing() const { return slicing_; }
    void setSlicing(bool on);         // on only if the cursor's lane plays a recording; if not, it says why
    bool canSlice(int lane) const;    // the lane plays a recording that is there
    int slices() const { return session_.laneSlices(project_.track, lane_); } // the cursor's lane
    int sliceOf(int step) const;      // the slice a hit plays, from 0: the one its start falls in
    float ownStart(int step) const;   // where a step's own slice begins: step 5 of bar 1 is slice 5
    const char* said() const { return lastMs_ < saidUntil_ ? said_ : ""; }
    // COPY A BAR: hold L1, then R1 copies the bar shown (every lane), A pastes over it (Session::copyBar).
    // L1's own job, the slices, is then done on its LET GO, and only if nothing else was pressed while it was held.
    bool barKeyHeld() const { return l1First_; } // L1 is held, pressed first: R1 copies, A pastes
    void stepBars(int by); // the cell's bars: 1, 2, 4 or 8, one step along that row
    void shouldersPressed() { l1Used_ = true; } // (the App: another shoulder went down with L1 held, a panic on its way)
    bool fromDice(int lane, int step) const { return pat().fromDice[lane][step]; }

    Feel feel;

private:
    static constexpr int kLaneName = -1; // step_ value when the cursor is on a lane's name
    enum class Axis { None, UpDown, Sideways };
    enum class Turning { Nothing, Level, Nudge, Chance, Start }; // which value the stick has hold of, for drawing
    enum class Paint { None, OneLevel, Random };

    bool onStep() const { return !onTempo_ && !onBars_ && step_ != kLaneName; }
    bool hitHere() const { return onStep() && pat().level[lane_][step_] > 0.f; }
    // The pattern being shown: the project's current track and scene.
    DrumPattern& pat() { return project_.current().drums[project_.scene][project_.bar]; }
    const DrumPattern& pat() const { return project_.current().drums[project_.scene][project_.bar]; }
    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    void moveCursor(Button direction);
    void dragCursor(Button direction); // a move the player made: it paints if a brush is down
    void direction(Button b, const Input& input); // a press of the D-pad, or its held repeat: the same job
    void placeHit(float chance);
    void paintHere();
    void diceOrder(int lane, int order[kSteps]) const;
    float diceLevel(int lane, int step) const;
    bool diceAdd();
    bool diceRemove();
    void diceTurn(float hits);
    void diceReshuffle();
    // EVEN (easywins batch 5): with Y held on a lane's name, R1 one hit more and L1 one fewer, the hits
    // spread evenly over the bar (euclidHit); then, while Y stays held, LEFT / RIGHT turn the pattern a step.
    void evenLane(int hits, int turn);
    int laneHits() const;
    float randomLevel();
    void turnUpDown(float amount);   // with A: level, drum volume, tempo or bars
    void moveBar(int by);              // the bar before or after, round the ends
    void turnSideways(float amount); // with A: nudge
    void turnChance(float amount);   // with Y
    void nudgeExact(Button direction, bool withY);
    void setLaneLevel(float level);
    void setTempo(float bpm);
    void audition(int lane, float level, float start = 0.f, float part = 0.f, const StepFx& fx = StepFx()); // part: this much of the sound (0: as the lane is set)
    void sliceUpDown(int by);        // the slice view's up / down
    void fxTurn(Button direction);   // with X held: one click of pitch (up / down) or ratchet (left / right)
    void fxRoll();                   // with X held, Y: the dice set the hit's step effects
    void fxThrow();                  // with X held, A: its THROW a step round (off, ECHO, SPACE, both)
    void fxEmpty();                  // with X held, B: what X shows emptied (a hit's effects; a lane's FEEL and SWING), the hit kept
    void hearHit();                  // the hit under the cursor, as the pattern would play it
    void feelTurn(Button direction); // with X held on a lane's name: one click of HUMAN TIME (left / right) or HUMAN LEVEL (up / down)
    void feelRoll();                 // ...Y: the dice set the lane's feel, small amounts
    void feelToAll();                // ...A: this lane's feel (its SWING too) onto every lane of the kit
    void swingTurn(int by);          // ...L1 / R1: one click of its SWING, less / more
    // The marks a hit's step effects put on its block (pattern_slices.cpp). `rise` is how far the pitch dash moves for a semitone.
    static void drawFxMarks(SDL_Renderer* renderer, const StepFx& fx, int x, int y, int w, int h, int middleY, float rise);
    void drawSlices(SDL_Renderer* renderer, const Fonts& fonts) const; // pattern_slices.cpp
    void turnStart(float amount);    // with Y, sideways
    const char* laneLabel(int lane) const; // the drum's name, or its recording's

    Session& session_;
    Engine& engine_;
    Project& project_;
    int lane_ = 0;
    int step_ = 0;
    bool onTempo_ = false;
    float tempoExact_; // the stick moves this; the project's tempo is it, rounded
    bool onBars_ = false; // BARS, above the tempo: the bar shown, and the cell's count
    float barsSaved_ = 0.f; // the stick's push toward the next bar more or fewer

    bool shaping_ = false;  // A is held
    bool dicing_ = false;   // Y is held
    bool evening_ = false;  // ...and R1 or L1 made the lane EVEN in this hold: LEFT / RIGHT turn it
    int evenHits_ = 0, evenTurn_ = 0;
    bool yDidSomething_ = false; // this hold of Y already placed or turned, so letting go is not a tap
    Axis axis_ = Axis::None; // the stick does one job at a time until it is let go
    Turning turning_ = Turning::Nothing;

    bool rolled_ = false;        // this push of the stick has already rolled the lane's starts
    Uint32 heardStartAt_ = 0;    // when a sliding start was last played
    float diceSaved_ = 0.f; // part-hits saved up while the stick turns

    bool fxHeld_ = false;  // X is held on a hit: the stick sets its step effects
    bool feelHeld_ = false; // X is held on a lane's name: the stick sets the lane's feel
    // A or Y pressed while X was held (the feel's copy and dice, a hit's THROW and dice): that press
    // does nothing of its own if X is let go first. Until the button comes up.
    bool aSpent_ = false, ySpent_ = false;
    float fxSaved_ = 0.f;  // part-clicks saved up while the stick is held over
    bool fxWas_ = false;
    bool fxWait_ = false; // the stick was already over when X went down: it does nothing until it has been let go
    bool fxLastFrame_ = false; // X was held on a hit or a lane's name last frame (its hit can go from under it)
    bool stickWait_ = false;   // X let go with the stick still pushed: the stick does nothing until it comes back (as FX and DEVICE)
    bool slicing_ = false; // the slice view is showing
    char said_[96] = "";   // a line said for a moment (why a lane cannot be sliced)
    Uint32 saidUntil_ = 0;

    bool l1First_ = false; // L1 is held and went down with nothing else held: R1 copies the bar, A pastes it
    bool l1Used_ = false;  // ...and something was pressed while it was held: letting go is no tap (no slices)
    bool erasing_ = false; // B is held: hits the cursor crosses are deleted
    Paint paint_ = Paint::None;
    float brush_ = kPlacedLevel;              // the level R1 is painting with
    std::uint32_t dice_ = 0x9E3779B9u; // for R2's random levels

    Button repeating_ = Button::None; // D-pad direction being held
    ui::Repeat repeat_; // the held direction's repeats (ui.hpp)
    Uint32 lastMs_ = 0;
    BarFollow follow_; // the view follows the bar playing (follow_bar.hpp)
    float stickX_ = 0.f, stickY_ = 0.f; // cursor part-moves saved up
    bool stickWasX_ = false, stickWasY_ = false;
    ui::StickClimb climbX_, climbY_; // a full push held climbs (G07)
};

} // namespace apotheca
