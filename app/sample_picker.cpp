#include "sample_picker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "samples.hpp"

namespace apotheca {

using namespace ui;

namespace {
std::string upper(std::string text) {
    for (char& c : text) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return text;
}
// A place's name at the head of its column.
std::string placeName(int depth, const std::string& folder) { return depth == 0 ? "SAMPLES" : folder.empty() ? "UNSORTED" : upper(sampleLabel(folder)); }
// A place's key, to remember where the cursor was in it.
std::string placeKey(int depth, const std::string& folder) { return static_cast<char>('0' + depth) + folder; }
// A row's key: a folder and a recording of one name are two rows.
std::string rowKey(const SamplePicker::Row& row) { return static_cast<char>('0' + static_cast<int>(row.kind)) + row.name; }
// The first row to show of `count`, to keep row `lit` in view.
int firstShown(int lit, int count) {
    int first = lit - SamplePicker::kRows / 2;
    first = first > count - SamplePicker::kRows ? count - SamplePicker::kRows : first;
    return first < 0 ? 0 : first;
}
} // namespace

std::vector<SamplePicker::Row> SamplePicker::rowsAt(int depth, const std::string& folder) const {
    std::vector<Row> rows;
    if (depth == 0) {
        rows.push_back({Kind::First, "", 0});
        int loose = 0;
        for (const std::string& name : all_) loose += sampleFolderOf(name).empty();
        if (flat()) { // no folders at all: the recordings themselves
            for (const std::string& name : all_) rows.push_back({Kind::Recording, name, 0});
            return rows;
        }
        if (loose) rows.push_back({Kind::Folder, "", loose});
    }
    // The folders in it (a folder in a folder holds none), each with how many recordings are under it.
    if (depth == 0 || (depth == 1 && !folder.empty())) {
        for (const std::string& dir : dirs_) {
            if (sampleFolderOf(dir) != folder) continue;
            int count = 0;
            for (const std::string& name : all_) count += name.compare(0, dir.size() + 1, dir + "/") == 0;
            rows.push_back({Kind::Folder, dir, count});
        }
    }
    if (depth > 0)
        for (const std::string& name : all_)
            if (sampleFolderOf(name) == folder) rows.push_back({Kind::Recording, name, 0});
    return rows;
}

void SamplePicker::show(int depth, const std::string& folder) {
    depth_ = depth;
    here_ = folder;
    rows_ = rowsAt(depth, folder);
    cursor_ = 0;
    const auto was = last_.find(placeKey(depth, folder));
    if (was != last_.end())
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rowKey(rows_[i]) == was->second) cursor_ = static_cast<int>(i);
}

void SamplePicker::open(std::vector<std::string> names, std::vector<std::string> folders, const std::string& current, const char* title, const char* first, Jobs jobs) {
    all_ = std::move(names);
    original_ = on_ = current;
    title_ = title;
    first_ = first;
    jobs_ = std::move(jobs);
    // Every folder there is: those on the card, and those the names speak of (and the folders those are in).
    dirs_ = std::move(folders);
    for (const std::string& name : all_)
        for (std::string in = sampleFolderOf(name); !in.empty(); in = sampleFolderOf(in)) dirs_.push_back(in);
    for (std::size_t i = 0, n = dirs_.size(); i < n; ++i)
        if (!sampleFolderOf(dirs_[i]).empty()) dirs_.push_back(sampleFolderOf(dirs_[i]));
    std::sort(dirs_.begin(), dirs_.end());
    dirs_.erase(std::unique(dirs_.begin(), dirs_.end()), dirs_.end());
    last_.clear();
    // Open in the folder the recording that is on is in, with the cursor on it.
    show(0, "");
    if (!current.empty() && !flat()) {
        const std::string in = sampleFolderOf(current);
        const int depth = 1 + static_cast<int>(std::count(in.begin(), in.end(), '/')); // (a loose one: in UNSORTED)
        const std::vector<Row> there = rowsAt(depth, in);
        for (const Row& row : there)
            if (row.kind == Kind::Recording && row.name == current) show(depth, in);
    }
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].kind == Kind::Recording && rows_[i].name == current) cursor_ = static_cast<int>(i);
    waiting_ = false;
    arrivedAt_ = 0;
    saved_ = 0.f;
    stickWas_ = false;
    repeating_ = Button::None;
    bDown_ = false;
    deleting_ = false;
    deletePart_ = 0.f;
    open_ = true;
}

// What the cursor is on goes on now, and is played if the song is not doing that already.
void SamplePicker::put(bool playing) {
    waiting_ = false;
    const Row* row = under();
    if (!row) return; // (never a folder: arrive() and the buttons see to that)
    on_ = row->kind == Kind::First ? "" : row->name;
    if (jobs_.use) jobs_.use(on_);
    if (!playing && jobs_.hear) jobs_.hear(false);
}

