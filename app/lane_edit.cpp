#include "lane_edit.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

namespace {
int held(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }


// The resampler's kernel: a sinc cut at `cutoff` (of the input's Nyquist), under a Blackman window
// kHalf of its zero crossings wide either side. Worked out once for each speed into a table of
// kPerSample points to a sample, read with a straight line between two points.
constexpr int kHalf = 8;
constexpr int kPerSample = 256;
struct Kernel {
    double reach;              // input samples either side of the centre
    std::vector<float> table;  // (reach * kPerSample + 2) points from the centre out
    explicit Kernel(double cutoff) {
        reach = kHalf / cutoff;
        const int n = static_cast<int>(reach * kPerSample) + 2;
        table.resize(static_cast<std::size_t>(n));
        const double pi = 3.14159265358979323846;
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(i) / kPerSample;
            if (x >= reach) { table[static_cast<std::size_t>(i)] = 0.f; continue; }
            const double t = pi * x * cutoff;
            const double sinc = x == 0.0 ? 1.0 : std::sin(t) / t;
            const double w = 0.5 + 0.5 * x / reach; // 0.5 at the centre, 1 at the reach
            const double window = 0.42 - 0.5 * std::cos(2 * pi * w) + 0.08 * std::cos(4 * pi * w);
            table[static_cast<std::size_t>(i)] = static_cast<float>(cutoff * sinc * window);
        }
    }
    float at(double x) const {
        x = std::fabs(x) * kPerSample;
        const std::size_t i = static_cast<std::size_t>(x);
        if (i + 1 >= table.size()) return 0.f;
        const float f = static_cast<float>(x - static_cast<double>(i));
        return table[i] + f * (table[i + 1] - table[i]);
    }
};

// `in` played `speed` times as fast: ceil(n / speed) samples. Each is the kernel's weighted sum of
// the input about its place (outside the input: silence), the weights made to add up to one so a
// steady level stays steady. Going up, the cut is lowered with the speed, so nothing folds back.
std::vector<float> resampled(const std::vector<float>& in, double speed) {
    const int n = static_cast<int>(in.size());
    const int m = static_cast<int>(std::ceil(static_cast<double>(n) / speed - 1e-9));
    std::vector<float> out(static_cast<std::size_t>(m > 0 ? m : 1), 0.f);
    const Kernel kernel(0.95 * (speed > 1.0 ? 1.0 / speed : 1.0));
    for (int i = 0; i < static_cast<int>(out.size()); ++i) {
        const double at = i * speed;
        const int from = static_cast<int>(std::ceil(at - kernel.reach)), to = static_cast<int>(std::floor(at + kernel.reach));
        double sum = 0.0, weights = 0.0;
        for (int k = from; k <= to; ++k) {
            const float w = kernel.at(at - k);
            weights += w;
            if (k >= 0 && k < n) sum += static_cast<double>(w) * in[static_cast<std::size_t>(k)];
        }
        out[static_cast<std::size_t>(i)] = weights != 0.0 ? static_cast<float>(sum / weights) : 0.f;
    }
    return out;
}
} // namespace

int fadeStopPlace(int ms) {
    int place = 0;
    for (int i = 0; i < kFadeStopCount; ++i)
        if (kFadeStops[i] <= ms) place = i;
    return place;
}

float LaneEdit::speed() const { return std::pow(2.f, (static_cast<float>(pitch) + static_cast<float>(fine) / 100.f) / 12.f); }

const char* laneEditName(int row) {
    static const char* const names[kEditRows] = {"START", "END", "FADE IN", "FADE OUT", "GAIN", "PITCH", "FINE", "REVERSE"};
    return row >= 0 && row < kEditRows ? names[row] : "";
}

int laneEditValue(const LaneEdit& e, int row) {
    switch (row) {
    case kEditStart: return e.start;
    case kEditEnd: return e.end;
    case kEditFadeIn: return e.fadeIn;
    case kEditFadeOut: return e.fadeOut;
    case kEditGain: return e.gain;
    case kEditPitch: return e.pitch;
    case kEditFine: return e.fine;
    case kEditReverse: return e.reverse;
    default: return 0;
    }
}

LaneEdit laneEditWith(LaneEdit e, int row, int value) {
    switch (row) {
    case kEditStart: e.start = value; break;
    case kEditEnd: e.end = value; break;
    case kEditFadeIn: e.fadeIn = value; break;
    case kEditFadeOut: e.fadeOut = value; break;
    case kEditGain: e.gain = value; break;
    case kEditPitch: e.pitch = value; break;
    case kEditFine: e.fine = value; break;
    case kEditReverse: e.reverse = value; break;
    default: break;
    }
    return e;
}

