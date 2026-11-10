#pragma once
// A song as a text file. One fact per line, so a file can be read by eye,
// compared, and survive a later version adding lines this one does not know.
//
//   apotheca-song 1
//   tempo 120
//   repeats 2 2 4 1 ...
//   track drums
//   lanes 1 1 0.7 ...
//   scene 1
//   seeds ...
//   hit <lane> <step> <level> <nudge> <chance> <from dice 0/1>
//   track phazed
//   preset 7
//   macros 0.5 ...
//   settings 0 -1 ...
//   scene 1
//   note <step> <key> <length> <level>
//
// Lanes, steps and scenes are written from 1, as on screen.

#include <string>

#include "project.hpp"

namespace apotheca {

std::string songText(const Project& project);

// Fills `project` from text. Values out of range are held to their limits and
// lines it does not understand are skipped. False (and `project` untouched)
// if the text is not a song at all.
bool parseSong(const std::string& text, Project& project);

bool writeTextFile(const std::string& path, const std::string& text); // whole or not at all
bool readTextFile(const std::string& path, std::string& text);

} // namespace apotheca
