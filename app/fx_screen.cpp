#include "fx_screen.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace apotheca {

using namespace ui;

namespace {
constexpr Uint32 kRepeatDelayMs = ui::kCursorDelayMs, kRepeatEveryMs = ui::kCursorEveryMs;
constexpr float kMovesPerSecond = 14.f, kKindsPerSecond = 5.f, kTurnPerSecond = 0.9f, kNotch = 0.05f;
}

// Nothing here spells an effect out: its name, its family and type, its
// settings, which of them are choices and how each reads all come from the
// one table (apotheca/effects.hpp).

bool changeEffectType(Session& session, int track, int slot, EffectKind to) {
    if (slot < 0 || slot >= slotsOf(track)) return false;
    const EffectData was = session.effectData(track, slot);
    const EffectKind from = static_cast<EffectKind>(was.kind);
    if (to == from || to == EffectKind::None || from == EffectKind::None) return false;
    if (static_cast<int>(to) < 0 || static_cast<int>(to) >= kEffectKinds || std::strcmp(effectFamily(from), effectFamily(to)) != 0) return false;
    const EffectInfo& info = effectInfo(to);

    // What the pattern holds for the settings that are kept, taken before the old effect's lanes are cleared.
    struct Kept { int scene, index, step; float value; };
    std::vector<Kept> kept;
    if (track >= 0) {
        const TrackData& t = session.project.tracks[track];
        for (int i = 0; i < info.params; ++i) {
            const int had = effectCarryFrom(from, to, i);
            if (had < 0) continue;
            std::map<float, float> known; // a lane is mostly a few values many times
            for (int scene = 0; scene < kMaxScenes; ++scene)
                for (int step = 0; step < kAutoSteps; ++step) {
                    const float v = t.automation[scene][autoLaneOfEffect(slot, had)][step];
                    if (!(v >= 0.f)) continue;
                    auto found = known.find(v);
                    if (found == known.end()) found = known.emplace(v, effectCarryValue(from, had, to, i, v)).first;
                    kept.push_back({scene, i, step, found->second});
                }
        }
    }

    const bool writing = session.writing();
    session.setWriting(false); // none of this is the player moving a knob
    session.setEffect(track, slot, to); // (clears all four of the slot's lanes, in every scene)
    if (session.effectData(track, slot).kind != static_cast<int>(to)) {
        session.setWriting(writing);
        return false; // refused: everything is as it was
    }
    float now[kEffectParams];
    effectCarry(from, to, was.param, busOfTrack(track) >= 0, now);
    for (int i = 0; i < info.params; ++i) session.setEffectParam(track, slot, i, now[i]);
    for (const Kept& k : kept) session.setAutomation(track, k.scene, autoLaneOfEffect(slot, k.index), k.step, k.value);
    session.setWriting(writing);
    return true;
}

int FxScreen::lineY(int row) {
    // Three blocks, each under a heading: the track's four lines, the buses' two, the master's two.
    int y = 33 + 19 + row * (kLineH + kLineGap);
    if (row >= kBusRow) y += 5 + 19;
    if (row >= kMasterRow) y += 5 + 19;
    return y;
}

void FxScreen::goTo(int row, int column) {
    row_ = ((row % kRows) + kRows) % kRows;
    const int last = settings(row_);
    column_ = column < 0 ? 0 : (column > last ? last : column);
    if (column_ > 0) last_[row_] = column_;
}

float FxScreen::meterOf(int row) const {
    if (row < 0 || row >= kRows || sends(row)) return 0.f;
    return session_.engine.effectMeter(trackOf(row), slotOf(row));
}

int FxScreen::meterPixels(float dB, int width) {
    if (!(dB > 0.f) || width <= 0) return 0; // (and "not a number" draws nothing)
    if (dB >= kMeterFullDb) return width;
    const int px = static_cast<int>(std::round(static_cast<float>(width) * dB / kMeterFullDb));
    return px < 1 && dB >= 0.1f ? 1 : px; // a tenth of a dB and up is seen
}

