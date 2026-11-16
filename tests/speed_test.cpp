// The speed plan's changes (docs/plans/speed-plan.md): each makes the same sound with less work,
// so each is held here to the sound it made before, to the bit, beside the old way of working it
// out in the same run. Nothing here times anything: a slow machine passes.
//
//   speed_test              everything
//   speed_test NAME...      just those tests (by the names in main)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/chord_rule.hpp"
#include "apotheca/drums.hpp"
#include "apotheca/engine.hpp"
#include "apotheca/glide_rule.hpp"
#include "apotheca/phazed.hpp"
#include "apotheca/rate_converter.hpp"
#include "bench_run.hpp"
#include "fx/glide.hpp"

extern "C" {
#include "dsp/cz_output.h"
}

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

// Two runs the same to the bit (+0 and -0 told apart, as a checksum would).
bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}
// Where they first part, for the message.
long firstApart(const std::vector<float>& a, const std::vector<float>& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return static_cast<long>(i);
    return a.size() == b.size() ? -1 : static_cast<long>(std::min(a.size(), b.size()));
}
bool anySound(const std::vector<float>& a) {
    for (float v : a)
        if (v != 0.f) return true;
    return false;
}

// ---- batch 0: the heaviest track first ------------------------------------------------------

// The busiest song for `seconds`, on `helpers` helpers, taken in track order or heaviest first.
std::vector<float> busiest(int helpers, bool heaviest, double seconds) {
    std::unique_ptr<Engine> engine(new Engine);
    sendBusiestSong(*engine);
    if (helpers > 0) engine->useWorkers(helpers);
    engine->setHeaviestFirst(heaviest);
    const int blocks = static_cast<int>(seconds * 48000.0 / 256.0);
    std::vector<float> out(static_cast<std::size_t>(blocks) * 512), block(512);
    for (int b = 0; b < blocks; ++b) {
        engine->render(block.data(), 256);
        engine->collectGarbage();
        std::memcpy(&out[static_cast<std::size_t>(b) * 512], block.data(), 512 * sizeof(float));
    }
    CHECK(engine->dropped() == 0);
    return out;
}

// The helpers taking the heaviest track first change nothing in the sound: the mix is in track
// order whoever worked a track out (Engine::setHeaviestFirst; the SPEED TEST's rows).
void testHeaviestFirst() {
    const std::vector<float> alone = busiest(0, false, 3.0);
    CHECK(anySound(alone));
    for (int helpers : {2, 3}) {
        const std::vector<float> inOrder = busiest(helpers, false, 3.0), heaviest = busiest(helpers, true, 3.0);
        CHECK(sameBits(alone, inOrder));
        CHECK(sameBits(alone, heaviest));
        if (!sameBits(alone, heaviest)) std::printf("  %d helpers, heaviest first: apart from sample %ld\n", helpers, firstApart(alone, heaviest));
    }
}


// ---- batch 1: the step clock walks only what waits ------------------------------------------

// A synth that makes a little sound of its own and writes down what it is told, with the track.
struct Told {
    long frame;
    int track, pitch, on;
    float velocity;
    int from;
    float seconds;
    bool operator==(const Told& o) const {
        return frame == o.frame && track == o.track && pitch == o.pitch && on == o.on && std::memcmp(&velocity, &o.velocity, sizeof velocity) == 0 && from == o.from &&
               std::memcmp(&seconds, &o.seconds, sizeof seconds) == 0;
    }
};
struct Spy : Instrument {
    std::vector<Told>* log;
    int track;
    long frame = 0;
    int held = 0;
    Spy(std::vector<Told>* to, int t) : log(to), track(t) {}
    void prepare(float) override {}
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int, float) override {}
    void noteOn(int note, float velocity) override { log->push_back({frame, track, note, 1, velocity, -1, 0.f}), ++held; }
    void noteOnFrom(int note, float velocity, int from, float seconds) override { log->push_back({frame, track, note, 1, velocity, from, seconds}), ++held; }
    void noteOff(int note) override { log->push_back({frame, track, note, 0, 0.f, -1, 0.f}), --held; }
    void allNotesOff() override { log->push_back({frame, track, -1, 0, 0.f, -1, 0.f}), held = 0; }
    void render(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.01f * static_cast<float>(held); // (so the mix hears what is held)
        frame += frames;
    }
};

Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}

