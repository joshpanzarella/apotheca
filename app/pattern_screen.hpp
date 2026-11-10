#pragma once
// The Pattern screen for the drum track: eight lanes by sixteen steps.
//
// One rule for the whole screen, so there is nothing to memorise:
//
//   move        D-pad or left stick. Hold either and it runs.
//   A           act on what is under the cursor
//   hold A + right stick   turn what is under the cursor; it fills or drains
//   B           remove what is under the cursor
//
// The cursor can sit on three kinds of thing:
//
//   a step       A places a hit      hold A + stick: that hit's level
//   a lane name  A plays the drum    hold A + stick: that drum's volume
//   the tempo    (up from the top)   hold A + stick: the tempo
//
// Also: X hears the lane, START plays and stops. With A held, D-pad up and
// down nudge the same value in exact steps.

#include <SDL.h>

#include "apotheca/engine.hpp"
#include "input.hpp"
#include "text.hpp"

namespace apotheca {

// How fast the controls feel. One place, so it can become a setting later.
// The owner's rule: faster is better; nobody has ever asked for slower.
struct Feel {
    Uint32 repeatDelayMs = 170;   // hold the D-pad this long before it runs
    Uint32 repeatEveryMs = 30;    // then one move this often (33 a second)
    float stickCellsPerSecond = 36.f; // left stick held fully over
    float fillPerSecond = 3.f;    // right stick fully over: empty to full in a third of a second
    float tempoPerSecond = 90.f;  // right stick fully over, in BPM
};

struct Fonts {
    Font small;  // 12: labels, hints, tabs
    Font medium; // 14: lane names
    Font large;  // 16: values
};

class PatternScreen {
public:
    explicit PatternScreen(Engine& engine) : engine_(engine) {
        for (float& level : laneLevel_) level = 1.f;
    }

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs); // once per frame
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    // For the build check and for stills: writes through the same path the
    // buttons use.
    void setStep(int lane, int step, float level);
    void moveCursorTo(int lane, int step); // step -1 is the lane's name
    float level(int lane, int step) const { return pattern_[lane][step]; }
    float laneLevel(int lane) const { return laneLevel_[lane]; }
    bool playing() const { return playing_; }
    int cursorLane() const { return lane_; }
    int cursorStep() const { return step_; }
    bool cursorOnTempo() const { return onTempo_; }
    float tempo() const { return tempo_; }

    Feel feel;

private:
    static constexpr int kLaneName = -1; // step_ value when the cursor is on a lane's name

    void moveCursor(Button direction);
    void turn(float amount);  // right stick: smooth, in units of "full range"
    void nudge(Button direction); // A + D-pad: exact steps
    void setHitLevel(float level);
    void setLaneLevel(float level);
    void setTempo(float bpm);
    void audition(int lane, float level);

    Engine& engine_;
    float pattern_[kDrumLanes][kSteps] = {};
    float laneLevel_[kDrumLanes];
    int lane_ = 0;
    int step_ = 0;
    bool onTempo_ = false;
    bool playing_ = false;
    bool adjusting_ = false; // A is held
    float tempo_ = 120.f;
    float tempoExact_ = 120.f; // the stick moves this; tempo_ is it, rounded

    Button repeating_ = Button::None; // D-pad direction being held
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f; // left stick: part-moves saved up
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
