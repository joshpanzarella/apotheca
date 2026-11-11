#include "sample_picker.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kRepeatDelayMs = 220, kRepeatEveryMs = 110;
constexpr float kRowsPerSecond = 8.f; // the stick: slow enough to hear each one go by
}

void SamplePicker::open(std::vector<std::string> names, const std::string& current, const char* title, const char* first, Jobs jobs) {
    names_ = std::move(names);
    original_ = current;
    title_ = title;
    first_ = first;
    jobs_ = std::move(jobs);
    cursor_ = 0;
    for (std::size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == current) cursor_ = static_cast<int>(i) + 1;
    saved_ = 0.f;
    stickWas_ = false;
    repeating_ = Button::None;
    open_ = true;
}

// The ends stop. Arriving on a row puts it on, and plays it if the song is not doing that already.
void SamplePicker::moveTo(int row, bool playing) {
    row = row < 0 ? 0 : (row >= count() ? count() - 1 : row);
    if (row == cursor_) return;
    cursor_ = row;
    if (jobs_.use) jobs_.use(row == 0 ? "" : names_[static_cast<std::size_t>(row - 1)]);
    if (!playing && jobs_.hear) jobs_.hear();
}

void SamplePicker::onButton(const ButtonEvent& event, bool playing) {
    if (!open_) return;
    const Button b = event.button;
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right) {
        moveTo(cursor_ + (b == Button::Up ? -1 : b == Button::Down ? 1 : b == Button::Left ? -kPage : kPage), playing);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::A) {
        open_ = false; // what is showing is already on
    } else if (b == Button::B) {
        // As it was. (Nothing to do if the cursor is back where it began.)
        const std::string now = cursor_ == 0 ? "" : names_[static_cast<std::size_t>(cursor_ - 1)];
        if (now != original_ && jobs_.use) jobs_.use(original_);
        open_ = false;
    } else if (b == Button::X) {
        if (jobs_.hear) jobs_.hear();
    }
}

void SamplePicker::update(const Input& input, Uint32 nowMs, bool playing) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed;
    lastMs_ = nowMs;
    playing_ = playing;
    if (!open_) return;
    const float y = input.stickY();
    if (y == 0.f) {
        saved_ = 0.f;
        stickWas_ = false;
    } else {
        saved_ += stickWas_ ? std::fabs(y) * kRowsPerSecond * dt : 1.f;
        stickWas_ = true;
        for (; saved_ >= 1.f; saved_ -= 1.f) moveTo(cursor_ + (y > 0.f ? 1 : -1), playing);
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        moveTo(cursor_ + (repeating_ == Button::Up ? -1 : repeating_ == Button::Down ? 1 : repeating_ == Button::Left ? -kPage : kPage), playing);
    }
}

void SamplePicker::draw(SDL_Renderer* r, const Fonts& fonts) const {
    if (!open_) return;
    const int w = 440, rowH = 30, x = (kScreenW - w) / 2, y = 34, h = kScreenH - kHintsH - y - 8;
    fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 215));
    box(r, x, y, w, h, kPanel, kLine);
    char text[64];
    fonts.small.draw(r, x + 12.f, y + 11.f, title_.c_str(), kDim);
    std::snprintf(text, sizeof text, "%d OF %d", cursor_ + 1, count());
    fonts.small.drawRight(r, x + w - 12.f, y + 11.f, text, kDim);

    // The one that is on: its outline and how long it is.
    const int wx = x + 12, wy = y + 32, ww = w - 24, wh = 56;
    box(r, wx, wy, ww, wh, kBg, kLine);
    const Session::LaneSound* sound = jobs_.sound ? jobs_.sound() : nullptr;
    if (cursor_ > 0 && sound && !sound->peaks.empty()) {
        const int n = static_cast<int>(sound->peaks.size());
        for (int px = 0; px < ww - 4; ++px) {
            const float p = sound->peaks[static_cast<std::size_t>(px * n / (ww - 4))];
            const int half = static_cast<int>(std::round((p > 1.f ? 1.f : p) * (wh - 8) / 2.f));
            fill(r, wx + 2 + px, wy + wh / 2 - half, 1, 2 * half + 1, kTeal);
        }
        std::snprintf(text, sizeof text, "%.2f S", static_cast<double>(sound->seconds));
        fonts.small.drawRight(r, wx + ww - 6.f, wy + 5.f, text, kText);
    } else {
        fonts.small.drawCentered(r, wx + ww / 2.f, wy + (wh - fonts.small.capHeight()) / 2.f, cursor_ == 0 ? first_.c_str() : "...", kDim);
    }

    // The list, scrolled to keep the cursor in view.
    int first = cursor_ - kRows / 2;
    first = first > count() - kRows ? count() - kRows : first;
    first = first < 0 ? 0 : first;
    const int ly = wy + wh + 10;
    for (int row = 0; row < kRows && first + row < count(); ++row) {
        const int i = first + row, by = ly + row * rowH;
        const bool here = i == cursor_;
        box(r, x + 12, by, w - 24, rowH - 4, here ? kPanel2 : kPanel, kLine);
        std::string name = i == 0 ? first_ : names_[static_cast<std::size_t>(i - 1)];
        for (char& c : name) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        while (!name.empty() && fonts.medium.width(name.c_str()) > w - 48) name.pop_back();
        fonts.medium.draw(r, x + 24.f, by + (rowH - 4 - fonts.medium.capHeight()) / 2.f, name.c_str(), here ? kTeal : (i == 0 ? kDim : kText));
        if (here) outline(r, x + 11, by - 1, w - 22, rowH - 2, kTeal, 2);
    }
    if (count() == 1) {
        fonts.small.draw(r, x + 24.f, ly + rowH + 8.f, "No recordings yet. Send .wav files over the WiFi page,", kDim);
        fonts.small.draw(r, x + 24.f, ly + rowH + 24.f, "catch a bar with Y, or record with R1.", kDim);
    }
    // A bar down the side says where in the list this is.
    if (count() > kRows) {
        const int track = kRows * rowH - 4, bar = track * kRows / count();
        fill(r, x + w - 8, ly, 3, track, kLine);
        fill(r, x + w - 8, ly + (track - bar) * first / (count() - kRows), 3, bar, kTeal);
    }
    const Hint list[] = {{"A", "keep it"}, {"B", "as it was"}, {"UP / DOWN", playing_ ? "next (heard in the beat)" : "next"}, {"LEFT / RIGHT", "8 at a time"}};
    hints(r, fonts, list, 4, true);
}

} // namespace apotheca
