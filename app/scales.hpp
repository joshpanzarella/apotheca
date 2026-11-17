#pragma once
// SCALE LOCK (easywins batch 4): the song's KEY and SCALE (Project::scaleKey, scaleKind; the SET page's
// KEY and SCALE rows). With a scale on, the piano roll's cursor steps only through the scale's keys
// (and keys a note of the bar shown is on), the keys out of it are drawn dim, a note carried sideways
// goes to the next key of the scale, and the chord in hand is the pack's moved into the key.
//
// The kinds are in song files (`scale KEY KIND`, left out at OFF): appended, never reordered.

namespace apotheca {

enum ScaleKind {
    kScaleOff, kScaleMajor, kScaleMinor, kScaleDorian, kScalePhrygian, kScaleLydian, kScaleMixolydian, kScaleLocrian,
    kScaleMajorPent, kScaleMinorPent, kScaleHarmonicMinor, kScaleBlues, kScaleKinds
};

// What the SET page reads (the longest, HARMONIC MINOR, is checked at XL by textsize).
inline const char* scaleName(int kind) {
    static const char* const names[kScaleKinds] = {"OFF", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "LOCRIAN",
                                                   "MAJOR PENT", "MINOR PENT", "HARMONIC MINOR", "BLUES"};
    return kind >= 0 && kind < kScaleKinds ? names[kind] : names[0];
}
inline const char* keyName(int key) {
    static const char* const names[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return names[((key % 12) + 12) % 12];
}
// A kind nobody knows (a later program's) is OFF: the roll is free.
inline int scaleKindOf(int number) { return number >= 0 && number < kScaleKinds ? number : kScaleOff; }

// The scale's keys above its KEY, a bit each (bit 0 the key itself).
inline unsigned scaleMask(int kind) {
    static const unsigned masks[kScaleKinds] = {
        0xFFFu,
        (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 11),  // MAJOR
        (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 10),  // MINOR
        (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 10),  // DORIAN
        (1u << 0) | (1u << 1) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 10),  // PHRYGIAN
        (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 11),  // LYDIAN
        (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 10),  // MIXOLYDIAN
        (1u << 0) | (1u << 1) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 8) | (1u << 10),  // LOCRIAN
        (1u << 0) | (1u << 2) | (1u << 4) | (1u << 7) | (1u << 9),                           // MAJOR PENT
        (1u << 0) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 10),                          // MINOR PENT
        (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 11),  // HARMONIC MINOR
        (1u << 0) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10),              // BLUES
    };
    return masks[scaleKindOf(kind)];
}
inline bool inScale(int key, int kind, int pitch) { return (scaleMask(kind) >> ((((pitch - key) % 12) + 12) % 12)) & 1u; }
// A scale with a minor third and no major one: a pack's chords are moved to its relative major's key (+3).
inline bool minorScale(int kind) { return kind != kScaleOff && (scaleMask(kind) & (1u << 3)) && !(scaleMask(kind) & (1u << 4)); }

// How far a chord pack (its tonic, and whether its scale is minor) is moved to sit in the song's
// KEY and SCALE: the tonics lined up, and a major pack moved to a minor scale's relative major (and a
// minor pack to a major scale's relative minor), so its chords keep their places in the scale. -5..6.
inline int packShift(int packTonic, bool packMinor, int key, int kind) {
    if (kind == kScaleOff) return 0;
    int to = key;
    if (minorScale(kind) && !packMinor) to = key + 3;
    if (!minorScale(kind) && packMinor) to = key - 3;
    int by = (((to - packTonic) % 12) + 12) % 12;
    return by > 6 ? by - 12 : by;
}

} // namespace apotheca
