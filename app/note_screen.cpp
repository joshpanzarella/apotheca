#include "note_screen.hpp"

#include "note_marks.hpp"
#include "scales.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace apotheca {

static_assert(NoteScreen::kLowest == kRollLowest && NoteScreen::kHighest == kRollHighest, "TRANSPOSE keeps a cell on the roll's keys");

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
constexpr float kNewLevel = kPlacedLevel, kMinLevel = 0.05f; // a note or a chord placed (project.hpp)
// Values turned by the stick, however far it is pushed (times STICK SPEED): a note's level and length with A held, and
// a carried note slid freely in time with R1 held. Their own rates, not twice a held D-pad's: the D-pad's UP / DOWN go
// to the step lines, the stick to any point in a step (owner, 2026-11-25: "d-pad for detailed ones").
constexpr float kFillPerSecond = 3.f, kLengthStepsPerSecond = 10.f, kCarryStepsPerSecond = 6.f;
constexpr float kShortestLength = 0.1f; // of a step
constexpr float kMostNudge = 0.5f;      // half a step early or late

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
    if (onBars_) return nullptr; // the cursor is on the BARS stop, above the grid
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
    if (onBars_) return nullptr; // the cursor is on the BARS stop, above the grid
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
    onBars_ = false;
    pitch_ = pitch < kLowest ? kLowest : (pitch > kHighest ? kHighest : pitch);
    // A step past either end of the bar is in the next bar of the cell, or
    // the one before (its FILL after its last, once made); past the ends it comes round.
    const TrackData& cell = project_.current();
    if (step < 0) project_.bar = cell.barAfter(project_.scene, project_.bar, -1 - (-step - 1) / kSteps), step = kSteps - 1 - (-step - 1) % kSteps;
    if (step >= kSteps) project_.bar = cell.barAfter(project_.scene, project_.bar, step / kSteps), step %= kSteps;
    step_ = step;
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

