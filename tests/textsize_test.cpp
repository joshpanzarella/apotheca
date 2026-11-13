// TEXT SIZE: the size of the lettering is a setting (SET page; SMALL, MEDIUM, LARGE, XL).
//
//   textsize_test                     the setting itself (kept, read back, an old file reads
//                                     LARGE), its row on the SET page, the two sets of fonts,
//                                     which rows stay at LARGE, LARGE unchanged, and the FX
//                                     screen's every setting at every size
//   textsize_test views APOTHECA      every still the program knows (--view NAME), drawn by
//                                     the program itself at all four sizes: no text leaves its
//                                     box or runs into its neighbour (tests/textfit.hpp)
//   ... views APOTHECA NAME           just the stills whose name has NAME in it, each fault said
//
// LARGE is what every screen was laid out at: it is the measure the others are held to.

#include <SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "textfit.hpp"

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

// ---- every still the program knows ---------------------------------------------------------
// As they are asked for: the view's name, then any flags. (An empty name is the drum grid, which
// has no name of its own.) testEveryViewIsListed reads main.cpp and fails if it knows one that
// is not here.
const char* const kViews[] = {
    "song", "deleting", "effects", "effects-held",
    "chordfx", "chordfx-held", "chordfx-ratchet", "chordfx-strum", "chordfx-order", "chordfx-time", "chordfx-level", "chordfx-lengths",
    "chordfx-off", "chordfx-arp", "chordfx-worst", "chordfx-line",
    "notefx", "notefx-held", "notefx-chord", "notefx-chord-held", "notefx-worst", "notefx-space", "notefx-space-held",
    "notefx-glides", "notefx-glides-alone", "notefx-glides-longest-held", "notefx-glides-far1", "notefx-glides-far2", "notefx-glides-far3", "notefx-glides-far4",
    "under", "songs", "sampler", "bars", "bars-roll",
    "fx", "fx --hold-a", "fx-type", "fx-type --hold-a", "fx-pick", "fx-pick --hold-a", "fx-comp", "fx-comp --hold-a", "fx-master", "fx-master --hold-a",
    "fx-duck", "fx-duck --hold-a", "fx-choice", "fx-choice --hold-a", "fx-auto", "fx-auto --hold-a", "fx-sends", "fx-sends --hold-a",
    "fx-bus", "fx-bus --hold-a", "fx-level", "fx-level --hold-a",
    "bounce", "bounce-tail", "panic", "wifi", "revs", "add-track", "chords", "chords-swap", "notes",
    "phazed", "page0", "page1", "page2", "page3", "page4", "page5", "twin", "twin0", "twin1", "twin2", "twin3", "twin4",
    "kit", "slices", "slices-32", "slices-name", "slices-throw", "starts", "picker",
    "setup", "buttons", "typing", "qmod", "qmod0", "qmod1", "qmod2", "qmod3", "help",
    "next-scene", "next-scene-notes", "next-scene-full", "volume", "nomenu", "help-nomenu", "kit-old",
    "", " --dice", " --paint", " --paint-random", " --on-name", " --on-tempo", " --hold-a", " --hold-y",
    "help --no-menu", "song --no-menu", "setup --no-menu",
};
const int kViewCount = static_cast<int>(sizeof kViews / sizeof kViews[0]);

// The names main.cpp compares `view` with: strcmp(view, "x"), strncmp(view, "x", n), strstr(view, "x"),
// strcmp(view + n, "-x").
std::vector<std::string> viewsInMain(const std::string& source) {
    std::vector<std::string> found;
    for (std::size_t at = 0; (at = source.find("(view", at)) != std::string::npos; ++at) {
        const std::size_t open = source.find('"', at), close = open == std::string::npos ? open : source.find('"', open + 1);
        if (close == std::string::npos || open - at > 16) continue; // ("(view + 10, " is the longest there is before the name)
        found.push_back(source.substr(open + 1, close - open - 1));
    }
    return found;
}

