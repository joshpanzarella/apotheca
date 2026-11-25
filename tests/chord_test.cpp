// Tests for chords: reading a pack, working out a chord's notes, and the Y
// button on the note screen. No window and no sound card.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>

#include <algorithm>
#include <initializer_list>

#include "app.hpp"
#include "canvas.hpp"
#include "chords.hpp"
#include "song_file.hpp"
#include "temp_folder.hpp"

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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, START = SDLK_RETURN;
const SDL_Keycode L1 = SDLK_q, R1 = SDLK_w;
const SDL_Keycode STICK_UP = SDLK_i, STICK_RIGHT = SDLK_l, STICK_LEFT = SDLK_j, STICK_DOWN = SDLK_k;

// A handheld with one Phazed track, on its note screen.
struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    Rig() {
        app.session().addTrack(TrackKind::Phazed);
        app.project().track = 1;
        app.session().view = View::Pattern;
        notes().moveCursorTo(60, 0);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) { for (int i = 0; i < times; ++i) { key(code, true); key(code, false); } }
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    std::vector<float> render(int audioFrames) {
        std::vector<float> out(2 * audioFrames);
        engine->render(out.data(), audioFrames);
        return out;
    }
    float listen(int audioFrames = 4000) {
        float p = 0.f;
        for (float s : render(audioFrames)) p = std::fabs(s) > p ? std::fabs(s) : p;
        return p;
    }
    // Let anything that was played to be heard die away.
    void quiet() {
        frames(40);
        for (int i = 0; i < 40 && listen(48000) > 1e-5f; ++i) {}
    }
    NoteScreen& notes() { return app.notes(); }
    NotePattern& pat() { return app.project().tracks[1].notes[0][0]; }
    const ChordEvent* chordOn(int step) { const int i = pat().chordAt(step); return i < 0 ? nullptr : &pat().chords[i]; }
};

bool keysAre(const ChordEvent& c, std::vector<int> want) {
    int keys[kChordNotes];
    const int n = c.pitches(keys);
    return std::vector<int>(keys, keys + n) == want;
}

ChordEvent chord(int root, std::vector<int> intervals, int inversion = 0) {
    ChordEvent c;
    c.root = root;
    c.inversion = inversion;
    c.length = 1;
    c.velocity = 1.f;
    for (int v : intervals) c.intervals[c.count++] = v;
    return c;
}

const char* kPack = R"json({
  "schemaVersion": 2, "name": "Test \"Pack\"", "sourceTonic": 9,
  "scale": {"id": "major", "pitchClasses": [0, 2, 4, 5, 7, 9, 11]},
  "legacy": {"deep": [[1, 2], [3, {"x": null, "y": true, "z": false}]]},
  "chords": [
    {"legacyName": "Amaj7", "root": {"degree": 0, "alteration": 0}, "intervals": [0, 4, 7, 11]},
    {"legacyName": "F#m7", "root": {"alteration": 0, "degree": 5}, "intervals": [0, 3, 7, 10]},
    {"quality": "major(open)", "root": {"degree": 3, "alteration": 0}, "intervals": [0, 7, 16]},
    {"quality": "minor7", "root": {"degree": 6, "alteration": -1}, "intervals": [0, 3, 7, 10]},
    {"quality": "sus2", "root": {"degree": 1, "alteration": 2}, "intervals": [0, 2, 7]},
    {"legacyName": "bad interval", "root": {"degree": 0, "alteration": 0}, "intervals": [0, -4, 7]},
    {"legacyName": "bad degree", "root": {"degree": 7, "alteration": 0}, "intervals": [0, 4, 7]},
    {"legacyName": "bad alteration", "root": {"degree": 0, "alteration": 3}, "intervals": [0, 4, 7]},
    {"legacyName": "too many", "root": {"degree": 0, "alteration": 0}, "intervals": [0, 1, 2, 3, 4, 5, 6, 7, 8]},
    {"legacyName": "half", "root": {"degree": 0, "alteration": 0}, "intervals": [0, 3.5, 7]},
    {"legacyName": "none", "root": {"degree": 0, "alteration": 0}, "intervals": []},
    {"legacyName": "A very long chord name indeed", "root": {"degree": 0, "alteration": 0}, "intervals": [0, 1, 2, 3, 4, 5, 6, 7]}
  ]
})json";

void testReadingAPack() {
    ChordPack pack;
    CHECK(parseChordPack(kPack, pack));
    CHECK(pack.name == "Test \"Pack\"");
    CHECK(pack.chords.size() == 6); // the six that make sense
    if (pack.chords.size() != 6) return;
    // A major: the root is the key plus the scale's note plus the alteration.
    CHECK(std::strcmp(pack.chords[0].name, "Amaj7") == 0 && pack.chords[0].rootKey == 9 && pack.chords[0].count == 4);
    CHECK(pack.chords[0].intervals[1] == 4 && pack.chords[0].intervals[3] == 11);
    CHECK(std::strcmp(pack.chords[1].name, "F#m7") == 0 && pack.chords[1].rootKey == 6);
    // No name given: the root's name and the quality, "major" left unsaid.
    CHECK(std::strcmp(pack.chords[2].name, "D(open)") == 0 && pack.chords[2].rootKey == 2);
    CHECK(std::strcmp(pack.chords[3].name, "Gm7") == 0 && pack.chords[3].rootKey == 7); // G#, a semitone down
    CHECK(std::strcmp(pack.chords[4].name, "C#sus2") == 0 && pack.chords[4].rootKey == 1); // B, two up, comes round
    CHECK(std::strlen(pack.chords[5].name) == 15 && pack.chords[5].count == 8);
    CHECK(std::strchr(pack.chords[5].name, ' ') == nullptr); // one word, for the song file

    // Things that are not packs leave the pack alone.
    ChordPack same = pack;
    const char* bad[] = {"", "nonsense", "[1, 2]", "{\"chords\": 3}", "{\"chords\": []}", "{\"name\": \"x\"}",
                         "{\"chords\": [{\"intervals\": [0, 4, 7]}]} trailing", "{\"chords\": [{\"intervals\": [0, 4, 7]}]",
                         "{\"chords\": [{\"intervals\": [0, 4, 7,]}]}", "{\"chords\" [{\"intervals\": [0]}]}",
                         "{\"chords\": [{\"intervals\": [0, 4, 7]}], \"name\": \"unfinished}"};
    for (const char* text : bad) {
        CHECK(!parseChordPack(text, same));
        CHECK(same.name == pack.name && same.chords.size() == 6);
    }
    // Too deep to be real: refused, not a crash.
    CHECK(!parseChordPack(std::string(100000, '['), same));
    CHECK(!parseChordPack("{\"chords\": [{\"intervals\": [0]}], \"x\": " + std::string(40, '[') + std::string(40, ']') + "}", same));
    CHECK(parseChordPack("{\"chords\": [{\"intervals\": [0]}], \"x\": " + std::string(20, '[') + std::string(20, ']') + "}", same));

    // The least a pack can be: C major assumed, and a name made up.
    ChordPack least;
    CHECK(parseChordPack("{\"chords\":[{\"intervals\":[0,4,7],\"root\":{\"degree\":4}}]}", least));
    CHECK(least.name == "pack" && least.chords.size() == 1 && least.chords[0].rootKey == 7);
    CHECK(std::strcmp(least.chords[0].name, "G") == 0);
}