// The cursor has come to a row. A recording (or the first row) goes on: at once, unless rows are
// going by faster than one every kAtOnceMs (the stick pushed far, a D-pad held a while), when the
// one the cursor rests on does (update). A folder puts nothing on: the lane keeps what it had.
void SamplePicker::arrive(bool playing, bool atOnce) {
    const Uint32 since = lastMs_ - arrivedAt_;
    arrivedAt_ = lastMs_;
    waiting_ = false;
    const Row* row = under();
    if (!row || row->kind == Kind::Folder) return;
    if (atOnce || since >= kAtOnceMs) put(playing);
    else waiting_ = true;
}

// The ends stop.
void SamplePicker::moveTo(int row, bool playing, bool atOnce) {
    row = row >= count() ? count() - 1 : row;
    row = row < 0 ? 0 : row;
    if (row == cursor_ || count() == 0) return;
    cursor_ = row;
    arrive(playing, atOnce);
}

void SamplePicker::into(bool playing) {
    const Row* row = under();
    if (!row || row->kind != Kind::Folder || depth_ >= kSampleFolderLevels) return;
    const std::string folder = row->name;
    last_[placeKey(depth_, here_)] = rowKey(*row);
    show(depth_ + 1, folder);
    arrive(playing, true); // its first row (or the one the cursor was on before) goes on, as arriving on any row does
}

void SamplePicker::out() {
    if (depth_ == 0) return;
    const std::string was = here_;
    if (const Row* row = under()) last_[placeKey(depth_, here_)] = rowKey(*row);
    show(depth_ - 1, depth_ == 2 ? sampleFolderOf(was) : "");
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].kind == Kind::Folder && rows_[i].name == was) cursor_ = static_cast<int>(i);
    waiting_ = false; // on a folder: the lane keeps what it had
}

int SamplePicker::step(Button b) const { return b == Button::Up ? -1 : b == Button::Down ? 1 : b == Button::L1 ? -kPage : b == Button::R1 ? kPage : 0; }

