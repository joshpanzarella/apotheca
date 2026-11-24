#pragma once
// The stills: main() hands over what it read from the command line, and the App is put in the state the view
// shows (stills.cpp). Every view: docs/dev/stills.md.
#include "app.hpp"
#include "controls.hpp"
#include "input.hpp"

namespace apotheca {

struct StillOptions {
    const char* view = "";        // --view NAME
    const char* samples = nullptr; // --samples FOLDER
    const char* songs = nullptr;   // --songs FOLDER
    const char* shot = nullptr;    // --shot FILE
    bool demo = false, smoke = false, holdA = false, holdY = false, onName = false, onTempo = false, dice = false, noMenu = false;
    int paint = 0; // 1: R1, 2: L1
};

// Puts the App in the state `options.view` shows (and the smoke run's keys); a view may shorten the still's time.
void setUpStill(App& app, const StillOptions& options, Input& input, ControlMap& controlMap, int& stillMs);

} // namespace apotheca
