#pragma once
// Chord packs: sets of chords that belong together, to build a part from
// quickly. A pack is a JSON file in the same form Shapetaker's Transmutation
// reads, so one file works in both.
//
// What is read from a pack:
//   name                  shown on screen
//   sourceTonic           the key, 0 = C ... 11 = B
//   scale.pitchClasses    the scale, as semitones above the key
//   chords[]              each with
//     root.degree         which note of the scale the chord is built on
//     root.alteration     semitones to move that by
//     intervals           the chord's notes, as semitones above its root
//     legacyName          its name; or made from the root and `quality`
// Everything else in the file is ignored.

#include <string>
#include <vector>

#include "project.hpp"

namespace apotheca {

struct ChordShape {
    char name[16] = {};
    int rootKey = 0; // 0 = C ... 11 = B
    int count = 0;
    int intervals[kChordNotes] = {};
};

struct ChordPack {
    std::string name;
    std::vector<ChordShape> chords;
};

// False if the text is not a pack, or has no usable chord in it. A chord
// that makes no sense is left out; the rest of the pack still loads.
bool parseChordPack(const std::string& json, ChordPack& pack);

// The pack that is always there: the plain chords of C major.
const ChordPack& builtInPack();

// The built-in pack, then every pack in the folder, by name.
std::vector<ChordPack> loadChordPacks(const std::string& folder);

// A chord from a shape, with its root on the key nearest to `nearPitch`.
ChordEvent makeChord(const ChordShape& shape, int nearPitch, int step, float length, float velocity, int inversion);

} // namespace apotheca
