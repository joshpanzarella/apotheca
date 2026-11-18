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
#include <istream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "project.hpp"

namespace apotheca {

// A number added to `out` as snprintf("%.<precision><conversion>") writes it, to the byte, for the
// conversions the song file uses ('f' with precision 0, 'g' with 1 to 9). Without snprintf for the
// usual numbers (speed plan batch 4: it was 65% of writing a song's text); save_test holds the two
// the same. Anything else (another conversion, a precision past 9) is snprintf's.
void appendNumber(std::string& out, double v, char conversion, int precision);

std::string songText(const Project& project);
// The song's own lines as the song file writes them (songText is these, then its tracks).
std::string songHeadText(const ProjectHead& head);
// One track as the song file writes it (songText is these, after the song's own lines).
std::string trackText(const TrackData& track);
// A track's patterns as trackText writes them, and nothing else: its `auto` lines, then its scenes
// (each as its kind writes them). Two tracks of one kind whose sounds and settings are the same
// have the same trackText exactly when these are the same: the parts between are the same, and
// no line of theirs begins as an `auto` or a `scene` line does.
std::string patternText(const TrackData& track);

struct TextUsed;
// songText, made a track at a time and remembered. A track whose bytes are as they were when its
// text was made is not written again, so asking for the text of a song nothing has changed in costs
// a compare of its tracks' memory, not the writing of it (a 2.5 MB song: 1 ms against 70 on the
// desk). What comes back is songText(project) to the byte; nothing has to say what it changed.
// The kept copies take a track's size each (about 1 MB) for the tracks the song has.
class SongTextCache {
public:
    SongTextCache();
    ~SongTextCache();
    const std::string& text(const Project& project);
    // Track t's part of the last text(): from its "track" line to the next one's. Empty past the song's last track.
    const std::string& track(int t) const;

private:
    struct Part {
        std::unique_ptr<TrackData> seen; // the track as it was when `text` was made
        std::unique_ptr<TextUsed> used;  // ...its bars and lanes that had something in them then
        std::string text;
    };
    Part parts_[kNumTracks];
    int count_ = 0;
    std::string head_, text_;
    bool made_ = false;
};

// One MOD of a track as the song file writes it: its `mod` line and, where its row is not as a new
// one's, its `modsteps` line. Empty when it is OFF. (Undo asks whether two MODs are the same by this.)
std::string modText(const ModSettings& mod, int index);

// A track's sound past its synth: its three effects, its two sends, its GLIDE FROM and its two MODs,
// in the lines a song file writes them in (fx, send, glidefrom, mod, modsteps), in that order. A
// preset file holds the same lines (presets.hpp): THE ONE WRITER AND THE ONE READER of them.
std::string soundText(TrackKind kind, const EffectData* effects, const float* send, int glideFrom, const ModSettings* mods);
struct SoundParts { // where readSoundLine puts what it reads: a track's, or a preset's
    TrackKind kind;
    EffectData* effects;      // kTrackSlots
    float* send;              // kBuses
    std::uint8_t* glideFrom;
    ModSettings* mods;        // kModsPerTrack
};
enum class SoundLine { NotOne, Taken, Refused }; // not one of those words; read; one of them, but not read (a slot past 3, an effect nobody knows...)
// One line, its first word already read. Every number is held to its limits as a double first. A MOD's
// target is not checked here: modsFit, once the whole track is read (its effects may come after).
// `oldSound`: a Twin Analog or QMOD track's, whose MODs on macros are not taken.
SoundLine readSoundLine(const std::string& word, std::istream& in, const SoundParts& to, bool oldSound = false);

// How many tracks songText, trackText and SongTextCache have written, and how many bytes of text
// parseSong has read, since the program began: for the tests, which hold what a gesture costs by them.
std::uint64_t tracksWritten();
std::uint64_t bytesRead();

// Fills `project` from text. Values out of range are held to their limits and
// lines it does not understand are skipped. False (and `project` untouched)
// if the text is not a song at all.
bool parseSong(const std::string& text, Project& project);

// A song's text read a part at a time, as parseSong reads it whole (Session::readStep: a step of
// undo reads only the tracks whose text differs from the track's now). The text must cut
// (cutSongAt); each part is read alone, as it reads in the whole.
struct SongReading;
class SongReader {
public:
    // `head` is made as new now. `slots[t]` is where track t's part goes (kNumTracks of them; only
    // those read are touched). `text` is kept by reference: it must outlast the reader.
    SongReader(const std::string& text, ProjectHead& head, TrackData* const* slots);
    ~SongReader();
    void readHead(std::size_t to); // the song's own lines, up to `to` (where its first track begins)
    // Track `index`'s part, from its "track" line at `from` to `to`, into slots[index], made blank
    // first; its MODs made to fit (modsFit), as parseSong does once the whole is read.
    void readTrack(int index, std::size_t from, std::size_t to);
    // After the parts: what parseSong does last to the song's own lines (an old song's "steps"), with
    // each scene's length in bars (Project::sceneBars over every track, read or not).
    void finish(const int bars[kMaxScenes]);

private:
    const std::string& text_;
    std::unique_ptr<SongReading> reading_;
};

// cutSong's places, nothing copied: the song's own lines are [0, starts[0]) (the whole, with no
// track), track t is [starts[t], starts[t + 1]) and the last runs to the end.
bool cutSongAt(const std::string& text, std::vector<std::size_t>& starts);

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
// ...and with its track `track` written twice, the copy right after it and the tracks after a place
// down: what the steps of undo hold once a track is duplicated (each step's own version of the track,
// so an undo puts both back to what the one was). False, the text as it was, if it has no such track
// or twelve already.
bool doubleTrackText(std::string& text, int track);

// A song and its revs moved from `folder` into "deleted" inside it, under a name nothing there has
// ("ash well", else "ash well 2", ...). False if there is no such song or a file would not move.
bool binSong(const std::string& folder, const std::string& name);

bool writeTextFile(const std::string& path, const std::string& text); // whole or not at all
bool readTextFile(const std::string& path, std::string& text);

} // namespace apotheca