void testThePacksThatShip() {
    const std::vector<ChordPack> packs = loadChordPacks(APOTHECA_SOURCE_DIR "/assets/chords");
    CHECK(packs.size() == 20); // the built-in one and nineteen files
    CHECK(packs[0].name == "Plain C" && packs[0].chords.size() == 13);
    for (std::size_t i = 0; i < packs.size(); ++i) {
        CHECK(!packs[i].name.empty() && !packs[i].chords.empty());
        if (i >= 2) CHECK(packs[i - 1].name < packs[i].name); // by name after the built-in one
        for (const ChordShape& c : packs[i].chords) CHECK(c.name[0] && c.count >= 1 && c.rootKey >= 0 && c.rootKey < 12);
    }
    // A folder that is not there still leaves the built-in pack and the shipped ones.
    CHECK(loadChordPacks("/no/such/folder").size() == 20);
    CHECK(loadChordPacks("").size() == 20);
}

std::string readFile(const std::string& path) {
    std::string text;
    CHECK(readTextFile(path, text));
    return text;
}

std::vector<std::string> packFiles() {
    std::vector<std::string> names;
    if (DIR* dir = opendir(APOTHECA_SOURCE_DIR "/assets/chords")) {
        while (const dirent* e = readdir(dir)) {
            const std::string n = e->d_name;
            if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) names.push_back(n);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool sameShape(const ChordShape& a, const ChordShape& b) {
    if (std::strcmp(a.name, b.name) != 0 || a.rootKey != b.rootKey || a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i)
        if (a.intervals[i] != b.intervals[i]) return false;
    return true;
}

// The packs compiled in are the files in assets/chords, every chord of every one (run
// tools/chord_packs.py after changing a file), so an update over WiFi brings what the zip would.
void testTheShippedPacksAreTheFiles() {
    const std::vector<std::string> files = packFiles();
    CHECK(files.size() == shippedPackTexts().size() && files.size() == shippedPacks().size());
    for (std::size_t i = 0; i < files.size() && i < shippedPackTexts().size(); ++i) {
        ChordPack fromFile, compiled;
        CHECK(parseChordPack(readFile(std::string(APOTHECA_SOURCE_DIR "/assets/chords/") + files[i]), fromFile));
        CHECK(parseChordPack(shippedPackTexts()[i], compiled)); // in file-name order
        CHECK(fromFile.name == compiled.name && fromFile.chords.size() == compiled.chords.size());
        for (std::size_t c = 0; c < fromFile.chords.size() && c < compiled.chords.size(); ++c)
            CHECK(sameShape(fromFile.chords[c], compiled.chords[c]));
        // And every fact the checks below read (the .inc is the whole file, its spaces out).
        PackFacts a, b;
        CHECK(readPackFacts(readFile(std::string(APOTHECA_SOURCE_DIR "/assets/chords/") + files[i]), a) && readPackFacts(shippedPackTexts()[i], b));
        CHECK(a.description == b.description && a.progressions.size() == b.progressions.size() && a.chords.size() == b.chords.size());
        for (std::size_t c = 0; c < a.chords.size() && c < b.chords.size(); ++c)
            CHECK(a.chords[c].quality == b.chords[c].quality && a.chords[c].outside == b.chords[c].outside && a.chords[c].legacyName == b.chords[c].legacyName);
    }
}

// A file on the card takes the place of a shipped pack of the same name; another name is one more.
void testACardPackTakesAShippedOnesPlace() {
    const std::string folder = tempFolder("chordpacks");
    const std::string mine = "{\"name\":\"Smoke\",\"chords\":[{\"intervals\":[0,4,7],\"root\":{\"degree\":0}}]}";
    const std::string other = "{\"name\":\"Zinc\",\"chords\":[{\"intervals\":[0,3,7],\"root\":{\"degree\":1}}]}";
    CHECK(writeTextFile(folder + "/smoke-mine.json", mine) && writeTextFile(folder + "/zinc.json", other));
    const std::vector<ChordPack> packs = loadChordPacks(folder);
    CHECK(packs.size() == 21);
    int smokes = 0;
    for (const ChordPack& p : packs)
        if (p.name == "Smoke") {
            ++smokes;
            CHECK(p.chords.size() == 1 && std::strcmp(p.chords[0].name, "C") == 0);
        }
    CHECK(smokes == 1 && packs.back().name == "Zinc" && packs[0].name == "Plain C");
    for (std::size_t i = 2; i < packs.size(); ++i) CHECK(packs[i - 1].name < packs[i].name);
}

// ---- every shipped chord is what it says ------------------------------------------------------
// Each chord is worked out again from its quality and its root, and held to its pack's scale and
// its name. The quality's notes, as pitch classes over the root: those it must have, and those it
// may have besides (the fifth, mostly; the ninth under an eleventh or a thirteenth).

int bits(std::initializer_list<int> pcs) {
    int b = 0;
    for (int pc : pcs) b |= 1 << pc;
    return b;
}

struct Quality {
    const char* name;
    int must, may;
};

const Quality kQualities[] = {
    {"major", bits({0, 4, 7}), 0},          {"m", bits({0, 3, 7}), 0},
    {"dim", bits({0, 3, 6}), 0},            {"aug", bits({0, 4, 8}), 0},
    {"5", bits({0, 7}), 0},                 {"sus2", bits({0, 2, 7}), 0},
    {"sus4", bits({0, 5, 7}), 0},           {"sus2add11", bits({0, 2, 5, 7}), 0},
    {"sus4add9", bits({0, 2, 5, 7}), 0},    {"add9", bits({0, 2, 4}), bits({7})},
    {"madd9", bits({0, 2, 3}), bits({7})},  {"madd11", bits({0, 3, 5}), bits({7})},
    {"6", bits({0, 4, 9}), bits({7})},      {"m6", bits({0, 3, 9}), bits({7})},
    {"6/9", bits({0, 2, 4, 9}), bits({7})}, {"m6/9", bits({0, 2, 3, 9}), bits({7})},
    {"7", bits({0, 4, 10}), bits({7})},     {"maj7", bits({0, 4, 11}), bits({7})},
    {"m7", bits({0, 3, 10}), bits({7})},    {"mMaj7", bits({0, 3, 11}), bits({7})},
    {"m7b5", bits({0, 3, 6, 10}), 0},       {"dim7", bits({0, 3, 6, 9}), 0},
    {"7sus4", bits({0, 5, 10}), bits({7})}, {"7#5", bits({0, 4, 8, 10}), 0},
    {"maj7#5", bits({0, 4, 8, 11}), bits({2})},
    {"9", bits({0, 2, 4, 10}), bits({7})},  {"maj9", bits({0, 2, 4, 11}), bits({7})},
    {"m9", bits({0, 2, 3, 10}), bits({7})}, {"mMaj9", bits({0, 2, 3, 11}), bits({7})},
    {"9sus4", bits({0, 2, 5, 10}), bits({7})},
    {"m11", bits({0, 3, 5, 10}), bits({2, 7})},
    {"maj7#11", bits({0, 4, 6, 11}), bits({2, 7})},
    {"maj9#11", bits({0, 2, 4, 6, 11}), bits({7})},
    {"13", bits({0, 4, 9, 10}), bits({2, 7})},
    {"maj13", bits({0, 4, 9, 11}), bits({2, 7})},
    {"maj7add13", bits({0, 4, 9, 11}), bits({7})},
    {"13sus4", bits({0, 5, 9, 10}), bits({2, 7})},
    {"7b9", bits({0, 1, 4, 10}), bits({7})},
    {"7#9", bits({0, 3, 4, 10}), bits({7})},
    {"7#9b13", bits({0, 3, 4, 8, 10}), 0},
    {"7sus4b9", bits({0, 1, 5, 10}), bits({7})},
    {"quartal", bits({0, 5, 10}), 0},       {"quartal4", bits({0, 3, 5, 10}), 0},
    {"quartal5", bits({0, 3, 5, 8, 10}), 0},
};

// A quality's notes: "(open)" is a voicing and changes none; "(no3)", "no3" and "(no5)" take the
// third or the fifth out, and then it must not be there. False: a quality nobody knows.
bool qualityNotes(std::string q, int& must, int& may) {
    int without = 0;
    const auto ends = [&](const char* tail) {
        const std::size_t n = std::strlen(tail);
        if (q.size() <= n || q.compare(q.size() - n, n, tail) != 0) return false;
        q.erase(q.size() - n);
        return true;
    };
    ends("(open)");
    if (ends("(no3)") || ends("no3")) without = bits({3, 4});
    else if (ends("(no5)")) without = bits({7});
    for (const Quality& known : kQualities)
        if (q == known.name) {
            must = known.must & ~without;
            may = known.may & ~without;
            return true;
        }
    return false;
}

const char* const kKeys[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

// The root spelled as its scale spells it: the degree's own letter (in a seven-note scale), with
// what the alteration takes it to. A flat second is Db, never C#.
std::string spelled(const PackFacts& pack, const PackFacts::Chord& c) {
    const int pc = ((pack.tonic + pack.scale[static_cast<std::size_t>(c.degree)] + c.alteration) % 12 + 12) % 12;
    if (pack.scale.size() != 7) return kKeys[pc];
    static const char* const letters = "CDEFGAB";
    static const int natural[7] = {0, 2, 4, 5, 7, 9, 11};
    const int tonicLetter = static_cast<int>(std::strchr(letters, kKeys[pack.tonic][0]) - letters);
    const int letter = (tonicLetter + c.degree) % 7;
    const int accidental = ((pc - natural[letter]) % 12 + 18) % 12 - 6;
    if (accidental < -2 || accidental > 2) return "?";
    std::string out(1, letters[letter]);
    out.append(static_cast<std::size_t>(accidental < 0 ? -accidental : accidental), accidental < 0 ? 'b' : '#');
    return out;
}

std::string shownQuality(const std::string& q) { // as parseChordPack names a chord
    if (q.compare(0, 5, "major") == 0) return q.substr(5);
    if (q.compare(0, 5, "minor") == 0) return "m" + q.substr(5);
    return q;
}

bool isNoteName(const std::string& s) {
    return (s.size() == 1 || (s.size() == 2 && (s[1] == 'b' || s[1] == '#'))) && s[0] >= 'A' && s[0] <= 'G';
}

// Everything wrong with a pack, one line each; none for a good one.
std::vector<std::string> packFaults(const std::string& json) {
    std::vector<std::string> faults;
    const auto fault = [&](const std::string& where, const std::string& what) { faults.push_back(where + ": " + what); };
    PackFacts pack;
    if (!readPackFacts(json, pack)) {
        fault("pack", "not a pack");
        return faults;
    }
    if (pack.schemaVersion != 2) fault(pack.name, "schemaVersion is not 2");
    if (pack.tonic < 0 || pack.tonic > 11) fault(pack.name, "no tonic");
    if (pack.name.empty() || pack.name.size() > 12) fault(pack.name, "a name of 1 to 12 letters");
    if (pack.description.empty() || pack.description.size() > 200) fault(pack.name, "a description of 1 to 200 letters");
    if (pack.scale.size() < 5 || pack.scale[0] != 0) fault(pack.name, "the scale");
    for (std::size_t i = 1; i < pack.scale.size(); ++i)
        if (pack.scale[i] <= pack.scale[i - 1] || pack.scale[i] > 11) fault(pack.name, "the scale");
    if (pack.chords.size() < 7 || pack.chords.size() > 24) fault(pack.name, "7 to 24 chords");
    if (!faults.empty()) return faults;
    int scaleBits = 0;
    for (int pc : pack.scale) scaleBits |= 1 << pc;
    std::vector<std::string> ids, names;
    std::vector<std::vector<int>> shapes;
    const std::string idStart = pack.id.substr(pack.id.find('.') + 1) + ".";
    for (const PackFacts::Chord& c : pack.chords) {
        const std::string at = pack.name + " " + c.id;
        if (c.id.compare(0, idStart.size(), idStart) != 0) fault(at, "an id that does not start with the pack's");
        if (std::find(ids.begin(), ids.end(), c.id) != ids.end()) fault(at, "an id twice");
        ids.push_back(c.id);
        // Voices: 1 to 6, the root at the bottom, rising, in two octaves; a pedal is a voice too.
        const std::size_t voices = c.intervals.size() + (c.pedal >= 0 ? 1 : 0);
        if (c.intervals.empty() || voices > 6) fault(at, "1 to 6 voices");
        if (c.intervals.empty() || c.intervals[0] != 0) fault(at, "the root at the bottom");
        for (std::size_t i = 1; i < c.intervals.size(); ++i)
            if (c.intervals[i] <= c.intervals[i - 1] || c.intervals[i] > 24) fault(at, "rising, two octaves at most");
        for (int r : c.required)
            if (std::find(c.intervals.begin(), c.intervals.end(), r) == c.intervals.end()) fault(at, "a required note it does not have");
        if (c.preferredVoices < static_cast<int>(c.required.size()) || c.preferredVoices > 6) fault(at, "preferredVoices");
        if (c.function != "tonic" && c.function != "predominant" && c.function != "dominant" && c.function != "unknown") fault(at, "its function");
        if (c.degree < 0 || c.degree >= static_cast<int>(pack.scale.size()) || c.alteration < -2 || c.alteration > 2) {
            fault(at, "its root");
            continue;
        }
        // The quality: every note it must have, and nothing it may not.
        int must = 0, may = 0, has = 0;
        if (!qualityNotes(c.quality, must, may)) fault(at, "a quality nobody knows: " + c.quality);
        else {
            for (int i : c.intervals) has |= 1 << (i % 12);
            if ((has & must) != must) fault(at, "a note of " + c.quality + " missing");
            if (has & ~(must | may)) fault(at, "a note that is not " + c.quality);
        }
        // The scale: a note outside it is borrowed, says so, and says which.
        const int root = ((pack.scale[static_cast<std::size_t>(c.degree)] + c.alteration) % 12 + 12) % 12; // over the tonic
        std::vector<int> outside;
        for (int i : c.intervals) {
            const int pc = (root + i) % 12;
            if (!(scaleBits & (1 << pc)) && std::find(outside.begin(), outside.end(), pc) == outside.end()) outside.push_back(pc);
        }
        std::sort(outside.begin(), outside.end());
        if (!outside.empty() && c.borrowedFrom.empty()) fault(at, "notes outside the scale, not borrowed");
        if (outside != c.outside || (!outside.empty() && !c.hasOutside)) fault(at, "outside is not the notes outside the scale");
        // The name the strip shows: the root as the scale spells it, and the quality.
        const std::string spelling = spelled(pack, c), expected = spelling + shownQuality(c.quality);
        const std::string automatic = kKeys[(pack.tonic + root) % 12] + shownQuality(c.quality);
        std::string name = c.legacyName.empty() ? automatic : c.legacyName;
        const std::size_t slash = name.rfind('/');
        if (slash != std::string::npos && isNoteName(name.substr(slash + 1))) {
            if (name.substr(slash + 1) != spelling) fault(at, "a slash chord whose bass is not its root");
        } else if (name != expected) {
            fault(at, "named " + name + ", spelled " + expected + (c.legacyName.empty() ? " (give it a legacyName)" : ""));
        }
        if (name.size() > 15) fault(at, "a name longer than 15 letters");
        if (std::find(names.begin(), names.end(), name) != names.end()) fault(at, "a name twice: " + name);
        names.push_back(name);
        std::vector<int> shape = c.intervals;
        shape.insert(shape.begin(), root);
        if (std::find(shapes.begin(), shapes.end(), shape) != shapes.end()) fault(at, "the same chord twice");
        shapes.push_back(shape);
    }
    std::vector<std::string> progressions;
    if (pack.progressions.size() > 4) fault(pack.name, "more than 4 progressions");
    for (const PackFacts::Progression& p : pack.progressions) {
        if (p.id.compare(0, idStart.size(), idStart) != 0 || std::find(progressions.begin(), progressions.end(), p.id) != progressions.end())
            fault(pack.name + " " + p.id, "its id");
        progressions.push_back(p.id);
        if (p.segments.empty() || p.segments.size() > 64) fault(pack.name + " " + p.id, "1 to 64 chords");
        for (const auto& segment : p.segments) {
            if (std::find(ids.begin(), ids.end(), segment.first) == ids.end()) fault(pack.name + " " + p.id, "no chord " + segment.first);
            if (segment.second <= 0) fault(pack.name + " " + p.id, "a length that is not one");
        }
    }
    return faults;
}

void testEveryShippedChordIsWhatItSays() {
    std::vector<std::string> names;
    for (const std::string& file : packFiles()) {
        const std::string text = readFile(std::string(APOTHECA_SOURCE_DIR "/assets/chords/") + file);
        for (const std::string& f : packFaults(text)) {
            std::printf("  %s: %s\n", file.c_str(), f.c_str());
            CHECK(f.empty());
        }
        PackFacts pack;
        CHECK(readPackFacts(text, pack));
        CHECK(pack.id == "authored." + file.substr(0, file.size() - 5)); // the file is named after the pack
        CHECK(std::find(names.begin(), names.end(), pack.name) == names.end() && pack.name != builtInPack().name);
        names.push_back(pack.name);
    }
    CHECK(names.size() == 19);

    // And the checks catch what they are for: each fault planted in a good pack is found.
    const std::string good = shippedPackTexts().size() > 13 ? shippedPackTexts()[13] : std::string(); // smoke.json
    CHECK(good.find("\"name\":\"Smoke\"") != std::string::npos && packFaults(good).empty());
    struct Plant {
        const char* what;
        const char* from;
        const char* to;
        const char* found;
    };
    const Plant plants[] = {
        {"a thirteenth made a flat thirteenth", ".V13\",\"quality\":\"13\",\"root\":{\"degree\":4,\"alteration\":0},\"intervals\":[0,4,10,14,21]",
         ".V13\",\"quality\":\"13\",\"root\":{\"degree\":4,\"alteration\":0},\"intervals\":[0,4,10,14,20]", "a note"},
        {"a major seventh called a seventh", ".Imaj7\",\"quality\":\"maj7\"", ".Imaj7\",\"quality\":\"7\"", "not 7"},
        {"the ii on the wrong degree", ".iim9\",\"quality\":\"m9\",\"root\":{\"degree\":1", ".iim9\",\"quality\":\"m9\",\"root\":{\"degree\":2", "outside the scale"},
        {"a flat second left to be named C#", ",\"legacyName\":\"Db9\"", "", "give it a legacyName"},
        {"a borrowed note not listed", "\"outside\":[8]", "\"outside\":[]", "outside is not"},
        {"a chord twice", "\"id\":\"smoke.iim7\"", "\"id\":\"smoke.iim9\"", "an id twice"},
        {"a progression to no chord", "\"chordId\":\"smoke.V13\"", "\"chordId\":\"smoke.V14\"", "no chord"},
        {"seven voices", "[0,4,9,14,19]", "[0,4,9,14,19,21,23]", "1 to 6 voices"},
        {"a quality nobody knows", "\"quality\":\"m7b5\"", "\"quality\":\"half\"", "nobody knows"},
        {"a slash chord on another bass", "\"legacyName\":\"Db9\"", "\"legacyName\":\"F/G\"", "slash chord"},
    };
    for (const Plant& plant : plants) {
        std::string bad = good;
        const std::size_t at = bad.find(plant.from);
        CHECK(at != std::string::npos);
        if (at == std::string::npos) continue;
        bad.replace(at, std::strlen(plant.from), plant.to);
        bool found = false;
        for (const std::string& f : packFaults(bad)) found = found || f.find(plant.found) != std::string::npos;
        if (!found) std::printf("  not caught: %s\n", plant.what);
        CHECK(found);
    }
}

// Every chord's name fits its tile on the strip, and every pack's name fits between L1 and R1
// before the count (note_screen.cpp: tiles 88 across, the count at 300). The strip is a tight
// row, the same at every TEXT SIZE.
void testTheNamesFitTheStrip() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    const Font& small = canvas.fonts->fixed().small;
    float widest = 0.f;
    for (const ChordPack& pack : loadChordPacks("")) {
        std::string upper = pack.name;
        for (char& ch : upper) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
        CHECK(8.f + small.width("L1") + 6.f + small.width(upper.c_str()) + 6.f + small.width("R1") <= 294.f);
        for (const ChordShape& c : pack.chords) {
            widest = std::max(widest, small.width(c.name));
            if (small.width(c.name) > 82.f) std::printf("  too wide: %s\n", c.name);
            CHECK(small.width(c.name) <= 82.f);
        }
    }
    CHECK(widest > 40.f); // (measured, not nothing)
}

// The picking strip: the inversion stands clear of the count and the right-hand words.
void testTheInversionStandsClear() {
    Canvas canvas;
    CHECK(canvas.ok);
    if (!canvas.ok) return;
    Rig rig;
    rig.key(Y, true);
    rig.tap(SDLK_UP, 3); // inversion +3: the widest it reads
    CHECK(rig.notes().picking());
    DrawLog drawn;
    canvas.clear();
    gDrawLog = &drawn;
    rig.app.draw(canvas.r, *canvas.fonts);
    gDrawLog = nullptr;
    const DrawLog::Text* inv = nullptr, *letgo = nullptr, *count = nullptr;
    for (const DrawLog::Text& t : drawn.texts) {
        if (t.words.compare(0, 3, "INV") == 0) inv = &t;
        if (t.words == "LET GO OF Y TO PLACE") letgo = &t;
        if (!t.words.empty() && t.words.find_first_not_of("0123456789 /") == std::string::npos) count = &t;
    }
    CHECK(inv && letgo && count);
    if (inv && letgo && count) {
        if (count->x + count->w > inv->x) std::printf("  count ends %.0f, inversion starts %.0f\n", (double)(count->x + count->w), (double)inv->x);
        if (inv->x + inv->w > letgo->x) std::printf("  inversion ends %.0f, words start %.0f\n", (double)(inv->x + inv->w), (double)letgo->x);
        CHECK(count->x + count->w <= inv->x);
        CHECK(inv->x + inv->w <= letgo->x);
    }
    rig.key(Y, false);
}

void testAChordsNotes() {
    CHECK(keysAre(chord(60, {0, 4, 7}), {60, 64, 67}));
    CHECK(keysAre(chord(60, {7, 0, 4}), {60, 64, 67})); // lowest first, however it was written
    // Inversions: the bottom goes up an octave, or the top comes down.
    CHECK(keysAre(chord(60, {0, 4, 7}, 1), {64, 67, 72}));
    CHECK(keysAre(chord(60, {0, 4, 7}, 2), {67, 72, 76}));
    CHECK(keysAre(chord(60, {0, 4, 7}, 3), {72, 76, 79}));
    CHECK(keysAre(chord(60, {0, 4, 7}, -1), {55, 60, 64}));
    CHECK(keysAre(chord(60, {0, 4, 7}, -2), {52, 55, 60}));
    // No key twice, and nothing off the ends of the keyboard.
    CHECK(keysAre(chord(60, {0, 12, 7}, 1), {67, 72}));
    CHECK(keysAre(chord(120, {0, 4, 7, 10}), {120, 124, 127}));
    CHECK(keysAre(chord(2, {0, 4, 7}, -1), {2, 6}));
    CHECK(keysAre(chord(60, {}), {}));

    // A chord from a pack lands on the nearest key of its name.
    ChordShape g;
    g.rootKey = 7;
    g.count = 3;
    g.intervals[1] = 4;
    g.intervals[2] = 7;
    std::snprintf(g.name, sizeof g.name, "G");
    CHECK(makeChord(g, 60, 3, 4, 0.5f, 0).root == 55); // G below middle C is nearer than G above
    CHECK(makeChord(g, 61, 0, 4, 0.5f, 0).root == 55); // six away: the lower one
    CHECK(makeChord(g, 62, 0, 4, 0.5f, 0).root == 67);
    CHECK(makeChord(g, 67, 0, 4, 0.5f, 0).root == 67);
    CHECK(makeChord(g, 0, 0, 4, 0.5f, 0).root == 7);
    CHECK(makeChord(g, 127, 0, 4, 0.5f, 0).root == 127);
    g.rootKey = 0;
    CHECK(makeChord(g, 127, 0, 4, 0.5f, 0).root == 120); // the C above is off the keyboard
    g.rootKey = 7;
    const ChordEvent c = makeChord(g, 60, 3, 4, 0.5f, 9);
    CHECK(c.step == 3 && c.length == 4 && c.velocity == 0.5f && c.count == 3 && c.inversion == kMaxInversion);
    CHECK(makeChord(g, 60, 3, 4, 0.5f, -9).inversion == -kMaxInversion);
    CHECK(std::strcmp(c.name, "G") == 0 && c.intervals[2] == 7);
}

void testChordsAndLooseNotesShareTheSlots() {
    NotePattern p;
    CHECK(p.empty() && p.used() == 0);
    p.notes[0].velocity = 1.f; p.notes[0].pitch = 40; p.notes[0].length = 2;
    p.notes[2].velocity = 1.f; p.notes[2].pitch = 41; p.notes[2].length = 2;
    p.chords[3] = chord(60, {0, 4, 7});
    p.chords[3].step = 4;
    p.chords[3].length = 6;
    p.chords[3].velocity = 0.6f;
    CHECK(!p.empty() && p.used() == 5);
    CHECK(p.chordAt(3) == -1 && p.chordAt(4) == 3 && p.chordAt(9) == 3 && p.chordAt(10) == -1);
    CHECK(p.freeChord() == 0);
    NoteEvent out[kMaxNotes];
    CHECK(p.render(out) == 5);
    CHECK(out[0].pitch == 40 && out[2].pitch == 41); // loose notes stay where they are
    CHECK(out[1].pitch == 60 && out[3].pitch == 64 && out[4].pitch == 67); // the chord takes the gaps
    CHECK(out[1].step == 4 && out[1].length == 6 && out[1].velocity == 0.6f);
    CHECK(out[5].velocity == 0.f);
    // Only a chord: not empty.
    NotePattern only;
    only.chords[0] = chord(60, {0, 4, 7});
    CHECK(!only.empty() && only.used() == 3);
    // Full: the chord's notes that do not fit are dropped, and nothing is overwritten.
    for (int i = 0; i < kMaxNotes - 1; ++i) { only.notes[i].velocity = 1.f; only.notes[i].pitch = 30; only.notes[i].length = 1; }
    CHECK(only.render(out) == kMaxNotes);
    CHECK(out[kMaxNotes - 1].pitch == 60 && out[kMaxNotes - 2].pitch == 30);
}

void testATapOfYPlacesTheChordInHand() {
    Rig rig;
    rig.key(Y, true);
    CHECK(rig.notes().picking() && rig.notes().chordCount() == 0); // nothing lands until Y is let go
    CHECK(rig.listen() > 0.01f); // but it is heard
    rig.key(Y, false);
    CHECK(!rig.notes().picking() && rig.notes().chordCount() == 1);
    const ChordEvent* c = rig.chordOn(0);
    CHECK(c && std::strcmp(c->name, "C") == 0 && c->root == 60 && c->step == 0 && c->length == 4 && c->velocity == 0.9f);
    CHECK(c && keysAre(*c, {60, 64, 67}));
    CHECK(rig.notes().cursorStep() == 4 && rig.notes().cursorPitch() == 60); // on to where the next one goes
    CHECK(rig.notes().noteCount() == 0 && rig.pat().used() == 3);
    // Again, and a different one: hold, right twice, let go.
    rig.tap(Y);
    CHECK(rig.chordOn(4) && std::strcmp(rig.chordOn(4)->name, "C") == 0);
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT, 2);
    CHECK(rig.notes().handIndex() == 2 && rig.notes().chordCount() == 2);
    rig.key(Y, false);
    CHECK(rig.chordOn(8) && std::strcmp(rig.chordOn(8)->name, "Em") == 0 && rig.chordOn(8)->root == 64);
    // The last four steps; then the cursor comes round to the start.
    rig.tap(Y);
    CHECK(rig.notes().chordCount() == 4 && rig.notes().cursorStep() == 0);
    CHECK(rig.chordOn(15) && rig.chordOn(15)->step == 12);

    // The engine plays them: the bar is not silent, and stops being heard when emptied.
    rig.quiet();
    rig.tap(START);
    CHECK(rig.listen(24000) > 0.01f);
}

void testPickingWithTheDPadAndTheStick() {
    Rig rig;
    rig.key(Y, true);
    rig.tap(SDLK_LEFT);
    CHECK(rig.notes().handIndex() == 12); // round to the last
    rig.tap(SDLK_RIGHT);
    CHECK(rig.notes().handIndex() == 0);
    // Inversions stop at three each way.
    rig.tap(SDLK_UP, 5);
    CHECK(rig.notes().handInversion() == 3);
    rig.tap(SDLK_DOWN, 9);
    CHECK(rig.notes().handInversion() == -3);
    rig.tap(SDLK_UP, 4);
    CHECK(rig.notes().handInversion() == 1);
    // The stick does the same: one at once, then about seven a second.
    rig.key(STICK_RIGHT, true);
    rig.frames(1);
    CHECK(rig.notes().handIndex() == 1);
    rig.frames(31); // half a second
    CHECK(rig.notes().handIndex() >= 4 && rig.notes().handIndex() <= 5);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    const int hand = rig.notes().handIndex();
    rig.key(STICK_UP, true);
    rig.frames(1);
    CHECK(rig.notes().handInversion() == 2 && rig.notes().handIndex() == hand);
    rig.key(STICK_UP, false);
    rig.frames(1);
    // A held D-pad repeats, but slowly enough to hear each chord: the cursor's wait, then the list's pace,
    // each counted from when the one before was due (ui::Repeat): the press and eight more in 60 frames.
    rig.key(SDLK_RIGHT, true);
    rig.frames(60); // about a second
    rig.key(SDLK_RIGHT, false);
    const int chords = static_cast<int>(builtInPack().chords.size()); // (the hand goes round the pack)
    CHECK((rig.notes().handIndex() - hand + chords) % chords == (1 + 1 + static_cast<int>((60 * 16 - 16 - ui::kCursorDelayMs) / ui::kListEveryMs)) % chords);
    CHECK(rig.notes().cursorStep() == 0); // the cursor has not moved through any of it
    // What lands is what was chosen.
    const int chosen = rig.notes().handIndex();
    rig.key(Y, false);
    const ChordEvent* c = rig.chordOn(0);
    CHECK(c && c->inversion == 2 && std::strcmp(c->name, builtInPack().chords[static_cast<std::size_t>(chosen)].name) == 0);
    // X hears the chord in hand again; A and R1 are not listening.
    rig.quiet();
    rig.notes().moveCursorTo(60, 8);
    rig.key(Y, true);
    rig.quiet();
    rig.tap(A);
    CHECK(rig.notes().noteCount() == 0);
    CHECK(rig.listen() < 1e-4f);
    rig.tap(X);
    CHECK(rig.listen() > 0.01f);
    rig.key(Y, false);
}

void testBCallsItOff() {
    Rig rig;
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT);
    rig.tap(B);
    CHECK(!rig.notes().picking());
    rig.key(Y, false);
    CHECK(rig.notes().chordCount() == 0 && rig.notes().cursorStep() == 0 && rig.pat().empty());
    // The chord chosen is still in hand for next time.
    rig.tap(Y);
    CHECK(rig.chordOn(0) && std::strcmp(rig.chordOn(0)->name, "Dm") == 0);
}

