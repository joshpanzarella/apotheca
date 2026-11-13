#include "sample_picker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "samples.hpp"

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kRepeatDelayMs = ui::kListDelayMs, kRepeatEveryMs = ui::kListEveryMs;
constexpr float kRowsPerSecond = 8.f; // the stick: slow enough to hear each one go by
}

void SamplePicker::open(std::vector<std::string> names, const std::string& current, const char* title, const char* first, Jobs jobs) {
    all_ = std::move(names);
    original_ = current;
    title_ = title;
    first_ = first;
    jobs_ = std::move(jobs);
    // The folders there are, in the order the names come in (the loose ones first). With no
    // recordings at all there is still one, empty, so that there is a list to show.
    folders_.clear();
    for (const std::string& name : all_) {
        const std::string in = sampleFolderOf(name);
        if (std::find(folders_.begin(), folders_.end(), in) == folders_.end()) folders_.push_back(in);
    }
    if (folders_.empty()) folders_.push_back("");
    // Open on the folder the recording that is on is in, with the cursor on it.
    const auto has = std::find(folders_.begin(), folders_.end(), sampleFolderOf(current));
    showFolder(has == folders_.end() ? 0 : static_cast<int>(has - folders_.begin()));
    cursor_ = 0;
    for (std::size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == current) cursor_ = static_cast<int>(i) + 1;
    saved_ = 0.f;
    stickWas_ = false;
    repeating_ = Button::None;
    bDown_ = false;
    open_ = true;
}

void SamplePicker::showFolder(int index) {
    folder_ = index;
    names_.clear();
    for (const std::string& name : all_)
        if (sampleFolderOf(name) == folders_[static_cast<std::size_t>(folder_)]) names_.push_back(name);
}

