#include "mod_knob.hpp"

#include <cmath>
#include <cstdio>

#include "mods.hpp"

namespace apotheca {

using namespace ui;

namespace {

constexpr float kDepthPerSecond = 1.2f;  // the stick fully over: DEPTH from +25% to +100% in under a second
constexpr float kRatesPerSecond = 5.f;   // the stick held over on RATE: as a choice is stepped
constexpr float kDepthStep = 0.01f;      // the D-pad: 1%

float held1(float v) { return v > 1.f ? 1.f : (v < -1.f ? -1.f : v); }

// A roll for the dice, from a seed and which roll it is (the screen's; nothing of the song's).
std::uint32_t mix(std::uint32_t x) {
    x += 0x9e3779b9u;
    x = (x ^ (x >> 16)) * 0x85ebca6bu;
    x = (x ^ (x >> 13)) * 0xc2b2ae35u;
    return x ^ (x >> 16);
}

} // namespace

// ---- what a MOD says ---------------------------------------------------------------------------

void ModKnob::shapeText(const ModSettings& m, char* out, int size) {
    std::snprintf(out, static_cast<std::size_t>(size), "%s", modKind(m.kind) == kModStepper ? modKindName(kModStepper) : modShapeName(m.shape));
}

void ModKnob::depthText(float depth, char* out, int size) {
    const int percent = static_cast<int>(std::lround(held1(depth) * 100.f));
    std::snprintf(out, static_cast<std::size_t>(size), percent > 0 ? "+%d%%" : "%d%%", percent);
}

void ModKnob::speedText(float hz, char* out, int size) {
    std::snprintf(out, static_cast<std::size_t>(size), "%.2g HZ", static_cast<double>(modSpeed(hz))); // two figures: 0.025, 1.2, 15
}

void ModKnob::moreText(const ModSettings& m, char* out, int size) {
    static const char* const phases[kModPhases] = {"0", "1/4", "1/2", "3/4"};
    if (modKind(m.kind) == kModStepper) std::snprintf(out, static_cast<std::size_t>(size), "%d", modLength(m.length));
    else if (modFree(m)) speedText(m.speed, out, size);
    else std::snprintf(out, static_cast<std::size_t>(size), "%s", phases[modPhase(m.phase)]);
}

ModSettings ModKnob::fresh(int target) {
    ModSettings m;
    m.kind = kModLfo;
    m.target = target;
    m.shape = kModSine;
    m.rate = kModRateBar;
    m.depth = 0.25f;
    return m;
}

ModSettings ModKnob::rolled(const ModSettings& m, std::uint32_t seed) {
    ModSettings out = m;
    if (modKind(m.kind) == kModStepper) {
        for (int i = 0; i < kModSteps; ++i)
            out.steps[i] = static_cast<float>(static_cast<int>(mix(seed + 977u * static_cast<std::uint32_t>(i)) % 201u) - 100) / 100.f;
        return out; // the row only: DEPTH stays, as the STEPS line's roll
    }
    // An LFO: a shape, a rate and a depth, never all three as they were. The rates a wobble is
    // heard at (the faster ones are mostly smoothed away on the newer effects: CLAUDE.md). DEPTH
    // whole percents, 20 to 100 either way (less is barely felt). A FREE one stays free: a shape
    // and a SPEED, 0.1 to 5 Hz (the clicks between), and a depth the same way.
    const auto depth = [&](std::uint32_t i) {
        const std::uint32_t d = mix(seed + 104729u * i + 1u);
        const float magnitude = static_cast<float>(20u + d % 81u) / 100.f;
        return (d >> 8) & 1u ? magnitude : -magnitude;
    };
    if (modFree(m)) {
        for (std::uint32_t i = 0;; ++i) {
            const std::uint32_t roll = mix(seed + 7919u * i);
            out.shape = static_cast<int>(roll % kModShapes);
            out.speed = modSpeedClick(kSpeedRollFirst + static_cast<int>((roll >> 8) % (kSpeedRollLast - kSpeedRollFirst + 1)));
            out.depth = depth(i);
            if (out.shape != m.shape || out.speed != m.speed || out.depth != m.depth) return out;
        }
    }
    static const int rates[] = {kModRateEighth, kModRateQuarterTriplet, kModRateQuarter, kModRateDottedQuarter,
                                kModRateHalf, kModRateBar, kModRateTwoBars, kModRateFourBars};
    for (std::uint32_t i = 0;; ++i) {
        const std::uint32_t roll = mix(seed + 7919u * i);
        out.shape = static_cast<int>(roll % kModShapes);
        out.rate = rates[(roll >> 8) % (sizeof rates / sizeof rates[0])];
        out.depth = depth(i);
        if (out.shape != m.shape || out.rate != m.rate || out.depth != m.depth) return out;
    }
}

// ---- the hold ----------------------------------------------------------------------------------

bool ModKnob::hold(int track, int target, const Input& input) {
    letGo();
    if (track < 0 || track >= kNumTracks || !modTargetFits(session_.project.tracks[track], target)) return false;
    track_ = track;
    target_ = target;
    // The stick already pushed (the cursor was on its way here) is not a turn: it must come back first.
    stickWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
    return true;
}

void ModKnob::letGo() {
    track_ = target_ = -1;
    deleting_ = false;
    deletePart_ = 0.f;
    repeating_ = Button::None;
    stickAxis_ = 0;
    stickSaved_ = 0.f;
    stickWas_ = false;
}

int ModKnob::mod() const {
    if (!held()) return -1;
    for (int m = 0; m < kModsPerTrack; ++m) {
        const ModSettings& s = session_.modData(track_, m);
        if (s.kind != kModOff && s.target == target_) return m;
    }
    return -1;
}

bool ModKnob::full() const {
    if (!held() || mod() >= 0) return false;
    for (int m = 0; m < kModsPerTrack; ++m)
        if (session_.modData(track_, m).kind == kModOff) return false;
    return true;
}

// ---- changing it -------------------------------------------------------------------------------

void ModKnob::depthBy(float by) {
    const int m = mod();
    if (m < 0) {
        // The first UP makes one, in the first MOD that is free (owner's call C). Both taken: MODS FULL.
        if (by <= 0.f) return;
        for (int k = 0; k < kModsPerTrack; ++k)
            if (session_.modData(track_, k).kind == kModOff) {
                session_.setMod(track_, k, fresh(target_));
                return;
            }
        return;
    }
    ModSettings s = session_.modData(track_, m);
    const float hundredths = s.depth * 100.f;
    s.depth = held1((by > 0.f ? std::floor(hundredths + 1.001f) : std::ceil(hundredths - 1.001f)) / 100.f); // landing on whole percents
    set(s);
}

void ModKnob::rateBy(int by) {
    const int m = mod();
    if (m < 0) return;
    ModSettings s = session_.modData(track_, m);
    const int place = modRatePlace(s.rate) + by;
    if (place < 0 || place >= rates(s)) return; // the ends stop (a STEPPER's at 8 BARS, an LFO's at FREE)
    s.rate = modRateAt(place);
    set(s);
}

void ModKnob::moreBy(int by) {
    const int m = mod();
    if (m < 0) return;
    ModSettings s = session_.modData(track_, m);
    if (modKind(s.kind) == kModStepper) {
        const int to = modLength(s.length) + by;
        if (to < 1 || to > kModSteps) return;
        s.length = to;
    } else if (modFree(s)) {
        s.speed = modSpeedStep(s.speed, by); // (at an end: as it was, and set() sends nothing)
    } else {
        const int to = modPhase(s.phase) + by;
        if (to < 0 || to >= kModPhases) return;
        s.phase = to;
    }
    set(s);
}

ModSettings ModKnob::shapeAt(const ModSettings& m, int place, int track) {
    ModSettings s = m;
    if (place < kModShapes) {
        s.kind = kModLfo;
        s.shape = modShape(place);
        return s;
    }
    s.kind = kModStepper;
    // A row of nothing moves nothing: one never written is rolled as it comes in, so it is heard.
    bool any = false;
    for (int i = 0; i < kModSteps; ++i) any = any || s.steps[i] != 0.f;
    if (!any) s = rolled(s, 0x51ed27u + static_cast<std::uint32_t>(track) * 131u + static_cast<std::uint32_t>(s.target));
    return s;
}

void ModKnob::nextShape() {
    const int m = mod();
    if (m < 0) return;
    const ModSettings& s = session_.modData(track_, m);
    set(shapeAt(s, (shapePlace(s) + 1) % kShapePlaces, track_));
}

void ModKnob::dice(Uint32 nowMs) {
    const int m = mod();
    if (m < 0) return;
    set(rolled(session_.modData(track_, m), nowMs * 2654435761u + (++rolls_) * 40503u));
}

void ModKnob::press(Button b, Uint32) {
    if (b == Button::Up) depthBy(kDepthStep);
    else if (b == Button::Down) depthBy(-kDepthStep);
    else if (b == Button::Left) rateBy(-1);
    else if (b == Button::Right) rateBy(1);
}

bool ModKnob::onButton(const ButtonEvent& event, Uint32 nowMs) {
    if (!held()) return false;
    const Button b = event.button;
    if (b == Button::Start || b == Button::Select || b == Button::X || b == Button::L2 || b == Button::R2 || b == Button::Menu) return false;
    if (event.down && b != Button::B) deleting_ = false, deletePart_ = 0.f; // any other press calls a held B off
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        if (b == Button::B) deleting_ = false, deletePart_ = 0.f;
        return true;
    }
    if (isDirection(b)) {
        press(b, nowMs);
        repeating_ = b;
        repeat_.restart(); // (the wait before it repeats is counted from the next frame, as the screens' own)
    } else if (b == Button::A) {
        nextShape();
    } else if (b == Button::Y) {
        dice(nowMs);
    } else if (b == Button::L1 || b == Button::R1) {
        moreBy(b == Button::R1 ? 1 : -1);
    } else if (b == Button::B) {
        deleting_ = mod() >= 0;
        deleteTimed_ = false; // timed from the frame it is first seen held (update)
        deletePart_ = 0.f;
    }
    return true;
}

