#pragma once
// The keys a name is typed with: ONE set, wherever a name is typed (gesture audit G01, 2026-11-24).
// A track's name (SET's TRACK NAME, R1 on a SONG head), a new folder in the list of recordings, and a
// synth's or the sampler's own preset (R1 on PRESET). It was four copies that had drifted apart: the
// preset keys left on B with the name thrown away and ran at a cursor's pace.
//
//   the stick, the D-pad   move over forty keys (they come round at the edges); a held D-pad runs at a
//                          list's pace (ui::kListDelayMs, kListEveryMs), the stick kMovesPerSecond
//   A       types the key under the cursor: no space first, never two together, `most` at most
//   Y       deletes the last letter; held, deletes on at a list's pace (gesture audit G48, 2026-11-25)
//   B       keeps what is typed and leaves (owner, 2026-11-15: "I find myself pressing B to back out of
//           the track naming"). With nothing typed it leaves, and nothing changes.
//   START   keeps it too. (With nothing typed, what the screen says: a track gets its instrument's name.)
// Every button is the keys' while they are up (App::typingAName), the tabs, SELECT and L3 too; only the
// four shoulders' panic still acts. What "keep" does is the screen's: press() says which was asked, and
// the screen closes the keys (or leaves them up: a folder name refused).

#include <SDL.h>

#include <string>

#include "input.hpp"
#include "ui.hpp"

namespace apotheca {

struct Fonts; // (ui.hpp)

class NameKeys {
public:
    static constexpr const char* kKeys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_ "; // four rows of ten; the space is the last key
    static constexpr int kColumns = 10, kCount = 40;
    static constexpr float kMovesPerSecond = 7.f; // the stick pushed all the way (times ui::gStickSpeed)

    enum class Asked { Nothing, Keep, Start }; // what a press asks of the screen: B's keep, START's

    // Up, with `name` already typed (cut to `most`), the cursor on the first key.
    void open(const std::string& name, int most);
    void close();
    bool isOpen() const { return open_; }
    Asked press(Button b);  // a button went down
    void letGo(Button b);   // ...and up
    void update(const Input& input, Uint32 nowMs, float dt); // the stick, and a held D-pad (dt: seconds, gStickSpeed in it)

    const std::string& typed() const { return typed_; }
    int keyAt() const { return key_; } // the key the cursor is on, 0..39
    int most() const { return most_; }

    // Drawn over the screen's middle: `title` above the box of what is typed (in `titleColour`), `emptyNote`
    // (or nothing) in the box while nothing is typed, the grid, and the bar: A type, Y delete (with something
    // typed), B `bDoes`, START `startDoes` (null: not said).
    void draw(SDL_Renderer* r, const Fonts& fonts, const std::string& title, SDL_Color titleColour, const char* emptyNote, const char* bDoes, const char* startDoes) const;
    // Where the grid and the box stand, for the tests that read the screen.
    static constexpr int kKeyW = 52, kGridY = 140, kBoxY = 66, kBoxH = 52;

private:
    void move(Button direction);
    void deleteLast();
    bool open_ = false;
    std::string typed_;
    int key_ = 0, most_ = 0;
    float saved_ = 0.f;     // part-moves saved up while the stick is pushed
    bool stickWas_ = false; // the stick was pushed last frame (the first frame's push moves one at once)
    Button repeating_ = Button::None; // the held direction, or Y
    ui::Repeat repeat_; // the held key's repeats (ui.hpp)
};

} // namespace apotheca
