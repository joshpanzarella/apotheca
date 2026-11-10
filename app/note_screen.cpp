#include "note_screen.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {

constexpr int kGridLeft = 116, kGridTop = 54, kStepW = 32, kRowH = 20;
constexpr int kCellW = kStepW - 2, kCellH = kRowH - 2;
constexpr int kNameX = 8, kNameW = 104;
constexpr float kNewLevel = 0.8f, kMinLevel = 0.05f;
constexpr Uint32 kRepeatDelayMs = 170, kRepeatEveryMs = 30;
constexpr float kStickCellsPerSecond = 36.f, kFillPerSecond = 3.f, kLengthStepsPerSecond = 24.f;

bool isDirection(Button b) { return b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right; }
bool isBlackKey(int pitch) {
    const int k = ((pitch % 12) + 12) % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}
float nextTenth(float now, float sign) {
    const float tenths = now * 10.f;
    return (sign > 0.f ? std::floor(tenths + 1.001f) : std::ceil(tenths - 1.001f)) / 10.f;
}

} // namespace

const NoteEvent* NoteScreen::noteHere() const {
    const int slot = pat().at(pitch_, step_);
    return slot < 0 ? nullptr : &pat().notes[slot];
}

int NoteScreen::noteCount() const {
    int count = 0;
    for (const NoteEvent& n : pat().notes) count += n.velocity > 0.f ? 1 : 0;
    return count;
}

void NoteScreen::moveCursorTo(int pitch, int step) {
    pitch_ = pitch < kLowest ? kLowest : (pitch > kHighest ? kHighest : pitch);
    step_ = ((step % kSteps) + kSteps) % kSteps;
    // The keys scroll to keep the cursor in view.
    if (pitch_ > top_) top_ = pitch_;
    if (pitch_ < top_ - (kVisibleRows - 1)) top_ = pitch_ + (kVisibleRows - 1);
}

void NoteScreen::moveCursor(Button direction) {
    if (direction == Button::Up) moveCursorTo(pitch_ + 1, step_);
    if (direction == Button::Down) moveCursorTo(pitch_ - 1, step_);
    if (direction == Button::Left) moveCursorTo(pitch_, step_ - 1);
    if (direction == Button::Right) moveCursorTo(pitch_, step_ + 1);
    if (painting_) paintHere();
}

void NoteScreen::sendNote(int slot) {
    const NoteEvent& n = pat().notes[slot];
    Command c;
    c.type = Command::SetNote;
    c.track = project_.track;
    c.scene = project_.scene;
    c.a = slot;
    c.b = n.step;
    c.c = n.pitch;
    c.d = n.length;
    c.value = n.velocity;
    session_.engine.send(c);
}

// A new one-step note under the cursor. False if one is already there, or
// the pattern is full.
bool NoteScreen::placeNote(float velocity) {
    if (pat().at(pitch_, step_) >= 0) return false;
    const int slot = pat().freeSlot();
    if (slot < 0) return false;
    NoteEvent& n = pat().notes[slot];
    n.step = step_;
    n.pitch = pitch_;
    n.length = 1;
    n.velocity = velocity;
    sendNote(slot);
    if (!project_.playing) session_.audition(project_.track, pitch_, velocity);
    return true;
}

void NoteScreen::setLevel(float level) {
    const int slot = pat().at(pitch_, step_);
    if (slot < 0) return;
    level = level < kMinLevel ? kMinLevel : (level > 1.f ? 1.f : level);
    if (level == pat().notes[slot].velocity) return;
    pat().notes[slot].velocity = level;
    sendNote(slot);
}

void NoteScreen::setLength(int length) {
    const int slot = pat().at(pitch_, step_);
    if (slot < 0) return;
    NoteEvent& n = pat().notes[slot];
    // It may run to the end of the bar, or up to the next note on this key.
    int room = kSteps - n.step;
    for (const NoteEvent& other : pat().notes) {
        if (&other != &n && other.velocity > 0.f && other.pitch == n.pitch && other.step > n.step && other.step - n.step < room) {
            room = other.step - n.step;
        }
    }
    length = length < 1 ? 1 : (length > room ? room : length);
    // Never shrink it out from under the cursor.
    if (length < step_ - n.step + 1) length = step_ - n.step + 1;
    if (length == n.length) return;
    n.length = length;
    sendNote(slot);
}

