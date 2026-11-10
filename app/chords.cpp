#include "chords.hpp"

#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

std::vector<ChordPack> loadChordPacks(const std::string& folder) {
    std::vector<ChordPack> found;
    if (DIR* dir = folder.empty() ? nullptr : opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() <= 5 || file.compare(file.size() - 5, 5, ".json") != 0) continue;
            std::string text;
            ChordPack pack;
            if (readTextFile(folder + "/" + file, text) && parseChordPack(text, pack)) found.push_back(pack);
        }
        closedir(dir);
    }
    std::sort(found.begin(), found.end(), [](const ChordPack& a, const ChordPack& b) { return a.name < b.name; });
    found.insert(found.begin(), builtInPack());
    return found;
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
