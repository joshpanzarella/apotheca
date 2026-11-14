#include "song_file.hpp"
#include "instruments.hpp"
#include "samples.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>

#include "synth_spec.hpp"

namespace apotheca {

namespace {

// A number read from a file, held to its limits BEFORE it is made an int or a float: a double too
// big for one ("repeats 1e300") made into one is undefined, and the desk and the handheld read it
// differently (one 1, the other 16). In range, the same as making it one and then holding it.
int wholeIn(double v, int lo, int hi) { return !(v >= lo) ? lo : (v > hi ? hi : static_cast<int>(v)); } // (not a number: lo)
float fractionIn(double v, float lo, float hi) { return !(v >= lo) ? lo : (v > hi ? hi : static_cast<float>(v)); }
std::uint32_t seedIn(double v) { return !(v >= 0.0) ? 0u : (v > 4294967295.0 ? 4294967295u : static_cast<std::uint32_t>(v)); }

void put(std::string& out, const char* format, double a) {
    char text[48];
    std::snprintf(text, sizeof text, format, a);
    out += text;
}


// Has the cell bars past its end with something in them? (A cell made shorter hides its last bars.)
bool hidesBars(const TrackData& track, int scene) {
    for (int bar = track.bars[scene]; bar < kMaxBars; ++bar) {
        if (track.kind == TrackKind::Drums && !track.drums[scene][bar].empty()) return true;
        if (isSynth(track.kind) && !track.notes[scene][bar].empty()) return true;
    }
    return false;
}

// How long a note or a chord may be, from its step: to the end of the cell, over the bar lines.
// One in a bar the cell hides may run to where the longest cell would end.
float longestFrom(const TrackData& track, int scene, int bar, int step) {
    const int bars = bar < track.bars[scene] ? track.bars[scene] : kMaxBars;
    return static_cast<float>((bars - bar) * kSteps - step);
}

void putEffects(std::string& out, const EffectData* effects, int slots) {
    for (int slot = 0; slot < slots; ++slot) {
        const EffectKind kind = static_cast<EffectKind>(effects[slot].kind);
        if (kind == EffectKind::None) continue;
        put(out, "fx %.0f ", slot + 1);
        out += effectInfo(kind).word; // its own word, not its screen name: the name may change, the word never
        for (int i = 0; i < effectInfo(kind).params; ++i) put(out, " %.6g", effects[slot].param[i]);
        out += "\n";
    }
}

// One track of a song file: from its "track" line to the next one's.
void putTrack(std::string& out, const TrackData& track) {
    out += std::string("track ") + instrument(track.kind)->word + "\n";
    if (track.name[0]) out += std::string("name ") + track.name + "\n";
    // auto SCENE LANE then pairs INDEX VALUE: the steps of a lane of automation that have something on them.
    for (int scene = 0; scene < kMaxScenes; ++scene)
        for (int lane = 0; lane < kAutoLanes; ++lane) {
            std::string line;
            for (int i = 0; i < kAutoSteps; ++i) {
                const float v = track.automation[scene][lane][i];
                if (v < 0.f) continue;
                put(line, " %.0f", i);
                put(line, " %.4g", v);
            }
            if (line.empty()) continue;
            put(out, "auto %.0f", scene + 1);
            put(out, " %.0f", lane);
            out += line + "\n";
        }
    if (track.muted) out += "muted\n";
    putEffects(out, track.effects, kTrackSlots);
    // send BUS AMOUNT: how much of the track goes to the ECHO (1) or the SPACE (2) bus. Left out when none does.
    for (int bus = 0; bus < kBuses; ++bus) {
        if (!(track.send[bus] > 0.f)) continue;
        put(out, "send %.0f", bus + 1);
        put(out, " %.6g\n", track.send[bus]);
    }
    // glidefrom N: a synth track's GLIDE FROM (1 PAIRED, 2 NEAREST, 3 RANDOM). Left out at TOP.
    if (isSynth(track.kind) && track.glideFrom) put(out, "glidefrom %.0f\n", track.glideFrom);
    if (track.kind == TrackKind::Drums) {
        out += "lanes";
        for (float level : track.laneLevel) put(out, " %.6g", level);
        out += "\n";
        // sample LANE FIT NAME: a recording on a lane, stretched to FIT steps (0: as recorded)
        for (int lane = 0; lane < kDrumLanes; ++lane) {
            if (!track.laneSample[lane][0]) continue;
            put(out, "sample %.0f", lane + 1);
            put(out, " %.0f ", track.laneFit[lane]);
            out += track.laneSample[lane];
            out += "\n";
        }
        // oneslice LANE...: the lanes whose hits play one slice of their sound and stop
        bool anySliced = false;
        for (bool on : track.laneOneSlice) anySliced = anySliced || on;
        if (anySliced) {
            out += "oneslice";
            for (int lane = 0; lane < kDrumLanes; ++lane)
                if (track.laneOneSlice[lane]) put(out, " %.0f", lane + 1);
            out += "\n";
        }
        // scene N, then "bars B" if the cell is longer than one, then each
        // bar's hits; "bar K" comes before those of the second bar on.
        // Then the bars the cell hides (made shorter: they come back as they were if it is made
        // longer again), each after its "bar K" past the cell's end. A program from before
        // reads no line for a bar the cell does not have, so it skips them.
        for (int scene = 0; scene < kMaxScenes; ++scene) {
            if (track.sceneEmpty(scene) && track.bars[scene] == 1 && !hidesBars(track, scene)) continue;
            put(out, "scene %.0f\n", scene + 1);
            if (track.bars[scene] > 1) put(out, "bars %.0f\n", track.bars[scene]);
            for (int bar = 0; bar < kMaxBars; ++bar) {
                const DrumPattern& p = track.drums[scene][bar];
                if (p.empty()) continue;
                if (bar > 0) put(out, "bar %.0f\n", bar + 1);
                out += "seeds";
                for (std::uint32_t seed : p.diceSeed) put(out, " %.0f", seed);
                out += "\n";
                for (int lane = 0; lane < kDrumLanes; ++lane) {
                    for (int step = 0; step < kSteps; ++step) {
                        if (p.level[lane][step] <= 0.f) continue;
                        put(out, "hit %.0f", lane + 1);
                        put(out, " %.0f", step + 1);
                        put(out, " %.6g", p.level[lane][step]);
                        put(out, " %.6g", p.nudge[lane][step]);
                        put(out, " %.6g", p.chance[lane][step]);
                        out += p.fromDice[lane][step] ? " 1" : " 0";
                        if (p.start[lane][step] > 0.f) put(out, " %.6g", p.start[lane][step]); // where in the sound it begins
                        out += "\n";
                        // stepfx LANE STEP PITCH RATCHET BACKWARDS CUT [THROW]: the hit's step effects, if it has any.
                        // THROW (1 ECHO, 2 SPACE, 3 both) is left off when it is 0, as every older song has it.
                        // ("fx" is taken: it is an effect in a slot.)
                        if (const StepFx& fx = p.fx[lane][step]; fx.any()) {
                            put(out, "stepfx %.0f", lane + 1);
                            put(out, " %.0f", step + 1);
                            put(out, " %.0f", fx.pitch);
                            put(out, " %.0f", fx.hits());
                            put(out, " %.0f", fx.reverse ? 1 : 0);
                            put(out, " %.0f", fx.cut);
                            if (fx.throwTo) put(out, " %.0f", fx.throwTo);
                            out += "\n";
                        }
                    }
                }
            }
        }
    } else {
        if (track.kind == TrackKind::Sampler) {
            // sound NAME: its recording. sampler: root tune start attack release loop level.
            if (track.sample[0]) out += std::string("sound ") + track.sample + "\n";
            out += "sampler";
            for (int v : track.sampler) put(out, " %.0f", v);
            out += "\n";
        } else {
            put(out, "preset %.0f\n", track.preset + 1);
            out += "macros";
            for (float m : track.macro) put(out, " %.6g", m);
            out += "\nsettings";
            const int* values = specFor(track.kind)->values(const_cast<TrackData&>(track));
            for (int i = 0; i < settingCount(track.kind); ++i) put(out, " %.0f", values[i]);
            out += "\n";
        }
        for (int scene = 0; scene < kMaxScenes; ++scene) { // (and the hidden bars, as a drum track's)
            if (track.sceneEmpty(scene) && track.bars[scene] == 1 && !hidesBars(track, scene)) continue;
            put(out, "scene %.0f\n", scene + 1);
            if (track.bars[scene] > 1) put(out, "bars %.0f\n", track.bars[scene]);
            for (int bar = 0; bar < kMaxBars; ++bar) {
                const NotePattern& p = track.notes[scene][bar];
                if (p.empty()) continue;
                if (bar > 0) put(out, "bar %.0f\n", bar + 1);
                for (const NoteEvent& n : p.notes) {
                    if (n.velocity <= 0.f) continue;
                    put(out, "note %.0f", n.step + 1);
                    put(out, " %.0f", n.pitch);
                    put(out, " %.6g", n.length);
                    put(out, " %.6g", n.velocity);
                    // early or late, in steps; and then its THROW (1 ECHO, 2 SPACE, 3 both), which
                    // has the nudge written before it even at 0, so that it is the sixth number
                    if (n.nudge != 0.f || n.throwTo) put(out, " %.6g", n.nudge);
                    if (n.throwTo) put(out, " %.0f", n.throwTo);
                    out += "\n";
                    // notefx STEP KEY RATCHET GLIDE: what the note does besides play, if anything
                    if (n.ratchet >= 2 || n.glide) {
                        put(out, "notefx %.0f", n.step + 1);
                        put(out, " %.0f", n.pitch);
                        put(out, " %.0f", n.ratchet >= 2 ? n.ratchet : 1);
                        put(out, " %.0f\n", glideKind(n.glide)); // (1, "on" in an old song, is half a step)
                    }
                }
                // chord STEP LENGTH LEVEL INVERSION ROOT NAME then its notes above the root
                for (const ChordEvent& c : p.chords) {
                    if (c.velocity <= 0.f) continue;
                    put(out, "chord %.0f", c.step + 1);
                    put(out, " %.6g", c.length);
                    put(out, " %.6g", c.velocity);
                    put(out, " %.0f", c.inversion);
                    put(out, " %.0f ", c.root);
                    out += c.name[0] ? c.name : "?";
                    for (int i = 0; i < c.count; ++i) put(out, " %.0f", c.intervals[i]);
                    if (c.nudge != 0.f) put(out, " nudge %.6g", c.nudge);
                    out += "\n";
                    if (c.ratchet >= 2 || c.throwTo) { // chordfx STEP RATCHET [THROW]: THROW is left off at 0
                        put(out, "chordfx %.0f", c.step + 1);
                        put(out, " %.0f", c.ratchet >= 2 ? c.ratchet : 1);
                        if (c.throwTo) put(out, " %.0f", c.throwTo);
                        out += "\n";
                    }
                    // chordplay STEP STRUM ORDER TIME LEVEL LENGTHS: the chord effects (chord_rule.hpp).
                    // A line of its own, left out when none is set: a chord that has none is written
                    // as it always was, and a program from before them skips the line whole.
                    if (c.play.strum || c.play.order || c.play.time || c.play.level || c.play.lengths) {
                        put(out, "chordplay %.0f", c.step + 1);
                        put(out, " %.0f", c.play.strum);
                        put(out, " %.0f", c.play.order);
                        put(out, " %.0f", c.play.time);
                        put(out, " %.0f", c.play.level);
                        put(out, " %.0f\n", c.play.lengths);
                    }
                }
            }
        }
    }
}

} // namespace

std::string trackText(const TrackData& track) {
    std::string out;
    putTrack(out, track);
    return out;
}

std::string songText(const Project& project) {
    std::string out = "apotheca-song 1\n";
    put(out, "tempo %.6g\n", project.tempo);
    out += "repeats";
    for (int r : project.repeats) put(out, " %.0f", r);
    // length: how many steps each scene lasts, where one is cut short (0: all of them). Only if any is.
    bool cut = false;
    for (int n : project.steps) cut = cut || n > 0;
    if (cut) {
        out += "\nlength";
        for (int n : project.steps) put(out, " %.0f", n);
    }
    out += "\n";
    if (project.chordPack[0]) out += std::string("pack ") + project.chordPack + "\n";
    // fx SLOT NAME and its settings. Before any track: the master's.
    putEffects(out, project.masterEffects, kMasterSlots);
    // busfx BUS NAME and its settings: what the ECHO (1) and the SPACE (2) bus hold. Always both, an
    // empty one as "none": a song with no such line is an old song, and gets the usual two.
    for (int bus = 0; bus < kBuses; ++bus) {
        const EffectKind kind = static_cast<EffectKind>(project.busEffects[bus].kind);
        put(out, "busfx %.0f ", bus + 1);
        out += kind == EffectKind::None ? "none" : effectInfo(kind).word;
        for (int i = 0; i < effectInfo(kind).params; ++i) put(out, " %.6g", project.busEffects[bus].param[i]);
        out += "\n";
    }

    for (int t = 0; t < project.trackCount(); ++t) putTrack(out, project.tracks[t]);
    return out;
}

bool parseSong(const std::string& text, Project& project) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 13, "apotheca-song") != 0) return false;

    std::unique_ptr<Project> song(new Project);
    song->tracks[0] = TrackData(); // no tracks until the file names them
    int count = 0, scene = -1, bar = 0;
    int oldSteps[kMaxScenes]; // "steps": to a bar, as songs were kept before a scene had a length
    for (int& n : oldSteps) n = kSteps;
    TrackData* track = nullptr;

    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); // a file kept with Windows' line ends: a name must not end in it
        std::istringstream in(line);
        std::string word;
        if (!(in >> word)) continue;

        if (word == "tempo") {
            double tempo = 120;
            if (in >> tempo) song->tempo = fractionIn(tempo, kMinTempo, kMaxTempo);
        } else if (word == "length") {
            double n;
            for (int i = 0; i < kMaxScenes && (in >> n); ++i) song->steps[i] = wholeIn(n, 0, kMaxBars * kSteps);
        } else if (word == "steps") {
            // As songs were kept before: steps to a BAR, 1..16. Turned into a length once the cells' bars are known (below).
            double n;
            for (int i = 0; i < kMaxScenes && (in >> n); ++i) oldSteps[i] = wholeIn(n, 1, kSteps);
        } else if (word == "repeats") {
            double r;
            for (int i = 0; i < kMaxScenes && (in >> r); ++i) song->repeats[i] = wholeIn(r, 1, kMaxRepeats);
        } else if (word == "pack") {
            std::string name;
            std::getline(in >> std::ws, name);
            std::snprintf(song->chordPack, sizeof song->chordPack, "%s", name.c_str());
        } else if (word == "fx") {
            double slot, value;
            std::string name;
            if (!(in >> slot >> name) || slot < 1 || slot > (track ? kTrackSlots : kMasterSlots)) continue;
            EffectData& data = (track ? track->effects : song->masterEffects)[static_cast<int>(slot) - 1];
            const EffectKind kind = effectFromWord(name.c_str());
            if (kind == EffectKind::None) continue; // an effect this version does not have: the slot is left as it was
            const EffectInfo& info = effectInfo(kind);
            data.kind = static_cast<int>(kind);
            for (int i = 0; i < kEffectParams; ++i) data.param[i] = info.defaults[i];
            for (int i = 0; i < info.params && (in >> value); ++i) data.param[i] = fractionIn(value, 0.f, 1.f);
        } else if (word == "busfx") {
            double bus, value;
            std::string name;
            if (!(in >> bus >> name) || bus < 1 || bus > kBuses) continue;
            const int b = static_cast<int>(bus) - 1;
            const EffectKind kind = effectFromWord(name.c_str());
            // An effect this version does not have, or one that is not for this bus: the bus keeps what it had.
            if (kind == EffectKind::None ? name != "none" : effectBus(kind) != b) continue;
            song->setBusEffect(b, kind);
            for (int i = 0; i < effectInfo(kind).params && (in >> value); ++i) song->busEffects[b].param[i] = fractionIn(value, 0.f, 1.f);
        } else if (word == "track") {
            std::string kind;
            in >> kind;
            const TrackKind k = trackKindFromWord(kind.c_str());
            track = nullptr;
            scene = -1;
            if (k == TrackKind::None || count >= kNumTracks) continue; // an instrument this version does not have, or a thirteenth track
            track = &song->tracks[count++];
            *track = TrackData();
            track->kind = k;
            if (const SynthSpec* spec = specFor(k)) spec->loadPreset(*track, 0);
        } else if (!track) {
            continue; // nothing else means anything outside a track
        } else if (word == "name") {
            std::string rest;
            std::getline(in, rest);
            std::string safe;
            for (char ch : rest) {
                const bool fine = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '_';
                if (fine && !(ch == ' ' && safe.empty()) && safe.size() < sizeof track->name - 1) safe += ch;
            }
            while (!safe.empty() && safe.back() == ' ') safe.pop_back();
            std::snprintf(track->name, sizeof track->name, "%s", safe.c_str());
        } else if (word == "auto") {
            double sceneNumber = 0, lane = -1, index = 0, value = 0;
            if (!(in >> sceneNumber >> lane) || sceneNumber < 1 || sceneNumber > kMaxScenes || lane < 0 || lane >= kAutoLanes) continue;
            while (in >> index >> value) {
                if (index < 0 || index >= kAutoSteps || !(value >= 0.0)) continue;
                track->automation[static_cast<int>(sceneNumber) - 1][static_cast<int>(lane)][static_cast<int>(index)] = static_cast<float>(value > 1.0 ? 1.0 : value);
            }
        } else if (word == "send") {
            double bus, amount;
            if (!(in >> bus >> amount) || bus < 1 || bus > kBuses || !(amount >= 0.0)) continue;
            track->send[static_cast<int>(bus) - 1] = static_cast<float>(amount > 1.0 ? 1.0 : amount);
        } else if (word == "glidefrom") {
            double type = 0;
            if (!(in >> type) || !(type >= 0.0) || type > 255.0) continue;
            if (!isSynth(track->kind)) continue; // (a drum track has none: one written there by hand would wait for the day it became a synth)
            track->glideFrom = static_cast<std::uint8_t>(glideFromKind(static_cast<int>(type)));
        } else if (word == "muted") {
            track->muted = true;
        } else if (word == "lanes") {
            double level;
            for (int i = 0; i < kDrumLanes && (in >> level); ++i) track->laneLevel[i] = fractionIn(level, 0.f, 1.f);
        } else if (word == "sample" && track->kind == TrackKind::Drums) {
            double lane, fit;
            std::string name;
            if (!(in >> lane >> fit) || lane < 1 || lane > kDrumLanes) continue;
            std::getline(in >> std::ws, name);
            if (!sampleNameOk(name)) continue; // a name, or a folder and a name: not a path out of the samples folder
            const int l = static_cast<int>(lane) - 1;
            std::snprintf(track->laneSample[l], sizeof track->laneSample[l], "%s", name.c_str());
            track->laneFit[l] = wholeIn(fit, 0, 64);
        } else if (word == "oneslice" && track->kind == TrackKind::Drums) {
            double lane;
            while (in >> lane)
                if (lane >= 1 && lane <= kDrumLanes) track->laneOneSlice[static_cast<int>(lane) - 1] = true;
        } else if (word == "sound" && track->kind == TrackKind::Sampler) {
            std::string name;
            std::getline(in >> std::ws, name);
            if (!sampleNameOk(name)) continue; // a name, or a folder and a name: not a path out of the samples folder
            std::snprintf(track->sample, sizeof track->sample, "%s", name.c_str());
        } else if (word == "sampler" && track->kind == TrackKind::Sampler) {
            double v;
            for (int id = 0; id < sampler::kSettings && (in >> v); ++id) track->sampler[id] = wholeIn(v, sampler::settingMin(id), sampler::settingMax(id));
        } else if (word == "preset") {
            double preset;
            const SynthSpec* spec = specFor(track->kind);
            if (spec && (in >> preset)) {
                track->preset = wholeIn(preset - 1.0, 0, spec->presetCount() - 1);
                spec->loadPreset(*track, track->preset); // "settings", if it follows, overrides
            }
        } else if (word == "macros") {
            double m;
            for (int i = 0; i < kSynthMacros && (in >> m); ++i) track->macro[i] = fractionIn(m, 0.f, 1.f);
        } else if (word == "settings") {
            const SynthSpec* spec = specFor(track->kind);
            if (!spec) continue;
            int* values = spec->values(*track);
            double v;
            for (int i = 0; i < settingCount(track->kind) && (in >> v); ++i) {
                values[i] = wholeIn(v, spec->settingMin(i), spec->settingMax(i));
            }
        } else if (word == "scene") {
            double s;
            scene = (in >> s) && s >= 1 && s <= kMaxScenes ? static_cast<int>(s) - 1 : -1;
            bar = 0;
        } else if (scene < 0) {
            continue;
        } else if (word == "bars") {
            double b;
            if (in >> b) track->bars[scene] = b < 2 ? 1 : (b < 4 ? 2 : (b < 8 ? 4 : 8));
        } else if (word == "bar") {
            double b;
            bar = (in >> b) && b >= 1 && b <= kMaxBars ? static_cast<int>(b) - 1 : -1;
        } else if (bar < 0) {
            continue; // lines for a bar there is not (a bar past the cell's end is one it hides: kept)
        } else if (word == "seeds" && track->kind == TrackKind::Drums) {
            double seed;
            for (int i = 0; i < kDrumLanes && (in >> seed); ++i) track->drums[scene][bar].diceSeed[i] = seedIn(seed);
        } else if (word == "hit" && track->kind == TrackKind::Drums) {
            double lane, step, level, nudge = 0, chance = 1, dice = 0, start = 0;
            if (!(in >> lane >> step >> level)) continue;
            in >> nudge >> chance >> dice >> start;
            if (lane < 1 || lane > kDrumLanes || step < 1 || step > kSteps || !(level > 0)) continue;
            DrumPattern& p = track->drums[scene][bar];
            const int l = static_cast<int>(lane) - 1, s = static_cast<int>(step) - 1;
            p.level[l][s] = fractionIn(level, 0.05f, 1.f);
            p.nudge[l][s] = fractionIn(nudge, -0.5f, 0.5f);
            p.chance[l][s] = fractionIn(chance, 0.05f, 1.f);
            p.fromDice[l][s] = dice != 0;
            p.start[l][s] = fractionIn(start, 0.f, 1.f);
        } else if (word == "stepfx" && track->kind == TrackKind::Drums) {
            double lane, step, pitch = 0, ratchet = 1, reverse = 0, cut = 0, thrown = 0;
            if (!(in >> lane >> step)) continue;
            in >> pitch >> ratchet >> reverse >> cut >> thrown;
            if (lane < 1 || lane > kDrumLanes || step < 1 || step > kSteps) continue;
            StepFx& fx = track->drums[scene][bar].fx[static_cast<int>(lane) - 1][static_cast<int>(step) - 1];
            fx.pitch = static_cast<std::int8_t>(wholeIn(pitch, -kMaxStepPitch, kMaxStepPitch));
            fx.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? wholeIn(ratchet, 2, kMaxRatchet) : 0);
            fx.reverse = reverse != 0 ? 1 : 0;
            fx.cut = static_cast<std::uint8_t>(cut >= 1 && cut < kCutChoices ? cut : 0);
            fx.throwTo = static_cast<std::uint8_t>(thrown >= 1 && thrown < kThrowChoices ? thrown : 0);
        } else if (word == "note" && isSynth(track->kind)) {
            double step, pitch, length, level, nudge = 0, thrown = 0;
            if (!(in >> step >> pitch >> length >> level)) continue;
            in >> nudge >> thrown;
            if (step < 1 || step > kSteps || pitch < 0 || pitch > 127 || !(level > 0)) continue;
            NotePattern& p = track->notes[scene][bar];
            const int slot = p.freeSlot();
            if (slot < 0 || p.used() >= kMaxNotes) continue; // a sixty-fifth note
            const int s = static_cast<int>(step) - 1;
            p.notes[slot].step = s;
            p.notes[slot].pitch = static_cast<int>(pitch);
            p.notes[slot].length = fractionIn(length, kShortestNote, longestFrom(*track, scene, bar, s)); // to the end of the cell: a note may run on over the bar lines
            p.notes[slot].velocity = fractionIn(level, 0.05f, 1.f);
            p.notes[slot].nudge = fractionIn(nudge, -0.5f, 0.5f);
            p.notes[slot].throwTo = static_cast<std::uint8_t>(thrown >= 1 && thrown < kThrowChoices ? thrown : 0);
        } else if (word == "notefx" && isSynth(track->kind)) {
            // For the note of this bar that starts on that step and key (it comes just before this line).
            double step, pitch, ratchet = 1, glide = 0;
            if (!(in >> step >> pitch)) continue;
            in >> ratchet >> glide;
            if (step < 1 || step > kSteps || pitch < 0 || pitch > 127) continue;
            // The note that STARTS there. (Not the cursor's "at": that finds whichever note sounds
            // there first, and a longer note held over this one hid it, so its effect was lost.)
            for (NoteEvent& n : track->notes[scene][bar].notes) {
                if (n.velocity <= 0.f || n.pitch != static_cast<int>(pitch) || n.step != static_cast<int>(step) - 1) continue;
                n.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? wholeIn(ratchet, 2, kMaxRatchet) : 0);
                n.glide = static_cast<std::uint8_t>(glide == 0 ? kGlideOff : (glide >= 1 && glide < 256 ? glideKind(static_cast<int>(glide)) : kGlideHalf)); // (a number not known: on, as it was)
                break;
            }
        } else if (word == "chordfx" && isSynth(track->kind)) {
            double step, ratchet = 1, thrown = 0;
            if (!(in >> step >> ratchet) || step < 1 || step > kSteps) continue;
            in >> thrown;
            for (ChordEvent& c : track->notes[scene][bar].chords) {
                if (c.velocity <= 0.f || c.step != static_cast<int>(step) - 1) continue;
                c.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? wholeIn(ratchet, 2, kMaxRatchet) : 0);
                c.throwTo = static_cast<std::uint8_t>(thrown >= 1 && thrown < kThrowChoices ? thrown : 0);
            }
        } else if (word == "chordplay" && isSynth(track->kind)) {
            // For the chord of this bar that starts on that step (it comes just before this line).
            // Numbers left off are 0; one that is not known is off, one too big is the most.
            double step, value[5] = {0, 0, 0, 0, 0};
            if (!(in >> step) || step < 1 || step > kSteps) continue;
            for (double& v : value) in >> v; // (one that is not there, and all after it, stay 0)
            const auto whole = [](double v) { return v >= 0 && v < 1000 ? static_cast<int>(v) : (v >= 1000 ? 1000 : 0); }; // (not a number: 0)
            for (ChordEvent& c : track->notes[scene][bar].chords) {
                if (c.velocity <= 0.f || c.step != static_cast<int>(step) - 1) continue;
                c.play = chordPlayOf(whole(value[0]), whole(value[1]), whole(value[2]), whole(value[3]), whole(value[4]));
            }
        } else if (word == "chord" && isSynth(track->kind)) {
            double step, length, level, inversion, root, interval;
            std::string name;
            if (!(in >> step >> length >> level >> inversion >> root >> name)) continue;
            if (step < 1 || step > kSteps || root < 0 || root > 127 || !(level > 0)) continue;
            NotePattern& p = track->notes[scene][bar];
            ChordEvent c;
            c.step = static_cast<int>(step) - 1;
            c.length = fractionIn(length, kShortestNote, longestFrom(*track, scene, bar, c.step));
            c.velocity = fractionIn(level, 0.05f, 1.f);
            c.inversion = wholeIn(inversion, -kMaxInversion, kMaxInversion);
            c.root = static_cast<int>(root);
            std::snprintf(c.name, sizeof c.name, "%s", name == "?" ? "" : name.c_str());
            while (c.count < kChordNotes && (in >> interval)) {
                if (interval >= 0 && interval <= 48) c.intervals[c.count++] = static_cast<int>(interval);
            }
            // After its notes, perhaps "nudge" and how far.
            in.clear();
            std::string extra;
            double nudge = 0;
            if ((in >> extra) && extra == "nudge" && (in >> nudge)) c.nudge = fractionIn(nudge, -0.5f, 0.5f);
            // Not one with no notes, not a seventeenth, not two at once, and
            // not past the sixty-four notes.
            int keys[kChordNotes];
            const int slot = p.freeChord();
            bool clash = false;
            for (const ChordEvent& other : p.chords)
                clash = clash || (other.velocity > 0.f && other.step < c.step + c.length && c.step < other.step + other.length);
            if (c.count == 0 || slot < 0 || clash || p.used() + c.pitches(keys) > kMaxNotes) continue;
            p.chords[slot] = c;
        }
    }
    if (count == 0) song->tracks[0].kind = TrackKind::Drums; // a song always has its drum track
    for (int s = 0; s < kMaxScenes; ++s) {
        // An old song's "13 steps to a bar": its bars but the last whole, and thirteen steps of the last.
        if (song->steps[s] == 0 && oldSteps[s] < kSteps) song->steps[s] = (song->sceneBars(s) - 1) * kSteps + oldSteps[s];
        if (song->steps[s] >= song->sceneBars(s) * kSteps) song->steps[s] = 0; // every step: kept as "all of them"
    }
    project = *song;
    return true;
}

