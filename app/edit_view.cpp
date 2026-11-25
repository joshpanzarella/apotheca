#include "edit_view.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
constexpr float kMovesPerSecond = 14.f; // the stick among the boxes, as on the sampler's page
// How far along its bar a box stands (below 0: no bar), and whether the bar grows from the middle.
float editPart(const LaneEdit& e, int row) {
    const int v = laneEditValue(e, row);
    switch (row) {
    case kEditStart: case kEditEnd: return static_cast<float>(v) / kEditUnits;
    case kEditFadeIn: case kEditFadeOut: return static_cast<float>(fadeStopPlace(v)) / (kFadeStopCount - 1);
    case kEditGain: return static_cast<float>(v + kGainMost) / (2 * kGainMost);
    case kEditPitch: return static_cast<float>(v + kPitchMost) / (2 * kPitchMost);
    case kEditFine: return static_cast<float>(v + kFineMost) / (2 * kFineMost);
    default: return -1.f;
    }
}
bool editFromMiddle(int row) { return row == kEditGain || row == kEditPitch || row == kEditFine; }
}

void EditView::open(const Target& target, int row) {
    target_ = target;
    open_ = true;
    yJust_ = false;
    hearOwed_ = false;
    holding_ = deleting_ = bBack_ = false;
    deletePart_ = 0.f;
    repeating_ = Button::None;
    row_ = row < 0 ? 0 : (row >= kEditRows ? kEditRows - 1 : row);
    if (!has(row_)) row_ = shownNear(row_ / 4, row_ % 4);
    if (row_ < 0) row_ = 0;
}

int EditView::shownNear(int row, int column) const {
    for (int off = 0; off < 4; ++off)
        for (int c : {column - off, column + off})
            if (c >= 0 && c < 4 && has(row * 4 + c)) return row * 4 + c;
    return -1;
}

void EditView::hearNow(bool held) {
    hearOwed_ = false; // (whatever is heard now pays what a turn owed)
    target_.hear(held);
}

void EditView::hearWhenDone(const Input& input, Uint32 nowMs) {
    if (!hearOwed_) return;
    if (project_.playing) { hearOwed_ = false; return; } // the song is heard instead
    const bool turning = input.held(Button::A) || input.stickX() != 0.f || input.stickY() != 0.f ||
                         (repeating_ != Button::None && input.held(repeating_));
    const Session::LaneSound& sound = target_.sound();
    if (turning || sound.making || sound.shaping || nowMs - changedAt_ < kHearAfterMs) return;
    hearNow(false);
}

void EditView::set(const LaneEdit& edit) {
    const LaneEdit before = target_.edit();
    target_.set(edit);
    if (target_.edit() == before || project_.playing) return;
    hearOwed_ = true;
    changedAt_ = lastMs_;
}

void EditView::press(Button b) {
    if (!holding_) {
        // Two rows of four: left / right along a row, up / down between the rows, the boxes it has not passed by.
        // The ends stop.
        const int row = row_ / 4, column = row_ % 4;
        if (b == Button::Left || b == Button::Right) {
            const int by = b == Button::Left ? -1 : 1;
            for (int c = column + by; c >= 0 && c < 4; c += by)
                if (has(row * 4 + c)) { row_ = row * 4 + c; break; }
        }
        if ((b == Button::Up && row == 1) || (b == Button::Down && row == 0)) {
            const int to = shownNear(1 - row, column);
            if (to >= 0) row_ = to;
        }
        return;
    }
    if (b != Button::Up && b != Button::Down) return;
    set(laneEditStepped(target_.edit(), row_, b == Button::Up ? 1 : -1, target_.sound().sourceSeconds));
}

