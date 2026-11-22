// Session: songs on disk, revisions, and the handheld's own settings files.
#include "session.hpp"
#include "instruments.hpp"
#include "ui.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <utime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <memory>

#include "samples.hpp"
#include "session_parts.hpp"
#include "song_file.hpp"
#include "stretch.hpp"
#include "synth_spec.hpp"

namespace apotheca {

namespace {
// New songs are named with two words, so nobody has to type on a D-pad.
const char* const kFirstWords[] = {"brass", "salt", "amber", "ash", "copper", "glass", "iron", "lunar", "pale", "quiet", "red", "silver",
                                   "slow", "smoke", "tin", "velvet", "wax", "black", "green", "hollow", "mercury", "night", "old", "vitriol"};
const char* const kSecondWords[] = {"moth", "lantern", "furnace", "garden", "mirror", "river", "vessel", "key", "crow", "flask", "orchard", "tower",
                                    "engine", "lion", "salt", "window", "road", "bell", "serpent", "star", "well", "ember", "hour", "stone"};
}

std::string Session::pathOf(const std::string& name) const { return folder_ + "/" + name + ".song"; }

std::string Session::freshName() {
    const int firsts = sizeof kFirstWords / sizeof kFirstWords[0], seconds = sizeof kSecondWords / sizeof kSecondWords[0];
    for (int attempt = 0; attempt < 2000; ++attempt) {
        nameSeed_ = nameSeed_ * 1664525u + 1013904223u;
        std::string name = std::string(kFirstWords[(nameSeed_ >> 8) % firsts]) + " " + kSecondWords[(nameSeed_ >> 16) % seconds];
        if (attempt >= 1000) name += " " + std::to_string(attempt - 999); // every pair taken: number them
        if (folder_.empty() || !fileExists(pathOf(name))) return name;
    }
    return "untitled";
}

void Session::useSongFolder(const std::string& folder, std::uint32_t seed) {
    folder_ = folder;
    nameSeed_ = seed | 1u;
    mkdir(folder_.c_str(), 0755); // fine if it is already there
    follows_.clear();
    songName_.clear(); // (no song is open while what is owed is finished: every saved one is gone through)
    finishOwed();      // a program stopped while saved songs followed a rename: finished before any is opened
    std::string last;
    if (readTextFile(folder_ + "/last.txt", last)) {
        while (!last.empty() && (last.back() == '\n' || last.back() == '\r')) last.pop_back();
        std::string text;
        if (!last.empty() && readTextFile(pathOf(last), text) && parseSong(text, project)) {
            songName_ = last;
            savedText_ = songText(project);
            applyProject();
            findMissing(); // a recording moved since: the new name is saved with the next autosave
            return;
        }
    }
    songName_ = freshName();
    savedText_.clear();
    if (project.tracks[0].kind == TrackKind::Drums) chokeNewKit(0); // the first song there is is a new song: its hats in CHOKE 1
}

std::vector<std::string> Session::songNames() const {
    std::vector<std::pair<long long, std::string>> found;
    if (DIR* dir = folder_.empty() ? nullptr : opendir(folder_.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() <= 5 || file.compare(file.size() - 5, 5, ".song") != 0) continue;
            struct stat info;
            if (stat((folder_ + "/" + file).c_str(), &info) != 0) continue;
            found.push_back({static_cast<long long>(info.st_mtime), file.substr(0, file.size() - 5)});
        }
        closedir(dir);
    }
    // Newest first; the song that is open leads when times tie.
    std::sort(found.begin(), found.end(), [this](const std::pair<long long, std::string>& a, const std::pair<long long, std::string>& b) {
        if (a.first != b.first) return a.first > b.first;
        if ((a.second == songName_) != (b.second == songName_)) return a.second == songName_;
        return a.second < b.second;
    });
    std::vector<std::string> names;
    for (const auto& song : found) names.push_back(song.second);
    return names;
}

