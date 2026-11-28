#include "stretch.hpp"

#include <cmath>

namespace apotheca {

namespace {
constexpr int kPiece = 1024;       // samples in a piece
constexpr int kHop = kPiece / 2;   // pieces are laid down this far apart
constexpr int kSearch = 384;       // how far a piece may slide to line up
}

namespace {
// The overlapping pieces, on one side or two: where each piece is taken from is found on `guide` (the sound, or
// both sides mixed to one), and that piece is laid down on every side in `sides`.
void stretchSides(const std::vector<float>& guide, const std::vector<const std::vector<float>*>& sides, int frames, const std::vector<std::vector<float>*>& outs) {
    const int length = static_cast<int>(guide.size());
    const auto at = [&](const std::vector<float>& in, int i) { return i >= 0 && i < length ? in[static_cast<std::size_t>(i)] : 0.f; };
    float window[kPiece];
    for (int i = 0; i < kPiece; ++i) window[i] = 0.5f - 0.5f * std::cos(6.28318530718f * (static_cast<float>(i) + 0.5f) / kPiece);

    const double step = static_cast<double>(length) / frames * kHop; // how far through the recording each piece moves
    int last = -kHop; // where the previous piece was taken from; the first is taken from the very start
    for (int piece = 0; piece * kHop - kHop < frames; ++piece) {
        const int to = piece * kHop - kHop; // the first piece is half off the front, so the start is at full level
        const int wanted = static_cast<int>(std::lround(piece * step)) - kHop;
        // What would follow the last piece if nothing were stretched. The
        // new piece should look like that where the two overlap.
        const int follows = last + kHop;
        int from = wanted;
        if (piece > 0 && wanted != follows) {
            double best = -1e30;
            for (int slide = -kSearch; slide <= kSearch; slide += 2) {
                double match = 0.0;
                for (int i = 0; i < kHop; i += 2) match += static_cast<double>(at(guide, wanted + slide + i)) * at(guide, follows + i);
                if (match > best) { best = match; from = wanted + slide; }
            }
        }
        last = from;
        for (std::size_t side = 0; side < sides.size(); ++side)
            for (int i = 0; i < kPiece; ++i) {
                const int o = to + i;
                if (o >= 0 && o < frames) (*outs[side])[static_cast<std::size_t>(o)] += at(*sides[side], from + i) * window[i];
            }
    }
}
} // namespace

std::vector<float> stretchTo(const std::vector<float>& in, int frames) {
    std::vector<float> out(frames > 0 ? static_cast<std::size_t>(frames) : 0u, 0.f);
    const int length = static_cast<int>(in.size());
    if (length == 0 || frames <= 0) return out;
    if (length == frames) return in;
    stretchSides(in, {&in}, frames, {&out});
    return out;
}

Clip stretchTo(const Clip& in, int frames) {
    if (!in.stereo()) return Clip(stretchTo(in.left(), frames));
    const std::size_t n = frames > 0 ? static_cast<std::size_t>(frames) : 0u;
    Clip out(std::vector<float>(n, 0.f), std::vector<float>(n, 0.f));
    if (in.empty() || frames <= 0) return out;
    if (static_cast<int>(in.size()) == frames) return in;
    stretchSides(in.mono(), {&in.left(), &in.right}, frames, {&out.left(), &out.right});
    return out;
}

std::vector<float> fitTo(const std::vector<float>& in, int frames, float sampleRate) {
    if (frames > 0 && static_cast<int>(in.size()) == frames) return in;
    std::vector<float> out;
    if (stretchSmooth(in, frames, sampleRate, out)) return out;
    return stretchTo(in, frames); // too short for that, or nothing to stretch
}

} // namespace apotheca

namespace apotheca {

Clip fitTo(const Clip& in, int frames, float sampleRate) {
    if (!in.stereo()) return Clip(fitTo(in.left(), frames, sampleRate));
    if (frames > 0 && static_cast<int>(in.size()) == frames) return in;
    Clip out;
    if (stretchSmooth(in, frames, sampleRate, out)) return out;
    return stretchTo(in, frames);
}

} // namespace apotheca
