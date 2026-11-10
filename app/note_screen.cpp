#include "note_screen.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace apotheca {

using namespace ui;

namespace {

constexpr int kGridLeft = 116, kGridTop = 54, kStepW = 32, kRowH = 20;
constexpr int kCellW = kStepW - 2, kCellH = kRowH - 2;
constexpr int kNameX = 8, kNameW = 104;
constexpr float kNewLevel = 0.8f, kMinLevel = 0.05f;
constexpr Uint32 kRepeatDelayMs = 170, kRepeatEveryMs = 30, kPickRepeatEveryMs = 140;
constexpr float kStickCellsPerSecond = 36.f, kFillPerSecond = 3.f, kLengthStepsPerSecond = 24.f;
constexpr float kChordsPerSecond = 7.f, kInversionsPerSecond = 5.f; // the stick with Y held

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

int NoteScreen::chordSlotHere() const {
    if (pat().at(pitch_, step_) >= 0) return -1; // a loose note on top is what the cursor has
    const int slot = pat().chordAt(step_);
    if (slot < 0) return -1;
    int keys[kChordNotes];
    const int n = pat().chords[slot].pitches(keys);
    for (int i = 0; i < n; ++i)
        if (keys[i] == pitch_) return slot;
    return -1;
}

const ChordEvent* NoteScreen::chordHere() const {
    const int slot = chordSlotHere();
    return slot < 0 ? nullptr : &pat().chords[slot];
}

int NoteScreen::chordCount() const {
    int count = 0;
    for (const ChordEvent& c : pat().chords) count += c.velocity > 0.f ? 1 : 0;
    return count;
}

float NoteScreen::levelNow() const {
    if (const NoteEvent* n = noteHere()) return n->velocity;
    if (const ChordEvent* c = chordHere()) return c->velocity;
    return 0.f;
}

int NoteScreen::lengthNow() const {
    if (const NoteEvent* n = noteHere()) return n->length;
    if (const ChordEvent* c = chordHere()) return c->length;
    return 0;
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

// The engine is told the whole bar again: a chord's notes take whichever
// slots the loose notes leave, so one change can move several.
void NoteScreen::resend() { session_.sendNotePattern(project_.track, project_.scene); }

// A new one-step note under the cursor. False if one is already there, or
// the pattern is full.
bool NoteScreen::placeNote(float velocity) {
    if (pat().at(pitch_, step_) >= 0) return false;
    const int slot = pat().freeSlot();
    if (slot < 0 || pat().used() >= kMaxNotes) return false;
    NoteEvent& n = pat().notes[slot];
    n.step = step_;
    n.pitch = pitch_;
    n.length = 1;
    n.velocity = velocity;
    resend();
    if (!project_.playing) session_.audition(project_.track, pitch_, velocity);
    return true;
}

void NoteScreen::setLevel(float level) {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot < 0 && chord < 0) return;
    float& now = slot >= 0 ? pat().notes[slot].velocity : pat().chords[chord].velocity;
    level = level < kMinLevel ? kMinLevel : (level > 1.f ? 1.f : level);
    if (level == now) return;
    now = level;
    resend();
}

void NoteScreen::setLength(int length) {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot < 0 && chord < 0) return;
    const int start = slot >= 0 ? pat().notes[slot].step : pat().chords[chord].step;
    int& now = slot >= 0 ? pat().notes[slot].length : pat().chords[chord].length;
    // It may run to the end of the bar, or up to the next note on this key
    // (for a chord, up to the next chord).
    int room = kSteps - start;
    if (slot >= 0) {
        for (const NoteEvent& other : pat().notes) {
            if (&other != &pat().notes[slot] && other.velocity > 0.f && other.pitch == pitch_ && other.step > start && other.step - start < room) {
                room = other.step - start;
            }
        }
    } else {
        for (const ChordEvent& other : pat().chords) {
            if (other.velocity > 0.f && other.step > start && other.step - start < room) room = other.step - start;
        }
    }
    length = length < 1 ? 1 : (length > room ? room : length);
    // Never shrink it out from under the cursor.
    if (length < step_ - start + 1) length = step_ - start + 1;
    if (slot < 0) handLength_ = length; // the next chord placed will be this long
    if (length == now) return;
    now = length;
    resend();
}

