#pragma once

#include <string>

namespace apotheca {
// The timings, printed (see bench_run.cpp). Returns the program's exit code.
int runBench();
// Only the app's rows (undo, the song's text, a frame): `bench --app`.
int runAppBench();

// One of the bench's setups rendered for `seconds` of sound with no clock and nothing printed
// but its name, for callgrind (`bench --profile NAME [SECONDS]`; docs/plans/speed-plan.md). The
// rendering is all inside profiledRender(), so `--toggle-collect='*profiledRender*'` counts only
// it. An unknown NAME prints the names. Returns the program's exit code.
int runProfile(const char* name, double seconds);

// What the sound card itself runs at: the default output opened with any change allowed, said
// in words, and shut again. SDL's audio must be started. The program asks once at start, before
// it opens the card for its own sound (main.cpp), and says it to the SPEED TEST through the
// environment (kSoundCardVariable): the test cannot open a card the program holds.
std::string probeSoundCard();
constexpr const char* kSoundCardVariable = "APOTHECA_SOUND_CARD";
constexpr const char* kRendererVariable = "APOTHECA_RENDERER"; // the screen's renderer, said at start for the SPEED TEST

// The busiest song a player is likely to make, played in real time in the
// program's own blocks (256 frames, 5.33 ms) for `seconds`, on the audio
// thread alone (helpers 0) or with helper threads. Timed by the clock on the
// wall, as the sound card sees it.
struct WholeSongTimes {
    int blocks = 0;          // how many were timed
    int helpers = 0;         // the helpers the engine had (Engine::workers())
    bool realtime = false;   // the timing thread was given realtime priority
    double meanMs = 0.0, p99Ms = 0.0, worstMs = 0.0;
    int over = 0;            // blocks that took longer than a block lasts: heard as a gap
    float peak = 0.f;        // the loudest sample: it was playing
    double chunksPerSecond = 0.0; // chunks the engine cut the sound into (a MOD's tick cuts more)
};
// `mods`: how many MODs run (0, 1, or 24: two on every track), for the bench's own rows.
WholeSongTimes timeWholeSong(int helpers, double seconds, int mods = 0);

// That song, sent to a fresh engine and set playing (what the rows above time). For bench_test,
// which holds what the song is: nothing else in ctest runs the timings.
class Engine;
void sendBusiestSong(Engine& engine);
// Its two FM-6 tracks (8 and 9) are on this preset: E.PIANO 1. The two JAX-EP tracks (6 and 7) are on A1.
constexpr int kBusiestFm6Preset = 10;
// Its MODs, as the timing rows run them: 1 (on the first drum track's ECHO send) or 24 (two a
// track, every kind of target, the DEPTH lanes written). For bench_test, as the song.
void sendBusiestMods(Engine& engine, int mods);
}
