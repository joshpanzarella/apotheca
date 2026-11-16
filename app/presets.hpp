#pragma once
// Presets as text: the player's own preset files, and the built-in presets' data compiled in from
// app/presets/*.inc. How to write a built-in preset: docs/plans/presets-plan.md.
//
// A preset holds a synth's sound (its settings and macros) and may hold the track's sound past its
// synth too (TrackSound, synth_spec.hpp: three effects, two sends, GLIDE FROM, two MODs), in the
// song file's own lines, read and written by song_file.cpp's one reader and writer of them
// (readSoundLine, soundText). A player's own preset always holds it ("whole"); a built-in may.
//
// The built-ins are: the engine's own (the booklet's, the cartridges', PHYSIC's first table), each
// of which may be given a track sound here; then the ones added here, appended in their own
// folders after the engine's. Then the player's own, in USER. A preset's place is in songs:
// built-ins are APPENDED, never put in before another.

#include <string>
#include <vector>

#include "project.hpp"
#include "synth_spec.hpp"

namespace apotheca {

// How long an added built-in's names may be (the FOLDER box holds WOOD AND SKIN, PHYSIC's longest).
constexpr std::size_t kPresetFolderMost = 14, kPresetNameMost = 20;

// What the built-in data of one synth gave.
struct PresetData {
    std::vector<UserPreset> added;      // built-ins past the engine's own, in their order (each names its folder)
    std::vector<TrackSound> sounds;     // the engine's own built-ins' track sounds, by place (not `whole`: plain)
    std::vector<std::string> problems;  // every line not understood, with its number: "line 12: ..." (the tests hold this empty)
};

// The data, read from `text` (app/presets/NAME.inc's words). Bad lines are passed by and named in
// `problems`; numbers are held as doubles first, then to each setting's range.
PresetData readPresetData(TrackKind kind, const std::string& text);
// The synth's built-in data as compiled in, read once (the first time it is asked for).
const PresetData& builtInData(TrackKind kind);
const char* builtInText(TrackKind kind); // the text itself ("" for a kind with none)
// For the tests: the synth's built-in data is this instead (readPresetData of their own text), until
// set again (builtInText's to put it back). The program never calls it.
void useBuiltInDataForTests(TrackKind kind, const PresetData& data);

// A player's preset file ("apotheca-preset 1", then macros, settings and the track-sound lines).
// False if it is not one. Read as carefully as a song: unknown lines skipped, every number held.
bool readPresetFile(TrackKind kind, const std::string& text, UserPreset& out);
// The file for a track as it is now: its synth's sound (a preset's settings: presetSettingCount)
// and its whole track sound, marked "whole" even with nothing in it (so it empties a track's slots).
std::string presetFileText(const TrackData& track);

// The track sound a preset carries, by its place in the synth's list (engine's, added, the
// player's): null where it carries none. Picking it puts that in (Session::chooseSynthPreset).
const TrackSound* presetSound(TrackKind kind, int index);

// The track's own sound past its synth, now, and as the file writes it (two the same: one text).
TrackSound trackSoundOf(const TrackData& track);
std::string trackSoundText(TrackKind kind, const TrackSound& sound);

} // namespace apotheca
