// FM-6 (docs/plans/fm6-plan.md): the settings and the 32 factory sounds, the pitch, every
// preset played and ended, velocity, the macros and what a preset puts back, the assigner,
// glide, and the house rules (blocks, Play from the top while ringing, no allocation).
//
// Each test says FAITHFUL (held to the original's own numbers: the bank as published, a key's
// pitch) or SELF (held to the plan's own choice). A green SELF test only says the code does what
// the plan chose.
//
//   fm6_test                  all of them
//   fm6_test testPitch ...    just those
//   fm6_test --levels         each preset's loudest and how long it rings (the plan's table)
//   fm6_test --print          the pinned sound's checksums (tests/fm6_pins.inc), from code known to be right

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/fm6.hpp"
#include "measure.hpp"

using namespace apotheca;
using namespace apotheca::fm6;

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

static const int kRate = 48000;

// `frames` samples of the synth's left side (FM-6 is mono: the right is the same).
static std::vector<float> play(Fm6& s, int frames) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    s.render(l.data(), r.data(), frames);
    return l;
}
static std::unique_ptr<Fm6> preset(int index) {
    std::unique_ptr<Fm6> s(new Fm6);
    s->selectPreset(index);
    return s;
}
static int presetNamed(const char* name) {
    for (int i = 0; i < Fm6::presetCount(); ++i)
        if (std::strcmp(Fm6::presetName(i), name) == 0) return i;
    std::printf("no preset %s\n", name);
    ++failures;
    return 0;
}
// The loudest tenth of a second.
static double loudest(const std::vector<float>& v) {
    double most = 0.0;
    for (std::size_t i = 0; i + kRate / 10 <= v.size(); i += kRate / 10) most = std::max(most, rmsOf(v, i, i + kRate / 10));
    return most;
}
static double cents(double hz, int key) { return 1200.0 * std::log2(hz / (440.0 * std::exp2((key - 69) / 12.0))); }

// FAITHFUL: the 32 are the first factory cartridge, in its order, under its names (tools/fm6/README.md:
// the bank's checksum is right, every value in its range); and no name is a maker's or a model's.
static void testPresets() {
    const char* const names[32] = {"BRASS 1", "BRASS 2", "BRASS 3", "STRINGS 1", "STRINGS 2", "STRINGS 3", "ORCHESTRA", "PIANO 1",
                                   "PIANO 2", "PIANO 3", "E.PIANO 1", "GUITAR 1", "GUITAR 2", "SYN-LEAD 1", "BASS 1", "BASS 2",
                                   "E.ORGAN 1", "PIPES 1", "HARPSICH 1", "CLAV 1", "VIBE 1", "MARIMBA", "KOTO", "FLUTE 1",
                                   "ORCH-CHIME", "TUB BELLS", "STEEL DRUM", "TIMPANI", "REFS WHISL", "VOICE 1", "TRAIN", "TAKE OFF"};
    CHECK(Fm6::presetCount() == 32);
    for (int i = 0; i < 32; ++i) {
        CHECK(std::strcmp(Fm6::presetName(i), names[i]) == 0);
        CHECK(std::strcmp(Fm6::presetCategory(i), "ROM 1A") == 0);
        std::string low = Fm6::presetName(i);
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        CHECK(low.find("yamaha") == std::string::npos && low.find("dx") == std::string::npos);
        const Fm6Patch p = Fm6::presetPatch(i);
        for (int id = 0; id < kSettings; ++id) CHECK(p.value[id] >= settingMin(id) && p.value[id] <= settingMax(id));
        CHECK(p.value[OpsOn] == 63);
    }
    // A few values read off the bank by hand (the bank's own bytes, unpacked): BRASS 1 is algorithm 22,
    // feedback 7, its OP6 rates 49 99 28 68; E.PIANO 1 is algorithm 5; TAKE OFF has TRANSPOSE 0.
    const Fm6Patch brass = Fm6::presetPatch(0);
    CHECK(brass.value[Algorithm] == 21 && brass.value[Feedback] == 7);
    CHECK(brass.value[opSetting(6, OpR1)] == 49 && brass.value[opSetting(6, OpR2)] == 99 && brass.value[opSetting(6, OpR3)] == 28 && brass.value[opSetting(6, OpR4)] == 68);
    CHECK(Fm6::presetPatch(10).value[Algorithm] == 4);
    CHECK(Fm6::presetPatch(31).value[Transpose] == 0);
    // The settings' ranges are the original's.
    CHECK(settingMax(opSetting(1, OpCoarse)) == 31 && settingMax(opSetting(1, OpDetune)) == 14 && settingMax(opSetting(3, OpLeftCurve)) == 3);
    CHECK(settingMax(opSetting(2, OpRateScaling)) == 7 && settingMax(opSetting(2, OpMode)) == 1 && settingMax(opSetting(4, OpLevel)) == 99);
    CHECK(settingMax(Algorithm) == 31 && settingMax(Feedback) == 7 && settingMax(LfoWave) == 5 && settingMax(Transpose) == 48 && settingMax(OpsOn) == 63);
    CHECK(settingMax(-1) == 0 && settingMax(kSettings) == 0);
    // A setting is held to its range, and read back.
    Fm6 s;
    s.setSetting(Algorithm, 40);
    CHECK(s.setting(Algorithm) == 31);
    s.setSetting(Feedback, -3);
    CHECK(s.setting(Feedback) == 0);
    s.setSetting(kSettings, 5); // not a setting: nothing
    CHECK(s.setting(kSettings) == 0);
    s.selectPreset(32); // not a preset: nothing
    CHECK(s.setting(Algorithm) == 31);
    s.selectPreset(3);
    for (int id = 0; id < kSettings; ++id) CHECK(s.setting(id) == Fm6::presetPatch(3).value[id]);
}

