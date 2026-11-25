// EDIT on a drum lane (app/lane_edit.hpp): the edited sound against sounds made by hand (the cut,
// its edge fades, REVERSE, PITCH, the fades, GAIN and the guard), NORMALIZE and TRIM SILENCE, what
// the engine plays (beside a second engine handed the hand-made sound), in any block length, FIT and
// slices on an edited sound, a run of slices still carried on, the built-in drums, the file on the
// card untouched, the song file (a damaged line too), undo, a new recording, and the KIT page's
// gestures and its EDIT view's hints.
//
// laneedit_test NAME... runs just those.

#include <SDL.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "lane_edit.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "stretch.hpp"
#include "ui.hpp"
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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, Up = SDLK_UP, Down = SDLK_DOWN, Left = SDLK_LEFT, Right = SDLK_RIGHT;
constexpr float kRate = 48000.f;
constexpr int kStep = 6000; // 120 BPM at 48 kHz
constexpr int kLane = 5;

// A sound with something in every sample: a falling saw and a sine, under full scale.
std::vector<float> made(int n) {
    std::vector<float> s(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        s[static_cast<std::size_t>(i)] = 0.5f * std::sin(0.031f * static_cast<float>(i)) + 0.3f * (static_cast<float>(i % 97) / 97.f - 0.5f);
    return s;
}

// The same file as the program reads it (16 bits and back), so a reference is made from what is played.
std::vector<float> asRead(const std::string& path) {
    std::vector<float> s;
    CHECK(readWav(path, s));
    return s;
}

struct TempFolder {
    std::string path;
    explicit TempFolder() {
        path = tempFolder("laneedit");
        CHECK(writeWav(path + "/hit.wav", made(24000), 48000));       // half a second
        CHECK(writeWav(path + "/break.wav", made(96000), 48000));     // two seconds: a bar at 120 is exactly two
        std::vector<float> quiet(48000, 0.f);
        for (int i = 12000; i < 30000; ++i) quiet[static_cast<std::size_t>(i)] = 0.25f * std::sin(0.05f * static_cast<float>(i));
        CHECK(writeWav(path + "/quiet.wav", quiet, 48000));          // silence, a quarter-scale sine, silence
        CHECK(writeWav(path + "/silent.wav", std::vector<float>(4800, 0.f), 48000));
    }
    ~TempFolder() {
        for (const char* name : {"hit", "break", "quiet", "silent"}) std::remove((path + "/" + name + ".wav").c_str());
        rmdir(path.c_str());
    }
    std::string file(const char* name) const { return path + "/" + name + ".wav"; }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};

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
    void stick(int x, int y) {
        for (int axis = 0; axis < 2; ++axis) {
            SDL_Event event = {};
            event.type = SDL_CONTROLLERAXISMOTION;
            event.caxis.axis = static_cast<Uint8>(axis == 0 ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_LEFTY);
            event.caxis.value = static_cast<Sint16>((axis == 0 ? x : y) * 32767);
            ButtonEvent press;
            if (input.handle(event, press)) app.onButton(press, input);
        }
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
    Session& session() { return app.session(); }
    TrackData& kit() { return app.project().tracks[0]; }
    // Quiet first (nothing heard by hand still ringing), then one hit on the lane, `frames` of it, in blocks of `block`.
    std::vector<float> hit(int lane, int frames, int block = 256) {
        std::vector<float> out(2 * 256);
        for (int i = 0; i < 1000; ++i) engine->render(out.data(), 256); // (5 s: the longest sound here, 4 s, has rung out)
        Command c;
        c.type = Command::Trigger;
        c.track = 0;
        c.a = lane;
        c.value = 0.9f;
        engine->send(c);
        std::vector<float> all;
        std::vector<float> piece(static_cast<std::size_t>(2 * block));
        for (int done = 0; done < frames; done += block) {
            engine->render(piece.data(), block);
            all.insert(all.end(), piece.begin(), piece.end());
        }
        all.resize(static_cast<std::size_t>(2 * frames));
        return all;
    }
};

// The engine of `rig` handed this sound on the lane, as the program hands it an edited one.
void handOver(Rig& rig, int lane, const std::vector<float>& sound) {
    std::unique_ptr<SampleData> data(new SampleData);
    data->data = sound;
    CHECK(rig.engine->setLaneSample(0, lane, std::move(data)));
    std::vector<float> out(2 * 256);
    rig.engine->render(out.data(), 256);
}

float peakOf(const std::vector<float>& s) {
    float p = 0.f;
    for (float v : s) p = std::fabs(v) > p ? std::fabs(v) : p;
    return p;
}

// Where a sine crosses upward, a whole-sound count between two places: its frequency.
double frequencyOf(const std::vector<float>& s, int from, int to) {
    int first = -1, last = -1, count = 0;
    for (int i = from + 1; i < to; ++i)
        if (s[static_cast<std::size_t>(i - 1)] < 0.f && s[static_cast<std::size_t>(i)] >= 0.f) {
            if (first < 0) first = i;
            last = i;
            ++count;
        }
    return count > 1 ? (count - 1) * static_cast<double>(kRate) / (last - first) : 0.0;
}

double rms(const std::vector<float>& s, int from, int to) {
    double sum = 0.0;
    for (int i = from; i < to; ++i) sum += static_cast<double>(s[static_cast<std::size_t>(i)]) * s[static_cast<std::size_t>(i)];
    return std::sqrt(sum / (to - from));
}

// ---- the edits by hand -----------------------------------------------------------------------------

void testAtRest() {
    const std::vector<float> in = made(1000);
    CHECK(LaneEdit().atRest() && !LaneEdit().shapes());
    CHECK(makeShape(in, LaneEdit(), kRate) == in);
    LaneEdit gain;
    gain.gain = 30;
    CHECK(!gain.atRest() && !gain.shapes() && makeShape(in, gain, kRate) == in); // GAIN changes nothing of the shape
    std::vector<float> out(in.size());
    makeLevel(in.data(), static_cast<int>(in.size()), LaneEdit(), kRate, out.data());
    CHECK(out == in); // (all of them under the guard's knee: untouched)
}

// START and END cut the file; each cut edge fades over kEdgeMs (48 samples), an edge that is the file's own is left.
void testTheCut() {
    const std::vector<float> in = made(10000);
    const int edge = 48;
    LaneEdit e;
    e.start = 20000; // 2000 samples in
    e.end = 70000;   // to 7000
    std::vector<float> want(in.begin() + 2000, in.begin() + 7000);
    for (int i = 0; i < edge; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(edge);
        want[static_cast<std::size_t>(i)] *= g;
        want[want.size() - 1 - static_cast<std::size_t>(i)] *= g;
    }
    CHECK(makeShape(in, e, kRate) == want);
    // START alone: only its edge fades; the file's end is its own.
    e.end = kEditUnits;
    std::vector<float> fromStart(in.begin() + 2000, in.end());
    for (int i = 0; i < edge; ++i) fromStart[static_cast<std::size_t>(i)] *= static_cast<float>(i) / static_cast<float>(edge);
    CHECK(makeShape(in, e, kRate) == fromStart);
    // END alone.
    e.start = 0;
    e.end = 50000;
    std::vector<float> toEnd(in.begin(), in.begin() + 5000);
    for (int i = 0; i < edge; ++i) toEnd[toEnd.size() - 1 - static_cast<std::size_t>(i)] *= static_cast<float>(i) / static_cast<float>(edge);
    CHECK(makeShape(in, e, kRate) == toEnd);
    // The cut never comes to nothing: a sample at least.
    e.start = kEditUnits;
    e.end = kEditUnits;
    CHECK(makeShape(in, e, kRate).size() == 1);
}

void testReverse() {
    const std::vector<float> in = made(5000);
    LaneEdit e;
    e.reverse = 1;
    const std::vector<float> want(in.rbegin(), in.rend());
    CHECK(makeShape(in, e, kRate) == want);
    // With a cut: the cut part (its edges faded) turned round.
    e.start = 10000;
    e.end = 90000;
    LaneEdit plain = e;
    plain.reverse = 0;
    const std::vector<float> cut = makeShape(in, plain, kRate);
    CHECK(makeShape(in, e, kRate) == std::vector<float>(cut.rbegin(), cut.rend()));
}

// The fades are straight lines over their ms, as the sound is heard; GAIN is 10^(dB/20); the guard last.
void testTheLevel() {
    const std::vector<float> in = made(20000);
    LaneEdit e;
    e.gain = -60; // -6 dB
    e.fadeIn = 10;
    e.fadeOut = 100;
    std::vector<float> out(in.size());
    makeLevel(in.data(), static_cast<int>(in.size()), e, kRate, out.data());
    const float gain = std::pow(10.f, -60.f / 200.f);
    const int n = static_cast<int>(in.size()), fin = 480, fout = 4800;
    bool same = true;
    for (int i = 0; i < n; ++i) {
        float g = gain;
        if (i < fin) g *= static_cast<float>(i) / static_cast<float>(fin);
        if (n - 1 - i < fout) g *= static_cast<float>(n - 1 - i) / static_cast<float>(fout);
        same = same && out[static_cast<std::size_t>(i)] == in[static_cast<std::size_t>(i)] * g;
    }
    CHECK(same);
    CHECK(out[0] == 0.f && out.back() == 0.f);
    // The guard: below the knee untouched, over it rounded into full scale and never past it, one way up.
    CHECK(guarded(0.5f) == 0.5f && guarded(-kKnee) == -kKnee && guarded(0.f) == 0.f && guarded(0.895f) == 0.895f && guarded(0.8999f) == 0.8999f);
    float last = 0.f;
    bool rises = true, under = true;
    for (int i = 1; i <= 4000; ++i) {
        const float v = guarded(static_cast<float>(i) / 1000.f);
        rises = rises && v >= last;
        under = under && v <= 1.f;
        last = v;
    }
    CHECK(rises && under && guarded(4.f) > 0.999f && guarded(-4.f) < -0.999f);
    CHECK(std::fabs(guarded(kKnee + 1e-4f) - (kKnee + 1e-4f)) < 1e-6f); // no step at the knee
    // GAIN +24 dB on a half-scale sound: every sample over the knee rounded, none past full.
    LaneEdit loud;
    loud.gain = kGainMost;
    makeLevel(in.data(), n, loud, kRate, out.data());
    CHECK(peakOf(out) <= 1.f && peakOf(out) > 0.99f);
    for (int i = 0; i < n; ++i) CHECK(out[static_cast<std::size_t>(i)] == guarded(in[static_cast<std::size_t>(i)] * std::pow(10.f, kGainMost / 200.f)));
}

// PITCH and FINE: the length divided by the speed, a sine's pitch moved, nothing folded back going up.
void testPitch() {
    const int n = 48000;
    std::vector<float> sine(static_cast<std::size_t>(n)), high(static_cast<std::size_t>(n)), steady(static_cast<std::size_t>(n), 0.5f);
    for (int i = 0; i < n; ++i) {
        sine[static_cast<std::size_t>(i)] = 0.5f * std::sin(2.f * 3.14159265f * 1000.f * static_cast<float>(i) / kRate);
        high[static_cast<std::size_t>(i)] = 0.5f * std::sin(2.f * 3.14159265f * 15000.f * static_cast<float>(i) / kRate);
    }
    LaneEdit up;
    up.pitch = 12;
    const std::vector<float> u = makeShape(sine, up, kRate);
    CHECK(u.size() == 24000);
    CHECK(std::fabs(frequencyOf(u, 2000, 22000) - 2000.0) < 2.0);
    CHECK(std::fabs(rms(u, 2000, 22000) - rms(sine, 2000, 22000)) < 0.01); // as loud
    LaneEdit down;
    down.pitch = -12;
    const std::vector<float> d = makeShape(sine, down, kRate);
    CHECK(d.size() == 96000);
    CHECK(std::fabs(frequencyOf(d, 4000, 92000) - 500.0) < 1.0);
    LaneEdit fine;
    fine.fine = 50; // a quarter tone up
    const std::vector<float> f = makeShape(sine, fine, kRate);
    CHECK(static_cast<int>(f.size()) == static_cast<int>(std::ceil(n / std::pow(2.0, 0.5 / 12.0) - 1e-9)));
    CHECK(std::fabs(frequencyOf(f, 2000, 40000) - 1000.0 * std::pow(2.0, 0.5 / 12.0)) < 1.5);
    // 15 kHz an octave up is past what can be held: it is taken out, not folded back to 18 kHz.
    const std::vector<float> h = makeShape(high, up, kRate);
    CHECK(rms(h, 2000, 22000) < 0.01 * rms(high, 2000, 46000));
    // A steady level stays steady.
    const std::vector<float> s = makeShape(steady, up, kRate);
    bool flat = true;
    for (int i = 100; i < 23900; ++i) flat = flat && std::fabs(s[static_cast<std::size_t>(i)] - 0.5f) < 2e-6f;
    CHECK(flat);
    // Two octaves either way, the ends.
    LaneEdit most;
    most.pitch = -kPitchMost;
    CHECK(makeShape(made(1000), most, kRate).size() == 4000);
    most.pitch = kPitchMost;
    CHECK(makeShape(made(1000), most, kRate).size() == 250);
}

void testNormalizeAndTrim() {
    std::vector<float> s = made(5000);
    for (float& v : s) v *= 0.5f;
    const float peak = peakOf(s);
    int gain = 0;
    CHECK(normalGain(s.data(), static_cast<int>(s.size()), gain));
    const double db = -1.0 - 20.0 * std::log10(static_cast<double>(peak));
    CHECK(gain == static_cast<int>(std::floor(db * 10.0 + 1e-9)));
    const double after = 20.0 * std::log10(static_cast<double>(peak)) + gain / 10.0;
    CHECK(after <= -1.0 + 1e-9 && after > -1.1); // about -1 dB, never over it
    std::vector<float> silence(100, 0.f);
    CHECK(!normalGain(silence.data(), 100, gain));
    std::vector<float> tiny(100, 1e-8f);
    CHECK(normalGain(tiny.data(), 100, gain) == false);
    std::vector<float> faint(100, 1e-4f); // -80 dB: GAIN tops out at +24
    CHECK(normalGain(faint.data(), 100, gain) && gain == kGainMost);
    // TRIM SILENCE: START a millisecond ahead of the first sound, END past the last.
    std::vector<float> q(48000, 0.f);
    for (int i = 12000; i < 30000; ++i) q[static_cast<std::size_t>(i)] = 0.25f;
    q[100] = 0.001f; // under the floor (40 dB down): silence
    int start = 0, end = 0;
    CHECK(silenceEnds(q, kRate, start, end));
    CHECK(start == (12000 - 48) * kEditUnits / 48000);
    CHECK(end == static_cast<int>((30000LL * kEditUnits + 47999) / 48000));
    CHECK(!silenceEnds(std::vector<float>(100, 0.f), kRate, start, end));
}

void testTheRows() {
    // END after START by 5 ms at least, whatever is asked; START gives way first.
    const float seconds = 2.f;
    CHECK(leastSpan(seconds) == 250);
    LaneEdit e;
    e.start = 60000;
    e.end = 50000;
    LaneEdit held = laneEditHeld(e, seconds);
    CHECK(held.end == 50000 && held.start == 49750);
    e.end = 0;
    held = laneEditHeld(e, seconds);
    CHECK(held.end == 250 && held.start == 0);
    CHECK(leastSpan(0.f) == 1 && leastSpan(0.001f) == kEditUnits && leastSpan(1000.f) == 1);
    CHECK(laneEditStepped(LaneEdit(), kEditStart, 1, 500.f).start == 1); // (a file this long cannot be read: a unit at least all the same)
    // The D-pad: START by a millisecond of the file, held under END.
    LaneEdit s;
    s = laneEditStepped(s, kEditStart, 1, seconds);
    CHECK(s.start == 50);
    s = laneEditStepped(s, kEditStart, -1, seconds);
    s = laneEditStepped(s, kEditStart, -1, seconds);
    CHECK(s.start == 0);
    s.end = 300;
    s = laneEditStepped(s, kEditStart, 1, seconds);
    CHECK(s.start == 50);
    s = laneEditStepped(s, kEditStart, 1, seconds);
    CHECK(s.start == 50); // (300 - 250: no further)
    // GAIN by half a dB, to its ends; FADE by its stops; PITCH and FINE by one; REVERSE on and off.
    LaneEdit g;
    g = laneEditStepped(g, kEditGain, 1, seconds);
    CHECK(g.gain == 5);
    for (int i = 0; i < 100; ++i) g = laneEditStepped(g, kEditGain, 1, seconds);
    CHECK(g.gain == kGainMost);
    LaneEdit f;
    f = laneEditStepped(f, kEditFadeOut, 1, seconds);
    CHECK(f.fadeOut == 1);
    f.fadeOut = 7; // between two stops (a song file can say so): up is the next stop, down the one under it
    CHECK(laneEditStepped(f, kEditFadeOut, 1, seconds).fadeOut == 10 && laneEditStepped(f, kEditFadeOut, -1, seconds).fadeOut == 5);
    f.fadeOut = kFadeMost;
    CHECK(laneEditStepped(f, kEditFadeOut, 1, seconds).fadeOut == kFadeMost);
    f.fadeOut = 0;
    CHECK(laneEditStepped(f, kEditFadeOut, -1, seconds).fadeOut == 0);
    CHECK(fadeStopPlace(0) == 0 && fadeStopPlace(7) == 4 && fadeStopPlace(5000) == kFadeStopCount - 1);
    LaneEdit p;
    for (int i = 0; i < 30; ++i) p = laneEditStepped(p, kEditPitch, -1, seconds);
    CHECK(p.pitch == -kPitchMost);
    p = laneEditStepped(p, kEditFine, 1, seconds);
    CHECK(p.fine == 1);
    p = laneEditStepped(p, kEditReverse, 1, seconds);
    CHECK(p.reverse == 1);
    p = laneEditStepped(p, kEditReverse, 1, seconds);
    CHECK(p.reverse == 1);
    p = laneEditStepped(p, kEditReverse, -1, seconds);
    CHECK(p.reverse == 0);
    // What the rows read.
    char text[32];
    LaneEdit r;
    r.start = 25000;
    r.gain = -35;
    r.fadeIn = 1500;
    r.pitch = 3;
    r.fine = -7;
    laneEditText(r, kEditStart, 4.f, text, sizeof text);
    CHECK(std::strcmp(text, "1.000 S") == 0);
    laneEditText(r, kEditStart, 0.2f, text, sizeof text);
    CHECK(std::strcmp(text, "50.0 MS") == 0);
    laneEditText(r, kEditGain, 2.f, text, sizeof text);
    CHECK(std::strcmp(text, "-3.5 DB") == 0);
    laneEditText(r, kEditFadeIn, 2.f, text, sizeof text);
    CHECK(std::strcmp(text, "1.5 S") == 0);
    laneEditText(r, kEditPitch, 2.f, text, sizeof text);
    CHECK(std::strcmp(text, "+3") == 0);
    laneEditText(r, kEditFine, 2.f, text, sizeof text);
    CHECK(std::strcmp(text, "-7 CENTS") == 0);
    laneEditText(r, kEditReverse, 2.f, text, sizeof text);
    CHECK(std::strcmp(text, "OFF") == 0);
}

// ---- what the engine plays --------------------------------------------------------------------------

LaneEdit busyEdit() {
    LaneEdit e;
    e.start = 10000;
    e.end = 80000;
    e.gain = 45;
    e.fadeIn = 5;
    e.fadeOut = 50;
    e.reverse = 1;
    e.pitch = -3;
    e.fine = 20;
    return e;
}

std::vector<float> handMade(const std::vector<float>& file, const LaneEdit& e) {
    const std::vector<float> shaped = makeShape(file, e, kRate);
    std::vector<float> out(shaped.size());
    makeLevel(shaped.data(), static_cast<int>(shaped.size()), e, kRate, out.data());
    return out;
}

void testTheEnginePlaysIt() {
    TempFolder folder;
    const std::vector<float> file = asRead(folder.file("hit"));
    const LaneEdit e = busyEdit();
    const std::vector<float> want = handMade(file, e);
    Rig edited(folder.path), byHand(folder.path), plain(folder.path);
    edited.session().setLaneSample(0, kLane, "hit");
    edited.session().setLaneEdit(0, kLane, e);
    CHECK(edited.kit().laneEdit[kLane] == e);
    byHand.session().setLaneSample(0, kLane, "hit");
    handOver(byHand, kLane, want);
    plain.session().setLaneSample(0, kLane, "hit");
    const int frames = 40000;
    const std::vector<float> heard = edited.hit(kLane, frames);
    CHECK(heard == byHand.hit(kLane, frames));
    CHECK(heard != plain.hit(kLane, frames));
    CHECK(peakOf(heard) > 0.01f);
    // The lane's outline and length are the edited sound's; the file's are kept for the view.
    const Session::LaneSound& sound = edited.session().laneSound(0, kLane);
    CHECK(std::fabs(sound.seconds - static_cast<float>(want.size()) / kRate) < 1e-6f);
    CHECK(std::fabs(sound.sourceSeconds - 0.5f) < 1e-6f && sound.sourcePeaks.size() == 512 && sound.peaks.size() == 512);
    // In any block length, the same to the bit.
    for (int block : {1, 7, 64, 333}) CHECK(edited.hit(kLane, 9000, block) == std::vector<float>(heard.begin(), heard.begin() + 2 * 9000));
    // Back at rest: the file as it always played, to the bit.
    edited.session().setLaneEdit(0, kLane, LaneEdit());
    CHECK(edited.hit(kLane, frames) == plain.hit(kLane, frames));
    // GAIN alone, with nothing reshaped: made from what was made before (no new shape).
    LaneEdit g;
    g.gain = -120;
    const std::shared_ptr<const std::vector<float>> shapedBefore = edited.session().laneSound(0, kLane).shaped;
    edited.session().setLaneEdit(0, kLane, g);
    CHECK(edited.session().laneSound(0, kLane).shaped == shapedBefore);
    handOver(byHand, kLane, handMade(file, g));
    CHECK(edited.hit(kLane, frames) == byHand.hit(kLane, frames));
}

// PITCH on a long sound is made on the helper thread: until it is, the lane plays what it had.
void testALongShapeWaits() {
    TempFolder folder;
    const std::vector<float> file = asRead(folder.file("break"));
    LaneEdit e;
    e.pitch = -12;
    e.gain = -20;
    CHECK(shapeCost(file.size(), e) > kShapeInAFrame);
    LaneEdit small = e;
    small.end = kEditUnits / 20; // a tenth of a second of it: at once
    CHECK(shapeCost(file.size(), small) < kShapeInAFrame && shapeCost(file.size(), LaneEdit()) == 0.0);
    LaneEdit fine;
    fine.fine = 3;
    CHECK(shapeCost(file.size(), fine) > 1.0e6); // (a few cents is a resampling all the same)
    Rig edited(folder.path), byHand(folder.path), plain(folder.path);
    for (Rig* rig : {&edited, &byHand, &plain}) rig->session().setLaneSample(0, kLane, "break");
    edited.session().setLaneEdit(0, kLane, e);
    const Session::LaneSound& sound = edited.session().laneSound(0, kLane);
    CHECK(sound.shaping && sound.waiting && edited.session().stretching());
    // Meanwhile: the file as it was, with the new GAIN (the level is made at once).
    handOver(byHand, kLane, handMade(file, [&] { LaneEdit g; g.gain = e.gain; return g; }()));
    CHECK(edited.hit(kLane, 9000) == byHand.hit(kLane, 9000));
    // The frames go on and the helper's work comes in.
    for (int i = 0; i < 5000 && edited.session().stretching(); ++i) {
        edited.frames(1);
        usleep(2000); // (the helper thread's own time: frames here take none)
    }
    CHECK(!sound.shaping && !sound.waiting && sound.shaped->size() == 2 * file.size());
    handOver(byHand, kLane, handMade(file, e));
    CHECK(edited.hit(kLane, 30000) == byHand.hit(kLane, 30000));
    // Freshly read with such an edit (a song opened): the file as it is first, then the shape.
    Rig opened(folder.path);
    opened.app.project().tracks[0].laneEdit[kLane] = e;
    opened.session().setLaneSample(0, kLane, "break"); // (START and END were at rest: kept as they are)
    CHECK(opened.session().laneSound(0, kLane).shaping);
    opened.session().finishStretching();
    CHECK(opened.hit(kLane, 30000) == byHand.hit(kLane, 30000));
    // Moved on before the work came in: what came in is not taken.
    edited.session().setLaneEdit(0, kLane, small);
    CHECK(!sound.shaping);
    edited.session().setLaneEdit(0, kLane, e);
    edited.frames(1); // (the job starts)
    edited.session().setLaneEdit(0, kLane, small); // made at once, and the job's sound is for the edit before
    CHECK(!sound.shaping);
    edited.session().finishStretching();
    CHECK(sound.shaped->size() == 9600); // (small is pitched too: two tenths of a second)
    handOver(byHand, kLane, handMade(file, small));
    CHECK(edited.hit(kLane, 15000) == byHand.hit(kLane, 15000));
}

// A built-in drum takes the edits too, made from the kit's own sound; at rest it is the kit's again.
void testTheBuiltInDrums() {
    Rig edited, byHand, plain;
    LaneEdit e;
    e.gain = -60;
    e.pitch = 5;
    e.end = 60000;
    e.reverse = 1;
    edited.session().setLaneEdit(0, 1, e);
    CHECK(edited.kit().laneEdit[1] == e && edited.kit().laneSample[1][0] == 0);
    handOver(byHand, 1, handMade(DrumKit::builtIn(kRate).samples[1], e));
    CHECK(edited.hit(1, 20000) == byHand.hit(1, 20000));
    CHECK(edited.hit(1, 20000) != plain.hit(1, 20000));
    CHECK(!edited.session().laneSound(0, 1).peaks.empty()); // (an edited built-in drum is drawn on the KIT page)
    edited.session().setLaneEdit(0, 1, LaneEdit());
    CHECK(edited.hit(1, 20000) == plain.hit(1, 20000));
    CHECK(edited.session().laneSound(0, 1).peaks.empty()); // (left as it is: not drawn, as before)
}

// FIT stretches the EDITED sound; slices cut it; a run of slices in order still carries the sound on.
void testFitAndSlices() {
    TempFolder folder;
    const std::vector<float> file = asRead(folder.file("break"));
    LaneEdit e;
    e.start = 25000; // the second half of the break...
    e.gain = -30;
    e.reverse = 1;   // ...backwards
    Rig edited(folder.path), byHand(folder.path);
    edited.session().setLaneSample(0, kLane, "break");
    edited.session().setLaneFit(0, kLane, kSteps);
    edited.session().setLaneEdit(0, kLane, e);
    edited.session().finishStretching();
    edited.frames(2);
    CHECK(!edited.session().laneSound(0, kLane).waiting && edited.session().laneSound(0, kLane).fitted);
    // By hand: shaped, fitted to a bar at 120 (96000 samples), then its level.
    const std::vector<float> shaped = makeShape(file, e, kRate);
    const std::vector<float> fitted = fitTo(shaped, 16 * kStep, kRate);
    std::vector<float> want(fitted.size());
    makeLevel(fitted.data(), static_cast<int>(fitted.size()), e, kRate, want.data());
    CHECK(want.size() == static_cast<std::size_t>(16 * kStep));
    byHand.session().setLaneSample(0, kLane, "break");
    handOver(byHand, kLane, want);
    CHECK(edited.hit(kLane, 20000) == byHand.hit(kLane, 20000));
    // A GAIN turn on a fitted lane is not fitted again: the fitted sound is kept.
    const std::shared_ptr<const std::vector<float>> fittedBefore = edited.session().laneSound(0, kLane).fitted;
    e.gain = -40;
    edited.session().setLaneEdit(0, kLane, e);
    CHECK(edited.session().laneSound(0, kLane).fitted == fittedBefore && !edited.session().laneSound(0, kLane).waiting);
    // NORMALIZE on a fitted lane takes the FITTED sound's peak (the stretch moves it).
    LaneEdit n = e;
    n.start = 0;
    n.reverse = 0;
    Rig half(folder.path);
    half.session().setLaneSample(0, kLane, "break");
    half.session().setLaneFit(0, kLane, 4); // half a second: stretched to a quarter
    half.session().setLaneEdit(0, kLane, n);
    half.session().finishStretching();
    CHECK(half.session().normalizeLane(0, kLane));
    {
        const std::vector<float> f = *half.session().laneSound(0, kLane).fitted;
        const double at = 20.0 * std::log10(static_cast<double>(peakOf(f))) + half.kit().laneEdit[kLane].gain / 10.0;
        CHECK(at <= -1.0 && at > -1.1);
    }
    // FIT off: the edited sound unfitted again, and nothing fitted kept.
    half.session().setLaneFit(0, kLane, 0);
    CHECK(!half.session().laneSound(0, kLane).fitted && !half.session().laneSound(0, kLane).waiting);
    {
        Rig plainFit(folder.path);
        plainFit.session().setLaneSample(0, kLane, "break");
        handOver(plainFit, kLane, handMade(file, half.kit().laneEdit[kLane]));
        CHECK(half.hit(kLane, 20000) == plainFit.hit(kLane, 20000));
    }
    // A cut moved is fitted again (and waits for it).
    e.start = 30000;
    edited.session().setLaneEdit(0, kLane, e);
    CHECK(edited.session().laneSound(0, kLane).waiting && !edited.session().laneSound(0, kLane).fitted);
    edited.session().finishStretching();
    // The break played in order, a slice a step, RUNS ON: one unbroken sound, the same as one hit of it.
    for (Rig* rig : {&edited, &byHand}) {
        PatternScreen& grid = rig->app.drums();
        for (int s = 0; s < kSteps; ++s) {
            grid.setStep(kLane, s, 0.9f);
            grid.setStart(kLane, s, static_cast<float>(s) / kSteps);
        }
        rig->frames(4);
    }
    const std::vector<float> fitted2 = fitTo(makeShape(file, e, kRate), 16 * kStep, kRate);
    std::vector<float> want2(fitted2.size());
    makeLevel(fitted2.data(), static_cast<int>(fitted2.size()), e, kRate, want2.data());
    handOver(byHand, kLane, want2);
    const auto played = [](Rig& rig) {
        std::vector<float> out(2 * 256), all;
        for (int i = 0; i < 400; ++i) rig.engine->render(out.data(), 256);
        rig.session().togglePlay(false);
        for (int done = 0; done < 15 * kStep; done += 256) {
            rig.engine->render(out.data(), 256);
            all.insert(all.end(), out.begin(), out.end());
        }
        rig.session().togglePlay(false);
        rig.frames(2);
        return all;
    };
    const std::vector<float> inOrder = played(edited);
    CHECK(inOrder == played(byHand));
    // One hit, alone: the slices in order are it, sample for sample (no seam at any step).
    for (int s = 1; s < kSteps; ++s) edited.app.drums().setStep(kLane, s, 0.f);
    edited.frames(4);
    const std::vector<float> one = played(edited);
    CHECK(one == inOrder);
}

// The file on the card is never written: its bytes before and after every edit, NORMALIZE, TRIM and a save.
void testTheFileIsUntouched() {
    TempFolder folder;
    std::string before, after;
    CHECK(readTextFile(folder.file("quiet"), before));
    {
        Rig rig(folder.path);
        Session& session = rig.session();
        session.setLaneSample(0, kLane, "quiet");
        session.setLaneEdit(0, kLane, busyEdit());
        CHECK(session.normalizeLane(0, kLane));
        CHECK(session.trimLaneSilence(0, kLane));
        rig.frames(4);
        session.saveNow();
    }
    CHECK(readTextFile(folder.file("quiet"), after) && after == before);
}

void testNormalizeAndTrimOnALane() {
    TempFolder folder;
    Rig rig(folder.path);
    Session& session = rig.session();
    session.setLaneSample(0, kLane, "quiet");
    CHECK(session.normalizeLane(0, kLane));
    // The quarter-scale sine: up 11 dB or so, to just under -1 dB.
    const LaneEdit& e = rig.kit().laneEdit[kLane];
    const float peak = peakOf(asRead(folder.file("quiet")));
    const double at = 20.0 * std::log10(static_cast<double>(peak)) + e.gain / 10.0;
    CHECK(at <= -1.0 && at > -1.1);
    CHECK(e.start == 0 && e.end == kEditUnits); // it set GAIN alone
    // TRIM: START and END at the sine's ends.
    CHECK(session.trimLaneSilence(0, kLane));
    CHECK(std::abs(e.start - (12000 - 48) * kEditUnits / 48000) <= 2 && std::abs(e.end - static_cast<int>(30000LL * kEditUnits / 48000)) <= 2);
    // NORMALIZE takes the part played: the trim changes nothing of its peak.
    const int gain = e.gain;
    CHECK(session.normalizeLane(0, kLane) && e.gain == gain);
    // A silent recording: nothing to do, nothing changed.
    session.setLaneSample(0, 2, "silent");
    CHECK(!session.normalizeLane(0, 2) && !session.trimLaneSilence(0, 2));
    CHECK(rig.kit().laneEdit[2].atRest());
    // On a synth track, or no track: nothing.
    CHECK(!session.normalizeLane(5, 0) && !session.normalizeLane(-1, 0) && !session.trimLaneSilence(0, kDrumLanes));
}

// ---- the song, undo, a new recording ------------------------------------------------------------------

void testTheSongFile() {
    Rig rig;
    Session& session = rig.session();
    const std::string plain = songText(rig.app.project());
    CHECK(plain.find("laneedit") == std::string::npos); // nothing written at rest
    session.setLaneEdit(0, 2, busyEdit());
    LaneEdit g;
    g.gain = -5;
    session.setLaneEdit(0, 7, g);
    LaneEdit r;
    r.reverse = 1; // GAIN at 0, and still an edit
    session.setLaneEdit(0, 4, r);
    const std::string text = songText(rig.app.project());
    CHECK(text.find("\nlaneedit 3 0.1 0.8 4.5 5 50 1 -3 20\n") != std::string::npos);
    CHECK(text.find("\nlaneedit 8 0 1 -0.5 0 0 0 0 0\n") != std::string::npos);
    CHECK(text.find("\nlaneedit 5 0 1 0 0 0 1 0 0\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    CHECK(back->tracks[0].laneEdit[2] == busyEdit() && back->tracks[0].laneEdit[7] == g && back->tracks[0].laneEdit[0].atRest());
    session.setLaneEdit(0, 2, LaneEdit());
    session.setLaneEdit(0, 7, LaneEdit());
    session.setLaneEdit(0, 4, LaneEdit());
    CHECK(songText(rig.app.project()) == plain);
    // By hand: held to the limits as doubles first; a lane that is not there skipped; a number that
    // will not read ends the line there (the rest at rest); END after START.
    const std::size_t lanes = plain.find("\nlanes ");
    CHECK(lanes != std::string::npos);
    if (lanes == std::string::npos) return;
    const std::size_t after = plain.find('\n', lanes + 1) + 1;
    const auto with = [&](const std::string& lines) { return plain.substr(0, after) + lines + plain.substr(after); };
    std::unique_ptr<Project> odd(new Project);
    CHECK(parseSong(with("laneedit 1 1e300 -1e300 1e300 -5 1e300 7 99 -99\nlaneedit 2 0.25 0.5 x 9 9\nlaneedit 9 0.5 1 0 0 0 0 0 0\nlaneedit 0 0.5\n"
                         "laneedit x\nlaneedit 3 nan nan nan nan nan nan nan nan\nlaneedit 4 0.6 0.4\nlaneedit 5\n"),
                    *odd));
    const TrackData& t = odd->tracks[0];
    CHECK(t.laneEdit[0].end == 1 && t.laneEdit[0].start == 0 && t.laneEdit[0].gain == kGainMost && t.laneEdit[0].fadeIn == 0 && t.laneEdit[0].fadeOut == kFadeMost);
    CHECK(t.laneEdit[0].reverse == 1 && t.laneEdit[0].pitch == kPitchMost && t.laneEdit[0].fine == -kFineMost);
    CHECK(t.laneEdit[1].start == 25000 && t.laneEdit[1].end == 50000 && t.laneEdit[1].gain == 0 && t.laneEdit[1].fadeIn == 0);
    CHECK(t.laneEdit[2].atRest()); // ("nan" does not read as a number: the line ends at once)
    CHECK(t.laneEdit[3].end == 40000 && t.laneEdit[3].start == 39999);
    CHECK(t.laneEdit[4].atRest());
    for (int lane = 5; lane < kDrumLanes; ++lane) CHECK(t.laneEdit[lane].atRest());
    // Under a synth track it is not taken.
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.frames(2);
    std::string synth = songText(rig.app.project());
    synth += "laneedit 3 0.1 0.8 4.5 5 50 1 -3 20\n";
    std::unique_ptr<Project> none(new Project);
    CHECK(parseSong(synth, *none));
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(none->tracks[1].laneEdit[lane].atRest() && none->tracks[0].laneEdit[lane].atRest());
}

void testUndoAndANewRecording() {
    TempFolder folder;
    Rig rig(folder.path);
    Session& session = rig.session();
    session.setLaneSample(0, kLane, "hit");
    rig.frames(60);
    session.setLaneEdit(0, kLane, busyEdit());
    rig.frames(60); // the burst is over: a step of undo
    LaneEdit g = busyEdit();
    g.gain = -100;
    session.setLaneEdit(0, kLane, g);
    rig.frames(60);
    CHECK(session.undo());
    rig.frames(4);
    CHECK(rig.kit().laneEdit[kLane] == busyEdit());
    // ...and the engine plays what undo put back.
    Rig byHand(folder.path);
    byHand.session().setLaneSample(0, kLane, "hit");
    byHand.session().setLaneEdit(0, kLane, busyEdit());
    CHECK(rig.hit(kLane, 20000) == byHand.hit(kLane, 20000));
    CHECK(session.undo());
    rig.frames(4);
    CHECK(rig.kit().laneEdit[kLane].atRest());
    CHECK(session.redo() && session.redo());
    rig.frames(4);
    CHECK(rig.kit().laneEdit[kLane] == g);
    // Another recording: START and END go back to the whole of it, the rest stays. A step of undo with
    // its name (gesture audit G04): undo puts the old recording back WITH its own cut, as it was heard.
    session.setLaneSample(0, kLane, "break");
    rig.frames(60);
    LaneEdit kept = g;
    kept.start = 0;
    kept.end = kEditUnits;
    CHECK(rig.kit().laneEdit[kLane] == kept);
    const int steps = session.undoSteps();
    CHECK(session.undo());
    rig.frames(4);
    CHECK(std::string(rig.kit().laneSample[kLane]) == "hit" && rig.kit().laneEdit[kLane] == g && session.undoSteps() == steps - 1);
    CHECK(!session.laneSound(0, kLane).missing);
    byHand.session().setLaneEdit(0, kLane, g);
    CHECK(rig.hit(kLane, 20000) == byHand.hit(kLane, 20000));
    CHECK(session.redo());
    rig.frames(4);
    CHECK(std::string(rig.kit().laneSample[kLane]) == "break" && rig.kit().laneEdit[kLane] == kept);
}

// ---- the KIT page -------------------------------------------------------------------------------------

void testTheGestures() {
    TempFolder folder;
    Rig rig(folder.path);
    Session& session = rig.session();
    session.setLaneSample(0, kLane, "quiet"); // one second: a millisecond is 100 units
    session.view = View::Device;
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    kit.goTo(kLane, KitScreen::kSound);
    rig.tap(Right, 3);
    CHECK(kit.cursorColumn() == KitScreen::kEdit && !kit.editing());
    rig.tap(A);
    CHECK(kit.editing() && kit.editRow() == kEditStart);
    // Hold A + up: START a millisecond on; the stick turns it further.
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Up);
    CHECK(rig.kit().laneEdit[kLane].start == 100);
    rig.tap(Down, 2);
    CHECK(rig.kit().laneEdit[kLane].start == 0);
    rig.stick(0, -1);
    rig.frames(10);
    rig.stick(0, 0);
    CHECK(rig.kit().laneEdit[kLane].start > 5000);
    rig.key(A, false);
    rig.frames(1);
    // B: its usual value; B again (there already): back to the kit.
    rig.tap(B);
    CHECK(rig.kit().laneEdit[kLane].start == 0 && kit.editing());
    rig.tap(B);
    CHECK(!kit.editing());
    rig.tap(A);
    CHECK(kit.editing() && kit.editRow() == kEditStart); // (the row it was on)
    // Along a row and between the two; the ends stop.
    rig.tap(Left);
    CHECK(kit.editRow() == kEditStart);
    rig.tap(Right, 5);
    CHECK(kit.editRow() == kEditFadeOut);
    rig.tap(Down);
    CHECK(kit.editRow() == kEditReverse);
    rig.tap(Down);
    CHECK(kit.editRow() == kEditReverse);
    rig.key(A, true);
    rig.tap(Up);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.kit().laneEdit[kLane].reverse == 1);
    rig.tap(Left, 3);
    CHECK(kit.editRow() == kEditGain);
    // Y on GAIN: NORMALIZE. Y on START: TRIM SILENCE.
    rig.tap(Y);
    CHECK(rig.kit().laneEdit[kLane].gain > 100 && kit.said().find("NORMALIZED") == 0);
    rig.tap(Up);
    rig.tap(Left);
    CHECK(kit.editRow() == kEditStart);
    rig.tap(Y);
    CHECK(rig.kit().laneEdit[kLane].start > 20000 && rig.kit().laneEdit[kLane].end < 70000);
    // Hold B: the bar fills, and at a second every edit goes; let go, the view stays.
    rig.key(B, true);
    rig.frames(30);
    CHECK(kit.deletePart() > 0.3f && !rig.kit().laneEdit[kLane].atRest());
    rig.frames(40);
    CHECK(rig.kit().laneEdit[kLane].atRest() && kit.deletePart() == 0.f);
    rig.key(B, false);
    rig.frames(2);
    CHECK(kit.editing());
    // Another button calls a held B off.
    rig.key(A, true);
    rig.tap(Up);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.kit().laneEdit[kLane].start == 100);
    rig.tap(Right);
    rig.key(B, true); // END is at its usual value: a tap would go back...
    rig.frames(10);
    rig.tap(Left); // ...but a move calls it off
    rig.frames(70);
    rig.key(B, false);
    rig.frames(2);
    CHECK(kit.editing() && rig.kit().laneEdit[kLane].start == 100);
    // X held hears it as edited, and calls a held B off as any other button does.
    rig.tap(Right); // (on END, at its usual value: B is "back")
    rig.key(B, true);
    rig.frames(10);
    rig.key(X, true);
    rig.frames(1);
    CHECK(session.hearingHeld() && kit.deletePart() == 0.f);
    rig.frames(70);
    rig.key(X, false);
    rig.key(B, false);
    rig.frames(2);
    CHECK(kit.editing() && rig.kit().laneEdit[kLane].start == 100);
    // The edit view's bar holds every hint, A held and not, on every box.
    for (int row = 0; row < kEditRows; ++row) {
        kit.openEdit(row);
        for (int held = 0; held < 2; ++held) {
            if (held) rig.key(A, true);
            rig.frames(1);
            Canvas canvas;
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(ui::gHintsOver == 0);
            if (held) rig.key(A, false);
            rig.frames(1);
        }
    }
    // The KIT page on the EDIT column, A held and not.
    rig.tap(B);
    rig.tap(B);
    CHECK(!kit.editing());
    for (int held = 0; held < 2; ++held) {
        if (held) rig.key(A, true);
        rig.frames(1);
        Canvas canvas;
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0);
    }
    rig.key(A, false);
    rig.frames(1);
    CHECK(kit.editing()); // (A let go on EDIT opens it)
}