// Everything that waits on the step clock: on eleven listening synth tracks, chords plain,
// strummed, as arpeggios, humanised and ratcheted, notes nudged early and late, ratchets, glides of
// every length (PAIRED on some tracks); on the drum track (0), hits on every other step, ratchets,
// nudges both ways, a lane with FEEL. Two scenes.
void stepClockSong(Engine& engine, std::vector<Told>* log) {
    for (int t = 1; t < kNumTracks; ++t) {
        Command c = make(Command::InstallSynth, t);
        c.pointer = new Spy(log, t);
        CHECK(engine.send(c));
    }
    std::vector<float> scratch(512);
    engine.render(scratch.data(), 64);
    for (int scene = 0; scene < 2; ++scene)
        for (int t = 1; t < kNumTracks; ++t) {
            int slot = 0;
            for (int chord = 0; chord < 4; ++chord) {
                const ChordPlay plays[6] = {ChordPlay(), chordPlayOf(kStrumWide, kOrderRandom, 100, 100, 100), chordPlayOf(kStrumQuarter, kOrderUpDown, 50, 0, 50),
                                            chordPlayOf(kStrumTight, kOrderDown, 0, 100, 0), chordPlayOf(kStrumHalf, kOrderUp, 100, 0, 0), chordPlayOf(kStrumStep, kOrderRandom, 100, 0, 100)};
                const ChordPlay play = plays[(t + chord + scene) % 6];
                for (int key = 0; key < 4; ++key) {
                    Command c = make(Command::SetNote, t, slot++, chord * 4, 0.7f + 0.05f * key);
                    c.scene = scene;
                    c.c = 40 + t + 3 * key + chord;
                    c.d = 2 + (t % 2);
                    c.e = (t % 3 == 0 ? 3 : 0) + (t % 4 == 1 ? 512 : 0);
                    c.play = play;
                    c.play.index = static_cast<std::uint8_t>(key);
                    c.play.count = 4;
                    CHECK(engine.send(c));
                }
            }
            for (int n = 0; n < 8; ++n) { // loose notes: nudged, ratcheted, gliding
                Command c = make(Command::SetNote, t, slot++, 3 + 4 * (n / 2), 0.8f); // (two on each of steps 3, 7, 11, 15)
                c.scene = scene;
                c.c = 60 + ((n * 7 + t) % 12);
                c.d = n % 3 == 0 ? 0 : 1;
                c.extra = n % 3 == 0 ? 0.5f : 0.25f;
                c.shift = n % 4 == 0 ? -0.4f : (n % 4 == 2 ? 0.3f : 0.f);
                c.e = (n % 5 == 1 ? 4 : 0) + (n % 2 ? 256 + (((n / 2) % 5 + 1) << 11) : 0);
                CHECK(engine.send(c));
            }
            for (int chord = 0; chord < 4; ++chord) { // a glide straight after each chord: from its keys (told when the chord's moment comes)
                Command c = make(Command::SetNote, t, slot++, chord * 4 + 2, 0.75f);
                c.scene = scene;
                c.c = 50 + t + chord;
                c.d = 1;
                c.e = 256 + ((chord % 5 + 1) << 11);
                CHECK(engine.send(c));
            }
            if (t % 3 == 2) CHECK(engine.send(make(Command::SetGlideFrom, t, kFromPaired)));
            if (t % 5 == 4) CHECK(engine.send(make(Command::SetGlideFrom, t, kFromRandom)));
            engine.render(scratch.data(), 64);
        }
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = lane % 2; step < kSteps; step += 2) {
            CHECK(engine.send(make(Command::SetStep, 0, lane, step, 0.9f)));
            if (step % 4 == 1) CHECK(engine.send(make(Command::SetStepNudge, 0, lane, step, lane % 2 ? -0.35f : 0.4f)));
            if (lane < 3 && step % 4 < 2) {
                Command c = make(Command::SetStepFx, 0, lane, step);
                c.d = 2 + lane;
                CHECK(engine.send(c));
            }
            engine.render(scratch.data(), 64);
        }
    Command feel = make(Command::SetLaneFeel, 0, 2, 100);
    feel.c = 50;
    CHECK(engine.send(feel));
    CHECK(engine.send(make(Command::SetSceneCount, 0, 2)));
    engine.render(scratch.data(), 64);
}

