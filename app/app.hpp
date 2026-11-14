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
#include <string>

#include "fx_screen.hpp"
#include "input.hpp"
#include "kit_screen.hpp"
#include "note_screen.hpp"
#include "pattern_screen.hpp"
#include "sampler_screen.hpp"
#include "synth_screen.hpp"
#include "session.hpp"
#include "settings_screen.hpp"
#include "song_screen.hpp"
#include "screen.hpp"
#include "ui.hpp"

namespace apotheca {

class App {
public:
    explicit App(Engine& engine);

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs); // once per frame
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;
    // Read the battery from another folder (the stills and the tests have no battery of their own).
    void useBatteryFolder(const std::string& folder) { batteryRoot_ = folder; batteryReadAt_ = 0; }

    Project& project() { return *project_; }
    Session& session() { return session_; }
    SongScreen& song() { return song_; }
    PatternScreen& drums() { return drums_; }
    NoteScreen& notes() { return notes_; }
    SynthScreen& phazed() { return phazed_; }
    SynthScreen& twin() { return twin_; }
    SynthScreen& qmod() { return qmod_; }
    KitScreen& kit() { return kit_; }
    FxScreen& fx() { return fx_; }
    SamplerScreen& sampler() { return sampler_; }
    SettingsScreen& settings() { return settings_; }
    // Quieter or louder, and show the volume for a moment.
    void changeVolume(float by);
    bool volumeShowing() const { return now_ < volumeShownUntil_; }
    // MENU held: a card of what the buttons do everywhere. It goes when MENU is let go.
    // (With no MENU button on the device: SELECT, held for a moment.)
    bool helpShowing() const { return help_; }
    // SELECT + B takes back the last change to the patterns; SELECT + A puts it forward again.
    // What it said about that, for a moment: "UNDONE", "NOTHING TO UNDO"...
    const char* undoSaid() const { return now_ < undoSaidUntil_ ? undoSaid_ : ""; }
    // The notice that says a panic happened. It stays while the buttons are
    // held, and after that until any button is pressed (that press does
    // nothing else) or four seconds have gone by.
    bool panicNoteShowing() const { return panicNote_; }
    int updateSaid() const { return updateSaid_; } // 0 nothing, 1 "accepted", 2 "could not"
    void sayUpdateAcceptedForStill() { updateSaid_ = 1, updateSaidUntil_ = ~0u; } // (--view update-accepted)

private:
    Screen& screenNow();
    const Screen& screenNow() const { return const_cast<App*>(this)->screenNow(); }
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
    bool slicingBeforeShoulders_ = false;
    NoteScreen notes_;
    KitScreen kit_;
    FxScreen fx_;
    SamplerScreen sampler_;
    SettingsScreen settings_;
    std::string batteryRoot_;  // where to read it from, if not the usual place (for stills and tests)
    int battery_ = -1;         // percent, or -1 if the device has none to read
    bool charging_ = false;
    Uint32 batteryReadAt_ = 0;
    char undoSaid_[64] = "";
    int resyncsSeen_ = 0;
    Uint32 undoSaidUntil_ = 0;
    bool help_ = false;
    Uint32 selectHeldSince_ = 0; // 0: not held
    bool selectAlone_ = false; // SELECT is down and nothing else has been pressed since
    bool nextSceneY_ = false;  // Y is down from SELECT + Y (next scene): letting it go is not a tap of Y
    SynthScreen phazed_; // the same screen, once for each synth
    SynthScreen twin_;
    SynthScreen qmod_;
};

} // namespace apotheca
