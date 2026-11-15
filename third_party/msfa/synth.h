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

// Changed for Apotheca (2026-11-15): everything in namespace msfa; N and LG_N
// are constants, not macros (a macro named N broke any header after it); no
// NEON (the C++ kernels everywhere, so the desk and the handheld make the same
// numbers); no memory barrier (nothing here is shared between threads); min
// and max are std's. See third_party/CHANGES.md.

#ifndef __SYNTH_H
#define __SYNTH_H

#include <stdint.h>

#include <algorithm>

namespace msfa {

// The envelopes, the LFO and the gains move once every N samples.
constexpr int LG_N = 6;
constexpr int N = 1 << LG_N;

using std::min;
using std::max;

}  // namespace msfa

#endif  // __SYNTH_H
