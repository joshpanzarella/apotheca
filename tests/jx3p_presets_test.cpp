// The JX-3P's 32 factory patches (plan batch 7): the engine's table against the booklet as
// read (tests/jx3p_nam.inc), the booklet's own three checks, and every patch played: it sounds,
// it stays bounded, it ends, it makes no number too small to hold, and it is the same sound
// however it is cut into blocks.
//
// Each test says FAITHFUL (held to a published figure: its source named) or SELF (held to the
// plan's own guess). NAM is the maker's 1983 booklet of the programmer's settings (NAM-0150).
//
//   jx3p_presets_test                 all of them
//   jx3p_presets_test testNam ...     just those
//   jx3p_presets_test --print         how loud each patch is, and how long it rings

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/jx3p.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::jx3p;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

struct Read { const char* number; const char* name; const char* panel; };
static const Read kNam[] = {
#include "jx3p_nam.inc"
};
static const int kNamCount = static_cast<int>(sizeof kNam / sizeof kNam[0]);

static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

// ---- the booklet's line, made the engine's numbers ---------------------------------------------------
// Written apart from the script that wrote the engine's table: one word at a time, by its place.

static int oneOf(const std::string& word, const char* const* words, int count, const char* what) {
    for (int i = 0; i < count; ++i)
        if (word == words[i]) return i;
    std::printf("FAIL the booklet's %s reads \"%s\"\n", what, word.c_str());
    ++failures;
    return 0;
}
// A knob's printed number, in tenths: "5.3" is 53, "10" is 100, "0" is 0. Not through a float.
static int tenths(const std::string& word) {
    int whole = 0, part = 0;
    std::size_t i = 0;
    bool any = false;
    for (; i < word.size() && word[i] >= '0' && word[i] <= '9'; ++i) whole = whole * 10 + (word[i] - '0'), any = true;
    if (i < word.size() && word[i] == '.') {
        ++i;
        if (i + 1 != word.size() || word[i] < '0' || word[i] > '9') any = false;
        else part = word[i] - '0', ++i;
    }
    if (!any || i != word.size() || whole * 10 + part > 100) {
        std::printf("FAIL the booklet's number reads \"%s\"\n", word.c_str());
        ++failures;
        return 0;
    }
    return whole * 10 + part;
}
struct Panel {
    int value[kPatchSettings];
    bool drawn[kPatchSettings];
};
static Panel panelOf(const char* line) {
    std::vector<std::string> words;
    std::istringstream in(line);
    for (std::string w; in >> w;)
        if (w != "|") words.push_back(w);
    Panel p{};
    if (words.size() != static_cast<std::size_t>(kPatchSettings)) {
        std::printf("FAIL the booklet's line has %d words: %s\n", static_cast<int>(words.size()), line);
        ++failures;
        return p;
    }
    static const char* const ranges[] = {"16", "8", "4"};
    static const char* const waves[] = {"SAW", "PUL", "SQU", "NOISE"};
    static const char* const cross[] = {"OFF", "SYNC", "METAL"};
    static const char* const onOff[] = {"off", "on"};
    static const char* const polarity[] = {"N", "I"};
    static const char* const modes[] = {"ENV", "GATE"};
    static const char* const lfoWaves[] = {"SINE", "SQUARE", "RANDOM"};
    // What the engine keeps where the booklet draws nothing.
    int blank[kPatchSettings] = {};
    blank[Dco1Range] = blank[Dco2Range] = 1;
    blank[Dco2Tune] = blank[Dco2Fine] = 50;
    for (int id = 0; id < kPatchSettings; ++id) {
        const std::string& w = words[static_cast<std::size_t>(id)];
        p.drawn[id] = w != "_";
        if (!p.drawn[id]) {
            p.value[id] = blank[id];
            continue;
        }
        switch (id) {
        case Dco1Range: case Dco2Range: p.value[id] = oneOf(w, ranges, 3, "RANGE"); break;
        case Dco1Wave: p.value[id] = oneOf(w, waves, 3, "DCO-1 WAVE"); break;
        case Dco2Wave: p.value[id] = oneOf(w, waves, 4, "DCO-2 WAVE"); break;
        case Dco2CrossMod: p.value[id] = oneOf(w, cross, 3, "CROSS MOD"); break;
        case Dco1Lfo: case Dco1Env: case Dco2Lfo: case Dco2Env: case Chorus: p.value[id] = oneOf(w, onOff, 2, "switch"); break;
        case DcoEnvPolarity: case VcfEnvPolarity: p.value[id] = oneOf(w, polarity, 2, "POLARITY"); break;
        case VcaMode: p.value[id] = oneOf(w, modes, 2, "VCA MODE"); break;
        case LfoWave: p.value[id] = oneOf(w, lfoWaves, 3, "LFO WAVE"); break;
        default: p.value[id] = tenths(w); break;
        }
    }
    return p;
}

