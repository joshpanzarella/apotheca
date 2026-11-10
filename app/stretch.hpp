#pragma once
// Make a recording longer or shorter without changing its pitch, so a loop
// can be fitted to a number of steps at the song's tempo.
//
// Done ahead of time, never while playing: the result is a new recording
// that the engine plays like any other.
//
// fitTo() is the one to call. It uses Signalsmith Stretch, and for a
// recording too short for that, the simpler method below.
//
// The simpler method (stretchTo): the recording is cut into short overlapping pieces (21 ms), and the
// pieces are laid down closer together or further apart. Each piece is
// slid a little to where it lines up best with the one before, so the join
// does not buzz. (The textbook name is WSOLA.) It suits drums and loops.
// Held notes and voices stretched a long way pick up a flutter; a better
// method for those would be a phase vocoder.

#include <vector>

namespace apotheca {

// `in` as `frames` samples, at the same pitch: the best way there is for it.
std::vector<float> fitTo(const std::vector<float>& in, int frames, float sampleRate);

// By overlapping pieces, as above.
std::vector<float> stretchTo(const std::vector<float>& in, int frames);

// The same job done by Signalsmith Stretch (third_party/signalsmith), which
// works on the sound's frequencies instead of its pieces: clean on held
// notes and voices, softer on the front of a drum. False if the recording is
// too short for it (under about a tenth of a second); `out` is then no use.
bool stretchSmooth(const std::vector<float>& in, int frames, float sampleRate, std::vector<float>& out);

} // namespace apotheca
