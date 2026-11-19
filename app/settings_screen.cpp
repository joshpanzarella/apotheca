#include "settings_screen.hpp"
#include "scales.hpp"

#include <string>

#include "song_screen.hpp"
#include "version.hpp"

#include <cmath>
#include <cstdio>

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kRelearnHoldMs = 1000; // A held this long on CONTROLS
constexpr Uint32 kTestQuietMs = 4000;   // the button test closes this long after the last press
constexpr Uint32 kRepeatDelayMs = ui::kListDelayMs, kRepeatEveryMs = ui::kListEveryMs;
constexpr float kMovesPerSecond = 7.f, kChangesPerSecond = 9.f;
}

// Four rows of ten: the letters, the digits, and the three marks a name may have. '_' at the end; the space is the wide one.
const char* const SettingsScreen::kKeys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_ ";

// The PASSWORD keys: three sets of forty. Every sign a WPA password may hold is on one of them.
const char* const SettingsScreen::kPasswordKeys[3] = {
    "abcdefghijklmnopqrstuvwxyz0123456789 -_.",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.",
    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ \x01\x01\x01\x01\x01\x01\x01",
};
const char* const SettingsScreen::kPasswordSetNames[3] = {"abc", "ABC", "#+="};

void SettingsScreen::passwordButton(Button b) {
    if (isDirection(b)) {
        moveKey(b);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::A) {
        const char c = kPasswordKeys[keySet_][key_];
        if (c != '\x01' && static_cast<int>(typed_.size()) < kWifiPasswordMost) typed_ += c;
    } else if (b == Button::Y) {
        if (!typed_.empty()) typed_.pop_back();
    } else if (b == Button::X) {
        keySet_ = (keySet_ + 1) % 3;
    } else if (b == Button::B || b == Button::Start) {
        // Keeps it and joins. Nothing typed: B leaves, and nothing is joined.
        if (typed_.empty()) {
            if (b == Button::B) typing_ = false, password_ = false;
        } else if (static_cast<int>(typed_.size()) < kWifiPasswordLeast) {
            networkSaid_ = "8 AT LEAST";
            networkSaidUntil_ = lastMs_ + 2500;
        } else {
            session_.wifi().join(passwordFor_, typed_);
            std::fill(typed_.begin(), typed_.end(), '\0'); // (it goes no further than the program's stdin)
            typed_.clear();
            typing_ = false, password_ = false;
        }
    }
}

// Hold A on NETWORK: the network before or after in the list (strongest first). The ends stop.
void SettingsScreen::pickStep(int by) {
    const int count = static_cast<int>(nets_.size());
    if (count == 0) return;
    // (Moved, even where an end stops it: the network the row is about may be picked again, as after a wrong password.)
    pickMoved_ = true;
    const int to = pick_ < 0 ? (by > 0 ? 0 : -1) : pick_ + by;
    if (to < 0 || to >= count) return;
    pick_ = to;
}

// A let go on a network picked: joined at once if it is open or the system has its password, else
// the keys come up for one.
void SettingsScreen::joinPicked() {
    if (pick_ < 0 || pick_ >= static_cast<int>(nets_.size())) return;
    const WifiNetwork& net = nets_[static_cast<std::size_t>(pick_)];
    if (net.needsPassword()) {
        typing_ = true, password_ = true, fromSong_ = false;
        typed_.clear();
        passwordFor_ = net.name;
        key_ = 0, keySet_ = 0;
    } else {
        session_.wifi().join(net.name, "");
    }
}

void SettingsScreen::moveKey(Button direction) {
    const int count = 40, columns = kKeyColumns;
    int col = key_ % columns, row = key_ / columns;
    if (direction == Button::Left) col = (col + columns - 1) % columns;
    if (direction == Button::Right) col = (col + 1) % columns;
    if (direction == Button::Up) row = (row + count / columns - 1) % (count / columns);
    if (direction == Button::Down) row = (row + 1) % (count / columns);
    key_ = row * columns + col;
}

void SettingsScreen::typeButton(Button b) {
    if (isDirection(b)) {
        moveKey(b);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::A) {
        const char c = kKeys[key_];
        // No space first, and not two together; ten at most.
        const bool space = c == ' ';
        if (static_cast<int>(typed_.size()) < kNameLength && !(space && (typed_.empty() || typed_.back() == ' '))) typed_ += c;
    } else if (b == Button::Y) {
        if (!typed_.empty()) typed_.pop_back(); // the last letter goes
    } else if (b == Button::B) {
        // Leaves, keeping what is typed (owner, 2026-11-15: B is pressed to back out). Nothing typed: the
        // name stays as it was.
        if (!typed_.empty()) session_.setTrackName(project_.track, typed_);
        typing_ = false;
    } else if (b == Button::Start) {
        session_.setTrackName(project_.track, typed_); // (nothing typed: the instrument's own name)
        typing_ = false;
    }
    // Opened from the SONG screen: back to it when the typing is over.
    if (!typing_ && fromSong_) {
        fromSong_ = false;
        session_.view = View::Song;
    }
}

