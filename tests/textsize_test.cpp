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
#include "chord_effects.hpp"
#include "chords.hpp"
#include "controls.hpp"
#include "note_marks.hpp"
#include "pattern_layout.hpp"
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
    "notefx-glides-far2-short", "notefx-glides-far2-short-drums",
    "notefx-from", "notefx-from-paired", "notefx-from-nearest", "notefx-from-random",
    "notefx-from-held", "notefx-from-paired-held", "notefx-from-nearest-held", "notefx-from-random-held",
    "under", "songs", "sampler", "bars", "bars-roll",
    "fx", "fx --hold-a", "fx-type", "fx-type --hold-a", "fx-pick", "fx-pick --hold-a", "fx-comp", "fx-comp --hold-a", "fx-master", "fx-master --hold-a",
    "fx-duck", "fx-duck --hold-a", "fx-choice", "fx-choice --hold-a", "fx-auto", "fx-auto --hold-a", "fx-sends", "fx-sends --hold-a",
    "fx-bus", "fx-bus --hold-a", "fx-level", "fx-level --hold-a", "fx-ping", "fx-ping --hold-a", "fx-ping-bus", "fx-ping-bus --hold-a",
    "bounce", "bounce-tail", "panic", "wifi", "revs", "add-track", "chords", "chords-swap", "notes",
    "phazed", "page0", "page1", "page2", "page3", "page4", "page5", "page6", "page7", "page8", "page9", "twin", "twin0", "twin1", "twin2", "twin3", "twin4",
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

// The names in a source that no still in kViews has in it.
std::vector<std::string> unlisted(const std::string& source) {
    std::vector<std::string> missing;
    for (const std::string& name : viewsInMain(source)) {
        bool listed = false;
        for (const char* view : kViews) listed = listed || std::strstr(view, name.c_str()) != nullptr;
        if (!listed) missing.push_back(name);
    }
    return missing;
}

