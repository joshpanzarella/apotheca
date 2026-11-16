#include "presets.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "apotheca/fm6.hpp"
#include "instruments.hpp"
#include "mods.hpp"
#include "song_file.hpp"

namespace apotheca {

namespace {

// The built-in data, one text per synth: app/presets/NAME.inc, each a raw string. How to write it:
// docs/plans/presets-plan.md.
const char* const kPhazedText =
#include "presets/phazed.inc"
    ;
const char* const kJaxepText =
#include "presets/jaxep.inc"
    ;
const char* const kFm6Text =
#include "presets/fm6.inc"
    ;
const char* const kPhysicText =
#include "presets/physic.inc"
    ;

// Held as a double first, as a song's numbers are (song_file.cpp's wholeIn and fractionIn).
const double kValueMost = 1000000.0;
int wholeHeld(double v) { return v != v ? 0 : (v < -kValueMost ? static_cast<int>(-kValueMost) : (v > kValueMost ? static_cast<int>(kValueMost) : static_cast<int>(v))); }
float macroHeld(double v) { return std::isfinite(v) ? static_cast<float>(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v)) : 0.5f; }

// An engine built-in's settings, as many as a preset holds: what `base` starts a new one from.
std::vector<int> engineValues(TrackKind kind, int index) {
    const int count = presetSettingCount(kind);
    const int* from = nullptr;
    PhazedPatch phazed;
    Jx3pPatch jx3p;
    Fm6Patch fm6;
    PhysicPatch physic;
    if (kind == TrackKind::Phazed) phazed = Phazed::presetPatch(index), from = phazed.value;
    if (kind == TrackKind::Jx3p) jx3p = Jx3p::presetPatch(index), from = jx3p.value;
    if (kind == TrackKind::Fm6) fm6 = Fm6::presetPatch(index), from = fm6.value;
    if (kind == TrackKind::Physic) physic = Physic::presetPatch(index), from = physic.value;
    return from ? std::vector<int>(from, from + count) : std::vector<int>();
}

// An engine built-in's folder (the engine's own, not through the spec: that asks this data).
const char* engineFolder(TrackKind kind, int index) {
    return kind == TrackKind::Phazed ? Phazed::presetCategory(index) : kind == TrackKind::Jx3p ? Jx3p::presetCategory(index)
         : kind == TrackKind::Fm6 ? Fm6::presetCategory(index) : kind == TrackKind::Physic ? Physic::presetCategory(index) : "";
}

// An engine built-in's name (the engine's own, as engineFolder).
const char* engineName(TrackKind kind, int index) {
    return kind == TrackKind::Phazed ? Phazed::presetName(index) : kind == TrackKind::Jx3p ? Jx3p::presetName(index)
         : kind == TrackKind::Fm6 ? Fm6::presetName(index) : kind == TrackKind::Physic ? Physic::presetName(index) : "";
}

// Two names the same, as a designer writes them: capitals not counted.
bool sameName(const std::string& a, const char* b) {
    if (a.size() != std::strlen(b)) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string trimmed(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t"), last = s.find_last_not_of(" \t");
    return first == std::string::npos ? "" : s.substr(first, last - first + 1);
}

// The track sound's own lines, into a preset's TrackSound: song_file.cpp's one reader of them.
SoundLine soundLine(TrackKind kind, const std::string& word, std::istream& in, TrackSound& sound) {
    return readSoundLine(word, in, SoundParts{kind, sound.effects, sound.send, &sound.glideFrom, sound.mods});
}

// After the whole preset is read: a MOD aimed where nothing fits is OFF (its effect may come after it).
// The count of MODs that went OFF so.
int finishSound(TrackKind kind, TrackSound& sound) {
    int dropped = 0;
    for (const ModSettings& m : sound.mods) dropped += modKind(m.kind) != kModOff;
    modsFit(kind, sound.effects, sound.mods);
    for (const ModSettings& m : sound.mods) dropped -= modKind(m.kind) != kModOff;
    return dropped;
}

bool soundWord(const std::string& word) { return word == "fx" || word == "send" || word == "glidefrom" || word == "mod" || word == "modsteps"; }

} // namespace

const char* builtInText(TrackKind kind) {
    return kind == TrackKind::Phazed ? kPhazedText : kind == TrackKind::Jx3p ? kJaxepText : kind == TrackKind::Fm6 ? kFm6Text
         : kind == TrackKind::Physic ? kPhysicText : "";
}

// ---- the player's own preset files ---------------------------------------------------------

bool readPresetFile(TrackKind kind, const std::string& text, UserPreset& out) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.compare(0, 15, "apotheca-preset") != 0) return false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); // (kept with Windows' line ends)
        std::istringstream in(line);
        std::string word;
        in >> word;
        double v = 0.0;
        // A number from a file is held to its limits as a double before it is made a float or an int
        // ("settings 1e300" cast first is undefined). Each synth's loadPreset then holds a value to
        // its own setting's range.
        if (word == "macros") while (out.macros.size() < static_cast<std::size_t>(kSynthMacros) && (in >> v)) out.macros.push_back(macroHeld(v));
        else if (word == "settings") while (out.values.size() < static_cast<std::size_t>(presetSettingCount(kind)) && (in >> v)) out.values.push_back(std::isfinite(v) ? wholeHeld(v) : 0);
        else if (word == "whole") out.sound.whole = true;
        else if (soundLine(kind, word, in, out.sound) == SoundLine::Taken) out.sound.whole = true;
    }
    finishSound(kind, out.sound);
    return !out.values.empty();
}

