#pragma once
// Every still the program knows (--view NAME, then any flags), for the tests that draw them all:
// textsize_test (at every TEXT SIZE) and theme_test (in every THEME).

#include <cstring>
#include <string>
#include <vector>

namespace apotheca {
namespace stills {

// ---- every still the program knows ---------------------------------------------------------
// As they are asked for: the view's name, then any flags. (An empty name is the drum grid, which
// has no name of its own.) testEveryViewIsListed reads main.cpp and fails if it knows one that
// is not here.
inline const char* const kViews[] = {
    "song", "deleting", "effects", "effects-held",
    "chordfx", "chordfx-held", "chordfx-ratchet", "chordfx-strum", "chordfx-order", "chordfx-time", "chordfx-level", "chordfx-lengths",
    "chordfx-off", "chordfx-arp", "chordfx-worst", "chordfx-line",
    "notefx", "notefx-held", "notefx-chord", "notefx-chord-held", "notefx-worst", "notefx-space", "notefx-space-held",
    "notefx-glides", "notefx-glides-alone", "notefx-glides-longest-held", "notefx-glides-far1", "notefx-glides-far2", "notefx-glides-far3", "notefx-glides-far4",
    "notefx-glides-far2-short", "notefx-glides-far2-short-drums",
    "notefx-from", "notefx-from-paired", "notefx-from-nearest", "notefx-from-random",
    "notefx-from-held", "notefx-from-paired-held", "notefx-from-nearest-held", "notefx-from-random-held",
    "under", "songs", "sampler", "bars", "bars-roll",
    "fx", "fx --hold-a", "fx-type", "fx-type --hold-a", "fx-pick", "fx-pick --hold-a", "fx-comp", "fx-comp --hold-a", "fx-switch", "fx-switch --hold-a", "fx-master", "fx-master --hold-a",
    "fx-duck", "fx-duck --hold-a", "fx-choice", "fx-choice --hold-a", "fx-auto", "fx-auto --hold-a", "fx-sends", "fx-sends --hold-a",
    "fx-bus", "fx-bus --hold-a", "fx-level", "fx-level --hold-a", "fx-ping", "fx-ping --hold-a", "fx-ping-bus", "fx-ping-bus --hold-a", "fx-spring", "fx-spring --hold-a", "fx-plate", "fx-plate --hold-a",
    "fx-kind-comb", "fx-kind-comb --hold-a", "fx-kind-comb-1", "fx-kind-vowel", "fx-kind-vowel --hold-a", "fx-kind-vowel-1", "fx-kind-eq", "fx-kind-eq --hold-a", "fx-kind-eq-3",
    "fx-kind-fold", "fx-kind-fold --hold-a", "fx-kind-fold-2", "fx-kind-fuzz", "fx-kind-fuzz --hold-a", "fx-kind-fuzz-3", "fx-kind-ring", "fx-kind-ring --hold-a", "fx-kind-ring-2",
    "fx-kind-wear", "fx-kind-wear --hold-a", "fx-kind-wear-2", "fx-kind-limiter", "fx-kind-limiter --hold-a", "fx-kind-limiter-1", "fx-kind-snap", "fx-kind-snap --hold-a", "fx-kind-snap-1",
    "fx-kind-flanger", "fx-kind-flanger --hold-a", "fx-kind-flanger-3", "fx-kind-ensemble", "fx-kind-ensemble --hold-a", "fx-kind-ensemble-3",
    "fx-kind-phaser", "fx-kind-phaser --hold-a", "fx-kind-phaser-3", "fx-kind-tremolo", "fx-kind-tremolo --hold-a", "fx-kind-tremolo-1", "fx-kind-tremolo-3",
    "fx-kind-dub", "fx-kind-dub --hold-a", "fx-kind-dub-2", "fx-kind-reverse", "fx-kind-reverse --hold-a", "fx-kind-reverse-1",
    "fx-kind-stutter", "fx-kind-stutter --hold-a", "fx-kind-stutter-2",
    "fx-kind-bloom", "fx-kind-bloom --hold-a", "fx-kind-bloom-1", "fx-kind-shimmer", "fx-kind-shimmer --hold-a", "fx-kind-shimmer-1",
    "fx-family-1 --hold-a", "fx-family-2 --hold-a", "fx-family-3 --hold-a", "fx-family-4 --hold-a", "fx-family-5 --hold-a", "fx-family-6 --hold-a", "fx-family-7 --hold-a",
    "fx-deleting", "fx-trying", "fx-mod-held", "fx-mod-stepper", "fx-mod-new", "fx-mod-full", "fx-sends-mod-held", "fx-modded",
    "mod", "mod-lfo", "mod-steps", "mod-depth", "mod-mixer", "mod-target", "mod-target --hold-a", "mod-off", "mod-drums", "mod-drums --hold-a", "mod-deleting", "mod-12", "mod-free",
    "device-mod-held", "device-mod-stepper", "device-mod-new", "device-mod-full", "device-mod-deleting", "device-modded", "device-mod-free", "change-instrument", "song-deleting", "song-head", "song-marks", "song-marks-12", "song-mixer", "song-mixer-master", "song-mixer-12", "song-mixer-master-12", "song-end-loop", "song-end-stop",
    "bounce", "bounce-tail", "bounce-ready", "panic", "wifi", "revs", "add-track", "chords", "chords-swap", "notes",
    "phazed", "page0", "page1", "page2", "page3", "page4", "page5", "page6", "page7", "page8", "page9", "jaxep", "jaxep0", "jaxep1", "jaxep2", "jaxep3", "jaxep4", "jaxep5", "jaxep-bank", "jaxep-held",
    "kit", "slices", "slices-32", "slices-name", "slices-name-feel", "slices-throw", "starts", "picker", "picker-in", "picker-deep", "picker-deleting", "picker-new", "picker-typing", "picker-carrying", "picker-folder-deleting", "sampler-picker",
    "feel", "feel-held", "feel-off-held", "feel-same-held", "feel-said",
    "setup", "buttons", "typing", "help",
    "fm6", "fm6-1", "fm6-2", "fm6-3", "fm6-4", "fm6-5", "fm6-6", "fm6-7", "fm6-8", "fm6-long", "fm6-fixed", "fm6-off",
    "physic", "physic-1", "physic-2", "physic-3", "physic-4", "physic-5", "physic-long", "physic-string", "physic-noise",
    "next-scene", "next-scene-notes", "next-scene-full", "volume", "update-ready", "update-ready-drums", "update-accepted", "nomenu", "help-nomenu", "kit-old",
    "", " --dice", " --paint", " --paint-random", " --on-name", " --on-tempo", " --hold-a", " --hold-y",
    "help --no-menu", "song --no-menu", "setup --no-menu",
};
inline const int kViewCount = static_cast<int>(sizeof kViews / sizeof kViews[0]);

// The names main.cpp compares `view` with: strcmp(view, "x"), strncmp(view, "x", n), strstr(view, "x"),
// strcmp(view + n, "-x").
inline std::vector<std::string> viewsInMain(const std::string& source) {
    std::vector<std::string> found;
    for (std::size_t at = 0; (at = source.find("(view", at)) != std::string::npos; ++at) {
        const std::size_t open = source.find('"', at), close = open == std::string::npos ? open : source.find('"', open + 1);
        if (close == std::string::npos || open - at > 16) continue; // ("(view + 10, " is the longest there is before the name)
        found.push_back(source.substr(open + 1, close - open - 1));
    }
    return found;
}

// The names in a source that no still in kViews has in it.
inline std::vector<std::string> unlisted(const std::string& source) {
    std::vector<std::string> missing;
    for (const std::string& name : viewsInMain(source)) {
        bool listed = false;
        for (const char* view : kViews) listed = listed || std::strstr(view, name.c_str()) != nullptr;
        if (!listed) missing.push_back(name);
    }
    return missing;
}

} // namespace stills
} // namespace apotheca
