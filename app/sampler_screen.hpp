#pragma once
// The Device screen for a sampler track: one recording, played from the
// piano roll and the chord button like any synth.
//
//   the stick or D-pad moves the cursor
//   SOUND      hold A + up / down: each recording in the "samples" folder,
//              heard as it changes. B: none.
//   a setting  hold A + stick up / down turns it; the D-pad moves it by one.
//              B: its usual value.
//   PRESET     the sound kept as a preset of its own, or "-". Hold A + up /
//              down: the preset after / before (ends stop). R1 saves it: the
//              name keys come up (name_keys.hpp: A types, Y deletes, B or START
//              saves; with nothing typed B leaves), on one of your own with its
//              name typed in. Hold B a second on one of your own: deletes its
//              file, CANNOT BE UNDONE (the track keeps its sound).
//                PITCH    semitones up or down: up is higher. (Kept in the song
//                         as the old "root key": middle C less the pitch.)
//                TUNE     cents
//                START    where in the recording a note begins
//                END      where it ends (beside START; never before it: each stops a
//                         percent short of the other)
//                ATTACK   fade in
//                RELEASE  fade out when the key is let go
//                LOOP     go round from START to END while the key is held
//                LEVEL
//   Y          CATCH (on its let go): the last bars the song played become this track's recording. Hold
//              Y + LEFT / RIGHT: 1, 2 or 4 bars; + UP / DOWN: of THE MIX or one track (catch_key.hpp)
//   L1         CHOP: the recording in 16 even slices from C1 up, each at its own pitch
//              (PITCH moves every slice together). START, END and LOOP work within a
//              slice. Not with a multisample: its zones win.
//   R1         RECORD from a USB microphone or interface: once to start (it
//              waits for sound), again to stop. Twenty seconds at most. While a
//              take waits or runs, R1 on any stop stops it (PRESET too: no SAVE).
//   X          hear it, at middle C (its own pitch, until ROOT or TUNE is moved),
//              for as long as X is held, to the recording's end; let go, its
//              RELEASE runs. A tap is heard for a moment, as before.
//              A setting is also heard as it is changed, when the song is stopped.
//   START      play / stop

#include "redraw.hpp"
#include "screen.hpp"
#include <SDL.h>

#include <string>
#include <vector>

#include "catch_key.hpp"
#include "input.hpp"
#include "name_keys.hpp"
#include "sample_picker.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

void samplerSettingText(int id, int value, char* out, int size);
const char* samplerSettingName(int id); // PITCH, TUNE ... END: as the screen names it
constexpr int kHearKey = 60;         // the sampler is heard at middle C
constexpr Uint32 kHearEveryMs = 150; // a setting being turned is heard this often at most

class SamplerScreen : public Screen {
public:
    explicit SamplerScreen(Session& session) : session_(session), project_(session.project) {}

    void onButton(const ButtonEvent& event, const Input& input) override;
    void update(const Input& input, Uint32 nowMs) override;
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const override;
    unsigned stirring() const override { // (redraw.hpp)
        return (!said_.empty() && lastMs_ < saidUntil_ ? kStirScreenSaid : 0u) | (head_.at(lastMs_) >= 0.f ? kStirPlayhead : 0u) | picker_.stirring();
    }

    // Where things are, for the tests too: the recording's box at the top (its picture is as tall as
    // the box allows), then two rows of settings, then PRESET, down to the SIGNAL strip (ui::kAboveScope).
    static constexpr int kSoundY = 36, kSoundH = 136, kWaveY = kSoundY + 48, kWaveH = kSoundH - 56;
    static constexpr int kSettingY = kSoundY + kSoundH + 10, kSettingGap = 6;
    static constexpr int kPresetH = 34;
    static constexpr int kSettingH = (ui::kAboveScope - kSettingY - kSettingGap - kPresetH - kSettingGap) / 2;
    static constexpr int kPresetY = kSettingY + 2 * (kSettingH + kSettingGap);
    static constexpr int kPresetStop = sampler::kSettings + 1; // below the settings

