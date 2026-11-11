#pragma once
// What the Device screen needs to know about a synth to show it: its
// presets, its macros, its pages of settings and how each value reads.
// One screen (phazed_screen.hpp) serves every synth from one of these.

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
};

const SynthSpec& phazedSpec();
const SynthSpec& twinSpec();

// Presets the player has made (on the sound-designer page) and saved. They
// come after the built-in ones, in a folder called USER. The spec's preset
// functions know about them; the engine does not, so choosing one sends
// every setting (Session::chooseSynthPreset).
struct UserPreset {
    std::string name;
    std::vector<int> values; // one per setting, in the synth's own order
    std::vector<float> macros; // the eight macros as they were when it was saved
};
void setUserPresets(TrackKind kind, std::vector<UserPreset> presets);
const std::vector<UserPreset>& userPresets(TrackKind kind);
int builtInPresets(TrackKind kind); // how many presets the synth comes with
int settingCount(TrackKind kind);   // how many settings its patch has

// The spec for a track's instrument, or nullptr for drums and empty tracks.
const SynthSpec* specFor(TrackKind kind);

} // namespace apotheca