void testEveryViewIsListed() {
    std::string source;
    CHECK(readTextFile(std::string(APOTHECA_SOURCE_DIR) + "/app/main.cpp", source));
    const std::vector<std::string> names = viewsInMain(source);
    CHECK(names.size() > 60); // (main.cpp was read, and the names found in it)
    for (const std::string& name : names) {
        bool listed = false;
        for (const char* view : kViews) listed = listed || std::strstr(view, name.c_str()) != nullptr;
        if (!listed) std::printf("    main.cpp knows a still named \"%s\": add it to kViews\n", name.c_str());
        CHECK(listed);
    }
    // The reading itself: each of the ways main.cpp asks.
    const std::vector<std::string> read = viewsInMain("if (std::strcmp(view, \"aa\") == 0 || std::strncmp(view, \"bb\", 2) == 0) x(\"no\"); if (std::strstr(view, \"cc\")) y; std::strcmp(view + 10, \"-dd\")");
    CHECK(read.size() == 4 && read[0] == "aa" && read[1] == "bb" && read[2] == "cc" && read[3] == "-dd");
}

// A text in one of the two bars: they are drawn with the fixed set, so at every size it is where LARGE has it.
bool inABar(const textfit::Text& t) { return t.y + t.h <= ui::kTopBarH || t.y >= ui::kScreenH - ui::kHintsH; }

// One still: the program draws it at each size and writes down what it drew. Gives its faults.
std::vector<std::string> viewFaults(const std::string& program, const std::string& folder, const std::string& samples, int index, bool* ran) {
    const std::string view = kViews[index];
    const std::size_t space = view.find(' ');
    const std::string name = view.substr(0, space), flags = space == std::string::npos ? "" : view.substr(space);
    const std::string texts = folder + "/texts" + std::to_string(index), songs = folder + "/songs" + std::to_string(index);
    mkdir(songs.c_str(), 0755);
    const std::string command = "SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + program + "' --demo" + (name.empty() ? "" : " --view " + name) + flags + " --samples '" + samples +
                                "' --songs '" + songs + "' --still-ms 250 --texts '" + texts + "' > /dev/null 2>&1";
    std::vector<std::string> out;
    textfit::Frame frames[4];
    *ran = std::system(command.c_str()) == 0 && textfit::readFrames(texts.c_str(), frames) == 4 && !frames[kTextLarge].texts.empty();
    if (!*ran) return out;
    for (int size = 0; size < kTextSizes; ++size) {
        if (size == kTextLarge) continue;
        for (const std::string& fault : textfit::faults(frames[kTextLarge], frames[size])) out.push_back(std::string(kTextSizeNames[size]) + ": " + fault);
        if (frames[size].texts.size() != frames[kTextLarge].texts.size()) continue;
        for (std::size_t i = 0; i < frames[size].texts.size(); ++i) {
            const textfit::Text& was = frames[kTextLarge].texts[i];
            const textfit::Text& is = frames[size].texts[i];
            if (inABar(was) && (is.x != was.x || is.y != was.y || is.w != was.w || is.h != was.h || is.cap != was.cap))
                out.push_back(std::string(kTextSizeNames[size]) + ": a bar's text is not as at LARGE: \"" + is.words + "\"");
        }
    }
    return out;
}

int runViews(const char* program, const char* only) {
    testEveryViewIsListed();
    char pattern[] = "/tmp/apotheca-textviews-XXXXXX";
    const std::string folder = mkdtemp(pattern), samples = folder + "/samples";
    mkdir(samples.c_str(), 0755);
    mkdir((samples + "/kicks").c_str(), 0755);
    // A few recordings, one a break, one in a folder with a long name: the kit, the sampler, the picker and the slices need them.
    const auto wav = [&](const char* name, float seconds, float hz) {
        std::vector<float> sound(static_cast<std::size_t>(48000.f * seconds));
        for (std::size_t i = 0; i < sound.size(); ++i) sound[i] = 0.4f * std::sin(6.2831853f * hz * static_cast<float>(i) / 48000.f) * (1.f - static_cast<float>(i) / static_cast<float>(sound.size()));
        CHECK(writeWav(samples + "/" + name + ".wav", sound, 48000));
    };
    wav("amen break 136", 1.76f, 180.f);
    wav("kicks/808 long boom", 0.5f, 55.f);
    wav("hat closed", 0.1f, 4000.f);
    wav("vocal take 06", 1.2f, 220.f);
    wav("long ambient texture", 2.f, 110.f);

    std::vector<std::vector<std::string>> faults(static_cast<std::size_t>(kViewCount));
    std::vector<char> ran(static_cast<std::size_t>(kViewCount), 0);
    std::atomic<int> next{0};
    std::vector<std::thread> workers;
    for (int w = 0; w < 4; ++w)
        workers.emplace_back([&] {
            for (int i = next++; i < kViewCount; i = next++) {
                if (only && !std::strstr(kViews[i], only)) continue;
                bool did = false;
                faults[static_cast<std::size_t>(i)] = viewFaults(program, folder, samples, i, &did);
                ran[static_cast<std::size_t>(i)] = did ? 1 : 0;
            }
        });
    for (std::thread& worker : workers) worker.join();
    int drawn = 0;
    for (int i = 0; i < kViewCount; ++i) {
        if (only && !std::strstr(kViews[i], only)) continue;
        ++drawn;
        if (!ran[static_cast<std::size_t>(i)]) std::printf("    the still \"%s\" was not drawn\n", kViews[i]);
        CHECK(ran[static_cast<std::size_t>(i)]);
        for (const std::string& fault : faults[static_cast<std::size_t>(i)]) std::printf("    %s: %s\n", kViews[i][0] ? kViews[i] : "(the drum grid)", fault.c_str());
        CHECK(faults[static_cast<std::size_t>(i)].empty());
    }
    CHECK(only || drawn == kViewCount);
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("text size, %d stills at four sizes: tests passed\n", drawn);
    return 0;
}


