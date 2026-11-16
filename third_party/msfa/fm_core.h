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

// Changed for Apotheca (2026-11-15): in namespace msfa; compute() makes `count`
// samples starting `from` samples into the period (the gains ramp as over the
// whole period), so a voice can be cut anywhere and make the same numbers;
// the feedback is a gain (fm_op_kernel.h); is_carrier() says which operators
// are heard. See third_party/CHANGES.md.

#ifndef __FM_CORE_H
#define __FM_CORE_H

#include "synth.h"
#include "aligned_buf.h"

namespace msfa {

// An operator whose gain is under this all through a period is not worked out.
constexpr int32_t kLevelThresh = 1120;

struct FmOpParams {
  int32_t gain[2];
  int32_t freq;
  int32_t phase;
};

class FmCore {
 public:
  static void dump();
  // Writes (adds into) output[0 .. count): samples from .. from + count of
  // the period whose gains are params[op].gain[0] -> gain[1].
  void compute(int32_t *output, FmOpParams *params, int algorithm,
               int32_t *fb_buf, int32_t fb_gain, int from, int count);
  // Whether operator `op` (0 is OP6, as in a patch) of `algorithm` (0..31)
  // goes to the output.
  static bool is_carrier(int algorithm, int op);
 private:
  AlignedBuf<int32_t, N>buf_[2];
};

}  // namespace msfa

#endif  // __FM_CORE_H
