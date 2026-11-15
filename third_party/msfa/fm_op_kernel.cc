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

// Changed for Apotheca (2026-11-15): see fm_op_kernel.h and
// third_party/CHANGES.md.

#include "synth.h"

#include "sin.h"
#include "fm_op_kernel.h"

namespace msfa {

void FmOpKernel::compute(int32_t *output, const int32_t *input,
                         int32_t phase0, int32_t freq,
                         int32_t gain, int32_t dgain, int count, bool add) {
  int32_t phase = phase0;
  if (add) {
    for (int i = 0; i < count; i++) {
      gain += dgain;
      int32_t y = Sin::lookup(phase + input[i]);
      output[i] += ((int64_t)y * (int64_t)gain) >> 24;
      phase += freq;
    }
  } else {
    for (int i = 0; i < count; i++) {
      gain += dgain;
      int32_t y = Sin::lookup(phase + input[i]);
      output[i] = ((int64_t)y * (int64_t)gain) >> 24;
      phase += freq;
    }
  }
}

void FmOpKernel::compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                              int32_t gain, int32_t dgain, int count, bool add) {
  int32_t phase = phase0;
  if (add) {
    for (int i = 0; i < count; i++) {
      gain += dgain;
      int32_t y = Sin::lookup(phase);
      output[i] += ((int64_t)y * (int64_t)gain) >> 24;
      phase += freq;
    }
  } else {
    for (int i = 0; i < count; i++) {
      gain += dgain;
      int32_t y = Sin::lookup(phase);
      output[i] = ((int64_t)y * (int64_t)gain) >> 24;
      phase += freq;
    }
  }
}

void FmOpKernel::compute_fb(int32_t *output, int32_t phase0, int32_t freq,
                            int32_t gain, int32_t dgain, int count,
                            int32_t *fb_buf, int32_t fb_gain, bool add) {
  int32_t phase = phase0;
  int32_t y0 = fb_buf[0];
  int32_t y = fb_buf[1];
  if (add) {
    for (int i = 0; i < count; i++) {
      gain += dgain;
      int32_t scaled_fb = ((int64_t)(y0 + y) * fb_gain) >> 32;
      y0 = y;
      y = Sin::lookup(phase + scaled_fb);
      y = ((int64_t)y * (int64_t)gain) >> 24;
      output[i] += y;
      phase += freq;
    }
  } else {
    for (int i = 0; i < count; i++) {
      gain += dgain;
      int32_t scaled_fb = ((int64_t)(y0 + y) * fb_gain) >> 32;
      y0 = y;
      y = Sin::lookup(phase + scaled_fb);
      y = ((int64_t)y * (int64_t)gain) >> 24;
      output[i] = y;
      phase += freq;
    }
  }
  fb_buf[0] = y0;
  fb_buf[1] = y;
}

}  // namespace msfa
