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
//   X          hear it, at its root key
//   START      play / stop

#include <SDL.h>

#include <string>
#include <vector>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

void samplerSettingText(int id, int value, char* out, int size);

class SamplerScreen {
public:
    explicit SamplerScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs);
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    // For tests and stills. Stop 0 is SOUND; 1.. are the settings in order.
    void goTo(int stop) { stop_ = stop < 0 ? 0 : (stop > sampler::kSettings ? sampler::kSettings : stop); }
    int cursor() const { return stop_; }
    const std::string& said() const { return said_; }

private:
    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    void press(Button direction);
    void stepSound(int by);
    void hear();

    Session& session_;
    Project& project_;
    int stop_ = 0;
    bool holding_ = false;
    std::vector<std::string> names_;
    std::string said_;
    Uint32 saidUntil_ = 0;
    float saved_ = 0.f, turned_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
};

} // namespace apotheca
