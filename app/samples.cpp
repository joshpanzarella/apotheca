#include "samples.hpp"

#include <sys/stat.h>

#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "song_file.hpp"

namespace apotheca {

namespace {

std::uint32_t u32(const std::string& s, std::size_t at) {
    return static_cast<std::uint8_t>(s[at]) | static_cast<std::uint8_t>(s[at + 1]) << 8 | static_cast<std::uint8_t>(s[at + 2]) << 16 |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[at + 3])) << 24;
}
std::uint32_t u16(const std::string& s, std::size_t at) { return static_cast<std::uint8_t>(s[at]) | static_cast<std::uint8_t>(s[at + 1]) << 8; }
void put32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 255); }
void put16(std::string& s, std::uint32_t v) { for (int i = 0; i < 2; ++i) s += static_cast<char>((v >> (8 * i)) & 255); }

// To 48 kHz. Four-point (Catmull-Rom) reading between samples: clean enough
// for going up, and for going down from 96 kHz the top octave folds over a
// little, which a drum hides.
std::vector<float> toEngineRate(const std::vector<float>& in, double rate) {
    if (rate == kSampleRate || in.empty()) return in;
    const double ratio = rate / kSampleRate;
    const std::size_t frames = static_cast<std::size_t>(std::floor(static_cast<double>(in.size()) / ratio));
    std::vector<float> out(frames);
    const long last = static_cast<long>(in.size()) - 1;
    const auto at = [&](long i) { return in[static_cast<std::size_t>(i < 0 ? 0 : (i > last ? last : i))]; };
    for (std::size_t i = 0; i < frames; ++i) {
        const double x = static_cast<double>(i) * ratio;
        const long k = static_cast<long>(x);
        const float t = static_cast<float>(x - static_cast<double>(k));
        const float a = at(k - 1), b = at(k), c = at(k + 1), d = at(k + 2);
        out[i] = b + 0.5f * t * (c - a + t * (2.f * a - 5.f * b + 4.f * c - d + t * (3.f * (b - c) + d - a)));
    }
    return out;
}

} // namespace

namespace {
// `stereo`: a file of two channels is read as two (into out and right); else every channel mixed to one.
bool parseWavInto(const std::string& bytes, std::vector<float>& out, std::vector<float>* right) {
    if (bytes.size() < 12 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) return false;
    int format = 0, channels = 0, bits = 0;
    double rate = 0;
    std::size_t data = 0, dataSize = 0;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const std::size_t size = u32(bytes, at + 4), body = at + 8;
        if (bytes.compare(at, 4, "fmt ") == 0 && size >= 16 && body + 16 <= bytes.size()) {
            format = static_cast<int>(u16(bytes, body));
            channels = static_cast<int>(u16(bytes, body + 2));
            rate = u32(bytes, body + 4);
            bits = static_cast<int>(u16(bytes, body + 14));
            // "Extensible": the real format is further in.
            if (format == 0xFFFE && size >= 26 && body + 26 <= bytes.size()) format = static_cast<int>(u16(bytes, body + 24));
        } else if (bytes.compare(at, 4, "data") == 0) {
            data = body;
            dataSize = size > bytes.size() - body ? bytes.size() - body : size; // a file cut short still plays
            break;
        }
        at = body + size + (size & 1); // chunks are padded to an even length
    }
    const bool whole = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool real = format == 3 && bits == 32;
    if (!data || !(whole || real) || channels < 1 || channels > 8 || rate < 4000 || rate > 192000) return false;