// ---- chords ---------------------------------------------------------------------------

ChordEvent NoteScreen::inHand() const {
    const ChordPack& p = pack();
    const ChordShape& shape = p.chords[static_cast<std::size_t>(hand_) % p.chords.size()];
    ChordEvent c = makeChord(shape, pitch_, step_, handLength_, kNewLevel, inversion_);
    // It stops at the end of the bar, or where the next chord starts.
    if (c.length > kSteps - c.step) c.length = kSteps - c.step;
    for (const ChordEvent& other : pat().chords) {
        if (other.velocity > 0.f && other.step > c.step && other.step - c.step < c.length) c.length = other.step - c.step;
    }
    return c;
}

void NoteScreen::hearChord(const ChordEvent& chord) {
    if (project_.playing) return; // the song will play it soon enough
    int keys[kChordNotes];
    const int n = chord.pitches(keys);
    for (int i = 0; i < n; ++i) session_.audition(project_.track, keys[i], chord.velocity);
}

// Scroll the keys so the chord shows, as far as that keeps the cursor on screen.
void NoteScreen::showChord(const ChordEvent& chord) {
    int keys[kChordNotes];
    const int n = chord.pitches(keys);
    if (n == 0) return;
    int top = top_;
    if (keys[n - 1] > top) top = keys[n - 1];
    if (keys[0] < top - (kVisibleRows - 1)) top = keys[0] + (kVisibleRows - 1);
    if (top < pitch_) top = pitch_;
    if (top > pitch_ + (kVisibleRows - 1)) top = pitch_ + (kVisibleRows - 1);
    if (top > kHighest) top = kHighest;
    top_ = top;
}

void NoteScreen::beginPick() {
    picking_ = true;
    editing_ = pat().chordAt(step_);
    const int count = static_cast<int>(pack().chords.size());
    hand_ = ((hand_ % count) + count) % count;
    if (editing_ >= 0) {
        // Start from the chord that is there, if the pack has it.
        const ChordEvent& c = pat().chords[editing_];
        inversion_ = c.inversion;
        for (int i = 0; i < count; ++i)
            if (std::strcmp(pack().chords[static_cast<std::size_t>(i)].name, c.name) == 0) { hand_ = i; break; }
        hearChord(c);
        showChord(c);
    } else {
        hearChord(inHand());
        showChord(inHand());
    }
}

void NoteScreen::pickChanged() {
    const int count = static_cast<int>(pack().chords.size());
    hand_ = ((hand_ % count) + count) % count;
    if (editing_ < 0) {
        hearChord(inHand());
        showChord(inHand());
        return;
    }
    // Swap it in place: the same steps, level and octave.
    ChordEvent& c = pat().chords[editing_];
    ChordEvent swapped = makeChord(pack().chords[static_cast<std::size_t>(hand_)], c.root, c.step, c.length, c.velocity, inversion_);
    int keys[kChordNotes];
    if (pat().used() - c.pitches(keys) + swapped.pitches(keys) > kMaxNotes) return; // no room for a bigger chord
    c = swapped;
    resend();
    hearChord(c);
    showChord(c);
}

void NoteScreen::endPick() {
    if (!picking_) return; // B already called it off
    picking_ = false;
    if (editing_ >= 0) return; // the swaps were made as they were chosen
    const ChordEvent c = inHand();
    int keys[kChordNotes];
    const int slot = pat().freeChord();
    if (slot < 0 || pat().used() + c.pitches(keys) > kMaxNotes) return;
    pat().chords[slot] = c;
    resend();
    moveCursorTo(pitch_, step_ + c.length); // on to where the next one goes
}

