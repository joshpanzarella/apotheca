#include "redraw.hpp"

#include <cstring>

#include "app.hpp"

namespace apotheca {

const char* stirName(unsigned bit) {
    switch (bit) {
    case kStirEvent: return "event";
    case kStirHeld: return "held";
    case kStirStick: return "stick";
    case kStirPlaying: return "playing";
    case kStirPlace: return "place";
    case kStirScope: return "scope";
    case kStirMeters: return "meters";
    case kStirMidi: return "midi";
    case kStirVolume: return "volume";
    case kStirSaid: return "said";
    case kStirPanicNote: return "panic note";
    case kStirUpdateSaid: return "update said";
    case kStirFitting: return "fitting";
    case kStirRecording: return "recording";
    case kStirBouncing: return "bouncing";
    case kStirWifi: return "wifi";
    case kStirScreenSaid: return "screen said";
    case kStirPlayhead: return "playhead";
    case kStirPicker: return "picker";
    case kStirButtonTest: return "button test";
    case kStirAfter: return "after";
    case kStirMargin: return "margin";
    case kStirFirst: return "first";
    case kStirLoad: return "load";
    }
    return "?";
}

namespace {
bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0); // to the bit
}
} // namespace

bool Redraw::wanted(const App& app, const Input& input, bool stirred, Uint32 nowMs) {
    unsigned stirs = app.stirring();
    if (stirred) stirs |= kStirEvent;
    if (input.anyHeld()) stirs |= kStirHeld;
    if (input.stickX() != 0.f || input.stickY() != 0.f) stirs |= kStirStick;
    app.readouts(now_);
    if (std::memcmp(now_.place, shown_.place, sizeof now_.place) != 0) stirs |= kStirPlace;
    if (!same(now_.scope, shown_.scope)) stirs |= kStirScope;
    if (!same(now_.meters, shown_.meters)) stirs |= kStirMeters;
    if (std::memcmp(now_.midi, shown_.midi, sizeof now_.midi) != 0) stirs |= kStirMidi;
    if (std::memcmp(now_.load, shown_.load, sizeof now_.load) != 0) stirs |= kStirLoad;
    stirs &= ~ignored_;

    unsigned why = stirs;
    if (before_) why |= kStirAfter;
    if (keepMs_ > 0 && stirredAt_ != 0 && nowMs - stirredAt_ < keepMs_) why |= kStirMargin;
    if (first_) why |= kStirFirst;
    why &= ~ignored_;
    if (stirs) stirredAt_ = nowMs ? nowMs : 1;
    before_ = stirs;
    first_ = false;
    why_ = why;
    if (why) std::swap(shown_, now_); // what this picture shows (now_ is filled afresh next pass)
    return why != 0;
}

} // namespace apotheca
