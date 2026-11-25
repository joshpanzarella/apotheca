// The FILL (fill_rule.hpp): a cell's one extra bar, kept in its eighth bar's place, played INSTEAD of
// the cell's own bar in the scene's last bar, on the scene's last time round. The engine's side.
//
// Nobody listens to these: they are measured. A lane is given a recording that is one sample of 1 and
// then silence, so every hit is one sample in the output: where it is says when the hit came. The
// cell's own bar and its FILL have their hits on other steps, so where a hit is says which played.
//
//   1. the rule by hand
//   2. a song: the FILL exactly in the scene's last bar of its last time round, never before, and the
//      cell's own bar again from the top; where it plays is told (playRepeat, playPass)
//   3. a FILL turned off, or never on: the cell's own bars, the same to the bit
//   4. a scene played alone counts its time rounds round and round: the FILL on the last of each REPEATS
//   5. a scene longer than the cell, cut short, or in 3/4: the FILL in the scene's last bar only
//   6. a cell of eight bars has none; a cell of four plays its FILL in place of its fourth bar
//   7. a synth's notes: its FILL bar's notes, in their place
//   8. automation: the FILL bar's steps of a lane
//   9. early hits: the FILL's asked a step ahead, and the bar after it asked from the FILL's last step
//  10. REPEATS or the FILL changed on the step before the FILL's bar: what was asked ahead is asked again
//  11. a track's image and a scene put in carry it; a FILL on an empty scene is given a store
//  12. two scenes: each its own
//  13. a slice's run begins in the FILL (FEEL and SWING do not look back into the bar before its place)

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "apotheca/engine.hpp"
#include "apotheca/fill_rule.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static const long kStep = 6000; // a step at 120 BPM, 48 kHz
static const long kBar = 16 * kStep;

static Command make(Command::Type type, int track = 0, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}

// Told the notes a synth track plays: which key, on which frame.
struct Told {
    long frame;
    int pitch;
};
struct Spy : Instrument {
    std::vector<Told>* log;
    long frame = 0;
    explicit Spy(std::vector<Told>* to) : log(to) {}
    void prepare(float) override {}
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int, float) override {}
    void noteOn(int note, float) override { log->push_back({frame, note}); }
    void noteOff(int) override {}
    void allNotesOff() override {}
    void render(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
        frame += frames;
    }
};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    std::vector<float> out; // the left side of everything rendered
    std::vector<int> repeatSeen, passSeen; // playRepeat and playPass at the middle of each bar rendered by bars()
    void send(const Command& c) { CHECK(engine->send(c)); }
    void render(long frames, int block = 256) {
        std::vector<float> both(static_cast<std::size_t>(2 * block));
        while (frames > 0) {
            const int n = frames < block ? static_cast<int>(frames) : block;
            engine->render(both.data(), n);
            for (int i = 0; i < n; ++i) out.push_back(both[static_cast<std::size_t>(2 * i)]);
            frames -= n;
        }
    }
    // `count` bars of 16 steps, each read at its middle: where the engine says it is.
    void bars(int count) {
        for (int i = 0; i < count; ++i) {
            render(kBar / 2);
            repeatSeen.push_back(engine->playRepeat());
            passSeen.push_back(engine->playPass());
            render(kBar / 2);
        }
    }
    // One sample of 1 and then nothing, on a lane (`whole`: 200 samples of 1, so a hit from part way in
    // sounds too: rising from nothing over a millisecond, its first sample silent).
    void click(int track, int lane, bool whole = false) {
        std::unique_ptr<SampleData> s(new SampleData);
        s->data.assign(4, 0.f);
        s->data[0] = 1.f;
        if (whole) s->data.assign(200, 1.f);
        CHECK(engine->setLaneSample(track, lane, std::move(s)));
    }
    void clicks(int track) {
        for (int lane = 0; lane < kDrumLanes; ++lane) click(track, lane);
    }
    void hit(int lane, int step, int bar = 0, float nudge = 0.f, int scene = 0, int track = 0) {
        Command c = make(Command::SetStep, track, lane, step, 0.8f);
        c.bar = bar;
        c.scene = scene;
        send(c);
        if (nudge != 0.f) {
            c = make(Command::SetStepNudge, track, lane, step, nudge);
            c.bar = bar;
            c.scene = scene;
            send(c);
        }
    }
    void fill(bool on, int scene = 0, int track = 0) {
        Command c = make(Command::SetCellFill, track, on ? 1 : 0);
        c.scene = scene;
        send(c);
    }
    void repeats(int count, int scene = 0) {
        Command c = make(Command::SetSceneRepeats, 0, count);
        c.scene = scene;
        send(c);
    }
    void cellBars(int bars, int scene = 0, int track = 0) {
        Command c = make(Command::SetCellBars, track, bars);
        c.scene = scene;
        send(c);
    }
    void play(bool loop = false) { send(make(Command::Play, 0, 0, loop ? 1 : 0)); }
    // Where the output is not silent.
    std::vector<long> onsets() const {
        std::vector<long> at;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (out[i] != 0.f) at.push_back(static_cast<long>(i));
        return at;
    }
    // Where each stretch of sound begins.
    std::vector<long> starts() const {
        std::vector<long> at;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (out[i] != 0.f && (i == 0 || out[i - 1] == 0.f)) at.push_back(static_cast<long>(i));
        return at;
    }
};

