// STEREO (2026-11-28): a recording plays as its file is. A stereo WAV is read as two sides (Clip) and played so
// by a drum lane, the sampler (its zones too), CLOUD and every way a hit or a note is shaped; a mono file is read
// and played exactly as before. EDIT's MONO mixes a stereo sound's sides to one (saved in the song, undone like any
// edit). CATCH keeps both sides; a bounce into samples too; SAVE A COPY keeps what the lane plays. FIT stretches
// both sides together.
//
// stereo_test NAME... runs just those.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/cloud.hpp"
#include "apotheca/drums.hpp"
#include "apotheca/engine.hpp"
#include "apotheca/sampler.hpp"
#include "app.hpp"
#include "lane_edit.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "stretch.hpp"
#include "temp_folder.hpp"

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

const SDL_Keycode A = SDLK_x, B = SDLK_z, Up = SDLK_UP, Down = SDLK_DOWN, Right = SDLK_RIGHT;
constexpr int kStep = 6000; // 120 BPM at 48 kHz
constexpr int kLane = 2;

// A sound with something in every sample, under full scale; `seed` makes another.
std::vector<float> made(int n, int seed = 0) {
    std::vector<float> s(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        s[static_cast<std::size_t>(i)] = 0.5f * std::sin((0.031f + 0.007f * static_cast<float>(seed)) * static_cast<float>(i)) + 0.3f * (static_cast<float>((i + 13 * seed) % 97) / 97.f - 0.5f);
    return s;
}

float peakOf(const std::vector<float>& s, std::size_t from = 0, std::size_t step = 1) {
    float p = 0.f;
    for (std::size_t i = from; i < s.size(); i += step) p = std::fabs(s[i]) > p ? std::fabs(s[i]) : p;
    return p;
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    explicit Rig(const std::string& folder = "") {
        if (!folder.empty()) app.session().useSampleFolder(folder);
        frames(2);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) {
            key(code, true);
            frames(1);
            key(code, false);
            frames(1);
        }
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256);
            engine->render(out.data(), 256);
        }
    }
    std::vector<float> render(int n) {
        std::vector<float> out(2 * static_cast<std::size_t>(n));
        engine->render(out.data(), n);
        return out;
    }
    // Quiet first, then one hit on a lane of track 0: `n` frames of it, interleaved.
    std::vector<float> hit(int lane, int n) {
        for (int i = 0; i < 400; ++i) render(256);
        Command c;
        c.type = Command::Trigger;
        c.track = 0;
        c.a = lane;
        c.value = 0.9f;
        engine->send(c);
        return render(n);
    }
    Session& session() { return app.session(); }
    TrackData& kit() { return app.project().tracks[0]; }
};

// Interleaved into its two sides.
void sides(const std::vector<float>& out, std::vector<float>& l, std::vector<float>& r) {
    l.clear(), r.clear();
    for (std::size_t i = 0; i + 1 < out.size(); i += 2) l.push_back(out[i]), r.push_back(out[i + 1]);
}

// A drum machine with the built-in kit, one lane on these samples (`right` nullptr: mono), one hit, `n` frames.
void drumHit(const std::vector<float>& left, const std::vector<float>* right, const HitPlay& how, int n, std::vector<float>& l, std::vector<float>& r,
             std::vector<float>* echo = nullptr, int block = 64) {
    std::unique_ptr<DrumMachine> drums(new DrumMachine);
    drums->setKit(&DrumKit::builtIn(48000.f));
    drums->prepare(48000.f);
    drums->setLaneSample(1, left.data(), static_cast<int>(left.size()), right ? right->data() : nullptr);
    drums->trigger(1, 0.8f, 0.f, -1.f, how);
    l.assign(static_cast<std::size_t>(n), 0.f), r.assign(static_cast<std::size_t>(n), 0.f);
    if (echo) echo->assign(static_cast<std::size_t>(n), 0.f);
    std::vector<float> e(static_cast<std::size_t>(block)), s(static_cast<std::size_t>(block));
    for (int at = 0; at < n; at += block) {
        const int m = n - at < block ? n - at : block;
        const int wrote = drums->render(l.data() + at, r.data() + at, m, e.data(), s.data());
        if (echo && (wrote & 1))
            for (int i = 0; i < m; ++i) (*echo)[static_cast<std::size_t>(at + i)] = e[static_cast<std::size_t>(i)];
    }
}

} // namespace

// ---- the file --------------------------------------------------------------------------------------------