// ---- in the test's own process ---------------------------------------------------------------

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, UP = SDLK_UP, DOWN = SDLK_DOWN, LEFT = SDLK_LEFT, RIGHT = SDLK_RIGHT, SELECT = SDLK_BACKSPACE,
                  MENU = SDLK_TAB, L1 = SDLK_q, R1 = SDLK_w, L2 = SDLK_1, R2 = SDLK_2;
bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

// A screen to draw on, the lettering alone at LARGE (as every other test has it), and the four
// sets the program would hold with each TEXT SIZE chosen: each with LARGE as its fixed set.
struct Screens {
    Canvas canvas;
    std::unique_ptr<Fonts> at[kTextSizes]; // (after the canvas: its letters go before the renderer does)
    Screens() {
        const std::vector<unsigned char> ttf = loadFontFile();
        for (int size = 0; size < kTextSizes; ++size) {
            at[size].reset(new Fonts);
            at[size]->fixedSet = canvas.fonts.get();
            CHECK(canvas.ok && at[size]->load(canvas.r, ttf, size));
        }
    }
};
Screens& screens() {
    static Screens* all = new Screens; // (kept to the end: SDL is never shut down here)
    return *all;
}

struct Drawn {
    textfit::Frame frame;
    std::vector<Uint32> pixels;
    std::vector<Uint32> rows(int from, int to) const { return std::vector<Uint32>(pixels.begin() + from * ui::kScreenW, pixels.begin() + to * ui::kScreenW); }
};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;

    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256); // (a test that never renders never empties the queue)
            engine->render(out.data(), 256);
        }
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) {
            key(code, true);
            frames(1);
            key(code, false);
            frames(1);
        }
    }
    Session& session() { return app.session(); }
    Project& project() { return app.project(); }
    // The screen as it is now, with one of the sets (`fonts` null: the canvas's own, LARGE alone).
    Drawn draw(const Fonts* fonts = nullptr) {
        Screens& s = screens();
        DrawLog log;
        s.canvas.clear();
        gDrawLog = &log;
        app.draw(s.canvas.r, fonts ? *fonts : *s.canvas.fonts);
        gDrawLog = nullptr;
        SDL_RenderPresent(s.canvas.r);
        return Drawn{textfit::frameOf(log), s.canvas.all()};
    }
};

