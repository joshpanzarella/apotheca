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
#include "midi.hpp"
#include "designer.hpp"
#include "project.hpp"

namespace apotheca {

struct Project;
// How many frames the song lasts once through, at this sample rate (bounce.cpp).
long long songFrames(const Project& project, float sampleRate);

// WHERE A BOUNCE ENDS (bounce.cpp). After the song's last step the bounce
// runs on for as long as there is sound: echoes, a hall, drums ringing out.
//   - Sound is anything at or over kBounceFloor: one step of a 16-bit file,
//     90 dB under full. Less than that is not in the file anyway.
//   - It is over when there has been none for kBounceQuietSeconds. That must
//     stay longer than the longest gap between two echoes (3 seconds: half a
//     bar at 40 BPM, the longest DELAY and TAPE go), or a sparse echo would
//     be taken for the end.
//   - The file ends kBounceAfterSeconds after the last sound.
//   - A tail never runs past kBounceCapSeconds: the last kBounceFadeSeconds
//     before the cap are faded out, so it is not a click. The cap is longer
//     than any delay or reverb takes to die away at any setting
//     (kLongestTailSeconds in the engine: effects_test measures it).
constexpr float kBounceFloor = 3.1e-5f;
constexpr float kBounceQuietSeconds = 4.f;
constexpr float kBounceAfterSeconds = 0.5f;
constexpr float kBounceCapSeconds = 480.f;
constexpr float kBounceFadeSeconds = 1.f;

// Listens to a bounce's tail, block by block, and says when it is over and
// how much of it belongs in the file. No engine, no file: it can be tested
// with made-up sound.
class BounceTail {
public:
    explicit BounceTail(float sampleRate, float capSeconds = kBounceCapSeconds);
    // The next block of the tail: interleaved stereo, `frames` of it. The fade
    // before the cap is put on the block here (and nothing is left past the
    // cap). False: that was the last block; do not render another.
    bool take(float* block, int frames);
    long long seen() const { return seen_; } // frames taken so far
    // How many of them belong in the file, as far as is known yet: up to
    // kBounceAfterSeconds past the last sound, and never past the cap.
    long long keep() const;
    bool capped() const { return seen_ >= cap_; } // it ran into the cap, and was faded out

private:
    long long seen_ = 0, lastSound_ = 0, cap_, fade_, quiet_, after_;
};

enum class View { Song, Pattern, Device, Effects, Settings };

// Somewhere to record from: a USB microphone or interface. The app gives the
// session one (main.cpp, through SDL); the tests give it a made-up one.
struct AudioInput {
    virtual ~AudioInput() = default;
    virtual bool start() = 0;                     // false: nothing to record from
    virtual void stop() = 0;
    virtual int read(float* out, int most) = 0;   // mono samples at 48 kHz that have come in since last asked
};

// A hit's step effects, as the engine is told them (Command::SetStepFx).
Command stepFxCommand(int track, int scene, int bar, int lane, int step, const StepFx& fx);

class Session {
public:
    Session(Engine& engine, Project& project);
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
    // Another instrument on a track that is there. Its patterns stay: notes carry over between the
    // instruments played from the keys, and a drum pattern waits untouched under a synth (and the
    // other way about). The new instrument starts on its first sound; the track's effects, name and
    // mute stay; its automation goes (the knobs it moved are not the new instrument's). The song stops.
    bool setTrackKind(int track, TrackKind kind);
    // Take a track out of the song, with everything on it; the tracks after it move down one.
    // The song stops. It cannot be undone, and the last track cannot be removed.
    bool removeTrack(int track);

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
    // `track` may be kMasterTrack, or kEchoBus / kSpaceBus (one slot each: what the bus holds).
    EffectData& effectData(int track, int slot) { return busOfTrack(track) >= 0 ? project.busEffects[busOfTrack(track)] : (track == kMasterTrack ? project.masterEffects[slot] : project.tracks[track].effects[slot]); }
    const EffectData& effectData(int track, int slot) const { return busOfTrack(track) >= 0 ? project.busEffects[busOfTrack(track)] : (track == kMasterTrack ? project.masterEffects[slot] : project.tracks[track].effects[slot]); }
    void setEffect(int track, int slot, EffectKind kind); // goes in at its usual settings
    void setEffectParam(int track, int slot, int index, float value); // 0..1
    // How much of a track goes to a bus (0 ECHO, 1 SPACE), 0..1. Written as automation like any setting.
    void setSend(int track, int bus, float value);