void FxScreen::meterWords(EffectKind kind, float dB, char* out, int size) {
    if (!out || size <= 0) return;
    out[0] = 0;
    if (effectMeterParam(kind) < 0 || !(dB >= 0.5f)) return;
    const int whole = static_cast<int>(std::round(dB > 99.f ? 99.f : dB));
    std::snprintf(out, static_cast<std::size_t>(size), kind == EffectKind::Duck ? "DIPPING %d DB" : "TURNING DOWN %d DB", whole);
}

void FxScreen::hear() {
    const TrackKind kind = project_.current().kind;
    if (kind == TrackKind::Drums) {
        Command c;
        c.type = Command::Trigger;
        c.track = project_.track;
        c.value = 0.8f;
        session_.engine.send(c);
    } else if (isSynth(kind)) {
        session_.audition(project_.track, 60, 0.8f);
    }
}

// Nothing sent to a bus, and whatever automation the send had in this cell goes.
void FxScreen::emptySend(int bus) {
    const bool writing = session_.writing();
    session_.setWriting(false);
    session_.clearAutomation(project_.track, project_.scene, autoLaneOfSend(bus));
    session_.setSend(project_.track, bus, 0.f);
    session_.setWriting(writing);
}

// A direction. Without A: the cursor. With A held: change the thing under it by one notch.
void FxScreen::press(Button b) {
    const bool upDown = b == Button::Up || b == Button::Down;
    if (!holding_) {
        if (column_ == 0) {
            // In the list: up and down through its lines; right into the line's settings, where it was last.
            if (b == Button::Up) goTo(row_ - 1, 0);
            if (b == Button::Down) goTo(row_ + 1, 0);
            if (b == Button::Right && settings(row_) > 0) goTo(row_, last_[row_]);
        } else {
            // In the settings: up and down through them (the ends stop); left back to the list.
            if (b == Button::Up && column_ > 1) goTo(row_, column_ - 1);
            if (b == Button::Down) goTo(row_, column_ + 1);
            if (b == Button::Left) column_ = 0;
        }
        return;
    }
    if (column_ == 0) {
        if (sends(row_)) return; // the sends are turned one at a time, on the right
        const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
        const int bus = busOf(row_);
        if (upDown) {
            // The list of effects, which is drawn as this is held: down is the next line of it.
            // A family goes in as its first type, at its usual settings. The ends stop.
            const int from = familyPlace(kind, bus), to = from + (b == Button::Down ? 1 : -1);
            if (to < 0 || to >= familyListSize(bus)) return;
            session_.setEffect(trackOf(row_), slotOf(row_), familyAt(to, bus));
        } else {
            // Its TYPE: the next of the family's, or the one before. The ends stop.
            const EffectKind next = effectTypeAt(kind, effectTypeIndex(kind) + (b == Button::Right ? 1 : -1));
            if (next == EffectKind::None || !changeEffectType(session_, trackOf(row_), slotOf(row_), next)) return;
        }
        if (!project_.playing) hear();
        return;
    }
    if (!upDown) return;
    const int by = b == Button::Up ? 1 : -1;
    if (sends(row_)) {
        // A send: a twentieth at a push.
        const int bus = column_ - 1;
        const float notches = send(bus) / kNotch;
        session_.setSend(project_.track, bus, (by > 0 ? std::floor(notches + 1.001f) : std::ceil(notches - 1.001f)) * kNotch);
        return;
    }
    const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
    const int index = column_ - 1;
    const float now = data(row_).param[index];
    if (const int count = effectChoices(kind, index)) {
        // The next choice: the middle of its part of the range.
        int which = static_cast<int>(now * static_cast<float>(count));
        which = (which >= count ? count - 1 : which) + by;
        if (which < 0 || which >= count) return;
        session_.setEffectParam(trackOf(row_), slotOf(row_), index, (static_cast<float>(which) + 0.5f) / static_cast<float>(count));
    } else {
        const float notches = now / kNotch;
        const float to = (by > 0 ? std::floor(notches + 1.001f) : std::ceil(notches - 1.001f)) * kNotch;
        session_.setEffectParam(trackOf(row_), slotOf(row_), index, to);
    }
}

