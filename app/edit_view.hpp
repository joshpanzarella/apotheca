#pragma once
// The EDIT view (owner, 2026-11-16: "rudimentary sample editing"; lane_edit.hpp): a sound drawn large,
// the part played lit between START and END, and eight boxes in two rows of four, START, END, FADE IN,
// FADE OUT / GAIN, PITCH, FINE, REVERSE. The file on the card is never written. One view for every
// sound that is edited (S4, 2026-11-25: it was the KIT page's own): a drum lane (KitScreen) and the
// SAMPLER's one recording (SamplerScreen, without PITCH and FINE, which its page has as PITCH and TUNE:
// their boxes are empty and the cursor passes them by, as the VOICE view's unused places).
//   the stick or D-pad moves among the boxes
//   hold A + up / down: turns the box (the stick through its range in about a second, however far it is
//           pushed; the D-pad a millisecond of the file, half a dB, a fade's stop, a semitone, a cent)
//   B       the box back to its usual value; on a box already there, or straight after Y, back (G16).
//           HELD: every edit back to none, as the bar fills (undo brings them back)
//   Y       on START or END: TRIM SILENCE (START and END at the first and last sound). Elsewhere
//           NORMALIZE: GAIN so the part played peaks at -1 dB
//   R1      SAVE A COPY where the Target has `save` (the KIT's lanes, batch S1): what it plays, written as
//           a new recording beside its file; the sound keeps its own
//   X       hear it as edited (held: to its end). A box turned is heard once it is done, with the song stopped
//   START   play / stop
// MONO (STEREO, 2026-11-28): on a stereo sound, a stop of its own on the sound's box, at the right of its name,
// reading STEREO or MONO: UP from the top row goes to it, DOWN back. Hold A + UP is MONO (both sides mixed to one),
// + DOWN STEREO, once a push (REVERSE's way); B its usual, STEREO. A mono sound has no such stop and looks as
// it always did.
//
// What it edits is the Target's: the edit, how to set it, the sound (Session::LaneSound) and the rest.

#include <SDL.h>

#include <functional>
#include <string>

#include "input.hpp"
#include "lane_edit.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class EditView {
public:
    struct Target {
        std::function<const LaneEdit&()> edit;              // the edit as it is now
        std::function<void(const LaneEdit&)> set;           // kept (held to its limits there)
        std::function<const Session::LaneSound&()> sound;   // its sound: the source's picture and length, MAKING IT...
        std::function<bool()> trim, normalize;              // Y: false where it is silent
        std::function<void(bool held)> hear;                // X (held), and a turn heard once it is done
        std::function<float()> heardAt;                     // where its line has got to, 0..1 of what plays (below 0: none)
        std::function<void(const std::string&)> say;        // a line said for a moment, in the heading
        std::function<const std::string&()> said;           // ...what is said now ("" for none)
        std::function<void()> save;                         // R1: SAVE A COPY (empty: R1 does nothing; batch S1)
        unsigned rows = (1u << kEditRows) - 1;              // the boxes it has, a bit each (LaneEditRow)
        std::string quiet;   // the heading when nothing is said
        std::string title;   // the sound's name, as the box writes it
        std::string missing; // said where the picture would be when the file is not there
    };
    EditView(Session& session, Project& project) : session_(session), project_(project) {}

    void open(const Target& target, int row);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }
    // The Target's jobs name the track it opened on. EDIT stays open across a tab, so a track picked on SONG
    // meanwhile, or one added, deleted or duplicated (the numbers shift), or the track's kind changed, would
    // leave it drawing and editing another track's sound under this one's name: the screen calls this first
    // in its onButton and update, and it closes.
    void closeIfMoved() {
        if (open_ && (project_.track != track_ || project_.tracks[track_].kind != kind_ || project_.trackCount() != tracks_)) open_ = false;
    }
    int row() const { return row_; }
    bool holding() const { return holding_; }
    float deletePart() const { return deletePart_; } // the hold-B bar, 0..1
    bool hearOwed() const { return hearOwed_; }
    void callOffDelete() { deleting_ = bBack_ = false, deletePart_ = 0.f; } // a hold of B that never saw its let go
    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs, float dt);
    void draw(SDL_Renderer* r, const Fonts& fonts) const; // the sound, the boxes and the hints (the top bar is the screen's)

    // Laid out as the sampler's page: the sound's box at the top (its picture as tall as the box allows),
    // then two rows of four boxes down to the SIGNAL strip.
    static constexpr int kY = 36, kH = 136, kWaveY = kY + 48, kWaveH = kH - 56;
    static constexpr int kRowY = kY + kH + 10, kRowGap = 6, kRowH = (ui::kAboveScope - kRowY - kRowGap) / 2;
    static constexpr int kBoxX = 8, kBoxStep = 157, kBoxW = 153;
    static constexpr Uint32 kDeleteHoldMs = ui::kHoldUndoneMs; // every box back: undo brings them back (G29)
    static constexpr const char* kDeletingWords = "PUTTING EVERY EDIT BACK TO NONE"; // in the heading while the bar fills
    // A box turned is heard ONCE, when the turning is done (owner, 2026-11-17: heard at every step it
    // "sounds odd and you can't really hear what has changed"): A let go, the D-pad and the stick at
    // rest, kHearAfterMs since the last change, and the new sound made (not MAKING IT...). X hears at once.
    static constexpr Uint32 kHearAfterMs = 300;

private:
    bool has(int row) const { return row == kEditMono ? stereo() : row >= 0 && row < kEditRows && (target_.rows >> row & 1u); }
    bool stereo() const; // the sound's source has two sides: MONO is offered (on the sound's box)
    int shownNear(int row, int column) const; // the box it has nearest `column` in a row (the column first, then the left)
    void press(Button direction);
    void set(const LaneEdit& edit); // kept, and heard once the turning is done
    void hearNow(bool held);
    void hearWhenDone(const Input& input, Uint32 nowMs);
    bool bLeavesAfterY(int box) const { return yJust_ && yBox_ == box; }

    Session& session_;
    Project& project_;
    Target target_;
    bool open_ = false;
    int track_ = 0, tracks_ = 0; // the track it opened on, and how many there were then (closeIfMoved)
    TrackKind kind_ = TrackKind::None;
    int row_ = 0;
    bool holding_ = false;       // A is held
    float turned_ = 0.f;         // the stick's part-turned box
    bool reversePushed_ = false; // the stick is pushed on REVERSE (or MONO): it clicks once a push
    int monoFrom_ = 0;           // the box the cursor went up to MONO from (DOWN goes back to it)
    bool deleting_ = false, deleteTimed_ = false, bBack_ = false; // B held; bBack_: its tap goes back
    // Y (trim or normalize) was the last press, on this box: a B straight after it goes back and keeps what Y
    // set (gesture audit G16, 2026-11-25: it put the box under the cursor back).
    bool yJust_ = false;
    int yBox_ = 0;
    Uint32 deleteSince_ = 0, changedAt_ = 0, lastMs_ = 0;
    bool hearOwed_ = false; // a box was turned and its sound is not heard yet
    float deletePart_ = 0.f;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    ui::Repeat repeat_;
};

} // namespace apotheca
