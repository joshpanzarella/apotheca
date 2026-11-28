#include "song_file.hpp"
#include "scales.hpp"
#include "line_in.hpp"
#include "instruments.hpp"
#include "samples.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <type_traits>

#include "mods.hpp"
#include "synth_spec.hpp"

namespace apotheca {

// ---- numbers as snprintf writes them (speed plan batch 4) -------------------------------------
// snprintf("%.6g") was 65% of writing a song's text. These write the same bytes by exact integer
// arithmetic: the value is m * 2^e (m a whole number of 53 bits at most), so a * 10^s is a fraction
// of two whole numbers that fit 128 bits for every s the song's numbers need, and its rounding
// (glibc's: to nearest, a half to the even digit, on the exact value) is decided exactly. Where
// they would not fit (under about 1e-22, over about 1e38, infinite, not a number, a %.0f of a
// number with a fraction) snprintf writes it, as before.
namespace {
using Wide = unsigned __int128;

constexpr std::uint64_t kTens[20] = {1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull, 100000000ull,
    1000000000ull, 10000000000ull, 100000000000ull, 1000000000000ull, 10000000000000ull, 100000000000000ull,
    1000000000000000ull, 10000000000000000ull, 100000000000000000ull, 1000000000000000000ull, 10000000000000000000ull};

int bitsOf(Wide v) {
    const std::uint64_t high = static_cast<std::uint64_t>(v >> 64), low = static_cast<std::uint64_t>(v);
    return high ? 128 - __builtin_clzll(high) : (low ? 64 - __builtin_clzll(low) : 0);
}

constexpr std::uint64_t kFives[28] = {1ull, 5ull, 25ull, 125ull, 625ull, 3125ull, 15625ull, 78125ull, 390625ull, 1953125ull,
    9765625ull, 48828125ull, 244140625ull, 1220703125ull, 6103515625ull, 30517578125ull, 152587890625ull, 762939453125ull,
    3814697265625ull, 19073486328125ull, 95367431640625ull, 476837158203125ull, 2384185791015625ull, 11920928955078125ull,
    59604644775390625ull, 298023223876953125ull, 1490116119384765625ull, 7450580596923828125ull};

// floor(a * 10^s) into `whole`, and how the rest stands against a half (-1 under, 0 a half exactly,
// 1 over). False where the numbers would not fit. `a` is finite and over 0.
bool tensOf(double a, int s, std::uint64_t& whole, int& rest) {
    if (s > 27 || s < -27) return false;
    int e = 0;
    const double f = std::frexp(a, &e);                                   // a = f * 2^e, f in [0.5, 1)
    const std::uint64_t m = static_cast<std::uint64_t>(std::ldexp(f, 53)); // exact: f has 53 bits at most
    e -= 53;                                                              // a = m * 2^e
    const Wide five = kFives[s < 0 ? -s : s];
    Wide num = static_cast<Wide>(m) * (s > 0 ? five : 1);
    const int q = e + s; // a * 10^s = num * 2^q / (s < 0 ? five : 1)
    Wide w, r, den;
    if (s >= 0) { // a power of two below: shifts
        if (q >= 0) {
            if (bitsOf(num) + q > 126) return false;
            w = num << q;
            r = 0;
            den = 1;
        } else {
            const int shift = -q;
            if (shift > 126) return false;
            w = num >> shift;
            r = num & ((static_cast<Wide>(1) << shift) - 1);
            den = static_cast<Wide>(1) << shift;
        }
    } else {
        den = five;
        if (q >= 0) {
            if (bitsOf(num) + q > 126) return false;
            num <<= q;
        } else {
            if (bitsOf(den) - q > 126) return false;
            den <<= -q;
        }
        w = num / den;
        r = num % den;
    }
    if (w >> 64) return false;
    whole = static_cast<std::uint64_t>(w);
    const Wide twice = r << 1;
    rest = twice < den ? -1 : (twice == den ? 0 : 1);
    return true;
}

int digitsInto(char* to, std::uint64_t v) { // v's decimal digits, the first not 0 (v > 0)
    char back[24];
    int n = 0;
    while (v) back[n++] = static_cast<char>('0' + v % 10), v /= 10;
    for (int i = 0; i < n; ++i) to[i] = back[n - 1 - i];
    return n;
}

// "%.Pg" of a finite v other than 0, P 1..17. 0 where tensOf could not.
std::size_t gText(char* out, double v, int p) {
    const double a = v < 0 ? -v : v;
    int e2 = 0;
    std::frexp(a, &e2);
    int x = static_cast<int>(std::floor((e2 - 1) * 0.30102999566398120)); // the decimal exponent, or one under it
    std::uint64_t whole = 0;
    int rest = 0;
    for (int tries = 0;; ++tries) {
        if (tries > 3 || !tensOf(a, p - 1 - x, whole, rest)) return 0;
        if (whole < kTens[p - 1]) --x;
        else if (whole >= kTens[p]) ++x;
        else break;
    }
    std::uint64_t d = whole + ((rest > 0 || (rest == 0 && (whole & 1))) ? 1 : 0);
    if (d == kTens[p]) d = kTens[p - 1], ++x;
    char digits[24];
    digitsInto(digits, d); // p of them
    int used = p; // the digits that are written: trailing zeros of the fraction are not
    std::size_t n = 0;
    if (v < 0) out[n++] = '-';
    if (x < -4 || x >= p) {
        while (used > 1 && digits[used - 1] == '0') --used;
        out[n++] = digits[0];
        if (used > 1) {
            out[n++] = '.';
            for (int i = 1; i < used; ++i) out[n++] = digits[i];
        }
        out[n++] = 'e';
        out[n++] = x < 0 ? '-' : '+';
        const int ex = x < 0 ? -x : x;
        if (ex < 10) out[n++] = '0';
        char exDigits[8];
        const int en = digitsInto(exDigits, static_cast<std::uint64_t>(ex));
        for (int i = 0; i < en; ++i) out[n++] = exDigits[i];
        return n;
    }
    // As %f with p - 1 - x places: the point after x + 1 digits (x >= 0), or "0." and -x - 1 zeros first.
    const int before = x >= 0 ? x + 1 : 0;
    while (used > before && digits[used - 1] == '0') --used;
    if (x >= 0) {
        for (int i = 0; i < before; ++i) out[n++] = digits[i];
        if (used > before) {
            out[n++] = '.';
            for (int i = before; i < used; ++i) out[n++] = digits[i];
        }
    } else {
        out[n++] = '0';
        out[n++] = '.';
        for (int i = 0; i < -x - 1; ++i) out[n++] = '0';
        for (int i = 0; i < used; ++i) out[n++] = digits[i];
    }
    return n;
}
} // namespace

void appendNumber(std::string& out, double v, char conversion, int precision) {
    char text[40];
    std::size_t n = 0;
    if (v == v && v - v == 0.0) { // finite
        if (conversion == 'f' && precision == 0 && v == std::floor(v) && v > -9.2e18 && v < 9.2e18) {
            const long long whole = static_cast<long long>(v);
            if (whole == 0) {
                if (std::signbit(v)) text[n++] = '-';
                text[n++] = '0';
            } else {
                if (whole < 0) text[n++] = '-';
                n += static_cast<std::size_t>(digitsInto(text + n, static_cast<std::uint64_t>(whole < 0 ? -whole : whole)));
            }
        } else if (conversion == 'g' && precision >= 1 && precision <= 17) {
            if (v == 0.0) {
                if (std::signbit(v)) text[n++] = '-';
                text[n++] = '0';
            } else {
                n = gText(text, v, precision);
            }
        }
    }
    if (n > 0) {
        out.append(text, n);
        return;
    }
    char format[8], wide[400]; // (a %.0f of the largest double is 309 digits)
    std::snprintf(format, sizeof format, "%%.%d%c", precision, conversion);
    std::snprintf(wide, sizeof wide, format, v);
    out += wide;
}

namespace {

std::uint64_t gTracksWritten = 0, gBytesRead = 0;

// A number read from a file, held to its limits BEFORE it is made an int or a float: a double too
// big for one ("repeats 1e300") made into one is undefined, and the desk and the handheld read it
// differently (one 1, the other 16). In range, the same as making it one and then holding it.
int wholeIn(double v, int lo, int hi) { return !(v >= lo) ? lo : (v > hi ? hi : static_cast<int>(v)); } // (not a number: lo)
// An address read from a file is written whole: 1.9 is not lane 1 (a hand-written one refuses
// the line; it is not cut down to where it never meant).
bool isWhole(double v) { return v == std::floor(v); }
// The first line, "<word> <version>": the word with anything stuck to it, or with no version
// and no line end, is not a song (nor a kit).
bool hasHead(const std::string& text, const char* word) {
    const std::size_t n = std::strlen(word);
    if (text.compare(0, n, word) != 0 || text.size() <= n || text[n] != ' ') return false;
    std::size_t at = n + 1;
    if (at >= text.size() || text[at] < '0' || text[at] > '9') return false;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
    if (at < text.size() && text[at] == '\r') ++at;
    return at < text.size() && text[at] == '\n';
}
float fractionIn(double v, float lo, float hi) { return !(v >= lo) ? lo : (v > hi ? hi : static_cast<float>(v)); }
std::uint32_t seedIn(double v) { return !(v >= 0.0) ? 0u : (v > 4294967295.0 ? 4294967295u : static_cast<std::uint32_t>(v)); }

// A MOD's DEPTH in a song file: 0..1, 0.5 no depth, 0 is -100% and 1 is +100% (the scale lanes 22
// and 23 automate it on, mod_rule.hpp's modDepthToLane: batch 7). Their `auto` lines are any lane's.
double depthToFile(float depth) { return (static_cast<double>(depth) + 1.0) * 0.5; }
// (Held as a double, and made a float last: through a float first, 0.92 came back as 0.84000003 and not 0.84.)
float depthFromFile(double v) { return v == v ? static_cast<float>(2.0 * (v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0) - 1.0) : 0.f; }
// A value of a STEPPER's row, -1..1; not a number is 0.
float rowIn(double v) { return v == v ? fractionIn(v, -1.f, 1.f) : 0.f; }

// `format` is words and one number, "%.0f", "%.4g" or "%.6g" (all the song file writes): written as
// snprintf writes it, to the byte (numberText), without snprintf for the usual numbers. Anything else
// in a format goes to snprintf whole.
void put(std::string& out, const char* format, double a) {
    const char* at = std::strchr(format, '%');
    if (at && at[1] == '.' && at[2] >= '0' && at[2] <= '9' && (at[3] == 'f' || at[3] == 'g') && !std::strchr(at + 4, '%')) {
        out.append(format, static_cast<std::size_t>(at - format));
        appendNumber(out, a, at[3], at[2] - '0');
        out += at + 4;
        return;
    }
    char text[48];
    std::snprintf(text, sizeof text, format, a);
    out += text;
}


// Has the cell bars past its end with something in them? (A cell made shorter hides its last bars.)
bool hidesBars(const TrackData& track, int scene) {
    for (int bar = track.bars[scene]; bar < kMaxBars; ++bar) {
        if (track.kind == TrackKind::Drums && !track.drums[scene][bar].empty()) return true;
        if (isSynth(track.kind) && !track.notes[scene][bar].empty()) return true;
    }
    return false;
}

// How long a note or a chord may be, from its step: to the end of the cell, over the bar lines.
// One in a bar the cell hides may run to where the longest cell would end.
float longestFrom(const TrackData& track, int scene, int bar, int step) {
    const int bars = bar < track.bars[scene] ? track.bars[scene] : kMaxBars;
    return static_cast<float>((bars - bar) * kSteps - step);
}

void putEffects(std::string& out, const EffectData* effects, int slots) {
    for (int slot = 0; slot < slots; ++slot) {
        const EffectKind kind = static_cast<EffectKind>(effects[slot].kind);
        if (kind == EffectKind::None) continue;
        put(out, "fx %.0f ", slot + 1);
        out += effectInfo(kind).word; // its own word, not its screen name: the name may change, the word never
        for (int i = 0; i < effectInfo(kind).params; ++i) put(out, " %.6g", effects[slot].param[i]);
        out += "\n";
    }
}

} // namespace

// mod N KIND TARGET DEPTH RATE SHAPE RESTART PHASE [SPEED]: MOD N (1 or 2) of the track. KIND 1 an
// LFO, 2 a STEPPER; TARGET the automation lane it is aimed at (0-7 macros, 8 + slot * 4 + setting, 20
// and 21 the sends); DEPTH 0..1 with 0.5 none (depthToFile); the rest by their numbers in
// mod_rule.hpp. SPEED (a FREE LFO's, Hz) is left off at its usual, 1 Hz: a program before it reads
// the eight numbers it knows and skips the ninth.
// modsteps N LENGTH SLEW CHANCE v1..v16: its row (a STEPPER's), each value -1..1; left out when it
// is as a new MOD's (16 long, no slew, no chance, every value 0). Both left out when the MOD is OFF.
std::string modText(const ModSettings& mod, int index) {
    std::string out;
    if (modKind(mod.kind) == kModOff) return out;
    put(out, "mod %.0f", index + 1);
    put(out, " %.0f", mod.kind);
    put(out, " %.0f", mod.target);
    put(out, " %.6g", depthToFile(mod.depth));
    put(out, " %.0f", mod.rate);
    put(out, " %.0f", mod.shape);
    put(out, " %.0f", mod.restart);
    put(out, " %.0f", mod.phase);
    if (mod.speed != kModSpeedUsual) put(out, " %.6g", mod.speed);
    out += "\n";
    bool row = mod.length != kModSteps || mod.slew != 0.f || mod.chance != 0.f;
    for (float v : mod.steps) row = row || v != 0.f;
    if (!row) return out;
    put(out, "modsteps %.0f", index + 1);
    put(out, " %.0f", mod.length);
    put(out, " %.6g", mod.slew);
    put(out, " %.6g", mod.chance);
    for (float v : mod.steps) put(out, " %.6g", v);
    out += "\n";
    return out;
}

std::string soundText(TrackKind kind, const EffectData* effects, const float* send, int glideFrom, const ModSettings* mods) {
    std::string out;
    putEffects(out, effects, kTrackSlots);
    // send BUS AMOUNT: how much of the track goes to the ECHO (1) or the SPACE (2) bus. Left out when none does.
    for (int bus = 0; bus < kBuses; ++bus) {
        if (!(send[bus] > 0.f)) continue;
        put(out, "send %.0f", bus + 1);
        put(out, " %.6g\n", send[bus]);
    }
    // glidefrom N: a synth track's GLIDE FROM (1 PAIRED, 2 NEAREST, 3 RANDOM). Left out at TOP.
    if (isSynth(kind) && glideFrom) put(out, "glidefrom %.0f\n", glideFrom);
    for (int m = 0; m < kModsPerTrack; ++m) out += modText(mods[m], m); // (nothing for one that is OFF)
    return out;
}

namespace {
// (A template: a preset is read through a std::istream, a song through a LineIn. The same reading.)
template <class In>
SoundLine readSoundLineIn(const std::string& word, In& in, const SoundParts& to, bool oldSound) {
    if (word == "fx") {
        double slot, value;
        std::string name;
        if (!(in >> slot >> name) || !(slot >= 1) || slot > kTrackSlots || !isWhole(slot)) return SoundLine::Refused;
        EffectData& data = to.effects[static_cast<int>(slot) - 1];
        const EffectKind kind = effectFromWord(name.c_str());
        if (kind == EffectKind::None) return SoundLine::Refused; // an effect this version does not have: the slot is left as it was
        const EffectInfo& info = effectInfo(kind);
        data.kind = static_cast<int>(kind);
        for (int i = 0; i < kEffectParams; ++i) data.param[i] = info.defaults[i];
        for (int i = 0; i < info.params && (in >> value); ++i) data.param[i] = fractionIn(value, 0.f, 1.f);
        return SoundLine::Taken;
    }
    if (word == "send") {
        double bus, amount;
        if (!(in >> bus >> amount) || !(bus >= 1) || bus > kBuses || !isWhole(bus) || !(amount >= 0.0)) return SoundLine::Refused;
        to.send[static_cast<int>(bus) - 1] = static_cast<float>(amount > 1.0 ? 1.0 : amount);
        return SoundLine::Taken;
    }
    if (word == "glidefrom") {
        double type = 0;
        if (!(in >> type) || !(type >= 0.0) || type > 255.0) return SoundLine::Refused;
        if (!isSynth(to.kind)) return SoundLine::Refused; // (a drum track has none: one written there by hand would wait for the day it became a synth)
        *to.glideFrom = static_cast<std::uint8_t>(glideFromKind(static_cast<int>(type)));
        return SoundLine::Taken;
    }
    if (word == "mod") {
        // (KIND and TARGET must be there; what is missing after them is as a new MOD's. A KIND or a
        // TARGET nobody knows is OFF, and so is a target that does not fit the track: seen once the
        // whole song or preset is read (modsFit), since the track's effects may be written after this line.)
        double n = 0, kind = 0, target = -1, depth = 0.5, rate = kModRateUsual, shape = 0, restart = 0, phase = 0, speed = kModSpeedUsual;
        if (!(in >> n >> kind >> target) || !(n >= 1.0) || n > kModsPerTrack || !isWhole(n)) return SoundLine::Refused;
        if (oldSound && target >= 0.0 && target < kSynthMacros) return SoundLine::Refused; // on another synth's macro: OFF, as its automation goes
        if (!(in >> depth)) depth = 0.5;
        else if (!(in >> rate)) rate = kModRateUsual;
        else if (!(in >> shape)) shape = 0;
        else if (!(in >> restart)) restart = 0;
        else if (!(in >> phase)) phase = 0;
        else if (!(in >> speed)) speed = kModSpeedUsual;
        ModSettings& m = to.mods[static_cast<int>(n) - 1];
        m.kind = modKind(wholeIn(kind, -1, 255));
        const int lane = wholeIn(target, -1, 255);
        m.target = modTargetKnown(lane) ? lane : kModTargetNone;
        m.depth = depthFromFile(depth);
        m.rate = modRate(wholeIn(rate, -1, 255));
        m.shape = modShape(wholeIn(shape, -1, 255));
        m.restart = modRestart(wholeIn(restart, -1, 255));
        m.phase = modPhase(wholeIn(phase, -1, 255));
        m.speed = fractionIn(speed, kModSpeedLeast, kModSpeedMost);
        return SoundLine::Taken;
    }
    if (word == "modsteps") {
        double n = 0, length = kModSteps, slew = 0, chance = 0, v = 0;
        if (!(in >> n >> length) || !(n >= 1.0) || n > kModsPerTrack || !isWhole(n)) return SoundLine::Refused;
        ModSettings& m = to.mods[static_cast<int>(n) - 1];
        m.length = wholeIn(length, 1, kModSteps);
        if (in >> slew) m.slew = fractionIn(slew, 0.f, 1.f);
        if (in >> chance) m.chance = fractionIn(chance, 0.f, 1.f);
        for (int i = 0; i < kModSteps && (in >> v); ++i) m.steps[i] = rowIn(v);
        return SoundLine::Taken;
    }
    return SoundLine::NotOne;
}
} // namespace

SoundLine readSoundLine(const std::string& word, std::istream& in, const SoundParts& to, bool oldSound) {
    return readSoundLineIn(word, in, to, oldSound);
}

// What of a track its text was made from, beyond its bytes: which bars had something in them and
// which lanes of automation had a step written (SongTextCache::Part). A bar with nothing in it is
// written as nothing whatever its other numbers are (putDrumScenes and putNoteScenes skip it; the
// scene's own line asks only whether it is empty), and so is a lane with no step at 0 or over.
struct TextUsed {
    bool bar[kMaxScenes][kMaxBars];
    bool lane[kMaxScenes][kAutoLanes];
};

namespace {

// (Each first asks whether its piece is exactly as nothing written leaves it, a compare the library
// does many bytes at a time; only a piece that is not is looked at number by number.)
bool laneEmpty(const float* steps) { // as putAutomation reads a lane: a step under 0 is not written
    static const struct None { float v[kAutoSteps]; None() { for (float& x : v) x = kAutoNone; } } none;
    static_assert(kAutoNone < 0.f, "an unwritten step is under 0");
    if (std::memcmp(steps, none.v, sizeof none.v) == 0) return true;
    for (int i = 0; i < kAutoSteps; ++i)
        if (!(steps[i] < 0.f)) return false;
    return true;
}
bool levelsEmpty(const DrumPattern& p) { // DrumPattern::empty, over the levels alone
    static const float none[kDrumLanes][kSteps] = {};
    if (std::memcmp(p.level, none, sizeof none) == 0) return true;
    for (const auto& lane : p.level)
        for (float v : lane)
            if (v > 0.f) return false;
    return true;
}
bool notesEmpty(const NotePattern& p) { // NotePattern::empty
    static const NotePattern* const none = new NotePattern();
    return std::memcmp(&p, none, sizeof p) == 0 || p.empty();
}

// One track of a song file: from its "track" line to the next one's.
// The patterns' own lines of a track (putTrack writes them among its others): its automation...
void putAutomation(std::string& out, const TrackData& track) {
    // auto SCENE LANE then pairs INDEX VALUE: the steps of a lane of automation that have something on them.
    for (int scene = 0; scene < kMaxScenes; ++scene)
        for (int lane = 0; lane < kAutoLanes && track.automation.has(scene); ++lane) {
            if (laneEmpty(track.automation[scene][lane])) continue;
            std::string line;
            for (int i = 0; i < kAutoSteps; ++i) {
                const float v = track.automation[scene][lane][i];
                if (v < 0.f) continue;
                put(line, " %.0f", i);
                put(line, " %.4g", v);
            }
            if (line.empty()) continue;
            put(out, "auto %.0f", scene + 1);
            put(out, " %.0f", lane);
            out += line + "\n";
        }
}

// fill 1 (on) or fill 0 (made, off): the cell's FILL (fill_rule.hpp), its bar the "bar 8" lines. Left out
// where none was ever made, so a song without one is written as it always was, and a program from
// before skips the line (and plays the cell's own bars: it never plays a short cell's eighth).
void putFill(std::string& out, const TrackData& track, int scene) {
    if (track.fillMade(scene)) out += track.fillOn(scene) ? "fill 1\n" : "fill 0\n";
}

// ...a drum track's scenes...
void putDrumScenes(std::string& out, const TrackData& track) {
    // scene N, then "bars B" if the cell is longer than one, then each
    // bar's hits; "bar K" comes before those of the second bar on.
    // Then the bars the cell hides (made shorter: they come back as they were if it is made
    // longer again), each after its "bar K" past the cell's end. A program from before
    // reads no line for a bar the cell does not have, so it skips them.
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        if (track.sceneEmpty(scene) && track.bars[scene] == 1 && !hidesBars(track, scene) && !track.fillMade(scene)) continue;
        put(out, "scene %.0f\n", scene + 1);
        if (track.bars[scene] > 1) put(out, "bars %.0f\n", track.bars[scene]);
        putFill(out, track, scene);
        for (int bar = 0; bar < kMaxBars; ++bar) {
            const DrumPattern& p = track.drums[scene][bar];
            if (p.empty()) continue;
            if (bar > 0) put(out, "bar %.0f\n", bar + 1);
            out += "seeds";
            for (std::uint32_t seed : p.diceSeed) put(out, " %.0f", seed);
            out += "\n";
            for (int lane = 0; lane < kDrumLanes; ++lane) {
                for (int step = 0; step < kSteps; ++step) {
                    if (p.level[lane][step] <= 0.f) continue;
                    put(out, "hit %.0f", lane + 1);
                    put(out, " %.0f", step + 1);
                    put(out, " %.6g", p.level[lane][step]);
                    put(out, " %.6g", p.nudge[lane][step]);
                    put(out, " %.6g", p.chance[lane][step]);
                    out += p.fromDice[lane][step] ? " 1" : " 0";
                    if (p.start[lane][step] > 0.f) put(out, " %.6g", p.start[lane][step]); // where in the sound it begins
                    out += "\n";
                    // stepfx LANE STEP PITCH RATCHET BACKWARDS CUT [THROW]: the hit's step effects, if it has any.
                    // THROW (1 ECHO, 2 SPACE, 3 both) is left off when it is 0, as every older song has it.
                    // ("fx" is taken: it is an effect in a slot.)
                    if (const StepFx& fx = p.fx[lane][step]; fx.any()) {
                        put(out, "stepfx %.0f", lane + 1);
                        put(out, " %.0f", step + 1);
                        put(out, " %.0f", fx.pitch);
                        put(out, " %.0f", fx.hits());
                        put(out, " %.0f", fx.reverse ? 1 : 0);
                        put(out, " %.0f", fx.cut);
                        if (fx.throwTo) put(out, " %.0f", fx.throwTo);
                        out += "\n";
                    }
                }
            }
        }
    }
}

// ...and a synth track's.
void putNoteScenes(std::string& out, const TrackData& track) {
    for (int scene = 0; scene < kMaxScenes; ++scene) { // (and the hidden bars, as a drum track's)
        if (track.sceneEmpty(scene) && track.bars[scene] == 1 && !hidesBars(track, scene) && !track.fillMade(scene)) continue;
        put(out, "scene %.0f\n", scene + 1);
        if (track.bars[scene] > 1) put(out, "bars %.0f\n", track.bars[scene]);
        putFill(out, track, scene);
        for (int bar = 0; bar < kMaxBars; ++bar) {
            const NotePattern& p = track.notes[scene][bar];
            if (p.empty()) continue;
            if (bar > 0) put(out, "bar %.0f\n", bar + 1);
            for (const NoteEvent& n : p.notes) {
                if (n.velocity <= 0.f) continue;
                put(out, "note %.0f", n.step + 1);
                put(out, " %.0f", n.pitch);
                put(out, " %.6g", n.length);
                put(out, " %.6g", n.velocity);
                // early or late, in steps; and then its THROW (1 ECHO, 2 SPACE, 3 both), which
                // has the nudge written before it even at 0, so that it is the sixth number
                if (n.nudge != 0.f || n.throwTo) put(out, " %.6g", n.nudge);
                if (n.throwTo) put(out, " %.0f", n.throwTo);
                out += "\n";
                // notefx STEP KEY RATCHET GLIDE: what the note does besides play, if anything
                if (n.ratchet >= 2 || n.glide) {
                    put(out, "notefx %.0f", n.step + 1);
                    put(out, " %.0f", n.pitch);
                    put(out, " %.0f", n.ratchet >= 2 ? n.ratchet : 1);
                    put(out, " %.0f\n", glideKind(n.glide)); // (1, "on" in an old song, is half a step)
                }
            }
            // chord STEP LENGTH LEVEL INVERSION ROOT NAME then its notes above the root
            for (const ChordEvent& c : p.chords) {
                if (c.velocity <= 0.f) continue;
                put(out, "chord %.0f", c.step + 1);
                put(out, " %.6g", c.length);
                put(out, " %.6g", c.velocity);
                put(out, " %.0f", c.inversion);
                put(out, " %.0f ", c.root);
                out += c.name[0] ? c.name : "?";
                for (int i = 0; i < c.count; ++i) put(out, " %.0f", c.intervals[i]);
                if (c.nudge != 0.f) put(out, " nudge %.6g", c.nudge);
                out += "\n";
                if (c.ratchet >= 2 || c.throwTo) { // chordfx STEP RATCHET [THROW]: THROW is left off at 0
                    put(out, "chordfx %.0f", c.step + 1);
                    put(out, " %.0f", c.ratchet >= 2 ? c.ratchet : 1);
                    if (c.throwTo) put(out, " %.0f", c.throwTo);
                    out += "\n";
                }
                // chordplay STEP STRUM ORDER TIME LEVEL LENGTHS: the chord effects (chord_rule.hpp).
                // A line of its own, left out when none is set: a chord that has none is written
                // as it always was, and a program from before them skips the line whole.
                if (c.play.strum || c.play.order || c.play.time || c.play.level || c.play.lengths) {
                    put(out, "chordplay %.0f", c.step + 1);
                    put(out, " %.0f", c.play.strum);
                    put(out, " %.0f", c.play.order);
                    put(out, " %.0f", c.play.time);
                    put(out, " %.0f", c.play.level);
                    put(out, " %.0f\n", c.play.lengths);
                }
            }
        }
    }
}

// A drum track's KIT: every lane's level, recording or synth voice, FIT, one-slice, EDIT, FEEL, SWING and
// CHOKE, in the song file's own lines. The one writer of them: a track in a song (putTrack) and a kit
// preset's file (kitText) are written here.
void putKit(std::string& out, const TrackData& track) {
    out += "lanes";
    for (float level : track.laneLevel) put(out, " %.6g", level);
    out += "\n";
    // sample LANE FIT NAME: a recording on a lane, stretched to FIT steps (0: as recorded)
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        if (!track.laneSample[lane][0]) continue;
        put(out, "sample %.0f", lane + 1);
        put(out, " %.0f ", track.laneFit[lane]);
        out += track.laneSample[lane];
        out += "\n";
    }
    // lanefit LANE STEPS: a lane's FIT with no recording on it (a recording's is on its sample
    // line). Left out at 0.
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        if (!track.laneFit[lane] || track.laneSample[lane][0]) continue;
        put(out, "lanefit %.0f", lane + 1);
        put(out, " %.0f\n", track.laneFit[lane]);
    }
    // lanesynth LANE WORD v1..v8: a lane's DRUM SYNTH voice (apotheca/drum_synth.hpp), its model by its
    // word and its eight dials in place order. Left out when the lane has none.
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const drumsynth::Voice& v = track.laneSynth[lane];
        if (!v.on()) continue;
        put(out, "lanesynth %.0f ", lane + 1);
        out += drumsynth::modelWord(v.model);
        for (int p = 0; p < drumsynth::kPlaces; ++p) put(out, " %.0f", v.dial[p]);
        out += "\n";
    }
    // oneslice LANE...: the lanes whose hits play one slice of their sound and stop
    bool anySliced = false;
    for (bool on : track.laneOneSlice) anySliced = anySliced || on;
    if (anySliced) {
        out += "oneslice";
        for (int lane = 0; lane < kDrumLanes; ++lane)
            if (track.laneOneSlice[lane]) put(out, " %.0f", lane + 1);
        out += "\n";
    }
    // laneedit LANE START END GAIN FADEIN FADEOUT REVERSE PITCH FINE [MONO]: what a lane does to its sound
    // (lane_edit.hpp). START and END as parts of the file (0..1), GAIN in dB, the fades in ms, REVERSE
    // 0 or 1, PITCH in semitones, FINE in cents, MONO 1 (STEREO, 2026-11-28: appended, and written only when on,
    // so a song without it is written as it always was). Left out when every edit is at rest.
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const LaneEdit& e = track.laneEdit[lane];
        if (e.atRest()) continue;
        put(out, "laneedit %.0f", lane + 1);
        put(out, " %.6g", static_cast<double>(e.start) / kEditUnits);
        put(out, " %.6g", static_cast<double>(e.end) / kEditUnits);
        put(out, " %.6g", e.gain / 10.0);
        put(out, " %.0f", e.fadeIn);
        put(out, " %.0f", e.fadeOut);
        put(out, " %.0f", e.reverse);
        put(out, " %.0f", e.pitch);
        put(out, " %.0f", e.fine);
        if (e.mono) put(out, " %.0f", e.mono);
        out += "\n";
    }
    // feel LANE TIME LEVEL: a lane's HUMAN TIME and HUMAN LEVEL, each 0..100. Left out when both are off.
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        if (track.laneFeel[lane].atRest()) continue;
        put(out, "feel %.0f", lane + 1);
        put(out, " %.0f", track.laneFeel[lane].time);
        put(out, " %.0f\n", track.laneFeel[lane].level);
    }
    // swing LANE AMOUNT: a lane's SWING, 0..100. Left out at 0.
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        if (track.laneSwing[lane] == 0) continue;
        put(out, "swing %.0f", lane + 1);
        put(out, " %.0f\n", track.laneSwing[lane]);
    }
    // choke LANE GROUP: a lane in CHOKE 1 or 2. Left out in none.
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        if (track.laneChoke[lane] == kChokeNone) continue;
        put(out, "choke %.0f", lane + 1);
        put(out, " %.0f\n", track.laneChoke[lane]);
    }
    // slices LANE SENSE AT...: a lane's own cuts (SLICES AT THE HITS, slice_rule.hpp): the SENSITIVITY they
    // were found at (0: by hand), then where each slice after the first begins (0..1 of what the lane
    // plays; the first is the top). Left out when its slices are EVEN.
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const LaneCuts& cuts = track.laneCuts[lane];
        if (cuts.even()) continue;
        put(out, "slices %.0f", lane + 1);
        put(out, " %.0f", cuts.sense);
        for (int k = 1; k < cuts.count; ++k) put(out, " %.6g", cuts.at[k]);
        out += "\n";
    }
}

