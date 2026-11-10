#include "samples.hpp"

#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

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

bool parseWav(const std::string& bytes, std::vector<float>& out) {
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
    std::vector<float> mono(frames);
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
            sum += v;
        }
        mono[i] = sum / static_cast<float>(channels);
    }
    out = toEngineRate(mono, rate);
    // Cut at ten seconds: fade the last few milliseconds so it does not click.
    if (cut) {
        const std::size_t fade = out.size() < 480 ? out.size() : 480;
        for (std::size_t i = 0; i < fade; ++i) out[out.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
    }
    return true;
}

bool readWav(const std::string& path, std::vector<float>& out) {
    std::string bytes;
    return readTextFile(path, bytes) && parseWav(bytes, out);
}

std::string wavBytes(const std::vector<float>& samples, int sampleRate) {
    std::string s = "RIFF";
    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * 2);
    put32(s, 36 + dataSize);
    s += "WAVEfmt ";
    put32(s, 16);
    put16(s, 1); // whole numbers
    put16(s, 1); // one channel
    put32(s, static_cast<std::uint32_t>(sampleRate));
    put32(s, static_cast<std::uint32_t>(sampleRate) * 2);
    put16(s, 2);
    put16(s, 16);
    s += "data";
    put32(s, dataSize);
    for (float v : samples) {
        if (!(v > -1.f)) v = -1.f; // also NaN
        if (v > 1.f) v = 1.f;
        put16(s, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v * 32767.f))) & 0xFFFF);
    }
    return s;
}

bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate) {
    return writeTextFile(path, wavBytes(samples, sampleRate));
}

std::vector<std::string> wavNames(const std::string& folder) {
    std::vector<std::string> names;
    if (DIR* dir = folder.empty() ? nullptr : opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() <= 4 || file[0] == '.') continue;
            std::string end = file.substr(file.size() - 4);
            for (char& c : end) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            if (end == ".wav") names.push_back(file);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    for (std::string& name : names) name.resize(name.size() - 4);
    return names;
}

} // namespace apotheca
