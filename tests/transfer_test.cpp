// Tests for files over WiFi: a real server on this machine, and requests
// made to it the way a browser makes them.

#include <SDL.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "transfer.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

std::string tempFolder() {
    char pattern[] = "/tmp/apotheca-transfer-XXXXXX";
    const char* made = mkdtemp(pattern);
    return made ? made : "/tmp";
}
bool exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

struct Answer {
    int status = 0; // 0: could not connect, or nothing came back
    std::string body;
    std::string head; // the status line and what came with it
};

// Send these bytes to the server and read everything it says.
Answer raw(int port, const std::string& request) {
    Answer answer;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in where = {};
    where.sin_family = AF_INET;
    where.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &where.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&where), sizeof where) != 0) {
        close(fd);
        return answer;
    }
    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t n = send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break; // the server may refuse a big file before it has all been sent
        sent += static_cast<std::size_t>(n);
    }
    shutdown(fd, SHUT_WR);
    std::string all;
    char block[4096];
    for (ssize_t n = recv(fd, block, sizeof block, 0); n > 0; n = recv(fd, block, sizeof block, 0)) all.append(block, static_cast<std::size_t>(n));
    close(fd);
    // A "100 Continue" may come first; the real answer is the last one.
    std::size_t at = 0;
    while (all.compare(at, 12, "HTTP/1.1 100") == 0) at = all.find("\r\n\r\n", at) + 4;
    if (all.size() > at + 12) answer.status = std::atoi(all.c_str() + at + 9);
    const std::size_t blank = all.find("\r\n\r\n", at);
    if (blank != std::string::npos) answer.body = all.substr(blank + 4);
    if (blank != std::string::npos) answer.head = all.substr(at, blank - at);
    return answer;
}
Answer get(int port, const std::string& path) { return raw(port, "GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n"); }
Answer put(int port, const std::string& path, const std::string& body) {
    return raw(port, "PUT " + path + " HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
}

Answer del(int port, const std::string& path) { return raw(port, "DELETE " + path + " HTTP/1.1\r\nHost: x\r\n\r\n"); }
// Rename or move: where it is, and where it is to go (written the way an address is).
Answer post(int port, const std::string& path, const std::string& to) {
    return raw(port, "POST " + path + " HTTP/1.1\r\nHost: x\r\nX-To: " + to + "\r\nContent-Length: 0\r\n\r\n");
}

std::string aWav(float seconds = 0.1f) {
    std::vector<float> v(static_cast<std::size_t>(seconds * 48000.f));
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = 0.3f * std::sin(0.05f * static_cast<float>(i));
    return wavBytes(v, 48000);
}
const char* const kPack = "{\"name\":\"Sent\",\"chords\":[{\"intervals\":[0,4,7],\"root\":{\"degree\":0}}]}";

void testNames() {
    CHECK(safeFileName("kick.wav", ".wav") == "kick.wav");
    CHECK(safeFileName("my%20loop.WAV", ".wav") == "my loop.WAV"); // as a browser sends a space; any case of ending
    CHECK(safeFileName("quiet road.song", ".song") == "quiet road.song");
    const char* bad[] = {"", ".wav", "kick", "kick.mp3", "kick.wav.exe", "../kick.wav", "..%2Fkick.wav", "a/b.wav", "a%2Fb.wav", "a\\b.wav",
                         ".hidden.wav", " space.wav", "a..b.wav", "a:b.wav", "a%00b.wav", "a%0Ab.wav", "a*b.wav", "a?b.wav", "a\"b.wav", "a|b.wav"};
    for (const char* name : bad) CHECK(safeFileName(name, ".wav").empty());
    CHECK(safeFileName(std::string(56, 'a') + ".wav", ".wav").size() == 60);
    CHECK(safeFileName(std::string(57, 'a') + ".wav", ".wav").empty()); // 60 characters at most
    CHECK(safeFileName("kick.wav", ".song").empty());
}

void testTheServer() {
    const std::string root = tempFolder();
    const std::string samples = root + "/samples", songs = root + "/songs", chords = root + "/chords";
    mkdir(songs.c_str(), 0755);
    writeTextFile(songs + "/quiet road.song", "apotheca-song 1\ntempo 97\n");
    writeTextFile(songs + "/quiet road.1.rev", "apotheca-song 1\n");

    Transfer transfer;
    CHECK(!transfer.running());
    CHECK(transfer.start(samples, songs, chords, 0, 12345));
    CHECK(transfer.running() && transfer.port() > 0 && transfer.pin().size() == 4);
    const int port = transfer.port();
    const std::string door = "/" + transfer.pin();
    CHECK(transfer.address().find(":" + std::to_string(port) + door) != std::string::npos);

    // The page, only with the number from the screen.
    Answer a = get(port, door);
    CHECK(a.status == 200 && a.body.find("APOTHECA") != std::string::npos && a.body.find("<script>") != std::string::npos);
    CHECK(get(port, door + "/").status == 200);
    CHECK(get(port, "/").status == 403 && get(port, "/0000x").status == 403 && get(port, door + "9").status == 403);
    CHECK(get(port, "/list").status == 403 && put(port, "/samples/kick.wav", aWav()).status == 403);
    CHECK(get(port, "/" + std::string(transfer.pin().rbegin(), transfer.pin().rend()) + "x/list").status == 403);
    CHECK(!exists(samples + "/kick.wav"));

    // A recording goes on: checked, saved, listed, and comes back off the same.
    const std::string wav = aWav();
    a = put(port, door + "/samples/kick%20one.wav", wav);
    CHECK(a.status == 200 && a.body == "saved\n" && transfer.received() == 1);
    std::string kept;
    CHECK(readTextFile(samples + "/kick one.wav", kept) && kept == wav); // the folder was made for it
    a = get(port, door + "/list");
    CHECK(a.status == 200 && a.body.find("{\"version\":\"") == 0 && a.body.find(",\"samples\":[\"kick one.wav\"],\"songs\":[\"quiet road.song\"],\"chords\":[],\"bounces\":[]}") != std::string::npos);
    a = get(port, door + "/samples/kick%20one.wav");
    CHECK(a.status == 200 && a.body == wav);
    CHECK(get(port, door + "/songs/quiet%20road.song").body == "apotheca-song 1\ntempo 97\n");
    CHECK(get(port, door + "/samples/missing.wav").status == 404);
    // A chord pack and a song go on too.
    CHECK(put(port, door + "/chords/sent.json", kPack).status == 200 && exists(chords + "/sent.json"));
    CHECK(put(port, door + "/songs/new%20one.song", "apotheca-song 1\ntrack drums\n").status == 200 && exists(songs + "/new one.song"));
    CHECK(transfer.received() == 3);
    // A browser's query string on the end is ignored.
    CHECK(get(port, door + "/list?x=1").status == 200);

    // What is not what it says is refused and nothing is written.
    CHECK(put(port, door + "/samples/fake.wav", "this is not sound").status == 400 && !exists(samples + "/fake.wav"));
    CHECK(put(port, door + "/samples/empty.wav", "").status == 400 && !exists(samples + "/empty.wav"));
    CHECK(put(port, door + "/chords/fake.json", "{\"chords\":[]}").status == 400 && !exists(chords + "/fake.json"));
    CHECK(put(port, door + "/songs/fake.song", "hello").status == 400 && !exists(songs + "/fake.song"));
    CHECK(put(port, door + "/songs/kick.song", wav).status == 400); // a WAV sent as a song
    // Names that are not plain names; folders that are not there to write to.
    CHECK(put(port, door + "/samples/..%2Fescaped.wav", wav).status == 400 && !exists(root + "/escaped.wav"));
    // A recording may go in a folder, one level down: the folder is made for it, and it is listed and fetched from there.
    CHECK(put(port, door + "/samples/kicks/deep%20one.wav", wav).status == 200 && readTextFile(samples + "/kicks/deep one.wav", kept) && kept == wav);
    CHECK(put(port, door + "/samples/breaks%2Famen.wav", wav).status == 200 && exists(samples + "/breaks/amen.wav")); // the slash as a browser may send it
    a = get(port, door + "/list");
    CHECK(a.body.find(",\"samples\":[\"kick one.wav\",\"breaks/amen.wav\",\"kicks/deep one.wav\"],") != std::string::npos);
    a = get(port, door + "/samples/kicks/deep%20one.wav");
    CHECK(a.status == 200 && a.body == wav);
    CHECK(get(port, door + "/samples/kicks/missing.wav").status == 404);
    // ...and no deeper, and not by a name that is not a plain one, and only recordings.
    CHECK(put(port, door + "/samples/a/b/c.wav", wav).status == 400 && !exists(samples + "/a"));
    CHECK(put(port, door + "/samples/../up.wav", wav).status == 400 && put(port, door + "/samples/a/..%2Fup.wav", wav).status == 400 && !exists(root + "/up.wav"));
    CHECK(put(port, door + "/samples/.hid/x.wav", wav).status == 400 && put(port, door + "/samples/a/.x.wav", wav).status == 400 && put(port, door + "/samples//x.wav", wav).status == 400);
    CHECK(put(port, door + "/samples/a/.wav", wav).status == 400 && put(port, door + "/samples/a/b.json", wav).status == 400);
    CHECK(put(port, door + "/samples/a%20folder%20name%20that%20is%20far%20too%20long/b.wav", wav).status == 400);
    CHECK(put(port, door + "/songs/sub/quiet.song", "x").status == 400 && put(port, door + "/chords/sub/p.json", kPack).status == 400);
    CHECK(get(port, door + "/songs/sub/quiet%20road.song").status == 400);
    CHECK(put(port, door + "/samples/kick.json", kPack).status == 400 && put(port, door + "/samples/.hidden.wav", wav).status == 400);
    CHECK(put(port, door + "/elsewhere/kick.wav", wav).status == 400 && put(port, door + "/samples", wav).status == 400);
    CHECK(get(port, door + "/songs/quiet%20road.1.rev").status == 400); // only the three kinds
    CHECK(get(port, door + "/songs/..%2F..%2Fsecret.song").status == 400);
    // Too big is refused on its size alone, before any of it is read.
    a = raw(port, "PUT " + door + "/samples/huge.wav HTTP/1.1\r\nContent-Length: 40000000\r\n\r\nRIFF");
    CHECK(a.status == 413 && !exists(samples + "/huge.wav"));
    // No size given; a file cut off part-way; a method it does not take; rubbish.
    CHECK(raw(port, "PUT " + door + "/samples/x.wav HTTP/1.1\r\n\r\n" + wav).status == 411);
    a = raw(port, "PUT " + door + "/samples/cut.wav HTTP/1.1\r\nContent-Length: " + std::to_string(wav.size()) + "\r\n\r\n" + wav.substr(0, 100));
    CHECK(a.status == 0 && !exists(samples + "/cut.wav") && !exists(samples + "/cut.wav.part"));
    CHECK(raw(port, "PATCH " + door + "/samples/kick%20one.wav HTTP/1.1\r\n\r\n").status == 405 && exists(samples + "/kick one.wav"));
    CHECK(raw(port, "nonsense\r\n\r\n").status == 400);
    CHECK(raw(port, std::string(10000, 'x')).status == 431);
    CHECK(raw(port, "").status == 0);
    // A file sent with more after it than it said: only what it said is kept.
    a = raw(port, "PUT " + door + "/samples/exact.wav HTTP/1.1\r\nContent-Length: " + std::to_string(wav.size()) + "\r\n\r\n" + wav + "EXTRA");
    CHECK(a.status == 200 && readTextFile(samples + "/exact.wav", kept) && kept == wav);
    // The same for a small one that arrives all at once with the headers.
    const std::string tiny = "apotheca-song 1\ntempo 88\n";
    a = raw(port, "PUT " + door + "/songs/tiny.song HTTP/1.1\r\nContent-Length: " + std::to_string(tiny.size()) + "\r\n\r\n" + tiny + "tempo 200\n");
    CHECK(a.status == 200 && readTextFile(songs + "/tiny.song", kept) && kept == tiny);
    // A big honest file, sent the way a browser does: in pieces, and asked to wait for the go-ahead first.
    const std::string big = aWav(9.f);
    a = raw(port, "PUT " + door + "/samples/big.wav HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: " + std::to_string(big.size()) + "\r\n\r\n" + big);
    CHECK(a.status == 200 && readTextFile(samples + "/big.wav", kept) && kept == big);
    // It still answers after all that.
    CHECK(get(port, door + "/list").status == 200);

    // Off: nobody is listening. On again: a new number.
    const std::string oldPin = transfer.pin();
    transfer.stop();
    CHECK(!transfer.running() && get(port, door).status == 0);
    transfer.stop(); // twice is fine
    CHECK(transfer.start(samples, songs, chords, 0, 999));
    CHECK(transfer.pin() != oldPin && get(transfer.port(), "/" + transfer.pin()).status == 200);
    // A port something else has: it says it could not.
    Transfer second;
    CHECK(!second.start(samples, songs, chords, transfer.port(), 1) && !second.running());
}

// Deleting, renaming and moving from the page, and what the page is told about the card.
void testDeletingAndMoving() {
    const std::string root = tempFolder();
    const std::string samples = root + "/samples", songs = root + "/songs", chords = root + "/chords", bounces = root + "/bounces", out = root + "/out";
    for (const std::string& folder : {songs, bounces, out}) mkdir(folder.c_str(), 0755);
    const std::string song = "apotheca-song 1\ntempo 97\n", wav = aWav(), other = aWav(0.2f);
    for (const char* name : {"quiet road.song", "quiet road.1.rev", "quiet road.12.rev", "quiet roadster.song", "quiet roadster.1.rev", "quiet road.x.rev", "loud roads.1.rev"}) writeTextFile(songs + "/" + name, song);
    writeTextFile(bounces + "/mix.wav", wav);
    writeTextFile(root + "/secret.wav", wav); // outside every folder: never to be touched
    writeTextFile(out + "/x.wav", wav);

    Transfer transfer;
    transfer.setBounceFolder(bounces);
    CHECK(transfer.start(samples, songs, chords, 0, 4242));
    const int port = transfer.port();
    const std::string door = "/" + transfer.pin();
    for (const char* name : {"a.wav", "kicks/one.wav", "kicks/two.wav"}) CHECK(put(port, door + "/samples/" + name, wav).status == 200);
    CHECK(put(port, door + "/samples/b.wav", other).status == 200 && put(port, door + "/chords/sent.json", kPack).status == 200);
    mkdir((samples + "/empty").c_str(), 0755);
    unsigned received = transfer.received();
    std::string kept;

    // The list also says how much room is left, which folders there are (empty ones too) and how big each file is.
    Answer a = get(port, door + "/list");
    const std::size_t free = a.body.find("\"free\":");
    CHECK(free != std::string::npos && std::atoll(a.body.c_str() + free + 7) > 0);
    CHECK(a.body.find(",\"folders\":[\"empty\",\"kicks\"],") != std::string::npos);
    const std::string size = std::to_string(wav.size());
    CHECK(a.body.find("\"sizes\":{\"samples\":[" + size + "," + std::to_string(other.size()) + "," + size + "," + size + "],\"songs\":[" + std::to_string(song.size()) + "," + std::to_string(song.size()) + "],\"chords\":[" +
                      std::to_string(std::strlen(kPack)) + "],\"bounces\":[" + size + "]}") != std::string::npos);
    CHECK(a.body.find(",\"samples\":[\"a.wav\",\"b.wav\",\"kicks/one.wav\",\"kicks/two.wav\"],\"songs\":[\"quiet road.song\",\"quiet roadster.song\"],\"chords\":[\"sent.json\"],\"bounces\":[\"mix.wav\"]}") != std::string::npos);
    // A recording and a bounce are sent as sound, so a browser can play them; a song is not.
    CHECK(get(port, door + "/samples/a.wav").head.find("Content-Type: audio/wav") != std::string::npos);
    CHECK(get(port, door + "/bounces/mix.wav").head.find("Content-Type: audio/wav") != std::string::npos);
    CHECK(get(port, door + "/songs/quiet%20road.song").head.find("audio") == std::string::npos);

    // ---- deleting ----
    // Not without the number; not what is not there; not by a name that is not a plain one.
    CHECK(del(port, "/samples/a.wav").status == 403 && del(port, "/0000/samples/a.wav").status == 403 && exists(samples + "/a.wav"));
    CHECK(del(port, door + "/samples/missing.wav").status == 404 && del(port, door + "/samples/kicks/missing.wav").status == 404 && del(port, door + "/samples/nowhere/one.wav").status == 404);
    for (const char* bad : {"/samples/..%2Fsecret.wav", "/samples/../secret.wav", "/samples/kicks/..%2F..%2Fsecret.wav", "/samples/kicks/../../secret.wav", "/samples/..%2Fout/x.wav", "/samples/../out/x.wav",
                            "/samples/kicks%5Cone.wav", "/samples/kicks/sub/one.wav", "/samples/.hid/one.wav", "/samples/kicks/.one.wav", "/samples/kicks", "/samples/kicks/", "/samples/", "/samples",
                            "/songs/quiet%20road.1.rev", "/songs/..%2Fsecret.wav", "/songs/deleted/x.song", "/chords/..%2Fsongs%2Fquiet%20road.song", "/bounces/..%2Fsecret.wav", "/bounces/sub/mix.wav",
                            "/elsewhere/a.wav", "/out/x.wav", "/secret.wav", "/samples/a.wav%00.song", "/songs/a.wav"})
        CHECK(del(port, door + bad).status == 400);
    CHECK(exists(root + "/secret.wav") && exists(out + "/x.wav") && exists(samples + "/kicks/one.wav") && exists(songs + "/quiet road.1.rev") && exists(samples + "/a.wav"));
    // Only a file: not a folder with a file's name, not a link to somewhere else, not a file inside a link.
    mkdir((samples + "/dir.wav").c_str(), 0755);
    CHECK(symlink((root + "/secret.wav").c_str(), (samples + "/link.wav").c_str()) == 0 && symlink(out.c_str(), (samples + "/linked").c_str()) == 0);
    CHECK(del(port, door + "/samples/dir.wav").status == 404 && exists(samples + "/dir.wav"));
    CHECK(del(port, door + "/samples/link.wav").status == 404 && exists(samples + "/link.wav") && exists(root + "/secret.wav"));
    CHECK(del(port, door + "/samples/linked/x.wav").status == 404 && exists(out + "/x.wav"));
    CHECK(transfer.received() == received);
    // One that is there goes. Its folder stays while there is another in it, and goes with the last.
    a = del(port, door + "/samples/kicks/one.wav");
    CHECK(a.status == 200 && a.body == "deleted\n" && !exists(samples + "/kicks/one.wav") && exists(samples + "/kicks/two.wav") && transfer.received() == received + 1);
    CHECK(del(port, door + "/samples/kicks/one.wav").status == 404); // twice is not there the second time
    CHECK(del(port, door + "/samples/kicks%2Ftwo.wav").status == 200 && !exists(samples + "/kicks") && exists(samples));
    CHECK(get(port, door + "/list").body.find("kicks") == std::string::npos);
    // A song goes where the handheld puts a deleted one, with its revs and nobody else's.
    CHECK(del(port, door + "/songs/quiet%20road.song").status == 200 && !exists(songs + "/quiet road.song") && !exists(songs + "/quiet road.1.rev") && !exists(songs + "/quiet road.12.rev"));
    CHECK(readTextFile(songs + "/deleted/quiet road.song", kept) && kept == song && exists(songs + "/deleted/quiet road.1.rev") && exists(songs + "/deleted/quiet road.12.rev"));
    CHECK(exists(songs + "/quiet roadster.song") && exists(songs + "/quiet roadster.1.rev") && exists(songs + "/quiet road.x.rev") && exists(songs + "/loud roads.1.rev")); // that last name is as long as the deleted song's
    CHECK(del(port, door + "/songs/quiet%20road.song").status == 404);
    // A chord pack and a bounce.
    CHECK(del(port, door + "/chords/sent.json").status == 200 && !exists(chords + "/sent.json") && exists(chords));
    CHECK(put(port, door + "/bounces/new.wav", wav).status == 405 && !exists(bounces + "/new.wav")); // nothing is sent there
    CHECK(del(port, door + "/bounces/mix.wav").status == 200 && !exists(bounces + "/mix.wav") && exists(bounces));
    received = transfer.received();

    // ---- renaming and moving a recording ----
    // Refused: no number, nowhere to go, somewhere it may not go, not there, and only recordings.
    CHECK(post(port, "/samples/a.wav", "c.wav").status == 403);
    CHECK(raw(port, "POST " + door + "/samples/a.wav HTTP/1.1\r\nContent-Length: 0\r\n\r\n").status == 400);
    for (const char* bad : {"", "c", "c.json", "c.song", "..%2Fc.wav", "../c.wav", "..%2Fout%2Fc.wav", "../out/c.wav", "/c.wav", "%2Fc.wav", "x/y/c.wav", "x/..%2Fc.wav", "x/../c.wav", ".x/c.wav", "x/.c.wav", "x%5Cc.wav",
                            "x/.wav", "/tmp/c.wav", "a%20folder%20name%20that%20is%20far%20too%20long/c.wav", "c.wav%00", "c%0A.wav", "x:y/c.wav"})
        CHECK(post(port, door + "/samples/a.wav", bad).status == 400);
    CHECK(post(port, door + "/samples/a.wav", "x/" + std::string(33, 'n') + ".wav").status == 400); // 36 at most in a folder
    for (const char* bad : {"/samples/..%2Fsecret.wav", "/samples/../secret.wav", "/samples/x/y/a.wav", "/samples/.a.wav", "/elsewhere/a.wav", "/samples/kicks%5Ca.wav"}) CHECK(post(port, door + bad, "c.wav").status == 400);
    CHECK(post(port, door + "/samples/missing.wav", "c.wav").status == 404 && post(port, door + "/samples/dir.wav", "c.wav").status == 404);
    CHECK(post(port, door + "/samples/link.wav", "c.wav").status == 404 && post(port, door + "/samples/linked/x.wav", "c.wav").status == 404 && exists(out + "/x.wav"));
    CHECK(post(port, door + "/samples/a.wav", "linked/c.wav").status == 400 && !exists(out + "/c.wav")); // not into a link to somewhere else
    CHECK(post(port, door + "/songs/quiet%20roadster.song", "c.wav").status == 405 && exists(songs + "/quiet roadster.song") && !exists(songs + "/c.wav"));
    writeTextFile(bounces + "/mix.wav", wav);
    CHECK(post(port, door + "/bounces/mix.wav", "c.wav").status == 405 && exists(bounces + "/mix.wav"));
    // Nothing is written over.
    a = post(port, door + "/samples/a.wav", "b.wav");
    CHECK(a.status == 409 && readTextFile(samples + "/a.wav", kept) && kept == wav && readTextFile(samples + "/b.wav", kept) && kept == other);
    CHECK(post(port, door + "/samples/a.wav", "dir.wav").status == 409);
    CHECK(exists(samples + "/a.wav") && !exists(samples + "/c.wav") && !exists(root + "/c.wav") && !exists(out + "/c.wav") && !exists(samples + "/x") && transfer.received() == received);
    // A new name; then into a folder that comes into being for it; then to another, and the first folder goes.
    a = post(port, door + "/samples/a.wav", "c%20new.wav");
    CHECK(a.status == 200 && a.body == "moved\n" && !exists(samples + "/a.wav") && readTextFile(samples + "/c new.wav", kept) && kept == wav && transfer.received() == received + 1);
    CHECK(post(port, door + "/samples/c%20new.wav", "new%20place/c.wav").status == 200 && !exists(samples + "/c new.wav") && readTextFile(samples + "/new place/c.wav", kept) && kept == wav);
    CHECK(post(port, door + "/samples/b.wav", "new%20place%2Fb.wav").status == 200 && exists(samples + "/new place/b.wav"));
    CHECK(post(port, door + "/samples/new%20place/c.wav", "empty/c.wav").status == 200 && exists(samples + "/empty/c.wav") && exists(samples + "/new place/b.wav"));
    // The header in small letters, with spaces round what it says; and back out to the loose ones.
    a = raw(port, "POST " + door + "/samples/new%20place/b.wav HTTP/1.1\r\nx-to:   B.WAV  \r\nHost: x\r\n\r\n");
    CHECK(a.status == 200 && readTextFile(samples + "/B.WAV", kept) && kept == other && !exists(samples + "/new place"));
    // To the name it has: nothing happens, and nothing is lost.
    CHECK(post(port, door + "/samples/empty/c.wav", "empty/c.wav").status == 200 && readTextFile(samples + "/empty/c.wav", kept) && kept == wav);
    a = get(port, door + "/list");
    CHECK(a.body.find(",\"samples\":[\"B.WAV\",\"dir.wav\",\"link.wav\",\"empty/c.wav\",\"linked/x.wav\"],") != std::string::npos);
    CHECK(exists(root + "/secret.wav") && exists(out + "/x.wav"));
}

void testTurningItOnFromTheDevice() {
    const std::string root = tempFolder();
    std::unique_ptr<Engine> engine(new Engine);
    Input input;
    App app(*engine);
    Session& session = app.session();
    Uint32 clock = 1000;
    const auto key = [&](SDL_Keycode code) {
        for (bool down : {true, false}) {
            SDL_Event event = {};
            event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.keysym.sym = code;
            ButtonEvent press;
            if (input.handle(event, press)) app.onButton(press, input);
        }
    };
    // With no folders to put files in, it stays off.
    CHECK(!session.toggleTransfer(1) && !session.transferOn());
    session.useSongFolder(root + "/songs", 3);
    session.useSampleFolder(root + "/samples");
    session.useChordFolder(root + "/chords");
    CHECK(session.packs.size() == 1);

    // L1 in the song list turns it on.
    session.view = View::Song;
    app.update(input, clock += 16);
    key(SDLK_q); // L1 outside the list: not this
    CHECK(!session.transferOn());
    key(SDLK_BACKSPACE);
    key(SDLK_q);
    CHECK(session.transferOn() && app.song().listingSongs());
    const std::string address = session.transferAddress();
    const std::size_t colon = address.find(':'), slash = address.find('/');
    CHECK(colon != std::string::npos && slash != std::string::npos);
    const int port = std::atoi(address.c_str() + colon + 1);
    const std::string door = address.substr(slash);
    CHECK(get(port, door).status == 200);
    // A recording sent to it lands in the sample folder, where the kit screen looks.
    CHECK(put(port, door + "/samples/sent.wav", aWav()).status == 200);
    CHECK(session.sampleNames() == std::vector<std::string>({"sent"}));
    // A chord pack sent to it is there to pick from on the next frame.
    CHECK(put(port, door + "/chords/sent.json", kPack).status == 200);
    app.update(input, clock += 16);
    CHECK(session.packs.size() == 2 && session.packs[1].name == "Sent");
    // Deleted from the page, it is gone from the list on the next frame too.
    CHECK(del(port, door + "/chords/sent.json").status == 200);
    app.update(input, clock += 16);
    CHECK(session.packs.size() == 1);
    // A song sent to it is in the list the next time the list is opened.
    CHECK(put(port, door + "/songs/from%20afar.song", "apotheca-song 1\ntrack drums\nscene 1\nhit 1 1 1\n").status == 200);
    bool listed = false;
    for (const std::string& name : session.songNames()) listed = listed || name == "from afar";
    CHECK(listed);
    // L1 again: off, and nobody is listening.
    key(SDLK_q);
    CHECK(!session.transferOn() && get(port, door).status == 0);
    // On, then the session goes away: it is turned off with it.
    int lastPort = 0;
    std::string lastDoor;
    {
        std::unique_ptr<Engine> other(new Engine);
        App second(*other);
        second.session().useSongFolder(root + "/songs", 3);
        second.session().useSampleFolder(root + "/samples");
        CHECK(second.session().toggleTransfer(7));
        const std::string a = second.session().transferAddress();
        lastPort = std::atoi(a.c_str() + a.find(':') + 1);
        lastDoor = a.substr(a.find('/'));
        CHECK(get(lastPort, lastDoor).status == 200);
    }
    CHECK(get(lastPort, lastDoor).status == 0);
}

// A file that passes for a program on this machine (or another), of a size.
std::string aProgram(unsigned machine, std::size_t size = 200 * 1024) {
    std::string b(size, '\0');
    b[0] = 0x7F; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
    b[4] = 2;  // 64-bit
    b[5] = 1;  // low byte first
    b[16] = 3; // a program that can be loaded anywhere
    b[18] = static_cast<char>(machine & 255);
    b[19] = static_cast<char>(machine >> 8);
    for (std::size_t i = 64; i < size; ++i) b[i] = static_cast<char>(i * 31);
    return b;
}
#if defined(__aarch64__)
const unsigned kThisMachine = 183, kAnotherMachine = 62;
#else
const unsigned kThisMachine = 62, kAnotherMachine = 183;
#endif

void testUpdatingOverWiFi() {
    // What counts as a program for this machine.
    const std::string program = aProgram(kThisMachine);
    CHECK(isProgramForThisMachine(program));
    CHECK(!isProgramForThisMachine(aProgram(kAnotherMachine)));     // for a different processor
    CHECK(!isProgramForThisMachine(aProgram(kThisMachine, 4000)));  // far too small to be Apotheca
    CHECK(!isProgramForThisMachine("") && !isProgramForThisMachine(std::string(200000, 'x')) && !isProgramForThisMachine(aWav(3.f)));
    for (std::size_t at : {0u, 1u, 3u, 4u, 5u, 16u}) {
        std::string spoiled = program;
        spoiled[at] = 9;
        CHECK(!isProgramForThisMachine(spoiled));
    }

    const std::string root = tempFolder();
    const std::string programs = root + "/apotheca";
    mkdir(programs.c_str(), 0755);
    writeTextFile(programs + "/apotheca", "the one running now");
    writeTextFile(programs + "/apotheca.pending", "left over from last time");

    std::unique_ptr<Engine> engine(new Engine);
    Input input;
    App app(*engine);
    Session& session = app.session();
    Uint32 clock = 1000;
    const auto key = [&](SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    };
    const auto tap = [&](SDL_Keycode code) { key(code, true); key(code, false); };
    const auto frames = [&](int count) { for (int i = 0; i < count; ++i) app.update(input, clock += 16); };
    const SDL_Keycode A = SDLK_x, B = SDLK_z, START = SDLK_RETURN;
    session.useSongFolder(root + "/songs", 3);
    session.useSampleFolder(root + "/samples");

    // Before the program's folder is known, an update is refused.
    CHECK(session.toggleTransfer(5));
    std::string address = session.transferAddress();
    int port = std::atoi(address.c_str() + address.find(':') + 1);
    std::string door = address.substr(address.find('/'));
    CHECK(put(port, door + "/update/apotheca", program).status == 400);
    session.toggleTransfer(5);
    session.useProgramFolder(programs);
    CHECK(!exists(programs + "/apotheca.pending")); // one never answered last time does not linger
    CHECK(session.toggleTransfer(6));
    address = session.transferAddress();
    port = std::atoi(address.c_str() + address.find(':') + 1);
    door = address.substr(address.find('/'));

    // What is refused: no number, the wrong processor, not a program, too big, the wrong way of asking.
    CHECK(put(port, "/update/apotheca", program).status == 403);
    CHECK(put(port, door + "/update/apotheca", aProgram(kAnotherMachine)).status == 400);
    CHECK(put(port, door + "/update/apotheca", "#!/bin/sh\nrm -rf /\n").status == 400);
    CHECK(put(port, door + "/update/apotheca", aWav()).status == 400);
    CHECK(raw(port, "PUT " + door + "/update/apotheca HTTP/1.1\r\nContent-Length: 40000000\r\n\r\n\x7f" "ELF").status == 413);
    CHECK(raw(port, "PUT " + door + "/update/apotheca HTTP/1.1\r\n\r\n" + program).status == 411);
    CHECK(get(port, door + "/update/apotheca").status == 405);
    CHECK(put(port, door + "/update/other", program).status == 400 && put(port, door + "/update/..%2Fapotheca", program).status == 400);
    frames(2);
    CHECK(!session.updateWaiting() && !exists(programs + "/apotheca.pending") && !exists(programs + "/apotheca.new"));

    // A real one is only set aside. Nothing is in place for the next start yet.
    Answer a = put(port, door + "/update/apotheca", program);
    CHECK(a.status == 200 && a.body.find("hold A") != std::string::npos);
    std::string kept;
    CHECK(readTextFile(programs + "/apotheca.pending", kept) && kept == program);
    CHECK(!exists(programs + "/apotheca.new"));
    CHECK(readTextFile(programs + "/apotheca", kept) && kept == "the one running now");
    frames(1);
    CHECK(session.updateWaiting() && session.updateMegabytes() > 0.19f && session.updateMegabytes() < 0.21f);

    // The handheld asks, and nothing else happens until it is answered.
    session.view = View::Pattern;
    tap(START);
    CHECK(!app.project().playing);
    tap(SDLK_RIGHT);
    CHECK(app.drums().cursorStep() == 0);
    // A tap of A is not enough; nor is holding it for most of a second.
    tap(A);
    frames(80);
    CHECK(session.updateWaiting() && !exists(programs + "/apotheca.new") && app.drums().level(0, 0) == 0.f);
    key(A, true);
    frames(50);
    key(A, false);
    frames(80);
    CHECK(session.updateWaiting() && !exists(programs + "/apotheca.new"));
    // B throws it away.
    tap(B);
    CHECK(!session.updateWaiting() && !exists(programs + "/apotheca.pending") && !exists(programs + "/apotheca.new"));
    frames(2);
    tap(SDLK_RIGHT); // the buttons are the screen's again
    CHECK(app.drums().cursorStep() == 1 && app.updateSaid() == 0);

    // Sent again, and A held for the whole second: accepted. It is in place
    // for the next start; the one running is untouched.
    const std::string newer = aProgram(kThisMachine, 300 * 1024);
    CHECK(put(port, door + "/update/apotheca", program).status == 200);
    CHECK(put(port, door + "/update/apotheca", newer).status == 200); // a second one before any answer takes the first one's place
    frames(1);
    CHECK(session.updateWaiting());
    key(A, true);
    frames(61);
    CHECK(session.updateWaiting());
    frames(4);
    CHECK(!session.updateWaiting() && app.updateSaid() == 1);
    key(A, false);
    CHECK(readTextFile(programs + "/apotheca.new", kept) && kept == newer && !exists(programs + "/apotheca.pending"));
    CHECK(readTextFile(programs + "/apotheca", kept) && kept == "the one running now");
    CHECK(app.drums().level(0, 1) == 0.f); // letting go of A did not place a hit
    frames(320);
    CHECK(app.updateSaid() == 0); // the message goes after a few seconds

    // One nobody answers goes when WiFi is turned off.
    CHECK(put(port, door + "/update/apotheca", program).status == 200);
    frames(1);
    CHECK(session.updateWaiting());
    session.toggleTransfer(1);
    CHECK(!session.updateWaiting() && !exists(programs + "/apotheca.pending"));
    // The volume is kept beside the program, and comes back at the next start.
    session.loadVolume(0.4f);
    CHECK(std::fabs(session.volume() - 0.4f) < 1e-4f && !exists(programs + "/volume.txt")); // nothing kept yet: the quiet default
    session.setVolume(0.7f);
    CHECK(readTextFile(programs + "/volume.txt", kept) && kept == "70\n");
    {
        std::unique_ptr<Engine> next(new Engine);
        App later(*next);
        later.session().useProgramFolder(programs);
        later.session().loadVolume(0.4f);
        CHECK(std::fabs(later.session().volume() - 0.7f) < 1e-4f);
    }
    writeTextFile(programs + "/volume.txt", "nonsense");
    session.loadVolume(0.4f);
    CHECK(session.volume() == 0.f || std::fabs(session.volume() - 0.4f) < 1e-4f); // rubbish in the file is not loud
    // The program tells the launcher "this start worked" by taking its mark away.
    writeTextFile(programs + "/update-trying", "");
    session.startedFine();
    CHECK(!exists(programs + "/update-trying"));
}

} // namespace