// ---- playing a patch ---------------------------------------------------------------------------

static std::unique_ptr<Jx3p> patched(int preset) {
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->prepare(48000.f);
    j->selectPreset(preset);
    return j;
}
static const int kChord[4] = {48, 55, 60, 64};
struct Heard {
    std::vector<float> left, right;
    double heldRms = 0.0;    // while the chord is down: its loudest tenth of a second (a struck sound has died away by the end)
    double peak = 0.0;
    double rings = 0.0;      // seconds from the keys' letting go to exact silence, or -1: it never came
    bool number = true;      // every sample a number
    std::uint64_t clipped = 0; // samples of a voice the VCA's clip bent
};
// A chord held two seconds at full velocity (the patch as written), let go, and listened to until
// the synth puts out exact zeros for a tenth of a second (40 s at most: RELEASE 10 is 10 s).
// (`keys` of kSixKeys in place of the chord's four, and every macro at `macros`, where they are given.)
static const int kSixKeys[6] = {36, 48, 60, 67, 76, 88};
static Heard hear(int preset, int block = 256, int keys = 0, float macros = 0.5f) {
    std::unique_ptr<Jx3p> j = patched(preset);
    if (macros != 0.5f)
        for (int m = 0; m < kSynthMacros; ++m) j->setMacro(m, macros);
    Heard h;
    const long hold = 2 * 48000, most = hold + 40 * 48000;
    std::vector<float> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
    const int* const played = keys ? kSixKeys : kChord;
    const int count = keys ? keys : 4;
    for (int k = 0; k < count; ++k) j->noteOn(played[k], 1.f);
    long quiet = 0, lastSound = 0;
    for (long done = 0; done < most;) {
        if (done == hold)
            for (int k = 0; k < count; ++k) j->noteOff(played[k]);
        const int n = done < hold && hold - done < block ? static_cast<int>(hold - done) : block; // the keys go on their own sample, however it is cut
        j->render(l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) {
            const float a = l[static_cast<std::size_t>(i)], b = r[static_cast<std::size_t>(i)];
            if (!std::isfinite(a) || !std::isfinite(b)) h.number = false;
            h.peak = std::fmax(h.peak, std::fmax(std::fabs(a), std::fabs(b)));
            if (a != 0.f || b != 0.f) quiet = 0, lastSound = done + i;
            else ++quiet;
        }
        done += n;
        h.left.insert(h.left.end(), l.begin(), l.begin() + n);
        h.right.insert(h.right.end(), r.begin(), r.begin() + n);
        if (done > hold && quiet >= 4800) {
            h.rings = static_cast<double>(lastSound - hold) / 48000.0;
            break;
        }
    }
    h.clipped = j->samplesClipped();
    if (quiet < 4800) h.rings = -1.0;
    else if (h.rings < 0.0) h.rings = 0.0; // (a struck sound, over before the keys were let go)
    for (std::size_t from = 0; from + 4800 <= static_cast<std::size_t>(hold); from += 2400) h.heldRms = std::fmax(h.heldRms, rmsOf(h.left, from, from + 4800));
    return h;
}

static bool gPrint = false;

// ---- the tests ---------------------------------------------------------------------------------

