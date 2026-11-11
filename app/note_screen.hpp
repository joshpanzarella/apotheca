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
//   X                     hear the key under the cursor
//   hold R1 and drag      paint a note on every step crossed
//   L1                    zoom: a tap goes closer (2 octaves, 1, round to 3);
//                         held, up is closer and down is further
//   START                 play / stop (this scene, looping)
//
// A cell can be 1, 2, 4 or 8 bars long (set on the Song screen). The roll
// shows one bar; running the cursor off the bottom goes on to the next bar,
// and the top bar says which (BAR 2 OF 4). A note may run on over the bar
// lines to the end of the cell; in the bars after, it shows faint from the top.
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

    int visibleKeys() const; // 12, 24 or 36: the zoom (Session::octavesShown)
    void zoomTo(int octaves);

private:
    enum class Axis { None, UpDown, Sideways };

    NotePattern& pat() { return project_.current().notes[project_.scene][project_.bar]; }
    const NotePattern& pat() const { return project_.current().notes[project_.scene][project_.bar]; }
    int cellBars() const { return project_.current().bars[project_.scene]; }
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
    void breakUp(int chord);
    void showChord(const ChordEvent& chord);
    bool placeNote(float velocity);
    void setLevel(float level);
    void setLength(float length);
    void stepLength(int by);
    void nudgeBy(float steps);
    void paintHere();

    Session& session_;
    Project& project_;
    int pitch_ = 60; // middle C
    int step_ = 0;
    int keyW() const;
    int cellW() const;
    void keepInView();
    bool zoomPushed_ = false;
    bool zooming_ = false, zoomUsed_ = false; // L1 is held; it did something with up / down
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
    bool painting_ = false; // R1 is held
    float brush_ = 0.8f;

    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f;
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