    // ---- recordings on drum lanes ----------------------------------------------------
    // A lane of a drum track plays the built-in drum, or a WAV file from the
    // sample folder. A lane can be FITTED: stretched, at the same pitch, to
    // last a number of steps at the song's tempo. That is worked out on
    // another thread and swapped in when ready; when the tempo changes it is
    // worked out again.
    void useSampleFolder(const std::string& folder) { sampleFolder_ = folder; }
    std::vector<std::string> sampleNames() const;
    // Delete a recording's file from the samples folder. Lanes that were playing it go back to
    // their built-in drum (a sampler: to none). False if there is no such recording.
    bool deleteSample(const std::string& name);
    // A recording's file was changed from outside (the WiFi page), and the open song follows.
    // Renamed or moved (`from` and `to`): every lane and sampler track that used the old name
    // uses the new one, and goes on playing the sound it has: nothing is read again. Deleted
    // (`to` empty): they keep the name and are marked missing, as they would be the next time
    // the song was opened. Sent (`from` empty): one that was missing by that name is read.
    // A recording of that base name that turns up anywhere (`to` in another folder) is taken by
    // one that was missing, if it is the only one (findMissing).
    void recordingChanged(const std::string& from, const std::string& to);
    // Recordings that were renamed or moved while a song was shut. A lane or sampler track whose
    // recording is not where the song says takes the one of the same base name (the part after
    // the folder) from the root of the samples folder or one of its folders, if there is EXACTLY
    // ONE and it reads. None, or two: it stays missing, never a guess. Run when a song or a rev
    // is opened; the changed name is saved as any change is. `label`: only those of that base
    // name. Returns how many were found. The folder is listed once, and only if something is missing.
    int findMissing(const std::string& label = "");
    int sampleLookups() const { return lookups_; } // how many times findMissing has listed the folder (tests)
    // The page renamed or moved recordings (`first` to `second`, in order): every saved song and
    // rev in the song folder that names one names the new one (renameRecordings), written beside
    // itself and moved into place, with the date it had. Pairs with a name that is no
    // recording's (one sent or deleted has an empty side) are passed over. The open song's own file is left to
    // recordingChanged and the autosave. Returns how many files were written.
    int savedSongsFollow(const std::vector<std::pair<std::string, std::string>>& renames);
    void setLaneSample(int track, int lane, const std::string& name); // "" brings the drum back
    void setLaneFit(int track, int lane, int steps);                  // 0: as recorded
    // A lane's sound as slices, for chopping a break. How many there are: sixteen,
    // or one to a step if the recording is fitted to more than a bar (32 for two
    // bars, 64 for four). And whether a hit plays just its slice and stops, or
    // runs on until the next hit on the lane.
    int laneSlices(int track, int lane) const;
    void setLaneOneSlice(int track, int lane, bool on);
    // Asked for from another screen (the kit's): the drum grid opens on this lane's slices.
    int wantSlices = -1; // a lane, or -1
    // Asked for from the SONG screen: the keyboard for the current track's name (it lives on the SET tab).
    // When it is done, the SONG screen comes back.
    bool wantTrackName = false;
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

    // ---- updating Apotheca over WiFi ----------------------------------------------------
    // A new program sent to the page is only set aside. It is installed
    // when someone accepts it on the handheld, and used at the next start;
    // the launcher keeps the old one and goes back to it if the new one
    // does not get going (scripts/Apotheca.sh).
    void useProgramFolder(const std::string& folder);
    bool updateWaiting() const { return updateWaiting_; }   // one has arrived and wants an answer
    float updateMegabytes() const { return updateMegabytes_; }
    bool acceptUpdate();   // true if it is now in place for the next start
    void discardUpdate();
    void startedFine();    // call once the program has been running a few seconds: this start worked

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
    // How loud Apotheca is, 0..1, as on a volume knob (half-way is a good
    // deal quieter than full, the way ears hear it). Kept beside the
    // program, so it is the same at the next start.
    float volume() const { return volume_; }
    void setVolume(float volume);
    void loadVolume(float ifNone); // at start: what was kept, or this
    // Two more things about the handheld, kept in "settings.txt" beside the
    // program: how fast the stick is (1 is as first built) and how many
    // octaves the piano roll shows (1, 2 or 3).
    static constexpr float kStickSlowest = 0.25f, kStickFastest = 1.5f;
    float stickSpeed() const { return ui_stickSpeed(); }
    void setStickSpeed(float speed);
    int octavesShown() const { return octaves_; }
    void setOctavesShown(int octaves);
    void loadSettings();