int holdsCount = 0;
// The screen as it is now, at every size:
//   - with LARGE chosen it is, pixel for pixel, what the lettering alone at LARGE draws (as on main);
//   - at the other three no text has left its box, run into its neighbour or lost its middle (textfit.hpp);
//   - the two bars, and everything from `strip` down (the strip under a grid; 0: none), are the same pixels at every size.
// Gives how many of the screen's texts are drawn in another size than at LARGE, at XL: what follows the setting.
int holds(Rig& rig, const char* what, int strip = 0, bool say = true) {
    ++holdsCount;
    Screens& s = screens();
    const Drawn alone = rig.draw();
    const Drawn large = rig.draw(s.at[kTextLarge].get());
    const bool same = alone.pixels == large.pixels;
    if (!same && say) std::printf("    %s: with LARGE chosen it is not what LARGE alone draws\n", what);
    CHECK(same);
    CHECK(!large.frame.texts.empty());
    int follows = 0;
    for (int size = 0; size < kTextSizes; ++size) {
        if (size == kTextLarge) continue;
        const Drawn now = rig.draw(s.at[size].get());
        const std::vector<std::string> faults = textfit::faults(large.frame, now.frame);
        if (say)
            for (const std::string& fault : faults) std::printf("    %s: %s: %s\n", what, kTextSizeNames[size], fault.c_str());
        CHECK(faults.empty());
        const bool top = now.rows(0, ui::kTopBarH + 1) == large.rows(0, ui::kTopBarH + 1);
        const bool bottom = now.rows(ui::kScreenH - ui::kHintsH, ui::kScreenH) == large.rows(ui::kScreenH - ui::kHintsH, ui::kScreenH);
        const bool under = strip <= 0 || now.rows(strip, ui::kScreenH) == large.rows(strip, ui::kScreenH);
        if (say && !(top && bottom && under)) std::printf("    %s: %s: %s is not as at LARGE\n", what, kTextSizeNames[size], !top ? "the top bar" : !bottom ? "the hints bar" : "the strip");
        CHECK(top);
        CHECK(bottom);
        CHECK(under);
        if (size == kTextXl && now.frame.texts.size() == large.frame.texts.size())
            for (std::size_t i = 0; i < now.frame.texts.size(); ++i) follows += now.frame.texts[i].cap != large.frame.texts[i].cap ? 1 : 0;
    }
    return follows;
}

// ---- the sizes, and the two sets -----------------------------------------------------------
void testTheSizes() {
    // Where it started, the step after, what the screens were laid out at, and a little bigger.
    CHECK(kTextSizes == 4 && kTextSmall == 0 && kTextMedium == 1 && kTextLarge == 2 && kTextXl == 3); // (the numbers are kept in settings.txt)
    CHECK(Session::kTextSizeCount == kTextSizes && Session::kTextSizeAsBuilt == kTextLarge);
    const float points[kTextSizes][3] = {{12.f, 14.f, 16.f}, {13.5f, 16.f, 18.5f}, {14.5f, 17.f, 20.f}, {15.5f, 18.5f, 21.5f}};
    const char* names[kTextSizes] = {"SMALL", "MEDIUM", "LARGE", "XL"};
    for (int size = 0; size < kTextSizes; ++size) {
        CHECK(std::string(kTextSizeNames[size]) == names[size]);
        for (int font = 0; font < 3; ++font) CHECK(kTextPoints[size][font] == points[size][font]);
    }
    Screens& s = screens();
    if (!s.canvas.ok) return;
    const char* words = "WAVE KEY FOLLOW 120 bpm";
    for (int size = 0; size < kTextSizes; ++size) {
        const Fonts& f = *s.at[size];
        // Three fonts, small to large; each size bigger than the one before.
        CHECK(f.small.width(words) < f.medium.width(words) && f.medium.width(words) < f.large.width(words));
        CHECK(f.small.capHeight() <= f.medium.capHeight() && f.medium.capHeight() <= f.large.capHeight());
        if (size > 0) {
            const Fonts& before = *s.at[size - 1];
            CHECK(before.small.width(words) < f.small.width(words) && before.medium.width(words) < f.medium.width(words) && before.large.width(words) < f.large.width(words));
        }
        // The fixed set is LARGE's, whatever is chosen; a set by itself is its own.
        CHECK(&f.fixed() == s.canvas.fonts.get() && &s.canvas.fonts->fixed() == s.canvas.fonts.get());
    }
    // LARGE chosen is LARGE: the same widths and heights as the lettering every other test draws with.
    const Fonts& large = *s.at[kTextLarge];
    const Fonts& alone = *s.canvas.fonts;
    CHECK(large.small.width(words) == alone.small.width(words) && large.medium.width(words) == alone.medium.width(words) && large.large.width(words) == alone.large.width(words));
    CHECK(large.small.capHeight() == 12.f && large.medium.capHeight() == 14.f && large.large.capHeight() == 16.f); // (what the strips and bars were laid out round)
    CHECK(s.at[kTextXl]->small.capHeight() == 13.f && s.at[kTextSmall]->small.capHeight() == 10.f);

    // A size nobody knows loads LARGE.
    const std::vector<unsigned char> ttf = loadFontFile();
    Fonts odd;
    CHECK(odd.load(s.canvas.r, ttf, 9) && odd.small.width(words) == alone.small.width(words));
    CHECK(odd.load(s.canvas.r, ttf, -1) && odd.large.width(words) == alone.large.width(words));
    // Loading again is another size (the setting changed while the program runs), and can be drawn with.
    CHECK(odd.load(s.canvas.r, ttf, kTextXl) && odd.small.width(words) == s.at[kTextXl]->small.width(words) && odd.small.capHeight() == 13.f);
    CHECK(odd.load(s.canvas.r, ttf, kTextSmall) && odd.small.width(words) == s.at[kTextSmall]->small.width(words));
    s.canvas.clear();
    odd.large.draw(s.canvas.r, 100.f, 100.f, "H", ui::kText);
    SDL_RenderPresent(s.canvas.r);
    int lit = 0;
    for (int y = 90; y < 130; ++y)
        for (int x = 90; x < 130; ++x) lit += s.canvas.is(x, y, ui::kBg) ? 0 : 1;
    CHECK(lit > 20);
    CHECK(!odd.load(s.canvas.r, std::vector<unsigned char>(), kTextXl)); // no font data: said, not crashed
    {
        Fonts none; // (never loaded: letting go of it is harmless)
    }
}