// A card that will not rename a file into another folder: the move still happens, the long way.
static void testMovingWhereRenameIsRefused() {
    const std::string root = tempFolder(), samples = root + "/samples", songs = root + "/songs", chords = root + "/chords";
    mkdir(samples.c_str(), 0755);
    mkdir((samples + "/kicks").c_str(), 0755);
    const std::string wav = aWav();
    writeTextFile(samples + "/thud.wav", wav);
    writeTextFile(samples + "/kicks/there.wav", wav);
    Transfer transfer;
    CHECK(transfer.start(samples, songs, chords, 0, 4242));
    const int port = transfer.port();
    const std::string door = "/" + transfer.pin();
    Transfer::renameBroken = true;
    Answer a = raw(port, "POST " + door + "/samples/thud.wav HTTP/1.1\r\nX-To: kicks/thud.wav\r\nContent-Length: 0\r\n\r\n");
    std::string kept;
    CHECK(a.status == 200 && a.body.find("moved") == 0);
    CHECK(!exists(samples + "/thud.wav") && readTextFile(samples + "/kicks/thud.wav", kept) && kept == wav);
    // A name that is taken is still refused, and nothing is written over or lost.
    writeTextFile(samples + "/other.wav", wav + "x");
    a = raw(port, "POST " + door + "/samples/other.wav HTTP/1.1\r\nX-To: kicks/there.wav\r\nContent-Length: 0\r\n\r\n");
    CHECK(a.status == 409 && exists(samples + "/other.wav") && readTextFile(samples + "/kicks/there.wav", kept) && kept == wav);
    // Out of a folder, and into a new one: the emptied folder goes, the new one is made.
    a = raw(port, "POST " + door + "/samples/kicks/there.wav HTTP/1.1\r\nX-To: fresh/there.wav\r\nContent-Length: 0\r\n\r\n");
    CHECK(a.status == 200 && exists(samples + "/fresh/there.wav") && !exists(samples + "/kicks/there.wav"));
    Transfer::renameBroken = false;
    transfer.stop();
}