void NoteScreen::stepBars(int by) {
    static const int lengths[4] = {1, 2, 4, 8};
    int now = 0;
    for (int i = 0; i < 4; ++i)
        if (lengths[i] <= cellBars()) now = i;
    const int to = now + by;
    if (to < 0 || to > 3) return;
    session_.setCellBars(project_.track, project_.scene, lengths[to]);
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

// FOLLOW has shown a bar: the keys go to where its notes are, the most of them and centred on them (follow_bar.hpp,
// keysToShow). A bar with none leaves them be. The cursor keeps its step and, if it is no longer in view, goes to the
// nearest key shown, so A places among the notes.
void NoteScreen::followTheNotes() {
    std::vector<HeldNote> held;
    const NotePattern& p = static_cast<const NoteScreen*>(this)->pat(); // (read only: the writable one makes a scene's piece)
    for (const NoteEvent& n : p.notes)
        if (n.velocity > 0.f) held.push_back({n.pitch, n.length});
    for (const ChordEvent& c : p.chords) {
        if (c.count <= 0) continue;
        int keys[kChordNotes];
        const int count = c.pitches(keys);
        for (int k = 0; k < count; ++k) held.push_back({keys[k], c.length});
    }
    const int keys = visibleKeys();
    const int low = keysToShow(held, keys, kLowest, kHighest, low_);
    if (low < 0) return;
    low_ = low;
    shownKeys_ = keys;
    if (pitch_ < low_) pitch_ = low_;
    if (pitch_ > low_ + keys - 1) pitch_ = low_ + keys - 1;
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
    int dp = direction == Button::Right ? 1 : direction == Button::Left ? -1 : 0;
    if (zooming_ && dp != 0) { // L1 held: an octave at a time, a note or a chord
        zoomUsed_ = true;      // (letting go of L1 is no zoom: it was a carry)
        carryBy(dp * 12, 0);
        return;
    }
    // A note carried sideways with a SCALE on goes to the next key of the scale (a chord a semitone, as before).
    if (dp != 0 && carrying_ == Carry::Note && project_.scaleKind != kScaleOff) {
        const int from = pat().notes[carried_].pitch;
        int to = from + dp;
        while (to >= kLowest && to <= kHighest && !inScale(project_.scaleKey, project_.scaleKind, to)) to += dp;
        if (to < kLowest || to > kHighest) return;
        dp = to - from;
    }
    carryBy(dp, direction == Button::Down ? 1 : direction == Button::Up ? -1 : 0);
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

bool NoteScreen::keyStop(int pitch) const {
    if (project_.scaleKind == kScaleOff || inScale(project_.scaleKey, project_.scaleKind, pitch)) return true;
    const NotePattern& p = pat();
    for (const NoteEvent& n : p.notes)
        if (n.velocity > 0.f && n.pitch == pitch) return true;
    for (const ChordEvent& c : p.chords) {
        if (c.velocity <= 0.f) continue;
        int keys[kChordNotes];
        const int count = c.pitches(keys);
        for (int i = 0; i < count; ++i)
            if (keys[i] == pitch) return true;
    }
    return false;
}

int NoteScreen::nextKey(int from, int way) const {
    for (int p = from + way; p >= kLowest && p <= kHighest; p += way)
        if (keyStop(p)) return p;
    return from;
}

ChordShape NoteScreen::shapeAt(int index) const {
    const ChordPack& p = pack();
    ChordShape shape = p.chords[static_cast<std::size_t>(index) % p.chords.size()];
    const int by = packShift(p.tonic, p.minor, project_.scaleKey, project_.scaleKind);
    if (by != 0) {
        shape.rootKey = ((shape.rootKey + by) % 12 + 12) % 12;
        transposeChordName(shape.name, by);
    }
    return shape;
}

void NoteScreen::moveCursor(Button direction) {
    if (onBars_) {
        // BARS sits above the grid; down goes back to it, up comes round to the bottom.
        // Left and right show the bar before or after, round the ends.
        if (direction == Button::Left || direction == Button::Right) {
            project_.bar = project_.current().barAfter(project_.scene, project_.bar, direction == Button::Right ? 1 : -1); // (its FILL after its last)
        } else if (direction == Button::Down) {
            onBars_ = false;
        } else {
            onBars_ = false;
            project_.bar = 0;
            moveCursorTo(pitch_, -1); // past the cell's start: round to its last step
        }
        return;
    }
    if (carrying_ != Carry::Nothing) return carry(direction);
    if (direction == Button::Right) moveCursorTo(nextKey(pitch_, 1), step_); // (with a SCALE on: the next key of it)
    if (direction == Button::Left) moveCursorTo(nextKey(pitch_, -1), step_);
    if (direction == Button::Up && project_.bar == 0 && step_ == 0) onBars_ = true; // up from the top step: BARS
    else if (direction == Button::Up) moveCursorTo(pitch_, step_ - 1);
    if (direction == Button::Down) moveCursorTo(pitch_, step_ + 1);
    if (painting_ && !onBars_) paintHere();
    if (erasing_ && !onBars_) eraseHere();
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
    int room = barsToEnd() * kSteps - start; // to the end of the cell: a note may run on over the bar lines (on the FILL: its end)
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
    // From anywhere along it (owner, 2026-11-17: "it's kind of a pain to have to go all the way back to
    // the top left"): shortened past the cursor's step, the cursor comes back with its end.
    length = length < kShortestLength ? kShortestLength : (length > static_cast<float>(room) ? static_cast<float>(room) : length);
    if (slot < 0) handLength_ = length; // the next chord placed will be this long
    if (length == now) return;
    now = length;
    const int last = start + static_cast<int>(std::ceil(length)) - 1; // the last step it is in
    if (step_ > last) step_ = last < start ? start : last;
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
    if (by < 0 && to < 1.f) return; // already as short as whole steps go
    setLength(to);
}

// ---- chords ---------------------------------------------------------------------------

ChordEvent NoteScreen::inHand() const {
    ChordEvent c = makeChord(shapeAt(hand_), pitch_, step_, handLength_, kNewLevel, inversion_);
    // It stops at the end of the bar, or where the next chord starts.
    const float most = static_cast<float>(barsToEnd() * kSteps - c.step); // to the end of the cell (on the FILL: its end)
    if (c.length > most) c.length = most;
    for (const ChordEvent& other : pat().chords) {
        if (other.velocity > 0.f && other.step > c.step && other.step - c.step < c.length) c.length = other.step - c.step;
    }
    return c;
}

void NoteScreen::hearChord(const ChordEvent& chord) {
    // While choosing a chord (Y held) it is always heard, playing or not: that is how it is chosen.
    // Otherwise (a chord placed or moved), with the song playing, the song will play it soon enough. X on a
    // chord is not this: it holds the chord as long as X is, over the song too (onButton, gesture audit G33).
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
    // As it all was when Y went down: B puts it back (G26).
    pickWas_.packIndex = session_.packIndex();
    pickWas_.hand = hand_, pickWas_.inversion = inversion_;
    pickWas_.handLength = handLength_;
    if (editing_ >= 0) pickWas_.chord = pat().chords[editing_];
    const int count = static_cast<int>(pack().chords.size());
    hand_ = ((hand_ % count) + count) % count;
    if (editing_ >= 0) {
        // Start from the chord that is there, if the pack has it.
        const ChordEvent& c = pat().chords[editing_];
        inversion_ = c.inversion;
        for (int i = 0; i < count; ++i)
            if (std::strcmp(shapeAt(i).name, c.name) == 0) { hand_ = i; break; }
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
    ChordEvent swapped = makeChord(shapeAt(hand_), c.root, c.step, c.length, c.velocity, inversion_);
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

// With Y held, L2 / R2 (gesture audit G27, 2026-11-24): the chord in hand half or twice as long, along
// 1, 2, 4, 8 and 16 steps. A length between two (the stick sets any) goes to the next one that way; the
// ends stop. A new chord: the length it and the ones after it take (handLength_, as hold A sets it), held
// to the room where it lands as ever (inHand). A chord being swapped: its own length, held to the end of
// the cell and the next chord as hold A holds it (setLength), and the next chord placed takes it too. The
// box above the bar says what lands.
void NoteScreen::doubleHand(int way) {
    static const float ladder[] = {1.f, 2.f, 4.f, 8.f, 16.f};
    constexpr int rungs = static_cast<int>(sizeof ladder / sizeof ladder[0]);
    const float now = editing_ >= 0 ? pat().chords[editing_].length : handLength_;
    float to = -1.f;
    if (way > 0) {
        for (int i = 0; i < rungs && to < 0.f; ++i)
            if (ladder[i] > now + 0.001f) to = ladder[i];
    } else {
        for (int i = rungs - 1; i >= 0 && to < 0.f; --i)
            if (ladder[i] < now - 0.001f) to = ladder[i];
    }
    if (to < 0.f) return;
    float lands = to;
    if (editing_ >= 0) {
        ChordEvent& c = pat().chords[editing_];
        float room = static_cast<float>(barsToEnd() * kSteps - c.step); // to the end of the cell (on the FILL: its end)
        for (const ChordEvent& other : pat().chords)
            if (other.velocity > 0.f && other.step > c.step && static_cast<float>(other.step - c.step) < room) room = static_cast<float>(other.step - c.step);
        lands = to > room ? room : to;
        if (lands == c.length) return; // (it fills its room already)
        c.length = lands;
        handLength_ = lands;
        const int last = c.step + static_cast<int>(std::ceil(lands)) - 1; // the cursor stays inside it
        if (step_ > last) step_ = last < c.step ? c.step : last;
        resend();
    } else {
        handLength_ = to;
        lands = inHand().length;
    }
    char text[48];
    const int whole = static_cast<int>(std::lround(to)), here = static_cast<int>(std::lround(lands));
    // (Short: "CHORD 16 STEPS, 15 FIT HERE" ran past the box's room, 248 pixels, even held at LARGE.)
    if (here < whole) std::snprintf(text, sizeof text, "CHORD %d STEPS: %d FIT", whole, here);
    else std::snprintf(text, sizeof text, whole == 1 ? "CHORD %d STEP" : "CHORD %d STEPS", whole);
    session_.say(text);
}

// B while Y is held on a chord (gesture audit G26, 2026-11-25): the chord as it was when Y went down, its
// shape, inversion and length, and the pack and the chord in hand with it, as B with A held puts back what
// the KIT stop, the list of effects and a MOD's target were stepped through. It broke the chord into
// loose notes; that is Y held + A now.
// (A new chord's B only calls it off, as before: nothing landed, and the pack and chord stepped to stay
// in hand for the next tap of Y.)
void NoteScreen::putPickBack() {
    if (editing_ < 0) return;
    if (session_.packIndex() != pickWas_.packIndex) session_.choosePack(pickWas_.packIndex);
    hand_ = pickWas_.hand, inversion_ = pickWas_.inversion;
    handLength_ = pickWas_.handLength;
    ChordEvent& c = pat().chords[editing_];
    if (std::memcmp(&c, &pickWas_.chord, sizeof c) == 0) return; // nothing was changed
    c = pickWas_.chord;
    resend();
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

// With X held on a note or a chord, B empties what X shows (gesture audit G11, 2026-11-24): a note's
// RATCHET, GLIDE and THROW; a chord's six effects and its THROW. The note or the chord stays, its level,
// length and nudge as they were (plain B still deletes it, once X is let go). One step of undo of its
// own; with nothing to empty, nothing happens and there is no step. GLIDE FROM is the track's: it stays.
void NoteScreen::fxEmpty() {
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    if (slot < 0 && chord < 0) return;
    if (slot >= 0) {
        const NoteEvent& n = pat().notes[slot];
        if (n.ratchet == 0 && n.glide == 0 && n.throwTo == 0) return;
    } else {
        const ChordEvent& c = pat().chords[chord];
        if (c.ratchet == 0 && c.throwTo == 0 && c.play.atRest() && c.play.order == kOrderUp) return;
    }
    session_.beginOwnUndoStep();
    if (slot >= 0) {
        NoteEvent& n = pat().notes[slot];
        n.ratchet = 0;
        n.glide = 0;
        n.throwTo = 0;
    } else {
        ChordEvent& c = pat().chords[chord];
        c.ratchet = 0;
        c.throwTo = 0;
        c.play.strum = kStrumOff;
        c.play.order = kOrderUp;
        c.play.time = c.play.level = c.play.lengths = 0;
    }
    session_.sendNoteBar(project_.track, project_.scene, project_.bar);
    session_.endOwnUndoStep();
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
    if (onBars_) {
        // BARS: up and down with A held set the cell's count; the rest (the bar before or
        // after, off the stop) is the cursor's.
        if (shaping_ && (b == Button::Up || b == Button::Down)) stepBars(b == Button::Up ? 1 : -1);
        else moveCursor(b);
        return;
    }
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

    // R1 let go with L1 held first copies the bar, but only a tap does: anything else first means it was not one.
    if (copyBarArmed_ && event.down && b != Button::R1 && !isDirection(b)) copyBarArmed_ = false;
    // L1's own tap (the zoom) acts on its let go, and only if nothing else was pressed meanwhile: any
    // button, as the drum grid's slices (review 2026-11-23: B, X, Y or START with L1 held still zoomed).
    if (event.down && zooming_ && b != Button::L1) zoomUsed_ = true;

    if (effectsButton(event)) return;
    // L1 held (pressed first) + Y: the FILL (Session::fillKeyHere), caught before Y's own block. Y was pressed for
    // that: no chord in hand while it stays down, so none is placed when it is let go (endPick: nothing picked).
    if (event.down && l1First_ && input.held(Button::L1) && !input.held(Button::X) && b == Button::Y) {
        session_.fillKeyHere();
        return;
    }
    if (b == Button::Y) {
        if (event.down && !shaping_ && !painting_ && !onBars_) beginPick();
        else if (!event.down) endPick();
        return;
    }
    if (picking_) {
        pickButton(event);
        return;
    }

    if (b == Button::L2 || b == Button::R2) {
        // The app sends these here only while A is held: nudge, early or late.
        if (event.down && shaping_ && !onBars_) nudgeBy(b == Button::R2 ? 0.1f : -0.1f);
        return;
    }
    // L1 held (pressed first): A pastes over the bar shown. R1 takes hold as always instead;
    // let go without moving, it copies the bar (a tap copies, a carry carries).
    if (event.down && l1First_ && input.held(Button::L1) && !input.held(Button::X) && b == Button::A) {
        session_.pasteBarHere();
        zoomUsed_ = true; // (the tap's zoom does not happen as well)
        return;
    }
    if (b == Button::A) {
        if (event.down && !onBars_ && !chordHere()) placeNote(kNewLevel); // on a chord, A takes hold of the chord
        shaping_ = event.down;
        barsPushed_ = false; // (a new hold: a push already there is a click on BARS, as on a synth's choice)
        axis_ = Axis::None;
        return;
    }
    if (b == Button::L1) {
        l1Button(event);
        return;
    }
    if (barKeyButton(event, input)) return;
    if (b == Button::R1) {
        r1Button(event, input);
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        if (b == Button::B) erasing_ = false;
        // X let go with the stick still pushed for what it does: the stick moves nothing until it has come
        // back, as on FX and DEVICE (gesture audit G14), so the cursor does not jump off the note.
        if (b == Button::X && fxHeld_) stickWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
        if (b == Button::X) fxHeld_ = false;
        return;
    }

    if (isDirection(b)) {
        // A direction with L1 held first is a use of it, as moving bars was: letting go zooms nothing.
        if (zooming_ && l1First_ && !shaping_ && !painting_ && !erasing_ && !picking_ && !fxHeld_ && carrying_ == Carry::Nothing && !input.held(Button::R1)) zoomUsed_ = true;
        press(b);
        repeating_ = b;
        repeat_.restart();
        return;
    }
    if (b == Button::B && !input.held(Button::A) && !onBars_) {
        // Deletes what is under the cursor, and, held, whatever the cursor is dragged over.
        erasing_ = true;
        eraseHere();
    } else if (b == Button::X && !onBars_) {
        holdX(input);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

// With X held on a note or a chord, Y, A, L1 / R1 and B set what it does: true when the press was theirs.
bool NoteScreen::effectsButton(const ButtonEvent& event) {
    const Button b = event.button;
    if (fxHeld_ && b == Button::Y) {
        if (event.down) fxRoll(); // the dice, for what this note does. (Chords wait until X is let go.)
        return true;
    }
    if (fxHeld_ && b == Button::A) {
        if (event.down) fxThrow(); // THROW, a step round. (Placing and shaping wait until X is let go.)
        return true;
    }
    if (fxHeld_ && (b == Button::L1 || b == Button::R1) && noteHere()) {
        if (event.down) fxFrom(b == Button::R1 ? 1 : -1); // GLIDE FROM, a click. (Zoom and carry wait until X is let go.)
        return true;
    }
    if (fxHeld_ && b == Button::B) {
        if (event.down) fxEmpty(); // what X shows, emptied. (B's own delete waits until X is let go.)
        return true;
    }
    return false;
}

// Y is held: choosing a chord.
void NoteScreen::pickButton(const ButtonEvent& event) {
    const Button b = event.button;
    // Y has the screen: only the chord's own buttons do anything.
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
    } else if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeat_.restart();
    } else if (b == Button::L1 || b == Button::R1) {
        session_.choosePack(session_.packIndex() + (b == Button::R1 ? 1 : -1));
        hand_ = 0;
        pickChanged();
    } else if (b == Button::L2 || b == Button::R2) {
        doubleHand(b == Button::R2 ? 1 : -1); // (the App sends them here while Y is held on the roll)
    } else if (b == Button::X) {
        // Heard for as long as X is held (a tap: a moment), over the song too, as X on a chord placed (review
        // of G33: it was hearChord's fixed moment a key).
        const ChordEvent chord = editing_ >= 0 ? pat().chords[editing_] : inHand();
        int keys[kChordNotes];
        const int count = chord.pitches(keys);
        session_.auditionHeld(project_.track, keys, count, chord.velocity, this);
    } else if (b == Button::B) {
        putPickBack(); // as it was when Y went down (G26: it broke the chord up)
        picking_ = false;
    } else if (b == Button::A && editing_ >= 0) {
        breakUp(editing_); // Y held + A: into loose notes, as the chord is now (G26: it was B)
        picking_ = false;
    }
}

void NoteScreen::l1Button(const ButtonEvent& event) {
    // Zoom: a tap steps closer (2 octaves, 1, then round to 3). Held, R1 copies the bar
    // and A pastes over it; a direction only moves the cursor, and none of it zooms too.
    if (event.down) {
        zooming_ = true, zoomUsed_ = false;
        l1First_ = !shaping_ && !painting_ && !erasing_ && carrying_ == Carry::Nothing;
    } else {
        // A tap steps the zoom; held for an octave carry (or anything else), letting go zooms nothing.
        if (zooming_ && !zoomUsed_ && l1First_) zoomTo(session_.octavesShown() == 1 ? 3 : session_.octavesShown() - 1);
        zooming_ = false;
        l1First_ = false;
    }
}

// Hold L1 + a direction: the bars. True when the press was the bar key's.
bool NoteScreen::barKeyButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    // (The bar shown and the cell's count live on the BARS stop too, above the grid.)
    // Hold L1, up / down: the bar before or after, round the ends (the roll runs down, so down
    // is the next bar). Only with L1 held first and nothing else going: shaping, painting, carrying,
    // the chord in hand, erasing and what X holds keep the keys, and R1 held is a bar copy on its way.
    // Hold L1, left / right: the cell's bars, more or fewer (as the Song screen's hold A).
    if (zooming_ && l1First_ && event.down && !shaping_ && !painting_ && !erasing_ && !picking_ && !fxHeld_ && carrying_ == Carry::Nothing && !input.held(Button::R1) && !onBars_) {
        if (b == Button::Up || b == Button::Down) {
            project_.bar = project_.current().barAfter(project_.scene, project_.bar, b == Button::Down ? 1 : -1); // (1 ... the last, its FILL, 1)
            zoomUsed_ = true; // (letting go of L1 zooms nothing: it moved bars)
            return true;
        }
        if (b == Button::Left || b == Button::Right) {
            stepBars(b == Button::Right ? 1 : -1);
            zoomUsed_ = true; // (letting go of L1 zooms nothing: it set the bars)
            return true;
        }
    }
    return false;
}

void NoteScreen::r1Button(const ButtonEvent& event, const Input& input) {
    // On a note or a chord, R1 takes hold of it: moving now carries it. On an empty step it is the
    // brush: every step crossed gets a note. (Hold X used to carry; X is kept for what a note does.)
    // With L1 held first, letting go unmoved copies the bar instead: nothing paints under L1.
    const int slot = pat().at(pitch_, step_), chord = chordSlotHere();
    const bool barTap = l1First_ && input.held(Button::L1) && !input.held(Button::X);
    carrying_ = Carry::Nothing;
    painting_ = false;
    if (event.down && !shaping_ && !erasing_ && !onBars_ && (slot >= 0 || chord >= 0)) {
        carrying_ = slot >= 0 ? Carry::Note : Carry::Chord;
        carried_ = slot >= 0 ? slot : chord;
        axis_ = Axis::None; // a new hold is a new choice of job for the stick
        if (barTap) {
            copyBarArmed_ = true;
            copyBarPitch_ = pitch_;
            copyBarStep_ = step_;
        }
    } else if (event.down && !shaping_ && !barTap && !onBars_) {
        painting_ = true;
        brush_ = kNewLevel;
        paintHere();
    } else if (event.down && barTap) {
        copyBarArmed_ = true;
        copyBarPitch_ = pitch_;
        copyBarStep_ = step_;
    } else if (!event.down && copyBarArmed_) {
        copyBarArmed_ = false;
        if (pitch_ == copyBarPitch_ && step_ == copyBarStep_) session_.copyBarHere();
    }
}

// X pressed: heard, and on a note or a chord the hold for what it does.
void NoteScreen::holdX(const Input& input) {
    // Heard for as long as X is held (a tap a moment), a chord's keys together, and over the song while it
    // plays (gesture audit G33: a chord was silent then, and a note or a key a fixed moment).
    if (const ChordEvent* chord = chordHere()) {
        int keys[kChordNotes];
        const int count = chord->pitches(keys);
        session_.auditionHeld(project_.track, keys, count, chord->velocity, this);
    } else {
        session_.auditionHeld(project_.track, pitch_, noteHere() ? noteHere()->velocity : kNewLevel, this);
    }
    // On a note or a chord it is also a hold: the stick or D-pad then sets what it does besides play.
    fxHeld_ = (noteHere() || chordHere()) && !shaping_ && !painting_ && !picking_ && carrying_ == Carry::Nothing;
    fxSaved_ = 0.f;
    fxWas_ = false;
    // A direction that was already held (the cursor was on its way here) is not a turn of the
    // effect: X only heard the note. It must be let go and pushed again.
    fxWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
    if (fxHeld_) repeating_ = Button::None;
}

void NoteScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) shaping_ = false;
    if (!input.held(Button::R1)) painting_ = false;
    if (!input.held(Button::B)) erasing_ = false;
    if (!input.held(Button::R1)) carrying_ = Carry::Nothing;
    if (!input.held(Button::R1)) copyBarArmed_ = false;
    if (!input.held(Button::X) || (!noteHere() && !chordHere())) fxHeld_ = false;
    if (!input.held(Button::L1)) zooming_ = false, l1First_ = false;
    if (!input.held(Button::Y)) picking_ = false; // (let go properly, the chord has landed already)

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    if (!project_.current().barThere(project_.scene, project_.bar)) project_.bar = 0; // the cell was shortened, or it is another cell (its FILL is a bar to stand on)
    float x = input.stickX(), y = input.stickY();
    {
        // The view follows the bar playing, unless the player is at the controls (follow_bar.hpp); the FILL
        // played is one of its bars, and the FILL looked at by hand holds the view.
        const bool here = project_.playing && project_.playScene == project_.scene && project_.playStep >= 0 && cellBars() > 0;
        const int at = follow_.step(here, here ? project_.cellBarPlaying(project_.track) : -1, input.anyHeld() || x != 0.f || y != 0.f, nowMs, project_.bar, onFill());
        if (at >= 0) {
            project_.bar = at;
            followTheNotes();
        }
    }
    // The note went from under X (an undo) with the stick pushed for it: the same wait as X let go (onButton).
    if (fxLastFrame_ && !fxHeld_ && input.held(Button::X) && (x != 0.f || y != 0.f)) stickWait_ = true;
    fxLastFrame_ = fxHeld_;
    if (stickWait_ && x == 0.f && y == 0.f) stickWait_ = false;
    if (stickWait_) x = y = 0.f;
    // A stick move with L1 held is a use of L1, as a button is: its let go zooms nothing (gesture audit G31).
    if (zooming_ && (x != 0.f || y != 0.f)) zoomUsed_ = true;
    // A key played on a USB keyboard: the cursor goes to it, so A places there.
    if (const int key = session_.takeMidiKey(); key >= 0 && !shaping_ && !picking_ && !painting_) moveCursorTo(key, step_);
    keepInView(); // the zoom can be changed from SETUP too

    if (fxHeld_) {
        // X is held on a note: the stick clicks what it does. One way per push; a click at once, then at twice the held
        // D-pad's pace (the clicks'), however far it is pushed.
        if (x == 0.f && y == 0.f) {
            axis_ = Axis::None;
            fxSaved_ = 0.f;
            fxWas_ = false;
            fxWait_ = false;
        } else if (!fxWait_) {
            if (axis_ == Axis::None) axis_ = std::fabs(y) >= std::fabs(x) ? Axis::UpDown : Axis::Sideways;
            const float push = axis_ == Axis::UpDown ? -y : x;
            if (push != 0.f) {
                fxSaved_ += fxWas_ ? ui::stickPerSecond(Pace::Click) * dt : 1.f; // (G32: the clicks' pace)
                fxWas_ = true;
                for (; fxSaved_ >= 1.f; fxSaved_ -= 1.f)
                    fxTurn(axis_ == Axis::UpDown ? (push > 0.f ? Button::Up : Button::Down) : (push > 0.f ? Button::Right : Button::Left));
            }
        }
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
        while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), Pace::Click)) fxTurn(repeating_); // clicks, not a run
        return;
    }
    if (picking_) {
        // The stick steps through chords (sideways) or inversions (up and down): at once on the first push, then
        // at twice the held D-pad's pace (the list's), however far it is pushed.
        const bool sideways = std::fabs(x) >= std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? ui::stickPerSecond(Pace::List) * dt : 1.f;
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
        if (onBars_) {
            // BARS with A held, a choice (1, 2, 4, 8): the stick steps the cell's bars once for each push up or down,
            // however far it is pushed (CLAUDE.md: choices click once per push; the owner's stick rule, 2026-11-25,
            // took out the "past half"). Held, it does no more: the drum grid's BARS the same.
            const bool pushed = axis_ == Axis::UpDown && y != 0.f;
            if (pushed && !barsPushed_) stepBars(y < 0.f ? 1 : -1);
            barsPushed_ = pushed;
        }
        // However far it is pushed, each at its own rate: the level (a fill) and the length.
        const bool here = !onBars_ && levelNow() > 0.f;
        if (here && axis_ == Axis::Sideways && x != 0.f) {
            setLevel(levelNow() + stickWay(x) * kFillPerSecond * dt);
            turningLength_ = false;
        } else if (here && axis_ == Axis::UpDown && y != 0.f) {
            // Freely: the end follows the stick, to any point in a step.
            setLength(lengthNow() + stickWay(y) * kLengthStepsPerSecond * dt);
            turningLength_ = true;
        }
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else if (carrying_ != Carry::Nothing) {
        // R1 has hold of a note or a chord. One job per push, as with A held: whichever way the stick
        // leaves the centre is the job until it comes back. Sideways it goes as the D-pad does
        // (carry(): a SCALE's next key; with L1 held an octave, once a push: at the stick's pace an
        // octave would fly), at twice the held D-pad's pace (the cursor's); up and down it slides freely in time,
        // at its own rate (a value turned: the D-pad's UP / DOWN carry it a whole step). Both however far it is
        // pushed (owner, 2026-11-25).
        if (x == 0.f && y == 0.f) axis_ = Axis::None;
        else if (axis_ == Axis::None) axis_ = std::fabs(y) >= std::fabs(x) ? Axis::UpDown : Axis::Sideways;
        if (axis_ != Axis::Sideways || x == 0.f) {
            stickX_ = 0.f, stickWasX_ = false;
        } else if (zooming_) {
            if (!stickWasX_) carry(x > 0.f ? Button::Right : Button::Left);
            stickX_ = 0.f, stickWasX_ = true;
        } else {
            stickX_ += stickWasX_ ? ui::stickPerSecond(Pace::Cursor) * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) carry(x > 0.f ? Button::Right : Button::Left);
        }
        if (axis_ == Axis::UpDown) carryFree(stickWay(y) * kCarryStepsPerSecond * dt);
        stickY_ = 0.f;
        stickWasY_ = false;
    } else {
        axis_ = Axis::None;
        // The keys and the steps: one at once, then at twice the held D-pad's pace (the cursor's), however far it is
        // pushed; a slight drift off one axis is not the other (ui::stickAxes).
        const StickAxes both = stickAxes(x, y);
        if (both.x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? ui::stickPerSecond(Pace::Cursor) * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) moveCursor(both.x > 0.f ? Button::Right : Button::Left);
        }
        if (both.y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? ui::stickPerSecond(Pace::Cursor) * dt : 1.f;
            stickWasY_ = true;
            for (; stickY_ >= 1.f; stickY_ -= 1.f) moveCursor(both.y > 0.f ? Button::Down : Button::Up);
        }
    }

    // A held D-pad: the cursor's pace (BARS with A held too); the chords and their inversions with Y held at the
    // list's, after the cursor's wait they always had: chords are for hearing one at a time.
    while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), Pace::Cursor, picking_ ? Pace::List : Pace::Cursor)) press(repeating_);
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
    // Always: which bar is shown reads at a glance, one bar or eight.
    std::snprintf(text, sizeof text, "%s / %s / SCENE %d", name, Session::cellBarWords(project_.current(), project_.scene, project_.bar).c_str(), project_.scene + 1); // (the FILL: FILL, FILL OFF)
    const TopBar top = topBar(r, fonts, View::Pattern, text, project_);
    if (onBars_) {
        // BARS is the readout itself, outlined where it drew (it shortens while playing).
        outline(r, static_cast<int>(top.wordsX) - 4, 2, static_cast<int>(top.wordsWidth) + 8, kTopBarH - 4, shaping_ ? kBrass : kTeal, 2);
    }
    const bool held = shaping_ || painting_ || picking_;
    const SDL_Color cursorColor = held ? kBrass : kTeal;

    drawKeyboard(r, fonts, cursorColor);
    drawGrid(r, fonts);

    // ---- notes --------------------------------------------------------------------------
    const NoteEvent* under = noteHere();
    const ChordEvent* chordUnder = chordHere();
    drawEarlierNotes(r);
    drawNotesAndChords(r, fonts, held, under, chordUnder);
    drawChordMarks(r, fonts, chordUnder);
    drawNotesInside(r, held, under);
    drawGlideLines(r);
    if (picking_ && editing_ < 0) drawChord(r, fonts, inHand(), false, true); // where it will land
    if (!onBars_) outline(r, keyX(pitch_) - 1, stepY(step_) - 1, cellW() + 2, kCellH + 2, cursorColor, 2);
    const int playStep = project_.playScene == project_.scene && project_.cellBarPlaying(project_.track) == project_.bar ? project_.playStep : -1;
    if (playStep >= 0) fill(r, kGridLeft, stepY(playStep), visibleKeys() * keyW() - 2, kCellH, faded(kPurple, 76));
    // A scene of fewer than sixteen steps: the steps past its end are never played, and are shaded.
    if (const int steps = project_.barSteps(project_.scene, sceneBar()); steps < kSteps)
        fill(r, 0, stepY(steps) - 1, kGridLeft + visibleKeys() * keyW(), (kSteps - steps) * kRowH, faded(kBg, 185));

    drawStrip(r, fonts, under, chordUnder, cursorColor);
    drawHints(r, fonts, under, chordUnder, held);
}