void testSwappingAChordInPlace() {
    Rig rig;
    rig.tap(Y);                         // C on steps 1-4
    rig.notes().moveCursorTo(72, 2);    // anywhere in its steps will do
    rig.key(A, true);                   // a loose note above it, to show it is left alone
    rig.key(A, false);
    rig.notes().moveCursorTo(50, 2);
    rig.key(Y, true);
    CHECK(rig.notes().handIndex() == 0); // starts from the chord that is there
    rig.tap(SDLK_RIGHT, 3);
    rig.tap(SDLK_UP);
    const ChordEvent* c = rig.chordOn(0);
    CHECK(c && std::strcmp(c->name, "F") == 0 && c->step == 0 && c->length == 4 && c->velocity == 0.9f && c->inversion == 1);
    CHECK(c && c->root == 65); // the F nearest the old root, not nearest the cursor
    rig.key(Y, false);
    CHECK(rig.notes().chordCount() == 1 && rig.notes().noteCount() == 1); // nothing new landed
    CHECK(rig.notes().cursorStep() == 2 && rig.notes().cursorPitch() == 50);
    // Holding Y on it again starts from its chord and inversion, whatever
    // was in hand in between.
    rig.notes().moveCursorTo(50, 9);
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT, 4);
    rig.tap(SDLK_DOWN, 3);
    rig.tap(B);
    rig.key(Y, false);
    CHECK(rig.notes().handIndex() == 7 && rig.notes().handInversion() == -2 && rig.notes().chordCount() == 1);
    rig.notes().moveCursorTo(50, 2);
    rig.key(Y, true);
    CHECK(rig.notes().handIndex() == 3 && rig.notes().handInversion() == 1);
    rig.key(Y, false);
    CHECK(rig.chordOn(0) && std::strcmp(rig.chordOn(0)->name, "F") == 0 && rig.notes().chordCount() == 1);
}

