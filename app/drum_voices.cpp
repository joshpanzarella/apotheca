#include "drum_voices.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace apotheca {

namespace {
const char* const kText =
#include "presets/drumvoices.inc"
    ;

std::string trimmed(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

bool nameOk(const std::string& name) {
    if (name.empty() || name.size() > kVoiceNameMost || name.front() == ' ' || name.back() == ' ') return false;
    for (char c : name)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ')) return false;
    return true;
}
} // namespace

DrumVoiceData readDrumVoices(const std::string& text) {
    DrumVoiceData out;
    std::istringstream lines(text);
    std::string line;
    int number = 0, model = drumsynth::kNone;
    NamedVoice* now = nullptr;
    const auto problem = [&](const std::string& what) {
        char head[32];
        std::snprintf(head, sizeof head, "line %d: ", number);
        out.problems.push_back(head + what);
    };
    // A voice is checked when the next begins, and at the end: not the usual, not another's.
    const auto finish = [&] {
        if (!now) return;
        if (now->voice == drumsynth::usualVoice(model)) problem("\"" + now->name + "\" is the model's usual voice");
        const std::vector<NamedVoice>& list = out.voices[model];
        for (std::size_t i = 0; i + 1 < list.size(); ++i)
            if (list[i].voice == now->voice) problem("\"" + now->name + "\" is \"" + list[i].name + "\" again");
        now = nullptr;
    };
    while (std::getline(lines, line)) {
        ++number;
        const std::size_t note = line.find('#');
        if (note != std::string::npos) line.resize(note);
        line = trimmed(line);
        if (line.empty()) continue;
        std::istringstream in(line);
        std::string word;
        in >> word;
        if (word == "voice") {
            finish();
            std::string modelWord, name;
            in >> modelWord;
            std::getline(in, name);
            name = trimmed(name);
            model = drumsynth::modelFromWord(modelWord.c_str());
            if (model == drumsynth::kNone) {
                problem("no model called \"" + modelWord + "\"");
                continue;
            }
            if (!nameOk(name) || name == kUsualVoiceName) {
                problem("\"" + name + "\" is not a voice's name (capitals, digits, spaces, 12 at most; not CLASSIC)");
                continue;
            }
            bool twice = false;
            for (const NamedVoice& v : out.voices[model]) twice = twice || v.name == name;
            if (twice) {
                problem("\"" + name + "\" twice in " + drumsynth::modelName(model));
                continue;
            }
            out.voices[model].push_back({name, drumsynth::usualVoice(model)});
            now = &out.voices[model].back();
        } else if (word == "set") {
            if (!now) {
                problem("a set with no voice before it");
                continue;
            }
            std::string place, dial;
            while (in >> place) {
                int at = -1;
                for (int p = 0; p < drumsynth::kPlaces; ++p)
                    if (drumsynth::shown(model, p) && place == drumsynth::knob(model, p).name) at = p;
                if (!(in >> dial)) {
                    problem(place + " has no number");
                    break;
                }
                char* end = nullptr;
                const long v = std::strtol(dial.c_str(), &end, 10);
                if (at < 0) problem(std::string(drumsynth::modelName(model)) + " has no " + place);
                else if (*end || end == dial.c_str() || v < 0 || v > drumsynth::kDialMost) problem(place + " " + dial + " is not 0 to 100");
                else now->voice.dial[at] = static_cast<std::uint8_t>(v);
            }
        } else {
            problem("\"" + word + "\" is not understood");
        }
    }
    finish();
    return out;
}

const char* drumVoicesText() { return kText; }

const DrumVoiceData& drumVoices() {
    static const DrumVoiceData data = readDrumVoices(kText);
    return data;
}

int voiceCount(int model) { return drumsynth::knownModel(model) ? 1 + static_cast<int>(drumVoices().voices[model].size()) : 0; }

const char* voiceName(int model, int place) {
    if (place < 0 || place >= voiceCount(model)) return "";
    return place == 0 ? kUsualVoiceName : drumVoices().voices[model][static_cast<std::size_t>(place - 1)].name.c_str();
}

drumsynth::Voice voiceAt(int model, int place) {
    if (place < 0 || place >= voiceCount(model)) return drumsynth::Voice();
    return place == 0 ? drumsynth::usualVoice(model) : drumVoices().voices[model][static_cast<std::size_t>(place - 1)].voice;
}

int voicePlaceOf(const drumsynth::Voice& voice) {
    for (int place = 0; place < voiceCount(voice.model); ++place)
        if (voiceAt(voice.model, place) == voice) return place;
    return -1;
}

} // namespace apotheca
