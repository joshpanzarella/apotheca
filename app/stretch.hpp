#pragma once
// Make a recording longer or shorter without changing its pitch, so a loop
// can be fitted to a number of steps at the song's tempo.
//
// Done ahead of time, never while playing: the result is a new recording
// that the engine plays like any other.
//
// How: the recording is cut into short overlapping pieces (21 ms), and the
// pieces are laid down closer together or further apart. Each piece is
// slid a little to where it lines up best with the one before, so the join
// does not buzz. (The textbook name is WSOLA.) It suits drums and loops.
// Held notes and voices stretched a long way pick up a flutter; a better
// method for those would be a phase vocoder.

#include <vector>

namespace apotheca {

// `in` as `frames` samples, at the same pitch.
std::vector<float> stretchTo(const std::vector<float>& in, int frames);

} // namespace apotheca