// The stick with A held on a setting: turn it smoothly.
void FxScreen::turn(float amount) {
    if (amount == 0.f || column_ == 0) return;
    if (sends(row_)) {
        session_.setSend(project_.track, column_ - 1, send(column_ - 1) + amount);
        return;
    }
    const int index = column_ - 1;
    session_.setEffectParam(trackOf(row_), slotOf(row_), index, data(row_).param[index] + amount);
}

void FxScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (b == Button::A) {
        holding_ = event.down;
        saved_ = 0.f;
        stickWas_ = false;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeatAt_ = 0;
    } else if (b == Button::B && !holding_) {
        if (sends(row_)) {
            // On the line: nothing sent to either bus. On a send: that one.
            for (int bus = 0; bus < kBuses; ++bus)
                if (column_ == 0 || column_ == bus + 1) emptySend(bus);
        } else if (column_ == 0) {
            session_.setEffect(trackOf(row_), slotOf(row_), EffectKind::None);
        } else {
            const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
            // Its default value (on a bus, LEVEL's is full), and whatever automation it had in this cell goes.
            const bool writing = session_.writing();
            session_.setWriting(false);
            if (trackOf(row_) >= 0) session_.clearAutomation(trackOf(row_), project_.scene, autoLaneOfEffect(slotOf(row_), column_ - 1));
            session_.setEffectParam(trackOf(row_), slotOf(row_), column_ - 1, effectDefault(kind, column_ - 1, busOf(row_) >= 0));
            session_.setWriting(writing);
        }
    } else if (b == Button::X) {
        hear();
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void FxScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) holding_ = false;

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    // The track may have changed under the cursor, or the effect: the line stays, and the setting if
    // there is still one there (the nearest, else back in the list). What was last used is not forgotten.
    if (column_ > settings(row_)) column_ = settings(row_);
    const float x = input.stickX(), y = input.stickY();

    // A setting that is a choice (the filter's TYPE, the delay's TIME) is not turned: it clicks
    // from one choice to the next, a push at a time, with the stick as with the D-pad.
    const bool onSend = sends(row_);
    const bool choice = !onSend && column_ > 0 && effectChoices(static_cast<EffectKind>(data(row_).kind), column_ - 1) > 0;
    if (holding_ && column_ > 0 && !choice) {
        // A setting or a send: the stick turns it. Squared, so a small push is fine.
        turn(-y * std::fabs(y) * kTurnPerSecond * dt);
    } else {
        // Moving, or stepping through the effects or the types: one at once, then steadily.
        const bool sideways = (!holding_ || picking()) && std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : y;
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? std::fabs(push) * (holding_ ? kKindsPerSecond : kMovesPerSecond) * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f)
                press(sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up));
        }
    }
    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + (picking() ? 200u : kRepeatEveryMs);
        press(repeating_);
    }
}

// ---- drawing ------------------------------------------------------------------------------

namespace {

// A setting's wide bar. An amount: filled from the left. A choice: one piece for each choice,
// the one it is lit, so "the third of six" is seen and nothing reads as "somewhere along".
void wideBar(SDL_Renderer* r, int x, int y, int w, int h, float part, int choices, int which, SDL_Color lit) {
    if (choices > 0) {
        const int gap = 3;
        for (int c = 0; c < choices; ++c) {
            const int from = x + (w + gap) * c / choices, to = x + (w + gap) * (c + 1) / choices - gap;
            fill(r, from, y, to - from, h, c == which ? lit : kLine);
        }
        return;
    }
    part = part != part || part < 0.f ? 0.f : (part > 1.f ? 1.f : part);
    fill(r, x, y, w, h, kLine);
    fill(r, x, y, static_cast<int>(std::round(static_cast<float>(w) * part)), h, lit);
}

int whichChoice(float value, int count) {
    const int which = static_cast<int>((value < 0.f ? 0.f : value) * static_cast<float>(count));
    return which >= count ? count - 1 : which;
}

} // namespace

