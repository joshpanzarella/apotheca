// The only file that includes Signalsmith Stretch (third_party/signalsmith).
// It is a large template library; keeping it to one file keeps the build quick.

#include "stretch.hpp"

#include "signalsmith/signalsmith-stretch.h"

namespace apotheca {

bool stretchSmooth(const std::vector<float>& in, int frames, float sampleRate, std::vector<float>& out) {
    if (in.empty() || frames <= 0) return false;
    out.assign(static_cast<std::size_t>(frames), 0.f);
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault(1, sampleRate);
    const float* inputs[1] = {in.data()};
    float* outputs[1] = {out.data()};
    return stretch.exact(inputs, static_cast<int>(in.size()), outputs, frames);
}

bool stretchSmooth(const Clip& in, int frames, float sampleRate, Clip& out) {
    if (!in.stereo()) {
        out.right.clear();
        return stretchSmooth(in.left(), frames, sampleRate, out.left());
    }
    if (in.empty() || frames <= 0) return false;
    out.left().assign(static_cast<std::size_t>(frames), 0.f);
    out.right.assign(static_cast<std::size_t>(frames), 0.f);
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault(2, sampleRate);
    const float* inputs[2] = {in.data(), in.right.data()};
    float* outputs[2] = {out.data(), out.right.data()};
    return stretch.exact(inputs, static_cast<int>(in.size()), outputs, frames);
}

} // namespace apotheca
