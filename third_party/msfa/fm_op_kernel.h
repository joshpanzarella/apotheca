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

// Changed for Apotheca (2026-11-15): in namespace msfa; each kernel makes any
// count of samples (1..N) from a given gain, so a voice's period can be cut
// anywhere and still make the same numbers; the feedback is a gain, not a
// shift, so it can sit between two settings (the FEEDBACK macro; at a whole
// setting it is the original to the bit); no NEON, and the experimental
// kernels that were switched off are gone. See third_party/CHANGES.md.

#ifndef __FM_OP_KERNEL_H
#define __FM_OP_KERNEL_H

#include <stdint.h>

namespace msfa {

class FmOpKernel {
 public:
  // `gain` is the gain before the first sample; each sample adds `dgain`
  // first (the original's linear step: gain for sample i of a period is
  // gain1 + (1 + i) * dgain). Makes `count` samples.

  // This is the basic FM operator. No feedback.
  static void compute(int32_t *output, const int32_t *input,
                      int32_t phase0, int32_t freq,
                      int32_t gain, int32_t dgain, int count, bool add);

  // This is a sine generator, no feedback.
  static void compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                           int32_t gain, int32_t dgain, int count, bool add);

  // One op with feedback. fb_gain: the sum of the last two samples is
  // multiplied by it and shifted down 32 (the original's shift s is
  // fb_gain = 1 << (31 - s)).
  static void compute_fb(int32_t *output, int32_t phase0, int32_t freq,
                         int32_t gain, int32_t dgain, int count,
                         int32_t *fb_buf, int32_t fb_gain, bool add);
};

}  // namespace msfa

#endif  // __FM_OP_KERNEL_H
