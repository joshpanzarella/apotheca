// Tests for QMOD, the FM synth that is rolled: that every sound it can make
// is a sound (not silence, not rubbish), and that each control does what its
// name says. Sounds are compared by measures that do not depend on taste:
// how loud, how bright, how high.

#include <cmath>
#include <cstdio>
#include <vector>

#include "apotheca/qmod.hpp"

using namespace apotheca;
using namespace apotheca::qmod;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

struct Heard {
    std::vector<float> left, right;
    float peak = 0.f, rms = 0.f;
    double bright = 0.0; // energy of the sample-to-sample changes over the energy: higher is brighter
    int bad = 0, crossings = 0;
};

Heard render(Qmod& q, int frames) {
    Heard h;
    h.left.resize(static_cast<std::size_t>(frames));
    h.right.resize(static_cast<std::size_t>(frames));
    for (int at = 0; at < frames; at += 64) q.render(h.left.data() + at, h.right.data() + at, frames - at < 64 ? frames - at : 64);
    double sum = 0.0, diff = 0.0;
    for (int i = 0; i < frames; ++i) {
        const float l = h.left[static_cast<std::size_t>(i)], r = h.right[static_cast<std::size_t>(i)];
        if (!std::isfinite(l) || !std::isfinite(r)) ++h.bad;
        h.peak = std::fmax(h.peak, std::fmax(std::fabs(l), std::fabs(r)));
        sum += l * l;
        if (i > 0) {
            const float before = h.left[static_cast<std::size_t>(i - 1)];
            diff += (l - before) * (l - before);
            h.crossings += before < 0.f && l >= 0.f ? 1 : 0;
        }
    }
    h.rms = static_cast<float>(std::sqrt(sum / frames));
    h.bright = sum > 0.0 ? diff / sum : 0.0;
    return h;
}

// A fresh QMOD on a preset, with these settings, playing one note.
Heard play(int preset, const std::vector<std::pair<int, int>>& settings, int note = 57, int frames = 12000, float velocity = 0.8f) {
    Qmod q;
    q.prepare(48000.f);
    q.selectPreset(preset);
    for (const auto& s : settings) q.setSetting(s.first, s.second);
    q.noteOn(note, velocity);
    return render(q, frames);
}

void testEveryPresetIsASound() {
    CHECK(Qmod::presetCount() >= 20);
    for (int p = 0; p < Qmod::presetCount(); ++p) {
        const Heard h = play(p, {});
        CHECK(h.bad == 0 && h.peak > 0.05f && h.peak < 0.8f && h.rms > 0.01f);
        CHECK(Qmod::presetName(p)[0] && Qmod::presetCategory(p)[0]);
        const QmodPatch patch = Qmod::presetPatch(p);
        for (int id = 0; id < kSettings; ++id) CHECK(patch.value[id] >= settingMin(id) && patch.value[id] <= settingMax(id));
        const int family = Qmod::familyOf(patch);
        CHECK(family >= 1 && family <= 32 && familyName(family)[0]);
    }
    CHECK(familyName(0)[0] == 0 && familyName(33)[0] == 0);
    // Out of range asks for the nearest.
    CHECK(Qmod::presetPatch(-5).value[Sound] == Qmod::presetPatch(0).value[Sound]);
    CHECK(Qmod::presetPatch(9999).value[Sound] == Qmod::presetPatch(Qmod::presetCount() - 1).value[Sound]);
}

// The point of QMOD: any number is a playable sound. Several hundred of them,
// at gentle and at extreme settings, high notes and low.
void testEveryRollIsASound() {
    Qmod q;
    q.prepare(48000.f);
    q.setSetting(Sustain, 100);
    int checked = 0;
    for (int sound = 1; sound <= 9999; sound += 61) {
        for (int tension : {0, 55, 100}) {
            q.setSetting(Sound, sound);
            q.setSetting(Variation, sound % 100);
            q.setSetting(Tension, tension);
            q.setSetting(Evolve, (sound * 13) % 101);
            q.setSetting(Entangle, (sound * 7) % 101);
            q.setSetting(Affinity, (sound * 3) % 101);
            q.setSetting(Symmetry, (sound * 11) % 101);
            q.setSetting(Mutation, (sound * 5) % 101);
            const int note = 28 + sound % 70; // E1 to well up the keyboard
            q.noteOn(note, 1.f);
            const Heard h = render(q, 3000);
            q.noteOff(note);
            render(q, 600);
            if (h.bad != 0 || h.peak < 0.02f || h.peak > 0.9f) std::printf("sound %d tension %d note %d: peak %.3f bad %d\n", sound, tension, note, h.peak, h.bad);
            CHECK(h.bad == 0 && h.peak >= 0.02f && h.peak <= 0.9f);
            ++checked;
        }
    }
    CHECK(checked > 400);
    // Every family, asked for by name: a sound.
    for (int family = 1; family <= 32; ++family) {
        const Heard h = play(0, {{Family, family}});
        CHECK(h.bad == 0 && h.peak > 0.03f);
    }
}

