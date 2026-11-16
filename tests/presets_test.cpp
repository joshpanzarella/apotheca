// Presets as a track's whole sound (docs/plans/presets-plan.md): the player's own preset files with
// the effects, sends, GLIDE FROM and MODs in them; the built-in data compiled in from app/presets
// (presets added after the engine's own, and a track sound for any built-in); picking one, undo, and
// browsing many without losing a command. `presets_test --print` lists the shipped data's presets.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "instruments.hpp"
#include "mods.hpp"
#include "presets.hpp"
#include "song_file.hpp"
#include "synth_spec.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

const TrackKind kSynths[] = {TrackKind::Phazed, TrackKind::Jx3p, TrackKind::Fm6, TrackKind::Physic};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    std::vector<float> render(long audioFrames) {
        std::vector<float> all, out(2 * 1000);
        for (; audioFrames > 0; audioFrames -= 1000) {
            const int n = audioFrames < 1000 ? static_cast<int>(audioFrames) : 1000;
            engine->render(out.data(), n);
            all.insert(all.end(), out.begin(), out.begin() + 2 * n);
        }
        return all;
    }
    void settle() { frames(60); render(1000); } // a burst of edits ends; what was sent is taken
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
};

std::string folderMade() {
    char folder[] = "/tmp/apotheca-presets-XXXXXX";
    CHECK(mkdtemp(folder) != nullptr);
    return folder;
}
void removeFolder(const std::string& folder) {
    const std::string command = "rm -rf '" + folder + "'";
    CHECK(std::system(command.c_str()) == 0);
}

ModSettings mod(int kind, int target, float depth, int shape = kModSquare) {
    ModSettings m;
    m.kind = kind;
    m.target = target;
    m.depth = depth;
    m.shape = shape;
    return m;
}
int effectLane(int slot, int setting) { return kModFirstEffect + slot * 4 + setting; }
std::string soundNow(const TrackData& t) { return trackSoundText(t.kind, trackSoundOf(t)); }

// The engine holds the track's sound as the project does: each slot's kind and settings (a setting's
// lane rests on the knob), each send, and the MODs (run a moment after Play: a SQUARE gives its depth).
bool engineAgrees(Rig& rig, int track) {
    rig.render(2400); // the 10 ms fades done, the commands taken
    const TrackData& t = rig.project().tracks[track];
    bool fine = true;
    for (int slot = 0; slot < kTrackSlots; ++slot) {
        fine = fine && rig.engine->effectKindIn(track, slot) == t.effects[slot].kind;
        const EffectKind kind = static_cast<EffectKind>(t.effects[slot].kind);
        for (int i = 0; i < effectInfo(kind).params; ++i) fine = fine && rig.engine->laneCentre(track, effectLane(slot, i)) == t.effects[slot].param[i];
    }
    for (int bus = 0; bus < kBuses; ++bus) fine = fine && rig.engine->sendGiven(track, bus) == t.send[bus];
    for (int m = 0; m < kSynthMacros; ++m) fine = fine && rig.engine->laneCentre(track, m) == t.macro[m];
    return fine;
}
// What the engine's MODs add to a lane, a moment after Play (then Stop).
float added(Rig& rig, int track, int lane) {
    rig.frames(1);
    rig.render(1000);
    rig.session().togglePlay(false);
    rig.frames(1);
    rig.render(3000);
    const float offset = rig.engine->modOffsetForTests(track, lane);
    rig.session().togglePlay(false);
    rig.frames(1);
    rig.render(1000);
    return offset;
}

// A track sound with something in every part: CHORUS and HALL in slots 1 and 3, both sends, PAIRED,
// a SQUARE LFO on HALL's MIX and a STEPPER with a row on a macro.
void dress(Session& session, int track) {
    session.setEffect(track, 0, EffectKind::Chorus);
    session.setEffectParam(track, 0, 1, 0.75f);
    session.setEffect(track, 2, EffectKind::Hall);
    session.setEffectParam(track, 2, 0, 0.25f);
    session.setEffectParam(track, 2, 3, 0.5f);
    session.setSend(track, 0, 0.2f);
    session.setSend(track, 1, 0.6f);
    session.setGlideFrom(track, kFromPaired);
    CHECK(session.setMod(track, 0, mod(kModLfo, effectLane(2, 3), 0.25f)));
    ModSettings stepper = mod(kModStepper, 2, -0.5f);
    stepper.length = 8;
    stepper.slew = 0.25f;
    for (int i = 0; i < kModSteps; ++i) stepper.steps[i] = (i % 3 - 1) * 0.5f;
    CHECK(session.setMod(track, 1, stepper));
}

// ---- the built-in data as shipped -------------------------------------------------------------

