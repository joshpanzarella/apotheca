#pragma once

namespace apotheca {
// The timings, printed (see bench_run.cpp). Returns the program's exit code.
int runBench();

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
}