void testReadingAndWriting() {
    const std::string folder = tempFolder("stereo");
    const std::vector<float> left = made(9600, 1), right = made(9600, 2);
    CHECK(writeWav(folder + "/two.wav", Clip(left, right), 48000));
    CHECK(writeWav(folder + "/one.wav", left, 48000));
    std::string bytes;
    CHECK(readTextFile(folder + "/two.wav", bytes) && wavChannels(bytes) == 2 && bytes.size() == 44u + 4u * 9600u);
    CHECK(readTextFile(folder + "/one.wav", bytes) && wavChannels(bytes) == 1 && bytes.size() == 44u + 2u * 9600u);
    // Two sides kept apart (to 16 bits), and as one channel what a stereo file always read as: both mixed.
    Clip two;
    CHECK(readWav(folder + "/two.wav", two) && two.stereo() && two.size() == 9600u);
    float off = 0.f;
    for (std::size_t i = 0; i < two.size() && two.stereo(); ++i) off = std::fmax(off, std::fmax(std::fabs(two[i] - left[i]), std::fabs(two.right[i] - right[i])));
    CHECK(off < 1e-4f);
    std::vector<float> mixed;
    CHECK(readWav(folder + "/two.wav", mixed) && mixed == two.mono());
    // A mono file: no right side, and the same samples to the bit as the one-channel read.
    Clip one;
    std::vector<float> plain;
    CHECK(readWav(folder + "/one.wav", one) && !one.stereo() && one.right.empty());
    CHECK(readWav(folder + "/one.wav", plain) && plain == one.left());
    // A mono Clip writes the bytes a vector does; a stereo one is interleaved, left first.
    CHECK(wavBytes(Clip(left), 48000) == wavBytes(left, 48000));
    const std::string interleaved = wavBytes(Clip(std::vector<float>{0.5f}, std::vector<float>{-0.25f}), 48000);
    CHECK(interleaved.size() == 48u && static_cast<std::int16_t>(static_cast<std::uint8_t>(interleaved[44]) | static_cast<std::uint8_t>(interleaved[45]) << 8) == 16384 &&
          static_cast<std::int16_t>(static_cast<std::uint8_t>(interleaved[46]) | static_cast<std::uint8_t>(interleaved[47]) << 8) == -8192);
    // At 44.1 kHz both sides go to 48 kHz alike (the same length), and a 20 s cut fades both.
    CHECK(writeWav(folder + "/slow.wav", Clip(made(4410, 1), std::vector<float>(4410, 0.f)), 44100));
    Clip slow;
    CHECK(readWav(folder + "/slow.wav", slow) && slow.stereo() && slow.size() == 4800u && peakOf(slow.right) == 0.f && peakOf(slow) > 0.4f);
    const std::vector<float> longer(48000 * 21, 0.5f);
    CHECK(writeWav(folder + "/long.wav", Clip(longer, longer), 48000));
    Clip cut;
    CHECK(readWav(folder + "/long.wav", cut) && cut.size() == 960000u && cut.stereo() && cut.back() == 0.f && cut.right.back() == 0.f && cut.right == cut.left());
}

// ---- the engine: a drum lane, the sampler, CLOUD -------------------------------------------------------------------

void testADrumLaneInStereo() {
    const std::vector<float> sound = made(6000), silent(6000, 0.f);
    std::vector<float> l, r, lm, rm, echo, echoM;
    // Each side to its own: the left alone, then the right alone.
    drumHit(sound, &silent, HitPlay(), 6000, l, r);
    CHECK(peakOf(l) > 0.3f && peakOf(r) == 0.f);
    drumHit(silent, &sound, HitPlay(), 6000, l, r);
    CHECK(peakOf(r) > 0.3f && peakOf(l) == 0.f);
    // Two sides the same play to the bit as the mono sound does, however it is played: plain, pitched, backwards,
    // cut, thrown (and the THROW is both sides mixed to one).
    HitPlay ways[4];
    ways[1].rate = 1.5f;
    ways[2].reverse = true, ways[2].rate = 0.75f;
    ways[3].life = 1500, ways[3].throwTo = kThrowEcho;
    for (const HitPlay& how : ways) {
        drumHit(sound, &sound, how, 9000, l, r, &echo);
        drumHit(sound, nullptr, how, 9000, lm, rm, &echoM);
        CHECK(l == lm && r == rm && echo == echoM && peakOf(l) > 0.1f);
    }
    drumHit(sound, &silent, ways[3], 9000, l, r, &echo);
    drumHit(sound, nullptr, ways[3], 9000, lm, rm, &echoM);
    float off = 0.f;
    for (std::size_t i = 0; i < echo.size(); ++i) off = std::fmax(off, std::fabs(echo[i] - 0.5f * echoM[i]));
    CHECK(peakOf(echoM) > 0.1f && off < 1e-6f);
    // In any blocks the same.
    drumHit(made(6000), &silent, ways[1], 9000, lm, rm, nullptr, 64);
    drumHit(made(6000), &silent, ways[1], 9000, l, r, nullptr, 7);
    CHECK(l == lm && r == rm);
}

