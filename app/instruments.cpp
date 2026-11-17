#include "instruments.hpp"

#include <cstring>

namespace apotheca {

const InstrumentInfo kInstruments[] = {
    {TrackKind::Drums, "DRUMS", "DRUMS", "DRUMS", "8 lanes: built-in drums or your recordings", false, "drums", SynthKind::Phazed, nullptr},
    {TrackKind::Phazed, "PHAZED", "PHZD", "PHAZED", "phase distortion synth", true, "phazed", SynthKind::Phazed, ".phazed"},
    // JAX-EP (owner, 2026-11-14): the name on the device, and what it says it is. No maker's or model's
    // name anywhere a player reads; in the code it is Jx3p (jx3p-plan.md, sections 5 and 8). It stands
    // in the list where the synth before it stood (an old song's word for that one is read, below).
    // What it is, in the owner's words: "a virtual analog synth modeled after a popular DCO synth". The
    // list's line holds about 42 letters at LARGE (DRUMS' fills it), so here it is shortened to its
    // nouns, and the count of presets, which would not fit after it, is left off this one line.
    {TrackKind::Jx3p, "JAX-EP", "JAX", "JAX-EP", "virtual analog, after a popular DCO synth", false, "jaxep", SynthKind::Jx3p, ".jaxep"},
    {TrackKind::Sampler, "SAMPLER", "SMPL", "SAMPLER", "one recording, played from the keys", false, "sampler", SynthKind::Sampler, nullptr},
    // FM-6 (owner, 2026-11-15): six-operator FM on msfa, with the first factory cartridge's 32 sounds. It stands
    // where QMOD stood, whose tracks it takes (an old song's word for that one is read, below). As JAX-EP's, the
    // line names no maker and no model, and leaves the count of presets off (it would not fit after it).
    {TrackKind::Fm6, "FM-6", "FM-6", "FM-6", "6-operator FM, after a classic 80s synth", false, "fm6", SynthKind::Fm6, ".fm6"},
    // PHYSIC (owner, 2026-11-14; docs/plans/physic-plan.md): plucked and struck sounds that ring by themselves, a
    // model of a string or of the ways a bar, bell or skin rings. Last in the list (the plan's call 18). The line
    // says what it plays, not how many presets: the count would not fit after it.
    {TrackKind::Physic, "PHYSIC", "PHYS", "PHYSIC", "plucked and struck: strings, mallets, bells", false, "physic", SynthKind::Physic, ".physic"},
    // ACID (owner, 2026-11-14; docs/plans/acid-plan.md): a one-voice bass, ACCENT a note at 100%, SLIDE its
    // GLIDE. Last in the list. The line names no maker and no model (the words test looks for the box's).
    {TrackKind::Acid, "ACID", "ACID", "ACID", "mono bass, after a classic bass line box", false, "acid", SynthKind::Acid, ".acid"},
    // CLOUD (owner, 2026-11-14; docs/plans/granular-plan.md): clouds of grains from the track's recording (the
    // built-in TONE when it has none). Last in the list.
    {TrackKind::Cloud, "CLOUD", "CLD", "CLOUD", "clouds of grains from your recordings", false, "cloud", SynthKind::Cloud, ".cloud"},
    // MORPH (owner, 2026-11-17; docs/plans/wavetable-plan.md): two wavetable oscillators, POSITION sweeping
    // each from wave to wave. Last in the list. The line names no product.
    {TrackKind::Morph, "MORPH", "MRPH", "MORPH", "wavetables, swept from wave to wave", false, "morph", SynthKind::Morph, ".morph"},
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
    // track, without its sound (song_file.cpp, parseSong). And `qmod` the same: a QMOD track is read as
    // an FM-6 track on its first preset, its knobs in the middle (fm6-plan.md: the owner's decision).
    if (std::strcmp(word, kOldTwinWord) == 0) {
        if (old) *old = true;
        return TrackKind::Jx3p;
    }
    if (std::strcmp(word, kOldQmodWord) == 0) {
        if (old) *old = true;
        return TrackKind::Fm6;
    }
    return TrackKind::None;
}

const char* instrumentName(TrackKind kind) {
    const InstrumentInfo* info = instrument(kind);
    return info ? info->name : "";
}

} // namespace apotheca
