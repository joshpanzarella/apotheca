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

#include <initializer_list>
#include <memory>
#include <string>

#include "mod_screen.hpp"
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
#include "cpu_use.hpp"

namespace apotheca {

class App {
public:
    explicit App(Engine& engine);

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs); // once per frame
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;
    // What can change the picture with no button, as of the last update(): the Stir bits of
    // redraw.hpp, from the App's own timed things, the Session and the screen up.
    unsigned stirring() const;
    // What the screen reads from the engine and the MIDI thread, as it is now (redraw.hpp).
    void readouts(struct Readouts& out) const;
    // Read the battery from another folder (the stills and the tests have no battery of their own).
    void useBatteryFolder(const std::string& folder) { batteryRoot_ = folder; batteryReadAt_ = 0; }
    void useBatteryLog(const std::string& path) { batteryLog_ = path; batteryLoggedAt_ = 0; } // its history, for a power cut's post-mortem (empty: none kept)
    // Read the program's own time from another file than /proc/self/stat (the tests: a made-up one).
    void useCpuFile(const std::string& path, double ticksPerSecond = 100.0, int cores = 4) { cpu_.usePath(path); cpu_.pretend(ticksPerSecond, cores); }
    // The help card's line: "SOUND 43%  CPU 37%" (WORST is measured, not shown: owner, 2026-11-17), the parts known ("": none yet). SOUND is
    // the sound's share of its own time, WORST its worst call in that second, CPU the whole program's
    // share of the processor (all its threads, over every core).
    static std::string loadWords(const int load[3]);
    // On our own firmware for the RG40XXV quitting turns the handheld off (power_off.hpp), and the
    // help card and the update notice say so. Elsewhere it quits, as it always did.
    void setQuitTurnsOff(bool off) { quitTurnsOff_ = off; }
    bool quitTurnsOff() const { return quitTurnsOff_; }
    static const char* quitWords(bool turnsOff) { return turnsOff ? "turn off" : "quit Apotheca"; }
    static const char* updateNowWords(bool turnsOff) {
        return turnsOff ? "To update now: SEL + START turns off; then turn it on." : "To update now: SEL + START quits; then start it again.";
    }
    static constexpr float kLoadClear = 10.f; // its room on the help card ends this far right of the words to its left
    const int* loadShown() const { return load_; }
    // For the stills and the tests: the line says these numbers, whatever is measured (-1: none).
    void pretendLoad(int sound, int worst, int all) { pretend_[0] = sound, pretend_[1] = worst, pretend_[2] = all, pretending_ = true; }

