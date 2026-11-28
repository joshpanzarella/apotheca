#pragma once
// CLOUD: clouds of grains from a recording (docs/plans/granular-plan.md).
//
// A key is a cloud at its pitch: it starts a grain every 1 / DENSITY seconds (SCATTER rolls the
// gap), each a piece of the recording SIZE long, from POSITION (moved on by SCAN) give or take
// SPRAY, at the key's pitch give or take PITCH SPRAY, on a side within STEREO, backwards by
// REVERSE's chance. The rules and figures are cloud_rule.hpp's.
//
//   Keys     6 sounding, and 6 more slots that only fade out (5 ms): a stolen key, or every key at
//            allNotesOff (Play, panic). The same key struck again is the same slot: its grains go
//            on and its count starts again. A key is free when its envelope is done; its grains
//            end with it.
//   Grains   one pool of kPool (24) for the track, at most 12 a key. A grain that finds no room is not
//            started (the cloud thins); its number is still counted, so the rolls after it are the
//            same. A grain once started never changes, and is never cut but by its key.
//   Reads    4-point Hermite between samples; at the recording's own pitch (no PITCH SPRAY, no
//            TUNE, no glide) the start is rounded and whole samples are read. The recording has no
//            edges: a grain past one comes in from the other.
//
// Every onset is a whole number of samples counted from the key's strike, every setting is read at
// an onset, and the sum is added up in the pool's order sample by sample, so the sound does not
// hang on how it is cut into blocks. Rolls are by the strike's seed (noteSeed) and the grain's
// number: Play from the top is the same cloud.

#include <cstdint>
#include <memory>
#include <vector>

#include "cloud_rule.hpp"
#include "instrument.hpp"

namespace apotheca {

namespace cloud {
constexpr int kKeys = 6;            // sounding (the plan's call 2: one constant)
constexpr int kKeySlots = 2 * kKeys; // every key may be fading out while six new ones sound
constexpr int kPool = 24;           // grains for the track (call 3 said 32; the plan's fourth cut, measured: see As built)
constexpr int kPiece = 64;          // samples worked at a time (the keys' envelopes)
constexpr double kFadeSeconds = 0.005; // a stolen key and its grains, and every key at allNotesOff
constexpr float kCloudGain = 1.f;   // SELF: a lone grain at its peak, in the middle, is 0.71 of the recording a side
constexpr double kToneSeconds = 2.0; // the built-in TONE, when the track has no recording
} // namespace cloud

struct CloudPatch {
    int value[cloud::kSettingRoom] = {};
};

class Cloud : public Instrument {
public:
    Cloud();
    ~Cloud() override;

    void prepare(float sampleRate) override; // makes (or finds) the TONE: not for the audio thread

    static int presetCount();
    static const char* presetName(int index);
    static const char* presetCategory(int index); // the folder
    static CloudPatch presetPatch(int index);     // 0: Soft Cloud, the new track's sound

    void selectPreset(int index) override;
    void setSetting(int id, int value) override;
    void setMacro(int macro, float value) override; // stores; read at the next grain
    void noteSeed(std::uint32_t seed) override;
    void noteOn(int note, float velocity) override;
    void noteOnFrom(int note, float velocity, int from, float seconds) override;
    void noteOff(int note) override;
    void allNotesOff() override; // Play, panic: every key fades out over 5 ms
    void stopNotes() override;   // Stop: every key let go (RELEASE; its grains go on under it)
    void setSample(const float* data, int length, const float* right = nullptr) override; // every grain ends at once; `right`: a stereo one's right side
    void render(float* left, float* right, int frames) override;
    int grainPlaces(float* places, int most) const override;

    // The built-in TONE at a sample rate: made once, shared, never changed (as DrumKit::builtIn).
    static const std::vector<float>& tone(float sampleRate);

    // For the tests and the timings.
    int keysSounding() const;      // keys on, fading ones too
    int grainsSounding() const;
    long grainsStarted() const;    // since made
    long grainsSkipped() const;    // found no room
    bool onTone() const;           // no recording: the TONE plays
    // The sample (counted since made) each grain started on and where it read from, the last
    // `most` started: for the tests of timing and place.
    struct Started { long at = 0; double place = 0, step = 1; int key = -1; long length = 0; };
    int lastStarted(Started* out, int most) const;
    float smallestState() const;   // the smallest envelope or fade level held (not 0): 0 if none

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace apotheca
