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
// Where a carried recording goes, by name: a folder, or out of every folder.
std::string destName(const std::string& folder) { return folder.empty() ? "UNSORTED" : upper(sampleLabel(folder)); }
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
    ++rowsMade_;
    if (depth == 0) {
        rows.push_back({Kind::First, "", 0});
        const int loose = loose_;
        if (flat()) { // no folders at all: the recordings themselves
            for (const std::string& name : all_) rows.push_back({Kind::Recording, name, 0});
            if (jobs_.makeFolder) rows.push_back({Kind::NewFolder, "", 0});
            return rows;
        }
        if (loose) rows.push_back({Kind::Folder, "", loose});
    }
    // The folders in it (a folder in a folder holds none), each with how many recordings are under it.
    if (depth == 0 || (depth == 1 && !folder.empty())) {
        for (const std::string& dir : dirs_) {
            if (sampleFolderOf(dir) != folder) continue;
            const auto count = counts_.find(dir);
            rows.push_back({Kind::Folder, dir, count == counts_.end() ? 0 : count->second});
        }
    }
    if (depth > 0)
        for (const std::string& name : all_)
            if (sampleFolderOf(name) == folder) rows.push_back({Kind::Recording, name, 0});
    // The last row of a place a folder can be made in: SAMPLES, and a folder of the first level.
    if (jobs_.makeFolder && (depth == 0 || (depth == 1 && !folder.empty()))) rows.push_back({Kind::NewFolder, "", 0});
    return rows;
}

// The card's lists changed: how many recordings are under each folder, counted once (a column's
// rows ask for each folder's; counted there it was folders times recordings, every frame).
void SamplePicker::index() {
    counts_.clear();
    loose_ = 0;
    ++changes_;
    for (const std::string& name : all_) {
        loose_ += sampleFolderOf(name).empty();
        for (std::string in = sampleFolderOf(name); !in.empty(); in = sampleFolderOf(in)) ++counts_[in];
    }
}

// A side column's rows: worked out when the place or the card has changed, not every frame.
const std::vector<SamplePicker::Row>& SamplePicker::side(std::vector<Row>& rows, std::string& key, int depth, const std::string& folder) const {
    const std::string now = std::to_string(changes_) + ":" + placeKey(depth, folder);
    if (now != key) rows = rowsAt(depth, folder), key = now;
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
    index();
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
    refusing_ = false;
    deletePart_ = 0.f;
    r1Down_ = false;
    carried_.clear();
    typing_ = false;
    said_.clear();
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
    if (!row || row->kind == Kind::Folder || row->kind == Kind::NewFolder) return;
    if (!carried_.empty()) return; // carrying: the rows are only steered over
    if (atOnce || since >= kAtOnceMs) put(playing);
    else waiting_ = true;
}

// The ends stop.
void SamplePicker::moveTo(int row, bool playing, bool atOnce) {
    row = row >= count() ? count() - 1 : row;
    row = row < 0 ? 0 : row;
    if (row == cursor_ || count() == 0) return;
    refusing_ = false; // (B held on a folder, and the stick moved off it)
    // R1 is held on a recording and the cursor is about to leave it (the stick, a D-pad already held): it is carried.
    if (r1Down_ && carried_.empty() && carries(under())) carried_ = under()->name;
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

// Four rows of ten, as a track's name is typed on the SET page.
const char* const SamplePicker::kKeys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_ ";

// The rows again, after the card changed. The cursor stays on the row it was on (or goes to the row
// of key `onto`), else where it was in the column.
void SamplePicker::refresh(const std::string& onto) {
    const Row* row = under();
    const std::string key = !onto.empty() ? onto : row ? rowKey(*row) : "";
    const int at = cursor_;
    rows_ = rowsAt(depth_, here_);
    cursor_ = at >= count() ? count() - 1 : at;
    cursor_ = cursor_ < 0 ? 0 : cursor_;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rowKey(rows_[i]) == key) cursor_ = static_cast<int>(i);
}

// A folder hold B deletes: one with nothing under it, no folder either. (UNSORTED is listed only with something in it.)
bool SamplePicker::emptyFolder(const Row& row) const {
    if (row.kind != Kind::Folder || row.count != 0 || !jobs_.removeFolder) return false;
    for (const std::string& dir : dirs_)
        if (sampleFolderOf(dir) == row.name) return false;
    return true;
}

