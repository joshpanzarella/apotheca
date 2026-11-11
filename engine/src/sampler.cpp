#include "apotheca/sampler.hpp"

#include <cmath>

namespace apotheca {

namespace sampler {
namespace {
const int kMin[kSettings] = {0, -100, 0, 0, 0, 0, 0};
const int kMax[kSettings] = {127, 100, 100, 100, 100, 1, 100};
const int kDefault[kSettings] = {60, 0, 0, 0, 20, 0, 80};
}
int settingMin(int id) { return id >= 0 && id < kSettings ? kMin[id] : 0; }
int settingMax(int id) { return id >= 0 && id < kSettings ? kMax[id] : 0; }
int settingDefault(int id) { return id >= 0 && id < kSettings ? kDefault[id] : 0; }
}

using namespace sampler;

Sampler::Sampler() {
    for (int i = 0; i < kSettings; ++i) value_[i] = settingDefault(i);
    prepare(48000.f);
}

void Sampler::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kSettings; ++i) setSetting(i, value_[i]);
    allNotesOff();
    for (Voice& v : voices_) v = Voice();
}

void Sampler::setSetting(int id, int value) {
    if (id < 0 || id >= kSettings) return;
    value_[id] = value < settingMin(id) ? settingMin(id) : (value > settingMax(id) ? settingMax(id) : value);
    // The fades, as how far the envelope moves each sample.
    // No decay to speak of: a recording is at full level for as long as the key is down.
    shape_.setFrom(static_cast<float>(value_[Attack]), 0.f, 100.f, static_cast<float>(value_[Release]), sampleRate_);
    level_ = static_cast<float>(value_[Level]) / 100.f;
}

void Sampler::setSample(const float* data, int length) {
    data_ = length > 0 ? data : nullptr;
    length_ = data_ ? length : 0;
    for (Voice& v : voices_) v = Voice(); // they were reading the old recording
}

void Sampler::noteOn(int note, float velocity) {
    if (note < 0 || note > 127 || !(velocity > 0.f) || !data_) return;
    // The same key again restarts its voice; else a free one; else the oldest.
    Voice* voice = nullptr;
    for (Voice& v : voices_)
        if (v.note == note) voice = &v;
    for (Voice& v : voices_)
        if (!voice && v.note < 0) voice = &v;
    if (!voice) {
        voice = &voices_[0];
        for (Voice& v : voices_)
            if (v.age < voice->age) voice = &v;
    }
    voice->note = note;
    voice->position = static_cast<double>(value_[Start]) / 100.0 * length_;
    if (voice->position > length_ - 1) voice->position = length_ - 1;
    voice->step = std::pow(2.0, (note - value_[Root]) / 12.0 + value_[Tune] / 1200.0);
    voice->slide.stop();
    voice->gain = velocity > 1.f ? 1.f : velocity;
    voice->env.stop();
    voice->env.on();
    voice->age = ++clock_;
}

void Sampler::noteOff(int note) {
    for (Voice& v : voices_)
        if (v.note == note) v.env.off();
}

void Sampler::allNotesOff() {
    for (Voice& v : voices_) v.env.off();
}

void Sampler::noteOnFrom(int note, float velocity, int from, float seconds) {
    noteOn(note, velocity);
    for (Voice& v : voices_)
        if (v.note == note) v.slide.start(from, note, seconds, sampleRate_);
}

void Sampler::render(float* left, float* right, int frames) {
    for (int i = 0; i < frames; ++i) left[i] = 0.f;
    if (data_) {
        const double loopFrom = static_cast<double>(value_[Start]) / 100.0 * length_;
        const bool loop = value_[Loop] != 0 && loopFrom < length_ - 2;
        for (Voice& v : voices_) {
            if (v.note < 0) continue;
            const float off = v.slide.take(frames); // a glide: semitones from its own pitch, for this block
            const double step = off == 0.f ? v.step : v.step * std::exp2(static_cast<double>(off) / 12.0);
            for (int i = 0; i < frames; ++i) {
                if (v.position >= length_ - 1) {
                    if (!loop) { v.note = -1; break; }
                    v.position = loopFrom + std::fmod(v.position - loopFrom, length_ - 1 - loopFrom);
                }
                const float envelope = v.env.next(shape_);
                if (v.env.idle()) { v.note = -1; break; }
                const int at = static_cast<int>(v.position);
                const float part = static_cast<float>(v.position - at);
                left[i] += (data_[at] + part * (data_[at + 1] - data_[at])) * v.gain * envelope * level_ * kVoiceGain;
                v.position += step;
            }
        }
    }
    for (int i = 0; i < frames; ++i) right[i] = left[i];
}

} // namespace apotheca