// Where a key's column and a step's row are on the roll, and whether a key is in view.
int NoteScreen::keyX(int pitch) const { return kGridLeft + (pitch - low_) * keyW(); }
int NoteScreen::stepY(int step) const { return gridTop() + step * kRowH; }
bool NoteScreen::keyShows(int pitch) const { return pitch >= low_ && pitch <= low_ + (visibleKeys() - 1); }

void NoteScreen::drawKeyboard(SDL_Renderer* r, const Fonts& fonts, SDL_Color cursorColor) const {
    char text[96];
    // ---- the keyboard, across the top ---------------------------------------------
    // Drawn as a keyboard is: the white keys one cream strip with lines
    // between them, the black keys dark bars hanging from the top edge. On a light theme the
    // background is light, so the keys take the lightest surface and the lettering's dark: a
    // keyboard drawn by job there (white keys dim, black keys the background) reads as a negative.
    const int keysW = visibleKeys() * keyW() - 2;
    const SDL_Color whiteKey = ui::lightTheme() ? kPanel2 : kDim, blackKey = ui::lightTheme() ? kText : kBg;
    fill(r, kGridLeft, kKeysTop, keysW, keysH(), whiteKey);
    for (int pitch = low_; pitch < low_ + visibleKeys(); ++pitch) {
        const bool black = isBlackKey(pitch), here = !onBars_ && pitch == pitch_;
        const int x = keyX(pitch);
        if (black) {
            // The two white keys meet under the middle of a black one.
            fill(r, x + cellW() / 2, kKeysTop + blackKeyH(), 1, keysH() - blackKeyH(), blackKey);
            fill(r, x + 1, kKeysTop, cellW() - 2, blackKeyH(), here ? cursorColor : blackKey);
        } else {
            if (here) fill(r, x, kKeysTop + blackKeyH() + 1, cellW(), keysH() - blackKeyH() - 1, cursorColor);
            // Two white keys side by side (E and F, B and C): a line all the way up.
            if (pitch > low_ && !isBlackKey(pitch - 1)) fill(r, x - 1, kKeysTop, 1, keysH(), blackKey);
        }
    }
    // ---- which octave: a band over each, from its C ------------------------------------
    for (int pitch = low_; pitch < low_ + visibleKeys(); ++pitch) {
        if (pitch != low_ && pitch % 12 != 0) continue;
        const int c = pitch - ((pitch % 12) + 12) % 12; // this octave's C, perhaps off to the left
        int end = c + 12 < low_ + visibleKeys() ? c + 12 : low_ + visibleKeys();
        const int x0 = keyX(pitch), x1 = keyX(end) - 3;
        const bool mine = !onBars_ && pitch_ >= c && pitch_ < c + 12;
        const SDL_Color color = mine ? cursorColor : kDim;
        fill(r, x0, kOctaveTop + kOctaveH - 2, x1 - x0, 1, color);
        if (pitch % 12 == 0) fill(r, x0, kOctaveTop + 2, 1, kOctaveH - 3, color); // the C end
        std::snprintf(text, sizeof text, x1 - x0 > 70 ? "OCTAVE %d" : "%d", c / 12 - 1);
        // (The words stand on the band's line, whatever their size: at LARGE that is kOctaveTop - 3.)
        if (x1 - x0 > 14) fonts.small.draw(r, x0 + 5.f, kOctaveTop + kOctaveH - 2.f - fonts.small.capHeight(), text, mine ? cursorColor : kText);
    }
}