bool Session::saveNow() {
    if (folder_.empty()) return true;
    const std::string& text = textNow(); // (made again only where a track has changed: this comes round every two seconds)
    if (text == savedText_) return true;
    // A song nobody has put anything in is not worth a file.
    // (A new song as newSong makes one, its kit's hats in CHOKE 1; or a project as made, with none.)
    static const std::string empty = [] { std::unique_ptr<Project> fresh(new Project); chokeTheHats(fresh->tracks[0]); return songText(*fresh); }();
    static const std::string plain = [] { std::unique_ptr<Project> fresh(new Project); return songText(*fresh); }();
    if ((text == empty || text == plain) && !fileExists(pathOf(songName_))) return true;
    {
        // Not while the WiFi page writes or deletes a song (Transfer::songFiles).
        std::lock_guard<std::mutex> hold(transfer_.songFiles());
        if (!writeTextFile(pathOf(songName_), text)) return false; // try again next time
    }
    savedText_ = text;
    writeTextFile(folder_ + "/last.txt", songName_ + "\n");
    return true;
}

void Session::newSong() {
    saveNow();
    std::unique_ptr<Project> fresh(new Project);
    project = *fresh;
    chokeTheHats(project.tracks[0]); // its kit's hats in CHOKE 1 (told with the song, next)
    songName_ = freshName();
    savedText_.clear();
    applyProject();
}

bool Session::openSong(const std::string& name) {
    followNow(); // what it reads has followed every rename
    saveNow();
    std::string text;
    if (folder_.empty() || !readTextFile(pathOf(name), text) || !parseSong(text, project)) return false;
    songName_ = name;
    savedText_ = songText(project);
    writeTextFile(folder_ + "/last.txt", songName_ + "\n");
    applyProject();
    findMissing(); // a recording moved while this song was shut: the new name is saved with the next autosave
    return true;
}

namespace {
// Only pairs that are both a recording's name: one sent or deleted has an empty side.
std::vector<std::pair<std::string, std::string>> goodRenames(const std::vector<std::pair<std::string, std::string>>& renames) {
    std::vector<std::pair<std::string, std::string>> good;
    for (const auto& rename : renames)
        if (sampleNameOk(rename.first) && sampleNameOk(rename.second)) good.push_back(rename);
    return good;
}
} // namespace

const char* const Session::kOwedFile = "renames-owed.txt";

std::vector<std::string> Session::songFilesToFollow() const {
    std::vector<std::string> files;
    if (DIR* dir = folder_.empty() ? nullptr : opendir(folder_.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            const bool song = file.size() > 5 && file.compare(file.size() - 5, 5, ".song") == 0;
            const bool rev = file.size() > 4 && file.compare(file.size() - 4, 4, ".rev") == 0;
            // The open song is in memory and has followed already; the autosave writes it.
            if (file[0] != '.' && (song || rev) && file != songName_ + ".song") files.push_back(file);
        }
        closedir(dir);
    }
    std::sort(files.begin(), files.end()); // (the order the card lists them in is the card's: this one is the same every time)
    return files;
}

int Session::followFile(const std::string& file, const std::vector<std::pair<std::string, std::string>>& renames) {
    const std::string path = folder_ + "/" + file;
    struct stat info;
    std::string text;
    if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || !readTextFile(path, text)) return 0;
    // Most songs do not name the recording: those are told by a plain search, and not gone through line by line
    // (with hundreds of songs the going-through froze the screen for seconds on the handheld).
    bool named = false;
    for (const auto& rename : renames) named = named || text.find(rename.first) != std::string::npos;
    if (!named || !renameRecordings(text, renames)) return 0; // only one that names it is written
    if (!writeTextFile(path, text)) return -1; // (no room on the card: it still names the old place, and whoever asked is told)
    utimbuf times;
    times.actime = info.st_atime;
    times.modtime = info.st_mtime; // the song list is in order of date, and a rev shows its own: neither moves
    utime(path.c_str(), &times);
    return 1;
}