void testTheSameNumberIsTheSameSound() {
    const Heard a = play(3, {{Sound, 1234}, {Variation, 7}}), b = play(3, {{Sound, 1234}, {Variation, 7}});
    CHECK(a.left == b.left && a.right == b.right); // nothing random at play time: a kept sound comes back exactly
    CHECK(play(3, {{Sound, 1235}, {Variation, 7}}).left != a.left); // the next number is another sound
    // A setting put back is the sound put back.
    Qmod q;
    q.prepare(48000.f);
    q.selectPreset(3);
    q.setSetting(Sound, 4000);
    q.setSetting(Sound, Qmod::presetPatch(3).value[Sound]);
    q.noteOn(57, 0.8f);
    CHECK(render(q, 12000).left == play(3, {}).left);
}

void testWhatEachControlDoes() {
    const int p = 0;
    // TENSION: how hard the operators bend each other. None: a mix of plain sines. Full: much brighter.
    const Heard calm = play(p, {{Tension, 0}, {Sustain, 100}}), tense = play(p, {{Tension, 100}, {Sustain, 100}});
    CHECK(tense.bright > 5.0 * calm.bright && calm.bright < 0.02);
    CHECK(play(p, {{Tension, 50}, {Sustain, 100}}).bright > calm.bright);
    // EVOLVE: how long the brightness lasts in a note. Low: bright at the start and soon plain. High: it stays.
    const auto late = [&](int evolve) {
        Qmod q;
        q.prepare(48000.f);
        q.selectPreset(p);
        q.setSetting(Sustain, 100);
        q.setSetting(Tension, 100); // enough that the bending is most of the brightness
        q.setSetting(Evolve, evolve);
        q.noteOn(57, 0.8f);
        const double early = render(q, 2400).bright;
        render(q, 48000);
        const double after = render(q, 4800).bright;
        return after / early;
    };
    CHECK(late(5) < 0.6 * late(100));
    CHECK(late(100) > 0.7); // at full EVOLVE it is as bright a second later
    // ENTANGLE: all the first wiring, all the second, and between them a mix of the two.
    const Heard a = play(p, {{Entangle, 0}}), b = play(p, {{Entangle, 100}}), mid = play(p, {{Entangle, 50}});
    CHECK(a.left != b.left && mid.left != a.left && mid.left != b.left);
    bool between = true;
    for (std::size_t i = 0; i < a.left.size() && between; ++i) between = std::fabs(mid.left[i] - 0.5f * (a.left[i] + b.left[i])) < 1e-4f * (1.f + std::fabs(a.left[i]) + std::fabs(b.left[i]));
    CHECK(between); // the two are added in step, half of each: a straight crossfade
    // VARIATION and MUTATION are about the second wiring only: with ENTANGLE at none they change nothing.
    CHECK(play(p, {{Entangle, 0}, {Variation, 55}}).left == a.left && play(p, {{Entangle, 0}, {Mutation, 100}}).left == a.left);
    CHECK(play(p, {{Entangle, 100}, {Variation, 55}}).left != b.left);
    // OCTAVE: twice as high for each one up.
    const Heard low = play(p, {{Tension, 0}, {Octave, 0}}, 45), high = play(p, {{Tension, 0}, {Octave, 1}}, 45);
    CHECK(low.crossings > 10 && std::abs(high.crossings - 2 * low.crossings) <= low.crossings / 8 + 2);
    // WIDTH: none is the same in both ears; full is not.
    const Heard mono = play(8, {{Width, 0}}), wide = play(8, {{Width, 100}});
    CHECK(mono.left == mono.right && wide.left != wide.right);
    // LEVEL: 20 dB down is a tenth.
    const Heard full = play(p, {{Level, 0}}), quiet = play(p, {{Level, -20}});
    CHECK(std::fabs(quiet.peak / full.peak - 0.1f) < 0.01f);
    // A harder key is louder, and a little brighter.
    const Heard soft = play(p, {{Tension, 70}}, 57, 12000, 0.3f), hard = play(p, {{Tension, 70}}, 57, 12000, 1.f);
    CHECK(hard.peak > 2.f * soft.peak && hard.bright > soft.bright);
    // Settings are held to their ranges, and one that does not exist is nothing.
    Qmod q;
    q.prepare(48000.f);
    q.setSetting(Sound, 0);       // below 1
    q.setSetting(Sound, 99999);
    q.setSetting(Octave, 50);
    q.setSetting(-1, 5);
    q.setSetting(kSettings, 5);
    q.setMacro(-1, 0.3f);
    q.setMacro(kMacros, 0.3f);
    q.setMacro(MacroTension, std::nanf(""));
    q.noteOn(60, 0.8f);
    const Heard held = render(q, 4000);
    CHECK(held.bad == 0 && held.peak > 0.02f);
}

