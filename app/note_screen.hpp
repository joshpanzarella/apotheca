#pragma once
// The Pattern screen for a synth track: a piano roll on its side. The keys
// run across the top, laid out as a keyboard is, two octaves at a time (L1
// zooms: one, two or three);
// the bar runs down the screen, step 1 at the top.
//
// Holding A shapes a note the way it is drawn: it is as long as it is tall,
// and filled across as far as it is loud.
//
//   nothing held          the stick (or D-pad) moves the cursor: left / right
//                         along the keys (they scroll), up / down through the bar
//   A                     place a note, one step long
//   hold A, stick left / right   LEVEL   the note fills or drains
//   hold A, stick up / down      LENGTH  freely: the note's end follows the
//                                        stick to any point in a step
//   hold A, D-pad up / down      LENGTH  by whole steps: the end goes to the
//                                        next step line, or the one before
//   hold A, L2 / R2              NUDGE   the note a tenth of a step early
//                                        (L2) or late (R2), up to half a step
//   B                     delete the note under the cursor
//   hold B and drag       delete every note the cursor crosses
//   X                     hear the key, note or chord under the cursor, for as long as X
//                         is held (a tap for a moment), over the song too
//   hold X on a note      what it does besides play, a click at a time:
//                           left / right   RATCHET: the note 2, 3 or 4 times inside its first step
//                           up / down      GLIDE longer / shorter, down to OFF: it slides to its pitch from the note before it
//                           L1 / R1        GLIDE FROM, the track's: which key of a chord before its glides
//                                          come from (TOP, PAIRED, NEAREST, RANDOM). It stops at each end.
//                         A chord has six effects (chord_effects.hpp): up / down pick one,
//                         left / right set it. The strip lists them.
//                           B              empty what X shows: a note's RATCHET, GLIDE and THROW, a
//                                          chord's six and its THROW. The note or chord stays. One step of undo.
//   hold R1 on a note     carry the note (or the whole chord) under the cursor.
//                         The D-pad: to another key, or a whole step. The
//                         stick up / down: it slides freely in time, off
//                         the grid; sideways, key by key as the D-pad goes
//                         (a SCALE's keys; with L1 held an octave a push).
//                         It stops at the ends of the bar and
//                         will not land on another note.
//   hold R1 on an empty step, and drag    paint a note on every step crossed
//   L1                    zoom: a tap goes closer (2 octaves, 1, round to 3)
//   TEMPO (up from step 1 of bar 1: the top bar's tempo, as on the drum grid)
//                         A taps the tempo (TAP TEMPO). Hold A, up / down: a beat a
//                         minute; left / right: ten; the stick up / down: freely.
//                         No step of undo (a tempo change is not an edit). LEFT: BARS.
//   BARS (left of TEMPO)  hold A, left / right: the bar before or after, round the ends.
//                         Hold A, up / down: the cell's count, more or fewer
//                         (1, 2, 4 or 8, as the Song screen's hold A). One step
//                         of undo. RIGHT is TEMPO (owner, 2026-11-28: they were one
//                         above the other). Down from either is the grid; up comes
//                         round to the bottom.
//   hold L1, R1           copy the bar on screen: its notes and chords
//   hold L1, A            paste it over the bar on screen (another bar, cell, scene,
//                         or synth or sampler track). One step of undo; on the same
//                         track its automation goes with it
//   START                 play / stop (this scene, looping)
//
// A cell can be 1, 2, 4 or 8 bars long (the Song screen's hold A, or the BARS stop
// here). The roll shows one bar; running the cursor off the bottom goes on
// to the next bar, and the top bar says which (BAR 2 OF 4). A note may run on
// over the bar lines to the end of the cell; in the bars after, it shows
// faint from the top.
//
// Chords come from the song's chord pack, and Y is the chord button:
//
//   hold Y                the pack's chords appear along the bottom, and the
//                         one in hand sounds
//     left / right        the next chord (stick or D-pad, either)
//     up / down           inversion: the bottom note goes up an octave, or
//                         the top one comes down
//     L1 / R1             another pack
//     L2 / R2             half or twice as long: 1, 2, 4, 8 or 16 steps
//     X                   hear it again
//   let go of Y           the chord lands at the cursor, and the cursor moves
//                         on past it. So a tap of Y places the chord in hand
//                         again; hold, flick, let go places a different one.
//     B first             ...or nothing lands (on a chord swapped: it is put back as it was, and
//                         the pack too)
//
// A chord stays a chord. With the cursor on any of its notes, hold A and the
// stick sets the level and length of the whole chord, and B removes it. With
// the cursor anywhere in its steps, hold Y to swap it for another chord in
// place; A while Y is held breaks it up into loose notes instead (G26: it was B).