    Project& project() { return *project_; }
    Session& session() { return session_; }
    SongScreen& song() { return song_; }
    PatternScreen& drums() { return drums_; }
    NoteScreen& notes() { return notes_; }
    SynthScreen& phazed() { return phazed_; }
    SynthScreen& jaxep() { return jaxep_; }
    SynthScreen& fm6() { return fm6_; }
    SynthScreen& physic() { return physic_; }
    SynthScreen& acid() { return acid_; }
    SynthScreen& cloud() { return cloud_; }
    SynthScreen& morph() { return morph_; }
    KitScreen& kit() { return kit_; }
    FxScreen& fx() { return fx_; }
    ModScreen& mods() { return mods_; }
    SamplerScreen& sampler() { return sampler_; }
    SettingsScreen& settings() { return settings_; }
    // Quieter or louder, and show the volume for a moment.
    void changeVolume(float by);
    // A volume key (the handheld's own, which SDL hands over as a keyboard's): the volume, or, with MENU
    // held where the screen is the program's (lights.hpp), the BRIGHTNESS, kBrightKeyStep a press, shown
    // for a moment in the volume's box. MENU used so is no help card until it is let go.
    void volumeKey(bool up, const Input& input);
    void changeBrightness(int by);
    bool brightnessShowing() const { return now_ < volumeShownUntil_ && boxIsBrightness_; }
    bool volumeShowing() const { return now_ < volumeShownUntil_; }
    // MENU held: a card of what the buttons do everywhere. It goes when MENU is let go.
    // (With no MENU button on the device: SELECT, held by itself for a moment; another button pressed
    // with it is a combo, and the card stays away until SELECT is let go.)
    bool helpShowing() const { return help_; }
    // SELECT + B takes back the last change (the patterns, and since 2026-11-14 the sounds and effects:
    // Session::soundMark); SELECT + A puts it forward again.
    // What it said about that, for a moment: "UNDONE", "NOTHING TO UNDO"...
    const char* undoSaid() const { return now_ < undoSaidUntil_ ? undoSaid_ : ""; }
    // The notice that says a panic happened. It stays while the buttons are
    // held, and after that until any button is pressed (that press does
    // nothing else) or four seconds have gone by.
    bool panicNoteShowing() const { return panicNote_; }
    int updateSaid() const { return updateSaid_; } // 0 nothing, 1 "accepted", 2 "could not"
    // The update notice's line of what to do, and what it says while B is held (it cannot be undone).
    static constexpr const char* kUpdateWords = "HOLD A for a second to accept.    HOLD B to delete it.";
    static constexpr const char* kUpdateDeletingWords = "DELETING IT: CANNOT BE UNDONE";
    bool updateDeleting() const { return updateDeleting_; } // B is held on the update notice, its bar filling
    void sayUpdateAcceptedForStill() { updateSaid_ = 1, updateSaidUntil_ = ~0u; } // (--view update-accepted)

private:
    Screen& screenNow();
    // A name is being typed (name_keys.hpp) on the screen up: a new folder in the list of recordings, or a
    // synth's or the sampler's preset. Every button is the keys' then, the tabs, SELECT and L3 too; only the
    // four shoulders' panic still acts. (SET's keys, a track's name or a password, are the SET page's own
    // case in onButton: the button test is there too.)
    bool typingAName() const {
        const Screen* now = &screenNow();
        if (now == &kit_) return kit_.picker().typing();
        if (now == &sampler_) return sampler_.typing() || sampler_.picker().typing();
        for (const SynthScreen* synth : {&phazed_, &jaxep_, &fm6_, &physic_, &acid_, &cloud_, &morph_})
            if (now == synth) return synth->typing() || synth->picker().typing();
        return false;
    }
    bool browsingRecordings() const { // the list of recordings is up on the screen shown (Session::browsingRecordings)
        const Screen* now = &screenNow();
        return (now == &kit_ && kit_.picker().isOpen()) || (now == &sampler_ && sampler_.picker().isOpen()) || (now == &cloud_ && cloud_.picker().isOpen());
    }
    const Screen& screenNow() const { return const_cast<App*>(this)->screenNow(); }
    std::unique_ptr<Project> project_; // large, so not on the stack
    Session session_;
    SongScreen song_;
    PatternScreen drums_;
    bool panicking_ = false; // all four shoulder buttons are down
    Uint32 volumeShownUntil_ = 0;
    bool boxIsBrightness_ = false; // the volume's box says the BRIGHTNESS
    bool menuUsed_ = false;        // MENU held for a volume key: no help card until it is let go
    bool updateHeld_ = false;   // A is down on the update notice
    bool updateDeleting_ = false; // ...or B, deleting it
    Uint32 updateSince_ = 0, updateSaidUntil_ = 0, deleteSince_ = 0;
    int updateSaid_ = 0;        // 1: accepted, 2: could not; shown for a few seconds
    bool panicNote_ = false; // the notice is up
    Uint32 panicNoteUntil_ = 0, now_ = 0;
    View viewBeforeShoulders_ = View::Pattern;
    View viewBeforeL3_ = View::Song; // where L3 came from, to go back to
    bool cameByL3_ = false;          // the SONG screen's MASTER was reached by L3 (and nothing has moved since)
    bool slicingBeforeShoulders_ = false;
    int trackBeforeShoulders_ = 0;
    int sceneBeforeShoulders_ = 0, barBeforeShoulders_ = 0; // ...or R2 off SONG takes the cursor's cell (G46)
    int setRowBeforeShoulders_ = 0;                         // ...or, on SET, L1 / R1 jump a heading (G28)
    SongScreen::Visit songBeforeShoulders_{};               // ...or L2 / R2 open SONG, its cursor on the cell worked on (G46)
    // ...and the copies when R1 last went down: a copy or a clear its press made on the way to all four (the bar
    // on the grids, a cell or a scene on SONG) a panic takes back (Session::takeBackBarCopy and the others).
    unsigned barCopiesAtR1_ = 0, sceneCopiesAtR1_ = 0, cellCopiesAtR1_ = 0;
    Session::Rec recBeforeShoulders_ = Session::Rec::Idle; // ...and a recording: R1 first on a lane or the sampler begins one
    bool typingBeforeShoulders_ = false; // ...and the name keys: R1 first on a synth's or the sampler's PRESET opens them
    void closePresetKeys(); // the name keys of every synth's and the sampler's PRESET, wherever they were opened
    unsigned saidSeen_ = 0; // Session::saidCount() when its line was last taken
    void takeSessionSaid(); // what the Session asked to be said (a bar copied or pasted) into this box
    int sceneOfSelect() const; // the scene SELECT + Y and SELECT + L2 / R2 act on (on SONG: the cursor's)
    // To another tab by the player (L2 / R2, SELECT's tap): SONG opens with its cursor on the cell being
    // worked on, and a tab off SONG takes the cell its cursor was moved to on this visit (G46).
    void showTab(View to);
    void logBattery(Uint32 nowMs); // a line of it, when one is due (useBatteryLog)
    NoteScreen notes_;
    KitScreen kit_;
    FxScreen fx_;
    ModScreen mods_;
    SamplerScreen sampler_;
    SettingsScreen settings_;
    bool quitTurnsOff_ = false; // setQuitTurnsOff
    std::string batteryRoot_;  // where to read it from, if not the usual place (for stills and tests)
    int battery_ = -1;         // percent, or -1 if the device has none to read
    bool charging_ = false;
    Uint32 batteryReadAt_ = 0;
    std::string batteryLog_; // the history's file (empty: none kept)
    Uint32 batteryLoggedAt_ = 0;
    int batteryLoggedPercent_ = -1;
    bool batteryLoggedCharging_ = false;
    char undoSaid_[64] = "";
    int resyncsSeen_ = 0;
    Uint32 undoSaidUntil_ = 0;
    // A chord's length said by L2 / R2 with Y held on the roll (G27): the box sits over the middle of the pack's
    // tiles, so it goes as soon as the pick moves on (another chord, inversion or pack). When that box ends
    // (0: none up), and the pick it was said on.
    Uint32 lengthSaidUntil_ = 0;
    int lengthSaidPick_[3] = {};
    void dropLengthSaid(); // the box above, gone once the pick moves on
    bool help_ = false;
    // The help card's line of how hard the program works (cpu_use.hpp, Engine::soundShare): read
    // while it is open, kept here so a pass draws what the redraw compared (Readouts::load).
    ProgramCpu cpu_;
    int load_[3] = {-1, -1, -1}; // SOUND, WORST, CPU in percent; -1: not known (or the card is shut)
    bool helpWas_ = false;
    int pretend_[3] = {-1, -1, -1};
    bool pretending_ = false;
    Uint32 selectHeldSince_ = 0; // 0: not held
    bool selectAlone_ = false; // SELECT is down and nothing else has been pressed since
    bool selectUsed_ = false;  // ...and on every screen: something else was pressed while SELECT is down (no help card)
    bool nextSceneY_ = false;  // Y is down from SELECT + Y (next scene): letting it go is not a tap of Y
    SynthScreen phazed_; // the same screen, once for each synth
    SynthScreen jaxep_;
    SynthScreen fm6_;
    SynthScreen physic_;
    SynthScreen acid_;
    SynthScreen cloud_;
    SynthScreen morph_;
};

} // namespace apotheca
