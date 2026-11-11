#include "instruments.hpp"

#include <cstring>

namespace apotheca {

const InstrumentInfo kInstruments[] = {
    {TrackKind::Drums, "DRUMS", "DRUMS", "DRUMS", "8 lanes: built-in drums or your recordings", false, "drums", SynthKind::Phazed, nullptr},
    {TrackKind::Phazed, "PHAZED", "PHZD", "PHAZED", "the synth", true, "phazed", SynthKind::Phazed, ".phazed"},
    {TrackKind::Twin, "TWIN", "TWIN", "TWIN ANALOG", "two morphing oscillators", true, "twin", SynthKind::Twin, ".twin"},
    {TrackKind::Sampler, "SAMPLER", "SMPL", "SAMPLER", "one recording, played from the keys", false, "sampler", SynthKind::Sampler, nullptr},
    {TrackKind::Qmod, "QMOD", "QMOD", "QMOD", "FM you roll like dice, not program", false, "qmod", SynthKind::Qmod, ".qmod"},
};
const int kInstrumentCount = static_cast<int>(sizeof kInstruments / sizeof kInstruments[0]);

const InstrumentInfo* instrument(TrackKind kind) {
    for (int i = 0; i < kInstrumentCount; ++i)
        if (kInstruments[i].kind == kind) return &kInstruments[i];
    return nullptr;
}

TrackKind trackKindFromWord(const char* word) {
    for (int i = 0; i < kInstrumentCount; ++i)
        if (std::strcmp(kInstruments[i].word, word) == 0) return kInstruments[i].kind;
    return TrackKind::None;
}

const char* instrumentName(TrackKind kind) {
    const InstrumentInfo* info = instrument(kind);
    return info ? info->name : "";
}

} // namespace apotheca
