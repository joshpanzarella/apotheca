#pragma once
// Recordings for drum lanes: reading and writing WAV files.
//
// Whatever the file holds (8, 16, 24 or 32-bit whole numbers or 32-bit
// floats, any sample rate, any number of channels) comes out the way the
// engine plays it: one channel, 48 kHz, and no longer than ten seconds.

#include <string>
#include <vector>

namespace apotheca {

constexpr float kSampleRate = 48000.f;
constexpr float kMaxSampleSeconds = 10.f;

// False if the file is not there or is not a WAV this can read.
bool readWav(const std::string& path, std::vector<float>& out);
bool parseWav(const std::string& bytes, std::vector<float>& out);

// One channel, 16-bit.
std::string wavBytes(const std::vector<float>& samples, int sampleRate);
bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate);

// The .wav files in a folder, by name, without the ".wav".
std::vector<std::string> wavNames(const std::string& folder);

} // namespace apotheca