// Every shipped line reads, every added preset is in its folder after the engine's own, holds
// settings in their ranges, and plays: a number every sample, a note heard.
void testTheShippedData(bool print) {
    Rig rig;
    Session& session = rig.session();
    for (TrackKind kind : kSynths) {
        const PresetData& data = builtInData(kind);
        for (const std::string& p : data.problems) std::printf("  %s, %s\n", instrument(kind)->word, p.c_str());
        CHECK(data.problems.empty());
        CHECK(static_cast<int>(data.sounds.size()) == enginePresets(kind));
        CHECK(builtInPresets(kind) == enginePresets(kind) + static_cast<int>(data.added.size()));
        const SynthSpec& spec = *specFor(kind);
        CHECK(spec.presetCount() == builtInPresets(kind) + static_cast<int>(userPresets(kind).size()));
        CHECK(session.addTrack(kind));
        const int track = rig.project().trackCount() - 1;
        for (int i = 0; i < builtInPresets(kind); ++i) {
            const bool isAdded = i >= enginePresets(kind);
            if (print && (isAdded || presetSound(kind, i))) std::printf("  %-6s %3d %-14s %-20s %s\n", instrument(kind)->word, i + 1, spec.presetCategory(i), spec.presetName(i), presetSound(kind, i) ? "with its track sound" : "");
            if (!isAdded) continue;
            const UserPreset& p = data.added[static_cast<std::size_t>(i - enginePresets(kind))];
            CHECK(listedPreset(kind, i) == &p && std::string(spec.presetName(i)) == p.name && std::string(spec.presetCategory(i)) == p.folder);
            CHECK(static_cast<int>(p.values.size()) == presetSettingCount(kind) && p.macros.size() == static_cast<std::size_t>(kSynthMacros));
            for (int id = 0; id < presetSettingCount(kind); ++id) CHECK(p.values[static_cast<std::size_t>(id)] >= spec.settingMin(id) && p.values[static_cast<std::size_t>(id)] <= spec.settingMax(id));
            session.chooseSynthPreset(track, i);
            rig.settle();
            CHECK(engineAgrees(rig, track));
            session.audition(track, 60, 0.8f);
            const std::vector<float> heard = rig.render(24000);
            float most = 0.f;
            bool numbers = true;
            for (float v : heard) numbers = numbers && std::isfinite(v), most = std::fabs(v) > most ? std::fabs(v) : most;
            CHECK(numbers && most > 0.001f);
            rig.render(96000);
        }
    }
    // The added folders come after the engine's: each one's presets side by side, no folder twice.
    for (TrackKind kind : kSynths) {
        const SynthSpec& spec = *specFor(kind);
        for (int i = enginePresets(kind); i < builtInPresets(kind); ++i)
            for (int j = 0; j < enginePresets(kind); ++j) CHECK(std::strcmp(spec.presetCategory(i), spec.presetCategory(j)) != 0);
    }
}

// ---- the data's reader -------------------------------------------------------------------------

// What a designer writes: a new preset from a base with settings set, the track sound's lines, notes
// after #, and an engine built-in given a sound.
void testTheDataReads() {
    const std::string text =
        "# notes are passed by\n"
        "\n"
        "new PADS / Warm Pad   # a note after a line too\n"
        "base 2\n"
        "set 27 70 28 60\n"
        "macros 0.4 0.5 0.5 0.5 0.5 0.5 0.5 0.9\n"
        "fx 1 chorus 0.3 0.6 0 0.7\n"
        "fx 3 hall 0.7 0.6 0.45 0.35\n"
        "send 2 0.25\n"
        "glidefrom 2\n"
        "mod 1 1 0 0.6 9 0 0 0\n"
        "mod 2 2 19 0.25 5 0 0 0\n"
        "modsteps 2 4 0 0 1 -1 0.5 -0.5\n"
        "new PADS / Bare\n"
        "base 33\n"            // the one before: an added one, its settings and macros
        "set 16 10\n"
        "new PADS / Empty\n"
        "whole\n"              // a whole sound with nothing in it: picked, it empties the track's
        "preset 3\n"           // an engine built-in, given a sound
        "send 1 0.5\n";
    const PresetData data = readPresetData(TrackKind::Jx3p, text);
    for (const std::string& p : data.problems) std::printf("  %s\n", p.c_str());
    CHECK(data.problems.empty());
    CHECK(data.added.size() == 3 && data.sounds.size() == static_cast<std::size_t>(Jx3p::presetCount()));
    const UserPreset& pad = data.added[0];
    CHECK(pad.folder == "PADS" && pad.name == "Warm Pad");
    const Jx3pPatch two = Jx3p::presetPatch(1);
    for (int id = 0; id < jx3p::kPatchSettings; ++id) CHECK(pad.values[static_cast<std::size_t>(id)] == (id == jx3p::Attack ? 70 : id == jx3p::Decay ? 60 : two.value[id]));
    CHECK(pad.values.size() == static_cast<std::size_t>(jx3p::kPatchSettings));
    CHECK(pad.macros[0] == 0.4f && pad.macros[7] == 0.9f && pad.macros[3] == 0.5f);
    CHECK(pad.sound.whole && pad.sound.effects[0].kind == static_cast<int>(EffectKind::Chorus) && pad.sound.effects[0].param[1] == 0.6f);
    CHECK(pad.sound.effects[1].kind == 0 && pad.sound.effects[2].kind == static_cast<int>(EffectKind::Hall) && pad.sound.effects[2].param[3] == 0.35f);
    CHECK(pad.sound.send[0] == 0.f && pad.sound.send[1] == 0.25f && pad.sound.glideFrom == kFromNearest);
    CHECK(pad.sound.mods[0].kind == kModLfo && pad.sound.mods[0].target == 0 && std::fabs(pad.sound.mods[0].depth - 0.2f) < 1e-6f && pad.sound.mods[0].rate == kModRateTwoBars);
    CHECK(pad.sound.mods[1].kind == kModStepper && pad.sound.mods[1].target == 19 && pad.sound.mods[1].length == 4 && pad.sound.mods[1].steps[1] == -1.f && pad.sound.mods[1].steps[4] == 0.f);
    const UserPreset& bare = data.added[1];
    for (int id = 0; id < jx3p::kPatchSettings; ++id) CHECK(bare.values[static_cast<std::size_t>(id)] == (id == jx3p::Cutoff ? 10 : pad.values[static_cast<std::size_t>(id)]));
    CHECK(bare.macros == pad.macros && !bare.sound.whole); // (the base's settings and macros, not its sound)
    CHECK(data.added[2].sound.whole && trackSoundText(TrackKind::Jx3p, data.added[2].sound).empty());
    CHECK(data.sounds[2].whole && data.sounds[2].send[0] == 0.5f);
    for (int i = 0; i < Jx3p::presetCount(); ++i) CHECK(data.sounds[static_cast<std::size_t>(i)].whole == (i == 2));
}