#include "chord_effects.hpp"
#include "follow_bar.hpp"
#include "screen.hpp"
#include <SDL.h>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class NoteScreen : public Screen {
public:
    explicit NoteScreen(Session& session) : session_(session), project_(session.project), tempoExact_(session.project.tempo) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;
    // A note hidden on the screen by a chord, or a longer note, on the same key at the same time. (It is drawn inside the other.)
    bool isUnderAnother(const NoteEvent& note) const;
    bool effecting() const { return fxHeld_; }
    int effectPicked() const { return fxPick_; } // with X held on a chord: which of its effects left / right set
    void pickEffect(int effect) { fxPick_ = effect < 0 ? 0 : (effect >= kChordEffects ? kChordEffects - 1 : effect); } // (for a still) // X is held on a note or a chord: the stick sets what it does

    // For tests and stills.
    void moveCursorTo(int pitch, int step);
    int cursorPitch() const { return pitch_; }
    int cursorStep() const { return step_; }
    bool cursorOnBars() const { return onBars_; }
    bool cursorOnTempo() const { return onTempo_; }
    bool carrying() const { return carrying_ != Carry::Nothing; }
    int lowestKeyShown() const { return low_; }
    // The note under the cursor, or nullptr.
    const NoteEvent* noteHere() const;
    int noteCount() const;
    // The chord with a note under the cursor (and no loose note on top), or nullptr.
    const ChordEvent* chordHere() const;
    int chordCount() const;
    bool picking() const { return picking_; }
    int handIndex() const { return hand_; }
    int handInversion() const { return inversion_; }

    static constexpr int kLowest = 12, kHighest = 108; // C0 to C8

    // COPY A BAR: hold L1, then R1 copies the bar shown (its notes and chords) when let go
    // unmoved; moved, it carried instead (Session::copyBar). A pastes over it. A copy or a paste
    // is a use of L1: its tap's zoom does not happen too.
    bool barKeyHeld() const { return l1First_; }
    void shouldersPressed() { zoomUsed_ = true; copyBarArmed_ = false; } // (the App: another shoulder went down with L1 held)

    int visibleKeys() const; // 12, 24 or 36: the zoom (Session::octavesShown)
    // SCALE LOCK (scales.hpp): the pack's chord `index`, moved into the song's KEY when a SCALE is on (packShift).
    ChordShape shapeAt(int index) const;
    // A key the cursor stops on: every key with no SCALE; with one, the scale's keys and any key a note or chord of the bar shown is on.
    bool keyStop(int pitch) const;
    int nextKey(int from, int way) const; // the next stop that way from `from`, or `from` at the keyboard's end
    void zoomTo(int octaves);
    void stepBars(int by); // the cell's bars: 1, 2, 4 or 8, one step along that row
    float handLength() const { return handLength_; } // (for tests) the length a new chord takes