void testBreakingAChordIntoNotes() {
    // Two handhelds with the same chord; one breaks it up. They must sound the same.
    Rig whole, broken;
    for (Rig* rig : {&whole, &broken}) {
        rig->key(Y, true);
        rig->tap(SDLK_RIGHT, 7); // Cmaj7
        rig->key(Y, false);
        rig->notes().moveCursorTo(60, 1);
    }
    broken.key(Y, true);
    broken.tap(SDLK_x); // A (G26: it was B)
    broken.key(Y, false);
    CHECK(broken.notes().chordCount() == 0 && broken.notes().noteCount() == 4 && broken.pat().used() == 4);
    CHECK(whole.notes().chordCount() == 1 && whole.notes().noteCount() == 0 && whole.pat().used() == 4);
    const int want[4] = {60, 64, 67, 71};
    for (int i = 0; i < 4; ++i) {
        const int slot = broken.pat().at(want[i], 3);
        CHECK(slot >= 0 && broken.pat().notes[slot].step == 0 && broken.pat().notes[slot].length == 4 && broken.pat().notes[slot].velocity == 0.9f);
    }
    whole.quiet();
    broken.quiet();
    whole.tap(START);
    broken.tap(START);
    const std::vector<float> a = whole.render(48000), b = broken.render(48000);
    float peak = 0.f;
    for (float s : a) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    CHECK(peak > 0.01f);
    CHECK(a == b);
}