    // ---- bouncing (bounce.cpp) ---------------------------------------------------------
    // The song, or its tracks, written out as WAV files in the "bounces"
    // folder. Worked out off to one side, not recorded as it plays, so it can
    // use the best quality whatever the handheld could keep up with.
    enum class Bounce { Song, EachTrack, ThisTrack };
    void useBounceFolder(const std::string& folder) { bounceFolder_ = folder; }
    bool startBounce(Bounce what); // false if one is under way, or there is nowhere to put it
    bool bouncing() const;
    float bounceProgress() const;  // 0..1: how much of the playing of the song is done (the tails are not counted: nobody knows how long they will be)
    // While a tail is being worked out: how many seconds of it there are so
    // far. Below zero the rest of the time. The screen says this in place of
    // a percentage, since a tail has no known end to be a part of.
    float bounceTail() const;
    void useBounceCap(float seconds) { bounceCap_ = seconds; } // the longest tail (kBounceCapSeconds unless told otherwise: the tests)
    void pretendBounce(float progress, float tailSeconds);    // as if one were under way, this far along: for a still of the screen
    int bounceFiles() const;       // once done: how many files were written
    std::string bounceLast() const; // ...and the name of the last one
    void finishBounce();           // wait for it (the tests, and closing down)
    void cancelBounce();

    // ---- automation -------------------------------------------------------------------
    // With WRITE on and the song playing, moving a macro or a track effect's
    // setting records it: its value goes into the cell that is playing, on
    // the step that is playing. From then on the pattern moves it. A lane is
    // one of the track's twenty-four (the engine's kAutoLanes).
    void setWriting(bool on) { project.writing = on; }
    bool writing() const { return project.writing; }
    void setAutomation(int track, int scene, int lane, int index, float value); // below 0: nothing there
    void clearAutomation(int track, int scene, int lane);                       // -1: every lane of the cell
    bool hasAutomation(int track, int scene, int lane) const;
    // What the lane's setting is just now: the value written on the step that is playing, or `resting`.
    float automated(int track, int lane, float resting) const;

    // ---- undo ---------------------------------------------------------------------------
    // What is in the patterns (hits, notes, chords, how long each cell is,
    // how often each scene plays) can be taken back, twenty steps deep, and
    // put forward again. One step is one burst of editing: everything
    // changed until nothing has changed for seven tenths of a second (a
    // whole paint stroke, a roll of the dice, a cell emptied). The song
    // keeps playing. Sounds, effects and settings are not part of it.
    static constexpr int kUndoLevels = 20;
    bool undo();
    bool redo();
    int undoSteps() const;
    int redoSteps() const { return static_cast<int>(redo_.size()); }
    void noteEdit(); // something in a pattern has just changed

    // ---- mute, solo, names -----------------------------------------------------------
    // A muted track is silent. While any track is soloed, only soloed ones
    // are heard, muted or not. Both act at once; the song keeps mutes only.
    void setMuted(int track, bool muted);
    void setSolo(int track, bool solo);
    bool heard(int track) const; // as things stand, does this track sound?
    // What the player calls a track: capitals, digits, spaces, - and _, ten
    // at most. Empty puts back its instrument's name.
    void setTrackName(int track, const std::string& name);
    void hearTrack(int track); // X on the SONG screen: its kick, or a middle C
    void sendEffects(int track); // every slot of a track, the master or a bus, to the engine
    void sendMutes(); // tell the engine how things stand (after a song is opened)

    // ---- a synth's sound ---------------------------------------------------------------
    // One place for changing a synth track's patch, used by the Device screen
    // and by the sound-designer page alike.
    void setSynthSetting(int track, int id, int value);
    void setSynthMacro(int track, int macro, float value);
    // A built-in preset or one of the player's own. The macros go back to the middle.
    void chooseSynthPreset(int track, int index);
    // ROLL: a new generated sound, on a synth that has such a thing (QMOD). False on the others.
    bool rollSynth(int track, unsigned seed);
    void sendSynthPatch(int track);
    // The player's own presets: files in this folder, "NAME.phazed" and "NAME.twin".
    void usePresetFolder(const std::string& folder);
    // Keep the track's sound, as it is now with its macros, under a name.
    // The track is then on that preset. False if the name is no good or it could not be written.
    bool savePreset(int track, const std::string& name);
    // The sound-designer page's way in (see designer.hpp); the WiFi page serves it.
    Designer& designer() { return designer_; }