void testTheSamplerAndCloudInStereo() {
    const std::vector<float> sound = made(48000), silent(48000, 0.f);
    const auto play = [&](Instrument& synth, const std::vector<float>& left, const std::vector<float>* right, std::vector<float>& l, std::vector<float>& r) {
        synth.prepare(48000.f);
        synth.setSample(left.data(), static_cast<int>(left.size()), right ? right->data() : nullptr);
        synth.noteOn(60, 0.9f);
        synth.noteOn(67, 0.7f);
        l.assign(24000, 0.f), r.assign(24000, 0.f);
        for (int at = 0; at < 24000; at += 128) synth.render(l.data() + at, r.data() + at, 24000 - at < 128 ? 24000 - at : 128);
    };
    std::vector<float> l, r, lm, rm;
    {
        Sampler a, b, c;
        play(a, sound, &silent, l, r);
        CHECK(peakOf(l) > 0.2f && peakOf(r) == 0.f);
        play(b, sound, &sound, l, r);
        play(c, sound, nullptr, lm, rm);
        CHECK(l == lm && r == rm);
        // LOOP on, END short: the blend and the end both sides alike.
        Sampler d, e;
        for (Sampler* s : {&d, &e}) s->setSetting(sampler::Loop, 1), s->setSetting(sampler::Start, 10), s->setSetting(sampler::End, 12);
        play(d, sound, &sound, l, r);
        play(e, sound, nullptr, lm, rm);
        CHECK(l == lm && r == rm && peakOf(l) > 0.1f);
        // END at 5%: each note in its END fade, both sides alike.
        Sampler f, g;
        for (Sampler* x : {&f, &g}) x->setSetting(sampler::End, 5);
        play(f, sound, &sound, l, r);
        play(g, sound, nullptr, lm, rm);
        CHECK(l == lm && r == rm && peakOf(l) > 0.1f);
    }
    {
        // A multisample's zone with a right side: that note plays in stereo, the mono zone as it was.
        sampler::SampleZoneSet set;
        set.zones.resize(2);
        set.zones[0].data = sound, set.zones[0].right = silent, set.zones[0].root = 48, set.zones[0].hi = 59;
        set.zones[1].data = sound, set.zones[1].root = 72, set.zones[1].lo = 60;
        Sampler s;
        s.prepare(48000.f);
        s.setZones(&set);
        std::vector<float> zl(4096), zr(4096);
        s.noteOn(50, 0.9f);
        s.render(zl.data(), zr.data(), 4096);
        CHECK(peakOf(zl) > 0.2f && peakOf(zr) == 0.f);
        s.allNotesOff();
        Sampler t;
        t.prepare(48000.f);
        t.setZones(&set);
        t.noteOn(70, 0.9f);
        t.render(zl.data(), zr.data(), 4096);
        CHECK(peakOf(zl) > 0.2f && zl == zr);
    }
    {
        // CLOUD: each grain reads both sides at one place; its pan then weighs them.
        std::unique_ptr<Cloud> a(new Cloud), b(new Cloud), c(new Cloud);
        play(*a, sound, &silent, l, r);
        CHECK(peakOf(l) > 0.05f && peakOf(r) == 0.f);
        play(*b, sound, &sound, l, r);
        play(*c, sound, nullptr, lm, rm);
        CHECK(l == lm && r == rm);
        // PITCH SPRAY: every grain read between samples (Hermite), alike.
        std::unique_ptr<Cloud> d(new Cloud), e(new Cloud);
        for (Cloud* x : {d.get(), e.get()}) x->setSetting(cloud::PitchSpray, 40), x->setSetting(cloud::Density, 100);
        play(*d, sound, &sound, l, r);
        play(*e, sound, nullptr, lm, rm);
        CHECK(l == lm && r == rm && peakOf(l) > 0.05f);
        // A recording shorter than a grain: every grain reads round its ends, a sample at a time.
        const std::vector<float> tiny = made(300), tinySilent(300, 0.f);
        std::unique_ptr<Cloud> f(new Cloud);
        play(*f, tiny, &tinySilent, l, r);
        CHECK(peakOf(l) > 0.05f && peakOf(r) == 0.f);
        // ...grains at its own pitch (whole samples: no PITCH SPRAY, middle C), and the shortest (read straight).
        std::unique_ptr<Cloud> plain(new Cloud), straight(new Cloud);
        plain->setSetting(cloud::PitchSpray, 0), plain->setSetting(cloud::Tune, 0), plain->setSetting(cloud::Reverse, 50);
        straight->setSetting(cloud::Size, 0), straight->setSetting(cloud::PitchSpray, 30);
        play(*plain, tiny, &tinySilent, l, r);
        CHECK(peakOf(l) > 0.05f && peakOf(r) == 0.f);
        play(*straight, tiny, &tinySilent, l, r);
        CHECK(peakOf(l) > 0.05f && peakOf(r) == 0.f);
    }
}