int Session::savedSongsFollow(const std::vector<std::pair<std::string, std::string>>& renames, int* failed) {
    if (failed) *failed = 0;
    const std::vector<std::pair<std::string, std::string>> good = goodRenames(renames);
    if (folder_.empty() || good.empty()) return 0;
    // The server writes and deletes songs on its own thread: not while this is half-way through one.
    std::lock_guard<std::mutex> hold(transfer_.songFiles());
    int written = 0;
    for (const std::string& file : songFilesToFollow()) {
        const int did = followFile(file, good);
        written += did > 0;
        if (did < 0 && failed) ++*failed;
    }
    return written;
}

// What is owed, as a file: "job" before each batch of renames, then a line of each, FROM, a tab, TO.
bool Session::writeOwed() const {
    const std::string path = folder_ + "/" + kOwedFile;
    if (follows_.empty()) return std::remove(path.c_str()) == 0 || !fileExists(path);
    std::string text = "apotheca-owed 1\n";
    for (const Follow& follow : follows_) {
        text += "job\n";
        for (const auto& rename : follow.renames) text += rename.first + "\t" + rename.second + "\n";
    }
    return writeTextFile(path, text);
}

void Session::followLater(const std::vector<std::pair<std::string, std::string>>& renames) {
    Follow follow;
    follow.renames = goodRenames(renames);
    if (folder_.empty() || follow.renames.empty()) return;
    {
        std::lock_guard<std::mutex> hold(transfer_.songFiles());
        follow.files = songFilesToFollow();
    }
    if (follow.files.empty()) return;
    follows_.push_back(follow);
    if (!writeOwed()) followNow(); // nowhere to keep what is owed: done now, as before, not left to a flat battery
}

void Session::followSome() {
    if (follows_.empty()) return;
    std::lock_guard<std::mutex> hold(transfer_.songFiles());
    for (int read = 0; read < kFollowReadsAFrame && !follows_.empty();) {
        Follow& follow = follows_.front();
        if (follow.next < follow.files.size()) {
            const int did = followFile(follow.files[follow.next++], follow.renames);
            ++read;
            followFailures_ += did < 0;
            if (did != 0) break; // one written (or tried) a frame
            continue;
        }
        follows_.erase(follows_.begin());
        writeOwed(); // the rest, or nothing: the file goes
    }
    if (!follows_.empty() && follows_.front().next >= follows_.front().files.size()) {
        follows_.erase(follows_.begin());
        writeOwed();
    }
}

void Session::followNow() {
    if (follows_.empty()) return;
    {
        std::lock_guard<std::mutex> hold(transfer_.songFiles());
        for (Follow& follow : follows_)
            for (; follow.next < follow.files.size(); ++follow.next) followFailures_ += followFile(follow.files[follow.next], follow.renames) < 0;
    }
    follows_.clear();
    writeOwed();
}

// The program stopped while songs were following a rename: every batch owed, in order, through
// every saved song and rev (one already gone through no longer names the old place), then the file goes.
void Session::finishOwed() {
    const std::string path = folder_ + "/" + kOwedFile;
    std::string text;
    if (folder_.empty() || !readTextFile(path, text)) return;
    std::vector<std::vector<std::pair<std::string, std::string>>> jobs;
    std::istringstream lines(text);
    std::string line;
    bool known = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!known) {
            known = line == "apotheca-owed 1";
            if (!known) return; // not this program's words: let be
            continue;
        }
        if (line == "job") {
            jobs.emplace_back();
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || jobs.empty()) continue;
        jobs.back().push_back({line.substr(0, tab), line.substr(tab + 1)});
    }
    for (const auto& job : jobs) {
        int failed = 0;
        savedSongsFollow(job, &failed);
        followFailures_ += failed;
    }
    std::remove(path.c_str());
}

float Session::ui_stickSpeed() { return ui::gStickSpeed; }

