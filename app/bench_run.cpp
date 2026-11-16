// Timings of the things Apotheca does over and over, so that "is it getting
// slower?" has a number for an answer. Run it after a change that might cost:
//
//   cmake --build build --target bench && ./build/bench
//
// On the handheld: `apotheca --bench`, which the WiFi page's SPEED TEST runs
// (it is in the program itself, so that an update over WiFi brings it).
//
// The engine figures are how much of real time one core needs for each
// instrument at full polyphony (lower is better; the handheld is roughly a
// fifth as fast as a desk machine). The effect figures are the same for one
// effect in one slot. The app figures are per call.

#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/chord_rule.hpp"
#include "apotheca/fm6.hpp"
#include "apotheca/jx3p.hpp"
#include "apotheca/mod_rule.hpp"
#include "apotheca/phazed.hpp"
#include "app.hpp"
#include "bench_run.hpp"
#include "realtime.hpp"
#include "song_file.hpp"
#include "text.hpp"
#include "ui.hpp"

using namespace apotheca;

static double ms(const std::function<void()>& work, int times) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < times; ++i) work();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / times;
}

static void instrument(const char* name, SynthKind kind, int notes, int setting = -1, int value = 0) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->addSynthTrack(1, kind);
    Command c;
    c.track = 1;
    if (setting >= 0) {
        c.type = Command::SetSetting;
        c.a = setting;
        c.b = value;
        engine->send(c);
    }
    std::vector<float> out(512);
    engine->render(out.data(), 256);
    c.type = Command::NoteOn;
    c.value = 0.8f;
    for (int n = 0; n < notes; ++n) {
        c.a = 40 + 3 * n;
        engine->send(c);
    }
    const double block = ms([&] { engine->render(out.data(), 256); }, 600);
    std::printf("  %-28s %5.1f%% of one core\n", name, 100.0 * block / (256.0 / 48.0));
}

// The JX-3P (docs/plans/jx3p-plan.md, section 3): six notes, the most it plays. `how`:
//   0  the patch as a player meets it: A1 String I (both DCOs, the chorus on), the six keys held
//   1  the worst of the 32: B15 Planet (both DCOs, RESONANCE 10, the envelope on the VCF, so its cutoff
//      moves after every tick; the chorus on), each key played again every half second. The plan
//      named B12 Sync Sweep; measured, all 32 this way (2026-11-14): 0.47% (B3 Oboe) to 1.07% (Planet),
//      Sync Sweep 0.55%: its envelope moves DCO-2's pitch, which is cheap, and its filter stands still
//   2  a moving cutoff: every note a glide all the while, KEY FOLLOW 9.8, and (from batch 4) the LFO
//      on the cutoff and on DCO-1, so whatever is worked out from the pitch and the cutoff is worked out every sample
// (Until batch 7 rows 0 and 1 were the start patch and a patch made by hand: 0.52 and 0.96%.)
static void jx3pRow(const char* name, int how) {
    std::unique_ptr<Jx3p> synth(new Jx3p);
    synth->prepare(48000.f);
    if (how == 0) synth->selectPreset(0);
    if (how == 1) synth->selectPreset(30);
    if (how == 2) {
        synth->setSetting(jx3p::Cutoff, 40);
        synth->setSetting(jx3p::Resonance, 50);
        synth->setSetting(jx3p::KeyFollow, 98);
        synth->setSetting(jx3p::VcfLfo, 30);   // the LFO on the cutoff (batch 4): a sine at RATE 7
        synth->setSetting(jx3p::LfoRate, 70);
        synth->setSetting(jx3p::DcoLfoDepth, 20);
        synth->setSetting(jx3p::Dco1Lfo, 1);
    }
    std::vector<float> l(256), r(256);
    const int keys[6] = {48, 52, 55, 60, 64, 67};
    for (int k : keys) synth->noteOn(k, 0.8f);
    synth->render(l.data(), r.data(), 256);
    int block = 0;
    const double each = ms([&] {
        if (how > 0 && block % 94 == 0) // every half second
            for (int k : keys) {
                if (how == 2) synth->noteOnFrom(k, 0.8f, k - 12, 0.5f);
                else synth->noteOn(k, 0.8f);
            }
        ++block;
        synth->render(l.data(), r.data(), 256);
    }, 600);
    std::printf("  %-28s %5.2f%% of one core\n", name, 100.0 * each / (256.0 / 48.0));
}

// FM-6 (docs/plans/fm6-plan.md, section 6): `notes` keys of `preset`, held; `again`: each key played
// again every half second, so every operator's envelope is up (a decaying sound costs less as its
// operators fall under the level the core works out). Measured over the 32 (2026-11-15), 8 keys each:
// 0.22% (TUB BELLS) to 0.52% (BRASS 1, 2; STRINGS 1), most at 0.50 to 0.52: the cost is six operators
// a voice, whatever the patch. The worst row is 16 keys, every voice the synth has.
static void fm6Row(const char* name, int preset, int notes, bool again) {
    std::unique_ptr<Fm6> synth(new Fm6);
    synth->prepare(48000.f);
    synth->selectPreset(preset);
    std::vector<float> l(256), r(256);
    for (int n = 0; n < notes; ++n) synth->noteOn(40 + 3 * n, 0.8f);
    synth->render(l.data(), r.data(), 256);
    int block = 0;
    const double each = ms([&] {
        if (again && block % 94 == 0)
            for (int n = 0; n < notes; ++n) synth->noteOn(40 + 3 * n, 0.8f);
        ++block;
        synth->render(l.data(), r.data(), 256);
    }, 600);
    std::printf("  %-28s %5.2f%% of one core\n", name, 100.0 * each / (256.0 / 48.0));
}

// One effect, stereo at 48 kHz, in the engine's chunks of 64 frames, with
// noise going in so that nothing in it can rest. `setting` >= 0: that setting
// at `value` first (the filter's TYPE); `turning`, that setting turned all the while instead. The best of five runs, each ten
// seconds of sound: a busy machine only ever makes a run slower. A drum is
// hit eight times a second meanwhile (a sixteenth at 120 BPM), for DUCK,
// which does nothing until one is; no other effect hears it.
static void effect(const char* name, EffectKind kind, int setting = -1, float value = 0.f, bool turning = false) {
    std::unique_ptr<Effect> fx = makeEffect(kind, 48000.f, 120.f);
    if (setting >= 0) fx->setParam(setting, value);
    const int blocks = 7500;
    std::vector<float> noise(64 * 64);
    std::uint32_t seed = 12345u;
    for (float& v : noise) {
        seed = seed * 1664525u + 1013904223u;
        v = 0.5f * (static_cast<float>(static_cast<std::int32_t>(seed >> 8) - 8388608) / 8388608.f);
    }
    float left[64], right[64];
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            const float* in = noise.data() + 64 * (b % 63);
            for (int i = 0; i < 64; ++i) {
                left[i] = in[i];
                right[i] = in[i + 64];
            }
            if (b % 94 == 0) fx->drumHits(1u);
            if (turning) fx->setParam(setting, static_cast<float>(b % 200) / 200.f); // turned all the while: it never comes to rest
            fx->process(left, right, 64);
        }
        const double block = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / blocks;
        best = block < best ? block : best;
    }
    std::printf("  %-28s %6.2f%% of one core\n", name, 100.0 * best / (64.0 / 48.0));
}