// ---- the app: a lane, MONO, the song file, undo, SAVE A COPY ---------------------------------------------------------

void testALaneAndItsMono() {
    const std::string folder = tempFolder("stereo");
    const std::vector<float> left = made(24000, 1), quiet(24000, 0.f);
    CHECK(writeWav(folder + "/wide.wav", Clip(left, quiet), 48000)); // the left side alone
    CHECK(writeWav(folder + "/one.wav", left, 48000));
    Rig rig(folder);
    Session& session = rig.session();
    session.setLaneSample(0, kLane, "wide");
    rig.frames(60); // (a step of undo of its own)
    CHECK(session.laneSound(0, kLane).source && session.laneSound(0, kLane).source->stereo());
    std::vector<float> l, r;
    sides(rig.hit(kLane, 12000), l, r);
    CHECK(peakOf(l) > 0.2f && peakOf(r) == 0.f);
    // MONO: both sides mixed to one, heard on both.
    LaneEdit e = rig.kit().laneEdit[kLane];
    e.mono = 1;
    session.setLaneEdit(0, kLane, e);
    rig.frames(60);
    CHECK(rig.kit().laneEdit[kLane].mono == 1 && !session.laneSound(0, kLane).shaped->stereo());
    sides(rig.hit(kLane, 12000), l, r);
    CHECK(peakOf(l) > 0.1f && l == r);
    float half = 0.f; // ...at half the left's level (its right was silence), and with no guard on it
    std::vector<float> lw, rw;
    {
        Rig plain(folder);
        plain.session().setLaneSample(0, kLane, "wide");
        plain.frames(4);
        sides(plain.hit(kLane, 12000), lw, rw);
    }
    for (std::size_t i = 0; i < l.size(); ++i) half = std::fmax(half, std::fabs(l[i] - 0.5f * lw[i]));
    CHECK(half < 1e-6f);
    // The song file: MONO is its laneedit's tenth number, and comes back.
    const std::string text = songText(rig.app.project());
    CHECK(text.find("laneedit 3 0 1 0 0 0 0 0 0 1\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && back->tracks[0].laneEdit[kLane].mono == 1 && songText(*back) == text);
    // An old line (nine numbers) has it off; a line with every edit at rest is not written at all.
    std::unique_ptr<Project> old(new Project);
    CHECK(parseSong("apotheca-song 1\ntempo 120\ntrack drums\nlaneedit 3 0.25 1 0 0 0 0 0 0\n", *old) && old->tracks[0].laneEdit[kLane].mono == 0 && old->tracks[0].laneEdit[kLane].start == 25000);
    CHECK(songText(*old).find("laneedit 3 0.25 1 0 0 0 0 0 0\n") != std::string::npos);
    // Undo puts the two sides back.
    CHECK(session.undo());
    rig.frames(4);
    CHECK(rig.kit().laneEdit[kLane].mono == 0 && session.laneSound(0, kLane).shaped->stereo());
    sides(rig.hit(kLane, 12000), l, r);
    CHECK(peakOf(r) == 0.f && l == lw);
    // SAVE A COPY keeps what the lane plays: two sides, then (MONO) one.
    std::string name;
    CHECK(session.saveLaneSound(0, kLane, 0.f, 1.f, name) == Session::SaveSound::Saved);
    std::string bytes;
    CHECK(readTextFile(folder + "/" + name + ".wav", bytes) && wavChannels(bytes) == 2);
    session.setLaneEdit(0, kLane, e);
    rig.frames(4);
    CHECK(session.saveLaneSound(0, kLane, 0.f, 0.5f, name) == Session::SaveSound::Saved);
    CHECK(readTextFile(folder + "/" + name + ".wav", bytes) && wavChannels(bytes) == 1);
    // A slice saved of a stereo sound: both sides end on the 1 ms fade.
    CHECK(writeWav(folder + "/both.wav", Clip(made(24000, 1), made(24000, 2)), 48000));
    session.setLaneSample(0, kLane + 4, "both");
    rig.frames(4);
    CHECK(session.saveLaneSound(0, kLane + 4, 0.25f, 0.5f, name) == Session::SaveSound::Saved);
    Clip slice;
    CHECK(readWav(folder + "/" + name + ".wav", slice) && slice.stereo() && slice.size() == 12000u && slice.back() == 0.f && slice.right.back() == 0.f &&
          std::fabs(slice.right[slice.size() - 24]) < std::fabs(made(24000, 2)[6000 + 12000 - 24]) * 0.6f + 1e-3f);
    // NORMALIZE and TRIM SILENCE hear both sides: a right side louder than the left sets GAIN.
    std::vector<float> soft(24000, 0.f), loud(24000, 0.f);
    for (int i = 6000; i < 18000; ++i) soft[static_cast<std::size_t>(i)] = 0.1f, loud[static_cast<std::size_t>(i - 3000)] = 0.5f;
    CHECK(writeWav(folder + "/lopsided.wav", Clip(soft, loud), 48000));
    session.setLaneSample(0, kLane + 1, "lopsided");
    rig.frames(4);
    CHECK(session.normalizeLane(0, kLane + 1) && std::abs(rig.kit().laneEdit[kLane + 1].gain - 50) <= 1); // 0.5 to -1 dB: +5 dB
    CHECK(session.trimLaneSilence(0, kLane + 1) && rig.kit().laneEdit[kLane + 1].start < 3000 * kEditUnits / 24000);
    // A cut and GAIN on a stereo sound: each side its own (the silent right stays silent).
    LaneEdit cut;
    cut.start = 25000, cut.gain = 30;
    session.setLaneEdit(0, kLane, cut);
    rig.frames(4);
    sides(rig.hit(kLane, 12000), l, r);
    CHECK(peakOf(l) > 0.2f && peakOf(r) == 0.f && session.laneSound(0, kLane).shaped->stereo());
    session.setLaneEdit(0, kLane, LaneEdit());
    // MONO alone puts no guard on a loud sound: both sides the same at 0.95 play as the file does, to the bit.
    std::vector<float> loudSine(24000);
    for (int i = 0; i < 24000; ++i) loudSine[static_cast<std::size_t>(i)] = 0.95f * std::sin(0.02f * static_cast<float>(i));
    CHECK(writeWav(folder + "/loud.wav", Clip(loudSine, loudSine), 48000));
    session.setLaneSample(0, 0, "loud");
    rig.frames(4);
    std::vector<float> ls, rs;
    sides(rig.hit(0, 12000), ls, rs);
    LaneEdit monoOnly;
    monoOnly.mono = 1;
    session.setLaneEdit(0, 0, monoOnly);
    rig.frames(4);
    sides(rig.hit(0, 12000), l, r);
    CHECK(peakOf(ls) > 0.4f && l == ls && r == rs);
    CHECK(laneEditHeld(monoOnly, 1.f).mono == 1 && laneEditHeld(LaneEdit(laneEditWith(LaneEdit(), kEditMono, 7)), 1.f).mono == 1 && monoOnly.levelAtRest() && !monoOnly.atRest());
    // The picture is the louder side's (a sound on its right alone is drawn), and SLICES AT THE HITS finds hits on
    // both sides mixed to one (here on the right alone).
    std::vector<float> beats(48000, 0.f);
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < 2000; ++i) beats[static_cast<std::size_t>(b * 12000 + i)] = 0.8f * std::exp(-static_cast<float>(i) / 300.f) * (i % 2 ? 1.f : -1.f);
    CHECK(writeWav(folder + "/right.wav", Clip(std::vector<float>(48000, 0.f), beats), 48000));
    session.setLaneSample(0, kLane + 3, "right");
    rig.frames(4);
    CHECK(peakOf(session.laneSound(0, kLane + 3).sourcePeaks) > 0.5f && peakOf(session.laneSound(0, kLane + 3).peaks) > 0.5f);
    CHECK(session.cutLaneAtHits(0, kLane + 3, kSenseMost) == Session::Cut::Done && rig.kit().laneCuts[kLane + 3].count == 4);
    // A mono file has no MONO to speak of: its picture, its sound and its song as they were.
    session.setLaneSample(0, kLane + 2, "one");
    rig.frames(4);
    CHECK(!session.laneSound(0, kLane + 2).source->stereo());
    sides(rig.hit(kLane + 2, 12000), l, r);
    CHECK(l == r && peakOf(l) > 0.2f);
}

void testTheSamplerMono() {
    const std::string folder = tempFolder("stereo");
    CHECK(writeWav(folder + "/wide.wav", Clip(made(48000, 1), std::vector<float>(48000, 0.f)), 48000));
    Rig rig(folder);
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Sampler));
    session.setTrackSample(1, "wide");
    rig.frames(4);
    const auto note = [&] {
        for (int i = 0; i < 400; ++i) rig.render(256);
        Command c;
        c.type = Command::NoteOn;
        c.track = 1;
        c.a = 60;
        c.value = 0.9f;
        rig.engine->send(c);
        std::vector<float> l, r;
        sides(rig.render(9600), l, r);
        Command off;
        off.type = Command::NoteOff;
        off.track = 1;
        off.a = 60;
        rig.engine->send(off);
        return std::make_pair(l, r);
    };
    std::pair<std::vector<float>, std::vector<float>> heard = note();
    CHECK(peakOf(heard.first) > 0.1f && peakOf(heard.second) == 0.f);
    LaneEdit e = rig.app.project().tracks[1].samplerEdit;
    e.mono = 1;
    session.setSamplerEdit(1, e);
    rig.frames(4);
    heard = note();
    CHECK(peakOf(heard.first) > 0.05f && heard.first == heard.second);
    const std::string text = songText(rig.app.project());
    CHECK(text.find("sampleredit 0 1 0 0 0 0 1\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && back->tracks[1].samplerEdit.mono == 1);
    // MONO alone puts no guard on it: both sides the same at 0.95 play as the file does, to the bit.
    std::vector<float> loudSine(48000);
    for (int i = 0; i < 48000; ++i) loudSine[static_cast<std::size_t>(i)] = 0.95f * std::sin(0.02f * static_cast<float>(i));
    CHECK(writeWav(folder + "/loud.wav", Clip(loudSine, loudSine), 48000));
    session.setTrackSample(1, "loud");
    session.setSamplerEdit(1, LaneEdit()); // (MONO stays across another recording, as the rest of EDIT does)
    rig.frames(4);
    const std::pair<std::vector<float>, std::vector<float>> plain = note();
    session.setSamplerEdit(1, e);
    rig.frames(4);
    heard = note();
    CHECK(peakOf(plain.first) > 0.2f && heard == plain);
    // GAIN on a stereo one: each side its own (the silent right stays silent).
    session.setTrackSample(1, "wide");
    LaneEdit gain;
    gain.gain = 30;
    session.setSamplerEdit(1, gain);
    rig.frames(4);
    heard = note();
    CHECK(peakOf(heard.first) > 0.1f && peakOf(heard.second) == 0.f);
    session.setSamplerEdit(1, e);
    rig.frames(4);
    // A file of one note beside one that will not read: the one played as one recording, its two sides kept.
    CHECK(writeWav(folder + "/solo C3.wav", Clip(made(48000, 1), std::vector<float>(48000, 0.f)), 48000));
    CHECK(writeTextFile(folder + "/solo C4.wav", "not a wav"));
    session.setTrackSample(1, "solo C3");
    session.setSamplerEdit(1, LaneEdit());
    rig.frames(4);
    CHECK(session.trackSound(1).zones == 0 && session.trackSound(1).source && session.trackSound(1).source->stereo());
    session.setTrackSample(1, "wide");
    session.setSamplerEdit(1, e);
    rig.frames(4);
    // A multisample: each note as its file is (C3 stereo, its right silent; C4 mono).
    CHECK(writeWav(folder + "/pad C3.wav", Clip(made(48000, 1), std::vector<float>(48000, 0.f)), 48000));
    CHECK(writeWav(folder + "/pad C4.wav", made(48000, 2), 48000));
    CHECK(session.addTrack(TrackKind::Sampler));
    session.setTrackSample(2, "pad C3");
    rig.frames(4);
    CHECK(session.trackSound(2).zones == 2);
    for (int i = 0; i < 400; ++i) rig.render(256);
    Command c;
    c.type = Command::NoteOn;
    c.track = 2;
    c.a = 48;
    c.value = 0.9f;
    rig.engine->send(c);
    std::vector<float> l, r;
    sides(rig.render(9600), l, r);
    CHECK(peakOf(l) > 0.1f && peakOf(r) == 0.f);
    c.type = Command::NoteOff;
    rig.engine->send(c);
    // CLOUD: the recording as its file is.
    CHECK(session.addTrack(TrackKind::Cloud));
    session.setTrackSample(3, "wide");
    rig.frames(4);
    for (int i = 0; i < 400; ++i) rig.render(256);
    c.type = Command::NoteOn;
    c.track = 3;
    c.a = 60;
    rig.engine->send(c);
    sides(rig.render(24000), l, r);
    CHECK(peakOf(l) > 0.02f && peakOf(r) == 0.f);
    rig.app.project().track = 1;
    // A sampler preset keeps it too (its `edit` line, MONO appended).
    session.usePresetFolder(folder + "/presets");
    CHECK(session.saveSamplerPreset(1, "mono"));
    session.setSamplerEdit(1, LaneEdit());
    CHECK(rig.app.project().tracks[1].samplerEdit.mono == 0);
    CHECK(session.loadSamplerPreset(1, 0) && rig.app.project().tracks[1].samplerEdit.mono == 1);
}

// ---- the EDIT view's MONO stop --------------------------------------------------------------------------------------

void testTheMonoStop() {
    const std::string folder = tempFolder("stereo");
    CHECK(writeWav(folder + "/wide.wav", Clip(made(24000, 1), made(24000, 2)), 48000));
    CHECK(writeWav(folder + "/one.wav", made(24000, 1), 48000));
    Rig rig(folder);
    Session& session = rig.session();
    session.setLaneSample(0, kLane, "wide");
    session.setLaneSample(0, kLane + 1, "one");
    session.view = View::Device;
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    kit.goTo(kLane, KitScreen::kSound);
    rig.tap(Right, 4);
    rig.tap(A);
    CHECK(kit.editing() && kit.editRow() == kEditStart);
    rig.tap(Right);
    rig.tap(Up); // from the top row: MONO
    CHECK(kit.editRow() == kEditMono);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Up);
    rig.key(A, false);
    rig.frames(2);
    CHECK(rig.kit().laneEdit[kLane].mono == 1);
    rig.tap(B); // its usual: as the file (STEREO)
    CHECK(rig.kit().laneEdit[kLane].mono == 0 && kit.editing());
    rig.tap(Down); // back to the box it came from
    CHECK(kit.editRow() == kEditEnd);
    rig.tap(B);
    rig.tap(B);
    CHECK(!kit.editing());
    // A mono file: no such stop, UP stays on the top row.
    kit.goTo(kLane + 1, KitScreen::kSound);
    rig.tap(Right, 4);
    rig.tap(A);
    CHECK(kit.editing() && kit.editRow() < 4);
    const int row = kit.editRow();
    rig.tap(Up);
    CHECK(kit.editRow() == row);
}

