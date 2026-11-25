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
#include "presets.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "chord_effects.hpp"
#include "chords.hpp"
#include "controls.hpp"
#include "note_marks.hpp"
#include "pattern_layout.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "stills.hpp"
#include "textfit.hpp"
#include "temp_folder.hpp"

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

using namespace stills;

void testEveryViewIsListed() {
    std::string source;
    // (The stills are set up in stills.cpp since 2026-11-24; main.cpp still reads a few of its own: both are read.)
    std::string stills;
    CHECK(readTextFile(std::string(APOTHECA_SOURCE_DIR) + "/app/main.cpp", source));
    CHECK(readTextFile(std::string(APOTHECA_SOURCE_DIR) + "/app/stills.cpp", stills));
    source += stills;
    CHECK(viewsInMain(source).size() > 60); // (the files were read, and the names found in them)
    for (const std::string& name : unlisted(source)) std::printf("    main.cpp or stills.cpp knows a still named \"%s\": add it to kViews\n", name.c_str());
    CHECK(unlisted(source).empty());
    // The reading itself: each of the ways main.cpp asks, and a still nobody has listed is found.
    const std::vector<std::string> read = viewsInMain("if (std::strcmp(view, \"aa\") == 0 || std::strncmp(view, \"bb\", 2) == 0) x(\"no\"); if (std::strstr(view, \"cc\")) y; std::strcmp(view + 10, \"-dd\")");
    CHECK(read.size() == 4 && read[0] == "aa" && read[1] == "bb" && read[2] == "cc" && read[3] == "-dd");
    const std::vector<std::string> fresh = unlisted("if (std::strcmp(view, \"song\") == 0) a; else if (std::strcmp(view, \"brand-new-still\") == 0) b;");
    CHECK(fresh.size() == 1 && fresh[0] == "brand-new-still");
}

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
        // It was drawn at that size: every still has words outside the bars and strips, and they are smaller than LARGE's, or bigger.
        bool smaller = false, bigger = false;
        for (std::size_t i = 0; i < frames[size].texts.size() && i < frames[kTextLarge].texts.size(); ++i) {
            smaller = smaller || frames[size].texts[i].cap < frames[kTextLarge].texts[i].cap;
            bigger = bigger || frames[size].texts[i].cap > frames[kTextLarge].texts[i].cap;
        }
        if (size < kTextLarge ? !smaller || bigger : !bigger) out.push_back(std::string(kTextSizeNames[size]) + ": it was not drawn at that size");
    }
    return out;
}

// "--text-size NAME" on a still: the setting is set, and the program loads the lettering again
// before it draws. (The size it had come to is the first line of what "--texts" writes.)
void testTheStillsFlag(const std::string& program, const std::string& folder) {
    const auto chosen = [&](const char* flags, int* size, float* cap) {
        const std::string texts = folder + "/chosen";
        unlink(texts.c_str());
        const std::string command = "SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + program + "' --demo --view setup " + flags + " --still-ms 100 --texts '" + texts + "' > /dev/null 2>&1";
        if (std::system(command.c_str()) != 0) return false;
        std::string all;
        return readTextFile(texts, all) && std::sscanf(all.c_str(), "CHOSEN %d %f", size, cap) == 2;
    };
    int size = -1;
    float cap = 0.f;
    CHECK(chosen("", &size, &cap) && size == kTextLarge && cap == 12.f); // nothing asked: LARGE
    const float caps[kTextSizes] = {10.f, 11.f, 12.f, 13.f};
    const char* names[kTextSizes] = {"small", "medium", "large", "xl"};
    for (int want = 0; want < kTextSizes; ++want) {
        CHECK(chosen((std::string("--text-size ") + names[want]).c_str(), &size, &cap) && size == want && cap == caps[want]);
    }
    CHECK(chosen("--text-size XL", &size, &cap) && size == kTextXl); // (in either case)
    CHECK(!chosen("--text-size huge", &size, &cap));                 // a size nobody knows: said, and no still
    // The picture itself is at the size asked for: the SET page's row says so.
    const std::string texts = folder + "/row", picture = folder + "/row.bmp";
    const std::string command = "SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + program + "' --demo --view setup --text-size small --still-ms 100 --texts '" + texts + "' --shot '" + picture + "' > /dev/null 2>&1";
    CHECK(std::system(command.c_str()) == 0);
    struct stat info;
    CHECK(stat(picture.c_str(), &info) == 0 && info.st_size > 640 * 480 * 3);
    textfit::Frame frames[4];
    CHECK(textfit::readFrames(texts.c_str(), frames) == 4);
    bool said = false;
    for (const textfit::Text& t : frames[kTextSmall].texts) said = said || t.words == "SMALL";
    CHECK(said);
    // And writing the texts down (all four sizes) leaves the picture as asked for: the same rows of the SET page as a still without "--texts".
    const std::string plain = folder + "/plain.bmp";
    const std::string again = "SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + program + "' --demo --view setup --text-size small --still-ms 100 --shot '" + plain + "' > /dev/null 2>&1";
    CHECK(std::system(again.c_str()) == 0);
    SDL_Surface* with = SDL_LoadBMP(picture.c_str());
    SDL_Surface* without = SDL_LoadBMP(plain.c_str());
    CHECK(with && without && with->w == 640 && without->w == 640 && with->format->BytesPerPixel == without->format->BytesPerPixel);
    if (with && without && with->w == without->w && with->format->BytesPerPixel == without->format->BytesPerPixel) {
        bool same = true;
        int lit = 0;
        const int bytes = with->format->BytesPerPixel;
        for (int y = SettingsScreen::kRowsTop; y < SettingsScreen::kPageFoot; ++y) { // (the rows: nothing in them moves while the song plays)
            const Uint8* a = static_cast<const Uint8*>(with->pixels) + y * with->pitch;
            const Uint8* b = static_cast<const Uint8*>(without->pixels) + y * without->pitch;
            same = same && std::memcmp(a, b, static_cast<std::size_t>(640 * bytes)) == 0;
            for (int x = 0; x < 640 * bytes; ++x) lit += a[x] > 150 ? 1 : 0;
        }
        CHECK(same);
        CHECK(lit > 3000); // (there are words in it)
    }
    if (with) SDL_FreeSurface(with);
    if (without) SDL_FreeSurface(without);
}

// Presses that come while the program rests between passes are read (gesture audit G06; review of B1-latency). The
// rest (pass_wait.hpp) takes them off SDL's queue, so main.cpp must read its events from the PassWait: polling SDL's
// queue there, every press the rest took would be lost, and controls_test, idle_test and the other stills (their
// keys pushed before the first pass) would never see it. "rest-presses" pushes SEL and eight taps of UP from a timer,
// 7 ms apart, once the passes are going: most land in a rest, and the top bar reads 108 BPM (from 100) only if all
// eight were read.
void testPressesInTheRest(const std::string& program, const std::string& folder) {
    const std::string texts = folder + "/rest";
    const std::string command = "SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy '" + program + "' --demo --view rest-presses --still-ms 600 --texts '" + texts + "' > /dev/null 2>&1";
    CHECK(std::system(command.c_str()) == 0);
    textfit::Frame frames[4];
    CHECK(textfit::readFrames(texts.c_str(), frames) == 4);
    std::string tempo; // the number furthest right in the top bar, just before BPM (a scene's "1" is further left)
    float tempoX = 0.f;
    for (const textfit::Text& t : frames[kTextLarge].texts)
        if (t.y < ui::kTopBarH && t.x > tempoX && !t.words.empty() && t.words.find_first_not_of("0123456789") == std::string::npos) tempo = t.words, tempoX = t.x;
    CHECK(tempo == "108");
    if (tempo != "108") std::printf("    the top bar read %s BPM, not 108: a press that came in a rest was lost\n", tempo.c_str());
}