// The send buses: twelve drum tracks, every lane busy, and what the sends add
// to that. `sends`: every track sends to both buses. `echo` and `space`: what
// the buses hold (None: they are fed and give nothing back, which is the cost
// of the feeding alone). `throws`: every hit of every track is thrown to both
// buses (THROW), which is the most throwing a song can do. `ducks`: a DUCK in
// a slot of every track as well. `feelTime`, `feelLevel`: FEEL, every lane of every kit at that
// HUMAN TIME and HUMAN LEVEL (so every hit is rolled for, asked a step ahead and set waiting).
// The best of five runs of ten seconds.
static std::unique_ptr<Engine> busSongEngine(bool sends, EffectKind echo, EffectKind space, bool throws = false, bool ducks = false, int feelTime = 0, int feelLevel = 0) {
    std::unique_ptr<Engine> engine(new Engine);
    for (int t = 1; t < kNumTracks; ++t) engine->addDrumTrack(t);
    Command c;
    for (int t = 0; t < kNumTracks; ++t) {
        c = Command();
        c.type = Command::SetStep;
        c.track = t;
        c.value = 1.f;
        for (int lane = 0; lane < kDrumLanes; ++lane)
            for (int step = (t + lane) % 2; step < kSteps; step += 2) c.a = lane, c.b = step, engine->send(c);
        if (throws) {
            StepFx fx;
            fx.throwTo = kThrowEcho | kThrowSpace;
            c = Command();
            c.type = Command::SetStepFx;
            c.track = t;
            c.extra = fx.cutAndThrow();
            for (int lane = 0; lane < kDrumLanes; ++lane)
                for (int step = (t + lane) % 2; step < kSteps; step += 2) c.a = lane, c.b = step, engine->send(c);
        }
        c = Command();
        c.type = Command::SetSend;
        c.track = t;
        c.value = sends ? 0.5f : 0.f;
        for (int bus = 0; bus < kBuses; ++bus) c.a = bus, engine->send(c);
        c = Command();
        c.type = Command::SetLaneFeel;
        c.track = t;
        c.b = feelTime;
        c.c = feelLevel;
        if (feelTime || feelLevel)
            for (int lane = 0; lane < kDrumLanes; ++lane) c.a = lane, engine->send(c);
    }
    if (ducks)
        for (int t = 0; t < kNumTracks; ++t) engine->setEffect(t, 0, EffectKind::Duck);
    if (echo != EffectKind::None) engine->setEffect(kEchoBus, 0, echo);
    if (space != EffectKind::None) engine->setEffect(kSpaceBus, 0, space);
    c = Command();
    c.type = Command::Play;
    engine->send(c);
    std::vector<float> out(512);
    for (int i = 0; i < 400; ++i) engine->render(out.data(), 256);
    return engine;
}
static double busSong(bool sends, EffectKind echo, EffectKind space, bool throws = false, bool ducks = false, int feelTime = 0, int feelLevel = 0) {
    std::unique_ptr<Engine> engine = busSongEngine(sends, echo, space, throws, ducks, feelTime, feelLevel);
    std::vector<float> out(512);
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        const double block = ms([&] { engine->render(out.data(), 256); }, 1875);
        best = block < best ? block : best;
    }
    return 100.0 * best / (256.0 / 48.0);
}

// CHORD EFFECTS: what the step clock's own work costs. Eleven tracks, each a sampler with no
// recording (so there is next to no sound to work out), four chords of four keys a bar on
// each; `play` is what every chord does. The best of five runs of ten seconds.
// `from`: -1, or the tracks' GLIDE FROM, and then two notes that glide follow each chord (so
// the choice of key is worked out eight times a bar on each track; PAIRED also goes through
// each step's notes twice).
static std::unique_ptr<Engine> chordSongEngine(ChordPlay play, int ratchet, int from = -1) {
    std::unique_ptr<Engine> engine(new Engine);
    Command c;
    for (int t = 1; t < kNumTracks; ++t) {
        engine->addSynthTrack(t, SynthKind::Sampler);
        for (int chord = 0; chord < 4; ++chord)
            for (int key = 0; key < 4; ++key) {
                c = Command();
                c.type = Command::SetNote;
                c.track = t;
                c.a = chord * 4 + key;
                c.b = chord * 4;
                c.c = 48 + t + 4 * key;
                c.d = 4;
                c.e = ratchet;
                c.value = 0.8f;
                c.play = play;
                c.play.index = static_cast<std::uint8_t>(key);
                c.play.count = 4;
                engine->send(c);
            }
        if (from < 0) continue;
        for (int chord = 0; chord < 4; ++chord)
            for (int key = 0; key < 2; ++key) {
                c = Command();
                c.type = Command::SetNote;
                c.track = t;
                c.a = 16 + chord * 2 + key;
                c.b = chord * 4 + 2;
                c.c = 50 + t + 9 * key;
                c.d = 1;
                c.e = 256 + (kGlideFull << 11);
                c.value = 0.8f;
                engine->send(c);
            }
        c = Command();
        c.type = Command::SetGlideFrom;
        c.track = t;
        c.a = from;
        engine->send(c);
    }
    c = Command();
    c.type = Command::Play;
    engine->send(c);
    std::vector<float> out(512);
    for (int i = 0; i < 400; ++i) engine->render(out.data(), 256);
    return engine;
}
static double chordSong(ChordPlay play, int ratchet, int from = -1) {
    std::unique_ptr<Engine> engine = chordSongEngine(play, ratchet, from);
    std::vector<float> out(512);
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        const double block = ms([&] { engine->render(out.data(), 256); }, 1875);
        best = block < best ? block : best;
    }
    return 100.0 * best / (256.0 / 48.0);
}