// A B straight after Y goes back to the kit and keeps what Y set (gesture audit G16, 2026-11-25): it put the box
// under the cursor back to its usual value, half undoing TRIM SILENCE or NORMALIZE, and only a box at its usual
// value went back. Any press between (a move, X) and B is B as ever. The bar says "back" while it is so.
void testBAfterYKeepsIt() {
    TempFolder folder;
    Rig rig(folder.path);
    Session& session = rig.session();
    session.setLaneSample(0, kLane, "quiet");
    session.view = View::Device;
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    kit.goTo(kLane, KitScreen::kSound);
    rig.tap(Right, 3);
    rig.tap(A);
    CHECK(kit.editing() && kit.editRow() == kEditStart);
    // TRIM SILENCE on START, then B: back to the kit, START and END as Y set them.
    rig.tap(Y);
    const LaneEdit trimmed = rig.kit().laneEdit[kLane];
    CHECK(trimmed.start > 20000 && trimmed.end < 70000);
    {
        Canvas canvas, scratch;
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(barSays(canvas, scratch, "back") && !barSays(canvas, scratch, "default"));
    }
    rig.tap(B);
    CHECK(!kit.editing() && rig.kit().laneEdit[kLane] == trimmed);
    // NORMALIZE on GAIN, then B: the same.
    kit.openEdit(kEditGain);
    rig.tap(Y);
    const LaneEdit normalized = rig.kit().laneEdit[kLane];
    CHECK(normalized.gain > 100);
    rig.tap(B);
    CHECK(!kit.editing() && rig.kit().laneEdit[kLane] == normalized);
    // A move between: B is B again, the box to its usual value, and the view stays.
    kit.openEdit(kEditGain);
    rig.tap(Y);
    rig.tap(Right);
    rig.tap(Left);
    {
        Canvas canvas, scratch;
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(barSays(canvas, scratch, "default"));
    }
    rig.tap(B);
    CHECK(kit.editing() && rig.kit().laneEdit[kLane].gain == LaneEdit().gain);
    // X between (hearing it): the same.
    rig.tap(Y);
    CHECK(rig.kit().laneEdit[kLane].gain > 100);
    rig.tap(X);
    rig.tap(B);
    CHECK(kit.editing() && rig.kit().laneEdit[kLane].gain == LaneEdit().gain);
    // Y on GAIN, then a move to START (trimmed above): B there is START's own B, START back to 0, GAIN kept.
    rig.tap(Y);
    rig.tap(Up);
    CHECK(kit.editRow() == kEditStart && rig.kit().laneEdit[kLane].start > 0);
    rig.tap(B);
    CHECK(kit.editing() && rig.kit().laneEdit[kLane].start == 0 && rig.kit().laneEdit[kLane].gain > 100);
    // Y on START (a trim), then the stick to END (no button): B on END is END's own B, END back, the view stays.
    rig.tap(Y);
    CHECK(rig.kit().laneEdit[kLane].start > 0 && rig.kit().laneEdit[kLane].end < LaneEdit().end);
    rig.stick(1, 0);
    rig.frames(1);
    rig.stick(0, 0);
    rig.frames(1);
    CHECK(kit.editRow() == kEditEnd);
    rig.tap(B);
    CHECK(kit.editing() && rig.kit().laneEdit[kLane].end == LaneEdit().end && rig.kit().laneEdit[kLane].start > 0);
    // Held, B straight after Y still deletes every edit at the bar's end (a hold deletes; undo brings it back).
    kit.openEdit(kEditStart);
    rig.tap(Y);
    CHECK(!rig.kit().laneEdit[kLane].atRest());
    rig.key(B, true);
    rig.frames(70);
    rig.key(B, false);
    rig.frames(2);
    CHECK(rig.kit().laneEdit[kLane].atRest() && kit.editing());
}

} // namespace

