// Late sound blocks (app/late_blocks.hpp): the rule for late, what the audio thread counts and the screen's thread
// takes, the line written for one, and the owner's click (2026-11-25) on the FX screen: a move from one of FILTER's
// dials to the next sends the engine nothing, so the audio thread has nothing of the move's to do.
//
// What only the handheld can show is not here: whether a click it makes comes with a late block (logs/sound.log).

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#pragma GCC diagnostic ignored "-Wformat-truncation" // (testTheLine cuts a line short on purpose: lateLine is inlined)
#include "late_blocks.hpp"

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

constexpr std::int64_t kBlock = 8000; // 384 frames at 48 kHz, in microseconds: the program's block (kCardBlockFrames)

void testTheRule() {
    // The card holds two blocks (16 ms): late is a gap and a call together past that, when the card ran dry.
    CHECK(kCardBlockFrames == 384 && kCardHoldsBlocks == 2);
    CHECK(std::fabs(cardHoldsMs(kCardBlockFrames, 48000) - 16.0) < 1e-9);
    CHECK(std::fabs(cardHoldsMs(256, 48000) - 32.0 / 3.0) < 1e-9); // the old block's card: 10.7 ms
    CHECK(!LateBlocks::isLate(kBlock, kBlock / 2, kBlock));          // on time
    CHECK(!LateBlocks::isLate(2 * kBlock - 100, 100, kBlock));         // two blocks to the microsecond: not yet
    CHECK(LateBlocks::isLate(2 * kBlock - 100, 101, kBlock));          // a microsecond more: the card ran dry
    CHECK(LateBlocks::isLate(2 * kBlock + 1, 0, kBlock));              // past two blocks after the one before
    CHECK(!LateBlocks::isLate(15000, 100, kBlock));                    // the handheld's 11-15 ms gaps (app 0.981): not heard now
    CHECK(LateBlocks::isLate(11000, 100, 5333));                       // but heard with the old 256-frame block (10.7 ms)
    CHECK(LateBlocks::isLate(kBlock, kBlock + 1, kBlock));             // a call longer than its sound, after a usual gap
    CHECK(!LateBlocks::isLate(0, 0, kBlock));                          // back to back (the card catching up)
    CHECK(!LateBlocks::isLate(1000000, 1000000, 0));                   // no block: nothing is late
}

void testCounted() {
    LateBlocks late;
    LateBlock got;
    CHECK(!late.take(got));
    std::int64_t at = 1000000000; // a steady clock's microseconds
    late.block(at, 100, kBlock); // the first call: nothing before it, whatever the clock says
    for (int i = 0; i < 100; ++i) late.block(at += kBlock, 1000, kBlock);
    CHECK(late.count() == 0 && !late.take(got));
    late.block(at += 31000, 2100, kBlock); // 31 ms after the one before
    CHECK(late.count() == 1);
    CHECK(late.take(got));
    CHECK(got.count == 1 && got.since == 1 && got.gapUs == 31000 && got.tookUs == 2100 && got.blockUs == kBlock);
    CHECK(!late.take(got)); // taken once
    late.block(at += kBlock, 9000, kBlock);  // slow: 8 ms after, 9 ms long, past the card's 16
    late.block(at += 20000, 100, kBlock);    // and late
    late.block(at += kBlock, 100, kBlock);   // on time again
    CHECK(late.take(got));
    CHECK(got.count == 3 && got.since == 2 && got.gapUs == 20000 && got.tookUs == 100); // the latest's numbers
    // The card opened again after a second away: that second is no late block.
    late.restart();
    late.block(at += 1000000, 100, kBlock);
    late.block(at += kBlock, 100, kBlock);
    CHECK(late.count() == 3 && !late.take(got));
}

void testTheLine() {
    LateBlock got;
    got.count = 4, got.since = 1, got.gapUs = 31200, got.tookUs = 2100, got.blockUs = 8000;
    char line[400];
    lateLine(line, sizeof line, got, "FX, playing, FX track 1 line 1 FILTER, dial 2 CUTOFF", 18.04, 12.0);
    CHECK(std::strstr(line, "began 31.2 ms after the one before, took 2.1 ms (a block is 8.0 ms, the card holds 16.0)") != nullptr);
    CHECK(std::strstr(line, "1 since the last line, 4 this run") != nullptr);
    CHECK(std::strstr(line, "dial 2 CUTOFF") != nullptr);
    CHECK(std::strstr(line, "the pass before took 18.0 ms (drawing 12.0)") != nullptr);
    std::vector<char> small(9, 'x');
    lateLine(small.data(), 8, got, nullptr, 0.0, 0.0);
    CHECK(std::strlen(small.data()) == 7 && small[8] == 'x'); // cut short, never past the end
}