// Another folder, round and round. Its first recording goes on, as arriving on any row does.
void SamplePicker::turnFolder(int by, bool playing) {
    if (folders_.size() < 2) return;
    const int count = static_cast<int>(folders_.size());
    showFolder(((folder_ + by) % count + count) % count);
    cursor_ = -1; // nowhere yet, so that arriving on the first one counts as a move
    moveTo(names_.empty() ? 0 : 1, playing);
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
    if (b == Button::B) {
        // Held for a second on a recording: it is deleted, for good (hold B deletes for good on every
        // screen). Let go sooner: a tap, which puts back what was there before the list was opened.
        if (event.down) {
            bDown_ = true;
            deleting_ = cursor_ > 0;
            deleteTimed_ = false;
            deletePart_ = 0.f;
        } else if (bDown_) {
            bDown_ = false;
            deleting_ = false;
            deletePart_ = 0.f;
            // As it was. (Nothing to do if the cursor is back where it began.)
            const std::string now = cursor_ == 0 ? "" : names_[static_cast<std::size_t>(cursor_ - 1)];
            if (now != original_ && jobs_.use) jobs_.use(original_);
            open_ = false;
        }
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    deleting_ = false; // any other button calls it off
    deletePart_ = 0.f;
    if (b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right) {
        moveTo(cursor_ + (b == Button::Up ? -1 : b == Button::Down ? 1 : b == Button::Left ? -kPage : kPage), playing);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::L1 || b == Button::R1) {
        turnFolder(b == Button::R1 ? 1 : -1, playing);
    } else if (b == Button::A) {
        open_ = false; // what is showing is already on
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
    if (!input.held(Button::B)) deleting_ = false, deletePart_ = 0.f;
    if (deleting_) {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        if (deletePart_ >= 1.f && cursor_ > 0) {
            deleting_ = false;
            bDown_ = false; // this hold was a delete: letting go is not a tap
            deletePart_ = 0.f;
            const std::string gone = names_[static_cast<std::size_t>(cursor_ - 1)];
            if (jobs_.remove && jobs_.remove(gone)) {
                names_.erase(names_.begin() + (cursor_ - 1));
                all_.erase(std::remove(all_.begin(), all_.end(), gone), all_.end());
                if (original_ == gone) original_.clear(); // B cannot bring back what is no longer there
                // The last one of a folder: the folder has gone too (unless it is the only one there is).
                if (names_.empty() && folders_.size() > 1) {
                    folders_.erase(folders_.begin() + folder_);
                    showFolder(folder_ >= static_cast<int>(folders_.size()) ? 0 : folder_);
                    cursor_ = -1;
                    moveTo(names_.empty() ? 0 : 1, playing);
                    return;
                }
                // The row that takes its place (or the one before, at the end) goes on instead.
                const int to = cursor_ >= count() ? count() - 1 : cursor_;
                cursor_ = -1;
                moveTo(to, playing);
            }
        }
        return;
    }
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

    // The folder showing, when there is more than one: its name between the two buttons that turn it.
    const bool foldered = folders_.size() > 1;
    if (foldered) {
        const int fy = y + 30, fh = 22;
        box(r, x + 12, fy, w - 24, fh, kPanel2, kLine);
        const float ty = fy + (fh - fonts.small.capHeight()) / 2.f;
        const auto chip = [&](int cx, const char* label) {
            const int cw = static_cast<int>(fonts.small.width(label)) + 10;
            fill(r, cx, fy + 3, cw, fh - 6, kBrass);
            fonts.small.draw(r, cx + 5.f, ty, label, kBg);
        };
        chip(x + 16, "L1");
        chip(x + w - 16 - static_cast<int>(fonts.small.width("R1")) - 10, "R1");
        std::string label = folders_[static_cast<std::size_t>(folder_)].empty() ? "UNSORTED" : folders_[static_cast<std::size_t>(folder_)];
        for (char& c : label) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        std::snprintf(text, sizeof text, "FOLDER %d OF %d", folder_ + 1, static_cast<int>(folders_.size()));
        // The folder's name is in the middle, with "FOLDER 2 OF 12" ending at a fixed place to its
        // right: where the two would meet at XL, each is held at LARGE (Fonts::within).
        const Font& countFont = fonts.within(&Fonts::small, text, fonts.fixed().small.width(text));
        const float labelRoom = 2.f * (w / 2.f - 56.f - countFont.width(text) - 8.f); // what the count leaves it, about the middle
        const Font& labelFont = fonts.within(&Fonts::medium, label.c_str(), labelRoom < w - 200 ? labelRoom : w - 200);
        while (!label.empty() && labelFont.width(label.c_str()) > w - 200) label.pop_back();
        labelFont.drawCentered(r, x + w / 2.f, fy + (fh - labelFont.capHeight()) / 2.f, label.c_str(), kTeal);
        countFont.drawRight(r, x + w - 56.f, fy + (fh - countFont.capHeight()) / 2.f, text, kDim);
    }

    // The one that is on: its outline and how long it is. (Shorter when the folders take a line.)
    const int wx = x + 12, wy = y + (foldered ? 58 : 32), ww = w - 24, wh = foldered ? 40 : 56;
    box(r, wx, wy, ww, wh, kBg, kLine);
    const Session::LaneSound* sound = jobs_.sound ? jobs_.sound() : nullptr;
    if (cursor_ > 0 && sound && !sound->peaks.empty()) {
        const int n = static_cast<int>(sound->peaks.size());
        for (int px = 0; px < ww - 4; ++px) {
            const float p = sound->peaks[static_cast<std::size_t>(px * n / (ww - 4))];
            const int half = static_cast<int>(std::round((p > 1.f ? 1.f : p) * (wh - 8) / 2.f));
            fill(r, wx + 2 + px, wy + wh / 2 - half, 1, 2 * half + 1, kTeal);
        }
        if (const Playhead* head = jobs_.playhead ? jobs_.playhead() : nullptr) drawPlayhead(r, *head, lastMs_, wx + 2, wy + 2, ww - 4, wh - 4);
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
        std::string name = i == 0 ? first_ : sampleLabel(names_[static_cast<std::size_t>(i - 1)]); // the folder is said above
        for (char& c : name) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        const Font& rowFont = fonts.within(&Fonts::medium, name.c_str(), w - 48.f); // (a long name at XL: held at LARGE, and cut as at LARGE)
        while (!name.empty() && rowFont.width(name.c_str()) > w - 48) name.pop_back();
        rowFont.draw(r, x + 24.f, by + (rowH - 4 - rowFont.capHeight()) / 2.f, name.c_str(), here ? kTeal : (i == 0 ? kDim : kText));
        if (here && deletePart_ > 0.f) {
            fill(r, x + 13, by + 1, static_cast<int>((w - 26) * (deletePart_ > 1.f ? 1.f : deletePart_)), rowH - 6, faded(kStop, 150));
            fonts.small.drawRight(r, x + w - 24.f, by + (rowH - 4 - fonts.small.capHeight()) / 2.f, "DELETING: CANNOT BE UNDONE", kText);
        }
        if (here) outline(r, x + 11, by - 1, w - 22, rowH - 2, kTeal, 2);
    }
    if (all_.empty()) {
        fonts.small.draw(r, x + 24.f, ly + rowH + 8.f, "No recordings yet. Send .wav files over the WiFi page,", kDim);
        fonts.small.draw(r, x + 24.f, ly + rowH + 24.f, "catch a bar with Y, or record with R1.", kDim);
    }
    // A bar down the side says where in the list this is.
    if (count() > kRows) {
        const int track = kRows * rowH - 4, bar = track * kRows / count();
        fill(r, x + w - 8, ly, 3, track, kLine);
        fill(r, x + w - 8, ly + (track - bar) * first / (count() - kRows), 3, bar, kTeal);
    }
    const Hint list[] = {{"A", "keep it"}, {"B", "as it was"}, {"UP / DOWN", playing_ ? "next, in the beat" : "next"}, {foldered ? "L1 / R1" : "LEFT / RIGHT", foldered ? "folder" : "8 at a time"},
                         {"HOLD B", cursor_ > 0 ? "delete it" : nullptr}, {foldered ? "LEFT / RIGHT" : nullptr, "8 at a time"}};
    hints(r, fonts, list, 6);
}

} // namespace apotheca