// A song sent whole (another song opened, a resync, a track deleted) does not wait for the helper
// thread: its job is for lanes that are made again anyway (review 2026-11-23: it waited, seconds of a
// long PITCH or FIT on the handheld, and the screen stood still). The job is handed off, finishes by
// itself and is let go of; the lane is made again and comes out as it should.
void testASongSentDoesNotWait() {
    TempFolder folder;
    const std::string longFile = folder.path + "/long.wav";
    CHECK(writeWav(longFile, made(48000 * 20), 48000)); // (read as the longest a recording is taken)
    Rig rig(folder.path);
    rig.session().setLaneSample(0, kLane, "long");
    LaneEdit e;
    e.pitch = -12;
    rig.session().setLaneEdit(0, kLane, e);
    for (int i = 0; i < 100 && !rig.session().jobUnderWay(); ++i) rig.frames(1); // (the job starts once the tempo has settled)
    CHECK(rig.session().jobUnderWay());
    const auto t0 = std::chrono::steady_clock::now();
    rig.session().sendSong();
    const double sent = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const bool handedOff = rig.session().jobsLeftRunning() == 1;
    for (int i = 0; i < 5000 && rig.session().jobsLeftRunning() > 0; ++i) {
        rig.frames(1);
        usleep(2000);
    }
    const double whole = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!handedOff || sent > 0.5 * whole) std::printf("  sending the song took %.3f s; the job %.3f s (handed off: %d)\n", sent, whole, handedOff ? 1 : 0);
    CHECK(handedOff && sent < 0.5 * whole);
    CHECK(rig.session().jobsLeftRunning() == 0);
    // The lane is made again from the song, and the new job's sound is taken.
    rig.session().finishStretching();
    const Session::LaneSound& sound = rig.session().laneSound(0, kLane);
    CHECK(!sound.shaping && !sound.waiting && sound.shaped && sound.source && sound.shaped->size() == 2u * sound.source->size()); // (an octave down: twice as long)
    std::remove(longFile.c_str());
}

