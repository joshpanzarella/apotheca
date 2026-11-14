// The instruments a track can be: one table, and every part of the app that
// needs to know about them reads it here.
//
// Adding an instrument: a TrackKind in project.hpp, a row in instruments.cpp,
// and (if it is a synth with presets and macros) a SynthSpec in synth_spec.cpp.
// The row's place in the table is its place in the list a player picks from.
// The `word` is written into song files, so it never changes once it ships.
#pragma once

#include "apotheca/instrument.hpp"
#include "project.hpp"

namespace apotheca {

struct InstrumentInfo {
    TrackKind kind;
    const char* name;      // on a track's head, in a bounce's file name: "PHAZED"
    const char* shortName; // for a narrow column: "PHZD"
    const char* listName;  // in the list to pick from: "JAX-EP"
    const char* about;     // a line under it in that list
    bool countPresets;     // ... followed by ", N presets"
    const char* word;      // in a song file and a preset file: "phazed"
    SynthKind synth;       // what the engine calls it (means nothing for drums)
    const char* presetEnding; // the player's own presets are kept as NAME.phazed; nullptr: it has none
};

extern const InstrumentInfo kInstruments[];
extern const int kInstrumentCount;

// The row for a kind, or nullptr for TrackKind::None.
const InstrumentInfo* instrument(TrackKind kind);
// "phazed" -> TrackKind::Phazed. A word this version does not know is None.
// `old` is set when the word is one no song is written with any more: "twin", Twin Analog's, which
// is read as a JAX-EP track (jx3p-plan.md, section 5). Such a track's sound was another synth's: its
// preset, settings, macros and the macros' automation mean nothing here and are not read.
TrackKind trackKindFromWord(const char* word, bool* old = nullptr);
constexpr const char* kOldTwinWord = "twin";
// "DRUMS", "PHAZED"... and "" for None.
const char* instrumentName(TrackKind kind);

} // namespace apotheca
