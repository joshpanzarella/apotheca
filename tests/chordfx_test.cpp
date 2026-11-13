// CHORD EFFECTS, through the engine: STRUM (and the arpeggio), ORDER, HUMAN TIME, HUMAN LEVEL
// and LENGTHS (chord_rule.hpp). A listening synth writes down every note it is told to begin
// and to let go, to the frame; the checks read that.
// No framework: each CHECK prints its line on failure.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static const int kStep = 6000;  // frames in one step at 120 BPM and 48 kHz
static const int kBar = 16 * kStep;
static const int kMs = 48;      // frames in a millisecond
static const int kTight = 432;  // kStrumMs, in frames
static const int kWide = 1248;  // kWideStrumMs
static const int kHuman = 576;  // kHumanTimeMs

// A synth that makes no sound and writes down what it is told.
struct Told {
    long frame;
    int pitch;      // -1: every note let go at once
    bool on;
    float velocity;
    int from;       // a note begun: the key it slides from, or -1
    float seconds;
    bool operator==(const Told& o) const { return frame == o.frame && pitch == o.pitch && on == o.on && velocity == o.velocity && from == o.from && seconds == o.seconds; }
};
struct Spy : Instrument {
    std::vector<Told>* log;
    long frame = 0;
    explicit Spy(std::vector<Told>* to) : log(to) {}
    void prepare(float) override {}
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int, float) override {}
    void noteOn(int note, float velocity) override { log->push_back({frame, note, true, velocity, -1, 0.f}); }
    void noteOnFrom(int note, float velocity, int from, float seconds) override { log->push_back({frame, note, true, velocity, from, seconds}); }
    void noteOff(int note) override { log->push_back({frame, note, false, 0.f, -1, 0.f}); }
    void allNotesOff() override { log->push_back({frame, -1, false, 0.f, -1, 0.f}); }
    void render(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
        frame += frames;
    }
};

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}
static ChordPlay playOf(int strum, int order = 0, int time = 0, int level = 0, int lengths = 0) { return chordPlayOf(strum, order, time, level, lengths); }

// A note as it sounded: when it began, when it was let go (-1: never), how loud.
struct Sounded {
    long on, off;
    int pitch;
    float velocity;
    int from;
    float seconds;
};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    std::vector<Told> log;
    explicit Rig(int track = 1) {
        log.reserve(400000);
        Command c = make(Command::InstallSynth, track);
        c.pointer = new Spy(&log);
        CHECK(engine->send(c));
    }
    void send(const Command& c) { CHECK(engine->send(c)); }
    // A chord: its keys (lowest first) from `slot` on, as the Session sends them.
    void chord(int slot, int step, const std::vector<int>& keys, float length, ChordPlay play, int ratchet = 0, int throwTo = 0, float nudge = 0.f, int bar = 0, int track = 1, float level = 0.8f) {
        for (std::size_t k = 0; k < keys.size(); ++k) {
            Command c = make(Command::SetNote, track, slot + static_cast<int>(k), step, level);
            c.c = keys[k];
            c.d = static_cast<int>(length);
            c.extra = length - static_cast<float>(c.d);
            c.e = (ratchet >= 2 ? ratchet : 0) + (throwTo << 9);
            c.shift = nudge;
            c.bar = bar;
            c.play = play;
            c.play.index = static_cast<std::uint8_t>(k);
            c.play.count = static_cast<std::uint8_t>(keys.size());
            send(c);
        }
    }
    void note(int slot, int step, int pitch, float length, int glide = 0, float nudge = 0.f, int bar = 0, int track = 1) {
        Command c = make(Command::SetNote, track, slot, step, 0.8f);
        c.c = pitch;
        c.d = static_cast<int>(length);
        c.extra = length - static_cast<float>(c.d);
        c.e = glide ? 256 + (glide >= 2 ? glide << 11 : 0) : 0;
        c.shift = nudge;
        c.bar = bar;
        send(c);
    }
    void play() { send(make(Command::Play, 0, 0, 1)); }
    void run(long frames, int block = 1000) {
        std::vector<float> out(static_cast<std::size_t>(2 * block));
        for (long left = frames; left > 0; left -= block) engine->render(out.data(), left < block ? static_cast<int>(left) : block);
    }
    // Every note that began, in the order it began, with when it was let go.
    std::vector<Sounded> notes() const {
        std::vector<Sounded> all;
        for (const Told& t : log) {
            if (t.on) {
                all.push_back({t.frame, -1, t.pitch, t.velocity, t.from, t.seconds});
                continue;
            }
            for (std::size_t i = all.size(); i-- > 0;)
                if ((all[i].pitch == t.pitch || t.pitch < 0) && all[i].off < 0) {
                    all[i].off = t.frame;
                    if (t.pitch >= 0) break;
                }
        }
        return all;
    }
};
static bool near(long a, long b, long by = 1) { return std::labs(a - b) <= by; }

// ---- the numbers ------------------------------------------------------------------------------

static void testTheNumbers() {
    // In songs: never given another meaning.
    CHECK(kStrumOff == 0 && kStrumTight == 1 && kStrumWide == 2 && kStrumQuarter == 3 && kStrumHalf == 4 && kStrumStep == 5 && kStrumKinds == 6);
    CHECK(kOrderUp == 0 && kOrderDown == 1 && kOrderUpDown == 2 && kOrderRandom == 3 && kOrderKinds == 4);
    CHECK(kHumanTimeMs == 12.0 && kHumanLevelMost == 0.15 && kLengthsMost == 0.5); // the owner's "subtle"
    CHECK(kStrumMs == 9.0 && kWideStrumMs == 26.0);
    const char* strums[] = {"OFF", "STRUM", "WIDE STRUM", "1/4 STEP", "1/2 STEP", "1 STEP"};
    for (int i = 0; i < kStrumKinds; ++i) CHECK(std::string(strumName(i)) == strums[i]);
    CHECK(std::string(strumName(-1)) == "OFF" && std::string(strumName(6)) == "OFF");
    const char* orders[] = {"UP", "DOWN", "UP-DOWN", "RANDOM"};
    for (int i = 0; i < kOrderKinds; ++i) CHECK(std::string(orderName(i)) == orders[i]);
    CHECK(std::string(orderName(-1)) == "UP" && std::string(orderName(4)) == "UP");
    CHECK(arpeggioGap(kStrumOff) == 0.0 && arpeggioGap(kStrumTight) == 0.0 && arpeggioGap(kStrumWide) == 0.0);
    CHECK(arpeggioGap(kStrumQuarter) == 0.25 && arpeggioGap(kStrumHalf) == 0.5 && arpeggioGap(kStrumStep) == 1.0);
    CHECK(strumMs(kStrumOff) == 0.0 && strumMs(kStrumTight) == 9.0 && strumMs(kStrumWide) == 26.0 && strumMs(kStrumQuarter) == 0.0);
    const int clicks[] = {0, 10, 25, 50, 75, 100};
    for (int i = 0; i < kAmountClicks; ++i) CHECK(kAmountAt[i] == clicks[i] && amountPlace(clicks[i]) == i);
    CHECK(amountPlace(-5) == 0 && amountPlace(9) == 0 && amountPlace(11) == 1 && amountPlace(99) == 4 && amountPlace(200) == 5);

    // From outside: what is not known is off, what is too much is the most.
    ChordPlay p = chordPlayOf(5, 3, 100, 100, 100);
    CHECK(p.strum == 5 && p.order == 3 && p.time == 100 && p.level == 100 && p.lengths == 100 && p.index == 0 && p.count == 0);
    p = chordPlayOf(6, 4, 101, 300, 1000);
    CHECK(p.strum == 0 && p.order == 0 && p.time == 100 && p.level == 100 && p.lengths == 100);
    p = chordPlayOf(-1, -1, -1, -7, -100);
    CHECK(p.strum == 0 && p.order == 0 && p.time == 0 && p.level == 0 && p.lengths == 0 && p.atRest());
    CHECK(chordPlayOf(0, 3, 0, 0, 0).atRest()); // ORDER alone does nothing
    CHECK(!chordPlayOf(1, 0, 0, 0, 0).atRest() && !chordPlayOf(0, 0, 10, 0, 0).atRest() && !chordPlayOf(0, 0, 0, 10, 0).atRest() && !chordPlayOf(0, 0, 0, 0, 10).atRest());
    CHECK(!chordPlayOf(2, 0, 0, 0, 0).arpeggio() && chordPlayOf(3, 0, 0, 0, 0).arpeggio() && chordPlayOf(5, 0, 0, 0, 0).arpeggio());
    CHECK(chordPlayOf(1, 0, 0, 0, 0).timed() && chordPlayOf(0, 0, 10, 0, 0).timed() && !chordPlayOf(0, 0, 0, 50, 50).timed());
    ChordPlay raw = chordPlayOf(1, 1, 10, 10, 10);
    raw.index = 3;
    raw.count = 4;
    CHECK(chordPlayChecked(raw, 8).index == 3 && chordPlayChecked(raw, 8).count == 4 && chordPlayChecked(raw, 8).strum == 1);
    raw.index = 4; // not one of its four keys
    CHECK(chordPlayChecked(raw, 8).count == 0 && chordPlayChecked(raw, 8).index == 0);
    raw.index = 0;
    raw.count = 9;
    CHECK(chordPlayChecked(raw, 8).count == 0);
    raw.count = 8;
    raw.strum = 77;
    CHECK(chordPlayChecked(raw, 8).count == 8 && chordPlayChecked(raw, 8).strum == 0);

    // The places of a strum.
    for (int n = 1; n <= 8; ++n)
        for (int i = 0; i < n; ++i) {
            CHECK(strumPlace(kOrderUp, i, n, 99u, 0) == i && strumPlace(kOrderUp, i, n, 99u, 1) == i);
            CHECK(strumPlace(kOrderDown, i, n, 99u, 0) == n - 1 - i);
            CHECK(strumPlace(kOrderUpDown, i, n, 99u, 0) == i && strumPlace(kOrderUpDown, i, n, 99u, 1) == n - 1 - i && strumPlace(kOrderUpDown, i, n, 99u, 6) == i);
        }
    // RANDOM: every place once, and not the same order for every chord.
    int different = 0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        int seen = 0, inOrder = 0;
        for (int i = 0; i < 5; ++i) {
            const int place = strumPlace(kOrderRandom, i, 5, seed, 0);
            CHECK(place >= 0 && place < 5);
            seen |= 1 << place;
            inOrder += place == i;
        }
        CHECK(seen == 31);
        different += inOrder != 5;
    }
    CHECK(different > 180);
    // An arpeggio's keys.
    const int up[] = {0, 1, 2, 3, 0, 1, 2, 3, 0}, down[] = {3, 2, 1, 0, 3, 2, 1, 0, 3}, both[] = {0, 1, 2, 3, 2, 1, 0, 1, 2};
    for (int m = 0; m < 9; ++m) CHECK(arpeggioKey(kOrderUp, m, 4, 5u) == up[m] && arpeggioKey(kOrderDown, m, 4, 5u) == down[m] && arpeggioKey(kOrderUpDown, m, 4, 5u) == both[m]);
    CHECK(arpeggioKey(kOrderUpDown, 0, 2, 5u) == 0 && arpeggioKey(kOrderUpDown, 1, 2, 5u) == 1 && arpeggioKey(kOrderUpDown, 2, 2, 5u) == 0);
    for (int order = 0; order < kOrderKinds; ++order) CHECK(arpeggioKey(order, 7, 1, 5u) == 0);
    int repeats = 0, fresh = 0;
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        int first[4] = {};
        for (int round = 0; round < 6; ++round) {
            int seen = 0, same = 0;
            for (int at = 0; at < 4; ++at) {
                const int key = arpeggioKey(kOrderRandom, round * 4 + at, 4, seed);
                seen |= 1 << key;
                if (round == 0) first[at] = key;
                same += key == first[at];
                if (round * 4 + at > 0) repeats += key == arpeggioKey(kOrderRandom, round * 4 + at - 1, 4, seed);
            }
            CHECK(seen == 15); // every key once a round
            fresh += round > 0 && same != 4;
        }
    }
    CHECK(repeats == 0);  // never the same key twice running
    CHECK(fresh > 430);  // a fresh order each round (of 500; one in 24 is the same by chance)

    // The rolls: from where and which time, nothing else; spread over the whole range.
    CHECK(chordSeed(1, 2, 3, 4) == chordSeed(1, 2, 3, 4));
    CHECK(chordSeed(1, 2, 3, 4) != chordSeed(2, 2, 3, 4) && chordSeed(1, 2, 3, 4) != chordSeed(1, 3, 3, 4) && chordSeed(1, 2, 3, 4) != chordSeed(1, 2, 4, 4) && chordSeed(1, 2, 3, 4) != chordSeed(1, 2, 3, 5));
    double sum = 0.0, least = 1.0, most = -1.0;
    for (int i = 0; i < 4000; ++i) {
        const double v = chordSigned(chordSeed(1, 0, 4, 16L * i), kRollTime, 2);
        CHECK(v >= -1.0 && v < 1.0);
        sum += v;
        least = std::min(least, v);
        most = std::max(most, v);
    }
    CHECK(std::fabs(sum / 4000.0) < 0.05 && least < -0.98 && most > 0.98);
    CHECK(chordUnit(7u, kRollTime, 1) != chordUnit(7u, kRollLevel, 1) && chordUnit(7u, kRollTime, 1) != chordUnit(7u, kRollTime, 2) && chordUnit(7u, kRollLengths, 1) != chordUnit(7u, kRollOrder, 1));
}

