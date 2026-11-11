#pragma once
// The whole program behind the window: the project, the three screens, and
// moving between them. No SDL window or sound card is needed to drive it, so
// the tests press its buttons directly.
//
//   L2 / R2    previous / next screen: SONG, PATTERN, DEVICE, FX, SETUP
//   SELECT     tapped by itself, from anywhere: the SONG screen (and on the
//              SONG screen, the list of songs)
//              (with A held on a grid: nudge the note or hit early / late)
//   SELECT + D-pad   the tempo: up / down by 1, left / right by 10
//   SELECT + L1 / R1 the volume: quieter / louder
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
#include "sampler_screen.hpp"
#include "phazed_screen.hpp"
#include "session.hpp"
#include "settings_screen.hpp"
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
    SamplerScreen& sampler() { return sampler_; }
    SettingsScreen& settings() { return settings_; }
    // Quieter or louder, and show the volume for a moment.
    void changeVolume(float by);
    bool volumeShowing() const { return now_ < volumeShownUntil_; }
    // SELECT held for a moment: a card of what the buttons do everywhere. It goes when SELECT is let go.
    bool helpShowing() const { return help_; }
    // The notice that says a panic happened. It stays while the buttons are
    // held, and after that until any button is pressed (that press does
    // nothing else) or four seconds have gone by.
    bool panicNoteShowing() const { return panicNote_; }
    int updateSaid() const { return updateSaid_; } // 0 nothing, 1 "accepted", 2 "could not"

private:
    std::unique_ptr<Project> project_; // large, so not on the stack
    Session session_;
    SongScreen song_;
    PatternScreen drums_;
    bool panicking_ = false; // all four shoulder buttons are down
    Uint32 volumeShownUntil_ = 0;
    bool updateHeld_ = false;   // A is down on the update notice
    Uint32 updateSince_ = 0, updateSaidUntil_ = 0;
    int updateSaid_ = 0;        // 1: accepted, 2: could not; shown for a few seconds
    bool panicNote_ = false; // the notice is up
    Uint32 panicNoteUntil_ = 0, now_ = 0;
    View viewBeforeShoulders_ = View::Pattern;
    NoteScreen notes_;
    KitScreen kit_;
    FxScreen fx_;
    SamplerScreen sampler_;
    SettingsScreen settings_;
    bool help_ = false;
    Uint32 selectHeldSince_ = 0; // 0: not held
    bool selectAlone_ = false; // SELECT is down and nothing else has been pressed since
    PhazedScreen phazed_; // the same screen, once for each synth
    PhazedScreen twin_;
};

} // namespace apotheca
