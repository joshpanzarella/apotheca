#pragma once
// The Device screen for a drum track: the kit, a lane to a row.
//
// Each lane plays its built-in drum, or a recording (a WAV file from the
// "samples" folder). A recording can be FITTED: stretched, at the same pitch,
// to last a number of steps at the song's tempo, so a loop stays in time
// whatever the tempo is.
//
//   the stick or D-pad moves: up / down through the lanes, left / right
//   between SOUND and FIT.
//   SOUND   tap A: the list of recordings (sample_picker.hpp), to pick one
//           quickly. Or hold A + up / down: the built-in drum, then each
//           recording, one at a time. B: the drum back.
//   FIT     hold A + up / down: OFF, 1, 2, 4, 8, 16 (one bar), 32, 64 steps.
//           B: off.
//   Y       CATCH: the last bar the song played, all of it as it was
//           heard, becomes a recording on this lane, fitted to one bar.
//   R1      RECORD from a USB microphone or interface onto this lane: once
//           to start (it waits for sound), again to stop. Ten seconds at most.
//   L1      SLICES: the PATTERN tab, zoomed into this lane's recording cut
//           into slices, for chopping a break (pattern_screen.hpp)
//   X       hear the lane
//   START   play / stop

#include "screen.hpp"
#include <SDL.h>

#include <string>
#include <vector>

#include "input.hpp"
#include "sample_picker.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class KitScreen : public Screen {
public:
    explicit KitScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // For tests and stills.
    void goTo(int lane, int column) { lane_ = lane; column_ = column; }
    int cursorLane() const { return lane_; }
    int cursorColumn() const { return column_; }
    const std::string& said() const { return said_; }
    enum { kSound, kFit };
    SamplePicker& picker() { return picker_; }

private:
    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    void press(Button direction);
    void stepSound(int by);
    void stepFit(int by);
    void hear();

    Session& session_;
    Project& project_;
    int lane_ = 0, column_ = kSound;
    bool holding_ = false; // A is held
    bool stepped_ = false; // ...and something was changed with it, so letting go is not a tap
    SamplePicker picker_;
    void openPicker();
    std::vector<std::string> names_; // the recordings there were when A went down
    std::string said_; // what the last catch came to, shown for a moment
    Uint32 saidUntil_ = 0;
    bool wasRecording_ = false;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
};

} // namespace apotheca
