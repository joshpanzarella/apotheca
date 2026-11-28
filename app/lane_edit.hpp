#pragma once
// EDIT: what a drum lane does to its sound, without touching the file (owner, 2026-11-16: "it
// would be nice if the editing only affected the sample in the project, not the raw file").
//
// The file on the card is read and never written. The lane plays a copy made from it here, on the
// app thread, whenever an edit changes: START and END cut it, REVERSE turns it round, PITCH and FINE
// play it faster or slower (and so shorter or longer), FADE IN and FADE OUT shape its ends, and GAIN
// sets its level. The copy goes to the engine like any recording (the old one through the trash
// queue), so FIT stretches the edited sound, slices cut it, and a hit that carries the lane's sound on
// carries the edited one. A lane with every edit at rest plays the file as it always did, to the bit.
//
// The built-in drums take the same edits: theirs is the kit's own sound, copied.
//
// In what order, as the sound is made (makeShape, then FIT if the lane is fitted, then makeLevel):
//   1. START to END of the file, each cut edge faded over kEdgeMs (a cut never clicks; an edge that is
//      the file's own start or end is left as it is)
//   2. REVERSE: the cut part, end first
//   3. PITCH and FINE: resampled (a windowed sinc, so going up does not fold back), the length
//      divided by the speed
//   (FIT: stretched to its steps, at the same pitch)
//   4. FADE IN and FADE OUT, in ms of the sound as it is heard
//   5. GAIN, then the guard: from kKnee (0.9, -0.9 dB) up a sample is rounded softly into full scale
//      and never passes it. So GAIN past full scale flattens the peaks; it never clips hard.
// No SDL here: tests/laneedit_test.cpp holds it to sounds made by hand.

#include <vector>

#include "samples.hpp"

