#pragma once
// Files over WiFi: a small web server inside Apotheca. Turn it on, open the
// address the device shows in a browser on the same network, and drop files
// on the page: recordings (.wav), chord packs (.json) and songs (.song) go
// on; anything already there can be fetched back off.
//
// It is off until turned on, and off again when Apotheca closes. The address
// ends in a four-digit number made up each time it is turned on; a request
// without it is refused. That keeps out anyone on the network who has not
// seen the device's screen. It is not encryption: use it on a network you trust.
//
// What is checked before a file is kept:
//   - its name is a plain name with the right ending: no folders, no "..",
//     nothing hidden, 60 characters at most
//   - it is 30 MB at most
//   - it is what it says: a WAV this can read, a chord pack with chords in
//     it, a song this can open. Anything else is refused and not written.
//
// It runs on its own thread and only ever touches files. No SDL, no engine.

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace apotheca {

class Transfer {
public:
    ~Transfer() { stop(); }

    // Start listening. `port` 0 picks any free one. False if it could not.
    bool start(const std::string& sampleFolder, const std::string& songFolder, const std::string& chordFolder, int port, unsigned seed);
    void stop();
    bool running() const { return listening_ >= 0; }
    int port() const { return port_; }
    const std::string& pin() const { return pin_; }
    // "192.168.1.23:8080/4821": what to type into a browser. Without the
    // numbers before the colon if the device is on no network.
    std::string address() const;
    // Goes up each time a file is received, so the screen can look again.
    unsigned received() const { return received_.load(); }

    static constexpr std::size_t kLargestFile = 30u * 1024u * 1024u;

private:
    void serve();
    void answer(int connection);
    std::string folderFor(const std::string& kind) const;

    std::string samples_, songs_, chords_, pin_;
    int port_ = 0;
    int listening_ = -1;
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned> received_{0};
    std::thread thread_;
};

// A file's name as it came in a web address, made safe: "" if it is not a
// plain name ending in `ending` (".wav", ".json" or ".song").
std::string safeFileName(const std::string& fromAddress, const std::string& ending);

} // namespace apotheca
