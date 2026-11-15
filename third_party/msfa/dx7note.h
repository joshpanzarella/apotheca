/*
 * Copyright 2012 Google Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Changed for Apotheca (2026-11-15), see third_party/CHANGES.md: in namespace
// msfa; a note is worked out in two halves, control() once a period (the
// envelopes, the LFO, the gains) and render() for any part of the period, so a
// voice can be cut anywhere and make the same numbers; a retrigger can carry
// on from where the note is (no click on a stolen voice); the patch's
// TRANSPOSE, OPERATOR ON/OFF, OSCILLATOR KEY SYNC and the LFO's amplitude
// modulation (AMD and each operator's AMS) are played, which the original left
// out; a carrier's release always ends in silence; live bends (NoteBend) for a
// synth's macros; silent() says when the note can be let go.

#ifndef SYNTH_DX7NOTE_H_
#define SYNTH_DX7NOTE_H_

// This is the logic to put together a note from the MIDI description
// and run the low-level modules.

#include "env.h"
#include "pitchenv.h"
#include "fm_core.h"

namespace msfa {

// What a synth bends while a note sounds (Apotheca's macros). Read at every
// control step. A fresh one changes nothing.
struct NoteBend {
  int32_t mod_level = 0;     // Q24 doublings added to every modulator's level
  int32_t fb_gain = -1;      // the feedback as a gain (fm_op_kernel.h); -1: the patch's
  int32_t detune_keep = 256; // Q8: how much of the patch's own DETUNE each operator keeps
  int32_t detune_spread = 0; // Q24 octaves: the most an operator is moved apart from the others
  int32_t peg_scale = 256;   // Q8: the pitch envelope's depth
  int pmd = -1, amd = -1;    // the LFO's depths, 0..99; -1: the patch's
  int rate_bend[4] = {0, 0, 0, 0};  // qrate added to each stage of every operator's envelope
};

class Dx7Note {
 public:
  // patch: the 155 voice parameters as unpacked (OP6 first), [155] the
  // operators on (bit 5 OP1 .. bit 0 OP6). velocity: 0..127. retrigger: the
  // note carries on from where it is (its levels, and its phases unless the
  // patch's OSCILLATOR KEY SYNC is on); else it starts from silence.
  void init(const char patch[156], int midinote, int velocity, bool retrigger);

  // Once a period of N samples, before it: the envelopes and the pitch move,
  // and the gains for the period are set. pitch: Q24 octaves added to every
  // operator (a glide).
  void control(int32_t lfo_val, int32_t lfo_delay, int32_t pitch,
               const NoteBend &bend);

  // Adds samples from .. from + count of the period into buf[0 .. count).
  void render(int32_t *buf, int from, int count);

  void keyup();

  // Let go, and every carrier under the level the core works out, all
  // through the period control() set up: nothing more will be heard.
  bool silent() const;

 private:
  FmCore core_;
  Env env_[6];
  FmOpParams params_[6];
  PitchEnv pitchenv_;
  int32_t basepitch_[6];
  int32_t detune_[6];    // what the patch's DETUNE adds to each operator's pitch
  int32_t fb_buf_[2] = {0, 0};
  int32_t fb_gain_ = 0;      // the patch's feedback as a gain
  int32_t fb_gain_now_ = 0;  // ...as bent, for this period
  bool carrier_[6];
  bool on_[6];
  int ams_[6];

  int algorithm_ = 0;
  int feedback_ = 0;     // the patch's, 0..7
  int pmd_ = 0, amd_ = 0;  // the patch's LFO depths, 0..99
  int pitchmodsens_ = 0;
};

}  // namespace msfa

#endif  // SYNTH_DX7NOTE_H_
