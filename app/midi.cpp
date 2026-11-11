#include "midi.hpp"

#include <cerrno>
#include <cstring>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace apotheca {

bool MidiParser::feed(unsigned char byte, MidiEvent& out) {
    if (byte >= 0xF8) return false; // clock and the like may arrive in the middle of anything
    if (byte & 0x80) {
        have_ = 0;
        if (byte >= 0xF0) {
            status_ = 0; // a system message: no notes, and it ends "running status"
            need_ = 0;
            return false;
        }
        status_ = byte;
        const int kind = byte & 0xF0;
        need_ = kind == 0xC0 || kind == 0xD0 ? 1 : 2;
        return false;
    }
    if (status_ == 0) return false; // data nobody asked for: before any first byte, or inside a dump
    data_[have_++] = byte;
    if (have_ < need_) return false;
    have_ = 0; // "running status": the next data bytes are another message of the same kind
    const int kind = status_ & 0xF0;
    if (kind == 0x90 && data_[1] > 0) {
        out.kind = MidiEvent::NoteOn;
        out.note = data_[0];
        out.velocity = static_cast<float>(data_[1]) / 127.f;
        return true;
    }
    if (kind == 0x80 || kind == 0x90) { // a note on at no loudness is a note off
        out.kind = MidiEvent::NoteOff;
        out.note = data_[0];
        out.velocity = 0.f;
        return true;
    }
    return false;
}

void MidiInput::start(Engine* engine, const std::string& folder) {
    stop();
    engine_ = engine;
    folder_ = folder;
    quit_.store(false);
    thread_ = std::thread([this] { run(); });
}

void MidiInput::stop() {
    if (!thread_.joinable()) return;
    quit_.store(true);
    thread_.join();
    devices_.store(0);
}

void MidiInput::play(const MidiEvent& event) {
    const int target = target_.load(std::memory_order_relaxed);
    const int track = target / 4, kind = target % 4;
    Command c;
    if (event.kind == MidiEvent::NoteOn) {
        if (kind == Synth) {
            c.type = Command::NoteOn;
            c.track = track;
            c.a = event.note;
            c.value = event.velocity;
            engine_->sendLive(c);
            heldOn_[event.note] = track + 1;
        } else if (kind == Drums) {
            c.type = Command::Trigger;
            c.track = track;
            c.a = midiDrumLane(event.note);
            c.value = event.velocity;
            engine_->sendLive(c);
        }
    } else if (heldOn_[event.note]) {
        // Let go on the track it was played on, even if another is chosen now.
        c.type = Command::NoteOff;
        c.track = heldOn_[event.note] - 1;
        c.a = event.note;
        engine_->sendLive(c);
        heldOn_[event.note] = 0;
    }
    events_.push(event); // if the screens are not listening the oldest are kept and the rest dropped
}

void MidiInput::run() {
    struct Device {
        std::string name;
        int fd;
        MidiParser parser;
    };
    std::vector<Device> open;
    int sinceScan = 1000;
    while (!quit_.load(std::memory_order_relaxed)) {
        // Look for keyboards that have arrived, about three times a second.
        if (sinceScan >= 300) {
            sinceScan = 0;
            if (DIR* dir = opendir(folder_.c_str())) {
                while (const dirent* entry = readdir(dir)) {
                    if (std::strncmp(entry->d_name, "midiC", 5) != 0) continue;
                    bool have = false;
                    for (const Device& d : open) have = have || d.name == entry->d_name;
                    if (have || open.size() >= 8) continue;
                    const int fd = ::open((folder_ + "/" + entry->d_name).c_str(), O_RDONLY | O_NONBLOCK);
                    if (fd >= 0) open.push_back({entry->d_name, fd, MidiParser()});
                }
                closedir(dir);
            }
            devices_.store(static_cast<int>(open.size()), std::memory_order_relaxed);
        }
        pollfd fds[8];
        for (std::size_t i = 0; i < open.size(); ++i) fds[i] = {open[i].fd, POLLIN, 0};
        const int waitMs = 50;
        const int ready = ::poll(fds, open.size(), waitMs);
        sinceScan += waitMs;
        if (ready <= 0) continue;
        sinceScan -= waitMs / 2; // it came back early; near enough
        for (std::size_t i = open.size(); i-- > 0;) {
            bool gone = (fds[i].revents & (POLLERR | POLLNVAL)) != 0;
            if (fds[i].revents & POLLIN) {
                unsigned char bytes[256];
                const ssize_t got = read(open[i].fd, bytes, sizeof bytes);
                MidiEvent event;
                for (ssize_t b = 0; b < got; ++b)
                    if (open[i].parser.feed(bytes[b], event)) play(event);
                if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR)) gone = true;
            } else if (fds[i].revents & POLLHUP) {
                gone = true;
            }
            if (gone) {
                // Pulled out. Any key it was holding is let go.
                close(open[i].fd);
                open.erase(open.begin() + static_cast<long>(i));
                if (open.empty()) {
                    MidiEvent off;
                    off.kind = MidiEvent::NoteOff;
                    for (int note = 0; note < 128; ++note)
                        if (heldOn_[note]) off.note = note, play(off);
                }
                devices_.store(static_cast<int>(open.size()), std::memory_order_relaxed);
            }
        }
    }
    for (const Device& d : open) close(d.fd);
}

} // namespace apotheca
