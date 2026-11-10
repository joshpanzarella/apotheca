#pragma once
// The Pattern screen for a synth track: a piano roll on its side. The keys
// run across the top, laid out as a keyboard is, three octaves at a time;
// the bar runs down the screen, step 1 at the top.
//
// Holding A shapes a note the way it is drawn: it is as long as it is tall,
// and filled across as far as it is loud.
//
//   nothing held          the stick (or D-pad) moves the cursor: left / right
//                         along the keys (they scroll), up / down through the bar
//   A                     place a note, one step long
//   hold A, stick left / right   LEVEL   the note fills or drains
//   hold A, stick up / down      LENGTH  the note grows or shrinks
//   B                     delete the note under the cursor
//   X                     hear the key under the cursor
//   hold R1 and drag      paint a note on every step crossed
//   START                 play / stop (this scene, looping)
//
// Chords come from the song's chord pack, and Y is the chord button:
//
//   hold Y                the pack's chords appear along the bottom, and the
//                         one in hand sounds
//     left / right        the next chord (stick or D-pad, either)
//     up / down           inversion: the bottom note goes up an octave, or
//                         the top one comes down
//     L1 / R1             another pack
//     X                   hear it again
//   let go of Y           the chord lands at the cursor, and the cursor moves
//                         on past it. So a tap of Y places the chord in hand
//                         again; hold, flick, let go places a different one.
//     B first             ...or nothing lands
//
// A chord stays a chord. With the cursor on any of its notes, hold A and the
// stick sets the level and length of the whole chord, and B removes it. With
// the cursor anywhere in its steps, hold Y to swap it for another chord in
// place; B while Y is held breaks it up into loose notes instead.

#include <SDL.h>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class NoteScreen {
public:
    explicit NoteScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs);
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    // For tests and stills.
    void moveCursorTo(int pitch, int step);
    int cursorPitch() const { return pitch_; }
    int cursorStep() const { return step_; }
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

    static constexpr int kVisibleKeys = 36;

private:
    enum class Axis { None, UpDown, Sideways };

    NotePattern& pat() { return project_.current().notes[project_.scene]; }
    const NotePattern& pat() const { return project_.current().notes[project_.scene]; }
    void moveCursor(Button direction);
    void press(Button direction); // a direction, whatever it means just now
    void resend();
    int chordSlotHere() const;
    float levelNow() const;
    int lengthNow() const;
    const ChordPack& pack() const { return session_.packs[static_cast<std::size_t>(session_.packIndex())]; }
    ChordEvent inHand() const; // the chord Y would place here
    void hearChord(const ChordEvent& chord);
    void beginPick();
    void pickChanged(); // a new chord, inversion or pack: hear it, and swap it in if one is being changed
    void endPick();
    void breakUp(int chord);
    void showChord(const ChordEvent& chord);
    bool placeNote(float velocity);
    void setLevel(float level);
    void setLength(int length);
    void paintHere();

    Session& session_;
    Project& project_;
    int pitch_ = 60; // middle C
    int step_ = 0;
    int low_ = 48;   // the lowest key shown: C3, so middle C sits in the middle

    bool shaping_ = false; // A is held
    Axis axis_ = Axis::None;
    bool turningLength_ = false; // for drawing: which value the stick has hold of
    float lengthSaved_ = 0.f;
    bool picking_ = false;  // Y is held: choosing a chord
    int editing_ = -1;      // ...to replace this chord; -1 to place a new one
    int hand_ = 0;          // the chord in hand: its place in the pack
    int inversion_ = 0;
    int handLength_ = 4;    // new chords are as long as the last one set
    bool painting_ = false; // R1 is held
    float brush_ = 0.8f;

    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f;
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
