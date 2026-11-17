#pragma once
// The sound designer: a web page (design_page.cpp) for building sounds on a
// computer's big screen, with the HANDHELD making the sound. The page shows
// every setting of the synth on the track being worked on, plays it from the
// computer's keyboard, and saves the result as a preset.
//
// The page talks to the WiFi server's thread; the sound belongs to the
// program's own thread. This sits between them:
//   - what the page asks for waits in a list, taken up once a frame
//   - what the page is shown is text made once a frame and handed over whole
// so neither thread ever touches the other's things.
//
// What the page may ask (one line of text each):
//   set ID VALUE      a setting of the patch
//   macro N VALUE     a macro, 0..1
//   preset N          choose a preset (built-in or the player's own)
//   note KEY 1 | 0    a key down or up
//   save NAME         keep the sound as a preset called NAME
//   roll N            a new sound from the dice (PHYSIC's: SynthSpec::roll); N is any number, to vary them
//   name NAME         what the track is called ("name" alone: its instrument's name again)
//   sample NAME       a SAMPLER track's recording: one of the names the page was offered ("sample"
//                     alone: none). NAME is the whole name, folders and all ("drums/kicks/808").
//   lanesample L NAME a drum KIT's lane L (0..7): a recording the page was offered, or (no NAME) its
//                     built-in drum
//   hear L 1 | 0      a KIT's lane heard while held, as X held on the handheld; 0 lets go
//   normalize L       a KIT's lane: NORMALIZE (GAIN so what is played peaks at -1 dB)
//   trim L            a KIT's lane: TRIM SILENCE (START and END at the first and last sound)
//   lanesynth L WORD  a KIT's lane L: a DRUM SYNTH voice of the model whose song-file word is WORD
//                     ("kick"), at its usual knobs (drum_synth.hpp)
//   lanevoice L N     a synth lane's named voice N of its model (drum_voices.hpp: 0 is CLASSIC)
//
// What the page is shown says what KIND of page it is ("page": "synth", "sampler" or "kit"), and the
// page builds itself from that: a synth's pages and presets, a sampler's settings round the picture of
// its recording, or a kit's eight lanes, each with its picture and its settings (Session::designSpec).
//
// On a KIT, `set ID VALUE` names a lane's setting: ID is lane * kKitStride + a KitField. Fields 0 to 7
// are the lane's EDIT rows in LaneEditRow's order (lane_edit.hpp); then FIT (steps), CHOKE (group),
// VOLUME (percent), FEEL's HUMAN TIME and HUMAN LEVEL (percent) and SWING (percent). A synth lane's knobs: kKitKnobs below.

#include <mutex>
#include <string>
#include <vector>

#include "transfer.hpp"

namespace apotheca {

// A KIT's settings: lane * kKitStride + field. The EDIT rows keep LaneEditRow's numbers (0 to 7).
constexpr int kKitStride = 16;
enum KitField { kKitFit = 8, kKitChoke, kKitVolume, kKitHumanTime, kKitHumanLevel, kKitSwing, kKitFields };
// DRUM SYNTH: a synth lane's knob P is the id kKitKnobs + lane * 8 + P, after every lane's fields (so the
// ids above, in pages and tests, stay where they were). Its name and readings hang on the lane's model:
// the spec's wave gives the names, the state the readings.
constexpr int kKitKnobs = 8 * kKitStride;

struct DesignRequest {
    enum Kind { Set, Macro, Preset, Note, Save, Name, Roll, Sample, LaneSample, Hear, Normalize, Trim, LaneSynth, LaneVoice } kind = Set;
    int a = 0, b = 0;
    float value = 0.f;
    std::string name;
};

// A preset's name as the player typed it, made safe to be a file's name and
// to show on the handheld: capitals, digits, spaces, - and _, 24 at most.
// Empty if nothing usable is left.
std::string presetNameFrom(const std::string& typed);

class Designer : public DesignDesk {
public:
    // ---- the server's side (any thread) ----
    std::string spec() override;
    std::string state() override;
    bool ask(const std::string& line) override;

    // ---- the program's side (its own thread, once a frame) ----
    std::vector<DesignRequest> take();
    void show(const std::string& spec, const std::string& state);
    // How many times the page has asked how things are: it asks about once a
    // second while it is open, so when this stops going up the page has gone.
    unsigned looks() const;

private:
    mutable std::mutex mutex_;
    std::vector<DesignRequest> waiting_;
    std::string spec_ = "{}", state_ = "{}";
    unsigned looks_ = 0;
};

} // namespace apotheca
