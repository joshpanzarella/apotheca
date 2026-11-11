// Small things shared by the files Session is written in:
//   session.cpp          playing, tracks, patterns, mutes, undo, automation, effects, sending the song to the engine
//   session_songs.cpp    songs on disk, revisions, and the handheld's own settings files
//   session_synth.cpp    a synth's sound: settings, macros, presets, the sound designer page, a USB keyboard
//   session_samples.cpp  recordings: the lanes' sounds, recording, catching, fitting to tempo, the WiFi page and updates
#pragma once

#include <sys/stat.h>

#include <string>

namespace apotheca {

inline bool fileExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

} // namespace apotheca