void NoteScreen::drawGrid(SDL_Renderer* r, const Fonts& fonts) const {
    char text[96];
    // ---- the empty grid: a column for each key, a row for each step ------------------
    // The numbers at the left have the 25 pixels before the grid. Three figures at XL do not fit
    // them (bars 7 and 8 of a cell): such a bar's numbers are at LARGE, all sixteen of them.
    bool numbersFit = true;
    const int meter = project_.meterOf(project_.scene);
    for (int step = 0; step < kSteps; ++step) {
        const int number = sceneBar() * meter + step + 1;
        std::snprintf(text, sizeof text, "%d", number);
        numbersFit = numbersFit && fonts.small.width(text) <= kGridLeft - (number >= 100 ? 3.f : 7.f);
    }
    const Font& numberFont = numbersFit ? fonts.small : fonts.fixed().small;
    const bool scaleOn = project_.scaleKind != kScaleOff;
    for (int step = 0; step < kSteps; ++step) {
        // Counted through the cell: bar 2 reads 17 to 32. A step past the scene's end (a scene of
        // 25 steps: 26 to 32) is never played and reads "-".
        if (step < project_.barSteps(project_.scene, sceneBar())) std::snprintf(text, sizeof text, "%d", sceneBar() * meter + step + 1); // (the FILL: the scene's last bar's)
        else std::snprintf(text, sizeof text, "-");
        // Three figures (bars 7 and 8) need the room: they stand four pixels nearer the grid.
        numberFont.drawRight(r, kGridLeft - (sceneBar() * meter + step + 1 >= 100 ? 3.f : 7.f), stepY(step) + (kCellH - numberFont.capHeight()) / 2.f, text,
                              step == step_ ? kTeal : (step % 4 == 0 ? kText : kDim));
        for (int pitch = low_; pitch < low_ + visibleKeys(); ++pitch) {
            const SDL_Color inside = isBlackKey(pitch) ? kBg : (step % 4 == 0 ? kPanel2 : kPanel);
            // SCALE LOCK: a key out of the scale is dim, as a gap (its line the panel's, not the grid's).
            if (!scaleOn || inScale(project_.scaleKey, project_.scaleKind, pitch)) box(r, keyX(pitch), stepY(step), cellW(), kCellH, inside, kLine);
            else box(r, keyX(pitch), stepY(step), cellW(), kCellH, kBg, kPanel);
        }
    }
    // A brighter line down the grid where each octave starts.
    for (int pitch = low_ + 1; pitch < low_ + visibleKeys(); ++pitch)
        if (pitch % 12 == 0) fill(r, keyX(pitch) - 2, gridTop(), 1, kSteps * kRowH - 2, kDim);
}