void ModKnob::update(const Input& input, Uint32 nowMs, float dt) {
    if (!held()) return;
    // B held: the bar fills for a second, then the MOD is deleted (undo brings it back).
    if (!input.held(Button::B)) deleting_ = false;
    if (!deleting_) {
        deletePart_ = 0.f;
    } else {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        if (deletePart_ >= 1.f) {
            deleting_ = false;
            deletePart_ = 0.f;
            if (mod() >= 0) session_.setMod(track_, mod(), ModSettings());
        }
    }
    // A held D-pad: DEPTH at the cursor's pace (1% a time), RATE at a list's (each one seen).
    while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), repeating_ == Button::Up || repeating_ == Button::Down ? Pace::Cursor : Pace::List))
        press(repeating_, nowMs);
    // The stick: up / down turns DEPTH (the first push up on a knob with none makes one); left / right
    // clicks RATE. One way a push, the way it went first.
    const float x = input.stickX(), y = input.stickY();
    if (x == 0.f && y == 0.f) {
        stickWait_ = false;
        stickAxis_ = 0;
        stickSaved_ = 0.f;
        stickWas_ = false;
    } else if (!stickWait_) {
        if (stickAxis_ == 0) stickAxis_ = std::fabs(y) >= std::fabs(x) ? 1 : 2;
        if (stickAxis_ == 1) {
            const float push = -y;
            if (mod() < 0) {
                if (!stickWas_ && push > 0.f) depthBy(kDepthStep); // a new one, at +25%
            } else if (push != 0.f) {
                ModSettings s = session_.modData(track_, mod());
                s.depth = held1(s.depth + push * std::fabs(push) * kDepthPerSecond * dt);
                set(s);
            }
            stickWas_ = true;
        } else if (x != 0.f) {
            stickSaved_ += stickWas_ ? std::fabs(x) * kRatesPerSecond * dt : 1.f;
            stickWas_ = true;
            for (; stickSaved_ >= 1.f; stickSaved_ -= 1.f) rateBy(x > 0.f ? 1 : -1);
        }
    }
    // Stopped: the MOD on this knob runs, as from Play, while X is held (the Session sends only a change).
    unsigned mask = 0;
    for (int m = 0; m < kModsPerTrack; ++m) {
        const ModSettings& s = session_.modData(track_, m);
        if (s.kind != kModOff && s.target == target_) mask |= 1u << m;
    }
    session_.hearMods(track_, mask, owner_);
}

