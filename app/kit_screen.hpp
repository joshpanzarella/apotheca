#pragma once
// The Device screen for a drum track: the kit, a lane to a row.
//
// Each lane plays its built-in drum, or a recording (a WAV file from the
// "samples" folder). A recording can be FITTED: stretched, at the same pitch,
// to last a number of steps at the song's tempo, so a loop stays in time
// whatever the tempo is.
//
//   the stick or D-pad moves: up / down through the lanes, left / right
//   between SOUND, FIT, CHOKE and EDIT (round the ends).
//   SOUND   tap A: the list of recordings (sample_picker.hpp), to pick one
//           quickly. Or hold A + up / down: the built-in drum, then each
//           recording, one at a time. B: the drum back.
//   FIT     hold A + up / down: OFF, 1, 2, 4, 8, 16 (one bar), 32, 64 steps.
//           B: off.
//   CHOKE   hold A + up / down: NONE, 1, 2. A hit on a lane in a group cuts
//           the other lanes of that group (the closed hat cuts the open one:
//           a new kit has its two hats in 1). B: none.
//   EDIT    the sound's picture: tap A opens the lane's EDIT view (below).
//   Y       CATCH: the last bar the song played, all of it as it was
//           heard, becomes a recording on this lane, fitted to one bar.
//   R1      RECORD from a USB microphone or interface onto this lane: once
//           to start (it waits for sound), again to stop. Ten seconds at most.
//   L1      SLICES: the PATTERN tab, zoomed into this lane's recording cut
//           into slices, for chopping a break (pattern_screen.hpp)
//   X       hear the lane, for as long as X is held (to the recording's end);
//           let go, it fades. A tap is heard for a moment (Session::kAuditionMs).
//   START   play / stop
//
// The EDIT view (owner, 2026-11-16: "rudimentary sample editing"; lane_edit.hpp): the lane's sound
// drawn large, the part played lit between START and END, and eight boxes laid out as the sampler's,
// START, END, FADE IN, FADE OUT / GAIN, PITCH, FINE, REVERSE. The file on the card is never written.
//   the stick or D-pad moves among the boxes
//   hold A + up / down: turns the box (the stick through its range in about a second, by the square
//           of the push; the D-pad a millisecond of the file, half a dB, a fade's stop, a semitone, a cent)
//   B       the box back to its usual value; on a box already there, back to the kit.
//           HELD: every edit of the lane back to none, as the bar fills (undo brings them back)
//   Y       on START or END: TRIM SILENCE (START and END at the first and last sound). Elsewhere
//           NORMALIZE: GAIN so the part played peaks at -1 dB
//   X       hear the lane as edited (held: to its end). A box turned is heard, with the song stopped

#include "redraw.hpp"
#include "screen.hpp"
#include <SDL.h>

#include <string>
#include <vector>

#include "input.hpp"
#include "sample_picker.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

// FIT's stops, and how FIT and CHOKE read (on this screen and on the sound designer's page).
constexpr int kLaneFits[] = {0, 1, 2, 4, 8, 16, 32, 64};
constexpr int kLaneFitCount = sizeof kLaneFits / sizeof kLaneFits[0];
void laneFitText(int steps, char* text, std::size_t size); // "OFF", "4 STEPS", "1 BAR", "2 BARS"
const char* laneChokeText(int group);                      // "NONE", "1", "2"

class KitScreen : public Screen {
public:
    explicit KitScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;
    unsigned stirring() const override { // (redraw.hpp)
        return (!said_.empty() && lastMs_ < saidUntil_ ? kStirScreenSaid : 0u) | (head_.at(lastMs_) >= 0.f ? kStirPlayhead : 0u) | picker_.stirring();
    }