// ---- at rest: a chord's note is a note ----------------------------------------------------------

static std::vector<Told> restSong(bool asChord, int order) {
    Rig rig;
    const std::vector<int> keys = {60, 64, 67, 71};
    ChordPlay play = playOf(0, order);
    if (asChord) {
        rig.chord(0, 2, keys, 3.f, play, 0, 0, 0.f);
        rig.chord(4, 8, keys, 2.5f, play, 3, 0, 0.3f);
        rig.chord(8, 13, keys, 2.f, play, 0, 0, -0.3f);
    } else {
        for (int k = 0; k < 4; ++k) {
            Command c = make(Command::SetNote, 1, k, 2, 0.8f);
            c.c = keys[static_cast<std::size_t>(k)];
            c.d = 3;
            rig.send(c);
            c.a = 4 + k;
            c.b = 8;
            c.d = 2;
            c.extra = 0.5f;
            c.e = 3;
            c.shift = 0.3f;
            rig.send(c);
            c.a = 8 + k;
            c.b = 13;
            c.d = 2;
            c.extra = 0.f;
            c.e = 0;
            c.shift = -0.3f;
            rig.send(c);
        }
    }
    rig.note(20, 15, 50, 1.f, kGlideFull);
    rig.play();
    rig.run(3 * kBar);
    return rig.log;
}
static void testAtRest() {
    const std::vector<Told> plain = restSong(false, 0);
    CHECK(plain.size() > 100);
    CHECK(restSong(true, 0) == plain);
    CHECK(restSong(true, kOrderRandom) == plain); // ORDER alone: nothing
}

// ---- STRUM -------------------------------------------------------------------------------------

static void testStrum() {
    const std::vector<int> keys = {60, 64, 67, 71};
    // One after another, lowest first, each held to the chord's end.
    for (int strum : {kStrumTight, kStrumWide}) {
        const int gap = strum == kStrumTight ? kTight : kWide;
        Rig rig;
        rig.chord(0, 2, keys, 4.f, playOf(strum));
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 4);
        for (std::size_t k = 0; k < n.size() && k < 4; ++k) {
            CHECK(n[k].pitch == keys[k] && n[k].on == 2 * kStep + static_cast<long>(k) * gap);
            CHECK(near(n[k].off, 6 * kStep));
            CHECK(n[k].velocity == 0.8f && n[k].from == -1);
        }
    }
    // DOWN: highest first.
    {
        Rig rig;
        rig.chord(0, 2, keys, 4.f, playOf(kStrumTight, kOrderDown));
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 4);
        for (std::size_t k = 0; k < n.size() && k < 4; ++k) CHECK(n[k].pitch == keys[3 - k] && n[k].on == 2 * kStep + static_cast<long>(k) * kTight && near(n[k].off, 6 * kStep));
    }
    // UP-DOWN: up, then down, then up, chord by chord, as a hand goes. Play starts it going up again.
    {
        Rig rig;
        rig.chord(0, 0, keys, 2.f, playOf(kStrumTight, kOrderUpDown));
        rig.chord(4, 4, keys, 2.f, playOf(kStrumTight, kOrderUpDown));
        rig.chord(8, 8, keys, 2.f, playOf(kStrumTight, kOrderUpDown));
        for (int pass = 0; pass < 2; ++pass) {
            rig.log.clear();
            rig.play();
            const long top = pass * (2 * kBar - 100L);
            rig.run(2 * kBar - 100);
            const std::vector<Sounded> n = rig.notes();
            CHECK(n.size() == 24);
            for (std::size_t c = 0; c < 6 && n.size() == 24; ++c)
                for (std::size_t k = 0; k < 4; ++k) {
                    const long at = top + static_cast<long>(c / 3) * kBar + static_cast<long>(c % 3) * 4 * kStep + static_cast<long>(k) * kTight;
                    CHECK(n[c * 4 + k].on == at && n[c * 4 + k].pitch == (c % 2 == 0 ? keys[k] : keys[3 - k]));
                }
        }
    }
    // RANDOM: every key once, in an order that is not the same every time round.
    {
        Rig rig;
        rig.chord(0, 4, keys, 2.f, playOf(kStrumTight, kOrderRandom));
        rig.play();
        rig.run(60L * kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 240);
        int orders[256] = {}, kinds = 0;
        for (std::size_t loop = 0; loop + 1 <= n.size() / 4; ++loop) {
            int seen = 0, code = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                const Sounded& s = n[loop * 4 + k];
                CHECK(s.on == static_cast<long>(loop) * kBar + 4 * kStep + static_cast<long>(k) * kTight);
                const int key = static_cast<int>(std::find(keys.begin(), keys.end(), s.pitch) - keys.begin());
                seen |= 1 << key;
                code = code * 4 + key;
            }
            CHECK(seen == 15);
            kinds += orders[code]++ == 0;
        }
        CHECK(kinds >= 15); // of the 24 there are, in 60 goes
    }
    // A strum never takes more than half the chord: a short chord is strummed faster.
    {
        const std::vector<int> eight = {48, 52, 55, 59, 60, 64, 67, 71};
        Rig rig;
        rig.chord(0, 2, eight, 1.f, playOf(kStrumWide)); // 7 x 26 ms is more than half of a 125 ms step
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 8);
        for (std::size_t k = 0; k < n.size() && k < 8; ++k) CHECK(n[k].pitch == eight[k] && near(n[k].on, 2 * kStep + static_cast<long>(k) * (kStep / 2) / 7) && near(n[k].off, 3 * kStep));
        if (n.size() == 8) CHECK(near(n[7].on, 2 * kStep + kStep / 2));
    }
    // A chord pushed early on the song's first step: there is no "before" the first time, so it is
    // strummed from the top; every time after, from where it is, before the bar line.
    {
        Rig rig;
        rig.chord(0, 0, keys, 2.f, playOf(kStrumTight), 0, 0, -0.25f);
        rig.play();
        rig.run(2L * kBar - 4 * kStep);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 8);
        for (std::size_t k = 0; k < 4 && n.size() == 8; ++k) {
            CHECK(n[k].pitch == keys[k] && n[k].on == static_cast<long>(k) * kTight && near(n[k].off, 2 * kStep));
            CHECK(n[4 + k].pitch == keys[k] && near(n[4 + k].on, kBar - kStep / 4 + static_cast<long>(k) * kTight) && near(n[4 + k].off, kBar - kStep / 4 + 2 * kStep, 2));
        }
    }
    // One key: nothing to strum.
    {
        Rig rig;
        rig.chord(0, 2, {60}, 2.f, playOf(kStrumWide, kOrderDown));
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 1 && n[0].on == 2 * kStep && near(n[0].off, 4 * kStep));
    }
    // An early chord (nudged) is strummed from where it is; a late one too.
    for (float nudge : {-0.25f, 0.25f}) {
        Rig rig;
        rig.chord(0, 4, keys, 2.f, playOf(kStrumTight), 0, 0, nudge);
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 4);
        const long at = 4 * kStep + static_cast<long>(nudge * kStep);
        for (std::size_t k = 0; k < n.size() && k < 4; ++k) CHECK(n[k].pitch == keys[k] && near(n[k].on, at + static_cast<long>(k) * kTight) && near(n[k].off, at + 2 * kStep, 2));
    }
}

