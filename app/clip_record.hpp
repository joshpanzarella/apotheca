#pragma once
// CLIPS (desk only; owner, 2026-11-28): short videos of the app's features. A script of timed presses and stick
// pushes is played against the running app on a VIRTUAL clock, the screen kept at 30 frames a second with a band
// under it that shows the keys as they go down, and the sound written beside it, in step to the sample.
//
//   apotheca --demo --view NAME --script FILE --record OUTDIR      (scripts/make-clips.sh encodes OUTDIR)
//
// How the picture and the sound stay together: in a recording the sound card is never started. The app thread
// calls main.cpp's own audio callback itself, for exactly rate/60 frames each pass (two passes a frame), and the
// callback's output comes back through an AudioTap (a lock-free ring, filled on the thread that renders, emptied
// here). Pass p is at p/60 seconds: its presses go in, App::update gets that time, its sound is rendered, and on
// every other pass the screen is drawn. So frame k is at k/30 s and its sound starts at sample k * rate / 30:
// nothing hangs on how fast the desk is. Format and gestures: docs/dev/videos.md.
#include <SDL.h>

#include <atomic>
#include <string>
#include <vector>

#include "apotheca/spsc_queue.hpp"
#include "input.hpp"

namespace apotheca {

class App;
struct Fonts;

// The callback's output, for a recording: written where the sound is rendered (never waits, never allocates: a
// full ring drops and counts), read on the app thread.
class AudioTap {
public:
    void write(const float* samples, int count) {
        for (int i = 0; i < count; ++i)
            if (!ring_.push(samples[i])) dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    bool read(float& sample) { return ring_.pop(sample); }
    long dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    SpscQueue<float, 65536> ring_; // a pass is 1,600 (800 frames, two sides): forty passes of room
    std::atomic<long> dropped_{0};
};

// A clip's script, read from its file (docs/dev/videos.md). Times are seconds from the start of the run.
struct ClipScript {
    struct Event {
        enum Kind { Down, Up, Stick, Caption, Say } kind = Down;
        double at = 0.0;
        Button button = Button::None;
        bool hold = false;     // Down: shown as HLD- from the press (the script says it is a hold)
        float x = 0.f, y = 0.f; // Stick: where it is pushed to (-1..1; up and left negative); 0, 0 lets go
        std::string text;      // Caption, Say
        int order = 0;         // the file's order, for events at one time
    };
    std::string title, about, caption;
    double from = 0.0;   // recorded from here (the time before runs, heard by nobody: a song already going)
    double length = 10.0; // and up to here
    std::vector<Event> events; // in time order (the file's order within one time)

    // False, with `error` saying which line, if the text is not a script.
    bool parse(const std::string& text, std::string& error);
};

// What the band under the screen shows at a time: the keys of one combo, each lit while it is down; once all are
// let go, the combo fades out over kFadeSeconds so a quick tap can be read.
class KeyBand {
public:
    static constexpr double kFadeSeconds = 0.7;
    struct Chip {
        std::string key; // as drawKey reads it: "HOLD A", "RIGHT", "STICK LEFT"
        bool lit = false;
    };
    void apply(const ClipScript::Event& event);
    // How strongly the combo is drawn at `seconds`: 1 while a key is down, then down to 0.
    float strength(double seconds) const;
    const std::vector<Chip>& chips() const { return chips_; }
    // The combo as one line, "HOLD A + RIGHT" (for the tests and the log).
    std::string line() const;
    const std::string& caption() const { return caption_; }

private:
    void press(const std::string& key, bool hold, double at);
    void letGo(const std::string& key, double at);
    std::vector<Chip> chips_;
    double allUpAt_ = -1.0;
    std::string stick_; // the stick's chip while it is pushed ("STICK LEFT")
    std::string caption_;
};

struct ClipOptions {
    const char* script = nullptr; // --script FILE
    const char* out = nullptr;    // --record OUTDIR
    void (*callback)(void*, Uint8*, int) = nullptr; // main.cpp's audio callback, and its userdata
    void* userdata = nullptr;
    AudioTap* tap = nullptr; // where the callback puts what it rendered
    int rate = 48000;
};

// Plays the script and writes OUTDIR/frames.rgb (640x560 RGB24, the screen over the band), OUTDIR/sound.wav,
// OUTDIR/clip.srt and OUTDIR/clip.md (its narration), OUTDIR/presses.txt (each press as the app got it) and
// OUTDIR/clip.txt (frames, samples, rate). 0 when it all went; 1 with the reason on stderr.
int recordClip(App& app, Input& input, SDL_Renderer* renderer, const Fonts& fonts, const std::vector<unsigned char>& ttf,
               const ClipOptions& options);

} // namespace apotheca