// WHAT THIS TEST CANNOT SEE (the review, 2026-11-15): a digit misread off the scan. The table was
// written from tests/jx3p_nam.inc by a script, so a wrong reading is in both and passes; only the
// scan can say. The reading was made twice and a third time by the pointers' angles (the plan,
// batch 7), and the review read two panels again by eye (B4, B15: every number and switch as the
// .inc has it). The other thirty have not been read a fourth time.
// THE TABLE IS THE BOOKLET (FAITHFUL: NAM, as read twice): 32 patches, A1 to B16, in two folders
// of sixteen; every printed value stored exactly, as tenths of the dial; every switch where it is
// drawn; what is not drawn at the stated blanks. Their names, as the booklet's English headings
// (and Synthmania's list of the machine's own).
static void testNam() {
    CHECK(kNamCount == 32 && Jx3p::presetCount() == 32);
    int printed = 0, blanks = 0;
    for (int i = 0; i < kNamCount && i < Jx3p::presetCount(); ++i) {
        const Panel p = panelOf(kNam[i].panel);
        const Jx3pPatch patch = Jx3p::presetPatch(i);
        for (int id = 0; id < kPatchSettings; ++id) {
            if (patch.value[id] != p.value[id]) {
                std::printf("FAIL %s %s: setting %d is %d, the booklet has %d\n", kNam[i].number, kNam[i].name, id, patch.value[id], p.value[id]);
                ++failures;
            }
            CHECK(p.value[id] >= settingMin(id) && p.value[id] <= settingMax(id));
            p.drawn[id] ? ++printed : ++blanks;
        }
        CHECK(std::string(Jx3p::presetName(i)) == kNam[i].name);
        // The number is the factory's: a folder a bank, sixteen each, in order.
        const std::string number = std::string(i < 16 ? "A" : "B") + std::to_string(i % 16 + 1);
        CHECK(number == kNam[i].number);
        CHECK(std::string(Jx3p::presetCategory(i)) == (i < 16 ? "BANK A" : "BANK B"));
        // The track's own two are a new track's, whatever the patch.
        CHECK(patch.value[Velocity] == kNewTrackVelocity && patch.value[Brilliance] == kNewTrackBrilliance);
    }
    // Counted off the scan: 79 knobs and switches are not drawn (the DCOs' ENV POLARITY on 23 panels, the
    // VCF's on 6, DCO-2's seven on 5 panels, B3's one, B4's four, B16's ten).
    CHECK(printed + blanks == 32 * 32 && blanks == 23 + 6 + 35 + 1 + 4 + 10);
    // Names the plan and the other tests know them by.
    CHECK(std::string(Jx3p::presetName(0)) == "String I" && std::string(Jx3p::presetName(2)) == "Organ I" && std::string(Jx3p::presetName(14)) == "Accordion");
    CHECK(std::string(Jx3p::presetName(25)) == "Fat Fifth" && std::string(Jx3p::presetName(27)) == "Sync Sweep" && std::string(Jx3p::presetName(31)) == "Jet");
    // No two alike, and no name a maker's or a model's.
    for (int a = 0; a < Jx3p::presetCount(); ++a) {
        for (int b = a + 1; b < Jx3p::presetCount(); ++b) {
            CHECK(std::string(Jx3p::presetName(a)) != Jx3p::presetName(b));
            bool same = true;
            for (int id = 0; id < kPatchSettings; ++id) same = same && Jx3p::presetPatch(a).value[id] == Jx3p::presetPatch(b).value[id];
            CHECK(!same);
        }
        const std::string name = Jx3p::presetName(a);
        for (const char* maker : {"Roland", "JX", "PG-200", "Juno"}) CHECK(name.find(maker) == std::string::npos);
    }
    // Out of range is the nearest, as the other synths'.
    CHECK(std::string(Jx3p::presetName(-3)) == "String I" && std::string(Jx3p::presetName(99)) == "Jet");
    // The start patch is not one of them: what a synth is made with, before anything picks a patch.
    std::unique_ptr<Jx3p> fresh(new Jx3p);
    for (int id = 0; id < kSettings; ++id) CHECK(fresh->setting(id) == Jx3p::startPatch().value[id]);
    CHECK(Jx3p::startPatch().value[Cutoff] == 100 && Jx3p::startPatch().value[VcaLevel] == 50 && Jx3p::presetPatch(0).value[VcaLevel] == 34);
}

// PICKING ONE (SELF): selectPreset writes the 32, puts the macros back, and keeps the track's two.
static void testPicked() {
    for (int i = 0; i < Jx3p::presetCount(); ++i) {
        std::unique_ptr<Jx3p> j(new Jx3p);
        j->setSetting(Velocity, 0);
        j->setSetting(Brilliance, 81);
        j->selectPreset(i);
        for (int id = 0; id < kPatchSettings; ++id) CHECK(j->setting(id) == Jx3p::presetPatch(i).value[id]);
        CHECK(j->setting(Velocity) == 0 && j->setting(Brilliance) == 81);
    }
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->selectPreset(5);
    j->selectPreset(32); // none such: nothing moves
    j->selectPreset(-1);
    CHECK(j->setting(VcfEnv) == 100);
}