int leastSpan(float seconds) {
    if (!(seconds > 0.f)) return 1;
    const int units = static_cast<int>(std::ceil(kLeastSpanMs / 1000.f / seconds * kEditUnits)); // (a unit at least: ceil of more than 0)
    return units > kEditUnits ? kEditUnits : units;
}

LaneEdit laneEditHeld(LaneEdit e, float seconds) {
    const int least = leastSpan(seconds);
    e.end = held(e.end, least, kEditUnits);
    e.start = held(e.start, 0, e.end - least);
    e.gain = held(e.gain, -kGainMost, kGainMost);
    e.fadeIn = held(e.fadeIn, 0, kFadeMost);
    e.fadeOut = held(e.fadeOut, 0, kFadeMost);
    e.reverse = e.reverse ? 1 : 0;
    e.pitch = held(e.pitch, -kPitchMost, kPitchMost);
    e.fine = held(e.fine, -kFineMost, kFineMost);
    return e;
}

void laneEditText(const LaneEdit& e, int row, float seconds, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    const int v = laneEditValue(e, row);
    if (row == kEditStart || row == kEditEnd) {
        const double s = static_cast<double>(v) / kEditUnits * seconds;
        if (s < 1.0) std::snprintf(out, n, "%.1f MS", s * 1000.0);
        else std::snprintf(out, n, "%.3f S", s);
    } else if (row == kEditFadeIn || row == kEditFadeOut) {
        if (v >= 1000) std::snprintf(out, n, "%.1f S", v / 1000.0);
        else std::snprintf(out, n, "%d MS", v);
    } else if (row == kEditGain) {
        if (v == 0) std::snprintf(out, n, "0 DB");
        else std::snprintf(out, n, "%+.1f DB", v / 10.0);
    } else if (row == kEditPitch) {
        if (v == 0) std::snprintf(out, n, "0");
        else std::snprintf(out, n, "%+d", v);
    } else if (row == kEditFine) {
        if (v == 0) std::snprintf(out, n, "0 CENTS");
        else std::snprintf(out, n, "%+d CENTS", v);
    } else {
        std::snprintf(out, n, "%s", v ? "ON" : "OFF");
    }
}

LaneEdit laneEditStepped(const LaneEdit& e, int row, int by, float seconds) {
    const int v = laneEditValue(e, row);
    if (row == kEditStart || row == kEditEnd) {
        int ms = seconds > 0.f ? static_cast<int>(std::lround(0.001 / seconds * kEditUnits)) : 1;
        if (ms < 1) ms = 1;
        return laneEditHeld(laneEditWith(e, row, v + by * ms), seconds);
    }
    if (row == kEditFadeIn || row == kEditFadeOut) {
        // The next stop past it, or the one before it (from between two stops: the one under it). The ends stop.
        int to = v;
        if (by > 0) {
            for (int i = 0; i < kFadeStopCount && to == v; ++i)
                if (kFadeStops[i] > v) to = kFadeStops[i];
        } else {
            for (int i = kFadeStopCount - 1; i >= 0 && to == v; --i)
                if (kFadeStops[i] < v) to = kFadeStops[i];
        }
        return laneEditHeld(laneEditWith(e, row, to), seconds);
    }
    if (row == kEditGain) return laneEditHeld(laneEditWith(e, row, v + by * kGainStep), seconds);
    if (row == kEditReverse) return laneEditWith(e, row, by > 0 ? 1 : 0);
    return laneEditHeld(laneEditWith(e, row, v + by), seconds);
}

int laneEditRange(int row) {
    switch (row) {
    case kEditStart: case kEditEnd: return kEditUnits;
    case kEditFadeIn: case kEditFadeOut: return kFadeStopCount - 1;
    case kEditGain: return 2 * kGainMost;
    case kEditPitch: return 2 * kPitchMost;
    case kEditFine: return 2 * kFineMost;
    default: return 1;
    }
}

float laneEditLevelAt(const LaneEdit& e, float at, float seconds) {
    const float start = static_cast<float>(e.start) / kEditUnits, end = static_cast<float>(e.end) / kEditUnits;
    if (at < start || at >= end) return 1.f;
    const float speed = e.speed();
    const float heard = (e.reverse ? end - at : at - start) * seconds / speed, length = (end - start) * seconds / speed;
    float g = e.gain == 0 ? 1.f : std::pow(10.f, static_cast<float>(e.gain) / 200.f);
    const float in = e.fadeIn / 1000.f, out = e.fadeOut / 1000.f;
    if (heard < in) g *= heard / in;
    if (length - heard < out) g *= (length - heard) / out;
    return g;
}

float laneEditPlaceOf(const LaneEdit& e, float part) {
    const float start = static_cast<float>(e.start) / kEditUnits, end = static_cast<float>(e.end) / kEditUnits;
    return e.reverse ? end - part * (end - start) : start + part * (end - start);
}