void putTrack(std::string& out, const TrackData& track) {
    ++gTracksWritten;
    out += std::string("track ") + instrument(track.kind)->word + "\n";
    if (track.name[0]) out += std::string("name ") + track.name + "\n";
    putAutomation(out, track);
    if (track.muted) out += "muted\n";
    // mix DB PAN: the track's level on the mixer, in dB from its usual, and its pan (-50 left .. 50 right). Left out at 0 0.
    if (track.mixDb != 0 || track.mixPan != 0) {
        put(out, "mix %.0f", track.mixDb);
        put(out, " %.0f\n", track.mixPan);
    }
    out += soundText(track.kind, track.effects, track.send, track.glideFrom, track.mods);
    if (track.kind == TrackKind::Drums) {
        putKit(out, track);
        putDrumScenes(out, track);
    } else {
        if (track.kind == TrackKind::Sampler) {
            // sound NAME: its recording. sampler: root tune start attack release loop level [end [loopstart]]. END
            // and LOOP START (appended, S4) are left out at their usual (100%, and 0: START), so a sampler that does
            // not use them is written as it always was; one that does writes every number up to it.
            // chop: chopped into 16 slices from C1 up (left out whole).
            // sampleredit START END GAIN FADEIN FADEOUT REVERSE [MONO]: its EDIT (S4, as a lane's laneedit without PITCH
            // and FINE; MONO as there, written only when on), left out at rest.
            if (track.sample[0]) out += std::string("sound ") + track.sample + "\n";
            out += "sampler";
            int upTo = sampler::End - 1;
            for (int id = sampler::End; id < sampler::kSettings; ++id)
                if (track.sampler[id] != sampler::settingDefault(id)) upTo = id;
            for (int id = 0; id <= upTo; ++id) put(out, " %.0f", track.sampler[id]);
            out += "\n";
            if (track.samplerChop) out += "chop\n";
            const LaneEdit& e = track.samplerEdit;
            if (!e.atRest()) {
                put(out, "sampleredit %.6g", static_cast<double>(e.start) / kEditUnits);
                put(out, " %.6g", static_cast<double>(e.end) / kEditUnits);
                put(out, " %.6g", e.gain / 10.0);
                put(out, " %.0f", e.fadeIn);
                put(out, " %.0f", e.fadeOut);
                put(out, " %.0f", e.reverse);
                if (e.mono) put(out, " %.0f", e.mono);
                out += "\n";
            }
        } else {
            if (track.kind == TrackKind::Cloud && track.sample[0]) out += std::string("sound ") + track.sample + "\n"; // (CLOUD's recording: the sampler's line)
            put(out, "preset %.0f\n", track.preset + 1);
            out += "macros";
            for (float m : track.macro) put(out, " %.6g", m);
            out += "\nsettings";
            const int* values = specFor(track.kind)->values(const_cast<TrackData&>(track));
            for (int i = 0; i < settingCount(track.kind); ++i) put(out, " %.0f", values[i]);
            out += "\n";
        }
        putNoteScenes(out, track);
    }
}

