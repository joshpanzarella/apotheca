#include "icons.hpp"

#include <cstring>

#include "ui.hpp"

namespace apotheca {

using namespace ui;

namespace {

constexpr IconFamily S = IconFamily::Sound, M = IconFamily::Movement, T = IconFamily::Time;

// The drawings. (The first thirteen were the macros' pictures; their lines are as they were.)
const Icon kIcons[] = {
    {"filter", S, {{4, {0, 4, 9, 4, 13, 2, 19, 13}}}},                                            // a low pass: TONE, CUTOFF
    {"wave", M, {{7, {0, 7, 3, 2, 6, 7, 9, 12, 12, 7, 15, 2, 19, 9}}}},                            // a slow wave: MOTION, RATE
    {"rise", T, {{3, {0, 13, 9, 1, 19, 1}}}},                                                     // ATTACK
    {"fall", T, {{5, {0, 1, 6, 1, 9, 8, 13, 11, 19, 13}}}},                                       // RELEASE, DECAY
    {"two waves", M, {{5, {0, 7, 4, 2, 9, 12, 14, 2, 19, 7}}, {5, {0, 9, 5, 3, 11, 12, 17, 3, 19, 6}}}}, // DETUNE
    {"wobble", M, {{8, {0, 7, 3, 6, 6, 8, 9, 4, 12, 10, 15, 1, 18, 13, 19, 9}}}},                 // VIBRATO
    {"three waves", M, {{5, {0, 3, 5, 1, 10, 3, 15, 5, 19, 3}}, {5, {0, 7, 5, 5, 10, 7, 15, 9, 19, 7}}, {5, {0, 11, 5, 9, 10, 11, 15, 13, 19, 11}}}}, // CHORUS
    {"steps", S, {{6, {0, 13, 6, 13, 6, 7, 13, 7, 13, 1, 19, 1}}}},                               // OCTAVE, a pitch
    {"shapes", S, {{3, {0, 13, 4, 1, 8, 13}}, {4, {11, 13, 11, 2, 18, 2, 18, 13}}}},              // SHAPE, WAVE
    {"cross", S, {{2, {0, 1, 19, 13}}, {2, {0, 13, 19, 1}}}},                                     // MIX, BLEND
    {"peak", S, {{5, {0, 13, 4, 3, 8, 11, 13, 1, 19, 13}}}},                                      // FORMANT, a resonance
    {"width", M, {{3, {6, 2, 0, 7, 6, 12}}, {3, {13, 2, 19, 7, 13, 12}}, {2, {9, 5, 10, 9}}}},    // WIDTH, SPREAD
    {"worn", M, {{7, {0, 7, 3, 5, 6, 9, 9, 6, 12, 10, 15, 4, 19, 7}}}},                           // VINTAGE, WOW
    // ...and the rest.
    {"high pass", S, {{4, {0, 13, 6, 2, 10, 4, 19, 4}}}},                                          // HPF, LOW CUT
    {"resonance", S, {{5, {0, 6, 9, 6, 13, 1, 15, 8, 19, 13}}}},                                  // RESO: a peak at the cutoff
    {"fine", S, {{4, {0, 9, 9, 9, 9, 5, 19, 5}}, {2, {9, 12, 9, 13}}}},                            // FINE, cents
    {"level", S, {{2, {2, 13, 2, 9}}, {2, {8, 13, 8, 6}}, {2, {14, 13, 14, 3}}, {2, {19, 13, 19, 1}}}}, // LEVEL, GAIN, VELOCITY
    {"clipped", S, {{6, {0, 7, 3, 1, 8, 1, 11, 13, 16, 13, 19, 7}}}},                              // DRIVE, FUZZ, DIRT
    {"echoes", T, {{2, {1, 13, 1, 1}}, {2, {8, 13, 8, 5}}, {2, {14, 13, 14, 8}}, {2, {19, 13, 19, 11}}}}, // TIME, DELAY
    {"loop", T, {{5, {4, 3, 16, 3, 19, 7, 16, 11, 4, 11}}, {4, {4, 11, 1, 7, 4, 3, 7, 1}}}},      // FEEDBACK, REPEATS
    {"space", T, {{3, {4, 3, 7, 7, 4, 11}}, {3, {9, 1, 13, 7, 9, 13}}, {3, {14, 0, 19, 7, 14, 14}}}}, // SIZE, a room
    {"place", S, {{2, {0, 10, 19, 10}}, {2, {9, 2, 9, 13}}, {3, {6, 2, 9, 5, 12, 2}}}},            // POSITION, SCAN, START
    {"dots", M, {{2, {2, 3, 4, 3}}, {2, {9, 9, 11, 9}}, {2, {15, 4, 17, 4}}, {2, {5, 12, 7, 12}}, {2, {16, 11, 18, 11}}}}, // DENSITY, SPRAY
    {"die", M, {{5, {2, 1, 17, 1, 17, 13, 2, 13, 2, 1}}, {2, {6, 4, 7, 4}}, {2, {9, 7, 10, 7}}, {2, {12, 10, 13, 10}}}}, // DICE, CHANCE
    {"crush", S, {{8, {0, 13, 5, 13, 5, 9, 10, 9, 10, 5, 15, 5, 15, 1, 19, 1}}}},                  // BITS
    {"knee", S, {{3, {0, 13, 10, 4, 19, 1}}, {2, {0, 13, 19, 0}}}},                                // THRESHOLD, RATIO, CEILING
    {"envelope", T, {{5, {0, 13, 4, 1, 9, 6, 15, 6, 19, 13}}}},                                    // ENV, SWEEP, a step's RATE
    {"plateau", T, {{4, {0, 13, 3, 4, 16, 4, 19, 13}}}},                                           // SUSTAIN, HOLD
    {"keys", S, {{5, {0, 2, 19, 2, 19, 12, 0, 12, 0, 2}}, {2, {5, 2, 5, 12}}, {2, {10, 2, 10, 12}}, {2, {15, 2, 15, 12}}, {2, {7, 2, 7, 7}}}}, // KEY FOLLOW
    {"backwards", T, {{2, {19, 7, 1, 7}}, {3, {6, 2, 1, 7, 6, 12}}}},                              // REVERSE
    {"struck", S, {{2, {1, 10, 18, 10}}, {2, {1, 12, 18, 12}}, {2, {10, 1, 7, 7}}, {2, {6, 6, 9, 8}}}}, // MATERIAL, STRIKE, a body
    {"gate", T, {{6, {0, 13, 4, 13, 4, 2, 14, 2, 14, 13, 19, 13}}}},                               // GATE, LENGTH
    {"drops", M, {{2, {0, 7, 6, 7}}, {2, {9, 7, 11, 7}}, {2, {14, 7, 19, 7}}}},                    // DROPS
    {"drip", M, {{4, {9, 1, 5, 8, 9, 13, 13, 8}}, {2, {13, 8, 9, 1}}}},                            // DRIP
    {"sub", S, {{5, {0, 7, 5, 13, 10, 7, 15, 1, 19, 7}}}},                                         // SUB, a low wave
    {"power", S, {{2, {10, 0, 10, 7}}, {6, {5, 3, 2, 7, 5, 12, 15, 12, 18, 7, 15, 3}}}},           // ON
    {"source", T, {{5, {3, 4, 16, 4, 16, 12, 3, 12, 3, 4}}, {2, {3, 4, 9, 1}}, {2, {16, 4, 10, 1}}}}, // SOURCE: a drum
    {"mirror", S, {{3, {0, 13, 10, 1, 19, 13}}, {2, {10, 0, 10, 13}}}},                            // SYMMETRY
    {"lines", S, {{2, {0, 3, 19, 3}}, {2, {0, 7, 19, 7}}, {2, {0, 11, 19, 11}}}},                  // LINES, ALGORITHM, MODE
    {"bright", S, {{2, {10, 0, 10, 4}}, {2, {10, 10, 10, 14}}, {2, {3, 7, 7, 7}}, {2, {13, 7, 17, 7}}, {5, {8, 5, 12, 5, 12, 9, 8, 9, 8, 5}}}}, // BRIGHT, BRILLIANCE
    {"motor", M, {{5, {0, 7, 4, 4, 8, 7, 12, 4, 19, 7}}, {5, {0, 7, 4, 10, 8, 7, 12, 10, 19, 7}}}}, // MOTOR, TREMOLO
    {"bloom", T, {{4, {0, 13, 6, 6, 12, 2, 19, 1}}, {3, {0, 13, 10, 11, 19, 7}}}},                 // BLOOM, SHIMMER
};

const Icon& icon(const char* id) {
    for (const Icon& i : kIcons)
        if (std::strcmp(i.id, id) == 0) return i;
    return kIcons[0];
}

bool has(const char* name, const char* word) {
    // A whole word of the name: "RATE" in "LFO RATE", not in "SEPARATE".
    const std::size_t n = std::strlen(word);
    for (const char* at = std::strstr(name, word); at; at = std::strstr(at + 1, word)) {
        const bool start = at == name || at[-1] == ' ' || at[-1] == '>';
        const bool end = at[n] == 0 || at[n] == ' ';
        if (start && end) return true;
    }
    return false;
}

// The rules, first that takes the name wins: a name of one word is its word; a name of more is read by its LAST
// word first ("LFO DEPTH" is a depth, "AMP DECAY" a decay), then its others.
struct Rule {
    const char* word;
    const char* icon;
};
const Rule kRules[] = {
    {"TONE", "filter"}, {"CUTOFF", "filter"}, {"FILTER", "filter"}, {"DAMPING", "filter"}, {"HIGH", "filter"},
    {"BRIGHT", "bright"}, {"BRILLIANCE", "bright"},
    {"HPF", "high pass"}, {"LOW", "high pass"},
    {"RESO", "resonance"}, {"RESONANCE", "resonance"}, {"FORMANT", "peak"}, {"VOWEL", "peak"}, {"MID", "peak"}, {"FREQ", "peak"},
    {"SHIFT", "steps"}, {"OCTAVE", "steps"}, {"PITCH", "steps"}, {"TUNE", "steps"}, {"COARSE", "steps"}, {"TRANSPOSE", "steps"},
    {"SEMI", "steps"}, {"NOTE", "steps"}, {"RANGE", "steps"}, {"STEPS", "steps"},
    {"FINE", "fine"},
    {"DETUNE", "two waves"}, {"PAIR", "two waves"}, {"UNISON", "two waves"}, {"CROSS", "two waves"},
    {"CHORUS", "three waves"}, {"ENSEMBLE", "three waves"},
    {"VIBRATO", "wobble"}, {"MODULATION", "wobble"}, {"MOD", "wobble"},
    {"MOTION", "wave"}, {"LFO", "wave"}, {"RATE", "wave"}, {"SPEED", "wave"}, {"SYNC", "wave"}, {"DELAY", "echoes"},
    {"WOW", "worn"}, {"VINTAGE", "worn"}, {"AGE", "worn"},
    {"MOTOR", "motor"},
    {"ATTACK", "rise"}, {"RELEASE", "fall"}, {"DECAY", "fall"},
    {"SUSTAIN", "plateau"}, {"HOLD", "plateau"}, {"LAST", "plateau"},
    {"ENV", "envelope"}, {"SWEEP", "envelope"}, {"ACCENT", "envelope"}, {"SLIDE", "envelope"}, {"BREAK", "envelope"}, {"CURVE", "envelope"},
    {"SHAPE", "shapes"}, {"WAVE", "shapes"}, {"MODEL", "shapes"}, {"TABLE", "shapes"}, {"TYPE", "shapes"},
    {"MIX", "cross"}, {"BLEND", "cross"},
    {"WIDTH", "width"}, {"SPREAD", "width"}, {"STEREO", "width"}, {"DEPTH", "width"},
    {"LEVEL", "level"}, {"GAIN", "level"}, {"OUTPUT", "level"}, {"VELOCITY", "level"}, {"AMOUNT", "level"}, {"SENS", "level"},
    {"POLARITY", "mirror"}, {"SYMMETRY", "mirror"},
    {"DRIVE", "clipped"}, {"FUZZ", "clipped"}, {"DIRT", "clipped"}, {"BUZZ", "clipped"},
    {"TIME", "echoes"}, {"PRE", "echoes"}, {"REPEATS", "loop"}, {"FEEDBACK", "loop"},
    {"SIZE", "space"}, {"BODY", "struck"}, {"MATERIAL", "struck"}, {"STRIKE", "struck"}, {"HARDNESS", "struck"}, {"STRETCH", "struck"},
    {"POSITION", "place"}, {"SCAN", "place"}, {"START", "place"}, {"SCATTER", "dots"},
    {"DENSITY", "dots"}, {"SPRAY", "dots"},
    {"DICE", "die"}, {"CHANCE", "die"},
    {"BITS", "crush"},
    {"THRESHOLD", "knee"}, {"RATIO", "knee"}, {"CEILING", "knee"},
    {"KEY", "keys"}, {"SCALING", "keys"},
    {"REVERSE", "backwards"},
    {"GATE", "gate"}, {"LENGTH", "gate"},
    {"DROPS", "drops"}, {"DRIP", "drip"}, {"SUB", "sub"}, {"ON", "power"}, {"SOURCE", "source"},
    {"LINES", "lines"}, {"ALGORITHM", "lines"}, {"MODE", "lines"},
    {"BLOOM", "bloom"}, {"SHIMMER", "bloom"},
};

const Icon* byWord(const char* name, const char* word) {
    for (const Rule& rule : kRules)
        if (std::strcmp(rule.word, word) == 0) return &icon(rule.icon);
    (void)name;
    return nullptr;
}

} // namespace

const Icon* iconFor(const char* name) {
    if (!name || !name[0]) return nullptr;
    // An envelope page's step ("1" to "8"): the envelope.
    if (name[0] >= '1' && name[0] <= '9' && name[1] == 0) return &icon("envelope");
    // The name's words, last first; a word of digits ("LEVEL 2", "RATE 1") is passed over.
    char words[48];
    std::strncpy(words, name[0] == '>' ? name + 1 : name, sizeof words - 1);
    words[sizeof words - 1] = 0;
    const char* list[8];
    int count = 0;
    for (char* w = std::strtok(words, " "); w && count < 8; w = std::strtok(nullptr, " "))
        if (!(w[0] >= '0' && w[0] <= '9')) list[count++] = w;
    for (int i = count - 1; i >= 0; --i)
        if (const Icon* found = byWord(name, list[i])) return found;
    return nullptr;
}

void drawIcon(SDL_Renderer* r, const Icon& icon, int x, int y, bool lit) {
    const SDL_Color family = icon.family == IconFamily::Sound ? kTeal : icon.family == IconFamily::Movement ? kPurple : kText;
    const SDL_Color c = lit ? family : faded(family, 110);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (const IconStroke& s : icon.strokes)
        for (int i = 0; i + 1 < s.n; ++i)
            for (int thick = 0; thick < 2; ++thick) // two passes: one pixel is too faint at this size
                SDL_RenderDrawLine(r, x + s.p[2 * i], y + s.p[2 * i + 1] + thick, x + s.p[2 * i + 2], y + s.p[2 * i + 3] + thick);
}

} // namespace apotheca