// NAM'S OWN THREE CHECKS (FAITHFUL: NAM's words for the three patches).
// Fat Fifth's TUNE 7.9 is a fifth: DCO-2 seven semitones over DCO-1 (and its FINE 4.5, five cents under).
// Accordion's TUNE 10 is an octave (its FINE 6.6, sixteen cents over).
// Organ I plays in tune: RESONANCE 10 sings, and its pitch moves a semitone for a semitone (which
// also pins the plan's KEY FOLLOW 9.8 = 100%; SELF in that).
static void testNamsThree() {
    {
        std::unique_ptr<Jx3p> j = patched(25);
        CHECK(std::string(Jx3p::presetName(25)) == "Fat Fifth" && j->setting(Dco2Tune) == 79 && j->setting(Dco2Fine) == 45);
        j->noteOn(60, 1.f);
        std::vector<float> l(4800), r(4800);
        j->render(l.data(), r.data(), 4800);
        const double off = cents(j->voiceHz2(0), j->voiceHz(0));
        if (gPrint) std::printf("  Fat Fifth: DCO-2 is %.2f cents over DCO-1 (700 - 5)\n", off);
        CHECK(std::fabs(off - 695.0) < 1.5);
        CHECK(std::fabs(cents(j->voiceHz(0), 440.0 * std::pow(2.0, (60 - 12 - 69) / 12.0))) < 1.0); // 16': an octave under the key
    }
    {
        std::unique_ptr<Jx3p> j = patched(14);
        CHECK(std::string(Jx3p::presetName(14)) == "Accordion" && j->setting(Dco2Tune) == 100 && j->setting(Dco2Fine) == 66);
        j->noteOn(60, 1.f);
        std::vector<float> l(4800), r(4800);
        j->render(l.data(), r.data(), 4800);
        const double off = cents(j->voiceHz2(0), j->voiceHz(0));
        if (gPrint) std::printf("  Accordion: DCO-2 is %.2f cents over DCO-1 (1200 + 16)\n", off);
        CHECK(std::fabs(off - 1216.0) < 1.5);
    }
    {
        CHECK(std::string(Jx3p::presetName(2)) == "Organ I");
        const Jx3pPatch organ = Jx3p::presetPatch(2);
        CHECK(organ.value[Resonance] == 100 && organ.value[KeyFollow] == 98 && organ.value[Cutoff] == 24 && organ.value[VcaMode] == 1);
        // ORGAN I SINGS THE KEY PLAYED (the owner's call, 2026-11-15: the cutoff's range is 49.84 Hz to
        // 49.84 kHz, set by this patch; until then it sang 20.8 semitones under the key, 21 cents off
        // the grid, out of tune with every other track). The patch as written, but for the chorus (it
        // bends every pitch read off the sound: batch 5). What is heard is the filter's sine, 15 dB over
        // DCO-1's fundamental (DCO-1 is at 16', an octave under the key, so the sine sits on its second
        // harmonic and is held there by it): every key from 48 to 84 within 1 cent of the grid (measured
        // 2026-11-15: 0.4 at worst from 48 to 72, 0.9 from 36), read over five seconds.
        // (Five seconds, and the level over two halves of them: DCO-2 is 27 cents under DCO-1 in this
        // patch, FINE 2.3, so its harmonic beats with the sine twice a second at key 48, and a short
        // reading catches the beat, not the pitch.)
        double worst = 0.0;
        for (int key = 48; key <= 84; ++key) {
            std::unique_ptr<Jx3p> j = patched(2);
            j->setSetting(Chorus, 0);
            j->noteOn(key, 1.f);
            const std::size_t n = 6 * 48000;
            std::vector<float> l(n), r(n);
            j->render(l.data(), r.data(), static_cast<int>(n));
            const double written = 440.0 * std::pow(2.0, (key - 69) / 12.0), cv = j->voiceCutoffHz(0);
            const std::vector<float> band = bandOf(l, written);
            const double sings = pitchOf(band, 48000, n), off = cents(sings, written);
            const double level = rmsOf(band, 48000, n) * std::sqrt(2.0), dco = partNear(l.data() + 48000, n - 48000, j->voiceHz(0));
            worst = std::fmax(worst, std::fabs(off));
            if (gPrint)
                std::printf("  Organ I at key %d: the key is %.2f Hz, it sings %.2f Hz, %+.2f cents from it (its cutoff %+.2f); %.1f dB over its DCO-1 at %.2f Hz\n", key, written, sings, off,
                            cents(cv, written), dB(level / dco), j->voiceHz(0));
            CHECK(std::fabs(off) < 1.0);                       // in tune with the grid, and so with the other tracks
            CHECK(std::fabs(cents(cv, written)) < 0.5);        // the cutoff's CV is the key's own pitch (KEY FOLLOW 9.8 is 100%)
            CHECK(level > 4.0 * dco);                          // it is the filter that is heard: 12 dB and more over the DCO (15.3 measured)
            // It sings steadily: as loud over the last two and a half seconds as over the two and a half before.
            CHECK(level > 0.02 && std::fabs(dB(rmsOf(band, 48000 + 120000, n) / rmsOf(band, 48000, 48000 + 120000))) < 0.5);
        }
        if (gPrint) std::printf("  Organ I: %.2f cents from the key at worst, keys 48 to 84\n", worst);
    }
}

