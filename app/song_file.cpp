#include "song_file.hpp"

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

int settingCount(TrackKind kind) { return kind == TrackKind::Phazed ? phazed::kSettings : kind == TrackKind::Twin ? twin::kSettings : 0; }

} // namespace

std::string songText(const Project& project) {
    std::string out = "apotheca-song 1\n";
    put(out, "tempo %.6g\n", project.tempo);
    out += "repeats";
    for (int r : project.repeats) put(out, " %.0f", r);
    out += "\n";
    if (project.chordPack[0]) out += std::string("pack ") + project.chordPack + "\n";

    for (int t = 0; t < project.trackCount(); ++t) {
        const TrackData& track = project.tracks[t];
        out += track.kind == TrackKind::Drums ? "track drums\n" : track.kind == TrackKind::Phazed ? "track phazed\n" : "track twin\n";
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
            for (int scene = 0; scene < kMaxScenes; ++scene) {
                const DrumPattern& p = track.drums[scene];
                if (p.empty()) continue;
                put(out, "scene %.0f\n", scene + 1);
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
                    }
                }
            }
        } else {
            put(out, "preset %.0f\n", track.preset + 1);
            out += "macros";
            for (float m : track.macro) put(out, " %.6g", m);
            out += "\nsettings";
            const int* values = specFor(track.kind)->values(const_cast<TrackData&>(track));
            for (int i = 0; i < settingCount(track.kind); ++i) put(out, " %.0f", values[i]);
            out += "\n";
            for (int scene = 0; scene < kMaxScenes; ++scene) {
                const NotePattern& p = track.notes[scene];
                if (p.empty()) continue;
                put(out, "scene %.0f\n", scene + 1);
                for (const NoteEvent& n : p.notes) {
                    if (n.velocity <= 0.f) continue;
                    put(out, "note %.0f", n.step + 1);
                    put(out, " %.0f", n.pitch);
                    put(out, " %.0f", n.length);
                    put(out, " %.6g\n", n.velocity);
                }
                // chord STEP LENGTH LEVEL INVERSION ROOT NAME then its notes above the root
                for (const ChordEvent& c : p.chords) {
                    if (c.velocity <= 0.f) continue;
                    put(out, "chord %.0f", c.step + 1);
                    put(out, " %.0f", c.length);
                    put(out, " %.6g", c.velocity);
                    put(out, " %.0f", c.inversion);
                    put(out, " %.0f ", c.root);
                    out += c.name[0] ? c.name : "?";
                    for (int i = 0; i < c.count; ++i) put(out, " %.0f", c.intervals[i]);
                    out += "\n";
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
    int count = 0, scene = -1;
    TrackData* track = nullptr;

    while (std::getline(lines, line)) {
        std::istringstream in(line);
        std::string word;
        if (!(in >> word)) continue;

        if (word == "tempo") {
            double tempo = 120;
            if (in >> tempo) song->tempo = clampf(static_cast<float>(tempo), kMinTempo, kMaxTempo);
        } else if (word == "repeats") {
            double r;
            for (int i = 0; i < kMaxScenes && (in >> r); ++i) song->repeats[i] = clampi(static_cast<int>(r), 1, kMaxRepeats);
        } else if (word == "pack") {
            std::string name;
            std::getline(in >> std::ws, name);
            std::snprintf(song->chordPack, sizeof song->chordPack, "%s", name.c_str());
        } else if (word == "track") {
            std::string kind;
            in >> kind;
            const TrackKind k = kind == "drums" ? TrackKind::Drums : kind == "phazed" ? TrackKind::Phazed : kind == "twin" ? TrackKind::Twin : TrackKind::None;
            track = nullptr;
            scene = -1;
            if (k == TrackKind::None || count >= kNumTracks) continue; // an instrument this version does not have, or a thirteenth track
            track = &song->tracks[count++];
            *track = TrackData();
            track->kind = k;
            if (const SynthSpec* spec = specFor(k)) spec->loadPreset(*track, 0);
        } else if (!track) {
            continue; // nothing else means anything outside a track
        } else if (word == "lanes") {
            double level;
            for (int i = 0; i < kDrumLanes && (in >> level); ++i) track->laneLevel[i] = clampf(static_cast<float>(level), 0.f, 1.f);
        } else if (word == "sample" && track->kind == TrackKind::Drums) {
            double lane, fit;
            std::string name;
            if (!(in >> lane >> fit) || lane < 1 || lane > kDrumLanes) continue;
            std::getline(in >> std::ws, name);
            if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) continue; // a name, not a path
            const int l = static_cast<int>(lane) - 1;
            std::snprintf(track->laneSample[l], sizeof track->laneSample[l], "%s", name.c_str());
            track->laneFit[l] = clampi(static_cast<int>(fit), 0, 64);
        } else if (word == "preset") {
            double preset;
            const SynthSpec* spec = specFor(track->kind);
            if (spec && (in >> preset)) {
                track->preset = clampi(static_cast<int>(preset) - 1, 0, spec->presetCount() - 1);
                spec->loadPreset(*track, track->preset); // "settings", if it follows, overrides
            }
        } else if (word == "macros") {
            double m;
            for (int i = 0; i < phazed::kMacros && (in >> m); ++i) track->macro[i] = clampf(static_cast<float>(m), 0.f, 1.f);
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
        } else if (scene < 0) {
            continue;
        } else if (word == "seeds" && track->kind == TrackKind::Drums) {
            double seed;
            for (int i = 0; i < kDrumLanes && (in >> seed); ++i) track->drums[scene].diceSeed[i] = static_cast<std::uint32_t>(seed);
        } else if (word == "hit" && track->kind == TrackKind::Drums) {
            double lane, step, level, nudge = 0, chance = 1, dice = 0, start = 0;
            if (!(in >> lane >> step >> level)) continue;
            in >> nudge >> chance >> dice >> start;
            if (lane < 1 || lane > kDrumLanes || step < 1 || step > kSteps || !(level > 0)) continue;
            DrumPattern& p = track->drums[scene];
            const int l = static_cast<int>(lane) - 1, s = static_cast<int>(step) - 1;
            p.level[l][s] = clampf(static_cast<float>(level), 0.05f, 1.f);
            p.nudge[l][s] = clampf(static_cast<float>(nudge), -0.5f, 0.5f);
            p.chance[l][s] = clampf(static_cast<float>(chance), 0.05f, 1.f);
            p.fromDice[l][s] = dice != 0;
            p.start[l][s] = clampf(static_cast<float>(start), 0.f, 1.f);
        } else if (word == "note" && isSynth(track->kind)) {
            double step, pitch, length, level;
            if (!(in >> step >> pitch >> length >> level)) continue;
            if (step < 1 || step > kSteps || pitch < 0 || pitch > 127 || !(level > 0)) continue;
            NotePattern& p = track->notes[scene];
            const int slot = p.freeSlot();
            if (slot < 0 || p.used() >= kMaxNotes) continue; // a sixty-fifth note
            const int s = static_cast<int>(step) - 1;
            p.notes[slot].step = s;
            p.notes[slot].pitch = static_cast<int>(pitch);
            p.notes[slot].length = clampi(static_cast<int>(length), 1, kSteps - s);
            p.notes[slot].velocity = clampf(static_cast<float>(level), 0.05f, 1.f);
        } else if (word == "chord" && isSynth(track->kind)) {
            double step, length, level, inversion, root, interval;
            std::string name;
            if (!(in >> step >> length >> level >> inversion >> root >> name)) continue;
            if (step < 1 || step > kSteps || root < 0 || root > 127 || !(level > 0)) continue;
            NotePattern& p = track->notes[scene];
            ChordEvent c;
            c.step = static_cast<int>(step) - 1;
            c.length = clampi(static_cast<int>(length), 1, kSteps - c.step);
            c.velocity = clampf(static_cast<float>(level), 0.05f, 1.f);
            c.inversion = clampi(static_cast<int>(inversion), -kMaxInversion, kMaxInversion);
            c.root = static_cast<int>(root);
            std::snprintf(c.name, sizeof c.name, "%s", name == "?" ? "" : name.c_str());
            while (c.count < kChordNotes && (in >> interval)) {
                if (interval >= 0 && interval <= 48) c.intervals[c.count++] = static_cast<int>(interval);
            }
            // Not one with no notes, not a seventeenth, not two at once, and
            // not past the sixty-four notes.
            int keys[kChordNotes];
            const int slot = p.freeChord();
            bool clash = false;
            for (int s = c.step; s < c.step + c.length; ++s) clash = clash || p.chordAt(s) >= 0;
            if (c.count == 0 || slot < 0 || clash || p.used() + c.pitches(keys) > kMaxNotes) continue;
            p.chords[slot] = c;
        }
    }
    if (count == 0) song->tracks[0].kind = TrackKind::Drums; // a song always has its drum track
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
