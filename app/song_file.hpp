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

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "project.hpp"

namespace apotheca {

std::string songText(const Project& project);
// One track as the song file writes it (songText is these, after the song's own lines).
std::string trackText(const TrackData& track);

// songText, made a track at a time and remembered. A track whose bytes are as they were when its
// text was made is not written again, so asking for the text of a song nothing has changed in costs
// a compare of its tracks' memory, not the writing of it (a 2.5 MB song: 1 ms against 70 on the
// desk). What comes back is songText(project) to the byte; nothing has to say what it changed.
// The kept copies take a track's size each (about 1 MB) for the tracks the song has.
class SongTextCache {
public:
    const std::string& text(const Project& project);
    // Track t's part of the last text(): from its "track" line to the next one's. Empty past the song's last track.
    const std::string& track(int t) const;

private:
    struct Part {
        std::unique_ptr<TrackData> seen; // the track as it was when `text` was made
        std::string text;
    };
    Part parts_[kNumTracks];
    int count_ = 0;
    std::string head_, text_;
    bool made_ = false;
};

// How many tracks songText, trackText and SongTextCache have written, and how many bytes of text
// parseSong has read, since the program began: for the tests, which hold what a gesture costs by them.
std::uint64_t tracksWritten();
std::uint64_t bytesRead();

// Fills `project` from text. Values out of range are held to their limits and
// lines it does not understand are skipped. False (and `project` untouched)
// if the text is not a song at all.
bool parseSong(const std::string& text, Project& project);

// A song's text cut where parseSong cuts it: the song's own lines (`head`, from the first line),
// then each track's, from its "track" line to the next "track" line, as parseSong counts them.
// Each part then reads alone as it reads in the whole. False where it would not: a track of a
// kind this version does not know, a thirteenth, a song's own line (tempo, repeats...) among a
// track's, or text that is not a song. songText's text always cuts.
bool cutSong(const std::string& text, std::string& head, std::vector<std::string>& tracks);

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
