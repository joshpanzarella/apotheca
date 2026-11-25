#include "apotheca/sampler.hpp"

#include <cmath>

namespace apotheca {

namespace sampler {
namespace {
const int kMin[kSettings] = {0, -100, 0, 0, 0, 0, 0, 1, 0};
const int kMax[kSettings] = {127, 100, 100, 100, 100, 1, 100, 100, 100};
const int kDefault[kSettings] = {60, 0, 0, 0, 20, 0, 80, 100, 0};
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
    sampleRate_ = sampleRate > 0.f ? sampleRate : 48000.f; // (0, negative or NaN: the usual)
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
    zones_ = nullptr; // one recording or none: no zones
    for (Voice& v : voices_) v = Voice(); // they were reading the old recording
}

void Sampler::setZones(const SampleZoneSet* zones) {
    zones_ = zones && !zones->zones.empty() ? &zones->zones : nullptr;
    data_ = nullptr; // zones or nothing: no one recording
    length_ = 0;
    for (Voice& v : voices_) v = Voice(); // they were reading the old set
}

void Sampler::setChopped(bool chopped) {
    chopped_ = chopped;
    for (Voice& v : voices_) v = Voice(); // they were reading whole, or slices
}

void Sampler::noteOn(int note, float velocity) {
    if (note < 0 || note > 127 || !(velocity > 0.f)) return;
    // What plays: its zone's recording, its chop's slice, or the one recording.
    const float* data = data_;
    int length = length_, root = value_[Root], tune = value_[Tune];
    double spanFrom = 0.0, spanTo = static_cast<double>(length);
    if (zones_) {
        // The key's zones (a root's velocity layers), the softest that reaches this velocity; above them all,
        // the loudest of them. (No layers: the one zone of the key, as before.)
        const SampleZone* zone = nullptr;
        for (const SampleZone& z : *zones_) {
            if (note < z.lo || note > z.hi) continue;
            if (!zone || zone->top < velocity) zone = &z;
            else break; // (a root's layers lie together, softest first)
        }
        if (!zone) zone = &zones_->front();
        if (zone->data.empty()) return;
        data = zone->data.data();
        length = static_cast<int>(zone->data.size());
        root = zone->root + (value_[Root] - 60); // ROOT moves the whole set as it moves one recording (60: none)
        spanTo = static_cast<double>(length);
    } else if (chopped_ && data_) {
        const int slice = note - kChopBase;
        if (slice < 0 || slice >= kChopSlices) return; // off the slices: silent
        spanFrom = static_cast<double>(length) * slice / kChopSlices;
        spanTo = static_cast<double>(length) * (slice + 1) / kChopSlices;
        root = note - (value_[Root] - 60); // each slice at its own pitch; ROOT moves every slice together
    }
    if (!data || length <= 0) return;
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
    voice->data = data;
    voice->length = length;
    voice->spanFrom = spanFrom;
    voice->spanTo = spanTo;
    voice->position = spanFrom + static_cast<double>(value_[Start]) / 100.0 * (spanTo - spanFrom);
    if (voice->position > spanTo - 1) voice->position = spanTo - 1;
    if (voice->position < spanFrom) voice->position = spanFrom;
    voice->step = std::pow(2.0, (note - root) / 12.0 + tune / 1200.0);
    voice->slide.stop();
    voice->gain = velocity > 1.f ? 1.f : velocity;
    voice->fade = 1.f;
    voice->fadeStep = 0.f;
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

void Sampler::chokeAll() {
    for (Voice& v : voices_)
        if (v.note >= 0 && v.fadeStep == 0.f) v.fadeStep = 1.f / (0.003f * sampleRate_);
}

void Sampler::noteOnFrom(int note, float velocity, int from, float seconds) {
    noteOn(note, velocity);
    for (Voice& v : voices_)
        if (v.note == note) v.slide.start(from, note, seconds, sampleRate_);
}

void Sampler::render(float* left, float* right, int frames) {
    for (int i = 0; i < frames; ++i) left[i] = 0.f;
    for (Voice& v : voices_) {
        if (v.note < 0 || !v.data || v.length <= 0) continue;
        // END: where a note stops (or goes round to LOOP START). At 100% the recording's last sample, as
        // before END was there (the sample after it is read between the two). Each voice reads its own
        // recording (its zone's): the percents are of that one, or of its chop's slice.
        const double span = v.spanTo - v.spanFrom;
        const double loopFrom = v.spanFrom + static_cast<double>(playedLoop(value_[Start], value_[LoopStart], value_[End])) / 100.0 * span;
        const double endAt = v.spanFrom + static_cast<double>(playedEnd(value_[Start], value_[End])) / 100.0 * span;
        const double last = endAt < v.spanTo - 1 ? endAt : v.spanTo - 1;
        const bool loop = value_[Loop] != 0 && loopFrom < last - 1;
        // The blend at the jump back (LOOP on): the last `blend` samples before END fade into those after LOOP START,
        // and the jump lands `blend` past LOOP START, where the blend has got to. The fade at END (LOOP off). Before
        // `edge` a sample is worked out as it always was.
        double blend = 0.0;
        if (loop) {
            blend = static_cast<double>(kLoopBlendMs) / 1000.0 * sampleRate_;
            if (blend > (last - loopFrom) / 3.0) blend = (last - loopFrom) / 3.0;
            if (blend < 1.0) blend = 0.0; // (too short a loop to blend: the jump as it was)
        }
        const double endFade = static_cast<double>(kEndFadeMs) / 1000.0 * sampleRate_;
        const double edge = loop ? (blend > 0.0 ? last - blend : last) : last - endFade;
        // A glide moves a sample at a time, as JAX-EP's does (review 2026-11-23: worked out once a block,
        // it moved in steps, and a bounce was not live play: up to 0.45 apart in a song with glides).
        for (int i = 0; i < frames; ++i) {
            const double step = v.slide.semitones == 0.f ? v.step : v.step * std::exp2(static_cast<double>(v.slide.take(1)) / 12.0); // (semitones off its own pitch, at this sample)
            if (v.position >= last) {
                if (!loop) { v.note = -1; break; }
                if (blend > 0.0) v.position = loopFrom + blend + std::fmod(v.position - last, last - loopFrom - blend);
                else v.position = loopFrom + std::fmod(v.position - loopFrom, last - loopFrom);
            }
            const float envelope = v.env.next(shape_);
            if (v.env.idle()) { v.note = -1; break; }
            if (v.fadeStep > 0.f) {
                v.fade -= v.fadeStep;
                if (v.fade <= 0.f) { v.note = -1; break; }
            }
            const int at = static_cast<int>(v.position);
            const float part = static_cast<float>(v.position - at);
            float s = v.data[at] + part * (v.data[at + 1] - v.data[at]);
            if (v.position >= edge) {
                if (loop) { // blended into what follows LOOP START, by how far into the blend it is
                    const double into = v.position - edge, from = loopFrom + into;
                    const int to = static_cast<int>(from);
                    const float toPart = static_cast<float>(from - to);
                    const float head = v.data[to] + toPart * (v.data[to + 1] - v.data[to]);
                    s += static_cast<float>(into / blend) * (head - s);
                } else { // faded to nothing at END
                    s *= static_cast<float>((last - v.position) / endFade);
                }
            }
            left[i] += s * v.gain * v.fade * envelope * level_ * kVoiceGain;
            v.position += step;
        }
    }
    for (int i = 0; i < frames; ++i) right[i] = left[i];
}

} // namespace apotheca