// EVERY PATCH PLAYS (SELF): a chord sounds while it is held, every sample is a number, nothing
// passes a stated peak, and after the keys are let go the synth comes to exact silence, within
// the patch's own RELEASE and a little.
//
// HOW LOUD (measured 2026-11-15, C3 G3 C4 E4 at full velocity, the loudest tenth of a second;
// --print): from -36.1 dB (B15 Planet) to -5.7 dB (B7 Distortion Guitar, LEVEL 1.7); the middle of
// the 32 -16.6 dB, and A1 String I, what a new track plays, -13.5. The same chord on PHAZED's first
// presets is -15.5 to -12.6 dB. THE OWNER'S CALLS (2026-11-15) made it so. Before them the middle
// was -28.5 dB and two patches all but silent (B4 Song Whistle -62.3, B15 Planet -60.9):
//  - the VCA's taper is straight, 0 dB at LEVEL 10, and the clip comes in over a full wave there
//    (jx3p/env.hpp), with the synth's output gain at 0.6 (it was 4 dB a step, 0 dB at LEVEL 7, then
//    at 4.5 for a day: E. Piano I and Voice, at LEVEL 10, were then 21 and 18 dB into the clip);
//  - the cutoff's range is 49.84 Hz to 49.84 kHz about key 60 (it was 15 Hz to 15 kHz;
//    jx3p/ir3109.hpp): Song Whistle's CUTOFF 2.8 under KEY FOLLOW 10 lets its one DCO through
//    (-28.3 dB), and Planet's filter is no longer shut (-36.1).
// The straight taper moved the patches at a high LEVEL down against the rest: A11 Harpsichord (8.5)
// -17.4 to -28.6 dB, B16 Jet (6.6) -24.3 to -30.3; and those at a low one up: B7 -7.5 to -5.7.
// NO PATCH IS ALL BUT SILENT: every one is over -38 dB, and the checks below hold the middle
// and A1 where they were measured.
static const int kSongWhistle = 19, kPlanet = 30;
static void testEveryPatchPlays() {
    double quietest = 1e9, loudest = 0.0;
    int quietAt = 0, loudAt = 0;
    std::vector<double> held;
    for (int i = 0; i < Jx3p::presetCount(); ++i) {
        const Heard h = hear(i);
        CHECK(h.number);
        CHECK(h.heldRms > 0.0126);                                // -38 dB: no patch is all but silent (the quietest is B15 Planet, -36.1)
        CHECK(h.peak < 1.0);                                      // the highest, B14 Pulsar, peaks at 0.89
        CHECK(h.clipped == 0);                                    // THE OWNER'S CALL (2026-11-15): no patch as written reaches the VCA's clip
        held.push_back(h.heldRms);
        if (i == 0) CHECK(dB(h.heldRms) > -14.5 && dB(h.heldRms) < -12.5); // A1 String I, a new track's sound: -13.5, where PHAZED's first presets are
        if (i == kSongWhistle) CHECK(dB(h.heldRms) > -29.5 && dB(h.heldRms) < -27.0); // -28.3 (it was -62.3, then -25.8)
        if (i == kPlanet) CHECK(dB(h.heldRms) > -37.0 && dB(h.heldRms) < -35.0);      // -36.1 (it was -60.9, then -34.5)
        CHECK(h.rings >= 0.0 && h.rings < 11.0); // RELEASE 10 is 10 s (a guess: jx3p/env.hpp); Jet's
        if (h.heldRms < quietest) quietest = h.heldRms, quietAt = i;
        if (h.heldRms > loudest) loudest = h.heldRms, loudAt = i;
        if (gPrint)
            std::printf("  %s%-2d %-18s LEVEL %4.1f  held %6.1f dB  peak %5.2f  rings %5.2f s  clipped %llu\n", i < 16 ? "A" : "B", i % 16 + 1, Jx3p::presetName(i),
                        Jx3p::presetPatch(i).value[VcaLevel] / 10.0, dB(h.heldRms), h.peak, h.rings, static_cast<unsigned long long>(h.clipped));
        if (h.heldRms <= 0.0126 || h.peak >= 1.0 || h.clipped || h.rings < 0.0 || h.rings >= 11.0) std::printf("     (that was %s)\n", Jx3p::presetName(i));
    }
    // The middle of the 32 (the mean of the sixteenth and seventeenth by loudness): -16.6 dB.
    {
        std::sort(held.begin(), held.end());
        const double middle = 0.5 * (dB(held[15]) + dB(held[16]));
        if (gPrint) std::printf("  the middle of the 32: %.1f dB\n", middle);
        CHECK(middle > -17.6 && middle < -15.6);
    }
    // SIX KEYS, AND THE MACROS AT THEIR ENDS (the review, 2026-11-15; measured again after the owner's
    // call that day, "I don't think they should distort by default"; SELF). Four keys with the macros in
    // the middle is not the loudest a patch gets. SIX KEYS AS WRITTEN (held two seconds): NO PATCH
    // REACHES THE VCA'S CLIP, and the highest peak out of the synth is A8 E. Piano I's, 1.46 (0.95 after
    // the track's usual level, 0.65): it was 2.85, 21 dB into the clip, and A16 Voice 2.56 (now 1.08).
    // Every other patch as written is under 1.2 (A3 Organ I 1.17). WITH EVERY MACRO FULL UP the filter
    // rings and twenty patches reach the clip: two pass 1.5 (B8 Juicy Funk 1.79, A8 1.53; it was A8 2.69,
    // A16 2.58, A11 2.53, B8 1.90). Every sample is a number and every patch comes to exact silence in
    // all 96 runs. With every macro at 0 four of the 32 are under -60 dB (the filter shut).
    {
        double mostFull = 0.0, mostWritten = 0.0;
        int fullAt = 0, writtenAt = 0, over = 0, overWritten = 0, shut = 0, clippedFull = 0;
        double nextWritten = 0.0; // the highest peak as written but for E. Piano I's
        for (int i = 0; i < Jx3p::presetCount(); ++i) {
            const Heard written = hear(i, 256, 6), full = hear(i, 256, 6, 1.f), none = hear(i, 256, 6, 0.f);
            CHECK(written.number && full.number && none.number);
            CHECK(written.rings >= 0.0 && written.rings < 11.0 && full.rings >= 0.0 && full.rings < 11.0 && none.rings >= 0.0 && none.rings < 11.0);
            CHECK(written.peak < 1.5 && full.peak < 2.0 && none.peak < 0.3);
            CHECK(written.clipped == 0 && none.clipped == 0); // as written, and with the macros down, nothing reaches the clip
            if (written.peak > mostWritten) mostWritten = written.peak, writtenAt = i;
            overWritten += written.peak > 1.5;
            if (i != 7) nextWritten = std::fmax(nextWritten, written.peak);
            if (full.peak > mostFull) mostFull = full.peak, fullAt = i;
            over += full.peak > 1.5;
            clippedFull += full.clipped > 0;
            shut += none.heldRms < 1e-3;
            if (gPrint) std::printf("  %s%-2d %-18s six keys: peak %5.2f as written, %5.2f macros full (clipped %llu), %5.3f macros at 0 (held %6.1f dB)\n", i < 16 ? "A" : "B", i % 16 + 1, Jx3p::presetName(i), written.peak, full.peak, static_cast<unsigned long long>(full.clipped), none.peak, dB(none.heldRms));
        }
        if (gPrint)
            std::printf("  six keys: the highest peak as written %.3f (%s), %d patches over 1.5, the rest %.3f at most; macros full %.3f (%s), %d patches over 1.5, %d reach the clip; macros at 0, %d patches under -60 dB\n", mostWritten,
                        Jx3p::presetName(writtenAt), overWritten, nextWritten, mostFull, Jx3p::presetName(fullAt), over, clippedFull, shut);
        CHECK(writtenAt == 7 && mostWritten > 1.3 && mostWritten < 1.5 && overWritten == 0); // A8 E. Piano I, 1.46, and none over 1.5
        CHECK(nextWritten > 1.0 && nextWritten < 1.25);                                    // every other patch as written: 1.17 at most
        CHECK(fullAt == 23 && mostFull > 1.6 && over == 2 && shut == 4 && clippedFull == 20); // macros full: B8 Juicy Funk 1.79, two over 1.5, twenty reach the clip
    }
    CHECK(quietAt == kPlanet && loudAt == 22 && std::string(Jx3p::presetName(kSongWhistle)) == "Song Whistle" && std::string(Jx3p::presetName(kPlanet)) == "Planet");
    if (gPrint) std::printf("  the quietest %s, %.1f dB; the loudest %s, %.1f dB: %.1f dB apart\n", Jx3p::presetName(quietAt), dB(quietest), Jx3p::presetName(loudAt), dB(loudest), dB(loudest / quietest));
    // A patch with CHORUS on is stereo, one with it off the same on both sides.
    for (int i : {0, 5}) {
        const Heard h = hear(i);
        bool same = true;
        for (std::size_t k = 0; k < h.left.size(); ++k) same = same && h.left[k] == h.right[k];
        CHECK(same == (Jx3p::presetPatch(i).value[Chorus] == 0));
    }
}