    const std::size_t width = static_cast<std::size_t>(bits / 8), frame = width * static_cast<std::size_t>(channels);
    std::size_t frames = dataSize / frame;
    const std::size_t most = static_cast<std::size_t>(kMaxSampleSeconds * rate);
    const bool cut = frames > most;
    if (cut) frames = most;
    const bool two = right && channels == 2; // STEREO: each side kept to itself
    std::vector<float> mono(frames), side(two ? frames : 0);
    for (std::size_t i = 0; i < frames; ++i) {
        float sum = 0.f;
        for (int c = 0; c < channels; ++c) {
            const std::size_t at = data + i * frame + static_cast<std::size_t>(c) * width;
            float v = 0.f;
            if (real) {
                const std::uint32_t raw = u32(bytes, at);
                std::memcpy(&v, &raw, 4);
                if (!(v >= -8.f && v <= 8.f)) v = 0.f; // nonsense, NaN included
            } else if (bits == 8) {
                v = (static_cast<float>(static_cast<std::uint8_t>(bytes[at])) - 128.f) / 128.f;
            } else if (bits == 16) {
                v = static_cast<float>(static_cast<std::int16_t>(u16(bytes, at))) / 32768.f;
            } else if (bits == 24) {
                const std::int32_t raw = static_cast<std::int32_t>(u16(bytes, at) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + 2])) << 16);
                v = static_cast<float>(raw >= 0x800000 ? raw - 0x1000000 : raw) / 8388608.f;
            } else {
                v = static_cast<float>(static_cast<std::int32_t>(u32(bytes, at))) / 2147483648.f;
            }
            if (two && c == 1) side[i] = v;
            else sum += v;
        }
        mono[i] = two ? sum : sum / static_cast<float>(channels);
    }
    out = toEngineRate(mono, rate);
    if (right) *right = two ? toEngineRate(side, rate) : std::vector<float>();
    // Cut at kMaxSampleSeconds (20 s): fade the last few milliseconds so it does not click.
    if (cut) {
        const std::size_t fade = out.size() < 480 ? out.size() : 480;
        for (std::size_t i = 0; i < fade; ++i) out[out.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
        if (right && right->size() == out.size())
            for (std::size_t i = 0; i < fade; ++i) (*right)[right->size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
    }
    return true;
}
} // namespace

bool parseWav(const std::string& bytes, std::vector<float>& out) { return parseWavInto(bytes, out, nullptr); }

bool parseWav(const std::string& bytes, Clip& out) {
    Clip read;
    if (!parseWavInto(bytes, read.left(), &read.right)) return false;
    if (read.right.size() != read.size()) read.right.clear();
    out = std::move(read);
    return true;
}

int wavChannels(const std::string& bytes) {
    if (bytes.size() < 12 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) return 0;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const std::size_t size = u32(bytes, at + 4), body = at + 8;
        if (bytes.compare(at, 4, "fmt ") == 0 && size >= 16 && body + 16 <= bytes.size()) return static_cast<int>(u16(bytes, body + 2));
        at = body + size + (size & 1);
    }
    return 0;
}

bool readWav(const std::string& path, std::vector<float>& out) {
    std::string bytes;
    return readTextFile(path, bytes) && parseWav(bytes, out);
}

bool readWav(const std::string& path, Clip& out) {
    std::string bytes;
    return readTextFile(path, bytes) && parseWav(bytes, out);
}

std::vector<float> Clip::mono() const {
    if (!stereo()) return left();
    std::vector<float> out(size());
    for (std::size_t i = 0; i < size(); ++i) out[i] = ((*this)[i] + right[i]) / 2.f; // (as a two-channel file is read as one)
    return out;
}

bool Clip::oneIfSame() {
    if (right.empty()) return false;
    if (right.size() == size() && right != left()) return false;
    right.clear();
    return true;
}

std::vector<float> Clip::loudest() const {
    if (!stereo()) return left();
    std::vector<float> out(size());
    for (std::size_t i = 0; i < size(); ++i) out[i] = std::fabs((*this)[i]) > std::fabs(right[i]) ? std::fabs((*this)[i]) : std::fabs(right[i]);
    return out;
}

namespace {
void putSample(std::string& s, float v) {
    if (!(v > -1.f)) v = -1.f; // also NaN
    if (v > 1.f) v = 1.f;
    put16(s, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v * 32767.f))) & 0xFFFF);
}
// 16-bit, one or two channels (`right`: the second, as long as `samples`).
std::string wavOf(const std::vector<float>& samples, const std::vector<float>* right, int sampleRate) {
    const std::uint32_t channels = right ? 2 : 1;
    std::string s = "RIFF";
    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * 2 * channels);
    s.reserve(44 + dataSize);
    put32(s, 36 + dataSize);
    s += "WAVEfmt ";
    put32(s, 16);
    put16(s, 1); // whole numbers
    put16(s, channels);
    put32(s, static_cast<std::uint32_t>(sampleRate));
    put32(s, static_cast<std::uint32_t>(sampleRate) * 2 * channels);
    put16(s, 2 * channels);
    put16(s, 16);
    s += "data";
    put32(s, dataSize);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        putSample(s, samples[i]);
        if (right) putSample(s, (*right)[i]);
    }
    return s;
}
} // namespace

std::string wavBytes(const std::vector<float>& samples, int sampleRate) { return wavOf(samples, nullptr, sampleRate); }

