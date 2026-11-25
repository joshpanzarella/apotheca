// CATCH's lines (catch_key.hpp).
#include "catch_key.hpp"

#include "ui.hpp"

namespace apotheca {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return s;
}
std::string fromWords(const Session& session) {
    const int from = session.caughtFrom();
    if (from < 0) return "";
    char name[32];
    ui::trackName(session.project, from, name, sizeof name);
    return std::string(" OF ") + name;
}
} // namespace

std::string catchChoiceLine(const Session& session) {
    const int bars = session.catchBars(), from = session.catchFrom();
    std::string what = "THE MIX";
    if (from >= 0) {
        char name[32];
        ui::trackName(session.project, from, name, sizeof name);
        what = name;
    }
    return upper((bars == 1 ? std::string("CATCH THE LAST BAR OF ") : "CATCH THE LAST " + std::to_string(bars) + " BARS OF ") + what);
}

std::string caughtLine(const Session& session, const std::string& name) {
    const int got = session.caughtBars(), asked = session.catchBars();
    if (name.empty() && session.caughtNoWholeBar()) return upper("NO WHOLE BAR" + fromWords(session) + " YET: LET ONE GO BY");
    if (name.empty())
        return session.caughtFrom() < 0 ? "NOTHING TO CATCH: PLAY SOMETHING FIRST" : upper("NOTHING" + fromWords(session) + " TO CATCH: PLAY IT FIRST");
    std::string line;
    if (got == 0) line = asked == 1 ? "CAUGHT THE LAST BAR" : "CAUGHT " + std::to_string(asked) + " BARS' WORTH"; // (no whole bar: up to now)
    else if (got < asked) line = "CAUGHT ONLY " + std::to_string(got) + (got == 1 ? " BAR" : " BARS");
    else line = got == 1 ? "CAUGHT THE LAST BAR" : "CAUGHT THE LAST " + std::to_string(got) + " BARS";
    return upper(line + fromWords(session) + " AS \"" + name + "\"");
}

} // namespace apotheca
