// MOD in the app (batch 2 of docs/plans/modulation-plan.md): a track's two modulators in the
// project, the Session's setMod, what the engine is told (a song opened, undo, a full queue),
// the song file's `mod` and `modsteps` words, and a TYPE change carrying a MOD's target by name.
// The rule itself and the engine's side are in tests/mod_test.cpp. Nothing here is on a screen.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "fx_screen.hpp"
#include "mods.hpp"
#include "song_file.hpp"

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

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    void render(long audioFrames) {
        std::vector<float> out(2 * 1000);
        for (; audioFrames > 0; audioFrames -= 1000) engine->render(out.data(), audioFrames < 1000 ? static_cast<int>(audioFrames) : 1000);
    }
    void settle() { frames(60); render(1000); } // a burst of edits ends; what was sent is taken
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }

    // What the engine's MODs add to a lane of a track a moment after Play (then Stop again).
    // With a SQUARE or a STEPPER's first value that is depth x the value, from the first tick.
    float added(int track, int lane) {
        frames(1);
        render(1000);
        session().togglePlay(false);
        frames(1);
        render(3000);
        const float offset = engine->modOffsetForTests(track, lane);
        session().togglePlay(false);
        frames(1);
        render(1000);
        CHECK(engine->modOffsetForTests(track, lane) == 0.f); // Stop: the knob is heard
        return offset;
    }
    // ...to every lane of the track, for "nowhere else".
    std::vector<float> addedAll(int track) {
        frames(1);
        render(1000);
        session().togglePlay(false);
        frames(1);
        render(3000);
        std::vector<float> all;
        for (int lane = 0; lane < kAutoLanes; ++lane) all.push_back(engine->modOffsetForTests(track, lane));
        session().togglePlay(false);
        frames(1);
        render(1000);
        return all;
    }
    bool onlyAt(int track, int lane, float depth) {
        const std::vector<float> all = addedAll(track);
        bool fine = true;
        for (int l = 0; l < kAutoLanes; ++l) fine = fine && all[static_cast<std::size_t>(l)] == (l == lane ? depth : 0.f);
        return fine;
    }
};

// A MOD that gives its depth from the first tick after Play: a SQUARE, a bar long.
ModSettings square(int target, float depth) {
    ModSettings m;
    m.kind = kModLfo;
    m.target = target;
    m.depth = depth;
    m.shape = kModSquare;
    return m;
}
bool sameMod(const ModSettings& a, const ModSettings& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool blank(const ModSettings& m) { return sameMod(m, ModSettings()); }
int lane(int slot, int setting) { return kModFirstEffect + slot * 4 + setting; }
// The first setting of an effect that is an amount (or a choice), -1 if it has none.
int settingThat(EffectKind kind, bool choice) {
    for (int i = 0; i < effectInfo(kind).params; ++i)
        if ((effectChoices(kind, i) > 0) == choice) return i;
    return -1;
}
int settingNamed(EffectKind kind, const char* name) {
    for (int i = 0; i < effectInfo(kind).params; ++i)
        if (std::strcmp(effectInfo(kind).paramNames[i], name) == 0) return i;
    return -1;
}
std::size_t count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

// ---- the Session ------------------------------------------------------------------------------

// What a MOD may be aimed at, and that the engine is told: on, aimed elsewhere, OFF.
void testTheSessionHoldsAMod() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    CHECK(session.addTrack(TrackKind::Phazed) && session.addTrack(TrackKind::Fm6) && session.addTrack(TrackKind::Sampler) && session.addTrack(TrackKind::Jx3p));
    rig.settle();
    for (int t = 0; t < 5; ++t)
        for (int m = 0; m < kModsPerTrack; ++m) CHECK(blank(session.modData(t, m)));
    CHECK(rig.onlyAt(0, -1, 0.f));

    // A send: any track.
    CHECK(session.setMod(0, 0, square(20, 0.25f)) && sameMod(project.tracks[0].mods[0], square(20, 0.25f)));
    CHECK(rig.onlyAt(0, 20, 0.25f) && rig.onlyAt(1, -1, 0.f));
    CHECK(session.setMod(0, 1, square(21, -0.5f)) && rig.added(0, 21) == -0.5f && rig.added(0, 20) == 0.25f);
    // Aimed elsewhere: the lane it left has nothing added.
    CHECK(session.setMod(0, 1, square(20, -0.5f)) && rig.onlyAt(0, 20, -0.25f)); // (two on one setting add)
    // OFF: blank, whatever else was asked with it, and the engine lets go.
    ModSettings off = square(21, 0.7f);
    off.kind = kModOff;
    off.rate = kModRateQuarter;
    off.steps[3] = 0.4f;
    CHECK(session.setMod(0, 1, off) && blank(project.tracks[0].mods[1]) && rig.onlyAt(0, 20, 0.25f));
    CHECK(session.setMod(0, 0, ModSettings()) && blank(project.tracks[0].mods[0]) && rig.onlyAt(0, -1, 0.f));

    // An effect's setting: an amount is taken; a choice, a setting that is not there, an empty slot are not.
    session.setEffect(0, 1, EffectKind::Tape);
    const int amount = settingThat(EffectKind::Tape, false), choice = settingThat(EffectKind::Tape, true);
    CHECK(amount >= 0 && choice >= 0);
    CHECK(session.setMod(0, 0, square(lane(1, amount), 0.125f)) && rig.onlyAt(0, lane(1, amount), 0.125f));
    CHECK(!session.setMod(0, 0, square(lane(1, choice), 0.125f)) && blank(project.tracks[0].mods[0]) && rig.onlyAt(0, -1, 0.f)); // (and the one it had is gone: it was set OFF)
    CHECK(!session.setMod(0, 0, square(lane(0, 0), 0.125f)) && !session.setMod(0, 0, square(lane(2, 1), 0.125f))); // empty slots
    EffectKind few = EffectKind::None; // an effect with fewer than four settings
    for (int k = 1; k < kEffectKinds && few == EffectKind::None; ++k)
        if (effectInfo(static_cast<EffectKind>(k)).params < kEffectParams && effectChoices(static_cast<EffectKind>(k), 0) == 0) few = static_cast<EffectKind>(k);
    CHECK(few != EffectKind::None);
    session.setEffect(0, 0, few);
    CHECK(!session.setMod(0, 0, square(lane(0, effectInfo(few).params), 0.125f))); // a setting it does not have
    CHECK(session.setMod(0, 0, square(lane(0, 0), 0.125f)));
    session.setMod(0, 0, ModSettings());

    // A macro: a synth's, not the drums' or the sampler's; not a macro that is a choice.
    for (int macro = 0; macro < kSynthMacros; ++macro) {
        CHECK(!session.setMod(0, 0, square(macro, 0.5f)) && !session.setMod(3, 0, square(macro, 0.5f)));
        CHECK(session.setMod(1, 0, square(macro, 0.5f)) == (macro < 6)); // Phazed: CHORUS and OCTAVE are choices
        CHECK(session.setMod(2, 1, square(macro, 0.5f)));                // FM-6: every one an amount
        CHECK(session.setMod(4, 0, square(macro, 0.5f)));                // JAX-EP: every one an amount
        CHECK(blank(project.tracks[0].mods[0]) && blank(project.tracks[3].mods[0]));
        CHECK(blank(project.tracks[1].mods[0]) == (macro >= 6) && !blank(project.tracks[2].mods[1]));
    }
    CHECK(session.setMod(1, 0, square(4, 0.5f)) && rig.onlyAt(1, 4, 0.5f));
    // LEVEL and PAN (batch 8): every track that has something on it.
    for (int t : {0, 1, 2, 3, 4}) {
        CHECK(session.setMod(t, 1, square(kModTargetLevel, 0.5f)) && project.tracks[t].mods[1].target == kModTargetLevel);
        CHECK(session.setMod(t, 1, square(kModTargetPan, -0.5f)) && project.tracks[t].mods[1].target == kModTargetPan);
        CHECK(session.setMod(t, 1, ModSettings()));
    }
    // The DEPTH lanes and numbers nobody knows: no target.
    for (int target : {22, 23, 24, 31, 34, 99, -1, -7}) {
        CHECK(!session.setMod(1, 1, square(target, 0.5f)) && blank(project.tracks[1].mods[1]));
    }
    // No such track, no such MOD, a track with nothing on it.
    CHECK(!session.setMod(-1, 0, square(20, 0.5f)) && !session.setMod(kNumTracks, 0, square(20, 0.5f)) && !session.setMod(kMasterTrack, 0, square(20, 0.5f)));
    CHECK(!session.setMod(0, -1, square(20, 0.5f)) && !session.setMod(0, kModsPerTrack, square(20, 0.5f)) && !session.setMod(7, 0, square(20, 0.5f)));
    CHECK(blank(project.tracks[7].mods[0]) && blank(session.modData(-3, 0)) && blank(session.modData(0, 5)));
    CHECK(!session.setMod(7, 0, ModSettings())); // (OFF asked of a track with nothing: there is no such MOD)
    // What it holds already, asked again: nothing is sent.
    CHECK(session.setMod(1, 0, square(4, 0.5f)));
    rig.render(1000);
    CHECK(rig.engine->caughtUp() && session.setMod(1, 0, square(4, 0.5f)) && rig.engine->caughtUp());
    CHECK(session.setMod(1, 0, square(4, 0.25f)) && !rig.engine->caughtUp()); // (the check can tell)

    // Every field held to what it can be.
    ModSettings wild = square(20, 3.f);
    wild.rate = 99;
    wild.shape = -4;
    wild.restart = 7;
    wild.phase = 9;
    wild.length = 40;
    wild.slew = 2.f;
    wild.chance = -1.f;
    wild.steps[0] = 5.f;
    wild.steps[1] = -5.f;
    wild.steps[2] = std::nanf("");
    CHECK(session.setMod(0, 0, wild));
    const ModSettings& held = session.modData(0, 0);
    CHECK(held.depth == 1.f && held.rate == kModRateUsual && held.shape == kModSine && held.restart == kModRestartPlay && held.phase == 0);
    CHECK(held.length == kModSteps && held.slew == 1.f && held.chance == 0.f && held.steps[0] == 1.f && held.steps[1] == -1.f && held.steps[2] == 0.f);
    wild.kind = 9; // a kind nobody knows: OFF
    CHECK(!session.setMod(0, 0, wild) && blank(session.modData(0, 0)));

    // A STEPPER's row reaches the engine: its first value, times the depth.
    ModSettings row;
    row.kind = kModStepper;
    row.target = 21;
    row.depth = 0.5f;
    row.steps[0] = -0.5f;
    CHECK(session.setMod(0, 1, row) && rig.onlyAt(0, 21, -0.25f));
    row.steps[0] = 0.75f; // one value of the row changed: told
    CHECK(session.setMod(0, 1, row) && rig.onlyAt(0, 21, 0.375f));
    CHECK(session.engine.dropped() == 0);
}

