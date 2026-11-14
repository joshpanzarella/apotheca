// The one table of effects. Each effect is a file in fx/ and brings its own
// row (unit.hpp says how to add one); here a row gets its number and its
// place in the list a player picks from.

#include "apotheca/effects.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "fx/unit.hpp"

namespace apotheca {

namespace {

const EffectInfo kNoneInfo = {"NONE", 0, {"", "", "", ""}, {0.f, 0.f, 0.f, 0.f}, "", EffectFamily::None, {0, 0, 0, 0}, nullptr, nullptr};

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
    &fx::kDuckInfo,   // 11
    &fx::kPingInfo,   // 12
    &fx::kSpringInfo, // 13
    &fx::kPlateInfo,  // 14
    &fx::kCombInfo,   // 15
    &fx::kVowelInfo,  // 16
    &fx::kEqInfo,     // 17
    &fx::kFoldInfo,   // 18
    &fx::kFuzzInfo,   // 19
    &fx::kRingInfo,   // 20
    &fx::kWearInfo,   // 21
    &fx::kLimiterInfo, // 22
    &fx::kSnapInfo,   // 23
};

// The order a player meets them in: the families in EffectFamily's order, and
// the types of each side by side (the first is the one a family goes in as).
// This order can change freely; nothing stores it. (effects_test fails a list
// whose families are out of that order or in two pieces.)
const EffectKind kList[] = {
    EffectKind::None,
    EffectKind::Filter,   // FILTER: FILTER
    EffectKind::Comb,     //         COMB
    EffectKind::Vowel,    //         VOWEL
    EffectKind::Eq,       //         EQ
    EffectKind::Drive,    // DRIVE:  DRIVE
    EffectKind::Saturate, //         SATURATE
    EffectKind::Fold,     //         FOLD
    EffectKind::Fuzz,     //         FUZZ
    EffectKind::Crush,    // CRUSH:  CRUSH
    EffectKind::Ring,     //         RING
    EffectKind::Wear,     //         WEAR
    EffectKind::Chorus,   // MOD:    CHORUS
    EffectKind::Delay,    // DELAY:  CLEAN
    EffectKind::Tape,     //         TAPE
    EffectKind::Ping,     //         PING
    EffectKind::Reverb,   // REVERB: ROOM
    EffectKind::Hall,     //         HALL
    EffectKind::Spring,   //         SPRING
    EffectKind::Plate,    //         PLATE
    EffectKind::Comp,     // LEVEL:  COMP
    EffectKind::Duck,     //         DUCK
    EffectKind::Limiter,  //         LIMITER
    EffectKind::Snap,     //         SNAP
};
constexpr int kListSize = static_cast<int>(sizeof kList / sizeof kList[0]);

} // namespace

const EffectInfo& effectInfo(EffectKind kind) {
    const int index = static_cast<int>(kind);
    return *kTable[index < 0 || index >= kEffectKinds ? 0 : index];
}

