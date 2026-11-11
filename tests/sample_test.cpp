// Tests for recordings on drum lanes: reading WAV files, stretching, the
// session that loads and fits them, and the two screens that use them.

#include <SDL.h>

#include <stdlib.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "stretch.hpp"

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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, START = SDLK_RETURN, R2 = SDLK_2, R1 = SDLK_w;
const SDL_Keycode UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT;
const SDL_Keycode STICK_UP = SDLK_i, STICK_RIGHT = SDLK_l, STICK_DOWN = SDLK_k;
const float kTrackLevel = 0.65f;
const int kStep = 6000; // frames in a step at 120 BPM

std::string tempFolder() {
    char pattern[] = "/tmp/apotheca-samples-XXXXXX";
    const char* made = mkdtemp(pattern);
    return made ? made : "/tmp";
}

// Steady at one level: easy to measure how long it plays and how loud.
std::vector<float> steady(float seconds, float level) { return std::vector<float>(static_cast<std::size_t>(seconds * 48000.f), level); }

// A plain tone, for the tests that stretch: nothing real is a flat line,
// and the stretcher is not made for one.
std::vector<float> tone(float seconds, float level) {
    std::vector<float> out(static_cast<std::size_t>(seconds * 48000.f));
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = level * std::sin(0.5f + 6.2831853f * 220.f * static_cast<float>(i) / 48000.f);
    return out;
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    explicit Rig(const std::string& folder = "") {
        if (!folder.empty()) app.session().useSampleFolder(folder);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) { for (int i = 0; i < times; ++i) { key(code, true); key(code, false); } }
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    std::vector<float> render(int n) {
        std::vector<float> out(2 * static_cast<std::size_t>(n));
        engine->render(out.data(), n);
        return out;
    }
    // Hit a lane and report how many frames it sounds for, and how loud.
    int hit(int lane, float* level = nullptr, int within = 200000) {
        render(within); // let anything before die away
        Command c;
        c.type = Command::Trigger;
        c.a = lane;
        c.value = 1.f;
        engine->send(c);
        return sounding(render(within), level);
    }
    static int sounding(const std::vector<float>& out, float* level = nullptr) {
        int first = -1, last = -1;
        for (std::size_t i = 0; i < out.size(); i += 2) {
            if (out[i] == 0.f) continue;
            if (first < 0) first = static_cast<int>(i / 2);
            last = static_cast<int>(i / 2);
        }
        if (level) { // the loudest it gets in its middle third
            *level = 0.f;
            for (int i = first + (last - first) / 3; first >= 0 && i <= last - (last - first) / 3; ++i)
                *level = std::fabs(out[2 * static_cast<std::size_t>(i)]) > *level ? std::fabs(out[2 * static_cast<std::size_t>(i)]) : *level;
        }
        return first < 0 ? 0 : last - first + 1;
    }
    // Run screen frames until every fitted lane is ready, as the real loop would.
    bool settle() {
        for (int i = 0; i < 2000; ++i) {
            frames(1);
            if (!app.session().stretching()) return true;
            SDL_Delay(2);
        }
        return false;
    }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
};

std::string le(std::uint32_t v, int bytes) {
    std::string s;
    for (int i = 0; i < bytes; ++i) s += static_cast<char>((v >> (8 * i)) & 255);
    return s;
}
// A WAV file by hand: `value(frame, channel)` in -1..1.
template <class F>
std::string makeWav(int format, int bits, int channels, int rate, int frames, F value, bool extensible = false, bool junkFirst = false) {
    std::string data;
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) {
            const float v = value(i, c);
            if (format == 3) {
                std::uint32_t raw;
                std::memcpy(&raw, &v, 4);
                data += le(raw, 4);
            } else if (bits == 8) {
                data += static_cast<char>(static_cast<int>(std::lround(v * 127.f)) + 128);
            } else {
                const double scale = bits == 16 ? 32767.0 : bits == 24 ? 8388607.0 : 2147483647.0;
                data += le(static_cast<std::uint32_t>(static_cast<std::int32_t>(std::llround(v * scale))), bits / 8);
            }
        }
    std::string fmt = le(extensible ? 0xFFFE : static_cast<std::uint32_t>(format), 2) + le(static_cast<std::uint32_t>(channels), 2) +
                      le(static_cast<std::uint32_t>(rate), 4) + le(static_cast<std::uint32_t>(rate * channels * bits / 8), 4) +
                      le(static_cast<std::uint32_t>(channels * bits / 8), 2) + le(static_cast<std::uint32_t>(bits), 2);
    if (extensible) fmt += le(22, 2) + le(static_cast<std::uint32_t>(bits), 2) + le(0, 4) + le(static_cast<std::uint32_t>(format), 2) + std::string(14, '\0');
    std::string body = "WAVE";
    if (junkFirst) body += "LIST" + le(5, 4) + "abcde" + std::string(1, '\0'); // an odd-length chunk, padded
    body += "fmt " + le(static_cast<std::uint32_t>(fmt.size()), 4) + fmt + "data" + le(static_cast<std::uint32_t>(data.size()), 4) + data;
    return "RIFF" + le(static_cast<std::uint32_t>(body.size()), 4) + body;
}

float ramp(int i, int) { return -0.5f + 0.001f * static_cast<float>(i % 1000); }

void testReadingWavFiles() {
    std::vector<float> out;
    // Every kind of number a WAV keeps, read to the same values.
    const struct { int format, bits; float tolerance; } kinds[] = {{1, 8, 0.01f}, {1, 16, 1e-4f}, {1, 24, 1e-6f}, {1, 32, 1e-6f}, {3, 32, 1e-7f}};
    for (const auto& kind : kinds) {
        CHECK(parseWav(makeWav(kind.format, kind.bits, 1, 48000, 2000, ramp), out));
        CHECK(out.size() == 2000);
        bool close = out.size() == 2000;
        for (int i = 0; close && i < 2000; ++i) close = std::fabs(out[static_cast<std::size_t>(i)] - ramp(i, 0)) <= kind.tolerance;
        CHECK(close);
    }
    // Two channels become one: the middle of them.
    CHECK(parseWav(makeWav(1, 16, 2, 48000, 100, [](int, int c) { return c == 0 ? 0.6f : 0.2f; }), out));
    CHECK(out.size() == 100 && std::fabs(out[50] - 0.4f) < 1e-3f);
    // The newer header, and a chunk of something else before the sound.
    CHECK(parseWav(makeWav(1, 24, 1, 48000, 100, ramp, true), out) && out.size() == 100 && std::fabs(out[10] - ramp(10, 0)) < 1e-5f);
    CHECK(parseWav(makeWav(1, 16, 1, 48000, 100, ramp, false, true), out) && out.size() == 100 && std::fabs(out[10] - ramp(10, 0)) < 1e-4f);

    // Another sample rate comes out at 48 kHz: as long in time, at the same pitch.
    for (int rate : {22050, 44100, 96000}) {
        const int frames = rate / 2;
        CHECK(parseWav(makeWav(1, 16, 1, rate, frames, [rate](int i, int) { return 0.5f * std::sin(6.2831853f * 440.f * static_cast<float>(i) / static_cast<float>(rate)); }), out));
        CHECK(std::abs(static_cast<int>(out.size()) - 24000) <= 2);
        int crossings = 0;
        float peak = 0.f;
        for (std::size_t i = 1; i < out.size(); ++i) {
            crossings += (out[i - 1] < 0.f) != (out[i] < 0.f);
            peak = std::fabs(out[i]) > peak ? std::fabs(out[i]) : peak;
        }
        CHECK(std::abs(crossings - 440) <= 2); // 440 Hz for half a second
        CHECK(peak > 0.48f && peak < 0.52f);
    }
    // Ten seconds is the most: a longer file is cut there, and faded so it does not click.
    CHECK(parseWav(makeWav(1, 8, 1, 8000, 8000 * 12, [](int, int) { return 0.5f; }), out));
    CHECK(out.size() == 480000 && std::fabs(out[470000] - 0.5f) < 0.01f && std::fabs(out.back()) < 0.01f);
    CHECK(parseWav(makeWav(1, 8, 1, 8000, 8000 * 9, [](int, int) { return 0.5f; }), out));
    CHECK(out.size() == 432000 && std::fabs(out.back() - 0.5f) < 0.01f); // under ten: left alone

    // A file cut short plays what there is of it.
    std::string cutShort = makeWav(1, 16, 1, 48000, 1000, ramp);
    cutShort.resize(cutShort.size() - 600);
    CHECK(parseWav(cutShort, out) && out.size() == 700);
    // A float file with nonsense in it does not pass the nonsense on.
    CHECK(parseWav(makeWav(3, 32, 1, 48000, 4, [](int i, int) { return i == 1 ? std::nanf("") : i == 2 ? 1e9f : 0.25f; }), out));
    CHECK(out.size() == 4 && out[0] == 0.25f && out[1] == 0.f && out[2] == 0.f);

    // Not a WAV, or one this cannot read: refused, and `out` left alone.
    out.assign(3, 7.f);
    const std::string good = makeWav(1, 16, 1, 48000, 100, ramp);
    std::string notRiff = good, notWave = good;
    notRiff[0] = 'X';
    notWave[8] = 'X';
    const std::string bad[] = {"", "RIFF", notRiff, notWave, good.substr(0, 20), good.substr(0, 36),
                               makeWav(2, 16, 1, 48000, 100, ramp),  // a compressed kind
                               makeWav(1, 12, 1, 48000, 100, ramp),  // twelve-bit
                               makeWav(3, 64, 1, 48000, 100, ramp),  // double floats
                               makeWav(1, 16, 0, 48000, 100, ramp),  // no channels
                               makeWav(1, 16, 9, 48000, 10, ramp),   // too many
                               makeWav(1, 16, 1, 100, 100, ramp),    // a sample rate that is not one
                               makeWav(1, 16, 1, 400000, 100, ramp)};
    for (const std::string& text : bad) CHECK(!parseWav(text, out) && out.size() == 3 && out[0] == 7.f);

    // Written and read back.
    const std::string folder = tempFolder();
    std::vector<float> made(500);
    for (std::size_t i = 0; i < made.size(); ++i) made[i] = ramp(static_cast<int>(i), 0);
    made[3] = 2.f;   // too loud: held at full scale
    made[4] = -2.f;
    CHECK(writeWav(folder + "/b one.wav", made, 48000));
    CHECK(readWav(folder + "/b one.wav", out) && out.size() == 500);
    CHECK(std::fabs(out[10] - made[10]) < 1e-4f && out[3] > 0.999f && out[4] < -0.999f);
    CHECK(!readWav(folder + "/missing.wav", out));
    // The folder's recordings, by name. Other files are not listed.
    CHECK(writeWav(folder + "/a two.WAV", made, 48000));
    CHECK(writeWav(folder + "/.hidden.wav", made, 48000));
    writeTextFile(folder + "/notes.txt", "x");
    writeTextFile(folder + "/wav", "x");
    CHECK(wavNames(folder) == std::vector<std::string>({"a two", "b one"}));
    CHECK(wavNames("/no/such/folder").empty() && wavNames("").empty());
}