// The list down the left: always all of it.
void FxScreen::drawList(SDL_Renderer* r, const Fonts& fonts) const {
    char name[24];
    trackName(project_, project_.track, name, sizeof name);
    const auto heading = [&](int row, const char* words, const char* more) {
        const float y = static_cast<float>(lineY(row) - 19 + 3);
        // (A long track name takes the room: what it is beside says less than whose it is. Whether
        // there is room for both is asked at LARGE, so the same words are there at every size; where
        // the two do not fit side by side at XL, both are held at LARGE.)
        const Fonts& tight = fonts.fixed();
        const bool both = tight.small.width(words) + tight.small.width(more) + 10.f <= kListW - 2.f;
        const float wide = fonts.small.width(words) + (both ? fonts.small.width(more) + 10.f : 0.f);
        const Font& font = wide <= kListW - 2.f || wide <= tight.small.width(words) + (both ? tight.small.width(more) + 10.f : 0.f) ? fonts.small : tight.small;
        font.draw(r, kListX + 1.f, y, words, kText);
        if (both) font.drawRight(r, kListX + kListW - 1.f, y, more, kDim);
    };
    heading(0, name, "THIS TRACK");
    heading(kBusRow, "BUSES", "ALL TRACKS");
    heading(kMasterRow, "MASTER", "EVERYTHING");

    static const char* const tags[kRows] = {"1", "2", "3", "SENDS", "ECHO", "SPACE", "1", "2"};
    for (int row = 0; row < kRows; ++row) {
        const int y = lineY(row);
        const bool here = row == row_, cursor = here && column_ == 0;
        box(r, kListX, y, kListW, kLineH, here ? kPanel2 : kPanel, here && !cursor ? kDim : kLine);
        const float ty = y + (kLineH - fonts.small.capHeight()) / 2.f - 1.f;
        const float my = y + (kLineH - fonts.medium.capHeight()) / 2.f - 1.f;
        fonts.within(&Fonts::small, tags[row], kNameX - 5.f - (kListX + 8.f)).draw(r, kListX + 8.f, ty, tags[row], here ? kText : kDim); // ("SENDS" at XL: held at LARGE)
        bool moves = false; // something on the line moves with the pattern, in this scene
        if (sends(row)) {
            // Two little bars: how much of the track goes to each bus.
            for (int b = 0; b < kBuses; ++b) {
                const int bx = kNameX + b * 62;
                const float shown = session_.automated(project_.track, autoLaneOfSend(b), send(b));
                fonts.small.draw(r, static_cast<float>(bx), ty, b == 0 ? "E" : "S", kDim);
                fill(r, bx + 14, y + kLineH / 2 - 2, 38, 4, kLine);
                fill(r, bx + 14, y + kLineH / 2 - 2, static_cast<int>(std::round(38.f * shown)), 4, here ? kText : kDim);
                moves = moves || session_.hasAutomation(project_.track, project_.scene, autoLaneOfSend(b));
            }
        } else {
            const EffectData& d = data(row);
            const EffectKind kind = static_cast<EffectKind>(d.kind);
            if (kind == EffectKind::None) {
                fonts.medium.draw(r, static_cast<float>(kNameX), my, "EMPTY", kLine);
            } else {
                const char* family = effectFamily(kind);
                fonts.medium.draw(r, static_cast<float>(kNameX), my, family, kText);
                fonts.small.draw(r, kNameX + fonts.medium.width(family) + 7.f, ty + 1.f, effectTypeName(kind), kDim);
                // The tiny mark: how much of it there is (its MIX, or a bus's LEVEL), and for one with
                // a meter (COMP, DUCK) what it is doing just now, down from the top.
                const int mh = kLineH - 14, top = y + 7, mix = effectMixParam(kind), track = trackOf(row);
                if (mix >= 0) {
                    const float level = track >= 0 ? session_.automated(track, autoLaneOfEffect(slotOf(row), mix), d.param[mix]) : d.param[mix];
                    const int lit = static_cast<int>(std::round(static_cast<float>(mh) * level));
                    fill(r, kMarkX, top, 4, mh, kLine);
                    fill(r, kMarkX, top + mh - lit, 4, lit, here ? kText : kDim);
                }
                if (effectMeterParam(kind) >= 0) {
                    fill(r, kMarkX - 7, top, 4, mh, kLine);
                    fill(r, kMarkX - 7, top, 4, meterPixels(meterOf(row), mh), kBrass);
                }
                for (int p = 0; p < effectInfo(kind).params && track >= 0; ++p) moves = moves || session_.hasAutomation(track, project_.scene, autoLaneOfEffect(slotOf(row), p));
            }
        }
        if (moves) fill(r, kListX + 1, y + kLineH - 3, kListW - 2, 2, kPurple);
        if (cursor) outline(r, kListX - 1, y - 1, kListW + 2, kLineH + 2, holding_ ? kBrass : kTeal, 2);
    }
    // A tongue from the line to what is on the right, so the two read as one thing.
    fill(r, kListX + kListW, lineY(row_) + kLineH / 2 - 1, kPaneX - (kListX + kListW), 2, column_ == 0 ? (holding_ ? kBrass : kTeal) : kDim);
}

