#pragma once
// GLIDE: a note that slides up or down to its pitch from the note before it.
// Each voice of a synth keeps one of these. It holds how far off its own
// pitch the voice still is, in semitones, and comes in to nothing at a steady
// rate. A synth asks it once for each block it works out (a millisecond or
// so), which is fine enough for a slide not to be heard as steps.

namespace apotheca {

struct Slide {
    float semitones = 0.f; // how far the voice is from its own pitch now: below zero, it is still under it
    float perFrame = 0.f;  // how fast that closes

    // From the key `from` to the key `to`, over `seconds`. The same key, no time, or nonsense: no slide.
    void start(int from, int to, float seconds, float sampleRate) {
        semitones = 0.f;
        perFrame = 0.f;
        if (from == to || from < 0 || from > 127 || !(seconds > 0.f) || !(sampleRate > 0.f)) return;
        semitones = static_cast<float>(from - to);
        perFrame = (semitones < 0.f ? -semitones : semitones) / (seconds * sampleRate);
    }
    void stop() { semitones = perFrame = 0.f; }

    // Where it is at the start of the next `frames`, and moves on past them.
    float take(int frames) {
        const float now = semitones;
        if (semitones > 0.f) {
            semitones -= perFrame * static_cast<float>(frames);
            if (semitones < 0.f) semitones = 0.f;
        } else if (semitones < 0.f) {
            semitones += perFrame * static_cast<float>(frames);
            if (semitones > 0.f) semitones = 0.f;
        }
        return now;
    }
};

} // namespace apotheca