// The song's own lines, before its tracks.
void putHead(std::string& out, const ProjectHead& project) {
    out += "apotheca-song 1\n";
    put(out, "tempo %.6g\n", project.tempo);
    // repeats and length: a number a scene, sixteen of them as a song has always had (so a song of
    // sixteen scenes or fewer is written as it always was), and more only up to the last scene past
    // the sixteenth that is not at its usual (scenes-plan.md).
    const auto upTo = [](const int* values, int usual) {
        int count = kOldSceneCount;
        for (int s = kOldSceneCount; s < kMaxScenes; ++s)
            if (values[s] != usual) count = s + 1;
        return count;
    };
    out += "repeats";
    for (int s = 0; s < upTo(project.repeats, 1); ++s) put(out, " %.0f", project.repeats[s]);
    // length: how many steps each scene lasts, where one is cut short (0: all of them). Only if any is.
    bool cut = false;
    for (int n : project.steps) cut = cut || n > 0;
    if (cut) {
        out += "\nlength";
        for (int s = 0; s < upTo(project.steps, 0); ++s) put(out, " %.0f", project.steps[s]);
    }
    // stempi: what each scene plays at (0: the song's tempo). Only if any scene has its own.
    bool tempi = false;
    for (float t : project.sceneTempo) tempi = tempi || t > 0.f;
    if (tempi) {
        out += "\nstempi";
        int count = kOldSceneCount;
        for (int s = kOldSceneCount; s < kMaxScenes; ++s)
            if (project.sceneTempo[s] > 0.f) count = s + 1;
        for (int s = 0; s < count; ++s) put(out, " %.0f", project.sceneTempo[s]);
    }
    // meter: steps in each scene's bars (a bar of 12 is 3/4). Only if any scene is not 16.
    bool metered = false;
    for (int m : project.sceneMeter) metered = metered || (m != kSteps);
    if (metered) {
        out += "\nmeter";
        int count = kOldSceneCount;
        for (int s = kOldSceneCount; s < kMaxScenes; ++s)
            if (project.sceneMeter[s] != kSteps) count = s + 1;
        for (int s = 0; s < count; ++s) put(out, " %.0f", static_cast<double>(project.meterOf(s)));
    }
    out += "\n";
    if (project.chordPack[0]) out += std::string("pack ") + project.chordPack + "\n";
    // songend N: what the song does at its end (1: STOP). Left out at LOOP, so a song that loops is
    // written as it always was, and a program from before (which skips the line) plays it round.
    if (project.songEnd != kSongLoop) put(out, "songend %.0f\n", project.songEnd);
    // scale KEY KIND: SCALE LOCK (scales.hpp). Left out at OFF, so a song with none is written as before.
    if (project.scaleKind != kScaleOff) {
        put(out, "scale %.0f", project.scaleKey);
        put(out, " %.0f\n", project.scaleKind);
    }
    // master DB PAN: the master's level and pan on the mixer, as a track's `mix`. Left out at 0 0.
    if (project.masterDb != 0 || project.masterPan != 0) {
        put(out, "master %.0f", project.masterDb);
        put(out, " %.0f\n", project.masterPan);
    }
    // fx SLOT NAME and its settings. Before any track: the master's.
    putEffects(out, project.masterEffects, kMasterSlots);
    // busfx BUS NAME and its settings: what the ECHO (1) and the SPACE (2) bus hold. Always both, an
    // empty one as "none": a song with no such line is an old song, and gets the usual two.
    for (int bus = 0; bus < kBuses; ++bus) {
        const EffectKind kind = static_cast<EffectKind>(project.busEffects[bus].kind);
        put(out, "busfx %.0f ", bus + 1);
        out += kind == EffectKind::None ? "none" : effectInfo(kind).word;
        for (int i = 0; i < effectInfo(kind).params; ++i) put(out, " %.6g", project.busEffects[bus].param[i]);
        out += "\n";
    }
}