// Four bursts of a tone, a quarter of the way apart.
std::vector<float> fourBursts(int frames, float hz) {
    std::vector<float> in(static_cast<std::size_t>(frames), 0.f);
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < frames / 8; ++i)
            in[static_cast<std::size_t>(b * frames / 4 + i)] = 0.6f * std::sin(6.2831853f * hz * static_cast<float>(i) / 48000.f) * std::exp(-static_cast<float>(i) / 2500.f);
    return in;
}

void testStretching() {
    const std::vector<float> in = fourBursts(96000, 500.f);
    CHECK(stretchTo(in, 96000) == in);
    CHECK(stretchTo(std::vector<float>(), 100) == std::vector<float>(100, 0.f));
    CHECK(stretchTo(in, 0).empty() && stretchTo(in, -5).empty());
    for (int frames : {48000, 72000, 115200, 144000, 192000}) {
        const std::vector<float> out = stretchTo(in, frames);
        CHECK(static_cast<int>(out.size()) == frames); // exactly as long as asked
        const float ratio = static_cast<float>(frames) / 96000.f;
        for (int b = 0; b < 4; ++b) {
            // Each burst still begins a quarter of the way on: silence just
            // before it, sound just after, within 12 ms.
            const int at = b * frames / 4, slack = 576;
            float before = 0.f, after = 0.f;
            for (int i = at - 4000; i < at - slack; ++i)
                if (i >= 0 && b > 0) before = std::fabs(out[static_cast<std::size_t>(i)]) > before ? std::fabs(out[static_cast<std::size_t>(i)]) : before;
            for (int i = at; i < at + slack + 400; ++i) after = std::fabs(out[static_cast<std::size_t>(i)]) > after ? std::fabs(out[static_cast<std::size_t>(i)]) : after;
            CHECK(after > 0.3f);
            CHECK(before < 0.12f * (ratio < 1.f ? 2.f : 1.f)); // the tail of the burst before it, no more
            // And is the same note: count the crossings in its first 40 ms.
            int crossings = 0;
            for (int i = at + slack + 1; i < at + slack + 1920; ++i) crossings += (out[static_cast<std::size_t>(i - 1)] < 0.f) != (out[static_cast<std::size_t>(i)] < 0.f);
            CHECK(std::abs(crossings - 40) <= 3); // 500 Hz
        }
        float peak = 0.f;
        for (float v : out) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        CHECK(peak > 0.4f && peak < 0.8f); // no louder, no quieter to speak of
    }
    // A steady sound stays steady: the pieces add back up to what they were.
    const std::vector<float> flat = stretchTo(std::vector<float>(48000, 0.5f), 80000);
    bool even = true;
    for (int i = 0; i < 79000; ++i) even = even && std::fabs(flat[static_cast<std::size_t>(i)] - 0.5f) < 0.01f;
    CHECK(even);
}

void testFittingWithSignalsmith() {
    // What a fitted lane gets: exactly as long as asked, the hits where they
    // were (to 6 ms: tighter than the simpler method), the same note.
    const std::vector<float> in = fourBursts(96000, 500.f);
    CHECK(fitTo(in, 96000, 48000.f) == in);
    CHECK(fitTo(std::vector<float>(), 100, 48000.f) == std::vector<float>(100, 0.f));
    CHECK(fitTo(in, 0, 48000.f).empty());
    for (int frames : {72000, 120000, 144000, 192000}) {
        const std::vector<float> out = fitTo(in, frames, 48000.f);
        CHECK(static_cast<int>(out.size()) == frames);
        for (int b = 0; b < 4; ++b) {
            const int at = b * frames / 4;
            float before = 0.f, after = 0.f;
            for (int i = at - 4000; i < at - 600; ++i)
                if (i >= 0 && b > 0) before = std::fabs(out[static_cast<std::size_t>(i)]) > before ? std::fabs(out[static_cast<std::size_t>(i)]) : before;
            for (int i = at; i < at + 288; ++i) after = std::fabs(out[static_cast<std::size_t>(i)]) > after ? std::fabs(out[static_cast<std::size_t>(i)]) : after;
            CHECK(after > 0.3f);   // the hit is there within 6 ms of where it belongs
            CHECK(before < 0.15f); // and not much rings ahead of it
            int crossings = 0;
            for (int i = at + 577; i < at + 576 + 1920; ++i) crossings += (out[static_cast<std::size_t>(i - 1)] < 0.f) != (out[static_cast<std::size_t>(i)] < 0.f);
            CHECK(std::abs(crossings - 40) <= 3); // 500 Hz still
        }
        float peak = 0.f;
        for (float v : out) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        CHECK(peak > 0.4f && peak < 1.f);
    }
    // A held chord keeps a steadier level than the simpler method gives it.
    std::vector<float> chord(96000);
    for (std::size_t i = 0; i < chord.size(); ++i) {
        const float t = static_cast<float>(i) / 48000.f;
        chord[i] = 0.2f * (std::sin(6.2831853f * 220.f * t) + std::sin(6.2831853f * 277.18f * t) + std::sin(6.2831853f * 329.63f * t));
    }
    const auto wobble = [](const std::vector<float>& v) {
        float low = 1e9f, high = 0.f;
        for (std::size_t w = v.size() / 4; w + 960 < 3 * v.size() / 4; w += 960) {
            double energy = 0.0;
            for (std::size_t i = 0; i < 960; ++i) energy += static_cast<double>(v[w + i]) * v[w + i];
            const float level = static_cast<float>(std::sqrt(energy / 960.0));
            low = level < low ? level : low;
            high = level > high ? level : high;
        }
        return (high - low) / high;
    };
    CHECK(wobble(fitTo(chord, 192000, 48000.f)) < 0.6f * wobble(stretchTo(chord, 192000)));
    // And that is what a fitted lane plays: the same chord on a lane, fitted
    // to two bars (four seconds), comes out of the engine as steady as that.
    {
        const std::string folder = tempFolder();
        writeWav(folder + "/chord.wav", chord, 48000);
        Rig rig(folder);
        rig.session().setLaneSample(0, 2, "chord");
        rig.session().setLaneFit(0, 2, 32);
        rig.session().finishStretching();
        Command c;
        c.type = Command::Trigger;
        c.a = 2;
        c.value = 1.f;
        rig.engine->send(c);
        const std::vector<float> both = rig.render(192000);
        std::vector<float> heard(192000);
        for (std::size_t i = 0; i < heard.size(); ++i) heard[i] = both[2 * i];
        CHECK(wobble(heard) < 0.6f * wobble(stretchTo(chord, 192000)));
    }
    // Too short for it (a click of 20 ms): the simpler method does the job.
    const std::vector<float> click(960, 0.5f);
    std::vector<float> unused;
    CHECK(!stretchSmooth(click, 2000, 48000.f, unused));
    CHECK(fitTo(click, 2000, 48000.f) == stretchTo(click, 2000));
    CHECK(!stretchSmooth(std::vector<float>(), 2000, 48000.f, unused) && !stretchSmooth(in, 0, 48000.f, unused));
}

