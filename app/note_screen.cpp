#include "note_screen.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace apotheca {

using namespace ui;

namespace {

// Keys run across, as on a keyboard; the bar runs down the screen.
constexpr int kGridLeft = 28, kGridW = 612, kRowH = 20;
constexpr int kCellH = kRowH - 2;
constexpr int kOctaveTop = 29, kOctaveH = 11, kKeysTop = 42;
constexpr float kNewLevel = 0.8f, kMinLevel = 0.05f;
constexpr Uint32 kRepeatDelayMs = 170, kRepeatEveryMs = 30, kPickRepeatEveryMs = 140;
constexpr float kStickKeysPerSecond = 36.f, kStickStepsPerSecond = 24.f, kFillPerSecond = 3.f, kLengthStepsPerSecond = 10.f;
constexpr float kShortestLength = 0.1f; // of a step
constexpr float kMostNudge = 0.5f;      // half a step early or late
constexpr float kChordsPerSecond = 7.f, kInversionsPerSecond = 5.f; // the stick with Y held
constexpr float kCarryKeysPerSecond = 10.f, kCarryStepsPerSecond = 6.f; // the stick with X held on a note

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

float NoteScreen::lengthNow() const {
    if (const NoteEvent* n = noteHere()) return n->length;
    if (const ChordEvent* c = chordHere()) return c->length;
    return 0.f;
}

void NoteScreen::moveCursorTo(int pitch, int step) {
    pitch_ = pitch < kLowest ? kLowest : (pitch > kHighest ? kHighest : pitch);
    // A step past either end of the bar is in the next bar of the cell, or
    // the one before; past the cell's ends it comes round.
    const int all = cellBars() * kSteps;
    const int at = (((project_.bar * kSteps + step) % all) + all) % all;
    project_.bar = at / kSteps;
    step_ = at % kSteps;
    // The keys scroll to keep the cursor in view.
    if (pitch_ < low_) low_ = pitch_;
    if (pitch_ > low_ + (visibleKeys() - 1)) low_ = pitch_ - (visibleKeys() - 1);
}

// B: the note under the cursor goes, or the whole chord it is part of.
void NoteScreen::eraseHere() {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot >= 0) pat().notes[slot] = NoteEvent();
    else if (chord >= 0) pat().chords[chord] = ChordEvent();
    if (slot >= 0 || chord >= 0) resend();
}

int NoteScreen::visibleKeys() const { return session_.octavesShown() * 12; }
// Wider keys are taller keys, so a keyboard zoomed in still looks like one.
// The grid starts just under them, lower when they are taller.
int NoteScreen::keysH() const { return session_.octavesShown() == 1 ? 30 : session_.octavesShown() == 2 ? 22 : 16; }
int NoteScreen::blackKeyH() const { return keysH() * 5 / 8; }
int NoteScreen::gridTop() const { return kKeysTop + keysH() + 2; }
int NoteScreen::keyW() const { return kGridW / visibleKeys(); } // 51, 25 or 17
int NoteScreen::cellW() const { return keyW() - 2; }

// Show 1, 2 or 3 octaves. The keys start from the C at or below the cursor
// where that fits, so one octave shows C to B.
void NoteScreen::zoomTo(int octaves) {
    session_.setOctavesShown(octaves);
    keepInView();
}

void NoteScreen::keepInView() {
    const int keys = visibleKeys();
    if (pitch_ >= low_ && pitch_ < low_ + keys && keys == shownKeys_) return;
    if (keys != shownKeys_) {
        // A new zoom: the octave the cursor is in comes first; with more than
        // one showing, the cursor's octave sits in the middle or just above.
        const int c = pitch_ - pitch_ % 12;
        low_ = keys == 12 ? c : (keys == 24 ? c - (pitch_ % 12 < 6 ? 12 : 0) : c - 12);
        shownKeys_ = keys;
    }
    if (pitch_ < low_) low_ = pitch_;
    if (pitch_ > low_ + keys - 1) low_ = pitch_ - (keys - 1);
    if (low_ < kLowest) low_ = kLowest;
    if (low_ + keys - 1 > kHighest) low_ = kHighest - (keys - 1);
}

