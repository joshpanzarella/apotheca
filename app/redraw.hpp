#pragma once
// Drawing only when the picture can have changed (owner, 2026-11-15: "do the idle redraw if it
// doesn't impact usability in any way. I don't want the user to notice").
//
// The program still goes round 60 times a second: buttons are read and App::update runs on every
// pass, as before. Only the drawing and the present are left out on a pass where nothing on the
// screen can have moved since the last picture. The rule is the picture's, not the battery's: the
// screen must show exactly what drawing every frame would show (tests/idle_test.cpp draws every
// frame of a long session and holds each frame left out to the last one drawn, pixel for pixel).
//
// A pass is drawn when any of these is so (the "stirs"), or was so on the pass before (a thing
// that stops, a message that goes, a line that reaches its end: that last change is drawn too):
//   - a button event or any other event of the window this pass, a button held, the stick pushed;
//   - the song playing;
//   - something the engine shows moved since the last picture: where the song is, the LIVE
//     OUTPUT line, a meter (COMP, DUCK, LIMITER), a CLOUD's grains; a MIDI keyboard's note or one
//     plugged in;
//   - the App's own timed things: the volume, a message ("UNDONE"...), the panic notice, the
//     update's answer;
//   - the Session at work by itself: a recording being fitted to the tempo, a recording being
//     made, a bounce, WiFi on (files, an update, the designer page arrive from another thread);
//   - the help card's line of how hard the program works (a new second's numbers);
//   - the screen up says it changes by itself (Screen::stirring): a line said for a moment, a
//     line running over a recording, the list of recordings about to put on the row rested on,
//     the button test closing by itself.
// And for kKeepDrawingMs after any of them, as a margin for a thing nobody listed. idle_test holds
// the reasons to be enough with the margin at 0, and each one needed (but PLAYING, RECORDING and
// the list's resting row, which others cover wherever the test can show it: kept all the same).
//
// A NEW THING THAT MOVES BY ITSELF (a timer, an animation, anything read from another thread)
// MUST SAY SO, through App::stirring, Session::stirring, a screen's stirring() or the readouts,
// and gets a part in idle_test's session that sets it going and then leaves the buttons alone.
// Forgotten, that part fails (a pass left out shows the old picture); without the part, the test
// cannot see it.

#include <SDL.h>

#include <vector>

namespace apotheca {

class App;
class Input;

// Why a pass is drawn. One bit each, so a test can take one away and see the picture go wrong.
enum Stir : unsigned {
    kStirEvent = 1u << 0,        // a button pressed or let go, or any event of the window
    kStirHeld = 1u << 1,         // a button is held
    kStirStick = 1u << 2,        // the stick is pushed
    kStirPlaying = 1u << 3,      // the song is playing
    kStirPlace = 1u << 4,        // the engine's place in the song (or its stop, its silencing) moved
    kStirScope = 1u << 5,        // the LIVE OUTPUT line's sound moved
    kStirMeters = 1u << 6,       // an effect's meter moved
    kStirMidi = 1u << 7,         // a MIDI keyboard played, or one was plugged in or out
    kStirVolume = 1u << 8,       // the volume showing for a moment
    kStirSaid = 1u << 9,         // the App's line said for a moment (UNDONE, WRITE ON, SCENE...)
    kStirPanicNote = 1u << 10,   // the panic notice, up until a press or four seconds
    kStirUpdateSaid = 1u << 11,  // UPDATE ACCEPTED / COULD NOT, for five seconds
    kStirFitting = 1u << 12,     // a recording being fitted to the tempo
    kStirRecording = 1u << 13,   // a recording being made (waiting for sound, or recording)
    kStirBouncing = 1u << 14,    // a bounce under way
    kStirWifi = 1u << 15,        // the WiFi page is on: files, updates and the designer arrive
    kStirScreenSaid = 1u << 16,  // the screen's own line said for a moment
    kStirPlayhead = 1u << 17,    // a line running across a recording's picture
    kStirPicker = 1u << 18,      // the list of recordings will put the row rested on on
    kStirButtonTest = 1u << 19,  // SETUP's button test closes by itself
    kStirAfter = 1u << 20,       // something above was so on the pass before
    kStirMargin = 1u << 21,      // within kKeepDrawingMs of a stir
    kStirFirst = 1u << 22,       // the first pass, or the picture was asked for again (forget)
    kStirLoad = 1u << 23,
    kStirGrains = 1u << 24,      // CLOUD's grains moved (the track up: Engine::grainPlaces), on its MAIN's picture        // the help card's line of how hard the program works moved (a second at a time)
};
// The name of a stir, for a test's report.
const char* stirName(unsigned bit);

// What the screen reads from the engine and the MIDI thread, which move with no button: kept as of
// the last picture, and compared on every pass.
struct Readouts {
    int place[6] = {}; // play step, bar, repeat, scene (with the moves heard), silenced, the Play that ended
    std::vector<float> scope;  // the LIVE OUTPUT line's samples
    std::vector<float> meters; // every effect meter there is
    int midi[2] = {};          // MIDI notes seen, keyboards plugged in
    int load[3] = {};          // the help card's SOUND, WORST and ALL, as drawn (App::loadShown)
    std::vector<float> grains; // the track up's CLOUD grains, where each reads (Engine::grainPlaces); none for any other
};

class Redraw {
public:
    static constexpr Uint32 kKeepDrawingMs = 2000;
    explicit Redraw(Uint32 keepMs = kKeepDrawingMs) : keepMs_(keepMs) {}

    // Once a pass, after App::update and before drawing: true to draw and present this pass.
    // `stirred`: a button event, or any SDL event but a stick's motion, came this pass.
    bool wanted(const App& app, const Input& input, bool stirred, Uint32 nowMs);
    // The next pass is drawn whatever happens (the controls setup drew over the screen).
    void forget() { first_ = true; }
    // Why the last pass was drawn (0: it was not).
    unsigned why() const { return why_; }
    // The stirs of the last pass, without "after", "margin" and "first".
    unsigned stirs() const { return before_; }
    // For the tests: these stirs count for nothing (one taken away, to see the picture go wrong).
    void ignore(unsigned stirs) { ignored_ = stirs; }

private:
    Uint32 keepMs_;
    unsigned ignored_ = 0;
    unsigned before_ = 0; // the stirs of the pass before
    unsigned why_ = 0;
    bool first_ = true;
    Uint32 stirredAt_ = 0;
    Readouts shown_, now_;
};

} // namespace apotheca