// A recording renamed, moved or deleted from the page while a song that uses it is open.
static void testTheOpenSongFollows() {
    const std::string root = tempFolder(), samples = root + "/samples";
    mkdir(samples.c_str(), 0755);
    mkdir((samples + "/kicks").c_str(), 0755);
    const auto steady = [](float seconds) { return std::vector<float>(static_cast<std::size_t>(seconds * 48000.f), 0.25f); };
    writeWav(samples + "/thud.wav", steady(0.5f), 48000);
    writeWav(samples + "/kicks/snap.wav", steady(0.2f), 48000);
    writeWav(samples + "/pad.wav", steady(0.3f), 48000);
    std::unique_ptr<Engine> engine(new Engine);
    Input input;
    App app(*engine);
    Session& session = app.session();
    Project& project = app.project();
    Uint32 clock = 1000;
    // A frame of the screen and some sound with it: a test that never renders never empties the queue.
    const auto frames = [&](int count) {
        std::vector<float> out(2 * 512);
        for (int i = 0; i < count; ++i) {
            app.update(input, clock += 16);
            engine->render(out.data(), 512);
        }
    };
    // How long a lane (or, with a key, a sampler track's note) sounds for, in frames of sound.
    const auto sounds = [&](int track, int lane, int key = -1) {
        std::vector<float> out(2 * 100000);
        engine->render(out.data(), 100000); // let anything before die away
        Command c;
        c.type = key < 0 ? Command::Trigger : Command::NoteOn;
        c.track = track;
        c.a = key < 0 ? lane : key;
        c.value = 1.f;
        engine->send(c);
        engine->render(out.data(), 100000);
        int first = -1, last = -1;
        for (int i = 0; i < 100000; ++i) {
            if (out[2 * static_cast<std::size_t>(i)] == 0.f) continue;
            if (first < 0) first = i;
            last = i;
        }
        return first < 0 ? 0 : last - first + 1;
    };
    session.useSongFolder(root + "/songs", 3);
    session.useSampleFolder(samples);
    session.useChordFolder(root + "/chords");
    const int drum0 = sounds(0, 0), drum2 = sounds(0, 2); // the built-in drums on those lanes
    session.setLaneSample(0, 0, "thud");
    session.setLaneSample(0, 1, "thud");
    session.setLaneSample(0, 2, "kicks/snap");
    session.setLaneSample(0, 3, "pad");
    session.setLaneSample(0, 5, "never was");
    session.setLaneSample(0, kDrumLanes - 1, "thud"); // the last lane there is: a loop that stops one short misses it
    CHECK(session.addTrack(TrackKind::Sampler) && session.addTrack(TrackKind::Sampler) && session.addTrack(TrackKind::Phazed));
    session.setTrackSample(1, "pad");
    session.setTrackSample(2, "thud");
    std::snprintf(project.tracks[3].laneSample[0], sizeof project.tracks[3].laneSample[0], "thud"); // left over on a track that is no drum machine: not a lane
    std::snprintf(project.tracks[3].sample, sizeof project.tracks[3].sample, "thud");
    frames(4);
    const int thud = sounds(0, 0), snap = sounds(0, 2), pad = sounds(0, 3), padNote = sounds(1, 0, 60);
    CHECK(std::abs(thud - 24000) < 600 && std::abs(snap - 9600) < 600 && std::abs(pad - 14400) < 600 && padNote > 4800);
    CHECK(thud != drum0 && snap != drum2);
    CHECK(session.laneSound(0, 5).missing && !session.laneSound(0, 0).missing);
    const auto lane = [&](int l) { return std::string(project.tracks[0].laneSample[l]); };
    const auto version = [&](int t, int l) { return session.laneSound(t, l).version; };

    CHECK(session.toggleTransfer(9));
    const std::string address = session.transferAddress();
    const int port = std::atoi(address.c_str() + address.find(':') + 1);
    const std::string door = address.substr(address.find('/'));

    // Moved into a folder under a new name: both lanes and the sampler track that used it use the
    // new name on the next frame. The others are as they were.
    const unsigned v0 = version(0, 0), v1 = version(0, 1), v2 = version(0, 2), vs = version(2, 0);
    const std::shared_ptr<const std::vector<float>> held = session.laneSound(0, 0).recording;
    CHECK(post(port, door + "/samples/thud.wav", "kicks/big%20boom.wav").status == 200);
    CHECK(lane(0) == "thud"); // not from the server's thread: when the program next looks
    frames(1);
    CHECK(lane(0) == "kicks/big boom" && lane(1) == "kicks/big boom" && lane(2) == "kicks/snap" && lane(3) == "pad" && lane(5) == "never was");
    CHECK(std::string(project.tracks[2].sample) == "kicks/big boom" && std::string(project.tracks[1].sample) == "pad");
    CHECK(lane(kDrumLanes - 1) == "kicks/big boom" && !session.laneSound(0, kDrumLanes - 1).missing && sounds(0, kDrumLanes - 1) == thud);
    CHECK(std::string(project.tracks[3].laneSample[0]) == "thud" && std::string(project.tracks[3].sample) == "thud");
    // Nothing was read again: the very sound that was playing plays on.
    CHECK(version(0, 0) == v0 && version(0, 1) == v1 && version(0, 2) == v2 && version(2, 0) == vs && session.laneSound(0, 0).recording == held);
    CHECK(!session.laneSound(0, 0).missing && !session.laneSound(0, 1).missing && !session.trackSound(2).missing);
    CHECK(sounds(0, 0) == thud && sounds(0, 1) == thud && sounds(0, 2) == snap && std::abs(sounds(2, 0, 60) - sounds(1, 0, 60) * 5 / 3) < 2400);
    // The song is kept with the new name, as any change is: within a couple of seconds.
    frames(140);
    std::string kept;
    CHECK(readTextFile(root + "/songs/" + session.songName() + ".song", kept));
    CHECK(kept.find("\nsample 1 0 kicks/big boom\n") != std::string::npos && kept.find("\nsample 2 0 kicks/big boom\n") != std::string::npos);
    CHECK(kept.find("thud") == std::string::npos && kept.find("\nsample 6 0 never was\n") != std::string::npos);
    {
        // Opened afresh, it finds its recordings.
        std::unique_ptr<Engine> otherEngine(new Engine);
        App other(*otherEngine);
        other.session().useSampleFolder(samples);
        other.session().useSongFolder(root + "/songs", 3);
        CHECK(std::string(other.project().tracks[0].laneSample[1]) == "kicks/big boom");
        CHECK(!other.session().laneSound(0, 0).missing && !other.session().laneSound(0, 1).missing && !other.session().trackSound(2).missing);
        CHECK(other.session().laneSound(0, 5).missing);
    }
    // Renamed where it is, then again before the program has looked: it follows both, in order.
    CHECK(post(port, door + "/samples/pad.wav", "warm.wav").status == 200);
    CHECK(post(port, door + "/samples/warm.wav", "pads/warm.wav").status == 200);
    frames(1);
    CHECK(lane(3) == "pads/warm" && std::string(project.tracks[1].sample) == "pads/warm" && lane(0) == "kicks/big boom");
    CHECK(sounds(0, 3) == pad && sounds(1, 0, 60) == padNote && !session.trackSound(1).missing);
    // One that was refused changes nothing.
    CHECK(post(port, door + "/samples/kicks/snap.wav", "pads/warm.wav").status == 409);
    CHECK(post(port, door + "/samples/nothing.wav", "kicks/snap2.wav").status == 404);
    frames(1);
    CHECK(lane(2) == "kicks/snap" && lane(3) == "pads/warm");

    // Deleted from the page: the lane keeps the name and says the file is missing, now and not
    // only when the song is next opened. The built-in drum plays. The sampler track is silent.
    CHECK(del(port, door + "/samples/kicks/snap.wav").status == 200 && del(port, door + "/samples/pads/warm.wav").status == 200);
    frames(1);
    CHECK(lane(2) == "kicks/snap" && session.laneSound(0, 2).missing && sounds(0, 2) == drum2);
    CHECK(lane(3) == "pads/warm" && session.laneSound(0, 3).missing);
    CHECK(std::string(project.tracks[1].sample) == "pads/warm" && session.trackSound(1).missing && sounds(1, 0, 60) == 0);
    CHECK(!session.laneSound(0, 0).missing && sounds(0, 0) == thud && !session.trackSound(2).missing); // the others are untouched
    // Sent again by that name, it is back on the next frame. One sent by another name wakes nothing.
    const unsigned before = version(0, 0), waiting = version(0, 3);
    CHECK(put(port, door + "/samples/kicks/snap.wav", wavBytes(steady(0.1f), 48000)).status == 200);
    CHECK(put(port, door + "/samples/kicks/big%20boom.wav", wavBytes(steady(0.1f), 48000)).status == 200);
    frames(1);
    CHECK(!session.laneSound(0, 2).missing && std::abs(sounds(0, 2) - 4800) < 600);
    CHECK(session.laneSound(0, 3).missing && session.trackSound(1).missing && session.laneSound(0, 5).missing);
    CHECK(version(0, 3) == waiting); // and one still missing is not looked for each time some other file arrives
    CHECK(version(0, 0) == before && sounds(0, 0) == thud); // one that is playing is not read again when its file is sent over
    // A missing one whose file turns up under its name by a rename is read too.
    CHECK(put(port, door + "/samples/stray.wav", wavBytes(steady(0.4f), 48000)).status == 200);
    CHECK(post(port, door + "/samples/stray.wav", "never%20was.wav").status == 200);
    frames(1);
    CHECK(lane(5) == "never was" && !session.laneSound(0, 5).missing && std::abs(sounds(0, 5) - 19200) < 600);
    // Each change is followed once. A lane put back on the old name by hand stays there when the next thing happens.
    CHECK(put(port, door + "/samples/thud.wav", wavBytes(steady(0.1f), 48000)).status == 200);
    frames(1);
    session.setLaneSample(0, 4, "thud");
    CHECK(put(port, door + "/samples/other.wav", wavBytes(steady(0.1f), 48000)).status == 200);
    frames(1);
    CHECK(lane(4) == "thud" && lane(0) == "kicks/big boom");
    session.setLaneSample(0, 4, "");
    // A file ending .WAV is known by the same name.
    CHECK(post(port, door + "/samples/never%20was.wav", "LOUD.WAV").status == 200);
    frames(1);
    CHECK(lane(5) == "LOUD" && !session.laneSound(0, 5).missing);
    session.toggleTransfer(9);

    // Told directly: names that are no recording's change nothing, and an empty lane is not "sent" one.
    session.recordingChanged("LOUD", "../outside");
    session.recordingChanged("LOUD", "a/b/c");
    session.recordingChanged("", "");
    session.recordingChanged("kicks/../x", "fine");
    CHECK(lane(5) == "LOUD" && lane(4).empty() && lane(0) == "kicks/big boom");
    session.setLaneSample(0, 4, "../x"); // however a lane came by such a name, it is not followed out of the folder
    session.recordingChanged("../x", "fine");
    CHECK(lane(4) == "../x");
    session.setLaneSample(0, 4, "");
    const unsigned v4 = version(0, 4);
    session.recordingChanged("", "LOUD");
    session.recordingChanged("", "something");
    CHECK(lane(4).empty() && version(0, 4) == v4 && !session.laneSound(0, 4).missing);
    frames(2);
}


