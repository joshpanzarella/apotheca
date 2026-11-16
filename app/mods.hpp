#pragma once
// MOD in the app: a track's two modulators as the song holds them (TrackData::mods), and the one
// rule for what a MOD may be aimed at. The Session, the song file and a TYPE change all ask here.
// The shapes, rates and numbers are the engine's (apotheca/mod_rule.hpp); docs/plans/modulation-plan.md.
//
//   A MOD that is OFF is a blank one (ModSettings()): the song file writes no line for it, so
//   nothing else about it is kept anywhere, and "OFF" is one thing to compare.
//   A MOD whose target does not fit its track is OFF: a setting that is not there, a setting that
//   is a CHOICE (a wobbled choice clicks), a macro on a track with none.

#include "apotheca/effects.hpp"
#include "apotheca/mod_rule.hpp"
#include "project.hpp"
#include "synth_spec.hpp"

namespace apotheca {

// The Session compares two MODs, and marks a change for undo, by their bytes: nothing unnamed in one.
static_assert(sizeof(ModSettings) == (kModFields + kModSteps) * sizeof(float), "a ModSettings is its fields and its row, no padding");

// A synth's macro that is a choice, not an amount: it works by thresholds, so a MOD is not aimed
// at it (owner's call 12: later, a STEPPER only). The one place that says which.
inline bool macroIsChoice(TrackKind kind, int macro) {
    if (kind == TrackKind::Phazed) return macro == 6 || macro == 7; // CHORUS (on / off), OCTAVE
    if (kind == TrackKind::Physic) return macro == 7;                // OCTAVE (down / as the preset / up)
    return false;
}

// May a MOD on this track be aimed at `target` (an automation lane's number, or LEVEL / PAN)? Asked
// of a track's kind and its three slots (a preset's too: presets.hpp), or of the track itself.
inline bool modTargetFits(TrackKind trackKind, const EffectData* effects, int target) {
    if (trackKind == TrackKind::None || !modTargetKnown(target)) return false;
    if (modTargetIsMacro(target)) return specFor(trackKind) != nullptr && !macroIsChoice(trackKind, target - kModFirstMacro);
    if (modTargetIsEffect(target)) {
        const int kind = effects[modTargetSlot(target)].kind;
        if (kind <= 0 || kind >= static_cast<int>(kEffectKinds)) return false; // an empty slot (or an effect nobody knows)
        const EffectKind effect = static_cast<EffectKind>(kind);
        const int setting = modTargetSetting(target);
        return setting < effectInfo(effect).params && effectChoices(effect, setting) == 0;
    }
    return true; // a send, LEVEL or PAN (batch 8): every track has them
}
inline bool modTargetFits(const TrackData& track, int target) { return modTargetFits(track.kind, track.effects, target); }

// A MOD as the song may hold it on this track: every field held to what it can be (mod_rule.hpp's
// modSet: a number nobody knows is OFF, 1 BAR, SINE...), and OFF, blank, if it is OFF or its
// target does not fit. A STEPPER at FREE (an LFO's rate alone) is at 1 BAR.
inline ModSettings modHeldFor(TrackKind trackKind, const EffectData* effects, const ModSettings& asked) {
    ModSettings m;
    for (int field = 0; field < kModFields; ++field) modSet(m, field, modGet(asked, field));
    for (int step = 0; step < kModSteps; ++step) modSetStep(m, step, asked.steps[step]);
    if (m.kind == kModOff || !modTargetFits(trackKind, effects, m.target)) return ModSettings();
    if (m.kind == kModStepper && m.rate == kModRateFree) m.rate = kModRateBar; // FREE is an LFO's alone: a STEPPER made from a free LFO starts at 1 BAR
    return m;
}
inline ModSettings modHeldFor(const TrackData& track, const ModSettings& asked) { return modHeldFor(track.kind, track.effects, asked); }

// Every MOD of the track made to fit it (a song just read; an effect gone from a slot; a preset read).
inline void modsFit(TrackKind trackKind, const EffectData* effects, ModSettings* mods) {
    for (int i = 0; i < kModsPerTrack; ++i) mods[i] = modHeldFor(trackKind, effects, mods[i]);
}
inline void modsFit(TrackData& track) { modsFit(track.kind, track.effects, track.mods); }

// A slot's effect becomes another TYPE of its family: where a MOD aimed at the old effect's
// setting goes. The setting is followed BY NAME, as its value and its automation are
// (effectCarryFrom); a setting the new type does not keep, or keeps as a choice: no target (OFF).
inline int modTargetCarried(EffectKind from, EffectKind to, int target) {
    if (!modTargetIsEffect(target)) return target;
    const int slot = modTargetSlot(target), setting = modTargetSetting(target);
    for (int i = 0; i < effectInfo(to).params; ++i)
        if (effectCarryFrom(from, to, i) == setting && effectChoices(to, i) == 0) return kModFirstEffect + slot * 4 + i;
    return kModTargetNone;
}

} // namespace apotheca