// The step clock walking only the places that may hold something (PlaceBits) does exactly what
// walking every place did: the same notes, begun and let go on the same frames, the same sound.
// Two engines side by side, one walking every place (gWalkEveryPlace), in blocks of many lengths,
// through tempo changes, a Stop, Play again and a scene played alone.
void testStepClockPlaces() {
    std::vector<Told> logs[2];
    std::unique_ptr<Engine> engines[2] = {std::unique_ptr<Engine>(new Engine), std::unique_ptr<Engine>(new Engine)};
    for (int k = 0; k < 2; ++k) {
        logs[k].reserve(200000);
        gWalkEveryPlace = k == 1;
        stepClockSong(*engines[k], &logs[k]);
    }
    std::vector<float> outs[2], block(2 * 512);
    std::uint32_t dice = 12345u;
    long frames = 0;
    for (int round = 0; frames < 48000L * 14; ++round) {
        dice = dice * 1664525u + 1013904223u;
        const int length = 1 + static_cast<int>((dice >> 8) % 300);
        const long second = frames / 48000;
        for (int k = 0; k < 2; ++k) {
            Engine& engine = *engines[k];
            gWalkEveryPlace = k == 1;
            const long at = frames;
            const auto crossed = [&](double s) { return at <= static_cast<long>(s * 48000) && at + length > static_cast<long>(s * 48000); };
            if (round == 0) CHECK(engine.send(make(Command::Play)));
            if (crossed(2.0)) CHECK(engine.send(make(Command::SetTempo, 0, 0, 0, 133.f)));
            if (crossed(3.7)) CHECK(engine.send(make(Command::SetTempo, 0, 0, 0, 87.f)));
            if (crossed(6.1)) CHECK(engine.send(make(Command::Stop)));
            if (crossed(6.6)) CHECK(engine.send(make(Command::SetTempo, 0, 0, 0, 240.f)));
            if (crossed(6.9)) CHECK(engine.send(make(Command::Play, 0, 1, 1))); // scene 2, alone
            if (crossed(10.2)) CHECK(engine.send(make(Command::SetTempo, 0, 0, 0, 121.f)));
            engine.render(block.data(), length);
            outs[k].insert(outs[k].end(), block.begin(), block.begin() + 2 * length);
        }
        frames += length;
        (void)second;
    }
    gWalkEveryPlace = false;
    CHECK(logs[0].size() > 3000);
    CHECK(logs[0] == logs[1]);
    if (!(logs[0] == logs[1]))
        for (std::size_t i = 0; i < logs[0].size() && i < logs[1].size(); ++i)
            if (!(logs[0][i] == logs[1][i])) {
                std::printf("  the first told apart: %zu, frame %ld / %ld, track %d / %d, key %d / %d\n", i, logs[0][i].frame, logs[1][i].frame, logs[0][i].track, logs[1][i].track,
                            logs[0][i].pitch, logs[1][i].pitch);
                break;
            }
    CHECK(sameBits(outs[0], outs[1]));
    CHECK(anySound(outs[0]));
    CHECK(engines[0]->dropped() == 0 && engines[1]->dropped() == 0);
}

// The busiest song (real synths, every effect the song has) the same both ways too.
void testBusiestWalksAlike() {
    std::vector<float> outs[2];
    for (int k = 0; k < 2; ++k) {
        gWalkEveryPlace = k == 1;
        outs[k] = busiest(0, false, 4.0);
    }
    gWalkEveryPlace = false;
    CHECK(anySound(outs[0]));
    CHECK(sameBits(outs[0], outs[1]));
    if (!sameBits(outs[0], outs[1])) std::printf("  apart from sample %ld\n", firstApart(outs[0], outs[1]));
}