// X held on a note or chord: it goes one key or one step that way, and the
// cursor with it. It does not leave the bar, go off the keyboard, or land
// on top of another note (a chord: on steps another chord has).
bool NoteScreen::carryBy(int dp, int ds) {
    NotePattern& p = pat();
    if (step_ + ds < 0 || step_ + ds >= kSteps) return false; // the cursor has hold of it somewhere along its length, and stays in the bar too
    if (carrying_ == Carry::Note) {
        NoteEvent& n = p.notes[carried_];
        const int pitch = n.pitch + dp, step = n.step + ds;
        if (n.velocity <= 0.f || pitch < kLowest || pitch > kHighest || step < 0 || step >= kSteps) return false;
        // Is the place it would start on taken by another note of that key?
        const float v = n.velocity;
        n.velocity = 0.f; // out of the way while looking
        const bool taken = p.at(pitch, step) >= 0;
        n.velocity = v;
        if (taken) return false;
        n.pitch = pitch;
        n.step = step;
        if (dp != 0 && !project_.playing) session_.audition(project_.track, pitch, v);
    } else {
        ChordEvent& c = p.chords[carried_];
        const int step = c.step + ds;
        if (c.velocity <= 0.f || step < 0 || step >= kSteps) return false;
        ChordEvent moved = c;
        moved.root += dp;
        moved.step = step;
        int was[kChordNotes], now[kChordNotes];
        const int n = c.pitches(was);
        if (moved.pitches(now) != n) return false; // a note of it would fall off the keyboard
        for (int i = 0; i < n; ++i)
            if (now[i] < kLowest || now[i] > kHighest) return false;
        // Chords do not overlap in time.
        const float v = c.velocity;
        c.velocity = 0.f;
        bool taken = false;
        for (int at = step; at < step + static_cast<int>(std::ceil(moved.length)) && at < kSteps; ++at) taken = taken || p.chordAt(at) >= 0;
        c.velocity = v;
        if (taken) return false;
        c = moved;
        if (dp != 0) hearChord(c);
    }
    resend();
    moveCursorTo(pitch_ + dp, step_ + ds);
    return true;
}

void NoteScreen::carry(Button direction) {
    carryBy(direction == Button::Right ? 1 : direction == Button::Left ? -1 : 0, direction == Button::Down ? 1 : direction == Button::Up ? -1 : 0);
}

// The stick, up and down, with X held: the note slides freely in time, off
// the grid. It sits early or late of its step by up to half a step, and
// passes on to the next step when it goes further. Where it cannot go on
// (the end of the bar, another note in the way) it stops at the half step.
void NoteScreen::carryFree(float steps) {
    if (steps == 0.f || carrying_ == Carry::Nothing) return;
    const auto nudgeOf = [&]() -> float& { return carrying_ == Carry::Note ? pat().notes[carried_].nudge : pat().chords[carried_].nudge; };
    float t = nudgeOf() + steps;
    while (t > kMostNudge) {
        if (!carryBy(0, 1)) { t = kMostNudge; break; }
        t -= 1.f;
    }
    while (t < -kMostNudge) {
        if (!carryBy(0, -1)) { t = -kMostNudge; break; }
        t += 1.f;
    }
    nudgeOf() = t;
    resend();
}

void NoteScreen::moveCursor(Button direction) {
    if (carrying_ != Carry::Nothing) return carry(direction);
    if (direction == Button::Right) moveCursorTo(pitch_ + 1, step_);
    if (direction == Button::Left) moveCursorTo(pitch_ - 1, step_);
    if (direction == Button::Up) moveCursorTo(pitch_, step_ - 1);
    if (direction == Button::Down) moveCursorTo(pitch_, step_ + 1);
    if (painting_) paintHere();
    if (erasing_) eraseHere();
}

