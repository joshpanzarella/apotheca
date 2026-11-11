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

// The .wav files in a folder, by name, without the ".wav". Those in a folder
// inside it too, one level down, as "folder/name": the loose ones first,
// then each folder's in turn.
std::vector<std::string> wavNames(const std::string& folder);

// A recording's name as a song keeps it: "kick", or "kicks/808" for one in a
// folder. Is this such a name (and not a way out of the samples folder)?
bool sampleNameOk(const std::string& name);
// "kicks/808": the folder ("kicks"; "" for a loose one) and the name by itself ("808").
std::string sampleFolderOf(const std::string& name);
std::string sampleLabel(const std::string& name);
constexpr int kSampleNameRoom = 64; // the longest a name is kept, with its folder and the ending 0

} // namespace apotheca
