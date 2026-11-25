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
#include "song_file.hpp"
#include "synth_spec.hpp"
#include "transfer.hpp"
#include "wifi.hpp"
#include "midi.hpp"
#include "designer.hpp"
#include "lights.hpp"
#include "project.hpp"
#include "tap_tempo.hpp"

namespace apotheca {

struct Project;
// How many frames the song lasts once through, at this sample rate (bounce.cpp).
long long songFrames(const Project& project, float sampleRate);
// The song as a Standard MIDI file (Type 1): every scene as many times as it plays, one track per
// track, drums as General MIDI on channel 10 (bounce.cpp). For the tests as well as the bounce.
std::string midiFileBytes(const Project& song, const std::string& title);

// WHERE A BOUNCE ENDS (bounce.cpp). After the song's last step the bounce
// runs on for as long as there is sound: echoes, a hall, drums ringing out.
//   - Sound is anything at or over kBounceFloor: one step of a 16-bit file,
//     90 dB under full. Less than that is not in the file anyway.
//   - It is over when there has been none for kBounceQuietSeconds. That must
//     stay longer than the longest gap between two echoes
//     (kLongestEchoGapSeconds, 6 seconds: REVERSE at a half note and 40 BPM
//     gives a hit back two windows later; DELAY and TAPE wait 3 at most), or a
//     sparse echo would be taken for the end. It was 4 until REVERSE came.
//   - The file ends kBounceAfterSeconds after the last sound.
//   - A tail never runs past kBounceCapSeconds: the last kBounceFadeSeconds
//     before the cap are faded out, so it is not a click. The cap is longer
//     than any delay or reverb takes to die away at any setting
//     (kLongestTailSeconds in the engine: effects_test measures it).
constexpr float kBounceFloor = 3.1e-5f;
constexpr float kBounceQuietSeconds = 7.f;
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

enum class View { Song, Pattern, Device, Effects, Mods, Settings }; // (the order of the tabs; never saved by number)

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
    // DUPLICATE A TRACK (easywins batch 6): a copy of `track` (its instrument, sound, effects, sends,
    // MODs, mixer, mute, every scene's patterns and automation, its name) put right after it, the
    // tracks after a place down. Returns the copy's place, or -1 (no such track, or twelve already).
    // The song is sent to the engine again, as when a track is deleted; undo keeps its steps (each has
    // the track twice from then: doubleTrackText). The copy itself is not undone: hold B deletes it.
    int duplicateTrack(int track);
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

    // X HELD (owner, 2026-11-14): a recording is heard for as long as X is held, to its end, and
    // stops when X is let go: the sampler's note is let go (its RELEASE runs), a drum lane's
    // audition fades in 3 ms (Command::StopAudition). A tap still sounds kAuditionMs. Pressed
    // again, it starts again from the top. `owner` is the screen that asked: App lets go when
    // X is not held, when another screen is up or another track is picked (keepHearing), and
    // Play, a new song and panic let go too.
    static constexpr Uint32 kAuditionMs = 350;
    void auditionHeld(int track, int pitch, float velocity, const void* owner);  // a synth track (the sampler): this key
    // A chord's keys, held as one and let go together (the roll's X on a chord, gesture audit G33). heard_ names
    // the first; every key is held in its Sounding (kHeldOn).
    void auditionHeld(int track, const int* pitches, int count, float velocity, const void* owner);
    int keysHeld() const { int n = 0; for (const Sounding& s : sounding_) n += s.track >= 0 && s.until == kHeldOn ? 1 : 0; return n; } // (for the tests)
    void hearLaneHeld(int track, int lane, float level, const void* owner, float start = 0.f, float part = 0.f); // a drum lane
    void letGoHeard(Uint32 lasts = kAuditionMs); // X let go: it stops `lasts` after it began (a tap: kAuditionMs), or now if that has gone by
    void keepHearing(bool xHeld, const void* screen);
    // The list of recordings' preview (owner, 2026-11-15): what was begun held is let go to stop
    // `ms` after it began (half the recording), or, let go already and still sounding, its stop
    // is moved there (X: on to its end). The built-in drum, heard whole, has no stop to move.
    void hearFor(Uint32 ms);
    // What `owner` began is still heard: X still holds it, or it was let go and has not stopped.
    bool hearingOn(const void* owner) const { return heard_.track >= 0 && heard_.owner == owner && (heard_.held || (heard_.ends != 0 && now_ < heard_.ends)); }
    // What `owner` began stops now (the list's cursor moved on). Nothing else is touched.
    void stopHeard(const void* owner);
    bool hearingHeld() const { return heard_.track >= 0 && heard_.held; }
    int heardLane() const { return heard_.lane; } // the drum lane last heard (-1: a synth's key, or none)
    // When what X held stops, or stopped (now, for one cut by panic or a new song); 0 while X is
    // still held, when nothing has been heard, or when nothing stops it (the built-in drum tapped:
    // heard whole). The line across a recording's picture ends there.
    Uint32 heardEnds() const { return heard_.held ? 0 : heard_.ends; }
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
    // EACH TYPE'S OWN SETTINGS (owner, 2026-11-14): a slot remembers the settings of each type of its family it
    // has been switched away from (COMP -> DUCK -> COMP gives the COMP back as it was left, to the bit). Kept
    // in memory only, never in the song file: forgotten when another family or EMPTY goes in the slot
    // (setEffect), when another song opens (applyProject), when the track is given another instrument, and
    // with the track when it is deleted (the tracks after it take theirs with them). The FX screen asks it
    // (switchEffectType in fx_screen.hpp). `track` as effectData's: a track, kMasterTrack, or a bus.
    void rememberType(int track, int slot, const EffectData& settings); // under settings.kind
    bool rememberedType(int track, int slot, EffectKind kind, float out[kEffectParams]) const; // false: none kept
    void forgetTypes(int track, int slot);
    // How much of a track goes to a bus (0 ECHO, 1 SPACE), 0..1. Written as automation like any setting.
    void setSend(int track, int bus, float value);
    // GLIDE FROM, a synth track's: which key of the moment before its glides come from (kFromTop and
    // the rest, apotheca/glide_rule.hpp). A step of undo, as a send or a sound is.
    void setGlideFrom(int track, int type);
    // FEEL: a drum lane's HUMAN TIME and HUMAN LEVEL, each 0..100 (feel_rule.hpp). Live: the engine is told at
    // once, and hits asked for from then on go by it. A step of undo (seen by soundMark, as GLIDE FROM is).
    void setLaneFeel(int track, int lane, int time, int level);
    void setLaneSwing(int track, int lane, int amount); // SWING, 0..100 (held there)
    // A drum lane's volume, 0..1 (the drum grid's lane name, and the sound designer's page). Not a step
    // of undo, as it never was.
    void setLaneLevel(int track, int lane, float level);
    // CHOKE: a drum lane's group, kChokeNone, 1 or 2 (drums.hpp; anything else is NONE). The engine is told
    // at once. A step of undo (seen by soundMark, as FEEL is).
    void setLaneChoke(int track, int lane, int group);
    // A new kit's groups: its two hats in CHOKE 1 (the closed hat cuts the open one), the rest in none.
    // A drum track added, a new song's first track, a track given the kit, the first song on a card.
    void chokeNewKit(int track);
    // MOD 1 or 2 of a track (mod 0 or 1): the whole of it, held to what it can be. One that is OFF,
    // or aimed at something that does not fit the track (mods.hpp: an empty slot, a choice, a macro
    // on a track with none), is stored OFF, blank. A step of undo, as GLIDE FROM is. True if the
    // track holds what was asked for (false: it was put OFF, or there is no such track or MOD).
    // `record` false: never written into its DEPTH lane, WRITE on or not (a preset putting its MODs in).
    bool setMod(int track, int mod, const ModSettings& settings, bool record = true);
    const ModSettings& modData(int track, int mod) const;
    // A MOD's DEPTH as it is now: while the song plays, where its DEPTH lane's automation has it in
    // the scene playing (batch 7), else its own (exactly).
    float modDepthShown(int track, int mod) const;
    // X HELD on a knob with a MOD while the song is stopped (owner's call E): the engine runs the
    // MODs in `mask` (bit 0 MOD 1, bit 1 MOD 2) of `track`, counted from now as from Play
    // (Command::ModAudition), so the gesture is heard. The screen asks every frame X is held there;
    // a command goes only when what runs changes. Mask 0 lets go. While the song plays nothing is
    // sent (its MODs run), and after it stops the next ask starts it again. `owner` is the screen:
    // App lets go when X is up, another screen is up or another track picked (keepModsHeard), and
    // a new song and panic let go too.
    void hearMods(int track, unsigned mask, const void* owner);
    void keepModsHeard(bool xHeld, const void* screen);
    unsigned modsHeard(int track) const { return track == modsHeard_.track ? modsHeard_.mask : 0u; } // for the tests

