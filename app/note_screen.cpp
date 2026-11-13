#include "note_screen.hpp"

#include "note_marks.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace apotheca {

using namespace ui;

namespace {

// Keys run across, as on a keyboard; the bar runs down the screen.
// All sixteen steps show at once. 21 is the tallest a row can be with one
// octave showing (the tallest keys) and the chord tiles under the grid: at
// 22 the tiles reach the hints bar.
constexpr int kGridLeft = 28, kGridW = 612, kRowH = 21;
constexpr int kCellH = kRowH - 2;
constexpr int kOctaveTop = 33, kOctaveH = 11, kKeysTop = 45;
constexpr int kStripGap = 4; // from the grid down to the strip
constexpr float kNewLevel = 0.8f, kMinLevel = 0.05f;
constexpr Uint32 kRepeatDelayMs = ui::kCursorDelayMs, kRepeatEveryMs = ui::kCursorEveryMs, kPickRepeatEveryMs = ui::kListEveryMs;
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
int NoteScreen::keysH() const { return session_.octavesShown() == 1 ? 24 : session_.octavesShown() == 2 ? 22 : 16; }
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
    // Swap it in place: the same steps, level, octave, nudge and ratchet.
    ChordEvent& c = pat().chords[editing_];
    ChordEvent swapped = makeChord(pack().chords[static_cast<std::size_t>(hand_)], c.root, c.step, c.length, c.velocity, inversion_);
    swapped.nudge = c.nudge;
    swapped.ratchet = c.ratchet;
    swapped.throwTo = c.throwTo;
    swapped.play = c.play; // and what the chord does: it is the same place in the song
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
        // The notes play as the chord did: its nudge and its ratchet too. Its chord effects go
        // with the chord: a strum is the chord's, and loose notes are each their own.
        note = NoteEvent();
        note.step = c.step;
        note.pitch = keys[i];
        note.length = c.length;
        note.nudge = c.nudge;
        note.velocity = c.velocity;
        note.ratchet = c.ratchet;
        note.throwTo = c.throwTo;
    }
    resend();
}

// A direction from the D-pad: the cursor, or the thing a held button has hold of.
// With X held on a note or a chord, one click of what it does besides play. Left and right: a
// ratchet, the note 2, 3 or 4 times inside its first step. Up and down: a glide, one length
// longer or shorter, down to OFF (a note only: a chord has no one note to slide from).
//
// On a chord there are six things it can do (chord_effects.hpp), so the directions are
// shared out the other way: up and down pick which, left and right set it, a click a push.
void NoteScreen::fxTurn(Button direction) {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot < 0 && chord < 0) return;
    if (slot < 0) {
        if (direction == Button::Up || direction == Button::Down) {
            const int to = fxPick_ + (direction == Button::Down ? 1 : -1);
            if (to >= 0 && to < kChordEffects) fxPick_ = to; // (it stops at each end)
            return;
        }
        if (chordEffectStep(pat().chords[chord], fxPick_, direction == Button::Right ? 1 : -1)) session_.sendNoteBar(project_.track, project_.scene, project_.bar);
        return;
    }
    std::uint8_t& ratchet = pat().notes[slot].ratchet;
    const int hits = ratchet < 2 ? 1 : ratchet;
    int to = hits;
    if (direction == Button::Right && hits < kMaxRatchet) to = hits + 1;
    if (direction == Button::Left && hits > 1) to = hits - 1;
    bool changed = to != hits;
    ratchet = static_cast<std::uint8_t>(to < 2 ? 0 : to); // "one" is kept one way: 0
    if (direction == Button::Up || direction == Button::Down) {
        const int place = glidePlace(glideKind(pat().notes[slot].glide)) + (direction == Button::Up ? 1 : -1);
        const std::uint8_t glide = static_cast<std::uint8_t>(glideAt(place)); // (it stops at each end)
        changed = changed || pat().notes[slot].glide != glide;
        pat().notes[slot].glide = glide;
    }
    if (changed) session_.sendNoteBar(project_.track, project_.scene, project_.bar);
}

