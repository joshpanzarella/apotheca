#pragma once
// A USB keyboard (or anything that sends MIDI) plugged into the handheld.
//
// Linux shows each one as a file, /dev/snd/midiC<card>D<device>, that gives
// the bytes the keyboard sends. A thread of its own reads them, so a key is
// heard without waiting for the screen: it puts the note straight into the
// engine's "live" queue. The same events are kept for the screens (the piano
// roll's cursor follows the key; recording writes them into the pattern).
//
// Keyboards may be plugged in and pulled out at any time: the folder is
// looked at again a few times a second.
//
// Where the notes go is whichever track is being worked on:
//   a synth track   the keys play it
//   a drum track    C2 (note 36) upward plays the eight drums, lane 1 first,
//                   and round again every eight keys
//
// No sound card or SDL is needed, and the folder can be any folder, so the
// tests use made-up devices.

#include <atomic>
#include <string>
#include <thread>

#include "apotheca/engine.hpp"
#include "apotheca/spsc_queue.hpp"

namespace apotheca {

struct MidiEvent {
    enum Kind { NoteOn, NoteOff } kind = NoteOn;
    int note = 0;         // 0..127
    float velocity = 0.f; // 0..1 (a note on only)
};

// Bytes in, notes out. Everything else a keyboard sends (clock, pitch bend,
// knobs, "system exclusive" dumps) is read past without losing its place.
class MidiParser {
public:
    // True when this byte completed a note on or off.
    bool feed(unsigned char byte, MidiEvent& out);

private:
    int status_ = 0, need_ = 0, have_ = 0;
    unsigned char data_[2] = {};
};

// Which drum a key plays: C2 is the first lane.
inline int midiDrumLane(int note) { return ((note - 36) % kDrumLanes + kDrumLanes) % kDrumLanes; }

class MidiInput {
public:
    enum Target { Nothing, Drums, Synth };
    ~MidiInput() { stop(); }
    void start(Engine* engine, const std::string& folder = "/dev/snd");
    void stop();
    bool running() const { return thread_.joinable(); }
    // Where played notes go. Call it whenever the track being worked on changes.
    void setTarget(int track, Target kind) { target_.store(track * 4 + kind, std::memory_order_relaxed); }
    // How many keyboards are open just now.
    int devices() const { return devices_.load(std::memory_order_relaxed); }
    // For the screens, on their own thread: what was played, oldest first.
    bool poll(MidiEvent& out) { return events_.pop(out); }

private:
    void run();
    void play(const MidiEvent& event);

    Engine* engine_ = nullptr;
    std::string folder_;
    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<int> target_{0}, devices_{0};
    SpscQueue<MidiEvent, 256> events_;
    int heldOn_[128] = {}; // track + 1 a held key was sent to, so its note off goes to the same one
};

} // namespace apotheca