// What a designer gets wrong: every slip is named with its line, the rest is still read, and nothing
// past a setting's range or not a number gets in.
void testTheDataSaysWhatIsWrong() {
    const std::string text =
        "fx 1 hall 0.5\n"                            // 1 outside a preset
        "new PADS\n"                                 // 2 no " / "
        "new BANK A / Mine\n"                        // 3 an engine folder
        "new USER / Mine\n"                          // 4 the player's folder
        "new A FOLDER FAR TOO LONG / Mine\n"         // 5 a folder too long
        "new PADS / One\n"                           // 6 (fine)
        "set 17 1e300 16 -1e300 99 5 15 -7 3 2.5\n"  // 7 five slips: too big, too small, no such id, under its least (15 HPF: 0), not whole
        "settings 1 2 1\n"                           // (fine: the first three)
        "frobnicate 3\n"                             // 9 a word nobody knows
        "fx 4 hall 0.5\n"                            // 10 no slot 4
        "fx 2 wobbler 0.5\n"                         // 11 no such effect
        "fx 1 chorus 2 0.5\n"                        // 12 a setting past 1 (held to 1)
        "send 1 -3\n"                                // 13 a send under 0
        "mod 1 1 12 0.75 8 0 0 0\n"                  // (aimed at slot 2's first setting: an empty slot)
        "macros 0.5 0.5\n"                           // 15 not eight
        "base 40\n"                                  // 16 no such built-in yet
        "new LEADS / Two\n"                          // 17 (fine; the MOD's slip is named here, as the preset before it ends)
        "new PADS / Three\n"                         // 18 PADS again after LEADS
        "new LEADS / Two\n"                          // 19 a name its folder has
        "preset 0\n"                                 // 20 from 1
        "preset 99\n"                                // 21 past the engine's
        "preset 4\n"
        "set 3 4\n"                                  // 23 an engine built-in takes only the sound's lines
        "glidefrom 1\n"
        "new LAST / Pair\n"                          // 25 (fine)
        "set 3\n";                                   // 26 a pair cut short (twice: and nothing set)
    const PresetData data = readPresetData(TrackKind::Jx3p, text);
    const auto said = [&](int line) {
        const std::string start = "line " + std::to_string(line) + ":";
        int n = 0;
        for (const std::string& p : data.problems) n += p.compare(0, start.size(), start) == 0;
        return n;
    };
    CHECK(said(1) == 1 && said(2) == 1 && said(3) == 1 && said(4) == 1 && said(5) == 1 && said(6) == 0);
    CHECK(said(7) == 5 && said(8) == 0 && said(9) == 1 && said(10) == 1 && said(11) == 1 && said(12) == 1 && said(13) == 1);
    CHECK(said(15) == 1 && said(16) == 1 && said(17) == 1 && said(18) == 1 && said(19) == 1 && said(20) == 1 && said(21) == 1 && said(22) == 0 && said(23) == 1 && said(24) == 0);
    CHECK(said(25) == 0 && said(26) == 2);
    CHECK(data.problems.size() == 25u);
    if (data.problems.size() != 25u)
        for (const std::string& p : data.problems) std::printf("    %s\n", p.c_str());
    CHECK(data.added.size() == 3 && data.added[0].name == "One" && data.added[1].name == "Two");
    CHECK(data.added[0].values[jx3p::Dco2Range] == Jx3p::presetPatch(0).value[jx3p::Dco2Range] && Jx3p::presetPatch(0).value[jx3p::Dco2Range] != Jx3p::presetPatch(1).value[jx3p::Dco2Range]); // no base: the first built-in's
    const UserPreset& one = data.added[0];
    CHECK(one.values[jx3p::Resonance] == 100 && one.values[jx3p::Cutoff] == 0 && one.values[jx3p::Hpf] == 0 && one.values[3] == 1);
    CHECK(one.values[0] == 1 && one.values[1] == 2 && one.values[2] == 1); // settings 1 2 1, after the set
    CHECK(one.sound.effects[0].param[0] == 1.f && one.sound.send[0] == 0.f && one.sound.effects[1].kind == 0 && one.sound.effects[3 - 1].kind == 0);
    CHECK(one.sound.mods[0].kind == kModOff); // aimed at nothing there: OFF
    CHECK(one.macros[0] == 0.5f && one.macros[2] == 0.5f);
    CHECK(data.sounds[3].whole && data.sounds[3].glideFrom == kFromPaired);
}

