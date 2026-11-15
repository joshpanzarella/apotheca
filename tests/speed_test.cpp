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

#include "apotheca/engine.hpp"
#include "bench_run.hpp"

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

} // namespace

int main(int argc, char** argv) {
    const struct { const char* name; void (*run)(); } tests[] = {
        {"testHeaviestFirst", testHeaviestFirst},
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