bool SettingsScreen::here(int row) const {
    if (row == kBrightness) return session_.screenHere();
    if (row == kLight || row == kLightColour || row == kRingLevel) return session_.ringHere();
    if (row == kNetwork) return session_.wifiHere();
    return row >= 0 && row < kRows;
}

int SettingsScreen::placeOf(int row) const {
    int place = 0;
    for (int r = 0; r < row && r < kRows; ++r) place += here(r) ? 1 : 0;
    return place;
}

int SettingsScreen::rowAt(int place) const {
    int last = 0;
    for (int r = 0; r < kRows; ++r) {
        if (!here(r)) continue;
        if (place <= 0) return r;
        --place;
        last = r;
    }
    return last; // past the end: the last
}

const char* SettingsScreen::groupName(int group) {
    static const char* const names[kGroups] = {"SONG", "PLAYING", "LOOK", "HANDHELD", "SETUP"};
    return group >= 0 && group < kGroups ? names[group] : "";
}

int SettingsScreen::placeY(int place, int first) const {
    int y = kRowsTop;
    for (int p = first + 1; p <= place; ++p) y += kRowH + (headsGroup(p) ? kHeadingGap : 0);
    return y;
}

void SettingsScreen::goTo(int row) {
    row = row < 0 ? 0 : (row >= kRows ? kRows - 1 : row);
    int to = row;
    while (to < kRows && !here(to)) ++to;
    if (to >= kRows)
        for (to = row; to > 0 && !here(to); --to) {}
    row_ = to;
    session_.wifiLooking = row_ == kNetwork && !typing_; // (at once: Session::update asks before this screen's update)
    const int place = placeOf(row_);
    if (place < first_) first_ = place;
    while (first_ < place && !fits(place, first_)) ++first_;
    while (first_ > 0 && fits(places() - 1, first_ - 1)) --first_; // (the last row in view: no room left empty under it)
}

void SettingsScreen::change(int by) {
    switch (row_) {
    case kTempo: session_.setTempo(project_.tempo + static_cast<float>(by)); break;
    case kPack: session_.choosePack(session_.packIndex() + by); break;
    case kKey: session_.setScale(project_.scaleKey + by, project_.scaleKind); break; // (round: B to C)
    case kScale: session_.setScale(project_.scaleKey, project_.scaleKind + by); break; // (a choice with ends)
    case kBounce: bounceWhat_ = ((bounceWhat_ + by) % 3 + 3) % 3; break;
    case kTrackName: {
        // Through the list of names, round and round; the first is "its instrument's own".
        int now = 0;
        for (int i = 1; i < kTrackNameCount; ++i)
            if (std::string(kTrackNames[i]) == project_.current().name) now = i;
        now = ((now + by) % kTrackNameCount + kTrackNameCount) % kTrackNameCount;
        session_.setTrackName(project_.track, now == 0 ? "" : kTrackNames[now]);
        break;
    }
    case kVolume: session_.setVolume(session_.volume() + 0.05f * static_cast<float>(by)); break;
    case kBrightness: session_.setBrightness(session_.brightness() + kBrightStep * by); break;
    case kLight: session_.setStickLight(session_.stickLight() + by); break; // (a choice with ends)
    case kLightColour: session_.setLightColour(session_.lightColour() + by); break; // (so is this)
    case kRingLevel: session_.setRingLevel(session_.ringLevel() + kBrightStep * by); break;
    case kStick: session_.setStickSpeed(session_.stickSpeed() + 0.05f * static_cast<float>(by)); break;
    case kTextSize: session_.setTextSize(session_.textSize() + by); break; // (it has ends, as PIANO ROLL has)
    case kTheme: session_.setTheme(session_.theme() + by); break;       // (so has this; the colours change from the next frame)
    case kOctaves: session_.setOctavesShown(session_.octavesShown() + by); break;
    case kMidi: session_.setMidiRecording(by > 0); break;
    case kClick: session_.setClick(session_.click() + by); break; // (a choice with ends)
    case kCountIn: session_.setCountIn(by > 0); break;
    case kMutes: session_.setMutes(session_.mutes() + by); break;
    default: break;
    }
}

void SettingsScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (testing_) {
        // Everything is only shown.
        if (event.down) lastPressed_ = b, testTimed_ = false;
        return;
    }
    if (typing_) {
        if (event.down) password_ ? passwordButton(b) : typeButton(b);
        else if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (event.down && b != Button::B) forgetting_ = false; // (any other button calls a forgetting off)
    if (row_ == kNetwork && session_.wifiHere()) {
        // NETWORK: hold A + up / down picks, let go joins; hold B forgets the one joined.
        if (b == Button::A) {
            if (event.down && session_.transferOn()) {
                picking_ = true, pickMoved_ = false;
                const Wifi::Now now = session_.wifi().now();
                nets_ = now.networks;
                pick_ = -1;
                for (std::size_t i = 0; i < nets_.size(); ++i)
                    if (now.status.state != WifiState::Off && nets_[i].name == now.status.name) pick_ = static_cast<int>(i);
            } else if (!event.down && picking_) {
                picking_ = false;
                if (pickMoved_) joinPicked();
            }
            return;
        }
        if (b == Button::B) {
            const WifiStatus status = session_.wifi().now().status;
            if (event.down && picking_) picking_ = false; // (B with A held: nothing is joined)
            else if (event.down) forgetting_ = session_.transferOn() && status.state != WifiState::Off, forgetTimed_ = false;
            else forgetting_ = false;
            return;
        }
        if (picking_) {
            if (event.down && (b == Button::Up || b == Button::Down)) {
                pickStep(b == Button::Down ? 1 : -1);
                repeating_ = b;
                repeatAt_ = 0;
            } else if (!event.down && b == repeating_) {
                repeating_ = Button::None;
            }
            return;
        }
    }
    if (b == Button::A) {
        holding_ = event.down && row_ == kControls;
        holdTimed_ = false;
        holdPart_ = 0.f;
        if (!event.down) return;
        if (row_ == kBounce && !session_.bouncing()) session_.startBounce(bounceWhat_ == 0 ? Session::Bounce::Song : bounceWhat_ == 1 ? Session::Bounce::EachTrack : Session::Bounce::ThisTrack);
        if (row_ == kTempo) session_.tapTempo(lastMs_); // TAP TEMPO: A in time
        if (row_ == kTrackName) typing_ = true, password_ = false, typed_ = project_.current().name, key_ = 0;
        if (row_ == kWifi) session_.toggleTransfer(lastMs_ * 2654435761u + 977u);
        if (row_ == kTest) testing_ = true, testTimed_ = false, lastPressed_ = Button::None;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (isDirection(b)) {
        if (b == Button::Up) step(-1);
        if (b == Button::Down) step(1);
        if (b == Button::Left) change(-1);
        if (b == Button::Right) change(1);
        holding_ = false; // moving off lets go
        holdPart_ = 0.f;
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void SettingsScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) holding_ = false, holdPart_ = 0.f;
    if (!input.held(Button::A) || row_ != kNetwork) picking_ = false; // (a let go this screen never saw joins nothing)
    if (!input.held(Button::B) || row_ != kNetwork) forgetting_ = false;
    if (!networkSaid_.empty() && nowMs >= networkSaidUntil_) networkSaid_.clear();
    // Our firmware's WiFi: its status asked for now and then, and a scan while the NETWORK row is up.
    session_.wifiLooking = row_ == kNetwork && !typing_; // (Session::update asks; the status is asked for on every screen)
    forgetPart_ = 0.f;
    if (forgetting_) {
        if (!forgetTimed_) forgetTimed_ = true, forgetSince_ = nowMs;
        forgetPart_ = static_cast<float>(nowMs - forgetSince_) / static_cast<float>(kForgetHoldMs);
        if (nowMs - forgetSince_ >= kForgetHoldMs) {
            forgetting_ = false, forgetPart_ = 0.f;
            const WifiStatus status = session_.wifi().now().status;
            if (status.state != WifiState::Off) {
                session_.wifi().forget(status.name);
                networkSaid_ = "FORGOTTEN";
                networkSaidUntil_ = nowMs + 2500;
            }
        }
    }
    // The SONG screen asked for the keyboard, for the track its cursor is on.
    if (session_.wantTrackName) {
        session_.wantTrackName = false;
        row_ = kTrackName;
        typing_ = true;
        password_ = false;
        fromSong_ = true;
        typed_ = project_.current().name;
        key_ = 0;
    }

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed;
    lastMs_ = nowMs;
    stickX_ = input.stickX();
    stickY_ = input.stickY();
    // Did anything change since the last frame? (A button that is stuck
    // down, or a stick that leans, changes nothing: the test must still
    // close, or the very fault it is there to show would trap you in it.)
    bool changed = std::fabs(stickX_ - testStickX_) > 0.2f || std::fabs(stickY_ - testStickY_) > 0.2f;
    if (changed) testStickX_ = stickX_, testStickY_ = stickY_;
    for (int i = 0; i < static_cast<int>(Button::Count); ++i) {
        const bool now = input.held(static_cast<Button>(i));
        changed = changed || now != held_[i];
        held_[i] = now;
    }

    if (testing_) {
        if (!testTimed_ || changed) testTimed_ = true, testUntil_ = nowMs + kTestQuietMs;
        else if (nowMs >= testUntil_) testing_ = false;
        saved_ = 0.f;
        stickWas_ = false;
        repeating_ = Button::None;
        return;
    }
    if (typing_) {
        // The stick moves over the keys, one at once and then steadily; a held D-pad direction runs.
        const bool sideways = std::fabs(stickX_) > std::fabs(stickY_);
        const float push = sideways ? stickX_ : stickY_;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * kMovesPerSecond * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f) moveKey(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
        }
        if (repeating_ != Button::None && repeatAt_ == 0) {
            repeatAt_ = nowMs + kRepeatDelayMs;
        } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
            repeatAt_ = nowMs + kRepeatEveryMs;
            moveKey(repeating_);
        }
        return;
    }
    if (holding_) {
        if (!holdTimed_) holdTimed_ = true, holdSince_ = nowMs;
        holdPart_ = static_cast<float>(nowMs - holdSince_) / static_cast<float>(kRelearnHoldMs);
        if (holdPart_ >= 1.f) {
            holding_ = false;
            holdPart_ = 0.f;
            relearn_ = true;
        }
        return;
    }
    // The stick: up and down choose, sideways changes. One at once, then steadily.
    const float x = stickX_, y = stickY_;
    const bool sideways = std::fabs(x) > std::fabs(y);
    const float push = sideways ? x : y;
    if (push == 0.f) {
        saved_ = 0.f;
        stickWas_ = false;
    } else {
        saved_ += stickWas_ ? std::fabs(push) * (sideways ? kChangesPerSecond : kMovesPerSecond) * dt : 1.f;
        stickWas_ = true;
        for (; saved_ >= 1.f; saved_ -= 1.f) {
            if (picking_) {
                if (!sideways) pickStep(push > 0.f ? 1 : -1);
            } else if (sideways) {
                change(push > 0.f ? 1 : -1);
            } else {
                step(push > 0.f ? 1 : -1);
            }
        }
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        if (picking_) {
            pickStep(repeating_ == Button::Down ? 1 : repeating_ == Button::Up ? -1 : 0);
            return;
        }
        if (repeating_ == Button::Up) step(-1);
        if (repeating_ == Button::Down) step(1);
        if (repeating_ == Button::Left) change(-1);
        if (repeating_ == Button::Right) change(1);
    }
}

void SettingsScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    topBar(r, fonts, View::Settings, "", project_);
    char text[96];

    if (typing_ && password_) {
        // A network's password: the keys in three sets. What is typed is shown (the handheld is in
        // your hand, and a wrong letter costs a minute); its end where it is too long for the box.
        const std::string title = "PASSWORD FOR " + passwordFor_;
        fonts.within(&Fonts::small, title.c_str(), kScreenW - 80.f).draw(r, 40.f, 46.f, title.c_str(), kDim);
        box(r, 40, 66, kScreenW - 80, 52, kPanel2, kTeal);
        std::snprintf(text, sizeof text, "%d OF %d", static_cast<int>(typed_.size()), kWifiPasswordMost);
        const float countW = fonts.fixed().small.width("63 OF 63");
        const Font& typedFont = fonts.fixed().medium;
        const float tx = 56.f, room = kScreenW - 56.f - countW - 16.f - tx - 8.f;
        std::string shown = typed_;
        while (shown.size() > 1 && typedFont.width((std::string("...") + shown).c_str()) > room) shown.erase(0, 1);
        if (shown.size() < typed_.size()) shown = "..." + shown;
        typedFont.draw(r, tx, 66.f + (52.f - typedFont.capHeight()) / 2.f, shown.c_str(), kText);
        if (static_cast<int>(typed_.size()) < kWifiPasswordMost) fill(r, static_cast<int>(tx + typedFont.width(shown.c_str()) + (shown.empty() ? 0 : 3)), 66 + 14, 2, 24, kTeal);
        fonts.fixed().small.drawRight(r, kScreenW - 56.f, 66.f + (52.f - fonts.fixed().small.capHeight()) / 2.f, text, kDim);
        if (!networkSaid_.empty()) fonts.fixed().small.drawRight(r, kScreenW - 40.f, 46.f, networkSaid_.c_str(), kBrass);
        const int kw = 52, kh = 52, gx = (kScreenW - kKeyColumns * kw) / 2, gy = 140;
        for (int i = 0; i < 40; ++i) {
            const int x = gx + (i % kKeyColumns) * kw, y = gy + (i / kKeyColumns) * kh;
            const bool here = i == key_;
            const char c = kPasswordKeys[keySet_][i];
            box(r, x + 2, y + 2, kw - 4, kh - 4, here ? kTeal : (c == '\x01' ? kBg : kPanel), here ? kTeal : kLine);
            if (c == '\x01') continue;
            char one[8];
            std::snprintf(one, sizeof one, "%s", c == ' ' ? "SPACE" : std::string(1, c).c_str());
            const Font& font = c == ' ' ? fonts.within(&Fonts::small, one, kw - 4 - 6.f) : fonts.fixed().large;
            font.drawCentered(r, x + kw / 2.f, y + (kh - font.capHeight()) / 2.f, one, here ? kBg : kText);
        }
        const bool none = typed_.empty();
        const Hint list[] = {{"A", "type"}, {"Y", none ? nullptr : "delete"}, {"X", kPasswordSetNames[(keySet_ + 1) % 3]}, {"B", none ? "leave" : "join"}};
        hints(r, fonts, list, 4);
        return;
    }
    if (typing_) {
        char title[48];
        std::snprintf(title, sizeof title, "TRACK %d: ITS NAME", project_.track + 1);
        fonts.small.draw(r, 40.f, 46.f, title, kDim);
        // What has been typed, with a mark where the next letter goes.
        box(r, 40, 66, kScreenW - 80, 52, kPanel2, kTeal);
        const float tx = 56.f, ty = 66.f + (52.f - fonts.large.capHeight()) / 2.f;
        fonts.large.draw(r, tx, ty, typed_.c_str(), kText);
        if (static_cast<int>(typed_.size()) < kNameLength) fill(r, static_cast<int>(tx + fonts.large.width(typed_.c_str()) + (typed_.empty() ? 0 : 3)), 66 + 14, 2, 24, kTeal);
        std::snprintf(text, sizeof text, "%d OF %d", static_cast<int>(typed_.size()), kNameLength);
        fonts.small.drawRight(r, kScreenW - 56.f, 66.f + (52.f - fonts.small.capHeight()) / 2.f, text, kDim);
        if (typed_.empty()) fonts.small.draw(r, tx + 12.f, 66.f + (52.f - fonts.small.capHeight()) / 2.f, "(empty: its instrument's own name)", kDim);
        const int kw = 52, kh = 52, gx = (kScreenW - kKeyColumns * kw) / 2, gy = 140;
        for (int i = 0; i < 40; ++i) {
            const int x = gx + (i % kKeyColumns) * kw, y = gy + (i / kKeyColumns) * kh;
            const bool here = i == key_;
            box(r, x + 2, y + 2, kw - 4, kh - 4, here ? kTeal : kPanel, here ? kTeal : kLine);
            char one[8];
            std::snprintf(one, sizeof one, "%s", kKeys[i] == ' ' ? "SPACE" : std::string(1, kKeys[i]).c_str());
            const Font& font = kKeys[i] == ' ' ? fonts.within(&Fonts::small, one, kw - 4 - 6.f) : fonts.large; // ("SPACE" at XL is wider than its key: held at LARGE)
            font.drawCentered(r, x + kw / 2.f, y + (kh - font.capHeight()) / 2.f, one, here ? kBg : kText);
        }
        const bool none = typed_.empty();
        const Hint list[] = {{"A", "type"}, {"Y", none ? nullptr : "delete"}, {"B", none ? "leave" : "keep"}, {"START", none ? "its own name" : nullptr}};
        hints(r, fonts, list, 4);
        return;
    }
    if (testing_) {
        fonts.medium.draw(r, 24.f, 44.f, "BUTTON TEST", kText);
        fonts.small.draw(r, 24.f, 66.f, "Press each button. It should light up here, and only while you hold it.", kDim);
        fonts.small.draw(r, 24.f, 82.f, "One that stays lit, or never lights, was learned wrongly: set the controls up again.", kDim);
        const Button order[15] = {Button::Up, Button::Down, Button::Left, Button::Right, Button::A, Button::B, Button::X, Button::Y,
                                  Button::L1, Button::R1, Button::L2, Button::R2, Button::Start, Button::Select, Button::Menu};
        for (int i = 0; i < 15; ++i) {
            const int x = 24 + (i % 4) * 112, y = 112 + (i / 4) * 58;
            const bool on = held_[static_cast<int>(order[i])];
            box(r, x, y, 104, 48, on ? kTeal : kPanel, on ? kTeal : kLine);
            fonts.medium.drawCentered(r, x + 52.f, y + (48.f - fonts.medium.capHeight()) / 2.f, buttonLabel(order[i]), on ? kBg : kDim);
        }
        // The stick: a dot in a square.
        const int sx = 500, sy = 140, size = 110;
        box(r, sx, sy, size, size, kPanel, kLine);
        fill(r, sx + size / 2, sy + 1, 1, size - 2, kLine);
        fill(r, sx + 1, sy + size / 2, size - 2, 1, kLine);
        const int dx = sx + size / 2 + static_cast<int>(stickX_ * (size / 2 - 8)), dy = sy + size / 2 + static_cast<int>(stickY_ * (size / 2 - 8));
        fill(r, dx - 5, dy - 5, 10, 10, stickX_ != 0.f || stickY_ != 0.f ? kTeal : kDim);
        fonts.small.drawCentered(r, sx + size / 2.f, sy - 18.f, "THE STICK", kDim);
        std::snprintf(text, sizeof text, "LAST PRESSED: %s", lastPressed_ == Button::None ? "nothing yet" : buttonLabel(lastPressed_));
        fonts.medium.draw(r, 24.f, 360.f, text, kText);
        fonts.small.draw(r, 24.f, 386.f, "Nothing else works while this is open. To leave: hands off for four seconds.", kDim);
        hints(r, fonts, nullptr, 0);
        return;
    }

    const char* names[kRows] = {"TEMPO", "CHORD PACK", "KEY", "SCALE", "TRACK NAME", "BOUNCE", "CLICK", "COUNT IN", "MUTES", "USB KEYBOARD", "TEXT SIZE", "THEME", "PIANO ROLL",
                                "VOLUME", "BRIGHTNESS", "STICK LIGHT", "LIGHT COLOUR", "RING LEVEL", "STICK SPEED", "WIFI PAGE", "NETWORK", "BUTTONS", "CONTROLS"};
    const int rowH = kRowH, w = kRowW; // (twelve rows shown: 34 a row, as it was with eleven, would run into the hints bar)
    // The heading of the first row in view, above the rows.
    const char* topHeading = groupName(groupOf(rowAt(first_)));
    fonts.small.draw(r, 16.f, static_cast<float>(kHeadingY), topHeading, kDim);
    // A row out of sight above (the page has moved down) or below: an arrow after a heading.
    const auto more = [&](float y, const char* section, const char* way) { drawKey(r, fonts, 16.f + fonts.small.width(section) + 6.f, y, way, kDim); };
    if (first_ > 0) more(static_cast<float>(kHeadingY), topHeading, "UP");
    // Which Apotheca this is.
    std::snprintf(text, sizeof text, "APOTHECA %s   %s", kVersion, kBuilt);
    fonts.small.drawRight(r, kScreenW - 16.f, static_cast<float>(kHeadingY), text, kDim);
    float lastHeadingY = static_cast<float>(kHeadingY);
    const char* lastHeading = topHeading;
    for (int place = first_; place < places() && fits(place, first_); ++place) {
        // The first four belong to the song; the rest to the handheld.
        const int row = rowAt(place), y = placeY(place, first_);
        if (place > first_ && headsGroup(place)) {
            fonts.small.draw(r, 16.f, y - static_cast<float>(kHeadingUp), groupName(groupOf(row)), kDim);
            lastHeadingY = y - static_cast<float>(kHeadingUp), lastHeading = groupName(groupOf(row));
        }
        const bool here = row == row_;
        float part = -1.f;
        const char* note = "";
        char said[96] = "";
        bool lock = false, twoAddresses = false;
        switch (row) {
        case kTempo:
            std::snprintf(text, sizeof text, "%d BPM", static_cast<int>(project_.tempo));
            note = "A: tap it in time";
            break;
        case kPack: std::snprintf(text, sizeof text, "%s", session_.packs[session_.packIndex()].name.c_str()); break;
        case kKey:
            std::snprintf(text, sizeof text, "%s", keyName(project_.scaleKey));
            note = project_.scaleKind == kScaleOff ? "with SCALE, the roll's key" : "the chords in hand move to it";
            break;
        case kScale:
            std::snprintf(text, sizeof text, "%s", scaleName(project_.scaleKind));
            note = project_.scaleKind == kScaleOff ? "the roll takes every key" : "the roll keeps to it";
            break;
        case kTrackName: {
            char full[24];
            trackName(project_, project_.track, full, sizeof full);
            std::snprintf(text, sizeof text, "%s", full);
            note = "left / right: from a list.  A: type one";
            break;
        }
        case kBounce: {
            static const char* what[3] = {"THE SONG", "EACH TRACK", "THIS TRACK"};
            if (session_.bouncing()) {
                // The song's own length is known, so that part is a percentage. A tail's is not
                // (it runs until it has died away): it says how much of it there is so far.
                const float tail = session_.bounceTail();
                if (tail >= 0.f) std::snprintf(text, sizeof text, "TAIL  %d S", static_cast<int>(tail));
                else std::snprintf(text, sizeof text, "WORKING  %d%%", static_cast<int>(session_.bounceProgress() * 100.f));
                part = session_.bounceProgress();
                note = tail >= 0.f ? "until the echoes have died away" : "you can carry on meanwhile";
            } else {
                std::snprintf(text, sizeof text, "%s", what[bounceWhat_]);
                if (session_.bounceFiles() > 0) std::snprintf(bounced_, sizeof bounced_, "done: %d file%s, on the WiFi page", session_.bounceFiles(), session_.bounceFiles() == 1 ? "" : "s");
                note = session_.bounceFiles() > 0 ? bounced_ : "A: write it as a WAV file";
            }
            break;
        }
        case kVolume:
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(session_.volume() * 100.f)));
            part = session_.volume();
            note = "also SEL + L1 / R1";
            break;
        case kBrightness:
            std::snprintf(text, sizeof text, "%d%%", session_.brightness());
            part = static_cast<float>(session_.brightness()) / 100.f;
            note = "also MENU + volume keys";
            break;
        case kLight: {
            static const char* const what[kRingModes] = {"OFF", "ON", "BEAT", "STEPS"};
            static const char* const says[kRingModes] = {"the light round the stick", "steady, in LIGHT COLOUR",
                                                         "a flash on every beat", "once round a scene"};
            const int mode = session_.stickLight();
            std::snprintf(text, sizeof text, "%s", what[mode]);
            note = says[mode];
            break;
        }
        case kLightColour: {
            static const char* const what[kLightColours] = {"THEME", "TEAL", "PURPLE", "BRASS", "RED", "ORANGE", "YELLOW",
                                                            "GREEN", "BLUE", "PINK", "WHITE", "TRACK", "RAINBOW"};
            const int which = session_.lightColour();
            std::snprintf(text, sizeof text, "%s", what[which]);
            note = which == kLightTheme ? "a colour picked for the theme" : which == kLightTrack ? "a colour for each track" : which == kLightRainbow ? "turns a light a beat" : "";
            break;
        }
        case kRingLevel:
            std::snprintf(text, sizeof text, "%d%%", session_.ringLevel());
            part = static_cast<float>(session_.ringLevel()) / 100.f;
            note = "how bright the ring is";
            break;
        case kStick:
            std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::round(session_.stickSpeed() * 100.f)));
            part = (session_.stickSpeed() - Session::kStickSlowest) / (Session::kStickFastest - Session::kStickSlowest);
            note = "how fast the stick moves things";
            break;
        case kTextSize:
            std::snprintf(text, sizeof text, "%s", kTextSizeNames[session_.textSize()]);
            note = "the bars and the strips stay as they are";
            break;
        case kTheme:
            std::snprintf(text, sizeof text, "%s", kThemeTable[session_.theme()].name);
            note = "four dark, two light";
            break;
        case kOctaves:
            std::snprintf(text, sizeof text, "%d OCTAVE%s", session_.octavesShown(), session_.octavesShown() == 1 ? "" : "S");
            note = "L1 on the piano roll does the same";
            break;
        case kMidi:
            std::snprintf(text, sizeof text, "%s", session_.midiRecording() ? "PLAY + RECORD" : "PLAY ONLY");
            note = session_.midiKeyboards() > 0 ? "a keyboard is plugged in" : "none plugged in just now";
            break;
        case kClick: {
            static const char* const what[Session::kClickModes] = {"OFF", "ON", "RECORDING"};
            static const char* const says[Session::kClickModes] = {"a click on the beat", "on every beat while playing",
                                                                   "only with PLAY + RECORD"};
            std::snprintf(text, sizeof text, "%s", what[session_.click()]);
            note = says[session_.click()];
            break;
        }
        case kCountIn:
            std::snprintf(text, sizeof text, "%s", session_.countIn() ? "ONE BAR" : "OFF");
            note = "clicks before PLAY + RECORD";
            break;
        case kMutes:
            std::snprintf(text, sizeof text, "%s", session_.mutes() == Session::kMutesNextBar ? "NEXT BAR" : "AT ONCE");
            note = "mute and solo while playing";
            break;
        case kWifi:
            if (session_.transferOn()) std::snprintf(text, sizeof text, "ON   %s", session_.transferAddress().c_str());
            else std::snprintf(text, sizeof text, "OFF");
            note = session_.transferOn() ? "" : "files and updates from a browser";
            if (session_.transferOn() && session_.wifiHere()) {
                // Our firmware gives the handheld a name on the network: said beside the address (the
                // same port and number after it). Nothing where the system does not say one.
                const std::string name = session_.transferName();
                if (!name.empty()) std::snprintf(said, sizeof said, "or %.60s", name.c_str()), note = said, twoAddresses = true;
            }
            break;
        case kNetwork: {
            const Wifi::Now now = session_.wifi().now();
            const WifiStatus& status = now.status;
            if (!session_.transferOn()) {
                std::snprintf(text, sizeof text, "NONE");
                note = "turn WIFI PAGE on first";
            } else if (picking_) {
                const bool one = pick_ >= 0 && pick_ < static_cast<int>(nets_.size());
                std::snprintf(text, sizeof text, "%s", one ? nets_[static_cast<std::size_t>(pick_)].name.c_str() : "NONE");
                lock = one && nets_[static_cast<std::size_t>(pick_)].needsPassword();
                if (nets_.empty()) std::snprintf(said, sizeof said, "%s", now.scanned ? "NONE FOUND" : "LOOKING...");
                else std::snprintf(said, sizeof said, "%d OF %d", one ? pick_ + 1 : 0, static_cast<int>(nets_.size()));
                note = said;
            } else {
                std::snprintf(text, sizeof text, "%s", status.state == WifiState::Off ? "NONE" : status.name.c_str());
                if (forgetPart_ > 0.f) note = kForgettingWords;
                else if (!networkSaid_.empty()) std::snprintf(said, sizeof said, "%s", networkSaid_.c_str()), note = said;
                else if (status.state == WifiState::Joining) note = "JOINING...";
                else if (status.state == WifiState::WrongPassword) note = "WRONG PASSWORD";
                else if (status.state == WifiState::NotFound) note = "NOT FOUND";
                else if (status.state == WifiState::Joined) std::snprintf(said, sizeof said, "%.40s", status.address.c_str()), note = said;
                else if (!now.scanned) note = "LOOKING...";
                else std::snprintf(said, sizeof said, "%d FOUND", static_cast<int>(now.networks.size())), note = said;
            }
            break;
        }
        case kTest:
            std::snprintf(text, sizeof text, "TEST");
            note = "see what Apotheca hears from each button";
            break;
        default:
            std::snprintf(text, sizeof text, "SET UP AGAIN");
            note = "asks for every button in turn";
            break;
        }
        box(r, kRowX, y, w, rowH - 6, kPanel, kLine);
        fonts.small.draw(r, kNameX, y + (rowH - 6 - fonts.small.capHeight()) / 2.f, names[row], kDim);
        // A value and what is said about it share the row. Where the two would meet at XL (a track's
        // name of ten wide letters), both are held at LARGE (Fonts::within).
        const float valueRoom = kNoteRight - kValueX - (note[0] ? fonts.small.width(note) + kNoteGap : 0.f);
        // (WIFI PAGE with the page's name beside its address is two addresses on one row: they do not fit in
        // the large lettering at any size, so the address is drawn in the medium, or the small, of LARGE.)
        const Font* addressFont = nullptr;
        if (twoAddresses) {
            const float room = kNoteRight - kValueX - kNoteGap - fonts.fixed().small.width(note);
            addressFont = fonts.fixed().medium.width(text) <= room ? &fonts.fixed().medium : &fonts.fixed().small;
        }
        // (A network's name is the one value that may be too long even at LARGE: cut to its room there, "..."
        // at its end, so it is the same words at every size.)
        if (row == kNetwork) {
            const float room = kNoteRight - kValueX - (note[0] ? fonts.fixed().small.width(note) + kNoteGap : 0.f) - (lock ? kLockW + 8 : 0);
            for (std::size_t n = std::strlen(text); n > 4 && fonts.fixed().large.width(text) > room; --n) std::snprintf(text + n - 4, 5, "...");
        }
        const Font& valueFont = addressFont ? *addressFont : fonts.within(&Fonts::large, text, valueRoom - (lock ? kLockW + 8 : 0));
        const Font& noteFont = (&valueFont == &fonts.large ? fonts : fonts.fixed()).small;
        valueFont.draw(r, kValueX, y + (rowH - 6 - valueFont.capHeight()) / 2.f, text, here ? kTeal : kText);
        if (lock) {
            // It needs a password: a little lock after its name (a body, and a bow over it).
            const int lx = static_cast<int>(kValueX + valueFont.width(text)) + 8, ly = y + (rowH - 6 - kLockH) / 2;
            fill(r, lx, ly + 5, kLockW, kLockH - 5, kBrass);
            outline(r, lx + 2, ly, kLockW - 4, 7, kBrass);
        }
        if (row == kNetwork && here && forgetPart_ > 0.f) fill(r, 16, y + rowH - 10, static_cast<int>(w * (forgetPart_ > 1.f ? 1.f : forgetPart_)), 4, kStop);
        if (note[0]) noteFont.drawRight(r, kNoteRight, y + (rowH - 6 - noteFont.capHeight()) / 2.f, note, kDim);
        if (part >= 0.f) {
            // The bar, between the value and what is said: at 250 as it always was, or further right where
            // the value is longer (BOUNCE's WORKING 37%), and shorter where the words beside it need the room.
            int bx = 0, bw = 0;
            barPlace(valueFont.width(text), note[0] ? noteFont.width(note) : 0.f, &bx, &bw);
            fill(r, bx, y + rowH - 16, bw, 4, kLine);
            fill(r, bx, y + rowH - 16, static_cast<int>(std::round(bw * (part > 1.f ? 1.f : part))), 4, here ? kTeal : kDim);
        }
        if (here) outline(r, 15, y - 1, w + 2, rowH - 4, holdPart_ > 0.f ? kBrass : kTeal, 2);
        if (here && row == kControls && holdPart_ > 0.f) fill(r, 16, y + rowH - 10, static_cast<int>(w * (holdPart_ > 1.f ? 1.f : holdPart_)), 4, kBrass);
    }
    // A row out of sight below: an arrow after the last heading in view (after the UP arrow, where that is the top one's too).
    if (!fits(places() - 1, first_)) {
        const bool both = first_ > 0 && lastHeadingY == static_cast<float>(kHeadingY);
        drawKey(r, fonts, 16.f + fonts.small.width(lastHeading) + 6.f + (both ? kArrowsApart : 0.f), lastHeadingY, "DOWN", kDim);
    }

    Hint list[5];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    hint("UP / DOWN", "choose");
    if (row_ < kWifi) hint("LEFT / RIGHT", "change");
    if (row_ == kBounce) hint("A", session_.bouncing() ? "(working)" : "bounce");
    if (row_ == kTempo) hint("A", "tap it");
    if (row_ == kTrackName) hint("A", "type a name");
    if (row_ == kWifi) hint("A", session_.transferOn() ? "turn off" : "turn on");
    if (row_ == kNetwork && session_.transferOn()) {
        const bool joined = session_.wifi().now().status.state != WifiState::Off;
        if (forgetPart_ > 0.f) {
            count = 0;
            hint("HOLD B", "forgetting");
        } else if (picking_) {
            count = 0;
            hint("UP / DOWN", "networks");
            hint("B", "leave it");
        } else {
            hint("HOLD A", "networks");
            if (joined) hint("HOLD B", "forget");
        }
    }
    if (row_ == kTest) hint("A", "open the test");
    if (row_ == kControls) hint("HOLD A", "set up again");
    hint("SELECT", "to SONG");
    hints(r, fonts, list, count);
}

} // namespace apotheca
