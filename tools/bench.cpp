// The timings as a program of their own: ./build/bench. The work is in app/bench_run.cpp.
// `bench --profile NAME [SECONDS]`: one setup rendered for callgrind, nothing timed.
// `bench --app`: the app's rows alone (undo, the song as text and back, a frame).
#include <cstdlib>
#include <cstring>

#include "bench_run.hpp"

int main(int argc, char** argv) {
    if (argc > 2 && std::strcmp(argv[1], "--profile") == 0) return apotheca::runProfile(argv[2], argc > 3 ? std::atof(argv[3]) : 1.0);
    if (argc > 1 && std::strcmp(argv[1], "--app") == 0) return apotheca::runAppBench();
    return apotheca::runBench();
}
