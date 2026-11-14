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
    "fx", "fx --hold-a", "fx-type", "fx-type --hold-a", "fx-pick", "fx-pick --hold-a", "fx-comp", "fx-comp --hold-a", "fx-master", "fx-master --hold-a",
    "fx-duck", "fx-duck --hold-a", "fx-choice", "fx-choice --hold-a", "fx-auto", "fx-auto --hold-a", "fx-sends", "fx-sends --hold-a",
    "fx-bus", "fx-bus --hold-a", "fx-level", "fx-level --hold-a", "fx-ping", "fx-ping --hold-a", "fx-ping-bus", "fx-ping-bus --hold-a", "fx-spring", "fx-spring --hold-a", "fx-plate", "fx-plate --hold-a",
    "fx-family-1 --hold-a", "fx-family-2 --hold-a", "fx-family-3 --hold-a", "fx-family-4 --hold-a", "fx-family-5 --hold-a", "fx-family-6 --hold-a", "fx-family-7 --hold-a",
    "fx-deleting", "change-instrument", "song-deleting",
    "bounce", "bounce-tail", "panic", "wifi", "revs", "add-track", "chords", "chords-swap", "notes",
    "phazed", "page0", "page1", "page2", "page3", "page4", "page5", "page6", "page7", "page8", "page9", "twin", "twin0", "twin1", "twin2", "twin3", "twin4",
    "kit", "slices", "slices-32", "slices-name", "slices-throw", "starts", "picker",
    "setup", "buttons", "typing", "qmod", "qmod0", "qmod1", "qmod2", "qmod3", "help",
    "next-scene", "next-scene-notes", "next-scene-full", "volume", "nomenu", "help-nomenu", "kit-old",
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
