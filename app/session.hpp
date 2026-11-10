#pragma once
// What every screen shares: the engine, the project, which screen is showing,
// and the few actions that are the same everywhere.

#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "chords.hpp"
#include "recorder.hpp"
#include "transfer.hpp"
#include "project.hpp"

namespace apotheca {

enum class View { Song, Pattern, Device, Effects };

// Somewhere to record from: a USB microphone or interface. The app gives the
// session one (main.cpp, through SDL); the tests give it a made-up one.
struct AudioInput {
    virtual ~AudioInput() = default;
    virtual bool start() = 0;                     // false: nothing to record from
    virtual void stop() = 0;
    virtual int read(float* out, int most) = 0;   // mono samples at 48 kHz that have come in since last asked
};

class Session {
public:
    Session(Engine& engine, Project& project) : engine(engine), project(project), packs(1, builtInPack()) {}
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Engine& engine;
    Project& project;
    View view = View::Pattern;

    // START. `song` plays the scenes in order from `project.scene`; otherwise
    // that one scene loops.
    void togglePlay(bool song);

    // Too loud, and no time to find out why: everything stops and the
    // output shuts, at once, for as long as `held`. Let go, and it opens
    // again when what was sounding has died away.
    void panic(bool held);

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
    void useChordFolder(const std::string& folder) { chordFolder_ = folder; packs = loadChordPacks(folder); }
    int packIndex() const;       // the song's pack; the built-in one if it is not here
    void choosePack(int index);  // wraps round

    // ---- effects ---------------------------------------------------------------------
    // Two slots on every track and two on the master (track kMasterTrack).
    EffectData& effectData(int track, int slot) { return track == kMasterTrack ? project.masterEffects[slot] : project.tracks[track].effects[slot]; }
    const EffectData& effectData(int track, int slot) const { return track == kMasterTrack ? project.masterEffects[slot] : project.tracks[track].effects[slot]; }
    void setEffect(int track, int slot, EffectKind kind); // goes in at its usual settings
    void setEffectParam(int track, int slot, int index, float value); // 0..1

    // ---- recordings on drum lanes ----------------------------------------------------
    // A lane of a drum track plays the built-in drum, or a WAV file from the
    // sample folder. A lane can be FITTED: stretched, at the same pitch, to
    // last a number of steps at the song's tempo. That is worked out on
    // another thread and swapped in when ready; when the tempo changes it is
    // worked out again.
    void useSampleFolder(const std::string& folder) { sampleFolder_ = folder; }
    std::vector<std::string> sampleNames() const;
    void setLaneSample(int track, int lane, const std::string& name); // "" brings the drum back
    void setLaneFit(int track, int lane, int steps);                  // 0: as recorded
    // Catch: the last bar the song played (everything, as it was heard)
    // becomes a recording, "catch 1", "catch 2"..., and goes on this lane
    // fitted to one bar. Returns its name, or "" if there was only silence
    // to catch or nowhere to keep it.
    std::string catchLastBar(int track, int lane);
    struct LaneSound {
        std::shared_ptr<const std::vector<float>> recording; // as it came off the card; null on a built-in lane
        std::vector<float> peaks; // its outline, for drawing
        float seconds = 0.f;
        bool missing = false;     // the song names a file that is not here
        bool waiting = false;     // a fitted version is wanted and not yet in the engine
        float fittedTempo = 0.f;  // the tempo the one in the engine was made for
        int fittedSteps = 0;
        unsigned version = 0;     // goes up whenever the lane's sound changes
    };
    const LaneSound& laneSound(int track, int lane) const { return sounds_[track][lane]; }
    // A sampler track's one recording, and its settings (sampler.hpp).
    void setTrackSample(int track, const std::string& name); // "" for none
    void setSamplerSetting(int track, int id, int value);
    const LaneSound& trackSound(int track) const { return sounds_[track][0]; }
    bool stretching() const;  // some lane is still waiting for its fitted version
    void finishStretching();  // wait for them all. For tests, and before leaving.

    // ---- files over WiFi -------------------------------------------------------------
    // A web page in a browser on the same network: recordings, chord packs
    // and songs go on and come off (transfer.hpp). Off until turned on.
    bool toggleTransfer(unsigned seed);  // true if it is now on
    bool transferOn() const { return transfer_.running(); }
    std::string transferAddress() const { return transfer_.address(); } // what to type into the browser

    // ---- recording -------------------------------------------------------------------
    // Record from the input onto a drum lane, or onto a sampler track (the
    // lane is then ignored). The first call starts it waiting for sound; the
    // next stops it. It also stops by itself at ten seconds. What was
    // recorded is saved as "rec 1", "rec 2"... in the sample folder.
    enum class Rec { Idle, NoInput, Waiting, Recording };
    void useAudioInput(AudioInput* input) { input_ = input; }
    Rec toggleRecording(int track, int lane); // what it is doing now
    Rec recording() const { return rec_; }
    float recordSeconds() const { return recorder_.seconds(); }
    float recordLevel() const { return recorder_.level(); }
    const std::string& lastRecorded() const { return lastRecorded_; } // "" if the last one came to nothing

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
    // Give the open song a new two-word name (there is no keyboard to type
    // one). Its file and its revs follow. Returns the new name.
    std::string renameSong();
    // Take a song off the list. Its file and revs are not destroyed: they
    // are moved to a "deleted" folder inside the song folder. Deleting the
    // open song starts a new one.
    bool deleteSong(const std::string& name);
    // The tempo, from anywhere. Held to what the engine can play.
    void setTempo(float bpm);

    // ---- revs -------------------------------------------------------------------------
    // A rev is the song as it was at one moment, kept beside it and never
    // written to again. Saving is still automatic; a rev is a place to come
    // back to.
    int saveRev();                      // returns the new rev's number, or 0 if it could not be kept
    std::vector<int> revs() const;      // of the open song, newest first
    std::string revWhen(int rev) const; // "12 NOV 14:05", or "" if it is not there
    // Go back to a rev. If no rev holds the song as it is now, it is kept
    // as a new rev first, so going back loses nothing.
    bool openRev(int rev);

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
    std::string revPath(int rev) const;
    std::string freshName();
    std::string folder_, songName_ = "untitled", savedText_;
    std::uint32_t nameSeed_ = 1;
    Uint32 nextSaveAt_ = 0;

    void loadLane(int track, int lane);
    void finishRecording();
    Transfer transfer_;
    unsigned seenReceived_ = 0;
    std::string chordFolder_;
    AudioInput* input_ = nullptr;
    Recorder recorder_;
    Rec rec_ = Rec::Idle;
    int recTrack_ = 0, recLane_ = 0;
    std::string lastRecorded_;
    void loadTrackSample(int track);
    void install(int track, int lane, const std::vector<float>& samples);
    bool wantsFitting(int track, int lane) const;
    void startJob(int track, int lane);
    void collectJob(bool wait);
    struct Job {
        int track = 0, lane = 0, frames = 0;
        unsigned version = 0;
        float tempo = 0.f;
        std::shared_ptr<const std::vector<float>> in;
        std::vector<float> out;
        std::atomic<bool> done{false};
        std::thread thread;
    };
    std::string sampleFolder_;
    LaneSound sounds_[kNumTracks][kDrumLanes];
    std::unique_ptr<Job> job_;
    float steadyTempo_ = 0.f;  // the tempo, once it has stopped moving
    float seenTempo_ = 0.f;
    Uint32 tempoMovedAt_ = 0;
};

} // namespace apotheca
