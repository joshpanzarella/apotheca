// KIT PRESETS (easywins batch 7, docs/plans/easywins-plan.md): a drum track's whole kit kept as a named
// kit and put on a drum track. The built-in kits are recipes of DRUM SYNTH's named voices
// (drum_voices.hpp), looked up by NAME when first asked for; a player's own are files beside the synths'
// presets, "NAME.kit", in the song file's own lines (kitText / readKit, song_file.hpp).

#include "session.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "drum_voices.hpp"
#include "song_file.hpp"

namespace apotheca {

namespace {

// A built-in kit: each lane a model's named voice ("" the built-in drum), and its CHOKE group.
struct Recipe {
    const char* name;
    struct Lane {
        const char* model; // the model's song-file word, or "" for the built-in drum
        const char* voice; // the named voice (kUsualVoiceName: the model's usual)
        int choke;
    } lane[kDrumLanes];
};

const Recipe kRecipes[] = {
    {"DRUMS", {{"", "", 0}, {"", "", 0}, {"", "", 1}, {"", "", 1}, {"", "", 0}, {"", "", 0}, {"", "", 0}, {"", "", 0}}},
    {"SYNTH", {{"kick", "CLASSIC", 0}, {"snare", "CLASSIC", 0}, {"clhat", "CLASSIC", 1}, {"ophat", "CLASSIC", 1}, {"clap", "CLASSIC", 0}, {"tom", "CLASSIC", 0}, {"rim", "CLASSIC", 0}, {"cymbal", "CLASSIC", 0}}},
    {"TIGHT", {{"kick", "TIGHT", 0}, {"snare", "PICCOLO", 0}, {"clhat", "TICK", 1}, {"ophat", "HALF OPEN", 1}, {"clap", "TIGHT", 0}, {"tom", "HIGH", 0}, {"rim", "TIGHT RIM", 0}, {"cymbal", "SPLASH", 0}}},
    {"BIG", {{"kick", "BOOM", 0}, {"snare", "FAT", 0}, {"clhat", "METAL", 1}, {"ophat", "LONG", 1}, {"clap", "ROOM", 0}, {"tom", "FLOOR", 0}, {"rim", "WOODY", 0}, {"cymbal", "CHINA", 0}}},
    {"ELECTRO", {{"kick", "LASER KICK", 0}, {"snare", "CRACK", 0}, {"clhat", "GLASS", 1}, {"ophat", "TSS", 1}, {"clap", "FINGER SNAP", 0}, {"zap", "PEW", 0}, {"clave", "BLIP", 0}, {"cowbell", "SHORT", 0}}},
    {"DUSTY", {{"kick", "BREATH", 0}, {"snare", "BRUSH", 0}, {"clhat", "SHAKER", 1}, {"ophat", "SIZZLE", 1}, {"clap", "PAPER", 0}, {"tom", "LOG", 0}, {"rim", "BLOCK", 0}, {"noise", "HISS", 0}}},
    {"HAND DRUMS", {{"tom", "CONGA", 0}, {"tom", "LOW", 0}, {"clave", "WOODBLOCK", 0}, {"cowbell", "AGOGO", 0}, {"clap", "FINGER SNAP", 0}, {"clave", "HOLLOW", 0}, {"rim", "PING", 0}, {"cymbal", "BELL", 0}}},
};
const int kRecipeCount = sizeof kRecipes / sizeof kRecipes[0];

// The recipe as a kit's text; a voice it names that is not in the data is a problem (and the model's usual).
std::string recipeText(const Recipe& recipe, std::vector<std::string>* problems) {
    std::unique_ptr<TrackData> kit(new TrackData);
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const Recipe::Lane& l = recipe.lane[lane];
        kit->laneChoke[lane] = static_cast<std::uint8_t>(l.choke);
        if (!l.model[0]) continue;
        const int model = drumsynth::modelFromWord(l.model);
        int place = -1;
        for (int v = 0; v < voiceCount(model); ++v)
            if (std::strcmp(voiceName(model, v), l.voice) == 0) place = v;
        if (model == drumsynth::kNone || place < 0) {
            if (problems) problems->push_back(std::string(recipe.name) + ": no voice " + l.model + " " + l.voice);
            if (model == drumsynth::kNone) continue;
            place = 0;
        }
        kit->laneSynth[lane] = voiceAt(model, place);
    }
    return kitText(*kit);
}

} // namespace

