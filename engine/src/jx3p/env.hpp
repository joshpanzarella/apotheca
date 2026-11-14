#pragma once
// JX-3P: the control tick, the envelope, the sample-and-hold, the VCA.
//
// The real JX-3P works its envelope and its LFO out in software. Its CPU goes
// round one loop "within approx. 10mS" (Service Notes p.6): read the keys,
// work out the envelope and the LFO, read the panel, write every control
// voltage through a D/A into a sample-and-hold, write the DCO counters. So
// every CV is a STEP every 10 ms, softened only by the hold capacitor. That is
// part of the sound and is kept: here a voice's envelope moves once a TICK
// (kTickSeconds), and what it gives the VCA goes through a one-pole smoothing
// (`Hold`) for the capacitor.
//
// Rack-free and SDL-free: the engine and its tests read it as it is. No
// allocation, nothing to wait for: fine on the audio thread.
//
// THE GUESSES. Nothing published gives a time, a curve or a taper. Each is
// one constant or one table here, a stated guess (SELF), until a recording of
// a real JX-3P measures it (the plan's batch 9):
//
//   | What                         | Here                                         | Source |
//   | ---------------------------- | -------------------------------------------- | ------ |
//   | The tick                     | 10 ms (kTickSeconds), 480 samples at 48 kHz  | SN p.6 ("approx. 10mS") |
//   | The hold's smoothing         | 1 ms, one pole (kHoldSeconds), landing at    | SELF   |
//   |                              | 1e-5 from its target (kHoldLands)            |        |
//   | ATTACK 0 to 10               | one tick to 4 s, even in log (kAttackSeconds)| SELF   |
//   | DECAY, RELEASE 0 to 10       | one tick to 10 s, even in log (kFallSeconds) | SELF   |
//   | The attack's curve           | heads for 1.5 and is stopped at 1 (kAttackAim)| SELF  |
//   | Decay and release            | a fall by a fixed ratio a tick, landing on   | SELF   |
//   |                              | its target when 80 dB from it (kEnvLand)     |        |
//   | SUSTAIN 0 to 10              | 0 to 1, straight                             | SELF   |
//   | The VCA's taper              | 4 dB a dial step (kVcaDbPerDial), 0 dB at    | SELF   |
//   |                              | LEVEL 7 (kVcaUnityDial), LEVEL 0 silent      |        |
//   | Too high a LEVEL             | a soft clip over what LEVEL 7 gives a full   | OM p.26 says it distorts; |
//   |                              | wave (kVcaClipDial), to 1.5 times that at    | the shape is SELF |
//   |                              | most (kVcaClipCeiling)                       |        |
//   | VELOCITY ON                  | 30 dB from full to none (kVelocityDb); full  | SELF (the real one has none) |
//   |                              | velocity is exactly 1                        |        |
//
// "A time" here is the time to land: an attack from 0 to the top, a decay
// from the top to a SUSTAIN of 0, a release from the top to silence. Entry 0
// of each table is ONE TICK: ATTACK 0 is at the top on the first tick, and
// all four at 0 is a note one tick long, the owner's manual's "extremely
// short pulse" (OM p.27).

#include <cmath>

