// CLIPS (desk only; clip_record.hpp, docs/dev/videos.md): the script, the key band, and a short recording made by
// the program itself.
//
//   clip_test                 the script read and the band worked out, no program run
//   clip_test run APOTHECA    and a script played by the program: each press reaches the app at its time, the frames
//                             and the WAV are the same length, and a press's sound starts at its own sample

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "clip_record.hpp"
#include "temp_folder.hpp"

using namespace apotheca;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using Event = ClipScript::Event;

void testTheScript() {
    ClipScript s;
    std::string error;
    const char* text =
        "# a comment\n"
        "title THE DRUM GRID\n"
        "about One line.\n"
        "caption DRUMS\n"
        "run --demo --view drums   # for make-clips.sh\n"
        "from 1.0\n"
        "length 4.5\n"
        "2.0 tap RIGHT 3 0.25\n"
        "1.2 hold A 0.6\n"
        "3.0 stick LEFT 0.3 0.5\n"
        "3.5 down SEL\n"
        "3.6 up SEL\n"
        "1.5 say Hold A.\n"
        "4.0 caption FX\n";
    check(s.parse(text, error), "a script is read");
    check(s.title == "THE DRUM GRID" && s.about == "One line." && s.caption == "DRUMS", "its words");
    check(s.from == 1.0 && s.length == 4.5, "from and length");
    // In time order: A down 1.2, say 1.5, A up 1.8, RIGHT x3 (2.0/2.1, 2.25/2.35, 2.5/2.6), stick 3.0 and 3.3, SEL, caption.
    check(s.events.size() == 14, "fourteen events");
    if (s.events.size() != 14) return;
    check(s.events[0].kind == Event::Down && s.events[0].button == Button::A && s.events[0].hold && s.events[0].at == 1.2, "A held from 1.2");
    check(s.events[1].kind == Event::Say && s.events[1].text == "Hold A.", "said at 1.5");
    check(s.events[2].kind == Event::Up && s.events[2].button == Button::A && std::fabs(s.events[2].at - 1.8) < 1e-9, "let go at 1.8");
    check(s.events[3].kind == Event::Down && s.events[3].button == Button::Right && !s.events[3].hold && s.events[3].at == 2.0, "RIGHT at 2.0");
    check(s.events[4].kind == Event::Up && std::fabs(s.events[4].at - 2.1) < 1e-9, "a tap is a tenth of a second");
    check(std::fabs(s.events[7].at - 2.5) < 1e-9 && s.events[7].kind == Event::Down, "the third tap at 2.5");
    check(s.events[9].kind == Event::Stick && s.events[9].x == -0.5f && s.events[9].y == 0.f, "the stick half left");
    check(s.events[10].kind == Event::Stick && s.events[10].x == 0.f && std::fabs(s.events[10].at - 3.3) < 1e-9, "and let go at 3.3");
    check(s.events[11].button == Button::Select && s.events[12].button == Button::Select, "SEL, as the screen writes it");
    check(s.events[13].kind == Event::Caption && s.events[13].text == "FX", "a caption at a time");

    check(!s.parse("1.0 tap Q\n", error) && error.find("line 1") != std::string::npos, "no such button");
    check(!s.parse("x\n", error), "a word it does not know");
    check(!s.parse("1.0 stick SIDEWAYS 1\n", error), "the stick's four ways only");
    check(!s.parse("1.0 hold A\n", error), "a hold says how long");
    check(!s.parse("from 3\nlength 2\n", error), "length after from");
}

void testTheBand() {
    KeyBand band;
    const auto at = [](Event::Kind kind, double t, Button b, bool hold = false) {
        Event e;
        e.kind = kind, e.at = t, e.button = b, e.hold = hold;
        return e;
    };
    check(band.strength(0.0) == 0.f && band.line().empty(), "nothing at first");
    band.apply(at(Event::Down, 1.0, Button::A, true));
    band.apply(at(Event::Down, 1.2, Button::Right));
    check(band.line() == "HOLD A + RIGHT", "a held A and RIGHT: one line");
    check(band.chips()[0].lit && band.chips()[1].lit, "both lit while down");
    band.apply(at(Event::Up, 1.3, Button::Right));
    check(!band.chips()[1].lit && band.strength(2.0) == 1.f, "RIGHT let go: dim, the combo still there (A held)");
    band.apply(at(Event::Down, 1.5, Button::Right));
    check(band.line() == "HOLD A + RIGHT" && band.chips()[1].lit, "RIGHT again: lit again, not a second chip");
    band.apply(at(Event::Up, 1.6, Button::Right));
    band.apply(at(Event::Up, 2.0, Button::A));
    check(band.strength(2.0) == 1.f, "all let go: still drawn at once");
    check(std::fabs(band.strength(2.35) - 0.5f) < 1e-4f, "half gone halfway through the fade");
    check(band.strength(2.0 + KeyBand::kFadeSeconds) == 0.f, "gone after the fade");
    band.apply(at(Event::Down, 2.2, Button::Y));
    check(band.line() == "Y", "a new press after all were let go starts a new combo");
    band.apply(at(Event::Up, 2.3, Button::Y));
    Event stick;
    stick.kind = Event::Stick, stick.at = 3.0, stick.x = -1.f;
    band.apply(stick);
    check(band.line() == "STICK LEFT" && band.chips()[0].lit, "the stick pushed");
    stick.at = 3.2, stick.x = 0.f, stick.y = 1.f;
    band.apply(stick);
    check(band.line() == "STICK DOWN" && band.chips()[0].lit, "turned straight to down: the stick's new way");
    stick.at = 3.4, stick.y = 0.f;
    band.apply(stick);
    check(band.strength(3.4) == 1.f && band.strength(3.4 + KeyBand::kFadeSeconds + 0.01) == 0.f, "let go: it fades");
    Event caption;
    caption.kind = Event::Caption, caption.text = "FX: TURN A DIAL";
    band.apply(caption);
    check(band.caption() == "FX: TURN A DIAL", "the caption");
}

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