// An effect gone from a slot, or another family put there: a MOD aimed at it is OFF, in the
// project and in the engine. One aimed elsewhere stays.
void testAnEffectGoneTakesItsModOff() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    rig.settle();
    session.setEffect(0, 0, EffectKind::Drive);
    session.setEffect(0, 1, EffectKind::Drive);
    CHECK(session.setMod(0, 0, square(lane(1, 0), 0.25f)) && session.setMod(0, 1, square(lane(0, 0), 0.5f)));
    CHECK(rig.added(0, lane(1, 0)) == 0.25f && rig.added(0, lane(0, 0)) == 0.5f);
    session.setEffect(0, 1, EffectKind::None); // emptied
    CHECK(blank(project.tracks[0].mods[0]) && sameMod(project.tracks[0].mods[1], square(lane(0, 0), 0.5f)));
    CHECK(rig.onlyAt(0, lane(0, 0), 0.5f));
    session.setEffect(0, 0, EffectKind::Filter); // another family: the same lane is another setting now
    CHECK(blank(project.tracks[0].mods[1]) && rig.onlyAt(0, -1, 0.f));
    // A send's MOD does not mind what the slots hold. Nor does one on another slot.
    session.setEffect(0, 2, EffectKind::Drive);
    CHECK(session.setMod(0, 0, square(20, 0.25f)) && session.setMod(0, 1, square(lane(2, 0), 0.5f)));
    session.setEffect(0, 0, EffectKind::None);
    session.setEffect(0, 1, EffectKind::Crush);
    CHECK(sameMod(project.tracks[0].mods[0], square(20, 0.25f)) && sameMod(project.tracks[0].mods[1], square(lane(2, 0), 0.5f)));
    // An effect the engine refuses (a bus's kind is its own): nothing is touched.
    session.setEffect(kEchoBus, 0, EffectKind::Filter);
    CHECK(sameMod(project.tracks[0].mods[0], square(20, 0.25f)));
    // The master's and a bus's slots are not a track's: a MOD on track 0's slot 1 stays.
    CHECK(session.setMod(0, 0, square(lane(1, 0), 0.25f)));
    session.setEffect(kMasterTrack, 1, EffectKind::Filter);
    session.setEffect(kEchoBus, 0, EffectKind::Ping);
    CHECK(sameMod(project.tracks[0].mods[0], square(lane(1, 0), 0.25f)) && rig.added(0, lane(1, 0)) == 0.25f);
}

