#pragma once
// A stand-in for the firmware's `apotheca-wifi` (docs/plans/firmware-plan.md, section 14), for the
// tests: a shell script in a folder of the test's own that answers from files there.
//
//   status          says FOLDER/status          (none: "off")
//   scan            says FOLDER/scan
//   join NAME       keeps what came on its stdin in FOLDER/password; the password in FOLDER/right (or
//                   none there, for an open or saved one) makes the status "joined NAME 192.168.1.7
//                   apotheca.local", any other "wrong-password NAME"
//   forget NAME     the status "off"
//   hotspot         the status "hotspot apotheca 192.168.4.1 apotheca.local"
//   up, down        nothing more
// Every call's words are a line of FOLDER/log, as the program was given them (so a test can see the
// password was never among them).

#include <sys/stat.h>

#include <cstdio>
#include <string>

namespace apotheca {

inline void wifiFile(const std::string& path, const std::string& text) {
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

inline std::string wifiFileText(const std::string& path) {
    std::string text;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char piece[512];
        for (std::size_t n; (n = std::fread(piece, 1, sizeof piece, f)) > 0;) text.append(piece, n);
        std::fclose(f);
    }
    return text;
}

// Writes the script into `folder` and gives its path.
inline std::string wifiStandIn(const std::string& folder) {
    const std::string path = folder + "/apotheca-wifi";
    wifiFile(path, "#!/bin/sh\n"
                   "d='" + folder + "'\n"
                   "echo \"$@\" >> \"$d/log\"\n"
                   "case \"$1\" in\n"
                   "status) if [ -f \"$d/status\" ]; then cat \"$d/status\"; else echo off; fi ;;\n"
                   "scan) [ -f \"$d/scan\" ] && cat \"$d/scan\" ;;\n"
                   "join) cat > \"$d/password\"\n"
                   "  if [ \"$(cat \"$d/password\")\" = \"$(cat \"$d/right\" 2>/dev/null)\" ]; then echo \"joined $2 192.168.1.7 apotheca.local\" > \"$d/status\"\n"
                   "  else echo \"wrong-password $2\" > \"$d/status\"; fi ;;\n"
                   "forget) echo off > \"$d/status\" ;;\n"
                   "hotspot) echo \"hotspot apotheca 192.168.4.1 apotheca.local\" > \"$d/status\"\n"
                   "  echo \"hotspot apotheca\" ;;\n"
                   "up|down) ;;\n"
                   "*) exit 2 ;;\n"
                   "esac\n"
                   "exit 0\n");
    chmod(path.c_str(), 0755);
    return path;
}

} // namespace apotheca
