#pragma once
// The sound designer: a web page (design_page.cpp) for building sounds on a
// computer's big screen, with the HANDHELD making the sound. The page shows
// every setting of the synth on the track being worked on, plays it from the
// computer's keyboard, and saves the result as a preset.
//
// The page talks to the WiFi server's thread; the sound belongs to the
// program's own thread. This sits between them:
//   - what the page asks for waits in a list, taken up once a frame
//   - what the page is shown is text made once a frame and handed over whole
// so neither thread ever touches the other's things.
//
// What the page may ask (one line of text each):
//   set ID VALUE      a setting of the patch
//   macro N VALUE     a macro, 0..1
//   preset N          choose a preset (built-in or the player's own)
//   note KEY 1 | 0    a key down or up
//   save NAME         keep the sound as a preset called NAME

#include <mutex>
#include <string>
#include <vector>

#include "transfer.hpp"

namespace apotheca {

struct DesignRequest {
    enum Kind { Set, Macro, Preset, Note, Save } kind = Set;
    int a = 0, b = 0;
    float value = 0.f;
    std::string name;
};

// A preset's name as the player typed it, made safe to be a file's name and
// to show on the handheld: capitals, digits, spaces, - and _, 24 at most.
// Empty if nothing usable is left.
std::string presetNameFrom(const std::string& typed);

class Designer : public DesignDesk {
public:
    // ---- the server's side (any thread) ----
    std::string spec() override;
    std::string state() override;
    bool ask(const std::string& line) override;

    // ---- the program's side (its own thread, once a frame) ----
    std::vector<DesignRequest> take();
    void show(const std::string& spec, const std::string& state);
    // How many times the page has asked how things are: it asks about once a
    // second while it is open, so when this stops going up the page has gone.
    unsigned looks() const;

private:
    mutable std::mutex mutex_;
    std::vector<DesignRequest> waiting_;
    std::string spec_ = "{}", state_ = "{}";
    unsigned looks_ = 0;
};

} // namespace apotheca
