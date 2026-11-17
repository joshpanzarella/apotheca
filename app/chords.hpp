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
#include <utility>
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

// The packs that come with the program: assets/chords, compiled in (app/chord_packs.inc, written
// from those files by tools/chord_packs.py), so an update over WiFi brings them; until 2026-11-17
// only the zip did. By name. chord_test fails an .inc that is not the files.
const std::vector<ChordPack>& shippedPacks();
const std::vector<std::string>& shippedPackTexts(); // each as compiled in (for the tests)

// The built-in pack, then the shipped packs and every pack in the folder, by name. A file in
// the folder whose pack has a shipped pack's name takes that one's place (one pack to a name).
std::vector<ChordPack> loadChordPacks(const std::string& folder);

// Everything a pack says about its chords, as written: for the tests that hold every shipped
// chord to its quality, its scale and its name (chord_test). Apotheca itself reads a ChordPack.
struct PackFacts {
    struct Chord {
        std::string id, quality, function, legacyName, borrowedFrom;
        int degree = -1, alteration = 0, preferredVoices = 0, pedal = -1;
        bool hasOutside = false;
        std::vector<int> intervals, required, outside;
    };
    struct Progression {
        std::string id;
        std::vector<std::pair<std::string, int>> segments; // chord id, length in ticks
    };
    int schemaVersion = 0, tonic = -1;
    std::string id, name, scaleId, description;
    std::vector<int> scale;
    std::vector<Chord> chords;
    std::vector<Progression> progressions;
};
bool readPackFacts(const std::string& json, PackFacts& facts); // false: not JSON, or no chords

// A chord's name moved `by` semitones: a name that starts with a key (C, F#, Bb...) takes the key it
// moves to, as the packs spell keys (C C# D Eb E F F# G Ab A Bb B); so does one after a "/" (a slash
// chord's bass). Any other name (a roman numeral) is left as it is. TRANSPOSE and SCALE use it.
void transposeChordName(char name[16], int by);

// A chord from a shape, with its root on the key nearest to `nearPitch`.
ChordEvent makeChord(const ChordShape& shape, int nearPitch, int step, float length, float velocity, int inversion);

} // namespace apotheca