// THE SAME SOUND HOWEVER IT IS CUT (SELF: the house rule), and twice from fresh: every patch's
// chord and its release, in blocks of 256, of 64 and of 97.

static void testBlocks() {
    for (int i = 0; i < Jx3p::presetCount(); ++i) {
        const Heard a = hear(i, 256), again = hear(i, 256);
        CHECK(a.left == again.left && a.right == again.right);
        for (int block : {64, 97}) {
            const Heard b = hear(i, block);
            const std::size_t n = a.left.size() < b.left.size() ? a.left.size() : b.left.size();
            bool same = n > 96000;
            for (std::size_t k = 0; k < n && same; ++k) same = a.left[k] == b.left[k] && a.right[k] == b.right[k];
            if (!same) std::printf("     (%s, in blocks of %d)\n", Jx3p::presetName(i), block);
            CHECK(same);
        }
    }
}

// NO NUMBER TOO SMALL TO HOLD (SELF: the house rule; the plan's testTooSmall, "every preset
// through its release"): flush-to-zero off, each patch's chord held, let go and rung out; and once
// more ended by allNotesOff (the tails), with the patch after it picked while they ring.
static void testTooSmall() {
    std::vector<float> l(64), r(64);
    for (int i = 0; i < Jx3p::presetCount(); ++i) {
        std::feclearexcept(FE_UNDERFLOW);
        hear(i, 64);
        if (std::fetestexcept(FE_UNDERFLOW)) std::printf("     (%s)\n", Jx3p::presetName(i));
        CHECK(!std::fetestexcept(FE_UNDERFLOW));
        std::unique_ptr<Jx3p> j = patched(i);
        std::feclearexcept(FE_UNDERFLOW);
        for (int key : kChord) j->noteOn(key, 0.3f);
        for (long done = 0; done < 15 * 48000; done += 64) {
            if (done == 48000) j->allNotesOff();
            if (done == 48000 + 6400) j->selectPreset((i + 1) % Jx3p::presetCount());
            j->render(l.data(), r.data(), 64);
        }
        if (std::fetestexcept(FE_UNDERFLOW)) std::printf("     (%s, by allNotesOff)\n", Jx3p::presetName(i));
        CHECK(!std::fetestexcept(FE_UNDERFLOW));
    }
}

