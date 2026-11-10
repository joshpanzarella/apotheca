#pragma once
// The Pattern screen for a synth track: a piano roll, sixteen steps across
// and the keys up the side.
//
// It is the drum grid's rules again, with one change: sideways, a note has a
// length where a drum hit has a nudge.
//
//   nothing held          the stick (or D-pad) moves the cursor; the keys scroll
//   A                     place a note, one step long
//   hold A, stick up / down      LEVEL   the note fills or drains
//   hold A, stick left / right   LENGTH  the note stretches or shrinks
//   B                     delete the note under the cursor
//   X                     hear the key under the cursor
//   hold R1 and drag      paint a note on every step crossed
//   START                 play / stop (this scene, looping)

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
    // The note under the cursor, or nullptr.
    const NoteEvent* noteHere() const;
    int noteCount() const;

    static constexpr int kLowest = 12, kHighest = 108; // C0 to C8

private:
    enum class Axis { None, UpDown, Sideways };
    static constexpr int kVisibleRows = 16;

    NotePattern& pat() { return project_.current().notes[project_.scene]; }
    const NotePattern& pat() const { return project_.current().notes[project_.scene]; }
    void moveCursor(Button direction);
    void sendNote(int slot);
    bool placeNote(float velocity);
    void setLevel(float level);
    void setLength(int length);
    void paintHere();

    Session& session_;
    Project& project_;
    int pitch_ = 60; // middle C
    int step_ = 0;
    int top_ = 67;   // the highest key shown

    bool shaping_ = false; // A is held
    Axis axis_ = Axis::None;
    bool turningLength_ = false; // for drawing: which value the stick has hold of
    float lengthSaved_ = 0.f;
    bool painting_ = false; // R1 is held
    float brush_ = 0.8f;

    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f;
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