// FAITHFUL: the start patch (OP1 alone, a ratio of 1) sings the key played, to a tenth of a cent;
// a ratio of 2 an octave up; TRANSPOSE moves it by semitones; a FIXED operator ignores the key.
static void testPitch() {
    for (int key = 36; key <= 96; key += 5) {
        Fm6 s;
        s.noteOn(key, 0.8f);
        const std::vector<float> v = play(s, kRate);
        CHECK(std::fabs(cents(pitchOf(v, kRate / 10, v.size()), key)) < 0.1);
    }
    {
        Fm6 s;
        s.setSetting(opSetting(1, OpCoarse), 2);
        s.noteOn(57, 0.8f);
        const std::vector<float> v = play(s, kRate);
        CHECK(std::fabs(cents(pitchOf(v, kRate / 10, v.size()), 69)) < 0.1);
    }
    {
        Fm6 s;
        s.setSetting(Transpose, 24 + 7);
        s.noteOn(62, 0.8f);
        const std::vector<float> v = play(s, kRate);
        CHECK(std::fabs(cents(pitchOf(v, kRate / 10, v.size()), 69)) < 0.1);
    }
    {
        // FIXED, coarse 2 (100 Hz), fine 0: 100 Hz whatever the key (the original's fixed table: 1, 10, 100, 1000 Hz).
        for (int key : {40, 80}) {
            Fm6 s;
            s.setSetting(opSetting(1, OpMode), 1);
            s.setSetting(opSetting(1, OpCoarse), 2);
            s.noteOn(key, 0.8f);
            const std::vector<float> v = play(s, kRate);
            CHECK(std::fabs(pitchOf(v, kRate / 10, v.size()) - 100.0) < 0.5);
        }
    }
    {
        // DETUNE 14 on OP1 alone: sharp, by a few cents (the original: about 7 counts of 0.75 cents at the top).
        Fm6 s;
        s.setSetting(opSetting(1, OpDetune), 14);
        s.noteOn(69, 0.8f);
        const std::vector<float> v = play(s, kRate);
        const double c = cents(pitchOf(v, kRate / 10, v.size()), 69);
        CHECK(c > 3.0 && c < 8.0);
    }
    {
        // OPERATOR ON/OFF: OP1 off (bit 5) and the start patch is silent; OP6 off (bit 0) changes nothing.
        for (int bit : {5, 0}) {
            Fm6 s;
            s.setSetting(OpsOn, 63 & ~(1 << bit));
            s.noteOn(69, 0.8f);
            const std::vector<float> v = play(s, kRate / 10);
            CHECK(bit == 5 ? peakOf(v) == 0.f : peakOf(v) > 0.05f);
        }
    }
    {
        // Another sample rate: still in tune (the tables are made for 48 kHz; the pitch is put right).
        Fm6 s;
        s.prepare(44100.f);
        s.noteOn(69, 0.8f);
        const std::vector<float> v = play(s, 44100);
        CHECK(std::fabs(cents(pitchOf(v, 4410, v.size(), 44100.0), 69)) < 0.2);
    }
}