// The engine is told the whole bar again: a chord's notes take whichever
// slots the loose notes leave, so one change can move several.
void NoteScreen::resend() { session_.sendNoteBar(project_.track, project_.scene, project_.bar); }

// A new one-step note under the cursor. False if one is already there, or
// the pattern is full.
bool NoteScreen::placeNote(float velocity) {
    if (pat().at(pitch_, step_) >= 0) return false;
    const int slot = pat().freeSlot();
    if (slot < 0 || pat().used() >= kMaxNotes) return false;
    NoteEvent& n = pat().notes[slot];
    n.step = step_;
    n.pitch = pitch_;
    n.length = 1.f;
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

// Any length, not only whole steps: the stick slides a note's end freely,
// and the D-pad puts it on a step.
void NoteScreen::setLength(float length) {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot < 0 && chord < 0) return;
    const int start = slot >= 0 ? pat().notes[slot].step : pat().chords[chord].step;
    float& now = slot >= 0 ? pat().notes[slot].length : pat().chords[chord].length;
    // It may run to the end of the bar, or up to the next note on this key
    // (for a chord, up to the next chord).
    int room = (cellBars() - project_.bar) * kSteps - start; // to the end of the cell: a note may run on over the bar lines
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
    // Never shrink it out from under the cursor: some of it stays in the cursor's step.
    const float least = static_cast<float>(step_ - start) + kShortestLength;
    length = length < least ? least : (length > static_cast<float>(room) ? static_cast<float>(room) : length);
    if (slot < 0) handLength_ = length; // the next chord placed will be this long
    if (length == now) return;
    now = length;
    resend();
}

// Early or late, for the note or chord under the cursor: the outer
// shoulder buttons with A held, a tenth of a step a press.
void NoteScreen::nudgeBy(float steps) {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot < 0 && chord < 0) return;
    float& now = slot >= 0 ? pat().notes[slot].nudge : pat().chords[chord].nudge;
    float to = std::round((now + steps) * 10.f) / 10.f;
    to = to < -kMostNudge ? -kMostNudge : (to > kMostNudge ? kMostNudge : to);
    if (to == now) return;
    now = to;
    resend();
}

// The D-pad: the end goes to the next step line, or the one before.
void NoteScreen::stepLength(int by) {
    const float now = lengthNow();
    const float to = by > 0 ? std::floor(now + 1.0001f) : std::ceil(now - 1.0001f);
    if (by < 0 && to < static_cast<float>(step_ - (noteHere() ? noteHere()->step : chordHere()->step) + 1)) return; // already as short as whole steps go here
    setLength(to);
}

// ---- chords ---------------------------------------------------------------------------

ChordEvent NoteScreen::inHand() const {
    const ChordPack& p = pack();
    const ChordShape& shape = p.chords[static_cast<std::size_t>(hand_) % p.chords.size()];
    ChordEvent c = makeChord(shape, pitch_, step_, handLength_, kNewLevel, inversion_);
    // It stops at the end of the bar, or where the next chord starts.
    const float most = static_cast<float>((cellBars() - project_.bar) * kSteps - c.step); // to the end of the cell
    if (c.length > most) c.length = most;
    for (const ChordEvent& other : pat().chords) {
        if (other.velocity > 0.f && other.step > c.step && other.step - c.step < c.length) c.length = other.step - c.step;
    }
    return c;
}

void NoteScreen::hearChord(const ChordEvent& chord) {
    // While choosing a chord (Y held) it is always heard, playing or not: that is how it is chosen.
    // Otherwise, with the song playing, the song will play it soon enough.
    if (project_.playing && !picking_) return;
    int keys[kChordNotes];
    const int n = chord.pitches(keys);
    for (int i = 0; i < n; ++i) session_.audition(project_.track, keys[i], chord.velocity);
}