// ---- a TYPE change ----------------------------------------------------------------------------

// Every pair of types of every family, every setting: a MOD aimed at a setting follows it to the
// setting of the same NAME on the new type, where that is an amount there; else it goes OFF.
// Nothing is listed: the table says.
void testATypeChangeCarriesTheTarget() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    rig.settle();
    int pairs = 0, carried = 0, lost = 0, moved = 0, becameChoice = 0;
    for (int a = 1; a < kEffectKinds; ++a)
        for (int b = 1; b < kEffectKinds; ++b) {
            const EffectKind from = static_cast<EffectKind>(a), to = static_cast<EffectKind>(b);
            if (a == b || std::strcmp(effectFamily(from), effectFamily(to)) != 0) continue;
            ++pairs;
            for (int setting = 0; setting < effectInfo(from).params; ++setting) {
                if (effectChoices(from, setting) > 0) continue;
                rig.render(64); // (what was sent is taken: the queue does not fill)
                session.setEffect(0, 1, from);
                ModSettings mod = square(lane(1, setting), 0.25f);
                mod.rate = kModRateQuarter;
                mod.steps[5] = 0.5f;
                CHECK(session.setMod(0, 1, mod));
                CHECK(session.setMod(0, 0, square(21, 0.5f))); // (aimed elsewhere: it must not move)
                CHECK(changeEffectType(session, 0, 1, to));
                const int same = settingNamed(to, effectInfo(from).paramNames[setting]);
                const bool keeps = same >= 0 && effectChoices(to, same) == 0;
                if (same >= 0 && !keeps) ++becameChoice;
                if (keeps) {
                    mod.target = lane(1, same);
                    CHECK(sameMod(project.tracks[0].mods[1], mod)); // only the target changed
                    ++carried;
                    if (same != setting) ++moved;
                } else {
                    CHECK(blank(project.tracks[0].mods[1]));
                    ++lost;
                }
                CHECK(sameMod(project.tracks[0].mods[0], square(21, 0.5f)));
            }
        }
    CHECK(pairs > 50 && carried > 50 && lost > 50 && moved > 5);
    if (failures || std::getenv("MOD_PRINT"))
        std::printf("  a TYPE change: %d pairs of types; a MOD carried %d times (%d to another place in the slot), put OFF %d times (%d where the name is a choice on the new type)\n",
                    pairs, carried, moved, lost, becameChoice);

    // One by hand, and the engine with it: REVERB's MIX is its third setting, HALL's its fourth.
    session.setMod(0, 0, ModSettings());
    session.setEffect(0, 1, EffectKind::Reverb);
    const int reverbMix = settingNamed(EffectKind::Reverb, "MIX"), hallMix = settingNamed(EffectKind::Hall, "MIX");
    CHECK(reverbMix >= 0 && hallMix >= 0 && reverbMix != hallMix);
    CHECK(session.setMod(0, 1, square(lane(1, reverbMix), 0.25f)) && rig.onlyAt(0, lane(1, reverbMix), 0.25f));
    CHECK(changeEffectType(session, 0, 1, EffectKind::Hall));
    CHECK(project.tracks[0].mods[1].target == lane(1, hallMix) && rig.onlyAt(0, lane(1, hallMix), 0.25f));
    // Both MODs on the slot: each follows its own setting.
    const int hallDecay = settingNamed(EffectKind::Hall, "DECAY"), plateDecay = settingNamed(EffectKind::Plate, "DECAY"), plateMix = settingNamed(EffectKind::Plate, "MIX");
    CHECK(hallDecay >= 0 && plateDecay >= 0 && plateMix >= 0);
    CHECK(session.setMod(0, 0, square(lane(1, hallDecay), -0.125f)));
    CHECK(changeEffectType(session, 0, 1, EffectKind::Plate));
    CHECK(project.tracks[0].mods[0].target == lane(1, plateDecay) && project.tracks[0].mods[1].target == lane(1, plateMix));
    CHECK(rig.added(0, lane(1, plateDecay)) == -0.125f && rig.added(0, lane(1, plateMix)) == 0.25f);
    // A change that is refused (another family; the same type) leaves the MODs where they are.
    const ModSettings one = project.tracks[0].mods[0], two = project.tracks[0].mods[1];
    CHECK(!changeEffectType(session, 0, 1, EffectKind::Filter) && !changeEffectType(session, 0, 1, EffectKind::Plate));
    CHECK(sameMod(project.tracks[0].mods[0], one) && sameMod(project.tracks[0].mods[1], two));
    // A MOD on another slot's setting, or the same slot of another track, does not move.
    CHECK(session.addTrack(TrackKind::Phazed));
    session.setEffect(1, 1, EffectKind::Plate);
    session.setEffect(0, 2, EffectKind::Plate); // (the same effect in the slot beside it, a MOD on the setting that will move)
    CHECK(plateMix != reverbMix);
    CHECK(session.setMod(1, 0, square(lane(1, plateMix), 0.5f)) && session.setMod(0, 0, square(lane(2, plateMix), 0.5f)));
    CHECK(changeEffectType(session, 0, 1, EffectKind::Reverb));
    CHECK(sameMod(project.tracks[1].mods[0], square(lane(1, plateMix), 0.5f)) && sameMod(project.tracks[0].mods[0], square(lane(2, plateMix), 0.5f)));
    CHECK(project.tracks[0].mods[1].target == lane(1, reverbMix));
    CHECK(session.engine.dropped() == 0);
}

// ---- the song file ----------------------------------------------------------------------------

