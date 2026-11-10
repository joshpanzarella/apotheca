#pragma once
// The Pattern screen for the drum track: eight lanes by sixteen steps.
//
//   D-pad               move the cursor
//   A                   place a hit
//   hold A + right stick  set the hit's level; the block fills or drains
//   hold A + D-pad up/down  the same, a tenth at a time
//   B                   delete the hit
//   X                   hear the lane
//   START               play / stop
//   SELECT + D-pad      tempo (up/down by 1, left/right by 10)

#include <SDL.h>

#include "apotheca/engine.hpp"
#include "input.hpp"
#include "text.hpp"

namespace apotheca {

struct Fonts {
    Font small;  // 12: labels, hints, tabs
    Font medium; // 14: lane names
    Font large;  // 16: values
};

class PatternScreen {
public:
    explicit PatternScreen(Engine& engine) : engine_(engine) {}

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs); // once per frame
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    // For the build check and for stills: writes through the same path the
    // buttons use.
    void setStep(int lane, int step, float level);
    void moveCursorTo(int lane, int step);
    float level(int lane, int step) const { return pattern_[lane][step]; }
    bool playing() const { return playing_; }
    int cursorLane() const { return lane_; }
    int cursorStep() const { return step_; }
    float tempo() const { return tempo_; }

private:
    void moveCursor(Button direction);
    void changeLevel(float delta);
    void changeTempo(float delta);
    void audition(int lane, float level);

    Engine& engine_;
    float pattern_[kDrumLanes][kSteps] = {};
    int lane_ = 0;
    int step_ = 0;
    bool playing_ = false;
    bool adjusting_ = false; // A is held on a hit
    float tempo_ = 120.f;

    Button repeating_ = Button::None; // D-pad direction being held
    Uint32 repeatAt_ = 0;
};

} // namespace apotheca