// Scroll the keys so the chord shows, as far as that keeps the cursor on screen.
void NoteScreen::showChord(const ChordEvent& chord) {
    int keys[kChordNotes];
    const int n = chord.pitches(keys);
    if (n == 0) return;
    int low = low_;
    if (keys[0] < low) low = keys[0];
    if (keys[n - 1] > low + (visibleKeys() - 1)) low = keys[n - 1] - (visibleKeys() - 1);
    if (low > pitch_) low = pitch_;
    if (low < pitch_ - (visibleKeys() - 1)) low = pitch_ - (visibleKeys() - 1);
    low_ = low;
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
    moveCursorTo(pitch_, step_ + static_cast<int>(std::ceil(c.length))); // on to where the next one goes
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
        // As it is drawn: a note grows down the screen and fills across it.
        if (b == Button::Right) setLevel(nextTenth(levelNow(), 1.f)), turningLength_ = false;
        if (b == Button::Left) setLevel(nextTenth(levelNow(), -1.f)), turningLength_ = false;
        if (b == Button::Down) stepLength(1), turningLength_ = true;
        if (b == Button::Up) stepLength(-1), turningLength_ = true;
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

    if (b == Button::L2 || b == Button::R2) {
        // The app sends these here only while A is held: nudge, early or late.
        if (event.down && shaping_) nudgeBy(b == Button::R2 ? 0.1f : -0.1f);
        return;
    }
    if (b == Button::A) {
        if (event.down && !chordHere()) placeNote(kNewLevel); // on a chord, A takes hold of the chord
        shaping_ = event.down;
        axis_ = Axis::None;
        return;
    }
    if (b == Button::L1) {
        // Zoom. A tap steps closer (2 octaves, 1, then round to 3). Held, up
        // and down go closer and further.
        if (event.down) zooming_ = true, zoomUsed_ = false;
        else {
            if (zooming_ && !zoomUsed_) zoomTo(session_.octavesShown() == 1 ? 3 : session_.octavesShown() - 1);
            zooming_ = false;
        }
        return;
    }
    if (zooming_ && event.down && (b == Button::Up || b == Button::Down)) {
        zoomTo(session_.octavesShown() + (b == Button::Up ? -1 : 1));
        zoomUsed_ = true;
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
        if (b == Button::B) erasing_ = false;
        if (b == Button::X) carrying_ = Carry::Nothing;
        return;
    }

    if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeatAt_ = 0;
        return;
    }
    if (b == Button::B && !input.held(Button::A)) {
        // Deletes what is under the cursor, and, held, whatever the cursor is dragged over.
        erasing_ = true;
        eraseHere();
    } else if (b == Button::X) {
        if (const ChordEvent* chord = chordHere()) hearChord(*chord);
        else session_.audition(project_.track, pitch_, noteHere() ? noteHere()->velocity : kNewLevel);
        // Held, it has hold of what it heard: moving now carries it.
        const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
        if (!shaping_ && !painting_ && !erasing_ && (slot >= 0 || chord >= 0)) {
            carrying_ = slot >= 0 ? Carry::Note : Carry::Chord;
            carried_ = slot >= 0 ? slot : chord;
        }
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void NoteScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) shaping_ = false;
    if (!input.held(Button::R1)) painting_ = false;
    if (!input.held(Button::B)) erasing_ = false;
    if (!input.held(Button::X)) carrying_ = Carry::Nothing;
    if (!input.held(Button::L1)) zooming_ = false;
    if (!input.held(Button::Y)) picking_ = false; // (let go properly, the chord has landed already)

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    if (project_.bar >= cellBars()) project_.bar = 0; // the cell was shortened, or it is another cell
    const float x = input.stickX(), y = input.stickY();
    // A key played on a USB keyboard: the cursor goes to it, so A places there.
    if (const int key = session_.takeMidiKey(); key >= 0 && !shaping_ && !picking_ && !painting_) moveCursorTo(key, step_);
    keepInView(); // the zoom can be changed from SETUP too

    if (zooming_ && !picking_) {
        // L1 held: the stick zooms, once for each push up or down.
        const bool pushed = std::fabs(y) > 0.5f;
        if (pushed && !zoomPushed_) zoomTo(session_.octavesShown() + (y < 0.f ? -1 : 1)), zoomUsed_ = true;
        zoomPushed_ = pushed;
        return;
    }
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
        if (here && axis_ == Axis::Sideways && x != 0.f) {
            setLevel(levelNow() + x * std::fabs(x) * kFillPerSecond * dt);
            turningLength_ = false;
        } else if (here && axis_ == Axis::UpDown && y != 0.f) {
            // Freely: the end follows the stick, to any point in a step.
            setLength(lengthNow() + y * std::fabs(y) * kLengthStepsPerSecond * dt);
            turningLength_ = true;
        }
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else if (carrying_ != Carry::Nothing) {
        // X has hold of a note. Sideways it goes key by key (a key is a key);
        // up and down it slides freely in time. Squared: a small push is a fine move.
        axis_ = Axis::None;
        if (x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(x) * kCarryKeysPerSecond * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) carryBy(x > 0.f ? 1 : -1, 0);
        }
        if (std::fabs(y) > std::fabs(x)) carryFree(y * std::fabs(y) * kCarryStepsPerSecond * dt);
        stickY_ = 0.f;
        stickWasY_ = false;
    } else {
        axis_ = Axis::None;
        if (x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(x) * kStickKeysPerSecond * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) moveCursor(x > 0.f ? Button::Right : Button::Left);
        }
        if (y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? std::fabs(y) * kStickStepsPerSecond * dt : 1.f;
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

// Is this note written where something else on the same key is already sounding: one of a
// chord's notes, or another note that began before it and is still held? Then the other's
// block covers it on the screen.
bool NoteScreen::isUnderAnother(const NoteEvent& n) const {
    const float from = static_cast<float>(n.step) + n.nudge, to = from + n.length;
    for (const ChordEvent& c : pat().chords) {
        if (c.velocity <= 0.f) continue;
        const float cFrom = static_cast<float>(c.step) + c.nudge, cTo = cFrom + c.length;
        if (cFrom >= to || cTo <= from) continue; // not at the same time
        int keys[kChordNotes];
        const int count = c.pitches(keys);
        for (int i = 0; i < count; ++i)
            if (keys[i] == n.pitch) return true;
    }
    for (const NoteEvent& other : pat().notes) {
        if (&other == &n || other.velocity <= 0.f || other.pitch != n.pitch) continue;
        const float oFrom = static_cast<float>(other.step) + other.nudge;
        if (oFrom < from && oFrom + other.length > from) return true;
    }
    return false;
}

void NoteScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[96], name[24];
    trackName(project_, project_.track, name, sizeof name);
    if (cellBars() > 1) std::snprintf(text, sizeof text, "%s / BAR %d OF %d / SCENE %d", name, project_.bar + 1, cellBars(), project_.scene + 1);
    else std::snprintf(text, sizeof text, "%s / SCENE %d", name, project_.scene + 1);
    topBar(r, fonts, View::Pattern, text, project_);
    const bool held = shaping_ || painting_ || picking_;
    const SDL_Color cursorColor = held ? kBrass : kTeal;

    const auto gx = [&](int pitch) { return kGridLeft + (pitch - low_) * keyW(); };
    const auto gy = [&](int step) { return gridTop() + step * kRowH; };
    const auto shows = [&](int pitch) { return pitch >= low_ && pitch <= low_ + (visibleKeys() - 1); };

    // ---- the keyboard, across the top ---------------------------------------------
    // Drawn as a keyboard is: the white keys one cream strip with lines
    // between them, the black keys dark bars hanging from the top edge.
    const int keysW = visibleKeys() * keyW() - 2;
    fill(r, kGridLeft, kKeysTop, keysW, keysH(), kDim);
    for (int pitch = low_; pitch < low_ + visibleKeys(); ++pitch) {
        const bool black = isBlackKey(pitch), here = pitch == pitch_;
        const int x = gx(pitch);
        if (black) {
            // The two white keys meet under the middle of a black one.
            fill(r, x + cellW() / 2, kKeysTop + blackKeyH(), 1, keysH() - blackKeyH(), kBg);
            fill(r, x + 1, kKeysTop, cellW() - 2, blackKeyH(), here ? cursorColor : kBg);
        } else {
            if (here) fill(r, x, kKeysTop + blackKeyH() + 1, cellW(), keysH() - blackKeyH() - 1, cursorColor);
            // Two white keys side by side (E and F, B and C): a line all the way up.
            if (pitch > low_ && !isBlackKey(pitch - 1)) fill(r, x - 1, kKeysTop, 1, keysH(), kBg);
        }
    }
    // ---- which octave: a band over each, from its C ------------------------------------
    for (int pitch = low_; pitch < low_ + visibleKeys(); ++pitch) {
        if (pitch != low_ && pitch % 12 != 0) continue;
        const int c = pitch - ((pitch % 12) + 12) % 12; // this octave's C, perhaps off to the left
        int end = c + 12 < low_ + visibleKeys() ? c + 12 : low_ + visibleKeys();
        const int x0 = gx(pitch), x1 = gx(end) - 3;
        const bool mine = pitch_ >= c && pitch_ < c + 12;
        const SDL_Color color = mine ? cursorColor : kDim;
        fill(r, x0, kOctaveTop + kOctaveH - 2, x1 - x0, 1, color);
        if (pitch % 12 == 0) fill(r, x0, kOctaveTop + 2, 1, kOctaveH - 3, color); // the C end
        std::snprintf(text, sizeof text, x1 - x0 > 70 ? "OCTAVE %d" : "%d", c / 12 - 1);
        if (x1 - x0 > 14) fonts.small.draw(r, x0 + 5.f, kOctaveTop - 1.f, text, mine ? cursorColor : kText);
    }

    // ---- the empty grid: a column for each key, a row for each step ------------------
    for (int step = 0; step < kSteps; ++step) {
        std::snprintf(text, sizeof text, "%d", step + 1);
        fonts.small.drawRight(r, kGridLeft - 7.f, gy(step) + (kCellH - fonts.small.capHeight()) / 2.f, text,
                              step == step_ ? kTeal : (step % 4 == 0 ? kText : kDim));
        for (int pitch = low_; pitch < low_ + visibleKeys(); ++pitch) {
            const SDL_Color inside = isBlackKey(pitch) ? kBg : (step % 4 == 0 ? kPanel2 : kPanel);
            box(r, gx(pitch), gy(step), cellW(), kCellH, inside, kLine);
        }
    }
    // A brighter line down the grid where each octave starts.
    for (int pitch = low_ + 1; pitch < low_ + visibleKeys(); ++pitch)
        if (pitch % 12 == 0) fill(r, gx(pitch) - 2, gridTop(), 1, kSteps * kRowH - 2, kDim);

    // ---- notes --------------------------------------------------------------------------
    // A block as long (down the screen) as the note lasts, filled across as
    // far as it is loud.
    const NoteEvent* under = noteHere();
    const ChordEvent* chordUnder = chordHere();
    const auto block = [&](int pitch, int step, float length, float level, SDL_Color edge, SDL_Color inside, float nudge = 0.f) {
        // A nudged note sits a little up or down from its step line: early is up.
        const int x = gx(pitch) + 1, y = gy(step) + 1 + static_cast<int>(std::round(nudge * kRowH));
        const int full = cellW() - 2, bottom = gridTop() + kSteps * kRowH - 3;
        int tall = static_cast<int>(std::round(length * kRowH)) - 4;
        if (y + tall > bottom) tall = bottom - y; // it runs on into the next bar
        const int h = tall < 3 ? 3 : tall;
        int w = static_cast<int>(std::round(level * full));
        if (w < 2) w = 2;
        outline(r, x, y, full, h, edge);
        if (level > 0.f) fill(r, x, y, w, h, inside);
    };
    // A chord: its notes in purple, tied along the top, named above.
    const auto drawChord = [&](const ChordEvent& c, bool live, bool ghost) {
        int keys[kChordNotes];
        const int n = c.pitches(keys);
        if (n == 0) return;
        const SDL_Color color = ghost ? kBrass : (live ? kTeal : kPurple);
        for (int i = 0; i < n; ++i)
            if (shows(keys[i])) block(keys[i], c.step, c.length, ghost ? 0.f : c.velocity, color, color, c.nudge);
        const int low = keys[0] < low_ ? low_ : keys[0];
        const int high = keys[n - 1] > low_ + (visibleKeys() - 1) ? low_ + (visibleKeys() - 1) : keys[n - 1];
        if (high < low) return; // all of it is off the screen
        const int x0 = gx(low), x1 = gx(high) + cellW(), y = gy(c.step);
        fill(r, x0, y - 1, x1 - x0, 2, color);
        const float w = fonts.small.width(c.name) + 6.f;
        float lx = static_cast<float>(x0);
        if (lx + w > kScreenW - 2) lx = kScreenW - 2 - w;
        // Above it; on the first step there is no room, so just inside the top.
        const int ly = c.step == 0 ? y + 1 : y - 15;
        fill(r, static_cast<int>(lx), ly, static_cast<int>(w), 14, color);
        fonts.small.draw(r, lx + 3.f, ly + (14 - fonts.small.capHeight()) / 2.f, c.name, kBg);
    };
    // What is still sounding from earlier bars of the cell: drawn faint, from
    // the top, for as long as it has left. It is changed in the bar it starts in.
    for (int earlier = 0; earlier < project_.bar; ++earlier) {
        NoteEvent from[kMaxNotes];
        project_.current().notes[project_.scene][earlier].render(from);
        const float gone = static_cast<float>((project_.bar - earlier) * kSteps);
        for (const NoteEvent& n : from) {
            const float left = static_cast<float>(n.step) + n.length - gone;
            if (n.velocity <= 0.f || left <= 0.f || !shows(n.pitch)) continue;
            int tall = static_cast<int>(std::round(left * kRowH)) - 4;
            const int most = kSteps * kRowH - 4;
            outline(r, gx(n.pitch) + 1, gridTop() + 1, cellW() - 2, tall > most ? most : (tall < 3 ? 3 : tall), kLine);
            fill(r, gx(n.pitch) + 5, gridTop() + 1, 3, tall > most ? most : (tall < 3 ? 3 : tall), kLine);
        }
    }
    for (const NoteEvent& n : pat().notes) {
        if (n.velocity <= 0.f || !shows(n.pitch)) continue;
        const bool live = held && &n == under;
        block(n.pitch, n.step, n.length, n.velocity, live ? kTeal : kDim, live ? kTeal : kText, n.nudge);
    }
    for (int i = 0; i < kMaxChords; ++i) {
        const ChordEvent& c = pat().chords[i];
        if (c.velocity <= 0.f) continue;
        drawChord(c, (shaping_ && &c == chordUnder) || (picking_ && i == editing_), false);
    }
    // A note written where something longer is already sounding on the same key (a held chord, or
    // a long note) would be hidden under it. It is drawn again on top, narrower, with a dark edge:
    // a note inside a note.
    for (const NoteEvent& n : pat().notes) {
        if (n.velocity <= 0.f || !shows(n.pitch) || !isUnderAnother(n)) continue;
        const bool live = held && &n == under;
        const int inset = cellW() >= 20 ? 5 : 3;
        const int x = gx(n.pitch) + 1 + inset, y = gy(n.step) + 1 + static_cast<int>(std::round(n.nudge * kRowH));
        const int w = cellW() - 2 - 2 * inset, bottom = gridTop() + kSteps * kRowH - 3;
        int h = static_cast<int>(std::round(n.length * kRowH)) - 4;
        h = y + h > bottom ? bottom - y : h;
        h = h < 3 ? 3 : h;
        fill(r, x - 1, y, w + 2, h, kBg);
        fill(r, x, y + 1, w, h - 2, live ? kTeal : kText);
    }
    if (picking_ && editing_ < 0) drawChord(inHand(), false, true); // where it will land
    outline(r, gx(pitch_) - 1, gy(step_) - 1, cellW() + 2, kCellH + 2, cursorColor, 2);
    const int playStep = session_.engine.playScene() == project_.scene && session_.engine.playBar() % cellBars() == project_.bar ? session_.engine.playStep() : -1;
    if (playStep >= 0) fill(r, kGridLeft, gy(playStep), visibleKeys() * keyW() - 2, kCellH, faded(kPurple, 76));
    // A scene of fewer than sixteen steps: the steps past its end are never played, and are shaded.
    if (const int steps = project_.steps[project_.scene]; steps < kSteps)
        fill(r, 0, gy(steps) - 1, kGridLeft + visibleKeys() * keyW(), (kSteps - steps) * kRowH, faded(kBg, 185));

    // ---- what is under the cursor ---------------------------------------------------
    const int stripY = gridTop() + kSteps * kRowH + 8;
    const auto field = [&](float fx, const char* label, const char* value, SDL_Color color) {
        fonts.small.draw(r, fx, stripY + 12.f, label, kDim);
        fonts.large.draw(r, fx, stripY + 30.f, value, color);
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
            const float length = lengthNow();
            if (length == std::floor(length)) std::snprintf(text, sizeof text, "%d STEP%s", static_cast<int>(length), length == 1.f ? "" : "S");
            else std::snprintf(text, sizeof text, "%.2f STEPS", length);
            field(330.f, "LENGTH", text, shaping_ && turningLength_ ? kBrass : (shaping_ ? kText : kTeal));
        } else {
            field(190.f, "LEVEL", "-", kDim);
            field(330.f, "LENGTH", "-", kDim);
        }
        const float nudge = under ? under->nudge : (chordUnder ? chordUnder->nudge : 0.f);
        if (nudge != 0.f) {
            std::snprintf(text, sizeof text, "%d%% %s", static_cast<int>(std::round(std::fabs(nudge) * 100.f)), nudge < 0.f ? "EARLY" : "LATE");
            field(470.f, "NUDGE", text, shaping_ ? kText : kTeal);
        } else {
            std::snprintf(text, sizeof text, "%d / %d", pat().used(), kMaxNotes);
            field(470.f, "NOTES", text, kDim);
        }
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
        hint("B", editing_ >= 0 ? "break into notes" : "cancel");
    } else if (carrying_ != Carry::Nothing) {
        hint("STICK UP / DOWN", "slide in time, freely");
        hint("D-PAD", "by whole steps");
        hint("LEFT / RIGHT", "to another key");
    } else if (painting_) {
        std::snprintf(brushText, sizeof brushText, "paint notes at %d%%", static_cast<int>(std::round(brush_ * 100.f)));
        hint("DRAG", brushText);
    } else if (shaping_) {
        hint("LEFT / RIGHT", "level");
        hint("STICK UP / DOWN", "length");
        hint("D-PAD UP / DOWN", "by whole steps");
        hint("L2 / R2", "nudge early / late");
    } else {
        hint("A", under || chordUnder ? "level / length" : "place");
        if (under || chordUnder) hint("B", "delete");
        hint("HOLD Y", pat().chordAt(step_) >= 0 ? "swap chord" : "chord");
        hint("HOLD R1", "paint");
        if (under || chordUnder) hint("HOLD X", "move it");
        hint("HOLD B", "erase");
        hint("L1", "zoom");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
