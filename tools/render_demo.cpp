// Renders a short piece through the engine to a WAV file, so the kit and the
// synth can be heard without the app or the handheld.
//
//   build/render_demo out.wav
//
// Eight bars: the drum pattern from the app's --demo, a Phazed bass and Phazed chords.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "apotheca/engine.hpp"

using namespace apotheca;

namespace {

Command make(Command::Type type, int track, int a = 0, int b = 0, float value = 0.f) {
    Command c;
    c.type = type;
    c.track = track;
    c.a = a;
    c.b = b;
    c.value = value;
    return c;
}

int firstPresetIn(const char* folder) {
    for (int i = 0; i < Phazed::presetCount(); ++i) {
        if (std::strcmp(Phazed::presetCategory(i), folder) == 0) return i;
    }
    return 0;
}

void put16(std::FILE* f, int v) { std::fputc(v & 255, f); std::fputc((v >> 8) & 255, f); }
void put32(std::FILE* f, std::uint32_t v) { put16(f, v & 65535); put16(f, v >> 16); }

bool writeWav(const char* path, const std::vector<float>& stereo, int rate) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const std::uint32_t bytes = static_cast<std::uint32_t>(stereo.size() * 2);
    std::fputs("RIFF", f); put32(f, 36 + bytes); std::fputs("WAVEfmt ", f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, rate); put32(f, rate * 4); put16(f, 4); put16(f, 16);
    std::fputs("data", f); put32(f, bytes);
    for (float s : stereo) {
        const float c = s < -1.f ? -1.f : (s > 1.f ? 1.f : s);
        put16(f, static_cast<int>(c * 32767.f) & 65535);
    }
    std::fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: render_demo out.wav\n");
        return 1;
    }
    const int rate = 44100;
    const float tempo = 110.f;
    const int stepFrames = static_cast<int>(rate * 60.f / tempo / 4.f);

    Engine engine;
    engine.prepare(static_cast<float>(rate));
    engine.addSynthTrack(1);
    engine.addSynthTrack(2);
    const int bass = firstPresetIn("Bass"), pad = firstPresetIn("Strings & Pads");
    engine.send(make(Command::SelectPreset, 1, bass));
    engine.send(make(Command::SelectPreset, 2, pad));
    // Levels that leave the mix under the ceiling, so nothing is squashed.
    engine.send(make(Command::SetTrackLevel, 0, 0, 0, 0.5f));
    engine.send(make(Command::SetTrackLevel, 1, 0, 0, 0.35f));
    engine.send(make(Command::SetTrackLevel, 2, 0, 0, 0.2f));
    engine.send(make(Command::SetTempo, 0, 0, 0, tempo));
    std::printf("bass: %s\nchords: %s\n", Phazed::presetName(bass), Phazed::presetName(pad));

    // The drum pattern from the app's --demo.
    const struct { int lane, step; float level; } hits[] = {
        {0, 0, 1.f}, {0, 6, .7f}, {0, 8, 1.f}, {0, 11, .6f}, {1, 4, 1.f}, {1, 12, 1.f}, {1, 15, .4f},
        {2, 0, .7f}, {2, 2, .5f}, {2, 4, .7f}, {2, 6, .5f}, {2, 8, .7f}, {2, 10, .5f}, {2, 12, .7f},
        {3, 14, .8f}, {4, 12, .6f}, {6, 3, .5f}, {6, 9, .5f}};
    for (const auto& hit : hits) engine.send(make(Command::SetStep, 0, hit.lane, hit.step, hit.level));
    for (int step : {2, 6, 10}) engine.send(make(Command::SetStepNudge, 0, 2, step, 0.3f));
    engine.send(make(Command::SetStepChance, 0, 0, 11, 0.5f));
    engine.send(make(Command::SetStepChance, 0, 6, 3, 0.5f));
    engine.send(make(Command::SetStepChance, 0, 6, 9, 0.75f));

    // Two bars of drums alone, then bass, then chords.
    const int bassLine[16] = {33, 0, 0, 33, 0, 0, 45, 0, 33, 0, 0, 36, 0, 0, 40, 0};
    const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {55, 59, 62}, {52, 55, 59}};
    std::vector<float> out;
    std::vector<float> slice(2 * stepFrames);
    int bassNote = 0;
    const int* chord = nullptr;
    engine.send(make(Command::Play, 0));
    const int bars = 8;
    for (int step = 0; step < bars * 16; ++step) {
        const int bar = step / 16, inBar = step % 16;
        if (bar >= 2) {
            if (bassNote && (bassLine[inBar] || inBar % 4 == 2)) { engine.send(make(Command::NoteOff, 1, bassNote)); bassNote = 0; }
            if (bassLine[inBar]) { bassNote = bassLine[inBar]; engine.send(make(Command::NoteOn, 1, bassNote, 0, 1.f)); }
        }
        if (bar >= 4 && inBar == 0) {
            if (chord) for (int i = 0; i < 3; ++i) engine.send(make(Command::NoteOff, 2, chord[i]));
            chord = chords[bar % 4];
            for (int i = 0; i < 3; ++i) engine.send(make(Command::NoteOn, 2, chord[i], 0, 1.f));
        }
        engine.render(slice.data(), stepFrames);
        out.insert(out.end(), slice.begin(), slice.end());
    }
    // Let go and let it ring out.
    engine.send(make(Command::Stop, 0));
    if (bassNote) engine.send(make(Command::NoteOff, 1, bassNote));
    if (chord) for (int i = 0; i < 3; ++i) engine.send(make(Command::NoteOff, 2, chord[i]));
    for (int i = 0; i < 24; ++i) {
        engine.render(slice.data(), stepFrames);
        out.insert(out.end(), slice.begin(), slice.end());
    }

    float peak = 0.f;
    for (float s : out) peak = (s < 0 ? -s : s) > peak ? (s < 0 ? -s : s) : peak;
    std::printf("%.1f seconds, peak %.2f\n", out.size() / 2.0 / rate, peak);
    return writeWav(argv[1], out, rate) ? 0 : 1;
}