// EDIT stays open across a tab, and its jobs name the track and lane it opened on: another track picked on SONG
// meanwhile (or the tracks changed under it, their numbers shifting) closes it, so the page never shows one track's
// name over another's sound, nor edits the old track from the new one's page.
void testEditClosesOnAnotherTrack() {
    TempFolder folder;
    Rig rig(folder.path);
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Drums));
    session.setLaneSample(0, kLane, "quiet");
    session.setLaneSample(1, kLane, "quiet");
    rig.app.project().track = 0;
    session.view = View::Device;
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    kit.goTo(kLane, KitScreen::kEdit);
    rig.tap(A);
    CHECK(kit.editing());
    session.view = View::Song; // L2 / R2 away, another drum track picked, and back
    rig.frames(2);
    rig.app.project().track = 1;
    session.view = View::Device;
    rig.frames(2);
    CHECK(!kit.editing());
    rig.key(A, true); // ...so hold A + up on it now edits nothing on track 1's lane (nor track 0's)
    rig.tap(Up);
    rig.key(A, false);
    CHECK(rig.app.project().tracks[0].laneEdit[kLane].atRest() && rig.app.project().tracks[1].laneEdit[kLane].atRest());
    // The same track, but the tracks changed under it (a duplicate put in): closed too.
    rig.app.project().track = 0;
    rig.frames(2);
    kit.goTo(kLane, KitScreen::kEdit);
    rig.tap(A);
    CHECK(kit.editing());
    CHECK(session.duplicateTrack(1) == 2);
    rig.frames(2);
    CHECK(!kit.editing());
    // Nothing changed: it stays open.
    rig.tap(A);
    rig.frames(4);
    CHECK(kit.editing());
}

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testAtRest", testAtRest}, {"testTheCut", testTheCut}, {"testReverse", testReverse}, {"testTheLevel", testTheLevel}, {"testPitch", testPitch},
        {"testNormalizeAndTrim", testNormalizeAndTrim}, {"testTheRows", testTheRows}, {"testTheEnginePlaysIt", testTheEnginePlaysIt},
        {"testTheBuiltInDrums", testTheBuiltInDrums}, {"testALongShapeWaits", testALongShapeWaits}, {"testASongSentDoesNotWait", testASongSentDoesNotWait}, {"testFitAndSlices", testFitAndSlices}, {"testTheFileIsUntouched", testTheFileIsUntouched},
        {"testNormalizeAndTrimOnALane", testNormalizeAndTrimOnALane}, {"testTheSongFile", testTheSongFile}, {"testUndoAndANewRecording", testUndoAndANewRecording},
        {"testTheGestures", testTheGestures}, {"testBAfterYKeepsIt", testBAfterYKeepsIt}, {"testEditClosesOnAnotherTrack", testEditClosesOnAnotherTrack},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("laneedit_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
