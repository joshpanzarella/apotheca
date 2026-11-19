#pragma once
// The Device screen for a synth: Phazed, JAX-EP, FM-6 or PHYSIC. Each is described
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
//   X                hear the sound. Held on a macro: its MOD (mod_knob.hpp), its fields in LIVE
//                    OUTPUT's place while X is held, and the note heard as long as X is.
//   Y                PHYSIC and CLOUD (SynthSpec::roll): a new sound from the dice.
//
// A synth that plays the track's recording (CLOUD: SynthSpec::recording) shows its picture on MAIN,
// between PRESET and the macros (POSITION a brass line, SPRAY a lit band about it, each grain sounding
// a tick at its place), and its first page's first stop is RECORDING: a tap of A the list of
// recordings (the sampler's, with its preview), hold A + up / down the next one, R1 there RECORD
// (elsewhere R1 is the next page), Y anywhere on that page CATCH (the last bar the song played).
//
// ADVANCED is every setting. For Phazed that is nine pages: the sound as a
// whole, each of the two lines, and each line's three eight-step envelopes
// drawn out. For JAX-EP, five. For FM-6, eight: the voice and its LFO, one a
// operator (its ON and all 21 of its settings), and the pitch envelope. For
// PHYSIC, four: SOUND, RING, PLAY, DICE. L1 and R1 turn the pages; B goes back
// to MAIN.

#include "redraw.hpp"
#include "screen.hpp"
#include <SDL.h>

#include <string>
#include <vector>

#include "input.hpp"
#include "mod_knob.hpp"
#include "sample_picker.hpp"
#include "session.hpp"
#include "synth_spec.hpp"
#include "ui.hpp"

namespace apotheca {

class SynthScreen : public Screen {
public:
    SynthScreen(Session& session, const SynthSpec& spec) : session_(session), project_(session.project), spec_(spec), modKnob_(session, this) {}

    void choosePreset(int index); // any preset, the player's own included; it comes round at the ends
    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;
    unsigned stirring() const override { // (redraw.hpp)
        return (!said_.empty() && lastMs_ < saidUntil_ ? kStirScreenSaid : 0u) | picker_.stirring();
    }

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
    // Six rows (FM-6's operators) stand kTightGap apart: at kSettingGap they were 52 high, and a value
    // stood on its bar even at LARGE. At 3 the cursor's outline (a pixel outside its box) still clears
    // the next box.
    static constexpr int kSettingY = 60, kSettingGap = 6, kTightGap = 3, kTightRows = 6;
    static constexpr int settingGap(int rows) { return rows >= kTightRows ? kTightGap : kSettingGap; }
    static constexpr int settingH(int rows) {
        return rows < 1 ? kMacroH : (ui::kAboveScope - kSettingY - (rows - 1) * settingGap(rows)) / rows;
    }
    // Where row `row` of `rows` stands: the pixels the height does not divide evenly are shared out
    // between the gaps, so the last row ends on kAboveScope however many rows.
    static constexpr int settingY(int rows, int row) {
        return kSettingY + row * (settingH(rows) + settingGap(rows)) +
               (rows < 2 ? 0 : row * (ui::kAboveScope - kSettingY - rows * settingH(rows) - (rows - 1) * settingGap(rows)) / (rows - 1));
    }

    // The lettering a page's boxes are drawn in: the chosen size, or LARGE where the value would come
    // within kValueClear of the box's bar (its name, the gap under it and the value's capitals, from the
    // box's top: valueBox's own places). Nothing today (FM-6's six rows, 55 high, keep 2 pixels at XL):
    // a guard for a page of more rows, or a lettering made larger.
    static constexpr float kValueClear = 2.f;
    static const Fonts& boxFontsFor(const Fonts& fonts, int h) {
        return 7.f + fonts.small.capHeight() + 8.f + fonts.large.capHeight() + kValueClear > h - 10 ? fonts.fixed() : fonts;
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

    // X held on a macro: its MOD (for the tests: held(), mod(), full()...).
    const ModKnob& modKnob() const { return modKnob_; }

    // A synth that plays the recording (CLOUD): MAIN's picture under PRESET, and the macros and
    // ADVANCED below it, a little shorter, still down to the LIVE OUTPUT strip.
    static constexpr int kPictureY = kPresetY + kPresetH + 10, kPictureH = 80;
    static constexpr int kPicMacroY = kPictureY + kPictureH + 10;
    static constexpr int kPicMacroH = (ui::kAboveScope - 10 - kAdvancedH - kMacroGap - kPicMacroY) / 2;
    static constexpr int kPicAdvancedY = kPicMacroY + 2 * kPicMacroH + kMacroGap + 10;
    static_assert(kPicAdvancedY + kAdvancedH <= ui::kAboveScope, "MAIN ends above LIVE OUTPUT");
    // (MORPH's wave takes the same room: SynthSpec::table.)
    bool picture() const { return spec_.recording || spec_.table; }
    int macroY() const { return picture() ? kPicMacroY : kMacroY; }
    int macroH() const { return picture() ? kPicMacroH : kMacroH; }
    int advancedY() const { return picture() ? kPicAdvancedY : kAdvancedY; }
    // MORPH's picture: the strip of frames along its foot, and the wave's room above it.
    static constexpr int kFrameStripH = 10;
    // The picture's ticks are as tall as this, at its top and at its bottom.
    static constexpr int kTickH = 8;
    // CLOUD's RECORDING stop: the first of its first page (not a setting: its id is kRecordingId).
    static constexpr int kRecordingId = -1;
    bool onRecording() const { return spec_.recording && page_ == 1 && cursor_ == 0; }
    SamplePicker& picker() { return picker_; }
    const SamplePicker& picker() const { return picker_; }
    const std::string& said() const { return said_; }

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
    int valueOf(int id) const; // a setting's value, as settingValue reads it (a bit of another's too)
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
    unsigned rolls_ = 0;   // how many times Y has rolled, so two in one frame differ
    bool holding_ = false; // A is held
    float saved_ = 0.f;    // part-steps saved up while the stick turns

    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0;
    Uint32 lastMs_ = 0;
    float stickX_ = 0.f, stickY_ = 0.f;
    bool stickWasX_ = false, stickWasY_ = false;
    ModKnob modKnob_;
    bool stickWait_ = false; // X let go off a MOD with the stick still pushed: it moves nothing until it comes back
    void endMod(const Input& input);

    // CLOUD's recording (SynthSpec::recording).
    SamplePicker picker_;
    unsigned sampleFilesSeen_ = 0; // sampleFiles() the picker's lists were read at: behind, it reads them again
    std::string said_;
    Uint32 saidUntil_ = 0;
    bool stepped_ = false; // A was used to change something: letting go is not a tap
    std::vector<std::string> names_; // the recordings, read as A goes down on RECORDING
    void openPicker();
    void stepRecording(int by);
    void hearFromList(SamplePicker::Hear how);
    void say(const std::string& words, Uint32 ms);
    void drawPicture(SDL_Renderer* r, const Fonts& fonts) const;
    void drawTablePicture(SDL_Renderer* r, const Fonts& fonts) const;
    void drawScreen(SDL_Renderer* r, const Fonts& fonts) const;
    bool wasRecording_ = false;
};

} // namespace apotheca