void testShapingAndRemovingAWholeChord() {
    Rig rig;
    rig.tap(Y); // C on steps 1-4, cursor now on step 5
    rig.tap(Y); // and another on 5-8
    rig.notes().moveCursorTo(64, 1); // on the first chord's E
    CHECK(rig.notes().chordHere() && rig.notes().chordHere()->step == 0);
    rig.key(A, true);
    CHECK(rig.notes().noteCount() == 0); // A takes hold of the chord; it does not drop a note on it
    rig.tap(SDLK_RIGHT);
    CHECK(std::fabs(rig.chordOn(0)->velocity - 1.f) < 1e-4f);
    CHECK(rig.chordOn(4)->velocity == 0.9f); // only this chord
    rig.tap(SDLK_UP, 5);
    CHECK(rig.chordOn(0)->length == 1 && rig.notes().cursorStep() == 0); // from anywhere: the cursor comes back with its end
    CHECK(rig.notes().chordHere() && rig.notes().cursorPitch() == 64);
    rig.tap(SDLK_DOWN, 9);
    CHECK(rig.chordOn(0)->length == 4); // up to the next chord, no further
    rig.tap(SDLK_UP);
    CHECK(rig.chordOn(0)->length == 3);
    // The stick, the same.
    rig.key(STICK_RIGHT, true);
    rig.frames(40);
    rig.key(STICK_RIGHT, false);
    rig.frames(1);
    CHECK(rig.chordOn(0)->velocity == 1.f);
    rig.key(A, false);
    CHECK(rig.notes().noteCount() == 0);
    // The stick with A held slides the chord's end freely; the D-pad snaps it.
    rig.key(A, true);
    rig.key(STICK_UP, true);
    rig.frames(4);
    rig.key(STICK_UP, false);
    rig.frames(1);
    const float freeLength = rig.chordOn(0)->length;
    CHECK(freeLength > 2.f && freeLength < 3.f && freeLength != std::floor(freeLength));
    {
        // Every note of the chord goes to the engine that long.
        NoteEvent out[kMaxNotes];
        rig.pat().render(out);
        int same = 0;
        for (const NoteEvent& n : out) same += n.velocity > 0.f && n.step == 0 && n.length == freeLength;
        CHECK(same == 3);
    }
    // A chord that ends part-way through a step: the next one goes on the step after.
    rig.key(A, false);
    rig.notes().moveCursorTo(60, 9);
    rig.tap(Y);
    CHECK(rig.chordOn(9) && rig.chordOn(9)->length == freeLength && rig.notes().cursorStep() == 12);
    rig.notes().moveCursorTo(60, 9);
    rig.tap(B);
    rig.notes().moveCursorTo(64, 1);
    rig.key(A, true);
    rig.tap(SDLK_DOWN);
    CHECK(rig.chordOn(0)->length == 3.f);
    rig.key(A, false);
    // The next chord placed is as long as that one was left.
    rig.notes().moveCursorTo(60, 9);
    rig.tap(Y);
    CHECK(rig.chordOn(9) && rig.chordOn(9)->length == 3 && rig.notes().cursorStep() == 12);

    // In a chord's steps but not on one of its notes, A and B are about loose notes.
    rig.notes().moveCursorTo(62, 1);
    CHECK(!rig.notes().chordHere());
    rig.tap(B);
    CHECK(rig.notes().chordCount() == 3);
    rig.tap(A);
    CHECK(rig.notes().noteCount() == 1 && rig.notes().chordCount() == 3);
    // A loose note on top of a chord's note is what the cursor has.
    rig.notes().moveCursorTo(64, 1);
    rig.pat().notes[5].velocity = 0.5f; rig.pat().notes[5].pitch = 64; rig.pat().notes[5].step = 1; rig.pat().notes[5].length = 1;
    CHECK(!rig.notes().chordHere() && rig.notes().noteHere());
    rig.tap(B);
    CHECK(rig.notes().noteCount() == 1 && rig.notes().chordCount() == 3 && rig.notes().chordHere());
    // B on the chord's note removes the whole chord.
    rig.tap(B);
    CHECK(rig.notes().chordCount() == 2 && rig.chordOn(0) == nullptr && rig.chordOn(4) != nullptr);
    // And the engine was told: with every chord gone and the loose note too, the bar is silent.
    rig.notes().moveCursorTo(62, 1);
    rig.tap(B);
    rig.notes().moveCursorTo(60, 4);
    rig.tap(B);
    rig.notes().moveCursorTo(60, 9);
    rig.tap(B); // a chord goes last: removing one must reach the engine by itself
    CHECK(rig.pat().empty());
    rig.quiet();
    rig.tap(START);
    CHECK(rig.listen(96000) < 1e-4f);
}