// Fonts::within: the chosen size where the text fits its room, LARGE where it does not; never cut, never bigger than LARGE needs.
void testWithin() {
    Screens& s = screens();
    if (!s.canvas.ok) return;
    const char* words = "EQUAL POWER";
    const Fonts& large = *s.at[kTextLarge];
    const Fonts& xl = *s.at[kTextXl];
    const Fonts& small = *s.at[kTextSmall];
    const float atLarge = large.large.width(words), atXl = xl.large.width(words), atSmall = small.large.width(words);
    CHECK(atSmall < atLarge && atLarge < atXl);
    // Room for XL: XL. Room for LARGE only: LARGE's. To the pixel either way.
    CHECK(&xl.within(&Fonts::large, words, atXl) == &xl.large);
    CHECK(&xl.within(&Fonts::large, words, atXl - 0.01f) == &xl.fixed().large);
    CHECK(&xl.within(&Fonts::large, words, atLarge) == &xl.fixed().large);
    // No room even at LARGE: as LARGE draws it (it is the owner's layout), not something else.
    CHECK(&xl.within(&Fonts::large, words, 10.f) == &xl.fixed().large);
    // At LARGE and under, the chosen one always: a smaller size is never swapped for a bigger.
    CHECK(&large.within(&Fonts::large, words, 10.f) == &large.large && &large.within(&Fonts::large, words, 1000.f) == &large.large);
    CHECK(&small.within(&Fonts::large, words, 10.f) == &small.large && &small.within(&Fonts::large, words, atSmall) == &small.large);
    // Each of the three is asked for by itself.
    CHECK(&xl.within(&Fonts::small, words, 1000.f) == &xl.small && &xl.within(&Fonts::medium, words, 1000.f) == &xl.medium);
    CHECK(&xl.within(&Fonts::small, words, 1.f) == &xl.fixed().small && &xl.within(&Fonts::medium, words, 1.f) == &xl.fixed().medium);
    // The lettering alone (the tests' canvas) is its own fixed set.
    CHECK(&s.canvas.fonts->within(&Fonts::small, words, 1.f) == &s.canvas.fonts->small);
}