    // ---- recordings on drum lanes ----------------------------------------------------
    // A lane of a drum track plays the built-in drum, or a WAV file from the
    // sample folder. A lane can be FITTED: stretched, at the same pitch, to
    // last a number of steps at the song's tempo. That is worked out on
    // another thread and swapped in when ready; when the tempo changes it is
    // worked out again.
    void useSampleFolder(const std::string& folder) { sampleFolder_ = folder; }
    std::vector<std::string> sampleNames() const;
    unsigned sampleFiles() const { return sampleFiles_; } // how many times the card's sample lists changed (WiFi): open pickers read them again
    // Delete a recording's file from the samples folder. Lanes that were playing it go back to
    // their built-in drum (a sampler: to none). False if there is no such recording.
    std::vector<std::string> sampleFolders() const; // the folders in the samples folder, two levels, the empty ones too
    int sampleUses(const std::string& name) const; // how many drum lanes and sampler tracks of the open song have it on
    bool deleteSample(const std::string& name); // for good: the lanes and tracks it was on go back to their drum, no step of undo
    // What the handheld does to the samples folder itself (the list of recordings: carry, + NEW FOLDER,
    // hold B on an empty folder), and how it went.
    enum class FileJob { Done, Same, TooLong, Taken, NotEmpty, Failed, NotSaved, Moving, Copying, Busy }; // NotSaved: done, but a song that names it could not be written (a full card). Moving: done, and the saved songs are following (following()). Copying: a card that will not rename, so it is being written again in its new place, off the screen's thread: nothing has changed yet, and carryDone() then says how it went. Busy: another is being copied.
    // Move a recording to another folder (or out of every folder), under the name `to`: its file is
    // renamed (the long way where the card will not), the open song follows (recordingChanged: the
    // name only, the sound plays on), the open song is written, and every saved song and rev that
    // names it follows, a few a frame (followLater: Moving while they do). Nothing is written over (Taken, case not
    // counted); the folder it goes to must be there already; `to` must fit kSampleNameRoom (TooLong).
    FileJob moveSample(const std::string& from, const std::string& to);
    // A carry being copied (Copying): true until Session::update has taken the copy's end. Then
    // carryResult() is how the whole move went (Done, NotSaved, Moving or Failed), once, counted by carriesDone().
    bool carryCopying() const { return carry_ != nullptr; }
    FileJob carryResult() const { return carryResult_; }
    unsigned carriesDone() const { return carriesDone_; }
    // A new, empty folder: "name", or "folder/name" inside one of the first level.
    FileJob makeSampleFolder(const std::string& name);
    // Only an EMPTY folder is deleted: NotEmpty if anything at all is in it (a file that is no recording too).
    FileJob deleteSampleFolder(const std::string& name);
    // A recording's file was changed from outside (the WiFi page), and the open song follows.
    // Renamed or moved (`from` and `to`): every lane and sampler track that used the old name
    // uses the new one, and goes on playing the sound it has: nothing is read again. Deleted
    // (`to` empty): they keep the name and are marked missing, as they would be the next time
    // the song was opened. Sent (`from` empty): one that was missing by that name is read.
    // A recording of that base name that turns up anywhere (`to` in another folder) is taken by
    // one that was missing, if it is the only one (findMissing). None of it is a step of undo, and
    // every step of undo follows a rename or a move (undoFollows), as the saved songs do.
    void recordingChanged(const std::string& from, const std::string& to);
    // Recordings that were renamed or moved while a song was shut. A lane or sampler track whose
    // recording is not where the song says takes the one of the same base name (the part after
    // the folder) from the root of the samples folder or one of its folders, if there is EXACTLY
    // ONE and it reads. None, or two: it stays missing, never a guess. Run when a song or a rev
    // is opened; the changed name is saved as any change is. `label`: only those of that base
    // name. Returns how many were found. The folder is listed once, and only if something is missing.
    // A recording found is no step of undo, and the steps of undo name it where it was found.
    int findMissing(const std::string& label = "");
    int sampleLookups() const { return lookups_; } // how many times findMissing has listed the folder (tests)
    // The page renamed or moved recordings (`first` to `second`, in order): every saved song and
    // rev in the song folder that names one names the new one (renameRecordings), written beside
    // itself and moved into place, with the date it had. Pairs with a name that is no
    // recording's (one sent or deleted has an empty side) are passed over. The open song's own file is left to
    // recordingChanged and the autosave. Returns how many files were written.
    int savedSongsFollow(const std::vector<std::pair<std::string, std::string>>& renames, int* failed = nullptr); // failed: those that name one and could not be written
    // THE SAME, A FEW FILES A FRAME (owner, 2026-11-15: a carry let go froze the screen while every
    // saved song and rev was read and written; on the handheld, a recording in forty songs with
    // their revs, seconds). `followLater` lists the files now and goes through them in `update`:
    // up to kFollowReadsAFrame read, and no more after one is written (a write waits for the card).
    // BEFORE any file is touched it writes what is still owed (`kOwedFile` in the song folder, with
    // writeTextFile), rewrites that as each batch of renames is done and deletes it at the end; a
    // program stopped part-way finishes it the next time the song folder is opened, before any
    // song is (useSongFolder). A file gone through twice is no harm: it no longer names the old
    // place. Anything that reads or renames a saved song first finishes what is owed (followNow).
    // If the list cannot be written, it is all done at once, as before.
    void followLater(const std::vector<std::pair<std::string, std::string>>& renames);
    void followNow();                                   // finish what is owed, at once
    bool following() const { return !follows_.empty(); } // saved songs are still being gone through
    int followFailures() const { return followFailures_; } // files that named one and could not be written, since the program started
    static constexpr int kFollowReadsAFrame = 8;
    static const char* const kOwedFile;                 // "renames-owed.txt"
    // "" brings the drum back (and takes a synth voice off); START and END go back to the whole. A step of
    // undo with the name (soundMark): undo puts the old sound back with its own cut and voice.
    void setLaneSample(int track, int lane, const std::string& name);
    // DRUM SYNTH (apotheca/drum_synth.hpp): the lane's sound made from a voice, in place of a recording
    // (its name is emptied) or the built-in drum. Made here, or on the helper thread past
    // drumsynth::kWorkInAFrame, and handed to the engine as a recording is. A voice that is not on puts
    // the built-in drum back. Another model (or a voice where there was none) puts START and END back
    // to the whole, as another recording does; a knob turned keeps them. A step of undo (soundMark).
    void setLaneSynth(int track, int lane, const drumsynth::Voice& voice);
    void setLaneFit(int track, int lane, int steps);                  // 0: as recorded
    // A lane's sound as slices, for chopping a break. How many there are: sixteen,
    // or one to a step if the recording is fitted to more than a bar (32 for two
    // bars, 64 for four). And whether a hit plays just its slice and stops, or
    // runs on until the next hit on the lane.
    int laneSlices(int track, int lane) const;
    void setLaneOneSlice(int track, int lane, bool on);
    void setSamplerChop(int track, bool on); // chopped or whole: 16 even slices from C1 up, or the one recording
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
    // EDIT (lane_edit.hpp): what a lane does to its sound, made from the file (or the built-in drum)
    // here and handed to the engine. The file is never written. START and END go back to the whole of
    // it when the lane is given another sound (setLaneSample); the rest stay.
    void setLaneEdit(int track, int lane, const LaneEdit& edit); // held to its limits (laneEditHeld)
    void readyLane(int track, int lane);     // its sound read, if it has not been yet (the edit view draws it)
    bool normalizeLane(int track, int lane); // GAIN so the part played peaks at kNormalDb. False: silence
    bool trimLaneSilence(int track, int lane); // START and END at the first and last sound. False: silence
    struct LaneSound {
        std::shared_ptr<const std::vector<float>> recording; // as it came off the card; null on a built-in lane
        std::shared_ptr<const std::vector<float>> source;    // what the edits start from: the recording, or the built-in drum
        std::shared_ptr<const std::vector<float>> shaped;    // ...cut, turned round and pitched (the source itself if nothing does)
        std::shared_ptr<const std::vector<float>> fitted;    // ...and FIT to its steps, once that is made
        std::vector<float> sourcePeaks; // the source's outline (the edit view draws all of it)
        float sourceSeconds = 0.f;
        std::vector<float> peaks; // the outline of what the lane plays, for drawing (none for a built-in drum left as it is)
        float seconds = 0.f;      // ...and how long it lasts, unfitted
        bool missing = false;     // the song names a file that is not here
        bool waiting = false;     // a fitted (or shaped) version is wanted and not yet in the engine
        bool shaping = false;     // its shape is wanted from the helper thread (too costly for a frame)
        bool making = false;      // ...and before that its synth voice is made there (DRUM SYNTH: shaping is set too)
        float fittedTempo = 0.f;  // the tempo the one in the engine was made for
        int fittedSteps = 0;
        int zones = 0;            // a sampler's multisample: how many recordings play (0: one, as always)
        int layers = 0;           // ...and the most velocity layers on one of its notes (1: none)
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
    bool transferOn() const { return transfer_.running() || !pretendAddress_.empty(); }
    // HOTSPOT on the SET page: our own network, with or without the WIFI PAGE. Turning it on starts
    // the page too when it is off (the network exists for the page); turning it off stops the page
    // again only when HOTSPOT started it, else the radio joins again. True if it is now as asked.
    bool setHotspot(bool on, unsigned seed);
    Transfer& transfer() { return transfer_; } // (tests put a WiFi change in by hand: noteChange)
    // WiFi on our own firmware (wifi.hpp): joining a network. Here only where the system gives the
    // program its `apotheca-wifi` (APOTHECA_WIFI); elsewhere nothing of it shows or runs.
    Wifi& wifi() { return wifi_; }
    const Wifi& wifi() const { return wifi_; }
    bool wifiHere() const { return wifi_.here(); }
    // The NETWORK row is up on the SET page (set by it each frame): `update` then asks for a scan
    // every 10 s as well as the status every second (wifi.hpp), which it asks for on every screen.
    bool wifiLooking = false;
    std::string transferAddress() const { // what to type into the browser
        if (!pretendAddress_.empty()) return pretendAddress_;
        if (!addressAsked_ || now_ - addressAt_ >= 1000 || now_ < addressAt_) addressKept_ = transfer_.address(), addressAt_ = now_, addressAsked_ = true;
        return addressKept_;
    }
    // The page's address by NAME ("apotheca.local/4821": the name our firmware says, with the
    // address's port, unless it is 80, and number), or "" where no name is said (the port; not joined; an older system).
    std::string transferName() const;
    // For a still: the WIFI PAGE as if on at this address, and our firmware's WiFi saying this.
    void pretendWifi(const Wifi::Now& now, const std::string& address) { wifi_.pretend(now), pretendAddress_ = address; }
    std::mutex& songFiles() { return transfer_.songFiles(); } // held while a song file is written (saveNow) or sent

