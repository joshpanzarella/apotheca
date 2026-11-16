#pragma once
// What the Device screen needs to know about a synth to show it: its
// presets, its macros, its pages of settings and how each value reads.
// One screen (synth_screen.hpp) serves every synth from one of these.

#include <cstdint>
#include <string>
#include <vector>

#include "project.hpp"

namespace apotheca {

struct SynthSpec {
    const char* name; // as on screen: "PHAZED"
    int (*presetCount)();
    const char* (*presetName)(int index);
    const char* (*presetCategory)(int index);
    void (*loadPreset)(TrackData& track, int index); // fills the track's patch from a preset
    int* (*values)(TrackData& track);                // the track's patch, one whole number per setting
    int (*settingMin)(int id);
    int (*settingMax)(int id);
    void (*settingText)(int id, int value, char* out, int size);
    const char* macroNames[8];
    void (*macroText)(int macro, float value, char* out, int size);
    int pages; // MAIN and the advanced pages
    const char* const* pageNames;
    int (*itemCount)(int page);
    void (*item)(int page, int index, int& id, const char*& name);
    bool (*envelopePage)(int page); // an eight-step envelope, drawn out
    // A few words about the folder a preset is in, said on MAIN beside PRESET; null for a synth with
    // nothing to say, and may give null for a preset. JAX-EP's say that its speeds are guesses.
    const char* (*folderNote)(int preset);
    // The WiFi designer page is given every setting's values as the device reads them, however many
    // there are (JAX-EP: a knob reads as its dial, 0.0 to 10.0, not as 0 to 100). False: only for a
    // setting of few values, a choice, and a longer one shows its plain number there.
    bool pageReadsAsDevice;
    // A setting shown on a page that is one bit of another (FM-6's OPERATOR n ON, a bit of its
    // OPERATORS ON): its id is past the synth's own (settingCount); gives the setting it is a bit of
    // and which bit. Null for a synth with none. Read and set them through settingValue and
    // settingWrite, below, never by values()[id].
    bool (*bitOf)(int id, int& whole, int& bit);
    // How a setting reads beside the rest of the sound (FM-6's COARSE and FINE: a ratio of the key, or
    // under MODE FIXED a pitch in Hz). Null: as settingText reads it alone. The device reads this; the
    // WiFi page's list of readings is settingText's.
    void (*readingIn)(const int* values, int id, int value, char* out, int size);
    // Y on MAIN: a new sound from the dice, `seed` deciding it (PHYSIC's: a new strike and ring on the
    // same MODEL, its levels and dice left; physic-plan.md, section 3). Null: Y does nothing there.
    void (*roll)(TrackData& track, unsigned seed);
};

// What JAX-EP's folders say (jx3p-plan.md, section 4): the two banks are from a booklet of 1983 and may
// not be the machine's own sounds, and every time and rate in the synth is a guess until it is measured.
extern const char* const kJaxepBankNote;  // BANK A, BANK B
extern const char* const kJaxepGuessNote; // any other built-in folder
const char* jaxepFolderNote(const char* folder); // which of the two a built-in folder says

const SynthSpec& phazedSpec();
const SynthSpec& jx3pSpec(); // JAX-EP on the device
const SynthSpec& fm6Spec();
const SynthSpec& physicSpec();

// What a preset may hold past its synth (docs/plans/presets-plan.md): the track's three effects, its
// two sends, its GLIDE FROM and its two MODs, "the track's sound". A preset that holds it is WHOLE:
// picked, it puts every one of them in (a slot it leaves out is emptied, a send left out is 0, a MOD
// left out is OFF). One that is not (the booklet's and the cartridges' sounds, an older file) leaves
// the track's alone. Not the mixer's LEVEL and PAN: those are the song's balance, not the sound's.
struct TrackSound {
    bool whole = false;
    EffectData effects[kTrackSlots];
    float send[kBuses] = {};
    std::uint8_t glideFrom = 0; // GLIDE FROM (0 TOP)
    ModSettings mods[kModsPerTrack];
};

// A preset the app holds the values of: one the player made (on the sound-designer page, or SAVE)
// and saved, in a folder called USER after every built-in; or a built-in added in the app's preset
// data (app/presets/*.inc), after the engine's own, in the folder it names. The spec's preset
// functions know about both; the engine does not, so choosing one sends every setting
// (Session::chooseSynthPreset).
struct UserPreset {
    std::string name;
    std::vector<int> values;   // one per setting, in the synth's own order
    std::vector<float> macros; // the eight macros as they were when it was saved
    std::string folder;        // a built-in's folder; empty for the player's own (USER)
    TrackSound sound;          // the track's sound past its synth (`whole` if the preset holds it)
};
void setUserPresets(TrackKind kind, std::vector<UserPreset> presets);
const std::vector<UserPreset>& userPresets(TrackKind kind);
int builtInPresets(TrackKind kind); // how many presets the synth comes with: the engine's, then the app's added ones
int enginePresets(TrackKind kind);  // how many of them the engine has itself (Command::SelectPreset takes those)
// The preset at `index` of the synth's list when the app holds its values (an added built-in or the
// player's own); null for one of the engine's (or no preset).
const UserPreset* listedPreset(TrackKind kind, int index);
int settingCount(TrackKind kind);   // how many settings it has (what a song keeps for a track)
// How many of them, from id 0, a preset holds: what picking one writes and what saving one keeps. All
// of them, but for JAX-EP: its last two (VELOCITY, BRILLIANCE) are the track's own, not the patch's.
int presetSettingCount(TrackKind kind);
// How many a page may show: settingCount, then those that are a bit of another (FM-6's six ONs, ids
// settingCount to settingCount + 5). The WiFi page is given them all, in that order.
int shownSettingCount(TrackKind kind);
// A setting's value, a bit of another's included; and what to set for it: the engine's setting and its
// whole new value (`value` held to its range first). Then how it reads on the device.
int settingValue(const SynthSpec& spec, const int* values, int id);
void settingWrite(const SynthSpec& spec, const int* values, int id, int value, int& realId, int& realValue);
void settingReading(const SynthSpec& spec, const int* values, int id, char* out, int size);

// The spec for a track's instrument, or nullptr for drums and empty tracks.
const SynthSpec* specFor(TrackKind kind);

} // namespace apotheca