void testTheSongFile() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    CHECK(session.addTrack(TrackKind::Fm6) && session.addTrack(TrackKind::Phazed));
    project.tracks[1].notes[0][0].notes[0] = NoteEvent{2, 60, 1.f, 0.f, 0.8f};
    session.setEffect(1, 2, EffectKind::Tape);
    const int feedback = settingNamed(EffectKind::Tape, "FEEDBACK"), time = settingNamed(EffectKind::Tape, "TIME");
    CHECK(feedback >= 0 && time >= 0 && effectChoices(EffectKind::Tape, time) > 0);
    const std::string plain = songText(project);
    CHECK(plain.find("\nmod") == std::string::npos); // no MOD on: the song is written as it always was

    // An LFO: one line, in its own track's part, and nothing else is other than it was.
    ModSettings lfo;
    lfo.kind = kModLfo;
    lfo.target = lane(2, feedback);
    lfo.depth = -0.25f;
    lfo.rate = kModRateQuarterTriplet;
    lfo.shape = kModDrift;
    lfo.phase = 3;
    CHECK(session.setMod(1, 1, lfo));
    const std::string withLfo = songText(project);
    char expect[96];
    std::snprintf(expect, sizeof expect, "mod 2 1 %d 0.375 4 6 0 3\n", lane(2, feedback));
    CHECK(withLfo.size() == plain.size() + std::strlen(expect) && count(withLfo, "\nmod") == 1);
    {
        const std::size_t at = withLfo.find(std::string("\n") + expect), second = withLfo.find("\ntrack fm6"), third = withLfo.find("\ntrack phazed");
        CHECK(at != std::string::npos && second != std::string::npos && third != std::string::npos && at > second && at < third);
        std::string without = withLfo;
        if (at != std::string::npos) without.erase(at + 1, std::strlen(expect));
        CHECK(without == plain);
        std::unique_ptr<Project> back(new Project);
        CHECK(parseSong(withLfo, *back) && sameMod(back->tracks[1].mods[1], lfo) && blank(back->tracks[1].mods[0]) && blank(back->tracks[0].mods[1]) && blank(back->tracks[2].mods[1]));
        CHECK(songText(*back) == withLfo);
    }
    // A STEPPER with a row: its second line. Both MODs of a track, and another track's.
    ModSettings stepper;
    stepper.kind = kModStepper;
    stepper.target = 3;
    stepper.depth = 1.f;
    stepper.rate = kModRateStep;
    stepper.length = 5;
    stepper.slew = 0.25f;
    stepper.chance = 0.5f;
    for (int i = 0; i < kModSteps; ++i) stepper.steps[i] = static_cast<float>(i - 8) / 8.f;
    CHECK(session.setMod(1, 0, stepper) && session.setMod(0, 0, square(21, 0.5f)));
    const std::string full = songText(project);
    CHECK(full.find("\nmod 1 2 3 1 1 0 0 0\nmodsteps 1 5 0.25 0.5 -1 -0.875 -0.75 -0.625 -0.5 -0.375 -0.25 -0.125 0 0.125 0.25 0.375 0.5 0.625 0.75 0.875\nmod 2 1 ") != std::string::npos);
    CHECK(full.find("\nmod 1 1 21 0.75 8 4 0 0\n") != std::string::npos && count(full, "\nmod ") == 3 && count(full, "\nmodsteps ") == 1);
    {
        std::unique_ptr<Project> back(new Project);
        CHECK(parseSong(full, *back));
        for (int t = 0; t < 3; ++t)
            for (int m = 0; m < kModsPerTrack; ++m) CHECK(sameMod(back->tracks[t].mods[m], project.tracks[t].mods[m]));
        CHECK(songText(*back) == full);
        // Windows line ends.
        std::string dos;
        for (char ch : full) dos += ch == '\n' ? std::string("\r\n") : std::string(1, ch);
        std::unique_ptr<Project> again(new Project);
        CHECK(parseSong(dos, *again) && songText(*again) == full);
        // The parts undo cuts a song into read alone as they read in the whole.
        std::string head;
        std::vector<std::string> parts;
        CHECK(cutSong(full, head, parts) && parts.size() == 3 && count(parts[1], "\nmod ") == 2 && count(parts[0], "\nmod ") == 1);
    }
    // A row at its usual (16 long, no slew, no chance, every value 0) has no line, STEPPER or LFO;
    // each thing that is not, alone, has one. An LFO keeps a row written for it.
    for (int what = 0; what < 5; ++what) {
        ModSettings m = what == 4 ? lfo : stepper;
        m.length = kModSteps, m.slew = 0.f, m.chance = 0.f;
        for (float& v : m.steps) v = 0.f;
        if (what == 1) m.length = 15;
        if (what == 2) m.slew = 0.5f;
        if (what == 3) m.chance = 0.5f;
        if (what == 4) m.steps[15] = -0.5f;
        CHECK(session.setMod(1, 0, m));
        const std::string text = songText(project);
        CHECK(count(text, "\nmodsteps ") == (what == 0 ? 0u : 1u));
        std::unique_ptr<Project> back(new Project);
        CHECK(parseSong(text, *back) && sameMod(back->tracks[1].mods[0], m) && songText(*back) == text);
    }
    // Every whole percent of DEPTH, every kind, rate, shape and phase: there and back to the bit.
    for (int percent = -100; percent <= 100; ++percent) {
        ModSettings m = square(20, static_cast<float>(percent) / 100.f);
        m.kind = percent % 2 ? kModStepper : kModLfo;
        m.rate = (percent + 100) % kModRates;
        m.shape = (percent + 100) % kModShapes;
        m.phase = (percent + 100) % kModPhases;
        m.steps[(percent + 100) % kModSteps] = static_cast<float>(percent) / 100.f;
        m.slew = static_cast<float>(percent + 100) / 200.f;
        m.chance = static_cast<float>(100 - percent) / 200.f;
        m.length = 1 + (percent + 100) % kModSteps;
        CHECK(session.setMod(2, 1, m));
        std::unique_ptr<Project> back(new Project);
        CHECK(parseSong(songText(project), *back) && sameMod(back->tracks[2].mods[1], m));
    }
    session.setMod(2, 1, ModSettings());
    session.setMod(0, 0, ModSettings());
    session.setMod(1, 0, ModSettings());
    CHECK(songText(project) == withLfo);

    // ---- lines no version wrote ----
    const auto readWith = [&](const std::string& line, Project& back) {
        std::string changed = withLfo;
        const std::size_t at = changed.find(expect);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) changed.replace(at, std::strlen(expect), line + "\n");
        const bool read = parseSong(changed, back);
        CHECK(read && back.tracks[1].notes[0][0].at(60, 2) >= 0 && back.tracks[2].kind == TrackKind::Phazed && back.tracks[1].effects[2].kind == static_cast<int>(EffectKind::Tape)); // the rest of the song is read
        return read;
    };
    const std::string good = std::string(expect, std::strlen(expect) - 1);
    const std::string target = std::to_string(lane(2, feedback));
    {
        std::unique_ptr<Project> back(new Project);
        CHECK(readWith(good, *back) && sameMod(back->tracks[1].mods[1], lfo)); // (the helper puts back what it took)
    }
    // OFF: a KIND or a TARGET nobody knows, a MOD that is not 1 or 2, a line cut short before its TARGET,
    // a target that does not fit (a choice, an empty slot, a setting that is not there, a choice macro).
    for (const std::string& odd : {std::string("mod 2 9 ") + target + " 0.375 4 6 0 3", std::string("mod 2 0 ") + target + " 0.375 4 6 0 3", std::string("mod 2 -1 ") + target + " 0.375",
                                   std::string("mod 2 1 22 0.375 4 6 0 3"), std::string("mod 2 1 23 0.375"), std::string("mod 2 1 31 0.375"), std::string("mod 2 1 34 0.375"), std::string("mod 2 1 99 0.375"),
                                   std::string("mod 2 1 -1 0.375"), std::string("mod 2 1 1e9 0.375"), std::string("mod 2 1"), std::string("mod 2"), std::string("mod"), std::string("mod x 1 20 0.4"),
                                   std::string("mod 3 1 20 0.375"), std::string("mod 0 1 20 0.375"), std::string("mod -1 1 20 0.375"), std::string("mod 1e300 1 20 0.375"), std::string("mud 2 1 20 0.375"),
                                   std::string("mod 2 1 ") + std::to_string(lane(2, time)) + " 0.375", std::string("mod 2 1 ") + std::to_string(lane(0, 0)) + " 0.375",
                                   std::string("modsteps 2 4 0.5 0.5 1 1 1 1")}) {
        std::unique_ptr<Project> back(new Project);
        CHECK(readWith(odd, *back) && blank(back->tracks[1].mods[1]) && blank(back->tracks[1].mods[0]));
        CHECK(songText(*back) == plain); // ...and nothing else of the song is touched by the line
        if (!blank(back->tracks[1].mods[1])) std::printf("  (read as a MOD: %s)\n", odd.c_str());
    }
    // What is missing after the TARGET is as a new MOD's; a number nobody knows is 1 BAR, SINE, PLAY, 0.
    {
        std::unique_ptr<Project> back(new Project);
        CHECK(readWith("mod 2 2 " + target, *back));
        ModSettings m;
        m.kind = kModStepper;
        m.target = lane(2, feedback);
        CHECK(sameMod(back->tracks[1].mods[1], m));
        CHECK(readWith("mod 2 1 " + target + " 0.75", *back));
        m.kind = kModLfo;
        m.depth = 0.5f;
        CHECK(sameMod(back->tracks[1].mods[1], m));
        CHECK(readWith("mod 2 1 " + target + " 0.75 3", *back));
        m.rate = 3;
        CHECK(sameMod(back->tracks[1].mods[1], m));
        CHECK(readWith("mod 2 1 " + target + " 0.75 3 4", *back));
        m.shape = 4;
        CHECK(sameMod(back->tracks[1].mods[1], m));
        CHECK(readWith("mod 2 1 " + target + " 0.75 3 4 0 2", *back));
        m.phase = 2;
        CHECK(sameMod(back->tracks[1].mods[1], m));
        CHECK(readWith("mod 2 1 " + target + " 0.75 99 99 99 99", *back));
        m.rate = kModRateUsual, m.shape = kModSine, m.phase = 0;
        CHECK(sameMod(back->tracks[1].mods[1], m));
        CHECK(readWith("mod 2 1 " + target + " 0.75 -3 -1 -1 -1", *back) && sameMod(back->tracks[1].mods[1], m));
        CHECK(readWith("mod 2 1.9 " + target + ".7 0.75 3.9", *back)); // (a number between two: the one under it)
        m.rate = 3;
        CHECK(sameMod(back->tracks[1].mods[1], m));
        // DEPTH held to its ends.
        CHECK(readWith("mod 2 1 " + target + " 7", *back) && back->tracks[1].mods[1].depth == 1.f);
        CHECK(readWith("mod 2 1 " + target + " -7", *back) && back->tracks[1].mods[1].depth == -1.f);
        CHECK(readWith("mod 2 1 " + target + " 0", *back) && back->tracks[1].mods[1].depth == -1.f);
        CHECK(readWith("mod 2 1 " + target + " 0.5", *back) && back->tracks[1].mods[1].depth == 0.f && back->tracks[1].mods[1].kind == kModLfo);
    }
    // Numbers too big for anything, and not numbers: every field stays what it can be. (How a
    // library reads "1e300" or "nan" differs; that it is held does not.)
    for (const char* big : {"1e300", "-1e300", "nan", "inf", "-inf", "1e39", "4294967296", "-4294967296", "x"}) {
        for (int place = 0; place < 8; ++place) {
            std::string line = "mod";
            const std::string usual[8] = {"2", "1", target, "0.375", "4", "6", "0", "3"};
            for (int i = 0; i < 8; ++i) line += " " + (i == place ? std::string(big) : usual[i]);
            std::unique_ptr<Project> back(new Project);
            CHECK(readWith(line, *back));
            for (const ModSettings& m : back->tracks[1].mods) {
                CHECK(m.kind >= 0 && m.kind < kModKinds && (m.target == kModTargetNone || m.target == lane(2, feedback)) && m.depth >= -1.f && m.depth <= 1.f);
                CHECK(m.rate >= 0 && m.rate < kModRates && m.shape >= 0 && m.shape < kModShapes && m.restart == 0 && m.phase >= 0 && m.phase < kModPhases);
                CHECK(sameMod(m, modHeldFor(back->tracks[1], m))); // as the Session would hold it
            }
        }
        for (int place = 0; place < 20; ++place) {
            std::string line = good + "\nmodsteps";
            for (int i = 0; i < 20; ++i) line += " " + (i == place ? std::string(big) : std::string(i == 0 ? "2" : i == 1 ? "7" : "0.25"));
            std::unique_ptr<Project> back(new Project);
            CHECK(readWith(line, *back));
            const ModSettings& m = back->tracks[1].mods[1];
            CHECK(m.kind == kModLfo && m.length >= 1 && m.length <= kModSteps && m.slew >= 0.f && m.slew <= 1.f && m.chance >= 0.f && m.chance <= 1.f);
            for (float v : m.steps) CHECK(v >= -1.f && v <= 1.f);
            CHECK(sameMod(m, modHeldFor(back->tracks[1], m)));
        }
    }
    {
        // A row held to its ends; a short one keeps the rest at 0; `modsteps` before its `mod`, and
        // a `mod` written before the effect it is aimed at (by hand): read all the same.
        std::unique_ptr<Project> back(new Project);
        CHECK(readWith(good + "\nmodsteps 2 0 9 -9 5 -5 0.5", *back));
        const ModSettings& m = back->tracks[1].mods[1];
        CHECK(m.length == 1 && m.slew == 1.f && m.chance == 0.f && m.steps[0] == 1.f && m.steps[1] == -1.f && m.steps[2] == 0.5f && m.steps[3] == 0.f && m.steps[15] == 0.f);
        CHECK(readWith(good + "\nmodsteps 2 99", *back) && back->tracks[1].mods[1].length == kModSteps && back->tracks[1].mods[1].slew == 0.f);
        CHECK(readWith("modsteps 2 4 0.5 0.25 1 -1\n" + good, *back));
        ModSettings both = lfo;
        both.length = 4, both.slew = 0.5f, both.chance = 0.25f, both.steps[0] = 1.f, both.steps[1] = -1.f;
        CHECK(sameMod(back->tracks[1].mods[1], both));
        std::string early = plain;
        const std::size_t fx = early.find("\nfx 3 ");
        CHECK(fx != std::string::npos);
        if (fx != std::string::npos) early.insert(fx + 1, expect);
        CHECK(parseSong(early, *back) && sameMod(back->tracks[1].mods[1], lfo));
        // Under a track line this version does not know, a MOD is nobody's (as every line there).
        std::string unknown = withLfo;
        const std::size_t q = unknown.find("\ntrack fm6");
        if (q != std::string::npos) unknown.replace(q, 10, "\ntrack zzzzz");
        CHECK(parseSong(unknown, *back) && back->tracks[1].kind == TrackKind::Phazed && blank(back->tracks[0].mods[1]) && blank(back->tracks[1].mods[1]));
    }
    // A macro on a drum track or a sampler; a choice macro; an amount macro on a synth.
    {
        std::unique_ptr<Project> back(new Project);
        std::string hand = plain;
        const std::size_t drums = hand.find("\ntrack drums"), end = drums == std::string::npos ? drums : hand.find('\n', drums + 1);
        CHECK(end != std::string::npos);
        if (end != std::string::npos) hand.insert(end + 1, "mod 1 1 2 0.75\nmod 2 1 20 0.75\n");
        CHECK(parseSong(hand, *back) && blank(back->tracks[0].mods[0]) && back->tracks[0].mods[1].target == 20);
        // A third MOD, or a third MOD's row, at the end of the drum track's part (what lies after a track's
        // two MODs is its lanes' levels, read before this): not read, and nothing of the track is other than it was.
        for (const char* third : {"mod 3 2 20 0.75 1 1 0 1\n", "modsteps 3 4 0.3 0.7 1 1 1 1 1 1 1 1 1 1\n"}) {
            hand = plain;
            const std::size_t next = hand.find("\ntrack fm6");
            CHECK(next != std::string::npos);
            if (next != std::string::npos) hand.insert(next + 1, third);
            CHECK(parseSong(hand, *back) && songText(*back) == plain);
        }
        CHECK(readWith("mod 1 1 7 0.75\nmod 2 1 6 0.75", *back) && back->tracks[1].mods[0].target == 7 && back->tracks[1].mods[1].target == 6); // FM-6: every macro an amount
        hand = plain; // Phazed's OCTAVE is a choice, its VIBRATO not
        const std::size_t phazed = hand.find("\ntrack phazed\n");
        CHECK(phazed != std::string::npos);
        if (phazed != std::string::npos) hand.insert(phazed + 14, "mod 1 1 7 0.75\nmod 2 1 5 0.75\n");
        CHECK(parseSong(hand, *back) && blank(back->tracks[2].mods[0]) && back->tracks[2].mods[1].target == 5);
    }
}