void SamplePicker::onButton(const ButtonEvent& event, bool playing) {
    if (!open_) return;
    const Button b = event.button;
    const Row* row = under();
    if (b == Button::B) {
        // Held for a second on a recording: it is deleted, for good (hold B deletes for good on every
        // screen). Let go sooner: a tap, which puts back what was there before the list was opened.
        if (event.down) {
            bDown_ = true;
            deleting_ = row && row->kind == Kind::Recording;
            deleteTimed_ = false;
            deletePart_ = 0.f;
        } else if (bDown_) {
            bDown_ = false;
            deleting_ = false;
            deletePart_ = 0.f;
            waiting_ = false;
            // As it was. (Nothing to do if what is on is what was on.)
            if (on_ != original_ && jobs_.use) jobs_.use(original_);
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
    if (b == Button::Up || b == Button::Down) {
        moveTo(cursor_ + step(b), playing, true);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::L1 || b == Button::R1) {
        moveTo(cursor_ + step(b), playing, true);
    } else if (b == Button::Left) {
        out();
    } else if (b == Button::Right) {
        into(playing);
    } else if (b == Button::A) {
        if (row && row->kind == Kind::Folder) {
            into(playing);
        } else if (row) {
            if (waiting_) put(true); // (the one showing, if it had not gone on yet; kept, not played again)
            open_ = false;
        }
    } else if (b == Button::X) {
        if (!row || row->kind == Kind::Folder) return;
        if (waiting_) put(true);
        if (jobs_.hear) jobs_.hear(true); // for as long as X is held
    }
}

void SamplePicker::update(const Input& input, Uint32 nowMs, bool playing) {
    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed;
    lastMs_ = nowMs;
    if (!open_) return;
    if (waiting_ && nowMs - arrivedAt_ >= kAtOnceMs) put(playing); // the cursor has come to rest
    if (!input.held(Button::B)) deleting_ = false, deletePart_ = 0.f;
    if (deleting_) {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        const Row* row = under();
        if (deletePart_ >= 1.f && row && row->kind == Kind::Recording) {
            deleting_ = false;
            bDown_ = false; // this hold was a delete: letting go is not a tap
            deletePart_ = 0.f;
            const std::string gone = row->name;
            if (jobs_.remove && jobs_.remove(gone)) {
                all_.erase(std::remove(all_.begin(), all_.end(), gone), all_.end());
                if (original_ == gone) original_.clear(); // B cannot bring back what is no longer there
                if (on_ == gone) on_.clear();
                // The row that takes its place (or the one before, at the end) goes on instead. Its
                // folder stays, though that was its last: an empty folder is deleted by itself.
                const int at = cursor_;
                rows_ = rowsAt(depth_, here_);
                cursor_ = at >= count() ? count() - 1 : at;
                cursor_ = cursor_ < 0 ? 0 : cursor_;
                arrive(playing, true);
            }
        }
        return;
    }
    const float y = input.stickY();
    if (y == 0.f) {
        saved_ = 0.f;
        stickWas_ = false;
    } else {
        saved_ += stickWas_ ? std::fabs(y) * kStickRowsPerSecond * dt : 1.f;
        stickWas_ = true;
        for (; saved_ >= 1.f; saved_ -= 1.f) moveTo(cursor_ + (y > 0.f ? 1 : -1), playing, false);
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kListDelayMs;
        repeatSince_ = nowMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + (nowMs - repeatSince_ >= kFasterAfterMs ? kFasterEveryMs : kListEveryMs);
        moveTo(cursor_ + step(repeating_), playing, false);
    }
}

// One column's rows, from row `first`. `lit` is the row picked out (-1: none); `cursor`: this is
// the column the cursor is in.
void SamplePicker::drawColumn(SDL_Renderer* r, const Fonts& fonts, int x, int w, const std::vector<Row>& rows, int lit, bool cursor, int first) const {
    const int n = static_cast<int>(rows.size());
    char text[16];
    for (int shown = 0; shown < kRows && first + shown < n; ++shown) {
        const int i = first + shown, by = kRowsY + shown * kRowH;
        const Row& row = rows[static_cast<std::size_t>(i)];
        const bool here = i == lit;
        box(r, x, by, w, kRowH - 4, here ? kPanel2 : kPanel, kLine);
        if (cursor && here && deletePart_ > 0.f) {
            const char* words = "DELETING: CANNOT BE UNDONE";
            fill(r, x + 1, by + 1, static_cast<int>((w - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), kRowH - 6, faded(kStop, 150));
            const Font& font = fonts.within(&Fonts::small, words, w - 16.f);
            font.draw(r, x + 8.f, by + (kRowH - 4 - font.capHeight()) / 2.f, words, kText);
            outline(r, x - 1, by - 1, w + 2, kRowH - 2, kTeal, 2);
            continue;
        }
        // A folder says how many recordings are under it, at the right. (Its room is LARGE's at every size, so a name is cut as at LARGE.)
        float room = w - 20.f;
        if (row.kind == Kind::Folder) {
            std::snprintf(text, sizeof text, "%d", row.count);
            const float held = fonts.fixed().small.width(text);
            const Font& font = fonts.within(&Fonts::small, text, held);
            font.drawRight(r, x + w - 10.f, by + (kRowH - 4 - font.capHeight()) / 2.f, text, kDim);
            room -= held + 8.f;
        }
        std::string name = row.kind == Kind::First ? first_ : row.kind == Kind::Folder && row.name.empty() ? "UNSORTED" : upper(sampleLabel(row.name));
        const Font& font = fonts.within(&Fonts::medium, name.c_str(), room); // (a long name at XL: held at LARGE, and cut as at LARGE)
        while (!name.empty() && font.width(name.c_str()) > room) name.pop_back();
        font.draw(r, x + 10.f, by + (kRowH - 4 - font.capHeight()) / 2.f, name.c_str(), here ? kTeal : (!cursor || row.kind == Kind::First ? kDim : kText));
        if (cursor && here) outline(r, x - 1, by - 1, w + 2, kRowH - 2, kTeal, 2);
    }
}

void SamplePicker::draw(SDL_Renderer* r, const Fonts& fonts) const {
    if (!open_) return;
    fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 215));
    box(r, kX, kY, kW, kH, kPanel, kLine);
    char text[64];
    fonts.small.draw(r, kX + 12.f, kY + 11.f, title_.c_str(), kDim);
    const Row* row = under();
    if (row) {
        std::snprintf(text, sizeof text, "%d OF %d", cursor_ + 1, count());
        fonts.small.drawRight(r, kX + kW - 12.f, kY + 11.f, text, kDim);
    }
    // Each column's name at its head.
    const auto head = [&](int x, int w, std::string name, SDL_Color color) {
        const Font& font = fonts.within(&Fonts::small, name.c_str(), w - 20.f);
        while (!name.empty() && font.width(name.c_str()) > w - 20.f) name.pop_back();
        font.draw(r, x + 10.f, kHeadY + (16 - font.capHeight()) / 2.f, name.c_str(), color);
        fill(r, x, kHeadY + 17, w, 1, kLine);
    };

    // Where this folder is: the folder it is in, this one picked out.
    if (depth_ > 0) {
        const std::string parent = depth_ == 2 ? sampleFolderOf(here_) : "";
        const std::vector<Row> rows = rowsAt(depth_ - 1, parent);
        int lit = -1;
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].kind == Kind::Folder && rows[i].name == here_) lit = static_cast<int>(i);
        head(kLeftX, kLeftW, placeName(depth_ - 1, parent), kDim);
        drawColumn(r, fonts, kLeftX, kLeftW, rows, lit, false, firstShown(lit < 0 ? 0 : lit, static_cast<int>(rows.size())));
    }

    // This folder, scrolled to keep the cursor in view.
    const int first = firstShown(cursor_, count());
    head(kMidX, kMidW, placeName(depth_, here_), kText);
    drawColumn(r, fonts, kMidX, kMidW, rows_, cursor_, true, first);
    if (!row) fonts.small.draw(r, kMidX + 10.f, kRowsY + 8.f, "NOTHING IN IT", kDim);
    if (count() > kRows) { // a bar down its side says where in the list this is
        const int track = kRows * kRowH - 4, bar = track * kRows / count();
        fill(r, kMidX + kMidW + 3, kRowsY, 3, track, kLine);
        fill(r, kMidX + kMidW + 3, kRowsY + (track - bar) * first / (count() - kRows), 3, bar, kTeal);
    }

    // What is under the cursor: a folder's rows, or a recording's picture and how long it is.
    if (all_.empty() && dirs_.empty()) {
        fonts.small.draw(r, kMidX + 10.f, kRowsY + kRowH + 8.f, "No recordings yet. Send .wav files over the WiFi page,", kDim);
        fonts.small.draw(r, kMidX + 10.f, kRowsY + kRowH + 24.f, "catch a bar with Y, or record with R1.", kDim);
    } else if (row && row->kind == Kind::Folder) {
        const std::vector<Row> rows = rowsAt(depth_ + 1, row->name);
        head(kRightX, kRightW, placeName(depth_ + 1, row->name), kDim);
        drawColumn(r, fonts, kRightX, kRightW, rows, -1, false, 0);
        if (rows.empty()) fonts.small.draw(r, kRightX + 10.f, kRowsY + 8.f, "NOTHING IN IT", kDim);
    } else if (row) {
        const int wx = kRightX, wy = kRowsY, ww = kRightW, wh = kPictureH;
        head(kRightX, kRightW, row->kind == Kind::First ? first_ : upper(sampleLabel(row->name)), kDim);
        box(r, wx, wy, ww, wh, kBg, kLine);
        const Session::LaneSound* sound = jobs_.sound ? jobs_.sound() : nullptr;
        if (row->kind == Kind::Recording && !waiting_ && sound && !sound->peaks.empty()) {
            const int n = static_cast<int>(sound->peaks.size());
            for (int px = 0; px < ww - 4; ++px) {
                const float p = sound->peaks[static_cast<std::size_t>(px * n / (ww - 4))];
                const int half = static_cast<int>(std::round((p > 1.f ? 1.f : p) * (wh - 8) / 2.f));
                fill(r, wx + 2 + px, wy + wh / 2 - half, 1, 2 * half + 1, kTeal);
            }
            if (const Playhead* playhead = jobs_.playhead ? jobs_.playhead() : nullptr) drawPlayhead(r, *playhead, lastMs_, wx + 2, wy + 2, ww - 4, wh - 4);
            std::snprintf(text, sizeof text, "%.2f S", static_cast<double>(sound->seconds));
            fonts.fixed().small.drawRight(r, wx + ww - 6.f, wy + 5.f, text, kText); // (over the recording's picture: always at LARGE)
        } else {
            const char* words = row->kind == Kind::First ? first_.c_str() : "...";
            const Font& font = fonts.within(&Fonts::small, words, ww - 12.f);
            font.drawCentered(r, wx + ww / 2.f, wy + (wh - font.capHeight()) / 2.f, words, kDim);
        }
        // Where else in the open song it is on.
        const int used = row->kind == Kind::Recording && jobs_.used ? jobs_.used(row->name) : 0;
        if (used > 0) {
            std::snprintf(text, sizeof text, "ALSO IN THIS SONG: %d PLACE%s", used, used == 1 ? "" : "S");
            const Font& font = fonts.within(&Fonts::small, text, ww - 4.f);
            font.draw(r, wx + 2.f, wy + wh + 10.f, text, kDim);
        }
    }

    // (Up / down is not said: the bar is full, and the help card says the D-pad and the stick move.)
    const bool folder = row && row->kind == Kind::Folder, recording = row && row->kind == Kind::Recording;
    const Hint list[] = {{"A", !row ? nullptr : folder ? "open" : "keep it"}, {"B", "as it was"}, {flat() ? nullptr : row ? "LEFT / RIGHT" : "LEFT", row ? "out, in" : "out"},
                         {row ? "L1 / R1" : nullptr, "page"}, {"HOLD B", recording ? "delete it" : nullptr}};
    hints(r, fonts, list, 5);
}

} // namespace apotheca