void markUsed(const TrackData& track, TextUsed& used) {
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        const bool kit = track.kind == TrackKind::Drums;
        const bool bars = kit ? track.drums.has(scene) : track.notes.has(scene), lanes = track.automation.has(scene); // (no piece: nothing in it)
        for (int bar = 0; bar < kMaxBars; ++bar)
            used.bar[scene][bar] = bars && (kit ? !levelsEmpty(track.drums[scene][bar]) : !notesEmpty(track.notes[scene][bar]));
        for (int lane = 0; lane < kAutoLanes; ++lane) used.lane[scene][lane] = lanes && !laneEmpty(track.automation[scene][lane]);
    }
}

// Would `now` be written as `seen` was (whose bars and lanes in use are `used`)? Everything outside
// the patterns byte for byte; a bar or a lane that had something in it byte for byte; one that had
// nothing only looked at to see it still has nothing (one side read, a quarter of a drum bar). The
// drum patterns of a synth track, and the notes of a drum track, are not written: not looked at.
// Whole bytes where they are compared: a difference the text does not show only makes it written again.
bool sameForText(const TrackData& seen, const TextUsed& used, const TrackData& now) {
    if (!now.sameFields(seen)) return false; // the kind, the bars, the sound, the settings, the name, the sampler...
    const bool kit = seen.kind == TrackKind::Drums;
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        const bool bars = kit ? now.drums.has(scene) : now.notes.has(scene), lanes = now.automation.has(scene); // (no piece: nothing in it)
        for (int bar = 0; bar < kMaxBars; ++bar) {
            if (used.bar[scene][bar]) {
                if (!bars || (kit ? std::memcmp(&seen.drums[scene][bar], &now.drums[scene][bar], sizeof(DrumPattern)) != 0
                                  : std::memcmp(&seen.notes[scene][bar], &now.notes[scene][bar], sizeof(NotePattern)) != 0)) return false;
            } else if (bars && (kit ? !levelsEmpty(now.drums[scene][bar]) : !notesEmpty(now.notes[scene][bar]))) {
                return false;
            }
        }
        for (int lane = 0; lane < kAutoLanes; ++lane) {
            if (used.lane[scene][lane] ? !lanes || std::memcmp(seen.automation[scene][lane], now.automation[scene][lane], sizeof seen.automation[scene][lane]) != 0
                                       : lanes && !laneEmpty(now.automation[scene][lane])) return false;
        }
    }
    return true;
}

} // namespace

std::string patternText(const TrackData& track) {
    std::string out;
    putAutomation(out, track);
    if (track.kind == TrackKind::Drums) putDrumScenes(out, track);
    else if (isSynth(track.kind)) putNoteScenes(out, track);
    return out;
}

std::string trackText(const TrackData& track) {
    std::string out;
    putTrack(out, track);
    return out;
}

std::string songHeadText(const ProjectHead& head) {
    std::string out;
    putHead(out, head);
    return out;
}

std::string songText(const Project& project) {
    std::string out;
    putHead(out, project);
    for (int t = 0; t < project.trackCount(); ++t) putTrack(out, project.tracks[t]);
    return out;
}

SongTextCache::SongTextCache() = default;
SongTextCache::~SongTextCache() = default;

const std::string& SongTextCache::text(const Project& project) {
    std::string head;
    putHead(head, project);
    const int count = project.trackCount();
    bool changed = !made_ || count != count_ || head != head_;
    for (int t = 0; t < count; ++t) {
        Part& part = parts_[t];
        const TrackData& now = project.tracks[t];
        if (part.seen && sameForText(*part.seen, *part.used, now)) continue;
        if (!part.seen) part.seen.reset(new TrackData), part.used.reset(new TextUsed);
        *part.seen = now; // byte for byte, what lies between the fields too (TrackData's copy)
        markUsed(now, *part.used);
        part.text.clear();
        putTrack(part.text, now);
        changed = true;
    }
    for (int t = count; t < kNumTracks; ++t) { // a track taken away: its copy goes too
        parts_[t].seen.reset();
        parts_[t].used.reset();
        parts_[t].text.clear();
    }
    if (changed) {
        head_.swap(head);
        count_ = count;
        made_ = true;
        std::size_t size = head_.size();
        for (int t = 0; t < count; ++t) size += parts_[t].text.size();
        text_.clear();
        text_.reserve(size);
        text_ += head_;
        for (int t = 0; t < count; ++t) text_ += parts_[t].text;
    }
    return text_;
}