// STRUM meets RATCHET: every piece of the ratchet is strummed.
static void testStrumWithRatchet() {
    const std::vector<int> keys = {60, 64, 67, 71};
    Rig rig;
    rig.chord(0, 2, keys, 2.f, playOf(kStrumTight), 2);
    rig.play();
    rig.run(kBar);
    const std::vector<Sounded> n = rig.notes();
    CHECK(n.size() == 8);
    for (std::size_t k = 0; k < 4 && n.size() == 8; ++k) {
        // (in the order they began: the first piece of all four, then the second)
        CHECK(n[k].pitch == keys[k] && n[k].on == 2 * kStep + static_cast<long>(k) * kTight);
        CHECK(n[4 + k].pitch == keys[k] && n[4 + k].on == 2 * kStep + kStep / 2 + static_cast<long>(k) * kTight);
        CHECK(n[k].off < n[4 + k].on);          // let go before it is struck again
        CHECK(near(n[4 + k].off, 4 * kStep));   // the last piece to the chord's end
    }
    // x4: a piece is a quarter of a step, the strum half of that at the most.
    Rig four;
    four.chord(0, 2, keys, 2.f, playOf(kStrumWide), 4);
    four.play();
    four.run(kBar);
    const std::vector<Sounded> m = four.notes();
    CHECK(m.size() == 16);
    for (std::size_t piece = 0; piece < 4 && m.size() == 16; ++piece)
        for (std::size_t k = 0; k < 4; ++k) CHECK(m[piece * 4 + k].pitch == keys[k] && near(m[piece * 4 + k].on, 2 * kStep + static_cast<long>(piece) * (kStep / 4) + static_cast<long>(k) * (kStep / 8) / 3));
}

// ---- the arpeggio --------------------------------------------------------------------------------

static void testArpeggio() {
    const std::vector<int> keys = {60, 64, 67};
    struct Case { int strum, gap; };
    for (const Case c : {Case{kStrumQuarter, kStep / 4}, Case{kStrumHalf, kStep / 2}, Case{kStrumStep, kStep}}) {
        // Round and round the keys until the chord ends, one note at a time.
        Rig rig;
        rig.chord(0, 2, keys, 4.f, playOf(c.strum));
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        const std::size_t want = static_cast<std::size_t>(4 * kStep / c.gap);
        CHECK(n.size() == want);
        for (std::size_t m = 0; m < n.size() && m < want; ++m) {
            CHECK(n[m].pitch == keys[m % 3] && n[m].on == 2 * kStep + static_cast<long>(m) * c.gap);
            CHECK(n[m].off == n[m].on + c.gap); // held until the next one begins
            CHECK(n[m].velocity == 0.8f);
        }
        if (!n.empty()) CHECK(n.back().off == 6 * kStep); // and no further than the chord
    }
    // The orders.
    {
        const std::vector<int> four = {60, 64, 67, 71};
        const int down[] = {71, 67, 64, 60, 71, 67, 64, 60}, both[] = {60, 64, 67, 71, 67, 64, 60, 64};
        for (int order : {kOrderDown, kOrderUpDown, kOrderRandom}) {
            Rig rig;
            rig.chord(0, 0, four, 8.f, playOf(kStrumStep, order));
            rig.play();
            rig.run(4L * kBar);
            const std::vector<Sounded> n = rig.notes();
            CHECK(n.size() == 32);
            bool same = true;
            for (std::size_t m = 0; m < n.size() && m < 32; ++m) {
                CHECK(n[m].on == static_cast<long>(m / 8) * kBar + static_cast<long>(m % 8) * kStep);
                if (order == kOrderDown) CHECK(n[m].pitch == down[m % 8]);
                if (order == kOrderUpDown) CHECK(n[m].pitch == both[m % 8]);
                if (m >= 8) same = same && n[m].pitch == n[m - 8].pitch;
                if (order == kOrderRandom && m % 4 == 3) { // every key once in each four
                    int seen = 0;
                    for (std::size_t k = m - 3; k <= m; ++k) seen |= 1 << (std::find(four.begin(), four.end(), n[k].pitch) - four.begin());
                    CHECK(seen == 15);
                }
            }
            CHECK(same == (order != kOrderRandom)); // RANDOM: not the same each time round
        }
    }
    // A chord shorter than once round: as many notes as fit, the last cut at the chord's end.
    {
        Rig rig;
        rig.chord(0, 2, keys, 1.75f, playOf(kStrumStep));
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 2);
        if (n.size() == 2) CHECK(n[0].pitch == 60 && n[0].on == 2 * kStep && n[0].off == 3 * kStep && n[1].pitch == 64 && n[1].on == 3 * kStep && near(n[1].off, 3 * kStep + 3 * kStep / 4));
    }
    // A ratchet is not played in an arpeggio.
    {
        Rig rig;
        rig.chord(0, 2, keys, 2.f, playOf(kStrumHalf), 4);
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 4);
        for (std::size_t m = 0; m < n.size(); ++m) CHECK(n[m].on == 2 * kStep + static_cast<long>(m) * kStep / 2);
    }
    // One chord's arpeggio ends where the next chord's begins: an early one cuts it off, and
    // each keeps to its own keys while the next is already set waiting.
    {
        Rig rig;
        rig.chord(0, 0, keys, 4.f, playOf(kStrumQuarter));
        rig.chord(4, 4, {72, 76, 79}, 4.f, playOf(kStrumQuarter), 0, 0, -0.4f); // from 3.6 steps
        rig.chord(8, 8, {50, 53}, 4.f, playOf(0)); // and a plain chord after
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 15 + 16 + 2);
        const long early = 4 * kStep - 2400;
        int first = 0, second = 0;
        for (std::size_t m = 0; m < n.size(); ++m) {
            if (n[m].pitch >= 72) {
                CHECK(n[m].on == early + second * (kStep / 4) && n[m].pitch == 72 + (second % 3 == 0 ? 0 : second % 3 == 1 ? 4 : 7));
                ++second;
            } else if (n[m].pitch >= 60) {
                CHECK(n[m].on == first * (kStep / 4) && n[m].on < early && n[m].pitch == keys[static_cast<std::size_t>(first % 3)]);
                ++first;
            }
        }
        CHECK(first == 15 && second == 16);
    }
    // A chord on step 5 pushed early is set waiting on step 4, where another chord's arpeggio is
    // just being set too: two chords, two clocks, each its own keys.
    {
        Rig rig;
        rig.chord(0, 4, {60, 64}, 0.75f, playOf(kStrumQuarter));
        rig.chord(2, 5, {72, 76}, 1.f, playOf(kStrumQuarter), 0, 0, -0.25f);
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        const int want[] = {60, 64, 60, 72, 76, 72, 76};
        CHECK(n.size() == 7);
        for (std::size_t m = 0; m < n.size() && m < 7; ++m) CHECK(n[m].pitch == want[m] && n[m].on == 4 * kStep + static_cast<long>(m) * (kStep / 4));
    }
    // A chord with a key missing (the bar had no slot left for it): the arpeggio is of the keys there are.
    {
        Rig rig;
        const int given[] = {0, 2, 3}, pitch[] = {60, 67, 71};
        for (int i = 0; i < 3; ++i) {
            Command c = make(Command::SetNote, 1, i, 2, 0.8f);
            c.c = pitch[i];
            c.d = 2;
            c.play = playOf(kStrumQuarter);
            c.play.index = static_cast<std::uint8_t>(given[i]);
            c.play.count = 4;
            rig.send(c);
        }
        rig.play();
        rig.run(kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 8);
        for (std::size_t m = 0; m < n.size(); ++m) CHECK(n[m].pitch == pitch[m % 3]);
    }
    // A chord on the last step, running over the loop's start: it goes on, and the loop's own
    // first chord takes over from it.
    {
        Rig rig;
        rig.chord(0, 15, keys, 3.f, playOf(kStrumHalf));
        rig.play();
        rig.run(2L * kBar + 4 * kStep);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 12);
        for (std::size_t m = 0; m < n.size() && m < 12; ++m) CHECK(n[m].pitch == keys[(m % 6) % 3] && n[m].on == static_cast<long>(m / 6) * kBar + 15 * kStep + static_cast<long>(m % 6) * kStep / 2);
    }
}

// ---- HUMAN TIME ----------------------------------------------------------------------------------