    // ---- updating Apotheca over WiFi ----------------------------------------------------
    // A new program sent to the page is only set aside. It is installed
    // when someone accepts it on the handheld, and used at the next start;
    // the launcher keeps the old one and goes back to it if the new one
    // does not get going (scripts/Apotheca.sh).
    void useProgramFolder(const std::string& folder);
    bool updateWaiting() const { return updateWaiting_; }   // one has arrived and wants an answer
    float updateMegabytes() const { return updateMegabytes_; }
    bool acceptUpdate();   // true if it is now in place for the next start
    bool updateReady() const { return updateReady_; } // accepted: used when Apotheca next starts
    void markUpdateReadyForStill() { updateReady_ = true; } // (--view update-ready: no file is touched)
    void discardUpdate();
    void startedFine();    // call once the program has been running a few seconds: this start worked

    // ---- recording -------------------------------------------------------------------
    // Record from the input onto a drum lane, or onto a sampler track (the
    // lane is then ignored). The first call starts it waiting for sound; the
    // next stops it. It also stops by itself at twenty seconds. What was
    // recorded is saved as "rec 1", "rec 2"... in the sample folder.
    enum class Rec { Idle, NoInput, Waiting, Recording };
    void useAudioInput(AudioInput* input) { input_ = input; }
    Rec toggleRecording(int track, int lane); // what it is doing now
    Rec recording() const { return rec_; }
    void dropRecording(); // a recording under way called off, its take thrown away (the panic: one R1 began on the way)
    float recordSeconds() const { return recorder_.seconds(); }
    float recordLevel() const { return recorder_.level(); }
    const std::string& lastRecorded() const { return lastRecorded_; } // "" if the last one came to nothing
    // A take waits for sound or runs. While it does, R1 stops it from any stop of a screen that records
    // (the kit's lanes and its KIT stop, the sampler, CLOUD's every page: gesture audit G34), before
    // R1's own job there (SAVE, a page turned).
    bool recordingNow() const { return rec_ == Rec::Waiting || rec_ == Rec::Recording; }
    // The line a screen that records says meanwhile, the limit as Recorder::kMaxSeconds has it (it said
    // OF 10 after the limit went to 20). Empty when no take waits or runs.
    std::string recordingLine() const;

    // ---- songs on disk ------------------------------------------------------------
    // Nothing here needs asking for. The song being worked on is written to
    // its file a couple of seconds after any change, and again on the way
    // out; the last one open comes back at the next start.

