#include "lights.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <vector>

namespace apotheca {

namespace {

bool readNumber(const std::string& path, int* out) {
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    long v = 0;
    const bool ok = std::fscanf(f, "%ld", &v) == 1;
    std::fclose(f);
    if (!ok || v < 0 || v > 1000000) return false;
    *out = static_cast<int>(v);
    return true;
}

bool writeNumber(const std::string& path, int value) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char text[16];
    const int n = std::snprintf(text, sizeof text, "%d\n", value);
    const bool ok = ::write(fd, text, static_cast<std::size_t>(n)) == n;
    return ::close(fd) == 0 && ok;
}

} // namespace

constexpr std::uint32_t Lights::kResendMs[2];

Lights::~Lights() {
    if (fd_ >= 0) ::close(fd_);
}

Lights& Lights::operator=(Lights&& o) noexcept {
    if (this != &o) {
        if (fd_ >= 0) ::close(fd_);
        take(o);
    }
    return *this;
}

void Lights::take(Lights& o) {
    pretend_ = o.pretend_;
    backlight_ = std::move(o.backlight_);
    max_ = o.max_, written_ = o.written_;
    dark_ = o.dark_, darkReadAt_ = o.darkReadAt_, darkRead_ = o.darkRead_;
    fd_ = o.fd_;
    o.fd_ = -1;
    power_ = std::move(o.power_);
    powered_ = o.powered_, powerKnown_ = o.powerKnown_;
    sent_ = o.sent_, sentAny_ = o.sentAny_;
    poweredAt_ = o.poweredAt_, resends_ = o.resends_, sends_ = o.sends_;
    o.backlight_.clear();
    o.pretend_ = false;
}

void Lights::findBacklight(const std::string& sys) {
    // The first folder (by name) with a max_brightness over 0 and a brightness that can be written.
    const std::string root = sys + "/class/backlight";
    DIR* dir = ::opendir(root.c_str());
    if (!dir) return;
    std::vector<std::string> names;
    while (const dirent* e = ::readdir(dir))
        if (e->d_name[0] != '.') names.push_back(e->d_name);
    ::closedir(dir);
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        const std::string folder = root + "/" + name;
        int max = 0;
        if (readNumber(folder + "/max_brightness", &max) && max > 0 && ::access((folder + "/brightness").c_str(), W_OK) == 0) {
            backlight_ = folder;
            max_ = max;
            return;
        }
    }
}

void Lights::openRing(const std::string& line, const std::string& power) {
    if (::access(power.c_str(), W_OK) != 0) return;
    // Not blocking: a frame that does not go now goes on a later pass. O_APPEND changes nothing on a line,
    // and lets a test's plain file keep every frame.
    const int fd = ::open(line.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK | O_APPEND | O_CLOEXEC);
    if (fd < 0) return;
    termios t;
    if (::tcgetattr(fd, &t) == 0) { // (a plain file, in the tests, is no line: nothing to set)
        ::cfmakeraw(&t);
        ::cfsetispeed(&t, B115200);
        ::cfsetospeed(&t, B115200);
        t.c_cflag |= CLOCAL;
        ::tcsetattr(fd, TCSANOW, &t);
    }
    fd_ = fd;
    power_ = power;
}

void Lights::update(std::uint32_t nowMs, int percent, const RingFrame& wanted) {
    if (pretend_) return;
    // The screen: written when its number changes.
    if (!backlight_.empty()) {
        const int value = backlightFor(percent, max_);
        if (value != written_ && writeNumber(backlight_ + "/brightness", value)) written_ = value;
        // The power key's tap: is the screen off? (Read, never written: apotheca-power's.)
        if (fd_ >= 0 && (!darkRead_ || nowMs - darkReadAt_ >= kDarkEveryMs)) {
            darkRead_ = true;
            darkReadAt_ = nowMs;
            int power = 0;
            dark_ = readNumber(backlight_ + "/bl_power", &power) && power != 0;
        }
    }
    if (fd_ < 0) return;
    const RingFrame frame = dark_ ? RingFrame() : wanted;
    // Its power, when that changes (and once at the start, whatever the system left).
    if (!powerKnown_ || frame.on != powered_) {
        if (!writeNumber(power_, frame.on ? 1 : 0)) return; // tried again next pass
        powerKnown_ = true;
        powered_ = frame.on;
        sentAny_ = false;
        if (powered_) poweredAt_ = nowMs, resends_ = 2;
    }
    if (!frame.on) return;
    bool due = !sentAny_ || frame != sent_;
    if (resends_ > 0 && nowMs - poweredAt_ >= kResendMs[2 - resends_]) due = true;
    if (!due) return;
    std::uint8_t bytes[kRingFrameBytes];
    ringBytes(frame, bytes);
    const ssize_t n = ::write(fd_, bytes, sizeof bytes);
    if (n != static_cast<ssize_t>(sizeof bytes)) return; // the line is full: the frame goes again next pass
    if (resends_ > 0 && nowMs - poweredAt_ >= kResendMs[2 - resends_]) --resends_;
    sent_ = frame;
    sentAny_ = true;
    ++sends_;
}

} // namespace apotheca
