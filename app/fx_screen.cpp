#include "fx_screen.hpp"

#include "fx_picture.hpp"
#include "icons.hpp"

#include "mods.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace apotheca {

using namespace ui;

namespace {
constexpr float kTurnPerSecond = 0.9f, kNotch = 0.05f; // (kTurnPerSecond: the stick turning, however far it is pushed)
static_assert(kEffectPushes == 20, "kNotch is a push of the usual twenty");

// One push of the D-pad on an amount: to the next of its points, `pushes` across the range. The usual twenty keep
// the arithmetic they always had (a value the D-pad reaches is the same to the bit); BAND's FREQ has thirty.
float pushed(float now, int by, int pushes) {
    if (pushes == kEffectPushes) {
        const float notches = now / kNotch;
        return (by > 0 ? std::floor(notches + 1.001f) : std::ceil(notches - 1.001f)) * kNotch;
    }
    const float points = now * static_cast<float>(pushes);
    return (by > 0 ? std::floor(points + 1.001f) : std::ceil(points - 1.001f)) / static_cast<float>(pushes);
}
}

int FxScreen::bandGrip() const {
    if (!yHeld_ || holding_ || column_ < 1 || column_ > kBandQ + 1 || sends(row_) || tryRow_ >= 0 || modKnob_.held()) return -1;
    if (static_cast<EffectKind>(data(row_).kind) != EffectKind::Band) return -1;
    return column_ == kBandQ + 1 ? kBandQ : kBandGain;
}

void FxScreen::turnSetting(int index, float amount) {
    if (amount == 0.f) return;
    session_.setEffectParam(trackOf(row_), slotOf(row_), index, data(row_).param[index] + amount);
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
                for (int step = 0; step < kAutoSteps && t.automation.has(scene); ++step) {
                    const float v = t.automation[scene][autoLaneOfEffect(slot, had)][step];
                    if (!(v >= 0.f)) continue;
                    auto found = known.find(v);
                    if (found == known.end()) found = known.emplace(v, effectCarryValue(from, had, to, i, v)).first;
                    kept.push_back({scene, i, step, found->second});
                }
        }
    }

    // A MOD aimed at one of the old effect's settings follows it by name too, or goes OFF.
    ModSettings mods[kModsPerTrack];
    if (track >= 0)
        for (int m = 0; m < kModsPerTrack; ++m) {
            mods[m] = session.modData(track, m);
            mods[m].target = modTargetSlot(mods[m].target) == slot ? modTargetCarried(from, to, mods[m].target) : kModTargetNone;
        }

    const bool writing = session.writing();
    session.setWriting(false); // none of this is the player moving a knob
    session.setEffect(track, slot, to); // (clears all four of the slot's lanes, in every scene, and puts OFF a MOD aimed at them)
    if (session.effectData(track, slot).kind != static_cast<int>(to)) {
        session.setWriting(writing);
        return false; // refused: everything is as it was
    }
    float now[kEffectParams];
    effectCarry(from, to, was.param, busOfTrack(track) >= 0, now);
    for (int i = 0; i < info.params; ++i) session.setEffectParam(track, slot, i, now[i]);
    for (const Kept& k : kept) session.setAutomation(track, k.scene, autoLaneOfEffect(slot, k.index), k.step, k.value);
    if (track >= 0)
        for (int m = 0; m < kModsPerTrack; ++m)
            if (mods[m].target != kModTargetNone) session.setMod(track, m, mods[m]);
    session.setWriting(writing);
    return true;
}

bool moveEffectSlot(Session& session, int track, int slot, int by) {
    if (by == 0 || track < 0 || track >= session.project.trackCount() || slot < 0 || slot >= kTrackSlots) return false;
    const int to = slot + (by > 0 ? 1 : -1);
    if (to < 0 || to >= kTrackSlots) return false; // the ends stop
    const int a = slot, b = to;
    const TrackData& t = session.project.tracks[track];
    const EffectData dataA = t.effects[a], dataB = t.effects[b];
    // Each slot's automation, headed for the other's lanes.
    struct Wrote { int scene, lane, step; float value; };
    std::vector<Wrote> intoA, intoB;
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        if (!t.automation.has(scene)) continue;
        for (int p = 0; p < kEffectParams; ++p)
            for (int step = 0; step < kAutoSteps; ++step) {
                const float va = t.automation[scene][autoLaneOfEffect(a, p)][step];
                const float vb = t.automation[scene][autoLaneOfEffect(b, p)][step];
                if (va >= 0.f) intoB.push_back({scene, autoLaneOfEffect(b, p), step, va});
                if (vb >= 0.f) intoA.push_back({scene, autoLaneOfEffect(a, p), step, vb});
            }
    }
    // Each slot's remembered types, headed the same way.
    struct Known { bool has = false; EffectData data; };
    Known memA[kEffectKinds], memB[kEffectKinds];
    for (int k = 0; k < kEffectKinds; ++k) {
        float pa[kEffectParams], pb[kEffectParams];
        memA[k].has = session.rememberedType(track, a, static_cast<EffectKind>(k), pa);
        memB[k].has = session.rememberedType(track, b, static_cast<EffectKind>(k), pb);
        memA[k].data.kind = k;
        memB[k].data.kind = k;
        for (int i = 0; i < kEffectParams; ++i) {
            if (memA[k].has) memA[k].data.param[i] = pa[i];
            if (memB[k].has) memB[k].data.param[i] = pb[i];
        }
    }
    // The engine first: fresh DSP for both slots (a reorder re-patches; tails do not survive it).
    // If either refuses, everything is as it was.
    if (!session.engine.setEffect(track, a, static_cast<EffectKind>(dataB.kind)) ||
        !session.engine.setEffect(track, b, static_cast<EffectKind>(dataA.kind))) {
        session.engine.setEffect(track, a, static_cast<EffectKind>(dataA.kind));
        session.engine.setEffect(track, b, static_cast<EffectKind>(dataB.kind));
        return false;
    }
    const bool writing = session.writing();
    session.setWriting(false); // none of this is the player moving a knob
    TrackData& now = session.project.tracks[track];
    now.effects[a] = dataB;
    now.effects[b] = dataA;
    // The two slots' lanes swapped in the song, and the track's patterns sent once, whole (review
    // 2026-11-23: a command for every step written, up to 64 scenes of eight lanes, filled the
    // engine's queue on a busy track, and the whole song was sent again).
    bool movedAny = false;
    for (int scene = 0; scene < kMaxScenes; ++scene) {
        if (!now.automation.has(scene)) continue; // (nothing written there: no piece made for it)
        for (int p = 0; p < kEffectParams; ++p)
            for (const int l : {autoLaneOfEffect(a, p), autoLaneOfEffect(b, p)})
                for (float& v : now.automation[scene][l]) movedAny = movedAny || v >= 0.f, v = kAutoNone;
    }
    for (const std::vector<Wrote>* into : {&intoA, &intoB})
        for (const Wrote& w : *into) now.automation[w.scene][w.lane][w.step] = w.value;
    if (movedAny) {
        session.noteEdit();
        session.sendTrackImage(track);
    }
    for (int m = 0; m < kModsPerTrack; ++m) {
        ModSettings mod = session.modData(track, m);
        const int at = modTargetSlot(mod.target);
        if (at != a && at != b) continue;
        mod.target = kModFirstEffect + (at == a ? b : a) * 4 + modTargetSetting(mod.target);
        session.setMod(track, m, mod);
    }
    session.forgetTypes(track, a);
    session.forgetTypes(track, b);
    for (int k = 0; k < kEffectKinds; ++k) {
        if (memA[k].has) session.rememberType(track, b, memA[k].data);
        if (memB[k].has) session.rememberType(track, a, memB[k].data);
    }
    for (int q = 0; q < 2; ++q) { // the swapped settings to the fresh DSP, outright
        const int at = q == 0 ? a : b;
        for (int s = 0; s < kEffectParams; ++s) {
            Command c;
            c.type = Command::SetEffectParam;
            c.track = track;
            c.a = at;
            c.b = s;
            c.value = now.effects[at].param[s];
            session.engine.send(c);
        }
    }
    session.setWriting(writing);
    return true;
}