// THE BUSIEST SONG (the pit stop audit's, 2026-11-14): 140 BPM, twelve tracks. Six drum
// tracks, every lane on every other step, ratchets of four on three lanes, every hit thrown to
// both buses. Two JAX-EP (A1 String I), two FM-6 (E.PIANO 1) and two Phazed, each four chords of four keys a
// bar, half WIDE STRUM and half arpeggios at a quarter step, RANDOM, human at 100%, thrown;
// a ratchet of two on the strummed ones. COMP, then SATURATE or CHORUS, then HALL or PLATE on
// every track; TAPE and PLATE on the buses, every track sending 0.4 to each; COMP and SATURATE
// on the master. Flush-to-zero on, as in the program. (Until 2026-11-15 the two JAX-EP tracks
// were the synth that went, at x2 oversampling, and later that day the two FM-6 tracks were QMOD, the other
// synth that went: the figures before then are those songs'.)
static void busiestSong(Engine& e) {
    std::vector<float> scratch(512);
    const auto settle = [&](int blocks) {
        for (int r = 0; r < blocks; ++r) e.render(scratch.data(), 256);
    };
    Command c;
    c.type = Command::SetTempo;
    c.value = 140.f;
    e.send(c);
    for (int t = 1; t < 6; ++t) e.addDrumTrack(t); // (track 1 has drums from the start)
    const SynthKind kinds[6] = {SynthKind::Jx3p, SynthKind::Jx3p, SynthKind::Fm6, SynthKind::Fm6, SynthKind::Phazed, SynthKind::Phazed};
    for (int i = 0; i < 6; ++i) e.addSynthTrack(6 + i, kinds[i]);
    settle(4);
    for (int i = 0; i < 6; ++i)
        if (kinds[i] != SynthKind::Phazed) { // JAX-EP as a new track in the program is, on A1; FM-6 on E.PIANO 1, its
            c = Command();                  // costliest honest case (it rings longest of the common sounds). The
            c.type = Command::SelectPreset; // engine makes each on its start patch, which is not a preset.
            c.track = 6 + i;
            c.a = kinds[i] == SynthKind::Fm6 ? kBusiestFm6Preset : 0;
            e.send(c);
        }
    for (int t = 0; t < 6; ++t) {
        for (int lane = 0; lane < kDrumLanes; ++lane)
            for (int step = (t + lane) % 2; step < kSteps; step += 2) {
                c = Command();
                c.type = Command::SetStep;
                c.track = t;
                c.value = 1.f;
                c.a = lane;
                c.b = step;
                e.send(c);
                StepFx fx;
                fx.throwTo = kThrowEcho | kThrowSpace;
                c = Command();
                c.type = Command::SetStepFx;
                c.track = t;
                c.a = lane;
                c.b = step;
                c.extra = fx.cutAndThrow();
                c.d = (lane < 3 && step % 4 < 2) ? 4 : 0; // the ratchet
                e.send(c);
            }
        settle(2);
    }
    for (int i = 0; i < 6; ++i) {
        const int t = 6 + i;
        ChordPlay play = i % 2 == 0 ? chordPlayOf(kStrumWide, kOrderRandom, 100, 100, 100) : chordPlayOf(kStrumQuarter, kOrderRandom, 100, 100, 100);
        for (int chord = 0; chord < 4; ++chord) {
            for (int key = 0; key < 4; ++key) {
                c = Command();
                c.type = Command::SetNote;
                c.track = t;
                c.a = chord * 4 + key;
                c.b = chord * 4;
                c.c = 48 + i + 4 * key + (chord % 2) * 3;
                c.d = 4;
                c.e = (i % 2 == 0 ? 2 : 0) + 512 + 1024; // a ratchet of two on the strummed; thrown to both
                c.value = 0.8f;
                c.play = play;
                c.play.index = static_cast<std::uint8_t>(key);
                c.play.count = 4;
                e.send(c);
            }
            settle(1);
        }
    }
    for (int t = 0; t < kNumTracks; ++t) {
        e.setEffect(t, 0, EffectKind::Comp);
        e.setEffect(t, 1, t % 2 ? EffectKind::Chorus : EffectKind::Saturate);
        e.setEffect(t, 2, t % 2 ? EffectKind::Plate : EffectKind::Hall);
        settle(2);
    }
    e.setEffect(kMasterTrack, 0, EffectKind::Comp);
    e.setEffect(kMasterTrack, 1, EffectKind::Saturate);
    e.setEffect(kEchoBus, 0, EffectKind::Tape);
    e.setEffect(kSpaceBus, 0, EffectKind::Plate);
    for (int t = 0; t < kNumTracks; ++t)
        for (int bus = 0; bus < kBuses; ++bus) {
            c = Command();
            c.type = Command::SetSend;
            c.track = t;
            c.a = bus;
            c.value = 0.4f;
            e.send(c);
        }
    settle(20);
    e.collectGarbage();
    e.setFlushDenormals(true);
    c = Command();
    c.type = Command::Play;
    e.send(c);
}

// MODs on the busiest song: one, on the first drum track's ECHO send; or 24, two on every track,
// every kind of target a MOD takes (batch 9): MOD 1 an LFO on a drum track's second slot's first
// setting (SATURATE's DRIVE or CHORUS's first) or a synth track's first macro; MOD 2 a STEPPER on
// the ECHO send (tracks 0 to 3 and 6 to 9), on LEVEL (4 and 10) or on PAN (5 and 11); and every
// track's MOD 1 DEPTH lane written on three steps of the bar (+30%, -30%, +60%), so the DEPTH lanes
// glide as well. LFOs of four rates and seven shapes, STEPPERs of three rates at CHANCE 50%, SLEW 30%.
static ModSettings benchMod(int kind, int target, int rate, int shape) {
    ModSettings m;
    m.kind = kind;
    m.target = target;
    m.depth = 0.3f;
    m.rate = rate;
    m.shape = shape;
    m.chance = 0.5f;
    m.slew = 0.3f;
    for (int i = 0; i < kModSteps; ++i) m.steps[i] = static_cast<float>((i * 5) % 9) / 4.f - 1.f;
    return m;
}
static void busiestMods(Engine& e, int mods) {
    if (mods <= 0) return;
    if (mods == 1) {
        e.setMod(0, 0, benchMod(kModLfo, autoLaneOfSend(0), kModRateQuarter, kModSine));
        return;
    }
    for (int t = 0; t < kNumTracks; ++t) {
        const bool drums = t < 6;
        const int place = t % 6; // 0..3 the ECHO send, 4 LEVEL, 5 PAN
        e.setMod(t, 0, benchMod(kModLfo, drums ? autoLaneOfEffect(1, 0) : 0, kModRateEighth + t % 4, t % kModShapes));
        e.setMod(t, 1, benchMod(kModStepper, place < 4 ? autoLaneOfSend(0) : (place == 4 ? kModTargetLevel : kModTargetPan), kModRateStep + t % 3, 0));
        const float depths[3] = {0.3f, -0.3f, 0.6f};
        for (int i = 0; i < 3; ++i) {
            Command c;
            c.type = Command::SetAutomation;
            c.track = t;
            c.scene = 0;
            c.a = modDepthLane(0);
            c.b = 4 * i;
            c.value = modDepthToLane(depths[i]);
            e.send(c);
        }
    }
}
void apotheca::sendBusiestMods(Engine& engine, int mods) { busiestMods(engine, mods); }

// The busiest song in real time: a block is worked out, then the thread sleeps until the next
// is due, as the sound card's thread does. On its own thread, which asks for realtime priority
// as the program's audio thread does (`realtime`), and the helpers as the program's do.
static WholeSongTimes playWholeSong(int helpers, double seconds, bool realtime, int mods = 0, bool heaviest = false) {
    WholeSongTimes times;
    std::thread player([&] {
        if (realtime) times.realtime = makeThreadRealtime(70);
        std::unique_ptr<Engine> engine(new Engine);
        busiestSong(*engine);
        busiestMods(*engine, mods);
        std::vector<float> out(512);
        for (int i = 0; i < 400; ++i) { // two seconds, so every tail and bus is going
            engine->render(out.data(), 256);
            engine->collectGarbage();
        }
        if (helpers > 0) engine->useWorkers(helpers, realtime ? static_cast<void (*)()>([] { makeThreadRealtime(69); }) : nullptr);
        engine->setHeaviestFirst(heaviest);
        times.helpers = engine->workers();
        const double blockMs = 256.0 / 48.0;
        const int blocks = static_cast<int>(seconds * 48000.0 / 256.0);
        std::vector<double> took(static_cast<std::size_t>(blocks > 0 ? blocks : 0));
        const std::uint64_t chunksBefore = engine->chunksRendered();
        const std::chrono::microseconds each(5333);
        auto due = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            const auto t0 = std::chrono::steady_clock::now();
            engine->render(out.data(), 256);
            const auto t1 = std::chrono::steady_clock::now();
            took[static_cast<std::size_t>(b)] = std::chrono::duration<double, std::milli>(t1 - t0).count();
            for (float v : out) times.peak = std::max(times.peak, std::fabs(v));
            engine->collectGarbage(); // (the program's screen thread does this, once a frame)
            due += each;
            if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until(due);
            else due = std::chrono::steady_clock::now(); // behind: the card asks for the next at once
        }
        times.blocks = blocks;
        if (blocks == 0) return;
        times.chunksPerSecond = static_cast<double>(engine->chunksRendered() - chunksBefore) / (blocks * 256.0 / 48000.0);
        double sum = 0.0;
        for (double t : took) {
            sum += t;
            if (t > blockMs) ++times.over;
        }
        times.meanMs = sum / blocks;
        std::sort(took.begin(), took.end());
        times.p99Ms = took[static_cast<std::size_t>(blocks) * 99 / 100];
        times.worstMs = took.back();
    });
    player.join();
    return times;
}