// With X held on a note or a chord, A steps its THROW round: OFF, ECHO, SPACE, BOTH. While the
// note sounds, the track's send to that bus is full, whatever its knob says. (A chord is thrown
// as a note is: every key of it.)
void NoteScreen::fxThrow() {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot < 0 && chord < 0) return;
    std::uint8_t& throwTo = slot >= 0 ? pat().notes[slot].throwTo : pat().chords[chord].throwTo;
    throwTo = static_cast<std::uint8_t>((throwTo + 1) % kThrowChoices);
    session_.sendNoteBar(project_.track, project_.scene, project_.bar);
}

// With X held on a note, L1 and R1 step the track's GLIDE FROM: which key of the moment before
// its glides come from (TOP, PAIRED, NEAREST, RANDOM). It is the track's, not the note's: every
// line on the roll follows at once. It stops at each end. (The dice leave it alone.)
void NoteScreen::fxFrom(int by) {
    const int to = glideFromKind(project_.current().glideFrom) + by;
    if (to < 0 || to >= kGlideFroms) return;
    session_.setGlideFrom(project_.track, to);
}

// With X held on a note or a chord, Y rolls the dice for it: a ratchet, a glide of some length
// (a note only), or both. Each roll is something, and something else than it was. The dice do not throw: a
// THROW is left as the player set it.
// A chord's dice roll among its six (chordEffectRoll, chord_effects.hpp: the odds are there).
void NoteScreen::fxRoll() {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot < 0 && chord < 0) return;
    const auto roll = [&](std::uint32_t outOf) {
        dice_ = dice_ * 1664525u + 1013904223u;
        return (dice_ >> 12) % outOf;
    };
    if (slot < 0) {
        chordEffectRoll(pat().chords[chord], roll);
        session_.sendNoteBar(project_.track, project_.scene, project_.bar);
        return;
    }
    std::uint8_t& ratchet = pat().notes[slot].ratchet;
    const int ratchetWas = ratchet < 2 ? 0 : ratchet, glideWas = pat().notes[slot].glide;
    static const std::uint8_t ratchets[] = {2, 2, 3, 4};
    int to = 0, glide = 0;
    for (;;) { // (there is always another roll to be had, so this ends)
        to = roll(100) < 65 ? ratchets[roll(4)] : 0;
        glide = roll(100) < 40 ? glideAt(1 + static_cast<int>(roll(kGlideKinds - 1))) : kGlideOff;
        if ((to || glide) && (to != ratchetWas || glide != glideWas)) break;
    }
    ratchet = static_cast<std::uint8_t>(to);
    pat().notes[slot].glide = static_cast<std::uint8_t>(glide);
    session_.sendNoteBar(project_.track, project_.scene, project_.bar);
}