void testWhereAChordCannotGo() {
    Rig rig;
    // Near the end of the bar it is cut short.
    rig.notes().moveCursorTo(60, 14);
    rig.tap(Y);
    CHECK(rig.chordOn(14) && rig.chordOn(14)->length == 2 && rig.notes().cursorStep() == 0);
    // In front of another chord it stops where that one starts.
    rig.notes().moveCursorTo(60, 12);
    rig.tap(Y);
    CHECK(rig.chordOn(12) && rig.chordOn(12)->length == 2 && rig.chordOn(14)->step == 14 && rig.notes().cursorStep() == 14);
    CHECK(rig.notes().chordCount() == 2);

    // Sixty-four notes is the bar's limit, chords' notes included.
    for (int i = 0; i < kMaxNotes - 6 - 2; ++i) {
        NoteEvent& n = rig.pat().notes[i];
        n.velocity = 1.f; n.pitch = 20; n.step = 0; n.length = 1;
    }
    CHECK(rig.pat().used() == kMaxNotes - 2);
    rig.notes().moveCursorTo(60, 0);
    rig.tap(Y);
    CHECK(rig.notes().chordCount() == 2 && rig.notes().cursorStep() == 0); // three notes do not fit in two
    // Nor can a swap grow a chord past it.
    rig.pat().notes[60].velocity = 1.f; rig.pat().notes[60].pitch = 21; rig.pat().notes[60].length = 1;
    rig.pat().notes[61].velocity = 1.f; rig.pat().notes[61].pitch = 22; rig.pat().notes[61].length = 1;
    CHECK(rig.pat().used() == kMaxNotes);
    rig.notes().moveCursorTo(60, 12);
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT, 7); // Cmaj7 has four notes
    CHECK(std::strcmp(rig.chordOn(12)->name, "Cmaj7") != 0 && rig.pat().used() == kMaxNotes);
    rig.key(Y, false);
    // And a loose note has nowhere to go either.
    rig.notes().moveCursorTo(90, 5);
    rig.tap(A);
    CHECK(rig.pat().used() == kMaxNotes && rig.notes().noteHere() == nullptr);
    // Y is not listening while A has hold of something.
    rig.notes().moveCursorTo(20, 0);
    rig.key(A, true);
    rig.key(Y, true);
    CHECK(!rig.notes().picking());
    rig.key(Y, false);
    rig.key(A, false);
    CHECK(rig.notes().chordCount() == 2);
}