// SELF (the levels) and FAITHFUL (that each ends): every preset, a chord of four held a second
// and a half, then let go: a number every sample, its loudest tenth of a second between -40 and
// -6 dB (four keys; TAKE OFF swells for seconds, so it is given ten), no sample past 1.5, and
// every voice free within twenty seconds of the keys going up, after which it is exact silence.
static void testEveryPresetPlaysAndEnds(bool print) {
    const int keys[4] = {48, 55, 60, 64};
    for (int p = 0; p < Fm6::presetCount(); ++p) {
        std::unique_ptr<Fm6> s = preset(p);
        for (int k : keys) s->noteOn(k, 0.8f);
        const int held = p == 31 ? 10 * kRate : kRate * 3 / 2;
        std::vector<float> v = play(*s, held);
        bool numbers = true;
        for (float x : v) numbers = numbers && std::isfinite(x);
        const double most = loudest(v);
        CHECK(numbers);
        CHECK(dB(most) > -40.0 && dB(most) < -6.0);
        CHECK(peakOf(v) < 1.5f);
        for (int k : keys) s->noteOff(k);
        int tenths = 0;
        while (s->voicesSounding() > 0 && tenths < 200) {
            const std::vector<float> t = play(*s, kRate / 10);
            for (float x : t) numbers = numbers && std::isfinite(x);
            ++tenths;
        }
        CHECK(s->voicesSounding() == 0);
        CHECK(numbers);
        const std::vector<float> after = play(*s, kRate / 10);
        CHECK(peakOf(after) == 0.f);
        if (print) std::printf("  %2d %-11s loudest %6.1f dB, peak %.2f, rings %.1f s after the keys go up\n", p, Fm6::presetName(p), dB(most), peakOf(v), tenths / 10.0);
    }
}

// FAITHFUL (the direction): a preset whose operators read the velocity is louder struck harder; the
// start patch, whose operators do not, is the same either way.
static void testVelocity() {
    const int piano = presetNamed("PIANO 1"); // (E.PIANO 1's velocity is mostly brightness: 3.4 dB)
    double level[2];
    for (int i = 0; i < 2; ++i) {
        std::unique_ptr<Fm6> s = preset(piano);
        s->noteOn(60, i == 0 ? 0.3f : 1.f);
        level[i] = loudest(play(*s, kRate / 2));
    }
    CHECK(dB(level[1] / level[0]) > 6.0);
    for (int i = 0; i < 2; ++i) {
        Fm6 s;
        s.noteOn(60, i == 0 ? 0.3f : 1.f);
        level[i] = loudest(play(s, kRate / 2));
    }
    CHECK(std::fabs(dB(level[1] / level[0])) < 0.01);
    // Not a number is a velocity of 0, never a stuck or a silent-for-good voice.
    Fm6 s;
    s.noteOn(60, NAN);
    const std::vector<float> v = play(s, kRate / 10);
    for (float x : v) CHECK(std::isfinite(x));
    s.noteOff(60);
    play(s, kRate);
    CHECK(s.voicesSounding() == 0);
}