// ---- the player's own presets ------------------------------------------------------------------

// SAVE writes the track's whole sound, and picking it again puts every part of it back, in the
// project and in the engine; one step of undo takes the pick back.
void testASavedPresetHoldsTheWholeSound() {
    Rig rig;
    Session& session = rig.session();
    const std::string folder = folderMade();
    session.usePresetFolder(folder);
    for (TrackKind kind : kSynths) {
        CHECK(session.addTrack(kind));
        const int track = rig.project().trackCount() - 1;
        TrackData& t = rig.project().tracks[track];
        session.chooseSynthPreset(track, 1);
        session.setSynthMacro(track, 3, 0.8f);
        dress(session, track);
        rig.settle();
        const std::string sound = soundNow(t);
        CHECK(!sound.empty());
        CHECK(session.savePreset(track, "WHOLE"));
        const int mine = builtInPresets(kind);
        CHECK(t.preset == mine);
        std::string file;
        CHECK(readTextFile(folder + "/WHOLE" + instrument(kind)->presetEnding, file));
        // The file: the synth's lines as before, then "whole" and the song file's own lines.
        CHECK(file.find("\nwhole\nfx 1 chorus") != std::string::npos && file.find("\nfx 3 hall") != std::string::npos && file.find("\nsend 2 0.6\n") != std::string::npos);
        CHECK(file.find("\nglidefrom 1\n") != std::string::npos && file.find("\nmod 1 1 ") != std::string::npos && file.find("\nmodsteps 2 8 ") != std::string::npos);
        CHECK(file.substr(file.find("\nwhole\n") + 7) == sound);
        CHECK(file.find("mix") == std::string::npos); // the mixer is the song's balance, not the sound's
        const UserPreset& kept = userPresets(kind)[0];
        CHECK(kept.sound.whole && trackSoundText(kind, kept.sound) == sound && kept.macros[3] == 0.8f);

        // Everything taken off, and the preset picked: all of it back, the engine too.
        for (int slot = 0; slot < kTrackSlots; ++slot) session.setEffect(track, slot, EffectKind::None);
        session.setSend(track, 0, 0.f);
        session.setSend(track, 1, 0.f);
        session.setGlideFrom(track, kFromTop);
        session.setMod(track, 1, ModSettings());
        session.chooseSynthPreset(track, 0);
        rig.settle();
        CHECK(soundNow(t).empty());
        const int steps = session.undoSteps();
        session.chooseSynthPreset(track, mine);
        rig.settle();
        CHECK(soundNow(t) == sound && t.macro[3] == 0.8f);
        CHECK(engineAgrees(rig, track));
        CHECK(added(rig, track, effectLane(2, 3)) == 0.25f);
        // One step of undo: the pick taken back whole.
        CHECK(session.undoSteps() == steps + 1);
        CHECK(session.undo());
        rig.settle();
        CHECK(soundNow(t).empty() && t.preset == 0 && t.macro[3] == 0.5f);
        CHECK(engineAgrees(rig, track));
        CHECK(added(rig, track, effectLane(2, 3)) == 0.f);
        setUserPresets(kind, {});
        std::remove((folder + "/WHOLE" + instrument(kind)->presetEnding).c_str());
    }
    removeFolder(folder);
}

// A preset file written before the track sound was saved (no "whole"), or by hand with nothing past
// the synth, is plain: picked, it leaves the track's effects alone. One saved WHOLE with nothing in
// it empties them, as the track was when it was saved.
void testAnOlderFileIsPlain() {
    Rig rig;
    Session& session = rig.session();
    const std::string folder = folderMade();
    CHECK(writeTextFile(folder + "/OLD.fm6", "apotheca-preset 1\nsynth fm6\nmacros 0.5 0.5 0.5 0.5 0.5 0.5 0.5 0.5\nsettings 1 2 3\n"));
    CHECK(writeTextFile(folder + "/BARE.fm6", "apotheca-preset 1\nsynth fm6\nmacros 0.5 0.5 0.5 0.5 0.5 0.5 0.5 0.5\nsettings 1 2 3\nwhole\n"));
    session.usePresetFolder(folder);
    CHECK(session.addTrack(TrackKind::Fm6));
    const int track = 1, first = builtInPresets(TrackKind::Fm6);
    TrackData& t = rig.project().tracks[track];
    CHECK(userPresets(TrackKind::Fm6).size() == 2 && userPresets(TrackKind::Fm6)[1].name == "OLD");
    dress(session, track);
    rig.settle();
    const std::string mine = soundNow(t);
    session.chooseSynthPreset(track, first + 1); // OLD
    rig.settle();
    CHECK(soundNow(t) == mine && t.fm6.value[0] == 1 && t.fm6.value[2] == 3);
    session.chooseSynthPreset(track, first); // BARE: whole, and empty
    rig.settle();
    CHECK(soundNow(t).empty() && t.glideFrom == kFromTop);
    CHECK(engineAgrees(rig, track));
    setUserPresets(TrackKind::Fm6, {});
    removeFolder(folder);
}

