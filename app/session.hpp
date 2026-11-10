#pragma once
// What every screen shares: the engine, the project, which screen is showing,
// and the few actions that are the same everywhere.

#include <SDL.h>

#include "apotheca/engine.hpp"
#include "project.hpp"

namespace apotheca {

enum class View { Song, Pattern, Device };

class Session {
public:
    Session(Engine& engine, Project& project) : engine(engine), project(project) {}

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

    // Write a whole pattern to the engine, replacing what it had.
    void sendDrumPattern(int track, int scene);
    void sendNotePattern(int track, int scene);

private:
    struct Sounding { int track = -1, pitch = 0; Uint32 until = 0; };
    Sounding sounding_[8];
    Uint32 now_ = 0;
    int sentSceneCount_ = 1;
};

} // namespace apotheca