void EditView::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    const bool afterY = event.down && b == Button::B && bLeavesAfterY(row_);
    if (event.down) yJust_ = false; // (any press: Y is no longer the last)
    if (b == Button::A) {
        holding_ = event.down;
        turned_ = 0.f;
        saved_ = 0.f;
        stickWas_ = reversePushed_ = false;
        return;
    }
    if (event.down && b != Button::B) deleting_ = bBack_ = false; // any other button calls a held B off
    if (!event.down) {
        if (b == Button::B) {
            if (deleting_ && bBack_) open_ = hearOwed_ = false; // a tap on a box at its usual value, or just after Y: back (a turn left is not heard)
            deleting_ = bBack_ = false;
        }
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    const LaneEdit& e = target_.edit();
    if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeat_.restart();
    } else if (b == Button::B && !holding_) {
        // The box to its usual value at once; held on, the bar fills and every edit goes (update). Straight
        // after Y on this box: nothing put back, and the tap goes back with the trim kept (G16).
        const int usual = laneEditValue(LaneEdit(), row_);
        bBack_ = afterY || laneEditValue(e, row_) == usual;
        if (!bBack_) set(laneEditWith(e, row_, usual));
        deleting_ = true;
        deleteTimed_ = false;
        deletePart_ = 0.f;
    } else if (b == Button::Y && !holding_) {
        char text[64];
        if (row_ == kEditStart || row_ == kEditEnd) {
            target_.say(target_.trim() ? "START AND END ON THE FIRST AND LAST SOUND" : "NOTHING TO TRIM TO: IT IS SILENT");
        } else if (target_.normalize()) {
            laneEditText(target_.edit(), kEditGain, 0.f, text, sizeof text);
            target_.say(std::string("NORMALIZED: GAIN ") + text);
        } else {
            target_.say("NOTHING TO NORMALIZE: IT IS SILENT");
        }
        if (!project_.playing) { hearOwed_ = true; changedAt_ = lastMs_ - kHearAfterMs; } // heard once it is made
        yJust_ = true, yBox_ = row_;
    } else if (b == Button::X) {
        hearNow(true);
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void EditView::update(const Input& input, Uint32 nowMs, float dt) {
    lastMs_ = nowMs;
    if (!input.held(Button::A)) holding_ = false;
    hearWhenDone(input, nowMs);
    // B held: the bar fills for half a second (kDeleteHoldMs: undo brings it back, G29), then every edit goes (undo brings them back).
    if (!input.held(Button::B) || holding_) deleting_ = bBack_ = false;
    if (!deleting_) {
        deletePart_ = 0.f;
    } else {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        if (deletePart_ >= 1.f) {
            deleting_ = bBack_ = false; // (let go now, B does not go back)
            deletePart_ = 0.f;
            set(LaneEdit());
        }
    }
    const float x = input.stickX(), y = input.stickY();
    const float seconds = target_.sound().sourceSeconds;
    if (holding_) {
        // A box: the stick turns it through its whole range in about a second, by the square of the push.
        const LaneEdit& e = target_.edit();
        if (row_ == kEditReverse) { // a choice: once a push
            if (std::fabs(y) > 0.5f && !reversePushed_) set(laneEditWith(e, kEditReverse, y < 0.f ? 1 : 0));
            reversePushed_ = std::fabs(y) > 0.5f;
        } else {
            turned_ += -y * std::fabs(y) * static_cast<float>(laneEditRange(row_)) * dt;
            const int whole = static_cast<int>(turned_);
            if (whole != 0) {
                turned_ -= static_cast<float>(whole);
                const int v = laneEditValue(e, row_);
                if (row_ == kEditFadeIn || row_ == kEditFadeOut) {
                    int place = fadeStopPlace(v) + whole;
                    place = place < 0 ? 0 : (place >= kFadeStopCount ? kFadeStopCount - 1 : place);
                    set(laneEditWith(e, row_, kFadeStops[place]));
                } else {
                    set(laneEditHeld(laneEditWith(e, row_, v + whole), seconds));
                }
            }
            if (y == 0.f) turned_ = 0.f;
        }
    } else {
        const bool sideways = std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        const float climb = climb_.times(push, nowMs); // the cursor among the boxes: a full push held climbs (G07)
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * kMovesPerSecond * climb * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f) {
                deleting_ = bBack_ = false;
                press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
            }
        }
    }
    while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), Pace::Cursor)) press(repeating_);
}

