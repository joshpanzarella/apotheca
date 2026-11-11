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
//                      started on, or 80% if you started on an empty step
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
// there steps its chance 100 > 75 > 50 > 25. B removes. X hears the lane.
// START plays and stops.
//
// A cell can be 1, 2, 4 or 8 bars long (set on the Song screen). The grid
// shows one bar; running the cursor off the right of step 16 goes on to the
// next bar, and the top bar says which (BAR 2 OF 4).
//
// The cursor can also sit on a lane's name (A plays the drum, hold A + stick
// is that drum's volume) and on the tempo (up from the top lane; hold A +
// stick). With A or Y held, the D-pad nudges the same values in exact steps.
//
// STEP EFFECTS: what one hit does besides play. Hold X on a hit (X by itself
// still just hears it) and the stick or D-pad sets them, a click at a time,
// each heard as it changes:
//
//   hold X, up / down        PITCH    a semitone at a time, an octave either way
//   hold X, left / right     RATCHET  the hit played 2, 3 or 4 times inside its step
//   hold X, L1               REVERSE  backwards, on and off
//   hold X, R1               CUT      stopped after a step, half a step, a quarter; off
//
// They combine, and each hit has its own. Marks on the block: slits for a
// ratchet, an arrow for backwards, a red edge for a cut, and a dash beside
// it that sits higher or lower with the pitch.
//
// SLICES, for chopping a break. L1 on a lane that plays a recording zooms the
// grid into that one lane (L1 again: back). The recording is drawn large, cut
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
//   on the lane's name  up / down: whether a hit plays ONE SLICE and stops, or
//                       RUNS ON through the recording until the next hit.
//                       Hold Y + right rolls every hit's slice; left puts
//                       them back in order.
//
// Slices that follow each other in order play as one unbroken sound; only a
// slice out of order is heard as a cut (drums.hpp).

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
struct Feel {
    Uint32 repeatDelayMs = ui::kCursorDelayMs; // hold the D-pad this long before it runs
    Uint32 repeatEveryMs = ui::kCursorEveryMs; // then one move this often (33 a second)
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
    float tempo() const { return project_.tempo; }
    bool painting() const { return paint_ != Paint::None; }
    bool paintingRandom() const { return paint_ == Paint::Random; }
    int diceHits(int lane) const; // how many hits on the lane the dice placed and still own
    // Step effects (see the top of this file).
    void setFx(int lane, int step, StepFx fx);
    const StepFx& fx(int lane, int step) const { return pat().fx[lane][step]; }
    bool effecting() const { return fxHeld_; } // X is held on a hit
    // The slice view (see the top of this file).
    bool slicing() const { return slicing_; }
    void setSlicing(bool on);         // on only if the cursor's lane plays a recording; if not, it says why
    bool canSlice(int lane) const;    // the lane plays a recording that is there
    int slices() const { return session_.laneSlices(project_.track, lane_); } // the cursor's lane
    int sliceOf(int step) const;      // the slice a hit plays, from 0: the one its start falls in
    float ownStart(int step) const;   // where a step's own slice begins: step 5 of bar 1 is slice 5
    const char* said() const { return lastMs_ < saidUntil_ ? said_ : ""; }
    bool fromDice(int lane, int step) const { return pat().fromDice[lane][step]; }

    Feel feel;

private:
    static constexpr int kLaneName = -1; // step_ value when the cursor is on a lane's name
    enum class Axis { None, UpDown, Sideways };
    enum class Turning { Nothing, Level, Nudge, Chance, Start }; // which value the stick has hold of, for drawing
    enum class Paint { None, OneLevel, Random };

    bool onStep() const { return !onTempo_ && step_ != kLaneName; }
    bool hitHere() const { return onStep() && pat().level[lane_][step_] > 0.f; }
    // The pattern being shown: the project's current track and scene.
    DrumPattern& pat() { return project_.current().drums[project_.scene][project_.bar]; }
    const DrumPattern& pat() const { return project_.current().drums[project_.scene][project_.bar]; }
    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    void moveCursor(Button direction);
    void dragCursor(Button direction); // a move the player made: it paints if a brush is down
    void placeHit(float chance);
    void paintHere();
    void diceOrder(int lane, int order[kSteps]) const;
    float diceLevel(int lane, int step) const;
    bool diceAdd();
    bool diceRemove();
    void diceTurn(float hits);
    void diceReshuffle();
    float randomLevel();
    void turnUpDown(float amount);   // with A: level, drum volume or tempo
    void turnSideways(float amount); // with A: nudge
    void turnChance(float amount);   // with Y
    void nudgeExact(Button direction, bool withY);
    void setLaneLevel(float level);
    void setTempo(float bpm);
    void audition(int lane, float level, float start = 0.f, float part = 0.f, const StepFx& fx = StepFx()); // part: this much of the sound (0: as the lane is set)
    void sliceUpDown(int by);        // the slice view's up / down
    void fxTurn(Button direction);   // with X held: one click of pitch (up / down) or ratchet (left / right)
    void fxRoll();                   // with X held, Y: the dice set the hit's step effects
    void hearHit();                  // the hit under the cursor, as the pattern would play it
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

    bool shaping_ = false;  // A is held
    bool dicing_ = false;   // Y is held
    bool yDidSomething_ = false; // this hold of Y already placed or turned, so letting go is not a tap
    Axis axis_ = Axis::None; // the stick does one job at a time until it is let go
    Turning turning_ = Turning::Nothing;

    bool rolled_ = false;        // this push of the stick has already rolled the lane's starts
    Uint32 heardStartAt_ = 0;    // when a sliding start was last played
    float diceSaved_ = 0.f; // part-hits saved up while the stick turns

    bool fxHeld_ = false;  // X is held on a hit: the stick sets its step effects
    float fxSaved_ = 0.f;  // part-clicks saved up while the stick is held over
    bool fxWas_ = false;
    bool slicing_ = false; // the slice view is showing
    char said_[96] = "";   // a line said for a moment (why a lane cannot be sliced)
    Uint32 saidUntil_ = 0;

    bool erasing_ = false; // B is held: hits the cursor crosses are deleted
    Paint paint_ = Paint::None;
    float brush_ = 0.8f;              // the level R1 is painting with
    std::uint32_t dice_ = 0x9E3779B9u; // for R2's random levels

    Button repeating_ = Button::None; // D-pad direction being held
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f; // cursor part-moves saved up
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
