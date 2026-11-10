#pragma once
// The whole program behind the window: the project, the three screens, and
// moving between them. No SDL window or sound card is needed to drive it, so
// the tests press its buttons directly.
//
//   L2 / R2    previous / next screen: SONG, PATTERN, DEVICE
//
// PATTERN and DEVICE show whichever track the Song screen's cursor chose: the
// drum grid or the piano roll, the kit or the synth.

#include <SDL.h>

#include <memory>

#include "input.hpp"
#include "note_screen.hpp"
#include "pattern_screen.hpp"
#include "phazed_screen.hpp"
#include "session.hpp"
#include "song_screen.hpp"
#include "ui.hpp"

namespace apotheca {

class App {
public:
    explicit App(Engine& engine);

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs); // once per frame
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    Project& project() { return *project_; }
    Session& session() { return session_; }
    SongScreen& song() { return song_; }
    PatternScreen& drums() { return drums_; }
    NoteScreen& notes() { return notes_; }
    PhazedScreen& phazed() { return phazed_; }
    PhazedScreen& twin() { return twin_; }

private:
    void drawKit(SDL_Renderer* renderer, const Fonts& fonts) const;

    std::unique_ptr<Project> project_; // large, so not on the stack
    Session session_;
    SongScreen song_;
    PatternScreen drums_;
    NoteScreen notes_;
    PhazedScreen phazed_; // the same screen, once for each synth
    PhazedScreen twin_;
};

} // namespace apotheca