// ---- the setting: kept, read back, an old file reads LARGE ------------------------------------
void testTheSettingIsKept() {
    char pattern[] = "/tmp/apotheca-textsize-XXXXXX";
    const std::string folder = mkdtemp(pattern), file = folder + "/settings.txt";
    const float stickBefore = ui::gStickSpeed;
    std::string kept;
    {
        Rig rig;
        Session& session = rig.session();
        CHECK(session.textSize() == kTextLarge); // as built
        session.setTextSize(kTextXl);            // (no program folder yet: nothing is written anywhere)
        CHECK(session.textSize() == kTextXl && !readTextFile(file, kept));
        session.setTextSize(kTextLarge);
        session.useProgramFolder(folder);
        session.loadSettings(); // nothing kept yet: as built
        CHECK(session.textSize() == kTextLarge);
        // Every size is kept as its number, with the other settings, as soon as it is set.
        for (int size : {kTextXl, kTextSmall, kTextMedium, kTextLarge, kTextXl}) {
            session.setTextSize(size);
            CHECK(session.textSize() == size);
            CHECK(readTextFile(file, kept) && kept == "stick " + std::to_string(static_cast<int>(std::round(ui::gStickSpeed * 100.f))) + "\noctaves 2\nmidirec 0\ntext " + std::to_string(size) + "\n");
        }
        // It has ends.
        session.setTextSize(99);
        CHECK(session.textSize() == kTextXl);
        session.setTextSize(-5);
        CHECK(session.textSize() == kTextSmall);
        CHECK(readTextFile(file, kept) && kept.find("text 0\n") != std::string::npos);
        // Another setting changed keeps the size too.
        session.setTextSize(kTextXl);
        session.setOctavesShown(3);
        CHECK(readTextFile(file, kept) && kept.find("octaves 3\n") != std::string::npos && kept.find("text 3\n") != std::string::npos);
    }
    {
        // There again at the next start.
        Rig later;
        CHECK(later.session().textSize() == kTextLarge);
        later.session().useProgramFolder(folder);
        later.session().loadSettings();
        CHECK(later.session().textSize() == kTextXl && later.session().octavesShown() == 3);
        // Loading writes nothing.
        std::string after;
        CHECK(readTextFile(file, after) && after == kept);
        // Each size comes back.
        for (int size = 0; size < kTextSizes; ++size) {
            writeTextFile(file, "stick 75\noctaves 2\nmidirec 0\ntext " + std::to_string(size) + "\n");
            later.session().loadSettings();
            CHECK(later.session().textSize() == size);
        }
        // Nonsense is not believed: it stays as it was.
        later.session().setTextSize(kTextMedium);
        for (const char* bad : {"text 4\n", "text -1\n", "text 99\n", "text\n", "text big\n", "texts 3\n", "TEXT 3\n"}) {
            writeTextFile(file, bad);
            later.session().loadSettings();
            CHECK(later.session().textSize() == kTextMedium);
        }
        // Wherever the line is among the others, and with lines nobody knows.
        writeTextFile(file, "text 3\nlater 5\nstick 60\n");
        later.session().loadSettings();
        CHECK(later.session().textSize() == kTextXl && near(later.session().stickSpeed(), 0.6f));
    }
    {
        // A file from before there was such a setting has no line for it: LARGE, and the rest as kept.
        writeTextFile(file, "stick 80\noctaves 1\nmidirec 1\n");
        Rig old;
        old.session().useProgramFolder(folder);
        old.session().loadSettings();
        CHECK(old.session().textSize() == kTextLarge && old.session().octavesShown() == 1 && near(old.session().stickSpeed(), 0.8f) && old.session().midiRecording());
        CHECK(readTextFile(file, kept) && kept == "stick 80\noctaves 1\nmidirec 1\n"); // (and it is not rewritten until something is set)
    }
    ui::gStickSpeed = stickBefore;
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
}

