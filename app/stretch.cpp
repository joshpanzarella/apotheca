#include "stretch.hpp"

#include <cmath>

namespace apotheca {

namespace {
constexpr int kPiece = 1024;       // samples in a piece
constexpr int kHop = kPiece / 2;   // pieces are laid down this far apart
constexpr int kSearch = 384;       // how far a piece may slide to line up
}

std::vector<float> stretchTo(const std::vector<float>& in, int frames) {
    std::vector<float> out(frames > 0 ? static_cast<std::size_t>(frames) : 0u, 0.f);
    const int length = static_cast<int>(in.size());
    if (length == 0 || frames <= 0) return out;
    if (length == frames) return in;

    const auto at = [&](int i) { return i >= 0 && i < length ? in[static_cast<std::size_t>(i)] : 0.f; };
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
                for (int i = 0; i < kHop; i += 2) match += static_cast<double>(at(wanted + slide + i)) * at(follows + i);
                if (match > best) { best = match; from = wanted + slide; }
            }
        }
        last = from;
        for (int i = 0; i < kPiece; ++i) {
            const int o = to + i;
            if (o >= 0 && o < frames) out[static_cast<std::size_t>(o)] += at(from + i) * window[i];
        }
    }
    return out;
}

} // namespace apotheca
