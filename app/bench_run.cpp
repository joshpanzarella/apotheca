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

#include <algorithm>
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
#include "apotheca/twin.hpp"
#include "apotheca/jx3p.hpp"
#include "app.hpp"
#include "bench_run.hpp"
#include "realtime.hpp"
#include "song_file.hpp"

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
//   0  the patch as a player meets it (batch 7: A1 String I, chorus on; until then the start patch)
//   1  the worst patch (batch 7: B12 Sync Sweep; until then the start patch with its envelope
//      moving all the while: a slow attack, a decay, each key played again every half second)
//   2  a moving cutoff: every note a glide all the while (and, from batch 4, the LFO on the cutoff),
//      so whatever is worked out from the pitch is worked out every sample
static void jx3pRow(const char* name, int how) {
    std::unique_ptr<Jx3p> synth(new Jx3p);
    synth->prepare(48000.f);
    if (how == 1) {
        synth->setSetting(jx3p::Attack, 40);
        synth->setSetting(jx3p::Decay, 60);
        synth->setSetting(jx3p::Sustain, 30);
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
// a slot of every track as well. The best of five runs of ten seconds.
static double busSong(bool sends, EffectKind echo, EffectKind space, bool throws = false, bool ducks = false) {
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
static double chordSong(ChordPlay play, int ratchet, int from = -1) {
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
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        const double block = ms([&] { engine->render(out.data(), 256); }, 1875);
        best = block < best ? block : best;
    }
    return 100.0 * best / (256.0 / 48.0);
}

// THE BUSIEST SONG (the pit stop audit's, 2026-11-14): 140 BPM, twelve tracks. Six drum
// tracks, every lane on every other step, ratchets of four on three lanes, every hit thrown to
// both buses. Two Twin Analog (x2), two QMOD and two Phazed, each four chords of four keys a
// bar, half WIDE STRUM and half arpeggios at a quarter step, RANDOM, human at 100%, thrown;
// a ratchet of two on the strummed ones. COMP, then SATURATE or CHORUS, then HALL or PLATE on
// every track; TAPE and PLATE on the buses, every track sending 0.4 to each; COMP and SATURATE
// on the master. Flush-to-zero on, as in the program. Twin stays at full quality: the
// program would make it plainer if it fell behind (watchLoad), and this is to see whether
// it has to.
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
    const SynthKind kinds[6] = {SynthKind::Twin, SynthKind::Twin, SynthKind::Qmod, SynthKind::Qmod, SynthKind::Phazed, SynthKind::Phazed};
    for (int i = 0; i < 6; ++i) e.addSynthTrack(6 + i, kinds[i]);
    settle(4);
    for (int i = 0; i < 6; ++i)
        if (kinds[i] == SynthKind::Twin) {
            c = Command();
            c.type = Command::SetSetting;
            c.track = 6 + i;
            c.a = twin::Oversample;
            c.b = 1; // x2
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

// The busiest song in real time: a block is worked out, then the thread sleeps until the next
// is due, as the sound card's thread does. On its own thread, which asks for realtime priority
// as the program's audio thread does (`realtime`), and the helpers as the program's do.
static WholeSongTimes playWholeSong(int helpers, double seconds, bool realtime) {
    WholeSongTimes times;
    std::thread player([&] {
        if (realtime) times.realtime = makeThreadRealtime(70);
        std::unique_ptr<Engine> engine(new Engine);
        busiestSong(*engine);
        std::vector<float> out(512);
        for (int i = 0; i < 400; ++i) { // two seconds, so every tail and bus is going
            engine->render(out.data(), 256);
            engine->collectGarbage();
        }
        if (helpers > 0) engine->useWorkers(helpers, realtime ? static_cast<void (*)()>([] { makeThreadRealtime(69); }) : nullptr);
        times.helpers = engine->workers();
        const double blockMs = 256.0 / 48.0;
        const int blocks = static_cast<int>(seconds * 48000.0 / 256.0);
        std::vector<double> took(static_cast<std::size_t>(blocks > 0 ? blocks : 0));
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

WholeSongTimes apotheca::timeWholeSong(int helpers, double seconds) { return playWholeSong(helpers, seconds, false); }

// The SPEED TEST's rows for it: on the audio thread alone, then with the helpers the program
// uses on this machine. Ten seconds each, in real time.
static void wholeSongRows() {
    const int helpers = helpersFor(std::thread::hardware_concurrency());
    std::printf("the busiest song (12 tracks, chords, throws, 3 effects a track, buses), in real time, a block %.2f ms:\n", 256.0 / 48.0);
    for (int h : {0, helpers}) {
        const WholeSongTimes t = playWholeSong(h, 10.0, true);
        char name[40];
        std::snprintf(name, sizeof name, h == 0 ? "on the audio thread alone" : "with %d helper%s", t.helpers, t.helpers == 1 ? "" : "s");
        std::printf("  %-28s %5.1f%% of a block, 1 in 100 over %.2f ms, worst %.2f ms, %d of %d too late%s\n", name, 100.0 * t.meanMs / (256.0 / 48.0), t.p99Ms, t.worstMs, t.over, t.blocks,
                    t.realtime ? "" : " (no realtime priority)");
        if (h == helpers) break; // a machine of one core has no helpers: one row
    }
}

int apotheca::runBench() {
    std::printf("engine, one track, every voice sounding:\n");
    instrument("Phazed, 8 notes", SynthKind::Phazed, 8);
    instrument("Twin Analog x1, 8 notes", SynthKind::Twin, 8, twin::Oversample, 0);
    instrument("Twin Analog x2, 8 notes", SynthKind::Twin, 8, twin::Oversample, 1);
    instrument("Twin Analog x8, 8 notes", SynthKind::Twin, 8, twin::Oversample, 3);
    instrument("QMOD, 8 notes", SynthKind::Qmod, 8);
    jx3pRow("JX-3P, 6 notes", 0);
    jx3pRow("JX-3P, 6, worst patch", 1);
    jx3pRow("JX-3P, 6, moving cutoff", 2);
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
    }

    std::printf("app, on the screen's thread (a frame is 16.7 ms):\n");
    std::unique_ptr<Engine> engine(new Engine);
    Input input;
    App app(*engine);
    Session& session = app.session();
    // A full song: twelve tracks, eight scenes, notes and hits in every cell, some automation.
    const TrackKind kinds[4] = {TrackKind::Phazed, TrackKind::Twin, TrackKind::Qmod, TrackKind::Drums};
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
    std::printf("sizes: Project %zu KB, TrackData %zu KB, Engine %zu KB, a Command %zu bytes\n", sizeof(Project) / 1024, sizeof(TrackData) / 1024, sizeof(Engine) / 1024, sizeof(Command));
    return 0;
}
