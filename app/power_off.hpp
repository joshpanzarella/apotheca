#pragma once
// When Apotheca is the whole system (our own firmware for the RG40XXV,
// os/README.md), quitting turns the handheld off: there is nothing to go
// back to. The system says so in two environment variables, and the
// program tells it back with a file:
//
//   APOTHECA_OFF_MARK          a path. Set: SEL + START, and the idle rule
//                              below, end the program and write this file,
//                              AFTER the song is saved, holding why ("quit"
//                              or "idle"). The supervisor (os/.../apotheca-keep)
//                              then turns the handheld off; a program that
//                              stops with no mark (a crash, a kill) is
//                              started again, as before.
//   APOTHECA_IDLE_OFF_MINUTES  whole minutes, 0 or unset for never: the
//                              program ends as above after that long with
//                              nothing to draw (no button, the song stopped,
//                              nothing recording, bouncing or on WiFi: the
//                              redraw's own idea of "nothing is happening").
//
// Neither is set by the port's launcher (scripts/Apotheca.sh), so on another
// firmware quitting is quitting, as it always was. No SDL in here, so a test
// needs none.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace apotheca {

struct PowerOff {
    std::string mark;       // where to write the mark; empty: quitting only quits
    std::uint32_t idleMs = 0; // 0: never off by itself
    std::uint32_t busyAt = 0; // the last pass that had something to show

    // From the environment (or anything that answers as getenv does).
    template <typename Get>
    static PowerOff from(Get get) {
        PowerOff p;
        if (const char* m = get("APOTHECA_OFF_MARK")) p.mark = m;
        if (const char* i = get("APOTHECA_IDLE_OFF_MINUTES")) {
            char* end = nullptr;
            const double minutes = std::strtod(i, &end);
            // Held as a double first; a day at most; nothing that is not a number.
            if (end != i && minutes >= 1.0 && minutes <= 24.0 * 60.0) p.idleMs = static_cast<std::uint32_t>(minutes) * 60000u;
        }
        if (p.mark.empty()) p.idleMs = 0; // nobody to turn the handheld off: never end by itself
        return p;
    }
    static PowerOff fromEnvironment() { return from([](const char* name) { return std::getenv(name); }); }

    bool turnsOff() const { return !mark.empty(); }

    // Something to show this pass (the redraw wanted it): the idle time starts again.
    void busy(std::uint32_t nowMs) { busyAt = nowMs; }
    // Long enough with nothing to show? (Unsigned, so the clock going round is no harm.)
    bool idleDue(std::uint32_t nowMs) const { return idleMs != 0 && nowMs - busyAt >= idleMs; }

    // The mark, written once the song is saved. False if there is none to write or it could not be.
    bool writeMark(const char* why) const {
        if (mark.empty()) return false;
        std::FILE* f = std::fopen(mark.c_str(), "w");
        if (!f) return false;
        const bool ok = std::fprintf(f, "%s\n", why) > 0;
        return std::fclose(f) == 0 && ok;
    }
};

} // namespace apotheca