// A song's lines that name a recording, changed where they stand and nothing else.
static void testRenamingInSongText() {
    typedef std::vector<std::pair<std::string, std::string>> Renames;
    const Renames one = {{"thud", "kicks/big boom"}};
    const std::string before = "apotheca-song 1\ntempo 97\nmystery thud\nsound thud\nsample 1 0 thud\ntrack drums\nsample 1 0 thud\nsample 2 16 kicks/snap\n"
                               "sample 8 4 thud\nsample 9 0 thud\nsample 0 0 thud\nsample 3 0 thudx\nsample 4 0 x/thud\nsample 6 0 thud \nsample 7 thud\nsound thud\n"
                               "future 1 2 thud\n\n  sample   5   0   thud\ntrack sampler\nsound thud\nsample 1 0 thud\nsampler 60 0 0\ntrack phazed\nsound thud\n"
                               "sample 1 0 thud\ntrack fromthefuture\nsound thud\nsample 1 0 thud\ntrack sampler\nsound  thud\r\ntrack sampler\nsound\ttwo words/thud\n"
                               "track sampler\nsound thud";
    const std::string after = "apotheca-song 1\ntempo 97\nmystery thud\nsound thud\nsample 1 0 thud\ntrack drums\nsample 1 0 kicks/big boom\nsample 2 16 kicks/snap\n"
                              "sample 8 4 kicks/big boom\nsample 9 0 thud\nsample 0 0 thud\nsample 3 0 thudx\nsample 4 0 x/thud\nsample 6 0 thud \nsample 7 thud\nsound thud\n"
                              "future 1 2 thud\n\n  sample   5   0   kicks/big boom\ntrack sampler\nsound kicks/big boom\nsample 1 0 thud\nsampler 60 0 0\ntrack phazed\nsound thud\n"
                              "sample 1 0 thud\ntrack fromthefuture\nsound thud\nsample 1 0 thud\ntrack sampler\nsound  thud\r\ntrack sampler\nsound\ttwo words/thud\n"
                              "track sampler\nsound kicks/big boom"; // and no line end is added to a file that had none
    std::string text = before;
    CHECK(renameRecordings(text, one) && text == after);
    // What the program reads from it is the same song with the new name.
    std::unique_ptr<Project> was(new Project), now(new Project);
    CHECK(parseSong(before, *was) && parseSong(after, *now));
    CHECK(std::string(was->tracks[0].laneSample[0]) == "thud" && std::string(now->tracks[0].laneSample[0]) == "kicks/big boom");
    CHECK(std::string(now->tracks[0].laneSample[7]) == "kicks/big boom" && now->tracks[0].laneFit[7] == 4 && std::string(now->tracks[0].laneSample[4]) == "kicks/big boom");
    CHECK(std::string(now->tracks[1].sample) == "kicks/big boom" && std::string(now->tracks[0].laneSample[1]) == "kicks/snap" && now->tracks[0].laneFit[1] == 16);
    // Nothing names it: nothing changes, and it says so.
    text = after;
    CHECK(!renameRecordings(text, one) && text == after);
    CHECK(!renameRecordings(text, Renames()) && text == after);
    // Two in a row are followed in order; one that comes round to where it began is no change.
    text = "apotheca-song 1\ntrack sampler\nsound pad\ntrack sampler\nsound warm\n";
    CHECK(renameRecordings(text, Renames{{"pad", "warm"}, {"warm", "pads/warm"}}));
    CHECK(text == "apotheca-song 1\ntrack sampler\nsound pads/warm\ntrack sampler\nsound pads/warm\n");
    CHECK(!renameRecordings(text, Renames{{"pads/warm", "x"}, {"x", "pads/warm"}}));
    // Not a song: left alone.
    text = "track drums\nsample 1 0 thud\n";
    CHECK(!renameRecordings(text, one) && text == "track drums\nsample 1 0 thud\n");
    text = "";
    CHECK(!renameRecordings(text, one) && text.empty());
    // A thirteenth track is not read, so it is not changed either.
    std::string many = "apotheca-song 1\n";
    for (int t = 0; t < kNumTracks - 1; ++t) many += "track phazed\n";
    const std::string twelve = many + "track sampler\nsound thud\n", thirteen = many + "track phazed\ntrack sampler\nsound thud\n";
    text = twelve;
    CHECK(renameRecordings(text, one) && text == many + "track sampler\nsound kicks/big boom\n");
    text = thirteen;
    CHECK(!renameRecordings(text, one) && text == thirteen);
    // A track of a kind this version does not have takes no place among the twelve.
    text = "apotheca-song 1\ntrack fromthefuture\n" + twelve.substr(16);
    CHECK(renameRecordings(text, one) && text.find("sound kicks/big boom\n") != std::string::npos);
}

