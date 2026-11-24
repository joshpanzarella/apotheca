#pragma once
// Recordings for drum lanes: reading and writing WAV files.
//
// Whatever the file holds (8, 16, 24 or 32-bit whole numbers or 32-bit
// floats, any sample rate, any number of channels) comes out the way the
// engine plays it: one channel, 48 kHz, and no longer than kMaxSampleSeconds (twenty seconds).

#include <string>
#include <vector>

namespace apotheca {

constexpr float kSampleRate = 48000.f;
constexpr float kMaxSampleSeconds = 20.f; // a recording read from a file (owner, 2026-11-18: it was 10)

// False if the file is not there or is not a WAV this can read.
bool readWav(const std::string& path, std::vector<float>& out);
bool parseWav(const std::string& bytes, std::vector<float>& out);

// One channel, 16-bit.
std::string wavBytes(const std::vector<float>& samples, int sampleRate);
bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate);

// The .wav files in a folder, by name, without the ".wav". Those in a folder
// inside it too, and in a folder inside that (two levels down and no further),
// as "folder/name" and "folder/inner/name": the loose ones first, then each
// folder's in turn, its own before its inner folders'.
std::vector<std::string> wavNames(const std::string& folder);
// The folders there, the empty ones too: "kicks", then "kicks/long", in order.
// Not the hidden ones, nor one whose name a recording's could not carry.
std::vector<std::string> wavFolders(const std::string& folder);

// A recording's name as a song keeps it: "kick", "kicks/808" for one in a
// folder, "drums/kicks/808" for one in a folder in a folder (two levels: owner,
// 2026-11-14). Is this such a name (and not a way out of the samples folder)?
constexpr int kSampleFolderLevels = 2;
bool sampleNameOk(const std::string& name);
// "drums/kicks/808": the folder, all of it ("drums/kicks"; "" for a loose one), and the name by itself ("808").
std::string sampleFolderOf(const std::string& name);
std::string sampleLabel(const std::string& name);
// A multisample file's note from its name ("Piano/C2" goes at C): the last note spelled in the
// name by itself ("C2", "C#3", "Db3", any case), C4 as 60. -1: none there, or past the keys.
int sampleNote(const std::string& name);
constexpr int kSampleNameRoom = 64; // the longest a name is kept, with its folder and the ending 0

} // namespace apotheca
