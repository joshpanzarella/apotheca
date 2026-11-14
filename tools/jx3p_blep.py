#!/usr/bin/env python3
"""Makes engine/src/jx3p/blep_table.hpp: the JX-3P DCOs' corrections (plan batch 2).

A BLEP is what a jump in a wave needs added around it to take its aliasing out
(a "band-limited step" minus the plain step); a BLAMP is the same for a bend
(a change of slope). These are MINIMUM-PHASE ones: all of the correction comes
after the jump, so the DCOs are not late by a single sample (a key still sounds
on its own sample: jx3p_test testAKeyStartsOnItsSample).

How: a Kaiser-windowed sinc (8 zero crossings a side, cutoff 0.45 of the sample
rate, beta 9) at 64 points a sample, made minimum-phase through its cepstrum,
summed to a step, cut at 16 samples with its last two faded to exactly 0.
Measured with it (2026-11-14, numpy, the scratch prototype): a saw at 4186 Hz
-98 dB of aliasing under 15 kHz, a 10% pulse -91; the plan's bar is 50.
The same with 16 crossings a side: -56 / -43 (its minimum-phase tail is too
long for 16 samples). The step's response: -0.1 dB at 15 kHz, -3 at 20 kHz.

Run it again only to change the table (python3 tools/jx3p_blep.py); the
numbers are written out, so every machine plays the same bits (no maths
library runs on them at play time).
"""
import os
import numpy as np

ZEROS, PHASES, LENGTH, CUTOFF, BETA = 8, 64, 16, 0.45, 9.0


def tables():
    n = np.arange(-ZEROS * PHASES, ZEROS * PHASES + 1) / PHASES
    h = 2 * CUTOFF * np.sinc(2 * CUTOFF * n) * np.kaiser(len(n), BETA)
    h /= h.sum()
    size = 1 << 18
    spectrum = np.fft.fft(h, size)
    cep = np.fft.ifft(np.log(np.maximum(np.abs(spectrum), 1e-12))).real
    fold = np.zeros(size)
    fold[0] = 1
    fold[1:size // 2] = 2
    fold[size // 2] = 1
    minimum = np.fft.ifft(np.exp(np.fft.fft(cep * fold))).real[: LENGTH * PHASES + 1]
    step = np.cumsum(minimum)
    step /= step[-1]
    blep = step - 1.0
    # The bend's correction: the step's correction summed (in samples).
    ramp = np.concatenate([[0.0], np.cumsum((blep[1:] + blep[:-1]) * 0.5 / PHASES)])
    carry = ramp[-1]
    fade = np.ones_like(blep)
    k = 2 * PHASES
    fade[-k:] = 0.5 * (1 + np.cos(np.linspace(0, np.pi, k)))
    blep = blep * fade
    blamp = (ramp - carry) * fade
    for t in (blep, blamp):
        t[np.abs(t) < 1e-20] = 0.0
    blep[-1] = blamp[-1] = 0.0
    return blep, blamp, carry


def literal(v):
    text = "%.9g" % v
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def rows(values):
    out = []
    for i in range(0, len(values), 8):
        out.append("    " + ", ".join(literal(v) for v in values[i:i + 8]) + ",")
    return "\n".join(out)


def main():
    blep, blamp, carry = tables()
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, "..", "engine", "src", "jx3p", "blep_table.hpp")
    with open(path, "w") as f:
        f.write("""#pragma once
// MADE BY tools/jx3p_blep.py: do not edit by hand. The JX-3P DCOs' minimum-phase
// corrections (see that script, and engine/src/jx3p/dco.hpp).
//
// kBlep[i]: what a jump of 1 needs added i / kBlepPhases samples after it.
// kBlamp[i]: what a bend of 1 (a change of slope of 1 a sample) needs added
// there, less kBlampCarry, which it needs added from the bend on for good
// (a minimum-phase filter is late at the bottom: a bent line stays that far
// behind). Both are exactly 0 at their last entry.

namespace apotheca {
namespace jx3p {

constexpr int kBlepPhases = %d;  // points a sample
constexpr int kBlepLength = %d;  // samples
constexpr int kBlepPoints = kBlepPhases * kBlepLength + 1;
constexpr double kBlampCarry = %.17g;

constexpr float kBlep[kBlepPoints] = {
%s
};

constexpr float kBlamp[kBlepPoints] = {
%s
};

} // namespace jx3p
} // namespace apotheca
""" % (PHASES, LENGTH, carry, rows(blep), rows(blamp)))
    print("wrote", os.path.normpath(path), "carry", carry)


if __name__ == "__main__":
    main()
