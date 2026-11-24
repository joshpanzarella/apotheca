#include "fx_picture.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

#include "ui.hpp"

namespace apotheca {

using namespace ui;

namespace {

constexpr float kPi = 3.14159265358979f;

float held(float v) { return v != v ? 0.f : (v < 0.f ? 0.f : (v > 1.f ? 1.f : v)); }

// Rectangles gathered by colour and drawn in one call each (a call a pixel is slow on the handheld's renderer).
struct Ink {
    SDL_Color colour;
    std::vector<SDL_Rect> rects;
    explicit Ink(SDL_Color c) : colour(c) {}
    void add(int x, int y, int w, int h) {
        if (w > 0 && h > 0) rects.push_back({x, y, w, h});
    }
    void draw(SDL_Renderer* r) const {
        if (rects.empty()) return;
        SDL_SetRenderDrawColor(r, colour.r, colour.g, colour.b, colour.a);
        SDL_RenderFillRects(r, rects.data(), static_cast<int>(rects.size()));
    }
};

// The plot: what is drawn in, inside the picture's box.
struct Plot {
    int x, y, w, h;
    int yOf(float v) const { return y + h - 1 - static_cast<int>(std::round(held(v) * static_cast<float>(h - 1))); }
    int xOf(float t) const { return x + static_cast<int>(std::round(held(t) * static_cast<float>(w - 1))); }
};

// A curve over the plot, `fn(t)` 0..1 bottom to top for t 0..1 along it, two pixels thick; with `under`, filled down to
// `floor` (0..1) under it first.
template <typename Fn>
void curve(SDL_Renderer* r, const Plot& p, Fn fn, SDL_Color line, const SDL_Color* under = nullptr, float floor = 0.f) {
    Ink ink(line), fillInk(under ? *under : line);
    int last = -1;
    for (int i = 0; i < p.w; ++i) {
        const float t = p.w > 1 ? static_cast<float>(i) / static_cast<float>(p.w - 1) : 0.f;
        const int yy = p.yOf(fn(t));
        if (under) {
            const int base = p.yOf(floor);
            if (yy < base) fillInk.add(p.x + i, yy, 1, base - yy);
            else if (yy > base) fillInk.add(p.x + i, base, 1, yy - base);
        }
        const int from = last < 0 ? yy : (last < yy ? last : yy), to = last < 0 ? yy : (last > yy ? last : yy);
        ink.add(p.x + i, from, i + 1 < p.w ? 2 : 1, to - from + 2);
        last = yy;
    }
    if (under) fillInk.draw(r);
    ink.draw(r);
}

// The grid behind a plot: eight columns and its foot.
void grid(SDL_Renderer* r, const Plot& p, float baseline = 0.f) {
    Ink ink(kPanel2), base(kLine);
    for (int i = 1; i < 8; ++i) ink.add(p.x + p.w * i / 8, p.y, 1, p.h);
    base.add(p.x, p.yOf(baseline), p.w, 1);
    ink.draw(r);
    base.draw(r);
}

// A number from what a setting reads: the first in it ("-34 DB" -34, "4:1" 4), or with `last` the last ("1 IN 6" 6).
float readNumber(EffectKind kind, int index, const float* shown, bool last = false) {
    char text[48];
    effectParamText(kind, index, shown[index], text, sizeof text, shown);
    float found = 0.f;
    for (const char* c = text; *c;) {
        if ((*c >= '0' && *c <= '9') || ((*c == '-' || *c == '+') && c[1] >= '0' && c[1] <= '9')) {
            char* end = nullptr;
            found = std::strtof(c, &end);
            if (!last) return found;
            c = end && end > c ? end : c + 1;
        } else {
            ++c;
        }
    }
    return found;
}

unsigned hashOf(unsigned v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}
float noiseAt(int i) { return static_cast<float>(hashOf(static_cast<unsigned>(i) * 2654435761U) & 0xffff) / 65535.f; }

struct Settings {
    EffectKind kind;
    const EffectInfo& info;
    const float* shown;
    int index(const char* name) const {
        for (int i = 0; i < info.params; ++i)
            if (info.paramNames[i] && std::strcmp(info.paramNames[i], name) == 0) return i;
        return -1;
    }
    float operator()(const char* name, float otherwise) const {
        const int i = index(name);
        return i < 0 ? otherwise : held(shown[i]);
    }
    int choice(const char* name) const {
        const int i = index(name), n = i < 0 ? 0 : effectChoices(kind, i);
        if (n <= 0) return 0;
        const int which = static_cast<int>(held(shown[i]) * static_cast<float>(n));
        return which >= n ? n - 1 : which;
    }
};

// ---- FILTER: the response, quiet to loud, low to high ----
float dbToV(float db) { return (db + 30.f) / 48.f; } // -30..+18 dB
float freqAt(float t) { return 20.f * std::pow(1000.f, t); }
float bell(float f, float centre, float octaves) {
    const float d = std::log2(f / centre) / octaves;
    return std::exp(-d * d);
}

void filterPicture(SDL_Renderer* r, const Plot& p, const Settings& s) {
    grid(r, p, dbToV(0.f));
    const float mix = s("MIX", 1.f);
    const auto wet = [&](float gain, float t) { // the mix of the effect's gain with the plain sound, as dB
        const float g = mix * gain + (1.f - mix);
        return dbToV(20.f * std::log10(g < 1e-4f ? 1e-4f : g));
    };
    float markT = -1.f;
    std::function<float(float)> fn;
    switch (s.kind) {
    case EffectKind::Comb: {
        const float f0 = 40.f * std::pow(50.f, s("PITCH", 0.5f)), g = 0.95f * s("FEEDBACK", 0.5f);
        fn = [=](float t) {
            const float f = freqAt(t), c = std::cos(2.f * kPi * f / f0);
            return wet(1.f / std::sqrt(1.f + g * g - 2.f * g * c) * (1.f - g * 0.5f), t);
        };
        markT = std::log(f0 / 20.f) / std::log(1000.f);
        break;
    }
    case EffectKind::Vowel: {
        static const float kF1[5] = {800.f, 400.f, 300.f, 450.f, 330.f}, kF2[5] = {1150.f, 2000.f, 2300.f, 800.f, 900.f};
        const float at = s("VOWEL", 0.f) * 4.f;
        const int a = at >= 4.f ? 3 : static_cast<int>(at);
        const float part = at - static_cast<float>(a), shift = std::pow(2.f, (s("SHIFT", 0.5f) - 0.5f) * 2.f);
        const float f1 = (kF1[a] + (kF1[a + 1] - kF1[a]) * part) * shift, f2 = (kF2[a] + (kF2[a + 1] - kF2[a]) * part) * shift;
        const float width = 0.9f - 0.65f * s("RESONANCE", 0.5f);
        fn = [=](float t) {
            const float f = freqAt(t);
            return wet(0.08f + 2.2f * bell(f, f1, width) + 1.6f * bell(f, f2, width), t);
        };
        markT = std::log(f1 / 20.f) / std::log(1000.f);
        break;
    }
    case EffectKind::Eq: {
        const float low = (s("LOW", 0.5f) - 0.5f) * 24.f, mid = (s("MID", 0.5f) - 0.5f) * 24.f, high = (s("HIGH", 0.5f) - 0.5f) * 24.f;
        const float centre = 100.f * std::pow(80.f, s("MID FREQ", 0.5f));
        fn = [=](float t) {
            const float f = freqAt(t);
            const float lowPart = 1.f / (1.f + std::pow(f / 200.f, 2.f)), highPart = 1.f / (1.f + std::pow(5000.f / f, 2.f));
            return dbToV(low * lowPart + mid * bell(f, centre, 1.2f) + high * highPart);
        };
        markT = std::log(centre / 20.f) / std::log(1000.f);
        break;
    }
    default: { // FILTER: low 12, low 24, band, DJ, high (`int(v * 5)`)
        const int type = s.choice("TYPE");
        const float cut = s("CUTOFF", 0.5f), q = 0.7f + 7.f * s("RESONANCE", 0.f) * s("RESONANCE", 0.f);
        // DJ: below the middle a low pass closing, above it a high pass opening.
        const bool dj = type == 3, high = type == 4 || (dj && cut > 0.5f);
        const float place = dj ? (cut > 0.5f ? (cut - 0.5f) * 2.f : cut * 2.f) : cut;
        const float fc = 20.f * std::pow(1000.f, place);
        fn = [=](float t) {
            const float x = freqAt(t) / fc;
            const float lp = 1.f / std::sqrt((1.f - x * x) * (1.f - x * x) + (x / q) * (x / q));
            float g = lp;
            if (type == 1) g = lp * lp * (q > 1.f ? 1.f / q : 1.f);
            else if (type == 2) g = (x / q) * lp;
            else if (high) g = x * x * lp;
            return wet(g, t);
        };
        markT = place;
        break;
    }
    }
    curve(r, p, fn, kText, &kPanel2, dbToV(-30.f));
    if (markT >= 0.f && markT <= 1.f) fill(r, p.xOf(markT) - 2, p.yOf(fn(markT)) - 2, 6, 6, kBrass); // where it is set
}

// ---- DRIVE and CRUSH: two cycles of a sine through it, the plain one under it ----
void wavePicture(SDL_Renderer* r, const Plot& p, const Settings& s) {
    grid(r, p, 0.5f);
    const auto plain = [](float t) { return 0.9f * std::sin(2.f * kPi * 2.f * t); };
    curve(r, p, [&](float t) { return 0.5f + 0.45f * plain(t); }, kLine);
    const float mix = s("MIX", 1.f);
    std::function<float(float)> bent;
    switch (s.kind) {
    case EffectKind::Saturate: {
        const float k = 1.f + 6.f * s("DRIVE", 0.5f);
        const float lean = (s.choice("TYPE") % 2) ? 0.3f : 0.f;
        bent = [=](float t) { return (std::tanh(k * (plain(t) + lean)) - std::tanh(k * lean)) / std::tanh(k); };
        break;
    }
    case EffectKind::Fold: {
        const float g = 1.f + 4.f * s("DRIVE", 0.5f), off = (s("SYMMETRY", 0.5f) - 0.5f) * 0.8f;
        bent = [=](float t) { return std::sin(0.5f * kPi * g * (plain(t) + off)); };
        break;
    }
    case EffectKind::Fuzz: {
        const float k = 1.f + 30.f * s("FUZZ", 0.5f), gate = 0.35f * s("GATE", 0.f);
        bent = [=](float t) {
            const float in = plain(t);
            const float out = std::fabs(in) < gate ? 0.f : k * in;
            return out > 0.95f ? 0.95f : (out < -0.95f ? -0.95f : out);
        };
        break;
    }
    case EffectKind::Crush: {
        const float bits = readNumber(s.kind, 0, s.shown), levels = std::pow(2.f, bits - 1.f);
        const float every = readNumber(s.kind, 1, s.shown, true), hold = every < 1.f ? 1.f : every;
        const float steps = 2.f * 48.f / hold; // samples across the picture, as many as a coarse rate keeps
        bent = [=](float t) {
            const float held = std::floor(t * steps) / steps;
            const float v = plain(held < 0.f ? 0.f : held);
            return std::round(v * levels) / levels;
        };
        break;
    }
    case EffectKind::Ring: {
        const float cycles = 2.f + 22.f * s("FREQ", 0.3f) * s("FREQ", 0.3f);
        const bool square = s.choice("WAVE") > 0;
        bent = [=](float t) {
            const float c = std::sin(2.f * kPi * cycles * t);
            return plain(t) * (square ? (c >= 0.f ? 1.f : -1.f) : c);
        };
        break;
    }
    case EffectKind::Wear: {
        const float wow = s("WOW", 0.3f), dirt = s("DIRT", 0.3f), drops = s("DROPS", 0.f);
        bent = [=](float t) {
            const float bend = t + 0.03f * wow * std::sin(2.f * kPi * 1.3f * t);
            float v = 0.9f * std::sin(2.f * kPi * 2.f * bend) + 0.25f * dirt * (noiseAt(static_cast<int>(t * 400.f)) - 0.5f);
            if (drops > 0.f && noiseAt(static_cast<int>(t * 12.f) + 99) < 0.35f * drops) v *= 0.15f;
            return v;
        };
        break;
    }
    default: { // DRIVE
        const float k = 1.f + 12.f * s("DRIVE", 0.5f), out = 0.4f + 0.6f * s("OUTPUT", 1.f);
        bent = [=](float t) { return out * std::tanh(k * plain(t)) / std::tanh(k); };
        break;
    }
    }
    curve(r, p, [&](float t) { return 0.5f + 0.45f * (mix * bent(t) + (1.f - mix) * plain(t)); }, kText);
}

// ---- MOD: how it moves over four seconds ----
void movePicture(SDL_Renderer* r, const Plot& p, const Settings& s) {
    grid(r, p, 0.f);
    const float cycles = 0.6f + 9.f * s("RATE", 0.3f) * s("RATE", 0.3f), depth = s("DEPTH", 0.5f);
    if (s.kind == EffectKind::Tremolo) {
        const int shape = s.choice("SHAPE");
        const auto wave = [=](float t) {
            const float ph = cycles * t - std::floor(cycles * t);
            switch (shape) {
            case 1: return ph < 0.5f ? 1.f : -1.f;                                            // square
            case 2: return ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;                         // triangle
            case 3: return 2.f * noiseAt(static_cast<int>(cycles * t) + 7) - 1.f;              // random
            default: return std::sin(2.f * kPi * ph);
            }
        };
        const float spread = s("SPREAD", 0.f);
        if (spread > 0.01f) curve(r, p, [&](float t) { return 0.9f - 0.8f * depth * (0.5f + 0.5f * wave(t + spread * 0.5f / cycles)); }, kLine);
        curve(r, p, [&](float t) { return 0.9f - 0.8f * depth * (0.5f + 0.5f * wave(t)); }, kText, &kPanel2, 0.f);
        return;
    }
    const float spread = s("SPREAD", s.kind == EffectKind::Ensemble ? 0.5f : 0.f), fb = s("FEEDBACK", 0.f);
    const int voices = s.kind == EffectKind::Ensemble ? 3 : 1;
    for (int v = voices - 1; v >= 0; --v) {
        const float lag = static_cast<float>(v) * (0.15f + 0.6f * spread) / static_cast<float>(voices);
        curve(r, p, [&](float t) {
            const float sw = std::sin(2.f * kPi * (cycles * t + lag));
            const float sharp = fb > 0.f ? sw * (1.f + fb) / (1.f + fb * std::fabs(sw)) : sw; // feedback: a peakier sweep
            return 0.5f + 0.42f * (0.15f + 0.85f * depth) * sharp;
        }, v == 0 ? kText : kDim);
    }
}

// ---- DELAY: the sound and its echoes along two bars ----
void echoPicture(SDL_Renderer* r, const Plot& p, const Settings& s) {
    const bool ping = s.kind == EffectKind::Ping;
    grid(r, p, ping ? 0.5f : 0.f);
    const float time = s("TIME", s("LENGTH", 0.4f));
    const float gap = static_cast<float>(p.w) * (0.05f + 0.22f * time);
    float loss = 0.12f + 0.8f * s("FEEDBACK", 0.4f);
    if (loss > 0.94f) loss = 0.94f;
    const float mix = s("MIX", 0.5f), age = s("AGE", 0.f);
    Ink dry(kText), echo(kBrass), faint(kDim), ramp(kPanel2);
    const auto put = [&](Ink& ink, float x, float amp, int width, int side) {
        const int hh = static_cast<int>(std::round(amp * static_cast<float>(ping ? p.h / 2 - 2 : p.h - 2)));
        const int left = static_cast<int>(x) - width / 2;
        if (ping) {
            if (side >= 0) ink.add(left, p.yOf(0.5f) - hh, width, hh);
            if (side <= 0) ink.add(left, p.yOf(0.5f) + 1, width, hh);
        } else {
            ink.add(left, p.y + p.h - 1 - hh, width, hh);
        }
        return hh;
    };
    float x = static_cast<float>(p.x) + 4.f;
    put(dry, x, 1.f, 4, 0);
    if (s.kind == EffectKind::Stutter) {
        // A slice caught and played again: REPEATS blocks, each the slice's length.
        const int repeats = 2 + static_cast<int>(6.f * s("REPEATS", 0.4f));
        const float chance = 0.25f + 0.75f * s("CHANCE", 1.f), width = 6.f + 26.f * time;
        x += gap * 0.6f;
        for (int k = 0; k < repeats && x + width < p.x + p.w; ++k, x += width + 3.f)
            put(k % 3 == 2 && chance < 0.6f ? faint : echo, x + width / 2.f, (0.35f + 0.6f * mix) * (k == 0 ? 1.f : 0.95f), static_cast<int>(width), 0);
        dry.draw(r);
        faint.draw(r);
        echo.draw(r);
        return;
    }
    float amp = 0.3f + 0.65f * mix;
    for (int n = 1; n < 40 && amp > 0.03f; ++n) {
        x += gap;
        if (x > p.x + p.w - 4) break;
        const int width = 4 + static_cast<int>(age * static_cast<float>(n) * 1.5f);
        if (s.kind == EffectKind::Reverse) {
            // Played backwards: each echo swells up to its place.
            const int hh = static_cast<int>(amp * static_cast<float>(p.h - 2)), len = static_cast<int>(gap * 0.8f);
            for (int i = 0; i < len; ++i) {
                const int ih = hh * (i + 1) / len;
                ramp.add(static_cast<int>(x) - len + i, p.y + p.h - 1 - ih, 1, ih);
            }
        }
        put(n > 6 && age > 0.3f ? faint : echo, x, amp, width, ping ? (n % 2 ? -1 : 1) : 0);
        amp *= loss;
    }
    ramp.draw(r);
    dry.draw(r);
    faint.draw(r);
    echo.draw(r);
}

// ---- REVERB: the tail, the hit first ----
void tailPicture(SDL_Renderer* r, const Plot& p, const Settings& s) {
    grid(r, p, 0.f);
    const float decay = s("DECAY", s("SIZE", 0.5f));
    const float rate = 1.5f + 9.f * (1.f - decay) * (1.f - decay);
    const float pre = s.kind == EffectKind::Plate ? 0.12f * s("PRE", 0.f) : 0.02f;
    const float bloom = s.kind == EffectKind::Bloom ? s("BLOOM", 0.5f) : 0.f;
    const float drip = s.kind == EffectKind::Spring ? s("DRIP", 0.5f) : 0.f;
    const float density = s.kind == EffectKind::Reverb ? 0.4f + 0.6f * s("SIZE", 0.5f) : 1.f;
    const float mix = s("MIX", 0.5f);
    const auto envelope = [=](float t) {
        if (t < pre) return 0.f;
        const float u = t - pre;
        float e = std::exp(-u * rate);
        if (bloom > 0.f) {
            const float swell = 1.f - std::exp(-u * (40.f - 34.f * bloom));
            e *= swell;
        }
        if (drip > 0.f) e *= 1.f - 0.45f * drip * (0.5f + 0.5f * std::cos(2.f * kPi * 14.f * u));
        return (0.25f + 0.7f * mix) * e;
    };
    Ink body(kLine), sparkle(kBrass);
    for (int i = 6; i < p.w; i += 2) {
        const float t = static_cast<float>(i) / static_cast<float>(p.w - 1);
        if (noiseAt(i) > density) continue;
        const int hh = static_cast<int>(envelope(t) * (0.45f + 0.55f * noiseAt(i + 5000)) * static_cast<float>(p.h - 2));
        body.add(p.x + i, p.y + p.h - 1 - hh, 1, hh);
    }
    if (s.kind == EffectKind::Shimmer) {
        // The octaves climbing out of the tail.
        const float amount = s("SHIMMER", 0.5f);
        for (int k = 0; k < 60; ++k) {
            if (noiseAt(k + 900) > amount) continue;
            const float t = 0.08f + 0.9f * noiseAt(k + 1900);
            const int yy = p.yOf(envelope(t) + 0.08f + 0.5f * noiseAt(k + 2900) * (1.f - t));
            sparkle.add(p.xOf(t), yy, 2, 2);
        }
    }
    fill(r, p.x + 1, p.y + 1, 4, p.h - 2, kText); // the hit
    body.draw(r);
    curve(r, Plot{p.x + 6, p.y, p.w - 6, p.h}, [&](float t) { return envelope(t * (1.f - 6.f / static_cast<float>(p.w)) + 6.f / static_cast<float>(p.w)); }, kText);
    sparkle.draw(r);
}

// ---- LEVEL: what it does to the level; COMP and LIMITER as a curve of sound in against sound out ----
void meterCells(SDL_Renderer* r, int x, int y, int w, int h, float dB) {
    const int cells = static_cast<int>(kMeterFullDb / 2.f), gap = 2;
    const int lit = static_cast<int>(std::floor((dB < 0.f ? 0.f : dB) / 2.f + 0.5f));
    Ink on(kBrass), off(kPanel2);
    for (int c = 0; c < cells; ++c) {
        // Up from the foot, as a level does: one cell a 2 dB.
        const int top = y + h - (h + gap) * (c + 1) / cells, bottom = y + h - (h + gap) * c / cells - gap;
        (c < lit ? on : off).add(x, top, w, bottom - top);
    }
    off.draw(r);
    on.draw(r);
}

void levelPicture(SDL_Renderer* r, const Plot& p, const Settings& s, float meterDb) {
    const bool meter = effectMeterParam(s.kind) >= 0;
    const int meterW = 22;
    Plot left{p.x, p.y, p.w - (meter ? meterW + 14 : 0), p.h};
    if (meter) meterCells(r, p.x + p.w - meterW, p.y, meterW, p.h, meterDb);
    if (s.kind == EffectKind::Comp || s.kind == EffectKind::Limiter) {
        // In and out, -60 to 0 dB each way, on a square.
        const int side = left.h;
        const Plot sq{left.x + (left.w - side) / 2, left.y, side, side};
        Ink lines(kPanel2);
        for (int i = 1; i < 4; ++i) {
            lines.add(sq.x + sq.w * i / 4, sq.y, 1, sq.h);
            lines.add(sq.x, sq.y + sq.h * i / 4, sq.w, 1);
        }
        lines.draw(r);
        curve(r, sq, [](float t) { return t; }, kLine); // nothing done
        float thr, ratio, gain = 0.f;
        if (s.kind == EffectKind::Comp) {
            thr = readNumber(s.kind, 0, s.shown);
            ratio = readNumber(s.kind, 1, s.shown);
        } else {
            gain = readNumber(s.kind, 0, s.shown);
            thr = readNumber(s.kind, 1, s.shown); // the ceiling
            ratio = 1000.f;
        }
        if (ratio < 1.f) ratio = 1.f;
        const float mix = s("MIX", 1.f);
        const auto out = [=](float in) { const float g = in + gain, done = g < thr ? g : thr + (g - thr) / ratio; return mix * done + (1.f - mix) * in; };
        curve(r, sq, [&](float t) { return (out(-60.f + 60.f * t) + 60.f) / 60.f; }, kTeal);
        const float knee = thr - gain;
        if (knee > -60.f && knee < 0.f) fill(r, sq.xOf((knee + 60.f) / 60.f) - 2, sq.yOf((thr + 60.f) / 60.f) - 2, 6, 6, kBrass);
        return;
    }
    grid(r, left, 0.f);
    if (s.kind == EffectKind::Duck) {
        // A bar of four hits: the level dips at each and comes back.
        const float amount = s("AMOUNT", 0.5f), release = 0.02f + 0.35f * s("RELEASE", 0.4f);
        curve(r, left, [=](float t) {
            const float since = t * 4.f - std::floor(t * 4.f);
            return 0.9f - 0.8f * amount * std::exp(-since / (release * 4.f));
        }, kText, &kPanel2, 0.f);
        Ink hits(kBrass);
        for (int k = 0; k < 4; ++k) hits.add(left.x + left.w * k / 4, left.y + left.h - 6, 3, 6);
        hits.draw(r);
        return;
    }
    // SNAP: a hit's level, its front made sharper or softer and its tail longer or shorter.
    const float attack = s("ATTACK", 0.5f) - 0.5f, sustain = s("SUSTAIN", 0.5f) - 0.5f;
    curve(r, left, [](float t) { return 0.75f * std::exp(-t * 6.f) * (t < 0.02f ? t / 0.02f : 1.f); }, kLine);
    curve(r, left, [=](float t) {
        const float front = t < 0.02f ? t / 0.02f : 1.f;
        const float peak = 1.f + 0.9f * attack * std::exp(-t * 40.f);
        return 0.75f * front * peak * std::exp(-t * 6.f * (1.f - 0.8f * sustain));
    }, kText, &kPanel2, 0.f);
}

} // namespace

void drawEffectPicture(SDL_Renderer* r, int x, int y, int w, int h, EffectKind kind, const float* shown, float meterDb) {
    box(r, x, y, w, h, kPanel, kLine);
    if (kind == EffectKind::None) return;
    const Settings s{kind, effectInfo(kind), shown};
    const Plot p{x + 12, y + 12, w - 24, h - 24};
    switch (effectInfo(kind).family) {
    case EffectFamily::Filter: filterPicture(r, p, s); break;
    case EffectFamily::Drive:
    case EffectFamily::Crush: wavePicture(r, p, s); break;
    case EffectFamily::Mod: movePicture(r, p, s); break;
    case EffectFamily::Delay: echoPicture(r, p, s); break;
    case EffectFamily::Reverb: tailPicture(r, p, s); break;
    case EffectFamily::Level: levelPicture(r, p, s, meterDb); break;
    default: break;
    }
}

SDL_Point dialPoint(int cx, int cy, float radius, float part) {
    const float a = (kDialFrom + kDialSweep * held(part)) * kPi / 180.f;
    return {cx + static_cast<int>(std::floor(radius * std::cos(a))), cy + static_cast<int>(std::floor(radius * std::sin(a)))};
}

void drawDial(SDL_Renderer* r, int cx, int cy, int radius, int ring, float part, int choices, int which, SDL_Color lit, float mark) {
    part = held(part);
    // Where a pixel's centre stands on the ring: how far round it is (0..1), or past the end (> 1, the gap at the foot).
    const auto round = [&](int px, int py, float* d) {
        const float dx = static_cast<float>(px - cx) + 0.5f, dy = static_cast<float>(py - cy) + 0.5f;
        *d = std::sqrt(dx * dx + dy * dy);
        float a = std::atan2(dy, dx) * 180.f / kPi - kDialFrom;
        while (a < 0.f) a += 360.f;
        return a / kDialSweep;
    };
    Ink on(lit), off(kLine), face(kPanel2), pointer(kText), brass(kBrass);
    const float inner = static_cast<float>(radius - ring), faceR = static_cast<float>(radius - ring - 3);
    for (int py = cy - radius; py <= cy + radius; ++py) {
        for (int px = cx - radius; px <= cx + radius; ++px) {
            float d;
            const float t = round(px, py, &d);
            if (d <= faceR) face.add(px, py, 1, 1);
            else if (choices <= 0 && d >= inner && d < static_cast<float>(radius) && t <= 1.f) (t <= part ? on : off).add(px, py, 1, 1);
        }
    }
    float shownPart = part;
    if (choices > 0) {
        // A dot a choice, round the ring's middle; the one it is, larger and lit.
        const float mid = static_cast<float>(radius) - static_cast<float>(ring) / 2.f;
        for (int c = 0; c < choices; ++c) {
            const float t = choices > 1 ? static_cast<float>(c) / static_cast<float>(choices - 1) : 0.5f;
            const SDL_Point at = dialPoint(cx, cy, mid, t);
            const int size = c == which ? 6 : 3;
            (c == which ? on : off).add(at.x - size / 2, at.y - size / 2, size, size);
        }
        shownPart = choices > 1 ? static_cast<float>(which) / static_cast<float>(choices - 1) : 0.5f;
    }
    // The pointer: from near the middle out to the face's edge, two pixels wide.
    for (float k = faceR * 0.35f; k <= faceR - 1.f; k += 0.5f) {
        const SDL_Point at = dialPoint(cx, cy, k, shownPart);
        pointer.add(at.x, at.y, 2, 2);
    }
    if (mark == mark && choices <= 0) {
        // A MOD runs on it: a brass tick across the ring, standing out past it, where the setting is given now.
        for (float k = inner - 3.f; k <= static_cast<float>(radius) + 3.f; k += 0.5f) {
            const SDL_Point at = dialPoint(cx, cy, k, mark);
            brass.add(at.x, at.y, 2, 2);
        }
    }
    face.draw(r);
    off.draw(r);
    on.draw(r);
    pointer.draw(r);
    brass.draw(r);
}

} // namespace apotheca