namespace {
ino_t fileId(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 ? info.st_ino : 0;
}
time_t fileDate(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 ? info.st_mtime : 0;
}
std::string contents(const std::string& path) {
    std::string text;
    return readTextFile(path, text) ? text : "<no file>";
}
void setDate(const std::string& path, time_t when) {
    utimbuf times;
    times.actime = times.modtime = when;
    utime(path.c_str(), &times);
}
} // namespace

// A recording moved while its song was shut is found again when the song is opened: by its base
// name, and only if exactly one recording has it.
static void testFoundOnLoad() {
    const std::string root = tempFolder(), samples = root + "/samples", songs = root + "/songs";
    mkdir(samples.c_str(), 0755);
    for (const char* folder : {"kicks", "a", "b", "x", "y", "od..d", "pads", "solo", "dup"}) mkdir((samples + "/" + folder).c_str(), 0755);
    const auto steady = [](float seconds) { return std::vector<float>(static_cast<std::size_t>(seconds * 48000.f), 0.25f); };
    const auto wav = [&](const std::string& name, float seconds) { CHECK(writeWav(samples + "/" + name + ".wav", steady(seconds), 48000)); };
    const auto move = [&](const std::string& from, const std::string& to) { CHECK(std::rename((samples + "/" + from + ".wav").c_str(), (samples + "/" + to + ".wav").c_str()) == 0); };
    for (const char* name : {"thud", "snap", "gone", "kicks/deep", "bad", "lone", "pad", "odd", "solo/one", "dup/one", "fine"}) wav(name, 0.5f);
    std::string song, clean;
    {
        std::unique_ptr<Engine> engine(new Engine);
        App app(*engine);
        Session& session = app.session();
        session.useSampleFolder(samples);
        session.useSongFolder(songs, 3);
        session.setLaneSample(0, 0, "thud");
        session.setLaneFit(0, 0, 16);
        session.setLaneSample(0, 1, "snap");
        session.setLaneSample(0, 2, "gone");
        session.setLaneSample(0, 3, "kicks/deep");
        session.setLaneSample(0, 4, "bad");
        session.setLaneSample(0, 5, "lone");
        session.setLaneSample(0, 6, "odd");
        session.setLaneSample(0, kDrumLanes - 1, "thud");
        CHECK(session.addTrack(TrackKind::Sampler) && session.addTrack(TrackKind::Sampler) && session.addTrack(TrackKind::Drums) && session.addTrack(TrackKind::Phazed));
        session.setTrackSample(1, "pad");
        session.setTrackSample(2, "snap");
        session.setLaneSample(3, 0, "solo/one");
        session.setLaneSample(3, kDrumLanes - 1, "kicks/deep");
        session.finishStretching();
        session.saveNow();
        CHECK(session.saveRev() == 1);
        song = session.songName();
        CHECK(session.sampleLookups() == 0); // nothing was missing: the folder was never listed
        // And a song with every recording where it says.
        session.newSong();
        session.setLaneSample(0, 0, "fine");
        session.saveNow();
        clean = session.songName();
        session.newSong();
    }
    CHECK(contents(songs + "/" + song + ".song").find("\nsample 1 16 thud\n") != std::string::npos);
    // While it is shut, on the card:
    move("thud", "kicks/thud");       // into a folder
    move("kicks/deep", "deep");       // and out of one
    move("pad", "pads/pad");
    move("snap", "a/snap");           // two of this name now: no telling which
    wav("b/snap", 0.2f);
    std::remove((samples + "/gone.wav").c_str()); // none of this name
    CHECK(writeTextFile(samples + "/bad.wav", "not a recording")); // its own file will not read: another of its name is not put in its place
    wav("x/bad", 0.2f);
    std::remove((samples + "/lone.wav").c_str());
    CHECK(writeTextFile(samples + "/y/lone.wav", "not a recording")); // the only one of its name will not read
    move("odd", "od..d/odd");         // in a folder a song may not name
    writeTextFile(root + "/songs/last.txt", song + "\n");
    const std::string asKept = contents(songs + "/" + song + ".song");

    std::unique_ptr<Engine> engine(new Engine);
    Input input;
    App app(*engine);
    Session& session = app.session();
    Project& project = app.project();
    Uint32 clock = 1000;
    const auto frames = [&](int count) {
        std::vector<float> out(2 * 512);
        for (int i = 0; i < count; ++i) {
            app.update(input, clock += 16);
            engine->render(out.data(), 512);
        }
    };
    const auto lane = [&](int l, int t = 0) { return std::string(project.tracks[t].laneSample[l]); };
    const auto missing = [&](int l, int t = 0) { return session.laneSound(t, l).missing; };
    const auto check = [&] {
        CHECK(lane(0) == "kicks/thud" && !missing(0) && std::abs(session.laneSound(0, 0).seconds - 0.5f) < 0.01f && project.tracks[0].laneFit[0] == 16);
        CHECK(lane(kDrumLanes - 1) == "kicks/thud" && !missing(kDrumLanes - 1));
        CHECK(lane(3) == "deep" && !missing(3) && lane(kDrumLanes - 1, 3) == "deep" && !missing(kDrumLanes - 1, 3));
        CHECK(std::string(project.tracks[1].sample) == "pads/pad" && !session.trackSound(1).missing && std::abs(session.trackSound(1).seconds - 0.5f) < 0.01f);
        CHECK(lane(1) == "snap" && missing(1) && std::string(project.tracks[2].sample) == "snap" && session.trackSound(2).missing); // two: left as it is
        CHECK(lane(2) == "gone" && missing(2));  // none
        CHECK(lane(4) == "bad" && missing(4));   // its own file is there, and bad
        CHECK(lane(5) == "lone" && missing(5));  // the one found would not read: the name it had
        CHECK(lane(6) == "odd" && missing(6));   // not into a folder a song cannot name
        CHECK(lane(0, 3) == "solo/one" && !missing(0, 3)); // one that is where it says is not moved to another of its name
    };
    session.useSampleFolder(samples);
    session.useSongFolder(songs, 3);
    CHECK(session.songName() == song);
    check();
    CHECK(session.sampleLookups() == 1); // the folder was listed once for all of them
    CHECK(!session.stretching() || (session.finishStretching(), true));
    // The song is saved with the new names the way any change is: not at once, within a couple of seconds.
    CHECK(contents(songs + "/" + song + ".song") == asKept);
    frames(140);
    const std::string kept = contents(songs + "/" + song + ".song");
    CHECK(kept.find("\nsample 1 16 kicks/thud\n") != std::string::npos && kept.find("\nsample 8 0 kicks/thud\n") != std::string::npos);
    CHECK(kept.find("\nsample 4 0 deep\n") != std::string::npos && kept.find("\nsound pads/pad\n") != std::string::npos);
    CHECK(kept.find("\nsample 2 0 snap\n") != std::string::npos && kept.find("\nsound snap\n") != std::string::npos && kept.find("\nsample 3 0 gone\n") != std::string::npos);
    CHECK(kept.find("\nsample 5 0 bad\n") != std::string::npos && kept.find("\nsample 6 0 lone\n") != std::string::npos && kept.find("\nsample 7 0 odd\n") != std::string::npos);
    // A rev kept before the move asks for the old names: opened, it finds them too.
    CHECK(contents(songs + "/" + song + ".1.rev").find("\nsample 1 16 thud\n") != std::string::npos);
    CHECK(session.openRev(1));
    check();
    CHECK(session.sampleLookups() == 2);
    frames(2);
    // A song with nothing missing does not list the folder: opened, made new, or come back to.
    CHECK(session.openSong(clean) && lane(0) == "fine" && !missing(0));
    session.newSong();
    frames(2);
    CHECK(session.sampleLookups() == 2);
    // One with something still missing lists it once each time it is opened.
    CHECK(session.openSong(song));
    check();
    CHECK(session.sampleLookups() == 3);
    frames(2);

    // Asked directly. A name a song may not have is not looked for, whatever is on the card.
    wav("up", 0.2f);
    session.setLaneSample(0, 6, "x/../../up"); // however a lane came by such a name
    CHECK(missing(6) && session.findMissing() == 0 && lane(6) == "x/../../up");
    session.setLaneSample(0, 6, "odd");
    // Only those of the base name asked for.
    wav("x/gone", 0.3f);
    wav("y/odd", 0.3f);
    CHECK(session.findMissing("odd") == 1 && lane(6) == "y/odd" && !missing(6) && lane(2) == "gone" && missing(2));
    CHECK(session.findMissing("nothing of this name") == 0);
    const int looked = session.sampleLookups();
    CHECK(session.findMissing("fine") == 0 && session.sampleLookups() == looked); // nothing of that name is missing: no listing
    CHECK(session.findMissing() == 1 && lane(2) == "x/gone" && !missing(2) && std::abs(session.laneSound(0, 2).seconds - 0.3f) < 0.01f);
    CHECK(lane(1) == "snap" && missing(1) && session.sampleLookups() == looked + 1);
    session.setLaneSample(0, 2, "gone");
    session.setLaneSample(0, 6, "odd");
    std::remove((samples + "/x/gone.wav").c_str());
    std::remove((samples + "/y/odd.wav").c_str());
    frames(2);

    // With the song open: a recording of a missing one's base name that arrives from the page, in
    // whatever folder, is taken if it is the only one. One of a name that has two is not.
    CHECK(session.toggleTransfer(9));
    const std::string address = session.transferAddress();
    const int port = std::atoi(address.c_str() + address.find(':') + 1);
    const std::string door = address.substr(address.find('/'));
    const unsigned playing = session.laneSound(0, 0).version;
    // One of another name arriving wakes nothing: a missing one is not looked for each time some file comes.
    wav("y/odd", 0.3f); // put on the card by hand: nobody said
    CHECK(put(port, door + "/samples/late/fine.wav", wavBytes(steady(0.1f), 48000)).status == 200);
    frames(1);
    CHECK(lane(6) == "odd" && missing(6));
    std::remove((samples + "/y/odd.wav").c_str());
    CHECK(put(port, door + "/samples/late/gone.wav", wavBytes(steady(0.1f), 48000)).status == 200);
    CHECK(put(port, door + "/samples/late/snap.wav", wavBytes(steady(0.1f), 48000)).status == 200);
    frames(1);
    CHECK(lane(2) == "late/gone" && !missing(2) && std::abs(session.laneSound(0, 2).seconds - 0.1f) < 0.01f);
    CHECK(lane(1) == "snap" && missing(1) && session.trackSound(2).missing && lane(4) == "bad" && missing(4));
    CHECK(session.laneSound(0, 0).version == playing); // those that are playing are not read again
    // Moved on the page under a missing one's base name: taken too.
    wav("stray", 0.3f);
    CHECK(post(port, door + "/samples/stray.wav", "late/odd.wav").status == 200);
    frames(1);
    CHECK(lane(6) == "late/odd" && !missing(6));
    // Deleted on the page: the lane keeps the name and is missing. It is not given another of that name.
    CHECK(del(port, door + "/samples/solo/one.wav").status == 200);
    frames(1);
    CHECK(lane(0, 3) == "solo/one" && missing(0, 3));
    CHECK(lane(1) == "snap" && lane(4) == "bad" && lane(5) == "lone");
    session.toggleTransfer(9);
    frames(2);
}

