// How hard the sound works, for the help card (owner, 2026-11-15): the engine times each render()
// against how long its sound lasts (apotheca/load_meter.hpp), and the app reads the whole program's
// time from /proc/self/stat (app/cpu_use.hpp).
//
//   load_test            every test
//   load_test --measure  what the timing costs a render() call

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <stdlib.h>
#include <unistd.h>

#include "apotheca/engine.hpp"
#include "apotheca/load_meter.hpp"
#include "../app/cpu_use.hpp"

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

bool near(double a, double b, double within) { return std::fabs(a - b) <= within; }

// The rule by hand: a second of sound's calls, their share and the worst one.
void testTheRule() {
    LoadMeter meter;
    CHECK(meter.share() < 0.f && meter.worst() < 0.f && meter.windows() == 0); // nothing known yet
    const double block = 256.0 / 48000.0;
    int calls = 0;
    for (; calls < 186; ++calls) meter.add(0.25 * block, block); // 0.992 s: not yet a second
    CHECK(meter.windows() == 0 && meter.share() < 0.f);
    meter.add(0.25 * block, block);
    meter.add(1.5 * block, block); // one call that took longer than its sound lasts (a stutter)
    CHECK(meter.windows() == 1);
    CHECK(near(meter.share(), (186.0 * 0.25 + 0.25 + 1.5) / 188.0, 1e-5));
    CHECK(near(meter.worst(), 1.5, 1e-5));
    // The next second starts afresh: its own share, its own worst.
    for (int i = 0; i < 188; ++i) meter.add(0.5 * block, block);
    CHECK(meter.windows() == 2 && near(meter.share(), 0.5, 1e-5) && near(meter.worst(), 0.5, 1e-5));
    // Nothing made, or a time that is no time: not counted.
    meter.add(0.1, 0.0);
    meter.add(-1.0, block);
    meter.add(NAN, block);
    for (int i = 0; i < 187; ++i) meter.add(0.1 * block, block);
    CHECK(meter.windows() == 2); // (0.997 s counted: the three above added no sound)
    meter.add(0.1 * block, block);
    CHECK(meter.windows() == 3 && near(meter.share(), 0.1, 1e-5) && near(meter.worst(), 0.1, 1e-5));
}

// An instrument that takes `part` of each block's own time to make it: a slow sound.
struct Slow : Instrument {
    double part;
    float rate = 48000.f;
    explicit Slow(double p) : part(p) {}
    void prepare(float sampleRate) override { rate = sampleRate; }
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int, float) override {}
    void noteOn(int, float) override {}
    void noteOff(int) override {}
    void allNotesOff() override {}
    void render(float* left, float* right, int frames) override {
        const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(part * frames / rate);
        while (std::chrono::steady_clock::now() < until) {}
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
    }
};

// What the engine says of its own render(): a slow instrument reads high, an empty engine low.
void testASlowRenderReadsHigh() {
    for (const double part : {0.0, 0.6}) {
        std::unique_ptr<Engine> engine(new Engine);
        engine->prepare(48000.f);
        if (part > 0.0) {
            Command c;
            c.type = Command::InstallSynth;
            c.track = 1; // (a fresh engine's first track holds the drums)
            c.pointer = new Slow(part);
            CHECK(engine->send(c));
        }
        std::vector<float> out(2 * 256);
        const unsigned before = engine->loadSeconds();
        CHECK(engine->soundShare() < 0.f && before == 0);
        for (int i = 0; i < 48000 * 13 / 10 / 256; ++i) engine->render(out.data(), 256); // 1.3 s of sound
        CHECK(engine->loadSeconds() == 1);
        const float share = engine->soundShare(), worst = engine->soundWorst();
        std::printf("  an instrument taking %.0f%% of its time: SOUND %.0f%%, WORST %.0f%%\n", 100.0 * part, 100.0 * share, 100.0 * worst);
        CHECK(worst >= share);
        if (part > 0.0) CHECK(share >= 0.6f && share < 1.5f); // (a busy desk only makes it slower)
        else CHECK(share < 0.3f);
    }
}

// The whole program's time: utime + stime from the stat file, over the wall time, over the cores.
void testTheProgramsTime() {
    CHECK(statTicks("123 (apotheca) S 1 2 3 4 5 6 7 8 9 10 250 50 0 0") == 300);
    CHECK(statTicks("123 (a (b) c) R 1 2 3 4 -1 6 7 8 9 10 7 3 0 0") == 10); // a name with brackets and spaces
    CHECK(statTicks("nonsense") == -1 && statTicks(nullptr) == -1);
    CHECK(statTicks(std::string("1 (x) S 1 2 3").c_str()) == -1); // cut short
    // A made-up file read as the program would: four cores, 100 ticks a second.
    char pattern[] = "/tmp/apotheca-load-XXXXXX";
    const std::string folder = mkdtemp(pattern), file = folder + "/stat";
    const auto write = [&](long utime, long stime) {
        std::FILE* f = std::fopen(file.c_str(), "w");
        std::fprintf(f, "77 (apotheca) S 1 2 3 4 5 6 7 8 9 10 %ld %ld 0 0 20 0\n", utime, stime);
        std::fclose(f);
    };
    ProgramCpu cpu(file);
    cpu.pretend(100.0, 4);
    write(1000, 200);
    CHECK(!cpu.read(5000) && cpu.percent() == -1); // the first reading: where it starts from
    write(1080, 220); // a second later, a whole core: 100 ticks
    CHECK(!cpu.read(5500) && cpu.percent() == -1); // half a second: not read again yet
    CHECK(cpu.read(6000) && cpu.percent() == 25);  // one core of four
    write(1080 + 300, 220 + 100); // two seconds later, two cores
    CHECK(cpu.read(8000) && cpu.percent() == 50);
    cpu.restart();
    CHECK(cpu.percent() == -1 && !cpu.read(9000));
    // The real one reads (it moves, so only that it is a share).
    ProgramCpu self;
    CHECK(!self.read(1000));
    for (volatile long spin = 0; spin < 50000000; spin = spin + 1) {}
    usleep(200000);
    CHECK(self.read(3000) && self.percent() >= 0 && self.percent() <= 100);
    std::remove(file.c_str());
    rmdir(folder.c_str());
}

// What the timing costs a call: the clock read twice and a few sums.
void measure() {
    LoadMeter meter;
    const int calls = 2000000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < calls; ++i) {
        const LoadMeter::Timing timed(meter, 256.0 / 48000.0);
    }
    const double each = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / calls;
    std::printf("a render() call timed: %.1f ns, %.4f%% of a 256-frame block at 48 kHz (%.2f ms)\n", 1e9 * each, 100.0 * each / (256.0 / 48000.0), 1000.0 * 256.0 / 48000.0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
        measure();
        return 0;
    }
    testTheRule();
    testASlowRenderReadsHigh();
    testTheProgramsTime();
    if (failures) std::printf("%d check(s) failed\n", failures);
    else std::printf("load_test: all passed\n");
    return failures ? 1 : 0;
}
