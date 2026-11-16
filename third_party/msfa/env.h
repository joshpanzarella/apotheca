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

// Changed for Apotheca (2026-11-15): in namespace msfa; the rates follow the
// sample rate (init_sr: they were right only at the original's own 49096 Hz);
// a retrigger may keep the level it has (no click on a stolen voice); the
// rates can be bent while a note sounds (bend_rates: the ATTACK, DECAY and
// RELEASE macros); what the voice asks of it (level, stage, down). See
// third_party/CHANGES.md.

#ifndef __ENV_H
#define __ENV_H

// DX7 envelope generation

namespace msfa {

class Env {
 public:

  // The rates and levels arrays are calibrated to match the Dx7 parameters
  // (ie, value 0..99). The outlevel parameter is calibrated in microsteps
  // (ie units of approx .023 dB), with 99 * 32 = nominal full scale. The
  // rate_scaling parameter is in qRate units (ie 0..63).
  // keep_level: a retrigger, from the level it is at (else from silence).
  void init(const int rates[4], const int levels[4], int outlevel,
      int rate_scaling, bool keep_level = false);

  // The rates for this sample rate. Once, before any note.
  static void init_sr(double sample_rate);

  // Added to each stage's qrate (0..63), from the next sample on: the stage
  // under way moves at its new rate at once.
  void bend_rates(const int qrate_offsets[4]);

  int32_t level() const { return level_; }
  int stage() const { return ix_; }   // 0..3, 4: the release has landed
  bool down() const { return down_; }

  // Result is in Q24/doubling log format. Also, result is subsampled
  // for every N samples.
  // A couple more things need to happen for this to be used as a gain
  // value. First, the # of outputs scaling needs to be applied. Also,
  // modulation.
  // Then, of course, log to linear.
  int32_t getsample();

  void keydown(bool down);
  void setparam(int param, int value);
  static int scaleoutlevel(int outlevel);
 private:
  static int32_t sr_scale_;  // Q24: the original's sample rate over this one
  int bend_[4] = {0, 0, 0, 0};
  int rates_[4];
  int levels_[4];
  int outlevel_;
  int rate_scaling_;
  // Level is stored so that 2^24 is one doubling, ie 16 more bits than
  // the DX7 itself (fraction is stored in level rather than separate
  // counter)
  int32_t level_ = 0;
  int targetlevel_ = 0;
  bool rising_ = false;
  int ix_ = 4;
  int inc_ = 0;

  bool down_ = false;

  void advance(int newix);
};

}  // namespace msfa

#endif  // __ENV_H