// A recording renamed or moved on the page: the songs that are shut are rewritten on the card.
static void testSavedSongsFollow() {
    const std::string root = tempFolder(), samples = root + "/samples", songs = root + "/songs";
    mkdir(samples.c_str(), 0755);
    mkdir((samples + "/kicks").c_str(), 0755);
    const auto steady = [](float seconds) { return std::vector<float>(static_cast<std::size_t>(seconds * 48000.f), 0.25f); };
    for (const char* name : {"thud", "pad", "kicks/snap", "other"}) CHECK(writeWav(samples + "/" + name + ".wav", steady(0.2f), 48000));
    std::unique_ptr<Engine> engine(new Engine);
    Input input;
    App app(*engine);
    Session& session = app.session();
    Project& project = app.project();
    Uint32 clock = 1000;
    const auto frames = [&](int count) {
        std::vector<float> out(2 * 512);
        for (int i = 0; i < count; ++i) {
            app.update(input, clock += 16);
            engine->render(out.data(), 512);
        }
    };
    session.useSampleFolder(samples);
    session.useSongFolder(songs, 3);
    session.useChordFolder(root + "/chords");
    // The open song, saved, with a rev.
    session.setLaneSample(0, 0, "thud");
    session.setLaneSample(0, 1, "pad");
    session.saveNow();
    CHECK(session.saveRev() == 1);
    const std::string open = session.songName(), openFile = songs + "/" + open + ".song", openRev = songs + "/" + open + ".1.rev";
    const std::string openText = contents(openFile);
    CHECK(openText.find("\nsample 1 0 thud\n") != std::string::npos && contents(openRev) == openText);
    // Songs that are shut. One names the recording among lines this version does not know.
    const std::string usesBefore = "apotheca-song 1\ntempo 97\nmystery thud\ntrack drums\nlanes 1 1 1\nsample 1 0 thud\nsample 2 16 kicks/snap\nsample 8 4 thud\n"
                                   "sound thud\nfuture 1 2 thud\nscene 1\nhit 1 1 1 0 1 0\ntrack sampler\nsound thud\nsample 1 0 thud\ntrack sampler\nsound pad\nzz";
    const std::string usesAfter = "apotheca-song 1\ntempo 97\nmystery thud\ntrack drums\nlanes 1 1 1\nsample 1 0 kicks/big boom\nsample 2 16 kicks/snap\nsample 8 4 kicks/big boom\n"
                                  "sound thud\nfuture 1 2 thud\nscene 1\nhit 1 1 1 0 1 0\ntrack sampler\nsound kicks/big boom\nsample 1 0 thud\ntrack sampler\nsound pad\nzz";
    const std::string notText = "apotheca-song 1\ntrack drums\nsample 1 0 other\nsample 2 0 thudd\nname THUD\ntrack sampler\nsound kicks/snap\n";
    const std::string noSong = "track drums\nsample 1 0 thud\n";
    const std::string uses = songs + "/uses.song", usesRev = songs + "/uses.3.rev", notUsing = songs + "/not using.song", junk = songs + "/junk.song";
    const std::string binned = songs + "/deleted/old.song", hidden = songs + "/.hidden.song", aside = songs + "/uses.txt";
    mkdir((songs + "/deleted").c_str(), 0755);
    CHECK(writeTextFile(uses, usesBefore) && writeTextFile(usesRev, usesBefore) && writeTextFile(notUsing, notText) && writeTextFile(junk, noSong));
    CHECK(writeTextFile(binned, usesBefore) && writeTextFile(hidden, usesBefore) && writeTextFile(aside, usesBefore));
    setDate(uses, 1000000000);
    setDate(usesRev, 1000000500);
    const ino_t usesId = fileId(uses), revId = fileId(usesRev), notId = fileId(notUsing), junkId = fileId(junk), openId = fileId(openFile), openRevId = fileId(openRev);
    const time_t notDate = fileDate(notUsing);

    CHECK(session.toggleTransfer(9));
    const std::string address = session.transferAddress();
    const int port = std::atoi(address.c_str() + address.find(':') + 1);
    const std::string door = address.substr(address.find('/'));
    frames(1); // the autosave has had its turn: the next is two seconds off
    CHECK(post(port, door + "/samples/thud.wav", "kicks/big%20boom.wav").status == 200);
    CHECK(contents(uses) == usesBefore); // not from the server's thread: when the program next looks
    frames(1);
    // The song that names it has the new name on those lines, and is otherwise the same to the byte.
    CHECK(contents(uses) == usesAfter && contents(usesRev) == usesAfter);
    CHECK(fileId(uses) != usesId && fileId(usesRev) != revId); // written beside and moved into place: a new file
    CHECK(fileDate(uses) == 1000000000 && fileDate(usesRev) == 1000000500); // with the date it had: the song list keeps its order, a rev its date
    CHECK(!exists(uses + ".part") && !exists(usesRev + ".part"));
    // One that does not use it is not written at all. Nor is anything that is no song, or not in the song folder itself.
    CHECK(contents(notUsing) == notText && fileId(notUsing) == notId && fileDate(notUsing) == notDate);
    CHECK(contents(junk) == noSong && fileId(junk) == junkId);
    CHECK(contents(binned) == usesBefore && contents(hidden) == usesBefore && contents(aside) == usesBefore);
    // The open song followed in memory. Its file is left to the autosave: not written twice.
    CHECK(std::string(project.tracks[0].laneSample[0]) == "kicks/big boom");
    CHECK(contents(openFile) == openText && fileId(openFile) == openId);
    // Its older rev is a file like any other: it has the new name, and still matches the song as it was.
    CHECK(fileId(openRev) != openRevId && contents(openRev).find("\nsample 1 0 kicks/big boom\n") != std::string::npos && contents(openRev).find("thud") == std::string::npos);
    frames(140);
    CHECK(contents(openFile).find("\nsample 1 0 kicks/big boom\n") != std::string::npos && contents(openFile) == contents(openRev));
    CHECK(session.revs().size() == 1); // so coming back to the rev needs no new one kept

    // Renamed, then moved, before the program has looked: followed through both.
    const ino_t usesId2 = fileId(uses);
    CHECK(post(port, door + "/samples/pad.wav", "warm.wav").status == 200);
    CHECK(post(port, door + "/samples/warm.wav", "pads/warm.wav").status == 200);
    frames(1);
    std::string twice = usesAfter;
    twice.replace(twice.find("sound pad\n"), 10, "sound pads/warm\n");
    CHECK(contents(uses) == twice && fileId(uses) != usesId2 && contents(usesRev) == twice);
    // Refused on the page: nothing on the card changes.
    const ino_t usesId3 = fileId(uses);
    CHECK(post(port, door + "/samples/kicks/snap.wav", "other.wav").status == 409);
    frames(1);
    CHECK(contents(uses) == twice && fileId(uses) == usesId3);
    // Deleting a recording does not touch saved songs. Nor does sending one.
    CHECK(del(port, door + "/samples/kicks/snap.wav").status == 200);
    CHECK(put(port, door + "/samples/new.wav", wavBytes(steady(0.1f), 48000)).status == 200);
    frames(1);
    CHECK(contents(uses) == twice && fileId(uses) == usesId3 && contents(notUsing) == notText && fileId(notUsing) == notId);
    session.toggleTransfer(9);

    // Asked directly: how many files were written. Names that are no recording's change nothing.
    typedef std::vector<std::pair<std::string, std::string>> Renames;
    CHECK(session.savedSongsFollow(Renames{{"kicks/big boom", "../outside"}}) == 0);
    CHECK(session.savedSongsFollow(Renames{{"kicks/big boom", "a/b/c"}}) == 0);
    CHECK(session.savedSongsFollow(Renames{{"kicks/big boom", ""}, {"", "kicks/big boom"}}) == 0);
    CHECK(writeTextFile(junk, "apotheca-song 1\ntrack sampler\nsound ../x\n"));
    CHECK(session.savedSongsFollow(Renames{{"../x", "fine"}}) == 0 && contents(junk) == "apotheca-song 1\ntrack sampler\nsound ../x\n");
    CHECK(session.savedSongsFollow(Renames{{"nothing uses this", "fine"}}) == 0);
    CHECK(session.savedSongsFollow(Renames{{"other", "other"}}) == 0 && fileId(notUsing) == notId);
    CHECK(contents(uses) == twice && fileId(uses) == usesId3);
    // The open song's own file is passed over even when it names the recording: uses.song, its rev, and the open song's rev.
    CHECK(contents(openFile).find("kicks/big boom") != std::string::npos);
    CHECK(session.savedSongsFollow(Renames{{"kicks/big boom", "thud"}}) == 3);
    CHECK(contents(openFile).find("kicks/big boom") != std::string::npos && contents(uses).find("\nsample 8 4 thud\n") != std::string::npos);
    // A rewritten song opens, and is the song it was.
    CHECK(session.openSong("uses"));
    CHECK(std::string(project.tracks[0].laneSample[0]) == "thud" && std::string(project.tracks[0].laneSample[kDrumLanes - 1]) == "thud" && project.tracks[0].laneFit[kDrumLanes - 1] == 4);
    CHECK(project.tempo == 97 && std::string(project.tracks[1].sample) == "thud" && std::string(project.tracks[2].sample) == "pads/warm");
    frames(2);
}