    // Turn saving on: songs live in this folder. Opens the last song, or
    // starts a new one. `seed` picks the names new songs are given.
    void useSongFolder(const std::string& folder, std::uint32_t seed);
    const std::string& songName() const { return songName_; }
    std::vector<std::string> songNames() const; // most recently saved first
    bool newSong();                             // saves the current one first; false (and said) if it could not be saved: nothing changes
    bool openSong(const std::string& name);     // likewise (false too if the file could not be read)
    bool saveNow();                             // only writes if something changed; false: it had to and could not
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
    // volume.txt and settings.txt are written once what they would say has stood still for
    // kKeepRestMs (Session::update), never when it is what the card has already (G43: a held RIGHT on
    // a SET row wrote a file and two fsyncs nine times a second, at an end too), and on the way out.
    static constexpr Uint32 kKeepRestMs = 1000;
    void keepSettingsNow(); // what is owed, written now (main.cpp on the way out; the destructor too)
    bool settingsOwed() const { return settingsOwed_ || volumeOwed_; }
    // Two more things about the handheld, kept in "settings.txt" beside the
    // program: how fast the stick is (1 is as first built) and how many
    // octaves the piano roll shows (1, 2 or 3).
    static constexpr float kStickSlowest = 0.25f, kStickFastest = 1.5f;
    float stickSpeed() const { return ui_stickSpeed(); }
    void setStickSpeed(float speed);
    int octavesShown() const { return octaves_; }
    void setOctavesShown(int octaves);
    // And how big the lettering is: 0 SMALL, 1 MEDIUM, 2 LARGE (what the screens were laid out
    // at, and what a handheld with nothing kept gets), 3 XL. Only the number is held here: the
    // program loads the fonts again when it sees it change (main.cpp), between two frames.
    static constexpr int kTextSizeCount = 4, kTextSizeAsBuilt = 2;
    int textSize() const { return textSize_; }
    void setTextSize(int size);
    // And the colours: a THEME (ui.hpp's kThemeTable; 0 is the look as it was built, and what a
    // handheld with nothing kept gets). Only the number is held here: App::draw puts it to use,
    // so it takes effect between two frames.
    static constexpr int kThemeCount = 8, kThemeAsBuilt = 0;
    int theme() const { return theme_; }
    void setTheme(int theme);
    // The screen's BRIGHTNESS (a percent, kBrightLeast..100 in fives) and the STICK LIGHT (RingMode),
    // on our own firmware only (lights.hpp: the system says the files are the program's). Kept in
    // "settings.txt" everywhere; used only where `lightsHere`. Put to use by update(), once a frame.
    void useLights(Lights lights) { lights_ = std::move(lights); }
    const Lights& lights() const { return lights_; }
    bool screenHere() const { return lights_.screenHere(); }
    bool ringHere() const { return lights_.ringHere(); }
    int brightness() const { return brightness_; }
    void setBrightness(int percent);
    int stickLight() const { return stickLight_; }
    int lightColour() const { return lightColour_; } // LIGHT COLOUR (lights.hpp: LightColour)
    int ringLevel() const { return ringLevel_; }          // RING LEVEL: the ring's own brightness, percent
    // LIGHT COLOUR is TRACK on a ring that is here and lit: the SONG screen marks each head with its track's colour.
    bool trackColoursShown() const { return ringHere() && stickLight_ != kRingOff && lightColour_ == kLightTrack; }
    void setLightColour(int which);
    void setStickLight(int mode);
    void setRingLevel(int percent);
    // What the ring shows now (ringPicture with the song's step and the theme's colours).
    RingFrame ringNow() const;
    void loadSettings();

    // ---- bouncing (bounce.cpp) ---------------------------------------------------------
    // The song, or its tracks, written out as WAV files (or the song as one MIDI file) in the
    // "bounces" folder. Worked out off to one side, not recorded as it plays, so it is
    // whole whatever the handheld could keep up with.
    enum class Bounce { Song, EachTrack, ThisTrack, MidiFile };
    void useBounceFolder(const std::string& folder); // (and what a killed bounce left there is removed)
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
    // keeps playing. It covers the patterns and each track's effects, sends, sound (a recording on a
    // lane, a sampler or a CLOUD with it) and GLIDE FROM, the master's effects and the buses'
    // (soundMark_, restoreSounds).
    static constexpr int kUndoLevels = 20;
    bool undo();
    bool redo();
    int undoSteps() const;
    int redoSteps() const { return static_cast<int>(redo_.size()); }
    void noteEdit(); // something in a pattern has just changed
    // A list of recordings is open on the screen shown (the App says so each frame, before update). What
    // it puts on as the cursor goes is not watched for undo until it closes, however long it rests on
    // each: a visit is one step of undo (from before it opened to what A kept), or none where the sound
    // is as it was (B: what was there put back). Gesture audit G04, as a pick became a step of undo.
    void browsingRecordings(bool on) { browsing_ = on; }
    // An edit's own step of undo, from outside (SongScreen::pasteCellHere): whatever was being
    // edited just before ends here, and nothing after it joins the step (as pasteBar's inline).
    void beginOwnUndoStep();
    void endOwnUndoStep();
    void readUndoWhole(bool on) { readUndoWhole_ = on; } // the tests: every step of undo read back whole, as it was before (to hold the two the same)

    // ---- next scene ---------------------------------------------------------------------
    // The scene being worked on, copied whole (every track's cell with its length, the
    // scene's own length, how often it plays, its automation) into a new scene put in
    // right after it. The scenes after it move down one; nothing is written over. The
    // player is then on the new one, same track and bar. A song that is playing goes
    // with them and does not miss a step: the two scenes are the same. One step of
    // undo, which also takes the player (and the playing) back.
    // Returns the new scene, or -1 when there is no room (the song's sixteenth scene has
    // something in it, or the scene worked on is the sixteenth), and then nothing is changed.
    int nextScene() { return nextScene(project.scene); }
    // ...of another scene (the SONG screen: the one under its cursor). The player goes to its copy, on a bar
    // that cell has; with no room nothing changes, the player neither.
    int nextScene(int scene);
    // The scene that is playing (-1: none), as it is once the engine has heard every scene
    // move sent to it. For the block or so until it has, the engine still says the old number;
    // this is what to ask, not engine.playScene(). (project.playScene is this, once a frame.)
    int playScene() const;
    unsigned imagesSent() const { return imagesSent_; } // how many times a track's every pattern has gone to the engine in one piece (for tests: a scene move sends none)
    // For a screen that holds a scene's number: how many times scenes have moved, and the last time
    // (the scenes from `from` on went `by` places: 1 or -1). One call here moves them once at most.
    unsigned sceneMoves() const { return sceneMoves_; }
    int sceneMoveFrom() const { return lastMove_.from; }
    int sceneMoveBy() const { return lastMove_.by; }
    int sceneMovesUnheard() const { return static_cast<int>(moves_.size()); } // as of the last frame (for tests)

    // ---- mute, solo, names -----------------------------------------------------------
    // A muted track is silent. While any track is soloed, only soloed ones
    // are heard, muted or not. Both act at once; the song keeps mutes only.
    void setMuted(int track, bool muted);
    void setSolo(int track, bool solo);
    bool heard(int track) const; // as things stand, does this track sound?
    // QUEUED MUTES (easywins batch 8): SET's MUTES at NEXT BAR, a mute or a solo pressed while the song
    // plays lands on the next bar line. Until the engine says it has, the track's mark waits ('M' or
    // 'S': drawn hollow) and heardNow is what still sounds.
    enum { kMutesAtOnce, kMutesNextBar, kMutesModes };
    int mutes() const { return mutes_; }
    void setMutes(int mode);
    char muteWaits(int track) const { return track >= 0 && track < kNumTracks && mutesWaiting_ ? muteMark_[track] : 0; }
    bool mutesWaiting() const { return mutesWaiting_; }
    // For a still: this track's mark as if it waited for a bar line that never comes.
    void pretendMuteWaits(int track, char mark) {
        for (int t = 0; t < kNumTracks; ++t) heardBefore_[t] = t == track ? true : heard(t);
        mutesWaiting_ = true, mutesAsked_ = -1, muteMark_[track] = mark;
    }
    bool heardNow(int track) const { return mutesWaiting_ && track >= 0 && track < kNumTracks ? heardBefore_[track] : heard(track); }
    // What the player calls a track: capitals, digits, spaces, - and _, ten
    // at most. Empty puts back its instrument's name.
    void setTrackName(int track, const std::string& name);