void testARecordingOnALane() {
    const std::string folder = tempFolder();
    writeWav(folder + "/tone.wav", steady(0.5f, 0.25f), 48000);
    writeWav(folder + "/loud.wav", steady(0.25f, 0.5f), 48000);
    Rig rig(folder);
    Session& session = rig.session();
    CHECK(session.sampleNames() == std::vector<std::string>({"loud", "tone"}));
    const int drum = rig.hit(2);
    CHECK(drum > 2000);

    session.setLaneSample(0, 2, "tone");
    float level = 0.f;
    CHECK(rig.hit(2, &level) == 24000);
    CHECK(std::fabs(level - 0.25f * kTrackLevel) < 1e-3f);
    CHECK(std::strcmp(rig.project().tracks[0].laneSample[2], "tone") == 0);
    const Session::LaneSound& sound = session.laneSound(0, 2);
    CHECK(sound.recording && std::fabs(sound.seconds - 0.5f) < 1e-3f && !sound.missing && !sound.waiting);
    CHECK(sound.peaks.size() > 100 && std::fabs(sound.peaks[5] - 0.25f) < 1e-3f && std::fabs(sound.peaks.back() - 0.25f) < 1e-3f);
    CHECK(rig.hit(3) > 2000 && session.laneSound(0, 3).peaks.empty()); // the other lanes are still drums

    // A file that is not there: the drum plays, and the song keeps the name
    // so it comes back when the file does.
    session.setLaneSample(0, 2, "gone");
    CHECK(session.laneSound(0, 2).missing && rig.hit(2) == drum);
    CHECK(std::strcmp(rig.project().tracks[0].laneSample[2], "gone") == 0);
    session.setLaneSample(0, 2, "loud");
    CHECK(rig.hit(2, &level) == 12000 && std::fabs(level - 0.5f * kTrackLevel) < 1e-3f);
    session.setLaneSample(0, 2, "");
    CHECK(rig.hit(2) == drum && !session.laneSound(0, 2).recording && !session.laneSound(0, 2).missing);
    CHECK(rig.project().tracks[0].laneSample[2][0] == 0);
    // Only drum tracks, and only lanes there are.
    session.addTrack(TrackKind::Phazed);
    session.setLaneSample(1, 0, "tone");
    session.setLaneSample(0, 8, "tone");
    session.setLaneSample(-1, 0, "tone");
    session.setLaneSample(12, 0, "tone");
    CHECK(rig.project().tracks[1].laneSample[0][0] == 0);
    // With no samples folder nothing can be found.
    Rig bare;
    bare.session().setLaneSample(0, 0, "tone");
    CHECK(bare.session().laneSound(0, 0).missing && bare.session().sampleNames().empty());
}

void testFittingToTheTempo() {
    const std::string folder = tempFolder();
    writeWav(folder + "/tone.wav", tone(0.5f, 0.25f), 48000);
    writeWav(folder + "/loud.wav", tone(0.3f, 0.5f), 48000);
    Rig rig(folder);
    Session& session = rig.session();
    const Session::LaneSound& sound = session.laneSound(0, 2);
    session.setLaneSample(0, 2, "tone");
    rig.frames(30);
    CHECK(!session.stretching()); // nothing to fit

    // Eight steps at 120 BPM is a second. Until it is ready the recording
    // plays as it was; then it is that long, and as loud as before.
    session.setLaneFit(0, 2, 8);
    CHECK(rig.project().tracks[0].laneFit[2] == 8 && sound.waiting && session.stretching());
    CHECK(rig.hit(2) == 24000);
    CHECK(rig.settle());
    CHECK(!sound.waiting && sound.fittedTempo == 120.f && sound.fittedSteps == 8);
    float level = 0.f;
    const int fitted = rig.hit(2, &level);
    CHECK(std::abs(fitted - 8 * kStep) <= 600);
    CHECK(std::fabs(level - 0.25f * kTrackLevel) < 0.03f);

    // The tempo changes: it is fitted again, but only once the tempo has
    // stopped moving for a quarter of a second.
    for (int i = 0; i < 20; ++i) {
        rig.project().tempo = 119.f - static_cast<float>(i);
        rig.frames(1);
    }
    CHECK(sound.fittedTempo == 120.f && !session.stretching());
    rig.project().tempo = 60.f;
    rig.frames(10); // 160 ms: not yet
    CHECK(sound.fittedTempo == 120.f);
    rig.frames(10);
    CHECK(rig.settle() && sound.fittedTempo == 60.f);
    CHECK(std::abs(rig.hit(2) - 16 * kStep) <= 600);
    rig.project().tempo = 240.f;
    session.finishStretching(); // the wait, without the frames
    CHECK(sound.fittedTempo == 240.f && std::abs(rig.hit(2) - 4 * kStep) <= 600);
    rig.project().tempo = 120.f;
    session.finishStretching();

    // Another length. And off: back to as recorded, at once.
    session.setLaneFit(0, 2, 16);
    session.finishStretching();
    CHECK(sound.fittedSteps == 16 && std::abs(rig.hit(2) - 16 * kStep) <= 600);
    session.setLaneFit(0, 2, 0);
    CHECK(!session.stretching() && rig.hit(2) == 24000);
    rig.frames(30);
    CHECK(!session.stretching() && rig.hit(2) == 24000);
    session.setLaneFit(0, 2, 999);
    CHECK(rig.project().tracks[0].laneFit[2] == 64);
    session.setLaneFit(0, 2, -3);
    CHECK(rig.project().tracks[0].laneFit[2] == 0);
    session.setLaneFit(0, 9, 4); // no such lane
    session.setLaneFit(12, 0, 4);

    // A lane that changes its recording while a fit is being made gets the
    // new recording's fit, never the old one's.
    session.setLaneFit(0, 2, 4);
    rig.frames(20); // the job for "tone" is running or done
    session.setLaneSample(0, 2, "loud");
    session.finishStretching();
    CHECK(std::abs(rig.hit(2, &level) - 4 * kStep) <= 600 && std::fabs(level - 0.5f * kTrackLevel) < 0.06f);
    // Likewise when only the length changes under it.
    session.setLaneFit(0, 2, 8);
    rig.frames(20);
    session.setLaneFit(0, 2, 2);
    session.finishStretching();
    CHECK(std::abs(rig.hit(2) - 2 * kStep) <= 600);
    // Two lanes are fitted one after the other.
    session.setLaneSample(0, 5, "tone");
    session.setLaneFit(0, 5, 8);
    session.setLaneFit(0, 2, 16);
    CHECK(rig.settle());
    CHECK(std::abs(rig.hit(5) - 8 * kStep) <= 600 && std::abs(rig.hit(2) - 16 * kStep) <= 600);
    // The fit of a built-in lane is remembered but there is nothing to stretch.
    session.setLaneFit(0, 0, 8);
    rig.frames(30);
    CHECK(!session.stretching());
}