// The engine's whole-song rule: the same sound however it is cut into blocks, to the bit. Notes on
// and off at odd samples, a glide, a macro turned, a setting changed, a steal, allNotesOff.
static void testBlocks() {
    struct Event { int at; int what; int a; float b; };
    // what: 0 note on, 1 note off, 2 glide, 3 macro, 4 setting, 5 all notes off
    const std::vector<Event> events = {
        {0, 0, 60, 0.8f}, {37, 0, 64, 0.5f}, {1000, 0, 67, 1.f}, {3001, 3, MacroBright, 0.8f}, {5003, 1, 64, 0},
        {7777, 2, 72, 0.7f}, {9000, 3, MacroLfo, 0.9f}, {12001, 4, Feedback, 2}, {15000, 1, 60, 0}, {20011, 5, 0, 0},
        {20500, 0, 48, 0.6f}, {26000, 3, MacroDetune, 0.2f}, {30000, 1, 48, 0}, {31000, 1, 67, 0}, {33333, 1, 72, 0},
    };
    const int total = 60000;
    const int sizes[4] = {1, 64, 256, 333};
    for (int p : {presetNamed("STRINGS 3"), presetNamed("E.PIANO 1"), presetNamed("KOTO")}) {
        std::vector<float> first;
        for (int size : sizes) {
            std::unique_ptr<Fm6> s = preset(p);
            std::vector<float> out(total), r(total);
            int now = 0;
            std::size_t next = 0;
            while (now < total) {
                while (next < events.size() && events[next].at == now) {
                    const Event& e = events[next++];
                    if (e.what == 0) s->noteOn(e.a, e.b);
                    if (e.what == 1) s->noteOff(e.a);
                    if (e.what == 2) s->noteOnFrom(e.a, e.b, e.a - 12, 0.2f);
                    if (e.what == 3) s->setMacro(e.a, e.b);
                    if (e.what == 4) s->setSetting(e.a, static_cast<int>(e.b));
                    if (e.what == 5) s->allNotesOff();
                }
                int n = std::min(size, total - now);
                if (next < events.size()) n = std::min(n, events[next].at - now);
                s->render(out.data() + now, r.data() + now, n);
                now += n;
            }
            if (first.empty()) first = out;
            else CHECK(out == first);
        }
        CHECK(peakOf(first) > 0.01f);
    }
}

// The house rule (as JAX-EP's): allNotesOff lets every key go and hands every sounding voice to a
// tail, where it rings out its release (never cut), and the 16 start again as from fresh with the
// LFO and the clock from the start. So what is played after is a fresh synth's sound plus the tails.
static void testPlayFromTheTopWhileRinging() {
    const int strings = presetNamed("STRINGS 3"); // a long release and a deep LFO
    auto phrase = [](Fm6& s, std::vector<float>& out) {
        out.assign(static_cast<std::size_t>(kRate), 0.f);
        std::vector<float> r(out.size());
        s.noteOn(60, 0.8f);
        s.noteOn(67, 0.6f);
        s.render(out.data(), r.data(), 20000);
        s.noteOff(60);
        s.noteOn(62, 0.9f);
        s.render(out.data() + 20000, r.data() + 20000, kRate - 20000);
    };
    std::unique_ptr<Fm6> fresh = preset(strings), b = preset(strings), c = preset(strings);
    for (Fm6* s : {b.get(), c.get()}) {
        s->noteOn(55, 1.f);
        s->noteOn(59, 1.f);
        play(*s, 30011);
        s->allNotesOff(); // Stop: the two ring out
        CHECK(s->tailsRinging() == 2 && s->voicesSounding() == 0);
        play(*s, 3000);
        s->allNotesOff(); // Play, a moment later, still ringing
    }
    // Not cut: the release is heard after Stop.
    CHECK(c->tailsRinging() == 2);
    std::vector<float> a, bOut;
    phrase(*fresh, a);
    phrase(*b, bOut);
    const std::vector<float> tails = play(*c, kRate);
    CHECK(rmsOf(tails, 0, 4800) > 0.001);
    float most = 0.f;
    for (std::size_t i = 0; i < a.size(); ++i) most = std::max(most, std::fabs(bOut[i] - tails[i] - a[i]));
    CHECK(most < 1e-5f);
    CHECK(peakOf(a) > 0.05f);
    // ...and with nothing ringing, the same to the bit.
    std::unique_ptr<Fm6> d = preset(strings);
    d->noteOn(70, 1.f);
    play(*d, 777);
    d->noteOff(70);
    for (int i = 0; i < 100 && (d->voicesSounding() > 0 || d->tailsRinging() > 0); ++i) play(*d, kRate / 10);
    d->allNotesOff();
    std::vector<float> dOut;
    phrase(*d, dOut);
    CHECK(dOut == a);
}