void NoteScreen::paintHere() {
    if (placeNote(brush_)) return;
    setLevel(brush_); // a note already here takes the brush's level
}

void NoteScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;

    if (b == Button::A) {
        if (event.down) placeNote(kNewLevel);
        shaping_ = event.down;
        axis_ = Axis::None;
        lengthSaved_ = 0.f;
        return;
    }
    if (b == Button::R1) {
        painting_ = event.down && !shaping_;
        if (painting_) {
            const NoteEvent* here = noteHere();
            brush_ = here ? here->velocity : kNewLevel;
            paintHere();
        }
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }

    if (isDirection(b)) {
        if (input.held(Button::A)) {
            const NoteEvent* here = noteHere();
            if (here && b == Button::Up) setLevel(nextTenth(here->velocity, 1.f)), turningLength_ = false;
            if (here && b == Button::Down) setLevel(nextTenth(here->velocity, -1.f)), turningLength_ = false;
            if (here && b == Button::Right) setLength(here->length + 1), turningLength_ = true;
            if (here && b == Button::Left) setLength(here->length - 1), turningLength_ = true;
        } else {
            moveCursor(b);
        }
        repeating_ = b;
        repeatAt_ = 0;
        return;
    }
    if (b == Button::B && !input.held(Button::A)) {
        const int slot = pat().at(pitch_, step_);
        if (slot >= 0) {
            pat().notes[slot] = NoteEvent();
            sendNote(slot);
        }
    } else if (b == Button::X) {
        const NoteEvent* here = noteHere();
        session_.audition(project_.track, pitch_, here ? here->velocity : kNewLevel);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void NoteScreen::update(const Input& input, Uint32 nowMs) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    lastMs_ = nowMs;
    const float x = input.stickX(), y = input.stickY();

    if (shaping_) {
        // One job per push, as on the drum grid.
        if (x == 0.f && y == 0.f) axis_ = Axis::None;
        else if (axis_ == Axis::None) axis_ = std::fabs(y) >= std::fabs(x) ? Axis::UpDown : Axis::Sideways;
        const NoteEvent* here = noteHere();
        if (here && axis_ == Axis::UpDown && y != 0.f) {
            setLevel(here->velocity - y * std::fabs(y) * kFillPerSecond * dt);
            turningLength_ = false;
        } else if (here && axis_ == Axis::Sideways && x != 0.f) {
            lengthSaved_ += x * std::fabs(x) * kLengthStepsPerSecond * dt;
            turningLength_ = true;
            while (lengthSaved_ >= 1.f) { lengthSaved_ -= 1.f; setLength(noteHere()->length + 1); }
            while (lengthSaved_ <= -1.f) { lengthSaved_ += 1.f; setLength(noteHere()->length - 1); }
        }
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else {
        axis_ = Axis::None;
        if (x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(x) * kStickCellsPerSecond * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) moveCursor(x > 0.f ? Button::Right : Button::Left);
        }
        if (y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? std::fabs(y) * kStickCellsPerSecond * 0.5f * dt : 1.f;
            stickWasY_ = true;
            for (; stickY_ >= 1.f; stickY_ -= 1.f) moveCursor(y > 0.f ? Button::Down : Button::Up);
        }
    }

    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        if (input.held(Button::A)) {
            const NoteEvent* here = noteHere();
            if (here && repeating_ == Button::Up) setLevel(nextTenth(here->velocity, 1.f));
            if (here && repeating_ == Button::Down) setLevel(nextTenth(here->velocity, -1.f));
            if (here && repeating_ == Button::Right) setLength(here->length + 1);
            if (here && repeating_ == Button::Left) setLength(here->length - 1);
        } else {
            moveCursor(repeating_);
        }
    }
}

void NoteScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[48], name[24];
    trackName(project_, project_.track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / SCENE %d", name, project_.scene + 1);
    topBar(r, fonts, View::Pattern, text, project_);
    const bool held = shaping_ || painting_;
    const SDL_Color cursorColor = held ? kBrass : kTeal;

    for (int s = 0; s < kSteps; ++s) {
        std::snprintf(text, sizeof text, "%d", s + 1);
        fonts.small.drawCentered(r, kGridLeft + s * kStepW + kCellW / 2.f, 36.f, text, s % 4 == 0 ? kText : kDim);
    }

    // ---- the keys and the empty grid ---------------------------------------------
    for (int row = 0; row < kVisibleRows; ++row) {
        const int pitch = top_ - row;
        const int y = kGridTop + row * kRowH;
        const bool black = isBlackKey(pitch);
        box(r, kNameX, y, kNameW, kCellH, black ? kBg : kPanel2, kLine);
        noteName(pitch, text, sizeof text);
        // Every C is named; so is the key the cursor is on.
        if (pitch % 12 == 0 || pitch == pitch_) {
            fonts.small.draw(r, kNameX + 7.f, y + (kCellH - fonts.small.capHeight()) / 2.f, text, pitch == pitch_ ? kTeal : kDim);
        }
        for (int s = 0; s < kSteps; ++s) {
            const SDL_Color inside = black ? kBg : (s % 4 == 0 ? kPanel2 : kPanel);
            box(r, kGridLeft + s * kStepW, y, kCellW, kCellH, inside, kLine);
        }
    }

    // ---- notes --------------------------------------------------------------------------
    // A block as long as the note lasts, filled as far as it is loud.
    const NoteEvent* under = noteHere();
    for (const NoteEvent& n : pat().notes) {
        if (n.velocity <= 0.f || n.pitch > top_ || n.pitch < top_ - (kVisibleRows - 1)) continue;
        const int y = kGridTop + (top_ - n.pitch) * kRowH;
        const int x = kGridLeft + n.step * kStepW + 2;
        const int w = n.length * kStepW - 6;
        const int full = kCellH - 4;
        int h = static_cast<int>(std::round(n.velocity * full));
        if (h < 2) h = 2;
        const bool live = held && &n == under;
        outline(r, x, y + 2, w, full, live ? kTeal : kDim);
        fill(r, x, y + 2 + full - h, w, h, live ? kTeal : kText);
    }
    {
        const int y = kGridTop + (top_ - pitch_) * kRowH;
        outline(r, kGridLeft + step_ * kStepW - 1, y - 1, kCellW + 2, kCellH + 2, cursorColor, 2);
    }
    const int playStep = session_.engine.playScene() == project_.scene ? session_.engine.playStep() : -1;
    if (playStep >= 0) fill(r, kGridLeft + playStep * kStepW, kGridTop, kCellW, kVisibleRows * kRowH - 2, faded(kPurple, 76));

    // ---- what is under the cursor ---------------------------------------------------
    const int stripY = kGridTop + kVisibleRows * kRowH + 12;
    const auto field = [&](float fx, const char* label, const char* value, SDL_Color color) {
        fonts.small.draw(r, fx, static_cast<float>(stripY), label, kDim);
        fonts.large.draw(r, fx, stripY + 16.f, value, color);
    };
    noteName(pitch_, text, sizeof text);
    field(8.f, "KEY", text, kText);
    std::snprintf(text, sizeof text, "%d", step_ + 1);
    field(120.f, "STEP", text, kText);
    if (under) {
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(under->velocity * 100.f)));
        field(190.f, "LEVEL", text, shaping_ && !turningLength_ ? kBrass : (shaping_ ? kText : kTeal));
        std::snprintf(text, sizeof text, "%d STEP%s", under->length, under->length == 1 ? "" : "S");
        field(330.f, "LENGTH", text, shaping_ && turningLength_ ? kBrass : (shaping_ ? kText : kTeal));
    } else {
        field(190.f, "LEVEL", "-", kDim);
        field(330.f, "LENGTH", "-", kDim);
    }
    std::snprintf(text, sizeof text, "%d / %d", noteCount(), kMaxNotes);
    field(470.f, "NOTES", text, kDim);

    // ---- hints ----------------------------------------------------------------------
    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    char brushText[40];
    if (painting_) {
        std::snprintf(brushText, sizeof brushText, "paint notes at %d%%", static_cast<int>(std::round(brush_ * 100.f)));
        hint("DRAG WITH THE STICK", brushText);
    } else if (shaping_) {
        hint("STICK UP / DOWN", "level");
        hint("STICK LEFT / RIGHT", "length");
    } else {
        hint("STICK", "move");
        hint("A", under ? "level / length" : "place");
        if (under) hint("B", "delete");
        hint("X", "hear");
        hint("HOLD R1", "paint");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