void testPacksAndTheSongFile() {
    Rig rig;
    rig.app.session().useChordFolder(APOTHECA_SOURCE_DIR "/assets/chords");
    const std::vector<ChordPack>& packs = rig.app.session().packs;
    CHECK(packs.size() == 20 && rig.app.session().packIndex() == 0);
    rig.key(Y, true);
    rig.tap(SDLK_RIGHT, 2);
    rig.tap(R1);
    CHECK(rig.app.session().packIndex() == 1 && packs[1].name == rig.app.project().chordPack);
    CHECK(rig.notes().handIndex() == 0); // a new pack starts at its first chord
    rig.tap(L1, 2);
    CHECK(rig.app.session().packIndex() == static_cast<int>(packs.size()) - 1); // round the end
    rig.tap(R1, 2);
    CHECK(rig.app.session().packIndex() == 1);
    rig.tap(SDLK_RIGHT);
    rig.tap(SDLK_DOWN);
    rig.key(Y, false);
    const ChordEvent* c = rig.chordOn(0);
    CHECK(c && std::strcmp(c->name, packs[1].chords[1].name) == 0 && c->inversion == -1);
    rig.tap(Y);
    rig.notes().moveCursorTo(40, 9);
    rig.tap(A);

    // The song file keeps the pack and the chords, as chords.
    const std::string text = songText(rig.app.project());
    CHECK(text.find("pack " + packs[1].name + "\n") != std::string::npos);
    CHECK(text.find("\nchord 1 4 0.9 -1 ") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back));
    CHECK(songText(*back) == text);
    const NotePattern& p = back->tracks[1].notes[0][0];
    const int slot = p.chordAt(0);
    CHECK(slot >= 0 && c && p.chords[slot].root == c->root && p.chords[slot].count == c->count && p.chords[slot].length == 4);
    CHECK(slot >= 0 && std::strcmp(p.chords[slot].name, c->name) == 0 && p.chords[slot].intervals[c->count - 1] == c->intervals[c->count - 1]);
    CHECK(p.chordAt(4) >= 0 && p.used() == rig.pat().used());
    CHECK(std::strcmp(back->chordPack, rig.app.project().chordPack) == 0);
    // A song whose pack is not on this device falls back to the built-in one, and keeps its chords.
    std::snprintf(back->chordPack, sizeof back->chordPack, "%s", "Not Here");
    Rig other;
    other.app.project() = *back;
    CHECK(other.app.session().packIndex() == 0);

    // Chord lines that make no sense are left out; the song still opens.
    const std::string head = "apotheca-song 1\ntrack phazed\nscene 1\n";
    const auto chordsIn = [&](const std::string& lines) {
        std::unique_ptr<Project> song(new Project);
        if (!parseSong(head + lines, *song)) return -1;
        int count = 0;
        for (const ChordEvent& e : song->tracks[0].notes[0][0].chords) count += e.velocity > 0.f ? 1 : 0;
        return count;
    };
    CHECK(chordsIn("chord 1 4 0.8 0 60 C 0 4 7\n") == 1);
    CHECK(chordsIn("chord 1 4 0.8 0 60 C 0 4 7\nchord 5 4 0.8 0 60 C 0 4 7\n") == 2);
    CHECK(chordsIn("chord 1 4 0.8 0 60 C 0 4 7\nchord 4 4 0.8 0 60 C 0 4 7\n") == 1); // two at once
    CHECK(chordsIn("chord 1 3.5 0.8 0 60 C 0 4 7\nchord 4 4 0.8 0 60 C 0 4 7\n") == 1); // the first runs into step 4
    CHECK(chordsIn("chord 1 2.5 0.8 0 60 C 0 4 7\nchord 4 4 0.8 0 60 C 0 4 7\n") == 2); // ...and this one does not
    CHECK(chordsIn("chord 5 4 0.8 0 60 C 0 4 7\nchord 1 4.5 0.8 0 60 C 0 4 7\n") == 1);
    CHECK(chordsIn("chord 1 4 0.8 0 60 C\n") == 0);            // no notes
    CHECK(chordsIn("chord 0 4 0.8 0 60 C 0 4 7\n") == 0);      // no such step
    CHECK(chordsIn("chord 17 4 0.8 0 60 C 0 4 7\n") == 0);
    CHECK(chordsIn("chord 1 4 0 0 60 C 0 4 7\n") == 0);        // silent
    CHECK(chordsIn("chord 1 4 0.8 0 128 C 0 4 7\n") == 0);     // no such key
    CHECK(chordsIn("chord 1 4 0.8 0\n") == 0);
    {
        std::unique_ptr<Project> song(new Project);
        CHECK(parseSong(head + "chord 15 9 7 -8 60 ? 0 4 99 7\n", *song));
        const ChordEvent& e = song->tracks[0].notes[0][0].chords[0];
        CHECK(e.length == 2 && e.velocity == 1.f && e.inversion == -kMaxInversion && e.count == 3 && e.name[0] == 0);
        // Sixty-four notes, however the file is written.
        std::string many;
        for (int i = 0; i < 62; ++i) many += "note 1 30 1 1\n";
        CHECK(parseSong(head + many + "chord 1 4 0.8 0 60 C 0 4 7\n", *song));
        CHECK(song->tracks[0].notes[0][0].used() == 62);
        CHECK(parseSong(head + "chord 1 4 0.8 0 60 C 0 4 7\n" + many, *song));
        CHECK(song->tracks[0].notes[0][0].used() == kMaxNotes);
    }
}