// SELF: 16 voices. A key played again takes its own voice; a free voice is taken before a sounding
// one; a steal takes a voice whose key is up before one held, the oldest first.
static void testVoices() {
    Fm6 s;
    for (int k = 0; k < kVoices; ++k) s.noteOn(40 + k, 0.8f);
    CHECK(s.voicesSounding() == kVoices);
    play(s, 64);
    s.noteOn(45, 0.8f); // again: its own voice
    CHECK(s.voicesSounding() == kVoices);
    int held45 = 0;
    for (int v = 0; v < kVoices; ++v) held45 += s.voiceNote(v) == 45 ? 1 : 0;
    CHECK(held45 == 1);
    s.noteOff(50); // let go, ringing
    s.noteOn(90, 0.8f); // steals 50's voice (let go), not 40's (older, but held)
    bool has40 = false, has50 = false;
    for (int v = 0; v < kVoices; ++v) has40 = has40 || s.voiceNote(v) == 40, has50 = has50 || s.voiceNote(v) == 50;
    CHECK(has40 && !has50);
    s.noteOn(91, 0.8f); // every key held: the oldest held goes (40)
    has40 = false;
    for (int v = 0; v < kVoices; ++v) has40 = has40 || s.voiceNote(v) == 40;
    CHECK(!has40);
    // Keys outside 0..127 are refused (a key below 0 would read as a free voice held for ever).
    Fm6 t;
    t.noteOn(-1, 0.8f);
    t.noteOn(128, 0.8f);
    CHECK(t.voicesSounding() == 0);
    for (int v = 0; v < kVoices; ++v) CHECK(!t.voiceHeld(v));
    t.noteOnFrom(200, 0.8f, 60, 0.1f);
    CHECK(t.voicesSounding() == 0);
}

// SELF: a key played again on a sounding voice carries on from its level (no jump: no click), where
// a cut to silence and a fresh start would jump.
static void testRetriggerHasNoClick() {
    for (int sync = 0; sync < 2; ++sync) {
        Fm6 s;
        s.setSetting(opSetting(1, OpR4), 50); // a release still well up when the key comes again
        s.setSetting(opSetting(1, OpR1), 60); // an attack of a few milliseconds: from silence it would dip
        s.setSetting(KeySync, sync);
        s.noteOn(69, 0.8f);
        play(s, 5000);
        s.noteOff(69);
        const std::vector<float> before = play(s, 100);
        CHECK(rmsOf(before) > 0.03);
        s.noteOn(69, 0.8f);
        const std::vector<float> v = play(s, 2000);
        const double step = sineStep(440.0, 0.1);
        // KEY SYNC OFF: the wave runs on, no jump at all. ON: the wave starts again from the top at
        // its level (the original's way): one jump, no more than the wave's own height.
        float jump = std::fabs(v[0] - before.back());
        CHECK(rmsOf(v, 0, 218) > 0.8 * rmsOf(before)); // (two cycles) it carries on from its level
        if (sync == 0) CHECK(jump < step * 1.2 && largestJump(v) < step * 1.2);
        else CHECK(jump > step * 2.0 && jump < 0.15f);
    }
}

// SELF: GLIDE slides from the key it came from to its own over the time asked for.
static void testGlide() {
    Fm6 s;
    s.noteOnFrom(69, 0.8f, 57, 0.5f);
    const std::vector<float> v = play(s, kRate);
    const double early = pitchOf(v, 0, 2400), late = pitchOf(v, kRate * 6 / 10, kRate);
    CHECK(early > 225.0 && early < 240.0);
    CHECK(std::fabs(cents(late, 69)) < 0.2);
    const double middle = pitchOf(v, 11000, 13000);
    CHECK(cents(middle, 69) > -800.0 && cents(middle, 69) < -400.0);
}