int runViews(const char* program, const char* only) {
    testEveryViewIsListed();
    const std::string folder = tempFolder("textviews"), samples = folder + "/samples";
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
    // Folders two levels down (owner, 2026-11-14), one of them empty: the list of recordings shows them in columns.
    for (const char* dir : {"/drums", "/drums/kicks", "/drums/snares", "/drums/empty", "/vocals"}) mkdir((samples + dir).c_str(), 0755);
    wav("drums/kicks/tight", 0.2f, 60.f);
    wav("drums/kicks/sub drop with a long name", 1.1f, 40.f);
    wav("drums/snares/crack", 0.15f, 900.f);
    wav("drums/one shot", 0.3f, 300.f);
    // A multisample with velocity layers (sampler-layers): two notes, three layers each.
    mkdir((samples + "/grand piano").c_str(), 0755);
    for (const char* name : {"grand piano/Grand C3 pp", "grand piano/Grand C3 mf", "grand piano/Grand C3 ff", "grand piano/Grand C4 pp", "grand piano/Grand C4 mf", "grand piano/Grand C4 ff"})
        wav(name, 0.3f, 262.f);

    if (!only) testTheStillsFlag(program, folder), testPressesInTheRest(program, folder);
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


// ---- the measure itself (textfit.hpp), on made-up frames -----------------------------------------
void testTheMeasure() {
    using namespace textfit;
    int order = 0;
    const auto text = [&](Frame& f, const char* words, float x, float y, float w, float cap = 12.f) { f.texts.push_back({order++, cap, x, y, w, cap, words}); };
    const auto box = [&](Frame& f, float x, float y, float w, float h, bool solid = true) { f.boxes.push_back({order++, solid, x, y, w, h}); };
    const auto has = [](const std::vector<std::string>& faults, const char* what) {
        for (const std::string& fault : faults)
            if (fault.find(what) != std::string::npos) return true;
        return false;
    };
    // A screen, a row in it, a label and a value in the row, a bar beside the value.
    const auto screen = [&](float labelW, float valueX, float valueW, float valueY = 108.f, float cap = 12.f, const char* value = "100%") {
        Frame f;
        order = 0;
        box(f, 0, 0, 640, 480);
        box(f, 16, 100, 608, 28);
        text(f, "VOLUME", 26, 108, labelW);
        text(f, value, valueX, valueY, valueW, cap);
        box(f, 250, 118, 110, 4);
        return f;
    };
    const Frame large = screen(60, 170, 50);
    CHECK(faults(large, large).empty());
    CHECK(faults(large, screen(66, 170, 55, 107.f, 14.f)).empty()); // bigger, and everything still clear
    CHECK(faults(large, screen(50, 170, 40, 109.f, 10.f)).empty()); // smaller
    // Into its neighbour: the label reaches the value, or stops less than a space short of it.
    CHECK(has(faults(large, screen(150, 170, 50)), "ran into its neighbour"));
    CHECK(has(faults(large, screen(144, 170, 50)), "ran into its neighbour"));
    CHECK(has(faults(large, screen(141, 170, 50)), "ran into its neighbour")); // 3 short: a space is 3.6 (0.3 of 12)
    CHECK(!has(faults(large, screen(140, 170, 50)), "ran into its neighbour")); // 4 short
    // Side by side means on one line: a text mostly above or below the other need only not overlap it.
    CHECK(!has(faults(large, screen(143, 170, 50, 115.f)), "ran into its neighbour")); // 1 short, but 7 lower: they share 5 of 12
    CHECK(has(faults(large, screen(143, 170, 50, 114.f)), "ran into its neighbour"));  // 6 lower: they share half
    CHECK(has(faults(large, screen(150, 170, 50, 115.f)), "ran into its neighbour"));  // overlapping is overlapping
    CHECK(!has(faults(large, screen(150, 170, 50, 120.f)), "ran into its neighbour")); // under it
    // Into the bar beside it.
    CHECK(has(faults(large, screen(60, 170, 81, 108.f)), "ran into what is beside it"));
    CHECK(!has(faults(large, screen(60, 170, 80, 108.f)), "ran into what is beside it")); // (ends where the bar begins)
    CHECK(has(faults(large, screen(60, 170, 50, 107.f, 13.f)), "") == false);
    // Out of its box: past the row's far edge, or above its top; and off the screen.
    CHECK(has(faults(large, screen(60, 600, 50)), "left its box"));
    CHECK(has(faults(large, screen(60, 170, 50, 98.f)), "left its box"));
    CHECK(!has(faults(large, screen(60, 574, 50)), "left its box")); // ends on the edge
    CHECK(has(faults(large, screen(60, 600, 50)), "off the screen"));
    CHECK(!has(faults(large, screen(60, 580, 50)), "off the screen") && has(faults(large, screen(60, 580, 50)), "left its box"));
    // No longer in the middle of its row, up and down (it was: 8 above, 8 below).
    CHECK(has(faults(large, screen(60, 170, 50, 112.f)), "no longer in the middle"));
    CHECK(!has(faults(large, screen(60, 170, 50, 109.f)), "no longer in the middle"));
    CHECK(!has(faults(large, screen(60, 170, 50, 107.f, 14.f)), "no longer in the middle"));
    // Other words, a text more or fewer, a rectangle more or fewer.
    CHECK(has(faults(large, screen(60, 170, 50, 108.f, 12.f, "100")), "other words"));
    CHECK(has(faults(large, screen(60, 170, 50, 108.f, 14.f, "100% F")), "other words")); // more words in BIGGER lettering: no
    CHECK(has(faults(large, screen(60, 170, 50, 108.f, 10.f, "10")), "other words"));     // fewer in smaller: no
    CHECK(faults(large, screen(60, 170, 50, 109.f, 10.f, "100% FULL")).empty());          // a cut name shows more of itself in smaller lettering
    {
        Frame more = screen(60, 170, 50);
        text(more, "EXTRA", 400, 108, 30);
        CHECK(has(faults(large, more), "texts are drawn"));
        Frame fewer = screen(60, 170, 50);
        fewer.boxes.pop_back();
        CHECK(has(faults(large, fewer), "rectangles are drawn"));
    }
    // What LARGE itself does is the owner's layout, not a fault: a label already over its neighbour may stay so.
    const Frame crowded = screen(150, 170, 50);
    CHECK(faults(crowded, crowded).empty() && faults(crowded, screen(155, 170, 52)).empty());

    // Layers. A card laid over the screen: its words are not held to the row under it, nor the row's words to the card.
    const auto card = [&](float cardTextW, float rowTextW) {
        Frame f;
        order = 0;
        box(f, 0, 0, 640, 480);
        box(f, 8, 100, 104, 38);          // a lane's name box, under the card
        text(f, "CLAP", 15, 112, rowTextW); // its name: the card covers the end of it
        box(f, 40, 30, 560, 440);         // the card
        box(f, 40, 30, 560, 440, false);  // its edge
        text(f, "START", 62, 112, cardTextW);
        return f;
    };
    CHECK(faults(card(40, 47), card(60, 52)).empty());
    CHECK(has(faults(card(40, 47), card(545, 47)), "left its box")); // but the card's own edge holds its words
    // A chip that reaches over the next cell: its text is on the chip, not in the cell.
    const auto chip = [&](float w, bool withChip) {
        Frame f;
        order = 0;
        box(f, 0, 0, 640, 480);
        box(f, 100, 100, 23, 19); // the cell beside
        if (withChip) box(f, 78, 102, w + 6, 14);
        text(f, "Dm", 81, 103, w);
        return f;
    };
    CHECK(faults(chip(18, true), chip(22, true)).empty());
    CHECK(has(faults(chip(18, false), chip(22, false)), "ran into what is beside it"));
    // A chip snug round its text, inside a row: the chip grows with the text, and the row still holds it.
    {
        const auto chipped = [&](float w) {
            Frame f;
            order = 0;
            box(f, 0, 0, 640, 480);
            box(f, 100, 100, 60, 20);      // the row
            box(f, 110, 103, w + 6, 14);   // the chip, drawn in it
            text(f, "Dm7", 113, 104, w);
            return f;
        };
        CHECK(faults(chipped(30), chipped(40)).empty());
        CHECK(has(faults(chipped(30), chipped(50)), "left its box")); // out of the row, though still on its chip
    }
    // A mark that only touches a text (the end of a line) is not a layer over it.
    {
        const auto marked = [&](float w) {
            Frame f;
            order = 0;
            box(f, 0, 0, 640, 480);
            text(f, "OCTAVE 3", 33, 30, 70);
            box(f, 40, 35, 1, 8);  // a mark drawn after it and across it: a pixel wide, not a layer
            text(f, "OCTAVE 4", 110, 30, w);
            return f;
        };
        CHECK(has(faults(marked(70), marked(70)), "") == false);
        Frame wide = marked(70);
        wide.texts[0].w = 76; // into its neighbour all the same
        CHECK(has(faults(marked(70), wide), "ran into its neighbour"));
    }
    // What the program writes down ("--texts") is read back as it was drawn.
    const std::string folder = tempFolder("textsize"), file = folder + "/texts";
    writeTextFile(file, "SIZE 0\nT 3 10 26.5 108 60 10 TEMPO: 120 BPM\nB 0 1 0 0 640 480\nB 1 0 16 100 608 28\nSIZE 1\nSIZE 2\nT 0 12 1 2 3 4 A\nSIZE 3\n");
    Frame read[4];
    CHECK(readFrames(file.c_str(), read) == 4);
    CHECK(read[0].texts.size() == 1 && read[0].boxes.size() == 2 && read[1].texts.empty() && read[2].texts.size() == 1 && read[3].boxes.empty());
    if (read[0].texts.size() == 1 && read[0].boxes.size() == 2) {
        const Text& t = read[0].texts[0];
        CHECK(t.order == 3 && t.cap == 10.f && t.x == 26.5f && t.y == 108.f && t.w == 60.f && t.h == 10.f && t.words == "TEMPO: 120 BPM");
        CHECK(read[0].boxes[0].solid && !read[0].boxes[1].solid && read[0].boxes[1].order == 1 && read[0].boxes[1].x == 16.f && read[0].boxes[1].h == 28.f);
    }
    CHECK(readFrames((folder + "/none").c_str(), read) == 0);
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
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
textfit::Frame lastLarge, lastXl; // what the last holds() drew, at LARGE and at XL
// Did the last holds() draw these words (so the check was of them)? At XL, in which lettering: 0 none, else the capitals' height.
bool drew(const char* words) {
    for (const textfit::Text& t : lastLarge.texts)
        if (t.words == words) return true;
    return false;
}
float capAtXl(const char* words, float below = 0.f) { // (`below`: not the one in the top bar)
    for (const textfit::Text& t : lastXl.texts)
        if (t.words == words && t.y >= below) return t.cap;
    return 0.f;
}
// Is this text, held at LARGE at XL, drawn at XL just where LARGE draws it (the same letters in the same place)?
bool asAtLarge(const char* words, float below = 0.f) {
    for (const textfit::Text& was : lastLarge.texts)
        for (const textfit::Text& is : lastXl.texts)
            if (was.words == words && is.words == words && was.y >= below && is.y >= below)
                return std::fabs(was.x - is.x) < 0.001f && std::fabs(was.y - is.y) < 0.001f && std::fabs(was.w - is.w) < 0.001f && was.cap == is.cap; // (the first of each)
    return false;
}
// Words made of `letter` that fit `room` in a font at LARGE and not at XL: the case Fonts::within is for.
std::string fitsLargeOnly(Font Fonts::*font, float room, char letter = 'W') {
    Screens& s = screens();
    std::string words;
    while ((s.at[kTextLarge].get()->*font).width((words + letter).c_str()) <= room) words += letter;
    while (!words.empty() && (s.at[kTextXl].get()->*font).width(words.c_str()) <= room) words += 'I', words.erase(0, 1); // (cannot happen with a wide letter; guards the loop)
    return words;
}
// The screen as it is now, at every size:
//   - with LARGE chosen it is, pixel for pixel, what the lettering alone at LARGE draws (as on main);
//   - at the other three no text has left its box, run into its neighbour or lost its middle (textfit.hpp);
//   - the two bars, and everything from `strip` down (the strip under a grid; 0: none), are the same pixels at every size
//     (`overTheBar`: but for the hints bar, where the help card is drawn down over it).
// Gives how many of the screen's texts are drawn in another size than at LARGE, at XL: what follows the setting.
int holds(Rig& rig, const char* what, int strip = 0, bool overTheBar = false) {
    const bool say = true;
    ++holdsCount;
    Screens& s = screens();
    const Drawn alone = rig.draw();
    const Drawn large = rig.draw(s.at[kTextLarge].get());
    const bool same = alone.pixels == large.pixels;
    if (!same && say) std::printf("    %s: with LARGE chosen it is not what LARGE alone draws\n", what);
    CHECK(same);
    CHECK(!large.frame.texts.empty());
    lastLarge = large.frame;
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
        if (say && !(top && (bottom || overTheBar) && under)) std::printf("    %s: %s: %s is not as at LARGE\n", what, kTextSizeNames[size], !top ? "the top bar" : !under ? "the strip" : "the hints bar");
        CHECK(top);
        CHECK(bottom || overTheBar); // (the help card comes down over the hints bar)
        CHECK(under);
        if (size == kTextXl) lastXl = now.frame;
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
    const float points[kTextSizes][3] = {{12.f, 14.f, 16.f}, {13.25f, 15.5f, 18.f}, {14.5f, 17.f, 20.f}, {15.5f, 18.5f, 21.5f}};
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

// (A macro is a dial, design A: drawn here at MAIN's height, 114, where its name and value stand above and under it.)
// A box's name and value (the DEVICE pages) are held at LARGE where XL is too wide for the box; and what a frame drew is written down.
void testTheBoxes() {
    Screens& s = screens();
    if (!s.canvas.ok) return;
    const Fonts& xl = *s.at[kTextXl];
    const int w = 153;
    const std::string name = fitsLargeOnly(&Fonts::small, w - 14.f), value = fitsLargeOnly(&Fonts::large, w - 14.f);
    const std::string pictured = fitsLargeOnly(&Fonts::small, w - 14.f - 27.f);
    CHECK(name.size() > 4 && value.size() > 3 && pictured.size() > 3 && pictured.size() < name.size());
    DrawLog log;
    gDrawLog = &log;
    ui::valueBox(s.canvas.r, xl, 10, 40, w, 70, name.c_str(), value.c_str(), 0.5f, false, false);
    ui::valueBox(s.canvas.r, xl, 10, 140, w, 70, "CUTOFF", "1 KHZ", 0.5f, true, false);
    ui::macroBox(s.canvas.r, xl, 200, 40, w, 114, name.c_str(), value.c_str(), 0.7f, false, false);         // (no picture goes by that name)
    ui::macroBox(s.canvas.r, xl, 200, 140, w, 114, "TONE", "+60", 0.7f, true, false);                        // a picture in its corner
    ui::macroBox(s.canvas.r, xl, 400, 40, w, 114, "TONE", value.c_str(), 0.7f, false, false);
    gDrawLog = nullptr;
    CHECK(log.texts.size() == 10);
    if (log.texts.size() == 10) {
        const float held[10] = {12.f, 16.f, 13.f, 17.f, 12.f, 16.f, 13.f, 17.f, 13.f, 16.f}; // too wide at XL: LARGE's capitals; else XL's
        for (int i = 0; i < 10; ++i) CHECK(log.texts[static_cast<std::size_t>(i)].font->capHeight() == held[i]);
        for (const DrawLog::Text& t : log.texts) CHECK(t.x + t.w <= (t.x < 200.f ? 10 : t.x < 400.f ? 200 : 400) + w - 7.f); // every one ends inside its box
        // A text is written down with where its capitals are and how wide it is.
        CHECK(log.texts[2].words == "CUTOFF" && log.texts[2].x == 17.f && log.texts[2].y == 147.f && log.texts[2].w == xl.small.width("CUTOFF") && log.texts[2].h == 13.f);
    }
    // A macro whose name has a picture leaves the picture its corner: a name that would reach it is held at LARGE.
    log.clear();
    gDrawLog = &log;
    ui::macroBox(s.canvas.r, xl, 200, 40, w, 114, "TONE", "+0", 0.5f, false, false);
    gDrawLog = nullptr;
    CHECK(log.texts.size() == 2 && log.texts[0].font == &xl.small);
    CHECK(&xl.within(&Fonts::small, pictured.c_str(), w - 14.f - 27.f) == &xl.fixed().small && &xl.within(&Fonts::small, pictured.c_str(), w - 14.f) == &xl.small);
    {
        // In the box itself: RELEASE in one narrow enough that at XL it would reach its picture, and not the box's end.
        const int narrow = 110;
        CHECK(xl.small.width("RELEASE") > narrow - 14.f - 27.f && xl.small.width("RELEASE") <= narrow - 14.f && xl.fixed().small.width("RELEASE") <= narrow - 14.f - 27.f);
        log.clear();
        gDrawLog = &log;
        ui::macroBox(s.canvas.r, xl, 200, 240, narrow, 114, "RELEASE", "+0", 0.5f, false, false);
        gDrawLog = nullptr;
        CHECK(log.texts.size() == 2 && log.texts[0].font == &xl.fixed().small && log.texts[1].font == &xl.large);
    }

    // Rectangles: a filled one is solid unless its colour lets what is under it show; an outline is not; a box is both. In the order drawn.
    log.clear();
    gDrawLog = &log;
    ui::fill(s.canvas.r, 1, 2, 30, 40, ui::kPanel);
    ui::fill(s.canvas.r, 5, 6, 7, 8, ui::faded(ui::kBg, 200));
    xl.small.draw(s.canvas.r, 9.f, 9.f, "", ui::kText); // nothing to draw: not written down
    xl.small.draw(s.canvas.r, 9.f, 9.f, "A", ui::kText);
    ui::outline(s.canvas.r, 11, 12, 13, 14, ui::kTeal, 2);
    ui::box(s.canvas.r, 21, 22, 23, 24, ui::kPanel, ui::kLine);
    gDrawLog = nullptr;
    ui::fill(s.canvas.r, 0, 0, 5, 5, ui::kBg); // (nothing is written down once the log is put away)
    CHECK(log.boxes.size() == 5 && log.texts.size() == 1 && log.drawn == 6);
    if (log.boxes.size() == 5 && log.texts.size() == 1) {
        CHECK(log.boxes[0].solid && log.boxes[0].order == 0 && log.boxes[0].x == 1 && log.boxes[0].y == 2 && log.boxes[0].w == 30 && log.boxes[0].h == 40);
        CHECK(!log.boxes[1].solid && log.boxes[1].order == 1);
        CHECK(log.texts[0].order == 2 && log.texts[0].words == "A");
        CHECK(!log.boxes[2].solid && log.boxes[2].order == 3 && log.boxes[2].x == 11 && log.boxes[2].h == 14);
        CHECK(log.boxes[3].solid && log.boxes[3].order == 4 && !log.boxes[4].solid && log.boxes[4].order == 5 && log.boxes[4].w == 23);
    }
    const textfit::Frame frame = textfit::frameOf(log);
    CHECK(frame.texts.size() == 1 && frame.boxes.size() == 5 && frame.texts[0].cap == 13.f && frame.boxes[1].solid == false && frame.boxes[3].solid && frame.boxes[4].x == 21.f);
    log.clear();
    CHECK(log.texts.empty() && log.boxes.empty() && log.drawn == 0);
}

// ---- the setting: kept, read back, an old file reads LARGE ------------------------------------
void testTheSettingIsKept() {
    const std::string folder = tempFolder("textsize"), file = folder + "/settings.txt";
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
            session.keepSettingsNow(); // (written once it has stood still a second: kKeepRestMs)
            CHECK(session.textSize() == size);
            CHECK(readTextFile(file, kept) && kept == "stick " + std::to_string(static_cast<int>(std::round(ui::gStickSpeed * 100.f))) + "\noctaves 2\nmidirec 0\ntext " + std::to_string(size) + "\ntheme 0\nbrightness 70\nlight 1\nclick 0\ncountin 0\n");
        }
        // It has ends.
        session.setTextSize(99);
        CHECK(session.textSize() == kTextXl);
        session.setTextSize(-5);
        CHECK(session.textSize() == kTextSmall);
        session.keepSettingsNow();
        CHECK(readTextFile(file, kept) && kept.find("text 0\n") != std::string::npos);
        // Another setting changed keeps the size too.
        session.setTextSize(kTextXl);
        session.setOctavesShown(3);
        session.keepSettingsNow();
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
    // Among the handheld's rows, after STICK SPEED; thirteen rows now, THEME after it (tests/theme_test.cpp).
    // (Under LOOK since the page's headings: after USB KEYBOARD, the last of PLAYING.)
    CHECK(S::kRows == 24 && S::kTextSize == S::kMidi + 1 && S::kTheme == S::kTextSize + 1 && S::kOctaves == S::kTheme + 1 && S::groupOf(S::kTextSize) == S::kLook);
    setup.goTo(S::kMidi);
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
    // A, X and Y do nothing to it; B gives it its usual, LARGE (gesture audit G17: it did nothing).
    for (SDL_Keycode other : {A, X, Y}) rig.tap(other);
    CHECK(session.textSize() == kTextSmall && setup.cursorRow() == S::kTextSize && !setup.typing() && !setup.testing());
    rig.tap(B);
    CHECK(session.textSize() == kTextLarge && setup.cursorRow() == S::kTextSize);
    rig.tap(LEFT, 2);
    CHECK(session.textSize() == kTextSmall);
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
    // The other rows are where they were, by name: down from TEXT SIZE is THEME, then PIANO ROLL.
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kTheme);
    rig.tap(DOWN);
    CHECK(setup.cursorRow() == S::kOctaves);
    rig.tap(RIGHT);
    CHECK(session.octavesShown() == 3 && session.textSize() == kTextLarge);

    // As drawn. Each size's own name is on the row, in that size; twelve rows shown, none over another, the last clear of the hints bar.
    setup.goTo(S::kTextSize);
    for (int first = 0; first < setup.places(); ++first)
        for (int place = first; place < setup.places() && setup.fits(place, first); ++place) {
            CHECK(setup.placeY(place, first) >= ui::kTopBarH + 8 && setup.placeY(place, first) + S::kRowH - 6 <= ui::kScreenH - ui::kHintsH - 4);
            if (place > first) CHECK(setup.placeY(place, first) >= setup.placeY(place - 1, first) + S::kRowH - 6 + 4);
        }
    for (int size = 0; size < kTextSizes; ++size) {
        session.setTextSize(size);
        rig.frames(1);
        const Fonts& fonts = *s.at[size];
        const Drawn drawn = rig.draw(&fonts);
        bool name = false, value = false, hintChange = false;
        const float boxTop = static_cast<float>(setup.rowNowY(S::kTextSize)), boxBottom = boxTop + S::kRowH - 6;
        for (const textfit::Text& t : drawn.frame.texts) {
            if (t.words == "TEXT SIZE") name = t.x == S::kNameX && t.cap == fonts.small.capHeight() && near(t.y - boxTop, boxBottom - (t.y + t.h)); // in the middle of its row
            if (t.words == kTextSizeNames[size] && t.x == S::kValueX)
                value = t.cap == fonts.large.capHeight() && t.y >= boxTop && t.y + t.h <= boxBottom && near(t.y - boxTop, boxBottom - (t.y + t.h)); // in the middle of its row
            if (t.words == "change") hintChange = true;
        }
        CHECK(name);
        CHECK(value);
        CHECK(hintChange); // LEFT / RIGHT: change, as on the page's other rows that are changed so
        // The value is lit where the cursor is: teal letters at the value's place.
        int teal = 0;
        for (int y = setup.rowNowY(S::kTextSize); y < setup.rowNowY(S::kTextSize) + S::kRowH - 6; ++y)
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

// ---- NETWORK, on our firmware (wifi.hpp): the row, the list held, the password's keys ------------
void testTheNetworkRow() {
    Screens& s = screens();
    if (!s.canvas.ok) return;
    typedef SettingsScreen S;
    Rig rig;
    Session& session = rig.session();
    Wifi::Now now;
    now.scanned = true;
    now.networks = wifiNetworksFrom("-40 wpa2 WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW\n-52 wpa2 Hollow Lane Guest\n-60 open Cafe\n");
    const char* const statuses[] = {"off", "joining WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", "wrong-password WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", "not-found Hollow Lane Guest",
                                    "joined WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW 192.168.100.200 apotheca-2.local", "joined Cafe 192.168.1.7"};
    session.view = View::Settings;
    S& setup = rig.app.settings();
    for (const char* status : statuses) {
        now.status = wifiStatusFrom(status);
        session.pretendWifi(now, "192.168.100.200:8080/4821");
        setup.goTo(S::kNetwork);
        rig.frames(1);
        CHECK(setup.cursorRow() == S::kNetwork);
        CHECK(holds(rig, status) > 10);
        setup.goTo(S::kWifi); // (the page's two addresses, the cursor on them)
        rig.frames(1);
        CHECK(holds(rig, "WIFI PAGE with the page's name") > 10);
    }
    // Held: each network of the list, the longest name with its lock.
    now.status = wifiStatusFrom("off");
    session.pretendWifi(now, "192.168.100.200:8080/4821");
    setup.goTo(S::kNetwork);
    rig.key(A, true);
    rig.frames(1);
    for (int i = 0; i < 3; ++i) {
        rig.tap(DOWN);
        CHECK(setup.picked() == i);
        CHECK(holds(rig, "NETWORK, A held") > 10);
    }
    rig.tap(UP, 2);
    rig.key(A, false);
    rig.frames(1);
    // The password's keys, each set, empty and full.
    CHECK(setup.typingPassword());
    for (int set = 0; set < 3; ++set) {
        CHECK(holds(rig, "the password's keys") >= 0); // (the keys are a tight grid: drawn at LARGE at every size)
        rig.tap(X);
    }
    rig.tap(DOWN, 2), rig.tap(RIGHT, 2); // (W: the widest)
    rig.tap(X);
    for (int i = 0; i < 63; ++i) rig.tap(A);
    rig.tap(Y, 60);
    CHECK(holds(rig, "the password's keys, too short") >= 0); // (the keys are a tight grid: drawn at LARGE at every size)
    rig.tap(B); // too short to join: leaves, keeping the draft
    CHECK(!setup.typing());
    rig.key(A, true), rig.frames(1); // back: the draft is still there
    rig.tap(DOWN);
    rig.key(A, false), rig.frames(1);
    CHECK(setup.typingPassword() && static_cast<int>(setup.typed().size()) == 3);
    for (int i = 0; i < 60; ++i) rig.tap(A);
    CHECK(static_cast<int>(setup.typed().size()) == 63);
    CHECK(holds(rig, "the password's keys, full") >= 0); // (the keys are a tight grid: drawn at LARGE at every size)
    rig.tap(Y, 63);
    rig.tap(B);
    CHECK(!setup.typing());
    // Forgetting.
    now.status = wifiStatusFrom("joined Hollow Lane Guest 192.168.1.7 apotheca.local");
    session.pretendWifi(now, "192.168.1.7:8080/4821");
    rig.frames(1);
    rig.key(B, true);
    rig.frames(30);
    CHECK(setup.forgetPart() > 0.f);
    CHECK(holds(rig, "NETWORK, forgetting") > 10);
    rig.key(B, false);
    rig.frames(1);
}

// ---- the tight rows stay at LARGE ---------------------------------------------------------------
// The top bar, the hints bar and the strips under the two grids are the same pixels at every
// size (so every test that reads a strip or a bar off the screen at LARGE holds at every
// setting); the grid above them follows the setting.
const int kDrumStrip = pattern_layout::kStripY;
int rollStrip(int octaves) { return 45 + (octaves == 1 ? 24 : octaves == 2 ? 22 : 16) + 2 + 16 * 21 + 4; } // under the keys and sixteen rows of 21, 4 down

void testTheDrumGridsRows() {
    const std::string folder = tempFolder("textsize");
    std::vector<float> sound(48000);
    for (std::size_t i = 0; i < sound.size(); ++i) sound[i] = 0.4f * std::sin(static_cast<float>(i) * 0.05f);
    CHECK(writeWav(folder + "/a very long recording name.wav", sound, 48000)); // (a lane's name at its longest)
    Rig rig;
    rig.session().useSampleFolder(folder);
    rig.session().view = View::Pattern;
    PatternScreen& drums = rig.app.drums();
    rig.session().setLaneSample(0, 5, "a very long recording name");
    rig.session().setLaneFit(0, 5, 16);
    rig.session().finishStretching();
    drums.setStep(1, 4, 0.8f);
    StepFx fx;
    fx.pitch = -12, fx.ratchet = 4, fx.reverse = true, fx.cut = 2, fx.throwTo = 3; // the strip at its fullest
    drums.setFx(1, 4, fx);
    drums.setStart(1, 4, 0.5f);
    drums.setStep(5, 2, 0.6f);
    drums.moveCursorTo(1, 4);
    rig.frames(2);
    CHECK(holds(rig, "the drum grid, on a hit", kDrumStrip) == 16 + 7); // the step numbers and the lanes' names follow the setting, but for the one name too long at XL
    drums.moveCursorTo(1, 5);
    rig.frames(1);
    holds(rig, "the drum grid, on an empty step", kDrumStrip);
    drums.moveCursorTo(1, 4);
    rig.key(X, true); // its step effects, a chip under each
    rig.frames(2);
    CHECK(drums.effecting());
    holds(rig, "the drum grid, X held on a hit", kDrumStrip);
    rig.key(X, false);
    rig.key(A, true); // level and nudge
    rig.frames(2);
    holds(rig, "the drum grid, A held on a hit", kDrumStrip);
    rig.key(A, false);
    rig.key(Y, true); // chance and start
    rig.frames(2);
    holds(rig, "the drum grid, Y held on a hit", kDrumStrip);
    rig.key(Y, false);
    drums.moveCursorTo(5, -1); // on a lane's name: the longest there is
    rig.frames(2);
    holds(rig, "the drum grid, on a lane's name", kDrumStrip);
    rig.key(A, true);
    rig.frames(2);
    holds(rig, "the drum grid, A held on a lane's name", kDrumStrip);
    rig.key(A, false);
    rig.key(Y, true);
    rig.frames(2);
    holds(rig, "the drum grid, the dice on a lane", kDrumStrip);
    rig.key(Y, false);
    // FEEL: the name's one purple line, and X held (the strip is the feel): tight rows, the same at every size.
    rig.session().setLaneFeel(0, 5, 100, 100);
    rig.session().setLaneSwing(0, 5, 100); // (SWING too: the line at its widest)
    rig.frames(2);
    holds(rig, "the drum grid, on a felt lane's name", kDrumStrip);
    CHECK(drew("SWING 100%   HUMAN TIME 100%   HUMAN LEVEL 100%"));
    rig.key(X, true);
    rig.frames(2);
    CHECK(drums.feeling());
    holds(rig, "the drum grid, X held on a lane's name", kDrumStrip);
    rig.key(X, false);
    rig.session().setLaneFeel(0, 5, 0, 0);
    rig.session().setLaneSwing(0, 5, 0);
    rig.frames(40); // (what X heard has rung out)
    // The tempo: its box is in the top bar, which holds() compares.
    drums.moveCursorTo(0, 0);
    rig.tap(UP);
    holds(rig, "the drum grid, on the tempo", kDrumStrip);
    rig.key(A, true);
    rig.frames(2);
    holds(rig, "the drum grid, A held on the tempo", kDrumStrip);
    rig.key(A, false);
    rig.tap(DOWN);
    // The slice view of the recording's lane: the same strip at the same height.
    drums.moveCursorTo(5, 2);
    rig.frames(1);
    rig.tap(L1);
    CHECK(drums.slicing());
    CHECK(holds(rig, "the slice view", kDrumStrip) > 16);
    rig.key(X, true);
    rig.frames(2);
    holds(rig, "the slice view, X held on a hit", kDrumStrip);
    rig.key(X, false);
    rig.key(SDLK_a, true); // Y held on a hit: R1 saves the slice (batch S1)
    rig.frames(2);
    holds(rig, "the slice view, Y held on a hit", kDrumStrip);
    CHECK(ui::gHintsOver == 0);
    rig.tap(SDLK_w);
    rig.key(SDLK_a, false);
    rig.frames(2);
    CHECK(std::string(drums.said()).find("SAVED \"") == 0);
    holds(rig, "the slice view, a slice saved", kDrumStrip);
    drums.moveCursorTo(5, -1);
    rig.frames(2);
    holds(rig, "the slice view, on the lane's name", kDrumStrip);
    // ONE SLICE, beside the lane's picture: wider than the name's column at XL, so it is held at LARGE there (RUNS ON fits).
    CHECK(drew("RUNS ON") && capAtXl("RUNS ON") == screens().at[kTextXl]->medium.capHeight());
    rig.project().tracks[0].laneOneSlice[5] = true;
    rig.frames(1);
    holds(rig, "the slice view, a lane that plays one slice", kDrumStrip);
    CHECK(drew("ONE SLICE") && capAtXl("ONE SLICE") == screens().at[kTextLarge]->medium.capHeight());
    CHECK(screens().at[kTextXl]->medium.capHeight() > screens().at[kTextLarge]->medium.capHeight());
    // SLICES AT THE HITS (batch S2): the lane's own cuts, at the widest word (AT HITS 10), by the name and on the
    // strip with A held; Y held on a hit (move, add, delete its cut); and the longest of what is said.
    {
        LaneCuts cuts;
        cuts.count = 12;
        cuts.sense = kSenseMost;
        for (int k = 1; k < cuts.count; ++k) cuts.at[k] = static_cast<float>(k) / 13.f;
        rig.session().setLaneCuts(0, 5, cuts);
        rig.frames(1);
        holds(rig, "the slice view, cut at the hits", kDrumStrip);
        CHECK(drew("AT HITS 10") && drew("12 SLICES"));
        rig.key(A, true);
        rig.frames(2);
        holds(rig, "the slice view, A held on the name: the slices", kDrumStrip);
        CHECK(ui::gHintsOver == 0);
        rig.key(A, false);
        cuts.sense = 0;
        rig.session().setLaneCuts(0, 5, cuts);
        rig.frames(1);
        holds(rig, "the slice view, cut by hand", kDrumStrip);
        CHECK(drew("BY HAND"));
        drums.moveCursorTo(5, 3);
        drums.setStep(5, 3, 0.9f);
        rig.key(SDLK_a, true);
        rig.frames(2);
        holds(rig, "the slice view, Y held on a hit of its own cuts", kDrumStrip);
        CHECK(ui::gHintsOver == 0);
        drums.moveCursorTo(5, 0);
        drums.setStep(5, 0, 0.9f);
        rig.tap(SDLK_z); // B on slice 1: SLICE 1 ALWAYS STARTS AT THE TOP
        rig.key(SDLK_a, false);
        rig.frames(2);
        CHECK(std::string(drums.said()) == "SLICE 1 ALWAYS STARTS AT THE TOP");
        holds(rig, "the slice view, slice 1 stays at the top", kDrumStrip);
        rig.session().setLaneCuts(0, 5, LaneCuts());
        rig.project().tracks[0].laneOneSlice[5] = true;
        rig.frames(1);
    }
    // WRITE on, a scene of several bars, a twelfth track with the longest name: the top bar at its fullest.
    rig.tap(L1);
    rig.session().setTrackName(0, "WWWWWWWWWW");
    rig.session().setCellBars(0, 0, 4);
    rig.project().writing = true;
    rig.frames(1);
    holds(rig, "the drum grid, the top bar at its fullest", kDrumStrip);
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
}

void testThePianoRollsRows() {
    Rig rig;
    CHECK(rig.session().addTrack(TrackKind::Phazed));
    rig.project().track = 1;
    rig.session().view = View::Pattern;
    NotePattern& p = rig.project().tracks[1].notes[0][0];
    const ChordPack& pack = builtInPack();
    p.chords[0] = makeChord(pack.chords[1], 50, 4, 3.f, .7f, 0);
    p.chords[0].play = chordPlayOf(2, 3, 100, 100, 100); // WIDE STRUM, RANDOM, everything at its most
    p.chords[0].ratchet = 4;
    p.chords[0].throwTo = 3;
    p.chords[1] = makeChord(pack.chords[0], 48, 0, 3.f, .7f, 0); // on the first step: its name sits on the note
    p.notes[0].step = 9, p.notes[0].pitch = 64, p.notes[0].length = 2.f, p.notes[0].velocity = 0.8f;
    p.notes[0].nudge = -0.45f, p.notes[0].ratchet = 4, p.notes[0].glide = 2, p.notes[0].throwTo = 2; // the strip's longest line
    p.notes[1].step = 12, p.notes[1].pitch = 60, p.notes[1].length = 1.f, p.notes[1].velocity = 0.8f;
    rig.session().sendNotePattern(1, 0);
    NoteScreen& roll = rig.app.notes();
    for (int octaves = 1; octaves <= 3; ++octaves) {
        rig.session().setOctavesShown(octaves);
        const int strip = rollStrip(octaves);
        char what[64];
        const auto say = [&](const char* state) {
            std::snprintf(what, sizeof what, "the piano roll, %d octave%s, %s", octaves, octaves == 1 ? "" : "s", state);
            return what;
        };
        roll.moveCursorTo(64, 9);
        rig.frames(2);
        CHECK(holds(rig, say("on a note"), strip) >= 16); // the steps' numbers, the octaves' names and the chords' names follow the setting
        rig.key(X, true);
        rig.frames(2);
        CHECK(roll.effecting());
        holds(rig, say("X held on a note"), strip);
        rig.key(X, false);
        rig.key(A, true);
        rig.frames(2);
        holds(rig, say("A held on a note"), strip);
        rig.key(A, false);
        roll.moveCursorTo(62, 14);
        rig.frames(2);
        holds(rig, say("on an empty step"), strip);
        int keys[kChordNotes];
        p.chords[0].pitches(keys);
        roll.moveCursorTo(keys[0], 4);
        rig.frames(2);
        holds(rig, say("on a chord"), strip);
        rig.key(X, true);
        rig.frames(2);
        for (int effect = 0; effect < kChordEffects; ++effect) {
            roll.pickEffect(effect);
            holds(rig, say("X held on a chord"), strip);
        }
        rig.key(X, false);
        rig.frames(1);
        rig.key(Y, true); // the chord tiles
        rig.frames(2);
        rig.tap(RIGHT, 2);
        holds(rig, say("Y held: the chord tiles"), strip);
        rig.key(Y, false);
        rig.frames(2);
    }
    rig.session().setOctavesShown(2);
}

const int kLastWholeWord = 4; // the most tracks whose heads have room for DELETING whole; past it, DEL (5 before MASTER took a column)

// B held on a track of SONG: frames go by until its head says what is coming (the words flash).
void holdUntilItSays(Rig& rig) {
    for (int frame = 0; frame < 80; ++frame) {
        rig.frames(1);
        for (const textfit::Text& t : rig.draw().frame.texts)
            if (t.words == "DEL" || t.words == "DELETING") return;
    }
}

// ---- the other screens at their fullest ---------------------------------------------------------
void testTheOtherScreens() {
    const std::string folder = tempFolder("textsize"), samples = folder + "/samples";
    mkdir(samples.c_str(), 0755);
    mkdir((samples + "/a folder with a long name").c_str(), 0755);
    std::vector<float> sound(24000);
    for (std::size_t i = 0; i < sound.size(); ++i) sound[i] = 0.4f * std::sin(static_cast<float>(i) * 0.05f);
    CHECK(writeWav(samples + "/a folder with a long name/a recording with a very long name indeed.wav", sound, 48000));
    CHECK(writeWav(samples + "/short.wav", sound, 48000));
    // Two folders down, each name as long as a folder's may be, and an empty folder beside it.
    const std::string deep = "a folder with a long name/another just as long as it/x of names";
    mkdir((samples + "/a folder with a long name/another just as long as it").c_str(), 0755);
    mkdir((samples + "/a folder with a long name/nothing in this folder yet").c_str(), 0755);
    CHECK(writeWav(samples + "/" + deep + ".wav", sound, 48000) && sampleNameOk(deep) && deep.size() == 63);
    // A short one two folders down (a lane says the inner folder), and a folder of more than the list shows at once.
    mkdir((samples + "/ab").c_str(), 0755);
    mkdir((samples + "/ab/cd").c_str(), 0755);
    mkdir((samples + "/mm").c_str(), 0755);
    CHECK(writeWav(samples + "/ab/cd/ef.wav", sound, 48000));
    CHECK(writeWav(samples + "/ab/a dozen chars.wav", sound, 48000)); // (too long a name to carry two folders down)
    for (int i = 1; i <= 14; ++i) {
        char many[32];
        std::snprintf(many, sizeof many, "/mm/m %02d.wav", i);
        CHECK(writeWav(samples + many, sound, 48000));
    }
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    session.useSampleFolder(samples);
    const std::string longest = "a folder with a long name/a recording with a very long name indeed";

    // SONG: twelve tracks, the narrowest columns there are, every one with a name of ten wide letters.
    const TrackKind kinds[] = {TrackKind::Phazed, TrackKind::Jx3p, TrackKind::Fm6, TrackKind::Sampler, TrackKind::Drums, TrackKind::Physic, TrackKind::Acid, TrackKind::Cloud, TrackKind::Morph};
    for (int t = 1; t < kNumTracks; ++t) CHECK(session.addTrack(kinds[(t - 1) % 9]));
    session.view = View::Song;
    rig.frames(2);
    CHECK(holds(rig, "SONG, twelve tracks") > 12);
    for (int t = 0; t < kNumTracks; ++t) session.setTrackName(t, t % 2 ? "WWWWWWWWWW" : "MMMM MMMMM");
    project.tracks[2].muted = true;
    project.tracks[3].solo = true;
    session.setCellBars(1, 0, 4);
    rig.frames(2);
    holds(rig, "SONG, twelve tracks with long names");
    {
        // Track 4's number on its narrow head (beside the S or M mark) is at LARGE whatever is chosen; the scene's own
        // number 4 follows the setting. A four-bar cell has no count over its little picture (owner, 2026-11-14).
        int held = 0, follows = 0;
        for (const textfit::Text& t : lastXl.texts)
            if (t.words == "4") (t.cap == screens().at[kTextLarge]->small.capHeight() ? held : follows) += 1;
        CHECK(held == 1 && follows >= 1);
    }
    rig.app.song().moveCursorTo(0, 1);
    rig.key(B, true); // deleting a track: its head says so
    holdUntilItSays(rig);
    holds(rig, "SONG, a track being deleted");
    rig.key(B, false);
    rig.frames(2);
    // Every number of tracks there can be: the heads' words (DELETING, or DEL in a narrow column; + TRACK, +)
    // are picked by the column's width, which was worked out at LARGE.
    for (int tracks = 1; tracks <= kNumTracks; ++tracks) {
        Rig some;
        for (int t = 1; t < tracks; ++t) CHECK(some.session().addTrack(TrackKind::Phazed));
        some.session().view = View::Song;
        some.app.song().moveCursorTo(0, tracks - 1);
        some.frames(2);
        char what[48];
        std::snprintf(what, sizeof what, "SONG, %d track%s", tracks, tracks == 1 ? "" : "s");
        holds(some, what);
        CHECK(tracks == kNumTracks || drew("+ TRACK") || drew("+"));
        some.key(B, true);
        holdUntilItSays(some);
        std::snprintf(what, sizeof what, "SONG, %d track%s, one being deleted", tracks, tracks == 1 ? "" : "s");
        holds(some, what);
        CHECK(drew("DELETING") || drew("DEL"));
        CHECK(!drew("DELETING: NO UNDO") && !drew("DELETING TRACK") && !drew("DELETE"));
        if (std::getenv("DEL_PRINT")) std::printf("%d tracks: %s\n", tracks, drew("DELETING") ? "DELETING" : "DEL");
        CHECK(drew("DELETING") == (tracks <= kLastWholeWord));
        some.key(B, false);
    }
    // SONG with one track and with three: the wide columns, where the words are longer.
    {
        Rig few;
        few.session().view = View::Song;
        few.frames(2);
        holds(few, "SONG, one track");
        few.app.song().moveCursorTo(0, 0);
        few.key(B, true);
        holdUntilItSays(few);
        holds(few, "SONG, its one track being deleted");
        few.key(B, false);
        few.app.song().moveCursorTo(0, 1);
        few.tap(A); // the list of instruments
        holds(few, "SONG, the list of instruments");
        CHECK(drew("8 lanes: built-in drums or your recordings") && capAtXl("8 lanes: built-in drums or your recordings") == 12.f && capAtXl("6-operator FM, after a classic 80s synth") == 12.f && capAtXl("one recording, played from the keys") == 13.f);
        CHECK(drew("plucked and struck: strings, mallets, bells") && capAtXl("plucked and struck: strings, mallets, bells") == 12.f); // (held at LARGE at XL)
        few.tap(B);
        // Another instrument for track 1: under the list, what goes with the old one.
        few.app.song().moveCursorTo(0, 0);
        few.tap(UP);
        few.tap(A);
        few.tap(DOWN);
        few.frames(1);
        holds(few, "SONG, another instrument for a track");
        CHECK(drew(SongScreen::kChangeLostWords[0]) && drew(SongScreen::kChangeLostWords[1]));
        few.tap(B);
        few.tap(DOWN);
        few.app.song().moveCursorTo(0, 1);
        // The list of songs, and a song's revs: with the WiFi page off and on.
        mkdir((folder + "/songs").c_str(), 0755);
        few.session().useSongFolder(folder + "/songs", 7u);
        few.session().useSampleFolder(samples);
        few.app.drums().setStep(0, 0, 0.8f); // (a song with nothing in it is never written, and has no revs)
        few.frames(2);
        few.app.song().moveCursorTo(0, 0);
        few.tap(SELECT);
        holds(few, "the list of songs");
        CHECK(drew("SONGS") && drew("KEPT AS YOU GO. NOTHING TO SAVE.") && drew("Recordings, chord packs, songs: from a browser."));
        CHECK(capAtXl("Recordings, chord packs, songs: from a browser.") == 12.f && capAtXl("FILES OVER WIFI: OFF") == 13.f); // the one line too wide at XL is held at LARGE
        few.tap(L1); // files over WiFi: on
        CHECK(few.session().transferOn());
        holds(few, "the list of songs, the WiFi page on");
        CHECK(drew("FILES OVER WIFI: ON"));
        // The address under it, with a port in it (":8080", where the desk would not give port 80), is wider
        // than the panel at XL: it is held at LARGE. Without one (port 80: our firmware, or a desk that runs
        // the tests as root) it fits, and is drawn at XL. Which one a run gets is the desk's (a parallel test
        // may hold either port), so the check follows what was drawn; holds() still finds any that spills.
        bool address = false;
        for (const textfit::Text& t : lastXl.texts)
            if (t.words.compare(0, 17, "OPEN IN A BROWSER") == 0 || t.words.compare(0, 16, "NOT ON A NETWORK") == 0) {
                const std::size_t slash = t.words.rfind('/');
                const bool port = slash != std::string::npos && t.words.rfind(':', slash) != std::string::npos &&
                                  t.words.rfind(':', slash) > t.words.find(':');
                address = t.cap == (port || t.words.compare(0, 16, "NOT ON A NETWORK") == 0 ? 12.f : 13.f);
            }
        CHECK(address);
        few.tap(L1);
        CHECK(!few.session().transferOn());
        few.tap(DOWN); // B held on the song: its row says it cannot be undone
        few.key(B, true);
        few.frames(30);
        holds(few, "the list of songs, B held on one");
        CHECK(drew(SongScreen::kSongDeletingWords));
        few.key(B, false); // (let go sooner than a second: only a tap, which closes the list)
        few.frames(1);
        few.tap(SELECT);
        CHECK(few.app.song().listingSongs() && few.session().songNames().size() == 1);
        few.tap(Y); // a rev is saved: the heading says so, beside SONGS
        holds(few, "the list of songs, a rev just saved");
        CHECK(!drew("KEPT AS YOU GO. NOTHING TO SAVE."));
        few.tap(DOWN);
        few.tap(A); // the open song's revs
        holds(few, "a song's revs");
        CHECK(drew("+ SAVE A REV") && drew("THE SONG AS IT IS NOW") && drew("+ NEW NAME") && drew("TWO OTHER WORDS"));
        // A clear gap between the two on that line at every size (at XL they met: the second is held at LARGE there).
        float saveEnds = 0.f, whatStarts = 0.f, nameEnds = 0.f, otherStarts = 0.f;
        for (const textfit::Text& t : lastXl.texts) {
            if (t.words == "+ SAVE A REV") saveEnds = t.x + t.w;
            if (t.words == "THE SONG AS IT IS NOW") whatStarts = t.x;
            if (t.words == "+ NEW NAME") nameEnds = t.x + t.w;
            if (t.words == "TWO OTHER WORDS") otherStarts = t.x;
        }
        CHECK(saveEnds > 0.f && whatStarts - saveEnds >= 12.f);
        CHECK(nameEnds > 0.f && otherStarts - nameEnds >= 12.f);
    }

    // The DEVICE screens: every page of every synth, the cursor on its first box.
    struct Synth { TrackKind kind; SynthScreen* screen; const char* name; };
    const Synth synths[] = {{TrackKind::Phazed, &rig.app.phazed(), "PHAZED"}, {TrackKind::Jx3p, &rig.app.jaxep(), "JAX-EP"}, {TrackKind::Fm6, &rig.app.fm6(), "FM-6"},
                            {TrackKind::Physic, &rig.app.physic(), "PHYSIC"}, {TrackKind::Acid, &rig.app.acid(), "ACID"}, {TrackKind::Cloud, &rig.app.cloud(), "CLOUD"},
                            {TrackKind::Morph, &rig.app.morph(), "MORPH"}};
    session.view = View::Device;
    int pagesSeen = 0;
    for (const Synth& synth : synths) {
        for (int t = 0; t < kNumTracks; ++t)
            if (project.tracks[t].kind == synth.kind) project.track = t;
        CHECK(project.current().kind == synth.kind);
        for (int page = 0; page < synth.screen->pageCount(); ++page, ++pagesSeen) {
            synth.screen->goTo(page, 0);
            rig.frames(2);
            char what[48];
            std::snprintf(what, sizeof what, "%s, page %d", synth.name, page);
            // (FM-6's operators: six rows, their boxes held at LARGE at XL; the page's name, its count
            // and SIGNAL still follow the setting.)
            const bool sixRows = synth.kind == TrackKind::Fm6 && fm6Spec().itemCount(page) > 20;
            CHECK(holds(rig, what) > (sixRows ? 1 : 3));
        }
        synth.screen->goTo(0, SynthScreen::kFirstMacro);
        rig.key(A, true);
        rig.frames(2);
        holds(rig, "a synth's macro, A held");
        rig.key(A, false);
        rig.frames(1);
    }
    CHECK(pagesSeen == 10 + 6 + 9 + 6 + 3 + 4 + 6);

    // A synth's own preset (gesture audit G01, G30): the name keys at their longest, saying save and then replace;
    // what is said in the top bar after, a long own name on MAIN, and the bar while B deletes it.
    {
        for (int t = 0; t < kNumTracks; ++t)
            if (project.tracks[t].kind == TrackKind::Phazed) project.track = t;
        SynthScreen& phazed = rig.app.phazed();
        session.usePresetFolder(tempFolder("textsize-presets"));
        const std::string longest(SynthScreen::kPresetNameLength, 'W');
        phazed.goTo(0, SynthScreen::kPreset);
        rig.frames(1);
        rig.tap(R1);
        CHECK(phazed.typing() && phazed.typed().empty());
        holds(rig, "a synth's preset keys, nothing typed");
        CHECK(drew("(empty: name it to save)") && drew("leave"));
        rig.tap(DOWN, 2);
        rig.tap(RIGHT, 2); // (W, the widest letter)
        rig.tap(A, SynthScreen::kPresetNameLength + 2);
        CHECK(phazed.typed() == longest);
        holds(rig, "a synth's preset keys, a name at its longest");
        CHECK(drew("24 OF 24") && drew("save"));
        rig.tap(B);
        CHECK(!phazed.typing() && phazed.said() == "SAVED AS " + longest);
        holds(rig, "a synth, SAVED AS a long name");
        rig.tap(R1);
        CHECK(phazed.typed() == longest);
        holds(rig, "a synth's preset keys, B replacing");
        CHECK(drew("replace") && !drew("save"));
        CHECK(drew(SynthScreen::kPresetReplacingWords) && capAtXl(SynthScreen::kPresetReplacingWords) > 0.f); // (whole at XL: not cut)
        rig.tap(B);
        CHECK(phazed.said() == "REPLACED " + longest);
        holds(rig, "a synth, REPLACED a long name");
        rig.frames(200);
        holds(rig, "a synth on its own preset, a long name");
        CHECK(drew("delete this preset"));
        rig.key(B, true);
        rig.frames(30);
        holds(rig, "a synth's own preset being deleted");
        CHECK(drew(SynthScreen::kPresetDeletingWords) && phazed.presetDeletePart() > 0.f);
        rig.frames(50);
        rig.key(B, false);
        rig.frames(1);
        CHECK(userPresets(TrackKind::Phazed).empty() && phazed.said() == longest + " DELETED. THE TRACK KEEPS ITS SOUND");
        holds(rig, "a synth, a long name DELETED");
        session.usePresetFolder(tempFolder("textsize-presets-clean")); // (the registry is global)
    }

    // JAX-EP (jx3p-plan.md, batch 6b): every setting of every page with the cursor on it, at the bottom of what
    // it reads and, with A held, at the top (a knob: 0.0 and 10.0; a choice: its first and its last word).
    {
        for (int t = 0; t < kNumTracks; ++t)
            if (project.tracks[t].kind == TrackKind::Jx3p) project.track = t;
        CHECK(project.current().kind == TrackKind::Jx3p);
        SynthScreen& jax = rig.app.jaxep();
        const SynthSpec& spec = jx3pSpec();
        int settings = 0, knobs = 0;
        for (int page = 1; page < jax.pageCount(); ++page) {
            for (int stop = 0; stop < spec.itemCount(page); ++stop, ++settings) {
                jax.goTo(page, stop);
                const int id = jax.settingHere();
                CHECK(id >= 0 && id < jx3p::kSettings);
                const int was = project.current().jx3p.value[id];
                char reads[32];
                session.setSynthSetting(project.track, id, spec.settingMin(id));
                rig.frames(1);
                holds(rig, "JAX-EP, a setting at its least");
                spec.settingText(id, spec.settingMin(id), reads, sizeof reads);
                CHECK(drew(reads));
                session.setSynthSetting(project.track, id, spec.settingMax(id));
                rig.key(A, true);
                rig.frames(1);
                holds(rig, "JAX-EP, a setting at its most, A held");
                spec.settingText(id, spec.settingMax(id), reads, sizeof reads);
                CHECK(drew(reads));
                knobs += std::string(reads) == "10.0" ? 1 : 0;
                rig.key(A, false);
                rig.frames(1);
                session.setSynthSetting(project.track, id, was);
            }
        }
        CHECK(settings == jx3p::kSettings && knobs == 19);

        // MAIN: what the folder says stands on PRESET's top row, between the word and the count. The short
        // one follows the setting; the long one (a bank's, the plan's batch 7) does not fit at XL beside a
        // count of three figures, and the whole row is then at LARGE, as LARGE draws it.
        const Fonts& large = *screens().at[kTextLarge];
        const Fonts& xl = *screens().at[kTextXl];
        const auto place = [](const textfit::Frame& frame, const char* words, float& x, float& w) {
            for (const textfit::Text& t : frame.texts)
                if (t.words == words && t.y >= 30.f) { x = t.x; w = t.w; return true; }
            return false;
        };
        const auto rowIsClear = [&](const textfit::Frame& frame, const char* note, const char* count) {
            float px = 0.f, pw = 0.f, nx = 0.f, nw = 0.f, cx = 0.f, cw = 0.f;
            if (!place(frame, "PRESET", px, pw) || !place(frame, note, nx, nw) || !place(frame, count, cx, cw)) return false;
            return std::fabs(nx - (px + pw + SynthScreen::kNoteGap)) < 0.01f && cx - (nx + nw) >= SynthScreen::kNoteGap && std::fabs(cx + cw - 622.f) < 0.01f;
        };
        jax.goTo(0, SynthScreen::kPreset);
        rig.frames(1);
        // A bank's (batch 7: every built-in sound is in BANK A or BANK B), beside the preset's place in
        // its bank (owner, 2026-11-15: the factory's number, 1 to 16, not its place among the 32).
        holds(rig, "JAX-EP MAIN, what a bank says");
        CHECK(Jx3p::presetCount() == 32 && std::string(kJaxepBankNote) == "1983 BOOKLET, SPEEDS GUESSED" && drew(kJaxepBankNote) && drew("BANK A") && drew("String I"));
        CHECK(rowIsClear(lastLarge, kJaxepBankNote, "1 / 16") && rowIsClear(lastXl, kJaxepBankNote, "1 / 16") && !drew("1 / 32"));
        CHECK(xl.small.capHeight() > large.small.capHeight());
        CHECK(capAtXl(kJaxepBankNote) == large.small.capHeight() && capAtXl("PRESET", 30.f) == large.small.capHeight() && capAtXl("1 / 16") == large.small.capHeight());
        CHECK(asAtLarge(kJaxepBankNote) && asAtLarge("1 / 16"));
        // The short one (a built-in folder that is no bank: none today) follows the setting.
        jax.folderNoteAs(kJaxepGuessNote);
        rig.frames(1);
        holds(rig, "JAX-EP MAIN, the short note");
        CHECK(std::string(kJaxepGuessNote) == "SPEEDS GUESSED" && drew(kJaxepGuessNote) && !drew(kJaxepBankNote) && rowIsClear(lastLarge, kJaxepGuessNote, "1 / 16") && rowIsClear(lastXl, kJaxepGuessNote, "1 / 16"));
        CHECK(capAtXl(kJaxepGuessNote) == xl.small.capHeight() && capAtXl("PRESET", 30.f) == xl.small.capHeight() && capAtXl("1 / 16") == xl.small.capHeight());
        jax.folderNoteAs(nullptr);
        // Every one of the 32, by its name and its folder (B7's is the longest name).
        for (int preset = 0; preset < Jx3p::presetCount(); ++preset) {
            session.chooseSynthPreset(project.track, preset);
            rig.frames(2);
            char what[64], count[16];
            std::snprintf(what, sizeof what, "JAX-EP MAIN, %s", Jx3p::presetName(preset));
            std::snprintf(count, sizeof count, "%d / 16", preset % 16 + 1); // B7 is "7 / 16"
            holds(rig, what);
            CHECK(drew(Jx3p::presetName(preset)) && drew(preset < 16 ? "BANK A" : "BANK B") && drew(count) && drew(kJaxepBankNote));
        }
        session.chooseSynthPreset(project.track, 0);
        rig.frames(2);
        // A hundred of the player's own beside them: a bank's count is still its own sixteen, and the
        // player's own count among themselves, to three figures.
        std::vector<UserPreset> many(100);
        for (std::size_t i = 0; i < many.size(); ++i) many[i].name = "OWN " + std::to_string(i);
        setUserPresets(TrackKind::Jx3p, many);
        rig.frames(1);
        holds(rig, "JAX-EP MAIN, what a bank says, a hundred and thirty-two presets");
        CHECK(drew(kJaxepBankNote) && rowIsClear(lastLarge, kJaxepBankNote, "1 / 16") && rowIsClear(lastXl, kJaxepBankNote, "1 / 16") && !drew("1 / 132"));
        CHECK(capAtXl(kJaxepBankNote) == large.small.capHeight() && capAtXl("PRESET", 30.f) == large.small.capHeight() && capAtXl("1 / 16") == large.small.capHeight());
        CHECK(asAtLarge(kJaxepBankNote) && asAtLarge("1 / 16"));
        // ...and it would not have fitted at XL: the row's room is for the longest count there can be.
        CHECK(7.f + xl.small.width("PRESET") + xl.small.width(kJaxepBankNote) + xl.small.width(SynthScreen::kCountLongest) + 2 * SynthScreen::kNoteGap > SynthScreen::kPresetW - 10.f);
        std::printf("    JAX-EP's PRESET row at LARGE: %.1f + %.1f + %.1f, at XL %.1f + %.1f + %.1f, of %d\n", large.small.width("PRESET"), large.small.width(kJaxepBankNote), large.small.width(SynthScreen::kCountLongest), xl.small.width("PRESET"), xl.small.width(kJaxepBankNote), xl.small.width(SynthScreen::kCountLongest), SynthScreen::kPresetW - 17 - 2 * SynthScreen::kNoteGap);
        CHECK(7.f + large.small.width("PRESET") + large.small.width(kJaxepBankNote) + large.small.width(SynthScreen::kCountLongest) + 2 * SynthScreen::kNoteGap <= SynthScreen::kPresetW - 10.f);
        // One of the player's own says nothing, and its row is as any synth's.
        session.chooseSynthPreset(project.track, builtInPresets(TrackKind::Jx3p));
        rig.frames(2);
        holds(rig, "JAX-EP MAIN, one of the player's own");
        CHECK(drew("OWN 0") && drew("USER") && drew("PRESET") && !drew(kJaxepGuessNote) && !drew(kJaxepBankNote) && capAtXl("PRESET", 30.f) == xl.small.capHeight());
        CHECK(drew("1 / 100") && !drew("33 / 132")); // the first of the player's own hundred
        session.chooseSynthPreset(project.track, builtInPresets(TrackKind::Jx3p) + 99);
        rig.frames(2);
        holds(rig, "JAX-EP MAIN, the last of the player's own hundred");
        CHECK(drew("OWN 99") && drew("100 / 100") && xl.small.width("100 / 100") <= xl.small.width(SynthScreen::kCountLongest));
        session.chooseSynthPreset(project.track, 0);
        setUserPresets(TrackKind::Jx3p, {});
        rig.frames(2);
    }

    // FM-6: every setting of every page with the cursor on it, at its least and, with A held, at its most
    // (as the device reads it: COARSE and FINE with their operator, a FIXED one in Hz, an ON as a bit).
    // An operator's page has six rows: at XL its boxes are drawn at LARGE, names and values, or the value
    // stood on its bar (SynthScreen::boxFontsFor); at LARGE and under, as chosen.
    {
        for (int t = 0; t < kNumTracks; ++t)
            if (project.tracks[t].kind == TrackKind::Fm6) project.track = t;
        CHECK(project.current().kind == TrackKind::Fm6);
        SynthScreen& fm = rig.app.fm6();
        const SynthSpec& spec = fm6Spec();
        const Fonts& large = *screens().at[kTextLarge];
        const Fonts& xl = *screens().at[kTextXl];
        int settings = 0;
        char reads[32];
        for (int page = 1; page < fm.pageCount(); ++page) {
            for (int stop = 0; stop < spec.itemCount(page); ++stop, ++settings) {
                fm.goTo(page, stop);
                const int id = fm.settingHere();
                CHECK(id >= 0 && id < shownSettingCount(TrackKind::Fm6) && id != fm6::OpsOn);
                const int was = settingValue(spec, project.current().fm6.value, id);
                session.setSynthSetting(project.track, id, spec.settingMin(id));
                rig.frames(1);
                holds(rig, "FM-6, a setting at its least");
                settingReading(spec, project.current().fm6.value, id, reads, sizeof reads);
                CHECK(drew(reads));
                session.setSynthSetting(project.track, id, spec.settingMax(id));
                rig.key(A, true);
                rig.frames(1);
                holds(rig, "FM-6, a setting at its most, A held");
                settingReading(spec, project.current().fm6.value, id, reads, sizeof reads);
                CHECK(drew(reads));
                const bool six = spec.itemCount(page) > 20;
                if (std::string(reads) == "ON") reads[0] = 0; // (an operator's ON box is named ON too: its value is not told apart)
                if (reads[0] && capAtXl(reads, 50.f) != (six ? large.large.capHeight() : xl.large.capHeight()))
                    std::printf("  FM-6 page %d stop %d: \"%s\" at %.1f\n", page, stop, reads, static_cast<double>(capAtXl(reads, 50.f)));
                CHECK(!reads[0] || capAtXl(reads, 50.f) == (six ? large.large.capHeight() : xl.large.capHeight()));
                rig.key(A, false);
                rig.frames(1);
                session.setSynthSetting(project.track, id, was);
            }
        }
        CHECK(settings == fm6::kSettings + 5 && xl.large.capHeight() > large.large.capHeight());
        // The longest fixed pitch an operator reads, and the widest preset name.
        session.chooseSynthPreset(project.track, 18);
        fm.goTo(2, 3);
        session.setSynthSetting(project.track, fm6::opSetting(1, fm6::OpMode), 1);
        session.setSynthSetting(project.track, fm6::opSetting(1, fm6::OpCoarse), 3);
        session.setSynthSetting(project.track, fm6::opSetting(1, fm6::OpFine), 99);
        rig.frames(1);
        holds(rig, "FM-6, a FIXED operator at its highest");
        CHECK(drew("9772 HZ") && drew("1000 HZ"));
        fm.goTo(0, SynthScreen::kPreset);
        rig.frames(1);
        holds(rig, "FM-6 MAIN, HARPSICH 1");
        CHECK(drew("HARPSICH 1") && drew("ROM 1A") && drew("19 / 32"));
        session.chooseSynthPreset(project.track, 0);
        rig.frames(2);
    }

    // PHYSIC: every setting of every page with the cursor on it, at its least and, with A held, at its most,
    // as the device reads it, on a MALLET, a PLUCK and a NOISE, on a bar and on a string (HARDNESS reads the
    // mallet's contact or the burst's length, STRETCH a string's B). Two rows a page at most: at XL every value
    // is drawn at XL.
    {
        for (int t = 0; t < kNumTracks; ++t)
            if (project.tracks[t].kind == TrackKind::Physic) project.track = t;
        CHECK(project.current().kind == TrackKind::Physic);
        SynthScreen& ph = rig.app.physic();
        const SynthSpec& spec = physicSpec();
        const Fonts& xl = *screens().at[kTextXl];
        int settings = 0;
        char reads[32];
        for (int model : {physic::kMarimba, physic::kString}) {
            for (int strike = 0; strike < physic::kStrikes; ++strike) {
                for (int page = 1; page < ph.pageCount(); ++page) {
                    for (int stop = 0; stop < spec.itemCount(page); ++stop, ++settings) {
                        session.setSynthSetting(project.track, physic::Model, model);
                        session.setSynthSetting(project.track, physic::Strike, strike);
                        ph.goTo(page, stop);
                        const int id = ph.settingHere();
                        CHECK(id >= 0 && id < physic::kSettings);
                        const int was = project.current().physic.value[id];
                        for (int end = 0; end < 2; ++end) {
                            session.setSynthSetting(project.track, id, end ? spec.settingMax(id) : spec.settingMin(id));
                            if (end) rig.key(A, true);
                            rig.frames(1);
                            holds(rig, end ? "PHYSIC, a setting at its most, A held" : "PHYSIC, a setting at its least");
                            settingReading(spec, project.current().physic.value, id, reads, sizeof reads);
                            CHECK(drew(reads));
                            if (capAtXl(reads, 50.f) != xl.large.capHeight()) std::printf("  PHYSIC page %d stop %d: \"%s\" at %.1f\n", page, stop, reads, static_cast<double>(capAtXl(reads, 50.f)));
                            CHECK(capAtXl(reads, 50.f) == xl.large.capHeight());
                            if (end) rig.key(A, false);
                            rig.frames(1);
                        }
                        session.setSynthSetting(project.track, id, was);
                    }
                }
            }
        }
        CHECK(settings == 2 * physic::kStrikes * physic::kSettings);
        // The widest of its preset names, and its folder's.
        int widest = 0;
        for (int i = 1; i < builtInPresets(TrackKind::Physic); ++i)
            if (xl.large.width(Physic::presetName(i)) > xl.large.width(Physic::presetName(widest))) widest = i;
        session.chooseSynthPreset(project.track, widest);
        ph.goTo(0, SynthScreen::kPreset);
        rig.frames(1);
        holds(rig, "PHYSIC MAIN, its widest preset");
        CHECK(drew(Physic::presetName(widest)) && drew(Physic::presetCategory(widest)));
        session.chooseSynthPreset(project.track, 0);
        rig.frames(2);
    }

    // ACID: every setting of both pages with the cursor on it, at its least and, with A held, at its most.
    // Four rows a page at most: at XL every value is drawn at XL.
    {
        for (int t = 0; t < kNumTracks; ++t)
            if (project.tracks[t].kind == TrackKind::Acid) project.track = t;
        CHECK(project.current().kind == TrackKind::Acid);
        SynthScreen& ac = rig.app.acid();
        const SynthSpec& spec = acidSpec();
        const Fonts& xl = *screens().at[kTextXl];
        int settings = 0;
        char reads[32];
        for (int page = 1; page < ac.pageCount(); ++page)
            for (int stop = 0; stop < spec.itemCount(page); ++stop, ++settings) {
                ac.goTo(page, stop);
                const int id = ac.settingHere();
                CHECK(id >= 0 && id < acid::kSettings);
                const int was = project.current().acid.value[id];
                for (int end = 0; end < 2; ++end) {
                    session.setSynthSetting(project.track, id, end ? spec.settingMax(id) : spec.settingMin(id));
                    if (end) rig.key(A, true);
                    rig.frames(1);
                    holds(rig, end ? "ACID, a setting at its most, A held" : "ACID, a setting at its least");
                    settingReading(spec, project.current().acid.value, id, reads, sizeof reads);
                    CHECK(drew(reads));
                    if (capAtXl(reads, 50.f) != xl.large.capHeight()) std::printf("  ACID page %d stop %d: \"%s\" at %.1f\n", page, stop, reads, static_cast<double>(capAtXl(reads, 50.f)));
                    CHECK(capAtXl(reads, 50.f) == xl.large.capHeight());
                    if (end) rig.key(A, false);
                    rig.frames(1);
                }
                session.setSynthSetting(project.track, id, was);
            }
        CHECK(settings == acid::kSettings);
        ac.goTo(0, SynthScreen::kPreset);
        rig.frames(1);
        holds(rig, "ACID MAIN");
    }

    // The sampler: with nothing, with the longest name there is, and with a recording that has gone.
    for (int t = 0; t < kNumTracks; ++t)
        if (project.tracks[t].kind == TrackKind::Sampler) project.track = t;
    rig.frames(2);
    holds(rig, "the sampler, no recording");
    CHECK(drew("Hold A and push up to pick a recording, or press Y to catch the last bar played.") && drew("NONE"));
    // A name that just fits the box at LARGE is held at LARGE at XL; one letter shorter and it follows the setting.
    {
        const std::string fits = fitsLargeOnly(&Fonts::large, 624.f - 14.f, 'W');
        CHECK(fits.size() > 10 && fits.size() < 60);
        project.tracks[project.track].sample[0] = 0;
        std::snprintf(project.tracks[project.track].sample, sizeof project.tracks[project.track].sample, "%s", fits.c_str());
        rig.frames(1);
        holds(rig, "the sampler, a name that just fits at LARGE");
        CHECK(drew(fits.c_str()) && capAtXl(fits.c_str()) == 16.f);
        const std::string shorter = fits.substr(0, fits.size() - 3);
        std::snprintf(project.tracks[project.track].sample, sizeof project.tracks[project.track].sample, "%s", shorter.c_str());
        rig.frames(1);
        holds(rig, "the sampler, a shorter name");
        CHECK(drew(shorter.c_str()) && capAtXl(shorter.c_str()) == 17.f);
    }
    session.setTrackSample(project.track, longest);
    rig.frames(2);
    holds(rig, "the sampler, the longest name");
    for (int stop = 0; stop <= sampler::kSettings; ++stop) {
        rig.app.sampler().goTo(stop);
        rig.frames(1);
        holds(rig, "the sampler, each setting");
    }
    // Its own preset (gesture audit G01, G10): the name keys at their longest, saying save and then replace; what
    // is said after, a long own name on PRESET, and the bar while B deletes it, which says it cannot be undone.
    {
        SamplerScreen& screen = rig.app.sampler();
        session.usePresetFolder(tempFolder("textsize-sampler-presets"));
        const std::string longest(SamplerScreen::kPresetNameLength, 'W');
        screen.goTo(SamplerScreen::kPresetStop);
        rig.frames(1);
        rig.tap(R1);
        CHECK(screen.typing() && screen.typed().empty());
        rig.tap(DOWN, 2);
        rig.tap(RIGHT, 2);
        rig.tap(A, SamplerScreen::kPresetNameLength);
        CHECK(screen.typed() == longest);
        holds(rig, "the sampler's preset keys, a name at its longest");
        rig.tap(B);
        CHECK(!screen.typing() && screen.said() == "SAVED AS " + longest);
        holds(rig, "the sampler, SAVED AS a long name");
        rig.tap(R1);
        holds(rig, "the sampler's preset keys, B replacing");
        CHECK(drew("replace") && drew(SamplerScreen::kPresetReplacingWords) && capAtXl(SamplerScreen::kPresetReplacingWords) > 0.f);
        rig.tap(B);
        CHECK(screen.said() == "REPLACED " + longest);
        holds(rig, "the sampler, REPLACED a long name");
        rig.frames(200);
        holds(rig, "the sampler on its own preset, a long name");
        CHECK(drew("delete this preset"));
        rig.key(B, true);
        rig.frames(30);
        holds(rig, "the sampler's own preset being deleted");
        CHECK(drew(SamplerScreen::kPresetDeletingWords) && screen.presetDeletePart() > 0.f);
        rig.frames(50);
        rig.key(B, false);
        rig.frames(1);
        CHECK(session.samplerPresets().empty() && screen.said() == longest + " DELETED. THE TRACK KEEPS ITS SOUND");
        holds(rig, "the sampler, a long name DELETED");
        session.usePresetFolder(tempFolder("textsize-sampler-presets-clean"));
    }
    session.setTrackSample(project.track, "not there");
    rig.frames(2);
    holds(rig, "the sampler, its recording gone");
    CHECK(drew("NOT IN THE SAMPLES FOLDER. THIS TRACK IS SILENT UNTIL IT IS."));

    // The kit: built-in drums, a long name in a long folder, a recording that has gone; and the list to pick from.
    project.track = 0;
    session.setLaneSample(0, 2, longest);
    session.setLaneFit(0, 2, 32);
    session.setLaneSample(0, 3, "short");
    session.setLaneSample(0, 4, "not there");
    session.finishStretching();
    rig.app.kit().goTo(2, KitScreen::kSound);
    rig.frames(2);
    CHECK(holds(rig, "the kit") > 8);
    CHECK(drew("FILE NOT FOUND.") && drew("THE DRUM PLAYS.") && drew("BUILT IN") && drew("SHORT"));
    // (Since CHOKE that line is two short ones, in the narrower column, and they fit it at every size: it ran off the screen's edge before.)
    CHECK(capAtXl("FILE NOT FOUND.") == 13.f && capAtXl("THE DRUM PLAYS.") == 13.f && capAtXl("THE RECORDING") == 13.f && capAtXl("CHOKE") == 13.f);
    for (const textfit::Frame* frame : {&lastLarge, &lastXl})
        for (const textfit::Text& t : frame->texts)
            if (t.words == "FILE NOT FOUND." || t.words == "THE DRUM PLAYS.") CHECK(t.x + t.w <= KitScreen::kWaveX + KitScreen::kWaveW);
    rig.tap(Y); // nothing has played: it says so, over the heads of FIT TO, CHOKE and the recordings
    holds(rig, "the kit, nothing to catch");
    CHECK(drew("NOTHING TO CATCH: PLAY SOMETHING FIRST") && !drew("CHOKE"));
    for (const textfit::Text& t : lastLarge.texts)
        if (t.words == "NOTHING TO CATCH: PLAY SOMETHING FIRST") CHECK(t.x + t.w <= ui::kScreenW - 8);
    {
        // Batch S3: Y held, CATCH's choice said at its longest (4 bars of a track with a ten-letter name), and the hints.
        char before[sizeof project.tracks[0].name];
        std::memcpy(before, project.tracks[0].name, sizeof before);
        session.setTrackName(0, "WWWWWWWWWW");
        rig.key(SDLK_a, true); // Y held
        rig.tap(SDLK_RIGHT, 2);
        rig.tap(SDLK_DOWN);
        rig.frames(1);
        CHECK(holds(rig, "the kit, Y held: CATCH's choice at its longest") > 8);
        CHECK(drew("CATCH THE LAST 4 BARS OF 1 WWWWWWWWWW") && ui::gHintsOver == 0);
        for (const textfit::Frame* frame : {&lastLarge, &lastXl})
            for (const textfit::Text& t : frame->texts)
                if (t.words.find("CATCH THE LAST") == 0) CHECK(t.x + t.w <= ui::kScreenW - 8);
        rig.key(SDLK_a, false); // (a choice was made: nothing is caught)
        session.stepCatchFrom(-1);
        session.stepCatchBars(-1);
        session.stepCatchBars(-1);
        session.setTrackName(0, before);
        rig.frames(1);
    }
    rig.app.kit().goTo(2, KitScreen::kFit);
    rig.frames(1);
    holds(rig, "the kit, on FIT TO");
    // CHOKE: NONE, 1 and 2, with A up and held (what the bar says), every lane's at its widest.
    session.setLaneChoke(0, 0, 2);
    session.setLaneChoke(0, 1, 1);
    rig.app.kit().goTo(5, KitScreen::kChoke);
    rig.frames(200); // (what was said has gone: the heads are back)
    CHECK(holds(rig, "the kit, on CHOKE") > 8);
    CHECK(drew("CHOKE") && drew("NONE") && drew("2"));
    CHECK(ui::gHintsOver == 0);
    rig.key(SDLK_x, true); // A held
    rig.frames(1);
    holds(rig, "the kit, A held on CHOKE");
    CHECK(ui::gHintsOver == 0);
    rig.key(SDLK_x, false);
    rig.frames(1);
    // EDIT: the column, then the view on the long name in the long folder with every row at its
    // widest, A held, what is said, the bar filling, a built-in drum and a recording gone.
    {
        LaneEdit wide;
        wide.start = 99999;
        wide.end = kEditUnits;
        wide.gain = -kGainMost;
        wide.fadeIn = 1500;
        wide.fadeOut = 750;
        wide.reverse = 1;
        wide.pitch = -kPitchMost;
        wide.fine = -kFineMost;
        session.setLaneEdit(0, 2, wide);
        rig.app.kit().goTo(2, KitScreen::kEdit);
        rig.frames(2);
        holds(rig, "the kit, on EDIT");
        CHECK(ui::gHintsOver == 0);
        rig.app.kit().openEdit(kEditGain);
        rig.frames(2);
        CHECK(holds(rig, "a lane's EDIT, every row at its widest") > 8);
        CHECK(drew("FADE OUT") && drew("-24.0 DB") && drew("-50 CENTS") && drew("REVERSE"));
        CHECK(ui::gHintsOver == 0);
        rig.key(SDLK_x, true); // A held
        rig.frames(1);
        holds(rig, "a lane's EDIT, A held");
        rig.key(SDLK_x, false);
        rig.frames(1);
        rig.tap(SDLK_a); // NORMALIZED: GAIN ..., said in the heading
        holds(rig, "a lane's EDIT, NORMALIZED said");
        rig.key(SDLK_z, true); // B held: the bar fills
        rig.frames(30);
        holds(rig, "a lane's EDIT, its edits being deleted");
        rig.key(SDLK_z, false);
        rig.frames(2);
        rig.tap(SDLK_z); // back to the kit
        rig.app.kit().goTo(4, KitScreen::kEdit);
        rig.app.kit().openEdit(kEditStart);
        rig.frames(2);
        holds(rig, "a lane's EDIT, its recording gone");
        rig.tap(SDLK_z);
        rig.app.kit().goTo(7, KitScreen::kEdit);
        rig.app.kit().openEdit(kEditEnd);
        rig.frames(2);
        holds(rig, "a lane's EDIT, a built-in drum");
        rig.tap(SDLK_z);
        CHECK(!rig.app.kit().editing());
        session.setLaneEdit(0, 2, LaneEdit());
    }
    {
        // Batch S1: R1 in a lane's EDIT view saves a copy, said with its name at its longest (63 letters); A held on FIT
        // on a recording (LEFT / RIGHT the tempo from it) and the longest thing it says; the KIT stop on a folder.
        std::unique_ptr<TrackData> kept(new TrackData(project.tracks[0])); // (the kit as it was, put back after)
        session.setLaneSample(0, 6, deep);
        session.finishStretching();
        rig.app.kit().goTo(6, KitScreen::kEdit);
        rig.app.kit().openEdit(kEditGain);
        rig.frames(2);
        rig.tap(SDLK_w); // R1
        CHECK(rig.app.kit().said() == "SAVED \"X OF NAM 1\"");
        CHECK(std::remove((samples + "/" + sampleFolderOf(deep) + "/x of nam 1.wav").c_str()) == 0); // (the lists below count that folder's)
        holds(rig, "a lane's EDIT, SAVED said with the longest name");
        CHECK(ui::gHintsOver == 0);
        for (const textfit::Text& t : lastXl.texts)
            if (t.words.find("SAVED") == 0) CHECK(t.x + t.w <= ui::kScreenW - 8);
        rig.tap(SDLK_z);
        rig.app.kit().goTo(6, KitScreen::kFit);
        rig.key(SDLK_x, true); // A held
        rig.frames(1);
        holds(rig, "the kit, A held on FIT on a recording");
        CHECK(ui::gHintsOver == 0);
        rig.tap(SDLK_RIGHT); // (half a second: no tempo fits it)
        rig.key(SDLK_x, false);
        rig.frames(1);
        CHECK(rig.app.kit().said() == "NO TEMPO FITS IT IN 1, 2 OR 4 BARS");
        holds(rig, "the kit, no tempo fits the loop");
        for (const textfit::Text& t : lastXl.texts)
            if (t.words == rig.app.kit().said()) CHECK(t.x + t.w <= ui::kScreenW - 8);
        session.setLaneSample(0, 6, "");
        CHECK(session.loadFolderKit(0, sampleFolderOf(deep)));
        session.finishStretching();
        rig.app.kit().goToKit();
        rig.frames(200); // (what was said has gone)
        holds(rig, "the KIT stop, on a folder with a long name");
        CHECK(ui::gHintsOver == 0);
        rig.key(SDLK_x, true); // A held: kits, then folders
        rig.frames(1);
        holds(rig, "the KIT stop, A held");
        CHECK(ui::gHintsOver == 0);
        rig.key(SDLK_x, false);
        rig.frames(1);
        rig.tap(SDLK_DOWN);
        CHECK(session.putKit(0, *kept));
        session.finishStretching();
    }
    {
        // DRUM SYNTH: synth lanes on the kit, and a VOICE view at its widest readings (NOISE's TUNE at
        // 14.0 KHZ, its DECAY at 2.00 S; ZAP's SWEEP at +60 ST and BEND at 300 MS).
        drumsynth::Voice noise = drumsynth::usualVoice(drumsynth::kNoiseBurst), zap = drumsynth::usualVoice(drumsynth::kZap);
        noise.dial[drumsynth::kTune] = noise.dial[drumsynth::kDecay] = drumsynth::kDialMost;
        zap.dial[drumsynth::kSweep] = zap.dial[drumsynth::kOwn] = zap.dial[drumsynth::kDecay] = drumsynth::kDialMost;
        session.setLaneSynth(0, 2, noise);
        session.setLaneSynth(0, 3, zap);
        session.finishStretching();
        rig.app.kit().goTo(2, KitScreen::kSound);
        rig.frames(2);
        holds(rig, "the kit, two synth lanes");
        CHECK(ui::gHintsOver == 0);
        rig.app.kit().openVoice(drumsynth::kTune);
        rig.frames(2);
        CHECK(holds(rig, "a lane's VOICE, NOISE at its widest") > 8);
        CHECK(drew("14.0 KHZ") && drew("2.00 S") && drew("NOISE,") && drew("SYNTH"));
        CHECK(ui::gHintsOver == 0);
        rig.app.kit().goTo(3, KitScreen::kSound);
        rig.app.kit().openVoice(drumsynth::kOwn);
        rig.frames(2);
        holds(rig, "a lane's VOICE, ZAP at its widest");
        CHECK(drew("+60 ST") && drew("300 MS"));
        rig.key(SDLK_x, true); // A held
        rig.frames(1);
        holds(rig, "a lane's VOICE, A held");
        rig.key(SDLK_x, false);
        rig.frames(1);
        rig.key(SDLK_z, true); // B held: the bar fills
        rig.frames(30);
        holds(rig, "a lane's VOICE, its knobs being put back");
        rig.key(SDLK_z, false);
        rig.frames(2);
        rig.tap(SDLK_z); // back to the kit
        CHECK(!rig.app.kit().voicing());
        session.setLaneSample(0, 2, "");
        session.setLaneSample(0, 3, "");
    }
    rig.app.kit().goTo(2, KitScreen::kSound);
    // The list, in columns: in the long folder, on the long name; then every place it can be, and
    // the hints bar holding all it says at each.
    const SDL_Keycode UP = SDLK_UP;
    const auto listHolds = [&](const char* what, const char* words) {
        holds(rig, what);
        if (ui::gHintsOver != 0.f) std::printf("    %s: the hints are %.0f pixels over\n", what, static_cast<double>(ui::gHintsOver));
        if (!drew(words)) std::printf("    %s: \"%s\" is not drawn\n", what, words);
        CHECK(ui::gHintsOver == 0.f && drew(words));
    };
    session.setLaneSample(0, 2, deep);
    session.finishStretching();
    rig.tap(A);
    CHECK(rig.app.kit().picker().depth() == 2 && rig.app.kit().picker().cursor() == 0);
    listHolds("the kit's list, two folders down", "ANOTHER JUST AS LONG AS IT");
    CHECK(drew("carry") && drew("delete it") && drew("keep it")); // the bar, on a recording: HOLD R1 carries, HOLD B deletes
    CHECK(drew("1 OF 1") && drew("LANE 3: PICK ITS SOUND") && drew("0.50 S") && drew("X OF NAMES"));
    rig.key(B, true);
    rig.frames(10);
    listHolds("the kit's list, B held", "DELETING: CANNOT BE UNDONE");
    CHECK(asAtLarge("DELETING: CANNOT BE UNDONE")); // (too long for its row at XL: held at LARGE, where it fits)
    rig.key(B, false);
    rig.tap(A);
    session.setLaneSample(0, 7, deep); // on another lane too: the list says so
    listHolds("the kit's list, a recording on in two places", "ALSO IN THIS SONG:");
    CHECK(drew("1 PLACE")); // (its own line: on one it ran past the panel's edge)
    session.setLaneSample(0, 6, deep);
    listHolds("the kit's list, a recording on in three places", "2 PLACES");
    session.setLaneSample(0, 6, "");
    session.setLaneSample(0, 7, "");
    rig.tap(LEFT); // on the folder it is in, itself in a folder: what it holds beside it
    CHECK(rig.app.kit().picker().depth() == 1 && rig.app.kit().picker().count() == 3); // (the 66-letter one on the card is too long to be a name: not listed)
    listHolds("the kit's list, on a folder in a folder", "X OF NAMES");
    CHECK(!drew("carry") && !drew("delete it") && drew("open") && drew("page")); // a folder with something in it: neither
    rig.key(B, true); // B held on it: no bar, and after a moment the top line says why
    rig.frames(30);
    listHolds("the kit's list, B held on a folder that is not empty", "NOT DELETED: IT IS NOT EMPTY");
    rig.key(B, false);
    CHECK(rig.app.kit().picker().isOpen());
    rig.frames(SamplePicker::kSaidMs / 16 + 2);
    rig.tap(DOWN); // on an empty one
    listHolds("the kit's list, on an empty folder", "NOTHING IN IT");
    rig.tap(RIGHT); // in it
    CHECK(rig.app.kit().picker().depth() == 2 && rig.app.kit().picker().count() == 0);
    listHolds("the kit's list, in an empty folder", "NOTHING IN IT");
    rig.tap(LEFT);
    rig.tap(LEFT); // SAMPLES
    CHECK(rig.app.kit().picker().depth() == 0);
    listHolds("the kit's list, SAMPLES", "UNSORTED");
    rig.tap(UP);
    listHolds("the kit's list, UNSORTED", "SHORT");
    rig.tap(UP);
    listHolds("the kit's list, the first row", "THE BUILT-IN DRUM");
    // More rows than show: the list goes along with the cursor (a page at a time with R1, the ends stop).
    rig.tap(DOWN, 4);
    rig.tap(RIGHT);
    CHECK(rig.app.kit().picker().folderName() == "mm" && rig.app.kit().picker().count() == 15);
    listHolds("the kit's list, a long folder's top", "M 01");
    CHECK(drew("M 11") && !drew("M 12") && drew("1 OF 15"));
    rig.tap(R1);
    listHolds("the kit's list, a page down", "M 11");
    CHECK(drew("M 14") && !drew("M 04") && drew("M 05") && drew("+ NEW FOLDER") && drew("11 OF 15")); // (the last eleven: the cursor is not kept in the middle past the end)
    rig.tap(R1);
    listHolds("the kit's list, its end", "+ NEW FOLDER");
    CHECK(drew("15 OF 15") && drew("M 14") && drew("M 05") && !drew("M 04"));
    // + NEW FOLDER: its name typed, at its longest, and the folder made.
    rig.tap(A);
    CHECK(rig.app.kit().picker().typing());
    listHolds("the kit's list, a new folder's name, nothing typed", "NEW FOLDER IN MM: ITS NAME");
    rig.tap(DOWN, 2);
    rig.tap(RIGHT, 2);
    rig.tap(A, 14);
    listHolds("the kit's list, a new folder's name at its longest", "WWWWWWWWWWWW");
    CHECK(drew("12 OF 12") && drew("SPACE"));
    rig.tap(B);
    CHECK(!rig.app.kit().picker().typing());
    listHolds("the kit's list, a folder made", "MADE WWWWWWWWWWWW");
    // Carrying: the top line says what, and where it would go; then what came of it.
    const SDL_Keycode R1 = SDLK_w, L1 = SDLK_q;
    rig.tap(L1, 3);
    rig.tap(R1, 3);
    rig.tap(UP);
    rig.key(R1, true);
    rig.frames(25);
    CHECK(rig.app.kit().picker().carried() == "mm/m 14");
    listHolds("the kit's list, carrying", "CARRYING TO MM: M 14");
    rig.tap(LEFT);
    rig.tap(UP, 2);
    listHolds("the kit's list, carrying to a long folder", "CARRYING TO A FOLDER WITH A LONG NAME: M 14");
    rig.key(R1, false);
    listHolds("the kit's list, moved", "MOVED TO A FOLDER WITH A LONG NAME");
    rig.tap(DOWN);
    rig.tap(RIGHT);
    rig.tap(L1);
    rig.tap(DOWN);
    CHECK(rig.app.kit().picker().under() && rig.app.kit().picker().under()->name == "ab/a dozen chars");
    rig.key(R1, true);
    rig.tap(LEFT);
    rig.tap(UP);
    rig.tap(RIGHT);
    rig.tap(L1, 3);
    CHECK(rig.app.kit().picker().carryTo() == "a folder with a long name/another just as long as it");
    listHolds("the kit's list, carrying into a folder in a folder", "CARRYING TO ANOTHER JUST AS LONG AS IT: A DOZEN CHARS");
    rig.key(R1, false);
    listHolds("the kit's list, not moved", "NOT MOVED: ITS NAME WOULD BE TOO LONG THERE");
    // B held on an empty folder: its bar. Let go: B's tap, and the lane is as it was.
    rig.tap(LEFT);
    rig.tap(DOWN, 2);
    rig.tap(RIGHT);
    rig.tap(L1, 3);
    CHECK(rig.app.kit().picker().under() && rig.app.kit().picker().under()->name == "mm/wwwwwwwwwwww");
    listHolds("the kit's list, on an empty folder that can be deleted", "WWWWWWWWWWWW");
    CHECK(drew("delete it") && !drew("carry"));
    rig.key(B, true);
    rig.frames(10);
    CHECK(rig.app.kit().picker().deletePart() > 0.f);
    listHolds("the kit's list, B held on an empty folder", "DELETING: CANNOT BE UNDONE");
    rig.key(B, false);
    rig.frames(2);
    CHECK(!rig.app.kit().picker().isOpen());
    session.setLaneSample(0, 2, longest);
    session.setLaneSample(0, 3, "ab/cd/ef");
    session.finishStretching();
    rig.frames(2);
    holds(rig, "the kit, a recording two folders down");
    CHECK(drew("EF") && drew("CD") && !drew("AB/CD")); // its lane says the folder it is in: of two, the inner one
    session.setLaneSample(0, 3, "short");
    session.finishStretching();

    // What comes up over any screen: the volume, the help card (with a MENU button and without), ALL SOUND STOPPED.
    session.view = View::Song;
    rig.app.changeVolume(-0.1f);
    rig.frames(1);
    holds(rig, "the volume, just changed");
    rig.frames(200);
    rig.key(MENU, true);
    rig.frames(2);
    holds(rig, "the help card", 0, true);
    CHECK(drew("UNDO the last change (20 deep)")); // (it was "...to the patterns": undo covers the sounds and effects too, gesture audit G10)
    rig.key(MENU, false);
    rig.frames(2);
    for (SDL_Keycode shoulder : {L1, R1, L2, R2}) rig.key(shoulder, true);
    rig.frames(2);
    holds(rig, "ALL SOUND STOPPED, held");
    for (SDL_Keycode shoulder : {L1, R1, L2, R2}) rig.key(shoulder, false);
    rig.frames(2);
    holds(rig, "ALL SOUND STOPPED");
    rig.tap(A);

    // SET: typing a name at its longest, the button test with buttons down, a bounce under way and done.
    session.view = View::Settings;
    SettingsScreen& setup = rig.app.settings();
    setup.goTo(SettingsScreen::kTrackName);
    rig.tap(A);
    CHECK(setup.typing());
    holds(rig, "typing a name, as it was");
    rig.tap(Y, static_cast<int>(setup.typed().size())); // nothing typed: the words say what an empty name means
    CHECK(setup.typing() && setup.typed().empty());
    holds(rig, "typing a name, nothing typed");
    for (int i = 0; i < 22; ++i) rig.tap(RIGHT); // (W is the widest letter: row 3, column 3)
    rig.tap(DOWN, 2);
    rig.tap(RIGHT, 2);
    for (int i = 0; i < 10; ++i) rig.tap(A);
    CHECK(setup.typed().size() == 10);
    holds(rig, "typing a name, ten letters");
    rig.tap(B);
    CHECK(!setup.typing());
    setup.goTo(SettingsScreen::kTest);
    rig.tap(A);
    CHECK(setup.testing());
    rig.key(SELECT, true);
    rig.key(SDLK_l, true);
    rig.frames(2);
    holds(rig, "the button test");
    rig.key(SELECT, false);
    rig.key(SDLK_l, false);
    rig.frames(300); // it closes by itself
    CHECK(!setup.testing());
    setup.goTo(SettingsScreen::kBounce);
    rig.tap(SDLK_LEFT); // round to the last: TRACK TO SAMPLES (batch S3)
    rig.frames(1);
    holds(rig, "BOUNCE: TRACK TO SAMPLES");
    CHECK(drew("TRACK TO SAMPLES"));
    rig.tap(SDLK_RIGHT);
    session.pretendBounce(0.37f, -1.f);
    rig.frames(1);
    holds(rig, "a bounce under way");
    session.pretendBounce(1.f, 123.4f);
    rig.frames(1);
    holds(rig, "a bounce in its tail");
    CHECK(drew("stop it"));
    rig.key(B, true); // B held on it: its bar fills, and the row says nothing is kept
    rig.frames(20);
    CHECK(setup.stopPart() > 0.f);
    holds(rig, "a bounce being stopped");
    rig.frames(50); // ...held its second: told to stop, not waited for. Until it has (a pretended one never does),
    CHECK(rig.session().bounceStopping() && setup.stopPart() == 0.f); // the row still says so, and B's hint is gone
    holds(rig, "a bounce stopping");
    CHECK(drew(SettingsScreen::kStoppingWords) && !drew("stop it") && !drew("stopping"));
    rig.key(B, false);
    rig.frames(1);
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
}

// The controls setup (shown before anything else on a new handheld, and again from the SET page)
// is always at LARGE: the same pixels whatever is chosen.
void testTheControlsSetup() {
    Screens& s = screens();
    if (!s.canvas.ok) return;
    ControlLearner learner;
    learner.update(1000);
    std::vector<Uint32> seen[kTextSizes];
    for (int size = 0; size < kTextSizes; ++size) {
        s.canvas.clear();
        learner.draw(s.canvas.r, *s.at[size]);
        SDL_RenderPresent(s.canvas.r);
        seen[size] = s.canvas.all();
    }
    s.canvas.clear();
    learner.draw(s.canvas.r, *s.canvas.fonts);
    SDL_RenderPresent(s.canvas.r);
    const std::vector<Uint32> alone = s.canvas.all();
    int lit = 0;
    for (Uint32 pixel : alone) lit += pixel != Canvas::colour(ui::kBg) ? 1 : 0;
    CHECK(lit > 2000); // (something was drawn)
    for (int size = 0; size < kTextSizes; ++size) CHECK(seen[size] == alone);
    // Every step's words fit its box (the screen less 40 a side, at LARGE), L3's and R3's the longest;
    // the lines above and below fit the screen. Each step answered by a key of its own (the stick's by an axis).
    const Fonts& large = s.canvas.fonts->fixed();
    const float box = ui::kScreenW - 80.f - 16.f, line = ui::kScreenW - 80.f;
    ControlLearner walk;
    walk.update(1000);
    walk.update(2000);
    for (int step = 0; !walk.done() && step < 40; ++step) {
        const float w = large.large.width(walk.asking());
        if (w > box) std::printf("    \"%s\" is %.0f wide: the box has %.0f\n", walk.asking(), static_cast<double>(w), static_cast<double>(box));
        CHECK(w <= box);
        SDL_Event e = {};
        if (std::string(walk.asking()).find("THE STICK,") == 0) {
            e.type = SDL_JOYAXISMOTION;
            e.jaxis.axis = static_cast<Uint8>(std::string(walk.asking()).find("RIGHT") != std::string::npos ? 0 : 1);
            e.jaxis.value = 30000;
            walk.handle(e);
            e.jaxis.value = 0;
            walk.handle(e);
        } else {
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = SDLK_a + step;
            walk.handle(e);
            e.type = SDL_KEYUP;
            walk.handle(e);
        }
    }
    CHECK(walk.done());
    for (const char* words : {"One button is new since this handheld was set up. Once only.", "Once only. Apotheca learns what each of this handheld's buttons sends.",
                              "To do this again later: the SET tab, CONTROLS row, hold A for a second.",
                              "If this handheld has none, or nothing happens: wait 8 seconds and it is left out."})
        CHECK(large.small.width(words) <= line);
}

// The notices that only an update over WiFi brings up are not drawn here: their lines are measured.
// The notice is 470 wide and its words start 18 in: each line ends inside it, with room to spare, at every size.
void testTheUpdateNotice() {
    Screens& s = screens();
    if (!s.canvas.ok) return;
    const float room = 470.f - 18.f - 14.f;
    struct Line { Font Fonts::*font; const char* words; };
    const Line lines[] = {
        {&Fonts::large, "UPDATE ACCEPTED"}, {&Fonts::medium, "It will be used the next time Apotheca starts."},
        {&Fonts::small, "The one running now is kept. If the new one does not"}, {&Fonts::small, "start, Apotheca goes back to this one by itself."},
        {&Fonts::large, "COULD NOT KEEP THE UPDATE"}, {&Fonts::medium, "Nothing was changed. Is the card full?"},
        {&Fonts::large, "AN UPDATE HAS ARRIVED"}, {&Fonts::medium, "A new Apotheca was sent over WiFi (39.9 MB)."},
        {&Fonts::small, "Only accept it if you sent it yourself."}, {&Fonts::small, App::kUpdateWords}, {&Fonts::small, App::kUpdateDeletingWords},
    };
    for (const Line& line : lines)
        for (int size = 0; size < kTextSizes; ++size) {
            const float w = (s.at[size].get()->*line.font).width(line.words);
            if (w > room) std::printf("    \"%s\" is %.0f wide at %s: the notice has %.0f\n", line.words, static_cast<double>(w), kTextSizeNames[size], static_cast<double>(room));
            CHECK(w <= room);
        }
    // Its lines are 16 apart where two follow each other: the tallest small capitals are 13.
    CHECK(s.at[kTextXl]->small.capHeight() + 2.f <= 16.f && s.at[kTextXl]->medium.capHeight() + 16.f + 4.f <= 46.f);
}

// ---- the FX screen: every line, every setting of every effect -----------------------------------
void testTheFxScreen() {
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    FxScreen& fx = rig.app.fx();
    session.view = View::Effects;
    // The longest a track's name can be, and the last track's number.
    for (int t = 1; t < kNumTracks; ++t) session.addTrack(TrackKind::Drums);
    project.track = kNumTracks - 1;
    session.setTrackName(project.track, "WWWWWWWWWW");
    session.setSend(project.track, 0, 0.35f);
    session.setSend(project.track, 1, 1.f);
    int states = 0;
    char what[96];
    // A track's name of every length: beside it THIS TRACK, where there is room for both (asked at LARGE).
    int besides = 0;
    for (int letters = 0; letters <= 10; ++letters) {
        session.setTrackName(project.track, std::string(static_cast<std::size_t>(letters), 'W'));
        rig.frames(1);
        std::snprintf(what, sizeof what, "FX, a track's name of %d letters", letters);
        holds(rig, what);
        besides += drew("THIS TRACK") ? 1 : 0;
    }
    CHECK(besides >= 3 && besides <= 9); // (some with it and some without: the test reaches both)
    {
        // A name there is room for beside THIS TRACK at LARGE and not at XL: both are held at LARGE there.
        Screens& s = screens();
        const float room = FxScreen::kListW - 2.f - 10.f;
        std::string found;
        for (int letters = 1; letters <= 10 && found.empty(); ++letters)
            for (int mix = 0; mix < (1 << letters) && found.empty(); ++mix) {
                std::string own;
                for (int i = 0; i < letters; ++i) own += (mix >> i) & 1 ? 'W' : 'I';
                const std::string head = std::to_string(project.track + 1) + " " + own;
                if (s.at[kTextLarge]->small.width(head.c_str()) + s.at[kTextLarge]->small.width("THIS TRACK") <= room &&
                    s.at[kTextXl]->small.width(head.c_str()) + s.at[kTextXl]->small.width("THIS TRACK") > room)
                    found = own;
            }
        CHECK(!found.empty());
        session.setTrackName(project.track, found);
        rig.frames(1);
        holds(rig, "FX, a name that just fits beside THIS TRACK at LARGE");
        CHECK(drew("THIS TRACK") && capAtXl("THIS TRACK") == 12.f && capAtXl("BUSES") == 13.f);
    }
    session.setTrackName(project.track, "WWWWWWWWWW");
    // B held on an effect's line: the bar fills, and the heading says it cannot be undone.
    for (int row : {0, FxScreen::kBusRow, FxScreen::kMasterRow}) {
        session.setEffect(row == 0 ? project.track : row == FxScreen::kBusRow ? kEchoBus : kMasterTrack, 0, row == FxScreen::kBusRow ? EffectKind::Tape : EffectKind::Saturate);
        fx.goTo(row, 0);
        rig.key(B, true);
        rig.frames(30);
        std::snprintf(what, sizeof what, "FX, B held on line %d", row + 1);
        holds(rig, what);
        CHECK(drew(FxScreen::kDeletingWords) && fx.deletePart() > 0.f);
        rig.key(B, false);
        rig.frames(1);
    }
    // Every line of the list, on the line and on each of its settings, with A held and not.
    for (int row = 0; row < FxScreen::kRows; ++row)
        for (int column = 0; column <= 4; ++column)
            for (int held = 0; held < 2; ++held) {
                fx.goTo(row, column);
                rig.frames(1);
                if (held) rig.key(A, true), rig.frames(2);
                std::snprintf(what, sizeof what, "FX, line %d, column %d%s", row + 1, column, held ? ", A held" : "");
                holds(rig, what);
                ++states;
                if (held) rig.key(A, false), rig.frames(2);
            }
    // Hold A on the SENDS line: the D-pad is dead there, and the bar says where the sends turn (audit #13).
    fx.goTo(FxScreen::kSendsRow, 0);
    rig.key(A, true);
    rig.frames(1);
    holds(rig, "FX, SENDS line, A held");
    CHECK(drew("then RIGHT")); // the key's chip is drawn with it, a word at a time
    rig.key(A, false);
    rig.frames(1);
    // Every effect in a slot of the track and (a delay or a reverb) on its bus: each setting at
    // nine places along its range, the cursor on it. The list of effects open on each.
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
        // In its line of the list a family with a type ends clear of the mark at every size, 7 after its family:
        // the family drawn small, or the two held at LARGE (REVERB SPRING).
        // (A pair too long even so, DRIVE SATURATE, is its type alone, in the family's lettering.) A line with no type
        // shown (FILTER, CRUSH, DRIVE's DRIVE) is its family alone, as chosen at every size.
        for (int size = 0; size < kTextSizes && !effectTypeShown(kind)[0]; ++size) {
            const Fonts& chosen = *screens().at[size];
            float gap = 0.f;
            bool familySmall = true, typeAlone = true;
            CHECK(&FxScreen::lineFonts(chosen, kind, &gap, &familySmall, &typeAlone) == &chosen && !familySmall && !typeAlone);
        }
        for (int size = 0; size < kTextSizes && effectTypeShown(kind)[0]; ++size) {
            const Fonts& chosen = *screens().at[size];
            float gap = 0.f;
            bool familySmall = false, typeAlone = false;
            const Fonts& set = FxScreen::lineFonts(chosen, kind, &gap, &familySmall, &typeAlone);
            const float ends = typeAlone ? FxScreen::kNameX + set.medium.width(effectTypeShown(kind))
                                         : FxScreen::kNameX + (familySmall ? set.small : set.medium).width(effectFamily(kind)) + gap + set.small.width(effectTypeShown(kind));
            CHECK(gap == 7.f && ends <= FxScreen::nameEnd(kind) - (familySmall ? FxScreen::kNameClear : 0) + 0.01f);
            CHECK(familySmall == (kind == EffectKind::Spring || kind == EffectKind::Saturate || kind == EffectKind::Limiter || kind == EffectKind::Reverse || kind == EffectKind::Stutter || kind == EffectKind::Bloom || kind == EffectKind::Shimmer)); // small only where it must be at LARGE, and then at every size (a bigger setting never draws it smaller)
            CHECK(typeAlone == (kind == EffectKind::Saturate || kind == EffectKind::Shimmer));
            if (kind == EffectKind::Spring) CHECK((&set == &chosen) == (size <= kTextLarge)); // REVERB SPRING: as chosen to LARGE, held at LARGE at XL
            CHECK(&set == &chosen || &set == &chosen.fixed());
        }
        for (int onBus = 0; onBus < (effectBus(kind) >= 0 ? 2 : 1); ++onBus) {
            const int track = onBus ? busTrack(effectBus(kind)) : project.track, row = onBus ? FxScreen::kBusRow + effectBus(kind) : 1;
            session.setEffect(track, onBus ? 0 : 1, kind);
            CHECK(session.effectData(track, onBus ? 0 : 1).kind == k);
            fx.goTo(row, 0);
            rig.key(A, true);
            rig.frames(2);
            std::snprintf(what, sizeof what, "FX, the list of effects on %s%s", info.name, onBus ? " (bus)" : "");
            holds(rig, what);
            rig.key(A, false);
            rig.frames(2);
            for (int p = 0; p < info.params; ++p) {
                fx.goTo(row, p + 1);
                for (int i = 0; i <= 8; ++i) {
                    session.setEffectParam(track, onBus ? 0 : 1, p, static_cast<float>(i) / 8.f);
                    rig.frames(1);
                    std::snprintf(what, sizeof what, "FX, %s%s, %s at %d/8", info.name, onBus ? " (bus)" : "", info.paramNames[p], i);
                    holds(rig, what);
                    ++states;
                }
            }
            // On SPACE's line of the list (the only REVERB under the buses' heading): a type that fits at XL is
            // drawn at XL; REVERB SPRING and REVERB PLATE do not, and the family and the type are both held at LARGE
            // (SPRING's family in its smaller lettering, PLATE's as it always is). REVERB BLOOM fits at XL with its family
            // small; SHIMMER is drawn alone (as DRIVE SATURATE is).
            if (onBus && effectBus(kind) == 1) {
                const float below = static_cast<float>(FxScreen::lineY(FxScreen::kBusRow + 1)) - 2.f;
                float gap = 0.f;
                bool familySmall = false, alone = false;
                const bool held = &FxScreen::lineFonts(*screens().at[kTextXl], kind, &gap, &familySmall, &alone) != screens().at[kTextXl].get();
                CHECK(held == (kind == EffectKind::Spring || kind == EffectKind::Plate) && familySmall == (kind == EffectKind::Spring || kind == EffectKind::Bloom || kind == EffectKind::Shimmer));
                CHECK(alone == (kind == EffectKind::Shimmer)); // REVERB SHIMMER is too long even both small: SHIMMER alone, in the family's lettering
                const Fonts& at = *screens().at[held ? kTextLarge : kTextXl];
                CHECK(drew(effectTypeName(kind)) && capAtXl(effectTypeName(kind), below) == (alone ? at.medium : at.small).capHeight());
                if (!alone) CHECK(capAtXl("REVERB", below) == (familySmall ? at.small : at.medium).capHeight()); // (REVERB in SPRING's and BLOOM's smaller lettering)
                if (held) CHECK(asAtLarge(effectTypeName(kind), below) && asAtLarge("REVERB", below));
            }
            // A setting that moves with the pattern says so under its row.
            if (!onBus && info.params > 0) {
                for (int step = 0; step < kSteps; ++step) session.setAutomation(project.track, 0, autoLaneOfEffect(1, 0), step, 0.5f);
                fx.goTo(row, 1);
                rig.frames(1);
                std::snprintf(what, sizeof what, "FX, %s, a setting that moves with the pattern", info.name);
                holds(rig, what);
                for (int step = 0; step < kSteps; ++step) session.setAutomation(project.track, 0, autoLaneOfEffect(1, 0), step, -1.f);
            }
        }
    }
    CHECK(states > 300);
}
} // namespace

// THE LIST OF EFFECTS names each family's types on its line, and the heading's TYPE row names them too. Both must hold
// the fifteen types the owner has planned (2026-11-13, docs/plans/effects-plan.md "Seven families") as well as today's,
// at every size: a family's line in one row, as chosen or held at LARGE (MOD's five at XL); and names longer still
// (made up: every one as long as today's longest type word, SATURATE) in two rows at LARGE, inside the line. The TYPE
// row shows as many as fit about the one it is, "..." for the rest. (Asked of the functions the screen draws with;
// the six themes change colours only, and every still of the list is drawn in all six by themeviews.)
void testTheListOfEffectsHoldsTheLaterTypes() {
    Screens& s = screens();
    const std::vector<std::vector<std::string>> later = {
        {"filter", "comb", "vowel", "eq", "band", "wah"},
        {"drive", "saturate", "fold", "fuzz"},
        {"crush", "ring", "wear"},
        {"chorus", "flanger", "ensemble", "phaser", "tremolo", "pitch"},
        {"clean", "tape", "ping", "dub", "reverse", "stutter"},
        {"room", "hall", "spring", "plate", "bloom", "shimmer"},
        {"comp", "duck", "limiter", "snap"},
    };
    // Today's types are the first of each family's planned ones, in their order.
    CHECK(familyListSize() == 1 + static_cast<int>(later.size()));
    for (int f = 0; f < static_cast<int>(later.size()); ++f) {
        const std::vector<std::string> today = FxScreen::typeWords(familyAt(f + 1));
        CHECK(today.size() <= later[f].size());
        for (std::size_t t = 0; t < today.size(); ++t) CHECK(today[t] == later[f][t]);
    }
    std::size_t longest = 0;
    for (int k = 1; k < kEffectKinds; ++k) longest = std::max(longest, std::strlen(effectTypeName(static_cast<EffectKind>(k))));
    CHECK(longest == 8); // SATURATE
    const float room = static_cast<float>(FxScreen::kPickTypesRoom);
    int held = 0;
    for (int size = 0; size < kTextSizes; ++size) {
        const Fonts& chosen = *s.at[size];
        for (const std::vector<std::string>& names : later) {
            // The planned names: one row where LARGE has them in one (at the size chosen if they fit it, else at LARGE);
            // MOD's six since PITCH are two rows at LARGE, so two at every size (2026-11-24: the same words).
            const FxScreen::PickRows rows = FxScreen::pickRows(chosen, names);
            const int n = static_cast<int>(names.size());
            const bool oneAtLarge = FxScreen::typesWidth(chosen.fixed().small, names, 0, n) <= room;
            CHECK(rows.rows == (oneAtLarge ? 1 : 2));
            if (oneAtLarge) {
                CHECK(rows.split == n && FxScreen::typesWidth(rows.set->small, names, 0, n) <= room);
                const bool fitsChosen = FxScreen::typesWidth(chosen.small, names, 0, n) <= room;
                CHECK(rows.set == (fitsChosen ? &chosen : &chosen.fixed()));
                if (size <= kTextLarge) CHECK(rows.set == &chosen); // held only at XL
            } else {
                CHECK(FxScreen::typesWidth(rows.set->small, names, 0, rows.split) <= room && FxScreen::typesWidth(rows.set->small, names, rows.split, n) <= room);
            }
            held += rows.set != &chosen ? 1 : 0;
            // Made up: as many, each as long as SATURATE. Two rows at LARGE, each in the room, both inside the line.
            const std::vector<std::string> made(names.size(), "saturat" + std::string(1, 'e'));
            const FxScreen::PickRows two = FxScreen::pickRows(chosen, made);
            const int count = static_cast<int>(made.size());
            if (FxScreen::typesWidth(chosen.fixed().small, made, 0, count) > room) { // (LARGE decides: two rows at every size)
                CHECK(two.rows == 2 && two.split > 0 && two.split < count);
                CHECK(FxScreen::typesWidth(two.set->small, made, 0, two.split) <= room && FxScreen::typesWidth(two.set->small, made, two.split, count) <= room);
                const float rowsH = 2.f * two.set->small.capHeight() + FxScreen::kPickRowGap, top = (FxScreen::kPickLineH - rowsH) / 2.f - 1.f;
                CHECK(top >= 3.f && top + rowsH <= FxScreen::kPickLineH - 3.f); // clear of the cursor's outline, capitals and all
            } else {
                CHECK(two.rows == 1);
            }
            // The heading's TYPE row, in capitals, on each type in turn: that one in view, the rest that are in view about it,
            // all in the room the row has, and never fewer than three. Which are in view is LARGE's at every size (the same
            // words: a smaller size showing more would be other words), as many as fit at LARGE; drawn at the size chosen
            // where they fit its room, else at LARGE (only at XL).
            std::vector<std::string> upper;
            for (std::string name : names) {
                for (char& c : name) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
                upper.push_back(name);
            }
            const Fonts& large = chosen.fixed();
            const float largeRoom = FxScreen::typeRowRoom(large, count);
            for (int at = 0; at < count; ++at) {
                int from = -1, to = -1, lfrom = -1, lto = -1;
                const Fonts& f = FxScreen::typeRowView(chosen, upper, at, &from, &to);
                FxScreen::typesInView(large.small, upper, at, largeRoom, &lfrom, &lto);
                CHECK(from == lfrom && to == lto && (&f == &chosen || &f == &large));
                if (size <= kTextLarge) CHECK(&f == &chosen);
                // (Never fewer than three; two where both ends are cut: MOD's six since PITCH, on ENSEMBLE, "... ENSEMBLE PHASER ...".)
                CHECK(from >= 0 && from <= at && at < to && to <= count && to - from >= std::min(from > 0 && to < count ? 2 : 3, count));
                CHECK(FxScreen::typeRowWidth(f.small, upper, from, to) <= FxScreen::typeRowRoom(f, count));
                // Not one more would fit at LARGE, at either end (as many as fit there).
                const float w = FxScreen::typeRowWidth(large.small, upper, from, to);
                if (to < count) {
                    const float more = large.small.width(upper[to].c_str()) + FxScreen::kTypeGapX - (to + 1 == count ? large.small.width(FxScreen::kTypeMore) + FxScreen::kTypeGapX : 0.f);
                    CHECK(w + more > largeRoom);
                }
            }
        }
    }
    CHECK(held <= 7); // held only at XL (none since MOD went to two rows with PITCH, each row in XL's own lettering)
    std::printf("    the planned types: %d families held at LARGE at XL\n", held);
}

// ...and AS DRAWN, with names no effect has yet: what the two functions above decide is what the screen draws
// (a review's mutants broke the drawing of two rows, the "...", and the key's room, and every test still passed).
// The pixels are read off the canvas, at every size: the ink of a line of the list of effects in one band of rows
// or two, each ending where its words end (no comma after the last of a row), all of it in the line and clear of
// the cursor's outline; the TYPE row's "..." where the rest are left out, and nothing in the key's room unless the
// key is drawn.
void testTheListOfEffectsAsDrawn() {
    Screens& s = screens();
    Canvas& canvas = s.canvas;
    const Uint32 ground = Canvas::colour(ui::kBg);
    const auto inkIn = [&](int x0, int y0, int x1, int y1) { // pixels not the ground in [x0, x1) x [y0, y1)
        int n = 0;
        for (int y = std::max(0, y0); y < std::min(ui::kScreenH, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(ui::kScreenW, x1); ++x) n += canvas.at(x, y) != ground ? 1 : 0;
        return n;
    };
    const auto upper = [](std::vector<std::string> names) {
        for (std::string& name : names)
            for (char& c : name) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        return names;
    };
    const std::vector<std::vector<std::string>> tried = {
        {"clean", "tape", "ping", "dub", "reverse", "stutter"},
        {"room", "hall", "spring", "plate", "bloom", "shimmer"},
        {"chorus", "flanger", "ensemble", "phaser", "tremolo"},
        std::vector<std::string>(6, "saturate"), // made up: six of today's longest
        std::vector<std::string>(4, "saturate"),
        {"low, band, high, DJ and every other word"}, // a family's words, made up longer: never split
    };
    const int lineTop = FxScreen::pickLineY(3), right = FxScreen::kPickTypesX + FxScreen::kPickTypesRoom;
    int twoRows = 0, heldOneRow = 0, cut = 0, cutLeft = 0;
    for (int size = 0; size < kTextSizes; ++size) {
        const Fonts& chosen = *s.at[size];
        for (const std::vector<std::string>& names : tried) {
            // THE LIST OF EFFECTS: one band of ink a row, a row's ink ending where its words do.
            const FxScreen::PickRows rows = FxScreen::pickRows(chosen, names);
            const int count = static_cast<int>(names.size());
            canvas.clear();
            FxScreen::drawPickTypes(canvas.r, chosen, names, count - 1, lineTop);
            CHECK(inkIn(0, 0, ui::kScreenW, lineTop + 3) == 0 && inkIn(0, lineTop + FxScreen::kPickLineH - 3, ui::kScreenW, ui::kScreenH) == 0); // in the line, clear of the outline
            CHECK(inkIn(0, 0, FxScreen::kPickTypesX - 1, ui::kScreenH) == 0 && inkIn(right + 1, 0, ui::kScreenW, ui::kScreenH) == 0);
            std::vector<std::pair<int, int>> bands; // rows of ink, top and foot
            for (int y = lineTop; y < lineTop + FxScreen::kPickLineH; ++y) {
                if (inkIn(FxScreen::kPickTypesX - 1, y, right + 1, y + 1) == 0) continue;
                if (bands.empty() || bands.back().second < y - 1) bands.push_back({y, y});
                else bands.back().second = y;
            }
            CHECK(static_cast<int>(bands.size()) == rows.rows);
            const Font& font = rows.set->small;
            for (int b = 0; b < static_cast<int>(bands.size()) && b < 2; ++b) {
                const int from = b == 0 ? 0 : rows.split, to = b == 0 ? (rows.rows == 2 ? rows.split : count) : count;
                const float ends = FxScreen::kPickTypesX + FxScreen::typesWidth(font, names, from, to);
                int last = -1;
                for (int x = FxScreen::kPickTypesX - 1; x <= right; ++x)
                    if (inkIn(x, bands[b].first, x + 1, bands[b].second + 1) > 0) last = x;
                CHECK(last >= ends - font.width("e") && last <= ends + 1.f); // its last word ends the row: no ", " after it
                CHECK(inkIn(FxScreen::kPickTypesX - 1, bands[b].first, FxScreen::kPickTypesX + 3, bands[b].second + 1) > 0); // and it starts at the column
            }
            if (rows.rows == 2) {
                ++twoRows;
                CHECK(bands.size() == 2 && bands[1].first > bands[0].second + 1); // two rows apart (at the size chosen where each fits)
            }
            // The one lit is in brass, and only it.
            int brass = 0;
            for (int y = lineTop; y < lineTop + FxScreen::kPickLineH; ++y)
                for (int x = FxScreen::kPickTypesX; x <= right; ++x) brass += canvas.is(x, y, ui::kBrass) ? 1 : 0;
            CHECK(brass > 6);
            heldOneRow += rows.rows == 1 && rows.set != &chosen ? 1 : 0;

            // THE TYPE ROW, on each type, held and not: in its room, "..." for those left out, the key in its own room.
            // (Of a family with types to go to: one with one has no key, and its room goes to the edge.)
            if (count < 2) continue;
            const std::vector<std::string> big = upper(names);
            for (int at = 0; at < count; ++at) {
                int from = 0, to = 0;
                const Fonts& drawn = FxScreen::typeRowView(chosen, big, at, &from, &to); // (the set the row is drawn in)
                const float room = FxScreen::typeRowRoom(drawn, count), roomEnd = FxScreen::typeRowX(drawn) + room;
                const float keyX = FxScreen::kPaneX + FxScreen::kPaneW - 12.f - ui::keyWidth(drawn, "LEFT / RIGHT");
                CHECK(roomEnd + 12.f <= keyX + 0.01f);
                const int y0 = FxScreen::kTypeRowY - 6, y1 = FxScreen::kTypeRowY + static_cast<int>(drawn.small.capHeight()) + 7;
                for (int held = 0; held < 2; ++held) {
                    canvas.clear();
                    FxScreen::drawTypeRow(canvas.r, chosen, big, at, held == 1);
                    // Nothing past the room but the key, and the key only with A held.
                    CHECK(inkIn(static_cast<int>(roomEnd) + 7, y0, static_cast<int>(keyX) - 1, y1) == 0);
                    CHECK((inkIn(static_cast<int>(keyX), y0, FxScreen::kPaneX + FxScreen::kPaneW, y1) > 0) == (held == 1));
                    // "..." where the rest are: at the left before the first in view, at the right after the last.
                    float x = FxScreen::typeRowX(drawn);
                    const float dots = drawn.small.width(FxScreen::kTypeMore);
                    if (from > 0) {
                        CHECK(inkIn(static_cast<int>(x), y0, static_cast<int>(x + dots) + 1, y1) > 0);
                        cutLeft += held;
                        x += dots + FxScreen::kTypeGapX;
                    }
                    for (int t = from; t < to; ++t) x += drawn.small.width(big[t].c_str()) + FxScreen::kTypeGapX;
                    if (to < count) {
                        CHECK(inkIn(static_cast<int>(x), y0, static_cast<int>(x + dots) + 1, y1) > 0);
                        CHECK(x + dots <= roomEnd + 0.01f);
                        cut += held;
                    } else if (at != count - 1) {
                        CHECK(inkIn(static_cast<int>(x) - 2, y0, static_cast<int>(roomEnd) + 7, y1) == 0); // nothing after the last
                    }
                }
            }
        }
    }
    std::printf("    the list of effects as drawn: %d lines in two rows, %d held at LARGE in one; the TYPE row cut %d times on the right, %d on the left\n", twoRows, heldOneRow, cut, cutLeft);
    CHECK(twoRows >= 4 && heldOneRow >= 1 && cut >= 4 && cutLeft >= 4); // (each way of drawing was reached)
}

// What fits at LARGE and did not at XL, in states no still shows (a review found them): a
// song's own name beside OPEN NOW > REVS, a wide track name beside its note on the SET page,
// the longest thing the scene's length says, and the step numbers of a long cell's last bars.
void testWhatOnlyLargeHadRoomFor() {
    Screens& s = screens();
    const std::string folder = tempFolder("textsize");
    // A new song's name is two words from a list. The widest pairs that still clear OPEN NOW > REVS at LARGE:
    // a seed for each is looked for, and the open song's row drawn.
    {
        int tried = 0;
        for (std::uint32_t seed = 1; seed < 600 && tried < 4; seed += 2) {
            Rig rig;
            const std::string songs = folder + "/songs" + std::to_string(seed);
            rig.session().useSongFolder(songs, seed);
            std::string upper = rig.session().songName();
            for (char& c : upper) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
            if (s.at[kTextXl]->medium.width(upper.c_str()) <= 160.f) continue; // (a name with room to spare at XL: not the case looked for)
            ++tried;
            rig.session().view = View::Song;
            rig.app.drums().setStep(0, 0, 0.8f);
            rig.frames(2);
            rig.app.song().moveCursorTo(0, 0);
            rig.tap(SELECT);
            holds(rig, ("the list of songs, the open song " + upper).c_str());
            CHECK(drew(upper.c_str()) && drew("OPEN NOW  >  REVS"));
            // Held at LARGE, both of them: the name, and what stands beside it.
            CHECK(capAtXl(upper.c_str(), ui::kTopBarH) == s.at[kTextLarge]->medium.capHeight() && capAtXl("OPEN NOW  >  REVS") == s.at[kTextLarge]->small.capHeight());
            CHECK(asAtLarge(upper.c_str(), ui::kTopBarH) && asAtLarge("OPEN NOW  >  REVS"));
        }
        CHECK(tried == 4);
        // A short name follows the setting, and so does what stands beside it.
        Rig rig;
        for (std::uint32_t seed = 1; seed < 600; seed += 2) {
            rig.session().useSongFolder(folder + "/short" + std::to_string(seed), seed);
            if (rig.session().songName().size() <= 8) break;
        }
        std::string upper = rig.session().songName();
        for (char& c : upper) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        CHECK(upper.size() <= 8);
        rig.session().view = View::Song;
        rig.app.drums().setStep(0, 0, 0.8f);
        rig.frames(2);
        rig.app.song().moveCursorTo(0, 0);
        rig.tap(SELECT);
        holds(rig, "the list of songs, the open song's name short");
        CHECK(capAtXl(upper.c_str(), ui::kTopBarH) == s.at[kTextXl]->medium.capHeight() && capAtXl("OPEN NOW  >  REVS") == s.at[kTextXl]->small.capHeight());
    }
    // SET: the track's name at its widest, beside what the row says about it.
    {
        Rig rig;
        for (int t = 1; t < kNumTracks; ++t) CHECK(rig.session().addTrack(TrackKind::Phazed));
        rig.session().setTrackName(kNumTracks - 1, "WWWWWWWWWW");
        rig.project().track = kNumTracks - 1;
        rig.session().view = View::Settings;
        rig.app.settings().goTo(SettingsScreen::kTrackName);
        rig.frames(2);
        holds(rig, "SET, a track's name at its widest");
        CHECK(drew("12 WWWWWWWWWW") && drew("left / right: from a list.  A: type one"));
        CHECK(capAtXl("12 WWWWWWWWWW") == s.at[kTextLarge]->large.capHeight() && capAtXl("left / right: from a list.  A: type one") == s.at[kTextLarge]->small.capHeight());
        CHECK(asAtLarge("12 WWWWWWWWWW") && asAtLarge("left / right: from a list.  A: type one")); // (and in the middle of the row, as at LARGE)
        CHECK(capAtXl("TRACK NAME") == s.at[kTextXl]->small.capHeight()); // (the row's own name has its room: it follows)
        // An ordinary name follows the setting, value and note.
        rig.session().setTrackName(kNumTracks - 1, "BASS");
        rig.frames(2);
        holds(rig, "SET, a track's name");
        CHECK(capAtXl("12 BASS") == s.at[kTextXl]->large.capHeight() && capAtXl("left / right: from a list.  A: type one") == s.at[kTextXl]->small.capHeight());
    }
    // What SEL + L2 says at its longest: scene 10, a step short of eight bars. And the last two
    // bars' step numbers, three figures each, on the piano roll, the drum grid and its slices.
    {
        Rig rig;
        CHECK(rig.session().addTrack(TrackKind::Phazed));
        Project& project = rig.project();
        project.scene = 9;
        rig.session().setCellBars(0, 9, 8);
        rig.session().setCellBars(1, 9, 8);
        rig.session().view = View::Pattern;
        rig.frames(2);
        rig.key(SELECT, true);
        rig.tap(L2);
        rig.key(SELECT, false);
        rig.frames(1);
        CHECK(std::strcmp(rig.app.undoSaid(), "SCENE 10: 127 STEPS OF 128") == 0);
        holds(rig, "the scene's length, said at its longest"); // (it stands over the strip's top: no strip is held to the pixel here)
        CHECK(drew("SCENE 10: 127 STEPS OF 128") && capAtXl("SCENE 10: 127 STEPS OF 128") == s.at[kTextLarge]->medium.capHeight());
        CHECK(asAtLarge("SCENE 10: 127 STEPS OF 128"));
        rig.key(SELECT, true);
        rig.tap(R2);
        rig.key(SELECT, false);
        rig.frames(1);
        holds(rig, "the scene's length, all of it");
        CHECK(drew("SCENE 10: ALL 128 STEPS") && capAtXl("SCENE 10: ALL 128 STEPS") == s.at[kTextXl]->medium.capHeight()); // (it has the room: it follows)
        rig.frames(120); // (it has gone)
        CHECK(!rig.app.undoSaid()[0]);
        for (int bar = 5; bar < 8; ++bar) {
            project.bar = bar;
            for (int track = 0; track < 2; ++track) {
                project.track = track;
                rig.frames(2);
                char what[64];
                std::snprintf(what, sizeof what, "%s, bar %d of eight", track ? "the piano roll" : "the drum grid", bar + 1);
                holds(rig, what, track ? rollStrip(rig.session().octavesShown()) : kDrumStrip);
                char number[8];
                std::snprintf(number, sizeof number, "%d", bar * kSteps + 16);
                CHECK(drew(number));
                // The roll's numbers stand in 25 pixels at the left: a bar of three figures is held at LARGE, all sixteen of them; two figures follow.
                if (track) {
                    int held = 0, follow = 0;
                    for (const textfit::Text& t : lastXl.texts)
                        if (t.x + t.w <= 30.f && t.y > ui::kTopBarH && t.y < ui::kScreenH - ui::kHintsH - 40) (t.cap == s.at[kTextLarge]->small.capHeight() ? held : follow) += 1;
                    CHECK(bar >= 6 ? (held == kSteps && follow == 0) : (held == 0 && follow == kSteps));
                    for (const textfit::Text& t : lastXl.texts) CHECK(t.x >= 0.f);
                    CHECK(bar < 6 || asAtLarge(number)); // (and in the middle of its row, as at LARGE)
                }
            }
        }
    }
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
}

// A size loaded over another leaves nothing behind: a font holds one sheet of letters, whatever
// it has been through. (Changing TEXT SIZE loads three; so does every still's "--texts".)
void testNoLettersAreLeftBehind() {
    Screens& s = screens();
    const std::vector<unsigned char> ttf = loadFontFile();
    const int before = Font::sheets();
    CHECK(before == 3 * (1 + kTextSizes)); // the canvas's own, and the four sets
    {
        Fonts set;
        CHECK(Font::sheets() == before);
        for (int round = 0; round < 5; ++round)
            for (int size = 0; size < kTextSizes; ++size) {
                CHECK(set.load(s.canvas.r, ttf, size));
                CHECK(Font::sheets() == before + 3);
            }
        set.small.unload();
        CHECK(Font::sheets() == before + 2);
        set.small.unload(); // (nothing to let go: nothing happens)
        CHECK(Font::sheets() == before + 2);
        CHECK(set.small.load(s.canvas.r, ttf, 12.f) && Font::sheets() == before + 3);
        set.unload();
        CHECK(Font::sheets() == before);
        CHECK(set.load(s.canvas.r, ttf, kTextXl) && Font::sheets() == before + 3);
    }
    CHECK(Font::sheets() == before); // (a font that goes takes its letters with it)
}

// Where the dice's chip starts at the end of the bar under a strip: worked out from the bar's own
// lettering, so the same at every size.
void testTheDiceEndAtEverySize() {
    Screens& s = screens();
    for (int size = 0; size < kTextSizes; ++size) CHECK(ui::hintDiceEndX(*s.at[size], "Y") == ui::hintDiceEndX(*s.canvas.fonts, "Y"));
    CHECK(ui::hintDiceEndX(*s.canvas.fonts, "Y") > 500.f);
}

// ---- the SET page's bars: never through the words beside them -----------------------------------
// BOUNCE under way has a bar and a long value (WORKING 37%, TAIL 12 S): at every size the bar stands
// clear of the value and of what is said at the right. VOLUME's stays where it always was.
void testTheBounceBarClearsItsWords() {
    typedef SettingsScreen S;
    Screens& s = screens();
    if (!s.canvas.ok) return;
    Rig rig;
    rig.session().view = View::Settings;
    const struct { float progress, tail; } states[] = {{0.37f, -1.f}, {1.f, -1.f}, {0.f, -1.f}, {1.f, 12.4f}, {1.f, 479.9f}};
    for (int size = 0; size < kTextSizes; ++size)
        for (const auto& state : states)
            for (int here = 0; here < 2; ++here) {
                rig.app.settings().goTo(here ? S::kBounce : S::kVolume);
                rig.session().pretendBounce(state.progress, state.tail);
                rig.frames(1);
                rig.session().pretendBounce(state.progress, state.tail);
                const Drawn drawn = rig.draw(s.at[size].get());
                const auto barIn = [&](int row, float* x, float* w) {
                    const float top = static_cast<float>(rig.app.settings().rowNowY(row));
                    for (const textfit::Box& b : drawn.frame.boxes)
                        if (b.h == 4.f && b.y == top + S::kRowH - 16) { *x = b.x, *w = b.w; return true; } // (the first: the bar's whole length)
                    return false;
                };
                const float top = static_cast<float>(rig.app.settings().rowNowY(S::kBounce));
                float valueEnd = 0.f, noteStart = 0.f, barX = 0.f, barW = 0.f;
                for (const textfit::Text& t : drawn.frame.texts) {
                    if (t.y < top || t.y > top + S::kRowH) continue;
                    if (t.x == S::kValueX) valueEnd = t.x + t.w;
                    if (std::fabs(t.x + t.w - S::kNoteRight) < 0.01f) noteStart = t.x;
                }
                const bool bar = barIn(S::kBounce, &barX, &barW);
                CHECK(bar && valueEnd > S::kValueX && noteStart > valueEnd);
                CHECK(barX >= valueEnd + S::kBarGap - 1.f && barX + barW <= noteStart - S::kNoteGap + 1.f && barW >= 40.f);
                if (!(barX >= valueEnd + S::kBarGap - 1.f && barX + barW <= noteStart - S::kNoteGap + 1.f))
                    std::printf("    BOUNCE at %s: value to %.1f, bar %.1f to %.1f, words from %.1f\n", kTextSizeNames[size], valueEnd, barX, barX + barW, noteStart);
                float volX = 0.f, volW = 0.f;
                CHECK(barIn(S::kVolume, &volX, &volW) && volX == S::kBarX && volW == S::kBarW);
            }
    // On our own firmware: BRIGHTNESS's bar between its value and its words, and STICK LIGHT's value
    // clear of its words, at every size, each value and each choice.
    rig.session().useLights(Lights::pretend());
    for (int size = 0; size < kTextSizes; ++size)
        for (int row : {static_cast<int>(S::kBrightness), static_cast<int>(S::kLight)})
            for (int value = 0; value < (row == S::kBrightness ? 2 : kRingModes); ++value) {
                if (row == S::kBrightness) rig.session().setBrightness(value ? kBrightMost : kBrightLeast);
                else rig.session().setStickLight(value);
                rig.app.settings().goTo(row);
                rig.frames(1);
                const Drawn drawn = rig.draw(s.at[size].get());
                const float top = static_cast<float>(rig.app.settings().rowNowY(row));
                float valueEnd = 0.f, noteStart = 0.f, barX = 0.f, barW = 0.f;
                for (const textfit::Text& t : drawn.frame.texts) {
                    if (t.y < top || t.y > top + S::kRowH) continue;
                    if (t.x == S::kValueX) valueEnd = t.x + t.w;
                    if (std::fabs(t.x + t.w - S::kNoteRight) < 0.01f) noteStart = t.x;
                }
                CHECK(valueEnd > S::kValueX && noteStart >= valueEnd + S::kNoteGap - 0.5f);
                bool bar = false;
                for (const textfit::Box& b : drawn.frame.boxes)
                    if (!bar && b.h == 4.f && b.y == top + S::kRowH - 16) bar = true, barX = b.x, barW = b.w;
                CHECK(bar == (row == S::kBrightness));
                if (bar) CHECK(barX >= valueEnd + S::kBarGap - 1.f && barX + barW <= noteStart - S::kNoteGap + 1.f && barW >= 40.f);
            }
    rig.session().useLights(Lights());
}

// The step numbers of a scene of 25 steps, bar 2: they count through the cell (17 to 25) and a step
// past the scene's end reads "-" (26 to 32: seven of them), on the drum grid, its slice view and the
// piano roll alike. Read off what is drawn, at LARGE.
void testTheStepNumbersOfAShortScene() {
    const std::string folder = tempFolder("textsize");
    std::vector<float> sound(48000);
    for (std::size_t i = 0; i < sound.size(); ++i) sound[i] = 0.4f * std::sin(static_cast<float>(i) * 0.05f);
    CHECK(writeWav(folder + "/loop.wav", sound, 48000));
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    session.useSampleFolder(folder);
    CHECK(session.addTrack(TrackKind::Phazed));
    session.setLaneSample(0, 5, "loop");
    session.finishStretching();
    session.setCellBars(0, 0, 2);
    session.setCellBars(1, 0, 2);
    session.view = View::Pattern;
    rig.frames(2);
    // How many times each of these words is drawn now.
    const auto count = [&](const char* words) {
        const Drawn now = rig.draw(screens().at[kTextLarge].get());
        int n = 0;
        for (const textfit::Text& t : now.frame.texts) n += t.words == words ? 1 : 0;
        return n;
    };
    PatternScreen& drums = rig.app.drums();
    const auto look = [&](const char* what) {
        project.bar = 1;
        rig.frames(2);
        session.setSceneSteps(0, 2 * kSteps); // every step (kept as 0)
        rig.frames(2);
        const int dashes = count("-");
        CHECK(count("17") >= 1 && count("32") >= 1 && count("26") >= 1);
        session.setSceneSteps(0, 25);
        rig.frames(2);
        const bool right = count("17") >= 1 && count("25") >= 1 && count("26") == 0 && count("32") == 0 && count("-") == dashes + 7;
        if (!right) std::printf("    %s: a scene of 25 steps, bar 2: not 17 to 25 and seven dashes\n", what);
        CHECK(right);
        session.setSceneSteps(0, 2 * kSteps);
        rig.frames(2);
    };
    project.track = 0;
    drums.moveCursorTo(5, 2);
    rig.frames(2);
    look("the drum grid");
    rig.tap(L1);
    CHECK(drums.slicing());
    look("the slice view");
    rig.tap(L1);
    CHECK(!drums.slicing());
    project.track = 1;
    rig.frames(2);
    look("the piano roll");
    CHECK(std::system(("rm -rf '" + folder + "'").c_str()) == 0);
}

int main(int argc, char** argv) {
    // The engine's own presets only: the tests here are of their tables. The presets the app's data
    // adds (and any track sound it gives a built-in) are presets_test's.
    for (TrackKind kind : {TrackKind::Phazed, TrackKind::Jx3p, TrackKind::Fm6, TrackKind::Physic, TrackKind::Acid, TrackKind::Cloud, TrackKind::Morph}) useBuiltInDataForTests(kind, PresetData());
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (argc >= 3 && std::strcmp(argv[1], "views") == 0) return runViews(argv[2], argc >= 4 ? argv[3] : nullptr);
    CHECK(screens().canvas.ok);
    if (argc >= 2 && std::strcmp(argv[1], "--fx-list") == 0) { // just the list of effects (ctest runs everything)
        testTheListOfEffectsHoldsTheLaterTypes();
        testTheListOfEffectsAsDrawn();
        std::printf(failures ? "%d check(s) failed\n" : "text size: the list of effects passed\n", failures);
        return failures ? 1 : 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--bounce") == 0) { // just the SET page's bars (ctest runs everything)
        testTheBounceBarClearsItsWords();
        std::printf(failures ? "%d check(s) failed\n" : "text size: bounce bar passed\n", failures);
        return failures ? 1 : 0;
    }
    testTheMeasure();
    testTheSizes();
    testWithin();
    testTheBoxes();
    testTheSettingIsKept();
    testTheRow();
    testTheNetworkRow();
    testTheDrumGridsRows();
    testTheStepNumbersOfAShortScene();
    testThePianoRollsRows();
    testTheOtherScreens();
    testWhatOnlyLargeHadRoomFor();
    testNoLettersAreLeftBehind();
    testTheDiceEndAtEverySize();
    testTheControlsSetup();
    testTheUpdateNotice();
    testTheFxScreen();
    testTheListOfEffectsHoldsTheLaterTypes();
    testTheListOfEffectsAsDrawn();
    testTheBounceBarClearsItsWords();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("text size: tests passed\n");
    return 0;
}