// A block as long (down the screen) as the note lasts, filled across as
// far as it is loud.
void NoteScreen::drawBlock(SDL_Renderer* r, int pitch, int step, float length, float level, SDL_Color edge, SDL_Color inside, float nudge) const {
    // A nudged note sits a little up or down from its step line: early is up.
    const int x = keyX(pitch) + 1, y = stepY(step) + 1 + static_cast<int>(std::round(nudge * kRowH));
    const int full = cellW() - 2, bottom = gridTop() + kSteps * kRowH - 3;
    int tall = static_cast<int>(std::round(length * kRowH)) - 4;
    if (y + tall > bottom) tall = bottom - y; // it runs on into the next bar
    const int h = tall < 3 ? 3 : tall;
    int w = static_cast<int>(std::round(level * full));
    if (w < 2) w = 2;
    outline(r, x, y, full, h, edge);
    if (level > 0.f) fill(r, x, y, w, h, inside);
}

// Where a chord's name is: above its lowest key that shows; on the first step there is no
// room above, so just inside the top. False when the whole chord is off the side.
bool NoteScreen::chordNameBox(const Fonts& fonts, const ChordEvent& c, SDL_Rect* box) const {
    int keys[kChordNotes];
    const int n = c.pitches(keys);
    if (n == 0) return false;
    const int low = keys[0] < low_ ? low_ : keys[0];
    const int high = keys[n - 1] > low_ + (visibleKeys() - 1) ? low_ + (visibleKeys() - 1) : keys[n - 1];
    if (high < low) return false;
    const float w = fonts.small.width(c.name) + 6.f;
    float lx = static_cast<float>(keyX(low));
    if (lx + w > kScreenW - 2) lx = kScreenW - 2 - w;
    *box = SDL_Rect{static_cast<int>(lx), c.step == 0 ? stepY(c.step) + 1 : stepY(c.step) - 15, static_cast<int>(w), 14};
    return true;
}