bool dropTrackText(std::string& text, int track) {
    if (track < 0 || text.compare(0, 13, "apotheca-song") != 0) return false;
    std::size_t from = std::string::npos, to = text.size();
    int count = 0;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::istringstream in(text.substr(at, end - at));
        std::string word, which;
        if ((in >> word) && word == "track") {
            if (from != std::string::npos) { // the next track's line, of any kind: the end of the one taken out
                to = at;
                break;
            }
            in >> which;
            if (trackKindFromWord(which.c_str()) != TrackKind::None && count < kNumTracks && count++ == track) from = at; // as parseSong counts
        }
        at = end + 1;
    }
    if (from == std::string::npos) return false;
    text.erase(from, to - from);
    return true;
}

bool renameRecordings(std::string& text, const std::vector<std::pair<std::string, std::string>>& renames) {
    if (text.compare(0, 13, "apotheca-song") != 0) return false;
    std::string out;
    bool changed = false;
    TrackKind kind = TrackKind::None; // of the track the line belongs to, told the way parseSong tells it
    int count = 0;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        const bool windows = !line.empty() && line.back() == '\r'; // read without it, as parseSong reads; written back with it
        if (windows) line.pop_back();
        std::istringstream in(line);
        std::string word, name;
        double lane = 0, fit = 0;
        in >> word;
        if (word == "track") {
            std::string which;
            in >> which;
            kind = trackKindFromWord(which.c_str());
            if (kind != TrackKind::None && count++ >= kNumTracks) kind = TrackKind::None; // a thirteenth track is not read
        } else if ((word == "sample" && kind == TrackKind::Drums && (in >> lane >> fit) && lane >= 1 && lane <= kDrumLanes) ||
                   (word == "sound" && kind == TrackKind::Sampler)) {
            std::getline(in >> std::ws, name); // the rest of the line, as it is read
            std::string now = name;
            for (const auto& rename : renames)
                if (now == rename.first) now = rename.second;
            if (now != name) {
                line.replace(line.size() - name.size(), name.size(), now);
                changed = true;
            }
        }
        out += line;
        if (windows) out += '\r';
        if (end < text.size()) out += '\n';
        at = end + 1;
    }
    if (changed) text.swap(out);
    return changed;
}