void testTheSongKeepsItsRecordings() {
    const std::string folder = tempFolder();
    writeWav(folder + "/my loop.wav", steady(0.5f, 0.25f), 48000);
    Rig rig(folder);
    rig.session().setLaneSample(0, 2, "my loop");
    rig.session().setLaneFit(0, 2, 8);
    rig.session().setLaneSample(0, 6, "not here");
    rig.app.drums().setStep(2, 4, 0.9f);
    rig.app.drums().setStart(2, 4, 0.5f);
    rig.app.drums().setStep(3, 8, 0.9f);
    const std::string text = songText(rig.project());
    CHECK(text.find("\nsample 3 8 my loop\n") != std::string::npos);
    CHECK(text.find("\nsample 7 0 not here\n") != std::string::npos);
    CHECK(text.find("hit 3 5 0.9 0 1 0 0.5\n") != std::string::npos);
    CHECK(text.find("hit 4 9 0.9 0 1 0\n") != std::string::npos); // from the top: nothing extra written

    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    CHECK(std::strcmp(back->tracks[0].laneSample[2], "my loop") == 0 && back->tracks[0].laneFit[2] == 8);
    CHECK(back->tracks[0].drums[0][0].start[2][4] == 0.5f && back->tracks[0].drums[0][0].start[3][8] == 0.f);
    back->tracks[0].drums[0][0].level[3][8] = 0.f; // leave the one hit, to hear it by itself

    // Opened on another handheld with the same card: the recording plays,
    // fitted, and the hit starts where it did.
    Rig other(folder);
    other.project() = *back;
    other.session().applyProject();
    CHECK(other.session().laneSound(0, 2).recording && other.session().laneSound(0, 6).missing);
    other.session().finishStretching();
    CHECK(std::abs(other.hit(2) - 8 * kStep) <= 600);
    other.session().view = View::Pattern;
    other.tap(START);
    CHECK(std::abs(Rig::sounding(other.render(16 * kStep)) - 4 * kStep) <= 600); // it started half-way in: half of it is heard
    other.tap(START);
    // A bar sent to the engine again by itself (as copying a cell does) takes its starts with it.
    other.project().tracks[0].drums[0][0].start[2][4] = 0.75f;
    other.session().sendDrumPattern(0, 0);
    other.render(100000);
    other.tap(START);
    CHECK(std::abs(Rig::sounding(other.render(16 * kStep)) - 2 * kStep) <= 600);
    other.tap(START);
    // A new song after it has no recordings left over.
    {
        std::unique_ptr<Project> fresh(new Project);
        other.project() = *fresh;
    }
    other.session().applyProject();
    CHECK(!other.session().laneSound(0, 2).recording && other.hit(2) > 2000 && other.hit(2) != 8 * kStep);

    // Lines that make no sense are left out.
    const auto sampleOf = [](const std::string& line, int lane) {
        std::unique_ptr<Project> song(new Project);
        parseSong("apotheca-song 1\ntrack drums\n" + line + "\n", *song);
        return std::string(song->tracks[0].laneSample[lane]);
    };
    CHECK(sampleOf("sample 1 0 kick one", 0) == "kick one");
    CHECK(sampleOf("sample 0 0 x", 0).empty() && sampleOf("sample 9 0 x", 7).empty());
    CHECK(sampleOf("sample 1 0", 0).empty());
    CHECK(sampleOf("sample 1 0 ../secret", 0).empty() && sampleOf("sample 1 0 a\\b", 0).empty()); // a name, never a path
    std::unique_ptr<Project> song(new Project);
    CHECK(parseSong("apotheca-song 1\ntrack drums\nsample 2 500 x\nscene 1\nhit 1 1 1 0 1 0 7\nhit 1 2 1 0 1 0 -3\n", *song));
    CHECK(song->tracks[0].laneFit[1] == 64 && song->tracks[0].drums[0][0].start[0][0] == 1.f && song->tracks[0].drums[0][0].start[0][1] == 0.f);
    CHECK(parseSong("apotheca-song 1\ntrack phazed\nsample 1 0 x\n", *song) && song->tracks[0].laneSample[0][0] == 0);
}

void testWhereAHitStarts() {
    const std::string folder = tempFolder();
    writeWav(folder + "/tone.wav", steady(0.5f, 0.25f), 48000);
    Rig rig(folder);
    PatternScreen& drums = rig.app.drums();
    rig.session().view = View::Pattern;
    rig.session().setLaneSample(0, 2, "tone");
    drums.moveCursorTo(2, 4);
    rig.tap(A);
    rig.render(100000);
    CHECK(drums.start(2, 4) == 0.f);

    // Hold Y, stick right: the start slides in, and is heard as it goes.
    rig.key(Y, true);
    rig.key(STICK_RIGHT, true);
    rig.frames(10);
    const float some = drums.start(2, 4);
    CHECK(some > 0.05f && some < 0.5f);
    CHECK(Rig::sounding(rig.render(4000)) > 0);
    rig.frames(200);
    CHECK(drums.start(2, 4) == 15.f / 16.f); // the last sixteenth is always left
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    // The D-pad moves it by exact sixteenths of the sound.
    rig.tap(LEFT);
    CHECK(drums.start(2, 4) == 14.f / 16.f);
    rig.tap(RIGHT, 3);
    CHECK(drums.start(2, 4) == 15.f / 16.f);
    drums.setStart(2, 4, 0.1f);
    rig.tap(RIGHT);
    CHECK(drums.start(2, 4) == 2.f / 16.f);
    drums.setStart(2, 4, 0.1f);
    rig.tap(LEFT);
    CHECK(drums.start(2, 4) == 1.f / 16.f);
    rig.tap(LEFT, 5);
    CHECK(drums.start(2, 4) == 0.f);
    rig.tap(RIGHT, 8);
    CHECK(drums.start(2, 4) == 0.5f);
    // None of that was a tap of Y, and none of it touched the chance.
    rig.key(Y, false);
    CHECK(drums.chance(2, 4) == 1.f && drums.level(2, 4) == 0.8f);
    // Up and down with Y is still the chance, and leaves the start alone.
    rig.key(Y, true);
    rig.tap(DOWN);
    rig.key(Y, false);
    CHECK(drums.chance(2, 4) < 1.f && drums.start(2, 4) == 0.5f);
    drums.setChance(2, 4, 1.f);

    // X plays the hit from where it starts; the engine plays it so in the bar.
    rig.render(100000);
    rig.tap(X);
    CHECK(std::abs(Rig::sounding(rig.render(100000)) - 12000) <= 2);
    rig.tap(START);
    const std::vector<float> bar = rig.render(16 * kStep);
    CHECK(std::abs(Rig::sounding(bar) - 12000) <= 2);
    rig.tap(START);
    // While the song plays, sliding does not also play it.
    rig.render(100000);
    rig.tap(START);
    rig.render(6 * kStep); // past the hit, into silence
    rig.render(3 * kStep);
    rig.key(Y, true);
    drums.setStart(2, 4, 0.f);
    rig.tap(RIGHT);
    rig.key(Y, false);
    CHECK(Rig::sounding(rig.render(2000)) == 0);
    rig.tap(START);

    // On an empty step Y places its "maybe" hit as ever, and that hit can be slid at once.
    drums.moveCursorTo(2, 9);
    rig.key(Y, true);
    CHECK(drums.chance(2, 9) == 0.5f && drums.start(2, 9) == 0.f);
    rig.tap(RIGHT);
    rig.key(Y, false);
    CHECK(drums.start(2, 9) == 1.f / 16.f && drums.chance(2, 9) == 0.5f);
    // On the tempo there is nothing to slide.
    drums.moveCursorTo(0, 0);
    rig.tap(UP);
    CHECK(drums.cursorOnTempo());
    rig.key(Y, true);
    rig.tap(RIGHT);
    rig.key(STICK_RIGHT, true);
    rig.frames(5);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.key(Y, false);
    CHECK(drums.start(0, 0) == 0.f);
    // A removed hit leaves no start behind for the next one placed there.
    drums.moveCursorTo(2, 4);
    drums.setStart(2, 4, 0.5f);
    rig.tap(B);
    rig.tap(A);
    CHECK(drums.level(2, 4) > 0.f && drums.start(2, 4) == 0.f);
    drums.setStart(2, 4, 5.f);
    CHECK(drums.start(2, 4) == 15.f / 16.f);
    drums.setStart(2, 4, -1.f);
    CHECK(drums.start(2, 4) == 0.f);
    drums.setStart(9, 0, 0.5f); // no such lane
}

void testRollingALanesStarts() {
    Rig rig;
    PatternScreen& drums = rig.app.drums();
    rig.session().view = View::Pattern;
    for (int step = 0; step < kSteps; step += 2) drums.setStep(3, step, 0.8f);
    drums.setStep(4, 1, 0.8f);
    drums.moveCursorTo(3, -1); // the lane's name
    const auto starts = [&] {
        std::vector<float> all;
        for (int step = 0; step < kSteps; ++step) all.push_back(drums.start(3, step));
        return all;
    };
    const std::vector<float> none = starts();

    rig.key(Y, true);
    rig.tap(RIGHT);
    const std::vector<float> first = starts();
    CHECK(first != none);
    int different = 0;
    for (int step = 0; step < kSteps; ++step) {
        const float s = first[static_cast<std::size_t>(step)];
        if (step % 2 == 1) CHECK(s == 0.f); // no hit there: nothing rolled
        CHECK(s >= 0.f && s <= 15.f / 16.f && s * 16.f == std::floor(s * 16.f)); // on a sixteenth of the sound
        different += s != first[0];
    }
    CHECK(different >= 3); // a spread, not one value
    CHECK(drums.start(4, 1) == 0.f); // the other lanes are left alone
    rig.tap(RIGHT);
    CHECK(starts() != first); // again: a new roll
    rig.tap(LEFT);
    CHECK(starts() == none); // all back to the top
    // The stick: one roll for each push, however long it is held.
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    const std::vector<float> pushed = starts();
    CHECK(pushed != none);
    rig.frames(40);
    CHECK(starts() == pushed);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(starts() != pushed);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    // Rolling is not a tap of Y: the hits themselves are not reshuffled.
    rig.key(Y, false);
    for (int step = 0; step < kSteps; ++step) CHECK((drums.level(3, step) > 0.f) == (step % 2 == 0));
    // Up and down with Y on the name is still the dice.
    rig.key(Y, true);
    rig.tap(UP);
    rig.key(Y, false);
    CHECK(drums.diceHits(3) == 1);
}

