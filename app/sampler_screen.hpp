#pragma once
// The Device screen for a sampler track: one recording, played from the
// piano roll and the chord button like any synth.
//
//   the stick or D-pad moves the cursor
//   SOUND      hold A + up / down: each recording in the "samples" folder,
//              heard as it changes. B: none.
//   a setting  hold A + stick up / down turns it; the D-pad moves it by one.
//              B: its usual value.
//                ROOT     the key that plays the recording at its own pitch
//                TUNE     cents
//                START    where in the recording a note begins
//                ATTACK   fade in
//                RELEASE  fade out when the key is let go
//                LOOP     go round from START while the key is held
//                LEVEL
//   Y          CATCH: the last bar the song played becomes this track's recording
//   R1         RECORD from a USB microphone or interface: once to start (it
//              waits for sound), again to stop. Ten seconds at most.
//   X          hear it, at middle C (its own pitch, until ROOT or TUNE is moved).
//              A setting is also heard as it is changed, when the song is stopped.
//   START      play / stop

#include "screen.hpp"
#include <SDL.h>

#include <string>
#include <vector>

#include "input.hpp"
#include "sample_picker.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

void samplerSettingText(int id, int value, char* out, int size);
constexpr int kHearKey = 60;         // the sampler is heard at middle C
constexpr Uint32 kHearEveryMs = 150; // a setting being turned is heard this often at most

class SamplerScreen : public Screen {
public:
    explicit SamplerScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // For tests and stills. Stop 0 is SOUND; 1.. are the settings in order.
    void goTo(int stop) { stop_ = stop < 0 ? 0 : (stop > sampler::kSettings ? sampler::kSettings : stop); }
    int cursor() const { return stop_; }
    int timesHeard() const { return timesHeard_; } // how often it has been played from this screen (for tests)
    const std::string& said() const { return said_; }
    SamplePicker& picker() { return picker_; }

private:
    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    void press(Button direction);
    void stepSound(int by);
    void hear();
    void setSetting(int id, int value); // kept, and heard

    Session& session_;
    Project& project_;
    int stop_ = 0;
    bool holding_ = false;
    bool stepped_ = false; // A was used to change something: letting go is not a tap
    SamplePicker picker_;
    void openPicker();
    std::vector<std::string> names_;
    std::string said_;
    Uint32 saidUntil_ = 0;
    bool wasRecording_ = false;
    float saved_ = 0.f, turned_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
    Uint32 heardAt_ = 0; // when a changed setting was last heard
    int timesHeard_ = 0;
};

} // namespace apotheca