bool switchEffectType(Session& session, int track, int slot, EffectKind to) {
    if (slot < 0 || slot >= slotsOf(track)) return false;
    const EffectData was = session.effectData(track, slot);
    float back[kEffectParams];
    const bool known = session.rememberedType(track, slot, to, back);
    if (!changeEffectType(session, track, slot, to)) return false;
    session.rememberType(track, slot, was);
    if (known) {
        const bool writing = session.writing();
        session.setWriting(false); // not the player moving a knob
        for (int i = 0; i < effectInfo(to).params; ++i) session.setEffectParam(track, slot, i, back[i]);
        session.setWriting(writing);
    }
    return true;
}

// ---- trying an effect with A held ----------------------------------------------------------
//
// With A held on a slot's line the list of effects is open, and up / down / left / right try what the cursor
// comes to. The PROJECT is not touched while A is held: only the engine's slot is sent the effect tried
// (through the usual commands, so it fades in over 10 ms and nothing here holds an Effect), and the slot's
// automation is held in the engine so it does not move the effect being tried. Let go: what is tried goes in
// through the Session, as a pick always did. Called off: the slot is sent what the project holds.

namespace {
// What a slot holding `start` would hold with `to` put in by the list of effects: `to` itself, with the
// settings it shares with `start` carried by name (another type of the same family), or at its usual
// settings (another family; its first type's carried to the type picked, as stepping there always did).
// Another type the slot remembers (`remembered`, else nullptr) comes as it was left: switchEffectType.
EffectData wouldGoIn(const EffectData& start, EffectKind to, bool onBus, const float* remembered) {
    EffectData d;
    d.kind = static_cast<int>(to);
    const EffectKind from = static_cast<EffectKind>(start.kind);
    if (to == from) return start;
    if (to == EffectKind::None) return d;
    if (from != EffectKind::None && std::strcmp(effectFamily(from), effectFamily(to)) == 0) {
        if (remembered) {
            for (int i = 0; i < kEffectParams; ++i) d.param[i] = i < effectInfo(to).params ? remembered[i] : effectDefault(to, i, onBus); // (past its settings: as setEffect leaves them)
            return d;
        }
        effectCarry(from, to, start.param, onBus, d.param);
        return d;
    }
    const EffectKind first = effectTypeAt(to, 0);
    float usual[kEffectParams];
    for (int i = 0; i < kEffectParams; ++i) usual[i] = effectDefault(first, i, onBus);
    if (first == to) {
        for (int i = 0; i < kEffectParams; ++i) d.param[i] = usual[i];
    } else {
        effectCarry(first, to, usual, onBus, d.param);
    }
    return d;
}

bool sameEffect(const EffectData& a, const EffectData& b) {
    if (a.kind != b.kind) return false;
    for (int i = 0; i < kEffectParams; ++i)
        if (a.param[i] != b.param[i]) return false;
    return true;
}
} // namespace

void FxScreen::sendSlot(const EffectData& d) {
    const int track = tryTrack_, slot = trySlot_; // (whose it was: the track may have changed since)
    const EffectKind kind = static_cast<EffectKind>(d.kind);
    session_.engine.setEffect(track, slot, kind); // (a loss is counted, and mended by the song sent again)
    Command c;
    c.type = Command::SetEffectParam;
    c.track = track;
    c.a = slot;
    for (int i = 0; i < effectInfo(kind).params; ++i) {
        c.b = i;
        c.value = d.param[i];
        session_.engine.send(c);
    }
}

void FxScreen::holdAutomation(bool held) {
    // (The master and the buses have no automation.) A hold is said again with every slot tried: a song sent
    // again in the middle (a full queue) let the engine's go.
    if ((!held && !held_) || tryTrack_ < 0) return;
    held_ = held;
    Command c;
    c.type = Command::HoldEffectAutomation;
    c.track = tryTrack_;
    c.a = trySlot_; // (kept: let go, the row is no longer the one tried)
    c.b = held ? 1 : 0;
    session_.engine.send(c);
}

void FxScreen::startTrying() {
    tryRow_ = row_;
    tryTrack_ = trackOf(row_);
    trySlot_ = slotOf(row_);
    tried_ = session_.effectData(tryTrack_, slotOf(row_));
    away_ = false;
    held_ = false;
}

void FxScreen::tryKind(EffectKind kind) {
    if (tryRow_ < 0) return;
    const EffectData& start = session_.effectData(tryTrack_, slotOf(tryRow_));
    float remembered[kEffectParams];
    const bool known = session_.rememberedType(tryTrack_, slotOf(tryRow_), kind, remembered);
    tried_ = wouldGoIn(start, kind, busOf(tryRow_) >= 0, known ? remembered : nullptr);
    if (!away_ && sameEffect(tried_, start)) return; // (the engine has it already)
    holdAutomation(true);
    sendSlot(tried_);
    away_ = !sameEffect(tried_, start);
}

void FxScreen::callOff() {
    if (tryRow_ < 0) return;
    if (away_) sendSlot(session_.effectData(tryTrack_, slotOf(tryRow_)));
    holdAutomation(false);
    tryRow_ = -1;
    away_ = false;
}