// A chord: its notes in purple, tied along the top, named above.
void NoteScreen::drawChord(SDL_Renderer* r, const Fonts& fonts, const ChordEvent& c, bool live, bool ghost, bool nameOnly) const {
    int keys[kChordNotes];
    const int n = c.pitches(keys);
    if (n == 0) return;
    const SDL_Color color = ghost ? kBrass : (live ? kTeal : kPurple);
    for (int i = 0; i < n && !nameOnly; ++i)
        if (keyShows(keys[i])) drawBlock(r, keys[i], c.step, c.length, ghost ? 0.f : c.velocity, color, color, c.nudge);
    const int low = keys[0] < low_ ? low_ : keys[0];
    const int high = keys[n - 1] > low_ + (visibleKeys() - 1) ? low_ + (visibleKeys() - 1) : keys[n - 1];
    if (high < low) return; // all of it is off the screen
    const int x0 = keyX(low), x1 = keyX(high) + cellW(), y = stepY(c.step);
    if (!nameOnly) fill(r, x0, y - 1, x1 - x0, 2, color);
    SDL_Rect box;
    if (!chordNameBox(fonts, c, &box)) return;
    fill(r, box.x, box.y, box.w, box.h, color);
    fonts.small.draw(r, static_cast<float>(box.x) + 3.f, box.y + (14 - fonts.small.capHeight()) / 2.f, c.name, kBg);
}

void NoteScreen::drawEarlierNotes(SDL_Renderer* r) const {
    // What is still sounding from earlier bars of the cell: drawn faint, from
    // the top, for as long as it has left. It is changed in the bar it starts in.
    // (On the FILL: those before the bar it plays instead of, which play before it.)
    const int ownBar = project_.ownBarOf(project_.track, project_.scene, project_.bar);
    for (int earlier = 0; earlier < ownBar; ++earlier) {
        NoteEvent from[kMaxNotes];
        project_.current().notes[project_.scene][earlier].render(from);
        const float gone = static_cast<float>((ownBar - earlier) * kSteps);
        for (const NoteEvent& n : from) {
            const float left = static_cast<float>(n.step) + n.length - gone;
            if (n.velocity <= 0.f || left <= 0.f || !keyShows(n.pitch)) continue;
            int tall = static_cast<int>(std::round(left * kRowH)) - 4;
            const int most = kSteps * kRowH - 4;
            outline(r, keyX(n.pitch) + 1, gridTop() + 1, cellW() - 2, tall > most ? most : (tall < 3 ? 3 : tall), kLine);
            fill(r, keyX(n.pitch) + 5, gridTop() + 1, 3, tall > most ? most : (tall < 3 ? 3 : tall), kLine);
        }
    }
}

void NoteScreen::drawNotesAndChords(SDL_Renderer* r, const Fonts& fonts, bool held, const NoteEvent* under, const ChordEvent* chordUnder) const {
    for (const NoteEvent& n : pat().notes) {
        if (n.velocity <= 0.f || !keyShows(n.pitch)) continue;
        const bool live = held && &n == under;
        drawBlock(r, n.pitch, n.step, n.length, n.velocity, live ? kTeal : kDim, live ? kTeal : kText, n.nudge);
    }
    for (int i = 0; i < kMaxChords; ++i) {
        const ChordEvent& c = pat().chords[i];
        if (c.velocity <= 0.f) continue;
        drawChord(r, fonts, c, (shaping_ && &c == chordUnder) || (picking_ && i == editing_), false);
    }
    for (const NoteEvent& n : pat().notes)
        if (n.velocity > 0.f && (n.ratchet >= 2 || n.glide || n.throwTo) && !isUnderAnother(n)) drawMarks(r, n.pitch, n.step, n.nudge, n.length, n.ratchet, n.glide != 0, n.throwTo);
    // ACID's ACCENT (acid-plan.md, section 2): a note at velocity 0.95 or over (hold A + RIGHT once from
    // a placed note) is accented, and wears a small brass corner at its top right, where nothing else
    // goes: the glide's corner is at the left, a ratchet's purple edge at the right (it stands left of
    // that), THROW's feet at the foot. Only on an ACID track: on the others 100% is just loud.
    if (project_.tracks[project_.track].kind == TrackKind::Acid)
        for (const NoteEvent& n : pat().notes) {
            if (!(n.velocity >= acid::kAccentAt) || isUnderAnother(n) || !keyShows(n.pitch)) continue;
            const AccentMark a = accentMark(keyX(n.pitch) + 1, stepY(n.step) + 1 + static_cast<int>(std::round(n.nudge * kRowH)), cellW() - 2, n.ratchet >= 2);
            for (int d = 0; d < a.size; ++d) {
                fill(r, a.x + d, a.y + d, a.size - d, 1, kBrass);
                fill(r, a.x + d - 1, a.y + d, 1, 1, kBg); // a dark edge, so it stands clear of the note
            }
        }
}

// What a note does besides play, as marks on its block. A ratchet: dark lines across its first
// step, cutting it into as many pieces as it has notes. A glide: a purple corner at its top,
// on the side of the key it comes from... (the side it slides in from is not known here; the corner is on the left).
// A note drawn inside another (below) is narrower by `inset` each side, and its marks with it.
// A chord with a chord effect set (STRUM, HUMAN TIME and the rest): a light corner at the top
// of each of its keys, where a note has its glide's purple one (a chord does not glide).
void NoteScreen::drawMarks(SDL_Renderer* r, int pitch, int step, float nudge, float length, int ratchet, bool glide, int throwTo, int inset, bool chord, bool plays) const {
    if (!keyShows(pitch)) return;
    const int pad = inset ? 1 : 0; // the dark edge it has above and below
    const int x = keyX(pitch) + 1 + inset, y = stepY(step) + 1 + pad + static_cast<int>(std::round(nudge * kRowH)), full = cellW() - 2 - 2 * inset;
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
}