std::vector<std::string> builtInKitProblems() {
    std::vector<std::string> problems;
    for (const Recipe& r : kRecipes) recipeText(r, &problems);
    return problems;
}

void Session::readKits() {
    kits_.clear();
    for (const Recipe& r : kRecipes) kits_.push_back(Kit{r.name, recipeText(r, nullptr), false});
    if (presetFolder_.empty()) return;
    const std::string ending = ".kit";
    std::vector<std::string> names;
    if (DIR* dir = opendir(presetFolder_.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name.size() > ending.size() && name.compare(name.size() - ending.size(), ending.size(), ending) == 0 && name[0] != '.') names.push_back(name);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    std::unique_ptr<TrackData> held(new TrackData);
    for (const std::string& file : names) {
        std::string text, name = file.substr(0, file.size() - ending.size());
        for (char& c : name) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        if (name.size() > kKitNameMost) name.resize(kKitNameMost);
        // Kept as it reads back (a file from elsewhere with a number out of range is held, as a song's is).
        if (readTextFile(presetFolder_ + "/" + file, text) && readKit(text, *held)) kits_.push_back(Kit{name, kitText(*held), true, file});
        *held = TrackData();
    }
}

const std::vector<Session::Kit>& Session::kits() {
    if (kits_.empty()) readKits();
    return kits_;
}

int Session::kitOf(int track) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Drums) return -1;
    const std::string now = kitText(project.tracks[track]);
    const std::vector<Kit>& all = kits();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].text == now) return static_cast<int>(i);
    return -1;
}

bool Session::putKit(int track, const TrackData& kit) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Drums) return false;
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        // The sound first (another sound puts START and END back to the whole), then what is done to it.
        const TrackData& now = project.tracks[track];
        if (kit.laneSynth[lane].on()) {
            if (!(now.laneSynth[lane] == kit.laneSynth[lane])) setLaneSynth(track, lane, kit.laneSynth[lane]);
        } else if (now.laneSynth[lane].on() || std::strcmp(now.laneSample[lane], kit.laneSample[lane]) != 0) {
            setLaneSample(track, lane, kit.laneSample[lane]);
        }
        setLaneEdit(track, lane, kit.laneEdit[lane]);
        setLaneFit(track, lane, kit.laneFit[lane]);
        setLaneOneSlice(track, lane, kit.laneOneSlice[lane]);
        setLaneLevel(track, lane, kit.laneLevel[lane]);
        setLaneFeel(track, lane, kit.laneFeel[lane].time, kit.laneFeel[lane].level);
        setLaneSwing(track, lane, kit.laneSwing[lane]);
        setLaneChoke(track, lane, kit.laneChoke[lane]);
    }
    // A step of undo like any change to a kit (soundMark sees it): voices, EDIT, FEEL, SWING and CHOKE
    // are put back. A recording is not, as always: the kit's stop keeps the kit as it was while A is
    // held, and SAVE keeps one for good.
    return true;
}

bool Session::loadKit(int track, int index) {
    const std::vector<Kit>& all = kits();
    if (index < 0 || index >= static_cast<int>(all.size())) return false;
    std::unique_ptr<TrackData> kit(new TrackData);
    return readKit(all[static_cast<std::size_t>(index)].text, *kit) && putKit(track, *kit);
}

std::string Session::saveKit(int track) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Drums || presetFolder_.empty()) return "";
    mkdir(presetFolder_.c_str(), 0755);
    const std::vector<Kit>& all = kits();
    for (int n = 1; n < 1000; ++n) {
        char name[24];
        std::snprintf(name, sizeof name, "KIT %d", n);
        bool taken = false;
        for (const Kit& k : all) taken = taken || k.name == name;
        if (taken) continue;
        char file[32];
        std::snprintf(file, sizeof file, "kit %d.kit", n);
        if (!writeTextFile(presetFolder_ + "/" + file, kitText(project.tracks[track]))) return "";
        readKits();
        return name;
    }
    return "";
}

bool Session::deleteKit(int index) {
    const std::vector<Kit>& all = kits();
    if (index < 0 || index >= static_cast<int>(all.size()) || !all[static_cast<std::size_t>(index)].own || presetFolder_.empty()) return false;
    const bool gone = std::remove((presetFolder_ + "/" + all[static_cast<std::size_t>(index)].file).c_str()) == 0;
    readKits();
    return gone;
}

} // namespace apotheca