std::string wavBytes(const Clip& sound, int sampleRate) { return wavOf(sound.left(), sound.stereo() ? &sound.right : nullptr, sampleRate); }

bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate) {
    return writeTextFile(path, wavBytes(samples, sampleRate));
}

bool writeWav(const std::string& path, const Clip& sound, int sampleRate) {
    return writeTextFile(path, wavBytes(sound, sampleRate));
}

namespace {
// The .wav files directly in a folder (no ending), and the folders in it.
void listFolder(const std::string& folder, std::vector<std::string>& wavs, std::vector<std::string>* folders) {
    DIR* dir = folder.empty() ? nullptr : opendir(folder.c_str());
    if (!dir) return;
    while (const dirent* entry = readdir(dir)) {
        const std::string file = entry->d_name;
        if (file.empty() || file[0] == '.') continue; // hidden things, and "." and ".."
        struct stat info;
        if (stat((folder + "/" + file).c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) {
            if (folders) folders->push_back(file);
            continue;
        }
        if (file.size() <= 4) continue;
        std::string end = file.substr(file.size() - 4);
        for (char& c : end) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        if (end == ".wav") wavs.push_back(file.substr(0, file.size() - 4));
    }
    closedir(dir);
    std::sort(wavs.begin(), wavs.end());
    if (folders) std::sort(folders->begin(), folders->end());
}
} // namespace

std::vector<std::string> wavNames(const std::string& folder) {
    std::vector<std::string> names, folders;
    listFolder(folder, names, &folders);
    for (const std::string& inside : folders) {
        std::vector<std::string> theirs, deeper;
        listFolder(folder + "/" + inside, theirs, &deeper);
        for (const std::string& name : theirs) names.push_back(inside + "/" + name);
        for (const std::string& under : deeper) {
            std::vector<std::string> last;
            listFolder(folder + "/" + inside + "/" + under, last, nullptr); // two levels: a folder under that is not looked into
            for (const std::string& name : last) names.push_back(inside + "/" + under + "/" + name);
        }
    }
    // Only what can be a name (sampleNameOk): one too long to keep was listed, and resting on it put
    // a cut name on the lane, which is no file.
    names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& name) { return !sampleNameOk(name); }), names.end());
    return names;
}

std::vector<std::string> wavFolders(const std::string& folder) {
    std::vector<std::string> wavs, folders, all;
    listFolder(folder, wavs, &folders);
    for (const std::string& inside : folders) {
        if (!sampleNameOk(inside + "/x")) continue;
        all.push_back(inside);
        std::vector<std::string> deeper;
        listFolder(folder + "/" + inside, wavs, &deeper);
        for (const std::string& under : deeper)
            if (sampleNameOk(inside + "/" + under + "/x")) all.push_back(inside + "/" + under);
    }
    return all;
}

