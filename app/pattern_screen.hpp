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
//   hold Y, stick up / down      CHANCE  the block fades toward a ghost
//
// Painting, for rows of hits in one sweep. Hold a shoulder button and drag
// the cursor with the stick or D-pad; every step it crosses gets a hit:
//
//   hold R1 and drag   every hit at ONE level: the level of the hit you
//                      started on, or 80% if you started on an empty step
//   hold L1 and drag   every hit at a RANDOM level
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
// The cursor can also sit on a lane's name (A plays the drum, hold A + stick
// is that drum's volume) and on the tempo (up from the top lane; hold A +
// stick). With A or Y held, the D-pad nudges the same values in exact steps.

#include <SDL.h>

#include <cstdint>

#include "apotheca/engine.hpp"
#include "input.hpp"
#include "text.hpp"

namespace apotheca {

// How fast the controls feel. One place, so it can become a setting later.
// The owner's rule: faster is better; nobody has ever asked for slower.
struct Feel {
    Uint32 repeatDelayMs = 170;   // hold the D-pad this long before it runs
    Uint32 repeatEveryMs = 30;    // then one move this often (33 a second)
    float stickCellsPerSecond = 36.f; // stick held fully over, moving the cursor
    float fillPerSecond = 3.f;    // stick fully over: empty to full in a third of a second
    float tempoPerSecond = 90.f;  // stick fully over, in BPM
};

struct Fonts {
    Font small;  // 12: labels, hints, tabs
    Font medium; // 14: lane names
    Font large;  // 16: values
};

class PatternScreen {
public:
    explicit PatternScreen(Engine& engine);

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs); // once per frame
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    // For the build check and for stills: writes through the same path the
    // buttons use.
    void setStep(int lane, int step, float level);
    void setNudge(int lane, int step, float nudge);   // -0.5..0.5 of a step
    void setChance(int lane, int step, float chance); // 0.05..1
    void moveCursorTo(int lane, int step); // step -1 is the lane's name
    float level(int lane, int step) const { return pattern_[lane][step]; }
    float nudge(int lane, int step) const { return nudge_[lane][step]; }
    float chance(int lane, int step) const { return chance_[lane][step]; }
    float laneLevel(int lane) const { return laneLevel_[lane]; }
    bool playing() const { return playing_; }
    int cursorLane() const { return lane_; }
    int cursorStep() const { return step_; }
    bool cursorOnTempo() const { return onTempo_; }
    float tempo() const { return tempo_; }
    bool painting() const { return paint_ != Paint::None; }
    int diceHits(int lane) const; // how many hits on the lane the dice placed and still own
    bool fromDice(int lane, int step) const { return fromDice_[lane][step]; }

    Feel feel;

private:
    static constexpr int kLaneName = -1; // step_ value when the cursor is on a lane's name
    enum class Axis { None, UpDown, Sideways };
    enum class Turning { Nothing, Level, Nudge, Chance }; // which value the stick has hold of, for drawing
    enum class Paint { None, OneLevel, Random };

    bool onStep() const { return !onTempo_ && step_ != kLaneName; }
    bool hitHere() const { return onStep() && pattern_[lane_][step_] > 0.f; }
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
    void audition(int lane, float level);

    Engine& engine_;
    float pattern_[kDrumLanes][kSteps] = {};
    float nudge_[kDrumLanes][kSteps] = {};
    float chance_[kDrumLanes][kSteps];
    float laneLevel_[kDrumLanes];
    int lane_ = 0;
    int step_ = 0;
    bool onTempo_ = false;
    bool playing_ = false;
    float tempo_ = 120.f;
    float tempoExact_ = 120.f; // the stick moves this; tempo_ is it, rounded

    bool shaping_ = false;  // A is held
    bool dicing_ = false;   // Y is held
    bool yDidSomething_ = false; // this hold of Y already placed or turned, so letting go is not a tap
    Axis axis_ = Axis::None; // the stick does one job at a time until it is let go
    Turning turning_ = Turning::Nothing;

    bool fromDice_[kDrumLanes][kSteps] = {}; // hits the dice placed and nobody has touched since
    std::uint32_t diceSeed_[kDrumLanes];
    float diceSaved_ = 0.f; // part-hits saved up while the stick turns

    Paint paint_ = Paint::None;
    float brush_ = 0.8f;              // the level R1 is painting with
    std::uint32_t dice_ = 0x9E3779B9u; // for L1's random levels

    Button repeating_ = Button::None; // D-pad direction being held
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f; // cursor part-moves saved up
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