// ---- the engine is told ------------------------------------------------------------------------

// A song opened has its MODs running; the next one, with none, has none (nothing of the last is left).
void testASongOpenedSendsItsMods() {
    std::string text;
    {
        Rig rig;
        Session& session = rig.session();
        CHECK(session.addTrack(TrackKind::Phazed));
        session.setEffect(1, 0, EffectKind::Drive);
        CHECK(session.setMod(0, 1, square(21, 0.25f)) && session.setMod(1, 0, square(2, -0.5f)) && session.setMod(1, 1, square(lane(0, 0), 0.125f)));
        text = songText(rig.project());
    }
    Rig rig;
    rig.settle();
    std::unique_ptr<Project> song(new Project);
    CHECK(parseSong(text, *song));
    rig.project() = *song;
    rig.session().applyProject();
    CHECK(rig.onlyAt(0, 21, 0.25f));
    CHECK(rig.added(1, 2) == -0.5f && rig.added(1, lane(0, 0)) == 0.125f);
    // Another song, no MOD in it.
    std::unique_ptr<Project> empty(new Project);
    Rig other;
    CHECK(other.session().addTrack(TrackKind::Phazed));
    CHECK(parseSong(songText(other.project()), *empty));
    rig.project() = *empty;
    rig.session().applyProject();
    CHECK(rig.onlyAt(0, -1, 0.f) && rig.onlyAt(1, -1, 0.f));
    CHECK(rig.session().engine.dropped() == 0);
}

