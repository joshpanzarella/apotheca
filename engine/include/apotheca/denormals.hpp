#pragma once
// Flush-to-zero. A number too small to matter (below about 1e-38, a
// "denormal") is worked out by a slow path in the processor: a fading filter
// or reverb tail can cost many times more while it passes through them. With
// this set, the processor calls such a number zero instead.
//
// It is set per thread. An engine asked to (Engine::setFlushDenormals) sets
// it for the length of every render(), putting the thread back as it found
// it, and on its helper threads. Nothing outside the engine need call these.
//
// IT IS NOT QUITE "NO CHANGE IN SOUND". The very end of a tail, on its way
// to zero, passes through such numbers; flushed, it is zero a moment sooner,
// and the last bits of the samples just before differ. Nothing above 1e-30
// (600 dB down) moves, but a checksum of every bit does.
// tests/effects_test.cpp measures exactly this.
//
// On x86 this is the FTZ and DAZ bits of MXCSR; on 64-bit ARM (the handheld)
// the FZ bit of FPCR. On anything else it does nothing and
// denormalsFlushed() stays false.

namespace apotheca {

// Turn it on or off for the calling thread. Returns whether it was on.
bool flushDenormals(bool on = true);
// Whether it is on for the calling thread.
bool denormalsFlushed();

// On for as long as one of these lives; then as it was. (Given false, it
// does nothing at all.)
class FlushDenormals {
public:
    explicit FlushDenormals(bool on = true) : on_(on), was_(on ? flushDenormals(true) : false) {}
    ~FlushDenormals() {
        if (on_) flushDenormals(was_);
    }
    FlushDenormals(const FlushDenormals&) = delete;
    FlushDenormals& operator=(const FlushDenormals&) = delete;

private:
    bool on_, was_;
};

} // namespace apotheca