void Session::keepSettings() const {
    if (programFolder_.empty()) return;
    writeTextFile(programFolder_ + "/settings.txt", "stick " + std::to_string(static_cast<int>(std::round(ui::gStickSpeed * 100.f))) + "\noctaves " +
                                                        std::to_string(octaves_) + "\nmidirec " + (midiRecord_ ? "1" : "0") + "\ntext " + std::to_string(textSize_) +
                                                        "\ntheme " + std::to_string(theme_) + "\nbrightness " + std::to_string(brightness_) +
                                                        "\nlight " + std::to_string(stickLight_) + "\nclick " + std::to_string(click_) +
                                                        "\ncountin " + (countIn_ ? "1" : "0") + "\n" +
                                                        (lightColour_ != kLightTheme ? "lightcolour " + std::to_string(lightColour_) + "\n" : "") +
                                                        (mutes_ != kMutesAtOnce ? "mutes " + std::to_string(mutes_) + "\n" : "") +
                                                        (ringLevel_ != kBrightUsual ? "ringlevel " + std::to_string(ringLevel_) + "\n" : ""));
}

void Session::setBrightness(int percent) {
    percent = (percent / kBrightStep) * kBrightStep; // in fives
    brightness_ = percent < kBrightLeast ? kBrightLeast : (percent > kBrightMost ? kBrightMost : percent);
    keepSettings();
}

void Session::setStickLight(int mode) {
    stickLight_ = mode < 0 ? 0 : (mode >= kRingModes ? kRingModes - 1 : mode); // a choice with ends, as THEME
    keepSettings();
}

void Session::setLightColour(int which) {
    lightColour_ = which < 0 ? 0 : (which >= kLightColours ? kLightColours - 1 : which); // a choice with ends
    keepSettings();
}

void Session::setRingLevel(int percent) {
    percent = (percent / kBrightStep) * kBrightStep; // in fives, as BRIGHTNESS
    ringLevel_ = percent < kBrightLeast ? kBrightLeast : (percent > kBrightMost ? kBrightMost : percent);
    keepSettings();
}

RingFrame Session::ringNow() const {
    static_assert(kRingThemes == kThemes, "a new theme is given its ring colour (ringThemeColour, lights.hpp)");
    RingPlace at;
    if (project.playing && project.playStep >= 0) {
        at.step = project.playStep;
        at.sceneStep = (project.playBar < 0 ? 0 : project.playBar) * kSteps + project.playStep;
        at.sceneSteps = project.playScene >= 0 ? project.sceneSteps(project.playScene) : kSteps;
    }
    Rgb colours[kRingLeds];
    const int beats = at.sceneStep < 0 ? -1 : (project.playRepeat * at.sceneSteps + at.sceneStep) / 4;
    ringColours(lightColour_, ringThemeColour(theme_), project.track, beats, colours);
    // A bar's first step in brass; in white where the ring is too near brass to tell (RAINBOW and TRACK: brass).
    const Rgb first = lightColour_ == kLightTrack || lightColour_ == kLightRainbow ? lightColourOf(kLightBrass) : ringFirstFor(colours[0]);
    return ringPicture(stickLight_, ringLevel_, at, colours, first);
}

void Session::setStickSpeed(float speed) {
    if (speed != speed) return;
    speed = std::round(speed * 20.f) / 20.f; // in twentieths
    ui::gStickSpeed = speed < kStickSlowest ? kStickSlowest : (speed > kStickFastest ? kStickFastest : speed);
    keepSettings();
}

void Session::setOctavesShown(int octaves) {
    octaves_ = octaves < 1 ? 1 : (octaves > 3 ? 3 : octaves);
    keepSettings();
}

void Session::setTextSize(int size) {
    textSize_ = size < 0 ? 0 : (size >= kTextSizeCount ? kTextSizeCount - 1 : size);
    keepSettings();
}

void Session::setTheme(int theme) {
    theme_ = theme < 0 ? 0 : (theme >= kThemeCount ? kThemeCount - 1 : theme);
    keepSettings();
}