// ---- batch 1: Phazed rests in silence ------------------------------------------------------

// Two Phazeds, one that rests and one that never does, given the same things in the same blocks
// (lengths 1 to 300): notes, chords, silences long enough to rest, the volume and a macro turned
// while at rest (the volume glides on through it), another preset, notes let go one by one and all
// at once. They must be the same to the bit, every sample, and the one must have rested.
void phazedPair(float rate) {
    std::unique_ptr<Phazed> synths[2] = {std::unique_ptr<Phazed>(new Phazed), std::unique_ptr<Phazed>(new Phazed)};
    for (auto& synth : synths) synth->prepare(rate);
    std::vector<float> outs[2], left(300), right(300);
    std::uint32_t dice = 777u;
    long frames = 0, rested = 0;
    const long end = static_cast<long>(rate) * 26;
    for (int round = 0; frames < end; ++round) {
        dice = dice * 1664525u + 1013904223u;
        const int length = 1 + static_cast<int>((dice >> 8) % 300);
        for (int k = 0; k < 2; ++k) {
            Phazed& synth = *synths[k];
            Phazed::setRestingForTests(k == 0);
            const auto crossed = [&](double s) { return frames <= static_cast<long>(s * rate) && frames + length > static_cast<long>(s * rate); };
            if (crossed(0.1)) synth.noteOn(60, 0.8f);
            if (crossed(0.6)) synth.noteOff(60);
            if (crossed(3.0)) synth.setSetting(phazed::Level, -12); // at rest: the volume glides on
            if (crossed(3.2)) synth.setMacro(phazed::Tone, 0.8f);
            if (crossed(3.5)) synth.noteOn(64, 0.6f), synth.noteOn(67, 0.7f), synth.noteOn(71, 0.5f); // woken
            if (crossed(4.4)) synth.noteOff(64), synth.noteOff(67);
            if (crossed(4.9)) synth.noteOff(71);
            if (crossed(8.0)) synth.selectPreset(5); // at rest
            if (crossed(8.3)) synth.setSetting(phazed::Level, 3);
            if (crossed(8.31)) synth.noteOnFrom(55, 0.9f, 48, 0.2f); // woken while the volume glides
            if (crossed(9.0)) synth.allNotesOff();
            if (crossed(14.0)) synth.selectPreset(1), synth.setMacro(phazed::Chorus, 1.f);
            if (crossed(15.0)) synth.noteOn(48, 1.f);
            if (crossed(15.0005)) synth.noteOff(48); // a note of a few samples
            if (crossed(20.0)) synth.noteOn(72, 0.4f), synth.noteOn(72, 0.4f);
            if (crossed(21.0)) synth.allNotesOff();
            synth.render(left.data(), right.data(), length);
            for (int i = 0; i < length; ++i) outs[k].push_back(left[i]), outs[k].push_back(right[i]);
        }
        frames += length;
    }
    Phazed::setRestingForTests(true);
    rested = synths[0]->restedForTests();
    CHECK(anySound(outs[0]));
    CHECK(sameBits(outs[0], outs[1]));
    if (!sameBits(outs[0], outs[1])) std::printf("  at %.0f Hz: apart from sample %ld\n", rate, firstApart(outs[0], outs[1]));
    CHECK(synths[1]->restedForTests() == 0);
    CHECK(rested > static_cast<long>(rate) * 12); // most of the silences (they are 18 s of the 26)
    std::printf("  at %.0f Hz: rested %.1f s of %.1f\n", rate, static_cast<double>(rested) / rate, static_cast<double>(frames) / rate);
}
void testPhazedRests() {
    phazedPair(48000.f); // through the rate converter, as in the program
    phazedPair(44100.f); // the CZ's own rate: no converter
}