// The chord on `step` of a one-bar loop, `loops` times: how far each note began from the chord's
// own moment, in frames (loop by loop, key by key).
static std::vector<std::vector<long>> humanOffsets(int step, int percent, int loops, std::vector<Sounded>* all = nullptr, int tempoFrames = kStep) {
    const std::vector<int> keys = {60, 64, 67, 71};
    Rig rig;
    if (tempoFrames != kStep) rig.send(make(Command::SetTempo, 0, 0, 0, 120.f * kStep / tempoFrames));
    rig.chord(0, step, keys, 2.f, playOf(0, 0, percent));
    rig.play();
    rig.run(static_cast<long>(loops) * 16 * tempoFrames + 8 * tempoFrames);
    const std::vector<Sounded> n = rig.notes();
    if (all) *all = n;
    std::vector<std::vector<long>> offsets(static_cast<std::size_t>(loops), std::vector<long>(4, 999999));
    for (const Sounded& s : n) {
        const std::size_t key = static_cast<std::size_t>(std::find(keys.begin(), keys.end(), s.pitch) - keys.begin());
        const long nominal = static_cast<long>(step) * tempoFrames;
        const long loop = (s.on - nominal + 8L * tempoFrames) / (16L * tempoFrames);
        if (loop >= 0 && loop < loops && key < 4) offsets[static_cast<std::size_t>(loop)][key] = s.on - (loop * 16L * tempoFrames + nominal);
    }
    return offsets;
}
static void testHumanTime() {
    for (int percent : {100, 50, 10}) {
        const int loops = 300;
        const long most = kHuman * percent / 100;
        std::vector<Sounded> all;
        const std::vector<std::vector<long>> off = humanOffsets(4, percent, loops, &all);
        CHECK(all.size() == static_cast<std::size_t>(4 * (loops + 1)) || all.size() == static_cast<std::size_t>(4 * loops));
        double sum = 0.0;
        long least = 0, greatest = 0;
        int early = 0, late = 0, sameAsBefore = 0, together = 0;
        for (int loop = 0; loop < loops; ++loop) {
            const std::vector<long>& o = off[static_cast<std::size_t>(loop)];
            for (int k = 0; k < 4; ++k) {
                CHECK(o[static_cast<std::size_t>(k)] >= -most - 1 && o[static_cast<std::size_t>(k)] <= most + 1); // never further than the top figure
                sum += static_cast<double>(o[static_cast<std::size_t>(k)]);
                least = std::min(least, o[static_cast<std::size_t>(k)]);
                greatest = std::max(greatest, o[static_cast<std::size_t>(k)]);
                early += o[static_cast<std::size_t>(k)] < 0;
                late += o[static_cast<std::size_t>(k)] > 0;
            }
            if (loop > 0) sameAsBefore += o == off[static_cast<std::size_t>(loop - 1)];
            together += o[0] == o[1] && o[1] == o[2] && o[2] == o[3];
        }
        CHECK(std::fabs(sum / (4.0 * loops)) < 0.06 * most + 1.0); // centred: as often early as late
        CHECK(least < -0.9 * most && greatest > 0.9 * most);        // and it does reach the figure
        CHECK(early > loops && late > loops);
        CHECK(sameAsBefore == 0); // never the same twice
        CHECK(together <= (percent == 10 ? 3 : 0)); // each note its own way (at 10% the whole range is 115 frames)
        // Each note still ends at the chord's end.
        for (const Sounded& s : all)
            if (s.off >= 0) CHECK((s.off - 6 * kStep) % kBar == 0 || near((s.off - 6 * kStep) % kBar, 0) || near((s.off - 6 * kStep) % kBar, kBar));
    }
    // A chord on the song's first step: the first time there is nothing before the top to be early
    // in, so its early notes are AT the top; every time after, they are early, before the bar line.
    {
        std::vector<Sounded> all;
        const std::vector<std::vector<long>> off = humanOffsets(0, 100, 100, &all);
        for (std::size_t i = 0; i < 4 && i < all.size(); ++i) CHECK(all[i].on < 600 && near(all[i].off, 2 * kStep)); // ...and still end with the chord
        int atTop = 0, earlyLater = 0;
        for (int k = 0; k < 4; ++k) {
            CHECK(off[0][static_cast<std::size_t>(k)] >= 0);
            atTop += off[0][static_cast<std::size_t>(k)] == 0;
        }
        CHECK(atTop >= 1 && atTop <= 3);
        for (int loop = 1; loop < 100; ++loop)
            for (int k = 0; k < 4; ++k) {
                CHECK(std::labs(off[static_cast<std::size_t>(loop)][static_cast<std::size_t>(k)]) <= kHuman + 1);
                earlyLater += off[static_cast<std::size_t>(loop)][static_cast<std::size_t>(k)] < 0;
            }
        CHECK(earlyLater > 150 && earlyLater < 250);
    }
    // The figure is milliseconds, not a part of a step: the same at another tempo.
    {
        const int slow = 12000; // 60 BPM
        const std::vector<std::vector<long>> off = humanOffsets(4, 100, 100, nullptr, slow);
        long least = 0, greatest = 0;
        for (const auto& loop : off)
            for (long o : loop) {
                least = std::min(least, o);
                greatest = std::max(greatest, o);
            }
        CHECK(least >= -kHuman - 1 && greatest <= kHuman + 1 && least < -0.9 * kHuman && greatest > 0.9 * kHuman);
    }
    // With a strum: each note is moved from its own place in the strum.
    {
        const std::vector<int> keys = {60, 64, 67, 71};
        Rig rig;
        rig.chord(0, 4, keys, 4.f, playOf(kStrumWide, 0, 100));
        rig.play();
        rig.run(50L * kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 200);
        int moved = 0;
        for (const Sounded& s : n) {
            const long k = std::find(keys.begin(), keys.end(), s.pitch) - keys.begin();
            const long from = (s.on - 4 * kStep - k * kWide + kBar) % kBar; // 0 if not moved
            const long by = from > kBar / 2 ? from - kBar : from;
            CHECK(std::labs(by) <= kHuman + 1);
            moved += by != 0;
        }
        CHECK(moved > 190);
    }
    // In an arpeggio: no note moves by more than a quarter of the way to the next, so the order holds.
    {
        const std::vector<int> keys = {60, 64, 67};
        Rig rig;
        rig.chord(0, 2, keys, 4.f, playOf(kStrumQuarter, 0, 100)); // a quarter of a step is 1500 frames: 375 at the most
        rig.play();
        rig.run(40L * kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 40 * 16);
        long least = 0, greatest = 0;
        for (std::size_t i = 0; i < n.size(); ++i) {
            const long m = static_cast<long>(i % 16), loop = static_cast<long>(i / 16);
            const long by = n[i].on - (loop * kBar + 2 * kStep + m * (kStep / 4));
            CHECK(n[i].pitch == keys[static_cast<std::size_t>(m % 3)]);
            least = std::min(least, by);
            greatest = std::max(greatest, by);
        }
        CHECK(least >= -376 && greatest <= 376 && least < -330 && greatest > 330);
    }
    // ...and at a step a note it is the figure itself. Each note is held to the next one's own
    // moment, wherever it began itself (on six tracks: each rolls its own way).
    {
        long least = 0, greatest = 0;
        int firstEarly = 0;
        for (int track = 1; track <= 6; ++track) {
            Rig rig(track);
            rig.chord(0, 0, {60, 64, 67}, 16.f, playOf(kStrumStep, 0, 100), 0, 0, 0.f, 0, track);
            rig.play();
            rig.run(40L * kBar - 600);
            const std::vector<Sounded> n = rig.notes();
            CHECK(n.size() == 40 * 16);
            for (std::size_t i = 0; i < n.size(); ++i) {
                const long by = n[i].on - static_cast<long>(i) * kStep;
                least = std::min(least, by);
                greatest = std::max(greatest, by);
                if (i == 0) CHECK(by >= 0); // nothing is before the top
                // (The last of each round is the next round's first key: struck again when that one comes, early or not.)
                if (n[i].off >= 0 && i % 16 != 15) CHECK(near(n[i].off, static_cast<long>(i + 1) * kStep, 2));
            }
            firstEarly += n.size() > 16 && n[16].on < kBar; // (the chord's first note, the second time round)
        }
        CHECK(least >= -kHuman - 1 && greatest <= kHuman + 1 && least < -0.9 * kHuman && greatest > 0.9 * kHuman);
        CHECK(firstEarly >= 1 && firstEarly <= 5);
    }
}

// ---- HUMAN LEVEL and LENGTHS -------------------------------------------------------------------------

static void testHumanLevel() {
    const std::vector<int> keys = {60, 64, 67, 71};
    for (int percent : {100, 50}) {
        Rig rig;
        rig.chord(0, 4, keys, 2.f, playOf(0, 0, 0, percent), 0, 0, 0.f, 0, 1, 0.6f);
        rig.play();
        rig.run(300L * kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 1200);
        const double most = 0.6 * 0.15 * percent / 100.0;
        double sum = 0.0, least = 1.0, greatest = 0.0;
        int same = 0, together = 0;
        for (std::size_t i = 0; i < n.size(); ++i) {
            CHECK(n[i].on == static_cast<long>(i / 4) * kBar + 4 * kStep); // its time is not touched
            CHECK(near(n[i].off, n[i].on + 2 * kStep));                     // nor its length
            CHECK(std::fabs(n[i].velocity - 0.6) <= most + 1e-6);
            sum += n[i].velocity;
            least = std::min(least, static_cast<double>(n[i].velocity));
            greatest = std::max(greatest, static_cast<double>(n[i].velocity));
            if (i >= 4) same += n[i].velocity == n[i - 4].velocity;
            if (i % 4 == 3) together += n[i].velocity == n[i - 1].velocity && n[i].velocity == n[i - 2].velocity && n[i].velocity == n[i - 3].velocity;
        }
        CHECK(together == 0); // each key its own way, not the chord as one
        CHECK(std::fabs(sum / 1200.0 - 0.6) < 0.06 * most);
        CHECK(least < 0.6 - 0.9 * most && greatest > 0.6 + 0.9 * most);
        CHECK(same == 0);
    }
    // A loud chord is not pushed past full; an arpeggio's notes each get their own.
    {
        Rig rig;
        rig.chord(0, 0, keys, 16.f, playOf(kStrumStep, 0, 0, 100), 0, 0, 0.f, 0, 1, 1.f);
        rig.play();
        rig.run(20L * kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 320);
        int full = 0, under = 0;
        for (const Sounded& s : n) {
            CHECK(s.velocity <= 1.f && s.velocity >= 0.85f - 1e-6f);
            full += s.velocity == 1.f;
            under += s.velocity < 0.99f;
        }
        CHECK(full > 100 && under > 100);
    }
}

static void testLengths() {
    const std::vector<int> keys = {60, 64, 67, 71};
    for (int percent : {100, 25}) {
        Rig rig;
        rig.chord(0, 0, keys, 4.f, playOf(0, 0, 0, 0, percent));
        rig.chord(4, 4, {50, 53, 57}, 4.f, playOf(0, 0, 0, 0, percent)); // the next chord, straight after
        rig.play();
        rig.run(200L * kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 200 * 7);
        const long whole = 4 * kStep, least = static_cast<long>(whole * (1.0 - 0.5 * percent / 100.0));
        long shortest = whole, longest = 0;
        int same = 0, together = 0;
        for (std::size_t i = 0; i < n.size(); ++i) {
            if (i % 7 == 3) together += n[i].off == n[i - 1].off && n[i].off == n[i - 2].off && n[i].off == n[i - 3].off;
            CHECK(n[i].on % (4 * kStep) == 0 && n[i].velocity == 0.8f); // on its moment, at its level
            const long held = n[i].off - n[i].on;
            CHECK(n[i].off >= 0 && held <= whole + 1 && held >= least - 1); // shorter only, never under the figure
            shortest = std::min(shortest, held);
            longest = std::max(longest, held);
            if (i >= 7) same += held == n[i - 7].off - n[i - 7].on;
            // Nothing sounds on into the chord that follows it.
            CHECK(n[i].off <= (n[i].on / whole + 1) * whole + 1);
        }
        CHECK(shortest < least + (whole - least) / 10 && longest > whole - (whole - least) / 10);
        CHECK(same <= 2);
        CHECK(together == 0); // each key its own way
    }
    // In an arpeggio each note is let go sooner; none runs into the next.
    {
        Rig rig;
        rig.chord(0, 0, keys, 16.f, playOf(kStrumStep, 0, 0, 0, 100));
        rig.play();
        rig.run(20L * kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 320);
        long shortest = kStep, longest = 0;
        for (std::size_t i = 0; i < n.size(); ++i) {
            const long held = n[i].off - n[i].on;
            CHECK(n[i].on == static_cast<long>(i) * kStep && held <= kStep && held >= kStep / 2 - 1);
            shortest = std::min(shortest, held);
            longest = std::max(longest, held);
        }
        CHECK(shortest < kStep / 2 + 300 && longest > kStep - 300);
    }
}

// ---- the same every time, however it is rendered ------------------------------------------------