void Session::loadSettings() {
    std::string text;
    if (programFolder_.empty() || !readTextFile(programFolder_ + "/settings.txt", text)) return;
    std::istringstream lines(text);
    std::string name;
    int value = 0;
    const std::string folder = programFolder_;
    programFolder_.clear(); // reading is not a change worth writing down
    while (lines >> name >> value) {
        if (name == "stick" && value >= 25 && value <= 150) setStickSpeed(static_cast<float>(value) / 100.f);
        if (name == "octaves" && value >= 1 && value <= 3) setOctavesShown(value);
        if (name == "midirec" && (value == 0 || value == 1)) setMidiRecording(value == 1);
        if (name == "text" && value >= 0 && value < kTextSizeCount) setTextSize(value); // (a file from before the setting has no such line: LARGE)
        if (name == "theme" && value >= 0 && value < kThemeCount) setTheme(value); // (none: the look as it was built)
        if (name == "brightness" && value >= kBrightLeast && value <= kBrightMost) setBrightness(value); // (none: kBrightUsual)
        if (name == "light" && value >= 0 && value < kRingModes) setStickLight(value); // (none: ON)
        if (name == "click" && value >= 0 && value < kClickModes) setClick(value); // (none: OFF)
        if (name == "countin" && (value == 0 || value == 1)) setCountIn(value == 1);
        if (name == "mutes" && value >= 0 && value < kMutesModes) setMutes(value); // (none: AT ONCE)
        if (name == "lightcolour" && value >= 0 && value < kLightColours) setLightColour(value); // (none: THEME)
        if (name == "ringlevel" && value >= kBrightLeast && value <= kBrightMost) setRingLevel(value); // (none: 70, as the screen)
    }
    programFolder_ = folder;
}

void Session::loadVolume(float ifNone) {
    std::string text;
    float volume = ifNone;
    if (!programFolder_.empty() && readTextFile(programFolder_ + "/volume.txt", text) && !text.empty()) {
        const int percent = std::atoi(text.c_str());
        if (percent >= 0 && percent <= 100) volume = static_cast<float>(percent) / 100.f;
    }
    const std::string folder = programFolder_;
    programFolder_.clear(); // setting it now is not a change worth writing down
    setVolume(volume);
    programFolder_ = folder;
}

void Session::setTempo(float bpm) {
    bpm = std::round(bpm != bpm ? project.tempo : bpm);
    bpm = bpm < kMinTempo ? kMinTempo : (bpm > kMaxTempo ? kMaxTempo : bpm);
    if (bpm == project.tempo) return;
    project.tempo = bpm;
    Command c;
    c.type = Command::SetTempo;
    c.value = bpm;
    engine.send(c);
}

void Session::setSceneTempo(int scene, float bpm) {
    if (scene < 0 || scene >= kMaxScenes) return;
    bpm = bpm != bpm ? project.sceneTempo[scene] : std::round(bpm);
    bpm = bpm < 1.f ? 0.f : (bpm < kMinTempo ? kMinTempo : (bpm > kMaxTempo ? kMaxTempo : bpm));
    if (bpm == project.sceneTempo[scene]) return;
    project.sceneTempo[scene] = bpm;
    noteEdit(); // a step of undo: undo restores it, so changing it must mark one
    Command c;
    c.type = Command::SetSceneTempo;
    c.scene = scene;
    c.value = bpm;
    engine.send(c);
}

void Session::setSceneMeter(int scene, int steps) {
    if (scene < 0 || scene >= kMaxScenes) return;
    steps = steps < 1 ? 1 : (steps > kSteps ? kSteps : steps);
    if (steps == project.sceneMeter[scene]) return;
    project.sceneMeter[scene] = steps;
    noteEdit(); // a step of undo: undo restores it, so changing it must mark one
    Command c;
    c.type = Command::SetSceneMeter;
    c.scene = scene;
    c.a = steps;
    engine.send(c);
}

void Session::tapTempo(Uint32 ms) {
    const float bpm = taps_.tap(ms);
    char text[48];
    if (bpm <= 0.f) return; // the first of a run says nothing (A held to turn the tempo begins with one)
    setTempo(bpm);
    std::snprintf(text, sizeof text, "TAPPED: %d BPM", static_cast<int>(project.tempo));
    say(text);
}