static void show(const char* what, const std::vector<long>& at) {
    std::printf("  %s:", what);
    for (long a : at) std::printf(" %ld (bar %ld step %.2f)", a, a / kBar, static_cast<double>(a % kBar) / kStep);
    std::printf("\n");
}
#define CHECK_ONSETS(rig, ...)                              \
    do {                                                    \
        const std::vector<long> want = {__VA_ARGS__};       \
        const std::vector<long> got = (rig).onsets();       \
        CHECK(got == want);                                 \
        if (got != want) show("heard", got), show("wanted", want); \
    } while (0)

// ---- 1 -------------------------------------------------------------------------------------------

static void testRule() {
    CHECK(kFillSlot == 7 && kFillBars == kMaxBars);
    // The last bar: the bar the scene's last step is in. 16 steps: bar 0. 64: bar 3. 23 (a bar and seven
    // steps): bar 1. Four bars of 3/4 (48 steps of 12): bar 3.
    CHECK(fillTime(0, 16, 16, 0, 1));
    CHECK(fillTime(3, 16, 64, 0, 1) && !fillTime(2, 16, 64, 0, 1) && !fillTime(0, 16, 64, 0, 1));
    CHECK(fillTime(1, 16, 23, 0, 1) && !fillTime(0, 16, 23, 0, 1));
    CHECK(fillTime(3, 12, 48, 0, 1) && !fillTime(2, 12, 48, 0, 1));
    CHECK(fillTime(1, 12, 24, 0, 1) && !fillTime(0, 12, 24, 0, 1) && fillTime(0, 12, 12, 0, 1));
    // The last time round: of REPEATS 4, the fourth (3 from 0); never the ones before. Past it (REPEATS
    // turned down under a loop's count): still the last.
    CHECK(fillTime(0, 16, 16, 3, 4) && !fillTime(0, 16, 16, 2, 4) && !fillTime(0, 16, 16, 0, 4));
    CHECK(fillTime(0, 16, 16, 5, 4));
    CHECK(!fillTime(2, 16, 64, 3, 4) && fillTime(3, 16, 64, 3, 4) && !fillTime(3, 16, 64, 2, 4));
    // Which bar of the cell plays: its own, round and round; its FILL where on and due.
    CHECK(cellBarOf(1, true, 0, true) == kFillSlot);
    CHECK(cellBarOf(1, true, 0, false) == 0 && cellBarOf(1, false, 0, true) == 0 && cellBarOf(1, false, 3, true) == 0);
    CHECK(cellBarOf(2, true, 3, true) == kFillSlot && cellBarOf(2, true, 3, false) == 1 && cellBarOf(2, false, 3, true) == 1);
    CHECK(cellBarOf(4, true, 3, true) == kFillSlot && cellBarOf(4, true, 5, false) == 1 && cellBarOf(4, true, 2, false) == 2);
    // A cell of eight bars: its own, every bar, its flag passed over.
    for (int bar = 0; bar < 8; ++bar) CHECK(cellBarOf(8, true, bar, true) == bar && cellBarOf(8, true, bar, false) == bar);
    CHECK(cellBarOf(7, true, 6, true) == kFillSlot); // (a cell of seven bars, which the app never makes, has room)
}