// In the engine: twelve Phazed tracks that play, fall silent, are played again: the same both ways.
void testPhazedRestsInTheEngine() {
    std::vector<float> outs[2];
    for (int k = 0; k < 2; ++k) {
        Phazed::setRestingForTests(k == 0);
        std::unique_ptr<Engine> engine(new Engine);
        for (int t = 0; t < kNumTracks; ++t) engine->addSynthTrack(t, SynthKind::Phazed);
        std::vector<float> block(512);
        engine->render(block.data(), 64);
        for (int t = 0; t < kNumTracks; ++t) {
            Command c = make(Command::SetNote, t, 0, t % 16, 0.8f);
            c.c = 48 + t;
            c.d = 1;
            CHECK(engine->send(c));
        }
        CHECK(engine->send(make(Command::Play)));
        for (int b = 0; b < 48000 * 12 / 256; ++b) {
            engine->render(block.data(), 256);
            engine->collectGarbage();
            outs[k].insert(outs[k].end(), block.begin(), block.end());
        }
    }
    Phazed::setRestingForTests(true);
    CHECK(anySound(outs[0]));
    CHECK(sameBits(outs[0], outs[1]));
}

// The tables are made once and shared, and are what each synth made for itself before.
void testPhazedTablesShared() {
    std::unique_ptr<Phazed> a(new Phazed), b(new Phazed);
    a->prepare(48000.f);
    b->prepare(48000.f);
    CHECK(a->czTableForTests() == b->czTableForTests());
    CHECK(a->converterTableForTests() == b->converterTableForTests());
    resampler_t own;
    CHECK(resampler_init(&own) == 0);
    CHECK(std::memcmp(own.coef[RS_ANALOG], a->czTableForTests(), sizeof(float) * RS_UP * RS_TAPS) == 0);
    resampler_free(&own);
    RateConverter same, other;
    same.prepare(44100.0, 48000.0);
    other.prepare(48000.0, 44100.0);
    CHECK(same.table() == a->converterTableForTests());
    CHECK(other.table() != same.table());
}

// The converter's own part of the rest: silent() is true only once every sample it keeps, on
// both sides, is a zero (it is a guard: a Phazed resting has fed it zeros for longer already), and
// skip() leaves it where render() of zeros would, so what comes after is the same to the bit.
void testConverterSkips() {
    for (int side = 0; side < 2; ++side) {
        RateConverter c;
        c.prepare(44100.0, 48000.0);
        CHECK(c.silent());
        float l[64], r[64];
        int n = 0;
        c.render([&](float& a, float& b) { a = side == 0 && n == 3 ? 0.5f : 0.f, b = side == 1 && n == 3 ? 0.5f : 0.f, ++n; }, l, r, 8);
        CHECK(!c.silent());
        c.render([](float& a, float& b) { a = b = 0.f; }, l, r, 21); // 24 pulls since the sound: one is still kept
        CHECK(!c.silent());
        c.render([](float& a, float& b) { a = b = 0.f; }, l, r, 2);
        CHECK(c.silent());
    }
    RateConverter a, b;
    a.prepare(44100.0, 48000.0);
    b.prepare(44100.0, 48000.0);
    std::vector<float> outs[2];
    float l[300], r[300];
    long pulled[2] = {};
    std::uint32_t dice = 5u;
    for (int round = 0; round < 400; ++round) {
        dice = dice * 1664525u + 1013904223u;
        const int frames = 1 + static_cast<int>((dice >> 8) % 300);
        const bool quiet = (round / 20) % 2 == 1; // twenty blocks of sound, twenty of zeros
        for (int k = 0; k < 2; ++k) {
            RateConverter& c = k == 0 ? a : b;
            if (quiet && k == 0 && c.silent()) {
                pulled[k] += c.skip(frames);
                for (int i = 0; i < frames; ++i) l[i] = r[i] = 0.f;
            } else {
                c.render([&](float& x, float& y) { x = quiet ? 0.f : std::sin(0.01f * static_cast<float>(pulled[k])), y = quiet ? 0.f : 0.3f, ++pulled[k]; }, l, r, frames);
            }
            outs[k].insert(outs[k].end(), l, l + frames);
            outs[k].insert(outs[k].end(), r, r + frames);
        }
    }
    CHECK(pulled[0] == pulled[1]);
    CHECK(sameBits(outs[0], outs[1]));
    CHECK(anySound(outs[0]));
}