bool writeTextFile(const std::string& path, const std::string& text) {
    // Written beside the real file and moved into place, so a crash or a flat
    // battery mid-write cannot leave half a song. The new file is on the card
    // (fsync) before it takes the old one's place: else the card may be given
    // the move before the words, and a flat battery leaves an empty song.
    const std::string temporary = path + ".part";
    std::FILE* f = std::fopen(temporary.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fflush(f) == 0 && ok;
    ok = ok && fsync(fileno(f)) == 0;
    if (std::fclose(f) != 0 || !ok) {
        std::remove(temporary.c_str());
        return false;
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) return false;
    // And the move itself, which is written in the folder.
    const std::size_t slash = path.rfind('/');
    const std::string folder = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
    const int dir = open(folder.c_str(), O_RDONLY);
    if (dir >= 0) {
        fsync(dir); // (the file is in place either way: this is only to have it there for good, now)
        close(dir);
    }
    return true;
}

namespace {
// The files of a song in a folder: "<name>.song" (if there) and its revs, "<name>.<n>.rev".
std::vector<std::string> songFiles(const std::string& folder, const std::string& name) {
    std::vector<std::string> files;
    if (DIR* dir = opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            const bool song = file == name + ".song";
            const bool rev = file.size() > name.size() + 5 && file.compare(0, name.size() + 1, name + ".") == 0 &&
                             file.compare(file.size() - 4, 4, ".rev") == 0 &&
                             file.find_first_not_of("0123456789", name.size() + 1) == file.size() - 4;
            if (song || rev) files.push_back(file);
        }
        closedir(dir);
    }
    return files;
}
} // namespace

bool binSong(const std::string& folder, const std::string& name) {
    std::vector<std::string> files = songFiles(folder, name);
    bool there = false;
    for (const std::string& file : files) there = there || file == name + ".song";
    if (!there) return false;
    const std::string bin = folder + "/deleted";
    mkdir(bin.c_str(), 0755); // fine if it is already there
    // A name nothing in "deleted" has, the song's or a rev's: a song deleted before under this
    // name (new songs reuse the names of deleted ones) is not written over.
    std::string as = name;
    for (int n = 2; !songFiles(bin, as).empty(); ++n) as = name + " " + std::to_string(n);
    bool moved = true;
    for (const std::string& file : files) {
        const std::string to = as + file.substr(name.size()); // ".song", or ".<n>.rev"
        moved = std::rename((folder + "/" + file).c_str(), (bin + "/" + to).c_str()) == 0 && moved;
    }
    return moved;
}

bool readTextFile(const std::string& path, std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    text.clear();
    char block[4096];
    std::size_t got;
    while ((got = std::fread(block, 1, sizeof block, f)) > 0) text.append(block, got);
    const bool whole = !std::ferror(f); // a card that fails part-way must not pass for a short file: it may be written back
    std::fclose(f);
    return whole;
}

} // namespace apotheca
