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

// Changed for Apotheca (2026-11-15): see dx7note.h and third_party/CHANGES.md.
// The helpers below are the original's, given internal linkage; init,
// control, render and silent are Apotheca's, built on the original's init and
// compute.

#include <math.h>

#include "synth.h"
#include "freqlut.h"
#include "exp2.h"
#include "dx7note.h"

namespace msfa {

static int32_t midinote_to_logfreq(int midinote) {
  const int base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
  const int step = (1 << 24) / 12;
  return base + step * midinote;
}

static const int32_t coarsemul[] = {
  -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
  50331648, 53182516, 55732705, 58039632, 60145690, 62083076, 63876816,
  65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
  74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032,
  81503396, 82323963, 83117622
};

static int32_t osc_freq(int midinote, int mode, int coarse, int fine, int detune) {
  // TODO: pitch randomization
  int32_t logfreq;
  if (mode == 0) {
    logfreq = midinote_to_logfreq(midinote);
    logfreq += coarsemul[coarse & 31];
    if (fine) {
      // (1 << 24) / log(2)
      logfreq += (int32_t)floor(24204406.323123 * log(1 + 0.01 * fine) + 0.5);
    }
    // This was measured at 7.213Hz per count at 9600Hz, but the exact
    // value is somewhat dependent on midinote. Close enough for now.
    logfreq += 12606 * (detune - 7);
  } else {
    // ((1 << 24) * log(10) / log(2) * .01) << 3
    logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
    logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
  }
  return logfreq;
}

static const uint8_t velocity_data[64] = {
  0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
  166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202,
  205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232,
  233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251,
  252, 253, 254
};

// See "velocity" section of notes. Returns velocity delta in microsteps.
static int ScaleVelocity(int velocity, int sensitivity) {
  int clamped_vel = max(0, min(127, velocity));
  int vel_value = velocity_data[clamped_vel >> 1] - 239;
  int scaled_vel = ((sensitivity * vel_value + 7) >> 3) << 4;
  return scaled_vel;
}

static int ScaleRate(int midinote, int sensitivity) {
  int x = min(31, max(0, midinote / 3 - 7));
  int qratedelta = (sensitivity * x) >> 3;
#ifdef SUPER_PRECISE
  int rem = x & 7;
  if (sensitivity == 3 && rem == 3) {
    qratedelta -= 1;
  } else if (sensitivity == 7 && rem > 0 && rem < 4) {
    qratedelta += 1;
  }
#endif
  return qratedelta;
}

static const uint8_t exp_scale_data[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
  80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250
};

static int ScaleCurve(int group, int depth, int curve) {
  int scale;
  if (curve == 0 || curve == 3) {
    // linear
    scale = (group * depth * 329) >> 12;
  } else {
    // exponential
    int n_scale_data = sizeof(exp_scale_data);
    int raw_exp = exp_scale_data[min(group, n_scale_data - 1)];
    scale = (raw_exp * depth * 329) >> 15;
  }
  if (curve < 2) {
    scale = -scale;
  }
  return scale;
}

static int ScaleLevel(int midinote, int break_pt, int left_depth, int right_depth,
    int left_curve, int right_curve) {
  int offset = midinote - break_pt - 17;
  if (offset >= 0) {
    return ScaleCurve(offset / 3, right_depth, right_curve);
  } else {
    return ScaleCurve((-offset) / 3, left_depth, left_curve);
  }
}

static const uint8_t pitchmodsenstab[] = {
  0, 10, 20, 33, 55, 92, 153, 255
};

// Apotheca: the amplitude modulation's depth at AMD 99 for each AMS (0..3),
// in Q8 doublings: 0, 3, 9 and 24 dB. A GUESS: the original left AM out, and
// these were not measured on the real one.
static const int32_t ams_doublings[4] = {0, 128, 384, 1024};

// Apotheca: which way each operator is moved by NoteBend::detune_spread, in
// thirds (OP6 first).
static const int32_t spread_thirds[6] = {3, -3, 2, -2, 1, -1};

void Dx7Note::init(const char patch[156], int midinote, int velocity,
    bool retrigger) {
  algorithm_ = patch[134] & 31;
  // TRANSPOSE: 24 is none. It moves the pitch, not the keyboard's scaling.
  int pitchnote = midinote + patch[144] - 24;
  bool keysync = patch[136] != 0;
  int rates[4];
  int levels[4];
  for (int op = 0; op < 6; op++) {
    int off = op * 21;
    carrier_[op] = FmCore::is_carrier(algorithm_, op);
    on_[op] = ((patch[155] >> op) & 1) != 0;
    for (int i = 0; i < 4; i++) {
      rates[i] = patch[off + i];
      levels[i] = patch[off + 4 + i];
    }
    // A carrier's release ends in silence, whatever its L4: on the original a
    // carrier with L4 over 0 rang for ever once let go.
    if (carrier_[op]) levels[3] = 0;
    int outlevel = patch[off + 16];
    outlevel = Env::scaleoutlevel(outlevel);
    int level_scaling = ScaleLevel(midinote, patch[off + 8], patch[off + 9],
        patch[off + 10], patch[off + 11], patch[off + 12]);
    outlevel += level_scaling;
    outlevel = min(127, outlevel);
    outlevel = outlevel << 5;
    outlevel += ScaleVelocity(velocity, patch[off + 15]);
    outlevel = max(0, outlevel);
    int rate_scaling = ScaleRate(midinote, patch[off + 13]);
    env_[op].init(rates, levels, outlevel, rate_scaling, retrigger);

    int mode = patch[off + 17];
    int coarse = patch[off + 18];
    int fine = patch[off + 19];
    int detune = patch[off + 20];
    basepitch_[op] = osc_freq(pitchnote, mode, coarse, fine, detune);
    detune_[op] = basepitch_[op] - osc_freq(pitchnote, mode, coarse, fine, 7);
    ams_[op] = patch[off + 14] & 3;
    if (!retrigger || keysync) params_[op].phase = 0;
    if (!retrigger) params_[op].gain[1] = 0;
  }
  for (int i = 0; i < 4; i++) {
    rates[i] = patch[126 + i];
    levels[i] = patch[130 + i];
  }
  pitchenv_.set(rates, levels);
  feedback_ = patch[135] & 7;
  fb_gain_ = feedback_ != 0 ? 1 << (23 + feedback_) : 0;
  pmd_ = patch[139];
  amd_ = patch[140];
  pitchmodsens_ = pitchmodsenstab[patch[143] & 7];
  if (!retrigger) fb_buf_[0] = fb_buf_[1] = 0;
}

void Dx7Note::control(int32_t lfo_val, int32_t lfo_delay, int32_t pitch,
    const NoteBend &bend) {
  int32_t pitchmod = ((int64_t)pitchenv_.getsample() * bend.peg_scale) >> 8;
  int pmd = bend.pmd >= 0 ? bend.pmd : pmd_;
  int amd = bend.amd >= 0 ? bend.amd : amd_;
  uint32_t pmdq = (uint32_t)((pmd * 165) >> 6) * (uint32_t)lfo_delay;  // Q32
  int32_t senslfo = pitchmodsens_ * (lfo_val - (1 << 23));
  pitchmod += (((int64_t)pmdq) * (int64_t)senslfo) >> 39;
  pitchmod += pitch;
  // The amplitude modulation: Q24 of its most, 0 .. 1.
  uint32_t amdq = (uint32_t)((amd * 165) >> 6) * (uint32_t)lfo_delay;  // Q32
  int32_t am = (int32_t)(((uint64_t)amdq * (uint32_t)lfo_val) >> 32);
  for (int op = 0; op < 6; op++) {
    env_[op].bend_rates(bend.rate_bend);
    params_[op].gain[0] = params_[op].gain[1];
    int32_t level = env_[op].getsample();
    if (!carrier_[op]) level += bend.mod_level;
    level -= (int32_t)(((int64_t)am * ams_doublings[ams_[op]]) >> 8);
    int32_t x = level - (14 * (1 << 24));
    x = max(-(16 << 24), min(x, 3 << 24));  // (Exp2 shifts by 6 - whole doublings)
    params_[op].gain[1] = on_[op] ? Exp2::lookup(x) : 0;
    int32_t logfreq = basepitch_[op] - detune_[op] +
        (int32_t)(((int64_t)detune_[op] * bend.detune_keep) >> 8) +
        bend.detune_spread / 3 * spread_thirds[op] + pitchmod;
    logfreq = max(-(11 << 24), min(logfreq, (20 << 24) - 1));  // (Freqlut shifts by 20 - whole octaves)
    params_[op].freq = Freqlut::lookup(logfreq);
  }
  fb_gain_now_ = bend.fb_gain >= 0 ? bend.fb_gain : fb_gain_;
}

void Dx7Note::render(int32_t *buf, int from, int count) {
  core_.compute(buf, params_, algorithm_, fb_buf_, fb_gain_now_, from, count);
}

void Dx7Note::keyup() {
  for (int op = 0; op < 6; op++) {
    env_[op].keydown(false);
  }
  pitchenv_.keydown(false);
}

bool Dx7Note::silent() const {
  for (int op = 0; op < 6; op++) {
    if (env_[op].down()) return false;
    if (carrier_[op] && (params_[op].gain[0] >= kLevelThresh ||
        params_[op].gain[1] >= kLevelThresh)) return false;
  }
  return true;
}

}  // namespace msfa