// What one macro change costs a synth, a call (a MOD on a macro hands one over every
// kModTick * kModMacroTicks samples): the synth on its own, eight notes held, the value moved
// every call between two values that are not the middle; the best of five runs, so the cache is
// warm and this is a floor. Then what 24 MODs (12 tracks x 2) on that synth's macros would cost
// at the macro tick (512 samples) and at the effects' tick (64), and a call that gives the value
// the synth already has.
static void macroCost(const char* name, Instrument& synth, int macro) {
    synth.prepare(48000.f);
    synth.selectPreset(0);
    for (int n = 0; n < 8; ++n) synth.noteOn(40 + 3 * n, 0.8f);
    std::vector<float> left(256), right(256);
    for (int i = 0; i < 20; ++i) synth.render(left.data(), right.data(), 256);
    const int calls = 4000;
    double best = 1e30, bestSame = 1e30;
    for (int run = 0; run < 5; ++run) {
        int k = 0;
        best = std::min(best, ms([&] { synth.setMacro(macro, (++k & 1) ? 0.62f : 0.38f); }, calls));
        synth.render(left.data(), right.data(), 256);
        bestSame = std::min(bestSame, ms([&] { synth.setMacro(macro, 0.38f); }, calls));
        synth.render(left.data(), right.data(), 256);
    }
    const double us = best * 1000.0, same = bestSame * 1000.0;
    const double perSecondSlow = 48000.0 / (kModTick * kModMacroTicks), perSecondFast = 48000.0 / kModTick;
    std::printf("  %-28s %6.3f us a call (%5.3f the same value); 24 MODs: %5.2f%% of one core at %d samples, %5.2f%% at %d\n", name, us, same,
                100.0 * 24.0 * perSecondSlow * us / 1e6, kModTick * kModMacroTicks, 100.0 * 24.0 * perSecondFast * us / 1e6, kModTick);
}

WholeSongTimes apotheca::timeWholeSong(int helpers, double seconds, int mods) { return playWholeSong(helpers, seconds, false, mods); }
void apotheca::sendBusiestSong(Engine& engine) { busiestSong(engine); }

// What working MODs out costs, with next to nothing under it: twelve drum tracks that play no hit,
// their MODs on the sends (no effect on the buses, so nothing wakes), the chunks a tick cuts
// included. 0, 1 or 24 MODs; the difference is the MODs'. The three engines are timed in turns,
// a short run each, many rounds, and each keeps its best: the difference is a tenth of a percent
// and a busy machine moves the whole row more than that between two runs. Timed one after
// another, one run read 24 MODs as -0.04% and the next +0.10% (review, 2026-11-15).
// `mix`: the 24 on LEVEL and PAN instead (batch 8: a track whose level or pan moves takes the
// render's moving loop, not its at-rest one). `lfos` 1: the 24 all LFOs in time; 2: the same 24
// FREE, each at the SPEED its rate has at 120 (what FREE costs over an LFO in time).
static std::unique_ptr<Engine> modsAloneEngine(int mods, bool mix = false, int lfos = 0) {
    std::unique_ptr<Engine> engine(new Engine);
    std::vector<float> out(512);
    for (int t = 1; t < kNumTracks; ++t) engine->addDrumTrack(t);
    engine->render(out.data(), 256);
    for (int m = 0; m < mods; ++m) {
        const int target = mix ? (m % 2 ? kModTargetPan : kModTargetLevel) : autoLaneOfSend(m % 2);
        ModSettings mod = benchMod(m % 2 && !lfos ? kModStepper : kModLfo, target, kModRateEighth + m % 5, m % kModShapes);
        if (lfos == 2) mod.speed = static_cast<float>(8.0 / modRateSteps(mod.rate)), mod.rate = kModRateFree; // (8 steps a second at 120)
        engine->setMod(m / 2, m % 2, mod);
    }
    Command c;
    c.type = Command::Play;
    engine->send(c);
    for (int i = 0; i < 40; ++i) engine->render(out.data(), 256);
    return engine;
}

struct ModsAloneTimes {
    double none = 0.0, one = 0.0, all = 0.0, mix = 0.0, synced = 0.0, free = 0.0;
};

static ModsAloneTimes modsAlone() {
    std::unique_ptr<Engine> engines[6] = {modsAloneEngine(0), modsAloneEngine(1), modsAloneEngine(24), modsAloneEngine(24, true), modsAloneEngine(24, false, 1), modsAloneEngine(24, false, 2)};
    std::vector<float> out(512);
    double best[6] = {1e30, 1e30, 1e30, 1e30, 1e30, 1e30};
    for (int round = 0; round < 40; ++round)
        for (int i = 0; i < 6; ++i) {
            Engine& engine = *engines[i];
            best[i] = std::min(best[i], ms([&] { engine.render(out.data(), 256); }, 100));
        }
    ModsAloneTimes t;
    t.none = 100.0 * best[0] / (256.0 / 48.0);
    t.one = 100.0 * best[1] / (256.0 / 48.0);
    t.all = 100.0 * best[2] / (256.0 / 48.0);
    t.mix = 100.0 * best[3] / (256.0 / 48.0);
    t.synced = 100.0 * best[4] / (256.0 / 48.0);
    t.free = 100.0 * best[5] / (256.0 / 48.0);
    return t;
}

// 24 MODs on macros, run through the engine (not worked out from one call): twelve tracks of one
// synth, no note held (a synth that sounds costs the same with or without them, and its own time
// would bury these), each track's MOD 1 a SINE on macro 1 and MOD 2 a STEPPER on macro 2, both
// moving at every macro tick. 0 or 2 MODs a track; the difference is the macro ticks' whole cost.
static std::unique_ptr<Engine> macroModsEngine(SynthKind kind, bool mods) {
    std::unique_ptr<Engine> engine(new Engine);
    std::vector<float> out(512);
    for (int t = 0; t < kNumTracks; ++t) engine->addSynthTrack(t, kind);
    engine->render(out.data(), 256);
    for (int t = 0; t < kNumTracks && mods; ++t) {
        engine->setMod(t, 0, benchMod(kModLfo, 0, kModRateHalfStep, kModSine));
        engine->setMod(t, 1, benchMod(kModStepper, 1, kModRateHalfStep, 0));
    }
    Command c;
    c.type = Command::Play;
    engine->send(c);
    for (int i = 0; i < 40; ++i) engine->render(out.data(), 256);
    return engine;
}
// The two engines timed in turns, as modsAlone's (twelve silent Phazed tracks are 2.4% by
// themselves, and one after the other the 24 MODs read -0.06% in one run, +0.16% in the next).
static void macroModsRun(SynthKind kind, double& none, double& all) {
    std::unique_ptr<Engine> engines[2] = {macroModsEngine(kind, false), macroModsEngine(kind, true)};
    std::vector<float> out(512);
    double best[2] = {1e30, 1e30};
    for (int round = 0; round < 30; ++round)
        for (int i = 0; i < 2; ++i) {
            Engine& engine = *engines[i];
            best[i] = std::min(best[i], ms([&] { engine.render(out.data(), 256); }, 100));
        }
    none = 100.0 * best[0] / (256.0 / 48.0);
    all = 100.0 * best[1] / (256.0 / 48.0);
}

