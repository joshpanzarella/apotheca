// Tests for files over WiFi: a real server on this machine, and requests
// made to it the way a browser makes them.

#include <SDL.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
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
    return answer;
}
Answer get(int port, const std::string& path) { return raw(port, "GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n"); }
Answer put(int port, const std::string& path, const std::string& body) {
    return raw(port, "PUT " + path + " HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
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
    CHECK(a.status == 200 && a.body == "{\"samples\":[\"kick one.wav\"],\"songs\":[\"quiet road.song\"],\"chords\":[]}");
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
    CHECK(put(port, door + "/samples/a/b.wav", wav).status == 400);
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
    CHECK(raw(port, "DELETE " + door + "/samples/kick%20one.wav HTTP/1.1\r\n\r\n").status == 405 && exists(samples + "/kick one.wav"));
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

} // namespace

int main(int, char**) {
    testNames();
    testTheServer();
    testTurningItOnFromTheDevice();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("transfer tests passed\n");
    return 0;
}
