#include "mod_screen.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "mod_knob.hpp"
#include "mods.hpp"
#include "synth_spec.hpp"

namespace apotheca {

using namespace ui;

namespace {

constexpr Uint32 kRepeatDelayMs = kCursorDelayMs, kRepeatEveryMs = kCursorEveryMs;
constexpr float kMovesPerSecond = 14.f, kClicksPerSecond = 5.f, kTurnPerSecond = 1.8f; // (an amount here is -1..1 or 0..1)
constexpr float kDepthStep = 0.01f, kNotch = 0.05f;

float held(float v, float lo, float hi) { return v > hi ? hi : (v < lo ? lo : (v == v ? v : lo)); }

// To the next whole notch up or down (a value between two goes to the next one, not a notch on).
float notched(float v, float notch, int by) {
    const float n = v / notch;
    return (by > 0 ? std::floor(n + 1.001f) : std::ceil(n - 1.001f)) * notch;
}

std::uint32_t mixSeed(std::uint32_t x) {
    x += 0x9e3779b9u;
    x = (x ^ (x >> 16)) * 0x85ebca6bu;
    x = (x ^ (x >> 13)) * 0xc2b2ae35u;
    return x ^ (x >> 16);
}

void lower(const char* in, char* out, int size) {
    int i = 0;
    for (; in[i] && i + 1 < size; ++i) out[i] = static_cast<char>(in[i] >= 'A' && in[i] <= 'Z' ? in[i] - 'A' + 'a' : in[i]);
    out[i] = 0;
}

// A setting's bar: an amount filled from the left (or from the middle), a choice a piece each, the one it is lit.
void settingBar(SDL_Renderer* r, int x, int y, int w, int h, float part, int choices, int which, bool centre, SDL_Color lit) {
    if (choices > 0) {
        const int gap = choices > 12 ? 2 : 3;
        for (int c = 0; c < choices; ++c) {
            const int from = x + (w + gap) * c / choices, to = x + (w + gap) * (c + 1) / choices - gap;
            fill(r, from, y, to - from, h, c == which ? lit : kLine);
        }
        return;
    }
    fill(r, x, y, w, h, kLine);
    if (centre) {
        const int mid = x + w / 2, to = mid + static_cast<int>(std::lround(held(part, -1.f, 1.f) * static_cast<float>(w / 2)));
        fill(r, to < mid ? to : mid, y, to < mid ? mid - to : to - mid, h, lit);
        fill(r, mid, y - 2, 1, h + 4, kDim);
    } else {
        fill(r, x, y, static_cast<int>(std::lround(static_cast<float>(w) * held(part, 0.f, 1.f))), h, lit);
    }
}

} // namespace

// A target on its line of the list of targets, small: "tone", "cutoff", "echo".
static void itemWord(const TrackData& t, int target, char* out, int size) {
    char label[40];
    if (modTargetIsSend(target)) std::snprintf(label, sizeof label, "%s", target == kModFirstSend ? "ECHO" : "SPACE");
    else ModScreen::targetText(t, target, label, sizeof label);
    if (char* comma = std::strchr(label, ',')) *comma = 0;
    lower(label, out, size);
}

// ---- what things are called ------------------------------------------------------------------

void ModScreen::targetText(const TrackData& track, int target, char* out, int size) {
    const std::size_t n = static_cast<std::size_t>(size);
    if (modTargetIsMacro(target)) {
        const SynthSpec* spec = specFor(track.kind);
        std::snprintf(out, n, "%s", spec ? spec->macroNames[target - kModFirstMacro] : "MACRO");
    } else if (modTargetIsEffect(target)) {
        const int kind = track.effects[modTargetSlot(target)].kind;
        const char* name = kind > 0 && kind < static_cast<int>(kEffectKinds) ? effectInfo(static_cast<EffectKind>(kind)).paramNames[modTargetSetting(target)] : "?";
        std::snprintf(out, n, "%s, FX %d", name, modTargetSlot(target) + 1);
    } else if (modTargetIsSend(target)) {
        std::snprintf(out, n, "%s SEND", target == kModFirstSend ? "ECHO" : "SPACE");
    } else if (modTargetIsMix(target)) {
        std::snprintf(out, n, "%s", target == kModTargetLevel ? "LEVEL" : "PAN");
    } else {
        std::snprintf(out, n, "NOTHING");
    }
}

void ModScreen::pickLineTargets(const TrackData& track, int line, int* out, int* count) {
    *count = 0;
    int first = 0, many = 0;
    if (line == kPickMacros) first = kModFirstMacro, many = 8;
    else if (line >= kPickFx1 && line <= kPickFx3) first = kModFirstEffect + (line - kPickFx1) * 4, many = 4;
    else if (line == kPickSends) first = kModFirstSend, many = 2;
    else if (line == kPickMixer) first = kModTargetLevel, many = 2;
    for (int t = first; t < first + many; ++t)
        if (modTargetFits(track, t)) out[(*count)++] = t;
}

int ModScreen::pickTarget() const {
    int targets[8], count = 0;
    pickLineTargets(project_.tracks[track()], pickLine_, targets, &count);
    return pickItem_ >= 0 && pickItem_ < count ? targets[pickItem_] : kModTargetNone;
}

int ModScreen::settings(int row) const {
    if (row < 0 || row >= kRows) return 0;
    const ModSettings& m = mod(modOf(row));
    if (stepsLine(row)) return m.kind == kModOff ? 0 : kStepsColumns;
    return m.kind == kModOff ? 1 : kModColumns;
}

void ModScreen::goTo(int row, int column) {
    row_ = ((row % kRows) + kRows) % kRows;
    const int most = settings(row_);
    column_ = column < 0 ? 0 : (column > most ? most : column);
    if (column_ > 0) last_[row_] = column_;
    if (stepsLine(row_) && column_ >= kFirstStep && column_ < kLength) stepLast_[row_] = column_;
}

double ModScreen::nowPosition(int m) const {
    if (!project_.playing && !(session_.modsHeard(track()) & (1u << m))) return kModNotRunning;
    return session_.engine.modNow(track(), m);
}

int ModScreen::nowPlace(int m) const {
    const double at = nowPosition(m);
    return at == at ? session_.engine.modPlace(track(), m) : -1;
}

bool ModScreen::amount() const {
    if (column_ < 1) return false;
    if (stepsLine(row_)) return column_ != kLength;
    return column_ == kDepth;
}

// ---- turning -----------------------------------------------------------------------------------

void ModScreen::turnBy(int by) {
    const int m = modOf(row_);
    ModSettings s = mod(m);
    if (s.kind == kModOff) return;
    if (stepsLine(row_)) {
        if (column_ >= kFirstStep && column_ < kLength) {
            const int step = column_ - kFirstStep;
            s.steps[step] = held(notched(s.steps[step], kNotch, by), -1.f, 1.f);
        } else if (column_ == kLength) {
            const int to = modLength(s.length) + by;
            if (to < 1 || to > kModSteps) return;
            s.length = to;
        } else if (column_ == kSlew) {
            s.slew = held(notched(s.slew, kNotch, by), 0.f, 1.f);
        } else if (column_ == kChance) {
            s.chance = held(notched(s.chance, kNotch, by), 0.f, 1.f);
        }
        set(m, s);
        return;
    }
    switch (column_) {
    case kShape: {
        const int to = ModKnob::shapePlace(s) + by;
        if (to < 0 || to >= ModKnob::kShapePlaces) return;
        s = ModKnob::shapeAt(s, to, track());
        break;
    }
    case kDepth: {
        const float hundredths = s.depth * 100.f;
        s.depth = held((by > 0 ? std::floor(hundredths + 1.001f) : std::ceil(hundredths - 1.001f)) / 100.f, -1.f, 1.f);
        break;
    }
    case kRate: {
        const int to = modRatePlace(s.rate) + by;
        if (to < 0 || to >= kModRates) return;
        s.rate = modRateAt(to);
        break;
    }
    case kMore:
        if (modKind(s.kind) == kModStepper) {
            const int to = modLength(s.length) + by;
            if (to < 1 || to > kModSteps) return;
            s.length = to;
        } else {
            const int to = modPhase(s.phase) + by;
            if (to < 0 || to >= kModPhases) return;
            s.phase = to;
        }
        break;
    default: return;
    }
    set(m, s);
}

void ModScreen::turnSmooth(float by) {
    if (by == 0.f || !amount()) return;
    const int m = modOf(row_);
    ModSettings s = mod(m);
    if (s.kind == kModOff) return;
    if (!stepsLine(row_)) s.depth = held(s.depth + by, -1.f, 1.f);
    else if (column_ < kLength) s.steps[column_ - kFirstStep] = held(s.steps[column_ - kFirstStep] + by, -1.f, 1.f);
    else if (column_ == kSlew) s.slew = held(s.slew + by * 0.5f, 0.f, 1.f);
    else if (column_ == kChance) s.chance = held(s.chance + by * 0.5f, 0.f, 1.f);
    set(m, s);
}

void ModScreen::toUsual() {
    const int m = modOf(row_);
    ModSettings s = mod(m);
    if (s.kind == kModOff || column_ < 1) return;
    const ModSettings usual = ModKnob::fresh(s.target);
    if (stepsLine(row_)) {
        if (column_ < kLength) s.steps[column_ - kFirstStep] = 0.f;
        else if (column_ == kLength) s.length = usual.length;
        else if (column_ == kSlew) s.slew = usual.slew;
        else s.chance = usual.chance;
    } else if (column_ == kShape) {
        s.kind = usual.kind, s.shape = usual.shape;
    } else if (column_ == kDepth) {
        // Its usual DEPTH, and its lane in this scene emptied (as B on a macro or an FX setting). A reset,
        // not a turn to record: with WRITE on and the song playing, setMod would write the lane again.
        s.depth = usual.depth;
        const bool writing = session_.writing();
        session_.setWriting(false);
        session_.clearAutomation(track(), project_.scene, modDepthLane(m));
        set(m, s);
        session_.setWriting(writing);
        return;
    } else if (column_ == kRate) {
        s.rate = usual.rate;
    } else if (column_ == kMore) {
        if (modKind(s.kind) == kModStepper) s.length = usual.length;
        else s.phase = usual.phase;
    } else {
        return; // TARGET has no usual: hold B deletes
    }
    set(m, s);
}

void ModScreen::dice(Uint32 nowMs) {
    const int m = modOf(row_);
    const ModSettings& s = mod(m);
    if (s.kind == kModOff) return;
    const std::uint32_t seed = mixSeed(nowMs * 2654435761u + (++rolls_) * 40503u);
    if (stepsLine(row_)) {
        ModSettings stepper = s; // the row, whatever the MOD is: an LFO keeps it for when it is a STEPPER
        stepper.kind = kModStepper;
        ModSettings out = ModKnob::rolled(stepper, seed);
        out.kind = s.kind;
        set(m, out);
    } else {
        set(m, ModKnob::rolled(s, seed));
    }
}

void ModScreen::stepTrack(int by) {
    pickRow_ = -1;
    holding_ = false;
    const int to = project_.track + by;
    if (to < 0 || to >= project_.trackCount()) return;
    project_.track = to;
}

// ---- the list of targets -----------------------------------------------------------------------

void ModScreen::openPick() {
    pickRow_ = row_; // (a MOD's own line: its STEPS have no target, and open nothing)
    pickTrack_ = track();
    const ModSettings& s = mod(modOf(pickRow_));
    const TrackData& t = project_.tracks[track()];
    // On the MOD's target, if it has one; else the first line with something on it.
    pickLine_ = -1;
    for (int line = 0; line < kPickLines && s.kind != kModOff; ++line) {
        int targets[8], count = 0;
        pickLineTargets(t, line, targets, &count);
        for (int i = 0; i < count; ++i)
            if (targets[i] == s.target) pickLine_ = line, pickItem_ = i;
    }
    if (pickLine_ >= 0) return;
    pickItem_ = 0;
    for (int line = 0; line < kPickLines && pickLine_ < 0; ++line) {
        int targets[8], count = 0;
        pickLineTargets(t, line, targets, &count);
        if (count > 0) pickLine_ = line;
    }
    if (pickLine_ < 0) pickLine_ = kPickSends; // (a send always fits)
}

void ModScreen::pickMove(Button b) {
    const TrackData& t = project_.tracks[track()];
    int targets[8], count = 0;
    if (b == Button::Left || b == Button::Right) {
        pickLineTargets(t, pickLine_, targets, &count);
        const int to = pickItem_ + (b == Button::Right ? 1 : -1);
        if (to >= 0 && to < count) pickItem_ = to;
        return;
    }
    // Up / down: the next line with something on it (the ends stop); the place along it kept where it can be.
    for (int line = pickLine_ + (b == Button::Down ? 1 : -1); line >= 0 && line < kPickLines; line += b == Button::Down ? 1 : -1) {
        pickLineTargets(t, line, targets, &count);
        if (count == 0) continue;
        pickLine_ = line;
        if (pickItem_ >= count) pickItem_ = count - 1;
        return;
    }
}

void ModScreen::pickLetGo() {
    const int row = pickRow_;
    pickRow_ = -1;
    if (row < 0 || pickTrack_ != track()) return;
    const int target = pickTarget(), m = modOf(row);
    if (target == kModTargetNone) return;
    ModSettings s = mod(m);
    if (s.kind != kModOff && s.target == target) return; // as it was
    if (s.kind == kModOff) s = ModKnob::fresh(target);
    else s.target = target;
    set(m, s);
}

// ---- buttons -----------------------------------------------------------------------------------

void ModScreen::hear(bool held) {
    const TrackKind kind = project_.current().kind;
    if (kind == TrackKind::Drums) {
        session_.hearLaneHeld(track(), 0, 0.8f, this);
    } else if (held && !project_.playing && (isSynth(kind) || kind == TrackKind::Sampler)) {
        session_.auditionHeld(track(), 60, 0.8f, this); // (heard as long as X is: a MOD that runs is heard moving)
    } else if (isSynth(kind)) {
        session_.audition(track(), 60, 0.8f);
    }
}

void ModScreen::press(Button b) {
    const bool upDown = b == Button::Up || b == Button::Down;
    if (picking()) {
        pickMove(b);
        return;
    }
    const bool steps = stepsLine(row_), onStep = steps && column_ >= kFirstStep && column_ < kLength;
    if (holding_ && column_ > 0) {
        if (upDown) turnBy(b == Button::Up ? 1 : -1);
        else if (onStep) { // along the row in one grip (the ends stop)
            const int to = column_ + (b == Button::Right ? 1 : -1);
            if (to >= kFirstStep && to < kLength) goTo(row_, to);
        }
        return;
    }
    if (holding_) return; // (A held on the list's line: the list of targets, above)
    if (column_ == 0) {
        if (b == Button::Up) goTo(row_ - 1, 0);
        if (b == Button::Down) goTo(row_ + 1, 0);
        if (b == Button::Right && settings(row_) > 0) goTo(row_, last_[row_]);
        return;
    }
    if (onStep) {
        if (b == Button::Left) goTo(row_, column_ == kFirstStep ? 0 : column_ - 1);
        if (b == Button::Right && column_ + 1 < kLength) goTo(row_, column_ + 1);
        if (b == Button::Down) goTo(row_, kLength);
        return;
    }
    if (b == Button::Left) column_ = 0;
    if (b == Button::Up && steps && column_ == kLength) goTo(row_, stepLast_[row_]);
    else if (b == Button::Up && column_ > 1) goTo(row_, column_ - 1);
    if (b == Button::Down && column_ < settings(row_)) goTo(row_, column_ + 1);
}

void ModScreen::onButton(const ButtonEvent& event, const Input& input) {
    (void)input;
    const Button b = event.button;
    if (event.down && b != Button::B) deleting_ = false, deletePart_ = 0.f; // any other press calls a held B off
    if (b == Button::X) {
        xHeld_ = event.down;
        if (event.down) hear(true);
        return;
    }
    if (b == Button::A) {
        if (!event.down && picking()) pickLetGo();
        holding_ = event.down;
        if (event.down && !stepsLine(row_) && (column_ == 0 || column_ == kTarget)) openPick();
        saved_ = 0.f;
        stickWas_ = false;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (b == Button::L1 || b == Button::R1) {
        stepTrack(b == Button::R1 ? 1 : -1);
    } else if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::B && holding_) {
        if (picking()) pickRow_ = -1, holding_ = false; // as it was: A does nothing more until it is pressed again
    } else if (b == Button::B) {
        if (column_ == 0) {
            // Held, it deletes the MOD (update(), when the bar is full); a tap does nothing.
            deleting_ = mod(modOf(row_)).kind != kModOff;
            deleteTimed_ = false;
            deletePart_ = 0.f;
            deleteRow_ = row_;
        } else {
            toUsual();
        }
    } else if (b == Button::Y && !holding_) {
        dice(lastMs_);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void ModScreen::update(const Input& input, Uint32 nowMs) {
    if (!input.held(Button::A)) holding_ = false;
    if (picking() && (!holding_ || pickTrack_ != track())) pickRow_ = -1, holding_ = false; // A let go unseen, or another track: nothing goes in
    if (!input.held(Button::X)) xHeld_ = false;
    // B held on a MOD's line: the bar fills for a second, then the MOD is deleted (undo brings it back).
    if (!input.held(Button::B) || holding_ || column_ != 0 || row_ != deleteRow_) deleting_ = false;
    if (!deleting_) {
        deletePart_ = 0.f;
    } else {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        if (deletePart_ >= 1.f) {
            deleting_ = false;
            deletePart_ = 0.f;
            set(modOf(row_), ModSettings());
        }
    }
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= gStickSpeed;
    lastMs_ = nowMs;
    // The MOD may have gone OFF under the cursor (deleted, undone, its effect taken away): the line stays.
    if (column_ > settings(row_)) column_ = settings(row_);
    // X held on a MOD's line while stopped: that MOD runs (the Session sends only a change; App lets go).
    if (xHeld_ && !project_.playing) {
        const int m = modOf(row_);
        session_.hearMods(track(), mod(m).kind != kModOff ? 1u << m : 0u, this);
    }

    const float x = input.stickX(), y = input.stickY();
    if (holding_ && !picking() && amount()) {
        turnSmooth(-y * std::fabs(y) * kTurnPerSecond * dt); // squared, so a small push is fine
    } else {
        const bool sideways = std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * (holding_ ? kClicksPerSecond : kMovesPerSecond) * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f)
                press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
        }
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        const bool click = holding_ && !amount() && (repeating_ == Button::Up || repeating_ == Button::Down);
        repeatAt_ = nowMs + (picking() || click ? kListEveryMs : kRepeatEveryMs);
        press(repeating_);
    }
}

// ---- drawing -----------------------------------------------------------------------------------

ModScreen::Dot ModScreen::drawPicture(SDL_Renderer* r, int x, int y, int w, int h, const ModSettings& m, int track, int mod, double position, int place, bool cursor, bool asWritten) {
    Dot dot;
    fill(r, x, y + h / 2, w, 1, kLine); // the middle: the knob where it is
    if (m.kind == kModOff) return dot;
    const float flip = m.depth < 0.f && !asWritten ? -1.f : 1.f;
    const int half = h / 2 - 2;
    const auto yOf = [&](double v) { return y + h / 2 - static_cast<int>(std::lround(held(static_cast<float>(v) * flip, -1.f, 1.f) * static_cast<float>(half))); };
    const SDL_Color line = cursor ? kText : kDim;
    if (modKind(m.kind) == kModStepper) {
        const int length = modLength(m.length);
        for (int i = 0; i < length; ++i) {
            const int from = x + w * i / length, to = x + w * (i + 1) / length;
            const int vy = yOf(m.steps[i]), mid = y + h / 2;
            fill(r, from + 1, vy < mid ? vy : mid, to - from - 2 > 1 ? to - from - 2 : 1, vy < mid ? mid - vy : vy - mid + 1, i == place ? kPurple : line);
        }
        return dot;
    }
    const bool now = position == position;
    const double cycles = lfoCycles(m, now ? position : 0.0), first = std::floor(cycles);
    int before = -1;
    for (int px = 0; px < w; ++px) {
        const double at = lfoPositionAt(m, first + static_cast<double>(px) / static_cast<double>(w));
        const int vy = yOf(lfoValue(m, at, track, mod));
        const int from = before < 0 ? vy : (before < vy ? before : vy), to = before < 0 ? vy : (before > vy ? before : vy);
        fill(r, x + px, from - 1, 1, to - from + 2, line); // two pixels thick, joined where it jumps
        before = vy;
    }
    if (now) {
        dot.drawn = true;
        dot.x = x + static_cast<int>((cycles - first) * static_cast<double>(w));
        dot.y = yOf(lfoValue(m, position, track, mod));
        fill(r, dot.x - 3, dot.y - 3, 7, 7, kPurple);
    }
    return dot;
}

void ModScreen::drawRow(SDL_Renderer* r, const Fonts& fonts, int y, const char* name, const char* value, float part, int choices, int which, bool centre, bool cursor) const {
    const SDL_Color accent = holding_ ? kBrass : kTeal;
    box(r, kPaneX, y, kPaneW, kRowH, kPanel, kLine);
    fonts.small.draw(r, kPaneX + 12.f, y + 8.f, name, kDim);
    const Font& font = fonts.fit(value, static_cast<float>(kValueRoom));
    font.draw(r, static_cast<float>(kValueX), y + kRowH - 10.f - font.capHeight(), value, cursor ? accent : kText);
    if (part > -2.f) settingBar(r, kBarX, y + kRowH / 2 - kBarH / 2, kBarW, kBarH, part, choices, which, centre, cursor ? accent : kDim);
    if (cursor) outline(r, kPaneX - 1, y - 1, kPaneW + 2, kRowH + 2, accent, 2);
}

void ModScreen::drawList(SDL_Renderer* r, const Fonts& fonts) const {
    char name[24], text[48];
    trackName(project_, track(), name, sizeof name);
    const TrackData& t = project_.tracks[track()];
    for (int m = 0; m < kModsPerTrack; ++m) {
        const float hy = static_cast<float>(lineY(2 * m) - 19 + 3);
        std::snprintf(text, sizeof text, "MOD %d", m + 1);
        const Fonts& tight = fonts.fixed();
        const bool both = m == 0 && tight.small.width(text) + tight.small.width(name) + 10.f <= kListW - 2.f;
        const float wide = fonts.small.width(text) + (both ? fonts.small.width(name) + 10.f : 0.f);
        const Font& font = wide <= kListW - 2.f ? fonts.small : tight.small;
        font.draw(r, kListX + 1.f, hy, text, kText);
        if (both) font.drawRight(r, kListX + kListW - 1.f, hy, name, kDim);
    }
    for (int row = 0; row < kRows; ++row) {
        const int y = lineY(row), m = modOf(row);
        const ModSettings& s = mod(m);
        const bool here = row == row_, cursor = here && column_ == 0, off = s.kind == kModOff;
        box(r, kListX, y, kListW, kLineH, here ? kPanel2 : kPanel, here && !cursor ? kDim : kLine);
        if (here && deletePart_ > 0.f) fill(r, kListX + 1, y + 1, static_cast<int>((kListW - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), kLineH - 2, faded(kStop, 150));
        const float x = kListX + 10.f;
        const float room = static_cast<float>(kSmallPicX - 6) - x;
        if (!stepsLine(row)) {
            if (off) {
                fonts.medium.draw(r, x, y + 12.f, "OFF", kLine);
                fonts.within(&Fonts::small, "NOTHING MOVES", room).draw(r, x, y + 40.f, "NOTHING MOVES", kDim);
            } else {
                ModKnob::shapeText(s, text, sizeof text);
                fonts.within(&Fonts::medium, text, room).draw(r, x, y + 12.f, text, kText);
                targetText(t, s.target, text, sizeof text);
                fonts.within(&Fonts::small, text, room).draw(r, x, y + 40.f, text, kText);
                char depth[16];
                ModKnob::depthText(session_.modDepthShown(track(), m), depth, sizeof depth); // (while playing: its DEPTH lane's)
                std::snprintf(text, sizeof text, "%s  %s", depth, modRateInfo(s.rate).name);
                fonts.within(&Fonts::small, text, room).draw(r, x, y + 62.f, text, kDim);
            }
            if (!off) drawPicture(r, kSmallPicX, y + (kLineH - kSmallPicH) / 2, kSmallPicW, kSmallPicH, asNow(m), track(), m, nowPosition(m), nowPlace(m), here);
        } else {
            fonts.small.draw(r, x, y + 10.f, "STEPS", here ? kText : kDim);
            if (off || modKind(s.kind) != kModStepper) {
                fonts.within(&Fonts::small, off ? "NO MOD" : "AN LFO: NOT PLAYED", kListW - 22.f).draw(r, x, y + 34.f, off ? "NO MOD" : "AN LFO: NOT PLAYED", kDim);
            }
            if (!off) {
                std::snprintf(text, sizeof text, "%d LONG", modLength(s.length));
                fonts.small.drawRight(r, kListX + kListW - 10.f, y + 10.f, text, kDim);
                if (modKind(s.kind) == kModStepper) {
                    const int px = kListX + 10, pw = kListW - 20, py = y + 30, ph = kLineH - 38;
                    drawPicture(r, px, py, pw, ph, s, track(), m, kModNotRunning, nowPlace(m), here, true); // as written: as the pane beside it
                }
            }
        }
        if (cursor) outline(r, kListX - 1, y - 1, kListW + 2, kLineH + 2, holding_ ? kBrass : kTeal, 2);
    }
    fill(r, kListX + kListW, lineY(row_) + kLineH / 2 - 1, kPaneX - (kListX + kListW), 2, column_ == 0 ? (holding_ ? kBrass : kTeal) : kDim);
}

void ModScreen::drawModPane(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, track(), name, sizeof name);
    const int m = modOf(row_);
    const ModSettings& s = mod(m);
    const TrackData& t = project_.tracks[track()];
    const bool off = s.kind == kModOff;
    box(r, kPaneX, kPaneY, kPaneW, kHeadH, kPanel, kLine);
    const float room = static_cast<float>(kPicX - 12 - (kPaneX + 12));
    if (deletePart_ > 0.f) {
        fill(r, kPaneX + 1, kPaneY + kHeadH - 6, static_cast<int>((kPaneW - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), 5, faded(kStop, 150));
        fonts.within(&Fonts::small, kDeletingWords, kPaneW - 24.f).draw(r, kPaneX + 12.f, kPaneY + 10.f, kDeletingWords, kText);
    } else {
        std::snprintf(text, sizeof text, "%s, MOD %d", name, m + 1);
        fonts.within(&Fonts::small, text, room).draw(r, kPaneX + 12.f, kPaneY + 10.f, text, kDim);
    }
    if (off) std::snprintf(text, sizeof text, "OFF");
    else ModKnob::shapeText(s, text, sizeof text);
    fonts.within(&Fonts::large, text, room).draw(r, kPaneX + 12.f, kPaneY + 30.f, text, off ? kDim : kText);
    if (!off) {
        char target[40];
        targetText(t, s.target, target, sizeof target);
        std::snprintf(text, sizeof text, "moves %s", target);
        fonts.within(&Fonts::small, text, room).draw(r, kPaneX + 12.f, kPaneY + 66.f, text, kDim);
    }
    if (!off) drawPicture(r, kPicX, kPicY, kPicW, kPicH, asNow(m), track(), m, nowPosition(m), nowPlace(m), true);
    if (picking()) {
        outline(r, kPaneX - 1, kPaneY - 1, kPaneW + 2, kHeadH + 2, kBrass, 2);
        drawPick(r, fonts);
        return;
    }
    targetText(t, s.target, text, sizeof text);
    drawRow(r, fonts, rowY(0), "TARGET", off ? "NOTHING" : text, -3.f, 0, 0, false, column_ == kTarget);
    if (off) {
        const int y = rowY(1);
        box(r, kPaneX, y, kPaneW, kRowH * 2 + kSetGap, kPanel, kLine);
        fonts.small.draw(r, kPaneX + 12.f, y + 18.f, "Nothing moves here.", kText);
        fonts.within(&Fonts::small, "Hold A on TARGET to aim it: it starts as an LFO.", kPaneW - 24.f).draw(r, kPaneX + 12.f, y + 42.f, "Hold A on TARGET to aim it: it starts as an LFO.", kDim);
        return;
    }
    ModKnob::shapeText(s, text, sizeof text);
    drawRow(r, fonts, rowY(1), "SHAPE", text, 0.f, ModKnob::kShapePlaces, ModKnob::shapePlace(s), false, column_ == kShape);
    // DEPTH: while the song plays, where its DEPTH lane's automation has it (batch 7); written in the
    // scene worked on, the purple line and the words, as an automated setting on FX.
    const float depth = session_.modDepthShown(track(), m);
    ModKnob::depthText(depth, text, sizeof text);
    drawRow(r, fonts, rowY(2), "DEPTH", text, depth, 0, 0, true, column_ == kDepth);
    if (session_.hasAutomation(track(), project_.scene, modDepthLane(m))) {
        fill(r, kPaneX + 1, rowY(2) + kRowH - 3, kPaneW - 2, 2, kPurple);
        fonts.small.drawRight(r, static_cast<float>(kBarX + kBarW), rowY(2) + 8.f, kMovesWords, kPurple);
    }
    drawRow(r, fonts, rowY(3), "RATE", modRateInfo(s.rate).name, 0.f, kModRates, modRatePlace(s.rate), false, column_ == kRate);
    ModKnob::moreText(s, text, sizeof text);
    const bool stepper = modKind(s.kind) == kModStepper;
    drawRow(r, fonts, rowY(4), ModKnob::moreName(s), text, 0.f, stepper ? kModSteps : kModPhases, stepper ? modLength(s.length) - 1 : modPhase(s.phase), false, column_ == kMore);
}

int ModScreen::otherTarget() const {
    return mod(1 - modOf(picking() ? pickRow_ : row_)).target; // an OFF MOD is blank: kModTargetNone
}

void ModScreen::drawPick(SDL_Renderer* r, const Fonts& fonts) const {
    const TrackData& t = project_.tracks[track()];
    box(r, kPaneX, kPickY, kPaneW, kPickLineH * kPickLines, kPanel, kLine);
    static const char* const names[kPickLines] = {"MACROS", "FX 1", "FX 2", "FX 3", "SENDS", "MIXER"};
    const float itemsRight = static_cast<float>(kPaneX + kPaneW - 12);
    for (int line = 0; line < kPickLines; ++line) {
        const int y = pickLineY(line);
        const bool cursor = line == pickLine_;
        if (line > 0) fill(r, kPaneX + 8, y, kPaneW - 16, 1, kLine);
        if (cursor) {
            fill(r, kPaneX + 2, y + 1, kPaneW - 4, kPickLineH - 1, kPanel2);
            outline(r, kPaneX + 2, y + 1, kPaneW - 4, kPickLineH - 1, kBrass, 2);
        }
        int targets[8], count = 0;
        pickLineTargets(t, line, targets, &count);
        const int taken = otherTarget(); // the other MOD's target: marked in purple (owner, 2026-11-15)
        fonts.medium.draw(r, static_cast<float>(kPickNameX), y + 10.f, names[line], cursor ? kBrass : (count ? kText : kDim));
        if (line >= kPickFx1 && line <= kPickFx3) {
            const int kind = t.effects[line - kPickFx1].kind;
            const char* what = kind > 0 && kind < static_cast<int>(kEffectKinds) ? effectInfo(static_cast<EffectKind>(kind)).name : "EMPTY";
            fonts.within(&Fonts::small, what, static_cast<float>(kPickItemsX - kPickNameX - 6)).draw(r, static_cast<float>(kPickNameX), y + 36.f, what, kDim);
        }
        // What may be aimed at on this line, small, ", " between, rows as they fit; the one under the cursor lit.
        char word[32];
        const char* none = line == kPickMacros ? (specFor(t.kind) ? "none: each is a choice" : "none on this track") : (line == kPickSends ? "none" : "nothing to move");
        if (count == 0) {
            fonts.within(&Fonts::small, none, itemsRight - kPickItemsX).draw(r, static_cast<float>(kPickItemsX), y + 12.f, none, kLine);
            continue;
        }
        // At the size chosen if every word fits the line's rows, else at LARGE (the rows are worked out at that size).
        const auto rowsAt = [&](const Font& font) {
            float x = static_cast<float>(kPickItemsX);
            int rows = 1;
            for (int i = 0; i < count; ++i) {
                itemWord(t, targets[i], word, sizeof word);
                const float w = font.width(word) + (i + 1 < count ? font.width(", ") : 0.f);
                if (x + w > itemsRight && x > kPickItemsX) x = static_cast<float>(kPickItemsX), ++rows;
                x += w;
            }
            return rows;
        };
        const Font& font = rowsAt(fonts.small) <= 3 ? fonts.small : fonts.fixed().small;
        float x = static_cast<float>(kPickItemsX), ty = y + 10.f;
        for (int i = 0; i < count; ++i) {
            itemWord(t, targets[i], word, sizeof word);
            const float w = font.width(word) + (i + 1 < count ? font.width(", ") : 0.f);
            if (x + w > itemsRight && x > kPickItemsX) x = static_cast<float>(kPickItemsX), ty += font.capHeight() + 7.f;
            const bool lit = cursor && i == pickItem_;
            const bool other = targets[i] == taken;
            if (lit) fill(r, static_cast<int>(x) - 3, static_cast<int>(ty) - 3, static_cast<int>(font.width(word)) + 6, static_cast<int>(font.capHeight()) + 7, kBrass);
            font.draw(r, x, ty, word, lit ? kBg : (other ? kPurple : kText));
            if (other) fill(r, static_cast<int>(x), static_cast<int>(ty + font.capHeight()) + (lit ? 5 : 3), static_cast<int>(font.width(word)), 2, kPurple);
            if (i + 1 < count) font.draw(r, x + font.width(word), ty, ", ", kDim);
            x += w;
        }
    }
}

void ModScreen::drawStepsPane(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, track(), name, sizeof name);
    const int m = modOf(row_);
    const ModSettings& s = mod(m);
    const bool off = s.kind == kModOff, stepper = modKind(s.kind) == kModStepper;
    const SDL_Color accent = holding_ ? kBrass : kTeal;
    box(r, kPaneX, kPaneY, kPaneW, kHeadH, kPanel, kLine);
    if (deletePart_ > 0.f) {
        fill(r, kPaneX + 1, kPaneY + kHeadH - 6, static_cast<int>((kPaneW - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), 5, faded(kStop, 150));
        fonts.within(&Fonts::small, kDeletingWords, kPaneW - 24.f).draw(r, kPaneX + 12.f, kPaneY + 10.f, kDeletingWords, kText);
    } else {
        std::snprintf(text, sizeof text, "%s, MOD %d STEPS", name, m + 1);
        fonts.within(&Fonts::small, text, kPaneW - 24.f).draw(r, kPaneX + 12.f, kPaneY + 10.f, text, kDim);
    }
    const bool onStep = column_ >= kFirstStep && column_ < kLength;
    if (off) std::snprintf(text, sizeof text, "NO MOD");
    else if (onStep) {
        char value[16];
        ModKnob::depthText(s.steps[column_ - kFirstStep], value, sizeof value);
        std::snprintf(text, sizeof text, "STEP %d  %s", column_, value);
    } else std::snprintf(text, sizeof text, "%d STEPS", modLength(s.length));
    fonts.within(&Fonts::large, text, kPaneW - 24.f).draw(r, kPaneX + 12.f, kPaneY + 30.f, text, off ? kDim : kText);
    const char* says = off ? "Aim MOD first: its own line, TARGET." : (stepper ? "one value for each RATE, round and round" : "an LFO: played when its SHAPE is STEPPER");
    fonts.within(&Fonts::small, says, kPaneW - 24.f).draw(r, kPaneX + 12.f, kPaneY + 66.f, says, stepper ? kDim : kBrass);
    if (off) return;

    // The row: a bar a step from the middle, the one playing lit, past LENGTH faint, the cursor's outlined.
    box(r, kPaneX, kSetY, kPaneW, kStepsBoxH, kPanel, kLine);
    fill(r, kStepX, kStepsMiddle, kStepW * kModSteps - 4, 1, kLine);
    const int playing = stepper ? nowPlace(m) : -1;
    for (int i = 0; i < kModSteps; ++i) {
        const int x = kStepX + i * kStepW;
        const int to = kStepsMiddle - static_cast<int>(std::lround(held(s.steps[i], -1.f, 1.f) * kStepsHalf));
        const bool in = i < modLength(s.length), cursor = column_ == kFirstStep + i;
        const SDL_Color c = i == playing ? kPurple : (cursor ? accent : (in ? kText : kLine));
        fill(r, x, to < kStepsMiddle ? to : kStepsMiddle, kStepBarW, to < kStepsMiddle ? kStepsMiddle - to : to - kStepsMiddle + 1, c);
        if (i % 4 == 0) {
            std::snprintf(text, sizeof text, "%d", i + 1);
            fonts.fixed().small.draw(r, static_cast<float>(x), static_cast<float>(kStepsMiddle + kStepsHalf + 8), text, in ? kDim : kLine);
        }
        if (cursor) outline(r, x - 2, kStepsTop - 4, kStepBarW + 4, 2 * kStepsHalf + 9, accent, 2);
    }
    std::snprintf(text, sizeof text, "%d", modLength(s.length));
    drawRow(r, fonts, stepsRowY(0), "LENGTH", text, 0.f, kModSteps, modLength(s.length) - 1, false, column_ == kLength);
    std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::lround(s.slew * 100.f)));
    drawRow(r, fonts, stepsRowY(1), "SLEW", text, s.slew, 0, 0, false, column_ == kSlew);
    std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::lround(s.chance * 100.f)));
    drawRow(r, fonts, stepsRowY(2), "CHANCE", text, s.chance, 0, 0, false, column_ == kChance);
}

void ModScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char name[24];
    trackName(project_, track(), name, sizeof name);
    topBar(r, fonts, View::Mods, name, project_);
    drawList(r, fonts);
    if (stepsLine(row_)) drawStepsPane(r, fonts);
    else drawModPane(r, fonts);

    Hint list[6];
    hints(r, fonts, list, hintsNow(list));
}

int ModScreen::hintsNow(Hint* list) const {
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    const bool on = mod(modOf(row_)).kind != kModOff, steps = stepsLine(row_);
    const bool onStep = steps && column_ >= kFirstStep && column_ < kLength;
    if (picking()) {
        hint("UP / DOWN", "line");
        hint("LEFT / RIGHT", "target");
        hint("B", "as it was");
    } else if (holding_ && column_ > 0) {
        hint("UP / DOWN", onStep ? "value" : "turn it");
        if (onStep) hint("LEFT / RIGHT", "step");
    } else if (holding_ && column_ == 0) {
        hint("L1 / R1", "track"); // (A held on a STEPS line: the directions, B and Y wait for A)
    } else if (column_ == 0) {
        if (settings(row_) > 0) hint("RIGHT", steps ? "steps" : "settings");
        if (!steps) hint("HOLD A", "target");
        if (on) hint("HOLD B", "delete");
        if (on) hint("Y", "dice");
        hint("L1 / R1", "track");
        if (!on) hint("START", project_.playing ? "stop" : "play");
    } else {
        hint("LEFT", "list");
        hint("HOLD A", !steps && column_ == kTarget ? "target" : "turn");
        if (steps || column_ != kTarget) hint("B", onStep ? "zero" : "default");
        if (on) hint("Y", "dice");
        hint("L1 / R1", "track");
    }
    return count;
}

} // namespace apotheca