// A damaged preset file is read as carefully as a song: numbers held as doubles first, words nobody
// knows passed by, a MOD aimed at nothing OFF, and a file with no settings refused.
void testADamagedFile() {
    UserPreset p;
    const std::string text =
        "apotheca-preset 1\r\n"
        "synth physic\r\n"
        "macros 1e300 -1e300 0.25 0.5 0.5 0.5 0.5 0.5 0.7 nan\r\n" // (a "nan" ends what `>>` reads of a line)
        "macros nan 0.1\r\n"
        "settings 1e300 -1e300 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 nan\r\n"
        "whatever 1 2 3\r\n"
        "fx 1 hall 1e300 -5 0.5 9\r\n"
        "fx 2 chorus nan 0.5\r\n"
        "fx 2 wobbler 0.5\r\n"
        "fx nan chorus 0.5\r\n"
        "fx 1e300 chorus 0.5\r\n"
        "send 1 nan\r\n"
        "send 2 1e300\r\n"
        "send 3 0.5\r\n"
        "glidefrom 1e300\r\n"
        "mod 1 1 17 0.75 1e300 -4 9\r\n"              // aimed at slot 3: empty, so OFF
        "mod 2 1 9 1e300 -3 8 0 0 1e300\r\n"           // HALL's DECAY, depth past the most, rate unknown, speed past the most
        "modsteps 2 1e300 0.5 7 1e300 -1e300 0.5 nan 0.25\r\n"
        "mod 9 1 9 0.5\r\n";
    CHECK(readPresetFile(TrackKind::Physic, text, p));
    CHECK(p.macros.size() == 8u && p.macros[0] == 1.f && p.macros[1] == 0.f && p.macros[2] == 0.25f && p.macros[7] == 0.5f);
    CHECK(static_cast<int>(p.values.size()) == presetSettingCount(TrackKind::Physic));
    CHECK(p.values[0] == 1000000 && p.values[1] == -1000000 && p.values[2] == 3 && p.values[17] == 18);
    CHECK(p.sound.whole);
    const EffectData& hall = p.sound.effects[0];
    CHECK(hall.kind == static_cast<int>(EffectKind::Hall) && hall.param[0] == 1.f && hall.param[1] == 0.f && hall.param[2] == 0.5f && hall.param[3] == 1.f);
    const EffectData& chorus = p.sound.effects[1]; // "nan" read as nothing: its usual settings
    CHECK(chorus.kind == static_cast<int>(EffectKind::Chorus) && chorus.param[0] == effectInfo(EffectKind::Chorus).defaults[0] && chorus.param[1] == effectInfo(EffectKind::Chorus).defaults[1]);
    CHECK(p.sound.effects[2].kind == 0);
    CHECK(p.sound.send[0] == 0.f && p.sound.send[1] == 1.f && p.sound.glideFrom == kFromTop);
    CHECK(p.sound.mods[0].kind == kModOff);
    const ModSettings& m = p.sound.mods[1];
    CHECK(m.kind == kModLfo && m.target == 9 && m.depth == 1.f && m.rate == kModRateBar && m.speed == kModSpeedMost);
    CHECK(m.length == kModSteps && m.slew == 0.5f && m.chance == 1.f && m.steps[0] == 1.f && m.steps[1] == -1.f && m.steps[2] == 0.5f && m.steps[3] == 0.f);
    // ...and a track plays it: every sample a number.
    Rig rig;
    const std::string folder = folderMade();
    CHECK(writeTextFile(folder + "/DAMAGED.physic", text));
    rig.session().usePresetFolder(folder);
    CHECK(rig.session().addTrack(TrackKind::Physic) && userPresets(TrackKind::Physic).size() == 1);
    rig.session().chooseSynthPreset(1, builtInPresets(TrackKind::Physic));
    rig.settle();
    for (int id = 0; id < physic::kSettings; ++id) CHECK(rig.project().tracks[1].physic.value[id] >= physic::settingMin(id) && rig.project().tracks[1].physic.value[id] <= physic::settingMax(id));
    CHECK(engineAgrees(rig, 1));
    rig.session().audition(1, 60, 0.8f);
    bool numbers = true;
    for (float v : rig.render(48000)) numbers = numbers && std::isfinite(v);
    CHECK(numbers);
    setUserPresets(TrackKind::Physic, {});
    removeFolder(folder);
    // Not a preset at all, or no settings: refused.
    UserPreset none;
    CHECK(!readPresetFile(TrackKind::Physic, "apotheca-song 1\nsettings 1 2\n", none));
    CHECK(!readPresetFile(TrackKind::Physic, "apotheca-preset 1\nwhole\nfx 1 hall 0.5\n", none));
    CHECK(!readPresetFile(TrackKind::Physic, "", none));
}

// ---- built-ins with a sound ---------------------------------------------------------------------