std::string presetFileText(const TrackData& track) {
    const SynthSpec* spec = specFor(track.kind);
    const InstrumentInfo* info = instrument(track.kind);
    if (!spec || !info) return "";
    std::string text = "apotheca-preset 1\nsynth ";
    text += info->word;
    text += "\nmacros";
    char number[32];
    for (float m : track.macro) std::snprintf(number, sizeof number, " %.6g", m), text += number;
    text += "\nsettings";
    const int* values = spec->values(const_cast<TrackData&>(track));
    // (What a preset holds, not everything the track has: the JX-3P's VELOCITY and BRILLIANCE are not in the file.)
    for (int id = 0; id < presetSettingCount(track.kind); ++id) std::snprintf(number, sizeof number, " %d", values[id]), text += number;
    // The track's whole sound past the synth: "whole" says so even when it holds nothing (then picking
    // it empties the track's slots, sends and MODs, as they were when it was saved).
    text += "\nwhole\n";
    text += soundText(track.kind, track.effects, track.send, track.glideFrom, track.mods);
    return text;
}

// ---- the built-in data ------------------------------------------------------------------------

PresetData readPresetData(TrackKind kind, const std::string& text) {
    PresetData data;
    const SynthSpec* spec = specFor(kind);
    const int engine = enginePresets(kind), settings = presetSettingCount(kind);
    if (!spec) return data;
    data.sounds.resize(static_cast<std::size_t>(engine));
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    UserPreset* adding = nullptr;  // the new built-in being read
    TrackSound* sound = nullptr;   // ...or an engine built-in's sound
    std::vector<std::string> folders; // each added folder, in its order: one run each
    char text_[160];
    const auto problem = [&](const char* what) {
        std::snprintf(text_, sizeof text_, "line %d: %s", number, what);
        data.problems.push_back(text_);
    };
    const auto finish = [&] {
        TrackSound* s = adding ? &adding->sound : sound;
        if (s && finishSound(kind, *s) > 0) problem("a MOD aimed at nothing there (an empty slot, a choice, a macro that is a choice): OFF");
        adding = nullptr, sound = nullptr;
    };
    // Which built-in a `base` or `preset` line names, among the first `known` (the engine's own, then
    // the ones added before): its index, or -1 with the problem said. A built-in is named by its NAME
    // (capitals not counted), by FOLDER / NAME where two folders have the name, or by its place from 1.
    // A name is what to write: a place moves when presets are put into a folder (batch 10's did).
    const auto builtInCalled = [&](const std::string& said, int known, bool engineOnly) {
        const auto nameAt = [&](int i) { return i < engine ? engineName(kind, i) : data.added[static_cast<std::size_t>(i - engine)].name.c_str(); };
        const auto folderAt = [&](int i) { return i < engine ? engineFolder(kind, i) : data.added[static_cast<std::size_t>(i - engine)].folder.c_str(); };
        if (said.empty()) {
            problem("a built-in's NAME (or FOLDER / NAME, or its place from 1) is missing");
            return -1;
        }
        char* end = nullptr;
        const double n = std::strtod(said.c_str(), &end);
        if (end && *end == '\0') {
            if (!(n >= 1.0) || n > known || n != std::floor(n)) {
                problem(engineOnly ? "no such built-in of the engine's own (a place is from 1)" : "no such built-in before this one (a place is from 1)");
                return -1;
            }
            return static_cast<int>(n) - 1;
        }
        const std::size_t cut = said.find(" / ");
        const std::string folder = cut == std::string::npos ? "" : trimmed(said.substr(0, cut)), name = cut == std::string::npos ? said : trimmed(said.substr(cut + 3));
        int found = -1, count = 0;
        for (int i = 0; i < known; ++i)
            if (sameName(name, nameAt(i)) && (folder.empty() || sameName(folder, folderAt(i)))) found = found < 0 ? i : found, ++count;
        if (count == 0) {
            std::snprintf(text_, sizeof text_, "line %d: no built-in %s is called \"%s\"", number, engineOnly ? "of the engine's own" : "before this one", said.c_str());
            data.problems.push_back(text_);
            return -1;
        }
        if (count > 1) {
            std::snprintf(text_, sizeof text_, "line %d: %d built-ins are called \"%s\": write FOLDER / NAME", number, count, said.c_str());
            data.problems.push_back(text_);
            return -1;
        }
        return found;
    };
    while (std::getline(lines, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = line.substr(0, line.find('#')); // a note to the end of the line
        std::istringstream in(line);
        std::string word;
        if (!(in >> word)) continue;
        if (word == "new") {
            finish();
            // new FOLDER / NAME
            std::string rest;
            std::getline(in >> std::ws, rest);
            const std::size_t cut = rest.find(" / ");
            std::string folder = cut == std::string::npos ? "" : rest.substr(0, cut), name = cut == std::string::npos ? "" : rest.substr(cut + 3);
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!folder.empty() && folder.back() == ' ') folder.pop_back();
            bool plain = !folder.empty() && !name.empty();
            for (char c : folder + name) plain = plain && c >= 32 && c < 127;
            if (!plain || folder.size() > kPresetFolderMost || name.size() > kPresetNameMost) {
                problem("not \"new FOLDER / NAME\" (plain letters; a folder up to 14, a name up to 20)");
                continue;
            }
            bool engineFolder = folder == "USER";
            for (int i = 0; i < engine && !engineFolder; ++i) engineFolder = folder == apotheca::engineFolder(kind, i);
            if (engineFolder) {
                problem("a folder of the engine's own, or USER: an added preset goes in a folder of its own");
                continue;
            }
            if (folders.empty() || folders.back() != folder) {
                bool before = false;
                for (const std::string& f : folders) before = before || f == folder;
                if (before) {
                    problem("a folder that came before, with another between: a folder's presets stand together");
                    continue;
                }
                folders.push_back(folder);
            }
            for (const UserPreset& p : data.added)
                if (p.folder == folder && p.name == name) {
                    problem("a name its folder has already");
                    plain = false;
                }
            if (!plain) continue;
            data.added.emplace_back();
            adding = &data.added.back();
            adding->folder = folder;
            adding->name = name;
            adding->values = engineValues(kind, 0); // from the first built-in, unless `base` says another
            adding->macros.assign(kSynthMacros, 0.5f);
            continue;
        }
        if (word == "preset") {
            finish();
            // preset NAME (or FOLDER / NAME, or the place N from 1): one of the engine's own.
            std::string rest;
            std::getline(in >> std::ws, rest);
            const int n = builtInCalled(trimmed(rest), engine, true);
            if (n < 0) continue;
            sound = &data.sounds[static_cast<std::size_t>(n)];
            if (sound->whole) {
                problem("a built-in given its sound twice");
                *sound = TrackSound();
            }
            continue;
        }
        if (!adding && !sound) {
            problem("outside a preset: a \"new\" or \"preset\" line first");
            continue;
        }
        TrackSound& to = adding ? adding->sound : *sound;
        if (word == "whole") {
            to.whole = true;
            continue;
        }
        if (soundWord(word)) {
            if (soundLine(kind, word, in, to) != SoundLine::Taken) {
                problem("an effect, send, GLIDE FROM or MOD line not read (a word nobody knows, a number missing)");
                continue;
            }
            to.whole = true;
            // The song file holds what it reads to its limits; here a number past them is a slip, said.
            if (word == "fx" || word == "send") {
                std::istringstream again(line);
                std::string skip;
                again >> skip >> skip;
                if (word == "fx") again >> skip;
                double x = 0.0;
                bool fine = true;
                while (again >> x) fine = fine && x >= 0.0 && x <= 1.0;
                if (!fine) problem("an effect's setting or a send is 0 to 1");
            }
            continue;
        }
        if (!adding) {
            problem("a built-in of the engine's own takes only the track sound's lines (fx, send, glidefrom, mod, modsteps, whole)");
            continue;
        }
        if (word == "base") {
            // base NAME (or FOLDER / NAME, or the place N from 1): a built-in before this one.
            std::string rest;
            std::getline(in >> std::ws, rest);
            const int b = builtInCalled(trimmed(rest), engine + static_cast<int>(data.added.size()) - 1, false); // (not itself)
            if (b < 0) continue;
            if (b < engine) {
                adding->values = engineValues(kind, b);
                adding->macros.assign(kSynthMacros, 0.5f);
            } else {
                adding->values = data.added[static_cast<std::size_t>(b - engine)].values;
                adding->macros = data.added[static_cast<std::size_t>(b - engine)].macros;
            }
            continue;
        }
        double v = 0.0;
        if (word == "macros") {
            int m = 0;
            for (; m < kSynthMacros && (in >> v); ++m) {
                if (!(v >= 0.0 && v <= 1.0)) problem("a macro is 0 to 1");
                adding->macros[static_cast<std::size_t>(m)] = macroHeld(v);
            }
            if (m < kSynthMacros || (in >> v)) problem("\"macros\" takes eight numbers");
            continue;
        }
        const auto set = [&](int id, double value) {
            if (!std::isfinite(value) || value != std::floor(value) || value < spec->settingMin(id) || value > spec->settingMax(id)) {
                std::snprintf(text_, sizeof text_, "line %d: setting %d is a whole number from %d to %d", number, id, spec->settingMin(id), spec->settingMax(id));
                data.problems.push_back(text_);
            }
            const int w = wholeHeld(value);
            adding->values[static_cast<std::size_t>(id)] = w < spec->settingMin(id) ? spec->settingMin(id) : (w > spec->settingMax(id) ? spec->settingMax(id) : w);
        };
        if (word == "settings") {
            int id = 0;
            for (; id < settings && (in >> v); ++id) set(id, v);
            if (in >> v) problem("\"settings\" has more numbers than a preset holds");
            continue;
        }
        if (word == "set") {
            double id = 0;
            bool any = false;
            while (in >> id) {
                if (!(in >> v)) {
                    problem("\"set\" takes pairs: ID VALUE");
                    break;
                }
                any = true;
                if (!(id >= 0.0) || id >= settings || id != std::floor(id)) {
                    problem("\"set\": no such setting in a preset");
                    continue;
                }
                set(static_cast<int>(id), v);
            }
            if (!any) problem("\"set\" takes pairs: ID VALUE");
            continue;
        }
        if (word == "op") {
            // op N V0 ... V20: FM-6's operator N (1 to 6), its 21 settings in the original's order
            // (fm6::OpSetting: R1-R4, L1-L4, BREAK POINT, LEFT / RIGHT DEPTH, LEFT / RIGHT CURVE, RATE
            // SCALING, AMS, VELOCITY, LEVEL, MODE, COARSE, FINE, DETUNE): one line an operator, not 21 pairs.
            if (kind != TrackKind::Fm6) {
                problem("\"op\" is FM-6's: an operator and its 21 settings");
                continue;
            }
            double op = 0.0;
            if (!(in >> op) || !(op >= 1.0 && op <= 6.0) || op != std::floor(op)) {
                problem("\"op\" takes an operator, 1 to 6, then its 21 settings");
                continue;
            }
            int s = 0;
            for (; s < fm6::kOpSettings && (in >> v); ++s) set(fm6::opSetting(static_cast<int>(op), s), v);
            if (s < fm6::kOpSettings || (in >> v)) problem("\"op N\" takes 21 settings, R1 to DETUNE");
            continue;
        }
        problem("a word a preset does not have");
    }
    finish();
    return data;
}