// ---- drawing -----------------------------------------------------------------------------------

void ModKnob::drawFields(SDL_Renderer* r, const Fonts& chosen, const ModPlaces& p, float top, int x, int y, int w, int h, const char* knob) const {
    const Fonts& fonts = chosen.fixed(); // a tight row: the chips stand under it at LARGE
    box(r, x, y, w, h, kPanel, kLine);
    if (deletePart_ > 0.f) fill(r, x + 1, y + 1, static_cast<int>((w - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), h - 2, faded(kStop, 150)); // B held: it fills
    const int m = mod();
    char text[32];
    const auto field = [&](float fx, float fy, const char* name, const char* value, SDL_Color colour) {
        fonts.small.draw(r, fx, top + fy, name, kDim);
        fonts.medium.draw(r, fx, top + fy + kModValueDrop, value, colour);
    };
    if (m >= 0) std::snprintf(text, sizeof text, "MOD %d", m + 1);
    const char* lead = deletePart_ > 0.f ? "DELETING" : (m >= 0 ? text : (full() ? "MODS FULL" : "NO MOD"));
    field(p.lead, p.leadY, knob, lead, m >= 0 || full() ? kText : kDim);
    if (m < 0) {
        fonts.small.draw(r, p.depth, top + p.fieldsY + kModValueDrop + 1.f, full() ? kFullWords : kNoModWords, kDim);
        return;
    }
    const ModSettings& s = session_.modData(track_, m);
    shapeText(s, text, sizeof text);
    field(p.shape, p.fieldsY, "SHAPE", text, kText);
    depthText(session_.modDepthShown(track_, m), text, sizeof text); // (while playing: where its DEPTH lane has it)
    field(p.depth, p.fieldsY, "DEPTH", text, kText);
    field(p.rate, p.fieldsY, "RATE", modRateInfo(s.rate).name, kText);
    moreText(s, text, sizeof text);
    field(p.more, p.fieldsY, moreName(s), text, kText);
    // NOW: what it adds this moment (the engine's, as heard), from the middle out.
    fonts.small.draw(r, static_cast<float>(p.nowX), top + p.nowY, "NOW", kDim);
    const int barY = static_cast<int>(top) + p.nowY + static_cast<int>(kModValueDrop) + 3, half = p.nowW / 2, mid = p.nowX + half;
    fill(r, p.nowX, barY, p.nowW, 8, kLine);
    const float now = held1(session_.engine.modShown(track_, m));
    const int to = mid + static_cast<int>(std::lround(now * static_cast<float>(half)));
    if (to != mid) fill(r, to < mid ? to : mid, barY, to < mid ? mid - to : to - mid, 8, kPurple);
    fill(r, mid, barY - 2, 1, 12, kDim);
}

void ModKnob::drawHints(SDL_Renderer* r, const Fonts& fonts, const ModPlaces& p) const {
    if (mod() >= 0) {
        const HintUnder under[] = {{p.lead, "HOLD B"}, {p.shape, "A"}, {p.depth, "UP / DOWN"}, {p.rate, "LEFT / RIGHT"}, {p.more, "L1 / R1"}};
        hintsUnder(r, fonts, "Y", under, 5);
    } else if (!full()) {
        const HintUnder under[] = {{p.depth, "UP"}};
        hintsUnder(r, fonts, nullptr, under, 1);
    } else {
        hintsUnder(r, fonts, nullptr, nullptr, 0);
    }
}

// ---- the brass mark -----------------------------------------------------------------------------

float modMarkAt(const Session& session, int track, int lane, float shown) {
    if (track < 0 || track >= kNumTracks || lane < 0) return kModNotRunning;
    float added = 0.f;
    bool runs = false;
    for (int m = 0; m < kModsPerTrack; ++m) {
        const ModSettings& mod = session.modData(track, m);
        if (modKind(mod.kind) == kModOff || mod.target != lane) continue; // (at any DEPTH: its lane may be written)
        // As the MOD tab's dot: only while the song plays or X runs it, so a Stop takes it away on
        // the very pass (the engine's last tick would stand with nothing to stir the redraw).
        if (!session.project.playing && !(session.modsHeard(track) & (1u << m))) continue;
        const double at = session.engine.modNow(track, m);
        if (!(at == at)) continue;
        runs = true;
        added += session.engine.modShown(track, m);
    }
    if (!runs) return kModNotRunning;
    const float v = shown + added;
    return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f;
}

int modMarkX(int x, int w, float at) {
    const float part = at > 0.f ? (at < 1.f ? at : 1.f) : 0.f;
    const int centre = x + static_cast<int>(std::round(static_cast<float>(w) * part));
    const int left = centre - kModMarkW / 2;
    return left < x ? x : (left > x + w - kModMarkW ? x + w - kModMarkW : left);
}

void drawModMark(SDL_Renderer* r, int x, int y, int w, int h, float at) {
    if (!(at == at)) return;
    fill(r, modMarkX(x, w, at), y - kModMarkOver, kModMarkW, h + 2 * kModMarkOver, kBrass);
}

} // namespace apotheca