    // For tests and stills.
    void goTo(int lane, int column) { lane_ = lane; column_ = column; }
    int cursorLane() const { return lane_; }
    int cursorColumn() const { return column_; }
    const std::string& said() const { return said_; }
    const ui::Playhead& playhead() const { return head_; }
    enum { kSound, kFit, kChoke, kEdit, kColumns }; // (kEdit: the sound's picture)
    // The lanes' rows, from under the column heads down to the LIVE OUTPUT strip (ui::kAboveScope).
    static constexpr int kLaneTop = 50, kLaneGap = 4;
    static constexpr int kLaneRowH = (ui::kAboveScope + kLaneGap - kLaneTop) / kDrumLanes;
    // The columns at the right of SOUND: FIT TO, CHOKE, the recording's picture (to the screen's edge, 8 in).
    static constexpr int kFitX = 262, kFitW = 96, kChokeX = kFitX + kFitW + 4, kChokeW = 70, kWaveX = kChokeX + kChokeW + 4, kWaveW = ui::kScreenW - 8 - kWaveX;
    SamplePicker& picker() { return picker_; }
    const SamplePicker& picker() const { return picker_; }

    // The EDIT view. Laid out as the sampler's page: the sound's box at the top (its picture as tall
    // as the box allows), then two rows of four boxes down to the LIVE OUTPUT strip.
    bool editing() const { return editing_; }
    void openEdit(int row = 0) { session_.readyLane(project_.track, lane_); editing_ = true; editRow_ = row < 0 ? 0 : (row >= kEditRows ? kEditRows - 1 : row); }
    int editRow() const { return editRow_; }
    float deletePart() const { return deletePart_; } // the hold-B bar, 0..1
    static constexpr int kEditY = 36, kEditH = 136, kEditWaveY = kEditY + 48, kEditWaveH = kEditH - 56;
    static constexpr int kEditRowY = kEditY + kEditH + 10, kEditRowGap = 6, kEditRowH = (ui::kAboveScope - kEditRowY - kEditRowGap) / 2;
    static constexpr int kEditBoxX = 8, kEditBoxStep = 157, kEditBoxW = 153;
    static constexpr Uint32 kDeleteHoldMs = 1000;
    static constexpr const char* kDeletingWords = "PUTTING EVERY EDIT BACK TO NONE"; // in the heading while the bar fills
    static constexpr Uint32 kHearEveryMs = 150; // a box being turned is heard this often at most

private:
    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    void press(Button direction);
    void stepSound(int by);
    void stepFit(int by);
    void stepChoke(int by);
    void editButton(const ButtonEvent& event, const Input& input);
    void editPress(Button direction);
    void editUpdate(const Input& input, Uint32 nowMs, float dt);
    void setEdit(const LaneEdit& edit); // kept, and heard if the song is stopped
    void drawEdit(SDL_Renderer* r, const Fonts& fonts) const;
    void hear(bool held = false); // held: X is down, and it sounds until X is let go
    void hearFromList(SamplePicker::Hear how); // the list of recordings' preview (SamplePicker::Hear)

    Session& session_;
    Project& project_;
    int lane_ = 0, column_ = kSound;
    bool holding_ = false; // A is held
    bool stepped_ = false; // ...and something was changed with it, so letting go is not a tap
    SamplePicker picker_;
    void openPicker();
    std::vector<std::string> names_; // the recordings there were when A went down
    ui::Playhead head_; // where the lane last heard has got to
    int headLane_ = -1;
    bool headHeld_ = false; // head_ runs for as long as X holds the sound
    std::string said_; // what the last catch came to, shown for a moment
    Uint32 saidUntil_ = 0;
    bool wasRecording_ = false;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
    bool editing_ = false;
    int editRow_ = 0;
    float turned_ = 0.f;        // the stick's part-turned box
    bool reversePushed_ = false; // the stick is pushed on REVERSE: it clicks once a push
    bool deleting_ = false, deleteTimed_ = false, bBack_ = false; // B held; bBack_: its tap goes back to the kit
    Uint32 deleteSince_ = 0, heardAt_ = 0;
    float deletePart_ = 0.f;
};

} // namespace apotheca