// A PATCH PICKED AFTER ANOTHER IS THE PATCH (SELF): nothing of the sound before stays in the synth.
// Another patch played and rung out, Stop (allNotesOff), then this one picked and played: the
// same bits as this one picked on a fresh synth. (The HPF's corner, the chorus's fade and its
// line, the holds, the LFO's place: each would show here. Found on the way, 2026-11-14: with a key
// in the first milliseconds after the pick, 12 of the 32 differ, each one after a patch whose HPF
// is not at 0: the corner's slide, which is the design.)
static void testAfterAnother() {
    for (int i = 0; i < Jx3p::presetCount(); ++i) {
        const int other = (i + 7) % Jx3p::presetCount();
        std::unique_ptr<Jx3p> used = patched(other), fresh = patched(i);
        std::vector<float> l(4800), r(4800), fl(4800), fr(4800);
        for (int key : kChord) used->noteOn(key, 1.f);
        for (int block = 0; block < 10; ++block) used->render(l.data(), r.data(), 4800);
        for (int key : kChord) used->noteOff(key);
        for (int block = 0; block < 10 * 12; ++block) used->render(l.data(), r.data(), 4800); // 12 s: the longest release and its tail
        used->selectPreset(i);
        // A tenth of a second of silence first, on both: a knob turned slides for a moment (the HPF's
        // corner comes from where the last patch had it over some milliseconds, as it does when
        // turned by hand; with a key in that moment the two differ, by that slide and nothing else).
        used->render(l.data(), r.data(), 4800);
        fresh->render(fl.data(), fr.data(), 4800);
        used->allNotesOff();
        fresh->allNotesOff();
        bool same = true, sound = false;
        for (Jx3p* j : {used.get(), fresh.get()})
            for (int key : kChord) j->noteOn(key, 1.f);
        for (int block = 0; block < 20 && same; ++block) {
            if (block == 10)
                for (Jx3p* j : {used.get(), fresh.get()})
                    for (int key : kChord) j->noteOff(key);
            used->render(l.data(), r.data(), 4800);
            fresh->render(fl.data(), fr.data(), 4800);
            same = l == fl && r == fr;
            for (float v : fl) sound = sound || v != 0.f;
        }
        if (!same) std::printf("     (%s, after %s)\n", Jx3p::presetName(i), Jx3p::presetName(other));
        CHECK(same && sound);
    }
}