private:
    enum class Axis { None, UpDown, Sideways };

    NotePattern& pat() { return project_.current().notes[project_.scene][project_.bar]; }
    const NotePattern& pat() const { return project_.current().notes[project_.scene][project_.bar]; }
    int cellBars() const { return project_.current().bars[project_.scene]; }
    // The bar shown is the cell's FILL; the bar of the scene it is in (the FILL: the scene's last, where it plays:
    // the step numbers and the shading go by it); how many bars from it to the cell's end (a note may run on over
    // the bar lines to there; the FILL is a bar of its own: one).
    bool onFill() const { return project_.current().onFill(project_.scene, project_.bar); }
    int sceneBar() const { return project_.sceneBarOf(project_.track, project_.scene, project_.bar); }
    int barsToEnd() const { return onFill() ? 1 : cellBars() - project_.bar; }
    void moveCursor(Button direction);
    void press(Button direction); // a direction, whatever it means just now
    void resend();
    int chordSlotHere() const;
    float levelNow() const;
    float lengthNow() const;
    const ChordPack& pack() const { return session_.packs[static_cast<std::size_t>(session_.packIndex())]; }
    ChordEvent inHand() const; // the chord Y would place here
    void hearChord(const ChordEvent& chord);
    void beginPick();
    void pickChanged(); // a new chord, inversion or pack: hear it, and swap it in if one is being changed
    void endPick();
    void doubleHand(int way); // Y held, L2 (-1) / R2 (+1): the chord in hand half or twice as long
    void breakUp(int chord);
    void putPickBack(); // B while Y is held on a chord: the chord, the pack and the hand as Y found them
    void showChord(const ChordEvent& chord);
    bool placeNote(float velocity);
    void setLevel(float level);
    void setLength(float length);
    void stepLength(int by);
    void nudgeBy(float steps);
    void paintHere();

    // onButton(), in its parts (note_screen.cpp).
    bool effectsButton(const ButtonEvent& event); // X held on a note or chord: true when the press was its
    void pickButton(const ButtonEvent& event);    // Y held: the chord's own buttons
    void l1Button(const ButtonEvent& event);
    bool barKeyButton(const ButtonEvent& event, const Input& input); // hold L1 + a direction: true when it was
    void r1Button(const ButtonEvent& event, const Input& input);
    void holdX(const Input& input); // X pressed: heard, and the hold for what a note does
    // draw(), in its parts (note_screen.cpp).
    int keyX(int pitch) const;   // a key's column on the roll
    int stepY(int step) const;   // a step's row
    bool keyShows(int pitch) const;
    void drawKeyboard(SDL_Renderer* r, const Fonts& fonts, SDL_Color cursorColor) const;
    void drawGrid(SDL_Renderer* r, const Fonts& fonts) const;
    void drawBlock(SDL_Renderer* r, int pitch, int step, float length, float level, SDL_Color edge, SDL_Color inside, float nudge = 0.f) const;
    bool chordNameBox(const Fonts& fonts, const ChordEvent& c, SDL_Rect* box) const;
    void drawChord(SDL_Renderer* r, const Fonts& fonts, const ChordEvent& c, bool live, bool ghost, bool nameOnly = false) const;
    void drawEarlierNotes(SDL_Renderer* r) const;
    void drawNotesAndChords(SDL_Renderer* r, const Fonts& fonts, bool held, const NoteEvent* under, const ChordEvent* chordUnder) const;
    void drawMarks(SDL_Renderer* r, int pitch, int step, float nudge, float length, int ratchet, bool glide, int throwTo, int inset = 0, bool chord = false,
                   bool plays = false) const;
    void drawChordMarks(SDL_Renderer* r, const Fonts& fonts, const ChordEvent* chordUnder) const;
    void drawNotesInside(SDL_Renderer* r, bool held, const NoteEvent* under) const;
    void drawGlideLines(SDL_Renderer* r) const;
    void drawStrip(SDL_Renderer* r, const Fonts& fonts, const NoteEvent* under, const ChordEvent* chordUnder, SDL_Color cursorColor) const;
    void drawPackStrip(SDL_Renderer* r, const Fonts& tight, int stripY) const;
    void drawNoteStrip(SDL_Renderer* r, const Fonts& tight, int stripY, const NoteEvent* under, const ChordEvent* chordUnder, SDL_Color cursorColor) const;
    void drawStripValues(SDL_Renderer* r, const Fonts& tight, int stripY, const NoteEvent* under, const ChordEvent* chordUnder, bool chordFx) const;
    void drawHints(SDL_Renderer* r, const Fonts& fonts, const NoteEvent* under, const ChordEvent* chordUnder, bool held) const;

    Session& session_;
    Project& project_;
    int pitch_ = 60; // middle C
    int step_ = 0;
    int keyW() const;
    int keysH() const;
    int blackKeyH() const;
    int gridTop() const;
    int cellW() const;
    void keepInView();
    void followTheNotes(); // FOLLOW showed a bar: the keys to its notes (follow_bar.hpp, keysToShow)
    bool barsPushed_ = false; // the stick is pushed for a bar more or fewer, on the BARS stop (A held): once a push
    bool onBars_ = false;     // BARS, left of the tempo (one row above the grid): the bar shown, and the cell's count
    bool onTempo_ = false;    // TEMPO, above the grid (the top bar's tempo), as on the drum grid (owner, 2026-11-28)
    float tempoExact_ = 120.f; // the stick moves this; the project's tempo is it, rounded (as the drum grid's)
    bool offGrid() const { return onBars_ || onTempo_; } // the cursor is on BARS or TEMPO: nothing of the grid's
    void setTempo(float bpm);
    bool zooming_ = false, zoomUsed_ = false; // L1 is held; it did something (copied, pasted, moved)
    bool l1First_ = false; // L1 is held and went down with nothing else held: R1 let go unmoved copies the bar, A pastes it
    bool copyBarArmed_ = false; // ...R1 went down after it: let go where it went down, the bar copies
    int copyBarPitch_ = 0, copyBarStep_ = 0, copyBarBar_ = 0;
    int shownKeys_ = 24;
    int low_ = 48;   // the lowest key shown: C3, so middle C sits in the middle

    bool shaping_ = false; // A is held
    Axis axis_ = Axis::None;
    bool turningLength_ = false; // for drawing: which value the stick has hold of
    bool picking_ = false;  // Y is held: choosing a chord
    int editing_ = -1;      // ...to replace this chord; -1 to place a new one
    int hand_ = 0;          // the chord in hand: its place in the pack
    int inversion_ = 0;
    float handLength_ = 4.f;    // new chords are as long as the last one set
    struct PickWas {            // what Y found when it went down (B puts it back)
        ChordEvent chord;       // the chord being swapped, as it was
        int packIndex = 0, hand = 0, inversion = 0;
        float handLength = 4.f;
    } pickWas_;
    bool painting_ = false; // R1 is held
    // R1 is held on a note or a chord: the D-pad and the stick carry it, anywhere in the cell, in pitch and
    // in time, its shape and length kept (owner, 2026-11-28: "hold R1 and move it to wherever I want").
    enum class Carry { Nothing, Note, Chord };
    Carry carrying_ = Carry::Nothing;
    int carried_ = 0;       // its slot, in the bar carriedBar_ (not always the bar shown: see carryCursor)
    int carriedBar_ = 0;
    bool carryOnFill_ = false; // taken on the FILL: it stays on the FILL (a bar by itself)
    int carryGrab_ = 0;     // how many steps into it the cursor took hold of it
    struct CarryWas {       // what R1 found when it went down (B puts it back)
        NoteEvent note;
        ChordEvent chord;
        int slot = 0, bar = 0, pitch = 0, step = 0;
    } carryWas_;
    void fxRoll();                      // with X held, Y: the dice set the ratchet and the glide
    void fxThrow();                     // with X held, A: the note's or the chord's THROW a step round
    void fxFrom(int by);                // with X held on a note, L1 (-1) / R1 (+1): the track's GLIDE FROM, a click
    void fxEmpty();                     // with X held, B: what X shows emptied, the note or chord kept
    std::uint32_t dice_ = 0x9E3779B9u;
    void fxTurn(Button direction);      // with X held: one click of ratchet (left / right) or glide (up / down);
                                        // on a chord, up / down pick one of its six effects and left / right set it
    int fxPick_ = 0;                    // the chord effect picked (kFxRatchet and on, chord_effects.hpp); it is kept
                                        // from one hold to the next, so the same thing is quick to set on chord after chord
    bool fxHeld_ = false;
    float fxSaved_ = 0.f;
    bool fxWas_ = false;
    bool fxWait_ = false; // the stick was already over when X went down: it does nothing until it has been let go
    bool fxLastFrame_ = false; // X was held on a note or chord last frame (its note can go from under it)
    bool stickWait_ = false;   // X let go with the stick still pushed: the stick does nothing until it comes back (as FX and DEVICE)
    enum class CarryTry { Moved, Taken, Edge };
    void carry(Button direction);           // the D-pad and the stick: a whole step, or a key (over what is in the way)
    CarryTry carryBy(int keys, int steps);  // exactly that far, or why not (Taken: try further; Edge: stop)
    int carryCellSteps() const;             // the steps it may be carried over: the cell's bars, or the FILL's one
    int carryAt(int bar, int step) const { return carryOnFill_ ? step : bar * kSteps + step; } // a place in them
    bool carrySpanFree(int pitch, int from, float length) const; // nothing else of its kind there (-1 pitch: a chord)
    void carryCursor(int pitch);            // the cursor on it again, in the bar its start is in
    void putCarryBack();                    // B with R1 held: as R1 found it
    bool erasing_ = false;  // B is held: the cursor deletes what it crosses
    void eraseHere();
    float brush_ = kPlacedLevel;

    Button repeating_ = Button::None;
    ui::Repeat repeat_; // the held direction's repeats (ui.hpp)
    Uint32 lastMs_ = 0;
    BarFollow follow_; // the view follows the bar playing (follow_bar.hpp)
    float stickX_ = 0.f, stickY_ = 0.f;
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