// A MOD lost to a full queue is mended by the song sent again.
void testAFullQueueIsMended() {
    Rig rig;
    Session& session = rig.session();
    rig.settle();
    Command c;
    c.type = Command::SetSend;
    c.track = 0;
    c.a = 0;
    c.value = 0.f;
    int sent = 0;
    while (rig.engine->send(c) && sent < 100000) ++sent; // (nothing renders: the queue fills)
    CHECK(sent < 100000);
    const unsigned lost = rig.engine->dropped();
    CHECK(session.setMod(0, 0, square(21, 0.25f)) && rig.engine->dropped() > lost); // the project has it; the engine was not told
    rig.render(2000);
    rig.frames(3); // the Session sees the loss and sends the song again
    CHECK(rig.onlyAt(0, 21, 0.25f));
}

// Another instrument on a track: a MOD on a macro goes with the macros, one on a send or an
// effect stays, and the engine holds what the project does. A track deleted: the ones after it
// move up with their MODs.
void testAnotherInstrumentAndATrackDeleted() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    CHECK(session.addTrack(TrackKind::Phazed) && session.addTrack(TrackKind::Fm6));
    rig.settle();
    session.setEffect(1, 0, EffectKind::Drive);
    CHECK(session.setMod(1, 0, square(2, 0.5f)) && session.setMod(1, 1, square(lane(0, 0), 0.25f)));
    CHECK(session.setMod(2, 0, square(20, 0.125f)) && session.setMod(2, 1, square(3, -0.25f)));
    CHECK(rig.added(1, 2) == 0.5f);
    CHECK(session.setTrackKind(1, TrackKind::Jx3p));
    CHECK(blank(project.tracks[1].mods[0]) && sameMod(project.tracks[1].mods[1], square(lane(0, 0), 0.25f)));
    CHECK(rig.onlyAt(1, lane(0, 0), 0.25f)); // (JAX-EP has a macro 2 as well: the MOD is not on it)
    CHECK(rig.added(2, 20) == 0.125f && rig.added(2, 3) == -0.25f);
    // To a drum track: still the effect's.
    CHECK(session.setMod(1, 0, square(5, 0.5f)) && session.setTrackKind(1, TrackKind::Drums));
    CHECK(blank(project.tracks[1].mods[0]) && rig.onlyAt(1, lane(0, 0), 0.25f));
    // Track 1 deleted: track 2 is track 1 now.
    CHECK(session.removeTrack(1));
    CHECK(sameMod(project.tracks[1].mods[0], square(20, 0.125f)) && sameMod(project.tracks[1].mods[1], square(3, -0.25f)) && blank(project.tracks[2].mods[0]) && blank(project.tracks[2].mods[1]));
    {
        const std::vector<float> all = rig.addedAll(1);
        CHECK(all[20] == 0.125f && all[3] == -0.25f && all[static_cast<std::size_t>(lane(0, 0))] == 0.f);
    }
    CHECK(rig.onlyAt(2, -1, 0.f));
    CHECK(session.engine.dropped() == 0);
}