    // ---- the mixer (the SONG screen's heads) -----------------------------------------
    // A track's level, in whole dB from its usual (kMixLeastDb..kMixMostDb), and its pan
    // (-kPanMost..kPanMost); `track` may be kMasterTrack. Held to their limits; a step of undo.
    // Nothing else below zero is a track: a bus or a number past the tracks does nothing.
    void setMix(int track, int db, int pan);
    // AT END (kSongLoop, kSongStop): what the song does at its end, played as a song. A step of
    // undo; the engine is told by syncSong. The engine says when a song stopped by itself, and
    // update() then takes project.playing down (songEnded, playsSent_).
    void setSongEnd(int end);
    // SCALE LOCK (scales.hpp): the song's KEY (0..11, round) and SCALE (a choice with ends). A step of undo.
    void setScale(int key, int kind);
    int mixDb(int track) const { return track == kMasterTrack ? project.masterDb : (track >= 0 && track < kNumTracks ? project.tracks[track].mixDb : 0); }
    int mixPan(int track) const { return track == kMasterTrack ? project.masterPan : (track >= 0 && track < kNumTracks ? project.tracks[track].mixPan : 0); }
    void sendMix(int track); // what the engine is told for it: its level and its pan
    void sendEffects(int track); // every slot of a track, the master or a bus, to the engine
    void sendMutes(bool wait = false); // tell the engine how things stand (after a song is opened); wait: at the next bar line

    // ---- a synth's sound ---------------------------------------------------------------
    // One place for changing a synth track's patch, used by the Device screen
    // and by the sound-designer page alike.
    void setSynthSetting(int track, int id, int value);
    void setSynthMacro(int track, int macro, float value);
    // A built-in preset or one of the player's own. The macros go back to the middle (or to the
    // preset's own, where the app holds them). A preset that holds the track's whole sound puts its
    // effects, sends, GLIDE FROM and MODs in too (putPresetSound); a plain one leaves them. One step
    // of undo brings back what was there (soundMark sees it all).
    void chooseSynthPreset(int track, int index);
    // A player's own CLOUD preset names the recording it was saved with: put in if the card has it, by
    // its name, else by its base name where exactly one has it (as findMissing); else the track keeps
    // its own and the App says RECORDING NOT FOUND. True if it went in (or was in already).
    bool putPresetRecording(int track, const std::string& name);
    // A track sound put in as a preset puts it: each slot, send, GLIDE FROM and MOD that differs (as
    // the song file writes it) and nothing else, through the engine's usual commands (another kind of
    // effect: Session::setEffect, its 10 ms fade, its slot's automation cleared). Nothing is written
    // into automation, WRITE on or not.
    void putTrackSound(int track, const TrackSound& sound);
    // B with A held puts the sound back (gesture audit G12). Where hold A steps through sounds (the KIT
    // stop, a lane's SOUND, the sampler's SOUND and PRESET, a synth's FOLDER and PRESET, CLOUD's
    // RECORDING) the screen keeps the track's whole sound as A goes down (keepSound), and B with A still
    // held puts it back: the preset, its settings and macros, the recording, the sampler's settings and
    // CHOP, every lane's kit fields, the effects, sends, GLIDE FROM and MODs, and what a whole preset
    // found (PresetSoundMemory), each only where it differs, as a change like any other (a step of undo;
    // a recording, as ever, is not). One kept at a time, in memory only, for its track by number:
    // forgotten when the tracks move or another song comes, never put on another instrument.
    // putSoundBack is false where nothing is kept for this track.
    void keepSound(int track);
    bool putSoundBack(int track);
    // Y on MAIN: a new sound from the dice, on a synth that has such a thing (PHYSIC: SynthSpec::roll).
    // The engine is told the whole patch. False on the others, and nothing changes.
    bool rollSynth(int track, unsigned seed);
    void sendSynthPatch(int track);
    // The player's own presets: files in this folder, "NAME.phazed", "NAME.jaxep" (JAX-EP's), "NAME.fm6", "NAME.physic" and "NAME.acid".
    // (An old "NAME.twin" or "NAME.qmod", of the synths that went, is left on the card and not listed.)
    void usePresetFolder(const std::string& folder);
    // Keep the track's sound, as it is now with its macros, under a name.
    // The track is then on that preset. False if the name is no good or it could not be written.
    bool savePreset(int track, const std::string& name);
    // The player's own preset the track is on: its file deleted, for good (hold B on PRESET). The track keeps
    // its sound, and every track of that synth stays on its own preset (the numbers past it move up one).
    // False if the track is not on one of the player's own, or the file could not be deleted.
    bool deleteSynthPreset(int track);
    // KIT PRESETS (easywins batch 7; kits.cpp). The list: the built-in kits (recipes of DRUM SYNTH's named
    // voices), then the player's own ("NAME.kit" in the preset folder, written by SAVE as KIT 1, KIT 2...).
    // A kit is a drum track's every lane: its recording or synth voice, EDIT, FIT, one-slice, level,
    // FEEL, SWING and CHOKE; never its patterns, effects or mixer.
    static constexpr std::size_t kKitNameMost = 12;
    struct Kit {
        std::string name, text; // its text is a kit file's (kitText)
        bool own = false;
        std::string file;       // an own kit's file in the preset folder
    };
    const std::vector<Kit>& kits();
    int kitOf(int track);                          // which kit of the list the track's kit is now, or -1 (its own)
    bool loadKit(int track, int index);            // put on the track: every lane told to the engine. One step of undo, the whole kit back (recordings with it)
    bool putKit(int track, const TrackData& kit);  // ...the kit fields of `kit` (copyKit's)
    std::string saveKit(int track);                // as the next free "KIT n": its name, or "" (no folder, or the card refused)
    bool deleteKit(int index);                     // an own kit's file, for good
    // SAMPLER PRESETS (sampler_presets.cpp): a sampler track's sound kept as a file of its own, "NAME.sampler"
    // in the preset folder. A preset is its recording, its settings, chopped or whole; and the track's whole
    // sound (effects, sends, MODs), as a new synth preset's. There are no built-in ones: the list is the
    // player's own, or empty.
    static constexpr std::size_t kSamplerPresetNameMost = 24;
    struct SamplerPreset {
        std::string name, file, sample;
        int settings[sampler::kSettings];
        bool chop = false;
        TrackSound sound; // the whole sound, when the file holds it
    };
    const std::vector<SamplerPreset>& samplerPresets();
    int samplerPresetOf(int track);                             // which one the track's sound is now, or -1
    bool loadSamplerPreset(int track, int index);               // put on the track (its settings take a step back; as a kit's, the recording stays)
    bool saveSamplerPreset(int track, const std::string& name); // under this name (made a file's); false if no good or not written
    bool deleteSamplerPreset(int index);                        // its file, for good
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
    // The METRONOME and the COUNT IN (easywins batch 2), the handheld's own (settings.txt). CLICK: OFF,
    // ON (a click on every beat while the song plays), or RECORDING (only while it plays with USB
    // KEYBOARD at PLAY + RECORD). COUNT IN: OFF, or ONE BAR of clicks before START plays the song with
    // PLAY + RECORD on. Never in a bounce (its engine is never told).
    enum { kClickOff, kClickOn, kClickRecording, kClickModes };
    int click() const { return click_; }
    void setClick(int mode);
    bool countIn() const { return countIn_; }
    void setCountIn(bool on);
    bool clickWanted() const { return click_ == kClickOn || (click_ == kClickRecording && midiRecord_ && project.playing); }
    bool countsIn() const { return countIn_ && midiRecord_; } // START counts in a bar first
    void syncClick(); // tell the engine what clickWanted() says now, if it has changed (update() does, every frame)
    // The last key played and not yet asked about, or -1. The piano roll's cursor goes to it.
    int takeMidiKey() { const int key = midiKey_; midiKey_ = -1; return key; }
    // The tempo, from anywhere. Held to what the engine can play.
    void setTempo(float bpm);
    // One scene's own tempo (0: the song's). Like the song's tempo, no step of undo.
    void setSceneTempo(int scene, float bpm);
    // Steps in one scene's bars, 1..16. No step of undo either.
    void setSceneMeter(int scene, int steps);
    // TAP TEMPO (tap_tempo.hpp): A tapped on a TEMPO row, `ms` the frame's clock. The second tap of a
    // run on sets the tempo; each from the second says what it did in the App's box (TAPPED: 120 BPM).
    void tapTempo(Uint32 ms);

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

