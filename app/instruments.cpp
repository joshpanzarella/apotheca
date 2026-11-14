#include "instruments.hpp"

#include <cstring>

namespace apotheca {

const InstrumentInfo kInstruments[] = {
    {TrackKind::Drums, "DRUMS", "DRUMS", "DRUMS", "8 lanes: built-in drums or your recordings", false, "drums", SynthKind::Phazed, nullptr},
    {TrackKind::Phazed, "PHAZED", "PHZD", "PHAZED", "the synth", true, "phazed", SynthKind::Phazed, ".phazed"},
    // JAX-EP (owner, 2026-11-14): the name on the device, and what it says it is. No maker's or model's
    // name anywhere a player reads; in the code it is Jx3p (jx3p-plan.md, sections 5 and 8). It stands
    // in the list where Twin Analog stood.
    // What it is, in the owner's words: "a virtual analog synth modeled after a popular DCO synth". The
    // list's line holds about 42 letters at LARGE (DRUMS' fills it), so here it is shortened to its
    // nouns, and the count of presets, which would not fit after it, is left off this one line.
    {TrackKind::Jx3p, "JAX-EP", "JAX", "JAX-EP", "virtual analog, after a popular DCO synth", false, "jaxep", SynthKind::Jx3p, ".jaxep"},
    {TrackKind::Sampler, "SAMPLER", "SMPL", "SAMPLER", "one recording, played from the keys", false, "sampler", SynthKind::Sampler, nullptr},
    {TrackKind::Qmod, "QMOD", "QMOD", "QMOD", "FM you roll like dice, not program", false, "qmod", SynthKind::Qmod, ".qmod"},
};
const int kInstrumentCount = static_cast<int>(sizeof kInstruments / sizeof kInstruments[0]);

const InstrumentInfo* instrument(TrackKind kind) {
    for (int i = 0; i < kInstrumentCount; ++i)
        if (kInstruments[i].kind == kind) return &kInstruments[i];
    return nullptr;
}

TrackKind trackKindFromWord(const char* word, bool* old) {
    if (old) *old = false;
    for (int i = 0; i < kInstrumentCount; ++i)
        if (std::strcmp(kInstruments[i].word, word) == 0) return kInstruments[i].kind;
    // THE ONE PLACE the word `twin` lives on: an old song's Twin Analog track is read as a JAX-EP
    // track, without its sound (song_file.cpp, parseSong).
    if (std::strcmp(word, kOldTwinWord) == 0) {
        if (old) *old = true;
        return TrackKind::Jx3p;
    }
    return TrackKind::None;
}

const char* instrumentName(TrackKind kind) {
    const InstrumentInfo* info = instrument(kind);
    return info ? info->name : "";
}

} // namespace apotheca