// A song with every effect at work, on a listening track: what the synth was told.
static void fxSong(Rig& rig, int track = 1) {
    rig.send(make(Command::SetCellBars, track, 2));
    rig.chord(0, 0, {60, 64, 67, 71}, 3.f, playOf(kStrumWide, kOrderRandom, 100, 100, 100), 0, 0, 0.f, 0, track);
    rig.chord(4, 4, {62, 65, 69}, 2.f, playOf(kStrumTight, kOrderUpDown, 50, 25, 10), 3, 0, 0.f, 0, track);
    rig.chord(8, 7, {57, 60, 64, 67, 71}, 5.f, playOf(kStrumQuarter, kOrderRandom, 100, 100, 100), 0, 0, -0.2f, 0, track);
    rig.chord(16, 12, {55, 59, 62}, 4.f, playOf(0, 0, 100, 0, 0), 2, 0, 0.3f, 0, track);
    rig.chord(0, 0, {53, 57, 60, 64}, 8.f, playOf(kStrumHalf, kOrderUpDown, 75, 50, 50), 0, 0, 0.f, 1, track);
    rig.chord(4, 8, {48, 55, 64}, 7.5f, playOf(kStrumStep, kOrderDown, 0, 100, 0), 0, 0, 0.f, 1, track);
}
static std::vector<Told> performance(int block, int helpers, bool twice = false, float tempo = 120.f) {
    Rig rig;
    rig.engine->useWorkers(helpers);
    if (tempo != 120.f) rig.send(make(Command::SetTempo, 0, 0, 0, tempo));
    fxSong(rig);
    rig.play();
    rig.run(6L * kBar, block);
    if (twice) { // Play again, from the top
        const long before = rig.log.empty() ? 0 : rig.log.back().frame;
        rig.log.clear();
        rig.play();
        rig.run(1, 1); // (Play itself, and the top's notes, on a frame of their own)
        const long top = rig.log.empty() ? before : rig.log.front().frame;
        rig.run(6L * kBar - 1, block);
        for (Told& t : rig.log) t.frame -= top;
        // The notes Play let go are not part of the performance.
        std::vector<Told> kept;
        for (const Told& t : rig.log)
            if (t.pitch >= 0) kept.push_back(t);
        return kept;
    }
    std::vector<Told> kept;
    for (const Told& t : rig.log)
        if (t.pitch >= 0) kept.push_back(t);
    return kept;
}
static void testTheSameEveryTime() {
    const std::vector<Told> first = performance(64, 0);
    CHECK(first.size() > 300);
    for (int block : {48, 256, 1, 173}) CHECK(performance(block, 0) == first);
    for (int helpers = 1; helpers <= 3; ++helpers) CHECK(performance(64, helpers) == first);
    // At a tempo where a step is not a whole number of samples (113: 6371.68), too.
    const std::vector<Told> odd = performance(64, 0, false, 113.f);
    CHECK(odd.size() > 300 && !(odd == first));
    for (int block : {48, 256, 1, 173}) CHECK(performance(block, 0, false, 113.f) == odd);
    CHECK(performance(64, 3, false, 113.f) == odd);
    // Play from the top again: the same performance (a bounce is what was heard).
    CHECK(performance(64, 0, true) == first);
    // And the times round are not alike: bar 1 of the first two passes.
    std::vector<Told> one, two;
    for (const Told& t : first) {
        if (t.frame < kBar) one.push_back(t);
        else if (t.frame >= 2L * kBar && t.frame < 3L * kBar) {
            two.push_back(t);
            two.back().frame -= 2L * kBar;
        }
    }
    CHECK(one.size() > 20 && !(one == two));
}

// The real instruments play it: sound comes out, the same to the last bit on any number of
// helpers and from one run to the next (no voice is given away differently).
static std::vector<float> realSong(int helpers, bool effects) {
    std::unique_ptr<Engine> engine(new Engine);
    engine->useWorkers(helpers);
    const SynthKind kinds[] = {SynthKind::Phazed, SynthKind::Twin, SynthKind::Sampler, SynthKind::Qmod};
    Rig* none = nullptr;
    (void)none;
    for (int t = 1; t <= 4; ++t) {
        CHECK(engine->addSynthTrack(t, kinds[t - 1]));
        if (kinds[t - 1] == SynthKind::Sampler) {
            std::unique_ptr<SampleData> tone(new SampleData);
            for (int i = 0; i < 24000; ++i) tone->data.push_back(static_cast<float>(0.5 * std::sin(i * 0.03) * (1.0 - i / 24000.0)));
            CHECK(engine->setTrackSample(t, std::move(tone)));
        }
        const std::vector<int> keys = {48 + t, 52 + t, 55 + t, 59 + t};
        for (int which = 0; which < 3; ++which) {
            ChordPlay play = !effects ? ChordPlay() : which == 0 ? playOf(kStrumWide, (t - 1) % 4, 100, 100, 100) : which == 1 ? playOf(kStrumQuarter + (t % 3), t % 4, 50, 50, 50) : playOf(kStrumTight, kOrderRandom, 100, 0, 100);
            for (std::size_t k = 0; k < keys.size(); ++k) {
                Command c = make(Command::SetNote, t, which * 4 + static_cast<int>(k), which * 5, 0.8f);
                c.c = keys[k] + which * 2;
                c.d = which == 2 ? 2 : 4;
                c.e = which == 2 ? 4 : 0; // the last: ratcheted x4 and strummed
                c.play = play;
                c.play.index = static_cast<std::uint8_t>(k);
                c.play.count = 4;
                CHECK(engine->send(c));
            }
        }
    }
    CHECK(engine->send(make(Command::Play, 0, 0, 1)));
    std::vector<float> out(static_cast<std::size_t>(2 * 3 * kBar));
    for (std::size_t at = 0; at < out.size(); at += 512) engine->render(out.data() + at, static_cast<int>(std::min<std::size_t>(256, (out.size() - at) / 2)));
    return out;
}
static void testTheRealInstruments() {
    const std::vector<float> first = realSong(0, true);
    double loud = 0.0;
    for (float v : first) {
        CHECK(std::isfinite(v));
        loud += static_cast<double>(v) * v;
    }
    CHECK(loud / static_cast<double>(first.size()) > 1e-5);
    CHECK(realSong(0, true) == first); // one run and the next
    for (int helpers = 1; helpers <= 3; ++helpers) CHECK(realSong(helpers, true) == first);
    CHECK(!(realSong(0, false) == first)); // and it is not what the plain chords sound like
}

// Each instrument alone is given exactly the notes the listening synth is: the effects are the
// engine's, not a synth's.
static void testOnAnyTrack() {
    Rig a(1), b(7);
    fxSong(a, 1);
    fxSong(b, 7);
    a.play();
    b.play();
    a.run(2L * kBar);
    b.run(2L * kBar);
    CHECK(a.log.size() > 100 && b.log.size() > 100);
    CHECK(!(a.log == b.log)); // the rolls are the track's own: two tracks do not move as one
}

// ---- Stop, Play, panic, tempo, the scene's length ---------------------------------------------------

static void testStopAndPlay() {
    const std::vector<int> keys = {60, 64, 67, 71};
    for (int strum : {kStrumWide, kStrumStep}) {
        for (int how = 0; how < 3; ++how) {
            Rig rig;
            rig.chord(0, 2, keys, 6.f, playOf(strum, 0, 0, 100, 0));
            rig.play();
            rig.run(2 * kStep + (strum == kStrumWide ? kWide + 100 : kStep + 100)); // part-way through the strum
            const std::size_t before = rig.log.size();
            CHECK(before >= 2 && before < 5);
            if (how == 0) rig.send(make(Command::Stop));
            if (how == 1) rig.send(make(Command::Panic, 0, 1));
            if (how == 2) rig.play();
            rig.run(how == 2 ? 2 * kStep - 10 : 4 * kBar);
            // Everything was let go, and nothing that was waiting came after.
            CHECK(rig.log.size() == before + 1 && rig.log.back().pitch == -1 && !rig.log.back().on);
            if (how == 1) rig.send(make(Command::Panic, 0, 0));
            if (how != 2) {
                rig.run(kBar);
                CHECK(rig.log.size() == before + 1);
                rig.play();
                rig.run(2 * kStep - 10);
                CHECK(rig.log.size() == before + 2 && rig.log.back().pitch == -1);
            }
            // From the top it is the first performance again.
            rig.log.clear();
            rig.run(strum == kStrumWide ? kWide + 110 : kStep + 110);
            Rig fresh;
            fresh.chord(0, 2, keys, 6.f, playOf(strum, 0, 0, 100, 0));
            fresh.play();
            fresh.run(2 * kStep + (strum == kStrumWide ? kWide + 100 : kStep + 100));
            std::vector<Told> again, first;
            for (const Told& t : rig.log)
                if (t.on) again.push_back(t);
            for (const Told& t : fresh.log)
                if (t.on) first.push_back(t);
            CHECK(again.size() == first.size() && first.size() == 2);
            for (std::size_t i = 0; i < again.size() && i < first.size(); ++i)
                CHECK(again[i].pitch == first[i].pitch && again[i].velocity == first[i].velocity && again[i].frame - again[0].frame == first[i].frame - first[0].frame);
        }
    }
    // A track taken away mid-arpeggio and given a new synth: nothing of the old chord plays on it.
    {
        Rig rig;
        rig.chord(0, 0, keys, 16.f, playOf(kStrumStep));
        rig.play();
        rig.run(3 * kStep + 100);
        rig.send(make(Command::RemoveInstrument, 1));
        rig.run(1000);
        rig.engine->collectGarbage();
        std::vector<Told> second;
        second.reserve(1000);
        Command c = make(Command::InstallSynth, 1);
        c.pointer = new Spy(&second);
        rig.send(c);
        rig.run(kBar);
        CHECK(second.empty());
    }
}

static void testTempoMidChord() {
    // An arpeggio goes with the tempo: half the speed from the change on, and it ends with the chord.
    Rig rig;
    rig.chord(0, 0, {60, 64, 67}, 8.f, playOf(kStrumStep));
    rig.play();
    rig.run(2 * kStep + 3000); // half-way through the third step
    rig.send(make(Command::SetTempo, 0, 0, 0, 60.f));
    rig.run(40 * kStep);
    const std::vector<Sounded> n = rig.notes();
    CHECK(n.size() >= 8);
    if (n.size() >= 8) {
        CHECK(n[0].on == 0 && n[1].on == kStep && n[2].on == 2 * kStep);
        // What was left of the third step is twice as long now, and so is every step after.
        for (std::size_t m = 3; m < 8; ++m) CHECK(near(n[m].on, 2 * kStep + 3000 + 6000 + static_cast<long>(m - 3) * 2 * kStep));
        CHECK(n[7].off >= 0 && near(n[7].off, n[7].on + 2 * kStep, 2));
    }
    // No note is left sounding by a change in the middle of a strum either.
    Rig strum;
    strum.chord(0, 0, {60, 64, 67, 71}, 2.f, playOf(kStrumWide, 0, 100, 100, 100));
    strum.play();
    strum.run(kWide + 50);
    strum.send(make(Command::SetTempo, 0, 0, 0, 200.f));
    strum.run(30000); // (not into the next time round)
    const std::vector<Sounded> s = strum.notes();
    CHECK(s.size() == 4);
    for (const Sounded& one : s) CHECK(one.off > one.on);
}

