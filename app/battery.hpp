#pragma once
// The handheld's battery, as Linux reports it: a folder for each power
// supply under /sys/class/power_supply, with "capacity" (0..100) and
// "status" ("Charging", "Discharging", "Full") files in it. -1 if there is
// no battery to read (a desk computer).

#include <dirent.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace apotheca {

inline int batteryPercent(bool* charging = nullptr, const std::string& root = "/sys/class/power_supply") {
    int found = -1;
    if (charging) *charging = false;
    DIR* dir = opendir(root.c_str());
    if (!dir) return -1;
    while (const dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string base = root + "/" + entry->d_name;
        std::FILE* file = std::fopen((base + "/capacity").c_str(), "r");
        if (!file) continue;
        int percent = -1;
        const bool read = std::fscanf(file, "%d", &percent) == 1;
        std::fclose(file);
        if (!read || percent < 0 || percent > 100) continue;
        found = percent;
        if (charging) {
            char status[32] = "";
            if (std::FILE* s = std::fopen((base + "/status").c_str(), "r")) {
                if (std::fscanf(s, "%31s", status) != 1) status[0] = 0;
                std::fclose(s);
            }
            *charging = std::strcmp(status, "Charging") == 0 || std::strcmp(status, "Full") == 0;
        }
        break;
    }
    closedir(dir);
    return found;
}

// The same supply's voltage_now, in microvolts (-1: none to read).
inline long batteryMicrovolts(const std::string& root = "/sys/class/power_supply") {
    long found = -1;
    DIR* dir = opendir(root.c_str());
    if (!dir) return -1;
    while (const dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string base = root + "/" + entry->d_name;
        std::FILE* cap = std::fopen((base + "/capacity").c_str(), "r");
        if (!cap) continue; // (no "capacity": not a battery)
        std::fclose(cap);
        long microvolts = -1;
        if (std::FILE* file = std::fopen((base + "/voltage_now").c_str(), "r")) {
            if (std::fscanf(file, "%ld", &microvolts) == 1 && microvolts >= 0) found = microvolts;
            std::fclose(file);
        }
        break; // the first battery's: there is one
    }
    closedir(dir);
    return found;
}

} // namespace apotheca
