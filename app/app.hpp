#pragma once
// The whole program behind the window: the project, the three screens, and
// moving between them. No SDL window or sound card is needed to drive it, so
// the tests press its buttons directly.
//
//   L2 / R2    previous / next screen: SONG, PATTERN, DEVICE, FX
//   L1 + L2 + R1 + R2 together: PANIC. Everything stops and the sound is
//              shut off at once, for as long as they are held.
//
// PATTERN and DEVICE show whichever track the Song screen's cursor chose: the
// drum grid or the piano roll, the kit or the synth.

#include <SDL.h>

#include <memory>

#include "fx_screen.hpp"
#include "input.hpp"
#include "kit_screen.hpp"
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
    KitScreen& kit() { return kit_; }
    FxScreen& fx() { return fx_; }
    // The notice that says a panic happened. It stays while the buttons are
    // held, and after that until any button is pressed (that press does
    // nothing else) or four seconds have gone by.
    bool panicNoteShowing() const { return panicNote_; }

private:
    std::unique_ptr<Project> project_; // large, so not on the stack
    Session session_;
    SongScreen song_;
    PatternScreen drums_;
    bool panicking_ = false; // all four shoulder buttons are down
    bool panicNote_ = false; // the notice is up
    Uint32 panicNoteUntil_ = 0, now_ = 0;
    View viewBeforeShoulders_ = View::Pattern;
    NoteScreen notes_;
    KitScreen kit_;
    FxScreen fx_;
    PhazedScreen phazed_; // the same screen, once for each synth
    PhazedScreen twin_;
};

} // namespace apotheca
