#pragma once
// The Device screen for a synth: Phazed, JAX-EP or QMOD. Each is described
// by a SynthSpec (synth_spec.hpp); the screen itself is the same.
//
// MAIN is all most sounds need: the folder, the preset, eight macros, and a
// way into ADVANCED. Nothing else is on it, on purpose.
//
//   the stick moves; hold A and the stick turns the thing under the cursor.
//   FOLDER, PRESET   hold A + stick up / down (or D-pad) to step through.
//                    A new sound is played at once when the song is stopped.
//   a macro          hold A + stick up / down. B puts it back in the middle.
//   ADVANCED         A opens the pages.
//   X                hear the sound
//
// ADVANCED is every setting. For Phazed that is nine pages: the sound as a
// whole, each of the two lines, and each line's three eight-step envelopes
// drawn out. For JAX-EP, five. L1 and R1 turn the pages; B goes back
// to MAIN.

#include "screen.hpp"
#include <SDL.h>

#include "input.hpp"
#include "session.hpp"
#include "synth_spec.hpp"
#include "ui.hpp"

namespace apotheca {

class SynthScreen : public Screen {
public:
    SynthScreen(Session& session, const SynthSpec& spec) : session_(session), project_(session.project), spec_(spec) {}

    void choosePreset(int index); // any preset, the player's own included; it comes round at the ends
    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;

    // For tests and stills.
    static constexpr int kPages = 10; // Phazed's: MAIN, then nine advanced pages
    int pageCount() const { return spec_.pages; }
    int page() const { return page_; }
    int cursor() const { return cursor_; }
    void goTo(int page, int cursor);
    // The setting under the cursor on an advanced page, or -1.
    int settingHere() const;

    // Where MAIN's rows are, from the top down to the LIVE OUTPUT strip (ui::kAboveScope), which
    // they fill: FOLDER and PRESET, two rows of four macros, ADVANCED. For the tests too.
    static constexpr int kPresetY = 36, kPresetH = 60;
    static constexpr int kMacroY = kPresetY + kPresetH + 10, kMacroW = 153, kMacroPitchX = 157, kMacroGap = 6;
    static constexpr int kAdvancedH = 56;
    static constexpr int kMacroH = (ui::kAboveScope - 10 - kAdvancedH - kMacroGap - kMacroY) / 2;
    static constexpr int kAdvancedY = kMacroY + 2 * kMacroH + kMacroGap + 10;
    // An advanced page that is not an envelope: rows of four settings, spread down to the strip
    // however few they are (owner's decision: no large empty band on these pages), so one row is
    // one tall box a setting, its value in the middle and its bar at the bottom.
    static constexpr int kSettingY = 60, kSettingGap = 6;
    static constexpr int settingH(int rows) {
        return rows < 1 ? kMacroH : (ui::kAboveScope - kSettingY - (rows - 1) * kSettingGap) / rows;
    }

    // MAIN's two boxes, and the folder's note (SynthSpec::folderNote: JAX-EP's "SPEEDS GUESSED"). The
    // note stands on PRESET's top row, kNoteGap after the word PRESET, and the row keeps room for a
    // count as long as kCountLongest whatever the count is, so the note is drawn the same way for
    // every preset. Where the three do not fit at the size chosen the whole row is drawn at LARGE.
    static constexpr int kFolderX = 8, kFolderW = 200, kPresetX = 216, kPresetW = 416, kNoteGap = 10;
    static constexpr const char* kCountLongest = "888 / 888";
    // For a still or a test: the note as a folder of batch 7 (BANK A) will say it, or nullptr for the spec's own.
    void folderNoteAs(const char* note) { noteAs_ = note; }
    const char* folderNote() const { return noteAs_ ? noteAs_ : spec_.folderNote ? spec_.folderNote(trk().preset) : nullptr; }

    // The count on PRESET's row is the preset's place IN ITS FOLDER, out of that folder's presets
    // (owner, 2026-11-15: JAX-EP's B7 reads "7 / 16", the factory's number, not "23 / 32"). A folder
    // is the run of presets side by side under one name, as stepFolder steps them; the player's own
    // (USER) count among themselves. Every synth alike.
    int placeInFolder(int preset, int& of) const;

    // Cursor stops on MAIN.
    enum { kFolder, kPreset, kFirstMacro, kAdvanced = kFirstMacro + kSynthMacros, kMainStops };

private:
    struct Item { int id; const char* name; };

    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    int itemCount() const;
    Item item(int index) const;
    int columns() const;
    bool envelopePage() const { return page_ > 0 && spec_.envelopePage(page_); }
    int* values() { return spec_.values(trk()); }
    const char* noteAs_ = nullptr;
    const int* values() const { return spec_.values(const_cast<TrackData&>(trk())); }
    void moveCursor(Button direction);
    void stepPreset(int by);
    void stepFolder(int by);
    void setMacro(int macro, float value);
    void setSetting(int id, int value);
    void turn(float amount); // the stick with A held: -1..1 of "full range a second"
    void stepExact(int by);
    void stepHeld(Button direction); // the D-pad with A held
    static constexpr int kPresetJump = 8;  // the D-pad with A held
    void hear();

    Session& session_;
    Project& project_;
    const SynthSpec& spec_;
    int page_ = 0;
    int cursor_ = kPreset;
    static constexpr int kChoiceRange = 12; // a setting with this many steps or fewer is a choice, not a range
    bool isChoice(int id) const;
    bool choicePushed_ = false; // the stick is over, and has already clicked a choice once
    unsigned rolls_ = 0; // how many times Y has rolled, so two in one frame differ
    bool holding_ = false; // A is held
    float saved_ = 0.f;    // part-steps saved up while the stick turns

    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f;
    bool stickWasX_ = false, stickWasY_ = false;
};

} // namespace apotheca