    // Tell the engine everything in the project, from nothing. Another song: undo starts again.
    void applyProject();
    // The engine half of it: the song is the same song (a resync, a track deleted or given
    // another instrument), so undo and redo are kept.
    void sendSong();
    // How many times the engine fell behind (a full command queue) and was sent the whole song again.
    int resyncs() const { return resyncs_; }
    // What the Session does by itself that can change the screen (the Stir bits of redraw.hpp):
    // fitting a recording to the tempo (or the tempo settling first), recording, a bounce, WiFi.
    unsigned stirring() const;
    unsigned midiHeard() const { return midiHeard_; } // MIDI events taken so far (redraw.hpp's readouts)

    // Write a whole cell to the engine (every bar of it, and its length),
    // replacing what it had.
    void sendDrumPattern(int track, int scene);
    void sendDrumHit(int track, int scene, int bar, int lane, int step); // one hit of it, all of what it is: what a keyboard records
    void sendNotePattern(int track, int scene);
    void sendTrackImage(int track); // every pattern of a track to the engine, in one piece (patterns and automation)
    // TRANSPOSE (easywins batch 3): every note and chord of a synth or sampler track's cell in a scene
    // (every bar of it, hidden ones too) `by` semitones, a chord's name with it (transposeChordName).
    // All or nothing: kTransposed, kNothingToMove (a drum track, an empty cell, no track), or
    // kOutOfRange (a key would pass C0 or C8: nothing moves). A step of undo, as any edit.
    enum Transposed { kTransposed, kNothingToMove, kOutOfRange };
    Transposed transposeCell(int track, int scene, int by);
    void sendNoteBar(int track, int scene, int bar); // one bar of it: what the piano roll changes
    // How long a track's cell is in a scene: 1, 2, 4 or 8 bars.
    // How many steps a bar of a scene has, 1 to 16, for every track. Thirteen makes a thirteen-step loop.
    void setSceneSteps(int scene, int steps);
    void setCellBars(int track, int scene, int bars);

    // ---- copy a bar ----------------------------------------------------------------------
    // One bar of a cell (16 steps) held to be put on another bar: of the same cell, another cell,
    // scene or track (on the grids: hold L1, R1 copies the bar shown, A pastes over it). In memory
    // only, never in the song file; a new song keeps it. A drum bar holds every hit with all it is
    // (level, nudge, chance, start, step effects, THROW); a note bar its notes and chords (a note
    // that runs over the bar line keeps its length, held to the end of the cell it lands in). Its
    // 16 steps of every automation lane go too, but only onto the SAME track: another track's lanes
    // are other knobs. A paste REPLACES the bar and is one step of undo; onto a bar that is already
    // the same, nothing is done and it is no step.
    enum class BarCopy { Done, Same, NothingCopied, DrumsOnDrums, NotesOnNotes, NoTrack };
    struct BarClip {
        bool has = false;
        bool drums = false;              // a drum bar (else notes)
        int track = -1, scene = 0, bar = 0; // where it came from
        DrumPattern drumBar;
        NotePattern noteBar;
        float automation[kAutoLanes][kSteps] = {};
    };
    BarCopy copyBar(int track, int scene, int bar);
    BarCopy pasteBar(int track, int scene, int bar);
    // ...on the bar the grids show (project.track, scene, bar), and what was done said (say()):
    // BAR 1 COPIED, PASTED ON BAR 2, and why not.
    BarCopy copyBarHere();
    BarCopy pasteBarHere();
    static std::string barCopyWords(BarCopy answer, bool paste, int bar); // (bar from 0)
    enum class SceneCopy { Done, Same, NothingCopied, NoScene };
    struct SceneClip {
        bool has = false;
        int scene = 0; // where it came from
        struct Track {
            TrackKind kind = TrackKind::None;
            int bars = 1;
            DrumPattern drums[kMaxBars];
            NotePattern notes[kMaxBars];
            float automation[kAutoLanes][kAutoSteps];
        };
        Track tracks[kNumTracks];
        int repeats = 1, steps = 0;
        float tempo = 0.f; // the scene's own (0: the song's)
        int meter = kSteps;
    };
    SceneCopy copyScene(int scene);
    SceneCopy pasteScene(int scene);
    // ...on a scene of the SONG screen, and what was done said (say()):
    // SCENE 1 COPIED, PASTED ON SCENE 2, and why not.
    SceneCopy copySceneHere(int scene);
    SceneCopy pasteSceneHere(int scene);
    static std::string sceneCopyWords(SceneCopy answer, bool paste, int scene); // (scene from 0)
    enum class SceneClear { Done, AlreadyEmpty, NoScene };
    SceneClear clearScene(int scene);
    SceneClear clearSceneHere(int scene);
    static std::string sceneClearWords(SceneClear answer, int scene); // (scene from 0)
    int copiedScene() const { return sceneClip_->has ? sceneClip_->scene : -1; }
    const SceneClip& sceneClip() const { return *sceneClip_; }
    void setSceneClip(const SceneClip& clip) { *sceneClip_ = clip; } // (the App, putting it back after a panic begun with R1)
    unsigned sceneCopies() const { return sceneCopies_; }           // how many copies have been made (the App: did one come in a panic?)
    const BarClip& barClip() const { return clip_; }
    void setBarClip(const BarClip& clip) { clip_ = clip; } // (the App, putting it back after a panic began with L1 + R1)
    unsigned barCopies() const { return barCopies_; }      // how many copies have been made (the App: did one come in a panic?)
    // A line for the App to say for a moment (its box above the hints bar): what a copy or a paste did.
    void say(const char* text);
    unsigned saidCount() const { return saidCount_; }
    const char* saidText() const { return saidText_; }

private:
    // What rememberType keeps: for each slot (every track's, the master's, the buses'), each kind's settings.
    struct TypeMemory {
        bool has[kEffectKinds] = {};
        float param[kEffectKinds][kEffectParams] = {};
    };
    static constexpr int kMemorySlots = kNumTracks * kTrackSlots + kMasterSlots + kBuses;
    static int memorySlot(int track, int slot); // -1: no such slot
    TypeMemory typeMemory_[kMemorySlots];
    void forgetTrackTypes(int track) { for (int s = 0; s < kTrackSlots; ++s) forgetTypes(track, s); }
    // What a whole preset found on the track when it came (`before`), and what it left (`put`, as the
    // song file writes it): a plain preset picked while the track still holds `put` puts `before`
    // back (putPresetSound). Forgotten when anything else changed them, as the slots' types are.
    struct PresetSoundMemory {
        bool held = false;
        TrackSound before;
        std::string put;
    };
    PresetSoundMemory presetSounds_[kNumTracks];
    void putPresetSound(int track, const TrackSound* sound); // null: a plain preset
    // What keepSound kept: the track's fields as A went down (no patterns: none are copied), and what a
    // whole preset had found there then. track -1: nothing.
    struct KeptSound {
        int track = -1;
        std::unique_ptr<TrackData> was;
        PresetSoundMemory memory;
    };
    KeptSound kept_;
    struct Sounding { int track = -1, pitch = 0; Uint32 until = 0; };
    Sounding sounding_[8];
    static constexpr Uint32 kHeldOn = 0xffffffffu; // a Sounding's `until` while X holds it
    // What X holds (or last held). lane -1: a synth key (`pitch`; a chord's first), its Sounding (a chord's: each
    // key's) held at kHeldOn. Only what X holds now is ever at kHeldOn: letGoHeard lets every one go.
    struct Heard { int track = -1, lane = -1, pitch = 0; Uint32 since = 0, ends = 0; const void* owner = nullptr; bool held = false; };
    Heard heard_;
    struct LaneStop { int track = -1, lane = 0; Uint32 at = 0; }; // a drum lane's audition, let go, to be stopped at `at`
    LaneStop laneStops_[4]; // taps on four lanes in a moment; a fifth stops the soonest now
    void stopLane(int track, int lane);
    void endHearing();  // what is due to stop now, stopped
    void dropHearing(); // panic, a new song: nothing held, nothing waiting to stop
    struct ModsHeard { int track = -1; unsigned mask = 0; const void* owner = nullptr; };
    ModsHeard modsHeard_; // what the engine was told to run while stopped (hearMods); track -1: nothing
    void letGoOfMods();
    Uint32 now_ = 0;
    unsigned droppedSeen_ = 0; // engine.dropped() when the song was last sent whole
    int resyncs_ = 0;
    BarClip clip_;           // what copyBar holds (memory only)
    unsigned barCopies_ = 0;
    std::unique_ptr<SceneClip> sceneClip_; // what copyScene holds (memory only; itself on the heap: a whole scene)
    unsigned sceneCopies_ = 0;
    char saidText_[64] = "";
    unsigned saidCount_ = 0;
    TapTempo taps_;
    int sentSceneCount_ = 1;
    int sentSongEnd_ = -1;    // what the engine was last told of AT END (-1: nothing yet)
    unsigned playsSent_ = 0;  // Command::Play sent and taken into the queue (Engine::songEnded)