void testMacrosAndNotes() {
    const int p = 0;
    const auto withMacro = [&](int macro, float value, int note = 57) {
        Qmod q;
        q.prepare(48000.f);
        q.selectPreset(p);
        q.setSetting(Sustain, 100);
        q.setMacro(macro, value);
        q.noteOn(note, 0.8f);
        return render(q, 12000);
    };
    const Heard plain = play(p, {{Sustain, 100}});
    // In the middle a macro leaves the patch alone.
    for (int m = 0; m < kMacros; ++m) CHECK(withMacro(m, 0.5f).left == plain.left);
    // TENSION up is brighter, down is plainer.
    CHECK(withMacro(MacroTension, 1.f).bright > plain.bright && withMacro(MacroTension, 0.f).bright < plain.bright);
    // ENTANGLE and VARIATION change the sound; OCTAVE moves it an octave at each end.
    CHECK(withMacro(MacroEntangle, 1.f).left != plain.left);
    Qmod q;
    q.prepare(48000.f);
    q.selectPreset(p);
    q.setSetting(Sustain, 100);
    q.setSetting(Entangle, 100);
    q.noteOn(57, 0.8f);
    const Heard second = render(q, 12000);
    Qmod v;
    v.prepare(48000.f);
    v.selectPreset(p);
    v.setSetting(Sustain, 100);
    v.setSetting(Entangle, 100);
    v.setMacro(MacroVariation, 1.f);
    v.noteOn(57, 0.8f);
    CHECK(render(v, 12000).left != second.left);
    CHECK(withMacro(MacroOctave, 1.f).crossings > 1.7 * plain.crossings && withMacro(MacroOctave, 0.f).crossings < 0.6 * plain.crossings);
    // ATTACK up: a slow start.
    const auto early = [](const Heard& h) { float e = 0.f; for (int i = 0; i < 240; ++i) e = std::fmax(e, std::fabs(h.left[static_cast<std::size_t>(i)])); return e / h.peak; };
    CHECK(early(withMacro(MacroAttack, 1.f)) < 0.3f * early(plain));
    // A preset chosen puts the macros back in the middle.
    Qmod r;
    r.prepare(48000.f);
    r.setMacro(MacroTension, 1.f);
    r.selectPreset(p);
    r.setSetting(Sustain, 100);
    r.noteOn(57, 0.8f);
    CHECK(render(r, 12000).left == plain.left);

    // Notes: held it sounds, let go it dies away and stops; eight at once, and a ninth takes the oldest's place.
    Qmod n;
    n.prepare(48000.f);
    n.setSetting(Sustain, 100);
    n.setSetting(Release, 10);
    n.noteOn(60, 0.8f);
    CHECK(render(n, 24000).peak > 0.05f);
    CHECK(render(n, 4800).peak > 0.05f);
    n.noteOff(60);
    render(n, 4800);
    CHECK(render(n, 2400).peak == 0.f);
    n.noteOff(60); // twice is nothing
    for (int i = 0; i < kVoices; ++i) n.noteOn(48 + 3 * i, 0.5f);
    const float eight = render(n, 4800).rms;
    n.noteOn(90, 0.5f);
    const Heard nine = render(n, 4800);
    CHECK(nine.bad == 0 && nine.rms < 1.4f * eight && nine.rms > 0.5f * eight);
    n.allNotesOff();
    render(n, 9600);
    CHECK(render(n, 2400).peak == 0.f);
    // The same key again while it sounds does not start a second voice of it, or click.
    n.noteOn(60, 0.8f);
    render(n, 4800);
    const Heard before = render(n, 64);
    n.noteOn(60, 0.8f);
    const Heard after = render(n, 64);
    CHECK(std::fabs(after.left[0] - before.left[63]) < 0.08f && after.peak < 1.5f * before.peak + 0.05f);
    // A very high note with a high-tuned operator leaves that operator out rather than fold it back: still clean.
    Qmod high;
    high.prepare(48000.f);
    high.setSetting(Octave, 2);
    high.noteOn(108, 1.f);
    const Heard top = render(high, 4800);
    CHECK(top.bad == 0 && top.peak <= 0.9f);
    // Another sample rate: the same pitch.
    Qmod slow;
    slow.prepare(44100.f);
    slow.selectPreset(p);
    slow.setSetting(Tension, 0);
    slow.noteOn(45, 0.8f);
    const Heard at441 = render(slow, 44100);
    const Heard at48 = play(p, {{Tension, 0}}, 45, 48000);
    CHECK(std::abs(at441.crossings - at48.crossings) <= 3);
}

} // namespace

int main() {
    testEveryPresetIsASound();
    testEveryRollIsASound();
    testTheSameNumberIsTheSameSound();
    testWhatEachControlDoes();
    testMacrosAndNotes();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("qmod tests passed\n");
    return 0;
}