static void testTheScenesLength() {
    // A scene cut to eight steps: the chord on step 5 plays every eight, the one on step 13 never.
    const std::vector<int> keys = {60, 64, 67};
    Rig rig;
    Command cut = make(Command::SetSceneSteps, 0, 8);
    rig.send(cut);
    rig.chord(0, 4, keys, 2.f, playOf(kStrumTight, 0, 100, 0, 0));
    rig.chord(4, 12, {72, 76, 79}, 2.f, playOf(kStrumHalf, 0, 100, 0, 0));
    rig.chord(8, 0, {40, 43}, 1.f, playOf(0, 0, 100, 0, 0)); // on the top: early over the cut's own line
    rig.play();
    rig.run(20L * 8 * kStep);
    const std::vector<Sounded> n = rig.notes();
    int low = 0, mid = 0, high = 0, early = 0;
    for (const Sounded& s : n) {
        low += s.pitch < 50;
        mid += s.pitch >= 60 && s.pitch < 70;
        high += s.pitch >= 72;
        if (s.pitch < 50) {
            const long from = (s.on + 4 * kStep) % (8 * kStep) - 4 * kStep;
            CHECK(std::labs(from) <= kHuman + 1);
            early += from < 0;
        } else if (s.pitch < 70) {
            const long k = std::find(keys.begin(), keys.end(), s.pitch) - keys.begin();
            CHECK(std::labs((s.on % (8 * kStep)) - 4 * kStep - k * kTight) <= kHuman + 1);
        }
    }
    CHECK(high == 0 && mid == 60 && low >= 40 && low <= 42 && early > 8);
}

// ---- THROW: the whole chord, for its whole length ---------------------------------------------------

struct Probe : Effect {
    std::vector<float> fed;
    Probe() { fed.reserve(48000 * 20); }
    void setParam(int, float) override {}
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) {
            if (fed.size() < fed.capacity()) fed.push_back(left[i]);
            left[i] = right[i] = 0.f;
        }
    }
};
struct Steady : Effect { // a steady level in place of whatever the track's instrument made
    void setParam(int, float) override {}
    void process(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.5f;
    }
};
// The frames (from Play) the ECHO bus was first and last given the track in full.
static void thrownSpan(ChordPlay play, int ratchet, long* opened, long* shut, float nudge = 0.f) {
    std::unique_ptr<Engine> engine(new Engine);
    Probe* probe = new Probe;
    engine->setBusEffect(0, std::unique_ptr<Effect>(probe));
    CHECK(engine->addSynthTrack(1, SynthKind::Phazed));
    engine->setTrackEffect(1, 0, std::unique_ptr<Effect>(new Steady));
    const int keys[] = {60, 64, 67, 71};
    for (int k = 0; k < 4; ++k) {
        Command c = make(Command::SetNote, 1, k, 4, 0.8f);
        c.c = keys[k];
        c.d = 4;
        c.e = (ratchet >= 2 ? ratchet : 0) + (kThrowEcho << 9);
        c.shift = nudge;
        c.play = play;
        c.play.index = static_cast<std::uint8_t>(k);
        c.play.count = 4;
        CHECK(engine->send(c));
    }
    CHECK(engine->send(make(Command::Play, 0, 0, 1)));
    std::vector<float> out(2 * 500);
    const long total = kBar - 2000;
    for (long left = total; left > 0; left -= 500) engine->render(out.data(), 500);
    const long woke = total - static_cast<long>(probe->fed.size());
    *opened = *shut = -1;
    const float full = 0.5f * 0.65f;
    for (std::size_t i = 0; i < probe->fed.size(); ++i) {
        if (std::fabs(probe->fed[i] - full) < 1e-4f) {
            if (*opened < 0) *opened = woke + static_cast<long>(i);
            *shut = woke + static_cast<long>(i);
        } else if (*opened >= 0 && *shut == woke + static_cast<long>(i) - 1 && i + 300 < probe->fed.size()) {
            // Once open it stays open until it shuts for good: no gap between the notes.
            bool again = false;
            for (std::size_t j = i; j < probe->fed.size(); ++j) again = again || std::fabs(probe->fed[j] - full) < 1e-4f;
            CHECK(!again);
        }
    }
}
static void testThrow() {
    long plainOpen = 0, plainShut = 0;
    thrownSpan(ChordPlay(), 0, &plainOpen, &plainShut);
    CHECK(near(plainOpen, 4 * kStep + 239, 2) && near(plainShut, 8 * kStep, 2)); // 5 ms to open, shut from the chord's end
    struct Case { ChordPlay play; int ratchet; };
    const Case cases[] = {
        {playOf(kStrumWide), 0}, {playOf(kStrumWide, kOrderDown, 0, 0, 100), 0}, {playOf(kStrumTight), 4}, {playOf(0, 0, 0, 100, 100), 0},
        {playOf(kStrumStep, 0, 0, 0, 100), 0}, {playOf(kStrumQuarter, kOrderRandom, 0, 100, 100), 0}, {playOf(0, 0, 0, 0, 100), 3},
    };
    for (const Case& c : cases) {
        long opened = 0, shut = 0;
        thrownSpan(c.play, c.ratchet, &opened, &shut);
        CHECK(near(opened, plainOpen, 2)); // from its first note...
        CHECK(near(shut, plainShut, 2));   // ...to the chord's end, however short LENGTHS leaves its notes
    }
    // With HUMAN TIME the first note may be early or late, and the send with it: never past the chord's end.
    long opened = 0, shut = 0;
    thrownSpan(playOf(kStrumWide, 0, 100, 0, 100), 0, &opened, &shut);
    CHECK(std::labs(opened - plainOpen) <= kHuman + 2 && shut <= plainShut + 2 && shut >= plainShut - 2);
}

// ---- GLIDE: a chord is one moment, however it is spread ----------------------------------------------

static void testGlideAfterAChord() {
    const std::vector<int> keys = {60, 64, 67};
    const ChordPlay plays[] = {ChordPlay(), playOf(kStrumWide), playOf(kStrumWide, kOrderDown), playOf(kStrumTight, kOrderRandom, 100, 100, 100), playOf(0, 0, 100),
                               playOf(kStrumStep), playOf(kStrumHalf, kOrderDown, 100, 0, 100), playOf(kStrumQuarter, kOrderRandom)};
    // What the roll draws: the rule's own answer for these notes.
    GlideNote cell[kGlideNotes];
    for (int k = 0; k < 3; ++k) {
        cell[k].sounds = true;
        cell[k].step = 4;
        cell[k].pitch = keys[static_cast<std::size_t>(k)];
    }
    // Loose notes, in the order they begin, all BELOW the chord's keys: a chord's note that told
    // the trail of itself when it sounded (late in a strum, or round an arpeggio) would take the
    // place of the loose note before it there.
    const struct { int step; float nudge; int pitch, glide; } after[] = {{3, 0.f, 46, kGlideHalf}, {4, 0.1f, 40, kGlideFull}, {4, 0.3f, 42, kGlideFull}, {5, 0.5f, 44, kGlideFull}, {7, 0.5f, 48, kGlideOne}};
    const int kLoose = 5;
    for (int i = 0; i < kLoose; ++i) {
        GlideNote& n = cell[3 + i];
        n.sounds = true;
        n.step = after[i].step;
        n.nudge = after[i].nudge;
        n.pitch = after[i].pitch;
        n.glide = after[i].glide;
    }
    GlideLine lines[kGlideNotes];
    glideLines(cell, 1, 16, lines);
    CHECK(lines[4].from == 67 && std::fabs(lines[4].steps - 0.1) < 1e-6); // a note inside the strum: from the chord's top key
    CHECK(lines[3].from == 48 && lines[5].from == 40 && lines[6].from == 42 && lines[7].from == 44);
    std::vector<Told> plain;
    for (std::size_t which = 0; which < sizeof plays / sizeof plays[0]; ++which) {
        Rig rig;
        rig.chord(0, 4, keys, 4.f, plays[which]);
        for (int i = 0; i < kLoose; ++i) rig.note(3 + i, after[i].step, after[i].pitch, 0.25f, after[i].glide, after[i].nudge);
        rig.play();
        rig.run(4L * kBar);
        std::vector<Told> glides; // the loose notes, as they began
        for (const Told& t : rig.log)
            if (t.on && t.pitch < 60) glides.push_back(t);
        CHECK(glides.size() == 4 * kLoose);
        if (which == 0) plain = glides;
        CHECK(glides == plain); // exactly what they do after a chord that sounds all at once
        // And that is what the roll draws, once the song has gone round.
        for (std::size_t g = kLoose; g < glides.size(); ++g) {
            const std::size_t i = g % kLoose;
            const int slot = 3 + static_cast<int>(i);
            CHECK(glides[g].pitch == after[i].pitch && glides[g].from == lines[slot].from);
            CHECK(std::fabs(glides[g].seconds - lines[slot].steps * 0.125) < 1e-6);
        }
        // No note of the chord itself slides.
        for (const Told& t : rig.log)
            if (t.on && t.pitch >= 60) CHECK(t.from == -1);
    }
}

// A chord asked for a step ahead (HUMAN TIME) is not yet the last thing the track began: a note
// that begins between the asking and the chord's own moment slides from the note before it,
// not from the chord's top key.
static void testGlideBeforeAnAskedChord() {
    for (const ChordPlay& play : {playOf(0, 0, 100), playOf(kStrumWide, 0, 50), playOf(kStrumHalf, 0, 50)}) {
        Rig rig;
        rig.note(0, 2, 40, 0.5f);
        rig.note(1, 3, 43, 0.1f, 0, 0.2f);           // after the chord was asked for
        rig.note(2, 3, 80, 0.25f, kGlideFull, 0.5f); // half a step before the chord, above its top key: from the note before
        rig.chord(3, 4, {60, 64, 67}, 2.f, play);
        rig.note(6, 4, 50, 0.25f, kGlideFull, 0.4f); // and one inside it: from its top key
        rig.play();
        rig.run(3L * kBar);
        int seen = 0, inside = 0;
        for (const Told& t : rig.log) {
            if (t.on && t.pitch == 80) {
                ++seen;
                CHECK(t.from == 43);
            }
            if (t.on && t.pitch == 50) {
                ++inside;
                CHECK(t.from == 67);
            }
        }
        CHECK(seen == 3 && inside == 3);
    }
}

// ---- what a command may ask ----------------------------------------------------------------------------