// The log's first line says the block the card took and what the card holds, so the next log confirms the buffer.
void testTheFirstLine() {
    char line[240];
    soundFirstLine(line, sizeof line, "0.99", 48000, 384, "alsa", true, 3, 3);
    CHECK(std::strcmp(line, "Apotheca 0.99: sound at 48000 Hz, 384 frames a block (8.0 ms), the card holds two: 16.0 ms (alsa); realtime priority granted, helpers 3 of 3") == 0);
    soundFirstLine(line, sizeof line, "0.99", 48000, 384, "", true, 3, 3);
    CHECK(std::strstr(line, "16.0 ms (no driver);") != nullptr);
    soundFirstLine(line, sizeof line, "0.99", 48000, 512, nullptr, false, 0, 3);
    CHECK(std::strstr(line, "512 frames a block (10.7 ms), the card holds two: 21.3 ms (no driver); realtime priority NOT granted, helpers 0 of 3") != nullptr);
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    void frames(int count, bool render = true) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            if (render) {
                std::vector<float> out(2 * 256);
                engine->render(out.data(), 256);
            }
        }
    }
    void press(Button b) {
        ButtonEvent e;
        e.button = b;
        e.down = true;
        app.onButton(e, input);
        frames(1, false);
        e.down = false;
        app.onButton(e, input);
        frames(1, false);
    }
};

// The owner's click: FILTER in track 1's first slot, the song playing, the cursor moved from dial to dial with no
// sound made meanwhile. Nothing waits in the engine's queue after: no command was sent.
void testFilterDialsSendNothing() {
    for (const bool playing : {false, true}) {
        Rig rig;
        Session& s = rig.app.session();
        s.setEffect(0, 0, EffectKind::Filter);
        if (playing) s.togglePlay(true);
        s.view = View::Effects;
        rig.app.fx().goTo(0, 1);
        rig.frames(10);
        CHECK(rig.engine->caughtUp());
        for (int i = 0; i < 18; ++i) rig.press(i % 6 < 3 ? Button::Down : Button::Up);
        CHECK(rig.app.fx().cursorRow() == 0 && rig.app.fx().cursorColumn() == 1);
        CHECK(rig.engine->caughtUp()); // nothing sent by the moves
        // (The test can see a send: turning the dial does wait in the queue.)
        ButtonEvent a;
        a.button = Button::A, a.down = true;
        rig.app.onButton(a, rig.input);
        rig.press(Button::Up);
        CHECK(!rig.engine->caughtUp());
        a.down = false;
        rig.app.onButton(a, rig.input);
    }
}

void testCursorWords() {
    Rig rig;
    Session& s = rig.app.session();
    s.setEffect(0, 0, EffectKind::Filter);
    s.view = View::Effects;
    rig.app.fx().goTo(0, 2);
    char words[160];
    rig.app.fx().cursorWords(words, sizeof words);
    CHECK(std::strcmp(words, ", FX track 1 line 1 FILTER, dial 2 RESONANCE") == 0);
    rig.app.fx().goTo(0, 0);
    rig.app.fx().cursorWords(words, sizeof words);
    CHECK(std::strcmp(words, ", FX track 1 line 1 FILTER, dial 0 the list") == 0);
    rig.app.fx().goTo(3, 1);
    rig.app.fx().cursorWords(words, sizeof words);
    CHECK(std::strstr(words, "line SENDS") != nullptr && std::strstr(words, "dial 1 ECHO") != nullptr);
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testTheRule", testTheRule}, {"testCounted", testCounted}, {"testTheLine", testTheLine}, {"testTheFirstLine", testTheFirstLine},
        {"testFilterDialsSendNothing", testFilterDialsSendNothing}, {"testCursorWords", testCursorWords},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("late_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
