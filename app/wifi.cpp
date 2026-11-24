#include "wifi.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>

extern char** environ;

namespace apotheca {

namespace {

std::vector<std::string> wordsOf(const std::string& line) {
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
        std::size_t end = at;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t') ++end;
        if (end > at) words.push_back(line.substr(at, end - at));
        at = end;
    }
    return words;
}

std::string joined(const std::vector<std::string>& words, std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t i = from; i < to; ++i) out += (i > from ? " " : "") + words[i];
    return out;
}

// A name held to a network name's most. Its bytes are kept as they are: it is what `join` is given back.
std::string plain(std::string name) {
    if (name.size() > static_cast<std::size_t>(kWifiNameMost)) name.resize(static_cast<std::size_t>(kWifiNameMost));
    return name;
}

bool looksLikeAddress(const std::string& word) {
    if (word.empty()) return false;
    bool dot = false;
    for (char c : word) {
        if (c == '.' || c == ':') dot = true;
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return dot;
}

bool endsWith(const std::string& word, const char* tail) {
    const std::size_t n = std::strlen(tail);
    return word.size() > n && word.compare(word.size() - n, n, tail) == 0;
}

std::string firstLine(const std::string& text) {
    std::size_t end = text.find('\n');
    std::string line = text.substr(0, end);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}

} // namespace

WifiStatus wifiStatusFrom(const std::string& text) {
    WifiStatus status;
    const std::vector<std::string> words = wordsOf(firstLine(text));
    if (words.empty()) return status;
    const std::string& what = words[0];
    std::size_t end = words.size();
    if (what == "joined" || what == "hotspot") {
        // joined NAME ADDRESS [HOSTNAME] (hotspot: the same words): read from the right, since NAME
        // may hold spaces.
        if (end > 3 && endsWith(words[end - 1], ".local") && looksLikeAddress(words[end - 2])) status.hostname = words[--end];
        if (end > 2 && looksLikeAddress(words[end - 1])) status.address = words[--end];
        if (end < 2) return status; // (no name: not a line of ours)
        status.state = what == "joined" ? WifiState::Joined : WifiState::Hotspot;
    } else if (what == "joining") {
        status.state = WifiState::Joining;
    } else if (what == "wrong-password") {
        status.state = WifiState::WrongPassword;
    } else if (what == "not-found") {
        status.state = WifiState::NotFound;
    } else {
        return status; // off, or a word nobody knows
    }
    status.name = plain(joined(words, 1, end));
    if (status.name.empty()) status = WifiStatus();
    return status;
}

std::vector<WifiNetwork> wifiNetworksFrom(const std::string& lines) {
    std::vector<WifiNetwork> found;
    std::size_t at = 0;
    while (at < lines.size()) {
        std::size_t end = lines.find('\n', at);
        if (end == std::string::npos) end = lines.size();
        std::string line = lines.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        const std::vector<std::string> words = wordsOf(line);
        if (words.size() < 3) continue; // (a hidden network has no name: not offered)
        char* rest = nullptr;
        const double signal = std::strtod(words[0].c_str(), &rest);
        if (rest == words[0].c_str()) continue;
        WifiNetwork n;
        n.signal = signal < -1000.0 ? -1000 : (signal > 1000.0 ? 1000 : static_cast<int>(signal));
        std::string security = words[1];
        for (char& c : security) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        n.open = security == "open" || security == "none";
        n.saved = security.find("saved") != std::string::npos;
        // The name is the rest of the line as it stands (its own spaces kept).
        std::size_t name = line.find(words[1], line.find(words[0]) + words[0].size()) + words[1].size();
        while (name < line.size() && (line[name] == ' ' || line[name] == '\t')) ++name;
        n.name = plain(line.substr(name));
        while (!n.name.empty() && n.name.back() == ' ') n.name.pop_back();
        if (n.name.empty()) continue;
        bool twice = false;
        for (WifiNetwork& other : found) {
            if (other.name != n.name) continue;
            twice = true;
            if (n.signal > other.signal) other.signal = n.signal;
            other.open = other.open && n.open;
            other.saved = other.saved || n.saved;
        }
        if (!twice && found.size() < 64) found.push_back(n);
    }
    std::stable_sort(found.begin(), found.end(), [](const WifiNetwork& a, const WifiNetwork& b) { return a.signal > b.signal; });
    return found;
}

