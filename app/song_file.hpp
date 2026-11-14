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
#include <utility>
#include <vector>

#include "project.hpp"

namespace apotheca {

std::string songText(const Project& project);
// One track as the song file writes it (songText is these, after the song's own lines).
std::string trackText(const TrackData& track);

// Fills `project` from text. Values out of range are held to their limits and
// lines it does not understand are skipped. False (and `project` untouched)
// if the text is not a song at all.
bool parseSong(const std::string& text, Project& project);

// Recordings were renamed or moved (`first` to `second`, in the order it happened): the lines of
// this song that name one ("sample LANE FIT NAME" on a drum track, "sound NAME" on a sampler)
// name the new one. Everything else is left byte for byte, lines this version does not know
// included. True if anything changed; text that is not a song is left alone. The names are the
// caller's to check (sampleNameOk): Session::savedSongsFollow does.
bool renameRecordings(std::string& text, const std::vector<std::pair<std::string, std::string>>& renames);

// A song's text with its track `track` (counted as parseSong counts them) taken out, and the
// tracks after it a place up: what the steps of undo hold once a track is deleted. False if
// the text has no such track (it is then as it was).
bool dropTrackText(std::string& text, int track);

// A song and its revs moved from `folder` into "deleted" inside it, under a name nothing there has
// ("ash well", else "ash well 2", ...). False if there is no such song or a file would not move.
bool binSong(const std::string& folder, const std::string& name);

bool writeTextFile(const std::string& path, const std::string& text); // whole or not at all
bool readTextFile(const std::string& path, std::string& text);

} // namespace apotheca