namespace apotheca {
namespace jx3p {

constexpr double kTickSeconds = 0.010; // SN p.6
constexpr double kHoldSeconds = 0.001; // SELF
constexpr float kHoldLands = 1e-5f;    // a held CV this near its target is set to it: a held CV is a constant.
                                       // (Not much less: near 1 a float step of k times the gap is lost under 1.4e-6, and it never lands.)

// The dial is kept in tenths: 0..100 is the PG-200's 0..10.
constexpr float kAttackSeconds[101] = {
    0.01f, 0.01062f, 0.01127f, 0.01197f, 0.01271f, 0.01349f, 0.01433f, 0.01521f, 0.01615f, 0.01715f,
    0.01821f, 0.01933f, 0.02052f, 0.02179f, 0.02314f, 0.02456f, 0.02608f, 0.02769f, 0.0294f, 0.03122f,
    0.03314f, 0.03519f, 0.03736f, 0.03967f, 0.04212f, 0.04472f, 0.04748f, 0.05041f, 0.05353f, 0.05683f,
    0.06034f, 0.06407f, 0.06802f, 0.07222f, 0.07668f, 0.08142f, 0.08645f, 0.09178f, 0.09745f, 0.1035f,
    0.1099f, 0.1166f, 0.1238f, 0.1315f, 0.1396f, 0.1482f, 0.1574f, 0.1671f, 0.1774f, 0.1884f,
    0.2f, 0.2123f, 0.2255f, 0.2394f, 0.2542f, 0.2699f, 0.2865f, 0.3042f, 0.323f, 0.3429f,
    0.3641f, 0.3866f, 0.4105f, 0.4358f, 0.4627f, 0.4913f, 0.5216f, 0.5538f, 0.588f, 0.6243f,
    0.6629f, 0.7038f, 0.7473f, 0.7934f, 0.8424f, 0.8944f, 0.9497f, 1.008f, 1.071f, 1.137f,
    1.207f, 1.281f, 1.36f, 1.444f, 1.534f, 1.628f, 1.729f, 1.836f, 1.949f, 2.069f,
    2.197f, 2.333f, 2.477f, 2.63f, 2.792f, 2.965f, 3.148f, 3.342f, 3.548f, 3.767f,
    4.f,
};
constexpr float kFallSeconds[101] = {
    0.01f, 0.01072f, 0.01148f, 0.0123f, 0.01318f, 0.01413f, 0.01514f, 0.01622f, 0.01738f, 0.01862f,
    0.01995f, 0.02138f, 0.02291f, 0.02455f, 0.0263f, 0.02818f, 0.0302f, 0.03236f, 0.03467f, 0.03715f,
    0.03981f, 0.04266f, 0.04571f, 0.04898f, 0.05248f, 0.05623f, 0.06026f, 0.06457f, 0.06918f, 0.07413f,
    0.07943f, 0.08511f, 0.0912f, 0.09772f, 0.1047f, 0.1122f, 0.1202f, 0.1288f, 0.138f, 0.1479f,
    0.1585f, 0.1698f, 0.182f, 0.195f, 0.2089f, 0.2239f, 0.2399f, 0.257f, 0.2754f, 0.2951f,
    0.3162f, 0.3388f, 0.3631f, 0.389f, 0.4169f, 0.4467f, 0.4786f, 0.5129f, 0.5495f, 0.5888f,
    0.631f, 0.6761f, 0.7244f, 0.7762f, 0.8318f, 0.8913f, 0.955f, 1.023f, 1.096f, 1.175f,
    1.259f, 1.349f, 1.445f, 1.549f, 1.66f, 1.778f, 1.905f, 2.042f, 2.188f, 2.344f,
    2.512f, 2.692f, 2.884f, 3.09f, 3.311f, 3.548f, 3.802f, 4.074f, 4.365f, 4.677f,
    5.012f, 5.37f, 5.754f, 6.166f, 6.607f, 7.079f, 7.586f, 8.128f, 8.71f, 9.333f,
    10.f,
};

constexpr float kAttackAim = 1.5f;   // SELF: where the attack heads for; it is stopped at 1
constexpr float kEnvLand = 1e-4f;    // SELF: a decay or a release this near its target (80 dB) is on it
constexpr float kVcaDbPerDial = 4.f; // SELF: a whole step of the dial (ten tenths)
constexpr float kVcaUnityDial = 70.f;// SELF: LEVEL 7.0 is 0 dB: a full wave there just meets the clip
constexpr float kVcaClipDial = 70.f; // SELF: over what LEVEL 7.0 gives a full wave, a soft clip comes in
constexpr float kVcaClipCeiling = 1.5f; // SELF: ...and bends toward this many times that, never past it
constexpr float kVelocityDb = 30.f;  // SELF: VELOCITY ON, velocity 0 is this far under velocity 1

inline float dialHeld(float dial) { return dial < 0.f ? 0.f : (dial > 100.f ? 100.f : dial); }

// A time from a table at a dial that may sit between two tenths (a macro): even in log between them.
inline double tableSeconds(const float* table, float dial) {
    dial = dialHeld(dial);
    const int below = dial >= 100.f ? 99 : static_cast<int>(dial);
    const double part = static_cast<double>(dial) - below;
    return std::exp(std::log(static_cast<double>(table[below])) * (1.0 - part) + std::log(static_cast<double>(table[below + 1])) * part);
}
// That time as whole ticks: never less than one.
inline int ticksFor(double seconds) {
    const long n = std::lround(seconds / kTickSeconds);
    return n < 1 ? 1 : static_cast<int>(n);
}

// What every voice's envelope reads on its tick, worked out when a knob or a macro moves.
struct EnvTimes {
    float attackCoef = 1.f;   // per tick, toward kAttackAim
    float decayRatio = 0.f;   // per tick, of what is left to the sustain
    float releaseRatio = 0.f; // per tick, of what is left
    float sustain = 1.f;
    int attackTicks = 1, decayTicks = 1, releaseTicks = 1;