// A built-in that carries a track sound puts it in, and undo brings back what was there; a plain one
// leaves the player's own alone; and passing over a whole one onto a plain one gives the player's
// own back, unless something was turned in between.
void testABuiltInWithItsSound() {
    const PresetData shipped = builtInData(TrackKind::Jx3p);
    const std::string text =
        "preset 4\n"
        "fx 2 plate 0.2 0.5 0.6 0.4\n"
        "send 2 0.3\n"
        "mod 1 1 20 0.75 8 4 0 0\n"           // a SQUARE on the ECHO send, +50%
        "preset 6\n"
        "fx 2 plate 0.2 0.5 0.6 0.4\n"
        "send 2 0.3\n"
        "mod 1 1 20 0.9 8 4 0 0\n"           // preset 4's MOD, deeper: +80%
        "new MORE / Added\n"
        "base 5\n"
        "fx 1 shimmer 0.5 0.6 0.6 0.3\n"
        "mod 2 1 11 0.25 8 4 0 0\n"           // a SQUARE on SHIMMER's MIX, -50%
        "new MORE / Plain Added\n"
        "base 6\n";
    useBuiltInDataForTests(TrackKind::Jx3p, readPresetData(TrackKind::Jx3p, text));
    CHECK(builtInData(TrackKind::Jx3p).problems.empty());
    Rig rig;
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Jx3p));
    const int track = 1, engine = Jx3p::presetCount();
    TrackData& t = rig.project().tracks[track];
    CHECK(builtInPresets(TrackKind::Jx3p) == engine + 2 && std::string(jx3pSpec().presetCategory(engine)) == "MORE");
    CHECK(jx3pSpec().folderNote(engine) == kJaxepGuessNote && jx3pSpec().folderNote(0) == kJaxepBankNote); // an added folder's speeds are guesses too
    // The player's own: a FILTER in slot 1, a send, a MOD on it.
    session.setEffect(track, 0, EffectKind::Filter);
    session.setEffectParam(track, 0, 0, 0.3f);
    session.setSend(track, 0, 0.4f);
    CHECK(session.setMod(track, 1, mod(kModLfo, effectLane(0, 0), 0.5f)));
    rig.settle();
    const std::string mine = soundNow(t);
    // A plain built-in: the sound is left.
    session.chooseSynthPreset(track, 2);
    rig.settle();
    CHECK(soundNow(t) == mine && t.preset == 2);
    // A built-in with its sound: PLATE in slot 2, the other slots empty, SPACE 0.3, ECHO 0, the MOD.
    const int steps = session.undoSteps();
    session.chooseSynthPreset(track, 3);
    rig.settle();
    CHECK(t.effects[0].kind == 0 && t.effects[1].kind == static_cast<int>(EffectKind::Plate) && t.effects[1].param[3] == 0.4f && t.effects[2].kind == 0);
    CHECK(t.send[0] == 0.f && t.send[1] == 0.3f && t.mods[0].kind == kModLfo && t.mods[1].kind == kModOff);
    CHECK(engineAgrees(rig, track) && added(rig, track, 20) == 0.5f && added(rig, track, effectLane(0, 0)) == 0.f);
    for (int id = 0; id < jx3p::kPatchSettings; ++id) CHECK(t.jx3p.value[id] == Jx3p::presetPatch(3).value[id]);
    CHECK(session.undoSteps() == steps + 1);
    // Undo: the plain one and the player's own, the engine too.
    CHECK(session.undo());
    rig.settle();
    CHECK(soundNow(t) == mine && t.preset == 2 && engineAgrees(rig, track) && added(rig, track, effectLane(0, 0)) == 0.5f);
    CHECK(session.redo());
    rig.settle();
    CHECK(t.preset == 3 && t.effects[1].kind == static_cast<int>(EffectKind::Plate));
    CHECK(session.undo());
    rig.settle();
    // Passing over it: the whole one, then a plain one, gives the player's own back...
    session.chooseSynthPreset(track, 3);
    session.chooseSynthPreset(track, 4);
    rig.settle();
    CHECK(soundNow(t) == mine && t.preset == 4 && engineAgrees(rig, track));
    // ...across two whole ones too (the added one, then the built-in, then plain)...
    session.chooseSynthPreset(track, engine);
    CHECK(t.effects[0].kind == static_cast<int>(EffectKind::Shimmer) && t.mods[1].target == effectLane(0, 3));
    session.chooseSynthPreset(track, 3);
    session.chooseSynthPreset(track, engine + 1);
    rig.settle();
    CHECK(soundNow(t) == mine && engineAgrees(rig, track));
    // ...but not once anything was turned after the whole one came: the track's sound is the player's then.
    session.chooseSynthPreset(track, 3);
    session.setEffectParam(track, 1, 0, 0.9f);
    const std::string turned = soundNow(t);
    session.chooseSynthPreset(track, 4);
    rig.settle();
    CHECK(soundNow(t) == turned && turned != mine);
    // Saving leaves nothing to give back: the sound is the player's own preset's.
    const std::string folder = folderMade();
    session.usePresetFolder(folder);
    session.chooseSynthPreset(track, 4);
    session.chooseSynthPreset(track, 3);
    const std::string plate = soundNow(t);
    CHECK(session.savePreset(track, "MINE"));
    session.chooseSynthPreset(track, 4);
    CHECK(soundNow(t) == plate);
    setUserPresets(TrackKind::Jx3p, {});
    removeFolder(folder);
    // Another song, another instrument, a track added or taken away: nothing to give back either...
    const auto plainAfter = [&](int on, const std::function<void()>& between) {
        session.chooseSynthPreset(on, 4);
        for (int slot = 0; slot < kTrackSlots; ++slot) session.setEffect(on, slot, EffectKind::None);
        session.setEffect(on, 0, EffectKind::Filter);
        session.chooseSynthPreset(on, 3);
        between();
        session.chooseSynthPreset(on, 4);
        rig.settle();
        return rig.project().tracks[on].effects[1].kind == static_cast<int>(EffectKind::Plate);
    };
    CHECK(plainAfter(track, [&] { session.applyProject(); }));
    CHECK(plainAfter(track, [&] { CHECK(session.setTrackKind(track, TrackKind::Phazed) && session.setTrackKind(track, TrackKind::Jx3p)); }));
    // ...but a track taken away takes its memory with it, and the one after it brings its own.
    CHECK(session.addTrack(TrackKind::Jx3p) && session.addTrack(TrackKind::Jx3p));
    session.chooseSynthPreset(3, 4);
    session.setEffect(3, 0, EffectKind::Filter);
    session.chooseSynthPreset(3, 3); // the whole one on track 4, its FILTER remembered
    CHECK(session.removeTrack(2));   // track 4 is track 3 now
    session.chooseSynthPreset(2, 4);
    rig.settle();
    CHECK(rig.project().tracks[2].effects[0].kind == static_cast<int>(EffectKind::Filter) && rig.project().tracks[2].effects[1].kind == 0);
    CHECK(session.addTrack(TrackKind::Jx3p)); // a new track there has nothing to give back
    session.setEffect(3, 0, EffectKind::Filter);
    session.chooseSynthPreset(3, 4);
    CHECK(rig.project().tracks[3].effects[0].kind == static_cast<int>(EffectKind::Filter));
    // WRITE on and the song playing: a preset's sound is written into no lane (its MOD's DEPTH, its sends).
    session.chooseSynthPreset(track, 3);
    session.setWriting(true);
    session.togglePlay(false);
    rig.frames(2);
    rig.render(4800);
    session.chooseSynthPreset(track, 5); // the MOD stays on, deeper: a turn of DEPTH, were it the knob's
    rig.frames(2);
    rig.render(4800);
    session.togglePlay(false);
    session.setWriting(false);
    bool written = false;
    for (int scene = 0; scene < kMaxScenes; ++scene)
        for (int lane = 0; lane < kAutoLanes; ++lane)
            for (int i = 0; i < kAutoSteps; ++i) written = written || rig.project().tracks[track].automation[scene][lane][i] != kAutoNone;
    CHECK(!written && t.mods[0].kind == kModLfo && std::fabs(t.mods[0].depth - 0.8f) < 1e-6f);
    useBuiltInDataForTests(TrackKind::Jx3p, shipped);
}