void testTheKitScreen() {
    const std::string folder = tempFolder();
    writeWav(folder + "/alpha.wav", steady(0.5f, 0.25f), 48000);
    writeWav(folder + "/beta.wav", steady(0.25f, 0.25f), 48000);
    Rig rig(folder);
    KitScreen& kit = rig.app.kit();
    TrackData& track = rig.project().tracks[0];
    rig.session().view = View::Pattern;
    rig.tap(R2); // on to DEVICE
    CHECK(rig.session().view == View::Device);
    CHECK(kit.cursorLane() == 0 && kit.cursorColumn() == KitScreen::kSound);
    // Up and down through the lanes, round the ends; sideways between the two columns.
    rig.tap(UP);
    CHECK(kit.cursorLane() == 7);
    rig.tap(DOWN, 3);
    CHECK(kit.cursorLane() == 2);
    rig.tap(RIGHT);
    CHECK(kit.cursorColumn() == KitScreen::kFit);
    rig.tap(RIGHT);
    CHECK(kit.cursorColumn() == KitScreen::kSound);

    // Hold A and go up: the first recording, and it is heard.
    rig.render(100000);
    rig.key(A, true);
    rig.tap(UP);
    CHECK(std::strcmp(track.laneSample[2], "alpha") == 0 && kit.cursorLane() == 2);
    CHECK(Rig::sounding(rig.render(100000)) == 24000);
    rig.tap(UP);
    CHECK(std::strcmp(track.laneSample[2], "beta") == 0);
    CHECK(Rig::sounding(rig.render(100000)) == 12000);
    rig.tap(UP, 3);
    CHECK(std::strcmp(track.laneSample[2], "beta") == 0); // the last one: it stops
    rig.tap(DOWN, 2);
    CHECK(track.laneSample[2][0] == 0); // down past the first: the built-in drum
    rig.tap(DOWN);
    CHECK(track.laneSample[2][0] == 0);
    rig.tap(LEFT); // sideways does nothing while A is held
    CHECK(kit.cursorColumn() == KitScreen::kSound);
    rig.key(STICK_RIGHT, true);
    rig.frames(5);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(kit.cursorColumn() == KitScreen::kSound && track.laneSample[2][0] == 0);
    // The stick does the same, one at once and then slowly enough to hear each.
    rig.key(STICK_UP, true);
    rig.frames(1);
    CHECK(std::strcmp(track.laneSample[2], "alpha") == 0);
    rig.frames(4);
    CHECK(std::strcmp(track.laneSample[2], "alpha") == 0);
    rig.frames(10);
    CHECK(std::strcmp(track.laneSample[2], "beta") == 0);
    rig.key(STICK_UP, false);
    rig.frames(1);
    rig.key(A, false);
    // A recording copied to the card while the screen is open is found at the next hold.
    writeWav(folder + "/aaa.wav", steady(0.1f, 0.25f), 48000);
    rig.key(A, true);
    rig.tap(DOWN, 2);
    CHECK(std::strcmp(track.laneSample[2], "aaa") == 0);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(std::strcmp(track.laneSample[2], "alpha") == 0);
    // X hears the lane. Nothing is played for a change while the song runs.
    rig.render(100000);
    rig.tap(X);
    CHECK(Rig::sounding(rig.render(100000)) == 24000);
    rig.tap(START);
    CHECK(rig.project().playing);
    rig.render(100000);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(std::strcmp(track.laneSample[2], "beta") == 0 && Rig::sounding(rig.render(4000)) == 0);
    rig.tap(START);

    // FIT: off, then 1, 2, 4, 8, 16, 32, 64 steps.
    rig.tap(RIGHT);
    rig.key(A, true);
    const int want[] = {1, 2, 4, 8, 16, 32, 64, 64};
    for (int w : want) {
        rig.tap(UP);
        CHECK(track.laneFit[2] == w);
    }
    rig.tap(DOWN, 3);
    CHECK(track.laneFit[2] == 8);
    rig.key(A, false);
    CHECK(rig.settle());
    CHECK(std::abs(rig.hit(2) - 8 * kStep) <= 600);
    // B on FIT turns it off; B on SOUND brings the drum back. Not while A is held.
    rig.key(A, true);
    rig.tap(B);
    rig.key(A, false);
    CHECK(track.laneFit[2] == 8);
    rig.tap(B);
    CHECK(track.laneFit[2] == 0 && std::strcmp(track.laneSample[2], "beta") == 0);
    rig.tap(LEFT);
    rig.tap(B);
    CHECK(track.laneSample[2][0] == 0);
    // Each lane is its own.
    rig.tap(DOWN);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(std::strcmp(track.laneSample[3], "aaa") == 0 && track.laneSample[2][0] == 0);
}

void testCatch() {
    const std::string folder = tempFolder() + "/not-made-yet";
    Rig rig(folder);
    KitScreen& kit = rig.app.kit();
    TrackData& track = rig.project().tracks[0];
    rig.app.drums().setStep(0, 0, 1.f);
    rig.app.drums().setStep(0, 8, 1.f);
    rig.session().view = View::Device;
    kit.goTo(5, KitScreen::kSound);

    // Nothing has played: nothing to catch, and it says so.
    rig.tap(Y);
    CHECK(track.laneSample[5][0] == 0 && wavNames(folder).empty());
    CHECK(kit.said().find("NOTHING") != std::string::npos);
    rig.frames(170); // the message goes after a couple of seconds
    CHECK(kit.said().empty());

    // Play a bar and a half, and catch: the first bar is on the lane, as a
    // file, fitted to one bar.
    rig.tap(START);
    const std::vector<float> played = rig.render(16 * kStep + 8 * kStep);
    rig.tap(Y);
    CHECK(std::strcmp(track.laneSample[5], "catch 1") == 0 && track.laneFit[5] == 16);
    CHECK(kit.said().find("CATCH 1") != std::string::npos);
    std::vector<float> file;
    CHECK(readWav(folder + "/catch 1.wav", file) && file.size() == 16u * kStep);
    bool same = file.size() == 16u * kStep;
    for (int i = 0; same && i < 16 * kStep; i += 7) same = std::fabs(file[static_cast<std::size_t>(i)] - played[2 * static_cast<std::size_t>(i)]) < 1e-3f; // what was heard, to 16 bits
    CHECK(same);
    rig.tap(START);
    rig.session().finishStretching();
    CHECK(!rig.session().laneSound(0, 5).waiting);
    float level = 0.f;
    const int length = rig.hit(5, &level);
    CHECK(length > 8 * kStep && length <= 16 * kStep); // the bar, from its first kick to the tail of its second
    // A bar caught at this tempo is not put through the stretcher: it plays as it was.
    Command c;
    c.type = Command::Trigger;
    c.a = 5;
    c.value = 1.f;
    rig.render(200000);
    rig.engine->send(c);
    const std::vector<float> again = rig.render(16 * kStep);
    same = true;
    for (int i = 0; same && i < 16 * kStep; i += 7) same = std::fabs(again[2 * static_cast<std::size_t>(i)] - file[static_cast<std::size_t>(i)] * kTrackLevel) < 1e-3f;
    CHECK(same);
    {
        // The same for a recording a few samples off a bar: close enough is left alone.
        std::vector<float> nearly = tone(2.f, 0.25f);
        nearly.resize(96003, 0.1f);
        writeWav(folder + "/nearly.wav", nearly, 48000);
        rig.session().setLaneSample(0, 1, "nearly");
        rig.session().setLaneFit(0, 1, 16);
        rig.session().finishStretching();
        rig.render(200000);
        c.a = 1;
        rig.engine->send(c);
        const std::vector<float> heard = rig.render(16 * kStep);
        same = true;
        for (int i = 0; same && i < 16 * kStep; i += 7) same = std::fabs(heard[2 * static_cast<std::size_t>(i)] - nearly[static_cast<std::size_t>(i)] * kTrackLevel) < 1e-3f;
        CHECK(same);
        rig.session().setLaneSample(0, 1, "");
        std::remove((folder + "/nearly.wav").c_str());
    }
    // The next catch is "catch 2", on whichever lane the cursor is on; the first is left alone.
    rig.render(200000);
    rig.tap(X); // something to catch: the lane, played by hand
    rig.render(4000);
    rig.tap(DOWN);
    rig.tap(Y);
    CHECK(std::strcmp(track.laneSample[6], "catch 2") == 0 && std::strcmp(track.laneSample[5], "catch 1") == 0);
    CHECK(wavNames(folder) == std::vector<std::string>({"catch 1", "catch 2"}));
    // Not while A is held (that is for stepping through sounds).
    rig.tap(X);
    rig.render(4000);
    rig.key(A, true);
    rig.tap(Y);
    rig.key(A, false);
    CHECK(wavNames(folder).size() == 2);
    // With nowhere to keep it, or on a track that is not drums, nothing happens.
    CHECK(rig.session().catchLastBar(0, 8).empty() && rig.session().catchLastBar(3, 0).empty());
    Rig bare;
    bare.app.drums().setStep(0, 0, 1.f);
    bare.session().view = View::Pattern;
    bare.tap(START);
    bare.render(20 * kStep);
    CHECK(bare.session().catchLastBar(0, 5).empty() && bare.project().tracks[0].laneSample[5][0] == 0);
}