// The SENDS line, large: the two sends.
void FxScreen::drawSends(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    const SDL_Color accent = holding_ ? kBrass : kTeal;
    std::snprintf(text, sizeof text, "%s, SENDS", name);
    fonts.small.draw(r, kPaneX + 1.f, kPaneY + 2.f, text, column_ == 0 ? kText : kDim);
    for (int b = 0; b < kBuses; ++b) {
        const int y = kSendY + b * (kSendH + kSendGap), lane = autoLaneOfSend(b);
        const bool cursor = column_ == b + 1;
        box(r, kPaneX, y, kPaneW, kSendH, kPanel, kLine);
        fonts.small.draw(r, kPaneX + 12.f, y + 12.f, b == 0 ? "ECHO" : "SPACE", kDim);
        // Where it goes: what the bus holds just now.
        const EffectKind holds = static_cast<EffectKind>(project_.busEffects[b].kind);
        if (holds == EffectKind::None) std::snprintf(text, sizeof text, "the %s bus is EMPTY", b == 0 ? "ECHO" : "SPACE");
        else std::snprintf(text, sizeof text, "to the %s bus: %s%s%s", b == 0 ? "ECHO" : "SPACE", effectFamily(holds), effectTypeName(holds)[0] ? ", " : "", effectTypeName(holds));
        fonts.small.drawRight(r, kPaneX + kPaneW - 12.f, y + 12.f, text, kDim);
        // While the song plays an automated send shows where the pattern has it.
        const float shown = session_.automated(project_.track, lane, send(b));
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(shown * 100.f + 0.5f));
        fonts.large.draw(r, kPaneX + 12.f, y + 40.f, text, cursor ? accent : kText);
        wideBar(r, kPaneX + 12, y + 84, kPaneW - 24, 14, shown, 0, 0, cursor ? accent : kDim);
        if (session_.hasAutomation(project_.track, project_.scene, lane)) {
            fill(r, kPaneX + 12, y + 104, kPaneW - 24, 2, kPurple);
            fonts.small.draw(r, kPaneX + 12.f, y + 110.f, "moves with the pattern", kPurple);
        }
        if (cursor) outline(r, kPaneX - 1, y - 1, kPaneW + 2, kSendH + 2, accent, 2);
    }
}

