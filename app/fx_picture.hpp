#pragma once
// The FX screen's picture of what an effect does (owner, 2026-11-24: "a more visually interesting design"; design B):
// drawn from its settings as they read now, above the dials. A filter's curve, a drive's bent wave, a delay's echoes,
// a reverb's tail, a MOD's sweep, a COMP's curve and how far it is turning down. Theme colours only, no blending,
// no words: everything said is said by the heading and the dials.
#include <SDL.h>

#include "apotheca/effects.hpp"

namespace apotheca {

// While the song plays the picture moves with it (owner, 2026-11-24: "additional light visual feedback"): where the
// song is (in steps from the scene's top, with the part of the step gone), how long a step is, and how loud the track
// is coming out (0..1). Stopped, nothing moves: the picture is the settings alone.
struct FxLive {
    bool playing = false;
    double steps = 0.0;
    float stepSeconds = 0.125f;
    float level = 0.f;
};

// `shown`: the effect's settings, 0..1, as the dials show them (automation playing included). `meterDb`: what the
// effect is doing just now (Engine::effectMeter; 0 where it has no meter).
void drawEffectPicture(SDL_Renderer* r, int x, int y, int w, int h, EffectKind kind, const float* shown, float meterDb, const FxLive& live = FxLive());

// Each theme has a style of its own (owner, 2026-11-24: "a little extra sauce based on the theme"): VINTAGE (BROWN,
// GRUVBOX: a knurled cap, an engraved scale round the ring), NEON (DRACULA, TOKYO, MONOKAI: a glow ring outside the
// lit arc, the pointer lit, the curves with an afterglow) and CLEAN (NORD and the light ones: a thin ring, a dot for
// a pointer, no grid). Colours stay the theme's: only shapes differ.
enum class FxStyle { Vintage, Neon, Clean };
FxStyle fxStyle(); // the theme drawn now's

// The dials' ring, shared with the tests that read it. Angles are on the screen (y down), in degrees: a dial runs
// clockwise from kDialFrom (down and to the left) for kDialSweep.
constexpr float kDialFrom = 135.f, kDialSweep = 270.f;
// Where on a ring `part` (0..1) stands: the pixel at radius `radius` from (cx, cy).
SDL_Point dialPoint(int cx, int cy, float radius, float part);
// A dial: an amount as a lit arc of a ring `ring` thick and `radius` out; a choice (choices > 0) as a dot a choice,
// the one it is lit; the pointer in both. `mark`: a MOD's brass tick where the setting is given now (NaN: none).
void drawDial(SDL_Renderer* r, int cx, int cy, int radius, int ring, float part, int choices, int which, SDL_Color lit, float mark);

} // namespace apotheca