const std::string& SongTextCache::track(int t) const {
    static const std::string none;
    return t >= 0 && t < count_ ? parts_[t].text : none;
}

std::uint64_t tracksWritten() { return gTracksWritten; }
std::uint64_t bytesRead() { return gBytesRead; }

namespace {
// A track as new (1 MB: never on the stack), made once.
const TrackData& blankTrackData() {
    static const std::unique_ptr<const TrackData> blank(new TrackData);
    return *blank;
}

// `track` made as new, byte for byte what copying a blank one gives: the fields copied, and no
// piece of any scene (each is made again where the song writes it).
void makeBlank(TrackData& track) {
    track.fieldsFrom(blankTrackData());
    track.drums.clear();
    track.notes.clear();
    track.automation.clear();
}
} // namespace

// Where the reading of a song's lines has got to, from one run of lines to the next (parseSong
// reads them all in one run; SongReader a part at a time).
struct SongReading {
    ProjectHead& head;
    TrackData* const* slots; // where each track's lines go, by its place among the song's tracks
    TrackData* track = nullptr;
    bool oldSound = false; // the track being read was written as `track twin` or `track qmod`: its notes are read, its sound is not
    int count = 0, scene = -1, bar = 0;
    std::vector<int> heldSettings; // this track's "settings", as read: a "preset" after them is put back over
    int oldSteps[kMaxScenes]; // "steps": to a bar, as songs were kept before a scene had a length
    SongReading(ProjectHead& h, TrackData* const* s) : head(h), slots(s) {
        for (int& n : oldSteps) n = kSteps;
    }
};

