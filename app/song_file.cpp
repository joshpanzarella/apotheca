#include "song_file.hpp"
#include "instruments.hpp"
#include "samples.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>

#include "synth_spec.hpp"

namespace apotheca {

namespace {

float clampf(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); } // NaN goes to lo
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void put(std::string& out, const char* format, double a) {
    char text[48];
    std::snprintf(text, sizeof text, format, a);
    out += text;
}


} // namespace

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
    const auto putEffects = [&](const EffectData* effects) {
        for (int slot = 0; slot < kEffectSlots; ++slot) {
            const EffectKind kind = static_cast<EffectKind>(effects[slot].kind);
            if (kind == EffectKind::None) continue;
            put(out, "fx %.0f ", slot + 1);
            for (const char* c = effectInfo(kind).name; *c; ++c) out += static_cast<char>(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
            for (int i = 0; i < effectInfo(kind).params; ++i) put(out, " %.6g", effects[slot].param[i]);
            out += "\n";
        }
    };
    putEffects(project.masterEffects);

    for (int t = 0; t < project.trackCount(); ++t) {
        const TrackData& track = project.tracks[t];
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
        putEffects(track.effects);
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
            for (int scene = 0; scene < kMaxScenes; ++scene) {
                if (track.sceneEmpty(scene) && track.bars[scene] == 1) continue;
                put(out, "scene %.0f\n", scene + 1);
                if (track.bars[scene] > 1) put(out, "bars %.0f\n", track.bars[scene]);
                for (int bar = 0; bar < track.bars[scene]; ++bar) {
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
                            // stepfx LANE STEP PITCH RATCHET BACKWARDS CUT: the hit's step effects, if it has any
                            // ("fx" is taken: it is an effect in a slot.)
                            if (const StepFx& fx = p.fx[lane][step]; fx.any()) {
                                put(out, "stepfx %.0f", lane + 1);
                                put(out, " %.0f", step + 1);
                                put(out, " %.0f", fx.pitch);
                                put(out, " %.0f", fx.hits());
                                put(out, " %.0f", fx.reverse ? 1 : 0);
                                put(out, " %.0f\n", fx.cut);
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
            for (int scene = 0; scene < kMaxScenes; ++scene) {
                if (track.sceneEmpty(scene) && track.bars[scene] == 1) continue;
                put(out, "scene %.0f\n", scene + 1);
                if (track.bars[scene] > 1) put(out, "bars %.0f\n", track.bars[scene]);
                for (int bar = 0; bar < track.bars[scene]; ++bar) {
                    const NotePattern& p = track.notes[scene][bar];
                    if (p.empty()) continue;
                    if (bar > 0) put(out, "bar %.0f\n", bar + 1);
                    for (const NoteEvent& n : p.notes) {
                        if (n.velocity <= 0.f) continue;
                        put(out, "note %.0f", n.step + 1);
                        put(out, " %.0f", n.pitch);
                        put(out, " %.6g", n.length);
                        put(out, " %.6g", n.velocity);
                        if (n.nudge != 0.f) put(out, " %.6g", n.nudge); // early or late, in steps
                        out += "\n";
                        // notefx STEP KEY RATCHET GLIDE: what the note does besides play, if anything
                        if (n.ratchet >= 2 || n.glide) {
                            put(out, "notefx %.0f", n.step + 1);
                            put(out, " %.0f", n.pitch);
                            put(out, " %.0f", n.ratchet >= 2 ? n.ratchet : 1);
                            put(out, " %.0f\n", n.glide ? 1 : 0);
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
                        if (c.ratchet >= 2) { // chordfx STEP RATCHET
                            put(out, "chordfx %.0f", c.step + 1);
                            put(out, " %.0f\n", c.ratchet);
                        }
                    }
                }
            }
        }
    }
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
        std::istringstream in(line);
        std::string word;
        if (!(in >> word)) continue;

        if (word == "tempo") {
            double tempo = 120;
            if (in >> tempo) song->tempo = clampf(static_cast<float>(tempo), kMinTempo, kMaxTempo);
        } else if (word == "length") {
            double n;
            for (int i = 0; i < kMaxScenes && (in >> n); ++i) song->steps[i] = clampi(static_cast<int>(n), 0, kMaxBars * kSteps);
        } else if (word == "steps") {
            // As songs were kept before: steps to a BAR, 1..16. Turned into a length once the cells' bars are known (below).
            double n;
            for (int i = 0; i < kMaxScenes && (in >> n); ++i) oldSteps[i] = clampi(static_cast<int>(n), 1, kSteps);
        } else if (word == "repeats") {
            double r;
            for (int i = 0; i < kMaxScenes && (in >> r); ++i) song->repeats[i] = clampi(static_cast<int>(r), 1, kMaxRepeats);
        } else if (word == "pack") {
            std::string name;
            std::getline(in >> std::ws, name);
            std::snprintf(song->chordPack, sizeof song->chordPack, "%s", name.c_str());
        } else if (word == "fx") {
            double slot, value;
            std::string name;
            if (!(in >> slot >> name) || slot < 1 || slot > kEffectSlots) continue;
            EffectData& data = (track ? track->effects : song->masterEffects)[static_cast<int>(slot) - 1];
            for (int k = 1; k < kEffectKinds; ++k) {
                const EffectInfo& info = effectInfo(static_cast<EffectKind>(k));
                std::string lower;
                for (const char* c = info.name; *c; ++c) lower += static_cast<char>(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
                if (lower != name) continue;
                data.kind = k;
                for (int i = 0; i < kEffectParams; ++i) data.param[i] = info.defaults[i];
                for (int i = 0; i < info.params && (in >> value); ++i) data.param[i] = clampf(static_cast<float>(value), 0.f, 1.f);
            }
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
        } else if (word == "muted") {
            track->muted = true;
        } else if (word == "lanes") {
            double level;
            for (int i = 0; i < kDrumLanes && (in >> level); ++i) track->laneLevel[i] = clampf(static_cast<float>(level), 0.f, 1.f);
        } else if (word == "sample" && track->kind == TrackKind::Drums) {
            double lane, fit;
            std::string name;
            if (!(in >> lane >> fit) || lane < 1 || lane > kDrumLanes) continue;
            std::getline(in >> std::ws, name);
            if (!sampleNameOk(name)) continue; // a name, or a folder and a name: not a path out of the samples folder
            const int l = static_cast<int>(lane) - 1;
            std::snprintf(track->laneSample[l], sizeof track->laneSample[l], "%s", name.c_str());
            track->laneFit[l] = clampi(static_cast<int>(fit), 0, 64);
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
            for (int id = 0; id < sampler::kSettings && (in >> v); ++id) track->sampler[id] = clampi(static_cast<int>(v), sampler::settingMin(id), sampler::settingMax(id));
        } else if (word == "preset") {
            double preset;
            const SynthSpec* spec = specFor(track->kind);
            if (spec && (in >> preset)) {
                track->preset = clampi(static_cast<int>(preset) - 1, 0, spec->presetCount() - 1);
                spec->loadPreset(*track, track->preset); // "settings", if it follows, overrides
            }
        } else if (word == "macros") {
            double m;
            for (int i = 0; i < kSynthMacros && (in >> m); ++i) track->macro[i] = clampf(static_cast<float>(m), 0.f, 1.f);
        } else if (word == "settings") {
            const SynthSpec* spec = specFor(track->kind);
            if (!spec) continue;
            int* values = spec->values(*track);
            double v;
            for (int i = 0; i < settingCount(track->kind) && (in >> v); ++i) {
                values[i] = clampi(static_cast<int>(v), spec->settingMin(i), spec->settingMax(i));
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
        } else if (bar < 0 || bar >= track->bars[scene]) {
            continue; // lines for a bar the cell does not have
        } else if (word == "seeds" && track->kind == TrackKind::Drums) {
            double seed;
            for (int i = 0; i < kDrumLanes && (in >> seed); ++i) track->drums[scene][bar].diceSeed[i] = static_cast<std::uint32_t>(seed);
        } else if (word == "hit" && track->kind == TrackKind::Drums) {
            double lane, step, level, nudge = 0, chance = 1, dice = 0, start = 0;
            if (!(in >> lane >> step >> level)) continue;
            in >> nudge >> chance >> dice >> start;
            if (lane < 1 || lane > kDrumLanes || step < 1 || step > kSteps || !(level > 0)) continue;
            DrumPattern& p = track->drums[scene][bar];
            const int l = static_cast<int>(lane) - 1, s = static_cast<int>(step) - 1;
            p.level[l][s] = clampf(static_cast<float>(level), 0.05f, 1.f);
            p.nudge[l][s] = clampf(static_cast<float>(nudge), -0.5f, 0.5f);
            p.chance[l][s] = clampf(static_cast<float>(chance), 0.05f, 1.f);
            p.fromDice[l][s] = dice != 0;
            p.start[l][s] = clampf(static_cast<float>(start), 0.f, 1.f);
        } else if (word == "stepfx" && track->kind == TrackKind::Drums) {
            double lane, step, pitch = 0, ratchet = 1, reverse = 0, cut = 0;
            if (!(in >> lane >> step)) continue;
            in >> pitch >> ratchet >> reverse >> cut;
            if (lane < 1 || lane > kDrumLanes || step < 1 || step > kSteps) continue;
            StepFx& fx = track->drums[scene][bar].fx[static_cast<int>(lane) - 1][static_cast<int>(step) - 1];
            fx.pitch = static_cast<std::int8_t>(clampi(static_cast<int>(pitch), -kMaxStepPitch, kMaxStepPitch));
            fx.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? clampi(static_cast<int>(ratchet), 2, kMaxRatchet) : 0);
            fx.reverse = reverse != 0 ? 1 : 0;
            fx.cut = static_cast<std::uint8_t>(cut >= 1 && cut < kCutChoices ? cut : 0);
        } else if (word == "note" && isSynth(track->kind)) {
            double step, pitch, length, level, nudge = 0;
            if (!(in >> step >> pitch >> length >> level)) continue;
            in >> nudge;
            if (step < 1 || step > kSteps || pitch < 0 || pitch > 127 || !(level > 0)) continue;
            NotePattern& p = track->notes[scene][bar];
            const int slot = p.freeSlot();
            if (slot < 0 || p.used() >= kMaxNotes) continue; // a sixty-fifth note
            const int s = static_cast<int>(step) - 1;
            p.notes[slot].step = s;
            p.notes[slot].pitch = static_cast<int>(pitch);
            p.notes[slot].length = clampf(static_cast<float>(length), kShortestNote, static_cast<float>((track->bars[scene] - bar) * kSteps - s)); // to the end of the cell: a note may run on over the bar lines
            p.notes[slot].velocity = clampf(static_cast<float>(level), 0.05f, 1.f);
            p.notes[slot].nudge = clampf(static_cast<float>(nudge), -0.5f, 0.5f);
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
                n.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? clampi(static_cast<int>(ratchet), 2, kMaxRatchet) : 0);
                n.glide = glide != 0 ? 1 : 0;
                break;
            }
        } else if (word == "chordfx" && isSynth(track->kind)) {
            double step, ratchet = 1;
            if (!(in >> step >> ratchet) || step < 1 || step > kSteps) continue;
            for (ChordEvent& c : track->notes[scene][bar].chords)
                if (c.velocity > 0.f && c.step == static_cast<int>(step) - 1) c.ratchet = static_cast<std::uint8_t>(ratchet >= 2 ? clampi(static_cast<int>(ratchet), 2, kMaxRatchet) : 0);
        } else if (word == "chord" && isSynth(track->kind)) {
            double step, length, level, inversion, root, interval;
            std::string name;
            if (!(in >> step >> length >> level >> inversion >> root >> name)) continue;
            if (step < 1 || step > kSteps || root < 0 || root > 127 || !(level > 0)) continue;
            NotePattern& p = track->notes[scene][bar];
            ChordEvent c;
            c.step = static_cast<int>(step) - 1;
            c.length = clampf(static_cast<float>(length), kShortestNote, static_cast<float>((track->bars[scene] - bar) * kSteps - c.step));
            c.velocity = clampf(static_cast<float>(level), 0.05f, 1.f);
            c.inversion = clampi(static_cast<int>(inversion), -kMaxInversion, kMaxInversion);
            c.root = static_cast<int>(root);
            std::snprintf(c.name, sizeof c.name, "%s", name == "?" ? "" : name.c_str());
            while (c.count < kChordNotes && (in >> interval)) {
                if (interval >= 0 && interval <= 48) c.intervals[c.count++] = static_cast<int>(interval);
            }
            // After its notes, perhaps "nudge" and how far.
            in.clear();
            std::string extra;
            double nudge = 0;
            if ((in >> extra) && extra == "nudge" && (in >> nudge)) c.nudge = clampf(static_cast<float>(nudge), -0.5f, 0.5f);
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

bool writeTextFile(const std::string& path, const std::string& text) {
    // Written beside the real file and moved into place, so a crash or a flat
    // battery mid-write cannot leave half a song.
    const std::string temporary = path + ".part";
    std::FILE* f = std::fopen(temporary.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) {
        std::remove(temporary.c_str());
        return false;
    }
    return std::rename(temporary.c_str(), path.c_str()) == 0;
}

bool readTextFile(const std::string& path, std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    text.clear();
    char block[4096];
    std::size_t got;
    while ((got = std::fread(block, 1, sizeof block, f)) > 0) text.append(block, got);
    std::fclose(f);
    return true;
}

} // namespace apotheca