void NoteScreen::drawChordMarks(SDL_Renderer* r, const Fonts& fonts, const ChordEvent* chordUnder) const {
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
        const bool named = chordNameBox(fonts, c, &name);
        const int top = stepY(c.step) + 1 + static_cast<int>(std::round(c.nudge * kRowH));
        int once = -1;
        for (int i = 0; i < count; ++i) {
            if (!keyShows(keys[i])) continue;
            if (once < 0) once = i; // (if the name is over every key, the lowest all the same)
            const bool under = named && keyX(keys[i]) + cellW() > name.x && keyX(keys[i]) < name.x + name.w && top < name.y + name.h && top + kRowH > name.y;
            if (!under) {
                once = i;
                break;
            }
        }
        const int ratchet = chordEffectWaits(c, kFxRatchet) ? 0 : c.ratchet;
        for (int i = 0; i < count; ++i) drawMarks(r, keys[i], c.step, c.nudge, c.length, i == once ? ratchet : 0, false, c.throwTo, 0, true, i == once && chordPlays(c));
        if (named) drawChord(r, fonts, c, (shaping_ && &c == chordUnder) || (picking_ && slot == editing_), false, true);
    }
}

void NoteScreen::drawNotesInside(SDL_Renderer* r, bool held, const NoteEvent* under) const {
    // A note written where something longer is already sounding on the same key (a held chord, or
    // a long note) would be hidden under it. It is drawn again on top, narrower, with a dark edge:
    // a note inside a note.
    for (const NoteEvent& n : pat().notes) {
        if (n.velocity <= 0.f || !keyShows(n.pitch) || !isUnderAnother(n)) continue;
        const bool live = held && &n == under;
        const int inset = cellW() >= 20 ? 5 : 3;
        const int x = keyX(n.pitch) + 1 + inset, y = stepY(n.step) + 1 + static_cast<int>(std::round(n.nudge * kRowH));
        const int w = cellW() - 2 - 2 * inset, bottom = gridTop() + kSteps * kRowH - 3;
        int h = static_cast<int>(std::round(n.length * kRowH)) - 4;
        h = y + h > bottom ? bottom - y : h;
        h = h < 3 ? 3 : h;
        fill(r, x - 1, y, w + 2, h, kBg);
        fill(r, x, y + 1, w, h - 2, live ? kTeal : kText);
        if (n.ratchet >= 2 || n.glide || n.throwTo) drawMarks(r, n.pitch, n.step, n.nudge, n.length, n.ratchet, n.glide != 0, n.throwTo, inset);
    }
}

void NoteScreen::drawGlideLines(SDL_Renderer* r) const {
    // GLIDE: a slanted line from the key the note slides from to the note's start, as long as the
    // slide. (Which key and how long: the engine's own rule, Project::glideLines.) A line may
    // start bars before its note, or before the top of the first bar, which is the end of the
    // scene: so every bar's lines are drawn here, wherever they cross the bar shown
    // (glideShifts). A key off the side of the roll is where the line runs off it.
    {
        bool any = false;
        for (int b = 0; b < cellBars() && !any; ++b)
            for (const NoteEvent& n : project_.current().notes[project_.scene][b].notes) any = any || (n.velocity > 0.f && n.glide);
        if (any && !onFill()) { // (the FILL's are not drawn: it is not one of the cell's bars in order, Project::glideLines)
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
                        const double x0 = keyX(line.from) + cellW() / 2.0, x1 = keyX(n.pitch) + cellW() / 2.0;
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
}

void NoteScreen::drawStrip(SDL_Renderer* r, const Fonts& fonts, const NoteEvent* under, const ChordEvent* chordUnder, SDL_Color cursorColor) const {
    // ---- what is under the cursor ---------------------------------------------------
    // The strip is a tight row: its fields stand at fixed places (note_marks.hpp) with a chip
    // under each, and the chord tiles are 88 across. Always at LARGE, whatever TEXT SIZE is.
    const Fonts& tight = fonts.fixed();
    const int stripY = gridTop() + kSteps * kRowH + kStripGap;
    if (picking_) drawPackStrip(r, tight, stripY);
    else drawNoteStrip(r, tight, stripY, under, chordUnder, cursorColor);
}

// The pack laid out, Y held.
void NoteScreen::drawPackStrip(SDL_Renderer* r, const Fonts& tight, int stripY) const {
    char text[96];
    // The pack laid out: seven chords at a time, the one in hand lit.
    const ChordPack& p = pack();
    const int count = static_cast<int>(p.chords.size());
    float x = 8.f;
    tight.small.draw(r, x, static_cast<float>(stripY), "L1", kDim);
    x += tight.small.width("L1") + 6.f;
    std::string packName = p.name;
    for (char& c : packName) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    tight.small.draw(r, x, static_cast<float>(stripY), packName.c_str(), kText);
    x += tight.small.width(packName.c_str()) + 6.f;
    tight.small.draw(r, x, static_cast<float>(stripY), "R1", kDim);
    std::snprintf(text, sizeof text, "%d / %d", hand_ + 1, count);
    tight.small.draw(r, 300.f, static_cast<float>(stripY), text, kDim);
    if (inversion_ != 0) {
        std::snprintf(text, sizeof text, "INV %+d", inversion_); // short: the words at the right-hand end start at 466
        tight.small.draw(r, 380.f, static_cast<float>(stripY), text, kBrass);
    }
    tight.small.drawRight(r, kScreenW - 8.f, static_cast<float>(stripY), editing_ >= 0 ? "SWAPPING THIS CHORD" : "LET GO OF Y TO PLACE", kDim);
    const int shown = count < 7 ? count : 7, tileW = 88, tileH = 30, tileY = stripY + 15;
    int first = hand_ - 3;
    if (first > count - shown) first = count - shown;
    if (first < 0) first = 0;
    for (int i = 0; i < shown; ++i) {
        const int index = first + i, tx = 8 + i * (tileW + 2);
        const bool here = index == hand_;
        box(r, tx, tileY, tileW, tileH, here ? kPanel2 : kPanel, kLine);
        tight.small.drawCentered(r, tx + tileW / 2.f, tileY + (tileH - tight.small.capHeight()) / 2.f,
                                 shapeAt(index).name, here ? kText : kDim);
        if (here) outline(r, tx - 1, tileY - 1, tileW + 2, tileH + 2, kBrass, 2);
    }
}

// The note or chord under the cursor, or the key, and the step.
void NoteScreen::drawNoteStrip(SDL_Renderer* r, const Fonts& tight, int stripY, const NoteEvent* under, const ChordEvent* chordUnder, SDL_Color cursorColor) const {
    char text[96];
    const auto field = [&](float fx, const char* label, const char* value, SDL_Color color) {
        tight.small.draw(r, fx, stripY + 8.f, label, kDim);
        tight.large.draw(r, fx, stripY + 26.f, value, color);
    };
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
            if (picked) fill(r, static_cast<int>(x) - 8, static_cast<int>(y) - 1, 3, tight.small.capHeight() + 2, kBrass);
            // (One that waits is dim; picked all the same, it is still to be read.)
            tight.small.draw(r, x, y, chordEffectName(effect), waits ? (picked ? kDim : kLine) : picked ? kText : kDim);
            chordEffectValue(c, effect, text, sizeof text);
            tight.small.draw(r, x + kChordFxValueIn[effect / 3], y, text, waits ? (picked ? kDim : kLine) : set ? (picked ? kBrass : kText) : kDim);
        }
        field(kChordFxThrowX, "THROW", throwName(chordUnder->throwTo), chordUnder->throwTo ? kBrass : kText);
    } else if (chordUnder) {
        field(8.f, "CHORD", chordUnder->name, kPurple);
    } else if (onBars_) {
        field(8.f, "CELL", "BARS", kText);
    } else {
        noteName(pitch_, text, sizeof text);
        field(8.f, "KEY", text, kText);
    }
    if (onBars_ && onFill()) std::snprintf(text, sizeof text, "%s", project_.current().fillOn(project_.scene) ? "FILL" : "FILL OFF");
    else if (onBars_) std::snprintf(text, sizeof text, "%d OF %d", project_.bar + 1, cellBars());
    else std::snprintf(text, sizeof text, "%d", sceneBar() * project_.meterOf(project_.scene) + step_ + 1); // as the numbers at the left read
    if (!chordFx) field(100.f, onBars_ ? "BAR" : "STEP", text, onBars_ ? cursorColor : kText);
    drawStripValues(r, tight, stripY, under, chordUnder, chordFx);
}