// ---- What the machine is (docs/plans/speed-plan.md, batch 0) -----------------------------------
// The desk cannot say how fast the handheld's cores are let run, how big their cache is, or what
// its sound card takes. These rows say it, from Linux's own files; elsewhere they say so.

static std::string firstLine(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "r");
    if (!file) return "";
    char line[256] = {};
    const bool got = std::fgets(line, sizeof line, file) != nullptr;
    std::fclose(file);
    std::string text = got ? line : "";
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}
static std::string cpuFile(unsigned core, const char* name) { return "/sys/devices/system/cpu/cpu" + std::to_string(core) + "/" + name; }
static int mhzIn(const std::string& path) { // the file says kHz; -1: not told
    const std::string text = firstLine(path);
    return text.empty() ? -1 : static_cast<int>(std::atol(text.c_str()) / 1000);
}

// The governor and every core's clock: now, where the governor holds it, and how far the core can go.
static void processorRows(const char* when, bool all) {
    std::printf("the processor, %s:\n", when);
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    bool told = false;
    for (unsigned c = 0; c < cores; ++c) {
        const std::string governor = firstLine(cpuFile(c, "cpufreq/scaling_governor"));
        const int now = mhzIn(cpuFile(c, "cpufreq/scaling_cur_freq"));
        if (governor.empty() && now < 0) continue;
        told = true;
        std::printf("  cpu%-2u %-12s now %5d MHz, held to %d to %d, can go %d to %d\n", c, governor.c_str(), now, mhzIn(cpuFile(c, "cpufreq/scaling_min_freq")),
                    mhzIn(cpuFile(c, "cpufreq/scaling_max_freq")), mhzIn(cpuFile(c, "cpufreq/cpuinfo_min_freq")), mhzIn(cpuFile(c, "cpufreq/cpuinfo_max_freq")));
    }
    if (!told) std::printf("  no clock told (nothing in /sys/devices/system/cpu/cpu*/cpufreq)\n");
    if (!all) return;
    // May this program set the governor? Opened for writing and shut again, nothing written.
    const std::string governor = cpuFile(0, "cpufreq/scaling_governor");
    const int fd = open(governor.c_str(), O_WRONLY);
    const int why = errno;
    if (fd >= 0) close(fd);
    std::printf("  %-28s %s\n", "may set the governor?", fd >= 0 ? "yes" : (std::string("no (") + std::strerror(why) + ")").c_str());
    // The caches, as cpu0 sees them.
    std::string caches;
    for (int i = 0; i < 8; ++i) {
        const std::string base = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(i) + "/";
        const std::string level = firstLine(base + "level"), size = firstLine(base + "size");
        if (level.empty() || size.empty()) continue;
        const std::string type = firstLine(base + "type"), shared = firstLine(base + "shared_cpu_list");
        if (!caches.empty()) caches += ", ";
        const bool many = shared.find_first_of(",-") != std::string::npos;
        caches += "L" + level + " " + (type == "Data" ? "data " : type == "Instruction" ? "code " : "") + size + (many ? " (shared by cpus " + shared + ")" : "");
    }
    std::printf("  %-28s %s\n", "caches", caches.empty() ? "not told (nothing in /sys/devices/system/cpu/cpu0/cache)" : caches.c_str());
}

