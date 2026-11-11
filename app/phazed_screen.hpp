#pragma once
// The Device screen for a synth: Phazed or Twin Analog. Each is described
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
// drawn out. For Twin Analog, three. L1 and R1 turn the pages; B goes back
// to MAIN.

#include <SDL.h>

#include "input.hpp"
#include "session.hpp"
#include "synth_spec.hpp"
#include "ui.hpp"

namespace apotheca {

class PhazedScreen {
public:
    PhazedScreen(Session& session, const SynthSpec& spec) : session_(session), project_(session.project), spec_(spec) {}

    void choosePreset(int index); // any preset, the player's own included; it comes round at the ends
    void onButton(const ButtonEvent& event, const Input& input);
    void update(const Input& input, Uint32 nowMs);
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    // For tests and stills.
    static constexpr int kPages = 10; // Phazed's: MAIN, then nine advanced pages
    int pageCount() const { return spec_.pages; }
    int page() const { return page_; }
    int cursor() const { return cursor_; }
    void goTo(int page, int cursor);
    // The setting under the cursor on an advanced page, or -1.
    int settingHere() const;

    // Cursor stops on MAIN.
    enum { kFolder, kPreset, kFirstMacro, kAdvanced = kFirstMacro + phazed::kMacros, kMainStops };

private:
    struct Item { int id; const char* name; };

    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    int itemCount() const;
    Item item(int index) const;
    int columns() const;
    bool envelopePage() const { return page_ > 0 && spec_.envelopePage(page_); }
    int* values() { return spec_.values(trk()); }
    const int* values() const { return spec_.values(const_cast<TrackData&>(trk())); }
    void moveCursor(Button direction);
    void stepPreset(int by);
    void stepFolder(int by);
    void setMacro(int macro, float value);
    void setSetting(int id, int value);
    void turn(float amount); // the stick with A held: -1..1 of "full range a second"
    void stepExact(int by);  // the D-pad with A held
    void hear();

    Session& session_;
    Project& project_;
    const SynthSpec& spec_;
    int page_ = 0;
    int cursor_ = kPreset;
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