    std::string pathOf(const std::string& name) const;
    std::string revPath(int rev) const;
    std::string freshName();
    std::string folder_, songName_ = "untitled", savedText_;
    std::string lastSaid_;  // what last.txt says (rememberLast writes it only when the song open is another)
    void rememberLast();
    // The address the WiFi page is at, asked of the system at most once a second (it was asked two or
    // three times a frame where it is shown: review 2026-11-23).
    mutable std::string addressKept_;
    mutable Uint32 addressAt_ = 0;
    mutable bool addressAsked_ = false;
    // The song's text as it is now (songText(project), to the byte), made again a track at a time
    // where a track has changed since it was last asked. Valid until it is asked again.
    const std::string& textNow() const { return textNow_.text(project); }
    mutable SongTextCache textNow_;
    std::uint32_t nameSeed_ = 1;
    Uint32 nextSaveAt_ = 0;

    void loadLane(int track, int lane);
    void sendLanePart(int track, int lane); // tell the engine how much of its sound a hit on the lane plays
    void finishRecording();
    float volume_ = 1.f;
    int octaves_ = 2;
    int textSize_ = kTextSizeAsBuilt;
    int theme_ = kThemeAsBuilt;
    Lights lights_;
    int brightness_ = kBrightUsual;
    int stickLight_ = kRingOn;
    int lightColour_ = kLightTheme;
    int ringLevel_ = kBrightUsual; // the ring's own brightness (70: as the screen until it is turned)
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
    // (moved: a "next scene" lies between; see moveScenesHere). Only undo and redo call it, each just
    // after patternsNow(): readStep takes the text of each track now from what that made.
    void restorePatterns(const std::string& text, int movedFrom = 0, int movedBy = 0);
    std::unique_ptr<TrackImage> trackImage(int track) const; // ...made, not yet sent
    std::unique_ptr<SceneImage> sceneImage(int track, int scene) const; // one scene of a track
    unsigned imagesSent_ = 0;
    void standOn(int scene);                         // the player, put on a scene by undo
    // A scene put into the song or taken out. Three things, in this order: the song here
    // (moveScenesHere), the engine (tellScenesMoved: one command, which holds everything), and
    // whatever here holds the number of a scene that is playing (scenesMoved).
    struct StepRead;
    void moveScenesHere(int from, int by, const StepRead* last);
    bool patternsAre(const StepRead& was) const;
    void tellScenesMoved(int from, int by);
    void scenesMoved(int from, int by);
    // Scene moves told to the engine and not yet known to be heard, oldest first, and how many were
    // sent in all: the engine says how many it has taken with the scene it is on (Engine::playScene).
    struct SceneMove { int from = 0, by = 0; };
    std::vector<SceneMove> moves_;
    SceneMove lastMove_;
    unsigned sceneMoves_ = 0;
    unsigned movesSent_ = 0;
    std::size_t movesUnheard(unsigned heard) const;
    std::uint64_t cellMark() const;                  // changes when the cell being worked on does
    // A step of undo: the patterns as text. `nextScene` is -1, or the scene a "next scene" was
    // made from, when that is what lies between this text and the one it is swapped for: taking
    // it back or putting it forward moves the player and the playing as well as the patterns.
    static bool patternsSameAsText(const TrackData& now, const TrackData& then);
    // A step of undo as read back (readStep): the song's own lines, and the tracks whose text is not
    // the track's text now (`read`). Never a Project: its tracks are made once, the first time a step
    // needs each, and kept (a megabyte a track; most undos read one). Speed plan batch 4: building
    // and copying whole 12 MB Projects was most of an undo's 200 ms on the handheld.
    struct StepRead {
        ProjectHead head;
        int count = 0;                       // how many tracks the step has
        TrackKind kind[kNumTracks] = {};     // each read track's kind (None past `count`, and for one not read)
        bool read[kNumTracks] = {};          // read into tracks[t]; false: its text is the track's now, and it was not read
        std::unique_ptr<TrackData> tracks[kNumTracks];
        const TrackData& track(int t) const { return *tracks[t]; }
    };
    StepRead step_;
    std::vector<std::size_t> stepStarts_;
    // `textNow_` must be as the project is (undo and redo have just made it: patternsNow).
    bool readStep(const std::string& text, StepRead& was, bool same[kNumTracks], bool whole);
    bool readUndoWhole_ = false;
    // Undo covers more than the patterns: each track's effects, sends, sound (preset, settings,
    // macros, a sampler's settings, a kit's lanes, and a recording's NAME: a lane's, a sampler's,
    // a CLOUD's) and GLIDE FROM, the master's effects and the buses' (their LEVEL with them).
    // Whatever changes them is seen here, as the cell is (cellMark): no screen has to say so.
    // restoreSounds puts them back from a step, through the engine's usual commands.
    std::uint64_t soundMark() const;
    std::uint64_t soundMark_ = 0;
    void restoreSounds(const StepRead& was, const bool same[kNumTracks]);
    void restoreEffect(int track, int slot, const EffectData& then);
    void sendMod(int track, int mod); // the project's MOD told to the engine (OFF: one command)
    struct UndoStep { std::string text; int nextScene = -1; };
    std::vector<UndoStep> undo_, redo_;
    // A recording renamed, moved or found elsewhere (recordingChanged, findMissing): every step of
    // undo and redo, and settled_, name it where it is now (renameRecordings).
    void undoFollows(const std::vector<std::pair<std::string, std::string>>& renames);
    std::string settled_;                            // the patterns when things were last still
    bool editing_ = false, restoring_ = false, undoReady_ = false;
    bool browsing_ = false; // a list of recordings is open on the screen shown
    Uint32 editedAt_ = 0;
    std::uint64_t cellMark_ = 0;
    void serveDesigner();
    void serveKit(const DesignRequest& r); // a KIT's requests (serveDesigner)
    void serveFx(const DesignRequest& r);  // an EFFECTS request (any instrument: serveDesigner)
    void serveMod(const DesignRequest& r); // a MOD's request (any instrument: serveDesigner)
    std::string designSpec(int track) const;  // what the page builds itself from (a synth's, a sampler's or a kit's)
    std::string kitSpec(int track) const;
    std::string samplerSpec(int track) const;
    std::string designFxSpec(int track) const;   // ...and its EFFECTS (every instrument's page carries it)
    std::string designModsSpec(int track) const; // ...and its MODs (the same)
    std::string designFxState(int track) const;  // their values, for the state
    std::string designModsState(int track) const;
    std::string designState() const;
    Designer designer_;
    std::string presetFolder_;
    std::vector<Kit> kits_;
    void readKits();
    std::vector<SamplerPreset> samplerPresets_;
    bool samplerPresetsRead_ = false; // (empty is usual: no built-ins)
    void readSamplerPresets();
    bool designHeld_[128] = {}; // keys the page is holding down
    int designHeldTrack_ = 0;
    unsigned designLooks_ = 0;
    Uint32 designSeenAt_ = 0;
    TrackKind designSpecKind_ = TrackKind::None;
    int designSpecPresets_ = -1, designSpecTrack_ = -1;
    unsigned designSpecSound_ = 0;  // the sampler's recording's version (LaneSound::version) it was made from
    unsigned designSpecFx_ = 0;     // the slots', buses' and master's effects it was made from (their kinds, hashed)
    std::string designSpecText_ = "{}";
    unsigned designSpecSerial_ = 0; // goes up whenever designSpecText_ changes: the page asks for it again
    // The recordings a sampler's page is offered: read from the card on this thread, when the page
    // first looks, when the WiFi page has taken something, and every kDesignListEveryMs while it looks.
    std::vector<std::string> designRecordings_;
    Uint32 designListAt_ = 0;
    unsigned designReceived_ = 0;
    bool designListed_ = false;

