#include "name_keys.hpp"

#include "ui.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

void NameKeys::open(const std::string& name, int most) {
    most_ = most;
    typed_ = name.substr(0, static_cast<std::size_t>(most > 0 ? most : 0));
    key_ = 0;
    saved_ = 0.f;
    stickWas_ = false;
    repeating_ = Button::None;
    repeat_.restart();
    open_ = true;
}

void NameKeys::close() {
    open_ = false;
    repeating_ = Button::None;
    saved_ = 0.f;
    stickWas_ = false;
}

void NameKeys::deleteLast() {
    if (!typed_.empty()) typed_.pop_back();
}

void NameKeys::move(Button direction) {
    const int rows = kCount / kColumns;
    int col = key_ % kColumns, row = key_ / kColumns;
    if (direction == Button::Left) col = (col + kColumns - 1) % kColumns;
    if (direction == Button::Right) col = (col + 1) % kColumns;
    if (direction == Button::Up) row = (row + rows - 1) % rows;
    if (direction == Button::Down) row = (row + 1) % rows;
    key_ = row * kColumns + col;
}

NameKeys::Asked NameKeys::press(Button b) {
    if (!open_) return Asked::Nothing;
    if (isDirection(b)) {
        move(b);
        repeating_ = b;
        repeat_.restart();
    } else if (b == Button::A) {
        const char c = kKeys[key_];
        const bool space = c == ' ';
        if (static_cast<int>(typed_.size()) < most_ && !(space && (typed_.empty() || typed_.back() == ' '))) typed_ += c;
    } else if (b == Button::Y) {
        deleteLast(); // the last letter goes; held, the letters go at a list's pace (G48)
        repeating_ = b;
        repeat_.restart();
    } else if (b == Button::B) {
        return Asked::Keep;
    } else if (b == Button::Start) {
        return Asked::Start;
    }
    return Asked::Nothing;
}

void NameKeys::letGo(Button b) {
    if (b == repeating_) repeating_ = Button::None;
}

void NameKeys::update(const Input& input, Uint32 nowMs, float dt) {
    if (!open_) return;
    // The stick: one key at once, then at twice a held D-pad's pace (the list's) however far it is pushed, the
    // way it leans most.
    const float x = input.stickX(), y = input.stickY();
    const bool sideways = std::fabs(x) > std::fabs(y);
    const float push = sideways ? x : y;
    if (push == 0.f) {
        saved_ = 0.f;
        stickWas_ = false;
    } else {
        saved_ += stickWas_ ? ui::stickPerSecond(Pace::List) * dt : 1.f;
        stickWas_ = true;
        for (; saved_ >= 1.f; saved_ -= 1.f) move(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
    }
    // A held D-pad direction runs, at a list's pace: each key is seen go by. A held Y deletes at the same
    // pace, one letter at a time, so an old name goes in one hold and each letter is seen go (G48).
    while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), Pace::List)) {
        if (repeating_ == Button::Y) deleteLast();
        else move(repeating_);
    }
}

void NameKeys::draw(SDL_Renderer* r, const Fonts& fonts, const std::string& title, SDL_Color titleColour, const char* emptyNote, const char* bDoes, const char* startDoes) const {
    // The title, held at LARGE (and cut) where it would not fit at the size chosen.
    std::string head = title;
    const Font& headFont = fonts.within(&Fonts::small, head.c_str(), kScreenW - 80.f);
    while (!head.empty() && headFont.width(head.c_str()) > kScreenW - 80.f) head.pop_back();
    headFont.draw(r, 40.f, 46.f, head.c_str(), titleColour);
    // What has been typed, with a mark where the next letter goes, and how many of how many.
    box(r, 40, kBoxY, kScreenW - 80, kBoxH, kPanel2, kTeal);
    const float tx = 56.f, ty = kBoxY + (kBoxH - fonts.large.capHeight()) / 2.f;
    fonts.large.draw(r, tx, ty, typed_.c_str(), kText);
    if (static_cast<int>(typed_.size()) < most_) fill(r, static_cast<int>(tx + fonts.large.width(typed_.c_str()) + (typed_.empty() ? 0 : 3)), kBoxY + 14, 2, 24, kTeal);
    char text[32];
    std::snprintf(text, sizeof text, "%d OF %d", static_cast<int>(typed_.size()), most_);
    fonts.small.drawRight(r, kScreenW - 56.f, kBoxY + (kBoxH - fonts.small.capHeight()) / 2.f, text, kDim);
    if (typed_.empty() && emptyNote) fonts.small.draw(r, tx + 12.f, kBoxY + (kBoxH - fonts.small.capHeight()) / 2.f, emptyNote, kDim);
    // The grid.
    const int gx = (kScreenW - kColumns * kKeyW) / 2;
    for (int i = 0; i < kCount; ++i) {
        const int x = gx + (i % kColumns) * kKeyW, y = kGridY + (i / kColumns) * kKeyW;
        const bool here = i == key_;
        box(r, x + 2, y + 2, kKeyW - 4, kKeyW - 4, here ? kTeal : kPanel, here ? kTeal : kLine);
        char one[8];
        std::snprintf(one, sizeof one, "%s", kKeys[i] == ' ' ? "SPACE" : std::string(1, kKeys[i]).c_str());
        const Font& font = kKeys[i] == ' ' ? fonts.within(&Fonts::small, one, kKeyW - 4 - 6.f) : fonts.large; // ("SPACE" at XL is wider than its key: held at LARGE)
        font.drawCentered(r, x + kKeyW / 2.f, y + (kKeyW - font.capHeight()) / 2.f, one, here ? kBg : kText);
    }
    const bool none = typed_.empty();
    const Hint list[] = {{"A", "type"}, {"Y", none ? nullptr : "delete"}, {"B", bDoes}, {"START", startDoes}};
    hints(r, fonts, list, 4);
}

} // namespace apotheca