namespace {
// The lines of `text` from `from` (the start of a line) to `to`, each as std::getline cuts them,
// read where it lies (LineIn: no copy, no stream).
void readLines(SongReading& r, const std::string& text, std::size_t from, std::size_t to) {
    ProjectHead& head = r.head;
    TrackData*& track = r.track;
    bool& oldSound = r.oldSound;
    int &count = r.count, &scene = r.scene, &bar = r.bar;
    int* const oldSteps = r.oldSteps;
    const char* const all = text.data();
    const std::size_t size = to;
    std::size_t at = from;
    std::string word;
    while (at < size) {
        const char* begin = all + at;
        const void* stop = std::memchr(begin, '\n', size - at);
        const char* end = stop ? static_cast<const char*>(stop) : all + size;
        at = static_cast<std::size_t>(end - all) + 1;
        if (end > begin && end[-1] == '\r') --end; // a file kept with Windows' line ends: a name must not end in it
        LineIn in(begin, end);
        word.clear();
        if (!(in >> word)) continue;

        if (word == "tempo") {
            double tempo = 120;
            if (in >> tempo) head.tempo = fractionIn(tempo, kMinTempo, kMaxTempo);
        } else if (word == "length") {
            double n;
            for (int i = 0; i < kMaxScenes && (in >> n); ++i) head.steps[i] = wholeIn(n, 0, kMaxBars * kSteps);
        } else if (word == "steps") {
            // As songs were kept before: steps to a BAR, 1..16. Turned into a length once the cells' bars are known (below).
            double n;
            for (int i = 0; i < kMaxScenes && (in >> n); ++i) oldSteps[i] = wholeIn(n, 1, kSteps);
        } else if (word == "repeats") {
            double r;
            for (int i = 0; i < kMaxScenes && (in >> r); ++i) head.repeats[i] = wholeIn(r, 1, kMaxRepeats);
        } else if (word == "stempi") {
            double t;
            for (int i = 0; i < kMaxScenes && (in >> t); ++i) head.sceneTempo[i] = t < 1.0 ? 0.f : fractionIn(t, kMinTempo, kMaxTempo);
        } else if (word == "meter") {
            double m;
            for (int i = 0; i < kMaxScenes && (in >> m); ++i) head.sceneMeter[i] = wholeIn(m, 1, kSteps);
        } else if (word == "pack") {
            std::string name;
            skipSpace(in);
            restOfLine(in, name);
            std::snprintf(head.chordPack, sizeof head.chordPack, "%s", name.c_str());
        } else if (word == "fx" && !track) { // the master's (a track's: readSoundLine, below)
            double slot, value;
            std::string name;
            if (!(in >> slot >> name) || !(slot >= 1) || slot > kMasterSlots || !isWhole(slot)) continue;
            EffectData& data = head.masterEffects[static_cast<int>(slot) - 1];
            const EffectKind kind = effectFromWord(name.c_str());
            if (kind == EffectKind::None) continue; // an effect this version does not have: the slot is left as it was
            const EffectInfo& info = effectInfo(kind);
            data.kind = static_cast<int>(kind);
            for (int i = 0; i < kEffectParams; ++i) data.param[i] = info.defaults[i];
            for (int i = 0; i < info.params && (in >> value); ++i) data.param[i] = fractionIn(value, 0.f, 1.f);
        } else if (word == "songend") {
            double end = 0;
            if (in >> end) head.songEnd = songEndKind(wholeIn(end, 0, 255)); // (held as a double first; a number nobody knows: LOOP)
        } else if (word == "scale") {
            double key = 0, kind = 0;
            if (!(in >> key >> kind)) continue;
            head.scaleKey = wholeIn(key, 0, 11);
            head.scaleKind = scaleKindOf(wholeIn(kind, 0, 255)); // (held as doubles first; a kind nobody knows: OFF)
        } else if (word == "master") {
            double db = 0, pan = 0;
            if (!(in >> db)) continue;
            if (!(in >> pan)) pan = 0;
            head.masterDb = wholeIn(db, kMixLeastDb, kMixMostDb);
            head.masterPan = wholeIn(pan, -kPanMost, kPanMost);
        } else if (word == "busfx") {
            double bus, value;
            std::string name;
            if (!(in >> bus >> name) || !(bus >= 1) || bus > kBuses || !isWhole(bus)) continue;
            const int b = static_cast<int>(bus) - 1;
            const EffectKind kind = effectFromWord(name.c_str());
            // An effect this version does not have, or one that is not for this bus: the bus keeps what it had.
            if (kind == EffectKind::None ? name != "none" : effectBus(kind) != b) continue;
            head.setBusEffect(b, kind);
            for (int i = 0; i < effectInfo(kind).params && (in >> value); ++i) head.busEffects[b].param[i] = fractionIn(value, 0.f, 1.f);
        } else if (word == "track") {
            std::string kind;
            in >> kind;
            const TrackKind k = trackKindFromWord(kind.c_str(), &oldSound);
            track = nullptr;
            scene = -1;
            r.heldSettings.clear();
            if (k == TrackKind::None || count >= kNumTracks) continue; // an instrument this version does not have, or a thirteenth track
            track = r.slots[count++];
            makeBlank(*track);
            track->kind = k;
            if (const SynthSpec* spec = specFor(k)) spec->loadPreset(*track, 0);
        } else if (!track) {
            continue; // nothing else means anything outside a track
        } else if (word == "name") {
            std::string rest;
            restOfLine(in, rest);
            std::string safe;
            for (char ch : rest) {
                const bool fine = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '_';
                if (fine && !(ch == ' ' && safe.empty()) && safe.size() < sizeof track->name - 1) safe += ch;
            }
            while (!safe.empty() && safe.back() == ' ') safe.pop_back();
            std::snprintf(track->name, sizeof track->name, "%s", safe.c_str());
        } else if (word == "auto") {
            double sceneNumber = 0, lane = -1, index = 0, value = 0;
            if (!(in >> sceneNumber >> lane) || !(sceneNumber >= 1) || sceneNumber > kMaxScenes || !isWhole(sceneNumber) || !(lane >= 0) ||
                lane >= kAutoLanes || !isWhole(lane))
                continue;
            if (oldSound && lane < kSynthMacros) continue; // another synth's macros: they would turn knobs that are not theirs
            while (in >> index >> value) {
                if (!(index >= 0) || index >= kAutoSteps || !isWhole(index) || !(value >= 0.0)) continue;
                track->automation[static_cast<int>(sceneNumber) - 1][static_cast<int>(lane)][static_cast<int>(index)] = static_cast<float>(value > 1.0 ? 1.0 : value);
            }
        } else if (readSoundLineIn(word, in, SoundParts{track->kind, track->effects, track->send, &track->glideFrom, track->mods}, oldSound) != SoundLine::NotOne) {
            continue; // fx, send, glidefrom, mod, modsteps: the track's sound past its synth (a preset file's too)
        } else if (word == "mix") {
            double db = 0, pan = 0;
            if (!(in >> db)) continue;
            if (!(in >> pan)) pan = 0;
            track->mixDb = wholeIn(db, kMixLeastDb, kMixMostDb);
            track->mixPan = wholeIn(pan, -kPanMost, kPanMost);
        } else if (word == "muted") {
            track->muted = true;
        } else if (word == "lanes") {
            double level;
            for (int i = 0; i < kDrumLanes && (in >> level); ++i) track->laneLevel[i] = fractionIn(level, 0.f, 1.f);
        } else if (word == "sample" && track->kind == TrackKind::Drums) {
            double lane, fit;
            std::string name;
            if (!(in >> lane >> fit) || !(lane >= 1) || lane > kDrumLanes || !isWhole(lane)) continue;
            skipSpace(in);
            restOfLine(in, name);
            if (!sampleNameOk(name)) continue; // a name, or a folder and a name: not a path out of the samples folder
            const int l = static_cast<int>(lane) - 1;
            std::snprintf(track->laneSample[l], sizeof track->laneSample[l], "%s", name.c_str());
            track->laneFit[l] = wholeIn(fit, 0, 64);
            track->laneSynth[l] = drumsynth::Voice(); // (a lane is one or the other: the later line wins)
        } else if (word == "lanefit" && track->kind == TrackKind::Drums) {
            double lane, fit;
            if (!(in >> lane >> fit) || !(lane >= 1) || lane > kDrumLanes || !isWhole(lane)) continue;
            track->laneFit[static_cast<int>(lane) - 1] = wholeIn(fit, 0, 64);
        } else if (word == "lanesynth" && track->kind == TrackKind::Drums) {
            // A model nobody knows: the line is skipped. Each dial held as a double first; one missing
            // or not a number ends the line there, the rest the model's usual.
            double lane = 0;
            std::string model;
            if (!(in >> lane >> model) || !(lane >= 1) || lane > kDrumLanes || !isWhole(lane)) continue;
            const int m = drumsynth::modelFromWord(model.c_str());
            if (m == drumsynth::kNone) continue;
            drumsynth::Voice v = drumsynth::usualVoice(m);
            for (int p = 0; p < drumsynth::kPlaces; ++p) {
                double read = 0;
                if (!(in >> read)) break;
                v.dial[p] = static_cast<std::uint8_t>(wholeIn(read, 0, drumsynth::kDialMost));
            }
            const int l = static_cast<int>(lane) - 1;
            track->laneSynth[l] = drumsynth::voiceHeld(v);
            track->laneSample[l][0] = 0;
        } else if (word == "oneslice" && track->kind == TrackKind::Drums) {
            double lane;
            while (in >> lane)
                if (lane >= 1 && lane <= kDrumLanes && isWhole(lane)) track->laneOneSlice[static_cast<int>(lane) - 1] = true;
        } else if (word == "laneedit" && track->kind == TrackKind::Drums) {
            // Each number held as a double first; one missing or not a number ends the line there, the
            // rest at rest. END is held after START (by a unit: the file's length is not known here).
            double lane = 0, v[9] = {0, 1, 0, 0, 0, 0, 0, 0, 0};
            if (!(in >> lane) || !(lane >= 1) || lane > kDrumLanes || !isWhole(lane)) continue;
            for (double& one : v) {
                double read = 0;
                if (!(in >> read)) break; // (a failed read gives 0: not taken)
                one = read;
            }
            LaneEdit e;
            e.start = static_cast<int>(std::lround(fractionIn(v[0], 0.f, 1.f) * kEditUnits));
            e.end = static_cast<int>(std::lround(fractionIn(v[1], 0.f, 1.f) * kEditUnits));
            e.gain = static_cast<int>(std::lround(fractionIn(v[2], -kGainMost / 10.f, kGainMost / 10.f) * 10.f));
            e.fadeIn = wholeIn(v[3], 0, kFadeMost);
            e.fadeOut = wholeIn(v[4], 0, kFadeMost);
            e.reverse = wholeIn(v[5], 0, 1);
            e.pitch = wholeIn(v[6], -kPitchMost, kPitchMost);
            e.fine = wholeIn(v[7], -kFineMost, kFineMost);
            e.mono = wholeIn(v[8], 0, 1);
            track->laneEdit[static_cast<int>(lane) - 1] = laneEditHeld(e, 0.f);
        } else if (word == "feel" && track->kind == TrackKind::Drums) {
            double lane, time = 0, level = 0;
            if (!(in >> lane >> time) || !(lane >= 1) || lane > kDrumLanes || !isWhole(lane)) continue;
            if (!(in >> level)) level = 0;
            track->laneFeel[static_cast<int>(lane) - 1] = laneFeelOf(wholeIn(time, 0, 100), wholeIn(level, 0, 100)); // (held as doubles first)
        } else if (word == "swing" && track->kind == TrackKind::Drums) {
            double lane, amount;
            if (!(in >> lane >> amount) || !(lane >= 1) || lane > kDrumLanes || !isWhole(lane)) continue;
            track->laneSwing[static_cast<int>(lane) - 1] = laneSwingOf(wholeIn(amount, 0, 100)); // (held as a double first)
        } else if (word == "slices" && track->kind == TrackKind::Drums) {
            // Each place held as a double first; one missing, not a number, out of order or past the room ends
            // the cuts there (cutsHeld). A SENSITIVITY nobody knows: none (by hand).
            double lane = 0, sense = 0;
            if (!(in >> lane >> sense) || !(lane >= 1) || lane > kDrumLanes || !isWhole(lane)) continue;
            LaneCuts cuts;
            cuts.count = 1;
            cuts.sense = static_cast<std::uint8_t>(wholeIn(sense, 0, kSenseMost + 1));
            double at = 0;
            while (cuts.count < kMaxLaneCuts && (in >> at)) cuts.at[cuts.count++] = static_cast<float>(at > 0.0 && at < 1.0 ? at : 0.0);
            track->laneCuts[static_cast<int>(lane) - 1] = slices::cutsHeld(cuts);
        } else if (word == "choke" && track->kind == TrackKind::Drums) {
            double lane, group;
            if (!(in >> lane >> group) || !(lane >= 1) || lane > kDrumLanes || !isWhole(lane)) continue;
            track->laneChoke[static_cast<int>(lane) - 1] = static_cast<std::uint8_t>(chokeGroupOf(wholeIn(group, -1, kChokeGroups + 1))); // (a group nobody knows: none)
        } else if (word == "sound" && holdsRecording(track->kind)) {
            std::string name;
            skipSpace(in);
            restOfLine(in, name);
            if (!sampleNameOk(name)) continue; // a name, or a folder and a name: not a path out of the samples folder
            std::snprintf(track->sample, sizeof track->sample, "%s", name.c_str());
        } else if (word == "sampler" && track->kind == TrackKind::Sampler) {
            double v;
            for (int id = 0; id < sampler::kSettings && (in >> v); ++id) track->sampler[id] = wholeIn(v, sampler::settingMin(id), sampler::settingMax(id));
            int& end = track->sampler[sampler::End]; // (held after START, as the screen holds it: the engine plays it so anyway)
            end = std::min(sampler::playedEnd(track->sampler[sampler::Start], end), sampler::settingMax(sampler::End));
        } else if (word == "chop" && track->kind == TrackKind::Sampler) {
            track->samplerChop = true;
        } else if (word == "sampleredit" && track->kind == TrackKind::Sampler) {
            // As laneedit's numbers (no lane, no PITCH or FINE): one missing or not a number ends the line there.
            double v[7] = {0, 1, 0, 0, 0, 0, 0};
            for (double& one : v) {
                double read = 0;
                if (!(in >> read)) break;
                one = read;
            }
            LaneEdit e;
            e.start = static_cast<int>(std::lround(fractionIn(v[0], 0.f, 1.f) * kEditUnits));
            e.end = static_cast<int>(std::lround(fractionIn(v[1], 0.f, 1.f) * kEditUnits));
            e.gain = static_cast<int>(std::lround(fractionIn(v[2], -kGainMost / 10.f, kGainMost / 10.f) * 10.f));
            e.fadeIn = wholeIn(v[3], 0, kFadeMost);
            e.fadeOut = wholeIn(v[4], 0, kFadeMost);
            e.reverse = wholeIn(v[5], 0, 1);
            e.mono = wholeIn(v[6], 0, 1);
            track->samplerEdit = laneEditHeld(e, 0.f);
        } else if (oldSound && (word == "preset" || word == "macros" || word == "settings")) {
            continue; // an old Twin Analog or QMOD track's sound: it plays the first preset, the knobs in the middle
        } else if (word == "preset") {
            double preset;
            const SynthSpec* spec = specFor(track->kind);
            if (spec && (in >> preset) && isWhole(preset)) {
                track->preset = wholeIn(preset - 1.0, 0, spec->presetCount() - 1);
                spec->loadPreset(*track, track->preset); // "settings", if it follows, overrides
                int* values = spec->values(*track);      // "settings", if it came first, is put back
                for (std::size_t i = 0; i < r.heldSettings.size() && static_cast<int>(i) < settingCount(track->kind); ++i)
                    values[i] = r.heldSettings[i];
            }
        } else if (word == "macros") {
            double m;
            for (int i = 0; i < kSynthMacros && (in >> m); ++i) track->macro[i] = fractionIn(m, 0.f, 1.f);
        } else if (word == "settings") {
            const SynthSpec* spec = specFor(track->kind);
            if (!spec) continue;
            int* values = spec->values(*track);
            double v;
            r.heldSettings.clear();
            for (int i = 0; i < settingCount(track->kind) && (in >> v); ++i) {
                values[i] = wholeIn(v, spec->settingMin(i), spec->settingMax(i));
                r.heldSettings.push_back(values[i]);
            }
        } else if (word == "scene") {
            double s;
            scene = (in >> s) && s >= 1 && s <= kMaxScenes && isWhole(s) ? static_cast<int>(s) - 1 : -1;
            bar = 0;
        } else if (scene < 0) {
            continue;
        } else if (word == "bars") {
            double b;
            if (in >> b) track->bars[scene] = b < 2 ? 1 : (b < 4 ? 2 : (b < 8 ? 4 : 8));
            if (track->bars[scene] >= kMaxBars) track->fill[scene] = kFillNone; // (a cell of eight bars has no FILL)
        } else if (word == "fill") {
            double on;
            if ((in >> on) && track->bars[scene] < kMaxBars) track->fill[scene] = on == 1 ? kFillOn : kFillOff; // (a number not known: made, off)
        } else if (word == "bar") {
            double b;
            bar = (in >> b) && b >= 1 && b <= kMaxBars && isWhole(b) ? static_cast<int>(b) - 1 : -1;
        } else if (bar < 0) {
            continue; // lines for a bar there is not (a bar past the cell's end is one it hides: kept)
        } else if (word == "seeds" && track->kind == TrackKind::Drums) {
            double seed;
            for (int i = 0; i < kDrumLanes && (in >> seed); ++i) track->drums[scene][bar].diceSeed[i] = seedIn(seed);
        } else if (word == "hit" && track->kind == TrackKind::Drums) {
            double lane, step, level, nudge = 0, chance = 1, dice = 0, start = 0;
            if (!(in >> lane >> step >> level)) continue;
            in >> nudge >> chance >> dice >> start;
            if (!(lane >= 1) || lane > kDrumLanes || !isWhole(lane) || !(step >= 1) || step > kSteps || !isWhole(step) || !(level > 0)) continue;
            DrumPattern& p = track->drums[scene][bar];
            const int l = static_cast<int>(lane) - 1, s = static_cast<int>(step) - 1;
            p.level[l][s] = fractionIn(level, 0.05f, 1.f);
            p.nudge[l][s] = fractionIn(nudge, -0.5f, 0.5f);
            p.chance[l][s] = fractionIn(chance, 0.05f, 1.f);
            p.fromDice[l][s] = dice != 0;
            p.start[l][s] = fractionIn(start, 0.f, 1.f);
        } else if (word == "stepfx" && track->kind == TrackKind::Drums) {
            double lane, step, pitch = 0, ratchet = 1, reverse = 0, cut = 0, thrown = 0;
            if (!(in >> lane >> step)) continue;
            in >> pitch >> ratchet >> reverse >> cut >> thrown;
            if (!(lane >= 1) || lane > kDrumLanes || !isWhole(lane) || !(step >= 1) || step > kSteps || !isWhole(step)) continue;
            StepFx& fx = track->drums[scene][bar].fx[static_cast<int>(lane) - 1][static_cast<int>(step) - 1];
            fx.pitch = static_cast<std::int8_t>(wholeIn(pitch, -kMaxStepPitch, kMaxStepPitch));
            fx.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? wholeIn(ratchet, 2, kMaxRatchet) : 0);
            fx.reverse = reverse != 0 ? 1 : 0;
            fx.cut = static_cast<std::uint8_t>(cut >= 1 && cut < kCutChoices ? cut : 0);
            fx.throwTo = static_cast<std::uint8_t>(thrown >= 1 && thrown < kThrowChoices ? thrown : 0);
        } else if (word == "note" && isSynth(track->kind)) {
            double step, pitch, length, level, nudge = 0, thrown = 0;
            if (!(in >> step >> pitch >> length >> level)) continue;
            in >> nudge >> thrown;
            if (!(step >= 1) || step > kSteps || !isWhole(step) || pitch < 0 || pitch > 127 || !(level > 0)) continue;
            NotePattern& p = track->notes[scene][bar];
            const int slot = p.freeSlot();
            if (slot < 0 || p.used() >= kMaxNotes) continue; // a sixty-fifth note
            const int s = static_cast<int>(step) - 1;
            p.notes[slot].step = s;
            p.notes[slot].pitch = static_cast<int>(pitch);
            p.notes[slot].length = fractionIn(length, kShortestNote, static_cast<float>(kMaxBars * kSteps)); // to the cell's end, cut in finishLengths: the scene's "bars" may come after
            p.notes[slot].velocity = fractionIn(level, 0.05f, 1.f);
            p.notes[slot].nudge = fractionIn(nudge, -0.5f, 0.5f);
            p.notes[slot].throwTo = static_cast<std::uint8_t>(thrown >= 1 && thrown < kThrowChoices ? thrown : 0);
        } else if (word == "notefx" && isSynth(track->kind)) {
            // For the note of this bar that starts on that step and key (it comes just before this line).
            double step, pitch, ratchet = 1, glide = 0;
            if (!(in >> step >> pitch)) continue;
            in >> ratchet >> glide;
            if (!(step >= 1) || step > kSteps || !isWhole(step) || pitch < 0 || pitch > 127) continue;
            // The note that STARTS there. (Not the cursor's "at": that finds whichever note sounds
            // there first, and a longer note held over this one hid it, so its effect was lost.)
            for (NoteEvent& n : track->notes[scene][bar].notes) {
                if (n.velocity <= 0.f || n.pitch != static_cast<int>(pitch) || n.step != static_cast<int>(step) - 1) continue;
                n.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? wholeIn(ratchet, 2, kMaxRatchet) : 0);
                n.glide = static_cast<std::uint8_t>(glide == 0 ? kGlideOff : (glide >= 1 && glide < 256 ? glideKind(static_cast<int>(glide)) : kGlideHalf)); // (a number not known: on, as it was)
                break;
            }
        } else if (word == "chordfx" && isSynth(track->kind)) {
            double step, ratchet = 1, thrown = 0;
            if (!(in >> step >> ratchet) || !(step >= 1) || step > kSteps || !isWhole(step)) continue;
            in >> thrown;
            for (ChordEvent& c : track->notes[scene][bar].chords) {
                if (c.velocity <= 0.f || c.step != static_cast<int>(step) - 1) continue;
                c.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? wholeIn(ratchet, 2, kMaxRatchet) : 0);
                c.throwTo = static_cast<std::uint8_t>(thrown >= 1 && thrown < kThrowChoices ? thrown : 0);
            }
        } else if (word == "chordplay" && isSynth(track->kind)) {
            // For the chord of this bar that starts on that step (it comes just before this line).
            // Numbers left off are 0; one that is not known is off, one too big is the most.
            double step, value[5] = {0, 0, 0, 0, 0};
            if (!(in >> step) || !(step >= 1) || step > kSteps || !isWhole(step)) continue;
            for (double& v : value) in >> v; // (one that is not there, and all after it, stay 0)
            const auto whole = [](double v) { return v >= 0 && v < 1000 ? static_cast<int>(v) : (v >= 1000 ? 1000 : 0); }; // (not a number: 0)
            for (ChordEvent& c : track->notes[scene][bar].chords) {
                if (c.velocity <= 0.f || c.step != static_cast<int>(step) - 1) continue;
                c.play = chordPlayOf(whole(value[0]), whole(value[1]), whole(value[2]), whole(value[3]), whole(value[4]));
            }
        } else if (word == "chord" && isSynth(track->kind)) {
            double step, length, level, inversion, root, interval;
            std::string name;
            if (!(in >> step >> length >> level >> inversion >> root >> name)) continue;
            if (!(step >= 1) || step > kSteps || !isWhole(step) || root < 0 || root > 127 || !(level > 0)) continue;
            NotePattern& p = track->notes[scene][bar];
            ChordEvent c;
            c.step = static_cast<int>(step) - 1;
            c.length = fractionIn(length, kShortestNote, static_cast<float>(kMaxBars * kSteps)); // cut to the cell in finishLengths
            c.velocity = fractionIn(level, 0.05f, 1.f);
            c.inversion = wholeIn(inversion, -kMaxInversion, kMaxInversion);
            c.root = static_cast<int>(root);
            std::snprintf(c.name, sizeof c.name, "%s", name == "?" ? "" : name.c_str());
            while (c.count < kChordNotes && (in >> interval)) {
                if (interval >= 0 && interval <= 48) c.intervals[c.count++] = static_cast<int>(interval);
            }
            // After its notes, perhaps "nudge" and how far.
            in.clear();
            std::string extra;
            double nudge = 0;
            if ((in >> extra) && extra == "nudge" && (in >> nudge)) c.nudge = fractionIn(nudge, -0.5f, 0.5f);
            // Not one with no notes, not a seventeenth, not two at once, and
            // not past the sixty-four notes.
            int keys[kChordNotes];
            const int slot = p.freeChord();
            bool clash = false;
            for (const ChordEvent& other : p.chords)
                clash = clash || (other.velocity > 0.f && other.step < c.step + c.length && c.step < other.step + other.length);
            if (c.count == 0 || slot < 0 || clash || p.used() + c.pitches(keys) > kMaxNotes) continue;
            p.chords[slot] = c;
        }
    }
}