void FxScreen::letGo() {
    if (tryRow_ < 0) return;
    const int track = tryTrack_, slot = slotOf(tryRow_);
    const EffectData start = session_.effectData(track, slot);
    const EffectKind from = static_cast<EffectKind>(start.kind), to = static_cast<EffectKind>(tried_.kind);
    if (!away_ || to == from) {
        callOff(); // the one it started on: as it was
        return;
    }
    tryRow_ = -1; // (data() is the project's again)
    away_ = false;
    if (to != EffectKind::None && from != EffectKind::None && std::strcmp(effectFamily(from), effectFamily(to)) == 0) {
        switchEffectType(session_, track, slot, to); // the settings the two share (or its own, remembered), and their automation, go with it
    } else {
        const EffectKind first = to == EffectKind::None ? to : effectTypeAt(to, 0);
        session_.setEffect(track, slot, first); // (its automation is cleared: it meant nothing to this one)
        if (first != to) changeEffectType(session_, track, slot, to);
    }
    holdAutomation(false);
}

float FxScreen::typesWidth(const Font& font, const std::vector<std::string>& names, int from, int to) {
    float w = 0.f;
    for (int t = from; t < to; ++t) w += font.width(names[t].c_str()) + (t + 1 < to ? font.width(kPickComma) : 0.f);
    return w;
}

FxScreen::PickRows FxScreen::pickRows(const Fonts& fonts, const std::vector<std::string>& names) {
    const int count = static_cast<int>(names.size());
    const float room = static_cast<float>(kPickTypesRoom);
    const Fonts& large = fonts.fixed();
    // How many rows is LARGE's at every size (as the TYPE row's view is): a smaller size in one row where LARGE needs
    // two would be other words (the comma at the split). MOD's six since PITCH are two rows at LARGE.
    const bool oneAtLarge = count <= 1 || typesWidth(large.small, names, 0, count) <= room; // (one name is never split)
    if (oneAtLarge && typesWidth(fonts.small, names, 0, count) <= room) return {&fonts, 1, count};
    if (oneAtLarge) return {&large, 1, count};
    // Two rows: where the longer of the two is shortest.
    int split = 1;
    float best = -1.f;
    for (int s = 1; s < count; ++s) {
        const float a = typesWidth(large.small, names, 0, s), b = typesWidth(large.small, names, s, count), w = a > b ? a : b;
        if (best < 0.f || w < best) best = w, split = s;
    }
    // Two rows: in the size chosen where each fits, else LARGE's.
    const bool fits = typesWidth(fonts.small, names, 0, split) <= room && typesWidth(fonts.small, names, split, count) <= room;
    return {fits ? &fonts : &large, 2, split};
}

float FxScreen::typeRowRoom(const Fonts& fonts, int types) {
    const float end = kPaneX + kPaneW - 12.f - (types > 1 ? keyWidth(fonts, "LEFT / RIGHT") + 12.f : 0.f); // the key's room, held or not
    return end - (kPaneX + 12.f + fonts.small.width("TYPE") + 12.f);
}

void FxScreen::typesInView(const Font& font, const std::vector<std::string>& names, int at, float room, int* from, int* to) {
    const int count = static_cast<int>(names.size());
    at = at < 0 ? 0 : (at >= count ? count - 1 : at);
    const auto wide = [&](int a, int b) { return typeRowWidth(font, names, a, b); };
    int a = at, b = count > 0 ? at + 1 : 0;
    for (bool grew = true; grew;) {
        grew = false;
        if (b < count && wide(a, b + 1) <= room) ++b, grew = true;
        if (a > 0 && wide(a - 1, b) <= room) --a, grew = true;
    }
    *from = a, *to = b;
}

float FxScreen::typeRowWidth(const Font& font, const std::vector<std::string>& names, int from, int to) {
    const int count = static_cast<int>(names.size());
    float w = (from > 0 ? font.width(kTypeMore) + kTypeGapX : 0.f) + (to < count ? font.width(kTypeMore) + kTypeGapX : 0.f);
    for (int t = from; t < to; ++t) w += font.width(names[t].c_str()) + (t + 1 < to ? kTypeGapX : 0.f);
    return w;
}

const Fonts& FxScreen::typeRowView(const Fonts& fonts, const std::vector<std::string>& names, int at, int* from, int* to) {
    const Fonts& large = fonts.fixed();
    const int count = static_cast<int>(names.size());
    typesInView(large.small, names, at, typeRowRoom(large, count), from, to);
    return typeRowWidth(fonts.small, names, *from, *to) <= typeRowRoom(fonts, count) ? fonts : large;
}

std::vector<std::string> FxScreen::typeWords(EffectKind kind) {
    std::vector<std::string> words;
    for (int t = 0; t < effectTypes(kind); ++t) {
        std::string word = effectTypeName(effectTypeAt(kind, t));
        for (char& c : word) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        words.push_back(word);
    }
    return words;
}

void FxScreen::goTo(int row, int column) {
    row_ = ((row % kRows) + kRows) % kRows;
    const int last = settings(row_);
    column_ = column < 0 ? 0 : (column > last ? last : column);
    if (column_ > 0) last_[row_] = column_;
}