// While the session goes through the saved songs it holds a lock; the server waits for it before
// it writes or deletes a song, and for nothing else.
static void testTheServerWaitsForTheSongs() {
    const std::string root = tempFolder(), songs = root + "/songs";
    mkdir(songs.c_str(), 0755);
    CHECK(writeTextFile(songs + "/old.song", "apotheca-song 1\ntempo 97\n"));
    Transfer transfer;
    CHECK(transfer.start(root + "/samples", songs, root + "/chords", 0, 12345));
    const int port = transfer.port();
    const std::string door = "/" + transfer.pin();
    std::unique_lock<std::mutex> held(transfer.songFiles());
    CHECK(put(port, door + "/samples/kick.wav", aWav()).status == 200); // a recording is not held up
    std::atomic<int> sent{0}, deleted{0};
    std::thread sender([&] { sent = put(port, door + "/songs/new.song", "apotheca-song 1\ntempo 97\n").status; });
    usleep(300000);
    CHECK(sent == 0 && !exists(songs + "/new.song") && !exists(songs + "/new.song.part"));
    held.unlock();
    sender.join();
    CHECK(sent == 200 && exists(songs + "/new.song"));
    held.lock();
    std::thread deleter([&] { deleted = del(port, door + "/songs/old.song").status; });
    usleep(300000);
    CHECK(deleted == 0 && exists(songs + "/old.song"));
    held.unlock();
    deleter.join();
    CHECK(deleted == 200 && !exists(songs + "/old.song") && exists(songs + "/deleted/old.song"));
    transfer.stop();
}

int main(int, char**) {
    testTheServerWaitsForTheSongs();
    testRenamingInSongText();
    testFoundOnLoad();
    testSavedSongsFollow();
    testTheOpenSongFollows();
    testMovingWhereRenameIsRefused();
    testNames();
    testTheServer();
    testDeletingAndMoving();
    testTurningItOnFromTheDevice();
    testUpdatingOverWiFi();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("transfer tests passed\n");
    return 0;
}