    // ---- a USB keyboard ---------------------------------------------------------------
    // Keys play the track being worked on (midi.hpp). With recording on, and
    // the song playing, what is played is also written into the cell that is
    // playing: on the step it was played in, as long as it was held.
    void startMidi(const std::string& folder = "/dev/snd") { midi_.start(&engine, folder); }
    int midiKeyboards() const { return midi_.devices(); }
    bool midiRecording() const { return midiRecord_; }
    void setMidiRecording(bool on);
    // The last key played and not yet asked about, or -1. The piano roll's cursor goes to it.
    int takeMidiKey() { const int key = midiKey_; midiKey_ = -1; return key; }
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
    // How many times the engine fell behind (a full command queue) and was sent the whole song again.
    int resyncs() const { return resyncs_; }

    // Write a whole cell to the engine (every bar of it, and its length),
    // replacing what it had.
    void sendDrumPattern(int track, int scene);
    void sendNotePattern(int track, int scene);
    void sendNoteBar(int track, int scene, int bar); // one bar of it: what the piano roll changes
    // How long a track's cell is in a scene: 1, 2, 4 or 8 bars.
    // How many steps a bar of a scene has, 1 to 16, for every track. Thirteen makes a thirteen-step loop.
    void setSceneSteps(int scene, int steps);
    void setCellBars(int track, int scene, int bars);

private:
    struct Sounding { int track = -1, pitch = 0; Uint32 until = 0; };
    Sounding sounding_[8];
    Uint32 now_ = 0;
    unsigned droppedSeen_ = 0; // engine.dropped() when the song was last sent whole
    int resyncs_ = 0;
    int sentSceneCount_ = 1;

    std::string pathOf(const std::string& name) const;
    std::string revPath(int rev) const;
    std::string freshName();
    std::string folder_, songName_ = "untitled", savedText_;
    std::uint32_t nameSeed_ = 1;
    Uint32 nextSaveAt_ = 0;

    void loadLane(int track, int lane);
    void sendLanePart(int track, int lane); // tell the engine how much of its sound a hit on the lane plays
    void finishRecording();
    float volume_ = 1.f;
    int octaves_ = 2;
    struct BounceJob {
        std::unique_ptr<Project> song;
        Bounce what = Bounce::Song;
        int track = 0, files = 0;
        std::string sampleFolder, base, last;
        float cap = kBounceCapSeconds;
        std::atomic<float> progress{0.f}, tail{-1.f};
        std::atomic<bool> done{false}, cancel{false};
        std::thread thread;
    };
    std::shared_ptr<BounceJob> bounce_;
    std::string bounceFolder_;
    float bounceCap_ = kBounceCapSeconds;
    std::string patternsNow() const;                 // the patterns as text: a step of undo
    void restorePatterns(const std::string& text);
    void sendTrackImage(int track);                  // every pattern of a track to the engine, in one piece
    std::uint32_t cellMark() const;                  // changes when the cell being worked on does
    std::vector<std::string> undo_, redo_;
    std::string settled_;                            // the patterns when things were last still
    bool editing_ = false, restoring_ = false, undoReady_ = false;
    Uint32 editedAt_ = 0;
    std::uint32_t cellMark_ = 0;
    void serveDesigner();
    std::string designSpec(TrackKind kind) const;
    std::string designState() const;
    Designer designer_;
    std::string presetFolder_;
    bool designHeld_[128] = {}; // keys the page is holding down
    int designHeldTrack_ = 0;
    unsigned designLooks_ = 0;
    Uint32 designSeenAt_ = 0;
    TrackKind designSpecKind_ = TrackKind::None;
    int designSpecPresets_ = -1;
    std::string designSpecText_ = "{}";
    void recordAutomation(int track, int lane, float value);
    void hearMidi();
    MidiInput midi_;
    bool midiRecord_ = false;
    int midiKey_ = -1;
    // Notes being recorded: where each was written, until its key is let go.
    struct Recording { bool on = false; int track = 0, scene = 0, bar = 0, slot = 0, startedAt = 0; };
    Recording recordingNote_[128];
    static float ui_stickSpeed();
    void keepSettings() const;
    Transfer transfer_;
    std::string programFolder_;
    unsigned seenUpdates_ = 0;
    bool updateWaiting_ = false;
    float updateMegabytes_ = 0.f;
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
    int lookups_ = 0;
    LaneSound sounds_[kNumTracks][kDrumLanes];
    std::unique_ptr<Job> job_;
    float steadyTempo_ = 0.f;  // the tempo, once it has stopped moving
    float seenTempo_ = 0.f;
    Uint32 tempoMovedAt_ = 0;
};

} // namespace apotheca