// ---- 2, 3 ----------------------------------------------------------------------------------------

// A cell of one bar: its own hit on step 1, its FILL's on step 5 (lane 2). REPEATS 4.
static void owner(Rig& rig, bool fillHits = true) {
    rig.clicks(0);
    rig.hit(0, 0);
    if (fillHits) rig.hit(1, 4, kFillSlot);
    rig.repeats(4);
}

static void testSong() {
    Rig rig;
    owner(rig);
    rig.fill(true);
    rig.play();
    rig.bars(6);
    // Bars 1 to 3 the cell's own, bar 4 (the fourth time round) the FILL, then the top again.
    CHECK_ONSETS(rig, 0, kBar, 2 * kBar, 3 * kBar + 4 * kStep, 4 * kBar, 5 * kBar);
    CHECK((rig.repeatSeen == std::vector<int>{0, 1, 2, 3, 0, 1}));
    CHECK((rig.passSeen == std::vector<int>{0, 0, 0, 0, 0, 0})); // (a song counts by REPEATS: no pass)
    // Played from the top again: the same.
    Rig again;
    owner(again);
    again.fill(true);
    again.play();
    again.render(kBar + 100);
    again.send(make(Command::Stop));
    again.render(1000);
    again.out.clear();
    again.play();
    again.bars(6);
    CHECK(again.out == rig.out);
}

static void testOffAndNone() {
    Rig off, none, bare;
    owner(off);
    owner(none);
    owner(bare, false);
    off.fill(true);
    off.fill(false); // made, turned off: kept, and not played
    for (Rig* rig : {&off, &none, &bare}) {
        rig->play();
        rig->bars(5);
    }
    CHECK_ONSETS(off, 0, kBar, 2 * kBar, 3 * kBar, 4 * kBar);
    CHECK(off.out == none.out && none.out == bare.out); // to the bit
}

// ---- 4 -------------------------------------------------------------------------------------------

static void testLoop() {
    Rig rig;
    owner(rig);
    rig.repeats(3);
    rig.fill(true);
    rig.play(true);
    rig.bars(7);
    CHECK_ONSETS(rig, 0, kBar, 2 * kBar + 4 * kStep, 3 * kBar, 4 * kBar, 5 * kBar + 4 * kStep, 6 * kBar);
    CHECK((rig.repeatSeen == std::vector<int>{0, 0, 0, 0, 0, 0, 0})); // REPEATS' lights and bars read it: as they always did
    CHECK((rig.passSeen == std::vector<int>{0, 1, 2, 0, 1, 2, 0}));
    // Stopped part way and played again: counted from the top again.
    Rig again;
    owner(again);
    again.repeats(3);
    again.fill(true);
    again.play(true);
    again.render(kBar + kBar / 2);
    again.send(make(Command::Stop));
    again.render(1000);
    again.out.clear();
    again.play(true);
    again.bars(3);
    CHECK_ONSETS(again, 0, kBar, 2 * kBar + 4 * kStep);
    // REPEATS 1: every time round is the last.
    Rig one;
    owner(one);
    one.repeats(1);
    one.fill(true);
    one.play(true);
    one.bars(3);
    CHECK_ONSETS(one, 4 * kStep, kBar + 4 * kStep, 2 * kBar + 4 * kStep);
    // REPEATS turned down at a bar line: the round after is counted by the new REPEATS (1 of 2: the last).
    Rig down;
    owner(down);
    down.fill(true);
    down.play(true);
    down.bars(2); // rounds 0 and 1 of 4
    down.repeats(2);
    down.bars(4); // round 0 (2 is past 2: round again), 1 (the last), 0, 1
    CHECK_ONSETS(down, 0, kBar, 2 * kBar, 3 * kBar + 4 * kStep, 4 * kBar, 5 * kBar + 4 * kStep);
    // ...and part way through round 2 of 4: that round is past the new count, so the last, from that step
    // on (the FILL's step 5 plays); round again after it.
    Rig past;
    owner(past);
    past.fill(true);
    past.play(true);
    past.render(2 * kBar + 2 * kStep);
    past.repeats(2);
    past.render(3 * kBar - 2 * kStep);
    CHECK_ONSETS(past, 0, kBar, 2 * kBar, 2 * kBar + 4 * kStep, 3 * kBar, 4 * kBar + 4 * kStep);
}

