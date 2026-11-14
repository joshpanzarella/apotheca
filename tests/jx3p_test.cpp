// The JX-3P's skeleton (plan batch 1): the settings, the voices and the
// steal, the tick, the envelope, the sample-and-hold, the VCA, DCO-1's saw in
// tune, and the house rules (blocks, Play from the top, twice from fresh, no
// allocation, no number too small to hold).
//
// Each test says FAITHFUL (held to a published figure: its source named) or
// SELF (held to the plan's own guess). A green SELF test only says the code
// does what the plan guessed.
//
//   jx3p_test                  all of them
//   jx3p_test testTick ...     just those

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/jx3p.hpp"
#include "jx3p/env.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::jx3p;

// ---- counted new: nothing on the audio thread allocates -------------------------------------------

#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
static long gNews = 0;
static void* countedNew(std::size_t size) {
    ++gNews;
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(std::size_t size) { return countedNew(size); }
void* operator new[](std::size_t size) { return countedNew(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static const int kTick = 480; // 10 ms at 48 kHz

struct Set { int id, value; };

// A plain sound to measure on: the start patch, held at the top.
static std::vector<Set> held(std::vector<Set> more = {}) {
    std::vector<Set> s = {{Attack, 0}, {Decay, 0}, {Sustain, 100}, {Release, 0}, {VcaMode, 0}, {VcaLevel, 70}, {Dco1Range, 1}};
    s.insert(s.end(), more.begin(), more.end());
    return s;
}
static std::unique_ptr<Jx3p> synth(const std::vector<Set>& sets = {}) {
    std::unique_ptr<Jx3p> j(new Jx3p);
    j->prepare(48000.f);
    for (const Set& s : sets) j->setSetting(s.id, s.value);
    return j;
}
// `frames` of its sound (left only: it is mono until the chorus).
static std::vector<float> play(Jx3p& j, int frames, std::vector<float>* right = nullptr) {
    std::vector<float> l(static_cast<std::size_t>(frames), 99.f), r(static_cast<std::size_t>(frames), 99.f); // 99: an unwritten sample shows
    j.render(l.data(), r.data(), frames);
    if (right) *right = r;
    return l;
}
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
static double keyHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }
static double counterHz(double hz) { return kCounterClockHz / std::floor(kCounterClockHz / hz + 0.5); }

// ---- the settings ---------------------------------------------------------------------------------

static void testSettings() {
    // 34 ids: the 32 of the patch, then VELOCITY and BRILLIANCE. Ranges as the PG-200's panel (SELF:
    // the plan's table, section 1).
    CHECK(kSettings == 34 && kPatchSettings == 32 && Velocity == 32 && Brilliance == 33 && Chorus == 31 && Attack == 27);
    const int most[kSettings] = {2, 2, 1, 1, 2, 3, 2, 100, 100, 1, 1, 100, 100, 1, 100, 100, 100, 100, 100, 100, 1, 100, 1, 100, 2, 100, 100, 100, 100, 100, 100, 1, 1, 100};
    for (int id = 0; id < kSettings; ++id) CHECK(settingMin(id) == 0 && settingMax(id) == most[id]);
    std::unique_ptr<Jx3p> j = synth();
    for (int id = 0; id < kSettings; ++id) {
        j->setSetting(id, most[id] + 5);
        CHECK(j->setting(id) == most[id]);
        j->setSetting(id, -3);
        CHECK(j->setting(id) == 0);
    }
    // The start patch, and a new track's own two.
    std::unique_ptr<Jx3p> fresh = synth();
    CHECK(fresh->setting(Dco1Range) == 1 && fresh->setting(Dco1Wave) == 0 && fresh->setting(Dco2Tune) == 50 && fresh->setting(Cutoff) == 100);
    CHECK(fresh->setting(VcaLevel) == 70 && fresh->setting(Chorus) == 1 && fresh->setting(Velocity) == kNewTrackVelocity && fresh->setting(Brilliance) == kNewTrackBrilliance);
    CHECK(kNewTrackVelocity == 1 && kNewTrackBrilliance == 50 && kVoices == 6);
    CHECK(Jx3p::presetCount() >= 1 && std::string(Jx3p::presetName(0)) == "Start");
    CHECK(Jx3p::presetPatch(0).value[Velocity] == 1 && Jx3p::presetPatch(0).value[Brilliance] == 50);
}

// A PRESET KEEPS THE TRACK'S OWN: picking one writes ids 0..31 and leaves VELOCITY and BRILLIANCE
// as the track had them (SELF: the plan, section 1).
static void testAPresetKeepsTheTracksOwn() {
    std::unique_ptr<Jx3p> j = synth();
    j->setSetting(Velocity, 0);
    j->setSetting(Brilliance, 73);
    j->setSetting(Attack, 61);
    j->setSetting(VcaLevel, 12);
    j->selectPreset(0);
    CHECK(j->setting(Velocity) == 0 && j->setting(Brilliance) == 73);
    CHECK(j->setting(Attack) == Jx3p::presetPatch(0).value[Attack] && j->setting(VcaLevel) == 70);
    // And a macro goes back to the middle: ATTACK's macro turned up to 10 is the patch's ATTACK again
    // (up, not down: the start patch's ATTACK is 0, which the macro at 0 gives too).
    std::unique_ptr<Jx3p> a = synth(held({{Attack, 60}})), b = synth(held({{Attack, 60}}));
    a->setMacro(MacroAttack, 1.f);
    a->selectPreset(0);
    b->selectPreset(0);
    for (Jx3p* x : {a.get(), b.get()}) x->noteOn(60, 1.f);
    const std::vector<float> sa = play(*a, 9600), sb = play(*b, 9600);
    CHECK(sa == sb);
}

// ---- in tune --------------------------------------------------------------------------------------

// IN TUNE (SELF: A440 is the program's): DCO-1's saw, every RANGE, notes 21 to 108 (the piano's).
// Within 1 cent of the written note (0.90 at worst, note 106 at 4': the counter's rounding grows
// toward the top; the plan's section 8, "under 1 cent at the top"), and, with the pitch rounded to the counter (kRoundToCounter), the 8253's clock
// over a whole number of divisions, to the measuring's own error.
static void testInTune() {
    double worstWritten = 0.0, worstCounter = 0.0, counterOff = 0.0;
    for (int range = 0; range < 3; ++range) {
        for (int note = 21; note <= 108; ++note) {
            std::unique_ptr<Jx3p> j = synth(held({{Dco1Range, range}}));
            j->noteOn(note, 1.f);
            const std::vector<float> out = play(*j, 48000 + 4800);
            const double want = keyHz(note) * (range == 0 ? 0.5 : range == 2 ? 2.0 : 1.0);
            // Over 3 kHz through a band-pass first: a band-limited saw's ringing crosses zero by itself
            // there. (Not under: a band-pass at 14 Hz has not settled in a second.)
            const double got = want < 3000.0 ? pitchOf(out, 4800, out.size()) : pitchOf(bandOf(out, counterHz(want)), 4800, out.size());
            worstWritten = std::fmax(worstWritten, std::fabs(cents(got, want)));
            worstCounter = std::fmax(worstCounter, std::fabs(cents(got, counterHz(want))));
            counterOff = std::fmax(counterOff, std::fabs(cents(counterHz(want), want)));
        }
    }
    CHECK(worstWritten < 1.0 && worstWritten > 0.85); // (> 0.85: the top was reached)
    CHECK(worstCounter < 0.02);
    CHECK(counterOff > 0.1); // (so the line above can tell the counter's pitch from the written one)
    // A4 is exactly 13636 divisions of the clock taken: exactly 440 Hz.
    std::unique_ptr<Jx3p> j = synth(held());
    j->noteOn(69, 1.f);
    CHECK(j->voiceHz(0) == 440.0 && kCounterClockHz / 13636.0 == 440.0);
    if (failures || std::getenv("JX3P_PRINT")) std::printf("  in tune: worst %.4f cents from the written note, %.4f from the counter's (which is %.3f off at most)\n", worstWritten, worstCounter, counterOff);
}

// A KNOB TURNED DURING A NOTE IS READ ON THE TICK (FAITHFUL: SN p.6, step 3 reads the panel each
// loop): RANGE from 8' to 4' under a held key, and the pitch is an octave up from the voice's next
// tick, not before.
static void testRangeTurnedDuringANote() {
    std::unique_ptr<Jx3p> j = synth(held());
    j->noteOn(57, 1.f);
    play(*j, 100);
    CHECK(j->voiceHz(0) == counterHz(220.0));
    j->setSetting(Dco1Range, 2);
    play(*j, kTick - 100); // up to its next tick, not on it
    CHECK(j->voiceHz(0) == counterHz(220.0));
    play(*j, 1);
    CHECK(j->voiceHz(0) == counterHz(440.0));
    const std::vector<float> out = play(*j, 48000);
    CHECK(std::fabs(cents(pitchOf(out, 0, out.size()), 440.0)) < 1.0);
}

// A NOTE THROUGH THE ENGINE, written in a pattern and played: in tune (SELF).
static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
static Command noteAt(int slot, int stepIndex, int pitch, float length, float velocity = 1.f) {
    Command c = make(Command::SetNote, 1, slot, stepIndex, velocity);
    c.c = pitch;
    c.d = static_cast<int>(length);
    c.extra = length - static_cast<float>(c.d);
    return c;
}
static std::vector<float> renderEngine(Engine& e, int frames) {
    std::vector<float> out(2 * static_cast<std::size_t>(frames));
    e.render(out.data(), frames);
    return out;
}
static std::vector<float> leftOf(const std::vector<float>& stereo) {
    std::vector<float> l(stereo.size() / 2);
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = stereo[2 * i];
    return l;
}
static void testThroughTheEngine() {
    std::unique_ptr<Engine> e(new Engine);
    CHECK(e->addSynthTrack(1, SynthKind::Jx3p));
    for (const Set& s : held()) e->send(make(Command::SetSetting, 1, s.id, s.value));
    e->send(make(Command::SetTempo, 0, 0, 0, 60.f));
    e->send(noteAt(0, 0, 57, 15.f));
    e->send(make(Command::Play, 0, 0, 1));
    const std::vector<float> out = leftOf(renderEngine(*e, 3 * 48000));
    const double got = pitchOf(out, 4800, 2 * 48000);
    CHECK(std::fabs(cents(got, 220.0)) < 1.0);
    CHECK(peakOf(out) > 0.05f && peakOf(out) < 1.f);
}

// THE TOP (SELF): a DCO is held under 0.45 of the sample rate (kHighestStep), where its corrections
// still have room. Key 127 at 4' (25.1 kHz written) plays at 21.6 kHz, not folded down to 22.9 kHz as
// an unheld one would be. (The corrections' own band-limit takes 21.6 kHz down: 0.011 is heard
// there, measured 2026-11-14; the folded place 0.000004.)
static void testTheTopIsHeld() {
    std::unique_ptr<Jx3p> j = synth(held({{Dco1Range, 2}}));
    j->noteOn(127, 1.f);
    const std::vector<float> out = play(*j, 48000 + 4800);
    const double held = partAt(out.data() + 4800, 48000, 0.45 * 48000.0), folded = partAt(out.data() + 4800, 48000, 48000.0 - j->voiceHz(0));
    if (failures || std::getenv("JX3P_PRINT")) std::printf("  the top: %.4f at 21.6 kHz, %.6f at 22.9 kHz\n", held, folded);
    CHECK(j->voiceHz(0) > 25000.0 && held > 10.0 * folded && held > 0.01);
}

// ---- the voices -----------------------------------------------------------------------------------

static int voiceOf(const Jx3p& j, int note) {
    for (int v = 0; v < kVoices; ++v)
        if (j.voiceNote(v) == note) return v;
    return -1;
}

// SIX VOICES, AND THE ASSIGNER (FAITHFUL for six: SN p.6 "6-voice"; the order is SELF): the same key
// if it sounds, else the free voice longest silent, else the oldest sounding (a steal).
static void testVoices() {
    std::unique_ptr<Jx3p> j = synth(held({{Release, 10}}));
    for (int n = 0; n < 6; ++n) {
        j->noteOn(60 + n, 1.f);
        play(*j, 100);
    }
    for (int n = 0; n < 6; ++n) CHECK(voiceOf(*j, 60 + n) == n);
    // The same key again: its own voice.
    j->noteOn(62, 1.f);
    CHECK(voiceOf(*j, 62) == 2);
    play(*j, 100);
    // A seventh: the oldest sounding is stolen (60 began first; 62 began again just now).
    j->noteOn(70, 1.f);
    CHECK(voiceOf(*j, 60) == -1 && voiceOf(*j, 70) == 0);
    play(*j, 100);
    j->noteOn(71, 1.f); // and the next oldest
    CHECK(voiceOf(*j, 61) == -1 && voiceOf(*j, 71) == 1);
    // Let go of three, one after the other, and let them die away: the next note takes the one
    // silent longest, then the next.
    play(*j, 100);
    j->noteOff(64);
    play(*j, 2400);
    j->noteOff(63);
    play(*j, 2400);
    j->noteOff(62);
    play(*j, 24000);
    CHECK(voiceOf(*j, 64) == -1 && voiceOf(*j, 63) == -1 && voiceOf(*j, 62) == -1);
    j->noteOn(40, 1.f);
    CHECK(voiceOf(*j, 40) == 4);
    j->noteOn(41, 1.f);
    CHECK(voiceOf(*j, 41) == 3);
    j->noteOn(42, 1.f);
    CHECK(voiceOf(*j, 42) == 2);
}

// A STEAL TAKES A KEY LET GO BEFORE A KEY HELD (SELF; kStealLetGoFirst, put to the owner): six keys,
// the last three let go and ringing in a long release; a seventh takes the oldest of those three,
// not the oldest key still down. With all six held it is the oldest of all (testVoices).
static void testStealLetGoFirst() {
    std::unique_ptr<Jx3p> j = synth(held({{Release, 80}}));
    for (int n = 0; n < 6; ++n) {
        j->noteOn(60 + n, 1.f);
        play(*j, 100);
    }
    for (int k : {64, 63, 65}) j->noteOff(k); // (let go out of order: the oldest is still 63, by its key)
    play(*j, 100);
    j->noteOn(80, 1.f);
    CHECK(kStealLetGoFirst && voiceOf(*j, 80) == 3 && voiceOf(*j, 60) == 0 && j->voiceHeld(0));
    j->noteOn(81, 1.f); // then the next oldest let go
    CHECK(voiceOf(*j, 81) == 4);
    j->noteOn(82, 1.f);
    CHECK(voiceOf(*j, 82) == 5);
    j->noteOn(83, 1.f); // none let go now: the oldest held
    CHECK(voiceOf(*j, 83) == 0);
}

// A STEAL IS A RETRIGGER FROM WHERE THE VOICE IS (SELF; the owner's call, plan section 8): its
// envelope's attack starts from its level, not from 0; its VCA moves no faster than the hold lets
// it; its pitch is the new key's at once; and the sound has no jump larger than its own waves'.
static void testSteal() {
    std::unique_ptr<Jx3p> j = synth(held({{Attack, 60}, {Sustain, 70}, {Decay, 20}}));
    for (int n = 0; n < 6; ++n) j->noteOn(48 + 2 * n, 1.f);
    std::vector<float> before = play(*j, 48000);
    const float levelBefore = j->voiceEnv(0), vcaBefore = j->voiceVca(0);
    CHECK(std::fabs(levelBefore - 0.7f) < 1e-6f);
    j->noteOn(79, 1.f);
    CHECK(voiceOf(*j, 79) == 0);
    CHECK(std::fabs(j->voiceHz(0) - counterHz(keyHz(79))) < 1e-9);
    float worstVcaStep = 0.f, lastVca = vcaBefore;
    std::vector<float> after;
    for (int i = 0; i < 4800; ++i) {
        const std::vector<float> one = play(*j, 1);
        after.push_back(one[0]);
        worstVcaStep = std::fmax(worstVcaStep, std::fabs(j->voiceVca(0) - lastVca));
        lastVca = j->voiceVca(0);
        if (i == 0) CHECK(j->voiceEnv(0) > levelBefore); // the attack from 0.7, heading up
    }
    CHECK(worstVcaStep < 0.02f);
    // Its own waves: the largest step the same six keys make, played without a steal, over a second and a
    // half (their attacks too: the stolen voice goes up to the top again).
    std::unique_ptr<Jx3p> same = synth(held({{Attack, 60}, {Sustain, 70}, {Decay, 20}}));
    for (int key : {79, 50, 52, 54, 56, 58}) same->noteOn(key, 1.f);
    const std::vector<float> own = play(*same, 72000);
    const float ownJump = largestJump(own), stealJump = largestJump(after);
    CHECK(stealJump <= ownJump * 1.01f);
    if (failures || std::getenv("JX3P_PRINT")) std::printf("  steal: largest step %.4f after it, %.4f in the same keys' own waves (%.4f before it)\n", stealJump, ownJump, largestJump(before, 24000));
}

// ---- the tick and the envelope ---------------------------------------------------------------------

// THE TICK (FAITHFUL: SN p.6, "the CPU cycles steps 1 to 6 within approx. 10mS", step 2 refreshes the
// envelope): a voice's envelope moves only on its ticks, 480 samples apart, counted from the sample
// its key went down; between ticks the VCA's CV heads for a held value and never turns back.
static void testTick() {
    std::unique_ptr<Jx3p> j = synth(held({{Attack, 100}}));
    play(*j, 123); // the key goes down off any block's edge
    j->noteOn(60, 1.f);
    float lastEnv = -1.f, lastVca = 0.f;
    int moves = 0, wrongPlace = 0, turned = 0;
    for (int i = 0; i < 24000; ++i) {
        play(*j, 1);
        const float env = j->voiceEnv(0), vca = j->voiceVca(0);
        if (env != lastEnv) {
            ++moves;
            if (i % kTick != 0) ++wrongPlace;
        }
        if (i % kTick != 0 && vca < lastVca) ++turned; // (an attack: the held value only goes up)
        lastEnv = env;
        lastVca = vca;
    }
    CHECK(moves == 24000 / kTick && wrongPlace == 0 && turned == 0);
    CHECK(kTickSeconds == 0.010);
}

// THE TIMES (SELF: the guessed tables, kAttackSeconds, kFallSeconds): ATTACK, DECAY and RELEASE at
// 0, 2.5, 5, 7.5 and 10 each land in their table's time, within one tick. The times are worked out
// here from the plan's words (one tick to 4 s, one tick to 10 s, even in log), not read from the
// table, so a wrong entry shows.
static int landsAt(Jx3p& j, const std::function<bool(const Jx3p&)>& landed, int most) {
    for (int i = 0; i < most; ++i) {
        play(j, 1);
        if (landed(j)) return i;
    }
    return -1;
}
static void testEnvelopeTimes() {
    for (int dial : {0, 25, 50, 75, 100}) {
        const double attack = 0.01 * std::pow(400.0, dial / 100.0), fall = 0.01 * std::pow(1000.0, dial / 100.0);
        // Attack: from the key to the top. Tick 0 is the key's own sample.
        {
            std::unique_ptr<Jx3p> j = synth(held({{Attack, dial}}));
            j->noteOn(60, 1.f);
            const int at = landsAt(*j, [](const Jx3p& x) { return x.voiceEnv(0) == 1.f; }, 6 * 48000);
            CHECK(at >= 0 && at % kTick == 0 && std::fabs((at + kTick) / 48000.0 - attack) <= kTickSeconds * 1.001); // (the key's own tick is the first)
        }
        // Decay: from the top to a SUSTAIN of 0.
        {
            std::unique_ptr<Jx3p> j = synth(held({{Decay, dial}, {Sustain, 0}}));
            j->noteOn(60, 1.f);
            const int at = landsAt(*j, [](const Jx3p& x) { return x.voiceEnv(0) == 0.f; }, 12 * 48000);
            CHECK(at >= 0 && at % kTick == 0 && std::fabs(at / 48000.0 - fall) <= kTickSeconds * 1.001); // (the top was the key's tick)
        }
        // Release: from the top (held) to silence, from the sample the key is let go.
        {
            std::unique_ptr<Jx3p> j = synth(held({{Release, dial}}));
            j->noteOn(60, 1.f);
            play(*j, 4800 + 77);
            j->noteOff(60);
            const int at = landsAt(*j, [](const Jx3p& x) { return x.voiceEnv(0) == 0.f; }, 12 * 48000);
            CHECK(at >= 0 && at % kTick == 0 && std::fabs((at + kTick) / 48000.0 - fall) <= kTickSeconds * 1.001); // (the let-go's own tick is the first)
        }
    }
    // The tables themselves, every entry: within a part in a thousand of the plan's curve.
    for (int d = 0; d <= 100; ++d) {
        CHECK(std::fabs(kAttackSeconds[d] / (0.01 * std::pow(400.0, d / 100.0)) - 1.0) < 1e-3);
        CHECK(std::fabs(kFallSeconds[d] / (0.01 * std::pow(1000.0, d / 100.0)) - 1.0) < 1e-3);
    }
}

// ALL FOUR AT 0 (FAITHFUL for "an extremely short pulse": OM p.27; one tick is this plan's reading):
// at the top on the key's tick, at 0 on the next, the voice free once the hold has let go.
static void testAllAtZero() {
    std::unique_ptr<Jx3p> j = synth({{Attack, 0}, {Decay, 0}, {Sustain, 0}, {Release, 0}});
    j->noteOn(60, 1.f);
    play(*j, 1);
    CHECK(j->voiceEnv(0) == 1.f);
    play(*j, kTick - 1);
    CHECK(j->voiceEnv(0) == 1.f);
    play(*j, 1);
    CHECK(j->voiceEnv(0) == 0.f);
    std::vector<float> rest = play(*j, 2400);
    CHECK(j->voiceNote(0) == 60); // held: the key is down, at a SUSTAIN of 0
    CHECK(peakOf(std::vector<float>(rest.begin() + 1200, rest.end())) == 0.f);
    j->noteOff(60);
    play(*j, 1);
    CHECK(j->voiceNote(0) == -1); // and free the sample it is let go
}

// A KEY STARTS ON ITS OWN SAMPLE (SELF; the owner's call, kKeysOnTheSample): sound the sample after
// the key, wherever in the CPU's 10 ms loop it falls.
static void testAKeyStartsOnItsSample() {
    for (int off : {0, 1, 123, 479}) {
        std::unique_ptr<Jx3p> j = synth(held());
        play(*j, 1000 + off);
        j->noteOn(69, 1.f);
        const std::vector<float> out = play(*j, 4);
        CHECK(out[1] != 0.f);
    }
}

// ---- the VCA --------------------------------------------------------------------------------------

// THE VCA (SELF: the taper, kVcaDbPerDial; FAITHFUL to OM p.26 only that too high a LEVEL distorts).
static void testVca() {
    std::vector<double> rms;
    for (int level = 20; level <= 70; level += 10) {
        std::unique_ptr<Jx3p> j = synth(held({{VcaLevel, level}}));
        j->noteOn(57, 1.f);
        const std::vector<float> out = play(*j, 48000);
        rms.push_back(rmsOf(out, 4800));
    }
    for (std::size_t i = 1; i < rms.size(); ++i) CHECK(std::fabs(dB(rms[i] / rms[i - 1]) - kVcaDbPerDial) < 0.3);
    // LEVEL 0 is silence.
    {
        std::unique_ptr<Jx3p> j = synth(held({{VcaLevel, 0}}));
        j->noteOn(57, 1.f);
        CHECK(peakOf(play(*j, 9600)) == 0.f);
    }
    // Over LEVEL 7 a full wave is clipped: LEVEL 10 is louder than 7, but its peak never reaches
    // what a straight line would give, and it stays under the clip's ceiling.
    {
        std::unique_ptr<Jx3p> a = synth(held({{VcaLevel, 70}})), b = synth(held({{VcaLevel, 100}}));
        a->noteOn(57, 1.f);
        b->noteOn(57, 1.f);
        const std::vector<float> sa = play(*a, 48000), sb = play(*b, 48000);
        const float straight = peakOf(sa) * vcaGain(100.f) / vcaGain(70.f);
        CHECK(rmsOf(sb, 4800) > rmsOf(sa, 4800) * 1.2 && peakOf(sb) < 0.5f * straight && peakOf(sb) <= peakOf(sa) * kVcaClipCeiling);
        CHECK(vcaClip(0.5f, 1.f) == 0.5f && vcaClip(-1.f, 1.f) == -1.f && vcaClip(1e9f, 1.f) <= kVcaClipCeiling && vcaClip(1.5f, 1.f) < 1.5f && vcaClip(-1.5f, 1.f) > -1.5f);
        CHECK(std::fabs((vcaClip(1.f + 1e-3f, 1.f) - 1.f) / 1e-3f - 1.f) < 0.01f); // no corner where it starts to bend
    }
    // GATE: no slope. With ATTACK and RELEASE at their longest, the VCA is open 20 ms after the key
    // (the hold's smoothing only) and shut, and the voice free, 20 ms after it is let go.
    {
        std::unique_ptr<Jx3p> j = synth(held({{VcaMode, 1}, {Attack, 100}, {Release, 100}}));
        j->noteOn(57, 1.f);
        play(*j, 960);
        CHECK(j->voiceVca(0) == vcaGain(70.f));
        CHECK(j->voiceEnv(0) < 0.1f); // (the envelope is on its way up all the same: it is not the VCA's here)
        j->noteOff(57);
        play(*j, 960);
        CHECK(j->voiceVca(0) == 0.f && j->voiceNote(0) == -1);
    }
}

// GATE AT LEVEL 0 (SELF): a key held in GATE mode with LEVEL at 0 is silent but its voice stays its
// own: LEVEL turned up while the key is down, and it is heard.
static void testGateHeldAtLevelZero() {
    std::unique_ptr<Jx3p> j = synth(held({{VcaMode, 1}, {VcaLevel, 0}}));
    j->noteOn(57, 1.f);
    CHECK(peakOf(play(*j, 4800)) == 0.f && voiceOf(*j, 57) == 0);
    j->setSetting(VcaLevel, 70);
    CHECK(peakOf(play(*j, 4800)) > 0.05f);
}

// NOT A NUMBER (SELF: a house rule, a float input guards NaN itself): a key at a velocity that is
// not a number is velocity 0, a soft note that ends; its voice is freed and the output stays finite.
// (A NaN once made the VCA's target NaN: the hold never landed, the voice was never freed, and the
// track gave NaN for good.)
static void testNotANumber() {
    for (int velocityOn = 0; velocityOn < 2; ++velocityOn) {
        std::unique_ptr<Jx3p> j = synth(held({{Velocity, velocityOn}, {Release, 30}}));
        j->noteOn(60, std::nanf(""));
        std::vector<float> out = play(*j, 9600);
        j->noteOff(60);
        const std::vector<float> rest = play(*j, 5 * 48000);
        out.insert(out.end(), rest.begin(), rest.end());
        bool finite = true;
        for (float x : out) finite = finite && std::isfinite(x);
        CHECK(finite && voiceOf(*j, 60) == -1);
        if (velocityOn) CHECK(peakOf(out) < 0.05f); // velocity 0: the curve's floor
        else CHECK(peakOf(out) > 0.05f);            // OFF: every velocity plays as full
    }
}

// VELOCITY (SELF; the owner's call): ON, full velocity is the patch as written, the same bits as
// OFF; a soft note is quieter by the curve. OFF, every velocity plays as full.
static void testVelocity() {
    const auto note = [](int velocityOn, float velocity) {
        std::unique_ptr<Jx3p> j = synth(held({{Velocity, velocityOn}}));
        j->noteOn(57, velocity);
        return play(*j, 24000);
    };
    CHECK(note(1, 1.f) == note(0, 1.f));
    CHECK(note(0, 0.3f) == note(0, 1.f));
    const double soft = dB(rmsOf(note(1, 0.5f), 4800) / rmsOf(note(1, 1.f), 4800));
    CHECK(std::fabs(soft - (-0.5 * kVelocityDb)) < 0.1);
    CHECK(velocityGain(1.f) == 1.f && velocityGain(2.f) == 1.f && velocityGain(0.f) > 0.f);
}

// THE MACROS (SELF: the plan's rule, section 4): 0.5 is the patch; under it slides the setting to 0,
// over it to 10. Seen on ATTACK and RELEASE (the only ones with a part built so far).
static void testMacros() {
    const auto attackLands = [](int attack, float macro) {
        std::unique_ptr<Jx3p> j = synth(held({{Attack, attack}}));
        j->setMacro(MacroAttack, macro);
        j->noteOn(60, 1.f);
        return landsAt(*j, [](const Jx3p& x) { return x.voiceEnv(0) == 1.f; }, 6 * 48000);
    };
    CHECK(attackLands(50, 0.5f) == attackLands(50, 0.5f) && attackLands(50, 0.5f) == ticksFor(kAttackSeconds[50]) * kTick - kTick);
    CHECK(attackLands(50, 0.f) == 0);                                         // to 0: one tick
    CHECK(attackLands(50, 1.f) == ticksFor(kAttackSeconds[100]) * kTick - kTick); // to 10
    CHECK(attackLands(50, 0.75f) == ticksFor(kAttackSeconds[75]) * kTick - kTick); // half way up
    CHECK(attackLands(80, 0.25f) == ticksFor(kAttackSeconds[40]) * kTick - kTick); // half way down
    // RELEASE's macro.
    std::unique_ptr<Jx3p> j = synth(held({{Release, 50}}));
    j->setMacro(MacroRelease, 0.f);
    j->noteOn(60, 1.f);
    play(*j, 960);
    j->noteOff(60);
    CHECK(landsAt(*j, [](const Jx3p& x) { return x.voiceEnv(0) == 0.f; }, 48000) == 0); // one tick: the let-go's own
}

// GLIDE (SELF: the program's own, plan section 1): the key slides from `from` and is on its own pitch
// the sample its length ends.
static void testGlide() {
    std::unique_ptr<Jx3p> j = synth(held());
    j->noteOnFrom(69, 1.f, 57, 0.105f); // (not a whole number of ticks: it lands between two)
    CHECK(std::fabs(cents(j->voiceHz(0), 220.0)) < 1.0);
    const double home = counterHz(440.0);
    int at = -1;
    for (int i = 0; i < 9600 && at < 0; ++i) {
        play(*j, 1);
        if (j->voiceHz(0) == home) at = i;
    }
    CHECK(std::abs(at - 5040) <= 1);
    // It moved by itself, not on ticks: half way, half way in pitch.
    std::unique_ptr<Jx3p> k = synth(held());
    k->noteOnFrom(69, 1.f, 57, 0.1f);
    play(*k, 2400);
    CHECK(std::fabs(cents(k->voiceHz(0), keyHz(63))) < 5.0);
}

// ---- the house rules ------------------------------------------------------------------------------

// A phrase with every part built so far moving: keys, steals, glides, knobs, macros, VELOCITY, the
// DCOs (waves, cross mod, TUNE, FINE, MIX, ENV on them),
// GATE, RANGE, allNotesOff. Each event at its sample.
struct Event { long at; std::function<void(Jx3p&)> act; };
static std::vector<Event> phrase(double seconds) {
    std::vector<Event> ev;
    std::uint32_t seed = 7u;
    const auto roll = [&](int n) {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(n));
    };
    const long end = static_cast<long>(seconds * 48000.0);
    for (long t = 0; t < end; t += 1200 + roll(9000)) {
        const int note = 36 + roll(48), kind = roll(13);
        const float velocity = 0.2f + 0.08f * roll(11);
        const int a = roll(4), b = roll(101), c = roll(8), d = roll(11), e = roll(2), f = roll(3), g = roll(2), h = 40 + roll(61);
        const int w1 = roll(3), w2 = roll(4), x = roll(3), tune = roll(101), mix = roll(101), depth = roll(101), sw = roll(4); // the DCOs (batch 2)
        if (kind < 6) ev.push_back({t, [=](Jx3p& j) { j.noteOn(note, velocity); }});
        else if (kind < 8) ev.push_back({t, [=](Jx3p& j) { j.noteOnFrom(note, velocity, note - 7, 0.15f); }});
        else if (kind == 8) ev.push_back({t, [=](Jx3p& j) { j.setSetting(Attack + a, b); }});
        else if (kind == 9) ev.push_back({t, [=](Jx3p& j) { j.setMacro(c, 0.1f * d); }});
        else if (kind == 10) ev.push_back({t, [=](Jx3p& j) { j.setSetting(VcaMode, e); j.setSetting(Dco1Range, f); j.setSetting(Velocity, g); j.setSetting(VcaLevel, h); }});
        else if (kind == 11 && sw < 3) ev.push_back({t, [=](Jx3p& j) {
            j.setSetting(Dco1Wave, w1); j.setSetting(Dco2Wave, w2); j.setSetting(Dco2CrossMod, x); j.setSetting(Dco2Tune, tune);
            j.setSetting(SourceMix, mix); j.setSetting(DcoEnvDepth, depth); j.setSetting(Dco1Env, sw & 1); j.setSetting(Dco2Env, (sw + 1) & 1);
            j.setSetting(DcoEnvPolarity, depth & 1); j.setSetting(Dco2Range, f); j.setSetting(Dco2Fine, mix); }});
        else ev.push_back({t, [](Jx3p& j) { j.allNotesOff(); }});
        const long off = t + 600 + roll(30000);
        ev.push_back({off, [=](Jx3p& j) { j.noteOff(note); }});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    return ev;
}
// The phrase played in blocks of `block` frames (0: blocks of every size at random).
static std::vector<float> phraseIn(int block, double seconds = 20.0, std::vector<float>* right = nullptr) {
    std::unique_ptr<Jx3p> j = synth();
    const std::vector<Event> ev = phrase(seconds);
    const long end = static_cast<long>(seconds * 48000.0);
    std::vector<float> out(static_cast<std::size_t>(end)), r(static_cast<std::size_t>(end));
    std::uint32_t seed = 99u;
    std::size_t next = 0;
    for (long done = 0; done < end;) {
        while (next < ev.size() && ev[next].at <= done) ev[next++].act(*j);
        long n = block;
        if (block == 0) {
            seed = seed * 1664525u + 1013904223u;
            n = 1 + static_cast<long>((seed >> 8) % 700u);
        }
        if (next < ev.size() && ev[next].at - done < n) n = ev[next].at - done;
        if (end - done < n) n = end - done;
        j->render(&out[static_cast<std::size_t>(done)], &r[static_cast<std::size_t>(done)], static_cast<int>(n));
        done += n;
    }
    if (right) *right = r;
    return out;
}

// THE SAME SOUND HOWEVER THE BLOCKS ARE CUT (SELF: a house rule; Twin fails it, this must not): 20 s,
// cut 1, 48, 64, 100, 512 and at random, the same bits. And twice from fresh, the same bits.
static void testBlocks() {
    std::vector<float> right;
    const std::vector<float> whole = phraseIn(64, 20.0, &right);
    CHECK(peakOf(whole) > 0.1f && whole == right);
    for (int block : {1, 48, 100, 512, 0}) CHECK(phraseIn(block) == whole);
    CHECK(phraseIn(64) == whole); // twice from fresh
}

// TWICE FROM FRESH, ON ANY NUMBER OF HELPERS (SELF: a house rule): a song of JX-3P tracks through the
// engine, alone and with helpers, twice.
static std::vector<float> engineSong(int helpers) {
    std::unique_ptr<Engine> e(new Engine);
    e->useWorkers(helpers);
    for (int t = 1; t <= 3; ++t) {
        CHECK(e->addSynthTrack(t, SynthKind::Jx3p));
        for (int n = 0; n < 8; ++n) {
            Command c = noteAt(n, n * 2, 40 + 7 * t + 3 * n, 1.5f + (n % 3), 0.6f + 0.05f * n);
            c.track = t;
            if (n % 3 == 1) c.e = 256; // a glide
            e->send(c);
        }
        e->send(make(Command::SetSetting, t, Release, 40 + 20 * t));
        Command m = make(Command::SetAutomation, t, MacroAttack, 4, 0.9f);
        e->send(m);
    }
    e->send(make(Command::Play, 0, 0, 1));
    std::vector<float> all;
    for (int done = 0, size = 37; done < 3 * 16 * 6000; done += size, size = size % 301 + 64) {
        const std::vector<float> got = renderEngine(*e, size);
        all.insert(all.end(), got.begin(), got.end());
    }
    return all;
}
static void testTwiceFromFresh() {
    const std::vector<float> alone = engineSong(0);
    CHECK(peakOf(alone) > 0.05f);
    CHECK(engineSong(0) == alone);
    CHECK(engineSong(3) == alone);
}

// PLAY FROM THE TOP IS THE SAME PERFORMANCE (SELF: plan section 2): a chord heard with X, let go and
// rung out, then Play from the top, gives the same bits as Play from the top on a fresh engine.
static std::vector<float> fromTheTop(bool auditionFirst) {
    std::unique_ptr<Engine> e(new Engine);
    CHECK(e->addSynthTrack(1, SynthKind::Jx3p));
    e->send(make(Command::SetTempo, 0, 0, 0, 120.f));
    const int keys[6] = {48, 55, 60, 64, 67, 72};
    for (int n = 0; n < 6; ++n) e->send(noteAt(n, 0, keys[n], 3.f));
    for (int n = 0; n < 4; ++n) e->send(noteAt(6 + n, 4 + 2 * n, 50 + 2 * n, 1.f));
    renderEngine(*e, 4800);
    if (auditionFirst) {
        // An audition: three keys of another chord, let go one after another.
        for (int k : {41, 45, 52, 57}) e->send(make(Command::NoteOn, 1, k, 0, 0.8f));
        renderEngine(*e, 12000);
        e->send(make(Command::NoteOff, 1, 45));
        renderEngine(*e, 3000);
        for (int k : {41, 52, 57}) e->send(make(Command::NoteOff, 1, k));
    }
    renderEngine(*e, 48000); // rung out
    e->send(make(Command::Play, 0, 0, 1));
    return renderEngine(*e, 4 * 48000);
}
static void testPlayFromTheTop() {
    const std::vector<float> fresh = fromTheTop(false), after = fromTheTop(true);
    CHECK(peakOf(fresh) > 0.05f);
    CHECK(fresh == after);
}

// ...AND WHILE THE LAST NOTES RING (SELF: plan section 2). A long release (RELEASE 7, 1.25 s), a
// phrase, Stop, then Play 64, 4800 and 48000 samples later: what follows is the fresh synth's
// performance plus the tails, to a float's rounding. Through the engine, and the synth by itself.
static std::vector<float> ringingSong(int stop, int gap, bool playAgain, int release = 70) {
    std::unique_ptr<Engine> e(new Engine);
    CHECK(e->addSynthTrack(1, SynthKind::Jx3p));
    e->send(make(Command::SetSetting, 1, Release, release));
    e->send(make(Command::SetTempo, 0, 0, 0, 120.f));
    for (int n = 0; n <= 10; ++n) e->send(noteAt(n, n, 48 + 3 * n, 1.f));
    renderEngine(*e, 480);
    if (stop) {
        e->send(make(Command::Play, 0, 0, 1));
        renderEngine(*e, 48000);
        e->send(make(Command::Stop));
        renderEngine(*e, gap);
    }
    if (playAgain) e->send(make(Command::Play, 0, 0, 1));
    return renderEngine(*e, 2 * 48000);
}
static void testPlayFromTheTopWhileRinging() {
    const std::vector<float> fresh = ringingSong(0, 0, true);
    CHECK(peakOf(fresh) > 0.05f);
    for (int gap : {64, 4800, 48000}) {
        const std::vector<float> after = ringingSong(1, gap, true), tails = ringingSong(1, gap, false);
        float worst = 0.f;
        for (std::size_t i = 0; i < fresh.size(); ++i) worst = std::fmax(worst, std::fabs(after[i] - (fresh[i] + tails[i])));
        CHECK(peakOf(tails) > 1e-4f); // (there is a tail to hear)
        CHECK(worst < 1e-6f);
        if (failures || std::getenv("JX3P_PRINT")) std::printf("  from the top %d samples after Stop: tails peak %.3f, off fresh + tails by %.2g\n", gap, peakOf(tails), worst);
    }
    // The synth by itself: six keys ringing and a seventh, then allNotesOff; the six start again as
    // from fresh (the same bits where no tail is left), and the tails ring out and are let go.
    std::unique_ptr<Jx3p> a = synth(held({{Release, 70}})), b = synth(held({{Release, 70}}));
    for (int n = 0; n < 7; ++n) a->noteOn(50 + n, 0.8f);
    play(*a, 4800);
    a->noteOff(51);
    play(*a, 480);
    a->allNotesOff();
    CHECK(a->tailsRinging() == 6);
    for (int v = 0; v < kVoices; ++v) CHECK(a->voiceNote(v) == -1);
    for (Jx3p* x : {a.get(), b.get()}) x->noteOn(64, 1.f), x->noteOn(67, 1.f);
    std::vector<float> sa = play(*a, 4 * 48000), sb = play(*b, 4 * 48000);
    CHECK(a->tailsRinging() == 0);
    CHECK(std::vector<float>(sa.end() - 48000, sa.end()) == std::vector<float>(sb.end() - 48000, sb.end()));
    CHECK(sa != sb);
}

// THE START PHASE IS SEEDED (SELF: plan section 2): a silent voice's DCO starts at a phase from the
// count of keys since allNotesOff. So the same key on the same voice starts the same
// after allNotesOff, and elsewhere in the wave on the next key without it.
static void testSeededPhase() {
    std::unique_ptr<Jx3p> j = synth(held({{Release, 0}}));
    std::vector<float> first, again;
    // The same key seven times, each let go and freed before the next: voices 0 to 5 in turn, then
    // voice 0 again (silent longest), its seventh key.
    for (int k = 0; k < 7; ++k) {
        j->noteOn(57, 1.f);
        CHECK(voiceOf(*j, 57) == k % 6);
        const std::vector<float> got = play(*j, 2400);
        if (k == 0) first = got;
        if (k == 6) again = got;
        if (k == 1) CHECK(got != first); // another voice, another phase
        j->noteOff(57);
        play(*j, 4800);
    }
    CHECK(again != first); // the same voice, another count of keys: another phase
    j->allNotesOff();
    j->noteOn(57, 1.f);
    CHECK(voiceOf(*j, 57) == 0 && play(*j, 2400) == first); // put back: as from fresh
    std::unique_ptr<Jx3p> a = synth(held()), b = synth(held());
    a->noteOn(57, 1.f);
    b->noteOn(57, 1.f);
    CHECK(play(*a, 2400) == play(*b, 2400) && play(*a, 2400) == play(*b, 2400)); // the same from fresh
}

// SILENCE IN, SILENCE OUT (SELF: a house rule): every voice free, the output is exact zeros.
static void testSilence() {
    std::unique_ptr<Jx3p> j = synth();
    std::vector<float> right;
    CHECK(peakOf(play(*j, 4800, &right)) == 0.f && peakOf(right) == 0.f);
    j->noteOn(60, 1.f);
    play(*j, 4800);
    j->noteOff(60);
    play(*j, 3 * 48000);
    for (int v = 0; v < kVoices; ++v) CHECK(j->voiceNote(v) == -1);
    const std::vector<float> after = play(*j, 4800, &right);
    CHECK(peakOf(after) == 0.f && peakOf(right) == 0.f);
}

// NOTHING ON THE AUDIO THREAD ALLOCATES (SELF: a house rule): every call but the constructor and
// prepare, counted.
static void testNoAllocation() {
    std::unique_ptr<Jx3p> j = synth();
    std::vector<float> l(512), r(512);
    const long before = gNews;
    for (int round = 0; round < 50; ++round) {
        j->noteOn(40 + round % 30, 0.7f);
        j->noteOnFrom(50 + round % 20, 0.9f, 45, 0.05f);
        j->setSetting(round % kSettings, round);
        j->setMacro(round % kMacros, 0.02f * round);
        j->render(l.data(), r.data(), 512);
        if (round % 7 == 0) j->noteOff(40 + round % 30);
        if (round % 13 == 0) j->allNotesOff();
        if (round % 17 == 0) j->selectPreset(0);
    }
    CHECK(gNews == before);
}

// NO NUMBER TOO SMALL TO HOLD (SELF: a house rule): the whole synth, flush-to-zero off, a phrase and
// every voice through its release. The processor's underflow mark, cleared after setting up.
static void testTooSmall() {
    std::unique_ptr<Jx3p> j = synth();
    const std::vector<Event> ev = phrase(8.0);
    std::vector<float> l(64), r(64);
    std::feclearexcept(FE_UNDERFLOW);
    std::size_t next = 0;
    for (long done = 0; done < 8 * 48000 + 12 * 48000; done += 64) {
        while (next < ev.size() && ev[next].at <= done) ev[next++].act(*j);
        if (done == 8 * 48000) j->allNotesOff();
        j->render(l.data(), r.data(), 64);
    }
    CHECK(!std::fetestexcept(FE_UNDERFLOW));
    for (int v = 0; v < kVoices; ++v) CHECK(j->voiceNote(v) == -1);
}

// THE ENVELOPE AND THE HOLD AS PARTS (SELF): a hold lands exactly, and lands at 0; a release from the
// top is at 0 on its last tick; an attack from part way is quicker than from 0.
static void testTheParts() {
    Hold h;
    h.target = 1.f;
    const float k = holdCoef(48000.0);
    int n = 0;
    while (!h.still() && n < 48000) h.next(k), ++n;
    CHECK(h.value == 1.f && n > 200 && n < 1000);
    h.target = 0.f;
    n = 0;
    while (!h.still() && n < 48000) h.next(k), ++n;
    CHECK(h.value == 0.f && n < 1000);
    EnvTimes t;
    t.set(50.f, 50.f, 50.f, 50.f);
    Adsr a;
    a.on();
    int ticks = 0;
    while (a.stage == Adsr::Attack) a.tick(t), ++ticks;
    CHECK(ticks == t.attackTicks && a.level == 1.f);
    Adsr b;
    b.level = 0.5f;
    b.on();
    ticks = 0;
    while (b.stage == Adsr::Attack) b.tick(t), ++ticks;
    CHECK(ticks < t.attackTicks);
    a.off();
    ticks = 0;
    while (!a.idle()) a.tick(t), ++ticks;
    CHECK(ticks == t.releaseTicks && a.level == 0.f);
    CHECK(ticksFor(0.0) == 1 && ticksFor(0.012) == 1 && ticksFor(0.016) == 2);
    // The attack's curve (SELF, the plan's words: it heads for half as high again as the top and is
    // stopped there): half its ticks in, 1.5 x (1 - (1/3)^(1/2)) of the way.
    t.set(70.f, 50.f, 50.f, 50.f);
    CHECK(t.attackTicks % 2 == 0);
    Adsr c;
    c.on();
    for (int k = 0; k < t.attackTicks / 2; ++k) c.tick(t);
    CHECK(std::fabs(c.level - 1.5 * (1.0 - std::sqrt(1.0 / 3.0))) < 1e-4);
}

// The cost, for the plan: six notes on the start patch.
static void reportCost() {
    std::unique_ptr<Jx3p> j = synth();
    for (int n = 0; n < 6; ++n) j->noteOn(48 + 3 * n, 0.8f);
    std::vector<float> l(256), r(256);
    const auto t0 = std::clock();
    for (int i = 0; i < 6000; ++i) j->render(l.data(), r.data(), 256);
    const double seconds = static_cast<double>(std::clock() - t0) / CLOCKS_PER_SEC;
    std::printf("  six notes: %.2f%% of one core\n", 100.0 * seconds / (6000.0 * 256.0 / 48000.0));
}

int main(int argc, char** argv) {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"testSettings", testSettings}, {"testAPresetKeepsTheTracksOwn", testAPresetKeepsTheTracksOwn}, {"testInTune", testInTune},
        {"testThroughTheEngine", testThroughTheEngine}, {"testVoices", testVoices}, {"testStealLetGoFirst", testStealLetGoFirst}, {"testSteal", testSteal},
        {"testRangeTurnedDuringANote", testRangeTurnedDuringANote}, {"testGateHeldAtLevelZero", testGateHeldAtLevelZero}, {"testNotANumber", testNotANumber},
        {"testPlayFromTheTopWhileRinging", testPlayFromTheTopWhileRinging}, {"testTheTopIsHeld", testTheTopIsHeld}, {"testTick", testTick},
        {"testEnvelopeTimes", testEnvelopeTimes}, {"testAllAtZero", testAllAtZero}, {"testAKeyStartsOnItsSample", testAKeyStartsOnItsSample},
        {"testVca", testVca}, {"testVelocity", testVelocity}, {"testMacros", testMacros}, {"testGlide", testGlide}, {"testBlocks", testBlocks},
        {"testTwiceFromFresh", testTwiceFromFresh}, {"testPlayFromTheTop", testPlayFromTheTop}, {"testSilence", testSilence}, {"testSeededPhase", testSeededPhase},
        {"testNoAllocation", testNoAllocation}, {"testTooSmall", testTooSmall}, {"testTheParts", testTheParts},
    };
    bool any = false;
    for (int a = 1; a < argc; ++a) {
        if (std::string(argv[a]) == "--cost") {
            reportCost();
            return 0;
        }
        for (const Test& t : tests)
            if (std::string(argv[a]) == t.name) t.run(), any = true;
    }
    if (!any)
        for (const Test& t : tests) t.run();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("jx3p tests passed\n");
    return 0;
}