    // The dials, 0..100 each (fractions from a macro are fine).
    void set(float attack, float decay, float sustainDial, float release) {
        attackTicks = ticksFor(tableSeconds(kAttackSeconds, attack));
        decayTicks = ticksFor(tableSeconds(kFallSeconds, decay));
        releaseTicks = ticksFor(tableSeconds(kFallSeconds, release));
        // From 0, heading for kAttackAim, to be at 1 on tick `attackTicks`.
        attackCoef = static_cast<float>(1.0 - std::pow((kAttackAim - 1.0) / kAttackAim, 1.0 / attackTicks));
        // From a distance of 1, to be kEnvLand from the target on tick n (and so on it).
        decayRatio = static_cast<float>(std::pow(static_cast<double>(kEnvLand), 1.0 / decayTicks));
        releaseRatio = static_cast<float>(std::pow(static_cast<double>(kEnvLand), 1.0 / releaseTicks));
        sustain = dialHeld(sustainDial) / 100.f;
    }
};

// One voice's envelope: moved only on the voice's tick.
struct Adsr {
    enum Stage { Idle, Attack, Decay, Release };
    Stage stage = Idle;
    float level = 0.f;

    void on() { stage = Attack; } // from wherever it is: a note played again, or a voice stolen, does not start from 0
    void off() {
        if (stage != Idle) stage = Release;
    }
    void stop() { stage = Idle, level = 0.f; }
    bool idle() const { return stage == Idle; }

    // One tick. The landing slack (a part in a hundred thousand) is for float sums that come
    // to a hair under where they were worked out to be.
    void tick(const EnvTimes& t) {
        if (stage == Attack) {
            level += (kAttackAim - level) * t.attackCoef;
            if (level >= 1.f - 1e-5f) level = 1.f, stage = Decay;
        } else if (stage == Decay) {
            level = t.sustain + (level - t.sustain) * t.decayRatio;
            if (std::fabs(level - t.sustain) <= kEnvLand * 1.001f) level = t.sustain;
        } else if (stage == Release) {
            level *= t.releaseRatio;
            if (level <= kEnvLand * 1.001f) level = 0.f, stage = Idle;
        }
    }
};

// The sample-and-hold a CV is written into: a step, softened by one pole, landing exactly.
struct Hold {
    float value = 0.f, target = 0.f;
    bool still() const { return value == target; }
    float next(float k) {
        if (value != target) {
            value += (target - value) * k;
            if (std::fabs(target - value) < kHoldLands) value = target;
        }
        return value;
    }
};
inline float holdCoef(double sampleRate) { return static_cast<float>(1.0 - std::exp(-1.0 / (kHoldSeconds * sampleRate))); }

// The VCA's taper: LEVEL's dial (0..100) to a gain. 0 is silence.
inline float vcaGain(float dial) {
    dial = dialHeld(dial);
    return dial <= 0.f ? 0.f : static_cast<float>(std::pow(10.0, (dial - kVcaUnityDial) / 10.f * kVcaDbPerDial / 20.0));
}
// VELOCITY ON: a velocity (0..1) to a gain. Exactly 1 at full velocity, so a patch plays as written.
inline float velocityGain(float velocity) {
    velocity = velocity < 0.f ? 0.f : (velocity > 1.f ? 1.f : velocity);
    return velocity >= 1.f ? 1.f : static_cast<float>(std::pow(10.0, (velocity - 1.f) * kVelocityDb / 20.0));
}
// Too high a LEVEL distorts (OM p.26): straight up to `from`, then bending over toward
// kVcaClipCeiling times it, with no corner (its slope is 1 where it starts to bend).
inline float vcaClip(float y, float from) {
    const float a = std::fabs(y);
    if (a <= from) return y;
    const float room = (kVcaClipCeiling - 1.f) * from, u = (a - from) / room, bent = from + room * u / (1.f + u);
    return y < 0.f ? -bent : bent;
}

} // namespace jx3p
} // namespace apotheca