// ---- 5 -------------------------------------------------------------------------------------------

static void testLongerScenes() {
    {
        // Track 2's cell is four bars, so the scene is: track 1's one-bar cell goes round under it, and
        // its FILL plays in the scene's fourth bar, on its second time round, once.
        Rig rig;
        owner(rig);
        rig.repeats(2);
        CHECK(rig.engine->addDrumTrack(1));
        rig.cellBars(4, 0, 1);
        rig.fill(true);
        rig.play();
        rig.bars(9);
        CHECK_ONSETS(rig, 0, kBar, 2 * kBar, 3 * kBar, 4 * kBar, 5 * kBar, 6 * kBar, 7 * kBar + 4 * kStep, 8 * kBar);
    }
    {
        // Cut short to a bar and four steps: the FILL in the short last bar (its first four steps).
        Rig rig;
        rig.clicks(0);
        rig.hit(0, 0);
        rig.hit(1, 2, kFillSlot);
        rig.hit(1, 8, kFillSlot); // past the cut: never
        CHECK(rig.engine->addDrumTrack(1));
        rig.cellBars(2, 0, 1);
        Command cut = make(Command::SetSceneSteps, 0, 20);
        rig.send(cut);
        rig.fill(true);
        rig.play();
        rig.render(2 * 20 * kStep);
        CHECK_ONSETS(rig, 0, 18 * kStep, 20 * kStep, 38 * kStep);
    }
    {
        // 3/4: bars of twelve steps. REPEATS 2: the FILL's fifth step in the second bar.
        Rig rig;
        owner(rig);
        rig.repeats(2);
        Command meter = make(Command::SetSceneMeter, 0, 12);
        rig.send(meter);
        rig.fill(true);
        rig.play();
        rig.render(48 * kStep);
        CHECK_ONSETS(rig, 0, 16 * kStep, 24 * kStep, 40 * kStep);
    }
}

// ---- 6 -------------------------------------------------------------------------------------------

static void testCellLengths() {
    {
        // Eight bars: no FILL, whatever the flag says. Its eighth bar is its own, its fourth its own.
        Rig rig;
        rig.clicks(0);
        rig.cellBars(8);
        rig.hit(0, 0, 3);
        rig.hit(1, 4, 7);
        rig.fill(true);
        rig.play();
        rig.bars(8);
        CHECK_ONSETS(rig, 3 * kBar, 7 * kBar + 4 * kStep);
    }
    {
        // Four bars, each with its hit on its own step: the FILL in place of the fourth only.
        Rig rig;
        rig.clicks(0);
        rig.cellBars(4);
        for (int bar = 0; bar < 4; ++bar) rig.hit(0, bar, bar);
        rig.hit(1, 8, kFillSlot);
        rig.fill(true);
        rig.play();
        rig.bars(8); // REPEATS 1: every time round is the last
        CHECK_ONSETS(rig, 0, kBar + kStep, 2 * kBar + 2 * kStep, 3 * kBar + 8 * kStep, 4 * kBar, 5 * kBar + kStep, 6 * kBar + 2 * kStep, 7 * kBar + 8 * kStep);
    }
}