    // For tests and stills. Stop 0 is SOUND; 1.. are the settings in the order the page shows them
    // (settingAt: PITCH, TUNE, START, END, then ATTACK, RELEASE, LOOP, LEVEL); kPresetStop is PRESET.
    static int settingAt(int place);                 // the setting's id at this place (0..7), -1 past them
    static float endPart(const TrackData& t) { return static_cast<float>(sampler::playedEnd(t.sampler[sampler::Start], t.sampler[sampler::End])) / 100.f; } // 0..1 (past 1: START at 100)
    static int endMarkX(float end, int w) { return ui::waveEndMarkX(end, w); } // END's mark, inside the picture
    void goTo(int stop) { stop_ = stop < 0 ? 0 : (stop > kPresetStop ? kPresetStop : stop); }
    int cursor() const { return stop_; }
    bool typing() const { return keys_.isOpen(); }
    const std::string& typed() const { return keys_.typed(); }
    int keyAt() const { return keys_.keyAt(); }
    void closeKeys(); // closed, nothing kept (the panic: keys R1 opened on its way)
    float presetDeletePart() const { return presetDeletePart_; }
    static constexpr const char* kPresetDeletingWords = "DELETING THE PRESET: CANNOT BE UNDONE"; // the bar while B is held (as the kit's)
    // The keys' title while what is typed names a preset already: B (or START) writes over its file.
    static constexpr const char* kPresetReplacingWords = "B REPLACES THE PRESET: CANNOT BE UNDONE";
    // A hold of B that never saw its let go (L2 / R2 turned the tab, SELECT went to SONG): called off, so the
    // bar does not come back full (App::update, for every screen not shown).
    void callOffDelete();
    static constexpr int kTypeColumns = NameKeys::kColumns, kPresetNameLength = 24; // the grid, and a name's most (the synth's too)
    const ui::Playhead& playhead() const { return head_; }
    int timesHeard() const { return timesHeard_; } // how often it has been played from this screen (for tests)
    const std::string& said() const { return said_; }
    SamplePicker& picker() { return picker_; }
    const SamplePicker& picker() const { return picker_; }

private:
    TrackData& trk() { return project_.current(); }
    const TrackData& trk() const { return project_.current(); }
    void press(Button direction);
    void stepSound(int by);
    void stepPreset(int by);      // the presets after / before: ends stop (none: nothing)
    void hear(bool held = false); // held: X is down, and it sounds until X is let go
    void hearFromList(SamplePicker::Hear how); // the list of recordings' preview (SamplePicker::Hear)
    void setSetting(int id, int value); // kept, and heard

    Session& session_;
    Project& project_;
    int stop_ = 0;
    bool holding_ = false;
    bool stepped_ = false; // A was used to change something: letting go is not a tap
    SamplePicker picker_;
    void openPicker();
    void catchBar(); // Y: the last bars the song played, as the recording (the list of recordings with nothing in it asks too)
    CatchKey catchKey_; // Y's tap and its held choice (catch_key.hpp)
    void record();   // R1: record from the input, or stop
    unsigned sampleFilesSeen_ = 0; // sampleFiles() the picker's lists were read at: behind, it reads them again
    std::vector<std::string> names_;
    unsigned namesSeen_ = 0; // sampleFiles() names_ was read at: behind, the next step reads it again
    std::string said_;
    Uint32 saidUntil_ = 0;
    bool wasRecording_ = false;
    float saved_ = 0.f, turned_ = 0.f;
    bool stickWas_ = false;
    bool stickWait_ = false; // B with A held put the sound back with the stick still pushed: it moves nothing until it comes back
    int loopPush_ = 0; // LOOP with A held: which way the stick is pushed past half, 1 up, -1 down, 0 not (its click is spent)
    ui::StickClimb climb_; // a full push held on the cursor climbs (G07)
    Button repeating_ = Button::None;
    ui::Repeat repeat_; // the held direction's repeats (ui.hpp)
    Uint32 lastMs_ = 0;
    Uint32 heardAt_ = 0; // when a changed setting was last heard
    ui::Playhead head_;  // where the recording has got to while it is heard
    bool headHeld_ = false; // head_ runs for as long as X holds the sound
    float release_ = 0.f;   // seconds the held sound's RELEASE runs once X is let go
    int timesHeard_ = 0;
    // Naming a preset to save (R1 on PRESET): the name keys, as a track's name is typed.
    void typeButton(Button b);
    void drawTyping(SDL_Renderer* r, const Fonts& fonts) const;
    bool nameTaken() const; // what is typed names one of the presets already: B or START replaces it
    NameKeys keys_;
    // Deleting one's own preset: B held a second on it (as a kit's).
    static constexpr Uint32 kPresetDeleteMs = ui::kHoldForGoodMs;
    bool presetDeleting_ = false, presetDeleteTimed_ = false;
    Uint32 presetDeleteSince_ = 0;
    float presetDeletePart_ = 0.f;
};

} // namespace apotheca