// ---- undo -------------------------------------------------------------------------------------

void testUndo() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.settle();
    session.setEffect(0, 1, EffectKind::Reverb);
    rig.settle();
    const std::string start = songText(project);
    const int steps = session.undoSteps();

    // A MOD set is a step by itself: no screen says so.
    CHECK(session.setMod(0, 0, square(21, 0.25f)));
    rig.settle();
    CHECK(session.undoSteps() == steps + 1);
    const std::string one = songText(project);
    // Its depth turned; then aimed elsewhere; then a row; then the other MOD; then OFF.
    CHECK(session.setMod(0, 0, square(21, -0.5f)));
    rig.settle();
    CHECK(session.setMod(0, 0, square(20, -0.5f)));
    rig.settle();
    ModSettings row = square(20, -0.5f);
    row.kind = kModStepper;
    row.steps[0] = 0.5f;
    row.length = 3;
    CHECK(session.setMod(0, 0, row));
    rig.settle();
    const int mix = settingNamed(EffectKind::Reverb, "MIX");
    CHECK(session.setMod(0, 1, square(lane(1, mix), 0.125f)));
    rig.settle();
    CHECK(session.setMod(0, 0, ModSettings()));
    rig.settle();
    CHECK(session.undoSteps() == steps + 6);
    const std::string end = songText(project);
    CHECK(rig.onlyAt(0, lane(1, mix), 0.125f));

    const unsigned images = session.imagesSent();
    CHECK(session.undo() && sameMod(project.tracks[0].mods[0], row) && rig.added(0, 20) == -0.25f && rig.added(0, lane(1, mix)) == 0.125f);
    CHECK(session.undo() && blank(project.tracks[0].mods[1]) && rig.onlyAt(0, 20, -0.25f));
    CHECK(session.undo() && sameMod(project.tracks[0].mods[0], square(20, -0.5f)) && rig.onlyAt(0, 20, -0.5f));
    CHECK(session.undo() && sameMod(project.tracks[0].mods[0], square(21, -0.5f)) && rig.onlyAt(0, 21, -0.5f));
    CHECK(session.undo() && songText(project) == one && rig.onlyAt(0, 21, 0.25f));
    CHECK(session.undo() && songText(project) == start && blank(project.tracks[0].mods[0]) && rig.onlyAt(0, -1, 0.f));
    CHECK(session.undoSteps() == steps && session.imagesSent() == images); // none of it is a pattern: no track sent whole
    rig.frames(60);
    CHECK(session.undoSteps() == steps); // putting it back is not itself an edit
    for (int i = 0; i < 6; ++i) CHECK(session.redo());
    CHECK(songText(project) == end && rig.onlyAt(0, lane(1, mix), 0.125f) && session.imagesSent() == images);
    rig.frames(60);
    CHECK(session.undoSteps() == steps + 6 && session.redoSteps() == 0);

    // A TYPE change taken back: the old type, and the MOD on the old type's setting again.
    session.setMod(0, 1, ModSettings());
    CHECK(session.setMod(0, 0, square(lane(1, mix), 0.25f)));
    rig.settle();
    const int typeSteps = session.undoSteps();
    const int hallMix = settingNamed(EffectKind::Hall, "MIX");
    CHECK(changeEffectType(session, 0, 1, EffectKind::Hall) && project.tracks[0].mods[0].target == lane(1, hallMix) && hallMix != mix);
    rig.settle();
    CHECK(session.undoSteps() == typeSteps + 1 && rig.onlyAt(0, lane(1, hallMix), 0.25f));
    CHECK(session.undo() && project.tracks[0].effects[1].kind == static_cast<int>(EffectKind::Reverb) && sameMod(project.tracks[0].mods[0], square(lane(1, mix), 0.25f)));
    CHECK(rig.onlyAt(0, lane(1, mix), 0.25f));
    CHECK(session.redo() && project.tracks[0].mods[0].target == lane(1, hallMix) && rig.onlyAt(0, lane(1, hallMix), 0.25f));
    // An effect deleted took its MOD OFF: undo brings both back.
    rig.settle();
    session.setEffect(0, 1, EffectKind::None);
    rig.settle();
    CHECK(blank(project.tracks[0].mods[0]) && rig.onlyAt(0, -1, 0.f));
    CHECK(session.undo() && project.tracks[0].effects[1].kind == static_cast<int>(EffectKind::Hall) && rig.onlyAt(0, lane(1, hallMix), 0.25f));
    CHECK(session.engine.dropped() == 0);
}

// A value the text rounds (a third) is left to the bit by an undo of something else on its track;
// and a value that did change is still put back.
void testUndoLeavesWhatTheTextRounds() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    rig.settle();
    const float third = 1.f / 3.f;
    ModSettings m = square(20, third);
    m.kind = kModStepper;
    m.slew = third;
    m.chance = third;
    m.steps[4] = -third;
    CHECK(session.setMod(0, 0, m) && session.setMod(0, 1, square(21, 0.5f)));
    rig.settle();
    session.setSend(0, 1, 0.75f); // something else on the same track: the track's text differs, so it is read back
    rig.settle();
    CHECK(session.undo() && project.tracks[0].send[1] == 0.f);
    CHECK(sameMod(project.tracks[0].mods[0], m)); // to the last bit: not read back from the text
    {
        std::unique_ptr<Project> back(new Project);
        CHECK(parseSong(songText(project), *back) && !sameMod(back->tracks[0].mods[0], m) && std::fabs(back->tracks[0].mods[0].depth - third) < 1e-5f); // (the text does round it: the check can tell)
    }
    m.depth = 0.9f;
    m.steps[4] = 0.9f;
    CHECK(session.setMod(0, 0, m));
    rig.settle();
    CHECK(session.undo() && std::fabs(project.tracks[0].mods[0].depth - third) < 1e-5f && std::fabs(project.tracks[0].mods[0].steps[4] + third) < 1e-5f);
    CHECK(sameMod(project.tracks[0].mods[1], square(21, 0.5f)));
    CHECK(rig.added(0, 21) == 0.5f);
}