// ---- batch 1: the effects' way in looks at a block before it mends one ---------------------

void testSoundOnly() {
    const float odd[] = {std::nanf(""), INFINITY, -INFINITY, 1.0000001e8f, -2e9f};
    const float fine[] = {0.f, -0.f, 1e-40f, -1e-42f, 1e8f, -1e8f, 0.25f, -0.7f};
    std::uint32_t dice = 99u;
    int tried = 0;
    for (int frames = 1; frames <= 70; ++frames)
        for (int bad = -1; bad < frames; ++bad) { // -1: none bad
            std::vector<float> block(static_cast<std::size_t>(frames)), was;
            for (float& v : block) dice = dice * 1664525u + 1013904223u, v = fine[(dice >> 8) % 8];
            if (bad >= 0) block[static_cast<std::size_t>(bad)] = odd[(dice >> 12) % 5];
            was = block;
            std::vector<float> want = block;
            for (float& v : want)
                if (!(std::fabs(v) <= fx::kLoudest)) v = 0.f; // the old way, a sample at a time
            fx::soundOnly(block.data(), frames);
            CHECK(sameBits(block, want));
            ++tried;
        }
    CHECK(tried > 2000);
}

// ---- batch 2: the drum voice loop in runs ----------------------------------------------------

// Two drum machines on the built-in kit, one working every sample the long way (setRunsForTests),
// given the same things at the same moments in blocks of 1 to 300 frames: hits plain, pitched up
// and down, backwards, started part-way in, a part of the sound (its fade at its end), cut short,
// on a lane's own recording shorter than a block; hits that carry a sound on (slices in order), at
// another level (it glides) and another THROW (the bus share glides); lane levels and parts set,
// held auditions let go, the buses' ways out given in some blocks and not in others. The same bits
// on both sides and both buses, and the same buses written.
void testDrumRuns() {
    const DrumKit& kit = DrumKit::builtIn(48000.f);
    std::vector<float> recording(5000);
    for (std::size_t i = 0; i < recording.size(); ++i) recording[i] = std::sin(0.03f * static_cast<float>(i)) * (1.f - static_cast<float>(i) / 5000.f);
    std::unique_ptr<DrumMachine> machines[2] = {std::unique_ptr<DrumMachine>(new DrumMachine), std::unique_ptr<DrumMachine>(new DrumMachine)};
    for (auto& m : machines) {
        m->setKit(&kit);
        m->prepare(48000.f);
        m->setLaneSample(6, recording.data(), static_cast<int>(recording.size()));
    }
    const int crash = static_cast<int>(kit.samples[7].size());
    std::vector<float> outs[2];
    std::vector<int> wrote[2];
    float left[300], right[300], echo[300], space[300];
    std::uint32_t dice = 2024u;
    const auto roll = [&](int n) { dice = dice * 1664525u + 1013904223u; return static_cast<int>((dice >> 8) % static_cast<std::uint32_t>(n)); };
    long frames = 0, sliceFrom = -1;
    int slices = 0;
    while (frames < 48000L * 30) {
        const int length = 1 + roll(300);
        // What happens at this block's start: rolled once, done to both.
        struct Hit { int lane; float velocity, start, part; HitPlay how; };
        std::vector<Hit> hits;
        if (roll(3) == 0) {
            Hit h;
            h.lane = roll(kDrumLanes);
            const float velocities[3] = {1.f, 0.8f, 0.45f};
            h.velocity = velocities[roll(3)];
            h.start = roll(4) == 0 ? static_cast<float>(roll(16)) / 16.f : 0.f;
            const float parts[4] = {-1.f, 0.f, 0.05f, 0.2f};
            h.part = parts[roll(4)];
            const float rates[4] = {1.f, 1.f, 1.5f, 0.75f};
            h.how.rate = rates[roll(4)];
            h.how.reverse = roll(6) == 0;
            h.how.life = roll(4) == 0 ? 100 + roll(3000) : 0;
            h.how.throwTo = roll(4);
            h.how.audition = roll(8) == 0;
            hits.push_back(h);
        }
        // Slices of the crash in order, every quarter second or so: each carries the sound on.
        if (sliceFrom < 0 && roll(40) == 0) {
            sliceFrom = frames, slices = 0;
            Hit h{7, 0.9f, 0.01f, 0.f, HitPlay()};
            hits.push_back(h);
        } else if (sliceFrom >= 0 && frames - sliceFrom > 12000L * (slices + 1)) {
            ++slices;
            Hit h{7, slices % 2 ? 0.5f : 1.f, static_cast<float>(0.01 + static_cast<double>(frames - sliceFrom) / crash), 0.f, HitPlay()};
            h.how.throwTo = slices % 3; // the bus share glides when it changes
            hits.push_back(h);
            if (slices == 5) sliceFrom = -1;
        }
        const int lane = roll(kDrumLanes), what = roll(30);
        const float level = static_cast<float>(roll(5)) / 4.f, part = static_cast<float>(roll(3)) / 8.f;
        const bool buses = roll(3) != 0;
        for (int k = 0; k < 2; ++k) {
            DrumMachine& m = *machines[k];
            DrumMachine::setRunsForTests(k == 0);
            for (const Hit& h : hits) m.trigger(h.lane, h.velocity, h.start, h.part, h.how);
            if (what == 0) m.setLaneLevel(lane, level);
            if (what == 1) m.setLanePart(lane, part);
            if (what == 2) m.letGo(lane);
            for (int i = 0; i < length; ++i) echo[i] = space[i] = 7.f; // (left as found when not written)
            wrote[k].push_back(m.render(left, right, length, buses ? echo : nullptr, buses ? space : nullptr));
            outs[k].insert(outs[k].end(), left, left + length);
            outs[k].insert(outs[k].end(), right, right + length);
            outs[k].insert(outs[k].end(), echo, echo + length);
            outs[k].insert(outs[k].end(), space, space + length);
        }
        frames += length;
    }
    DrumMachine::setRunsForTests(true);
    CHECK(anySound(outs[0]));
    CHECK(wrote[0] == wrote[1]);
    CHECK(sameBits(outs[0], outs[1]));
    if (!sameBits(outs[0], outs[1])) std::printf("  apart from sample %ld\n", firstApart(outs[0], outs[1]));
}