// ---- 7 -------------------------------------------------------------------------------------------

static void testNotes() {
    Rig rig;
    std::vector<Told> told;
    told.reserve(1000);
    Command install = make(Command::InstallSynth, 1);
    install.pointer = new Spy(&told);
    rig.send(install);
    const auto note = [&](int bar, int step, int pitch) {
        Command c = make(Command::SetNote, 1, 0, step, 0.8f);
        c.c = pitch;
        c.d = 1;
        c.bar = bar;
        rig.send(c);
    };
    note(0, 0, 60);
    note(kFillSlot, 4, 72);
    rig.repeats(2);
    rig.fill(true, 0, 1);
    rig.play();
    rig.bars(4);
    std::vector<long> frames;
    std::vector<int> keys;
    for (const Told& t : told) frames.push_back(t.frame), keys.push_back(t.pitch);
    CHECK((frames == std::vector<long>{0, kBar + 4 * kStep, 2 * kBar, 3 * kBar + 4 * kStep}));
    CHECK((keys == std::vector<int>{60, 72, 60, 72}));
}

// ---- 8 -------------------------------------------------------------------------------------------

static void testAutomation() {
    Rig rig;
    owner(rig);
    rig.repeats(2);
    const auto point = [&](int bar, float value) { // every step of the bar
        for (int step = 0; step < kSteps; ++step) rig.send(make(Command::SetAutomation, 0, kAutoSendLane, bar * kSteps + step, value));
    };
    point(0, 0.2f);
    point(kFillSlot, 0.9f);
    rig.fill(true);
    rig.play();
    std::vector<float> seen;
    for (int bar = 0; bar < 4; ++bar) {
        rig.render(8 * kStep + kStep / 2);
        seen.push_back(rig.engine->laneCentre(0, kAutoSendLane));
        rig.render(kBar - 8 * kStep - kStep / 2);
    }
    CHECK((seen == std::vector<float>{0.2f, 0.9f, 0.2f, 0.9f}));
    if (seen != std::vector<float>{0.2f, 0.9f, 0.2f, 0.9f})
        for (float v : seen) std::printf("  seen %.9g\n", v);
}

// ---- 9 -------------------------------------------------------------------------------------------

// The cell's own first hit early by a quarter of a step, its FILL's by four tenths.
static void early(Rig& rig, int repeats) {
    rig.clicks(0);
    rig.hit(0, 0, 0, -0.25f);
    rig.hit(1, 0, kFillSlot, -0.4f);
    rig.repeats(repeats);
}

static void testEarly() {
    Rig rig;
    early(rig, 2);
    rig.fill(true);
    rig.play();
    rig.bars(4);
    // The very first at the top; the FILL's asked from the last step of the bar before it; the bar after
    // the FILL asked from the FILL's last step.
    CHECK_ONSETS(rig, 0, kBar - 2400, 2 * kBar - 1500, 3 * kBar - 2400, 4 * kBar - 1500);
}

// ---- 10 ------------------------------------------------------------------------------------------