// ---- CATCH and the bounce into samples ---------------------------------------------------------------------------------

void testCatchInStereo() {
    const std::string folder = tempFolder("stereo");
    CHECK(writeWav(folder + "/left.wav", Clip(std::vector<float>(48000, 0.4f), std::vector<float>(48000, 0.f)), 48000)); // a second, the left alone
    CHECK(writeWav(folder + "/mid.wav", std::vector<float>(48000, 0.4f), 48000));                                      // ...and a mono one
    Rig rig(folder);
    Session& session = rig.session();
    session.setLaneSample(0, 1, "left");
    rig.app.drums().setStep(1, 0, 1.f);
    rig.app.drums().setStep(1, 8, 1.f);
    rig.frames(2);
    // The mix: the left side caught on the left, nothing on the right; the file has two channels.
    session.togglePlay(false);
    rig.frames(1);
    rig.frames(2 * 16 * kStep / 256 + 8);
    Engine::Caught caught = rig.engine->lastBars(1);
    CHECK(caught.bars == 1 && caught.right.size() == caught.sound.size() && peakOf(caught.sound) > 0.2f && peakOf(caught.right) < 1e-6f);
    // lastBar is both sides mixed to one, as it always was.
    const std::vector<float> mixed = rig.engine->lastBar();
    CHECK(mixed.size() == caught.sound.size() && mixed.size() > 0 && std::fabs(mixed[100] - 0.5f * caught.sound[100]) < 1e-6f);
    const std::string name = session.catchLastBar(0, 5);
    CHECK(!name.empty());
    std::string bytes;
    CHECK(readTextFile(folder + "/" + name + ".wav", bytes) && wavChannels(bytes) == 2);
    Clip file;
    CHECK(readWav(folder + "/" + name + ".wav", file) && file.stereo() && peakOf(file) > 0.2f && peakOf(file.right) == 0.f);
    // A mono sound panned hard right on the mixer: caught on the right, the left silent. (The stereo one panned so
    // would be silence: PAN on a stereo sound is a balance, and its right side is silence.)
    session.setLaneSample(0, 1, "mid");
    session.setMix(0, 0, 50);
    rig.frames(2 * 16 * kStep / 256 + 8);
    caught = rig.engine->lastBars(1);
    CHECK(peakOf(caught.right) > 0.2f && peakOf(caught.sound) < 1e-6f);
    session.setLaneSample(0, 1, "left");
    session.setMix(0, 0, 0);
    // One track alone, before its pan: its own sides (the left).
    session.stepCatchFrom(1);
    rig.frames(1);
    rig.frames(3 * 16 * kStep / 256 + 8);
    caught = rig.engine->lastBars(1, 0);
    CHECK(caught.bars == 1 && peakOf(caught.sound) > 0.2f && peakOf(caught.right) < 1e-6f);
    // Nothing off the middle: still two channels (owner, 2026-11-28: every catch stereo; it was saved as one).
    session.stepCatchFrom(-1);
    session.setMix(0, 0, 0);
    session.setLaneSample(0, 1, "");
    rig.frames(1);
    rig.frames(2 * 16 * kStep / 256 + 8);
    const std::string middle = session.catchLastBar(0, 6);
    CHECK(!middle.empty() && readTextFile(folder + "/" + middle + ".wav", bytes) && wavChannels(bytes) == 2);
    session.togglePlay(false);
}

