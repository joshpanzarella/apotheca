#pragma once
// WiFi on our own firmware (docs/plans/firmware-plan.md, section 14): the app's side of the small
// program the system gives it, `apotheca-wifi` (its path in the environment variable APOTHECA_WIFI,
// set by the firmware's `run`; never on the port, where there is no Wifi and nothing here runs).
//
//   up, down               the radio on and off (WIFI PAGE on and off)
//   scan                   a line a network found:  SIGNAL SECURITY NAME   (NAME may hold spaces)
//                          SECURITY "open" needs no password; a word with "saved" in it is one the
//                          system keeps the password of. Anything else needs one typed.
//   join NAME              the password on its stdin, never on a command line (none: a saved or open one)
//   forget NAME
//   hotspot              our own network (no password; the page still asks for its PIN)
//   status                 off | joining NAME | joined NAME ADDRESS [HOSTNAME] | hotspot NAME ADDRESS [HOSTNAME] |
//                          wrong-password NAME | not-found NAME      (HOSTNAME: the name the page is reached by, apotheca.local)
//
// EVERY CALL RUNS ON A THREAD OF ITS OWN (one worker, the calls in the order asked): no frame waits
// for the radio. The worker touches nothing but this class's own answers, under a lock; the app
// thread reads a copy (`now()`). No SDL, no Project, no Session.
//
// THE PASSWORD IS NEVER LOGGED: it goes from the keys to the program's stdin and is then written
// over. Nothing here prints what it runs.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace apotheca {

struct WifiNetwork {
    std::string name;
    int signal = 0;      // as the system says it (the higher the stronger)
    bool open = false;   // needs no password
    bool saved = false;  // the system keeps its password
    bool needsPassword() const { return !open && !saved; }
};

enum class WifiState { Off, Joining, Joined, Hotspot, WrongPassword, NotFound };

struct WifiStatus {
    WifiState state = WifiState::Off;
    std::string name;     // the network it is about
    std::string address;  // joined: the handheld's address
    std::string hostname; // joined: the name it is reached by ("apotheca.local"); empty where the system does not say
};

// What the lines say. Free functions: the tests give them made-up lines.
WifiStatus wifiStatusFrom(const std::string& line);
std::vector<WifiNetwork> wifiNetworksFrom(const std::string& lines); // strongest first; a name twice: its strongest

constexpr int kWifiPasswordMost = 63; // a WPA password's most
constexpr int kWifiPasswordLeast = 8; // ...and its least
constexpr int kWifiNameMost = 32;     // a network name's most (bytes)

class Wifi {
public:
    Wifi();
    ~Wifi();
    Wifi(const Wifi&) = delete;
    Wifi& operator=(const Wifi&) = delete;

    // The program to run ("" none: nothing here does anything). The constructor takes APOTHECA_WIFI's.
    void useProgram(const std::string& path);
    bool here() const { return !program_.empty(); }

    // Asked from the app thread; each returns at once.
    void up();
    void down();
    void scan();   // (one already waiting is not asked twice)
    void status(); // (the same)
    void join(const std::string& name, std::string password); // the password "" for a saved or an open network
    void forget(const std::string& name);
    void hotspot(); // our own network on; `down` then `up` turns it off and joins again

    // Once a frame while the WIFI PAGE is on: asks for the status every kStatusEveryMs, and, while
    // `looking` (the NETWORK row is up), for a scan at once and every kScanEveryMs.
    void update(std::uint32_t nowMs, bool on, bool looking);
    static constexpr std::uint32_t kStatusEveryMs = 1000, kScanEveryMs = 10000;

    struct Now {
        WifiStatus status;
        std::vector<WifiNetwork> networks;
        bool scanned = false; // a scan has answered since `up`
        int serial = 0;       // goes up whenever anything above changes
    };
    Now now() const;
    int serial() const;

    // For a still: here, saying this, and nothing is ever run.
    void pretend(const Now& now);

    // For the tests: every call asked for has run and answered.
    void finish();
    int calls() const; // how many have run

private:
    struct Job {
        enum Kind { Up, Down, Scan, Status, Join, Forget, Hotspot } kind = Status;
        std::string name, password;
    };
    void ask(Job job, bool always = false); // always: a status or a scan asked for even though one waits (it must come AFTER what was just asked)
    void work();
    static bool run(const std::string& program, const Job& job, std::string& out);

    std::string program_;
    mutable std::mutex lock_;
    std::condition_variable wake_, idle_;
    std::deque<Job> jobs_;
    bool busy_ = false, quit_ = false, started_ = false, pretend_ = false;
    std::thread worker_;
    Now now_;
    int calls_ = 0;
    std::uint32_t statusAt_ = 0, scanAt_ = 0;
    bool timed_ = false, looked_ = false;
};

} // namespace apotheca