static void testCommands() {
    // Nonsense in a command's play values is off, not a crash; a key that is not one of the chord's is a plain note.
    Rig rig;
    for (int k = 0; k < 3; ++k) {
        Command c = make(Command::SetNote, 1, k, 2, 0.8f);
        c.c = 60 + 4 * k;
        c.d = 2;
        c.play.strum = 200;
        c.play.order = 200;
        c.play.time = 255;
        c.play.level = 0;
        c.play.lengths = 0;
        c.play.index = static_cast<std::uint8_t>(k == 2 ? 9 : k); // the third says it is the tenth of three
        c.play.count = 3;
        rig.send(c);
    }
    rig.play();
    rig.run(30L * kBar);
    const std::vector<Sounded> n = rig.notes();
    CHECK(n.size() == 90);
    long far = 0;
    for (const Sounded& s : n) {
        const long by = (s.on - 2 * kStep + kBar / 2) % kBar - kBar / 2;
        if (s.pitch == 68) CHECK(by == 0); // a plain note: on the step
        else far = std::max(far, std::labs(by));
    }
    CHECK(far <= kHuman + 1 && far > kHuman / 2); // time 255 is 100%, strum 200 is none
    // A whole track handed over (a song loading) carries the values too.
    std::unique_ptr<TrackImage> image(new TrackImage);
    for (int k = 0; k < 3; ++k) {
        TrackImage::Note& note = image->notes[0][k];
        note.step = 2;
        note.pitch = static_cast<std::uint8_t>(60 + 4 * k);
        note.length = 2.f;
        note.velocity = 0.8f;
        note.play = playOf(kStrumWide);
        note.play.index = static_cast<std::uint8_t>(k);
        note.play.count = 3;
    }
    Rig whole;
    CHECK(whole.engine->setTrackImage(1, std::move(image)));
    whole.play();
    whole.run(kBar);
    whole.engine->collectGarbage();
    const std::vector<Sounded> w = whole.notes();
    CHECK(w.size() == 3);
    for (std::size_t k = 0; k < w.size(); ++k) CHECK(w[k].on == 2 * kStep + static_cast<long>(k) * kWide);
    // ...and nonsense in it is off, as in a command.
    {
        std::unique_ptr<TrackImage> odd(new TrackImage);
        for (int k = 0; k < 3; ++k) {
            TrackImage::Note& note = odd->notes[0][k];
            note.step = 2;
            note.pitch = static_cast<std::uint8_t>(60 + 4 * k);
            note.length = 2.f;
            note.velocity = 0.8f;
            note.play.strum = 200;
            note.play.order = 200;
            note.play.time = 255;
            note.play.index = static_cast<std::uint8_t>(k == 2 ? 9 : k);
            note.play.count = 3;
        }
        Rig rig;
        CHECK(rig.engine->setTrackImage(1, std::move(odd)));
        rig.play();
        rig.run(30L * kBar);
        rig.engine->collectGarbage();
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 90);
        long far = 0;
        for (const Sounded& s : n) {
            const long by = (s.on - 2 * kStep + kBar / 2) % kBar - kBar / 2;
            if (s.pitch == 68) CHECK(by == 0);
            else far = std::max(far, std::labs(by));
        }
        CHECK(far <= kHuman + 1 && far > kHuman / 2);
    }
    // A note emptied takes its chord values with it: the slot's next note is plain.
    Rig again;
    again.chord(0, 2, {60, 64, 67}, 2.f, playOf(kStrumWide));
    for (int k = 0; k < 3; ++k) again.send(make(Command::SetNote, 1, k, 0, 0.f));
    for (int k = 0; k < 3; ++k) again.note(k, 2, 60 + 4 * k, 2.f);
    again.play();
    again.run(kBar);
    for (const Sounded& s : again.notes()) CHECK(s.on == 2 * kStep);
    CHECK(again.notes().size() == 3);
}

// ---- what the reviews found ----------------------------------------------------------------------------

// A song played ONCE (a bounce: Play with c = 1) begins nothing of its start before its end. Asked a
// step ahead across the song's end, the first chord's early keys sounded in the bounce's last 12 ms.
static void testOnceThrough() {
    const std::vector<int> keys = {60, 64, 67, 71};
    for (int time : {10, 100}) {
        for (int once = 0; once < 2; ++once) {
            Rig rig;
            rig.chord(0, 0, keys, 2.f, playOf(0, 0, time));
            Command play = make(Command::Play);
            play.c = once;
            rig.send(play);
            rig.run(kBar, 256); // the song is one bar, and this is all of it
            int begun = 0, early = 0;
            for (const Told& t : rig.log) begun += t.on, early += t.on && t.frame > kBar / 2;
            if (once) CHECK(begun == 4 && early == 0);
            else CHECK(begun > 4 && early == begun - 4); // going round, the next time's early keys are there
            rig.send(make(Command::Stop));
            rig.run(4000, 256);
            int after = 0;
            for (const Told& t : rig.log) after += t.on;
            CHECK(after == begun);
        }
    }
    // The same for a plain note and a drum hit nudged early on the song's first step.
    for (int once = 0; once < 2; ++once) {
        Rig rig;
        rig.note(0, 0, 60, 1.f, 0, -0.25f);
        Command play = make(Command::Play);
        play.c = once;
        rig.send(play);
        rig.run(kBar, 256);
        int begun = 0;
        for (const Told& t : rig.log) begun += t.on;
        CHECK(begun == (once ? 1 : 2));
    }
    // (a drum hit, by ear: the rim shot is there again in the last quarter step, or it is not)
    {
        float last[2] = {0.f, 0.f};
        for (int once = 0; once < 2; ++once) {
            std::unique_ptr<Engine> engine(new Engine);
            CHECK(engine->send(make(Command::SetStep, 0, 6, 0, 1.f)));
            CHECK(engine->send(make(Command::SetStepNudge, 0, 6, 0, -0.25f)));
            Command play = make(Command::Play);
            play.c = once;
            CHECK(engine->send(play));
            std::vector<float> out(2 * 256);
            for (long at = 0; at < kBar; at += 256) {
                engine->render(out.data(), 256);
                if (at >= kBar - 1024)
                    for (float v : out) last[once] = std::max(last[once], std::fabs(v));
            }
        }
        CHECK(last[0] > 0.05f && last[1] < last[0] * 0.01f);
    }
    // Once through is all of it: a scene that plays twice has its second time's early keys...
    {
        Rig rig;
        Command twice = make(Command::SetSceneRepeats, 0, 2);
        twice.scene = 0;
        rig.send(twice);
        rig.chord(0, 0, keys, 2.f, playOf(0, 0, 100));
        Command play = make(Command::Play);
        play.c = 1;
        rig.send(play);
        rig.run(2L * kBar, 256);
        int begun = 0, early = 0;
        for (const Told& t : rig.log) begun += t.on, early += t.on && t.frame > kBar / 2 && t.frame < kBar;
        CHECK(begun == 8 && early >= 1);
    }
    // ...and a cell of two bars its second bar's.
    {
        Rig rig;
        rig.send(make(Command::SetCellBars, 1, 2));
        rig.chord(0, 0, keys, 2.f, playOf(0, 0, 100), 0, 0, 0.f, 1);
        Command play = make(Command::Play);
        play.c = 1;
        rig.send(play);
        rig.run(2L * kBar, 256);
        int begun = 0, early = 0;
        for (const Told& t : rig.log) begun += t.on, early += t.on && t.frame < kBar;
        CHECK(begun == 4 && early >= 1);
    }
    // Looping one scene is not "once", whatever the command says.
    {
        Rig rig;
        rig.chord(0, 0, keys, 2.f, playOf(0, 0, 100));
        Command play = make(Command::Play, 0, 0, 1);
        play.c = 1;
        rig.send(play);
        rig.run(kBar, 256);
        int begun = 0;
        for (const Told& t : rig.log) begun += t.on;
        CHECK(begun > 4);
    }
    // Once through is the whole song: two scenes, the second's early chord is not lost.
    {
        Rig rig;
        rig.send(make(Command::SetSceneCount, 0, 2));
        rig.chord(0, 0, keys, 2.f, playOf(0, 0, 100));
        for (std::size_t k = 0; k < keys.size(); ++k) {
            Command c = make(Command::SetNote, 1, static_cast<int>(k), 0, 0.8f);
            c.scene = 1;
            c.c = keys[k] - 12;
            c.d = 2;
            c.play = playOf(0, 0, 100);
            c.play.index = static_cast<std::uint8_t>(k);
            c.play.count = 4;
            rig.send(c);
        }
        Command play = make(Command::Play);
        play.c = 1;
        rig.send(play);
        rig.run(2L * kBar, 256);
        int first = 0, second = 0, early = 0;
        for (const Told& t : rig.log) {
            if (!t.on) continue;
            ++(t.pitch >= 60 ? first : second);
            early += t.pitch < 60 && t.frame < kBar;
        }
        CHECK(first == 4 && second == 4 && early >= 1);
    }
}

// A ratcheted chord plays as many hits as it does at rest, on every key, every time: LENGTHS, a
// strum and HUMAN TIME neither lose one (the last piece running out of chord) nor add one (an
// early key finding room for a piece more).
static void testRatchetKeepsItsHits() {
    const std::vector<int> keys = {60, 64, 67, 71};
    const struct { float length; int ratchet; ChordPlay play; float tempo; int hits; } cases[] = {
        {1.f, 4, playOf(0, 0, 0, 0, 100), 120.f, 4}, {1.f, 4, playOf(0, 0, 0, 0, 50), 120.f, 4},  {1.f, 3, playOf(0, 0, 0, 0, 100), 120.f, 3},
        {1.f, 2, playOf(0, 0, 0, 0, 100), 120.f, 2}, {1.f, 4, playOf(kStrumWide, 0, 100), 120.f, 4}, {1.f, 4, playOf(kStrumWide, 0, 100), 174.f, 4},
        {0.5f, 2, playOf(0, 0, 100), 174.f, 1},      {0.5f, 2, playOf(0, 0, 50), 240.f, 1},        {0.5f, 4, playOf(kStrumTight, kOrderRandom, 100, 100, 100), 150.f, 2},
        {2.f, 4, playOf(kStrumWide, kOrderDown, 100, 100, 100), 120.f, 4},
    };
    for (const auto& c : cases) {
        const int loops = 300;
        const double step = 48000.0 * 60.0 / c.tempo / 4.0, bar = 16.0 * step;
        Rig rig;
        rig.send(make(Command::SetTempo, 0, 0, 0, c.tempo));
        rig.chord(0, 6, keys, c.length, c.play, c.ratchet);
        { // at rest, for the count: the same chord with nothing set
            Rig rest;
            rest.send(make(Command::SetTempo, 0, 0, 0, c.tempo));
            rest.chord(0, 6, keys, c.length, ChordPlay(), c.ratchet);
            rest.play();
            rest.run(static_cast<long>(bar));
            CHECK(static_cast<int>(rest.notes().size()) == 4 * c.hits);
        }
        rig.play();
        rig.run(static_cast<long>(loops * bar));
        std::vector<int> hits(static_cast<std::size_t>(loops * 4), 0);
        const std::vector<Sounded> n = rig.notes();
        int wrong = 0;
        for (const Sounded& s : n) {
            const int loop = static_cast<int>(s.on / bar), key = static_cast<int>(std::find(keys.begin(), keys.end(), s.pitch) - keys.begin());
            if (loop < loops && key < 4) ++hits[static_cast<std::size_t>(loop * 4 + key)];
            // Nothing sounds past the chord's end (and a hair: the shortest a note may be).
            const double end = loop * bar + (6.0 + c.length + kShortestNote) * step + 2.0;
            if ((s.off < 0 || s.off > end) && getenv("DBG")) std::printf("len %g x%d tempo %g: on %ld off %ld end %g loop %d\n", c.length, c.ratchet, c.tempo, s.on, s.off, end, loop);
            wrong += s.off < 0 || s.off > end;
        }
        int odd = 0;
        for (int h : hits) odd += h != c.hits;
        CHECK(odd == 0);
        CHECK(wrong == 0);
        // A key is let go before it is struck again.
        for (std::size_t i = 0; i < n.size(); ++i)
            for (std::size_t j = i + 1; j < n.size() && j < i + 8; ++j)
                if (n[j].pitch == n[i].pitch) {
                    CHECK(n[i].off <= n[j].on);
                    break;
                }
    }
    // LENGTHS on a ratcheted chord is the last piece let go sooner: different every time, never
    // under half of it, and the pieces before it as they always are.
    {
        Rig rig;
        rig.chord(0, 6, keys, 2.f, playOf(0, 0, 0, 0, 100), 2);
        rig.play();
        rig.run(200L * kBar);
        const std::vector<Sounded> n = rig.notes();
        CHECK(n.size() == 1600);
        long shortest = 2 * kStep, longest = 0;
        for (std::size_t i = 0; i < n.size(); ++i) {
            const long held = n[i].off - n[i].on, at = n[i].on % kBar;
            if (at == 6 * kStep) {
                CHECK(held == static_cast<long>(std::ceil(kStep * 0.5 * 0.85f))); // the first piece: 85% of its half step
            } else {
                CHECK(at == 6 * kStep + kStep / 2 && held <= 3 * kStep / 2 + 1 && held >= 3 * kStep / 4 - 1);
                shortest = std::min(shortest, held);
                longest = std::max(longest, held);
            }
        }
        CHECK(shortest < 3 * kStep / 4 + 300 && longest > 3 * kStep / 2 - 300);
    }
}