long fileSize(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? static_cast<long>(st.st_size) : -1;
}

// The program plays a script on the drum grid of a new song, stopped: X at 0.5 s hears the kick under the cursor,
// A at 1.0 s places a kick there (heard as it goes down). Recorded from 0.2 s.
void testARecording(const char* program) {
    const std::string folder = tempFolder("clip");
    const std::string script = folder + "/t.clip", out = folder + "/out";
    {
        std::ofstream f(script);
        f << "title T\ncaption DRUMS\nfrom 0.2\nlength 1.5\n0.5 tap X\n1.0 hold A 0.2\n1.1 say A places a hit.\n";
    }
    const std::string command = "SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + std::string(program) + "' --view drums --script '" + script +
                                "' --record '" + out + "' > '" + folder + "/log.txt' 2>&1";
    check(std::system(command.c_str()) == 0, "the program recorded the script");
    check(readAll(out + "/presses.txt") == "500 X down\n600 X up\n1000 A down\n1200 A up\n", "each press reached the app at its time");
    const std::string info = readAll(out + "/clip.txt");
    check(info.find("frames 39\n") != std::string::npos, "39 frames: 0.2 s to 1.5 s at 30 a second");
    check(info.find("samples 62400\n") != std::string::npos && info.find("rate 48000\n") != std::string::npos, "the sound as long as the frames, to the sample");
    check(info.find("dropped 0\n") != std::string::npos, "nothing dropped in the tap");
    check(fileSize(out + "/frames.rgb") == 39L * 640 * 560 * 3, "the frames: 640 x 560 each");
    check(fileSize(out + "/sound.wav") == 44 + 62400L * 4, "the WAV: 16-bit stereo");
    check(readAll(out + "/clip.srt") == "1\n00:00:00,900 --> 00:00:01,300\nA places a hit.\n\n", "the narration as subtitles");
    // In step: silent until X's own sample (0.5 s is 14,400 samples into the recording), heard at once; and A's.
    const std::string wav = readAll(out + "/sound.wav");
    const auto left = [&](long i) {
        const std::size_t at = 44 + static_cast<std::size_t>(i) * 4;
        return at + 1 < wav.size() ? static_cast<int>(static_cast<std::int16_t>(static_cast<unsigned char>(wav[at]) | (static_cast<unsigned char>(wav[at + 1]) << 8))) : 0;
    };
    const auto loudest = [&](long from, long to) {
        int most = 0;
        for (long i = from; i < to; ++i) most = std::max(most, std::abs(left(i)));
        return most;
    };
    check(loudest(0, 14400) == 0, "silence before X");
    check(loudest(14400, 14400 + 480) > 1000, "the kick within 10 ms of X's frame");
    check(loudest(38400 - 960, 38400) < 2000 && loudest(38400, 38400 + 480) > 10000, "A's kick at its own frame, not before (X's still dying away)");
    // The first frame is the drum grid, and the band under it is the theme's (not black).
    std::ifstream frames(out + "/frames.rgb", std::ios::binary);
    std::vector<unsigned char> picture(640 * 560 * 3);
    frames.read(reinterpret_cast<char*>(picture.data()), static_cast<std::streamsize>(picture.size()));
    const unsigned char* band = picture.data() + (500 * 640 + 5) * 3;
    check(band[0] + band[1] + band[2] > 0, "the band is drawn");
}

} // namespace

int main(int argc, char** argv) {
    testTheScript();
    testTheBand();
    if (argc >= 3 && std::strcmp(argv[1], "run") == 0) testARecording(argv[2]);
    if (failures) std::printf("%d failed\n", failures);
    else std::printf("clip: all passed\n");
    return failures ? 1 : 0;
}