void testAnOpenedSongPlaysItsChords() {
    // applyProject is how an opened song reaches the engine. Two fresh
    // handhelds open the same bar: one with the chord, one with the chord
    // written out by hand as loose notes. They must sound the same.
    Rig made, chordSong, noteSong;
    made.tap(Y);
    chordSong.app.project() = made.app.project();
    noteSong.app.project() = made.app.project();
    NotePattern& p = noteSong.pat();
    p.render(p.notes);
    p.chords[p.chordAt(0)] = ChordEvent();
    CHECK(p.used() == 3 && chordSong.pat().used() == 3 && chordSong.notes().chordCount() == 1);
    chordSong.app.session().applyProject();
    noteSong.app.session().applyProject();
    chordSong.tap(START);
    noteSong.tap(START);
    const std::vector<float> a = chordSong.render(48000), b = noteSong.render(48000);
    float peak = 0.f;
    for (float s : a) peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    CHECK(peak > 0.01f);
    CHECK(a == b);
}

// Choosing a chord is heard even while the song plays: hearing it is how it is chosen.
// (It used to be silent then, on the grounds that the song would play it soon enough.)
void testAChordIsHeardWhileChoosingEvenWhenPlaying() {
    Rig rig;
    rig.tap(START); // an empty bar, playing
    rig.render(100);
    CHECK(rig.listen(2000) < 1e-4f);
    rig.key(Y, true); // the chord in hand sounds at once
    CHECK(rig.listen(2000) > 0.005f);
    rig.render(96000);
    rig.tap(SDLK_RIGHT); // and each one flicked to
    CHECK(rig.listen(2000) > 0.005f);
    rig.render(96000);
    rig.tap(X); // and again when asked
    CHECK(rig.listen(2000) > 0.005f);
    rig.tap(B);
    rig.key(Y, false);
}

} // namespace

int main(int, char**) {
    ui::gStickSpeed = 1.f; // the checks below are of each screen's own speeds; the overall scale has its own check
    testReadingAPack();
    testThePacksThatShip();
    testTheShippedPacksAreTheFiles();
    testACardPackTakesAShippedOnesPlace();
    testEveryShippedChordIsWhatItSays();
    testTheNamesFitTheStrip();
    testTheInversionStandsClear();
    testAChordsNotes();
    testChordsAndLooseNotesShareTheSlots();
    testATapOfYPlacesTheChordInHand();
    testPickingWithTheDPadAndTheStick();
    testBCallsItOff();
    testSwappingAChordInPlace();
    testBreakingAChordIntoNotes();
    testShapingAndRemovingAWholeChord();
    testWhereAChordCannotGo();
    testPacksAndTheSongFile();
    testAnOpenedSongPlaysItsChords();
    testAChordIsHeardWhileChoosingEvenWhenPlaying();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("chord tests passed\n");
    return 0;
}