// Three arpeggios with their clocks all alive at once: one still playing (it was nudged late),
// one waiting for its nudge, one asked a step ahead for its HUMAN TIME. None takes another's.
static void testThreeArpeggios() {
    Rig rig;
    rig.chord(0, 0, {60, 64, 67}, 2.f, playOf(kStrumQuarter), 0, 0, 0.3f);
    rig.chord(3, 2, {50, 54, 57}, 1.f, playOf(kStrumQuarter), 0, 0, 0.5f);
    rig.chord(6, 3, {40, 44, 47}, 1.f, playOf(kStrumQuarter, 0, 50));
    rig.play();
    rig.run(10L * kBar);
    int first[10] = {}, third[10] = {};
    for (const Sounded& s : rig.notes()) {
        const int loop = static_cast<int>(s.on / kBar);
        if (loop >= 10) continue;
        if (s.pitch >= 60) ++first[loop];
        if (s.pitch < 50) ++third[loop];
    }
    for (int loop = 0; loop < 10; ++loop) CHECK(first[loop] == 8 && third[loop] == 4); // every note of the first, to its end
}

// A chord's values turned WHILE THE SONG PLAYS (the ordinary gesture: X held, left / right, the
// dice), in the step before the chord: that playing has each key once. Which of the two askings
// takes a chord was read from its values at each, so one changed between them was played twice
// or not at all.
static void testTurnedWhilePlaying() {
    const std::vector<int> keys = {60, 64, 67};
    const struct { ChordPlay from; float nudgeFrom; ChordPlay to; float nudgeTo; } cases[] = {
        {ChordPlay(), 0.f, playOf(0, 0, 10), 0.f},           // HUMAN TIME on
        {playOf(0, 0, 10), 0.f, ChordPlay(), 0.f},           // ...and off
        {playOf(0, 0, 0, 50), 0.f, playOf(0, 0, 25, 50), 0.f}, // with another effect set all along
        {playOf(0, 0, 25, 50), 0.f, playOf(0, 0, 0, 50), 0.f},
        {ChordPlay(), -0.2f, playOf(0, 0, 10), -0.2f},       // a chord nudged early, at rest, then not
        {playOf(0, 0, 10), -0.2f, ChordPlay(), -0.2f},
        {playOf(kStrumTight), 0.f, playOf(kStrumTight), -0.2f}, // nudged early while it plays
        {playOf(kStrumTight), -0.2f, playOf(kStrumTight), 0.f},
        {playOf(kStrumHalf), 0.f, playOf(kStrumHalf, 0, 25), 0.f}, // an arpeggio: twice over or none at all
        {playOf(kStrumHalf, 0, 25), 0.f, playOf(kStrumHalf), 0.f},
    };
    for (const auto& c : cases) {
        Rig rig;
        rig.chord(0, 4, keys, 1.5f, c.from, 0, 0, c.nudgeFrom);
        rig.play();
        rig.run(kBar + 3 * kStep + 2500); // the second time round, part way into the step before it
        const std::size_t before = rig.notes().size();
        CHECK(before == 3);
        rig.chord(0, 4, keys, 1.5f, c.to, 0, 0, c.nudgeTo);
        rig.run(13 * kStep - 2500 - 1000);
        CHECK(rig.notes().size() == before + 3);
        rig.run(1000 + 2L * kBar);
        CHECK(rig.notes().size() == before + 9); // and from then on, as ever
        for (const Sounded& s : rig.notes()) CHECK(s.off > s.on);
    }
}

// ...and what was asked in one playing is not remembered in the next: Stop, the value turned, Play.
static void testTurnedWhileStopped() {
    const std::vector<int> keys = {60, 64, 67};
    Rig rig;
    rig.chord(0, 1, keys, 1.5f, playOf(0, 0, 10));
    rig.play();
    rig.run(3 * kStep);
    CHECK(rig.notes().size() == 3);
    rig.send(make(Command::Stop));
    rig.run(1000);
    rig.chord(0, 1, keys, 1.5f, ChordPlay());
    rig.play();
    rig.run(3 * kStep);
    CHECK(rig.notes().size() == 6);
}

// An arpeggio ends with its chord. Deleted, carried off or set back to a plain chord while its
// arpeggio plays, it begins no more notes (they ran on to where the chord's end had been).
// Anything else written into the bar leaves it playing.
static void testAnArpeggioEndsWithItsChord() {
    const std::vector<int> keys = {60, 64, 67};
    const auto begunAfter = [](const Rig& rig, long frame) {
        int n = 0;
        for (const Told& t : rig.log) n += t.on && t.pitch >= 60 && t.frame > frame;
        return n;
    };
    for (int how = 0; how < 5; ++how) {
        Rig rig;
        rig.chord(1, 0, keys, 12.f, playOf(kStrumQuarter));
        rig.play();
        rig.run(2 * kStep + 100);
        CHECK(begunAfter(rig, -1) == 9);
        if (how == 0) { // deleted
            for (int k = 0; k < 3; ++k) rig.send(make(Command::SetNote, 1, 1 + k, 0, 0.f));
        } else if (how == 1) { // STRUM clicked to OFF
            rig.chord(1, 0, keys, 12.f, ChordPlay());
        } else if (how == 2) { // carried six steps down
            rig.chord(1, 6, keys, 4.f, playOf(kStrumQuarter));
        } else if (how == 3) { // a loose note written into the bar: the chord's keys are in other slots now
            rig.note(1, 9, 30, 1.f);
            rig.chord(2, 0, keys, 12.f, playOf(kStrumQuarter));
        } else { // HUMAN LEVEL turned on it: still the arpeggio it was
            rig.chord(1, 0, keys, 12.f, playOf(kStrumQuarter, 0, 0, 25));
        }
        rig.run(4 * kStep - 100 - 1);
        const int more = begunAfter(rig, 2 * kStep + 100);
        if (how <= 2) CHECK(more == 0);
        else CHECK(more == 15); // a quarter of a step apart, up to the sixth step
        rig.run(6 * kStep + 1);
        if (how == 2) CHECK(begunAfter(rig, 6 * kStep - 1) == 16); // where it was carried to, it plays whole
        for (const Sounded& s : rig.notes()) CHECK(s.off > s.on || s.on > 11 * kStep);
        // The next time round is as it is written.
        rig.run(4 * kStep + 100);
        const int round = begunAfter(rig, kBar - 1);
        CHECK(round == (how == 0 ? 0 : how == 1 ? 3 : how == 2 ? 0 : 1));
    }
    // One that has not begun yet (asked a step ahead) goes too.
    {
        Rig rig;
        rig.chord(1, 4, keys, 4.f, playOf(kStrumQuarter, 0, 25));
        rig.play();
        rig.run(3 * kStep + 100);
        for (int k = 0; k < 3; ++k) rig.send(make(Command::SetNote, 1, 1 + k, 0, 0.f));
        rig.run(8 * kStep);
        CHECK(begunAfter(rig, -1) == 0);
    }
}

int main(int argc, char** argv) {
    (void)kMs;
    struct Named { const char* name; void (*run)(); };
    const Named all[] = {
        {"testTheNumbers", testTheNumbers}, {"testAtRest", testAtRest}, {"testStrum", testStrum}, {"testStrumWithRatchet", testStrumWithRatchet},
        {"testArpeggio", testArpeggio}, {"testHumanTime", testHumanTime}, {"testHumanLevel", testHumanLevel}, {"testLengths", testLengths},
        {"testTheSameEveryTime", testTheSameEveryTime}, {"testTheRealInstruments", testTheRealInstruments}, {"testOnAnyTrack", testOnAnyTrack},
        {"testStopAndPlay", testStopAndPlay}, {"testTempoMidChord", testTempoMidChord}, {"testTheScenesLength", testTheScenesLength},
        {"testThrow", testThrow}, {"testGlideAfterAChord", testGlideAfterAChord}, {"testCommands", testCommands},
        {"testGlideBeforeAnAskedChord", testGlideBeforeAnAskedChord}, {"testOnceThrough", testOnceThrough}, {"testRatchetKeepsItsHits", testRatchetKeepsItsHits}, {"testThreeArpeggios", testThreeArpeggios},
        {"testTurnedWhilePlaying", testTurnedWhilePlaying}, {"testTurnedWhileStopped", testTurnedWhileStopped}, {"testAnArpeggioEndsWithItsChord", testAnArpeggioEndsWithItsChord},
    };
    for (const Named& t : all) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], t.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        t.run();
        if (failures != before) std::printf("  (in %s)\n", t.name);
    }
    if (failures == 0) std::printf("chordfx_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
