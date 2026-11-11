#include "designer.hpp"

#include <cmath>
#include <sstream>

namespace apotheca {

std::string presetNameFrom(const std::string& typed) {
    std::string out;
    for (char c : typed) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        const bool fine = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        if (!fine) continue;
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue; // no spaces first, or two together
        if (out.size() < 24) out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string Designer::spec() {
    std::lock_guard<std::mutex> lock(mutex_);
    return spec_;
}

std::string Designer::state() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++looks_;
    return state_;
}

unsigned Designer::looks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return looks_;
}

bool Designer::ask(const std::string& line) {
    std::istringstream in(line);
    std::string word;
    in >> word;
    DesignRequest r;
    double value = 0.0;
    if (word == "set") {
        r.kind = DesignRequest::Set;
        if (!(in >> r.a >> r.b)) return false;
    } else if (word == "macro") {
        r.kind = DesignRequest::Macro;
        if (!(in >> r.a >> value) || !std::isfinite(value) || r.a < 0 || r.a >= 8) return false;
        r.value = static_cast<float>(value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));
    } else if (word == "preset") {
        r.kind = DesignRequest::Preset;
        if (!(in >> r.a) || r.a < 0) return false;
    } else if (word == "note") {
        r.kind = DesignRequest::Note;
        if (!(in >> r.a >> r.b) || r.a < 0 || r.a > 127 || (r.b != 0 && r.b != 1)) return false;
    } else if (word == "save") {
        r.kind = DesignRequest::Save;
        std::string rest;
        std::getline(in, rest);
        r.name = presetNameFrom(rest);
        if (r.name.empty()) return false;
    } else if (word == "name") {
        r.kind = DesignRequest::Name;
        std::string rest;
        std::getline(in, rest);
        r.name = presetNameFrom(rest); // may be empty: the instrument's own name again
    } else {
        return false;
    }
    std::string more;
    if (r.kind != DesignRequest::Save && r.kind != DesignRequest::Name && (in >> more)) return false; // something extra on the line
    std::lock_guard<std::mutex> lock(mutex_);
    ++looks_; // asking for something shows the page is there, as much as looking does
    if (waiting_.size() >= 512) return false; // nobody is taking them: the program has stalled
    waiting_.push_back(r);
    return true;
}

std::vector<DesignRequest> Designer::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DesignRequest> out;
    out.swap(waiting_);
    return out;
}

void Designer::show(const std::string& spec, const std::string& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    spec_ = spec;
    state_ = state;
}

} // namespace apotheca
