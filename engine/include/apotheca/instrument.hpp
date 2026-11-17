#pragma once
// What the engine needs from any synth it puts on a track.
//
// The constructor and prepare() may allocate and are not for the audio
// thread. Everything else is called on it and must not allocate or wait.

#include <cstdint>

namespace apotheca {

class Instrument {
public:
    virtual ~Instrument() = default;
    virtual bool ok() const { return true; }
    virtual void prepare(float sampleRate) = 0;

    virtual void selectPreset(int index) = 0;      // macros go back to the middle
    virtual void setSetting(int id, int value) = 0; // edit the patch
    virtual void setMacro(int macro, float value) = 0; // 0..1, 0.5 = as the patch says
    virtual void noteOn(int note, float velocity) = 0; // MIDI note number; velocity 0..1
    // The same, but the note slides to its pitch from the key `from`, taking `seconds` (GLIDE).
    // A synth that cannot slide just plays the note.
    virtual void noteOnFrom(int note, float velocity, int from, float seconds) {
        (void)from;
        (void)seconds;
        noteOn(note, velocity);
    }
    virtual void noteOff(int note) = 0;
    virtual void allNotesOff() = 0;
    // The roll of the next noteOn / noteOnFrom: where and which time it is struck (the track, the
    // key and the strike's own moment since Play: physic_rule.hpp, strikeSeed), handed over just
    // before it. A synth with no dice ignores it. (PHYSIC's; docs/plans/physic-plan.md, section 3.)
    virtual void noteSeed(std::uint32_t seed) { (void)seed; }
    // How long a step of the song is, in seconds, handed over just before every noteOn /
    // noteOnFrom the step clock sends. A synth with no use for it ignores it. (ACID's: a GLIDE note
    // ties to the one before when the gap between them is under a step: acid_rule.hpp, tieSeconds.)
    virtual void noteStep(double seconds) { (void)seconds; }
    // True: a GLIDE whose key of the moment before is the note's own key comes as noteOnFrom with
    // from == note (a tie: one long note), not as a plain noteOn. Every other synth: false, and such
    // a glide is a plain noteOn as it always was. (ACID's.)
    virtual bool tiesSameKey() const { return false; }
    // Stop (not Play, not panic): every note let go as by noteOff. A synth whose notes ring on
    // after they are let go keeps ringing, so a bounce, which sends Stop when the song's last sample
    // has played and then records the tail, keeps it. By default allNotesOff, as it always was.
    virtual void stopNotes() { allNotesOff(); }
    // For an instrument that plays a recording: here it is (or nullptr for
    // none). The samples stay alive until replaced. Realtime safe.
    virtual void setSample(const float* data, int length) { (void)data; (void)length; }
    // For an instrument that plays grains (CLOUD): where each sounding grain reads now, as a share
    // of its recording (0..1), at most `most` of them, after the last render; how many. -1: it
    // plays no grains. Called on the thread that renders it, after render. MORPH gives its keys'
    // places in their tables here (morph.hpp says how): the same readout for the picture.
    virtual int grainPlaces(float* places, int most) const { (void)places; (void)most; return -1; }

    // Writes `frames` samples (replacing what was there).
    virtual void render(float* left, float* right, int frames) = 0;
};

enum class SynthKind { Phazed, Sampler, Jx3p, Fm6, Physic, Acid, Cloud, Morph }; // (memory only: QMOD went, FM-6 took its place, 2026-11-15; PHYSIC appended, ACID after it, CLOUD after that, MORPH after CLOUD)

// Every synth has this many macros (the eight knobs on its DEVICE screen).
// Songs, presets and automation lanes 0..7 all count on it; each synth's
// header checks its own list against it.
constexpr int kSynthMacros = 8;

} // namespace apotheca
