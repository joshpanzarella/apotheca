#pragma once
// MORPH: a wavetable synth (docs/plans/wavetable-plan.md). The rules and figures are morph_rule.hpp's;
// the tables are engine/src/morph/tables.hpp's.
//
// A key plays two table oscillators, A and B (B may be OFF), and a SUB sine an octave under A. Each
// oscillator reads its TABLE at POSITION: the two frames either side of the place, each read straight
// between two points at the stream's phase, crossfaded straight. UNISON gives each oscillator 1 to 4
// streams, spread in pitch by DETUNE and in the sides by WIDTH. Then the effects' state-variable filter
// (fx/svf.hpp's arithmetic) and the AMP envelope; the MOD ENV moves POSITION and CUTOFF.
//
//   Keys      6 sounding, and 6 more slots that only fade out (5 ms): a stolen key, a key struck
//             again, every key at allNotesOff (Play, panic).
//   Streams   a pool of 24 for the track: a key takes UNISON x (B on ? 2 : 1) at its strike; one that
//             finds too few steals the oldest let-go key, then the oldest. TABLE A, TABLE B and UNISON
//             are read at the strike.
//   Tick      every 16 samples counted from the key's strike: POSITION, the pitch, the size each
//             stream reads, the filter's numbers and the gains are worked out for the tick's end, and
//             slid to sample by sample. Every setting (the patch with the macros on it) is slid in
//             over 512 samples counted from when it changed (a MOD on POSITION is a line).
//
// So the sound does not hang on how it is cut into blocks; the only rolls are the unison streams'
// starts (noteSeed), so Play from the top is the same sound.

#include <cstdint>
#include <memory>

#include "instrument.hpp"
#include "morph_rule.hpp"

namespace apotheca {

namespace morph {
constexpr int kTick = 16;               // samples between two workings-out, counted from the strike
constexpr double kFadeSeconds = 0.005;  // a stolen key, and every key at allNotesOff
constexpr float kOutputGain = 0.25f;    // SELF: a frame peaks at 1; a chord of four at LEVEL 0 peaks near 0.9
} // namespace morph

struct MorphPatch {
    int value[morph::kSettingRoom] = {};
};

class Morph : public Instrument {
public:
    Morph();
    ~Morph() override;

    void prepare(float sampleRate) override; // makes the 16 built-in tables (once): not for the audio thread

    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index); // the folder
    static MorphPatch presetPatch(int index);     // 0: Analog Sweep, the new track's sound

    void selectPreset(int index) override;
    void setSetting(int id, int value) override;
    void setMacro(int macro, float value) override; // stores; slid in over 512 samples from the next render
    void noteSeed(std::uint32_t seed) override;
    void noteOn(int note, float velocity) override;
    void noteOnFrom(int note, float velocity, int from, float seconds) override;
    void noteOff(int note) override;
    void allNotesOff() override; // Play, panic: every key fades out over 5 ms
    void stopNotes() override;   // Stop: every key let go (RELEASE)
    void render(float* left, float* right, int frames) override;
    // The picture's readout (CLOUD's path): each sounding key's place in TABLE A as a share of the
    // table (0..1), newest first, then the same keys' places in TABLE B (-1 where B is OFF). The count
    // is twice the keys.
    int grainPlaces(float* places, int most) const override;

    // For the tests and the timings.
    int keysSounding() const;    // keys on, fading ones too
    long strikes() const;        // keys struck since it was made
    bool sounds(int note) const; // a key of that note on and not fading
    int streamsInUse() const;    // the pool's streams the keys not fading hold
    float smallestState() const; // the smallest envelope, fade or filter state held (not 0): 0 if none
    static double tablesSeconds(); // how long the built-in tables took to make (0 if not yet)

    // The built-in tables, for the app: made now (the app makes them on a helper thread as the
    // program starts, so the first MORPH track does not wait: not for the audio thread); whether
    // they are made; a table's frames; and its wave at a place along it (frames, 0 .. frames - 1:
    // the two either side crossfaded, as a key reads them) and a place in its cycle (x, 0 .. 1), read
    // from the largest size straight between points. tableRead gives 0 until the tables are made.
    static void makeTables();
    static bool tablesReady();
    static int tableFrames(int table);
    static float tableRead(int table, double place, double x);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace apotheca