void NoteScreen::breakUp(int chord) {
    const ChordEvent c = pat().chords[chord];
    pat().chords[chord] = ChordEvent();
    int keys[kChordNotes];
    const int n = c.pitches(keys);
    for (int i = 0; i < n; ++i) {
        const int slot = pat().freeSlot();
        if (slot < 0) break;
        NoteEvent& note = pat().notes[slot];
        note.step = c.step;
        note.pitch = keys[i];
        note.length = c.length;
        note.velocity = c.velocity;
    }
    resend();
}

// A direction from the D-pad: the cursor, or the thing a held button has hold of.
void NoteScreen::press(Button b) {
    if (picking_) {
        if (b == Button::Left || b == Button::Right) {
            hand_ += b == Button::Right ? 1 : -1;
        } else {
            const int to = inversion_ + (b == Button::Up ? 1 : -1);
            if (to < -kMaxInversion || to > kMaxInversion) return;
            inversion_ = to;
        }
        pickChanged();
    } else if (shaping_) {
        if (levelNow() <= 0.f) return;
        if (b == Button::Up) setLevel(nextTenth(levelNow(), 1.f)), turningLength_ = false;
        if (b == Button::Down) setLevel(nextTenth(levelNow(), -1.f)), turningLength_ = false;
        if (b == Button::Right) setLength(lengthNow() + 1), turningLength_ = true;
        if (b == Button::Left) setLength(lengthNow() - 1), turningLength_ = true;
    } else {
        moveCursor(b);
    }
}

void NoteScreen::paintHere() {
    if (placeNote(brush_)) return;
    setLevel(brush_); // a note already here takes the brush's level
}

void NoteScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;

    if (b == Button::Y) {
        if (event.down && !shaping_ && !painting_) beginPick();
        else if (!event.down) endPick();
        return;
    }
    if (picking_) {
        // Y has the screen: only the chord's own buttons do anything.
        if (!event.down) {
            if (b == repeating_) repeating_ = Button::None;
        } else if (isDirection(b)) {
            press(b);
            repeating_ = b;
            repeatAt_ = 0;
        } else if (b == Button::L1 || b == Button::R1) {
            session_.choosePack(session_.packIndex() + (b == Button::R1 ? 1 : -1));
            hand_ = 0;
            pickChanged();
        } else if (b == Button::X) {
            hearChord(editing_ >= 0 ? pat().chords[editing_] : inHand());
        } else if (b == Button::B) {
            if (editing_ >= 0) breakUp(editing_);
            picking_ = false;
        }
        return;
    }

    if (b == Button::A) {
        if (event.down && !chordHere()) placeNote(kNewLevel); // on a chord, A takes hold of the chord
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
        press(b);
        repeating_ = b;
        repeatAt_ = 0;
        return;
    }
    if (b == Button::B && !input.held(Button::A)) {
        const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
        if (slot >= 0) pat().notes[slot] = NoteEvent();
        else if (chord >= 0) pat().chords[chord] = ChordEvent();
        if (slot >= 0 || chord >= 0) resend();
    } else if (b == Button::X) {
        if (const ChordEvent* chord = chordHere()) hearChord(*chord);
        else session_.audition(project_.track, pitch_, noteHere() ? noteHere()->velocity : kNewLevel);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void NoteScreen::update(const Input& input, Uint32 nowMs) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    lastMs_ = nowMs;
    const float x = input.stickX(), y = input.stickY();

    if (picking_) {
        // The stick steps through chords (sideways) or inversions (up and
        // down): at once on the first push, then at a steady rate.
        const bool sideways = std::fabs(x) >= std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(push) * (sideways ? kChordsPerSecond : kInversionsPerSecond) * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f)
                press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
        }
        stickY_ = 0.f;
        stickWasY_ = false;
    } else if (shaping_) {
        // One job per push, as on the drum grid.
        if (x == 0.f && y == 0.f) axis_ = Axis::None;
        else if (axis_ == Axis::None) axis_ = std::fabs(y) >= std::fabs(x) ? Axis::UpDown : Axis::Sideways;
        const bool here = levelNow() > 0.f;
        if (here && axis_ == Axis::UpDown && y != 0.f) {
            setLevel(levelNow() - y * std::fabs(y) * kFillPerSecond * dt);
            turningLength_ = false;
        } else if (here && axis_ == Axis::Sideways && x != 0.f) {
            lengthSaved_ += x * std::fabs(x) * kLengthStepsPerSecond * dt;
            turningLength_ = true;
            while (lengthSaved_ >= 1.f) { lengthSaved_ -= 1.f; setLength(lengthNow() + 1); }
            while (lengthSaved_ <= -1.f) { lengthSaved_ += 1.f; setLength(lengthNow() - 1); }
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
        if (picking_) repeatAt_ = nowMs + kPickRepeatEveryMs; // chords are for hearing one at a time
        press(repeating_);
    }
}

void NoteScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[48], name[24];
    trackName(project_, project_.track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / SCENE %d", name, project_.scene + 1);
    topBar(r, fonts, View::Pattern, text, project_);
    const bool held = shaping_ || painting_ || picking_;
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
    const ChordEvent* chordUnder = chordHere();
    const auto shows = [&](int pitch) { return pitch <= top_ && pitch >= top_ - (kVisibleRows - 1); };
    const auto block = [&](int pitch, int step, int length, float level, SDL_Color edge, SDL_Color inside) {
        const int y = kGridTop + (top_ - pitch) * kRowH;
        const int x = kGridLeft + step * kStepW + 2;
        const int w = length * kStepW - 6;
        const int full = kCellH - 4;
        int h = static_cast<int>(std::round(level * full));
        if (h < 2) h = 2;
        outline(r, x, y + 2, w, full, edge);
        if (level > 0.f) fill(r, x, y + 2 + full - h, w, h, inside);
    };
    // A chord: its notes in purple, tied down the left side, named on top.
    const auto drawChord = [&](const ChordEvent& c, bool live, bool ghost) {
        int keys[kChordNotes];
        const int n = c.pitches(keys);
        if (n == 0) return;
        const SDL_Color color = ghost ? kBrass : (live ? kTeal : kPurple);
        for (int i = 0; i < n; ++i)
            if (shows(keys[i])) block(keys[i], c.step, c.length, ghost ? 0.f : c.velocity, color, color);
        const int high = keys[n - 1] > top_ ? top_ : keys[n - 1];
        const int low = keys[0] < top_ - (kVisibleRows - 1) ? top_ - (kVisibleRows - 1) : keys[0];
        if (high < low) return; // all of it is off the screen
        const int x = kGridLeft + c.step * kStepW;
        const int y0 = kGridTop + (top_ - high) * kRowH + 2, y1 = kGridTop + (top_ - low) * kRowH + kCellH - 2;
        fill(r, x - 1, y0, 2, y1 - y0, color);
        const float w = fonts.small.width(c.name) + 6.f;
        float lx = static_cast<float>(x - 1);
        if (lx + w > kScreenW - 2) lx = kScreenW - 2 - w;
        const int ly = y0 - 15 < kGridTop ? y1 + 1 : y0 - 15; // under it when there is no room above
        fill(r, static_cast<int>(lx), ly, static_cast<int>(w), 14, color);
        fonts.small.draw(r, lx + 3.f, ly + (14 - fonts.small.capHeight()) / 2.f, c.name, kBg);
    };
    for (const NoteEvent& n : pat().notes) {
        if (n.velocity <= 0.f || !shows(n.pitch)) continue;
        const bool live = held && &n == under;
        block(n.pitch, n.step, n.length, n.velocity, live ? kTeal : kDim, live ? kTeal : kText);
    }
    for (int i = 0; i < kMaxChords; ++i) {
        const ChordEvent& c = pat().chords[i];
        if (c.velocity <= 0.f) continue;
        drawChord(c, (shaping_ && &c == chordUnder) || (picking_ && i == editing_), false);
    }
    if (picking_ && editing_ < 0) drawChord(inHand(), false, true); // where it will land
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
    if (picking_) {
        // The pack laid out: seven chords at a time, the one in hand lit.
        const ChordPack& p = pack();
        const int count = static_cast<int>(p.chords.size());
        float x = 8.f;
        fonts.small.draw(r, x, static_cast<float>(stripY), "L1", kDim);
        x += fonts.small.width("L1") + 6.f;
        std::string packName = p.name;
        for (char& c : packName) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        fonts.small.draw(r, x, static_cast<float>(stripY), packName.c_str(), kText);
        x += fonts.small.width(packName.c_str()) + 6.f;
        fonts.small.draw(r, x, static_cast<float>(stripY), "R1", kDim);
        std::snprintf(text, sizeof text, "%d / %d", hand_ + 1, count);
        fonts.small.draw(r, 300.f, static_cast<float>(stripY), text, kDim);
        if (inversion_ != 0) {
            std::snprintf(text, sizeof text, "INVERSION %+d", inversion_);
            fonts.small.draw(r, 380.f, static_cast<float>(stripY), text, kBrass);
        }
        fonts.small.drawRight(r, kScreenW - 8.f, static_cast<float>(stripY), editing_ >= 0 ? "SWAPPING THIS CHORD" : "LET GO OF Y TO PLACE", kDim);
        const int shown = count < 7 ? count : 7, tileW = 88, tileH = 30, tileY = stripY + 17;
        int first = hand_ - 3;
        if (first > count - shown) first = count - shown;
        if (first < 0) first = 0;
        for (int i = 0; i < shown; ++i) {
            const int index = first + i, tx = 8 + i * (tileW + 2);
            const bool here = index == hand_;
            box(r, tx, tileY, tileW, tileH, here ? kPanel2 : kPanel, kLine);
            fonts.small.drawCentered(r, tx + tileW / 2.f, tileY + (tileH - fonts.small.capHeight()) / 2.f,
                                     p.chords[static_cast<std::size_t>(index)].name, here ? kText : kDim);
            if (here) outline(r, tx - 1, tileY - 1, tileW + 2, tileH + 2, kBrass, 2);
        }
    } else {
        if (chordUnder) {
            field(8.f, "CHORD", chordUnder->name, kPurple);
        } else {
            noteName(pitch_, text, sizeof text);
            field(8.f, "KEY", text, kText);
        }
        std::snprintf(text, sizeof text, "%d", step_ + 1);
        field(120.f, "STEP", text, kText);
        if (under || chordUnder) {
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(levelNow() * 100.f)));
            field(190.f, "LEVEL", text, shaping_ && !turningLength_ ? kBrass : (shaping_ ? kText : kTeal));
            std::snprintf(text, sizeof text, "%d STEP%s", lengthNow(), lengthNow() == 1 ? "" : "S");
            field(330.f, "LENGTH", text, shaping_ && turningLength_ ? kBrass : (shaping_ ? kText : kTeal));
        } else {
            field(190.f, "LEVEL", "-", kDim);
            field(330.f, "LENGTH", "-", kDim);
        }
        std::snprintf(text, sizeof text, "%d / %d", pat().used(), kMaxNotes);
        field(470.f, "NOTES", text, kDim);
    }

    // ---- hints ----------------------------------------------------------------------
    Hint list[8];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    char brushText[40];
    if (picking_) {
        hint("LEFT / RIGHT", "chord");
        hint("UP / DOWN", "inversion");
        hint("L1 R1", "pack");
        hint("X", "hear");
        hint("B", editing_ >= 0 ? "break into notes" : "cancel");
    } else if (painting_) {
        std::snprintf(brushText, sizeof brushText, "paint notes at %d%%", static_cast<int>(std::round(brush_ * 100.f)));
        hint("DRAG WITH THE STICK", brushText);
    } else if (shaping_) {
        hint("STICK UP / DOWN", "level");
        hint("STICK LEFT / RIGHT", "length");
    } else {
        hint("STICK", "move");
        hint("A", under || chordUnder ? "level / length" : "place");
        if (under || chordUnder) hint("B", "delete");
        hint("X", "hear");
        hint("HOLD Y", pat().chordAt(step_) >= 0 ? "swap chord" : "chord");
        hint("HOLD R1", "paint");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
