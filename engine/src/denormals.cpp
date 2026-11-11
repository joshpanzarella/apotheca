#include "apotheca/denormals.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__SSE__)
#include <xmmintrin.h>
#define APOTHECA_FLUSH_X86 1
#elif defined(__aarch64__)
#define APOTHECA_FLUSH_ARM64 1
#endif

namespace apotheca {

#if defined(APOTHECA_FLUSH_X86)
namespace {
// FTZ: an answer that small becomes zero. DAZ: a number that small coming in counts as zero.
constexpr unsigned kBits = 0x8000u | 0x0040u;
}
bool denormalsFlushed() { return (_mm_getcsr() & kBits) == kBits; }
bool flushDenormals(bool on) {
    const unsigned now = _mm_getcsr();
    _mm_setcsr(on ? (now | kBits) : (now & ~kBits));
    return (now & kBits) == kBits;
}
#elif defined(APOTHECA_FLUSH_ARM64)
namespace {
constexpr unsigned long long kBit = 1ull << 24; // FZ
unsigned long long control() {
    unsigned long long v;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(v));
    return v;
}
}
bool denormalsFlushed() { return (control() & kBit) != 0; }
bool flushDenormals(bool on) {
    const unsigned long long now = control();
    const unsigned long long to = on ? (now | kBit) : (now & ~kBit);
    __asm__ __volatile__("msr fpcr, %0" : : "r"(to));
    return (now & kBit) != 0;
}
#else
bool denormalsFlushed() { return false; }
bool flushDenormals(bool) { return false; }
#endif

} // namespace apotheca