// The rest of the strip: LEVEL and LENGTH (or, X held, what the note does), its effects, THROW and NUDGE.
void NoteScreen::drawStripValues(SDL_Renderer* r, const Fonts& tight, int stripY, const NoteEvent* under, const ChordEvent* chordUnder, bool chordFx) const {
    char text[96];
    const auto field = [&](float fx, const char* label, const char* value, SDL_Color color) {
        tight.small.draw(r, fx, stripY + 8.f, label, kDim);
        tight.large.draw(r, fx, stripY + 26.f, value, color);
    };
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
            // (Each of its values fits in the large lettering: under_test measures them.)
            field(kStripHeldFromX, "GLIDE FROM", from, !glideHere ? kDim : fromHere != kFromTop ? kBrass : kText);
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
        if (tight.small.width(text) > room) {
            int on = 0;
            for (int effect = 0; effect < kChordEffects; ++effect) on += chordEffectSet(*chordUnder, effect) && !chordEffectWaits(*chordUnder, effect);
            std::snprintf(text, sizeof text, "%d ON", on);
        }
        const Font* sizes[3] = {&tight.large, &tight.medium, &tight.small};
        const float widths[3] = {tight.large.width(text), tight.medium.width(text), tight.small.width(text)};
        const int size = stripFont(widths, room);
        const float x = stripEffectX(throwHere != 0);
        tight.small.draw(r, x, stripY + 8.f, "EFFECTS", kDim);
        sizes[size]->draw(r, x, stripY + 26.f + tight.large.capHeight() - sizes[size]->capHeight(), text, kPurple);
        effectEnd = x + widths[size] + kStripSpace;
    } else if (!fxHeld_ && (ratchetHere >= 2 || glideHere)) {
        // In a line, what X held would show in full.
        // A glide alone says how long it is; beside a ratchet there is only room to say it is there.
        std::snprintf(text, sizeof text, "%s%s%s", ratchetHere >= 2 ? (ratchetHere == 2 ? "x2" : ratchetHere == 3 ? "x3" : "x4") : "", ratchetHere >= 2 && glideHere ? " " : "", !glideHere ? "" : ratchetHere >= 2 ? "GLIDE" : glideName(glideKindHere));
        const Font* sizes[3] = {&tight.large, &tight.medium, &tight.small};
        const float widths[3] = {tight.large.width(text), tight.medium.width(text), tight.small.width(text)};
        const int size = ratchetHere >= 2 ? 0 : stripFont(widths, stripGlideRoom(throwHere != 0)); // (a ratchet's line is as it always was)
        const float x = stripEffectX(throwHere != 0);
        tight.small.draw(r, x, stripY + 8.f, ratchetHere >= 2 ? "RATCHET" : "GLIDE", kDim);
        sizes[size]->draw(r, x, stripY + 26.f + tight.large.capHeight() - sizes[size]->capHeight(), text, kPurple);
        effectEnd = x + widths[size] + kStripSpace;
    }
    if (!fxHeld_ && throwHere) {
        // Up to the right margin and no further: moved left, or drawn smaller beside a long effect.
        const char* to = throwName(throwHere);
        const Font* sizes[3] = {&tight.large, &tight.medium, &tight.small};
        const float widths[3] = {tight.large.width(to), tight.medium.width(to), tight.small.width(to)};
        float x = kStripThrowX;
        const Font& font = *sizes[stripThrowFont(widths, effectEnd, &x)];
        tight.small.draw(r, x, stripY + 8.f, "THROW", kDim);
        font.draw(r, x, stripY + 26.f + tight.large.capHeight() - font.capHeight(), to, kPurple);
    }
    if (chordFx || (fxHeld_ && under)) {
        // (the chord's effects have the strip, or GLIDE FROM has NUDGE's place: the nudge is there again when X is let go)
    } else if (nudge != 0.f) {
        std::snprintf(text, sizeof text, "%d%% %s", static_cast<int>(std::round(std::fabs(nudge) * 100.f)), nudge < 0.f ? "EARLY" : "LATE");
        // Beside an effect and a THROW there is less room: the value is drawn smaller, never over them.
        const bool effectHere = !fxHeld_ && (ratchetHere >= 2 || glideHere || chordPlaysHere);
        const Font* sizes[3] = {&tight.large, &tight.medium, &tight.small};
        const float widths[3] = {tight.large.width(text), tight.medium.width(text), tight.small.width(text)};
        const float nudgeX = kStripNudgeX;
        const Font& font = *sizes[stripFont(widths, stripNudgeRoom(effectHere, throwHere != 0))];
        tight.small.draw(r, nudgeX, stripY + 8.f, "NUDGE", kDim);
        font.draw(r, nudgeX, stripY + 26.f + tight.large.capHeight() - font.capHeight(), text, shaping_ ? kText : kTeal);
    } else if (!fxHeld_) {
        std::snprintf(text, sizeof text, "%d / %d", pat().used(), kMaxNotes);
        field(kStripNudgeX, "NOTES", text, kDim);
    }
}

void NoteScreen::drawHints(SDL_Renderer* r, const Fonts& fonts, const NoteEvent* under, const ChordEvent* chordUnder, bool held) const {
    // ---- hints ----------------------------------------------------------------------
    Hint list[8];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    char brushText[40];
    if (picking_) {
        hint("LEFT / RIGHT", "chord");
        hint("UP / DOWN", "invert"); // (G26: "inversion" with A's chip added ran 13 pixels over at XL)
        hint("L1 R1", "pack");
        // On a swap the row is full: with A's and B's longer words, L2 R2 ran 11 pixels over with "HLD SEL: HELP",
        // so it is shown on a new chord, and the manual says it does the same on a swap (G27 review).
        if (editing_ < 0) hint("L2 R2", "length");
        if (editing_ >= 0) hint("A", "notes"); // into loose notes
        hint("B", editing_ >= 0 ? "as it was" : "cancel");
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
    } else if (l1First_ && !held && carrying_ == Carry::Nothing && !onBars_) {
        hint("UP / DOWN", "change bar");
        hint("LEFT / RIGHT", "bar count");
        hint("R1", "copy bar");
        hint("A", "paste bar");
        if (const char* fill = Session::fillKeyHint(project_.current(), project_.scene, project_.bar)) hint("Y", fill); // the FILL
    } else if (carrying_ != Carry::Nothing) {
        hint("STICK UP / DOWN", "slide in time");
        hint("D-PAD", "whole steps");
        hint("LEFT / RIGHT", "another key");
        hint("L1", "octave");
    } else if (painting_) {
        std::snprintf(brushText, sizeof brushText, "paint notes at %d%%", static_cast<int>(std::round(brush_ * 100.f)));
        hint("DRAG", brushText);
    } else if (onBars_) {
        if (shaping_) hint("UP / DOWN", "more / fewer");
        else {
            hint("LEFT / RIGHT", "bar");
            hint("HOLD A", "more / fewer");
        }
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
        // Up and down past a bar's ends move across the cell's bars (up from the very top is the BARS stop).
        // Said instead of the zoom there: the bar has no room for both.
        if (step_ == 0) hint("UP", project_.bar == 0 ? "bars" : "prev bar");
        else if (step_ == kSteps - 1 && project_.current().barAfter(project_.scene, project_.bar, 1) != 0) hint("DOWN", "next bar"); // (the FILL after the last)
        else hint("L1", "zoom");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