// The macros (SELF): each moves the sound of a preset it means something to, at 0 and at 1; at 0.5
// it is the patch. A preset puts them all back to the middle: the same sound as a fresh synth's.
static void testMacros() {
    struct Case { const char* preset; int macro; int frames; bool letGo; };
    const Case cases[] = {
        {"E.PIANO 1", MacroBright, kRate / 2, false}, {"BRASS 3", MacroFeedback, kRate / 2, false},
        {"STRINGS 1", MacroAttack, kRate / 2, false}, {"PIANO 1", MacroDecay, kRate, false},
        {"STRINGS 1", MacroRelease, kRate, true},     {"STRINGS 2", MacroDetune, kRate, false},
        {"STRINGS 3", MacroLfo, kRate * 2, false},    {"REFS WHISL", MacroPitchEnv, kRate / 2, false},
    };
    for (const Case& c : cases) {
        const int p = presetNamed(c.preset);
        std::vector<float> at[3];
        const float values[3] = {0.f, 0.5f, 1.f};
        for (int i = 0; i < 3; ++i) {
            std::unique_ptr<Fm6> s = preset(p);
            s->setMacro(c.macro, values[i]);
            s->noteOn(60, 0.8f);
            if (c.letGo) {
                play(*s, kRate / 2);
                s->noteOff(60);
            }
            at[i] = play(*s, c.frames);
        }
        std::unique_ptr<Fm6> plain = preset(p);
        plain->noteOn(60, 0.8f);
        if (c.letGo) {
            play(*plain, kRate / 2);
            plain->noteOff(60);
        }
        CHECK(play(*plain, c.frames) == at[1]);
        for (int i : {0, 2}) {
            double diff = 0.0;
            for (std::size_t n = 0; n < at[1].size(); ++n) diff += (at[i][n] - at[1][n]) * (at[i][n] - at[1][n]);
            diff = std::sqrt(diff / at[1].size());
            if (!(diff > 0.1 * rmsOf(at[1]))) std::printf("  macro %d at %.0f on %s: moved only %.4f of %.4f\n", c.macro, values[i], c.preset, diff, rmsOf(at[1]));
            CHECK(diff > 0.1 * rmsOf(at[1]));
        }
    }
    // Which way: BRIGHT down takes the modulators away (a plain sine: the carrier's harmonics gone),
    // up brings more of them; ATTACK up is slower; RELEASE up is longer.
    {
        // OP2 into OP1 at a ratio of 1: the second harmonic is the modulation's
        double second[3];
        for (int i = 0; i < 3; ++i) {
            Fm6 s;
            s.setSetting(Algorithm, 0);
            s.setSetting(opSetting(2, OpLevel), 60);
            s.setMacro(MacroBright, 0.5f * i);
            s.noteOn(69, 0.8f);
            const std::vector<float> v = play(s, kRate);
            second[i] = partAt(v.data() + kRate / 10, 4800, 880.0) / partAt(v.data() + kRate / 10, 4800, 440.0);
        }
        CHECK(second[0] < 0.001 && second[1] > 0.02 && second[2] > 2.0 * second[1]);
    }
    {
        // ATTACK's slowest is the original's slowest rate (R1 0), never slower.
        std::vector<float> v[2];
        for (int i = 0; i < 2; ++i) {
            Fm6 s;
            s.setSetting(opSetting(1, OpR1), 0);
            s.setMacro(MacroAttack, i == 0 ? 0.5f : 1.f);
            s.noteOn(69, 0.8f);
            v[i] = play(s, kRate);
        }
        CHECK(v[0] == v[1]);
    }
    {
        const int str = presetNamed("STRINGS 1");
        double early[2];
        for (int i = 0; i < 2; ++i) {
            std::unique_ptr<Fm6> s = preset(str);
            s->setMacro(MacroAttack, i == 0 ? 0.5f : 1.f);
            s->noteOn(60, 0.8f);
            early[i] = rmsOf(play(*s, kRate / 10));
        }
        CHECK(early[1] < 0.5 * early[0]);
        int tenths[2];
        for (int i = 0; i < 2; ++i) {
            std::unique_ptr<Fm6> s = preset(str);
            s->setMacro(MacroRelease, i == 0 ? 0.5f : 1.f);
            s->noteOn(60, 0.8f);
            play(*s, kRate / 2);
            s->noteOff(60);
            tenths[i] = 0;
            while (s->voicesSounding() > 0 && tenths[i] < 600) play(*s, kRate / 10), ++tenths[i];
        }
        CHECK(tenths[1] > 2 * tenths[0]);
    }
    // A preset puts every macro back.
    std::unique_ptr<Fm6> s = preset(5);
    for (int m = 0; m < kMacros; ++m) s->setMacro(m, 0.9f);
    s->selectPreset(5);
    s->noteOn(60, 0.8f);
    std::unique_ptr<Fm6> plain = preset(5);
    plain->noteOn(60, 0.8f);
    CHECK(play(*s, kRate) == play(*plain, kRate));
    // The same value again works nothing out (a MOD gives one often); not a number is not taken.
    const unsigned before = s->applied();
    s->setMacro(MacroBright, 0.5f);
    s->setSetting(Algorithm, s->setting(Algorithm));
    CHECK(s->applied() == before);
    s->setMacro(MacroBright, NAN);
    s->setMacro(kMacros, 0.2f);
    CHECK(s->applied() == before);
    s->setMacro(MacroBright, 0.6f);
    CHECK(s->applied() == before + 1);
}

