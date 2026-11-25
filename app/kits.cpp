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
#include <initializer_list>
#include <memory>

#include "drum_voices.hpp"
#include "samples.hpp"
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
        setLaneCuts(track, lane, kit.laneCuts[lane]); // (after the sound: another sound makes its slices EVEN)
        setLaneLevel(track, lane, kit.laneLevel[lane]);
        setLaneFeel(track, lane, kit.laneFeel[lane].time, kit.laneFeel[lane].level);
        setLaneSwing(track, lane, kit.laneSwing[lane]);
        setLaneChoke(track, lane, kit.laneChoke[lane]);
    }
    // A step of undo like any change to a kit (soundMark sees it): recordings, voices, EDIT, FEEL, SWING
    // and CHOKE are put back (a recording by its name since gesture audit G04). The kit's stop also
    // keeps the kit as it was while A is held, and SAVE keeps one for good.
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

// ---- a folder as a kit (batch S1) ---------------------------------------------------------

namespace {

// A recording's name by itself, as words: small letters and numbers apart, anything else a gap.
std::vector<std::string> nameWords(const std::string& recording) {
    std::vector<std::string> words;
    std::string word;
    int was = 0; // 1 a letter, 2 a number
    for (char raw : sampleLabel(recording)) {
        const char c = static_cast<char>(raw >= 'A' && raw <= 'Z' ? raw + 32 : raw);
        const int kind = c >= 'a' && c <= 'z' ? 1 : (c >= '0' && c <= '9' ? 2 : 0);
        if ((kind != was || kind == 0) && !word.empty()) words.push_back(word), word.clear();
        if (kind) word += c;
        was = kind;
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

bool hasWord(const std::vector<std::string>& words, std::initializer_list<const char*> any) {
    for (const std::string& w : words)
        for (const char* a : any)
            if (w == a) return true;
    return false;
}

enum class Kind { Other, Kick, Snare, Clap, ClosedHat, OpenHat };

Kind kindOf(const std::string& recording) {
    const std::vector<std::string> w = nameWords(recording);
    const bool hat = hasWord(w, {"hat", "hats", "hihat", "hihats", "hh"});
    if (hasWord(w, {"oh", "ohh", "ophat", "openhat"}) || (hat && hasWord(w, {"open"}))) return Kind::OpenHat;
    if (hat || hasWord(w, {"ch", "chh", "clhat", "closedhat"})) return Kind::ClosedHat;
    if (hasWord(w, {"kick", "kicks", "bd"})) return Kind::Kick;
    if (hasWord(w, {"snare", "snares", "sd"})) return Kind::Snare;
    if (hasWord(w, {"clap", "claps"})) return Kind::Clap;
    return Kind::Other;
}

} // namespace

bool folderKitHat(const std::string& recording) {
    const Kind kind = kindOf(recording);
    return kind == Kind::ClosedHat || kind == Kind::OpenHat;
}

void folderKitLanes(const std::vector<std::string>& recordings, std::string lanes[kDrumLanes]) {
    for (int lane = 0; lane < kDrumLanes; ++lane) lanes[lane].clear();
    std::vector<bool> used(recordings.size(), false);
    std::vector<Kind> kinds;
    for (const std::string& r : recordings) kinds.push_back(kindOf(r));
    // Lanes 1 to 4 by name: each the first of its kind (lane 2 a snare before a clap).
    const struct { int lane; Kind first, second; } wanted[] = {
        {0, Kind::Kick, Kind::Kick}, {1, Kind::Snare, Kind::Clap}, {2, Kind::ClosedHat, Kind::ClosedHat}, {3, Kind::OpenHat, Kind::OpenHat}};
    for (const auto& w : wanted)
        for (Kind k : {w.first, w.second})
            for (std::size_t i = 0; i < recordings.size() && lanes[w.lane].empty(); ++i)
                if (!used[i] && kinds[i] == k) lanes[w.lane] = recordings[i], used[i] = true;
    // Then every lane still empty, in order: the rest in name order.
    std::size_t next = 0;
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        if (!lanes[lane].empty()) continue;
        while (next < recordings.size() && used[next]) ++next;
        if (next == recordings.size()) break;
        lanes[lane] = recordings[next];
        used[next] = true;
    }
}

std::vector<std::string> Session::kitFolders() const {
    const std::vector<std::string> names = sampleNames();
    std::vector<std::string> folders;
    for (const std::string& folder : sampleFolders())
        for (const std::string& name : names)
            if (sampleFolderOf(name) == folder) {
                folders.push_back(folder);
                break;
            }
    return folders;
}

bool Session::loadFolderKit(int track, const std::string& folder) {
    if (track < 0 || track >= kNumTracks || project.tracks[track].kind != TrackKind::Drums || folder.empty()) return false;
    std::vector<std::string> here;
    for (const std::string& name : sampleNames())
        if (sampleFolderOf(name) == folder) here.push_back(name); // (in name order: wavNames sorts each folder's)
    if (here.empty()) return false;
    std::string lanes[kDrumLanes];
    folderKitLanes(here, lanes);
    std::unique_ptr<TrackData> kit(new TrackData); // (every lane field at a new track's)
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        std::snprintf(kit->laneSample[lane], sizeof kit->laneSample[lane], "%s", lanes[lane].c_str());
        kit->laneChoke[lane] = static_cast<std::uint8_t>((lane == 2 || lane == 3) && folderKitHat(lanes[lane]) ? 1 : kChokeNone);
    }
    if (!putKit(track, *kit)) return false;
    folderKits_[track] = {folder, kitText(project.tracks[track])};
    return true;
}

std::string Session::folderKitOf(int track) const {
    if (track < 0 || track >= kNumTracks || folderKits_[track].folder.empty() || project.tracks[track].kind != TrackKind::Drums) return "";
    return kitText(project.tracks[track]) == folderKits_[track].text ? folderKits_[track].folder : "";
}

bool Session::deleteKit(int index) {
    const std::vector<Kit>& all = kits();
    if (index < 0 || index >= static_cast<int>(all.size()) || !all[static_cast<std::size_t>(index)].own || presetFolder_.empty()) return false;
    const bool gone = std::remove((presetFolder_ + "/" + all[static_cast<std::size_t>(index)].file).c_str()) == 0;
    readKits();
    return gone;
}

} // namespace apotheca
