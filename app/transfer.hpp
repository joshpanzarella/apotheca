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
//   - its name is a plain name with the right ending: no "..", nothing
//     hidden, 60 characters at most. A recording may be one folder down
//     ("kicks/808.wav": folder 24, name 36 at most); nothing else may.
//   - it is 30 MB at most
//   - it is what it says: a WAV this can read, a chord pack with chords in
//     it, a song this can open. Anything else is refused and not written.
//
// From the page a file can also be deleted (DELETE to its address) and a
// recording renamed or moved (POST to its address, "X-To: <where it goes>").
// Both hold names to the rules above and only touch a real file that is
// there: not a link, and not through one. A move writes over nothing. A
// deleted song goes to "deleted" in the song folder with its revs, as on the
// handheld; bounces are fetched and deleted, never sent. A folder inside the
// recordings goes with its last file.
//
// The Session is told through `received()`, and for a recording through
// `takeChanges()`: which one arrived, went, or was renamed or moved. The
// server never touches the song. The session reads the list on its own thread
// and makes the open song follow (Session::recordingChanged).
//
// Updating Apotheca itself the same way: the page takes a new program file.
// It must be a program for this kind of processor, and it is only SET ASIDE
// (as "apotheca.pending" beside the program). Nothing is installed by the
// network alone: someone holding the handheld has to accept it there, and
// then it is used at the next start. See session.hpp and scripts/Apotheca.sh.
//
// It runs on its own thread and only ever touches files. No SDL, no engine.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace apotheca {

// The sound-designer page talks to the program through one of these. The
// server only passes text along; what it means is the session's business
// (designer.hpp). Every call may come from the server's thread.
struct DesignDesk {
    virtual ~DesignDesk() = default;
    virtual std::string spec() = 0;  // JSON: what the synth on the track being worked on has
    virtual std::string state() = 0; // JSON: its values now
    virtual bool ask(const std::string& line) = 0; // "set 12 40", "note 60 1"...: false if it makes no sense
};

class Transfer {
public:
    ~Transfer() { stop(); }

    // Start listening. `port` 0 picks any free one. False if it could not.
    bool start(const std::string& sampleFolder, const std::string& songFolder, const std::string& chordFolder, int port, unsigned seed);
    void stop();
    // Where the program lives. Until this is set, updates are refused.
    void setProgramFolder(const std::string& folder) { program_ = folder; }
    // The sound-designer page is served only when there is one of these.
    void setDesk(DesignDesk* desk) { desk_ = desk; }
    // Where bounced songs are: listed on the page, to be fetched (and nothing is sent there).
    void setBounceFolder(const std::string& folder) { bounces_ = folder; }
    // Goes up each time a new program has been set aside for confirming.
    unsigned updatesArrived() const { return updates_.load(); }
    bool running() const { return listening_ >= 0; }
    int port() const { return port_; }
    const std::string& pin() const { return pin_; }
    // "192.168.1.23:8080/4821": what to type into a browser. Without the
    // numbers before the colon if the device is on no network.
    std::string address() const;
    // Goes up each time a file is received, deleted or moved, so the screen can look again.
    unsigned received() const { return received_.load(); }
    // What has happened to recordings since this was last asked, oldest first. Names are as a
    // song keeps them: no ".wav", and "kicks/808" in a folder. Sent: only `to`. Deleted: only
    // `from`. Renamed or moved: both. Safe to call from another thread than the server's; each
    // one is on the list before `received()` goes up for it.
    struct Change { std::string from, to; };
    std::vector<Change> takeChanges();
    // Held by the server while it writes or deletes a song file, and by the session while it
    // goes through the saved songs (Session::savedSongsFollow): neither undoes the other's work.
    std::mutex& songFiles() { return songFiles_; }
    // For tests: behave as a card does that will not rename a file into another folder, so the
    // long way round (write it again, then take the old one away) is the one that runs.
    static bool renameBroken;

    static constexpr std::size_t kLargestFile = 30u * 1024u * 1024u;

private:
    void serve();
    void answer(int connection);
    std::string folderFor(const std::string& kind) const;

    std::string samples_, songs_, chords_, pin_, program_, bounces_;
    std::atomic<unsigned> updates_{0};
    DesignDesk* desk_ = nullptr;
    int port_ = 0;
    int listening_ = -1;
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned> received_{0};
    void noteChange(const std::string& fromFile, const std::string& toFile); // file names, with their ".wav"
    std::mutex changesMutex_, songFiles_;
    std::vector<Change> changes_;
    std::thread thread_;
};

// True if these bytes are a program this machine could run: the right file
// format, 64-bit, for this processor, and not suspiciously small.
bool isProgramForThisMachine(const std::string& bytes);

// A file's name as it came in a web address, made safe: "" if it is not a
// plain name ending in `ending` (".wav", ".json" or ".song").
std::string safeFileName(const std::string& fromAddress, const std::string& ending);

} // namespace apotheca