void testEveryViewIsListed() {
    std::string source;
    CHECK(readTextFile(std::string(APOTHECA_SOURCE_DIR) + "/app/main.cpp", source));
    CHECK(viewsInMain(source).size() > 60); // (main.cpp was read, and the names found in it)
    for (const std::string& name : unlisted(source)) std::printf("    main.cpp knows a still named \"%s\": add it to kViews\n", name.c_str());
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
        for (int y = SettingsScreen::rowY(0); y < SettingsScreen::rowY(SettingsScreen::kRows - 1) + SettingsScreen::kRowH - 6; ++y) { // (the rows: nothing in them moves while the song plays)
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

    if (!only) testTheStillsFlag(program, folder);
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
    char pattern[] = "/tmp/apotheca-textsize-XXXXXX";
    const std::string folder = mkdtemp(pattern), file = folder + "/texts";
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
    ui::macroBox(s.canvas.r, xl, 200, 40, w, 70, name.c_str(), value.c_str(), 0.7f, false, false);         // (no picture goes by that name)
    ui::macroBox(s.canvas.r, xl, 200, 140, w, 70, "TONE", "+60", 0.7f, true, false);                        // a picture in its corner
    ui::macroBox(s.canvas.r, xl, 400, 40, w, 70, "TONE", value.c_str(), 0.7f, false, false);
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
    ui::macroBox(s.canvas.r, xl, 200, 40, w, 70, "TONE", "+0", 0.5f, false, false);
    gDrawLog = nullptr;
    CHECK(log.texts.size() == 2 && log.texts[0].font == &xl.small);
    CHECK(&xl.within(&Fonts::small, pictured.c_str(), w - 14.f - 27.f) == &xl.fixed().small && &xl.within(&Fonts::small, pictured.c_str(), w - 14.f) == &xl.small);
    {
        // In the box itself: RELEASE in one narrow enough that at XL it would reach its picture, and not the box's end.
        const int narrow = 110;
        CHECK(xl.small.width("RELEASE") > narrow - 14.f - 27.f && xl.small.width("RELEASE") <= narrow - 14.f && xl.fixed().small.width("RELEASE") <= narrow - 14.f - 27.f);
        log.clear();
        gDrawLog = &log;
        ui::macroBox(s.canvas.r, xl, 200, 240, narrow, 70, "RELEASE", "+0", 0.5f, false, false);
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

// ---- the tight rows stay at LARGE ---------------------------------------------------------------
// The top bar, the hints bar and the strips under the two grids are the same pixels at every
// size (so every test that reads a strip or a bar off the screen at LARGE holds at every
// setting); the grid above them follows the setting.
const int kDrumStrip = pattern_layout::kStripY;
int rollStrip(int octaves) { return 45 + (octaves == 1 ? 24 : octaves == 2 ? 22 : 16) + 2 + 16 * 21 + 4; } // under the keys and sixteen rows of 21, 4 down

void testTheDrumGridsRows() {
    char pattern[] = "/tmp/apotheca-textsize-XXXXXX";
    const std::string folder = mkdtemp(pattern);
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

const int kLastWholeWord = 5; // the most tracks whose heads have room for DELETING whole; past it, DEL

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
    char pattern[] = "/tmp/apotheca-textsize-XXXXXX";
    const std::string folder = mkdtemp(pattern), samples = folder + "/samples";
    mkdir(samples.c_str(), 0755);
    mkdir((samples + "/a folder with a long name").c_str(), 0755);
    std::vector<float> sound(24000);
    for (std::size_t i = 0; i < sound.size(); ++i) sound[i] = 0.4f * std::sin(static_cast<float>(i) * 0.05f);
    CHECK(writeWav(samples + "/a folder with a long name/a recording with a very long name indeed.wav", sound, 48000));
    CHECK(writeWav(samples + "/short.wav", sound, 48000));
    Rig rig;
    Session& session = rig.session();
    Project& project = rig.project();
    session.useSampleFolder(samples);
    const std::string longest = "a folder with a long name/a recording with a very long name indeed";

    // SONG: twelve tracks, the narrowest columns there are, every one with a name of ten wide letters.
    const TrackKind kinds[] = {TrackKind::Phazed, TrackKind::Twin, TrackKind::Qmod, TrackKind::Sampler, TrackKind::Drums};
    for (int t = 1; t < kNumTracks; ++t) CHECK(session.addTrack(kinds[(t - 1) % 5]));
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
        // The four-bar cell's count, over its little picture: at LARGE whatever is chosen. (The scene's own number 4 follows the setting.)
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
        CHECK(drew("8 lanes: built-in drums or your recordings") && capAtXl("8 lanes: built-in drums or your recordings") == 12.f && capAtXl("FM you roll like dice, not program") == 13.f);
        few.tap(B);
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
        // The address under it is wider than the panel at LARGE (as on main): at XL it is held at LARGE.
        bool address = false;
        for (const textfit::Text& t : lastXl.texts)
            if (t.words.compare(0, 17, "OPEN IN A BROWSER") == 0 || t.words.compare(0, 16, "NOT ON A NETWORK") == 0) address = t.cap == 12.f;
        CHECK(address);
        few.tap(L1);
        CHECK(!few.session().transferOn());
        few.tap(X); // a rev is saved: the heading says so, beside SONGS
        holds(few, "the list of songs, a rev just saved");
        CHECK(!drew("KEPT AS YOU GO. NOTHING TO SAVE."));
        few.tap(DOWN);
        few.tap(A); // the open song's revs
        holds(few, "a song's revs");
        CHECK(drew("+ SAVE A REV") && drew("THE SONG AS IT IS NOW"));
        // A clear gap between the two on that line at every size (at XL they met: the second is held at LARGE there).
        float saveEnds = 0.f, whatStarts = 0.f;
        for (const textfit::Text& t : lastXl.texts) {
            if (t.words == "+ SAVE A REV") saveEnds = t.x + t.w;
            if (t.words == "THE SONG AS IT IS NOW") whatStarts = t.x;
        }
        CHECK(saveEnds > 0.f && whatStarts - saveEnds >= 12.f);
    }

    // The DEVICE screens: every page of every synth, the cursor on its first box.
    struct Synth { TrackKind kind; SynthScreen* screen; const char* name; };
    const Synth synths[] = {{TrackKind::Phazed, &rig.app.phazed(), "PHAZED"}, {TrackKind::Twin, &rig.app.twin(), "TWIN"}, {TrackKind::Qmod, &rig.app.qmod(), "QMOD"}};
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
            CHECK(holds(rig, what) > 3);
        }
        synth.screen->goTo(0, SynthScreen::kFirstMacro);
        rig.key(A, true);
        rig.frames(2);
        holds(rig, "a synth's macro, A held");
        rig.key(A, false);
        rig.frames(1);
    }
    CHECK(pagesSeen >= 10 + 4 + 2);

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
    CHECK(drew("NOT IN THE SAMPLES FOLDER. THE DRUM PLAYS.") && drew("BUILT IN") && drew("SHORT"));
    // (That line runs off the screen's edge at LARGE, as it does on main: at XL it is held at LARGE, no further off.)
    CHECK(capAtXl("NOT IN THE SAMPLES FOLDER. THE DRUM PLAYS.") == 12.f && capAtXl("THE RECORDING") == 13.f);
    rig.tap(Y); // nothing has played: it says so, over the recordings (and that runs off the edge at LARGE too)
    holds(rig, "the kit, nothing to catch");
    CHECK(drew("NOTHING TO CATCH: PLAY SOMETHING FIRST") && capAtXl("NOTHING TO CATCH: PLAY SOMETHING FIRST") == 12.f);
    rig.app.kit().goTo(2, KitScreen::kFit);
    rig.frames(1);
    holds(rig, "the kit, on FIT TO");
    rig.app.kit().goTo(2, KitScreen::kSound);
    rig.tap(A); // the list, in the long folder
    holds(rig, "the kit's list of recordings");
    rig.tap(R1);
    holds(rig, "the kit's list, the next folder");
    rig.tap(DOWN);
    rig.key(B, true);
    rig.frames(10);
    holds(rig, "the kit's list, B held");
    rig.key(B, false);
    rig.tap(L1);
    rig.tap(A);
    rig.frames(2);

    // What comes up over any screen: the volume, the help card (with a MENU button and without), ALL SOUND STOPPED.
    session.view = View::Song;
    rig.app.changeVolume(-0.1f);
    rig.frames(1);
    holds(rig, "the volume, just changed");
    rig.frames(200);
    rig.key(MENU, true);
    rig.frames(2);
    holds(rig, "the help card", 0, true);
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
    rig.tap(B, static_cast<int>(setup.typed().size())); // nothing typed: the words say what an empty name means
    CHECK(setup.typing() && setup.typed().empty());
    holds(rig, "typing a name, nothing typed");
    for (int i = 0; i < 22; ++i) rig.tap(RIGHT); // (W is the widest letter: row 3, column 3)
    rig.tap(DOWN, 2);
    rig.tap(RIGHT, 2);
    for (int i = 0; i < 10; ++i) rig.tap(A);
    CHECK(setup.typed().size() == 10);
    holds(rig, "typing a name, ten letters");
    rig.tap(Y);
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
    session.pretendBounce(0.37f, -1.f);
    rig.frames(1);
    holds(rig, "a bounce under way");
    session.pretendBounce(1.f, 123.4f);
    rig.frames(1);
    holds(rig, "a bounce in its tail");
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
        {&Fonts::small, "Only accept it if you sent it yourself."}, {&Fonts::small, "HOLD A for a second to accept.    B to throw it away."},
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
    // Every effect in a slot of the track and (a delay or a reverb) on its bus: each setting at
    // nine places along its range, the cursor on it. The list of effects open on each.
    for (int k = 1; k < kEffectKinds; ++k) {
        const EffectKind kind = static_cast<EffectKind>(k);
        const EffectInfo& info = effectInfo(kind);
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

// What fits at LARGE and did not at XL, in states no still shows (a review found them): a
// song's own name beside OPEN NOW > REVS, a wide track name beside its note on the SET page,
// the longest thing the scene's length says, and the step numbers of a long cell's last bars.
void testWhatOnlyLargeHadRoomFor() {
    Screens& s = screens();
    char pattern[] = "/tmp/apotheca-textsize-XXXXXX";
    const std::string folder = mkdtemp(pattern);
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

// The offer at the start (BUTTONS NOT WORKING?) stands where the hints bar does and runs the
// screen's width: the same pixels whatever is chosen, and inside the screen.
void testTheOfferAtTheStart() {
    Screens& s = screens();
    // (And where the dice's chip starts at the end of the bar under a strip: worked out from the bar's own lettering.)
    for (int size = 0; size < kTextSizes; ++size) CHECK(ui::hintDiceEndX(*s.at[size], "Y") == ui::hintDiceEndX(*s.canvas.fonts, "Y"));
    CHECK(ui::hintDiceEndX(*s.canvas.fonts, "Y") > 500.f);
    std::vector<Uint32> large;
    for (int size : {kTextLarge, kTextSmall, kTextMedium, kTextXl}) {
        DrawLog log;
        s.canvas.clear();
        gDrawLog = &log;
        ui::relearnOffer(s.canvas.r, *s.at[size], 0.5f);
        gDrawLog = nullptr;
        SDL_RenderPresent(s.canvas.r);
        const std::vector<Uint32> now = s.canvas.all();
        if (size == kTextLarge) large = now;
        CHECK(now == large);
        CHECK(log.texts.size() == 1 && log.texts[0].font == &s.canvas.fonts->small);
        // (Its words are 655 pixels at LARGE, 15 more than the screen: so they are on main too, a letter's edge
        // lost at each end. Not mended here: it would change LARGE. They are in the bar's place, up and down.)
        CHECK(log.texts.size() == 1 && log.texts[0].y >= ui::kScreenH - ui::kHintsH && log.texts[0].y + log.texts[0].h <= ui::kScreenH);
        CHECK(log.texts.size() == 1 && near(log.texts[0].x + log.texts[0].w / 2.f, ui::kScreenW / 2.f));
        // The line that fills is half way, and the bar is nowhere but the hints bar's place.
        CHECK(log.boxes.size() == 2 && log.boxes[0].y == ui::kScreenH - ui::kHintsH && log.boxes[0].h == ui::kHintsH && log.boxes[1].w == ui::kScreenW / 2 && log.boxes[1].h == 3);
    }
}

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (argc >= 3 && std::strcmp(argv[1], "views") == 0) return runViews(argv[2], argc >= 4 ? argv[3] : nullptr);
    CHECK(screens().canvas.ok);
    testTheMeasure();
    testTheSizes();
    testWithin();
    testTheBoxes();
    testTheSettingIsKept();
    testTheRow();
    testTheDrumGridsRows();
    testThePianoRollsRows();
    testTheOtherScreens();
    testWhatOnlyLargeHadRoomFor();
    testNoLettersAreLeftBehind();
    testTheOfferAtTheStart();
    testTheControlsSetup();
    testTheUpdateNotice();
    testTheFxScreen();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("text size: tests passed\n");
    return 0;
}