// Every core's clock while something runs: read ten times a second on a thread of its own (an
// ordinary one, so it takes nothing from the realtime threads but a moment), lowest and highest.
struct ClockWatch {
    static const int kMost = 16;
    int low[kMost], high[kMost];
    unsigned cores = 0;
    std::atomic<bool> done{false};
    std::thread reader;
    ClockWatch() {
        cores = std::min<unsigned>(kMost, std::max(1u, std::thread::hardware_concurrency()));
        for (int c = 0; c < kMost; ++c) low[c] = 1 << 30, high[c] = -1;
        reader = std::thread([this] {
            while (!done.load()) {
                for (unsigned c = 0; c < cores; ++c) {
                    const int mhz = mhzIn(cpuFile(c, "cpufreq/scaling_cur_freq"));
                    if (mhz < 0) continue;
                    low[c] = std::min(low[c], mhz);
                    high[c] = std::max(high[c], mhz);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    ~ClockWatch() { stop(); }
    void stop() {
        done.store(true);
        if (reader.joinable()) reader.join();
    }
    // "cpu0 1008 to 1512 MHz, ..." or "" if no clock is told.
    std::string said() {
        stop();
        std::string out;
        for (unsigned c = 0; c < cores; ++c) {
            if (high[c] < 0) continue;
            char one[48];
            std::snprintf(one, sizeof one, low[c] == high[c] ? "%scpu%u %d" : "%scpu%u %d to %d", out.empty() ? "" : ", ", c, low[c], high[c]);
            out += one;
        }
        return out.empty() ? out : out + " MHz";
    }
};
static void sayClocks(ClockWatch& watch) {
    const std::string clocks = watch.said();
    if (!clocks.empty()) std::printf("  %-28s %s\n", "...the clocks meanwhile", clocks.c_str());
}

static const char* formatName(SDL_AudioFormat format) {
    switch (format) {
    case AUDIO_F32SYS: return "32-bit float";
    case AUDIO_S16SYS: return "16-bit";
    case AUDIO_S32SYS: return "32-bit";
    case AUDIO_U8: return "8-bit";
    case AUDIO_S8: return "8-bit signed";
    default: return "another format";
    }
}

std::string apotheca::probeSoundCard() {
    SDL_AudioSpec want = {}, have = {};
    want.freq = 48000; // as the program asks (main.cpp)
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = [](void*, Uint8* stream, int bytes) { std::memset(stream, 0, static_cast<std::size_t>(bytes)); };
    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
    const char* driver = SDL_GetCurrentAudioDriver();
    if (!device) return std::string("could not be opened (") + SDL_GetError() + ")";
    SDL_CloseAudioDevice(device);
    char words[200];
    const bool same = have.freq == 48000 && have.format == AUDIO_F32SYS && have.channels == 2;
    std::snprintf(words, sizeof words, "%d Hz, %s, %d channels, %d frames a buffer (%s): %s", have.freq, formatName(have.format), have.channels, have.samples,
                  driver ? driver : "no driver", same ? "what the program asks for" : "SDL converts the program's 48000 Hz float to it, on the audio thread");
    return words;
}

static void rendererRow() {
    const char* told = std::getenv(kRendererVariable);
    std::printf("  %-28s %s\n", "the screen (at start)", told && *told ? told : "not told (run by itself)");
}

static void soundCardRow() {
    const char* told = std::getenv(kSoundCardVariable);
    if (told && *told) {
        std::printf("  %-28s %s\n", "the sound card (at start)", told);
        return;
    }
    // Run by itself (`apotheca --bench`, ./build/bench): it looks for itself.
    const bool started = SDL_WasInit(SDL_INIT_AUDIO) != 0 || SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
    std::printf("  %-28s %s\n", "the sound card", started ? probeSoundCard().c_str() : (std::string("no sound (") + SDL_GetError() + ")").c_str());
    if (started) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// The busiest song with its tracks shared out other ways: three helpers, and the heaviest track
// taken first (Engine::setHeaviestFirst; the sound is the same). Real time, as the rows above.
static void sharingRows() {
    std::printf("the busiest song shared out other ways, in real time:\n");
    const struct { int helpers; bool heaviest; } ways[] = {{2, false}, {3, false}, {2, true}, {3, true}};
    const int own = helpersFor(std::thread::hardware_concurrency());
    for (const auto& way : ways) {
        if (way.helpers == own && !way.heaviest) continue; // the program's own way: the row above
        ClockWatch watch;
        const WholeSongTimes t = playWholeSong(way.helpers, 10.0, true, 0, way.heaviest);
        char name[48];
        std::snprintf(name, sizeof name, "%d helper%s%s", t.helpers, t.helpers == 1 ? "" : "s", way.heaviest ? ", heaviest first" : "");
        std::printf("  %-28s %5.1f%% of a block, 1 in 100 over %.2f ms, worst %.2f ms, %d of %d too late%s\n", name, 100.0 * t.meanMs / (256.0 / 48.0), t.p99Ms, t.worstMs, t.over, t.blocks,
                    t.realtime ? "" : " (no realtime priority)");
        sayClocks(watch);
    }
}

// A pass of the screen while the song plays: the app's update and its drawing, timed apart, on the
// SONG screen and the drum grid. Drawn by SDL's software renderer into memory (the SPEED TEST
// cannot draw on the program's own screen), so it is the drawing's work, not the handheld's
// renderer or its present. The song is the app rows' twelve tracks, played; its sound is made
// between passes, as the audio thread would, and not timed.
static void screenRows(App& app, Session& session, Engine& engine, Input& input, Uint32& clock) {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, ui::kScreenW, ui::kScreenH, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    std::unique_ptr<Fonts> fonts(new Fonts);
    if (!renderer || !fonts->load(renderer, loadFontFile(), kTextLarge)) {
        std::printf("  %-28s no renderer or no lettering here\n", "a pass while playing");
    } else {
        std::vector<float> out(512);
        const View was = session.view;
        session.togglePlay(true);
        const struct { const char* name; View view; } places[] = {{"SONG, playing", View::Song}, {"drum grid, playing", View::Pattern}};
        for (const auto& place : places) {
            session.view = place.view;
            double work = 0.0, drawing = 0.0, worst = 0.0;
            const int passes = 60;
            for (int i = -10; i < passes; ++i) { // (ten first, not timed: the screen settles)
                for (int k = 0; k < 3; ++k) engine.render(out.data(), 256); // a pass's 16 ms of sound
                engine.collectGarbage();
                const auto t0 = std::chrono::steady_clock::now();
                app.update(input, clock += 16);
                const auto t1 = std::chrono::steady_clock::now();
                app.draw(renderer, *fonts);
                const auto t2 = std::chrono::steady_clock::now();
                if (i < 0) continue;
                work += std::chrono::duration<double, std::milli>(t1 - t0).count();
                const double drew = std::chrono::duration<double, std::milli>(t2 - t1).count();
                drawing += drew;
                worst = std::max(worst, drew);
            }
            char name[48];
            std::snprintf(name, sizeof name, "a pass, %s", place.name);
            std::printf("  %-28s %7.3f ms update, %7.3f ms drawing (worst %.3f), software renderer\n", name, work / passes, drawing / passes, worst);
        }
        session.togglePlay(true);
        for (int k = 0; k < 20; ++k) engine.render(out.data(), 256);
        session.view = was;
    }
    fonts.reset(); // the letters are the renderer's textures: they go first
    if (renderer) SDL_DestroyRenderer(renderer);
    if (surface) SDL_FreeSurface(surface);
}

// ---- bench --profile NAME (docs/plans/speed-plan.md, section 0) ---------------------------------
// The rendering alone, so callgrind can be pointed at it: --toggle-collect='*profiledRender*'.
__attribute__((noinline)) static void profiledRender(Engine& engine, int blocks) {
    std::vector<float> out(512);
    for (int b = 0; b < blocks; ++b) {
        engine.render(out.data(), 256);
        if (b % 3 == 0) engine.collectGarbage(); // (a frame's worth: as the screen's thread does)
    }
}

int apotheca::runProfile(const char* name, double seconds) {
    const std::string what = name;
    std::unique_ptr<Engine> engine;
    if (what == "busiest" || what == "busiest-mods") {
        engine.reset(new Engine);
        busiestSong(*engine);
        busiestMods(*engine, what == "busiest-mods" ? 24 : 0);
        std::vector<float> out(512);
        for (int i = 0; i < 400; ++i) engine->render(out.data(), 256), engine->collectGarbage(); // every tail and bus going, as the rows
    } else if (what == "drums") {
        engine = busSongEngine(false, EffectKind::None, EffectKind::None); // twelve busy drum tracks
    } else if (what == "drums-thrown") {
        engine = busSongEngine(false, EffectKind::None, EffectKind::None, true);
    } else if (what == "chords") {
        engine = chordSongEngine(ChordPlay(), 0); // eleven silent tracks, plain chords: the step clock
    } else if (what == "phazed-silent" || what == "jaxep-silent" || what == "fm6-silent") {
        engine = macroModsEngine(what == "phazed-silent" ? SynthKind::Phazed : what == "jaxep-silent" ? SynthKind::Jx3p : SynthKind::Fm6, false);
    } else if (what == "empty") {
        engine.reset(new Engine);
    } else {
        std::printf("bench --profile NAME [SECONDS]: busiest, busiest-mods, drums, drums-thrown, chords, phazed-silent, jaxep-silent, fm6-silent, empty\n");
        return 1;
    }
    const int blocks = static_cast<int>(std::max(0.0, seconds) * 48000.0 / 256.0);
    std::printf("profiling %s: %d blocks of 256 frames\n", name, blocks);
    profiledRender(*engine, blocks);
    return 0;
}

// The SPEED TEST's rows for it: on the audio thread alone, then with the helpers the program
// uses on this machine. Ten seconds each, in real time.
static void wholeSongRows() {
    const int helpers = helpersFor(std::thread::hardware_concurrency());
    std::printf("the busiest song (12 tracks, chords, throws, 3 effects a track, buses), in real time, a block %.2f ms:\n", 256.0 / 48.0);
    for (int h : {0, helpers}) {
        ClockWatch watch;
        const WholeSongTimes t = playWholeSong(h, 10.0, true);
        char name[40];
        std::snprintf(name, sizeof name, h == 0 ? "on the audio thread alone" : "with %d helper%s", t.helpers, t.helpers == 1 ? "" : "s");
        std::printf("  %-28s %5.1f%% of a block, 1 in 100 over %.2f ms, worst %.2f ms, %d of %d too late, %.0f chunks a second%s\n", name, 100.0 * t.meanMs / (256.0 / 48.0), t.p99Ms, t.worstMs, t.over,
                    t.blocks, t.chunksPerSecond, t.realtime ? "" : " (no realtime priority)");
        sayClocks(watch);
        if (h == helpers) break; // a machine of one core has no helpers: one row
    }
}

// MOD on the busiest song: one MOD, then 24, alone and with the helpers; and how many chunks a
// second the engine cut the sound into (a tick cuts more, and each chunk wakes the helpers; the
// rows above say how many with no MOD).
static void wholeSongModRows() {
    const int helpers = helpersFor(std::thread::hardware_concurrency());
    std::printf("the busiest song with MODs running (a tick every %d samples, macros every %d; 24: two a track on\n", kModTick, kModTick * kModMacroTicks);
    std::printf("macros, effect settings, sends, LEVEL and PAN, the DEPTH lanes written):\n");
    for (int mods : {1, 24})
        for (int h : {0, helpers}) {
            const WholeSongTimes t = playWholeSong(h, mods == 24 ? 10.0 : 5.0, true, mods); // the SPEED TEST runs these too: 24 for as long as the song without
            char name[48];
            std::snprintf(name, sizeof name, "%d MOD%s, %d helper%s", mods, mods == 1 ? "" : "s", t.helpers, t.helpers == 1 ? "" : "s");
            std::printf("  %-28s %5.1f%% of a block, worst %.2f ms, %d of %d too late, %.0f chunks a second\n", name, 100.0 * t.meanMs / (256.0 / 48.0), t.worstMs, t.over, t.blocks, t.chunksPerSecond);
            if (h == helpers) break;
        }
    std::printf("working MODs out, twelve silent drum tracks, MODs on the sends:\n");
    const ModsAloneTimes times = modsAlone();
    std::printf("  %-28s %5.3f%% of one core\n", "no MOD", times.none);
    std::printf("  %-28s %5.3f%% (%+.3f%%)\n", "1 MOD", times.one, times.one - times.none);
    std::printf("  %-28s %5.3f%% (%+.3f%%)\n", "24 MODs", times.all, times.all - times.none);
    std::printf("  %-28s %5.3f%% (%+.3f%%)\n", "24 MODs on LEVEL and PAN", times.mix, times.mix - times.none);
    std::printf("  %-28s %5.3f%% (%+.3f%%)\n", "24 LFOs in time", times.synced, times.synced - times.none);
    std::printf("  %-28s %5.3f%% (%+.3f%% over in time)\n", "24 LFOs FREE", times.free, times.free - times.synced);
}

int apotheca::runBench() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // a line at a time down the pipe, so the SPEED TEST page shows each row as it comes
    processorRows("before the rows", true);
    soundCardRow();
    rendererRow();
    std::printf("engine, one track, every voice sounding:\n");
    instrument("Phazed, 8 notes", SynthKind::Phazed, 8);
    jx3pRow("JAX-EP, 6 notes", 0);
    jx3pRow("JAX-EP, 6, worst patch", 1);
    jx3pRow("JAX-EP, 6, moving cutoff", 2);
    fm6Row("FM-6, 8 notes", 10, 8, false);          // E.PIANO 1
    fm6Row("FM-6, 16, worst preset", 3, 16, true);  // STRINGS 1, keys again every half second
    instrument("Sampler (no recording)", SynthKind::Sampler, 8);
    {
        std::unique_ptr<Engine> engine(new Engine);
        Command c;
        c.type = Command::SetStep;
        c.value = 1.f;
        for (int lane = 0; lane < kDrumLanes; ++lane)
            for (int step = 0; step < kSteps; step += 2) c.a = lane, c.b = step, engine->send(c);
        c = Command();
        c.type = Command::Play;
        engine->send(c);
        std::vector<float> out(512);
        const double block = ms([&] { engine->render(out.data(), 256); }, 600);
        std::printf("  %-28s %5.1f%% of one core\n", "Drums, 8 lanes busy", 100.0 * block / (256.0 / 48.0));
    }
    {
        std::unique_ptr<Engine> engine(new Engine);
        std::vector<float> out(512);
        const double block = ms([&] { engine->render(out.data(), 256); }, 2000);
        std::printf("  %-28s %5.1f%% of one core\n", "an empty song", 100.0 * block / (256.0 / 48.0));
    }
    wholeSongRows();
    sharingRows();
    wholeSongModRows();
    {
        std::printf("a macro changed (what a MOD on a macro costs), the synth alone, 8 notes held:\n");
        std::unique_ptr<Phazed> phazed(new Phazed);
        std::unique_ptr<Fm6> fm6(new Fm6);
        std::unique_ptr<Jx3p> jaxep(new Jx3p);
        macroCost("Phazed, TONE", *phazed, phazed::Tone);
        macroCost("FM-6, BRIGHT", *fm6, fm6::MacroBright);
        macroCost("JAX-EP, macro 1", *jaxep, 0);
        std::printf("24 MODs on macros, run (twelve silent tracks of the synth, a SINE and a STEPPER each):\n");
        const struct { const char* name; SynthKind kind; } synths[] = {{"Phazed", SynthKind::Phazed}, {"FM-6", SynthKind::Fm6}, {"JAX-EP", SynthKind::Jx3p}};
        for (const auto& one : synths) {
            double none = 0.0, all = 0.0;
            macroModsRun(one.kind, none, all);
            std::printf("  %-28s %5.3f%% of one core with none, %5.3f%% with 24 (%+.3f%%)\n", one.name, none, all, all - none);
        }
    }

    // Chord effects: the clock's work, with next to no sound under it.
    std::printf("chord effects, eleven silent tracks, four chords of four keys a bar on each:\n");
    std::printf("  %-28s %5.2f%% of one core\n", "plain chords", chordSong(ChordPlay(), 0));
    std::printf("  %-28s %5.2f%% of one core\n", "strummed, humanised, x4", chordSong(chordPlayOf(kStrumWide, kOrderRandom, 100, 100, 100), 4));
    std::printf("  %-28s %5.2f%% of one core\n", "arpeggios, 1/4 step, random", chordSong(chordPlayOf(kStrumQuarter, kOrderRandom, 100, 100, 100), 0));
    std::printf("the same plain chords, two gliding notes after each; the tracks' GLIDE FROM:\n");
    for (int from = 0; from < kGlideFroms; ++from) std::printf("  %-28s %5.2f%% of one core\n", glideFromName(from), chordSong(ChordPlay(), 0, from));

    // Every effect in the table, in the order of the list: a new one is here without being added.
    std::printf("effects, one each, stereo, at their usual settings:\n");
    for (int place = 1; place < effectListSize(); ++place) {
        const EffectKind kind = effectAt(place);
        effect(effectInfo(kind).name, kind);
        if (kind == EffectKind::Filter) effect("FILTER, LOW 24 DB", kind, 2, 0.3f);
        if (kind == EffectKind::Hall) effect("HALL, TONE ABOVE 50%", kind, 2, 1.f);
        if (kind == EffectKind::Chorus) effect("CHORUS, VIBRATO or FLANGE", kind, 2, 0.9f);
        if (kind == EffectKind::Comb) effect("COMB, PITCH TURNING", kind, 0, 0.f, true);
        if (kind == EffectKind::Vowel) effect("VOWEL, VOWEL TURNING", kind, 0, 0.f, true);
        if (kind == EffectKind::Eq) effect("EQ, MID FREQ TURNING", kind, 2, 0.f, true);
        if (kind == EffectKind::Fold) effect("FOLD, DRIVE TURNING", kind, 0, 0.f, true);
        if (kind == EffectKind::Fuzz) effect("FUZZ, TONE TURNING", kind, 1, 0.f, true);
        if (kind == EffectKind::Ring) effect("RING, SQUARE", kind, 1, 0.9f);
        if (kind == EffectKind::Ring) effect("RING, FREQ TURNING", kind, 0, 0.f, true);
        if (kind == EffectKind::Wear) effect("WEAR, DIRT 100%", kind, 1, 1.f);
        if (kind == EffectKind::Wear) effect("WEAR, WOW TURNING", kind, 0, 0.f, true);
        if (kind == EffectKind::Limiter) effect("LIMITER, +20 DB", kind, 0, 1.f);
        if (kind == EffectKind::Snap) effect("SNAP, ATTACK TURNING", kind, 0, 0.f, true);
        if (kind == EffectKind::Flanger) effect("FLANGER, DEPTH TURNING", kind, 1, 0.f, true);
        if (kind == EffectKind::Ensemble) effect("ENSEMBLE, DEPTH TURNING", kind, 1, 0.f, true);
        if (kind == EffectKind::Phaser) effect("PHASER, DEPTH TURNING", kind, 1, 0.f, true);
        if (kind == EffectKind::Tremolo) effect("TREMOLO, RANDOM", kind, 2, 0.9f);
        if (kind == EffectKind::Dub) effect("DUB, HOWLING (150%)", kind, 1, 1.f);
        if (kind == EffectKind::Dub) effect("DUB, TONE TURNING", kind, 2, 0.f, true);
        if (kind == EffectKind::Reverse) effect("REVERSE, FEEDBACK 100%", kind, 1, 1.f);
        if (kind == EffectKind::Stutter) effect("STUTTER, CHANCE 100%", kind, 2, 1.f);
        if (kind == EffectKind::Bloom) effect("BLOOM, BLOOM TURNING", kind, 0, 0.f, true);
        if (kind == EffectKind::Shimmer) effect("SHIMMER, SHIMMER 0%", kind, 0, 0.f);
        if (kind == EffectKind::Shimmer) effect("SHIMMER, DECAY TURNING", kind, 1, 0.f, true);
    }

    // The send buses, as what they add to a song of twelve busy drum tracks.
    {
        const EffectKind none = EffectKind::None;
        const double dry = busSong(false, none, none), fed = busSong(true, none, none);
        const double old = busSong(true, EffectKind::Delay, EffectKind::Reverb), now = busSong(true, EffectKind::Tape, EffectKind::Hall);
        std::printf("the send buses (twelve drum tracks, %.2f%% of one core by themselves):\n", dry);
        std::printf("  %-28s %6.2f%% of one core\n", "feeding both from 12 tracks", fed - dry);
        std::printf("  %-28s %6.2f%% of one core\n", "...and TAPE and HALL on them", now - dry);
        std::printf("  %-28s %6.2f%% of one core\n", "...or DELAY and REVERB", old - dry);
        const double thrown = busSong(false, none, none, true);
        std::printf("  %-28s %6.2f%% of one core\n", "every hit thrown to both", thrown - dry);
        // Where THROW, DUCK and the buses' own effects meet: all of it at once.
        const double all = busSong(true, EffectKind::Tape, EffectKind::Hall, true, true);
        std::printf("  %-28s %6.2f%% of one core\n", "...thrown, TAPE, HALL, 12 DUCKs", all - dry);
        const double plate = busSong(true, EffectKind::Tape, EffectKind::Plate);
        std::printf("  %-28s %6.2f%% of one core\n", "...or TAPE and PLATE on them", plate - dry);
        // FEEL: every lane of all twelve kits humanised, as what it adds to the same plain song
        // (rolls for every hit, every hit asked a step ahead and waiting; the sound is the same work).
        std::printf("FEEL on every lane of the twelve drum tracks (%.2f%% plain):\n", dry);
        std::printf("  %-28s %6.2f%% of one core\n", "HUMAN TIME and LEVEL 100%", busSong(false, none, none, false, false, 100, 100) - dry);
        std::printf("  %-28s %6.2f%% of one core\n", "HUMAN TIME 100% alone", busSong(false, none, none, false, false, 100, 0) - dry);
        std::printf("  %-28s %6.2f%% of one core\n", "HUMAN LEVEL 100% alone", busSong(false, none, none, false, false, 0, 100) - dry);
        std::printf("  %-28s %6.2f%% of one core\n", "...the plain song again", busSong(false, none, none) - dry);
    }

    std::printf("app, on the screen's thread (a frame is 16.7 ms):\n");
    std::unique_ptr<Engine> engine(new Engine);
    Input input;
    App app(*engine);
    Session& session = app.session();
    // A full song: twelve tracks, eight scenes, notes and hits in every cell, some automation.
    const TrackKind kinds[4] = {TrackKind::Phazed, TrackKind::Jx3p, TrackKind::Fm6, TrackKind::Drums};
    for (int t = 1; t < kNumTracks; ++t) session.addTrack(kinds[t % 4]);
    Project& p = app.project();
    for (int t = 0; t < kNumTracks; ++t)
        for (int scene = 0; scene < 8; ++scene) {
            for (int step = 0; step < kSteps; step += 2) p.tracks[t].drums[scene][0].level[step % kDrumLanes][step] = 0.8f;
            for (int n = 0; n < 12; ++n) p.tracks[t].notes[scene][0].notes[n] = NoteEvent{n, 48 + n, 1.f, 0.f, 0.8f};
            for (int i = 0; i < kSteps; ++i) p.tracks[t].automation[scene][t % 8][i] = 0.5f;
        }
    std::vector<float> out(512);
    const auto drain = [&] { engine->render(out.data(), 64); };
    std::string text;
    const double writing = ms([&] { text = songText(p); }, 20);
    std::printf("  %-28s %7.3f ms  (%zu KB)\n", "the song as text", writing, text.size() / 1024);
    std::unique_ptr<Project> again(new Project);
    std::printf("  %-28s %7.3f ms\n", "text back into a song", ms([&] { parseSong(text, *again); }, 20));
    Uint32 clock = 1000;
    session.view = View::Pattern;
    std::printf("  %-28s %7.3f ms\n", "one frame, nothing changing", ms([&] { app.update(input, clock += 16); drain(); }, 300));
    std::printf("  %-28s %7.3f ms\n", "a frame that ends an edit", ms([&] { session.noteEdit(); app.update(input, clock += 800); drain(); }, 20));
    {
        // Only the two calls are timed: the frames and the audio between them are not undo's cost.
        double spent = 0.0;
        const int rounds = 10;
        for (int i = 0; i < rounds; ++i) {
            session.noteEdit();
            app.update(input, clock += 800);
            spent += ms([&] { session.undo(); session.redo(); }, 1);
            for (int k = 0; k < 300; ++k) drain();
        }
        std::printf("  %-28s %7.3f ms\n", "undo, then redo", spent / rounds);
    }
    {
        // A knob turned on one track (an effect's setting) and a hit on another, then undo and redo.
        double spent = 0.0;
        const int rounds = 10;
        session.setEffect(3, 0, EffectKind::Drive);
        app.update(input, clock += 800);
        for (int i = 0; i < rounds; ++i) {
            session.setEffectParam(3, 0, 0, (i % 2) ? 0.25f : 0.75f);
            p.tracks[4].drums[0][0].level[1][3] = (i % 2) ? 0.f : 0.9f;
            session.noteEdit();
            app.update(input, clock += 800);
            spent += ms([&] { session.undo(); session.redo(); }, 1);
            for (int k = 0; k < 300; ++k) drain();
        }
        std::printf("  %-28s %7.3f ms\n", "...a knob and a hit, undone", spent / rounds);
    }
    std::printf("  %-28s %7.3f ms\n", "a bar of notes to the engine", ms([&] { session.sendNoteBar(1, 0, 0); drain(); }, 200));
    screenRows(app, session, *engine, input, clock);
    processorRows("after the rows", false);
    std::printf("sizes: Project %zu KB, TrackData %zu KB, Engine %zu KB, a Command %zu bytes\n", sizeof(Project) / 1024, sizeof(TrackData) / 1024, sizeof(Engine) / 1024, sizeof(Command));
    return 0;
}