double shapeCost(std::size_t n, const LaneEdit& e) {
    if (e.pitch == 0 && e.fine == 0) return 0.0; // copies: a pass over the file
    const double speed = e.speed(), cut = static_cast<double>(e.end - e.start) / kEditUnits;
    const double out = static_cast<double>(n) * (cut > 0.0 ? cut : 0.0) / speed;
    return out * (2.0 * kHalf / (speed > 1.0 ? 1.0 / speed : 1.0) + 1.0);
}

std::vector<float> makeShape(const std::vector<float>& in, const LaneEdit& e, float sampleRate) {
    if (!e.shapes() || in.empty()) return in;
    const int n = static_cast<int>(in.size());
    // 1. START to END, at least one sample.
    int from = static_cast<int>(static_cast<long long>(e.start) * n / kEditUnits);
    int to = static_cast<int>(static_cast<long long>(e.end) * n / kEditUnits);
    from = held(from, 0, n - 1);
    to = held(to, from + 1, n);
    std::vector<float> part(in.begin() + from, in.begin() + to);
    // ...each cut edge faded (a sample's gain is its distance into the fade over the fade's length)
    const int len = static_cast<int>(part.size());
    const int edge = static_cast<int>(std::lround(kEdgeMs / 1000.f * sampleRate));
    for (int i = 0; i < edge && i < len; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(edge);
        if (from > 0) part[static_cast<std::size_t>(i)] *= g;
        if (to < n) part[static_cast<std::size_t>(len - 1 - i)] *= g;
    }
    // 2. Backwards.
    if (e.reverse)
        for (int i = 0, j = len - 1; i < j; ++i, --j) std::swap(part[static_cast<std::size_t>(i)], part[static_cast<std::size_t>(j)]);
    // 3. Faster or slower.
    if (e.pitch != 0 || e.fine != 0) part = resampled(part, static_cast<double>(e.speed()));
    return part;
}

float guarded(float x) {
    const float a = std::fabs(x);
    if (a <= kKnee) return x;
    const float y = kKnee + (1.f - kKnee) * std::tanh((a - kKnee) / (1.f - kKnee));
    return x < 0.f ? -y : y;
}

void makeLevel(const float* in, int n, const LaneEdit& e, float sampleRate, float* out) {
    const float gain = e.gain == 0 ? 1.f : std::pow(10.f, static_cast<float>(e.gain) / 200.f);
    const int fadeIn = static_cast<int>(std::lround(e.fadeIn / 1000.f * sampleRate));
    const int fadeOut = static_cast<int>(std::lround(e.fadeOut / 1000.f * sampleRate));
    for (int i = 0; i < n; ++i) {
        float g = gain;
        if (i < fadeIn) g *= static_cast<float>(i) / static_cast<float>(fadeIn);
        if (n - 1 - i < fadeOut) g *= static_cast<float>(n - 1 - i) / static_cast<float>(fadeOut);
        out[i] = guarded(in[i] * g);
    }
}

bool normalGain(const float* in, int n, int& gain) {
    float peak = 0.f;
    for (int i = 0; i < n; ++i) peak = std::fabs(in[i]) > peak ? std::fabs(in[i]) : peak;
    if (!(peak > 1e-6f)) return false;
    const double db = kNormalDb - 20.0 * std::log10(static_cast<double>(peak));
    gain = held(static_cast<int>(std::floor(db * 10.0 + 1e-9)), -kGainMost, kGainMost); // (down to a tenth: never over -1 dB)
    return true;
}

bool silenceEnds(const std::vector<float>& file, float sampleRate, int& start, int& end) {
    const int n = static_cast<int>(file.size());
    float peak = 0.f;
    for (float v : file) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    if (!(peak > 1e-6f) || n < 2) return false;
    const float floor = peak * std::pow(10.f, kSilenceDb / 20.f);
    int first = 0, last = n - 1;
    while (first < n && std::fabs(file[static_cast<std::size_t>(first)]) < floor) ++first;
    while (last > first && std::fabs(file[static_cast<std::size_t>(last)]) < floor) --last;
    first -= static_cast<int>(std::lround(kSilenceLeadMs / 1000.f * sampleRate));
    first = first < 0 ? 0 : first;
    ++last; // END is past the last sound
    // In units, START rounded down and END up, so the sound is inside them.
    start = static_cast<int>(static_cast<long long>(first) * kEditUnits / n);
    end = static_cast<int>((static_cast<long long>(last) * kEditUnits + n - 1) / n);
    end = end > kEditUnits ? kEditUnits : end;
    return true;
}

} // namespace apotheca