// FAITHFUL (that it is there; its depth is a guess): the LFO on an operator's level, AMD and AMS:
// with AMS 3 and AMD 99 the level sways by more than 12 dB; with AMS 0 not at all.
static void testAmplitudeModulation() {
    double swing[2];
    for (int i = 0; i < 2; ++i) {
        Fm6 s;
        s.setSetting(LfoAmd, 99);
        s.setSetting(LfoWave, 4); // SINE
        s.setSetting(LfoSpeed, 20); // about 3.3 Hz
        s.setSetting(opSetting(1, OpAms), i == 0 ? 0 : 3);
        s.noteOn(69, 0.8f);
        const std::vector<float> v = play(s, 2 * kRate);
        double lo = 1e9, hi = 0.0;
        for (std::size_t n = 4800; n + 1200 <= v.size(); n += 1200) lo = std::min(lo, rmsOf(v, n, n + 1200)), hi = std::max(hi, rmsOf(v, n, n + 1200)); // (eleven whole cycles each)
        swing[i] = dB(hi / lo);
    }
    CHECK(swing[0] < 0.1);
    CHECK(swing[1] > 12.0);
}

// FAITHFUL: an envelope's decay is the original's, in time, at this sample rate (msfa's wiki,
// "Dx7Envelope": 0.2819 x 2^(qrate / 4) x (1 + (qrate mod 4) / 4) dB a second, qrate = R x 41 / 64;
// measured on the real one at its own 49096 samples a second, which msfa's rates count).
static void testEnvelopeTimes() {
    for (int rate : {30, 50, 62}) {
        Fm6 s;
        s.setSetting(opSetting(1, OpR2), rate);
        s.setSetting(opSetting(1, OpL2), 0);
        s.setSetting(opSetting(1, OpL3), 0);
        s.noteOn(69, 0.8f);
        const std::vector<float> v = play(s, kRate);
        const int q = rate * 41 / 64;
        const double want = 0.2819 * std::exp2(q / 4) * (1.0 + 0.25 * (q % 4));
        // Read over a stretch where the level is well clear of the top and of the floor.
        const double a = 0.05, span = std::min(0.4, 50.0 / want);
        const double got = (dB(rmsOf(v, static_cast<std::size_t>(a * kRate), static_cast<std::size_t>(a * kRate) + 2400)) -
                            dB(rmsOf(v, static_cast<std::size_t>((a + span) * kRate), static_cast<std::size_t>((a + span) * kRate) + 2400))) / span;
        if (std::fabs(got / want - 1.0) > 0.01) std::printf("  R2 %d: %.2f dB/s, want %.2f\n", rate, got, want);
        CHECK(std::fabs(got / want - 1.0) < 0.01);
    }
}

// SELF: a carrier's release always ends in silence, whatever its L4 (on the original a carrier with
// L4 over 0 rang for ever once let go: a voice that never ends). A modulator keeps its L4.
static void testACarrierAlwaysEnds() {
    Fm6 s;
    s.setSetting(opSetting(1, OpL4), 99);
    s.setSetting(opSetting(1, OpR4), 70);
    s.noteOn(60, 0.8f);
    play(s, kRate / 10);
    s.noteOff(60);
    for (int i = 0; i < 50 && s.voicesSounding() > 0; ++i) play(s, kRate / 10);
    CHECK(s.voicesSounding() == 0);
    // A modulator with L4 99 still holds its level through the release: the carrier's sound is
    // still bent there (its second harmonic stays while it fades).
    Fm6 m;
    m.setSetting(Algorithm, 0);                     // OP2 into OP1
    m.setSetting(opSetting(2, OpLevel), 70);
    m.setSetting(opSetting(2, OpL4), 99);
    m.setSetting(opSetting(1, OpR4), 40);           // a slow release on the carrier
    m.noteOn(69, 0.8f);
    play(m, kRate / 10);
    m.noteOff(69);
    const std::vector<float> v = play(m, kRate / 2);
    CHECK(partAt(v.data() + 9600, 4800, 880.0) > 0.1 * partAt(v.data() + 9600, 4800, 440.0));
}