// The busiest song (six drum tracks, ratchets, every hit thrown) the same both ways.
void testDrumRunsInTheSong() {
    std::vector<float> outs[2];
    for (int k = 0; k < 2; ++k) {
        DrumMachine::setRunsForTests(k == 0);
        outs[k] = busiest(0, false, 4.0);
    }
    DrumMachine::setRunsForTests(true);
    CHECK(anySound(outs[0]));
    CHECK(sameBits(outs[0], outs[1]));
    if (!sameBits(outs[0], outs[1])) std::printf("  apart from sample %ld\n", firstApart(outs[0], outs[1]));
}
} // namespace

int main(int argc, char** argv) {
    const struct { const char* name; void (*run)(); } tests[] = {
        {"testHeaviestFirst", testHeaviestFirst},
        {"testStepClockPlaces", testStepClockPlaces},
        {"testBusiestWalksAlike", testBusiestWalksAlike},
        {"testPhazedRests", testPhazedRests},
        {"testPhazedRestsInTheEngine", testPhazedRestsInTheEngine},
        {"testPhazedTablesShared", testPhazedTablesShared},
        {"testConverterSkips", testConverterSkips},
        {"testSoundOnly", testSoundOnly},
        {"testDrumRuns", testDrumRuns},
        {"testDrumRunsInTheSong", testDrumRunsInTheSong},
    };
    for (const auto& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        std::printf("%s\n", test.name);
        test.run();
    }
    if (failures) {
        std::printf("%d speed checks failed\n", failures);
        return 1;
    }
    std::printf("speed tests passed\n");
    return 0;
}