// Where the carried recording would go: the folder under the cursor, else the folder showing.
std::string SamplePicker::carryTo() const {
    const Row* row = under();
    return row && row->kind == Kind::Folder ? row->name : depth_ == 0 ? "" : here_;
}

// R1 let go: the carried recording goes where the top line said.
void SamplePicker::drop(bool playing) {
    const std::string from = carried_, folder = carryTo();
    const std::string to = folder.empty() ? sampleLabel(from) : folder + "/" + sampleLabel(from);
    carried_.clear();
    const Session::FileJob did = jobs_.move ? jobs_.move(from, to) : Session::FileJob::Failed;
    const int failuresBefore = jobs_.followFailures ? jobs_.followFailures() : 0;
    if (did == Session::FileJob::Done || did == Session::FileJob::NotSaved || did == Session::FileJob::Moving) { // (not saved: it HAS moved)
        std::replace(all_.begin(), all_.end(), from, to);
        std::sort(all_.begin(), all_.end());
        index();
        if (original_ == from) original_ = to; // (the lane has followed: B's "as it was" is the same recording)
        if (on_ == from) on_ = to;
        // In the folder it went to: the cursor on it. On that folder's row: the row, which counts one more.
        const bool there = depth_ > 0 && here_ == folder;
        refresh(there ? rowKey({Kind::Recording, to, 0}) : "");
        if (did == Session::FileJob::Moving) { // the saved songs follow a few a frame: said until they have (update)
            if (!moving_) failuresBefore_ = failuresBefore;
            moving_ = true;
            movingTo_ = destName(folder);
            say("MOVING TO " + movingTo_ + "...");
        } else {
            say(did == Session::FileJob::Done ? "MOVED TO " + destName(folder) : "MOVED, BUT A SONG COULD NOT BE SAVED WITH ITS NEW PLACE");
        }
    } else if (did == Session::FileJob::TooLong) {
        say("NOT MOVED: ITS NAME WOULD BE TOO LONG THERE");
    } else if (did == Session::FileJob::Taken) {
        say("NOT MOVED: ONE OF THAT NAME IS THERE");
    } else if (did == Session::FileJob::Same) {
        say("LEFT WHERE IT WAS"); // (R1 held too long for a page, or let go in its own folder: said, so it is not a mystery)
    } else {
        say("COULD NOT MOVE IT");
    }
    arrive(playing, true); // what the cursor is on now goes on, as arriving on any row does
}

void SamplePicker::moveKey(Button direction) {
    const int rows = kKeyCount / kKeyColumns;
    int col = key_ % kKeyColumns, row = key_ / kKeyColumns;
    if (direction == Button::Left) col = (col + kKeyColumns - 1) % kKeyColumns;
    if (direction == Button::Right) col = (col + 1) % kKeyColumns;
    if (direction == Button::Up) row = (row + rows - 1) % rows;
    if (direction == Button::Down) row = (row + 1) % rows;
    key_ = row * kKeyColumns + col;
}

// The name typed is kept: the folder is made (in small letters, as the folders the program makes
// are; every name is shown in capitals), and the cursor goes onto it.
void SamplePicker::keepFolder() {
    std::string name = typed_;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    for (char& c : name) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    if (name.empty()) {
        typing_ = false;
        return;
    }
    const std::string whole = typingIn_.empty() ? name : typingIn_ + "/" + name;
    const Session::FileJob did = jobs_.makeFolder ? jobs_.makeFolder(whole) : Session::FileJob::Failed;
    if (did == Session::FileJob::Done) {
        dirs_.push_back(whole);
        std::sort(dirs_.begin(), dirs_.end());
        index();
        typing_ = false;
        refresh(rowKey({Kind::Folder, whole, 0}));
        say("MADE " + upper(name));
    } else {
        say(did == Session::FileJob::Taken ? "THERE IS ONE OF THAT NAME" : did == Session::FileJob::TooLong ? "THAT NAME IS TOO LONG HERE" : "COULD NOT MAKE IT");
    }
}