void testABounceIntoSamplesInStereo() {
    const std::string folder = tempFolder("stereo");
    CHECK(writeWav(folder + "/left.wav", Clip(std::vector<float>(24000, 0.4f), std::vector<float>(24000, 0.f)), 48000));
    Rig rig(folder);
    Session& session = rig.session();
    session.setLaneSample(0, 1, "left");
    rig.app.drums().setStep(1, 0, 1.f);
    rig.frames(2);
    CHECK(session.startBounce(Session::Bounce::ToSamples));
    session.finishBounce();
    CHECK(session.bounceFiles() == 1);
    Clip file;
    std::string bytes;
    CHECK(readTextFile(folder + "/" + session.bounceSample() + ".wav", bytes) && wavChannels(bytes) == 2);
    CHECK(readWav(folder + "/" + session.bounceSample() + ".wav", file) && file.stereo() && peakOf(file) > 0.2f && peakOf(file.right) == 0.f);
}

// ---- FIT ---------------------------------------------------------------------------------------------------------------

void testTheStretcherInStereo() {
    const std::vector<float> sound = made(30000, 3);
    // Two sides the same: each the same as the mono stretch, the simpler method to the bit; Signalsmith's sides alike.
    const Clip same(sound, sound);
    const Clip simple = stretchTo(same, 40000);
    CHECK(simple.stereo() && simple.size() == 40000u && simple.left() == stretchTo(sound, 40000) && simple.right == simple.left());
    const Clip smooth = fitTo(same, 40000, 48000.f);
    float apart = 0.f; // (Signalsmith's two channels come out within a hair of each other, not to the bit)
    for (std::size_t i = 0; i < smooth.size() && smooth.stereo(); ++i) apart = std::fmax(apart, std::fabs(smooth[i] - smooth.right[i]));
    CHECK(smooth.stereo() && smooth.size() == 40000u && apart < 1e-4f && peakOf(smooth) > 0.2f);
    CHECK(smooth.left() != stretchTo(same, 40000).left()); // (Signalsmith's, not the simpler method's)
    // Where each piece is taken from is found on both sides mixed to one: a sound on the right alone is stretched
    // as that sound by itself is.
    const Clip rightOnly = stretchTo(Clip(std::vector<float>(sound.size(), 0.f), sound), 40000);
    CHECK(rightOnly.stereo() && rightOnly.right == stretchTo(sound, 40000) && peakOf(rightOnly.left()) == 0.f);
    // A mono Clip: the mono stretch, to the bit.
    CHECK(fitTo(Clip(sound), 40000, 48000.f).left() == fitTo(sound, 40000, 48000.f) && !fitTo(Clip(sound), 40000, 48000.f).stereo());
    // The sides stay in line: a right side half the left is half the left after, all along.
    std::vector<float> halfOf(sound);
    for (float& v : halfOf) v *= 0.5f;
    for (const Clip& out : {stretchTo(Clip(sound, halfOf), 24000), fitTo(Clip(sound, halfOf), 24000, 48000.f)}) {
        float off = 0.f;
        for (std::size_t i = 0; i < out.size() && out.stereo(); ++i) off = std::fmax(off, std::fabs(out.right[i] - 0.5f * out[i]));
        CHECK(out.stereo() && out.size() == 24000u && off < 1e-4f);
    }
    // A lane fitted: the engine plays the fitted sound's two sides.
    const std::string folder = tempFolder("stereo");
    CHECK(writeWav(folder + "/loop.wav", Clip(made(90000, 1), std::vector<float>(90000, 0.f)), 48000));
    Rig rig(folder);
    rig.session().setLaneSample(0, kLane, "loop");
    rig.session().setLaneFit(0, kLane, 16);
    rig.frames(2);
    rig.session().finishStretching();
    rig.frames(2);
    const Session::LaneSound& fitted = rig.session().laneSound(0, kLane);
    CHECK(fitted.fitted && fitted.fitted->stereo() && fitted.fitted->size() == 96000u && peakOf(fitted.fitted->right) < 1e-3f);
    std::vector<float> l, r;
    sides(rig.hit(kLane, 12000), l, r);
    CHECK(peakOf(l) > 0.1f && peakOf(r) < 1e-3f);
}

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testReadingAndWriting", testReadingAndWriting}, {"testADrumLaneInStereo", testADrumLaneInStereo}, {"testTheSamplerAndCloudInStereo", testTheSamplerAndCloudInStereo},
        {"testALaneAndItsMono", testALaneAndItsMono}, {"testTheSamplerMono", testTheSamplerMono}, {"testTheMonoStop", testTheMonoStop},
        {"testCatchInStereo", testCatchInStereo}, {"testABounceIntoSamplesInStereo", testABounceIntoSamplesInStereo}, {"testTheStretcherInStereo", testTheStretcherInStereo},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("stereo_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
