#pragma once
// Recordings for drum lanes: reading and writing WAV files.
//
// Whatever the file holds (8, 16, 24 or 32-bit whole numbers or 32-bit
// floats, 4 kHz to 192 kHz, 1 to 8 channels: parseWav) comes out the way the
// engine plays it: 48 kHz, and no longer than kMaxSampleSeconds (twenty seconds); into a
// std::vector<float> as one channel (both sides mixed to one, as before STEREO), into a Clip
// as the file has it: one channel, or two (STEREO, 2026-11-28; 3 to 8 are mixed to one).

#include <string>
#include <utility>
#include <vector>

namespace apotheca {

constexpr float kSampleRate = 48000.f;
constexpr float kMaxSampleSeconds = 20.f; // a recording read from a file (owner, 2026-11-18: it was 10)

// A recording in memory as its file has it (STEREO, 2026-11-28): the samples themselves are a mono
// recording, or the LEFT side of a stereo one, whose right side is `right` (as long). `right` empty: mono.
// Whatever takes a std::vector<float> takes a Clip as its left side alone: what must hear both asks for
// `right` (or mono()).
struct Clip : std::vector<float> {
    std::vector<float> right;
    Clip() = default;
    explicit Clip(std::vector<float> left) : std::vector<float>(std::move(left)) {}
    Clip(std::vector<float> left, std::vector<float> rightSide) : std::vector<float>(std::move(left)), right(std::move(rightSide)) {
        if (right.size() != size()) right.clear(); // (two sides of different lengths are no stereo recording)
    }
    bool stereo() const { return !right.empty() && right.size() == size(); }
    const std::vector<float>& left() const { return *this; }
    std::vector<float>& left() { return *this; }
    // Both sides mixed to one, as a mono file of the two would be read ((L + R) / 2); a mono one as it is.
    std::vector<float> mono() const;
    // The louder of the two sides, sample by sample (|L| or |R|): what a picture, TRIM SILENCE and a
    // peak are taken from. A mono one: its own size.
    std::vector<float> loudest() const;
};

// False if the file is not there or is not a WAV this can read.
bool readWav(const std::string& path, std::vector<float>& out);
bool parseWav(const std::string& bytes, std::vector<float>& out);
// As the file has it: two channels stay two (STEREO), one is one, 3 to 8 mixed to one. A mono file
// gives the same samples to the bit as the std::vector<float> forms.
bool readWav(const std::string& path, Clip& out);
bool parseWav(const std::string& bytes, Clip& out);
// The channels the file says it has (0: not a WAV this can read), without reading its sound.
int wavChannels(const std::string& bytes);

// One channel, 16-bit.
std::string wavBytes(const std::vector<float>& samples, int sampleRate);
bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate);
// As the Clip is: two channels for a stereo one (16-bit, interleaved), one for a mono one (the same
// bytes as above).
std::string wavBytes(const Clip& sound, int sampleRate);
bool writeWav(const std::string& path, const Clip& sound, int sampleRate);

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
// A multisample file's velocity layer from its name (owner, 2026-11-24: velocity layers), as a number to sort by,
// softest first; -1: none spelled. A word by itself: a dynamic, PPP 1, PP 2, P 3, MP 4, MF 5, F 6, FF 7, FFF 8 (never
// the name's first word: "P C3" is a P's C3), SOFT 2, MEDIUM or MED 5, HARD or LOUD 7; or V, VL, VEL, VELOCITY or LAYER
// with a number, by itself or after it ("v2", "vel 3", "layer2") or straight after a note ("A0v16"): the number. The
// last one spelled counts; a folder's name is not read.
int sampleLayer(const std::string& name);
// The name by itself with its layer word taken out, in small letters, its words one space apart ("Snare_HARD" and
// "snare soft" are both "snare"): the files that are layers of one sound.
std::string sampleLayerStem(const std::string& name);
constexpr int kSampleNameRoom = 64; // the longest a name is kept, with its folder and the ending 0

} // namespace apotheca