// After a track's every line: a note or a chord longer than its cell is cut to the cell's end.
// (At its line it is only cut to the longest a cell may be: the scene's "bars" may come after,
// and a note cut to the bars known so far could not be made long again.)
void finishLengths(TrackData& track) {
    if (!isSynth(track.kind)) return;
    for (int s = 0; s < kMaxScenes; ++s) {
        if (!track.notes.has(s)) continue; // looking must not make pieces
        for (int b = 0; b < kMaxBars; ++b) {
            NotePattern& p = track.notes[s][b];
            for (NoteEvent& n : p.notes) {
                if (n.velocity <= 0.f) continue;
                const float longest = longestFrom(track, s, b, n.step);
                if (n.length > longest) n.length = longest;
            }
            for (ChordEvent& c : p.chords) {
                if (c.velocity <= 0.f) continue;
                const float longest = longestFrom(track, s, b, c.step);
                if (c.length > longest) c.length = longest;
            }
        }
    }
}

// After every line: an old song's "steps" made a length, and a length of every step kept as 0.
// `bars` is how long each scene is (Project::sceneBars), the tracks read.
void finishSteps(SongReading& r, const int bars[kMaxScenes]) {
    for (int s = 0; s < kMaxScenes; ++s) {
        // An old song's "13 steps to a bar": its bars but the last whole, and thirteen steps of the last.
        if (r.head.steps[s] == 0 && r.oldSteps[s] < kSteps) r.head.steps[s] = (bars[s] - 1) * kSteps + r.oldSteps[s];
        const int meter = r.head.sceneMeter[s] >= 1 && r.head.sceneMeter[s] <= kSteps ? r.head.sceneMeter[s] : kSteps;
        if (r.head.steps[s] >= bars[s] * meter) r.head.steps[s] = 0; // every step: kept as "all of them"
    }
}

std::size_t afterFirstLine(const std::string& text) {
    const std::size_t first = text.find('\n');
    return first == std::string::npos ? text.size() : first + 1;
}
} // namespace

bool isSongText(const std::string& text) { return hasHead(text, "apotheca-song"); }

bool parseSong(const std::string& text, Project& project) {
    gBytesRead += text.size();
    if (!hasHead(text, "apotheca-song")) return false;

    // Read in place. (It was read into a second Project and copied over this one: 12 MB made and
    // 12 MB copied, 42% of the time.) From here it is a song, and every part of `project` is written.
    static_cast<ProjectHead&>(project) = ProjectHead();
    TrackData* slots[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t) slots[t] = &project.tracks[t];
    SongReading r(project, slots);
    readLines(r, text, afterFirstLine(text), text.size());
    for (int t = r.count; t < kNumTracks; ++t) makeBlank(project.tracks[t]); // no tracks but those the file names
    if (r.count == 0) project.tracks[0].kind = TrackKind::Drums;              // a song always has its drum track
    // A MOD whose target does not fit its track reads OFF: a slot that is empty, a setting that
    // is not there or is a choice, a macro on a track with none (mods.hpp).
    for (int t = 0; t < r.count; ++t) {
        modsFit(project.tracks[t]);
        finishLengths(project.tracks[t]);
    }
    int bars[kMaxScenes];
    for (int s = 0; s < kMaxScenes; ++s) bars[s] = project.sceneBars(s);
    finishSteps(r, bars);
    return true;
}

SongReader::SongReader(const std::string& text, ProjectHead& head, TrackData* const* slots)
    : text_(text), reading_(new SongReading(head, slots)) {
    head = ProjectHead();
}
SongReader::~SongReader() = default;

void SongReader::readHead(std::size_t to) {
    gBytesRead += to;
    const std::size_t first = afterFirstLine(text_);
    if (first < to) readLines(*reading_, text_, first, to);
}

void SongReader::readTrack(int index, std::size_t from, std::size_t to) {
    gBytesRead += to - from;
    reading_->count = index;
    reading_->track = nullptr;
    reading_->heldSettings.clear();
    readLines(*reading_, text_, from, to);
    if (reading_->count == index + 1) {
        modsFit(*reading_->slots[index]);
        finishLengths(*reading_->slots[index]);
    }
}

void SongReader::finish(const int bars[kMaxScenes]) { finishSteps(*reading_, bars); }

namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; } // as `>>` skips
// The next word of a line, from `at`, as `>>` reads it (the line ends at `end`): where it starts, and where it ends.
std::size_t wordAt(const std::string& text, std::size_t at, std::size_t end, std::size_t& from) {
    while (at < end && space(text[at])) ++at;
    from = at;
    while (at < end && !space(text[at])) ++at;
    return at;
}
} // namespace

bool cutSongAt(const std::string& text, std::vector<std::size_t>& starts) {
    starts.clear();
    if (!hasHead(text, "apotheca-song")) return false;
    bool inTrack = false;
    static const char* const songsOwn[] = {"tempo", "length", "steps", "repeats", "stempi", "meter", "pack", "busfx", "master", "songend", "scale"};
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        end = end == std::string::npos ? text.size() : end + 1;
        // (The first line, "apotheca-song", is neither a track's nor one of the song's own below.)
        std::size_t word = 0;
        const std::size_t after = wordAt(text, at, end, word);
        const auto is = [&](const char* w) { const std::size_t n = std::strlen(w); return after - word == n && text.compare(word, n, w) == 0; };
        if (is("track")) {
            std::size_t kindFrom = 0;
            const std::size_t kindTo = wordAt(text, after, end, kindFrom);
            if (trackKindFromWord(text.substr(kindFrom, kindTo - kindFrom).c_str()) == TrackKind::None || starts.size() >= static_cast<std::size_t>(kNumTracks)) return false;
            starts.push_back(at);
            inTrack = true;
        } else if (inTrack) {
            for (const char* own : songsOwn)
                if (is(own)) return false; // the song's, wherever it stands
        }
        at = end;
    }
    return true;
}