    void recordAutomation(int track, int lane, float value);
    void hearMidi();
    MidiInput midi_;
    bool midiRecord_ = false;
    int mutes_ = kMutesAtOnce;
    bool mutesWaiting_ = false;       // an asking the engine has not said is done
    int mutesAsked_ = 0;              // ...its number (Command::SetTrackHeard's c)
    char muteMark_[kNumTracks] = {};  // ...the mark that waits on each track
    bool heardBefore_[kNumTracks] = {}; // ...and what each sounded as before it
    void pressedMute(int track, bool solo, bool on);
    Wifi wifi_;
    std::string pretendAddress_;
    int click_ = kClickOff;
    bool countIn_ = false;
    int sentClick_ = -1; // what the engine was last told (SetClick); -1: nothing yet
    int midiKey_ = -1;
    unsigned midiHeard_ = 0;
    // Notes being recorded: where each was written, until its key is let go.
    struct Recording { bool on = false; int track = 0, scene = 0, bar = 0, slot = 0, startedAt = 0; };
    Recording recordingNote_[128];
    static float ui_stickSpeed();
    void keepSettings(); // settings.txt is owed, if what it would say is not what the card has
    std::string settingsText() const;
    std::string volumeText() const;
    std::string settingsKept_, volumeKept_; // what the card has (written or read; "" not known)
    bool settingsOwed_ = false, volumeOwed_ = false;
    Uint32 keepAfter_ = 0; // the last change owed: written kKeepRestMs after it
    Transfer transfer_;
    bool hotspotOwnsTransfer_ = false; // HOTSPOT turned the page on: turning it off turns it off again
    std::string programFolder_;
    unsigned seenUpdates_ = 0;
    bool updateWaiting_ = false;
    bool updateReady_ = false; // accepted this run; the bar says so until the next start
    float updateMegabytes_ = 0.f;
    unsigned seenChords_ = 0; // Transfer::chordsReceived() when the packs were last read
    unsigned sampleFiles_ = 0;
    // A carry the long way (a card that will not rename into another folder), on its own thread: the
    // file written again in its new place and the old one taken away; Session::update does the rest
    // (review 2026-11-23: a large recording held the screen for the whole copy, seconds on the card).
    struct CarryJob {
        std::string from, to, file, target;
        std::atomic<bool> done{false};
        bool ok = false;
        std::thread thread;
    };
    std::unique_ptr<CarryJob> carry_;
    FileJob carryResult_ = FileJob::Done;
    unsigned carriesDone_ = 0;
    void finishCarry(); // the copy has ended: the open song follows, as a rename does
    // Saved songs still to follow a rename (followLater): each batch of renames with the files it
    // goes through, in order.
    struct Follow {
        std::vector<std::pair<std::string, std::string>> renames;
        std::vector<std::string> files;
        std::size_t next = 0;
    };
    std::vector<Follow> follows_;
    int followFailures_ = 0;
    std::vector<std::string> songFilesToFollow() const;  // every saved song and rev but the open song
    // One file through the renames: 1 written, 0 nothing to do (or gone), -1 it names one and could not be written.
    int followFile(const std::string& file, const std::vector<std::pair<std::string, std::string>>& renames);
    void followSome();                                  // a frame's share
    bool writeOwed() const;                             // what is owed, or the file deleted when nothing is
    void finishOwed();                                  // a program stopped part-way: done now
    std::string toldOpenSong_; // the song name the WiFi page was last told (it refuses one sent under it)
    std::string chordFolder_;
    AudioInput* input_ = nullptr;
    Recorder recorder_;
    Rec rec_ = Rec::Idle;
    int recTrack_ = 0, recLane_ = 0;
    std::string lastRecorded_;
    void loadTrackSample(int track);
    std::unique_ptr<sampler::SampleZoneSet> multisampleOf(const std::string& name, std::vector<float>& main);
    void install(int track, int lane, const std::vector<float>& samples);
    void madeLane(int track, int lane, std::vector<float> made); // a synth voice made: the lane's recording, then shapeLane
    void shapeLane(int track, int lane);   // the lane's sound made again from its source, from the cut on (then FIT, then installLane)
    void installLane(int track, int lane); // the fades, GAIN and the guard on what is shaped (or fitted), into the engine
    bool wantsFitting(int track, int lane) const;
    void startJob(int track, int lane);
    void collectJob(bool wait);
    struct Job {
        int track = 0, lane = 0, frames = 0; // frames: what to FIT it to (0: no fit)
        unsigned version = 0;
        float tempo = 0.f;
        bool shape = false; // made from the source first (a costly shape: shapeLane)
        bool make = false;  // the lane's synth voice made, and nothing more (collectJob then shapes it)
        drumsynth::Voice voice;
        LaneEdit edit;
        std::shared_ptr<const std::vector<float>> in;
        std::vector<float> shaped, out;
        std::atomic<bool> done{false};
        std::thread thread;
    };
    std::string sampleFolder_;
    int lookups_ = 0;
    LaneSound sounds_[kNumTracks][kDrumLanes];
    std::unique_ptr<Job> job_;
    // Jobs handed off by a song sent whole (sendSong): their sound is for lanes made again since, so it
    // is never taken; each is joined once it is done (reapJobs, every frame) or as the Session ends.
    // (sendSong waited for the job, seconds of a long FIT or PITCH on the handheld, only to throw it
    // away: review 2026-11-23.)
    std::vector<std::unique_ptr<Job>> leftJobs_;
    void reapJobs(bool wait);
public:
    std::size_t jobsLeftRunning() const { return leftJobs_.size(); } // (for tests)
    bool jobUnderWay() const { return job_ != nullptr; }              // (for tests)
private:
    float steadyTempo_ = 0.f;  // the tempo, once it has stopped moving
    float seenTempo_ = 0.f;
    Uint32 tempoMovedAt_ = 0;
};

// A built-in kit's recipe naming a voice the data does not have ("TIGHT: no voice kick TIGHT"): the tests fail on one.
std::vector<std::string> builtInKitProblems();

} // namespace apotheca
