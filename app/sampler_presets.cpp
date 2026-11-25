// SAMPLER PRESETS: a sampler track's sound kept as a named preset and put on a sampler track
// (mirror kits.cpp: a preset is to a sampler track what a kit is to a drum one). The player's own
// are files beside the synths' presets, "NAME.sampler", in the song file's own lines:
//
//   apotheca-sampler 1
//   sound NAME.wav
//   sampler PITCH TUNE START END ATTACK RELEASE LOOP LEVEL
//   chop
//   edit START END GAIN FADEIN FADEOUT REVERSE   (its EDIT, S4: as the song's sampleredit; left out at rest)
//   whole
//   fx/send/glidefrom/mod... (soundText's lines)
//
// There are no built-in ones: the list is the player's own, or empty. A preset holds the track's
// whole sound too, as a new synth preset's (README, "Presets").

#include "session.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "apotheca/sampler.hpp"
#include "mods.hpp"
#include "presets.hpp"
#include "samples.hpp"
#include "song_file.hpp"

namespace apotheca {
namespace {

// The track's sound as a preset file holds it.
std::string samplerPresetText(const TrackData& t, const TrackSound& sound) {
    std::string out = "apotheca-sampler 1\n";
    if (t.sample[0]) out += std::string("sound ") + t.sample + "\n";
    out += "sampler";
    for (int id = 0; id < sampler::kSettings; ++id) {
        char number[16];
        std::snprintf(number, sizeof number, " %d", t.sampler[id]);
        out += number;
    }
    out += "\n";
    if (t.samplerChop) out += "chop\n";
    if (!t.samplerEdit.atRest()) {
        char line[160];
        const LaneEdit& e = t.samplerEdit;
        std::snprintf(line, sizeof line, "edit %.6g %.6g %.6g %d %d %d\n", static_cast<double>(e.start) / kEditUnits, static_cast<double>(e.end) / kEditUnits, e.gain / 10.0, e.fadeIn, e.fadeOut, e.reverse);
        out += line;
    }
    out += "whole\n" + soundText(TrackKind::Sampler, sound.effects, sound.send, sound.glideFrom, sound.mods);
    return out;
}

// A preset file's text into a preset. Tolerant, as a song's reading is: numbers held, unknown
// words passed over. False: not a sampler preset at all.
bool readSamplerPreset(const std::string& text, Session::SamplerPreset& out) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 16, "apotheca-sampler") != 0) return false;
    for (int id = 0; id < sampler::kSettings; ++id) out.settings[id] = sampler::settingDefault(id);
    out.sample.clear();
    out.chop = false;
    out.edit = LaneEdit();
    out.sound = TrackSound();
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); // (kept with Windows' line ends)
        std::istringstream in(line);
        std::string word;
        in >> word;
        if (word == "sound") {
            std::string name;
            std::getline(in >> std::ws, name);
            if (sampleNameOk(name)) out.sample = name;
        } else if (word == "sampler") {
            double v = 0.0;
            for (int id = 0; id < sampler::kSettings && (in >> v); ++id)
                out.settings[id] = !std::isfinite(v) ? sampler::settingDefault(id)
                    : (v < sampler::settingMin(id) ? sampler::settingMin(id) : (v > sampler::settingMax(id) ? sampler::settingMax(id) : static_cast<int>(v)));
        } else if (word == "chop") {
            out.chop = true;
        } else if (word == "edit") { // (a number missing or not one ends it there, the rest at rest)
            double v[6] = {0, 1, 0, 0, 0, 0};
            for (double& one : v) {
                double read = 0;
                if (!(in >> read) || !std::isfinite(read)) break;
                one = read;
            }
            const auto within = [](double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); };
            LaneEdit e;
            e.start = static_cast<int>(std::lround(within(v[0], 0.0, 1.0) * kEditUnits));
            e.end = static_cast<int>(std::lround(within(v[1], 0.0, 1.0) * kEditUnits));
            e.gain = static_cast<int>(std::lround(within(v[2], -kGainMost / 10.0, kGainMost / 10.0) * 10.0));
            e.fadeIn = static_cast<int>(within(v[3], 0, kFadeMost));
            e.fadeOut = static_cast<int>(within(v[4], 0, kFadeMost));
            e.reverse = v[5] >= 0.5 ? 1 : 0;
            out.edit = laneEditHeld(e, 0.f);
        } else if (word == "whole") {
            out.sound.whole = true;
        } else if (readSoundLine(word, in, SoundParts{TrackKind::Sampler, out.sound.effects, out.sound.send, &out.sound.glideFrom, out.sound.mods}) == SoundLine::Taken) {
            out.sound.whole = true;
        }
    }
    modsFit(TrackKind::Sampler, out.sound.effects, out.sound.mods); // a MOD aimed where nothing fits is OFF
    return true;
}

} // namespace