Wifi::Wifi() {
    if (const char* program = std::getenv("APOTHECA_WIFI")) program_ = program;
}

Wifi::~Wifi() {
    {
        std::lock_guard<std::mutex> hold(lock_);
        quit_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Wifi::useProgram(const std::string& path) {
    finish();
    std::lock_guard<std::mutex> hold(lock_);
    program_ = path;
    pretend_ = false;
    now_ = Now();
    timed_ = false;
}

void Wifi::pretend(const Now& now) {
    finish();
    std::lock_guard<std::mutex> hold(lock_);
    program_ = "(pretended)";
    pretend_ = true;
    now_ = now;
}

void Wifi::ask(Job job, bool always) {
    if (program_.empty() || pretend_) return;
    {
        std::lock_guard<std::mutex> hold(lock_);
        if (!always && (job.kind == Job::Scan || job.kind == Job::Status))
            for (const Job& waiting : jobs_)
                if (waiting.kind == job.kind) return; // one is enough
        jobs_.push_back(std::move(job));
        if (!started_) {
            started_ = true;
            worker_ = std::thread([this] { work(); });
        }
    }
    wake_.notify_all();
}

void Wifi::up() {
    Job job;
    job.kind = Job::Up;
    ask(job);
    status();
}

void Wifi::down() {
    Job job;
    job.kind = Job::Down;
    ask(job);
    std::lock_guard<std::mutex> hold(lock_);
    timed_ = false;
}

void Wifi::scan() {
    Job job;
    job.kind = Job::Scan;
    ask(job);
}

void Wifi::status() {
    Job job;
    job.kind = Job::Status;
    ask(job);
}

void Wifi::join(const std::string& name, std::string password) {
    if (program_.empty() || pretend_) return;
    Job job;
    job.kind = Job::Join;
    job.name = name;
    job.password.swap(password);
    {
        // What the row shows until the system says more: it is being joined.
        std::lock_guard<std::mutex> hold(lock_);
        now_.status = WifiStatus();
        now_.status.state = WifiState::Joining;
        now_.status.name = name;
        ++now_.serial;
    }
    ask(std::move(job));
    Job after;
    after.kind = Job::Status;
    ask(after, true); // (after the join, whatever was waiting before it)
}

void Wifi::forget(const std::string& name) {
    Job job;
    job.kind = Job::Forget;
    job.name = name;
    ask(job);
    Job after;
    after.kind = Job::Status;
    ask(after, true);
    after.kind = Job::Scan;
    ask(after, true); // (it is no longer a saved one)
}

void Wifi::hotspot() {
    Job job;
    job.kind = Job::Hotspot;
    ask(job);
    Job after;
    after.kind = Job::Status;
    ask(after, true); // (the address and the name, once the network is up)
}

void Wifi::update(std::uint32_t nowMs, bool on, bool looking) {
    if (program_.empty() || !on) {
        timed_ = false;
        looked_ = false;
        return;
    }
    if (!timed_) timed_ = true, statusAt_ = nowMs + kStatusEveryMs, scanAt_ = nowMs;
    if (nowMs - statusAt_ < 0x80000000u) statusAt_ = nowMs + kStatusEveryMs, status();
    if (looking && (!looked_ || nowMs - scanAt_ < 0x80000000u)) scanAt_ = nowMs + kScanEveryMs, scan();
    looked_ = looking;
}

Wifi::Now Wifi::now() const {
    std::lock_guard<std::mutex> hold(lock_);
    return now_;
}

int Wifi::serial() const {
    std::lock_guard<std::mutex> hold(lock_);
    return now_.serial;
}

int Wifi::calls() const {
    std::lock_guard<std::mutex> hold(lock_);
    return calls_;
}

void Wifi::finish() {
    std::unique_lock<std::mutex> hold(lock_);
    idle_.wait(hold, [this] { return jobs_.empty() && !busy_; });
}

void Wifi::work() {
    for (;;) {
        Job job;
        std::string program;
        {
            std::unique_lock<std::mutex> hold(lock_);
            wake_.wait(hold, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            busy_ = true;
            program = program_;
        }
        std::string out;
        const bool ran = run(program, job, out);
        // The password goes no further than the program's stdin.
        std::fill(job.password.begin(), job.password.end(), '\0');
        {
            std::lock_guard<std::mutex> hold(lock_);
            ++calls_;
            if (job.kind == Job::Status) {
                const WifiStatus status = ran ? wifiStatusFrom(out) : WifiStatus();
                // (A join asked for since this status was: the row keeps saying it is being joined.)
                bool joinWaits = false;
                for (const Job& waiting : jobs_) joinWaits = joinWaits || waiting.kind == Job::Join;
                const WifiStatus& was = now_.status;
                if (!joinWaits && (status.state != was.state || status.name != was.name || status.address != was.address || status.hostname != was.hostname)) {
                    now_.status = status;
                    ++now_.serial;
                }
            } else if (job.kind == Job::Scan) {
                if (ran) {
                    now_.networks = wifiNetworksFrom(out);
                    now_.scanned = true;
                    ++now_.serial;
                }
            } else if (job.kind == Job::Down || job.kind == Job::Up) {
                now_.networks.clear();
                now_.scanned = false;
                if (job.kind == Job::Down) now_.status = WifiStatus();
                ++now_.serial;
            } else if (job.kind == Job::Hotspot) {
                now_.networks.clear(); // the radio cannot look about while it is the network
                now_.scanned = false;
                if (ran) now_.status = wifiStatusFrom(out); // `hotspot NAME` (the status after says the rest)
                ++now_.serial;
            }
            busy_ = false;
        }
        idle_.notify_all();
    }
}

// Runs `program WORD [NAME]`, the password (if any) on its stdin, and gathers what it says. No shell:
// a network's name is one argument whatever it holds. Half a minute at most, then it is stopped.
namespace {
// A pipe whose two ends close in any program started from here. Linux makes it so in one call; a desk
// without pipe2 (a Mac) sets each end after (it starts no other program meanwhile that matters).
int closingPipe(int ends[2]) {
#ifdef __linux__
    return pipe2(ends, O_CLOEXEC);
#else
    if (pipe(ends) != 0) return -1;
    fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    return 0;
#endif
}
} // namespace

bool Wifi::run(const std::string& program, const Job& job, std::string& out) {
    static const char* const kWords[] = {"up", "down", "scan", "status", "join", "forget", "hotspot"};
    int in[2], back[2];
    if (closingPipe(in) != 0) return false;
    if (closingPipe(back) != 0) {
        close(in[0]), close(in[1]);
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, back[1], 1);
    std::string word = kWords[job.kind], name = job.name;
    std::string path = program;
    char* argv[4] = {const_cast<char*>(path.c_str()), const_cast<char*>(word.c_str()), nullptr, nullptr}; // (never &s[0]: empty is no place)
    if (job.kind == Job::Join || job.kind == Job::Forget) argv[2] = const_cast<char*>(name.c_str());
    pid_t child = 0;
    const int failed = posix_spawn(&child, program.c_str(), &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(in[0]), close(back[1]);
    if (failed != 0) {
        close(in[1]), close(back[0]);
        return false;
    }
    // The password, and the end of it. (A program that does not read it: SIGPIPE must not end us.)
    if (job.kind == Job::Join && !job.password.empty()) {
        struct sigaction ignore = {}, before = {};
        ignore.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &ignore, &before);
        const std::string line = job.password + "\n";
        std::size_t sent = 0;
        while (sent < line.size()) {
            const ssize_t n = write(in[1], line.data() + sent, line.size() - sent);
            if (n <= 0) break;
            sent += static_cast<std::size_t>(n);
        }
        sigaction(SIGPIPE, &before, nullptr);
    }
    close(in[1]);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool late = false;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            late = true;
            break;
        }
        pollfd waiting = {back[0], POLLIN, 0};
        const int ready = poll(&waiting, 1, static_cast<int>(left));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) {
            late = ready == 0;
            break;
        }
        char piece[1024];
        const ssize_t n = read(back[0], piece, sizeof piece);
        if (n <= 0) break;
        if (out.size() < 65536) out.append(piece, static_cast<std::size_t>(n));
    }
    close(back[0]);
    if (late) kill(child, SIGKILL);
    int how = 0;
    while (waitpid(child, &how, 0) < 0 && errno == EINTR) {}
    return !late && WIFEXITED(how) && WEXITSTATUS(how) == 0;
}

} // namespace apotheca