void testTheSamplerTrack() {
    const std::string folder = tempFolder();
    writeWav(folder + "/alpha.wav", tone(0.5f, 0.25f), 48000);
    writeWav(folder + "/beta.wav", tone(0.25f, 0.25f), 48000);
    Rig rig(folder);
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Sampler));
    TrackData& track = rig.project().tracks[1];
    CHECK(track.kind == TrackKind::Sampler && isSynth(track.kind) && track.sampler[sampler::Root] == 60 && track.sample[0] == 0);
    rig.project().track = 1;
    session.view = View::Device;
    SamplerScreen& screen = rig.app.sampler();
    const auto heard = [&](int frames = 100000) { return Rig::sounding(rig.render(frames)); };

    // No recording yet: X plays nothing.
    rig.tap(X);
    CHECK(heard(4800) == 0);
    // Hold A and go up: each recording, heard at its root key.
    rig.key(A, true);
    rig.tap(UP);
    CHECK(std::strcmp(track.sample, "alpha") == 0);
    int length = heard();
    CHECK(length > 9000 && length < 24100); // the note is let go after a moment, and fades
    rig.tap(UP);
    CHECK(std::strcmp(track.sample, "beta") == 0);
    rig.tap(UP);
    CHECK(std::strcmp(track.sample, "beta") == 0); // the last: it stops
    rig.tap(DOWN);
    CHECK(std::strcmp(track.sample, "alpha") == 0);
    rig.key(A, false);
    CHECK(session.trackSound(1).peaks.size() > 100 && std::fabs(session.trackSound(1).seconds - 0.5f) < 1e-3f);
    rig.frames(60);
    rig.render(100000);

    // The settings: right along the row, down to the second row, up to SOUND.
    rig.tap(RIGHT);
    CHECK(screen.cursor() == 1);
    rig.tap(RIGHT, 3);
    CHECK(screen.cursor() == 4);
    rig.tap(DOWN);
    CHECK(screen.cursor() == 7); // LEVEL is under ATTACK; there is no eighth
    rig.tap(LEFT, 2);
    CHECK(screen.cursor() == 5);
    rig.tap(DOWN);
    CHECK(screen.cursor() == 5);
    rig.tap(UP);
    CHECK(screen.cursor() == 1);
    rig.tap(UP);
    CHECK(screen.cursor() == 0);
    rig.tap(DOWN);
    CHECK(screen.cursor() == 1);
    // ROOT: hold A, the D-pad moves it a key at a time; the stick turns it.
    rig.key(A, true);
    rig.tap(UP, 12);
    CHECK(track.sampler[sampler::Root] == 72);
    rig.tap(DOWN, 24);
    CHECK(track.sampler[sampler::Root] == 48);
    rig.key(STICK_UP, true);
    rig.frames(200);
    rig.key(STICK_UP, false);
    rig.frames(1);
    CHECK(track.sampler[sampler::Root] == 127);
    rig.key(A, false);
    // B puts a setting back; on SOUND it takes the recording away.
    rig.tap(B);
    CHECK(track.sampler[sampler::Root] == 60);
    // The engine has it: with the root an octave up, the root key... is the key that plays it as made.
    session.setSamplerSetting(1, sampler::Release, 0);
    session.setSamplerSetting(1, sampler::Root, 72);
    rig.frames(60);
    rig.render(100000);
    session.audition(1, 60, 1.f); // an octave under the root: half speed
    rig.frames(1);
    length = Rig::sounding(rig.render(20000));
    CHECK(length == 20000); // still going: it would take a second
    rig.frames(60);
    rig.render(100000);
    session.setSamplerSetting(1, sampler::Root, 60);
    session.setSamplerSetting(1, sampler::Loop, 7);
    CHECK(track.sampler[sampler::Loop] == 1);
    session.setSamplerSetting(1, sampler::Loop, 0);
    session.setSamplerSetting(1, 99, 5);
    session.setSamplerSetting(0, sampler::Root, 5); // not a sampler track
    session.setTrackSample(0, "alpha");
    CHECK(rig.project().tracks[0].sample[0] == 0 && rig.project().tracks[0].sampler[sampler::Root] == 60);

    // It plays from the piano roll, and a chord stamps on it like on a synth.
    session.view = View::Pattern;
    rig.app.notes().moveCursorTo(60, 2);
    rig.tap(A);
    rig.frames(60);
    rig.render(100000);
    rig.tap(START);
    const std::vector<float> bar = rig.render(8 * kStep);
    int first = -1;
    for (std::size_t i = 0; i < bar.size() && first < 0; i += 2)
        if (bar[i] != 0.f) first = static_cast<int>(i / 2);
    CHECK(first >= 2 * kStep && first <= 2 * kStep + 2);
    rig.tap(START);
    rig.app.notes().moveCursorTo(60, 8);
    rig.tap(Y);
    CHECK(rig.app.notes().chordCount() == 1);

    // The song file keeps the recording by name and the settings.
    session.setSamplerSetting(1, sampler::Start, 25);
    const std::string text = songText(rig.project());
    CHECK(text.find("track sampler\nsound alpha\nsampler 60 0 25 0 0 0 80\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    // Opened on another handheld with the same card, it sounds the same.
    Rig other(folder);
    other.project() = *back;
    other.session().applyProject();
    CHECK(other.session().trackSound(1).peaks.size() > 100);
    // ...the same as one set up by hand, with no song opened.
    Rig fresh(folder);
    fresh.session().addTrack(TrackKind::Sampler);
    fresh.session().setTrackSample(1, "alpha");
    fresh.session().setSamplerSetting(1, sampler::Start, 25);
    fresh.session().setSamplerSetting(1, sampler::Release, 0);
    fresh.project().tracks[1].notes[0][0] = back->tracks[1].notes[0][0];
    fresh.session().sendNotePattern(1, 0);
    other.session().view = fresh.session().view = View::Pattern;
    other.project().track = fresh.project().track = 1;
    other.tap(START);
    fresh.tap(START);
    const std::vector<float> a = other.render(16 * kStep), b = fresh.render(16 * kStep);
    float peak = 0.f;
    for (float v : a) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    CHECK(peak > 0.02f && a == b);
    // A recording that is not on the card: silent, and it says so.
    session.setTrackSample(1, "gone");
    CHECK(session.trackSound(1).missing && std::strcmp(track.sample, "gone") == 0);
    rig.frames(60);
    rig.render(100000);
    session.audition(1, 60, 1.f);
    CHECK(heard(4800) == 0);

    // Y catches the last bar onto it.
    session.setTrackSample(1, "");
    rig.project().track = 0;
    rig.app.drums().setStep(0, 0, 1.f);
    rig.tap(START);
    rig.render(20 * kStep);
    rig.tap(START);
    rig.project().track = 1;
    session.view = View::Device;
    rig.tap(Y);
    CHECK(std::strcmp(track.sample, "catch 1") == 0 && screen.said().find("CATCH 1") != std::string::npos);
    CHECK(session.trackSound(1).peaks.size() > 100);
    rig.frames(170);
    CHECK(screen.said().empty());
    screen.goTo(0);
    rig.tap(B);
    CHECK(track.sample[0] == 0);
    // Lines that make no sense in a song file are left out.
    std::unique_ptr<Project> song(new Project);
    CHECK(parseSong("apotheca-song 1\ntrack sampler\nsound ../x\nsampler 999 -999 50\ntrack drums\nsound kick\nsampler 1 2 3\n", *song));
    CHECK(song->tracks[0].kind == TrackKind::Sampler && song->tracks[0].sample[0] == 0);
    CHECK(song->tracks[0].sampler[sampler::Root] == 127 && song->tracks[0].sampler[sampler::Tune] == -100 && song->tracks[0].sampler[sampler::Start] == 50);
    CHECK(song->tracks[0].sampler[sampler::Level] == 80 && song->tracks[1].sample[0] == 0 && song->tracks[1].sampler[sampler::Root] == 60);
    // How settings read.
    char line[32];
    samplerSettingText(sampler::Root, 60, line, sizeof line);
    CHECK(std::string(line) == "C4");
    samplerSettingText(sampler::Tune, -7, line, sizeof line);
    CHECK(std::string(line) == "-7 CENTS");
    samplerSettingText(sampler::Attack, 0, line, sizeof line);
    CHECK(std::string(line) == "1 MS");
    samplerSettingText(sampler::Release, 100, line, sizeof line);
    CHECK(std::string(line) == "20.0 S"); // the shared envelope: release runs to twenty seconds
    samplerSettingText(sampler::Loop, 1, line, sizeof line);
    CHECK(std::string(line) == "ON");
    samplerSettingText(sampler::Level, 80, line, sizeof line);
    CHECK(std::string(line) == "80%");
}

// ---- recording ---------------------------------------------------------------------------

void testTheRecorder() {
    Recorder r;
    CHECK(r.state() == Recorder::Idle && r.finish().empty());
    std::vector<float> quiet(48000, 0.001f); // hiss, under the level that counts as sound
    r.feed(quiet.data(), 48000); // not armed: ignored
    CHECK(r.state() == Recorder::Idle);
    r.arm();
    CHECK(r.state() == Recorder::Waiting);
    r.feed(quiet.data(), 48000);
    CHECK(r.state() == Recorder::Waiting && r.seconds() == 0.f); // still waiting: silence is not kept
    // Sound: it starts, with the 20 ms before it.
    const std::vector<float> sound = tone(0.5f, 0.25f);
    r.feed(sound.data(), static_cast<int>(sound.size()));
    CHECK(r.state() == Recorder::Recording && r.level() > 0.2f);
    CHECK(std::fabs(r.seconds() - 0.52f) < 0.005f);
    r.feed(quiet.data(), 48000); // and a second of nothing after
    std::vector<float> take = r.finish();
    CHECK(r.state() == Recorder::Idle);
    // Tidied: the front is the moment before the sound, the silence on the end is gone bar 50 ms.
    CHECK(std::abs(static_cast<int>(take.size()) - (960 + 24000 + 2400)) <= 300);
    // Brought up: the tone came in at a quarter and goes out near nine tenths.
    float peak = 0.f;
    for (float v : take) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    CHECK(std::fabs(peak - 0.9f) < 0.01f);
    CHECK(std::fabs(take.back()) < 1e-4f && std::fabs(take[2000]) > 0.05f);
    // Armed and stopped with no sound: nothing.
    r.arm();
    r.feed(quiet.data(), 48000);
    CHECK(r.finish().empty());
    // Ten seconds is the most.
    r.arm();
    const std::vector<float> loud = tone(1.f, 0.95f);
    for (int i = 0; i < 9; ++i) r.feed(loud.data(), 48000);
    CHECK(!r.full());
    for (int i = 0; i < 3; ++i) r.feed(loud.data(), 48000);
    CHECK(r.full() && std::fabs(r.seconds() - 10.f) < 0.001f);
    take = r.finish();
    CHECK(take.size() == 480000);
    peak = 0.f;
    for (float v : take) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    CHECK(std::fabs(peak - 0.95f) < 0.01f); // already loud: left as it is
    // Very quiet sound is brought up eight times at most, and nonsense from a driver is dropped.
    r.arm();
    std::vector<float> faint = tone(0.2f, 0.03f);
    faint[100] = std::nanf("");
    faint[101] = 50.f;
    r.feed(faint.data(), static_cast<int>(faint.size()));
    take = r.finish();
    peak = 0.f;
    for (float v : take) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    CHECK(std::fabs(peak - 0.24f) < 0.01f);
}

// An input made of whatever the test gives it.
struct FakeInput : AudioInput {
    bool there = true, open = false;
    std::vector<float> coming;
    std::size_t at = 0;
    bool start() override { open = there; return there; }
    void stop() override { open = false; }
    int read(float* out, int most) override {
        int got = 0;
        while (open && got < most && at < coming.size()) out[got++] = coming[at++];
        return got;
    }
    void add(const std::vector<float>& v) { coming.insert(coming.end(), v.begin(), v.end()); }
};

void testRecordingOntoALane() {
    const std::string folder = tempFolder();
    Rig rig(folder);
    Session& session = rig.session();
    FakeInput input;
    KitScreen& kit = rig.app.kit();
    TrackData& track = rig.project().tracks[0];
    session.view = View::Device;
    kit.goTo(3, KitScreen::kSound);

    // Nothing to record from: it says so, and nothing changes.
    rig.tap(R1);
    CHECK(session.recording() == Session::Rec::NoInput && kit.said().find("NO INPUT") != std::string::npos);
    session.useAudioInput(&input);
    input.there = false;
    rig.tap(R1);
    CHECK(session.recording() == Session::Rec::NoInput && !input.open);
    input.there = true;

    // R1: it waits for sound. Silence comes in and nothing is kept.
    rig.tap(R1);
    CHECK(session.recording() == Session::Rec::Waiting && input.open);
    input.add(std::vector<float>(24000, 0.f));
    rig.frames(5);
    CHECK(session.recording() == Session::Rec::Waiting && session.recordSeconds() == 0.f);
    CHECK(kit.said().find("WAITING") != std::string::npos);
    // Sound comes: it is recording, and says how long.
    input.add(tone(0.5f, 0.25f));
    rig.frames(5);
    CHECK(session.recording() == Session::Rec::Recording && session.recordSeconds() > 0.4f && session.recordLevel() > 0.1f);
    CHECK(kit.said().find("RECORDING") != std::string::npos);
    // R1 again: stopped, saved, and on the lane.
    rig.tap(R1);
    CHECK(session.recording() == Session::Rec::Idle && !input.open);
    CHECK(session.lastRecorded() == "rec 1" && std::strcmp(track.laneSample[3], "rec 1") == 0 && track.laneFit[3] == 0);
    rig.frames(1);
    CHECK(kit.said().find("REC 1") != std::string::npos);
    std::vector<float> file;
    CHECK(readWav(folder + "/rec 1.wav", file) && std::abs(static_cast<int>(file.size()) - (960 + 24000)) <= 300);
    const int played = rig.hit(3); // the lane plays it: all but the silent 20 ms on its front and the last faded sample
    CHECK(played > static_cast<int>(file.size()) - 1000 && played <= static_cast<int>(file.size()));
    rig.frames(200);
    CHECK(kit.said().empty());

    // Started and stopped with no sound: nothing saved, the lane left alone, and it says so.
    rig.tap(R1);
    input.add(std::vector<float>(4800, 0.f));
    rig.frames(3);
    rig.tap(R1);
    CHECK(session.lastRecorded().empty() && std::strcmp(track.laneSample[3], "rec 1") == 0 && wavNames(folder).size() == 1);
    rig.frames(1);
    CHECK(kit.said().find("NOTHING RECORDED") != std::string::npos);

    // Ten seconds: it stops by itself, as "rec 2", on the lane the cursor was on when it began.
    kit.goTo(5, KitScreen::kSound);
    rig.tap(R1);
    rig.frames(1);
    kit.goTo(6, KitScreen::kSound);
    for (int i = 0; i < 11; ++i) input.add(tone(1.f, 0.5f));
    for (int i = 0; i < 400 && session.recording() != Session::Rec::Idle; ++i) rig.frames(1);
    CHECK(session.recording() == Session::Rec::Idle && !input.open && session.lastRecorded() == "rec 2");
    CHECK(std::strcmp(track.laneSample[5], "rec 2") == 0 && track.laneSample[6][0] == 0);
    CHECK(readWav(folder + "/rec 2.wav", file) && file.size() == 480000);
    rig.frames(1);
    CHECK(kit.said().find("REC 2") != std::string::npos);
    // Not while A is held.
    rig.key(A, true);
    rig.tap(R1);
    rig.key(A, false);
    CHECK(session.recording() == Session::Rec::Idle);

    // Onto a sampler track: its one recording.
    session.addTrack(TrackKind::Sampler);
    rig.project().track = 1;
    rig.tap(R1);
    CHECK(session.recording() == Session::Rec::Waiting);
    input.add(tone(0.3f, 0.25f));
    rig.frames(3);
    rig.tap(R1);
    CHECK(std::strcmp(rig.project().tracks[1].sample, "rec 3") == 0 && session.trackSound(1).peaks.size() > 100);
    rig.frames(1);
    CHECK(rig.app.sampler().said().find("REC 3") != std::string::npos);
    // A synth track has nowhere to put one. Nor has a session with no sample folder.
    session.addTrack(TrackKind::Phazed);
    CHECK(session.toggleRecording(2, 0) == Session::Rec::Idle && !input.open);
    CHECK(session.toggleRecording(0, 9) == Session::Rec::Idle && session.toggleRecording(12, 0) == Session::Rec::Idle);
    Rig bare;
    bare.session().useAudioInput(&input);
    CHECK(bare.session().toggleRecording(0, 0) == Session::Rec::NoInput);
    // A session that goes away mid-recording lets go of the input.
    {
        Rig going(folder);
        going.session().useAudioInput(&input);
        CHECK(going.session().toggleRecording(0, 0) == Session::Rec::Waiting && input.open);
    }
    CHECK(!input.open);
}

} // namespace