namespace {
PresetData& dataOf(TrackKind kind) {
    static PresetData phazed = readPresetData(TrackKind::Phazed, kPhazedText), jaxep = readPresetData(TrackKind::Jx3p, kJaxepText),
                      fm6 = readPresetData(TrackKind::Fm6, kFm6Text), physic = readPresetData(TrackKind::Physic, kPhysicText);
    static PresetData none;
    none = PresetData();
    return kind == TrackKind::Phazed ? phazed : kind == TrackKind::Jx3p ? jaxep : kind == TrackKind::Fm6 ? fm6 : kind == TrackKind::Physic ? physic : none;
}
} // namespace

const PresetData& builtInData(TrackKind kind) { return dataOf(kind); }
void useBuiltInDataForTests(TrackKind kind, const PresetData& data) {
    if (specFor(kind)) dataOf(kind) = data;
}

const TrackSound* presetSound(TrackKind kind, int index) {
    if (const UserPreset* p = listedPreset(kind, index)) return p->sound.whole ? &p->sound : nullptr;
    const PresetData& data = builtInData(kind);
    if (index < 0 || index >= static_cast<int>(data.sounds.size())) return nullptr;
    return data.sounds[static_cast<std::size_t>(index)].whole ? &data.sounds[static_cast<std::size_t>(index)] : nullptr;
}

TrackSound trackSoundOf(const TrackData& track) {
    TrackSound s;
    s.whole = true;
    for (int i = 0; i < kTrackSlots; ++i) s.effects[i] = track.effects[i];
    for (int b = 0; b < kBuses; ++b) s.send[b] = track.send[b];
    s.glideFrom = track.glideFrom;
    for (int m = 0; m < kModsPerTrack; ++m) s.mods[m] = track.mods[m];
    return s;
}

std::string trackSoundText(TrackKind kind, const TrackSound& sound) { return soundText(kind, sound.effects, sound.send, sound.glideFrom, sound.mods); }

} // namespace apotheca