namespace apotheca {

constexpr int kEditUnits = 100000;                 // START and END: parts of the file, 0..kEditUnits
constexpr int kGainMost = 240;                     // GAIN: tenths of a dB, -kGainMost..kGainMost
constexpr int kGainStep = 5;                       // the D-pad: half a dB
constexpr int kPitchMost = 24;                     // PITCH: semitones either way
constexpr int kFineMost = 50;                      // FINE: cents either way
constexpr int kFadeStops[] = {0, 1, 2, 3, 5, 10, 15, 20, 30, 50, 75, 100, 150, 200, 300, 500, 750, 1000, 1500, 2000}; // FADE IN / OUT: ms
constexpr int kFadeStopCount = sizeof kFadeStops / sizeof kFadeStops[0];
constexpr int kFadeMost = 2000;
constexpr float kEdgeMs = 1.f;                     // a cut edge always fades over this, under any FADE
constexpr float kLeastSpanMs = 5.f;                // START and END are never closer than this
constexpr float kKnee = 0.9f;                      // the guard: untouched below, rounded into full scale above
constexpr float kNormalDb = -1.f;                  // NORMALIZE: the loudest sample lands here
constexpr float kSilenceDb = -40.f;                // TRIM SILENCE: under the loudest by this much is silence
constexpr float kSilenceLeadMs = 1.f;              // ...and START stands this far ahead of the first sound

// Everything is a whole number, so the struct has no bytes that are not set (undo hashes it whole) and
// two edits compare as the song file writes them.
struct LaneEdit {
    int start = 0, end = kEditUnits; // the part of the file played
    int gain = 0;                    // tenths of a dB
    int fadeIn = 0, fadeOut = 0;     // ms
    int reverse = 0;                 // 1: backwards
    int pitch = 0;                   // semitones
    int fine = 0;                    // cents
    int mono = 0;                    // 1: a stereo sound's two sides mixed to one (STEREO, 2026-11-28; appended). 0: as the file is
    bool operator==(const LaneEdit& o) const {
        return start == o.start && end == o.end && gain == o.gain && fadeIn == o.fadeIn && fadeOut == o.fadeOut && reverse == o.reverse && pitch == o.pitch && fine == o.fine &&
               mono == o.mono;
    }
    bool operator!=(const LaneEdit& o) const { return !(*this == o); }
    bool atRest() const { return *this == LaneEdit(); }
    // START, END, REVERSE, PITCH, FINE or MONO: what changes which samples are played, and FIT is worked from.
    bool shapes() const { return start != 0 || end != kEditUnits || reverse != 0 || pitch != 0 || fine != 0 || mono != 0; }
    // The level as the shape has it: nothing but MONO (or nothing) is on, so the sound is the shape itself, with no
    // fades, GAIN or guard (two sides mixed to one are never louder than the louder of them).
    bool levelAtRest() const {
        LaneEdit e = *this;
        e.mono = 0;
        return e.atRest();
    }
    float speed() const;             // PITCH and FINE as a speed: 2 an octave up
};

// The rows of the edit view, in the order the page shows them (and the ids the tests use).
// kEditRows is the eight boxes; MONO (STEREO, 2026-11-28) is a stop of its own after them, on the sound's box
// (edit_view.hpp), and a field of its own on the designer page: a ninth "row" the functions below take too.
enum LaneEditRow { kEditStart, kEditEnd, kEditFadeIn, kEditFadeOut, kEditGain, kEditPitch, kEditFine, kEditReverse, kEditRows };
constexpr int kEditMono = kEditRows;
const char* laneEditName(int row);
int laneEditValue(const LaneEdit& e, int row);
LaneEdit laneEditWith(LaneEdit e, int row, int value); // that row set (not yet held to its limits)
// Held to every limit: each row to its range, END after START by kLeastSpanMs of a file `seconds` long
// (START gives way first where both cannot), FADE to its stops' range.
LaneEdit laneEditHeld(LaneEdit e, float seconds);
// The least gap between START and END, in kEditUnits, for a file this long: at least one unit.
int leastSpan(float seconds);
// What a row reads (START and END as a time into the file).
void laneEditText(const LaneEdit& e, int row, float seconds, char* out, int size);
// One step of a row by the D-pad (by: +1 / -1): START and END by a millisecond of the file (a unit at
// least), GAIN by half a dB, FADE by its stops, PITCH and FINE by one, REVERSE turned.
LaneEdit laneEditStepped(const LaneEdit& e, int row, int by, float seconds);
// FADE's place among its stops: the last at or under `ms` (the stick moves FADE by stops).
int fadeStopPlace(int ms);
// A row's whole range, in its own numbers, for the stick (FADE: its stops, in stops).
int laneEditRange(int row);

// For the picture of the file: how loud the edit makes the point `at` of it (0..1), against the file as
// it is: GAIN and the fades as heard (turned round, faster or slower). 1 outside START to END.
float laneEditLevelAt(const LaneEdit& e, float at, float seconds);
// ...and where in the file the sound has got to when `part` (0..1) of what the lane plays has gone by.
float laneEditPlaceOf(const LaneEdit& e, float part);

// 1 to 3: the shape (START to END, REVERSE, PITCH). Returns `in` as it is when nothing shapes it. (MONO is not
// this one's: a std::vector<float> has one side.)
std::vector<float> makeShape(const std::vector<float>& in, const LaneEdit& e, float sampleRate);
// A recording as its file has it (STEREO): MONO first (a stereo one's sides mixed to one), then each side shaped as
// above with the same edit, so the two stay in line. A mono one is the samples above's, to the bit.
Clip makeShape(const Clip& in, const LaneEdit& e, float sampleRate);
// About how much work makeShape is for a file of n samples (taps worked out), and how much the app
// does in a frame before it hands a shape to the helper thread (about 1.5 ms on the desk, 20 to 30 on
// the handheld): a half-second hit pitched anywhere, a 10 s file cut or turned round, are made at once.
double shapeCost(std::size_t n, const LaneEdit& e);
constexpr double kShapeInAFrame = 2.0e6;
// 4 and 5: the fades, GAIN and the guard, from `in` into `out` (n samples each; they may be one).
void makeLevel(const float* in, int n, const LaneEdit& e, float sampleRate, float* out);
// Both sides of a Clip (out is made as long, and stereo if `in` is).
void makeLevel(const Clip& in, const LaneEdit& e, float sampleRate, Clip& out);
// The guard by itself (5).
float guarded(float x);
// NORMALIZE: the GAIN (tenths of a dB) that puts the loudest of these samples at kNormalDb, held to
// GAIN's range. False for silence: nothing to normalize.
bool normalGain(const float* in, int n, int& gain);
bool normalGain(const Clip& in, int& gain); // the louder side's loudest sample: both sides together

// TRIM SILENCE: START and END of a file at its first and last sound (kSilenceDb under its loudest,
// START kSilenceLeadMs ahead). False for silence.
bool silenceEnds(const std::vector<float>& file, float sampleRate, int& start, int& end);
bool silenceEnds(const Clip& file, float sampleRate, int& start, int& end); // on the louder side at each sample

} // namespace apotheca