void EditView::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[80];
    const LaneEdit& e = target_.edit();
    const Session::LaneSound& sound = target_.sound();
    const std::string& said = target_.said();

    // ---- the sound ----------------------------------------------------------------
    const int sx = 8, sy = kY, sw = 624, sh = kH;
    box(r, sx, sy, sw, sh, kPanel, kLine);
    if (deletePart_ > 0.f) {
        fill(r, sx + 1, sy + sh - 6, static_cast<int>((sw - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), 5, faded(kStop, 150));
        fonts.within(&Fonts::small, kDeletingWords, sw - 120.f).draw(r, sx + 7.f, sy + 7.f, kDeletingWords, kText);
    } else if (!said.empty()) {
        fonts.within(&Fonts::small, said.c_str(), sw - 120.f).draw(r, sx + 7.f, sy + 7.f, said.c_str(), kBrass);
    } else {
        fonts.within(&Fonts::small, target_.quiet.c_str(), sw - 120.f).draw(r, sx + 7.f, sy + 7.f, target_.quiet.c_str(), kDim);
    }
    fonts.within(&Fonts::large, target_.title.c_str(), sw - 14.f).draw(r, sx + 7.f, sy + 23.f, target_.title.c_str(), sound.missing ? kDim : kText);
    const float wordsY = kWaveY + (kWaveH - fonts.small.capHeight()) / 2.f;
    if (sound.missing) {
        fonts.small.draw(r, sx + 7.f, wordsY, target_.missing.c_str(), kBrass);
    } else if (!sound.sourcePeaks.empty()) {
        const int wx = sx + 7, wy = kWaveY, ww = sw - 14, wh = kWaveH;
        // The whole file, the part played lit, each point as loud as GAIN and the fades make it.
        std::vector<float> lit(sound.sourcePeaks.size());
        const int count = static_cast<int>(lit.size());
        for (int i = 0; i < count; ++i)
            lit[static_cast<std::size_t>(i)] = guarded(sound.sourcePeaks[static_cast<std::size_t>(i)] * laneEditLevelAt(e, static_cast<float>(i) / count, sound.sourceSeconds));
        wavePicture(r, sound.sourcePeaks, wx, wy, ww, wh, static_cast<float>(e.start) / kEditUnits, static_cast<float>(e.end) / kEditUnits, &lit);
        // The line while it is heard, through the part played (backwards with REVERSE).
        const float at = target_.heardAt();
        if (at >= 0.f) fill(r, wx + static_cast<int>(laneEditPlaceOf(e, at > 1.f ? 1.f : at) * static_cast<float>(ww - 2)), wy - 2, 2, wh + 4, kBrass);
        if (sound.shaping) std::snprintf(text, sizeof text, "MAKING IT...");
        else std::snprintf(text, sizeof text, "PLAYS %.2f S", static_cast<double>(sound.seconds));
        fonts.small.drawRight(r, sx + sw - 8.f, sy + 7.f, text, sound.shaping ? kBrass : e.atRest() ? kDim : kPurple);
    }

    // ---- the boxes ----------------------------------------------------------------
    for (int row = 0; row < kEditRows; ++row) {
        const int x = kBoxX + (row % 4) * kBoxStep, y = kRowY + (row / 4) * (kRowH + kRowGap);
        if (!has(row)) { // a box this sound has not (the sampler's PITCH and FINE: its page has them)
            box(r, x, y, kBoxW, kRowH, kPanel, kLine);
            continue;
        }
        laneEditText(e, row, sound.sourceSeconds, text, sizeof text);
        valueBox(r, fonts, x, y, kBoxW, kRowH, laneEditName(row), text, editPart(e, row), row == row_, holding_, editFromMiddle(row));
    }

    // ---- the hints ----------------------------------------------------------------
    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    if (holding_) {
        hint("UP / DOWN", row_ == kEditReverse ? "on / off" : "turn it");
    } else {
        hint("HOLD A", "turn");
        hint("B", bLeavesAfterY(row_) || laneEditValue(e, row_) == laneEditValue(LaneEdit(), row_) ? "back" : "default");
        hint("Y", row_ == kEditStart || row_ == kEditEnd ? "trim silence" : "normalize");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