static void testAskedAgain() {
    const long lastStep = 2 * kBar - kStep + 1000; // a thousand samples into the second bar's last step
    {
        // REPEATS 4 to 3 there: the third bar is the last time round now, the FILL's, and its early
        // hit is asked again in place of the cell's own.
        Rig rig;
        early(rig, 4);
        rig.fill(true);
        rig.play();
        rig.render(lastStep);
        rig.repeats(3);
        rig.render(4 * kBar - lastStep);
        CHECK_ONSETS(rig, 0, kBar - 1500, 2 * kBar - 2400, 3 * kBar - 1500, 4 * kBar - 1500);
    }
    {
        // ...and 3 to 4: the cell's own again.
        Rig rig;
        early(rig, 3);
        rig.fill(true);
        rig.play();
        rig.render(lastStep);
        rig.repeats(4);
        rig.render(4 * kBar - lastStep);
        CHECK_ONSETS(rig, 0, kBar - 1500, 2 * kBar - 1500, 3 * kBar - 2400, 4 * kBar - 1500);
    }
    {
        // The FILL turned on there.
        Rig rig;
        early(rig, 3);
        rig.play();
        rig.render(lastStep);
        rig.fill(true);
        rig.render(4 * kBar - lastStep);
        CHECK_ONSETS(rig, 0, kBar - 1500, 2 * kBar - 2400, 3 * kBar - 1500, 4 * kBar - 1500);
    }
    {
        // ...and off.
        Rig rig;
        early(rig, 3);
        rig.fill(true);
        rig.play();
        rig.render(lastStep);
        rig.fill(false);
        rig.render(4 * kBar - lastStep);
        CHECK_ONSETS(rig, 0, kBar - 1500, 2 * kBar - 1500, 3 * kBar - 1500, 4 * kBar - 1500);
    }
    {
        // Turned on part way through its bar: the rest of the bar is the FILL's, at once.
        Rig rig;
        rig.clicks(0);
        rig.hit(0, 0);
        rig.hit(0, 8);
        rig.hit(1, 4, kFillSlot);
        rig.hit(1, 12, kFillSlot);
        rig.repeats(2);
        rig.play();
        rig.render(kBar + 6 * kStep);
        rig.fill(true);
        rig.render(kBar - 6 * kStep);
        CHECK_ONSETS(rig, 0, 8 * kStep, kBar, kBar + 12 * kStep); // (the FILL's step 5 had gone by; the cell's own step 9 is not played)
    }
}

// ---- 11 ------------------------------------------------------------------------------------------

static void testImagesAndMoves() {
    {
        // A track's image: the FILL and its bar. A scene with nothing in it but a FILL on is given a store.
        Rig rig;
        rig.clicks(0);
        std::unique_ptr<TrackImage> image(new TrackImage);
        image->level[0][0][0] = 0.8f;
        image->level[kFillSlot][1][4] = 0.8f;
        image->fill[0] = true;
        image->fill[5] = true;
        CHECK(rig.engine->setTrackImage(0, std::move(image)));
        CHECK(rig.engine->scenesGiven(0) == 2);
        rig.repeats(2);
        rig.play();
        rig.bars(4);
        CHECK_ONSETS(rig, 0, kBar + 4 * kStep, 2 * kBar, 3 * kBar + 4 * kStep);
        // ...and the scene that had nothing but its FILL on has it on: written into later, played alone
        // (REPEATS 1: every time round the last), the FILL plays.
        rig.send(make(Command::Stop));
        rig.render(2000);
        rig.out.clear();
        rig.hit(0, 0, 0, 0.f, 5);
        rig.hit(1, 4, kFillSlot, 0.f, 5);
        rig.send(make(Command::Play, 0, 5, 1));
        rig.bars(2);
        CHECK_ONSETS(rig, 4 * kStep, kBar + 4 * kStep);
    }
    {
        // On a scene nobody has written, the FILL gives it a store; off, it needs none.
        Rig rig;
        CHECK(rig.engine->scenesGiven(0) == 0);
        rig.fill(false, 3);
        CHECK(rig.engine->scenesGiven(0) == 0);
        rig.fill(true, 3);
        CHECK(rig.engine->scenesGiven(0) == 1);
        rig.render(256);
        CHECK(Engine::blankStillBlank());
    }
    {
        // A scene put in at the top (NEXT SCENE's one command) carries its FILL: the old scene 1 is
        // scene 2 now, with none.
        Rig rig;
        rig.clicks(0);
        rig.hit(0, 8); // the old scene: its own hit on step 9
        std::unique_ptr<SceneImage> scenes[kNumTracks];
        scenes[0].reset(new SceneImage);
        scenes[0]->level[0][0][0] = 0.8f;
        scenes[0]->level[kFillSlot][1][4] = 0.8f;
        scenes[0]->fill = true;
        CHECK(rig.engine->moveScenes(0, 1, scenes, 2, 2, 0));
        rig.play();
        rig.bars(4);
        CHECK_ONSETS(rig, 0, kBar + 4 * kStep, 2 * kBar + 8 * kStep, 3 * kBar);
    }
}