// A slot's line, large: the heading, then its settings; or, with A held on the line, the list of effects.
void FxScreen::drawSlot(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    const EffectData& d = data(row_);
    const EffectKind kind = static_cast<EffectKind>(d.kind);
    const EffectInfo& info = effectInfo(kind);
    const int bus = busOf(row_), track = trackOf(row_), slot = slotOf(row_);
    const bool list = picking(), empty = kind == EffectKind::None;
    const SDL_Color accent = holding_ ? kBrass : kTeal;

    // The heading: where this is, what is in it, its TYPE, what it is doing.
    box(r, kPaneX, kPaneY, kPaneW, kHeadH, kPanel, kLine);
    if (bus >= 0) std::snprintf(text, sizeof text, "%s BUS: WHAT THE TRACKS SEND TO %s", bus == 0 ? "ECHO" : "SPACE", bus == 0 ? "ECHO" : "SPACE");
    else if (track == kMasterTrack) std::snprintf(text, sizeof text, "MASTER (EVERYTHING), EFFECT %d", slot + 1);
    else std::snprintf(text, sizeof text, "%s, EFFECT %d", name, slot + 1);
    fonts.small.draw(r, kPaneX + 12.f, kPaneY + 10.f, text, kDim);
    fonts.large.draw(r, kPaneX + 12.f, kPaneY + 30.f, empty ? "EMPTY" : effectFamily(kind), list ? kBrass : (empty ? kDim : kText));
    meterWords(kind, meterOf(row_), text, sizeof text);
    fonts.small.drawRight(r, kPaneX + kPaneW - 12.f, kPaneY + 36.f, text, kBrass);
    if (const int types = effectTypes(kind); types > 1) {
        // Its types in a row; the one it is stands out.
        float tx = kPaneX + 12.f;
        const float ty = kPaneY + 64.f;
        fonts.small.draw(r, tx, ty, "TYPE", kDim);
        tx += fonts.small.width("TYPE") + 12.f;
        for (int t = 0; t < types; ++t) {
            const char* type = effectTypeName(effectTypeAt(kind, t));
            const float w = fonts.small.width(type);
            if (t == effectTypeIndex(kind)) {
                fill(r, static_cast<int>(tx) - 6, static_cast<int>(ty) - 5, static_cast<int>(w) + 12, static_cast<int>(fonts.small.capHeight()) + 11, list ? kBrass : kText);
                fonts.small.draw(r, tx, ty, type, kBg);
            } else {
                fonts.small.draw(r, tx, ty, type, kDim);
            }
            tx += w + 20.f;
        }
        // Which way gives another, while A is held.
        if (list) drawKey(r, fonts, kPaneX + kPaneW - 12.f - keyWidth(fonts, "LEFT / RIGHT"), ty, "LEFT / RIGHT", kText);
    }
    if (list) outline(r, kPaneX - 1, kPaneY - 1, kPaneW + 2, kHeadH + 2, kBrass, 2);

    if (list) {
        // A held on the line: every effect it can hold is seen, not stepped through blind.
        const int count = familyListSize(bus), at = familyPlace(kind, bus);
        box(r, kPaneX, kPickY, kPaneW, kPickLineH * count + 8, kPanel, kLine);
        EffectGroup group = EffectGroup::None;
        for (int i = 0; i < count; ++i) {
            const EffectKind family = familyAt(i, bus);
            const int y = kPickY + 4 + i * kPickLineH;
            const bool cursor = i == at, first = effectInfo(family).group != group;
            group = effectInfo(family).group;
            if (i > 0 && first) fill(r, kPaneX + 8, y, kPaneW - 16, 1, kLine);
            if (cursor) {
                fill(r, kPaneX + 2, y + 1, kPaneW - 4, kPickLineH - 1, kPanel2);
                outline(r, kPaneX + 2, y + 1, kPaneW - 4, kPickLineH - 1, kBrass, 2);
            }
            const float ty = y + (kPickLineH - fonts.small.capHeight()) / 2.f - 1.f, my = y + (kPickLineH - fonts.medium.capHeight()) / 2.f - 1.f;
            if (first) fonts.small.draw(r, kPaneX + 12.f, ty, effectGroupName(group), kDim);
            fonts.medium.draw(r, static_cast<float>(kPickNameX), my, i == 0 ? "EMPTY" : effectFamily(family), cursor ? kBrass : kText);
            effectFamilySays(family, text, sizeof text);
            fonts.small.draw(r, static_cast<float>(kPickSaysX), ty, text, kDim);
        }
        return;
    }
    if (empty) {
        box(r, kPaneX, kSetY, kPaneW, kSetH * 2 + kSetGap, kPanel, kLine);
        fonts.small.draw(r, kPaneX + 12.f, kSetY + 24.f, "Nothing here.", kText);
        fonts.small.draw(r, kPaneX + 12.f, kSetY + 48.f, bus >= 0 ? "Hold A to see what this bus can hold." : "Hold A to see the effects and pick one.", kDim);
        return;
    }
    // While the song plays an automated setting shows where the pattern has it.
    float shown[kEffectParams];
    for (int i = 0; i < kEffectParams; ++i) shown[i] = track >= 0 ? session_.automated(track, autoLaneOfEffect(slot, i), d.param[i]) : d.param[i];
    for (int p = 0; p < info.params; ++p) {
        const int y = kSetY + p * (kSetH + kSetGap), barY = y + kSetH / 2 - kBarH / 2, choices = effectChoices(kind, p);
        const bool cursor = column_ == p + 1;
        box(r, kPaneX, y, kPaneW, kSetH, kPanel, kLine);
        // On a bus the MIX is how loud the bus comes back: it reads LEVEL.
        fonts.small.draw(r, kPaneX + 12.f, y + 10.f, bus >= 0 && p == effectMixParam(kind) ? "LEVEL" : info.paramNames[p], kDim);
        effectParamText(kind, p, shown[p], text, sizeof text, shown);
        fonts.fit(text, static_cast<float>(kValueRoom)).draw(r, static_cast<float>(kValueX), y + 30.f, text, cursor ? accent : kText);
        wideBar(r, kBarX, barY, kBarW, kBarH, shown[p], choices, whichChoice(shown[p], choices), cursor ? accent : kDim);
        if (track >= 0 && session_.hasAutomation(track, project_.scene, autoLaneOfEffect(slot, p))) {
            // It moves with the pattern in this scene: the purple line, and the words above the bar.
            fill(r, kPaneX + 1, y + kSetH - 3, kPaneW - 2, 2, kPurple);
            fonts.small.drawRight(r, static_cast<float>(kBarX + kBarW), y + 10.f, "moves with the pattern", kPurple);
        }
        if (p == effectMeterParam(kind)) {
            // What the effect is doing to the sound just now (COMP: how far it is turning it
            // down): a strip under the bar, growing from the right as a meter of this kind does.
            const int px = meterPixels(meterOf(row_), kBarW);
            if (px > 0) fill(r, kBarX + kBarW - px, barY + kBarH + 5, px, 4, kBrass);
        }
        if (cursor) outline(r, kPaneX - 1, y - 1, kPaneW + 2, kSetH + 2, accent, 2);
    }
}

void FxScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char name[24];
    trackName(project_, project_.track, name, sizeof name);
    topBar(r, fonts, View::Effects, name, project_);
    drawList(r, fonts);
    if (sends(row_)) drawSends(r, fonts);
    else drawSlot(r, fonts);

    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };
    const bool onSends = sends(row_);
    const EffectKind kind = onSends ? EffectKind::None : static_cast<EffectKind>(data(row_).kind);
    if (holding_) {
        if (column_ > 0) {
            hint("UP / DOWN", "turn it");
        } else if (!onSends) {
            hint("UP / DOWN", "effect");
            if (effectTypes(kind) > 1) hint("LEFT / RIGHT", "type");
        }
    } else if (column_ == 0) {
        if (settings(row_) > 0) hint("RIGHT", onSends ? "the sends" : "its settings");
        if (!onSends) hint("HOLD A", "pick effect");
        hint("B", onSends ? "send nothing" : "empty it");
        hint("START", project_.playing ? "stop" : "play");
    } else {
        hint("LEFT", "list");
        hint("HOLD A", "turn");
        hint("B", onSends ? "none" : "default value");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