// THROUGH THE ENGINE (SELF): Command::SelectPreset on a JX-3P track is the patch picked on the
// synth itself, sample for sample.
static void testThroughTheEngine() {
    for (int preset : {0, 12, 27, 29}) {
        std::unique_ptr<Engine> engine(new Engine);
        const int track = 1;
        CHECK(engine->addSynthTrack(track, SynthKind::Jx3p));
        Command c;
        c.track = track;
        c.type = Command::SelectPreset;
        c.a = preset;
        CHECK(engine->send(c));
        c.type = Command::NoteOn;
        c.a = 57;
        c.value = 1.f;
        CHECK(engine->send(c));
        std::vector<float> out(2 * 9600);
        engine->render(out.data(), 9600);
        std::unique_ptr<Jx3p> j = patched(preset);
        j->noteOn(57, 1.f);
        std::vector<float> l(9600), r(9600);
        j->render(l.data(), r.data(), 9600);
        double mine = 0.0, both = 0.0;
        for (std::size_t k = 0; k < 9600; ++k) mine += double(l[k]) * l[k], both += double(out[2 * k]) * l[k];
        // The engine's track level and its mixing are its own: the same wave, at one gain.
        const double gain = both / mine;
        double off = 0.0;
        for (std::size_t k = 0; k < 9600; ++k) off = std::fmax(off, std::fabs(out[2 * k] - gain * l[k]));
        CHECK(mine > 1e-6 && gain > 0.1 && off < 1e-4 * std::sqrt(mine / 9600.0) + 1e-6);
    }
}

int main(int argc, char** argv) {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"testNam", testNam}, {"testPicked", testPicked}, {"testNamsThree", testNamsThree}, {"testEveryPatchPlays", testEveryPatchPlays},
        {"testBlocks", testBlocks}, {"testTooSmall", testTooSmall}, {"testThroughTheEngine", testThroughTheEngine}, {"testAfterAnother", testAfterAnother},
    };
    bool any = false;
    for (int a = 1; a < argc; ++a) {
        if (std::string(argv[a]) == "--print") {
            gPrint = true;
            continue;
        }
        for (const Test& t : tests)
            if (std::string(argv[a]) == t.name) t.run(), any = true;
    }
    if (gPrint && !any) testNamsThree(), testEveryPatchPlays(), any = true;
    if (!any)
        for (const Test& t : tests) t.run();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("jx3p presets tests passed\n");
    return 0;
}