// ---- batch 7: lanes 22 and 23 are MOD 1's and MOD 2's DEPTH ------------------------------------

// Written in a scene, a DEPTH lane is the MOD's depth there while the song plays (the engine's, and
// what the screens show: Session::modDepthShown); a scene with nothing written is the MOD's own
// DEPTH; the song file keeps the lanes as any lane's `auto` lines; WRITE records a DEPTH turned;
// undo takes the turn and what it wrote back together; NEXT SCENE copies the lane with the scene.
void testTheDepthLanes() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    CHECK(session.addTrack(TrackKind::Phazed));
    const int t = project.trackCount() - 1;
    session.setSend(t, 0, 0.1f);
    CHECK(session.setMod(t, 0, square(kModFirstSend, 0.2f)));
    rig.settle();
    const int l0 = modDepthLane(0);
    // Scene 1, every step: +60%.
    for (int i = 0; i < kSteps; ++i) session.setAutomation(t, 0, l0, i, modDepthToLane(0.6f));
    rig.settle();
    CHECK(session.modDepthShown(t, 0) == 0.2f); // stopped: its own
    session.togglePlay(false);
    rig.frames(1);
    rig.render(4000);
    CHECK(std::fabs(rig.engine->modShown(t, 0) - 0.6f) < 1e-6f && std::fabs(session.modDepthShown(t, 0) - 0.6f) < 1e-6f);
    CHECK(session.modDepthShown(t, 1) == 0.f); // MOD 2 (OFF): its lane not written, nothing
    session.togglePlay(false);
    rig.frames(1);
    rig.render(1000);
    CHECK(session.modDepthShown(t, 0) == 0.2f && rig.engine->laneCentre(t, l0) == modDepthToLane(0.2f));

    // The song file: an `auto` line on lane 22, read back the same; the MOD's line as it was.
    const std::string text = songText(project);
    CHECK(text.find("auto 1 22 0 0.8 1 0.8") != std::string::npos);
    {
        std::unique_ptr<Project> back(new Project);
        CHECK(parseSong(text, *back) && songText(*back) == text);
        CHECK(back->tracks[t].automation[0][l0][5] == 0.8f && back->tracks[t].automation[0][modDepthLane(1)][5] == kAutoNone);
    }

    // WRITE on and playing: a DEPTH turned is written into its lane where the song is. Undo takes the
    // turn and what it wrote back in one step.
    for (int i = 0; i < kSteps; ++i) session.setAutomation(t, 0, l0, i, -1.f);
    rig.settle();
    const std::string before = songText(project);
    const int steps = session.undoSteps();
    session.setWriting(true);
    session.togglePlay(false);
    rig.frames(1);
    rig.render(6000 * 2 + 100); // on the third step
    rig.frames(1);
    const int step = rig.engine->playStep();
    CHECK(step == 2);
    ModSettings deeper = square(kModFirstSend, -0.4f);
    CHECK(session.setMod(t, 0, deeper));
    CHECK(project.tracks[t].automation[0][l0][step] == modDepthToLane(-0.4f));
    int written = 0;
    for (int i = 0; i < kAutoSteps; ++i) written += project.tracks[t].automation[0][l0][i] >= 0.f;
    CHECK(written == 1);
    // A MOD made (OFF to on) is not a turn of DEPTH: nothing written on MOD 2's lane.
    CHECK(session.setMod(t, 1, square(kModFirstSend + 1, 0.3f)));
    CHECK(!session.hasAutomation(t, 0, modDepthLane(1)));
    CHECK(session.setMod(t, 1, ModSettings()));
    session.togglePlay(false);
    session.setWriting(false);
    rig.settle();
    CHECK(session.undoSteps() > steps);
    while (session.undoSteps() > steps) CHECK(session.undo());
    rig.settle();
    CHECK(songText(project) == before && project.tracks[t].mods[0].depth == 0.2f && !session.hasAutomation(t, 0, l0));
    // WRITE off: a turn writes nothing.
    session.togglePlay(false);
    rig.frames(1);
    rig.render(3000);
    CHECK(session.setMod(t, 0, deeper));
    CHECK(!session.hasAutomation(t, 0, l0));
    session.togglePlay(false);
    CHECK(session.setMod(t, 0, square(kModFirstSend, 0.2f)));
    rig.settle();

    // NEXT SCENE: the copy has the lane, and plays it. (A note in the scene: a scene with automation
    // alone is an empty one.)
    project.tracks[t].notes[0][0].notes[0] = NoteEvent{2, 60, 1.f, 0.f, 0.8f};
    session.sendNotePattern(t, 0);
    for (int i = 0; i < kSteps; ++i) session.setAutomation(t, 0, l0, i, modDepthToLane(-1.f));
    rig.settle();
    project.scene = 0;
    CHECK(session.nextScene() == 1);
    rig.settle();
    CHECK(project.scene == 1 && project.sceneCount() == 2 && project.tracks[t].automation[1][l0][7] == 0.f && session.hasAutomation(t, 1, l0));
    session.togglePlay(false); // from the scene worked on: the copy
    rig.frames(1);
    rig.render(4000);
    CHECK(rig.engine->playScene() == 1 && rig.engine->modShown(t, 0) == -1.f && session.modDepthShown(t, 0) == -1.f);
    session.togglePlay(false);
    rig.settle();
    CHECK(session.undo() && project.sceneCount() == 1 && !session.hasAutomation(t, 1, l0) && session.hasAutomation(t, 0, l0));
}

} // namespace

int main() {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    testTheSessionHoldsAMod();
    testAnEffectGoneTakesItsModOff();
    testATypeChangeCarriesTheTarget();
    testTheSongFile();
    testASongOpenedSendsItsMods();
    testAFullQueueIsMended();
    testAnotherInstrumentAndATrackDeleted();
    testUndo();
    testUndoLeavesWhatTheTextRounds();
    testTheDepthLanes();
    if (failures == 0) std::printf("modapp tests passed\n");
    return failures == 0 ? 0 : 1;
}
