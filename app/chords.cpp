#include "chords.hpp"

#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>

#include "song_file.hpp"

namespace apotheca {

namespace {

// ---- just enough JSON -------------------------------------------------------------

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json* get(const char* key) const {
        for (const auto& f : fields)
            if (f.first == key) return &f.second;
        return nullptr;
    }
    bool isWhole() const { return type == Number && number == std::floor(number) && std::fabs(number) < 1e6; }
};

class Reader {
public:
    explicit Reader(const std::string& text) : s_(text) {}
    bool document(Json& out) {
        if (!value(out, 0)) return false;
        space();
        return at_ == s_.size();
    }

private:
    static constexpr int kDeepest = 32;
    void space() {
        while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\n' || s_[at_] == '\r' || s_[at_] == '\t')) ++at_;
    }
    bool word(const char* w) {
        const std::size_t n = std::strlen(w);
        if (s_.compare(at_, n, w) != 0) return false;
        at_ += n;
        return true;
    }
    bool string(std::string& out) {
        if (at_ >= s_.size() || s_[at_] != '"') return false;
        for (++at_; at_ < s_.size(); ++at_) {
            const char c = s_[at_];
            if (c == '"') { ++at_; return true; }
            if (c != '\\') { out += c; continue; }
            if (++at_ >= s_.size()) return false;
            const char e = s_[at_];
            if (e == 'n') out += '\n';
            else if (e == 't') out += '\t';
            else if (e == 'r' || e == 'b' || e == 'f') out += ' ';
            else if (e == 'u') { // not needed for names; keep the place
                if (at_ + 4 >= s_.size()) return false;
                at_ += 4;
                out += '?';
            } else out += e; // \" \\ \/
        }
        return false;
    }
    bool value(Json& out, int depth) {
        if (depth > kDeepest) return false;
        space();
        if (at_ >= s_.size()) return false;
        const char c = s_[at_];
        if (c == '{') {
            out.type = Json::Object;
            ++at_;
            space();
            if (at_ < s_.size() && s_[at_] == '}') { ++at_; return true; }
            for (;;) {
                space();
                std::string key;
                if (!string(key)) return false;
                space();
                if (at_ >= s_.size() || s_[at_++] != ':') return false;
                out.fields.emplace_back(key, Json());
                if (!value(out.fields.back().second, depth + 1)) return false;
                space();
                if (at_ >= s_.size()) return false;
                if (s_[at_] == ',') { ++at_; continue; }
                return s_[at_++] == '}';
            }
        }
        if (c == '[') {
            out.type = Json::Array;
            ++at_;
            space();
            if (at_ < s_.size() && s_[at_] == ']') { ++at_; return true; }
            for (;;) {
                out.items.emplace_back();
                if (!value(out.items.back(), depth + 1)) return false;
                space();
                if (at_ >= s_.size()) return false;
                if (s_[at_] == ',') { ++at_; continue; }
                return s_[at_++] == ']';
            }
        }
        if (c == '"') { out.type = Json::String; return string(out.text); }
        if (word("true")) { out.type = Json::Bool; out.number = 1; return true; }
        if (word("false")) { out.type = Json::Bool; return true; }
        if (word("null")) return true;
        const char* start = s_.c_str() + at_;
        char* end = nullptr;
        out.number = std::strtod(start, &end);
        if (end == start || !(c == '-' || (c >= '0' && c <= '9'))) return false;
        out.type = Json::Number;
        at_ += static_cast<std::size_t>(end - start);
        return true;
    }

    const std::string& s_;
    std::size_t at_ = 0;
};

void setName(char out[16], const std::string& from) {
    int n = 0;
    for (char c : from) {
        if (n >= 15) break;
        if (c > ' ' && c < 127) out[n++] = c; // no spaces: a song file keeps a name as one word
    }
    out[n] = 0;
}

ChordShape plain(const char* name, int rootKey, int a, int b, int c, int d = -1) {
    ChordShape s;
    setName(s.name, name);
    s.rootKey = rootKey;
    const int all[4] = {a, b, c, d};
    for (int v : all)
        if (v >= 0) s.intervals[s.count++] = v;
    return s;
}

} // namespace

