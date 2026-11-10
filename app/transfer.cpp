#include "transfer.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "chords.hpp"
#include "samples.hpp"
#include "song_file.hpp"

namespace apotheca {

namespace {

const char* const kPage = R"page(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Apotheca</title>
<style>
body{background:#14110e;color:#e0d5bf;font:16px/1.5 system-ui,sans-serif;margin:0;padding:24px;max-width:720px}
h1{font-size:20px;letter-spacing:.12em;margin:0 0 4px} h2{font-size:13px;letter-spacing:.12em;color:#8f8370;margin:28px 0 8px}
p{color:#8f8370;margin:0 0 16px} a{color:#6fbfa3}
#drop{border:2px dashed #43392f;padding:36px 16px;text-align:center;background:#1f1a16}
#drop.over{border-color:#6fbfa3} button{background:#6fbfa3;color:#14110e;border:0;padding:10px 18px;font:inherit;cursor:pointer}
li{list-style:none;padding:6px 0;border-bottom:1px solid #2a231d} ul{padding:0;margin:0}
.ok{color:#6fbfa3}.no{color:#c9a15a} input{display:none}
</style></head><body>
<h1>APOTHECA</h1><p>Files to and from the handheld.</p>
<div id="drop"><p>Drop files here: recordings (.wav), chord packs (.json), songs (.song).</p>
<button id="pick">Choose files</button><input id="files" type="file" multiple accept=".wav,.json,.song"></div>
<ul id="log"></ul>
<h2>RECORDINGS</h2><ul id="samples"></ul><h2>SONGS</h2><ul id="songs"></ul><h2>CHORD PACKS</h2><ul id="chords"></ul>
<script>
const base = location.pathname.replace(/\/$/, '');
const kinds = {wav: 'samples', json: 'chords', song: 'songs'};
function line(text, cls) { const li = document.createElement('li'); li.textContent = text; li.className = cls; document.getElementById('log').prepend(li); }
async function list() {
  const all = await (await fetch(base + '/list')).json();
  for (const kind of ['samples', 'songs', 'chords']) {
    const ul = document.getElementById(kind); ul.textContent = '';
    for (const name of all[kind]) { const li = document.createElement('li'); const a = document.createElement('a');
      a.href = base + '/' + kind + '/' + encodeURIComponent(name); a.download = name; a.textContent = name; li.append(a); ul.append(li); }
    if (!all[kind].length) { const li = document.createElement('li'); li.textContent = 'none yet'; li.className = 'no'; ul.append(li); }
  }
}
async function send(files) {
  for (const file of files) {
    const kind = kinds[file.name.split('.').pop().toLowerCase()];
    if (!kind) { line(file.name + ': not a .wav, .json or .song', 'no'); continue; }
    const answer = await fetch(base + '/' + kind + '/' + encodeURIComponent(file.name), {method: 'PUT', body: file});
    line(file.name + ': ' + (await answer.text()), answer.ok ? 'ok' : 'no');
  }
  list();
}
const drop = document.getElementById('drop'), input = document.getElementById('files');
document.getElementById('pick').onclick = () => input.click();
input.onchange = () => send(input.files);
drop.ondragover = e => { e.preventDefault(); drop.className = 'over'; };
drop.ondragleave = () => drop.className = '';
drop.ondrop = e => { e.preventDefault(); drop.className = ''; send(e.dataTransfer.files); };
list();
</script></body></html>
)page";

bool sendAll(int fd, const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t sent = ::send(fd, data, size, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

void reply(int fd, const char* status, const char* type, const std::string& body) {
    char head[256];
    const int n = std::snprintf(head, sizeof head,
                                "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n", status,
                                type, body.size());
    if (sendAll(fd, head, static_cast<std::size_t>(n))) sendAll(fd, body.data(), body.size());
}

std::string unescape(const std::string& in) { // %20 and the like
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() + 0 && std::isxdigit(static_cast<unsigned char>(in[i + 1])) && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            out += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

std::vector<std::string> filesEnding(const std::string& folder, const std::string& ending) {
    std::vector<std::string> names;
    if (DIR* dir = folder.empty() ? nullptr : opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (!safeFileName(file, ending).empty()) names.push_back(file);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string jsonList(const std::vector<std::string>& names) {
    std::string out = "[";
    for (std::size_t i = 0; i < names.size(); ++i) {
        out += i ? ",\"" : "\"";
        for (char c : names[i]) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += "\"";
    }
    return out + "]";
}

} // namespace

std::string safeFileName(const std::string& fromAddress, const std::string& ending) {
    const std::string name = unescape(fromAddress);
    if (name.size() <= ending.size() || name.size() > 60 || name[0] == '.' || name[0] == ' ') return "";
    for (char c : name)
        if (static_cast<unsigned char>(c) < 32 || c == 127 || c == '/' || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' || c == '*') return "";
    if (name.find("..") != std::string::npos) return "";
    std::string end = name.substr(name.size() - ending.size());
    for (char& c : end) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return end == ending ? name : "";
}

std::string Transfer::folderFor(const std::string& kind) const { return kind == "samples" ? samples_ : kind == "songs" ? songs_ : kind == "chords" ? chords_ : ""; }

bool Transfer::start(const std::string& sampleFolder, const std::string& songFolder, const std::string& chordFolder, int port, unsigned seed) {
    stop();
    samples_ = sampleFolder;
    songs_ = songFolder;
    chords_ = chordFolder;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in where = {};
    where.sin_family = AF_INET;
    where.sin_addr.s_addr = htonl(INADDR_ANY);
    where.sin_port = htons(static_cast<uint16_t>(port));
    socklen_t size = sizeof where;
    if (bind(fd, reinterpret_cast<sockaddr*>(&where), sizeof where) != 0 || listen(fd, 4) != 0 ||
        getsockname(fd, reinterpret_cast<sockaddr*>(&where), &size) != 0) {
        close(fd);
        return false;
    }
    port_ = ntohs(where.sin_port);
    seed = seed * 1664525u + 1013904223u;
    char pin[8];
    std::snprintf(pin, sizeof pin, "%04u", (seed >> 8) % 10000u);
    pin_ = pin;
    stopping_.store(false);
    listening_ = fd;
    thread_ = std::thread([this] { serve(); });
    return true;
}

void Transfer::stop() {
    if (listening_ < 0) return;
    stopping_.store(true);
    if (thread_.joinable()) thread_.join(); // it looks at `stopping_` five times a second
    close(listening_);
    listening_ = -1;
}

std::string Transfer::address() const {
    std::string host;
    ifaddrs* all = nullptr;
    if (getifaddrs(&all) == 0) {
        for (ifaddrs* a = all; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            const in_addr ip = reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr;
            if ((ntohl(ip.s_addr) >> 24) == 127) continue; // this machine talking to itself
            char text[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &ip, text, sizeof text)) host = text;
            break;
        }
        freeifaddrs(all);
    }
    return host + ":" + std::to_string(port_) + "/" + pin_;
}

void Transfer::serve() {
    while (!stopping_.load()) {
        pollfd waiting = {listening_, POLLIN, 0};
        if (poll(&waiting, 1, 200) <= 0) continue;
        const int connection = accept(listening_, nullptr, nullptr);
        if (connection < 0) continue;
        timeval patience = {5, 0}; // a browser that goes quiet does not hold the door
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof patience);
        setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &patience, sizeof patience);
        answer(connection);
        shutdown(connection, SHUT_WR);
        char rest[512]; // let the browser finish reading before the door shuts
        while (recv(connection, rest, sizeof rest, 0) > 0) {}
        close(connection);
    }
}

void Transfer::answer(int fd) {
    // The request line and headers: up to 8 KB, ending in a blank line.
    std::string head;
    std::size_t end = std::string::npos;
    char block[2048];
    while (end == std::string::npos && head.size() < 8192) {
        const ssize_t got = recv(fd, block, sizeof block, 0);
        if (got <= 0) return;
        head.append(block, static_cast<std::size_t>(got));
        end = head.find("\r\n\r\n");
    }
    if (end == std::string::npos) return reply(fd, "431 Too Much", "text/plain", "too much before the file\n");
    std::string body = head.substr(end + 4); // the start of the file, if one is coming
    head.resize(end);

    const std::size_t space = head.find(' '), space2 = head.find(' ', space + 1);
    if (space == std::string::npos || space2 == std::string::npos) return reply(fd, "400 Bad Request", "text/plain", "not a request\n");
    const std::string method = head.substr(0, space);
    std::string path = head.substr(space + 1, space2 - space - 1);
    if (const std::size_t query = path.find('?'); query != std::string::npos) path.resize(query);

    // The number from the device's screen comes first, or nothing is said.
    const std::string door = "/" + pin_;
    if (path.compare(0, door.size(), door) != 0 || (path.size() > door.size() && path[door.size()] != '/'))
        return reply(fd, "403 Forbidden", "text/plain", "Open the address shown on the Apotheca screen, with its four-digit number.\n");
    path = path.substr(door.size());

    if (method == "GET" && (path.empty() || path == "/")) return reply(fd, "200 OK", "text/html; charset=utf-8", kPage);
    if (method == "GET" && path == "/list") {
        return reply(fd, "200 OK", "application/json",
                     "{\"samples\":" + jsonList(filesEnding(samples_, ".wav")) + ",\"songs\":" + jsonList(filesEnding(songs_, ".song")) +
                         ",\"chords\":" + jsonList(filesEnding(chords_, ".json")) + "}");
    }

    // /samples/<name>.wav, /songs/<name>.song, /chords/<name>.json
    const std::size_t slash = path.find('/', 1);
    const std::string kind = slash == std::string::npos ? "" : path.substr(1, slash - 1);
    const std::string ending = kind == "samples" ? ".wav" : kind == "songs" ? ".song" : kind == "chords" ? ".json" : "";
    const std::string folder = folderFor(kind);
    const std::string name = ending.empty() ? "" : safeFileName(path.substr(slash + 1), ending);
    if (folder.empty() || name.empty()) return reply(fd, "400 Bad Request", "text/plain", "not a file name this takes\n");
    const std::string file = folder + "/" + name;

    if (method == "GET") {
        std::string contents;
        if (!readTextFile(file, contents)) return reply(fd, "404 Not Found", "text/plain", "no such file\n");
        return reply(fd, "200 OK", "application/octet-stream", contents);
    }
    if (method != "PUT") return reply(fd, "405 Not Allowed", "text/plain", "only GET and PUT\n");

    // How big it says it is.
    std::string lower = head;
    for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    const std::size_t at = lower.find("\r\ncontent-length:");
    if (at == std::string::npos) return reply(fd, "411 Length Required", "text/plain", "how big is it?\n");
    const unsigned long long length = std::strtoull(lower.c_str() + at + 17, nullptr, 10);
    if (length > kLargestFile) return reply(fd, "413 Too Large", "text/plain", "too big: 30 MB at most\n");
    if (lower.find("\r\nexpect: 100-continue") != std::string::npos) sendAll(fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    if (body.size() > length) body.resize(static_cast<std::size_t>(length));
    while (body.size() < length) {
        const ssize_t got = recv(fd, block, sizeof block, 0);
        if (got <= 0) return; // gave up part-way: nothing is kept
        body.append(block, std::min(static_cast<std::size_t>(got), static_cast<std::size_t>(length) - body.size()));
    }

    // Is it what it says it is?
    bool good = false;
    if (kind == "samples") {
        std::vector<float> sound;
        good = parseWav(body, sound) && !sound.empty();
    } else if (kind == "chords") {
        ChordPack pack;
        good = parseChordPack(body, pack);
    } else {
        std::unique_ptr<Project> song(new Project);
        good = parseSong(body, *song);
    }
    if (!good) {
        return reply(fd, "400 Bad Request", "text/plain",
                     kind == "samples" ? "not a WAV file this can read\n" : kind == "chords" ? "not a chord pack\n" : "not an Apotheca song\n");
    }
    mkdir(folder.c_str(), 0755); // fine if it is already there
    if (!writeTextFile(file, body)) return reply(fd, "500 Could Not Save", "text/plain", "could not save it: is the card full?\n");
    received_.fetch_add(1);
    reply(fd, "200 OK", "text/plain", "saved\n");
}

} // namespace apotheca