void NoteScreen::press(Button b) {
    if (fxHeld_) return fxTurn(b);
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

    if (fxHeld_ && b == Button::Y) {
        if (event.down) fxRoll(); // the dice, for what this note does. (Chords wait until X is let go.)
        return;
    }
    if (fxHeld_ && b == Button::A) {
        if (event.down) fxThrow(); // THROW, a step round. (Placing and shaping wait until X is let go.)
        return;
    }
    if (fxHeld_ && (b == Button::L1 || b == Button::R1) && noteHere()) {
        if (event.down) fxFrom(b == Button::R1 ? 1 : -1); // GLIDE FROM, a click. (Zoom and carry wait until X is let go.)
        return;
    }
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
        // On a note or a chord, R1 takes hold of it: moving now carries it. On an empty step it is the
        // brush: every step crossed gets a note. (Hold X used to carry; X is kept for what a note does.)
        const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
        carrying_ = Carry::Nothing;
        painting_ = false;
        if (event.down && !shaping_ && !erasing_ && (slot >= 0 || chord >= 0)) {
            carrying_ = slot >= 0 ? Carry::Note : Carry::Chord;
            carried_ = slot >= 0 ? slot : chord;
        } else if (event.down && !shaping_) {
            painting_ = true;
            brush_ = kNewLevel;
            paintHere();
        }
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        if (b == Button::B) erasing_ = false;
        if (b == Button::X) fxHeld_ = false;
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
        // On a note or a chord it is also a hold: the stick or D-pad then sets what it does besides play.
        fxHeld_ = (noteHere() || chordHere()) && !shaping_ && !painting_ && !picking_ && carrying_ == Carry::Nothing;
        fxSaved_ = 0.f;
        fxWas_ = false;
        // A direction that was already held (the cursor was on its way here) is not a turn of the
        // effect: X only heard the note. It must be let go and pushed again.
        fxWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
        if (fxHeld_) repeating_ = Button::None;
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
    if (!input.held(Button::R1)) carrying_ = Carry::Nothing;
    if (!input.held(Button::X) || (!noteHere() && !chordHere())) fxHeld_ = false;
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

    if (fxHeld_) {
        // X is held on a note: the stick clicks what it does. One way per push; a click at once, then slowly.
        if (x == 0.f && y == 0.f) {
            axis_ = Axis::None;
            fxSaved_ = 0.f;
            fxWas_ = false;
            fxWait_ = false;
        } else if (!fxWait_) {
            if (axis_ == Axis::None) axis_ = std::fabs(y) >= std::fabs(x) ? Axis::UpDown : Axis::Sideways;
            const float push = axis_ == Axis::UpDown ? -y : x;
            if (push != 0.f) {
                fxSaved_ += fxWas_ ? std::fabs(push) * 4.f * dt : 1.f;
                fxWas_ = true;
                for (; fxSaved_ >= 1.f; fxSaved_ -= 1.f)
                    fxTurn(axis_ == Axis::UpDown ? (push > 0.f ? Button::Up : Button::Down) : (push > 0.f ? Button::Right : Button::Left));
            }
        }
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
        if (repeating_ != Button::None && repeatAt_ == 0) {
            repeatAt_ = nowMs + ui::kListDelayMs;
        } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
            repeatAt_ = nowMs + 2 * ui::kListEveryMs; // clicks, not a run
            fxTurn(repeating_);
        }
        return;
    }
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
        if (x1 - x0 > 14) fonts.small.draw(r, x0 + 5.f, kOctaveTop - 3.f, text, mine ? cursorColor : kText);
    }

    // ---- the empty grid: a column for each key, a row for each step ------------------
    for (int step = 0; step < kSteps; ++step) {
        // Counted through the cell: bar 2 reads 17 to 32. A step past the scene's end (a scene of
        // 25 steps: 26 to 32) is never played and reads "-".
        if (step < project_.barSteps(project_.scene, project_.bar)) std::snprintf(text, sizeof text, "%d", project_.bar * kSteps + step + 1);
        else std::snprintf(text, sizeof text, "-");
        // Three figures (bars 7 and 8) need the room: they stand four pixels nearer the grid.
        fonts.small.drawRight(r, kGridLeft - (project_.bar * kSteps + step + 1 >= 100 ? 3.f : 7.f), gy(step) + (kCellH - fonts.small.capHeight()) / 2.f, text,
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
    // Where a chord's name is: above its lowest key that shows; on the first step there is no
    // room above, so just inside the top. False when the whole chord is off the side.
    const auto chordNameBox = [&](const ChordEvent& c, SDL_Rect* box) {
        int keys[kChordNotes];
        const int n = c.pitches(keys);
        if (n == 0) return false;
        const int low = keys[0] < low_ ? low_ : keys[0];
        const int high = keys[n - 1] > low_ + (visibleKeys() - 1) ? low_ + (visibleKeys() - 1) : keys[n - 1];
        if (high < low) return false;
        const float w = fonts.small.width(c.name) + 6.f;
        float lx = static_cast<float>(gx(low));
        if (lx + w > kScreenW - 2) lx = kScreenW - 2 - w;
        *box = SDL_Rect{static_cast<int>(lx), c.step == 0 ? gy(c.step) + 1 : gy(c.step) - 15, static_cast<int>(w), 14};
        return true;
    };
    // A chord: its notes in purple, tied along the top, named above.
    const auto drawChord = [&](const ChordEvent& c, bool live, bool ghost, bool nameOnly = false) {
        int keys[kChordNotes];
        const int n = c.pitches(keys);
        if (n == 0) return;
        const SDL_Color color = ghost ? kBrass : (live ? kTeal : kPurple);
        for (int i = 0; i < n && !nameOnly; ++i)
            if (shows(keys[i])) block(keys[i], c.step, c.length, ghost ? 0.f : c.velocity, color, color, c.nudge);
        const int low = keys[0] < low_ ? low_ : keys[0];
        const int high = keys[n - 1] > low_ + (visibleKeys() - 1) ? low_ + (visibleKeys() - 1) : keys[n - 1];
        if (high < low) return; // all of it is off the screen
        const int x0 = gx(low), x1 = gx(high) + cellW(), y = gy(c.step);
        if (!nameOnly) fill(r, x0, y - 1, x1 - x0, 2, color);
        SDL_Rect box;
        if (!chordNameBox(c, &box)) return;
        fill(r, box.x, box.y, box.w, box.h, color);
        fonts.small.draw(r, static_cast<float>(box.x) + 3.f, box.y + (14 - fonts.small.capHeight()) / 2.f, c.name, kBg);
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
    // What a note does besides play, as marks on its block. A ratchet: dark lines across its first
    // step, cutting it into as many pieces as it has notes. A glide: a purple corner at its top,
    // on the side of the key it comes from... (the side it slides in from is not known here; the corner is on the left).
    // A note drawn inside another (below) is narrower by `inset` each side, and its marks with it.
    // A chord with a chord effect set (STRUM, HUMAN TIME and the rest): a light corner at the top
    // of each of its keys, where a note has its glide's purple one (a chord does not glide).
    const auto marks = [&](int pitch, int step, float nudge, float length, int ratchet, bool glide, int throwTo, int inset = 0, bool chord = false, bool plays = false) {
        if (!shows(pitch)) return;
        const int pad = inset ? 1 : 0; // the dark edge it has above and below
        const int x = gx(pitch) + 1 + inset, y = gy(step) + 1 + pad + static_cast<int>(std::round(nudge * kRowH)), full = cellW() - 2 - 2 * inset;
        const int first = static_cast<int>(std::round((length < 1.f ? length : 1.f) * kRowH)) - 4 - 2 * pad; // the part of it in its first step
        int tops[3];
        const int line = ratchetLines(first, ratchet, tops);
        for (int k = 0; line > 0 && k < ratchet - 1; ++k) fill(r, x, y + tops[k], full, line, kBg);
        if (ratchet >= 2 && first > 2) fill(r, x + full - 4, y, 4, first, kPurple); // and a purple edge, so it is not taken for a few short notes
        if (glide || plays) {
            // Ten across where there is room. On narrow keys it stops short of a ratchet's purple edge.
            const int room = ratchet >= 2 ? full - 6 : full - 2;
            const int corner = room > 10 ? 10 : (room < 3 ? 3 : room);
            for (int d = 0; d < corner; ++d) {
                fill(r, x, y + d, corner - d, 1, plays ? kText : kPurple);
                fill(r, x + corner - d, y + d, 1, 1, kBg); // a dark edge, so it stands clear of the note and of a ratchet's lines
            }
        }
        if (throwTo) {
            // THROW: a foot at the note's end, where its send shuts. On the left for ECHO, on
            // the right for SPACE, one on each side for both (the order the sends have on the FX screen).
            const int bottom = gridTop() + kSteps * kRowH - 3;
            int tall = static_cast<int>(std::round(length * kRowH)) - 4 - 2 * pad;
            tall = y + tall > bottom ? bottom - y : tall;
            tall = tall < 3 ? 3 : tall;
            const int foot = tall >= 8 ? 4 : (tall >= 5 ? 3 : 2), wide = full / 2 - 1;
            for (int side = 0; side < 2; ++side) {
                if (!((throwTo >> side) & 1)) continue;
                const int fx = side == 0 ? x : x + full - wide;
                fill(r, fx, y + tall - foot - 1, wide, 1, kBg); // a dark edge above it, as the glide's corner has
                fill(r, fx, y + tall - foot, wide, foot, chord ? kText : kPurple); // (a chord is purple already)
            }
        }
    };
    for (const NoteEvent& n : pat().notes)
        if (n.velocity > 0.f && (n.ratchet >= 2 || n.glide || n.throwTo) && !isUnderAnother(n)) marks(n.pitch, n.step, n.nudge, n.length, n.ratchet, n.glide != 0, n.throwTo);
    // A chord's RATCHET lines and its chord-effects corner are drawn ONCE (owner, 2026-11-13): on
    // the first of its keys under its name, which is its lowest that shows. They never write over
    // the name: where the name sits on that key (a chord on the first step, or one nudged early)
    // they go on the next key clear of it, and the name is drawn again on top of whatever is
    // left. THROW's foot stays on every key: it is at each key's end, where nothing else is.
    // A ratchet that is not played (the chord is an arpeggio) is not drawn.
    for (int slot = 0; slot < kMaxChords; ++slot) {
        const ChordEvent& c = pat().chords[slot];
        if (c.velocity <= 0.f || (c.ratchet < 2 && !c.throwTo && !chordPlays(c))) continue;
        int keys[kChordNotes];
        const int count = c.pitches(keys);
        SDL_Rect name;
        const bool named = chordNameBox(c, &name);
        const int top = gy(c.step) + 1 + static_cast<int>(std::round(c.nudge * kRowH));
        int once = -1;
        for (int i = 0; i < count; ++i) {
            if (!shows(keys[i])) continue;
            if (once < 0) once = i; // (if the name is over every key, the lowest all the same)
            const bool under = named && gx(keys[i]) + cellW() > name.x && gx(keys[i]) < name.x + name.w && top < name.y + name.h && top + kRowH > name.y;
            if (!under) {
                once = i;
                break;
            }
        }
        const int ratchet = chordEffectWaits(c, kFxRatchet) ? 0 : c.ratchet;
        for (int i = 0; i < count; ++i) marks(keys[i], c.step, c.nudge, c.length, i == once ? ratchet : 0, false, c.throwTo, 0, true, i == once && chordPlays(c));
        if (named) drawChord(c, (shaping_ && &c == chordUnder) || (picking_ && slot == editing_), false, true);
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
        if (n.ratchet >= 2 || n.glide || n.throwTo) marks(n.pitch, n.step, n.nudge, n.length, n.ratchet, n.glide != 0, n.throwTo, inset);
    }
    // GLIDE: a slanted line from the key the note slides from to the note's start, as long as the
    // slide. (Which key and how long: the engine's own rule, Project::glideLines.) A line may
    // start bars before its note, or before the top of the first bar, which is the end of the
    // scene: so every bar's lines are drawn here, wherever they cross the bar shown
    // (glideShifts). A key off the side of the roll is where the line runs off it.
    {
        bool any = false;
        for (int b = 0; b < cellBars() && !any; ++b)
            for (const NoteEvent& n : project_.current().notes[project_.scene][b].notes) any = any || (n.velocity > 0.f && n.glide);
        if (any) {
            GlideLine lines[kMaxBars * kMaxNotes];
            project_.glideLines(project_.track, project_.scene, lines);
            const int rows = project_.barSteps(project_.scene, project_.bar);
            const int left = kGridLeft, right = kGridLeft + visibleKeys() * keyW() - 2, top = gridTop(), bottom = gridTop() + rows * kRowH - 2;
            // The rectangles of a line are drawn in one call: first its dark edge, then the purple over it.
            constexpr int kMost = 1024;
            SDL_Rect rects[2][kMost];
            int held[2] = {0, 0};
            const auto flush = [&] {
                for (int layer = 0; layer < 2; ++layer) {
                    const SDL_Color c = layer == 0 ? kBg : kPurple;
                    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
                    if (held[layer]) SDL_RenderFillRects(r, rects[layer], held[layer]);
                    held[layer] = 0;
                }
            };
            for (int bar = 0; bar < cellBars(); ++bar) {
                int shifts[kGlideShifts];
                const int count = glideShifts(bar, project_.bar, cellBars(), project_.sceneSteps(project_.scene), rows, shifts);
                const NotePattern& in = project_.current().notes[project_.scene][bar];
                for (int slot = 0; slot < kMaxNotes; ++slot) {
                    const NoteEvent& n = in.notes[slot];
                    const GlideLine& line = lines[bar * kMaxNotes + slot];
                    if (n.velocity <= 0.f || line.from < 0) continue;
                    for (int k = 0; k < count; ++k) {
                        const GlideSpan span = glideSpan(static_cast<double>(n.step) + n.nudge, line.steps, shifts[k], rows);
                        if (!span.seen) continue;
                        const double x0 = gx(line.from) + cellW() / 2.0, x1 = gx(n.pitch) + cellW() / 2.0;
                        const double y0 = top + 1 + span.from * kRowH, y1 = top + 1 + span.to * kRowH;
                        // A dark edge, so it stands clear of the notes it crosses. (A line's edge is over the line before it.)
                        for (int layer = 0; layer < 2; ++layer)
                            glideLineRects(x0, y0, x1, y1, layer == 0 ? 4 : 2, left, top, right, bottom, [&](int x, int y, int w, int h) {
                                if (held[layer] == kMost) flush(); // (never: a line has fewer squares inside the roll)
                                rects[layer][held[layer]++] = SDL_Rect{x, y, w, h};
                            });
                        flush();
                    }
                }
            }
        }
    }
    if (picking_ && editing_ < 0) drawChord(inHand(), false, true); // where it will land
    outline(r, gx(pitch_) - 1, gy(step_) - 1, cellW() + 2, kCellH + 2, cursorColor, 2);
    const int playStep = session_.playScene() == project_.scene && session_.engine.playBar() % cellBars() == project_.bar ? session_.engine.playStep() : -1;
    if (playStep >= 0) fill(r, kGridLeft, gy(playStep), visibleKeys() * keyW() - 2, kCellH, faded(kPurple, 76));
    // A scene of fewer than sixteen steps: the steps past its end are never played, and are shaded.
    if (const int steps = project_.barSteps(project_.scene, project_.bar); steps < kSteps)
        fill(r, 0, gy(steps) - 1, kGridLeft + visibleKeys() * keyW(), (kSteps - steps) * kRowH, faded(kBg, 185));

    // ---- what is under the cursor ---------------------------------------------------
    const int stripY = gridTop() + kSteps * kRowH + kStripGap;
    const auto field = [&](float fx, const char* label, const char* value, SDL_Color color) {
        fonts.small.draw(r, fx, stripY + 8.f, label, kDim);
        fonts.large.draw(r, fx, stripY + 26.f, value, color);
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
        const int shown = count < 7 ? count : 7, tileW = 88, tileH = 30, tileY = stripY + 15;
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
        const bool chordFx = fxHeld_ && !under && chordUnder; // X is held on a chord: the strip is its six effects
        if (chordFx) {
            // The one picked, in full: its name and its value.
            const ChordEvent& c = *chordUnder;
            chordEffectValue(c, fxPick_, text, sizeof text);
            field(kChordFxPickedX, chordEffectName(fxPick_), text, chordEffectWaits(c, fxPick_) ? kDim : chordEffectSet(c, fxPick_) ? kBrass : kText);
            // All six, the picked one marked. One that waits on another (ORDER with no STRUM) is dimmer still.
            for (int effect = 0; effect < kChordEffects; ++effect) {
                const float x = kChordFxListX + static_cast<float>(effect / 3) * kChordFxColumn;
                const float y = static_cast<float>(stripY + kChordFxRowTop + (effect % 3) * kChordFxRowH);
                const bool picked = effect == fxPick_, waits = chordEffectWaits(c, effect), set = chordEffectSet(c, effect);
                if (picked) fill(r, static_cast<int>(x) - 8, static_cast<int>(y) - 1, 3, fonts.small.capHeight() + 2, kBrass);
                // (One that waits is dim; picked all the same, it is still to be read.)
                fonts.small.draw(r, x, y, chordEffectName(effect), waits ? (picked ? kDim : kLine) : picked ? kText : kDim);
                chordEffectValue(c, effect, text, sizeof text);
                fonts.small.draw(r, x + kChordFxValueIn[effect / 3], y, text, waits ? (picked ? kDim : kLine) : set ? (picked ? kBrass : kText) : kDim);
            }
            field(kChordFxThrowX, "THROW", throwName(chordUnder->throwTo), chordUnder->throwTo ? kBrass : kText);
        } else if (chordUnder) {
            field(8.f, "CHORD", chordUnder->name, kPurple);
        } else {
            noteName(pitch_, text, sizeof text);
            field(8.f, "KEY", text, kText);
        }
        std::snprintf(text, sizeof text, "%d", project_.bar * kSteps + step_ + 1); // as the numbers at the left read
        if (!chordFx) field(100.f, "STEP", text, kText);
        const int ratchetHere = under ? under->ratchet : (chordUnder ? chordUnder->ratchet : 0);
        const int glideKindHere = under ? glideKind(under->glide) : kGlideOff;
        const bool glideHere = glideKindHere != kGlideOff;
        const int throwHere = under ? under->throwTo : (chordUnder ? chordUnder->throwTo : 0);
        const bool chordPlaysHere = !under && chordUnder && chordPlays(*chordUnder);
        if (chordFx) {
            // (drawn above)
        } else if (fxHeld_ && (under || chordUnder)) {
            // X is held: the strip is what the note does besides play.
            std::snprintf(text, sizeof text, ratchetHere >= 2 ? "x%d" : "OFF", ratchetHere);
            field(kStripHeldRatchetX, "RATCHET", text, ratchetHere >= 2 ? kBrass : kText);
            field(kStripHeldGlideX, "GLIDE", !under ? "-" : glideName(glideKindHere), !under ? kDim : glideHere ? kBrass : kText);
            field(kStripHeldThrowX, "THROW", throwName(throwHere), throwHere ? kBrass : kText);
            if (under) {
                // GLIDE FROM, the track's: dim while this note has no glide for it to say anything of.
                const int fromHere = glideFromKind(project_.current().glideFrom);
                const char* from = glideFromName(fromHere);
                const Font* sizes[3] = {&fonts.large, &fonts.medium, &fonts.small};
                const float widths[3] = {fonts.large.width(from), fonts.medium.width(from), fonts.small.width(from)};
                const Font& font = *sizes[stripFont(widths, kStripRight - kStripHeldFromX)];
                fonts.small.draw(r, kStripHeldFromX, stripY + 8.f, "GLIDE FROM", kDim);
                font.draw(r, kStripHeldFromX, stripY + 26.f + fonts.large.capHeight() - font.capHeight(), from, !glideHere ? kDim : fromHere != kFromTop ? kBrass : kText);
            }
        } else if (under || chordUnder) {
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(levelNow() * 100.f)));
            field(160.f, "LEVEL", text, shaping_ && !turningLength_ ? kBrass : (shaping_ ? kText : kTeal));
            const float length = lengthNow();
            if (length == std::floor(length)) std::snprintf(text, sizeof text, "%d STEP%s", static_cast<int>(length), length == 1.f ? "" : "S");
            else std::snprintf(text, sizeof text, "%.2f STEPS", length);
            field(250.f, "LENGTH", text, shaping_ && turningLength_ ? kBrass : (shaping_ ? kText : kTeal));
        } else {
            field(160.f, "LEVEL", "-", kDim);
            field(250.f, "LENGTH", "-", kDim);
        }
        const float nudge = under ? under->nudge : (chordUnder ? chordUnder->nudge : 0.f);
        float effectEnd = 0.f; // where the effect's value ends, and a gap (0: there is none)
        if (!fxHeld_ && chordPlaysHere) {
            // A chord with chord effects: the first of them, and how many more ("STRUM +2"). Where
            // that does not fit even in the small size, how many are on.
            chordEffectLine(*chordUnder, text, sizeof text);
            const float room = stripGlideRoom(throwHere != 0);
            if (fonts.small.width(text) > room) {
                int on = 0;
                for (int effect = 0; effect < kChordEffects; ++effect) on += chordEffectSet(*chordUnder, effect) && !chordEffectWaits(*chordUnder, effect);
                std::snprintf(text, sizeof text, "%d ON", on);
            }
            const Font* sizes[3] = {&fonts.large, &fonts.medium, &fonts.small};
            const float widths[3] = {fonts.large.width(text), fonts.medium.width(text), fonts.small.width(text)};
            const int size = stripFont(widths, room);
            const float x = stripEffectX(throwHere != 0);
            fonts.small.draw(r, x, stripY + 8.f, "EFFECTS", kDim);
            sizes[size]->draw(r, x, stripY + 26.f + fonts.large.capHeight() - sizes[size]->capHeight(), text, kPurple);
            effectEnd = x + widths[size] + kStripSpace;
        } else if (!fxHeld_ && (ratchetHere >= 2 || glideHere)) {
            // In a line, what X held would show in full.
            // A glide alone says how long it is; beside a ratchet there is only room to say it is there.
            std::snprintf(text, sizeof text, "%s%s%s", ratchetHere >= 2 ? (ratchetHere == 2 ? "x2" : ratchetHere == 3 ? "x3" : "x4") : "", ratchetHere >= 2 && glideHere ? " " : "", !glideHere ? "" : ratchetHere >= 2 ? "GLIDE" : glideName(glideKindHere));
            const Font* sizes[3] = {&fonts.large, &fonts.medium, &fonts.small};
            const float widths[3] = {fonts.large.width(text), fonts.medium.width(text), fonts.small.width(text)};
            const int size = ratchetHere >= 2 ? 0 : stripFont(widths, stripGlideRoom(throwHere != 0)); // (a ratchet's line is as it always was)
            const float x = stripEffectX(throwHere != 0);
            fonts.small.draw(r, x, stripY + 8.f, ratchetHere >= 2 ? "RATCHET" : "GLIDE", kDim);
            sizes[size]->draw(r, x, stripY + 26.f + fonts.large.capHeight() - sizes[size]->capHeight(), text, kPurple);
            effectEnd = x + widths[size] + kStripSpace;
        }
        if (!fxHeld_ && throwHere) {
            // Up to the right margin and no further: moved left, or drawn smaller beside a long effect.
            const char* to = throwName(throwHere);
            const Font* sizes[3] = {&fonts.large, &fonts.medium, &fonts.small};
            const float widths[3] = {fonts.large.width(to), fonts.medium.width(to), fonts.small.width(to)};
            float x = kStripThrowX;
            const Font& font = *sizes[stripThrowFont(widths, effectEnd, &x)];
            fonts.small.draw(r, x, stripY + 8.f, "THROW", kDim);
            font.draw(r, x, stripY + 26.f + fonts.large.capHeight() - font.capHeight(), to, kPurple);
        }
        if (chordFx || (fxHeld_ && under)) {
            // (the chord's effects have the strip, or GLIDE FROM has NUDGE's place: the nudge is there again when X is let go)
        } else if (nudge != 0.f) {
            std::snprintf(text, sizeof text, "%d%% %s", static_cast<int>(std::round(std::fabs(nudge) * 100.f)), nudge < 0.f ? "EARLY" : "LATE");
            // Beside an effect and a THROW there is less room: the value is drawn smaller, never over them.
            const bool effectHere = !fxHeld_ && (ratchetHere >= 2 || glideHere || chordPlaysHere);
            const Font* sizes[3] = {&fonts.large, &fonts.medium, &fonts.small};
            const float widths[3] = {fonts.large.width(text), fonts.medium.width(text), fonts.small.width(text)};
            const float nudgeX = kStripNudgeX;
            const Font& font = *sizes[stripFont(widths, stripNudgeRoom(effectHere, throwHere != 0))];
            fonts.small.draw(r, nudgeX, stripY + 8.f, "NUDGE", kDim);
            font.draw(r, nudgeX, stripY + 26.f + fonts.large.capHeight() - font.capHeight(), text, shaping_ ? kText : kTeal);
        } else if (!fxHeld_) {
            std::snprintf(text, sizeof text, "%d / %d", pat().used(), kMaxNotes);
            field(kStripNudgeX, "NOTES", text, kDim);
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
    } else if (fxHeld_ && !under && chordUnder) {
        // Each button under what it changes, with no word: LEFT / RIGHT sets the one picked, under
        // its large value at the left (right whichever of the six is picked); UP / DOWN picks one,
        // under the list; A under THROW. The dice roll all six and stand alone at the right-hand
        // end (owner, 2026-11-13).
        const HintUnder below[] = {{kChordFxSetX, "LEFT / RIGHT"}, {kChordFxListX, "UP / DOWN"}, {kChordFxThrowX, "A"}};
        hintsUnder(r, fonts, "Y", below, 3, true);
        return;
    } else if (fxHeld_ && under) {
        // Each button under the field it sets.
        const HintUnder below[] = {{kStripHeldRatchetX, "LEFT / RIGHT"}, {kStripHeldGlideX, "UP / DOWN"}, {kStripHeldThrowX, "A"}, {kStripHeldFromX, "L1 / R1"}};
        hintsUnder(r, fonts, "Y", below, 4);
        return;
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
        hint("HOLD Y", pat().chordAt(step_) >= 0 ? "swap" : "chord");
        hint("HOLD R1", under || chordUnder ? "move" : "paint");
        if (under || chordUnder) hint("HOLD X", "effects");
        if (!under && !chordUnder) hint("HOLD B", "delete"); // (on a note, B's own hint says it)
        hint("L1", "zoom");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
