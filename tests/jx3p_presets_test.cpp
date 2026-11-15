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
};
// A chord held two seconds at full velocity (the patch as written), let go, and listened to until
// the synth puts out exact zeros for a tenth of a second (40 s at most: RELEASE 10 is 10 s).
static Heard hear(int preset, int block = 256) {
    std::unique_ptr<Jx3p> j = patched(preset);
    Heard h;
    const long hold = 2 * 48000, most = hold + 40 * 48000;
    std::vector<float> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
    for (int key : kChord) j->noteOn(key, 1.f);
    long quiet = 0, lastSound = 0;
    for (long done = 0; done < most;) {
        if (done == hold)
            for (int key : kChord) j->noteOff(key);
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
    if (quiet < 4800) h.rings = -1.0;
    else if (h.rings < 0.0) h.rings = 0.0; // (a struck sound, over before the keys were let go)
    for (std::size_t from = 0; from + 4800 <= static_cast<std::size_t>(hold); from += 2400) h.heldRms = std::fmax(h.heldRms, rmsOf(h.left, from, from + 4800));
    return h;
}

static bool gPrint = false;

// ---- the tests ---------------------------------------------------------------------------------

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
    CHECK(Jx3p::startPatch().value[Cutoff] == 100 && Jx3p::startPatch().value[VcaLevel] == 70 && Jx3p::presetPatch(0).value[VcaLevel] == 34);
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
        // The patch as written, but for the chorus (it bends every pitch read off the sound: batch 5).
        double first = 0.0, worst = 0.0, worstCv = 0.0, firstCv = 0.0;
        for (int key = 48; key <= 84; key += 4) {
            std::unique_ptr<Jx3p> j = patched(2);
            j->setSetting(Chorus, 0);
            j->noteOn(key, 1.f);
            std::vector<float> l(72000), r(72000);
            j->render(l.data(), r.data(), 72000);
            const double cv = j->voiceCutoffHz(0);
            const std::vector<float> band = bandOf(l, cv);
            const double hz = pitchOf(band, 36000, band.size());
            if (key == 48) first = hz, firstCv = cv;
            const double off = cents(hz, first) - 100.0 * (key - 48);
            worst = std::fmax(worst, std::fabs(off));
            worstCv = std::fmax(worstCv, std::fabs(cents(cv, firstCv) - 100.0 * (key - 48)));
            if (gPrint) std::printf("  Organ I at key %d: sings at %.2f Hz (its cutoff %.2f), %.2f cents off a semitone a semitone\n", key, hz, cv, off);
            // It sings: the part at its pitch is there, steady, a second in and at the end.
            CHECK(rmsOf(band, 36000, 48000) > 1e-3 && std::fabs(dB(rmsOf(band, 60000, 72000) / rmsOf(band, 36000, 48000))) < 1.0);
        }
        CHECK(worst < 5.0);
        CHECK(worstCv < 0.5);
    }
}

// EVERY PATCH PLAYS (SELF): a chord sounds while it is held, every sample is a number, nothing
// passes a stated peak, and after the keys are let go the synth comes to exact silence, within
// the patch's own RELEASE and a little.
//
// HOW LOUD (measured 2026-11-14, C3 G3 C4 E4 at full velocity, the loudest tenth of a second;
// --print): 30 of the 32 are between -37.9 dB (B16 Jet) and -5.9 dB (A8 E. Piano I, LEVEL 9.9);
// the middle one -28.5 dB. The same chord on PHAZED's first presets is -15.5 to -12.6 dB: the
// factory bank is some 13 dB quieter, since the booklet's LEVELs stand about 4.5 and the VCA's
// guessed taper has 0 dB at 7 (4 dB a dial step: jx3p/env.hpp). The owner's call (the plan).
//
// KNOWN, HELD HERE, NOT HIDDEN: B4 SONG WHISTLE AND B15 PLANET ARE ALL BUT SILENT (-62.3 and
// -60.9 dB). Both lean on where the cutoff's Hz sit, which is a guess (15 Hz to 15 kHz about
// key 60: jx3p/ir3109.hpp): Song Whistle's CUTOFF 2.8 under KEY FOLLOW 10 is two octaves and
// more under its one DCO, and Planet's RESONANCE 10 sings at 74 Hz and under while its inverted
// envelope holds the filter shut. With the cutoff anchored so Organ I sings the key played
// (tried, not kept: 49.6 Hz at CUTOFF 0) they come up to -35.8 and -44.5 dB. When the cutoff
// is measured or moved, the two checks marked KNOWN below fail, and are then taken out.
static const int kSongWhistle = 19, kPlanet = 30;
static void testEveryPatchPlays() {
    double quietest = 1e9, loudest = 0.0;
    int quietAt = 0, loudAt = 0;
    for (int i = 0; i < Jx3p::presetCount(); ++i) {
        const Heard h = hear(i);
        CHECK(h.number);
        const bool known = i == kSongWhistle || i == kPlanet;
        if (known) CHECK(h.heldRms > 3e-4 && h.heldRms < 2e-3); // KNOWN: all but silent, not silent (above)
        else CHECK(h.heldRms > 0.0125 * 0.8);                    // -38 dB and a little: the quietest is B16 Jet, -37.9
        CHECK(h.peak < 1.5);                                      // the loudest, A8 E. Piano I (LEVEL 9.9, in the VCA's clip), peaks at 1.34
        CHECK(h.rings >= 0.0 && h.rings < 11.0); // RELEASE 10 is 10 s (a guess: jx3p/env.hpp); Jet's
        if (h.heldRms < quietest) quietest = h.heldRms, quietAt = i;
        if (h.heldRms > loudest) loudest = h.heldRms, loudAt = i;
        if (gPrint)
            std::printf("  %s%-2d %-18s LEVEL %4.1f  held %6.1f dB  peak %5.2f  rings %5.2f s\n", i < 16 ? "A" : "B", i % 16 + 1, Jx3p::presetName(i),
                        Jx3p::presetPatch(i).value[VcaLevel] / 10.0, dB(h.heldRms), h.peak, h.rings);
        if ((!known && h.heldRms <= 0.01) || h.peak >= 1.5 || h.rings < 0.0 || h.rings >= 11.0) std::printf("     (that was %s)\n", Jx3p::presetName(i));
    }
    CHECK(quietAt == kSongWhistle && loudAt == 7 && std::string(Jx3p::presetName(kSongWhistle)) == "Song Whistle" && std::string(Jx3p::presetName(kPlanet)) == "Planet");
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