// ---- 12 ------------------------------------------------------------------------------------------

static void testTwoScenes() {
    Rig rig;
    rig.clicks(0);
    rig.send(make(Command::SetSceneCount, 0, 2));
    rig.hit(0, 0);
    rig.hit(1, 4, kFillSlot);
    rig.repeats(2);
    rig.fill(true);
    rig.hit(0, 8, 0, 0.f, 1);
    rig.hit(1, 12, kFillSlot, 0.f, 1); // scene 2's eighth bar: no FILL on there, never played
    rig.play();
    rig.bars(6);
    CHECK_ONSETS(rig, 0, kBar + 4 * kStep, 2 * kBar + 8 * kStep, 3 * kBar, 4 * kBar + 4 * kStep, 5 * kBar + 8 * kStep);
}

// ---- 13 ------------------------------------------------------------------------------------------

static void testSliceRuns() {
    // The FILL's first hit starts part way into its sound, and the seventh bar (which the one-bar cell
    // never plays) has a hit on its last step: a run would begin there if the FILL looked back.
    const auto cell = [](Rig& rig) {
        rig.click(0, 1, true);
        Command start = make(Command::SetStepStart, 0, 1, 0, 0.5f);
        start.bar = kFillSlot;
        rig.hit(1, 0, kFillSlot);
        rig.send(start);
        rig.hit(1, 15, kFillSlot - 1);
        rig.fill(true); // REPEATS 1: every bar is the FILL
    };
    {
        // SWING: step 1 is on the beat, never late; step 16 would be.
        Rig rig;
        cell(rig);
        rig.send(make(Command::SetLaneSwing, 0, 1, 100));
        rig.play();
        rig.render(3 * kBar);
        const std::vector<long> got = rig.starts(), want = {1, kBar + 1, 2 * kBar + 1}; // (each from its second sample: the rise)
        CHECK(got == want);
        if (got != want) show("heard", got), show("wanted", want);
    }
    {
        // HUMAN TIME: each felt hit asked a step ahead and rolled as the FILL's own first step (bar 8, step 1).
        Rig rig;
        cell(rig);
        Command feel = make(Command::SetLaneFeel, 0, 1, 100);
        rig.send(feel);
        rig.play();
        rig.render(4 * kBar);
        std::vector<long> want;
        for (long bar = 0; bar < 4; ++bar) {
            const long n = bar * kSteps;
            const std::uint32_t seed = feelSeed(0, 1, kFillSlot, 0, n);
            long at = n * kStep + static_cast<long>(std::ceil(feelSamples(100, 0, seed, 48.0, static_cast<double>(kStep))));
            if (bar == 0 && at < 0) at = 0;
            want.push_back(at + 1);
        }
        const std::vector<long> got = rig.starts();
        CHECK(got == want);
        if (got != want) show("heard", got), show("wanted", want);
    }
}

int main(int argc, char** argv) {
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testRule", testRule}, {"testSong", testSong}, {"testOffAndNone", testOffAndNone}, {"testLoop", testLoop},
        {"testLongerScenes", testLongerScenes}, {"testCellLengths", testCellLengths}, {"testNotes", testNotes},
        {"testAutomation", testAutomation}, {"testEarly", testEarly}, {"testAskedAgain", testAskedAgain},
        {"testImagesAndMoves", testImagesAndMoves}, {"testTwoScenes", testTwoScenes}, {"testSliceRuns", testSliceRuns},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("fill_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