const char* effectFamilyName(EffectFamily family) {
    static const char* const names[static_cast<int>(EffectFamily::Count)] = {"", "FILTER", "DRIVE", "CRUSH", "MOD", "DELAY", "REVERB", "LEVEL"};
    const int index = static_cast<int>(family);
    return names[index < 0 || index >= static_cast<int>(EffectFamily::Count) ? 0 : index];
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

// ---- families and types ----

namespace {

// Two effects are one family when their rows name the same one.
bool sameFamily(EffectKind a, EffectKind b) {
    if (a == b) return true;
    const EffectFamily family = effectInfo(a).family;
    return family != EffectFamily::None && family == effectInfo(b).family;
}

// Where in kList a family starts: the place of its first type. -1: not in the list.
int familyStart(EffectKind kind) {
    for (int i = 0; i < kListSize; ++i)
        if (sameFamily(kList[i], kind)) return i;
    return -1;
}

bool listed(EffectKind kind, int bus) { return bus < 0 || bus >= 2 || kind == EffectKind::None || effectBus(kind) == bus; }

std::string readAs(EffectKind kind, int index, float value) {
    char text[48] = "";
    effectParamText(kind, index, value, text, sizeof text);
    return text;
}
float numberIn(const std::string& text) { return std::strtof(text.c_str(), nullptr); }

} // namespace

const char* effectFamily(EffectKind kind) {
    const EffectInfo& info = effectInfo(kind);
    return info.family == EffectFamily::None ? info.name : effectFamilyName(info.family);
}

const char* effectTypeName(EffectKind kind) {
    const EffectInfo& info = effectInfo(kind);
    return info.family == EffectFamily::None ? "" : (info.type ? info.type : info.name);
}

const char* effectTypeShown(EffectKind kind) {
    const char* type = effectTypeName(kind);
    return std::strcmp(type, effectFamily(kind)) == 0 ? "" : type;
}

bool effectFamilyNamesTypes(EffectKind kind) {
    return effectInfo(kind).family != EffectFamily::None && (effectTypes(kind) > 1 || effectTypeShown(kind)[0] != 0);
}

int effectTypes(EffectKind kind) {
    int n = 0;
    for (int i = 0; i < kListSize; ++i) n += sameFamily(kList[i], kind) ? 1 : 0;
    return n < 1 ? 1 : n;
}

EffectKind effectTypeAt(EffectKind kind, int index) {
    const int start = familyStart(kind);
    if (start < 0 || index < 0 || index >= effectTypes(kind)) return EffectKind::None;
    return kList[start + index];
}

int effectTypeIndex(EffectKind kind) {
    const int start = familyStart(kind), place = effectPlace(kind);
    return start < 0 || place < start ? 0 : place - start;
}

int familyListSize(int bus) {
    int n = 0;
    for (int i = 0; i < kListSize; ++i) n += familyStart(kList[i]) == i && listed(kList[i], bus) ? 1 : 0;
    return n;
}

EffectKind familyAt(int place, int bus) {
    if (place < 0) return EffectKind::None;
    for (int i = 0; i < kListSize; ++i)
        if (familyStart(kList[i]) == i && listed(kList[i], bus) && place-- == 0) return kList[i];
    return EffectKind::None;
}

int familyPlace(EffectKind kind, int bus) {
    const int start = familyStart(kind);
    if (start < 0 || !listed(kind, bus)) return 0;
    int place = 0;
    for (int i = 0; i < start; ++i) place += familyStart(kList[i]) == i && listed(kList[i], bus) ? 1 : 0;
    return place;
}

void effectFamilySays(EffectKind kind, char* out, int size) {
    if (!out || size <= 0) return;
    const EffectKind first = effectTypeAt(kind, 0);
    std::snprintf(out, static_cast<std::size_t>(size), "%s", kind == EffectKind::None || first == EffectKind::None ? "nothing here" : effectInfo(first).says);
}

int effectCarryFrom(EffectKind from, EffectKind to, int toIndex) {
    const EffectInfo& a = effectInfo(from);
    const EffectInfo& b = effectInfo(to);
    if (toIndex < 0 || toIndex >= b.params) return -1;
    for (int i = 0; i < a.params; ++i)
        if (std::strcmp(a.paramNames[i], b.paramNames[toIndex]) == 0 && (a.choices[i] > 0) == (b.choices[toIndex] > 0)) return i;
    return -1;
}

float effectCarryValue(EffectKind from, int fromIndex, EffectKind to, int toIndex, float value) {
    const EffectInfo& a = effectInfo(from);
    const EffectInfo& b = effectInfo(to);
    if (toIndex < 0 || toIndex >= b.params) return 0.f;
    if (fromIndex < 0 || fromIndex >= a.params || effectCarryFrom(from, to, toIndex) != fromIndex) return b.defaults[toIndex];
    value = value != value ? 0.f : (value < 0.f ? 0.f : (value > 1.f ? 1.f : value));
    const std::string reads = readAs(from, fromIndex, value);
    if (const int count = b.choices[toIndex]) {
        // A choice: the one that reads the same; or the nearest by what it amounts to; or the same way along.
        const auto middle = [&](int which) { return (static_cast<float>(which) + 0.5f) / static_cast<float>(count); };
        for (int c = 0; c < count; ++c)
            if (readAs(to, toIndex, middle(c)) == reads) return middle(c);
        const float want = a.amount ? a.amount(fromIndex, value) : std::nanf("");
        int best = -1;
        float nearest = 0.f;
        for (int c = 0; c < count && want == want && b.amount; ++c) {
            const float gives = b.amount(toIndex, middle(c));
            if (gives != gives) continue;
            const float apart = std::fabs(gives - want);
            if (best < 0 || apart < nearest) best = c, nearest = apart;
        }
        if (best < 0) {
            const int had = a.choices[fromIndex];
            int which = static_cast<int>(value * static_cast<float>(had));
            which = which >= had ? had - 1 : which;
            best = had > 1 ? static_cast<int>(std::lround(static_cast<float>(which) * static_cast<float>(count - 1) / static_cast<float>(had - 1))) : 0;
        }
        return middle(best);
    }
    // An amount. As it is, if that reads the same (two scales that are one: nothing moves at all).
    if (readAs(to, toIndex, value) == reads) return value;
    // Else by the number in the reading, halving the range (a setting reads more as it is turned up)...
    const float want = numberIn(reads);
    float low = 0.f, high = 1.f;
    const bool rising = numberIn(readAs(to, toIndex, 1.f)) >= numberIn(readAs(to, toIndex, 0.f));
    for (int i = 0; i < 24; ++i) {
        const float mid = 0.5f * (low + high);
        if ((numberIn(readAs(to, toIndex, mid)) < want) == rising) low = mid;
        else high = mid;
    }
    const float found = 0.5f * (low + high);
    // ...then the nearest value to that which reads the very same; if none does, that is the nearest there is.
    float at = -1.f;
    for (int k = 0; k <= 80 && at < 0.f; ++k)
        for (int sign = -1; sign <= 1 && at < 0.f; sign += 2) {
            float v = found + static_cast<float>(sign * k) / 4000.f;
            v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            if (readAs(to, toIndex, v) == reads) at = v;
        }
    if (at < 0.f) return found;
    // That is an EDGE of the stretch that reads so, and a push of the D-pad from an edge may move nothing that
    // can be seen. So: the roundest value in the stretch, looked for from its middle (a twentieth is where the
    // D-pad leaves a setting, so one that came from a twentieth goes back to it, to the bit).
    const auto edge = [&](float out) {
        float in = at;
        for (int i = 0; i < 24; ++i) {
            const float mid = 0.5f * (in + out);
            if (readAs(to, toIndex, mid) == reads) in = mid;
            else out = mid;
        }
        return in;
    };
    const float middle = 0.5f * (edge(0.f) + edge(1.f));
    for (int grid : {100, 1000}) { // (a twentieth is a hundredth too)
        const float v = static_cast<float>(std::lround(middle * static_cast<float>(grid))) / static_cast<float>(grid);
        if (readAs(to, toIndex, v) == reads) return v;
    }
    return middle;
}

void effectCarry(EffectKind from, EffectKind to, const float* was, bool onBus, float* now) {
    if (!now) return;
    for (int i = 0; i < kEffectParams; ++i) {
        const int had = was ? effectCarryFrom(from, to, i) : -1;
        now[i] = had >= 0 ? effectCarryValue(from, had, to, i, was[had]) : effectDefault(to, i, onBus);
    }
}

int effectBus(EffectKind kind) {
    const EffectFamily family = effectInfo(kind).family;
    return family == EffectFamily::Delay ? 0 : (family == EffectFamily::Reverb ? 1 : -1);
}

int effectMeterParam(EffectKind kind) { return kind == EffectKind::Comp || kind == EffectKind::Duck || kind == EffectKind::Limiter ? 0 : -1; }

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