bool cutSong(const std::string& text, std::string& head, std::vector<std::string>& tracks) {
    head.clear();
    tracks.clear();
    std::vector<std::size_t> starts;
    if (!cutSongAt(text, starts)) return false;
    head.assign(text, 0, starts.empty() ? text.size() : starts[0]);
    for (std::size_t t = 0; t < starts.size(); ++t) {
        const std::size_t to = t + 1 < starts.size() ? starts[t + 1] : text.size();
        tracks.emplace_back(text, starts[t], to - starts[t]);
    }
    return true;
}

bool doubleTrackText(std::string& text, int track) {
    std::string head;
    std::vector<std::string> tracks;
    if (track < 0 || !cutSong(text, head, tracks) || track >= static_cast<int>(tracks.size()) || static_cast<int>(tracks.size()) >= kNumTracks) return false;
    std::string out = head;
    for (int t = 0; t < static_cast<int>(tracks.size()); ++t) {
        std::string one = tracks[static_cast<std::size_t>(t)];
        if (!one.empty() && one.back() != '\n') one += '\n';
        out += one;
        if (t == track) out += one;
    }
    text = out;
    return true;
}

bool dropTrackText(std::string& text, int track) {
    if (track < 0 || !hasHead(text, "apotheca-song")) return false;
    std::size_t from = std::string::npos, to = text.size();
    int count = 0;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::istringstream in(text.substr(at, end - at));
        std::string word, which;
        if ((in >> word) && word == "track") {
            if (from != std::string::npos) { // the next track's line, of any kind: the end of the one taken out
                to = at;
                break;
            }
            in >> which;
            if (trackKindFromWord(which.c_str()) != TrackKind::None && count < kNumTracks && count++ == track) from = at; // as parseSong counts
        }
        at = end + 1;
    }
    if (from == std::string::npos) return false;
    text.erase(from, to - from);
    return true;
}

// The first word of the line at `at` (to `end`) is one of the three a recording's name is found by. A plain
// look at the bytes, as `in >> word` would read them (white space first): most of a song's lines are a
// pattern's, and a stream made for each of them made a rename's every step of undo cost a whole read of the
// song (review of G04, 2026-11-25).
static bool namesARecording(const std::string& text, std::size_t at, std::size_t end) {
    while (at < end && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
    for (const char* word : {"track", "sample", "sound"}) {
        const std::size_t length = std::strlen(word);
        if (end - at >= length && text.compare(at, length, word) == 0 && (end - at == length || std::isspace(static_cast<unsigned char>(text[at + length]))))
            return true;
    }
    return false;
}

bool renameRecordings(std::string& text, const std::vector<std::pair<std::string, std::string>>& renames) {
    if (!hasHead(text, "apotheca-song")) return false;
    std::string out;
    out.reserve(text.size());
    bool changed = false;
    TrackKind kind = TrackKind::None; // of the track the line belongs to, told the way parseSong tells it
    int count = 0;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        if (!namesARecording(text, at, end)) { // (as it is, the '\r' of a Windows line too)
            out.append(text, at, end - at);
            if (end < text.size()) out += '\n';
            at = end + 1;
            continue;
        }
        std::string line = text.substr(at, end - at);
        const bool windows = !line.empty() && line.back() == '\r'; // read without it, as parseSong reads; written back with it
        if (windows) line.pop_back();
        std::istringstream in(line);
        std::string word, name;
        double lane = 0, fit = 0;
        in >> word;
        if (word == "track") {
            std::string which;
            in >> which;
            kind = trackKindFromWord(which.c_str());
            if (kind != TrackKind::None && count++ >= kNumTracks) kind = TrackKind::None; // a thirteenth track is not read
        } else if ((word == "sample" && kind == TrackKind::Drums && (in >> lane >> fit) && lane >= 1 && lane <= kDrumLanes) ||
                   (word == "sound" && holdsRecording(kind))) {
            std::getline(in >> std::ws, name); // the rest of the line, as it is read
            std::string now = name;
            for (const auto& rename : renames)
                if (now == rename.first) now = rename.second;
            if (now != name) {
                line.replace(line.size() - name.size(), name.size(), now);
                changed = true;
            }
        }
        out += line;
        if (windows) out += '\r';
        if (end < text.size()) out += '\n';
        at = end + 1;
    }
    if (changed) text.swap(out);
    return changed;
}

bool writeTextFile(const std::string& path, const std::string& text) {
    // Written beside the real file and moved into place, so a crash or a flat
    // battery mid-write cannot leave half a song. The new file is on the card
    // (fsync) before it takes the old one's place: else the card may be given
    // the move before the words, and a flat battery leaves an empty song.
    const std::string temporary = path + ".part";
    std::FILE* f = std::fopen(temporary.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fflush(f) == 0 && ok;
    ok = ok && fsync(fileno(f)) == 0;
    if (std::fclose(f) != 0 || !ok) {
        std::remove(temporary.c_str());
        return false;
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) return false;
    // And the move itself, which is written in the folder.
    const std::size_t slash = path.rfind('/');
    const std::string folder = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
    const int dir = open(folder.c_str(), O_RDONLY);
    if (dir >= 0) {
        fsync(dir); // (the file is in place either way: this is only to have it there for good, now)
        close(dir);
    }
    return true;
}

namespace {
// The files of a song in a folder: "<name>.song" (if there) and its revs, "<name>.<n>.rev".
std::vector<std::string> songFiles(const std::string& folder, const std::string& name) {
    std::vector<std::string> files;
    if (DIR* dir = opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            const bool song = file == name + ".song";
            const bool rev = file.size() > name.size() + 5 && file.compare(0, name.size() + 1, name + ".") == 0 &&
                             file.compare(file.size() - 4, 4, ".rev") == 0 &&
                             file.find_first_not_of("0123456789", name.size() + 1) == file.size() - 4;
            if (!(song || rev)) continue;
            struct stat info; // a folder (or anything else) named like one is not one
            if (lstat((folder + "/" + file).c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
            files.push_back(file);
        }
        closedir(dir);
    }
    return files;
}
// The folder itself, written for good: a rename in it is there after a crash too.
void fsyncDir(const std::string& folder) {
    const int dir = open(folder.c_str(), O_RDONLY);
    if (dir >= 0) {
        fsync(dir); // (the files are in place either way: this is only to have them there for good, now)
        close(dir);
    }
}
} // namespace

bool binSong(const std::string& folder, const std::string& name) {
    std::vector<std::string> files = songFiles(folder, name);
    bool there = false;
    for (const std::string& file : files) there = there || file == name + ".song";
    if (!there) return false;
    const std::string bin = folder + "/deleted";
    mkdir(bin.c_str(), 0755); // fine if it is already there
    // A name nothing in "deleted" has, the song's or a rev's: a song deleted before under this
    // name (new songs reuse the names of deleted ones) is not written over.
    std::string as = name;
    for (int n = 2; !songFiles(bin, as).empty(); ++n) as = name + " " + std::to_string(n);
    std::vector<std::pair<std::string, std::string>> moved; // (from, to): put back if one fails
    for (const std::string& file : files) {
        const std::string to = as + file.substr(name.size()); // ".song", or ".<n>.rev"
        if (std::rename((folder + "/" + file).c_str(), (bin + "/" + to).c_str()) != 0) {
            for (std::vector<std::pair<std::string, std::string>>::const_reverse_iterator back = moved.rbegin(); back != moved.rend(); ++back)
                std::rename((bin + "/" + back->second).c_str(), (folder + "/" + back->first).c_str());
            return false; // the song is all where it was (or all but what the disk refused twice)
        }
        moved.emplace_back(file, to);
    }
    fsyncDir(folder);
    fsyncDir(bin);
    return true;
}

bool readTextFile(const std::string& path, std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    text.clear();
    char block[4096];
    std::size_t got;
    while ((got = std::fread(block, 1, sizeof block, f)) > 0) {
        if (text.size() + got > kLargestRead) { // a corrupt giant is not read into RAM
            std::fclose(f);
            text.clear();
            return false;
        }
        text.append(block, got);
    }
    const bool whole = !std::ferror(f); // a card that fails part-way must not pass for a short file: it may be written back
    std::fclose(f);
    return whole;
}

std::string kitText(const TrackData& track) {
    std::string out = "apotheca-kit 1\n";
    putKit(out, track);
    return out;
}

void copyKit(const TrackData& from, TrackData& to) {
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        to.laneLevel[lane] = from.laneLevel[lane];
        std::memcpy(to.laneSample[lane], from.laneSample[lane], sizeof to.laneSample[lane]);
        to.laneFit[lane] = from.laneFit[lane];
        to.laneOneSlice[lane] = from.laneOneSlice[lane];
        to.laneSynth[lane] = from.laneSynth[lane];
        to.laneEdit[lane] = from.laneEdit[lane];
        to.laneFeel[lane] = from.laneFeel[lane];
        to.laneSwing[lane] = from.laneSwing[lane];
        to.laneChoke[lane] = from.laneChoke[lane];
        to.laneCuts[lane] = from.laneCuts[lane];
    }
}

bool readKit(const std::string& text, TrackData& kit) {
    if (!hasHead(text, "apotheca-kit")) return false;
    const std::size_t line = text.find('\n');
    if (line == std::string::npos) return false;
    // A kit holds only a kit's own lines: anything else (a scene, another track) is refused here,
    // not read as a song and silently dropped from what comes back.
    static const char* const kitWords[] = {"lanes", "sample", "lanefit", "lanesynth", "oneslice", "laneedit", "feel", "swing", "choke", "slices"};
    std::size_t at = line + 1;
    while (at < text.size()) {
        const std::size_t stop = text.find('\n', at);
        const std::size_t end = stop == std::string::npos ? text.size() : stop;
        std::size_t word = at;
        while (word < end && (text[word] == ' ' || text[word] == '\t' || text[word] == '\r')) ++word;
        std::size_t wordEnd = word;
        while (wordEnd < end && text[wordEnd] != ' ' && text[wordEnd] != '\t' && text[wordEnd] != '\r') ++wordEnd;
        if (wordEnd > word) {
            const std::string first = text.substr(word, wordEnd - word);
            bool known = false;
            for (const char* knownWord : kitWords) known = known || first == knownWord;
            if (!known) return false;
        }
        if (stop == std::string::npos) break;
        at = stop + 1;
    }
    // Read as a song of one drum track would be: the kit's lines are the song file's own, and parseSong
    // is the one reader of them (every number held, every name checked, as a song's).
    std::unique_ptr<Project> song(new Project);
    if (!parseSong("apotheca-song 1\ntrack drums\n" + text.substr(line + 1), *song) ) return false;
    copyKit(song->tracks[0], kit);
    return true;
}

} // namespace apotheca