// ---- the list of recordings ---------------------------------------------------------------

void testTheSamplePicker() {
    const std::string folder = tempFolder();
    // Twenty recordings, each a different length so that they can be told apart by ear.
    char name[32];
    for (int i = 0; i < 20; ++i) {
        std::snprintf(name, sizeof name, "/take %02d.wav", i + 1);
        std::vector<float> made(static_cast<std::size_t>(2000 + 500 * i));
        for (std::size_t n = 0; n < made.size(); ++n) made[n] = 0.5f * std::sin(0.05f * static_cast<float>(n));
        CHECK(writeWav(folder + name, made, 48000));
    }
    Rig rig(folder);
    Session& session = rig.app.session();
    session.view = View::Device;
    KitScreen& kit = rig.app.kit();
    SamplePicker& list = kit.picker();
    const TrackData& track = rig.app.project().tracks[0];
    kit.goTo(5, KitScreen::kSound);
    rig.frames(1);
    const int drum = rig.hit(5);

    // A tap of A on SOUND opens it, on the built-in drum, with the lane as it was.
    rig.tap(A);
    CHECK(list.isOpen() && list.cursor() == 0 && list.count() == 21 && track.laneSample[5][0] == 0);
    // Down: the next one is on the lane at once, and (the song being stopped) it is heard.
    rig.render(96000);
    rig.tap(DOWN);
    CHECK(list.cursor() == 1 && std::string(track.laneSample[5]) == "take 01");
    CHECK(Rig::sounding(rig.render(20000)) > 1500);
    rig.tap(DOWN, 2);
    CHECK(std::string(track.laneSample[5]) == "take 03");
    // Right and left: eight at a time; the ends stop.
    rig.tap(RIGHT);
    CHECK(list.cursor() == 11 && std::string(track.laneSample[5]) == "take 11");
    rig.tap(RIGHT, 3);
    CHECK(list.cursor() == 20 && std::string(track.laneSample[5]) == "take 20");
    rig.render(96000);
    rig.tap(DOWN); // at the end: nothing moves, so nothing is loaded or played again
    CHECK(list.cursor() == 20 && Rig::sounding(rig.render(20000)) == 0);
    rig.tap(LEFT);
    CHECK(list.cursor() == 12);
    // While it is open the kit's own buttons do nothing: Y does not catch, R1 does not record, and the lane cursor stays.
    rig.tap(Y);
    rig.tap(R1);
    CHECK(kit.cursorLane() == 5 && kit.said().empty() && session.recording() == Session::Rec::Idle);
    // X hears it again.
    rig.render(96000);
    rig.tap(X);
    CHECK(Rig::sounding(rig.render(20000)) > 1500);
    // A keeps what is showing.
    rig.tap(A);
    CHECK(!list.isOpen() && std::string(track.laneSample[5]) == "take 12");
    CHECK(std::abs(rig.hit(5) - (2000 + 500 * 11)) < 400);
    // Opened again it is on that one. B puts back what was there when it opened, wherever the cursor went.
    rig.tap(A);
    CHECK(list.isOpen() && list.cursor() == 12);
    rig.tap(LEFT, 2);
    CHECK(list.cursor() == 0 && track.laneSample[5][0] == 0); // the first row is the drum itself
    rig.tap(DOWN, 4);
    CHECK(std::string(track.laneSample[5]) == "take 04");
    rig.tap(B);
    CHECK(!list.isOpen() && std::string(track.laneSample[5]) == "take 12");
    // B with nothing moved changes nothing (and does not load it again).
    rig.tap(A);
    rig.tap(B);
    CHECK(!list.isOpen() && std::string(track.laneSample[5]) == "take 12");

    // The stick scrolls it: the first row at once, then about eight a second.
    rig.tap(A);
    rig.tap(LEFT, 2);
    rig.key(STICK_DOWN, true);
    rig.frames(1);
    CHECK(list.cursor() == 1);
    rig.frames(62); // a second at the stick's own speed (three quarters)
    rig.key(STICK_DOWN, false);
    CHECK(list.cursor() >= 6 && list.cursor() <= 8);
    // A D-pad direction held runs too.
    const int from = list.cursor();
    rig.key(DOWN, true);
    rig.frames(40);
    rig.key(DOWN, false);
    CHECK(list.cursor() >= from + 3 && list.cursor() <= from + 6);
    rig.tap(B);

    // With the song playing, moving does not also play the sound by itself: it is heard in the beat.
    for (int step = 0; step < kSteps; ++step) rig.app.drums().setStep(5, step, 0.f);
    rig.tap(A);
    rig.tap(START); // START still plays and stops with the list open
    CHECK(rig.app.project().playing && list.isOpen());
    rig.render(3000);
    rig.frames(1);
    rig.tap(DOWN);
    CHECK(Rig::sounding(rig.render(6000)) == 0); // lane 6 has no hits, and moving does not play it: silence
    rig.tap(START);
    rig.tap(B);
    rig.render(96000);

    // Hold A and up / down still steps one at a time, and letting go is then not a tap.
    kit.goTo(2, KitScreen::kSound);
    rig.key(A, true);
    rig.tap(UP);
    rig.key(A, false);
    CHECK(!list.isOpen() && std::string(track.laneSample[2]) == "take 01");
    // On FIT a tap of A is not the list.
    kit.goTo(2, KitScreen::kFit);
    rig.tap(A);
    CHECK(!list.isOpen());
    CHECK(drum > 0);

    // The sampler has the same list, for its one recording.
    rig.app.session().view = View::Song;
    rig.app.song().moveCursorTo(0, 1);
    rig.tap(A);
    rig.tap(DOWN, 2);
    rig.tap(A); // SAMPLER
    CHECK(rig.app.project().tracks[1].kind == TrackKind::Sampler);
    rig.app.project().track = 1;
    session.view = View::Device;
    rig.frames(1);
    SamplePicker& one = rig.app.sampler().picker();
    rig.tap(A);
    CHECK(one.isOpen() && one.cursor() == 0 && one.count() == 21);
    rig.tap(DOWN, 3);
    CHECK(std::string(rig.app.project().tracks[1].sample) == "take 03");
    rig.tap(B);
    CHECK(!one.isOpen() && rig.app.project().tracks[1].sample[0] == 0);
    rig.tap(A);
    rig.tap(DOWN, 2);
    rig.tap(A);
    CHECK(!one.isOpen() && std::string(rig.app.project().tracks[1].sample) == "take 02");
    // On one of its settings, a tap of A is not the list.
    rig.app.sampler().goTo(2);
    rig.tap(A);
    CHECK(!one.isOpen());

    // No recordings at all: the list opens with its one row, and says how to get some.
    Rig empty(tempFolder());
    empty.app.session().view = View::Device;
    empty.app.kit().goTo(0, KitScreen::kSound);
    empty.tap(A);
    CHECK(empty.app.kit().picker().isOpen() && empty.app.kit().picker().count() == 1);
    empty.tap(DOWN);
    empty.tap(RIGHT);
    CHECK(empty.app.kit().picker().cursor() == 0);
    empty.tap(A);
    CHECK(!empty.app.kit().picker().isOpen());
}

int main(int, char**) {
    testTheSamplePicker();
    testReadingWavFiles();
    testStretching();
    testFittingWithSignalsmith();
    testARecordingOnALane();
    testFittingToTheTempo();
    testTheSongKeepsItsRecordings();
    testWhereAHitStarts();
    testRollingALanesStarts();
    testTheKitScreen();
    testCatch();
    testTheSamplerTrack();
    testTheRecorder();
    testRecordingOntoALane();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("sample tests passed\n");
    return 0;
}
