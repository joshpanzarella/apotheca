#pragma once
// DRUM SYNTH's named voices (docs/plans/drumsynth-plan.md, batch 3): each model's built-in voices,
// as data compiled in from app/presets/drumvoices.inc, read once. A voice is a starting point, not
// a reference: a lane keeps its eight dials (`lanesynth` in the song file), never a voice's name or
// place, so the list may be put in any order and grow anywhere. The VOICE view steps through a
// model's list on MODEL with hold A + LEFT / RIGHT, and names the voice a lane's dials are.
//
// A model's list: place 0 is its usual voice, called CLASSIC (drumsynth::usualVoice); then the
// data's, in their order.
//
// The data's words, one a line, `#` to the end of a line is a note:
//   voice WORD NAME      a new voice of the model whose song-file word is WORD ("kick"); NAME up to
//                        kVoiceNameMost letters, digits and spaces (upper case)
//   set PLACE DIAL ...   its dials by the place's name on that model (TUNE, DECAY, BEND...), 0..100;
//                        a place not set is the model's usual
// Every line it cannot take is a problem, and `drumsynthapp_test` fails on one; so is a voice that is
// the usual, or another's, or a name twice in a model. No SDL here.

#include <string>
#include <vector>

#include "apotheca/drum_synth.hpp"

namespace apotheca {

constexpr std::size_t kVoiceNameMost = 12;
constexpr const char* kUsualVoiceName = "CLASSIC";

struct NamedVoice {
    std::string name;
    drumsynth::Voice voice;
};

struct DrumVoiceData {
    std::vector<NamedVoice> voices[drumsynth::kModels]; // the data's, by model (the usual is not in it)
    std::vector<std::string> problems;                  // "line 12: ..." for each line not taken
};

DrumVoiceData readDrumVoices(const std::string& text);
const DrumVoiceData& drumVoices();        // the data compiled in, read the first time it is asked for
const char* drumVoicesText();             // ...as written

// A model's list, the usual first. Out of range: 0 voices, "" and no voice.
int voiceCount(int model);
const char* voiceName(int model, int place);
drumsynth::Voice voiceAt(int model, int place);
// The place in its model's list whose dials these are, or -1 (turned by hand, or no voice).
int voicePlaceOf(const drumsynth::Voice& voice);

} // namespace apotheca