// What is heard while browsing is what loads, and browsing many presets each with a sound, fast,
// loses no command: every step's sound is the engine's, the queue never fills.
void testBrowsingMany() {
    const PresetData shipped = builtInData(TrackKind::Fm6);
    std::string text;
    const char* const kinds[] = {"chorus", "hall", "tape", "plate", "fold", "ring"};
    for (int i = 0; i < 200; ++i) {
        char line[200];
        std::snprintf(line, sizeof line, "new MANY %d / Number %d\nbase %d\nfx %d %s 0.%d 0.5 0.5 0.5\nsend %d 0.%d\nmod 1 1 %d 0.75 8 4 0 0\n%s", i / 50, i, i % 160 + 1,
                      i % 3 + 1, kinds[i % 6], i % 10, i % 2 + 1, i % 9, 20 + i % 2, i % 4 == 0 ? "glidefrom 3\n" : "");
        text += line;
    }
    useBuiltInDataForTests(TrackKind::Fm6, readPresetData(TrackKind::Fm6, text));
    CHECK(builtInData(TrackKind::Fm6).problems.empty() && builtInData(TrackKind::Fm6).added.size() == 200u);
    Rig rig;
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Fm6));
    TrackData& t = rig.project().tracks[1];
    const unsigned dropped = rig.engine->dropped();
    // The D-pad held at the list's pace: a preset a frame, a frame's sound between.
    for (int i = 0; i < 200; ++i) {
        session.chooseSynthPreset(1, Fm6::presetCount() + i);
        rig.frames(1);
        rig.render(256);
        CHECK(t.effects[i % 3].kind == static_cast<int>(effectFromWord(kinds[i % 6])) && (t.send[i % 2] > 0.f) == (i % 9 != 0));
    }
    CHECK(rig.engine->dropped() == dropped);
    rig.settle();
    CHECK(engineAgrees(rig, 1));
    // Back through them two a frame (faster than the stick's ten a second): still nothing lost.
    for (int i = 199; i >= 0; --i) {
        session.chooseSynthPreset(1, Fm6::presetCount() + i);
        if (i % 2) continue;
        rig.frames(1);
        rig.render(256);
    }
    rig.settle();
    CHECK(rig.engine->dropped() == dropped);
    CHECK(engineAgrees(rig, 1) && t.preset == Fm6::presetCount());
    // All 200 between two frames (no hand does it) fill the queue; the song is sent again once the
    // engine has caught up, and the engine holds what the track shows.
    for (int i = 0; i < 200; ++i) session.chooseSynthPreset(1, Fm6::presetCount() + i);
    CHECK(rig.engine->dropped() > dropped);
    for (int f = 0; f < 30; ++f) {
        rig.render(4096);
        rig.frames(1);
    }
    CHECK(engineAgrees(rig, 1) && t.preset == Fm6::presetCount() + 199);
    useBuiltInDataForTests(TrackKind::Fm6, shipped);
}