bool sampleNameOk(const std::string& name) {
    if (name.empty() || name.size() >= static_cast<std::size_t>(kSampleNameRoom)) return false;
    if (name.find('\\') != std::string::npos || name.find("..") != std::string::npos) return false;
    for (char c : name)
        if (static_cast<unsigned char>(c) < 32 || c == 127) return false;
    // Its parts, between the slashes: three at most (two folders and the name), none empty, none hidden.
    int parts = 0;
    for (std::size_t from = 0;; ++parts) {
        const std::size_t slash = name.find('/', from);
        const std::size_t end = slash == std::string::npos ? name.size() : slash;
        if (end == from || name[from] == '.') return false;
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return parts <= kSampleFolderLevels;
}

std::string sampleFolderOf(const std::string& name) {
    const std::size_t slash = name.rfind('/');
    return slash == std::string::npos ? "" : name.substr(0, slash);
}

std::string sampleLabel(const std::string& name) {
    const std::size_t slash = name.rfind('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

int sampleNote(const std::string& name) {
    // The name by itself: a folder's name is not read ("C Major/Piano" is no C).
    const std::string label = sampleLabel(name);
    static const int semitone[7] = {9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
    int found = -1;
    for (std::size_t i = 0; i < label.size(); ++i) {
        const char c = static_cast<char>(label[i] >= 'A' && label[i] <= 'Z' ? label[i] - 'A' + 'a' : label[i]);
        if (c < 'a' || c > 'g') continue;
        if (i > 0) { // by itself, not in a word ("take2" is no E2)
            const char before = label[i - 1];
            if ((before >= 'A' && before <= 'Z') || (before >= 'a' && before <= 'z')) continue;
        }
        std::size_t at = i + 1;
        int accidental = 0;
        if (at < label.size() && (label[at] == '#' || label[at] == 'b' || label[at] == 'B') && at + 1 < label.size() &&
            (label[at + 1] == '-' || (label[at + 1] >= '0' && label[at + 1] <= '9'))) {
            accidental = label[at] == '#' ? 1 : -1; // (a flat sign, not the note B: only where a number follows it)
            ++at;
        }
        int sign = 1, octave = 0, digits = 0;
        if (at < label.size() && label[at] == '-') {
            sign = -1;
            ++at;
        }
        while (at < label.size() && label[at] >= '0' && label[at] <= '9') {
            octave = octave * 10 + (label[at] - '0');
            ++at;
            ++digits;
        }
        if (!digits) continue;
        const int midi = 12 * (sign * octave + 1) + semitone[c - 'a'] + accidental;
        if (midi >= 0 && midi <= 127) found = midi;
        i = at - 1; // past the whole match
    }
    return found;
}

namespace {

bool letterOrDigit(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
// "12" as 12; -1 if it is not all digits (or too long to be a layer: more than four).
int numberOf(const std::string& word) {
    if (word.empty() || word.size() > 4) return -1;
    int n = 0;
    for (char c : word) {
        if (!digit(c)) return -1;
        n = n * 10 + (c - '0');
    }
    return n;
}

struct LayerWord {
    int rank = -1;
    std::size_t at = 0, length = 0; // where it is spelled in the label (for its stem)
};

LayerWord findLayer(const std::string& label) {
    struct Word { std::string text; std::size_t at; };
    std::vector<Word> words; // its words, in small letters
    for (std::size_t i = 0; i < label.size();) {
        if (!letterOrDigit(label[i])) {
            ++i;
            continue;
        }
        Word w{std::string(), i};
        for (; i < label.size() && letterOrDigit(label[i]); ++i) w.text += static_cast<char>(label[i] >= 'A' && label[i] <= 'Z' ? label[i] - 'A' + 'a' : label[i]);
        words.push_back(w);
    }
    static const char* const kDynamics[] = {"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"}; // 1 to 8
    static const struct { const char* word; int rank; } kWords[] = {{"soft", 2}, {"medium", 5}, {"med", 5}, {"hard", 7}, {"loud", 7}};
    static const char* const kNumbered[] = {"velocity", "layer", "vel", "vl", "v"}; // the longest first
    LayerWord found;
    for (std::size_t k = 0; k < words.size(); ++k) {
        const std::string& t = words[k].text;
        LayerWord here;
        for (int d = 0; d < 8 && k > 0; ++d)
            if (t == kDynamics[d]) here = {d + 1, words[k].at, t.size()};
        for (const auto& w : kWords)
            if (t == w.word) here = {w.rank, words[k].at, t.size()};
        for (const char* prefix : kNumbered) {
            if (here.rank >= 0) break;
            const std::string p(prefix);
            if (t == p && k + 1 < words.size() && numberOf(words[k + 1].text) >= 0) { // "vel 3": the next word's number
                const std::size_t end = words[k + 1].at + words[k + 1].text.size();
                here = {numberOf(words[k + 1].text), words[k].at, end - words[k].at};
                break;
            }
            // "v2", "layer2", or after a note's octave ("a0v16"): never inside a word ("dev2").
            for (std::size_t i = 0; i + p.size() < t.size(); ++i) {
                if (t.compare(i, p.size(), p) != 0 || (i > 0 && !digit(t[i - 1]))) continue;
                const int n = numberOf(t.substr(i + p.size()));
                if (n >= 0) {
                    here = {n, words[k].at + i, t.size() - i};
                    break;
                }
            }
        }
        if (here.rank >= 0) found = here;
    }
    return found;
}

} // namespace

int sampleLayer(const std::string& name) { return findLayer(sampleLabel(name)).rank; }

std::string sampleLayerStem(const std::string& name) {
    std::string label = sampleLabel(name);
    const LayerWord layer = findLayer(label);
    if (layer.rank >= 0) label.erase(layer.at, layer.length);
    std::string stem;
    bool gap = false;
    for (char c : label) {
        if (!letterOrDigit(c)) {
            gap = !stem.empty();
            continue;
        }
        if (gap) stem += ' ';
        gap = false;
        stem += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return stem;
}

} // namespace apotheca