bool parseChordPack(const std::string& json, ChordPack& pack) {
    Json doc;
    if (!Reader(json).document(doc) || doc.type != Json::Object) return false;
    const Json* chords = doc.get("chords");
    if (!chords || chords->type != Json::Array) return false;

    int tonic = 0;
    if (const Json* t = doc.get("sourceTonic"))
        if (t->isWhole()) tonic = ((static_cast<int>(t->number) % 12) + 12) % 12;
    std::vector<int> scale;
    if (const Json* s = doc.get("scale"))
        if (const Json* classes = s->get("pitchClasses"))
            for (const Json& v : classes->items)
                if (v.isWhole()) scale.push_back(static_cast<int>(v.number));
    if (scale.empty()) scale = {0, 2, 4, 5, 7, 9, 11};

    ChordPack made;
    made.tonic = tonic;
    bool third = false, majorThird = false;
    for (int pc : scale) third = third || pc == 3, majorThird = majorThird || pc == 4;
    made.minor = third && !majorThird;
    if (const Json* name = doc.get("name")) made.name = name->type == Json::String ? name->text : "";
    for (char& c : made.name)
        if (c < ' ' || c > 126) c = ' ';
    if (made.name.size() > 24) made.name.resize(24);

    static const char* keys[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    for (const Json& c : chords->items) {
        const Json* intervals = c.get("intervals");
        const Json* root = c.get("root");
        if (!intervals || intervals->type != Json::Array || intervals->items.empty()) continue;
        ChordShape shape;
        bool good = true;
        for (const Json& v : intervals->items) {
            if (!v.isWhole() || v.number < 0 || v.number > 48 || shape.count >= kChordNotes) { good = false; break; }
            shape.intervals[shape.count++] = static_cast<int>(v.number);
        }
        int degree = 0, alteration = 0;
        if (root) {
            const Json* d = root->get("degree");
            const Json* a = root->get("alteration");
            if (d && d->isWhole()) degree = static_cast<int>(d->number);
            if (a && a->isWhole()) alteration = static_cast<int>(a->number);
        }
        if (!good || degree < 0 || degree >= static_cast<int>(scale.size()) || alteration < -2 || alteration > 2) continue;
        shape.rootKey = (((tonic + scale[static_cast<std::size_t>(degree)] + alteration) % 12) + 12) % 12;

        const Json* legacy = c.get("legacyName");
        const Json* quality = c.get("quality");
        std::string name;
        if (legacy && legacy->type == Json::String && !legacy->text.empty()) {
            name = legacy->text;
        } else {
            std::string q = quality && quality->type == Json::String ? quality->text : "";
            if (q.compare(0, 5, "major") == 0) q.erase(0, 5);       // "major(open)" reads as C(open)
            else if (q.compare(0, 5, "minor") == 0) q.replace(0, 5, "m");
            name = keys[shape.rootKey] + q;
        }
        setName(shape.name, name);
        made.chords.push_back(shape);
    }
    if (made.chords.empty()) return false;
    if (made.name.empty()) made.name = "pack";
    pack = made;
    return true;
}

namespace {
// The key a name starts at `at` with (0..11) and how many letters it takes, or -1.
int keyAt(const char* name, int at, int* letters) {
    static const int natural[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    const char c = name[at];
    if (c < 'A' || c > 'G') return -1;
    int key = natural[c - 'A'];
    *letters = 1;
    if (name[at + 1] == '#') key += 1, *letters = 2;
    else if (name[at + 1] == 'b') key += 11, *letters = 2;
    return key % 12;
}
} // namespace

void transposeChordName(char name[16], int by) {
    static const char* keys[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    std::string out;
    int letters = 0;
    const std::string was = name;
    for (std::size_t i = 0; i < was.size(); ++i) {
        const bool keyHere = i == 0 || was[i - 1] == '/';
        const int key = keyHere ? keyAt(was.c_str(), static_cast<int>(i), &letters) : -1;
        if (key < 0) {
            out += was[i];
            continue;
        }
        out += keys[((key + by) % 12 + 12) % 12];
        i += static_cast<std::size_t>(letters - 1);
    }
    setName(name, out);
}

const ChordPack& builtInPack() {
    static const ChordPack pack = [] {
        ChordPack p;
        p.name = "Plain C";
        p.chords = {plain("C", 0, 0, 4, 7),        plain("Dm", 2, 0, 3, 7),       plain("Em", 4, 0, 3, 7),
                    plain("F", 5, 0, 4, 7),        plain("G", 7, 0, 4, 7),        plain("Am", 9, 0, 3, 7),
                    plain("Bdim", 11, 0, 3, 6),    plain("Cmaj7", 0, 0, 4, 7, 11), plain("Dm7", 2, 0, 3, 7, 10),
                    plain("Em7", 4, 0, 3, 7, 10),  plain("Fmaj7", 5, 0, 4, 7, 11), plain("G7", 7, 0, 4, 7, 10),
                    plain("Am7", 9, 0, 3, 7, 10)};
        return p;
    }();
    return pack;
}

namespace {

// The shipped packs' JSON, one raw string each (tools/chord_packs.py writes it).
const char* const kShippedPackTexts[] = {
#include "chord_packs.inc"
};

bool byName(const ChordPack& a, const ChordPack& b) { return a.name < b.name; }

} // namespace

const std::vector<std::string>& shippedPackTexts() {
    static const std::vector<std::string> texts(std::begin(kShippedPackTexts), std::end(kShippedPackTexts));
    return texts;
}

const std::vector<ChordPack>& shippedPacks() {
    static const std::vector<ChordPack> packs = [] {
        std::vector<ChordPack> made;
        for (const std::string& text : shippedPackTexts()) {
            ChordPack pack;
            if (parseChordPack(text, pack)) made.push_back(pack);
        }
        std::sort(made.begin(), made.end(), byName);
        return made;
    }();
    return packs;
}

std::vector<ChordPack> loadChordPacks(const std::string& folder) {
    std::vector<ChordPack> found = shippedPacks();
    const std::size_t shipped = found.size();
    if (DIR* dir = folder.empty() ? nullptr : opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() <= 5 || file.compare(file.size() - 5, 5, ".json") != 0) continue;
            std::string text;
            ChordPack pack;
            if (!readTextFile(folder + "/" + file, text) || !parseChordPack(text, pack)) continue;
            // A file with a shipped pack's name takes its place (the copies the zip puts on the card do).
            std::size_t same = 0;
            while (same < shipped && found[same].name != pack.name) ++same;
            if (same < shipped) found[same] = pack;
            else found.push_back(pack);
        }
        closedir(dir);
    }
    std::stable_sort(found.begin(), found.end(), byName);
    found.insert(found.begin(), builtInPack());
    return found;
}

namespace {

std::string textOf(const Json* j) { return j && j->type == Json::String ? j->text : std::string(); }

std::vector<int> wholesOf(const Json* j) {
    std::vector<int> out;
    if (j && j->type == Json::Array)
        for (const Json& v : j->items) out.push_back(v.isWhole() ? static_cast<int>(v.number) : -999);
    return out;
}

int wholeOf(const Json* j, int otherwise) { return j && j->isWhole() ? static_cast<int>(j->number) : otherwise; }

} // namespace

bool readPackFacts(const std::string& json, PackFacts& facts) {
    Json doc;
    if (!Reader(json).document(doc) || doc.type != Json::Object) return false;
    const Json* chords = doc.get("chords");
    if (!chords || chords->type != Json::Array) return false;
    PackFacts made;
    made.schemaVersion = wholeOf(doc.get("schemaVersion"), 0);
    made.tonic = wholeOf(doc.get("sourceTonic"), -1);
    made.id = textOf(doc.get("id"));
    made.name = textOf(doc.get("name"));
    made.description = textOf(doc.get("description"));
    if (const Json* scale = doc.get("scale")) {
        made.scaleId = textOf(scale->get("id"));
        made.scale = wholesOf(scale->get("pitchClasses"));
    }
    for (const Json& c : chords->items) {
        PackFacts::Chord chord;
        chord.id = textOf(c.get("id"));
        chord.quality = textOf(c.get("quality"));
        chord.function = textOf(c.get("function"));
        chord.legacyName = textOf(c.get("legacyName"));
        chord.borrowedFrom = textOf(c.get("borrowedFrom"));
        if (const Json* root = c.get("root")) {
            chord.degree = wholeOf(root->get("degree"), -1);
            chord.alteration = wholeOf(root->get("alteration"), 0);
        }
        chord.preferredVoices = wholeOf(c.get("preferredVoices"), 0);
        chord.pedal = wholeOf(c.get("pedalSemitone"), -1);
        chord.intervals = wholesOf(c.get("intervals"));
        chord.required = wholesOf(c.get("requiredIntervals"));
        chord.hasOutside = c.get("outside") != nullptr;
        chord.outside = wholesOf(c.get("outside"));
        made.chords.push_back(chord);
    }
    if (const Json* progressions = doc.get("progressions"))
        for (const Json& p : progressions->items) {
            PackFacts::Progression progression;
            progression.id = textOf(p.get("id"));
            if (const Json* segments = p.get("segments"))
                for (const Json& segment : segments->items)
                    progression.segments.push_back({textOf(segment.get("chordId")), wholeOf(segment.get("durationTicks"), 0)});
            made.progressions.push_back(progression);
        }
    if (made.chords.empty()) return false;
    facts = made;
    return true;
}

ChordEvent makeChord(const ChordShape& shape, int nearPitch, int step, float length, float velocity, int inversion) {
    ChordEvent c;
    c.step = step;
    c.length = length;
    c.velocity = velocity;
    c.inversion = inversion < -kMaxInversion ? -kMaxInversion : (inversion > kMaxInversion ? kMaxInversion : inversion);
    c.count = shape.count;
    for (int i = 0; i < shape.count; ++i) c.intervals[i] = shape.intervals[i];
    std::memcpy(c.name, shape.name, sizeof c.name);
    // The same key in the octave nearest: at most six semitones away.
    int root = nearPitch - (((nearPitch - shape.rootKey) % 12) + 12) % 12; // at or below
    if (nearPitch - root > 6) root += 12;
    while (root < 0) root += 12;
    while (root > 127) root -= 12;
    c.root = root;
    return c;
}

} // namespace apotheca