// A song file's track lines are written and read as before (the writer and reader the presets
// share): the song's own text, read and written again, is the same.
void testTheSongFileIsAsItWas() {
    Rig rig;
    Session& session = rig.session();
    CHECK(session.addTrack(TrackKind::Jx3p));
    dress(session, 1);
    rig.settle();
    const std::string text = songText(rig.project());
    CHECK(text.find("\nfx 1 chorus") != std::string::npos && text.find("\nmodsteps 2 ") != std::string::npos && text.find("\nglidefrom 1\n") != std::string::npos);
    std::unique_ptr<Project> again(new Project);
    CHECK(parseSong(text, *again) && songText(*again) == text);
    CHECK(trackSoundText(TrackKind::Jx3p, trackSoundOf(again->tracks[1])) == soundNow(rig.project().tracks[1]));
}

// `--show`: every built-in that carries a track sound, as the device reads it, for the presets'
// designers to check what they wrote (docs/plans/presets-plan.md).
std::string targetName(TrackKind kind, const TrackSound& sound, int target) {
    char text[64];
    if (modTargetIsMacro(target)) return std::string("macro ") + specFor(kind)->macroNames[target - kModFirstMacro];
    if (modTargetIsEffect(target)) {
        const EffectKind effect = static_cast<EffectKind>(sound.effects[modTargetSlot(target)].kind);
        std::snprintf(text, sizeof text, "slot %d %s %s", modTargetSlot(target) + 1, effectInfo(effect).name, effectInfo(effect).paramNames[modTargetSetting(target)]);
        return text;
    }
    return target == 20 ? "the ECHO send" : target == 21 ? "the SPACE send" : target == kModTargetLevel ? "LEVEL" : target == kModTargetPan ? "PAN" : "?";
}
void show() {
    for (TrackKind kind : kSynths) {
        const SynthSpec& spec = *specFor(kind);
        for (int i = 0; i < builtInPresets(kind); ++i) {
            const TrackSound* sound = presetSound(kind, i);
            const bool isAdded = i >= enginePresets(kind);
            if (!sound && !isAdded) continue;
            std::printf("%s %d: %s / %s%s\n", instrument(kind)->name, i + 1, spec.presetCategory(i), spec.presetName(i), sound ? "" : " (plain)");
            if (isAdded) {
                const UserPreset& p = *listedPreset(kind, i);
                std::printf("  macros:");
                for (int m = 0; m < kSynthMacros; ++m) {
                    char text[32];
                    spec.macroText(m, p.macros[static_cast<std::size_t>(m)], text, sizeof text);
                    std::printf(" %s %s%s", spec.macroNames[m], text, m + 1 < kSynthMacros ? "," : "\n");
                }
            }
            if (!sound) continue;
            for (int slot = 0; slot < kTrackSlots; ++slot) {
                const EffectKind effect = static_cast<EffectKind>(sound->effects[slot].kind);
                if (effect == EffectKind::None) continue;
                std::printf("  slot %d %s:", slot + 1, effectInfo(effect).name);
                for (int k = 0; k < effectInfo(effect).params; ++k) {
                    char text[32];
                    effectParamText(effect, k, sound->effects[slot].param[k], text, sizeof text, sound->effects[slot].param);
                    std::printf(" %s %s%s", effectInfo(effect).paramNames[k], text, k + 1 < effectInfo(effect).params ? "," : "\n");
                }
            }
            std::printf("  sends: ECHO %.0f%%, SPACE %.0f%%; GLIDE FROM %d\n", sound->send[0] * 100.0, sound->send[1] * 100.0, sound->glideFrom);
            for (int m = 0; m < kModsPerTrack; ++m) {
                const ModSettings& mod = sound->mods[m];
                if (mod.kind == kModOff) continue;
                std::printf("  MOD %d: %s%s, %s, %+.0f%%, on %s\n", m + 1, mod.kind == kModLfo ? "LFO " : "STEPPER", mod.kind == kModLfo ? modShapeName(mod.shape) : "",
                            modRateInfo(mod.rate).name, mod.depth * 100.0, targetName(kind, *sound, mod.target).c_str());
                if (mod.kind == kModStepper) {
                    std::printf("    its row, %d long:", mod.length);
                    for (int k = 0; k < mod.length; ++k) std::printf(" %+.2f", mod.steps[k]);
                    std::printf("\n");
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    bool print = false;
    for (int a = 1; a < argc; ++a) print = print || std::strcmp(argv[a], "--print") == 0;
    for (int a = 1; a < argc; ++a)
        if (std::strcmp(argv[a], "--show") == 0) {
            show();
            return 0;
        }
    testTheShippedData(print);
    testTheDataReads();
    testTheDataSaysWhatIsWrong();
    testASavedPresetHoldsTheWholeSound();
    testAnOlderFileIsPlain();
    testADamagedFile();
    testABuiltInWithItsSound();
    testBrowsingMany();
    testTheSongFileIsAsItWas();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("presets tests passed\n");
    return 0;
}