void SamplePicker::typeButton(Button b) {
    if (isDirection(b)) {
        moveKey(b);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::A) {
        const char c = kKeys[key_];
        // No space first, and not two together.
        const bool space = c == ' ';
        if (static_cast<int>(typed_.size()) < kFolderNameLength && !(space && (typed_.empty() || typed_.back() == ' '))) typed_ += c;
    } else if (b == Button::Y) {
        if (!typed_.empty()) typed_.pop_back(); // the last letter goes
    } else if (b == Button::B || b == Button::Start) {
        keepFolder(); // B leaves keeping what is typed, as on the SET page; nothing typed: no folder is made
    }
}

int SamplePicker::step(Button b) const { return b == Button::Up ? -1 : b == Button::Down ? 1 : b == Button::L1 ? -kPage : b == Button::R1 ? kPage : 0; }

void SamplePicker::onButton(const ButtonEvent& event, bool playing) {
    if (!open_) return;
    const Button b = event.button;
    const Row* row = under();
    if (typing_) { // a name is being typed: every button is its
        if (event.down) typeButton(b);
        else if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (b == Button::R1) {
        // On a recording it waits: let go soon, a page; held (or a direction with it), the recording is carried.
        // (With no folders there is nowhere to carry to: a page at once, as on any other row.)
        if (event.down && carries(row)) {
            r1Down_ = true;
            r1Timed_ = false;
            deleting_ = refusing_ = false; // R1 calls a filling delete off
            deletePart_ = 0.f;
        } else if (event.down) {
            deleting_ = refusing_ = false;
            deletePart_ = 0.f;
            moveTo(cursor_ + kPage, playing, true);
            repeating_ = b; // held: page after page
            repeatAt_ = 0;
        } else if (r1Down_) {
            r1Down_ = false;
            if (carried_.empty()) moveTo(cursor_ + kPage, playing, true);
            else drop(playing);
        } else if (b == repeating_) {
            repeating_ = Button::None;
        }
        return;
    }
    if (r1Down_) {
        if (!event.down) {
            if (b == repeating_) repeating_ = Button::None;
            return;
        }
        if (b == Button::B) { // called off: nothing moves, and this B is no tap
            const bool was = !carried_.empty();
            r1Down_ = false;
            carried_.clear();
            if (was) arrive(playing, true);
            return;
        }
        if (!isDirection(b) && b != Button::L1) return; // nothing else while R1 is held
        if (carried_.empty() && carries(row)) carried_ = row->name, waiting_ = false; // a direction with R1: carried at once
    }
    if (b == Button::B) {
        // Held for a second on a recording: it is deleted, for good (hold B deletes for good on every
        // screen). Let go sooner: a tap, which puts back what was there before the list was opened.
        if (event.down) {
            bDown_ = true;
            deleting_ = row && (row->kind == Kind::Recording || emptyFolder(*row));
            refusing_ = row && row->kind == Kind::Folder && !deleting_; // not empty: held, it says so (update)
            deleteTimed_ = false;
            deletePart_ = 0.f;
        } else if (bDown_) {
            bDown_ = false;
            deleting_ = refusing_ = false;
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
    deleting_ = refusing_ = false; // any other button calls it off
    deletePart_ = 0.f;
    if (b == Button::Up || b == Button::Down) {
        moveTo(cursor_ + step(b), playing, true);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::L1) {
        moveTo(cursor_ + step(b), playing, true);
        repeating_ = b; // held: page after page
        repeatAt_ = 0;
    } else if (b == Button::Left) {
        out();
    } else if (b == Button::Right) {
        into(playing);
    } else if (b == Button::A) {
        if (row && row->kind == Kind::Folder) {
            into(playing);
        } else if (row && row->kind == Kind::NewFolder) {
            typing_ = true;
            typed_.clear();
            key_ = 0;
            typingIn_ = depth_ == 0 ? "" : here_;
            repeating_ = Button::None;
        } else if (row) {
            if (waiting_) put(true); // (the one showing, if it had not gone on yet; kept, not played again)
            open_ = false;
        }
    } else if (b == Button::X) {
        if (!row || row->kind == Kind::Folder || row->kind == Kind::NewFolder) return;
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
    if (moving_) {
        if (jobs_.following && jobs_.following()) {
            saidUntil_ = nowMs + kSaidMs; // MOVING TO stays while the songs follow
        } else {
            moving_ = false;
            const bool failed = jobs_.followFailures && jobs_.followFailures() != failuresBefore_;
            if (said_.compare(0, 7, "MOVING ") == 0 || said_.empty()) say(failed ? "MOVED, BUT A SONG COULD NOT BE SAVED WITH ITS NEW PLACE" : "MOVED TO " + movingTo_);
        }
    }
    if (!said_.empty() && nowMs >= saidUntil_) said_.clear();
    if (typing_) {
        // The stick moves over the keys, one at once and then steadily; a held D-pad direction runs.
        const float sx = input.stickX(), sy = input.stickY();
        const bool sideways = std::fabs(sx) > std::fabs(sy);
        const float push = sideways ? sx : sy;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * kKeysPerSecond * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f) moveKey(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
        }
        if (repeating_ != Button::None && repeatAt_ == 0) {
            repeatAt_ = nowMs + kListDelayMs;
        } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
            repeatAt_ = nowMs + kListEveryMs;
            moveKey(repeating_);
        }
        return;
    }
    // R1 held on a recording: after a moment it is carried. (Let go unseen: called off, nothing moves.)
    if (r1Down_ && !input.held(Button::R1)) r1Down_ = false, carried_.clear();
    if (r1Down_ && carried_.empty()) {
        if (!r1Timed_) r1Timed_ = true, r1Since_ = nowMs;
        const Row* row = under();
        if (nowMs - r1Since_ >= kCarryHoldMs && carries(row)) carried_ = row->name, waiting_ = false;
    }
    if (waiting_ && nowMs - arrivedAt_ >= kAtOnceMs) put(playing); // the cursor has come to rest
    if (!input.held(Button::B)) deleting_ = refusing_ = false, deletePart_ = 0.f; // (let go unseen: nothing is deleted)
    if (refusing_) { // B held on a folder that is not empty: no bar; after a moment it says why, and this hold is no tap
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        if (nowMs - deleteSince_ >= kHoldSaysMs) {
            refusing_ = false;
            bDown_ = false;
            say("NOT DELETED: IT IS NOT EMPTY");
        }
    }
    if (deleting_) {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        const Row* row = under();
        if (deletePart_ >= 1.f && row && emptyFolder(*row)) {
            deleting_ = false;
            bDown_ = false; // this hold was a delete: letting go is not a tap
            deletePart_ = 0.f;
            const std::string gone = row->name;
            const Session::FileJob did = jobs_.removeFolder(gone);
            if (did == Session::FileJob::Done) {
                dirs_.erase(std::remove(dirs_.begin(), dirs_.end(), gone), dirs_.end());
                index();
                last_.erase(placeKey(depth_ + 1, gone));
                refresh();
                arrive(playing, true);
            } else {
                say(did == Session::FileJob::NotEmpty ? "NOT DELETED: OTHER FILES ARE IN IT" : "COULD NOT DELETE IT");
            }
        } else if (deletePart_ >= 1.f && row && row->kind == Kind::Recording) {
            deleting_ = false;
            bDown_ = false; // this hold was a delete: letting go is not a tap
            deletePart_ = 0.f;
            const std::string gone = row->name;
            if (jobs_.remove && jobs_.remove(gone)) {
                all_.erase(std::remove(all_.begin(), all_.end(), gone), all_.end());
                index();
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
        // Big moves: by the square of the push, so a little push is slow enough to read each name
        // and all the way is a long list's length in a few seconds.
        saved_ += stickWas_ ? y * y * kStickRowsPerSecond * dt : 1.f;
        stickWas_ = true;
        for (; saved_ >= 1.f; saved_ -= 1.f) moveTo(cursor_ + (y > 0.f ? 1 : -1), playing, false);
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kListDelayMs;
        repeatSince_ = nowMs;
    } else if (repeating_ != Button::None && input.held(repeating_)) {
        // The D-pad: the list's pace, then faster, then fastest. A shoulder: a page at the list's pace.
        // Counted from when each was due, not from this frame: a pace faster than the frames is kept
        // (several rows a frame), and one a little slower is not rounded up to a whole frame more.
        const bool page = repeating_ == Button::L1 || repeating_ == Button::R1;
        if (nowMs > repeatAt_ + 100) repeatAt_ = nowMs; // (the program was held up: no burst to catch up)
        while (nowMs >= repeatAt_) {
            const Uint32 held = repeatAt_ - repeatSince_;
            repeatAt_ += page || held < kFasterAfterMs ? kListEveryMs : held < kFastestAfterMs ? kFasterEveryMs : kFastestEveryMs;
            moveTo(cursor_ + step(repeating_), playing, false);
        }
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
        std::string name = row.kind == Kind::First ? first_ : row.kind == Kind::NewFolder ? "+ NEW FOLDER" : row.kind == Kind::Folder && row.name.empty() ? "UNSORTED" : upper(sampleLabel(row.name));
        const bool carrying = row.kind == Kind::Recording && row.name == carried_; // the one being carried: brass, wherever the cursor is
        const Font& font = fonts.within(&Fonts::medium, name.c_str(), room); // (a long name at XL: held at LARGE, and cut as at LARGE)
        while (!name.empty() && font.width(name.c_str()) > room) name.pop_back();
        font.draw(r, x + 10.f, by + (kRowH - 4 - font.capHeight()) / 2.f, name.c_str(), carrying ? kBrass : here ? kTeal : (!cursor || row.kind == Kind::First || row.kind == Kind::NewFolder ? kDim : kText));
        if (cursor && here) outline(r, x - 1, by - 1, w + 2, kRowH - 2, kTeal, 2);
    }
}

void SamplePicker::draw(SDL_Renderer* r, const Fonts& fonts) const {
    if (!open_) return;
    fill(r, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - kHintsH - 1, faded(kBg, 215));
    box(r, kX, kY, kW, kH, kPanel, kLine);
    char text[64];
    if (typing_) {
        drawTyping(r, fonts);
        return;
    }
    const Row* row = under();
    float topRoom = kW - 24.f;
    if (row) {
        std::snprintf(text, sizeof text, "%d OF %d", cursor_ + 1, count());
        fonts.small.drawRight(r, kX + kW - 12.f, kY + 11.f, text, kDim);
        topRoom -= fonts.small.width(text) + 12.f;
    }
    // The top line: the list's title; or the recording being carried and where it would go; or what was just done.
    {
        std::string top = title_;
        SDL_Color color = kDim;
        if (!carried_.empty()) {
            top = "CARRYING TO " + destName(carryTo()) + ": " + upper(sampleLabel(carried_));
            color = kBrass;
        } else if (!said_.empty()) {
            top = said_;
            color = kText;
        }
        const Font& font = fonts.within(&Fonts::small, top.c_str(), topRoom);
        while (!top.empty() && font.width(top.c_str()) > topRoom) top.pop_back();
        font.draw(r, kX + 12.f, kY + 11.f, top.c_str(), color);
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
        const std::vector<Row>& rows = side(leftRows_, leftKey_, depth_ - 1, parent);
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
        fonts.small.draw(r, kMidX + 10.f, kRowsY + count() * kRowH + 8.f, "No recordings yet. Send .wav files over the WiFi page,", kDim);
        fonts.small.draw(r, kMidX + 10.f, kRowsY + count() * kRowH + 24.f, "catch a bar with Y, or record with R1.", kDim);
    } else if (row && row->kind == Kind::Folder) {
        const std::vector<Row>& rows = side(rightRows_, rightKey_, depth_ + 1, row->name);
        head(kRightX, kRightW, placeName(depth_ + 1, row->name), kDim);
        drawColumn(r, fonts, kRightX, kRightW, rows, -1, false, 0);
        if (rows.empty()) fonts.small.draw(r, kRightX + 10.f, kRowsY + 8.f, "NOTHING IN IT", kDim);
    } else if (row && row->kind == Kind::NewFolder) {
        head(kRightX, kRightW, "NEW FOLDER", kDim);
        fonts.small.draw(r, kRightX + 10.f, kRowsY + 8.f, "NOTHING IN IT", kDim);
    } else if (row) {
        const int wx = kRightX, wy = kRowsY, ww = kRightW, wh = kPictureH;
        head(kRightX, kRightW, row->kind == Kind::First ? first_ : upper(sampleLabel(row->name)), kDim);
        box(r, wx, wy, ww, wh, kBg, kLine);
        const Session::LaneSound* sound = jobs_.sound ? jobs_.sound() : nullptr;
        if (row->kind == Kind::Recording && !waiting_ && row->name == on_ && sound && !sound->peaks.empty()) { // (carried over another: that one is not on)
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
    const bool folder = row && row->kind == Kind::Folder, recording = row && row->kind == Kind::Recording, fresh = row && row->kind == Kind::NewFolder;
    if (!carried_.empty()) { // R1 is held: what letting go does, and the way out of it
        const Hint list[] = {{"R1", "let go: put it there"}, {"B", "call off"}, {flat() ? nullptr : "LEFT / RIGHT", "out, in"}};
        hints(r, fonts, list, 3);
        return;
    }
    // (On a recording L1 / R1's page gives its place to HOLD R1: the bar holds five.)
    const bool carries = this->carries(row);
    const Hint list[] = {{"A", !row ? nullptr : folder ? "open" : fresh ? "name it" : "keep it"}, {"B", "as it was"}, {flat() ? nullptr : row ? "LEFT / RIGHT" : "LEFT", row ? "out, in" : "out"},
                         {row && !carries ? "L1 / R1" : nullptr, "page"}, {"HOLD R1", carries ? "carry" : nullptr}, {"HOLD B", recording || (folder && emptyFolder(*row)) ? "delete it" : nullptr}};
    hints(r, fonts, list, 6);
}

// A new folder's name being typed: the keys a track's name is typed with, in the list's own panel.
void SamplePicker::drawTyping(SDL_Renderer* r, const Fonts& fonts) const {
    char text[32];
    std::string title = said_.empty() ? "NEW FOLDER IN " + placeName(typingIn_.empty() ? 0 : 1, typingIn_) + ": ITS NAME" : said_;
    const Font& head = fonts.within(&Fonts::small, title.c_str(), kScreenW - 80.f);
    while (!title.empty() && head.width(title.c_str()) > kScreenW - 80.f) title.pop_back();
    head.draw(r, 40.f, 46.f, title.c_str(), said_.empty() ? kDim : kText);
    // What has been typed, with a mark where the next letter goes.
    box(r, 40, 66, kScreenW - 80, 52, kPanel2, kTeal);
    const float tx = 56.f, ty = 66.f + (52.f - fonts.large.capHeight()) / 2.f;
    fonts.large.draw(r, tx, ty, typed_.c_str(), kText);
    if (static_cast<int>(typed_.size()) < kFolderNameLength) fill(r, static_cast<int>(tx + fonts.large.width(typed_.c_str()) + (typed_.empty() ? 0 : 3)), 66 + 14, 2, 24, kTeal);
    std::snprintf(text, sizeof text, "%d OF %d", static_cast<int>(typed_.size()), kFolderNameLength);
    fonts.small.drawRight(r, kScreenW - 56.f, 66.f + (52.f - fonts.small.capHeight()) / 2.f, text, kDim);
    const int kw = 52, kh = 52, gx = (kScreenW - kKeyColumns * kw) / 2, gy = 140;
    for (int i = 0; i < kKeyCount; ++i) {
        const int x = gx + (i % kKeyColumns) * kw, y = gy + (i / kKeyColumns) * kh;
        const bool here = i == key_;
        box(r, x + 2, y + 2, kw - 4, kh - 4, here ? kTeal : kPanel, here ? kTeal : kLine);
        char one[8];
        std::snprintf(one, sizeof one, "%s", kKeys[i] == ' ' ? "SPACE" : std::string(1, kKeys[i]).c_str());
        const Font& font = kKeys[i] == ' ' ? fonts.within(&Fonts::small, one, kw - 4 - 6.f) : fonts.large; // ("SPACE" at XL is wider than its key: held at LARGE)
        font.drawCentered(r, x + kw / 2.f, y + (kh - font.capHeight()) / 2.f, one, here ? kBg : kText);
    }
    const bool none = typed_.empty();
    const Hint list[] = {{"A", "type"}, {"Y", none ? nullptr : "delete"}, {"B", none ? "leave" : "make it"}};
    hints(r, fonts, list, 3);
}

} // namespace apotheca