// The sound pinned (SELF, from code shown to be msfa's own to the bit: the plan, section 4): a
// chord of each preset held a second and let go for half of one. A change to the core or to the
// voice moves these; `--print` writes them again, from code known to be right only.
static const std::uint64_t kPinned[32] = {
#include "fm6_pins.inc"
};
static void testPinned(bool print) {
    for (int p = 0; p < Fm6::presetCount(); ++p) {
        std::unique_ptr<Fm6> s = preset(p);
        for (int k : {48, 55, 60, 64}) s->noteOn(k, 0.8f);
        Sum sum;
        sum.add(play(*s, kRate));
        for (int k : {48, 55, 60, 64}) s->noteOff(k);
        sum.add(play(*s, kRate / 2));
        if (print) std::printf("    0x%016llxull, // %s\n", static_cast<unsigned long long>(sum.h), Fm6::presetName(p));
        else CHECK(sum.h == kPinned[p]);
    }
}

// The audio thread never allocates: everything a track does to its synth while the song plays.
static void testNoAllocation() {
    std::unique_ptr<Fm6> s = preset(3);
    std::vector<float> l(256), r(256);
    const long before = gNews;
    for (int round = 0; round < 3; ++round) {
        for (int k = 0; k < 20; ++k) s->noteOn(40 + k, 0.7f);
        s->noteOnFrom(80, 0.7f, 70, 0.1f);
        s->render(l.data(), r.data(), 256);
        s->setMacro(MacroBright, 0.3f + 0.1f * round);
        s->setSetting(Feedback, round);
        s->selectPreset(round);
        s->render(l.data(), r.data(), 100);
        for (int k = 0; k < 20; ++k) s->noteOff(40 + k);
        s->allNotesOff();
        s->render(l.data(), r.data(), 256);
    }
    CHECK(gNews == before);
}

// Through the engine: a track made as FM-6 plays what it is sent.
static void testThroughTheEngine() {
    std::unique_ptr<Engine> engine(new Engine);
    CHECK(engine->addSynthTrack(1, SynthKind::Fm6));
    Command c;
    c.track = 1;
    c.type = Command::SelectPreset;
    c.a = 10;
    engine->send(c);
    c = Command();
    c.track = 1;
    c.type = Command::NoteOn;
    c.a = 60;
    c.value = 0.8f;
    engine->send(c);
    std::vector<float> out(2 * 4800);
    engine->render(out.data(), 4800);
    float most = 0.f;
    for (float x : out) most = std::max(most, std::fabs(x));
    CHECK(most > 0.01f);
}

int main(int argc, char** argv) {
    struct Test { const char* name; std::function<void()> run; };
    const Test tests[] = {
        {"testPresets", testPresets}, {"testPitch", testPitch}, {"testEveryPresetPlaysAndEnds", [] { testEveryPresetPlaysAndEnds(false); }},
        {"testVelocity", testVelocity}, {"testBlocks", testBlocks}, {"testPlayFromTheTopWhileRinging", testPlayFromTheTopWhileRinging},
        {"testVoices", testVoices}, {"testRetriggerHasNoClick", testRetriggerHasNoClick}, {"testGlide", testGlide}, {"testMacros", testMacros},
        {"testAmplitudeModulation", testAmplitudeModulation}, {"testEnvelopeTimes", testEnvelopeTimes}, {"testACarrierAlwaysEnds", testACarrierAlwaysEnds},
        {"testPinned", [] { testPinned(false); }}, {"testNoAllocation", testNoAllocation}, {"testThroughTheEngine", testThroughTheEngine},
    };
    bool any = false;
    for (int a = 1; a < argc; ++a) {
        if (std::string(argv[a]) == "--print") {
            testPinned(true);
            return 0;
        }
        if (std::string(argv[a]) == "--levels") {
            testEveryPresetPlaysAndEnds(true);
            return failures ? 1 : 0;
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
    std::printf("fm6 tests passed\n");
    return 0;
}