std::string Session::renameSong() {
    if (folder_.empty()) return songName_;
    followNow(); // its revs are on the list under their old names
    saveNow();
    const std::vector<int> have = revs(); // under the old name
    const std::string old = songName_, fresh = freshName();
    if (fileExists(pathOf(old)) && std::rename(pathOf(old).c_str(), pathOf(fresh).c_str()) != 0) return songName_;
    for (int rev : have) {
        const std::string from = revPath(rev);
        songName_ = fresh;
        std::rename(from.c_str(), revPath(rev).c_str());
        songName_ = old;
    }
    songName_ = fresh;
    writeTextFile(folder_ + "/last.txt", songName_ + "\n");
    return songName_;
}

bool Session::deleteSong(const std::string& name) {
    if (folder_.empty() || name.empty() || name.find('/') != std::string::npos || !fileExists(pathOf(name))) return false;
    followNow(); // (one in "deleted" is never gone through: it goes there having followed)
    const bool moved = binSong(folder_, name); // the song and its revs, into "deleted"; never over another
    if (name == songName_) {
        // The open one: a new song takes its place. Nothing is saved over what was moved.
        std::unique_ptr<Project> fresh(new Project);
        project = *fresh;
        songName_ = freshName();
        savedText_.clear();
        applyProject();
    }
    return moved;
}

// ---- revs ---------------------------------------------------------------------------
// Kept as "<song name>.<number>.rev" in the song folder: the same text as a
// song file, under a name the song list does not pick up.

std::string Session::revPath(int rev) const { return folder_ + "/" + songName_ + "." + std::to_string(rev) + ".rev"; }

std::vector<int> Session::revs() const {
    std::vector<int> found;
    const std::string front = songName_ + ".";
    if (DIR* dir = folder_.empty() ? nullptr : opendir(folder_.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() <= front.size() + 4 || file.compare(0, front.size(), front) != 0) continue;
            if (file.compare(file.size() - 4, 4, ".rev") != 0) continue;
            const std::string number = file.substr(front.size(), file.size() - front.size() - 4);
            if (number.empty() || number.size() > 6 || number.find_first_not_of("0123456789") != std::string::npos) continue;
            const int rev = std::atoi(number.c_str());
            if (rev >= 1) found.push_back(rev);
        }
        closedir(dir);
    }
    std::sort(found.begin(), found.end(), [](int a, int b) { return a > b; });
    return found;
}

int Session::saveRev() {
    if (folder_.empty()) return 0;
    saveNow();
    if (!fileExists(pathOf(songName_))) return 0; // an empty song that was never written: nothing to keep
    const std::vector<int> have = revs();
    const int rev = have.empty() ? 1 : have.front() + 1;
    return writeTextFile(revPath(rev), songText(project)) ? rev : 0;
}

std::string Session::revWhen(int rev) const {
    struct stat info;
    if (folder_.empty() || stat(revPath(rev).c_str(), &info) != 0) return "";
    const std::time_t at = info.st_mtime;
    char text[32] = "";
    if (const std::tm* local = std::localtime(&at)) std::strftime(text, sizeof text, "%d %b %H:%M", local);
    for (char& c : text) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return text;
}

bool Session::openRev(int rev) {
    followNow(); // what it reads has followed every rename
    std::string text;
    std::unique_ptr<Project> back(new Project);
    if (folder_.empty() || !readTextFile(revPath(rev), text) || !parseSong(text, *back)) return false;
    // Keep where the song is now, unless a rev already holds exactly that.
    const std::string now = songText(project);
    bool kept = false; // the rev being opened is one of these, so "already there" is covered
    for (int other : revs()) {
        std::string theirs;
        if (kept) break;
        kept = readTextFile(revPath(other), theirs) && theirs == now;
    }
    if (!kept && saveRev() == 0) return false; // could not keep it: do not throw it away
    project = *back;
    applyProject();
    findMissing(); // a rev may ask for a recording that has moved since
    saveNow();
    return true;
}

} // namespace apotheca
