#pragma once
// What every screen shares: the engine, the project, which screen is showing,
// and the few actions that are the same everywhere.

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "chords.hpp"
#include "project.hpp"

namespace apotheca {

enum class View { Song, Pattern, Device };

class Session {
public:
    Session(Engine& engine, Project& project) : engine(engine), project(project), packs(1, builtInPack()) {}

    Engine& engine;
    Project& project;
    View view = View::Pattern;

    // START. `song` plays the scenes in order from `project.scene`; otherwise
    // that one scene loops.
    void togglePlay(bool song);

    // Tell the engine how long the song is, if that has changed.
    void syncSong();

    // Give the next free track an instrument. False when all twelve are used.
    bool addTrack(TrackKind kind);

    // Hear a synth note for a moment. update() lets go of it.
    void audition(int track, int pitch, float velocity);
    void update(Uint32 nowMs);

    // ---- chord packs ----------------------------------------------------------------
    // There is always at least the built-in pack. The song remembers which
    // one it picks from, by name.
    std::vector<ChordPack> packs;
    void useChordFolder(const std::string& folder) { packs = loadChordPacks(folder); }
    int packIndex() const;       // the song's pack; the built-in one if it is not here
    void choosePack(int index);  // wraps round

    // ---- songs on disk ------------------------------------------------------------
    // Nothing here needs asking for. The song being worked on is written to
    // its file a couple of seconds after any change, and again on the way
    // out; the last one open comes back at the next start.

    // Turn saving on: songs live in this folder. Opens the last song, or
    // starts a new one. `seed` picks the names new songs are given.
    void useSongFolder(const std::string& folder, std::uint32_t seed);
    const std::string& songName() const { return songName_; }
    std::vector<std::string> songNames() const; // most recently saved first
    void newSong();                             // saves the current one first
    bool openSong(const std::string& name);     // likewise
    void saveNow();                             // only writes if something changed

    // Tell the engine everything in the project, from nothing.
    void applyProject();

    // Write a whole pattern to the engine, replacing what it had.
    void sendDrumPattern(int track, int scene);
    void sendNotePattern(int track, int scene);

private:
    struct Sounding { int track = -1, pitch = 0; Uint32 until = 0; };
    Sounding sounding_[8];
    Uint32 now_ = 0;
    int sentSceneCount_ = 1;

    std::string pathOf(const std::string& name) const;
    std::string freshName();
    std::string folder_, songName_ = "untitled", savedText_;
    std::uint32_t nameSeed_ = 1;
    Uint32 nextSaveAt_ = 0;
};

} // namespace apotheca