// ---- its row on the SET page ----------------------------------------------------------------
void testTheRow() {
    Screens& s = screens();
    if (!s.canvas.ok) return;
    typedef SettingsScreen S;
    Rig rig;
    Session& session = rig.session();
    session.view = View::Settings;
    S& setup = rig.app.settings();
    // Among the handheld's rows, after STICK SPEED; twelve rows now.
    CHECK(S::kRows == 12 && S::kTextSize == S::kStick + 1 && S::kOctaves == S::kTextSize + 1 && S::kTextSize > S::kVolume);
    setup.goTo(S::kStick);
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kTextSize && session.textSize() == kTextLarge);
    // A choice: one click a push, and it has ends.
    rig.tap(RIGHT);
    CHECK(session.textSize() == kTextXl);
    rig.tap(RIGHT, 3);
    CHECK(session.textSize() == kTextXl);
    rig.tap(LEFT);
    CHECK(session.textSize() == kTextLarge);
    rig.tap(LEFT);
    CHECK(session.textSize() == kTextMedium);
    rig.tap(LEFT);
    CHECK(session.textSize() == kTextSmall);
    rig.tap(LEFT, 3);
    CHECK(session.textSize() == kTextSmall);
    // A, B, X and Y do nothing to it.
    for (SDL_Keycode other : {A, B, X, Y}) rig.tap(other);
    CHECK(session.textSize() == kTextSmall && setup.cursorRow() == S::kTextSize && !setup.typing() && !setup.testing());
    // A held D-pad goes at the speed of a list (one seen at a time), not a cursor's.
    rig.key(RIGHT, true);
    rig.frames(1);
    CHECK(session.textSize() == kTextMedium);
    rig.frames(static_cast<int>(ui::kListDelayMs / 16) - 2);
    CHECK(session.textSize() == kTextMedium);
    rig.frames(4);
    CHECK(session.textSize() == kTextLarge);
    rig.frames(static_cast<int>(ui::kListEveryMs / 16) + 1);
    CHECK(session.textSize() == kTextXl);
    rig.key(RIGHT, false);
    rig.frames(1);
    // The stick sideways changes it too, once a push.
    rig.key(SDLK_j, true); // the stick, to the left
    rig.frames(1);
    rig.key(SDLK_j, false);
    rig.frames(1);
    CHECK(session.textSize() == kTextLarge);
    // The other rows are where they were, by name: down from TEXT SIZE is PIANO ROLL.
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kOctaves);
    rig.tap(RIGHT);
    CHECK(session.octavesShown() == 3 && session.textSize() == kTextLarge);

    // As drawn. Each size's own name is on the row, in that size; twelve rows, none over another, the last clear of the hints bar.
    setup.goTo(S::kTextSize);
    for (int row = 0; row < S::kRows; ++row) {
        CHECK(S::rowY(row) >= ui::kTopBarH + 8 && S::rowY(row) + S::kRowH - 6 <= ui::kScreenH - ui::kHintsH - 4);
        if (row > 0) CHECK(S::rowY(row) >= S::rowY(row - 1) + S::kRowH - 6 + 4);
    }
    for (int size = 0; size < kTextSizes; ++size) {
        session.setTextSize(size);
        rig.frames(1);
        const Fonts& fonts = *s.at[size];
        const Drawn drawn = rig.draw(&fonts);
        bool name = false, value = false, hintChange = false;
        const float boxTop = static_cast<float>(S::rowY(S::kTextSize)), boxBottom = boxTop + S::kRowH - 6;
        for (const textfit::Text& t : drawn.frame.texts) {
            if (t.words == "TEXT SIZE") name = t.x == S::kNameX && t.cap == fonts.small.capHeight() && t.y >= boxTop && t.y + t.h <= boxBottom;
            if (t.words == kTextSizeNames[size] && t.x == S::kValueX)
                value = t.cap == fonts.large.capHeight() && t.y >= boxTop && t.y + t.h <= boxBottom && near(t.y - boxTop, boxBottom - (t.y + t.h)); // in the middle of its row
            if (t.words == "change") hintChange = true;
        }
        CHECK(name);
        CHECK(value);
        CHECK(hintChange); // LEFT / RIGHT: change, as on the page's other rows that are changed so
        // The value is lit where the cursor is: teal letters at the value's place.
        int teal = 0;
        for (int y = S::rowY(S::kTextSize); y < S::rowY(S::kTextSize) + S::kRowH - 6; ++y)
            for (int x = static_cast<int>(S::kValueX); x < static_cast<int>(S::kValueX) + 80; ++x) teal += s.canvas.is(x, y, ui::kTeal) ? 1 : 0;
        CHECK(teal > 20);
    }
    session.setTextSize(kTextLarge);
    // The whole page, the cursor on each row in turn, at every size.
    for (int row = 0; row < S::kRows; ++row) {
        setup.goTo(row);
        rig.frames(1);
        char what[40];
        std::snprintf(what, sizeof what, "the SET page, row %d", row + 1);
        CHECK(holds(rig, what) > 10);
    }
}
} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (argc >= 3 && std::strcmp(argv[1], "views") == 0) return runViews(argv[2], argc >= 4 ? argv[3] : nullptr);
    CHECK(screens().canvas.ok);
    testTheSizes();
    testWithin();
    testTheSettingIsKept();
    testTheRow();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("text size: tests passed\n");
    return 0;
}
