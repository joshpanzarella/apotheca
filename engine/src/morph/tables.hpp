#pragma once
// MORPH's tables (docs/plans/wavetable-plan.md, section 2): a TABLE is a row of FRAMES, each frame one
// cycle of a wave, each kept at morph::kSizes SIZES (one per half octave of pitch: size m holds
// harmonics 1 to sizeHarmonics(m) in sizeLength(m) points, with guard points round it). The voice
// (engine/src/morph.cpp) reads them; tests/morph_test.cpp holds them.
//
// A frame is made from its harmonics (a recipe gives them, or a wave worked out at kRecipePoints
// points and taken apart by an FFT), each size by an inverse FFT of the harmonics it holds. Every
// sum is the table's own (a sine by the house's polynomials, `exp2Poly`, no `sin`, `exp` or `pow`
// from the library), so the handheld makes the desk's tables to the bit. Each frame's top size peaks
// at 1 exactly; no frame has a DC part.
//
// The 16 built-in tables are made once (the first time any is asked for, all of them, under a
// lock), shared, never changed and never freed: an engine may still read one as the program ends.

#include <cstdint>
#include <vector>

#include "apotheca/morph_rule.hpp"

namespace apotheca {
namespace morph {

constexpr int kRecipePoints = 4096; // a wave recipe is worked out at this many points of its cycle

struct Table {
    int frames = 0;
    std::vector<float> data; // frames x frameFloats(): each frame's sizes, size 0 first
    std::vector<float> amp;  // frames x (kTopHarmonics + 1): each harmonic's size as made (0: DC, none)
    // The first point of size m of a frame (its guard is the point before it).
    const float* at(int frame, int m) const;
    float harmonic(int frame, int n) const { return amp[static_cast<std::size_t>(frame) * (kTopHarmonics + 1) + n]; }
};
// Where size m's first point stands in a frame.
long sizeOffset(int m);

// A table from its frames' harmonics: re[f][n] is the cosine part, im[f][n] the sine part of
// harmonic n (1 .. kTopHarmonics; [0] is not read), as frames x (kTopHarmonics + 1).
Table tableFromHarmonics(int frames, const std::vector<double>& cosine, const std::vector<double>& sine);
// A table from a wave worked out at kRecipePoints points of a cycle, frame by frame.
Table tableFromWaves(int frames, double (*wave)(double frameShare, double x));

// The built-in table t (morph::TableKind): made with the rest the first time any is asked for.
const Table& builtInTable(int t);
// All 16 made now (a synth's prepare(): never on the audio thread). Returns the seconds it took
// (0 when they were made already).
double makeBuiltInTables();
// How long they took to make (0 until they are), and whether they are.
double builtInTablesSeconds();
bool builtInTablesMade();

// A sine and a cosine of a number of TURNS, from the house's polynomials (no library call, so
// the same bits on every machine).
double turnSin(double turns);
double turnCos(double turns);

} // namespace morph
} // namespace apotheca
