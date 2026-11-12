// The one table of effects. Each effect is a file in fx/ and brings its own
// row (unit.hpp says how to add one); here a row gets its number and its
// place in the list a player picks from.

#include "apotheca/effects.hpp"

#include <cstring>

#include "fx/unit.hpp"

namespace apotheca {

namespace {

const EffectInfo kNoneInfo = {"NONE", 0, {"", "", "", ""}, {0.f, 0.f, 0.f, 0.f}, "", EffectGroup::None, {0, 0, 0, 0}, nullptr, nullptr};

// By number. A row's place here IS its EffectKind: append, never reorder.
const EffectInfo* const kTable[kEffectKinds] = {
    &kNoneInfo,       // 0
    &fx::kFilterInfo, // 1
    &fx::kDriveInfo,  // 2
    &fx::kDelayInfo,  // 3
    &fx::kReverbInfo, // 4
    &fx::kCrushInfo,  // 5
    &fx::kTapeInfo,   // 6
    &fx::kHallInfo,   // 7
    &fx::kChorusInfo, // 8
    &fx::kCompInfo,   // 9
    &fx::kSaturateInfo, // 10
};

// The list a player picks from, in groups. This order can change freely;
// nothing stores it.
const EffectKind kList[] = {
    EffectKind::None,
    EffectKind::Filter,
    EffectKind::Saturate,
    EffectKind::Drive,
    EffectKind::Tape,
    EffectKind::Delay,
    EffectKind::Hall,
    EffectKind::Reverb,
    EffectKind::Chorus,
    EffectKind::Comp,
    EffectKind::Crush,
};
constexpr int kListSize = static_cast<int>(sizeof kList / sizeof kList[0]);

} // namespace

const EffectInfo& effectInfo(EffectKind kind) {
    const int index = static_cast<int>(kind);
    return *kTable[index < 0 || index >= kEffectKinds ? 0 : index];
}

const char* effectGroupName(EffectGroup group) {
    static const char* const names[static_cast<int>(EffectGroup::Count)] = {"", "FILTER", "DRIVE", "DELAY", "REVERB", "MOVING", "LEVEL", "DIRT"};
    const int index = static_cast<int>(group);
    return names[index < 0 || index >= static_cast<int>(EffectGroup::Count) ? 0 : index];
}

int effectListSize() { return kListSize; }

EffectKind effectAt(int place) { return place < 0 || place >= kListSize ? EffectKind::None : kList[place]; }

int effectPlace(EffectKind kind) {
    for (int i = 0; i < kListSize; ++i)
        if (kList[i] == kind) return i;
    return 0;
}

EffectKind effectFromWord(const char* word) {
    if (!word || !word[0]) return EffectKind::None;
    for (int k = 1; k < kEffectKinds; ++k)
        if (std::strcmp(kTable[k]->word, word) == 0) return static_cast<EffectKind>(k);
    return EffectKind::None;
}

int groupJump(const EffectGroup* groups, int count, int place, int by) {
    if (!groups || place < 0 || place >= count || by == 0) return place;
    if (by > 0) {
        for (int to = place + 1; to < count; ++to)
            if (groups[to] != groups[place]) return to;
        return place; // the last group: nowhere to go
    }
    int to = place;
    while (to >= 0 && groups[to] == groups[place]) --to; // the last of the group before
    if (to < 0) return place;                            // the first group
    while (to > 0 && groups[to - 1] == groups[to]) --to; // ...and its first
    return to;
}

int effectGroupJump(int place, int by) {
    EffectGroup groups[kListSize];
    for (int i = 0; i < kListSize; ++i) groups[i] = effectInfo(kList[i]).group;
    return groupJump(groups, kListSize, place, by);
}

int effectBus(EffectKind kind) {
    const EffectGroup group = effectInfo(kind).group;
    return group == EffectGroup::Delay ? 0 : (group == EffectGroup::Reverb ? 1 : -1);
}

int effectMixParam(EffectKind kind) {
    const EffectInfo& info = effectInfo(kind);
    for (int i = 0; i < info.params; ++i)
        if (std::strcmp(info.paramNames[i], "MIX") == 0) return i;
    return -1;
}

float effectDefault(EffectKind kind, int index, bool onBus) {
    const EffectInfo& info = effectInfo(kind);
    if (index < 0 || index >= info.params) return 0.f;
    return onBus && index == effectMixParam(kind) ? 1.f : info.defaults[index];
}

int busListSize(int bus) {
    if (bus < 0 || bus >= 2) return 1; // no such bus: NONE alone
    int n = 1;
    for (int i = 1; i < kListSize; ++i)
        if (effectBus(kList[i]) == bus) ++n;
    return n;
}

EffectKind busEffectAt(int bus, int place) {
    if (place <= 0 || bus < 0 || bus >= 2) return EffectKind::None;
    for (int i = 1; i < kListSize; ++i)
        if (effectBus(kList[i]) == bus && --place == 0) return kList[i];
    return EffectKind::None;
}

int busEffectPlace(int bus, EffectKind kind) {
    int place = 0;
    if (bus < 0 || bus >= 2) return 0;
    for (int i = 1; i < kListSize; ++i) {
        if (effectBus(kList[i]) != bus) continue;
        ++place;
        if (kList[i] == kind) return place;
    }
    return 0;
}

int effectChoices(EffectKind kind, int index) {
    const EffectInfo& info = effectInfo(kind);
    return index < 0 || index >= info.params ? 0 : info.choices[index];
}

void effectParamText(EffectKind kind, int index, float value, char* out, int size, const float* all) {
    if (!out || size <= 0) return;
    const EffectInfo& info = effectInfo(kind);
    if (info.reading) info.reading(index, value, all, out, size);
    else fx::readPercent(value, out, size);
}

std::unique_ptr<Effect> makeEffect(EffectKind kind, float sampleRate, float tempo) {
    const int index = static_cast<int>(kind);
    if (index <= 0 || index >= kEffectKinds || !kTable[index]->create) return nullptr;
    std::unique_ptr<Effect> effect(kTable[index]->create());
    effect->prepare(sampleRate);
    effect->setTempo(tempo);
    return effect;
}

} // namespace apotheca