void Session::readSamplerPresets() {
    samplerPresets_.clear();
    samplerPresetsRead_ = true;
    if (presetFolder_.empty()) return;
    mkdir(presetFolder_.c_str(), 0755);
    std::vector<std::string> names;
    if (DIR* dir = opendir(presetFolder_.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name.size() > 8 && name.compare(name.size() - 8, 8, ".sampler") == 0 && name[0] != '.') names.push_back(name);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    for (const std::string& file : names) {
        std::string text;
        SamplerPreset preset;
        if (!readTextFile(presetFolder_ + "/" + file, text) || !readSamplerPreset(text, preset)) continue; // not one of ours (left where it is)
        preset.name = file.substr(0, file.size() - 8);
        for (char& c : preset.name) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        if (preset.name.size() > kSamplerPresetNameMost) preset.name.resize(kSamplerPresetNameMost);
        preset.file = file;
        samplerPresets_.push_back(preset);
    }
}

const std::vector<Session::SamplerPreset>& Session::samplerPresets() {
    if (!samplerPresetsRead_) readSamplerPresets();
    return samplerPresets_;
}

int Session::samplerPresetOf(int track) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Sampler) return -1;
    const TrackData& t = project.tracks[track];
    const std::vector<SamplerPreset>& all = samplerPresets();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const SamplerPreset& p = all[i];
        bool same = p.sample == t.sample && p.chop == t.samplerChop && p.edit == t.samplerEdit;
        for (int id = 0; same && id < sampler::kSettings; ++id) same = p.settings[id] == t.sampler[id];
        if (same) return static_cast<int>(i);
    }
    return -1;
}

bool Session::loadSamplerPreset(int track, int index) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Sampler) return false;
    const std::vector<SamplerPreset>& all = samplerPresets();
    if (index < 0 || index >= static_cast<int>(all.size())) return false;
    const SamplerPreset& p = all[static_cast<std::size_t>(index)];
    setTrackSample(track, p.sample);
    for (int id = 0; id < sampler::kSettings; ++id) setSamplerSetting(track, id, p.settings[id]);
    setSamplerChop(track, p.chop);
    setSamplerEdit(track, p.edit); // (after the recording, which puts START and END back to the whole)
    putPresetSound(track, p.sound.whole ? &p.sound : nullptr);
    return true;
}

bool Session::saveSamplerPreset(int track, const std::string& name) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Sampler) return false;
    const std::string clean = presetNameFrom(name);
    if (clean.empty() || presetFolder_.empty()) return false;
    if (!writeTextFile(presetFolder_ + "/" + clean + ".sampler", samplerPresetText(project.tracks[track], trackSoundOf(project.tracks[track])))) return false;
    readSamplerPresets();
    return true;
}

bool Session::deleteSamplerPreset(int index) {
    const std::vector<SamplerPreset>& all = samplerPresets();
    if (index < 0 || index >= static_cast<int>(all.size())) return false;
    if (std::remove((presetFolder_ + "/" + all[static_cast<std::size_t>(index)].file).c_str()) != 0) return false;
    readSamplerPresets();
    return true;
}

} // namespace apotheca