void FxScreen::cursorWords(char* out, int size) const {
    if (!out || size <= 0) return;
    static const char* const tags[kRows] = {"1", "2", "3", "SENDS", "ECHO", "SPACE", "MASTER 1", "MASTER 2"};
    const bool line = !sends(row_);
    const EffectInfo& info = effectInfo(line ? static_cast<EffectKind>(data(row_).kind) : EffectKind::None);
    const char* what = line ? (info.name ? info.name : "?") : "";
    const char* dial = column_ < 1 ? "the list" : (line ? (column_ - 1 < info.params && info.paramNames[column_ - 1] ? info.paramNames[column_ - 1] : "?") : (column_ == 1 ? "ECHO" : "SPACE"));
    std::snprintf(out, static_cast<std::size_t>(size), ", FX track %d line %s %s, dial %d %s%s", project_.track + 1, tags[row_], what, column_, dial, holding_ ? ", A held" : "");
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

void FxScreen::hear(bool held) {
    const TrackKind kind = project_.current().kind;
    if (held && kind == TrackKind::Drums) {
        session_.hearLaneHeld(project_.track, 0, kPlacedLevel, this);
    } else if (held && isSynth(kind)) { // (the sampler and CLOUD too) as long as X is held, over the song too (gesture audit G33)
        session_.auditionHeld(project_.track, 60, kPlacedLevel, this);
    } else if (kind == TrackKind::Drums) {
        Command c;
        c.type = Command::Trigger;
        c.track = project_.track;
        c.value = kPlacedLevel;
        session_.engine.send(c);
    } else if (isSynth(kind)) {
        session_.audition(project_.track, 60, kPlacedLevel);
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
    if (yHeld_ && !holding_ && column_ == 0 && row_ < kTrackSlots && upDown && tryRow_ < 0 && !deleting_) {
        // Y held on one of the track's own lines: up / down moves it there, settings, automation
        // and MODs with it. The cursor goes with it; the ends stop.
        const int by = b == Button::Up ? -1 : 1;
        if (moveEffectSlot(session_, project_.track, row_, by)) goTo(row_ + by, 0);
        return;
    }
    if (const int along = bandGrip(); along >= 0) {
        // Y held on BAND's FREQ, GAIN or Q (gesture audit G18): LEFT / RIGHT is FREQ, UP / DOWN GAIN (on the Q
        // dial, Q), a push each as A held gives the one under the cursor. The cursor stays.
        const int index = upDown ? along : kBandFreq;
        const int by = b == Button::Up || b == Button::Right ? 1 : -1;
        session_.setEffectParam(trackOf(row_), slotOf(row_), index, pushed(data(row_).param[index], by, effectPushes(EffectKind::Band, index)));
        return;
    }
    if (!holding_) {
        if (column_ == 0) {
            // In the list: up and down through its lines; right into the line's settings, where it was last.
            if (b == Button::Up) goTo(row_ - 1, 0);
            if (b == Button::Down) goTo(row_ + 1, 0);
            if (b == Button::Right && settings(row_) > 0) goTo(row_, last_[row_]);
        } else {
            // In the settings, which sit side by side as dials: LEFT / RIGHT walk them (owner, 2026-11-25: "moving
            // up and down to traverse the parameters is odd"). LEFT from the first is back to the list, on the same
            // line; RIGHT on the last stops. UP / DOWN do nothing without A. The slot's TYPE stays on A HELD + LEFT /
            // RIGHT (below; owner, 2026-11-14): plain LEFT is only ever the cursor.
            if (b == Button::Right) goTo(row_, column_ + 1);
            if (b == Button::Left) {
                if (column_ > 1) goTo(row_, column_ - 1);
                else column_ = 0;
            }
        }
        // Y held, and this move came onto BAND's FREQ, GAIN or Q (from its line, or from SHAPE): the push that
        // brought the cursor here was a move, so it turns nothing (moved(): a fresh push does).
        if (bandGrip() >= 0) gripEntered_ = true;
        return;
    }
    if (column_ == 0) {
        if (sends(row_)) return; // the sends are turned one at a time, on the right: the D-pad is dead here, and the bar says so
        const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
        const int bus = busOf(row_);
        if (tryRow_ != row_) startTrying();
        if (upDown) {
            // The list of effects, which is drawn as this is held: down is the next line of it. A family is
            // tried as its first type, at its usual settings (as the one it started on, at its own). The ends stop.
            const int from = familyPlace(kind, bus), to = from + (b == Button::Down ? 1 : -1);
            if (to < 0 || to >= familyListSize(bus)) return;
            const EffectKind family = familyAt(to, bus);
            const EffectKind started = static_cast<EffectKind>(session_.effectData(tryTrack_, slotOf(row_)).kind);
            tryKind(family != EffectKind::None && started != EffectKind::None && std::strcmp(effectFamily(family), effectFamily(started)) == 0 ? started : family);
        } else {
            // Its TYPE: the next of the family's, or the one before, with the settings they share. The ends stop.
            const EffectKind next = effectTypeAt(kind, effectTypeIndex(kind) + (b == Button::Right ? 1 : -1));
            if (next == EffectKind::None || kind == EffectKind::None) return;
            tryKind(next);
        }
        if (!project_.playing) hear();
        return;
    }
    if (!upDown) {
        // A held on a setting, LEFT / RIGHT: the slot's TYPE before / after, in the same grip that turns the
        // value with UP / DOWN (as LEFT / RIGHT is the TYPE with A held in the list). The ends stop.
        if (!sends(row_) && stepType(b == Button::Right ? 1 : -1) && !project_.playing) hear();
        return;
    }
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
        session_.setEffectParam(trackOf(row_), slotOf(row_), index, pushed(now, by, effectPushes(kind, index)));
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

int FxScreen::knobHere() const {
    if (column_ < 1) return -1;
    if (sends(row_)) return kModFirstSend + (column_ - 1);
    if (row_ < kTrackSlots) return kModFirstEffect + row_ * 4 + (column_ - 1);
    return -1; // a bus's or the master's setting: nothing there is automated or modulated
}

// After a push of a direction (press): one that brought the cursor onto BAND's dials with Y held stops the held
// D-pad's repeats, and the stick waits until it comes back to the middle (stickWait_), so neither goes on into
// the grip and turns FREQ or Q (review of G18: a sideways slip of the stick while moving the line swept FREQ).
// The roll's rule for X (note_screen.cpp, fxWait_): a direction that was already held is not a turn.
void FxScreen::moved(const Input& input) {
    if (!gripEntered_) return;
    gripEntered_ = false;
    repeating_ = Button::None;
    saved_ = 0.f;
    stickWas_ = false;
    stickWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
}

void FxScreen::endMod(const Input& input) {
    if (!modKnob_.held()) return;
    modKnob_.letGo();
    stickWait_ = input.stickX() != 0.f || input.stickY() != 0.f;
    repeating_ = Button::None;
}

void FxScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (event.down && b != Button::B) deleting_ = false, deletePart_ = 0.f; // any other press calls a held B off
    // X: hear the track. Held on a track's setting or a send (A not held), it is also that knob's MOD, and
    // the X-held check comes before every other button's own job (L1 / R1's track, B's default, the D-pad).
    if (b == Button::X) {
        if (!event.down) {
            endMod(input);
            return;
        }
        const bool knob = !holding_ && knobHere() >= 0 && modKnob_.hold(project_.track, knobHere(), input);
        if (knob) repeating_ = Button::None;
        hear(true); // as long as X is held, playing or not (a MOD held is heard only while stopped: call E)
        return;
    }
    if (modKnob_.held() && modKnob_.onButton(event, lastMs_)) return;
    if (b == Button::Y) {
        yHeld_ = event.down; // (with X held the MOD has it: above)
        return;
    }
    if (b == Button::A) {
        if (!event.down && tryRow_ >= 0) letGo(); // what is tried goes in (or, on the one it started on, it is as it was)
        holding_ = event.down;
        if (event.down && column_ == 0 && !sends(row_)) startTrying();
        saved_ = 0.f;
        stickWas_ = false;
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (b == Button::L1 || b == Button::R1) {
        stepTrack(b == Button::R1 ? 1 : -1);
    } else if (isDirection(b)) {
        press(b);
        repeating_ = b;
        repeat_.restart();
        moved(input);
    } else if (b == Button::B && holding_) {
        // B with A held calls it off: the slot is as it was, and A does nothing more until it is pressed again.
        if (tryRow_ >= 0) callOff(), holding_ = false;
    } else if (b == Button::B) {
        if (sends(row_)) {
            // On the line: nothing sent to either bus. On a send: that one.
            for (int bus = 0; bus < kBuses; ++bus)
                if (column_ == 0 || column_ == bus + 1) emptySend(bus);
        } else if (column_ == 0) {
            // Held, it deletes the effect for good (update(), when the bar is full); a tap does nothing.
            deleting_ = data(row_).kind != static_cast<int>(EffectKind::None);
            deleteTimed_ = false;
            deletePart_ = 0.f;
            deleteRow_ = row_;
        } else {
            const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
            // Its default value (on a bus, LEVEL's is full), and whatever automation it had in this cell goes.
            const bool writing = session_.writing();
            session_.setWriting(false);
            if (trackOf(row_) >= 0) session_.clearAutomation(trackOf(row_), project_.scene, autoLaneOfEffect(slotOf(row_), column_ - 1));
            session_.setEffectParam(trackOf(row_), slotOf(row_), column_ - 1, effectDefault(kind, column_ - 1, busOf(row_) >= 0));
            session_.setWriting(writing);
        }
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

bool FxScreen::stepType(int by) {
    if (sends(row_)) return false;
    const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
    const EffectKind next = effectTypeAt(kind, effectTypeIndex(kind) + by); // (None past an end, and for nothing)
    if (next == EffectKind::None || kind == EffectKind::None) return false;
    if (!switchEffectType(session_, trackOf(row_), slotOf(row_), next)) return false;
    goTo(row_, column_); // the row stays (one the new type does not have: its last)
    return true;
}

const char* FxScreen::typeKeyFor(int column, int types, int at) {
    if (column < 1) return nullptr; // the list. (A family of one: neither way is true, so no key.)
    const bool right = at + 1 < types, left = at > 0;
    return left && right ? "LEFT / RIGHT" : (right ? "RIGHT" : (left ? "LEFT" : nullptr));
}

const char* FxScreen::walkKeyFor(int column, int last) {
    if (column < 1) return nullptr;
    return column < last ? "LEFT / RIGHT" : "LEFT";
}

// With A held on a slot's setting: the direction(s) that switch its TYPE, or nullptr (not held, the list,
// SENDS, a family of one).
const char* FxScreen::typeKey() const {
    if (!holding_ || column_ == 0 || sends(row_)) return nullptr;
    const EffectKind kind = static_cast<EffectKind>(data(row_).kind);
    return typeKeyFor(column_, effectTypes(kind), effectTypeIndex(kind));
}

// L1 / R1: the track before / after, on the same line and setting. It stops at the first and the last, as the
// SONG screen's columns do. An effect being tried is called off first (it is the old track's). A held on a
// setting is kept where the new track's line holds the same effect (gesture audit G44: one dial turned across
// the tracks, a press each), so the stick and the D-pad turn the same setting there; another effect, or none,
// lets A go: its setting under the cursor could be a choice, another job, or the list (an empty slot), where a
// push would try effects. The sends are the same two on every track, and a bus's or the master's line is the
// same whatever the track.
void FxScreen::stepTrack(int by) {
    const bool turning = holding_ && tryRow_ < 0 && column_ > 0;
    const int was = sends(row_) ? -1 : data(row_).kind;
    callOff();
    holding_ = false;
    const int to = project_.track + by;
    if (to >= 0 && to < project_.trackCount()) project_.track = to; // (the line stays; a setting it no longer has: the nearest, in update())
    holding_ = turning && (sends(row_) ? -1 : data(row_).kind) == was;
}

void FxScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) holding_ = false;
    if (!input.held(Button::Y)) yHeld_ = false;
    // The RIGHT (D-pad or stick) that came in from the list is let go: RIGHT switches the TYPE from now.
    // Trying an effect with A held: A let go unseen, or the track changed under it: nothing goes in.
    if (tryRow_ >= 0 && (!holding_ || trackOf(tryRow_) != tryTrack_ || row_ != tryRow_)) callOff(), holding_ = false;
    // B held on an effect's line: the bar fills for half a second (G29), then the effect is deleted. Let go,
    // moved off the line, or A held: nothing is deleted.
    if (!input.held(Button::B) || holding_ || column_ != 0 || row_ != deleteRow_ || sends(row_)) deleting_ = false;
    if (!deleting_) deletePart_ = 0.f;
    else {
        if (!deleteTimed_) deleteTimed_ = true, deleteSince_ = nowMs;
        deletePart_ = static_cast<float>(nowMs - deleteSince_) / static_cast<float>(kDeleteHoldMs);
        if (deletePart_ >= 1.f) {
            deleting_ = false;
            deletePart_ = 0.f;
            session_.setEffect(trackOf(row_), slotOf(row_), EffectKind::None);
        }
    }

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    // The track may have changed under the cursor, or the effect: the line stays, and the setting if
    // there is still one there (the nearest, else back in the list). What was last used is not forgotten.
    if (column_ > settings(row_)) column_ = settings(row_);
    const float x = input.stickX(), y = input.stickY();
    // X held on a knob: the stick and the D-pad are its MOD's, and the cursor stays on it.
    // Let go when X is up, when the knob under the cursor is another (an undo can take its setting away), or
    // when another track is up (picked on SONG with X still held): the gesture is the knob's on the track shown.
    if (!input.held(Button::X) || knobHere() != modKnob_.target() || project_.track != modKnob_.track()) endMod(input);
    if (modKnob_.held()) {
        modKnob_.update(input, nowMs, dt);
        return;
    }
    if (stickWait_ && x == 0.f && y == 0.f) stickWait_ = false;
    if (stickWait_) return; // (the stick was pushed for the MOD: it moves nothing until it has come back)

    // A held D-pad (and the stick, at twice it): the list of effects and a TYPE (a choice) at the list's pace, after
    // the cursor's wait they always had (G32: the list ran at a bare 200 ms); the cursor, a setting and a send at
    // the cursor's.
    const auto runs = [&](Button way) {
        const bool types = typeKey() != nullptr && (way == Button::Left || way == Button::Right); // a TYPE: a choice
        return picking() || types ? Pace::List : Pace::Cursor;
    };
    // A setting that is a choice (the filter's TYPE, the delay's TIME) is not turned: with A held it clicks from one
    // choice to the next, at once, and held on, the stick runs at twice the LIST's pace (the standard's for choices:
    // CLAUDE.md, "choices click once per push; they are not sliders"), not twice the cursor's its held D-pad was left
    // at (gesture audit G08, the owner's question): at the cursor's, a push passed several types.
    const bool onSend = sends(row_);
    const bool choice = !onSend && column_ > 0 && effectChoices(static_cast<EffectKind>(data(row_).kind), column_ - 1) > 0;
    if (const int along = bandGrip(); along >= 0) {
        // Y held on BAND's FREQ, GAIN or Q (gesture audit G18): the stick moves the bell both ways at once, as
        // the mixer turns level and pan (no lock to one axis): across is FREQ, up / down GAIN (on the Q dial, Q),
        // each at the pace a turn with A held has, however far it is pushed. A slight drift off one way is not
        // the other (ui::stickAxes).
        const StickAxes both = stickAxes(x, y);
        turnSetting(kBandFreq, stickWay(both.x) * kTurnPerSecond * dt);
        turnSetting(along, -stickWay(both.y) * kTurnPerSecond * dt);
        saved_ = 0.f;
        stickWas_ = false;
    } else if (holding_ && column_ > 0 && !choice) {
        // A setting or a send: the stick turns it, however far it is pushed (the D-pad for exact steps); up / down
        // only, and not by a sideways push that drifts (ui::stickAxes).
        turn(-stickWay(stickAxes(x, y).y) * kTurnPerSecond * dt);
    } else {
        // Moving, or stepping through the effects or the types: one at once, then at twice the pace a held D-pad
        // runs the same way (runs(), above), however far it is pushed; a choice with A held at twice the list's.
        // With A held (not picking) only up / down counts, and not by a sideways push that drifts.
        const bool upDownOnly = holding_ && !picking();
        const bool sideways = !upDownOnly && std::fabs(x) > std::fabs(y);
        const float push = sideways ? x : upDownOnly ? stickAxes(x, y).y : y;
        const Button way = sideways ? (push > 0.f ? Button::Right : Button::Left) : (push > 0.f ? Button::Down : Button::Up);
        if (push == 0.f) {
            saved_ = 0.f;
            stickWas_ = false;
        } else {
            saved_ += stickWas_ ? stickPerSecond(upDownOnly && choice ? Pace::List : runs(way)) * dt : 1.f;
            stickWas_ = true;
            for (; saved_ >= 1.f; saved_ -= 1.f) {
                press(way);
                if (gripEntered_) break; // (onto BAND's dials with Y held: moved() below)
            }
            moved(input);
        }
    }
    while (repeating_ != Button::None && repeat_.due(nowMs, input.held(repeating_), Pace::Cursor, runs(repeating_))) {
        press(repeating_);
        moved(input);
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
        if (here && deletePart_ > 0.f) fill(r, kListX + 1, y + 1, static_cast<int>((kListW - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), kLineH - 2, faded(kStop, 150)); // B held: it fills
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
                // (A family and type too long for the line draw the family small too, or are held at LARGE together: lineFonts.)
                float gap = 0.f;
                bool familySmall = false, typeAlone = false;
                const Fonts& set = lineFonts(fonts, kind, &gap, &familySmall, &typeAlone);
                const Font& familyFont = familySmall ? set.small : set.medium;
                const float setTy = y + (kLineH - set.small.capHeight()) / 2.f - 1.f, setMy = y + (kLineH - familyFont.capHeight()) / 2.f - 1.f;
                if (typeAlone) {
                    set.medium.draw(r, static_cast<float>(kNameX), y + (kLineH - set.medium.capHeight()) / 2.f - 1.f, effectTypeShown(kind), kText); // (DRIVE SATURATE does not fit: SATURATE)
                } else {
                    familyFont.draw(r, static_cast<float>(kNameX), familySmall ? setTy + 1.f : setMy, family, kText);
                    set.small.draw(r, kNameX + familyFont.width(family) + gap, setTy + 1.f, effectTypeShown(kind), kDim);
                }
                // The tiny mark: how much of it there is (its MIX, or a bus's LEVEL), and for one with
                // a meter (COMP, DUCK) what it is doing just now, down from the top.
                const int mh = kLineH - 14, top = y + 7, mix = effectMixParam(kind), track = trackOf(row);
                if (mix >= 0) {
                    const float level = track >= 0 && !tryingOn(row) ? session_.automated(track, autoLaneOfEffect(slotOf(row), mix), d.param[mix]) : d.param[mix];
                    const int lit = static_cast<int>(std::round(static_cast<float>(mh) * level));
                    fill(r, kMarkX, top, 4, mh, kLine);
                    fill(r, kMarkX, top + mh - lit, 4, lit, here ? kText : kDim);
                }
                if (effectMeterParam(kind) >= 0) {
                    fill(r, kMarkX - 7, top, 4, mh, kLine);
                    fill(r, kMarkX - 7, top, 4, meterPixels(meterOf(row), mh), kBrass);
                }
                for (int p = 0; p < effectInfo(kind).params && track >= 0 && !tryingOn(row); ++p) moves = moves || session_.hasAutomation(track, project_.scene, autoLaneOfEffect(slotOf(row), p));
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
        else std::snprintf(text, sizeof text, "to the %s bus: %s%s%s", b == 0 ? "ECHO" : "SPACE", effectFamily(holds), effectTypeShown(holds)[0] ? ", " : "", effectTypeShown(holds));
        fonts.small.drawRight(r, kPaneX + kPaneW - 12.f, y + 12.f, text, kDim);
        // While the song plays an automated send shows where the pattern has it.
        const float shown = session_.automated(project_.track, lane, send(b));
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(shown * 100.f + 0.5f));
        fonts.large.draw(r, kPaneX + 12.f, static_cast<float>(y + kSendValueY), text, cursor ? accent : kText);
        wideBar(r, kPaneX + 12, y + kSendBarY, kPaneW - 24, 14, shown, 0, 0, cursor ? accent : kDim);
        drawModMark(r, kPaneX + 12, y + kSendBarY, kPaneW - 24, 14, modMarkAt(session_, project_.track, lane, shown)); // a MOD on it runs (batch 6)
        if (session_.hasAutomation(project_.track, project_.scene, lane)) {
            fill(r, kPaneX + 12, y + kSendLineY, kPaneW - 24, 2, kPurple);
            fonts.small.draw(r, kPaneX + 12.f, static_cast<float>(y + kSendWordsY), "moves with the pattern", kPurple);
        }
        if (cursor) outline(r, kPaneX - 1, y - 1, kPaneW + 2, kSendH + 2, accent, 2);
    }
    if (modKnob_.held()) drawModPanel(r, fonts); // X held on a send: its MOD in the other send's row
}

void FxScreen::drawModPanel(SDL_Renderer* r, const Fonts& fonts) const {
    const int target = modKnob_.target(), y = modPanelY();
    const char* knob = modTargetIsSend(target) ? (target == kModFirstSend ? "ECHO" : "SPACE")
                                               : effectInfo(static_cast<EffectKind>(data(row_).kind)).paramNames[modTargetSetting(target)];
    modKnob_.drawFields(r, fonts, kFxModPlaces, static_cast<float>(y), kPaneX, y, kPaneW, modPanelH(), knob);
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

    // The heading: where this is, what is in it, its TYPE, what it is doing. (X held on a setting: its
    // MOD's fields in its place.)
    if (modKnob_.held()) {
        drawModPanel(r, fonts);
    } else {
        box(r, kPaneX, kPaneY, kPaneW, kHeadH, kPanel, kLine);
        if (bus >= 0) std::snprintf(text, sizeof text, "%s BUS: WHAT THE TRACKS SEND TO %s", bus == 0 ? "ECHO" : "SPACE", bus == 0 ? "ECHO" : "SPACE");
        else if (track == kMasterTrack) std::snprintf(text, sizeof text, "MASTER (EVERYTHING), EFFECT %d", slot + 1);
        else std::snprintf(text, sizeof text, "%s, EFFECT %d", name, slot + 1);
        if (deletePart_ > 0.f) {
            // B held on the line: a bar along the heading's foot fills as the line does, and the heading says what is
            // coming in place of where it is. (A strip, not the whole heading: nothing the heading says is drawn in it.)
            fill(r, kPaneX + 1, kPaneY + kHeadH - 6, static_cast<int>((kPaneW - 2) * (deletePart_ > 1.f ? 1.f : deletePart_)), 5, faded(kStop, 150));
            fonts.small.draw(r, kPaneX + 12.f, kPaneY + 10.f, kDeletingWords, kText);
        } else {
            fonts.small.draw(r, kPaneX + 12.f, kPaneY + 10.f, text, kDim);
        }
        fonts.large.draw(r, kPaneX + 12.f, kPaneY + 30.f, empty ? "EMPTY" : effectFamily(kind), list ? kBrass : (empty ? kDim : kText));
        meterWords(kind, meterOf(row_), text, sizeof text);
        fonts.small.drawRight(r, kPaneX + kPaneW - 12.f, kPaneY + 36.f, text, kBrass);
        if (const int types = effectTypes(kind); effectFamilyNamesTypes(kind)) {
            // Its types in a row; the one it is stands out. (MOD has one, CHORUS: it is named all the same.)
            std::vector<std::string> names;
            for (int t = 0; t < types; ++t) names.push_back(effectTypeName(effectTypeAt(kind, t)));
            drawTypeRow(r, fonts, names, effectTypeIndex(kind), list, typeKey());
        }
    }
    if (list) outline(r, kPaneX - 1, kPaneY - 1, kPaneW + 2, kHeadH + 2, kBrass, 2);

    if (list) {
        // A held on the line: every family it can hold is seen, not stepped through blind, each naming its types
        // (the one in the slot lit), or for a family whose one type is its own word (FILTER), saying what it is.
        const int count = familyListSize(bus), at = familyPlace(kind, bus);
        box(r, kPaneX, kPickY, kPaneW, kPickLineH * count + 2 * kPickPad, kPanel, kLine);
        for (int i = 0; i < count; ++i) {
            const EffectKind family = familyAt(i, bus);
            const int y = pickLineY(i);
            const bool cursor = i == at;
            if (i > 0) fill(r, kPaneX + 8, y, kPaneW - 16, 1, kLine);
            if (cursor) {
                fill(r, kPaneX + 2, y + 1, kPaneW - 4, kPickLineH - 1, kPanel2);
                outline(r, kPaneX + 2, y + 1, kPaneW - 4, kPickLineH - 1, kBrass, 2);
            }
            const float my = y + (kPickLineH - fonts.medium.capHeight()) / 2.f - 1.f;
            fonts.medium.draw(r, static_cast<float>(kPickNameX), my, i == 0 ? "EMPTY" : effectFamily(family), cursor ? kBrass : kText);
            if (!effectFamilyNamesTypes(family)) {
                effectFamilySays(family, text, sizeof text);
                drawPickTypes(r, fonts, {std::string(text)}, -1, y);
            } else {
                drawPickTypes(r, fonts, typeWords(family), cursor ? effectTypeIndex(kind) : -1, y);
            }
        }
        return;
    }
    if (empty) {
        box(r, kPaneX, kSetY, kPaneW, kPageBottom - kSetY, kPanel, kLine);
        fonts.small.draw(r, kPaneX + 12.f, kSetY + 24.f, "Nothing here.", kText);
        fonts.small.draw(r, kPaneX + 12.f, kSetY + 48.f, bus >= 0 ? "Hold A to see what this bus can hold." : "Hold A to see the effects and pick one.", kDim);
        return;
    }
    // While the song plays an automated setting shows where the pattern has it.
    float shown[kEffectParams];
    for (int i = 0; i < kEffectParams; ++i) shown[i] = track >= 0 ? session_.automated(track, autoLaneOfEffect(slot, i), d.param[i]) : d.param[i];
    // What it does, drawn from those settings.
    // Playing, the picture moves with the song: where it is (the part of the step gone counted from when the step
    // came, so a line glides between steps) and how loud this track comes out (held, falling a fifth a pass).
    FxLive live;
    if (project_.playing && session_.engine.playStep() >= 0) {
        const Uint32 now = SDL_GetTicks();
        const int at = session_.engine.playBar() * kSteps + session_.engine.playStep();
        if (at != liveStep_) liveStep_ = at, liveAt_ = now;
        live.playing = true;
        live.stepSeconds = 15.f / (project_.tempo > 1.f ? project_.tempo : 120.f);
        const float part = static_cast<float>(now - liveAt_) / 1000.f / live.stepSeconds;
        live.steps = at + (part > 0.99f ? 0.99f : part);
        const float peak = track >= 0 ? session_.engine.trackPeak(track) : 0.f;
        liveLevel_ = peak > liveLevel_ * 0.8f ? peak : liveLevel_ * 0.8f;
        live.level = liveLevel_ > 1.f ? 1.f : liveLevel_;
    } else {
        liveStep_ = -1, liveLevel_ = 0.f;
    }
    drawEffectPicture(r, kPaneX, kPicY, kPaneW, kPicH, kind, shown, effectMeterParam(kind) >= 0 ? meterOf(row_) : 0.f, live);
    for (int p = 0; p < kEffectParams; ++p) {
        const int x = dialX(p), cx = dialCX(p);
        if (p >= info.params) {
            box(r, x, kDialY, kDialW, kDialH, kBg, kPanel2); // no setting there: an empty place, so the dials stand where they always do
            continue;
        }
        const int choices = effectChoices(kind, p);
        const bool cursor = column_ == p + 1;
        const bool automated = track >= 0 && session_.hasAutomation(track, project_.scene, autoLaneOfEffect(slot, p));
        box(r, x, kDialY, kDialW, kDialH, kPanel, kLine);
        // On a bus the MIX is how loud the bus comes back: it reads LEVEL. Purple: it moves with the pattern.
        const char* named = bus >= 0 && p == effectMixParam(kind) ? "LEVEL" : info.paramNames[p];
        fonts.small.drawCentered(r, static_cast<float>(cx), static_cast<float>(kDialNameY), named, automated ? kPurple : (cursor ? kText : kDim));
        // A MOD on it runs: the brass tick where the setting is given this moment (batch 6). Not on a bus's or the
        // master's (no MOD there). (A choice takes no MOD, and an effect is tried only with the list of effects
        // open, where no setting is drawn: modMarkAt finds nothing for either.)
        const float mark = track >= 0 ? modMarkAt(session_, track, autoLaneOfEffect(slot, p), shown[p]) : std::nanf("");
        drawDial(r, cx, kDialCY, kDialR, kDialRing, shown[p], choices, whichChoice(shown[p], choices), cursor ? accent : kText, mark);
        effectParamText(kind, p, shown[p], text, sizeof text, shown);
        fonts.fit(text, static_cast<float>(kValueRoom)).drawCentered(r, static_cast<float>(cx), static_cast<float>(kDialValueY), text, cursor ? accent : kText);
        // Its icon (every setting has one: icons.hpp), under the value.
        if (const Icon* icon = iconFor(named)) drawIcon(r, *icon, cx - kIconW / 2, kDialIconY, cursor || automated || std::fabs(shown[p] - effectDefault(kind, p, bus >= 0)) > 0.005f);
        if (p == effectMeterParam(kind)) {
            // What the effect is doing to the sound just now (COMP: how far it is turning it down): a strip growing
            // from the right, as a meter of this kind does; the picture has it as cells.
            const int px = meterPixels(meterOf(row_), kMeterW);
            if (px > 0) fill(r, x + kMeterX + kMeterW - px, kMeterY, px, kMeterH, kBrass);
        }
        if (automated) fill(r, x + 6, kAutoY, kDialW - 12, 2, kPurple);
        if (cursor) outline(r, x - 1, kDialY - 1, kDialW + 2, kDialH + 2, accent, 2);
    }
}

// The heading's TYPE row: as many as fit about the one it is, "..." for the rest (six DELAYs at LARGE do not all
// fit beside the key), and with A held the key that gives another. The room for the key is kept, held or not.
void FxScreen::drawTypeRow(SDL_Renderer* r, const Fonts& chosen, const std::vector<std::string>& names, int at, bool list, const char* key) {
    const int types = static_cast<int>(names.size());
    const float ty = static_cast<float>(kTypeRowY);
    int from = 0, to = types;
    const Fonts& fonts = typeRowView(chosen, names, at, &from, &to); // (held at LARGE, with its row, where LARGE's do not fit)
    fonts.small.draw(r, kPaneX + 12.f, ty, "TYPE", kDim);
    float tx = typeRowX(fonts);
    if (from > 0) {
        fonts.small.draw(r, tx, ty, kTypeMore, kDim);
        tx += fonts.small.width(kTypeMore) + kTypeGapX;
    }
    for (int t = from; t < to; ++t) {
        const char* type = names[t].c_str();
        const float w = fonts.small.width(type);
        if (t == at) {
            fill(r, static_cast<int>(tx) - 6, static_cast<int>(ty) - 5, static_cast<int>(w) + 12, static_cast<int>(fonts.small.capHeight()) + 11, list ? kBrass : kText);
            fonts.small.draw(r, tx, ty, type, kBg);
        } else {
            fonts.small.draw(r, tx, ty, type, kDim);
        }
        tx += w + kTypeGapX;
    }
    if (to < types) fonts.small.draw(r, tx, ty, kTypeMore, kDim);
    if (list) key = "LEFT / RIGHT";
    if (key && types > 1) drawKey(r, fonts, kPaneX + kPaneW - 12.f - keyWidth(fonts, key), ty, key, kText); // (its room is LEFT / RIGHT's, kept)
}

// A family's types on its line of the list of effects, ", " between: one row, or two at LARGE (pickRows).
void FxScreen::drawPickTypes(SDL_Renderer* r, const Fonts& fonts, const std::vector<std::string>& names, int lit, int y) {
    const PickRows rows = pickRows(fonts, names);
    const Font& font = rows.set->small;
    const int count = static_cast<int>(names.size());
    const float rowsH = rows.rows * font.capHeight() + (rows.rows - 1) * kPickRowGap;
    float tx = static_cast<float>(kPickTypesX), ty = y + (kPickLineH - rowsH) / 2.f - 1.f;
    for (int t = 0; t < count; ++t) {
        if (t == rows.split && t > 0) tx = static_cast<float>(kPickTypesX), ty += font.capHeight() + kPickRowGap;
        font.draw(r, tx, ty, names[t].c_str(), t == lit ? kBrass : kDim);
        tx += font.width(names[t].c_str());
        if (t + 1 < count && t + 1 != rows.split) {
            font.draw(r, tx, ty, kPickComma, kDim);
            tx += font.width(kPickComma);
        }
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
    if (modKnob_.held()) {
        modKnob_.drawHints(r, fonts, kFxModPlaces); // each button under its field in the panel
        return;
    }
    if (holding_) {
        if (column_ > 0) {
            hint("UP / DOWN", "turn it");
            if (const char* key = typeKey()) hint(key, "type");
        } else if (!onSends) {
            hint("UP / DOWN", "effect");
            if (effectTypes(kind) > 1) hint("LEFT / RIGHT", "type");
            hint("B", "as it was");
        } else {
            hint("LET GO", "then RIGHT");
        }
    } else if (const int along = bandGrip(); along >= 0) {
        hint("LEFT / RIGHT", "freq"); // (Y held: the bar has room for these; "HOLD Y" before it has none, so the manual says it)
        hint("UP / DOWN", along == kBandQ ? "q" : "gain");
    } else if (yHeld_ && column_ == 0 && row_ < kTrackSlots) {
        hint("UP / DOWN", "move it");
    } else if (column_ == 0) {
        if (settings(row_) > 0) hint("RIGHT", onSends ? "sends" : "settings");
        if (!onSends) hint("HOLD A", "effects");
        if (onSends) hint("B", "send nothing");
        else if (kind != EffectKind::None) hint("HOLD B", "delete");
        // A filled track line has all five it can hold: START yields to the move (it is on
        // every other line, and the top bar shows playing).
        const bool move = !onSends && row_ < kTrackSlots && kind != EffectKind::None;
        if (move) hint("HOLD Y", "move");
        hint("L1 / R1", "track");
        if (!move) hint("START", project_.playing ? "stop" : "play");
    } else {
        // The walk (owner, 2026-11-25): the ways that do something, as L1 / R1 "track" says the one before / after;
        // on the first dial LEFT is the list too.
        hint(walkKeyFor(column_, settings(row_)), column_ > 1 ? "dial" : settings(row_) > 1 ? "list, dial" : "list");
        hint("HOLD A", !onSends && effectTypes(kind) > 1 ? "turn, type" : "turn"); // (with A held the bar says which way)
        hint("B", onSends ? "none" : "default");
        // (No HOLD X: MOD, on a send either since the walk took its room, 2026-11-25: 43 pixels over on ECHO. The help
        // card says it.)
        hint("L1 / R1", "track");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
