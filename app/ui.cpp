#include "ui.hpp"
#include "instruments.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace apotheca {
namespace ui {

void fill(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

void outline(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c, int thickness) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int i = 0; i < thickness; ++i) {
        SDL_Rect rect = {x + i, y + i, w - 2 * i, h - 2 * i};
        SDL_RenderDrawRect(r, &rect);
    }
}

void box(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color inside, SDL_Color edge) {
    fill(r, x, y, w, h, inside);
    outline(r, x, y, w, h, edge);
}

SDL_Color faded(SDL_Color c, int alpha) {
    c.a = static_cast<Uint8>(alpha);
    return c;
}

void sceneProgress(SDL_Renderer* r, int x, int y, int w, int h, int plays, int play, int step, int steps) {
    if (steps < 1) steps = kSteps;
    if (plays < 1) plays = 1;
    const int gap = plays > 8 ? 1 : 2;
    for (int i = 0; i < plays; ++i) {
        const int x0 = x + (w + gap) * i / plays, x1 = x + (w + gap) * (i + 1) / plays - gap;
        const int bw = x1 - x0 < 1 ? 1 : x1 - x0;
        fill(r, x0, y, bw, h, kLine);
        int done = i < play ? bw : (i == play && step >= 0 ? bw * (step + 1) / steps : 0);
        if (i == play && step >= 0 && done < 1) done = 1;
        if (done > 0) fill(r, x0, y, done, h, kPurple);
    }
}

// ---- macro pictures -----------------------------------------------------------------
// Each is a few lines on a 20 by 14 grid, drawn in the top right of its box.

namespace {

struct Stroke { int n; int p[16]; }; // x,y pairs
struct Picture { const char* name; int family; Stroke strokes[3]; };
// family: 0 the sound's colour, 1 movement and width, 2 time
const Picture kPictures[] = {
    {"TONE", 0, {{4, {0, 4, 9, 4, 13, 2, 19, 13}}}},
    {"MOTION", 1, {{7, {0, 7, 3, 2, 6, 7, 9, 12, 12, 7, 15, 2, 19, 9}}}},
    {"ATTACK", 2, {{3, {0, 13, 9, 1, 19, 1}}}},
    {"RELEASE", 2, {{5, {0, 1, 6, 1, 9, 8, 13, 11, 19, 13}}}},
    {"DETUNE", 1, {{5, {0, 7, 4, 2, 9, 12, 14, 2, 19, 7}}, {5, {0, 9, 5, 3, 11, 12, 17, 3, 19, 6}}}},
    {"VIBRATO", 1, {{8, {0, 7, 3, 6, 6, 8, 9, 4, 12, 10, 15, 1, 18, 13, 19, 9}}}},
    {"CHORUS", 1, {{5, {0, 3, 5, 1, 10, 3, 15, 5, 19, 3}}, {5, {0, 7, 5, 5, 10, 7, 15, 9, 19, 7}}, {5, {0, 11, 5, 9, 10, 11, 15, 13, 19, 11}}}},
    {"OCTAVE", 0, {{6, {0, 13, 6, 13, 6, 7, 13, 7, 13, 1, 19, 1}}}},
    {"SHAPE", 0, {{3, {0, 13, 4, 1, 8, 13}}, {4, {11, 13, 11, 2, 18, 2, 18, 13}}}},
    {"BLEND", 0, {{2, {0, 1, 19, 13}}, {2, {0, 13, 19, 1}}}},
    {"FORMANT", 0, {{5, {0, 13, 4, 3, 8, 11, 13, 1, 19, 13}}}},
    {"WIDTH", 1, {{3, {6, 2, 0, 7, 6, 12}}, {3, {13, 2, 19, 7, 13, 12}}, {2, {9, 5, 10, 9}}}},
    {"VINTAGE", 1, {{7, {0, 7, 3, 5, 6, 9, 9, 6, 12, 10, 15, 4, 19, 7}}}},
};

const Picture* pictureFor(const char* name) {
    for (const Picture& p : kPictures)
        if (std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}

} // namespace

void macroBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value,
              float part, bool cursor, bool held) {
    const Picture* picture = pictureFor(name);
    const SDL_Color family = !picture ? kDim : picture->family == 0 ? kTeal : picture->family == 1 ? kPurple : kText;
    const bool turned = std::fabs(part - 0.5f) > 0.005f; // away from "as the preset has it"
    const SDL_Color accent = held ? kBrass : kTeal;
    box(r, x, y, w, h, turned ? kPanel2 : kPanel, kLine);
    fonts.small.draw(r, x + 7.f, y + 7.f, name, turned || cursor ? kText : kDim);
    fonts.large.draw(r, x + 7.f, y + 7.f + fonts.small.capHeight() + 8.f, value, cursor ? accent : (turned ? kText : kDim));
    if (picture) {
        const SDL_Color c = turned || cursor ? family : faded(family, 110);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        const int px = x + w - 27, py = y + 7;
        for (const Stroke& s : picture->strokes)
            for (int i = 0; i + 1 < s.n; ++i)
                for (int thick = 0; thick < 2; ++thick) // two passes: one pixel is too faint at this size
                    SDL_RenderDrawLine(r, px + s.p[2 * i], py + s.p[2 * i + 1] + thick, px + s.p[2 * i + 2], py + s.p[2 * i + 3] + thick);
    }
    // The bar grows from the middle, in the macro's own colour.
    const int bx = x + 7, bw = w - 14, by = y + h - 11;
    fill(r, bx, by, bw, 5, kLine);
    if (part > 1.f) part = 1.f;
    if (part < 0.f) part = 0.f;
    const int mid = bx + bw / 2, to = bx + static_cast<int>(std::round(bw * part));
    fill(r, to < mid ? to : mid, by, to < mid ? mid - to : to - mid, 5, cursor ? accent : family);
    fill(r, mid, by - 2, 1, 9, kDim);
    if (cursor) outline(r, x - 1, y - 1, w + 2, h + 2, accent, 2);
}

TopBar topBar(SDL_Renderer* r, const Fonts& fonts, View view, const char* context, const Project& project) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, 0, 0, kScreenW, kScreenH, kBg);
    fill(r, 0, 0, kScreenW, kTopBarH, kPanel);
    fill(r, 0, kTopBarH, kScreenW, 1, kLine);
    const float textY = (kTopBarH - fonts.small.capHeight()) / 2.f;

    // L2 < SONG PATTERN DEVICE > R2
    // L2 and R2 are buttons, not tabs: cream chips like the ones in the hint
    // bar, each with an arrow pointing the way it goes.
    const auto arrow = [&](float ax, bool left) {
        SDL_SetRenderDrawColor(r, kBrass.r, kBrass.g, kBrass.b, 255);
        for (int i = 0; i < 5; ++i) {
            const int px = static_cast<int>(ax) + (left ? i : 4 - i);
            SDL_RenderDrawLine(r, px, kTopBarH / 2 - i, px, kTopBarH / 2 + i);
        }
    };
    const auto chip = [&](float cx, const char* name) {
        const float w = fonts.small.width(name) + 8.f;
        fill(r, static_cast<int>(cx), 5, static_cast<int>(w), kTopBarH - 10, kBrass);
        fonts.small.draw(r, cx + 4.f, textY, name, kBg);
        return w;
    };
    float x = 4.f;
    arrow(x, true);
    x += 8.f;
    x += chip(x, "L2") + 4.f;
    const char* tabs[] = {"SONG", "PATTERN", "DEVICE", "FX", "SET"};
    for (int i = 0; i < 5; ++i) {
        const float w = fonts.small.width(tabs[i]) + 10.f;
        const bool here = static_cast<int>(view) == i;
        if (here) fill(r, static_cast<int>(x), 2, static_cast<int>(w), kTopBarH - 4, kTeal);
        fonts.small.draw(r, x + 5.f, textY, tabs[i], here ? kBg : kDim);
        x += w + 2.f;
    }
    x += 2.f;
    x += chip(x, "R2") + 3.f;
    arrow(x, false);
    x += 5.f + 10.f;
    // Tempo, then a triangle while playing or a square when stopped.
    const int markX = kScreenW - 20;
    SDL_SetRenderDrawColor(r, kPurple.r, kPurple.g, kPurple.b, 255);
    if (project.playing) {
        for (int i = 0; i <= 12; ++i) SDL_RenderDrawLine(r, markX, 7 + i, markX + (i <= 6 ? i : 12 - i) * 2, 7 + i);
    } else {
        fill(r, markX, 8, 10, 10, kStop);
    }
    char text[16];
    std::snprintf(text, sizeof text, "%d", static_cast<int>(project.tempo));
    TopBar bar;
    bar.tempoRight = markX - 14.f;
    bar.tempoNumberRight = bar.tempoRight - fonts.small.width("BPM") - 5.f;
    fonts.small.drawRight(r, bar.tempoRight, textY, "BPM", kDim);
    fonts.small.drawRight(r, bar.tempoNumberRight, textY, text, kText);

    // While it plays: which scene, and how far through it. One block for
    // each time the scene plays, filling as the bars go by, so the change
    // to the next scene can be seen coming. Then the bar of the scene, if
    // it has more than one.
    float room = bar.tempoNumberRight - 34.f; // where the screen's own words must stop
    if (project.writing) {
        // Automation WRITE is on: a red chip, so it cannot be left on by mistake.
        const float w = fonts.small.width("WRITE") + 8.f;
        fill(r, static_cast<int>(room - w), 5, static_cast<int>(w), kTopBarH - 10, kStop);
        fonts.small.draw(r, room - w + 4.f, textY, "WRITE", kBg);
        room -= w + 8.f;
    }
    if (project.silenced) {
        fonts.small.drawRight(r, room - 6.f, textY, "SILENCED", kBrass);
        room -= fonts.small.width("SILENCED") + 16.f;
    } else if (project.busy) {
        // The handheld could not keep up with the sound asked of it, and has
        // turned Twin Analog's oversampling down to stop the stutter.
        fonts.small.drawRight(r, room - 6.f, textY, "BUSY: OVERSAMPLING DOWN", kBrass);
        room -= fonts.small.width("BUSY: OVERSAMPLING DOWN") + 16.f;
    } else if (project.playing && project.playScene >= 0 && project.playScene < kMaxScenes) {
        const int right = static_cast<int>(room) - 6, w = 46;
        const int plays = project.playLoop ? 1 : project.repeats[project.playScene];
        const int bars = project.sceneBars(project.playScene);
        sceneProgress(r, right - w, 9, w, 8, plays, project.playRepeat, project.playBar * kSteps + project.playStep, bars * kSteps);
        char scene[48];
        // The scene's number, then (in a cell of several bars) which bar of it. Short, to leave the title its room.
        if (bars > 1) std::snprintf(scene, sizeof scene, "%d  %d/%d", project.playScene + 1, project.playBar + 1, bars);
        else std::snprintf(scene, sizeof scene, "%d", project.playScene + 1);
        fonts.small.drawRight(r, right - w - 7.f, textY, scene, kPurple);
        room = right - w - 7.f - fonts.small.width(scene) - 8.f;
    }
    // The screen's own words, cut short if they would run into that.
    char shown[64];
    std::snprintf(shown, sizeof shown, "%s", context);
    // Whole parts first ("2 PHAZED / SCENE 1" loses " / SCENE 1"), then letters.
    while (x + fonts.small.width(shown) > room) {
        char* cut = std::strrchr(shown, '/');
        if (!cut || cut == shown) break;
        while (cut > shown && cut[-1] == ' ') --cut;
        *cut = 0;
    }
    for (std::size_t n = std::strlen(shown); n > 0 && x + fonts.small.width(shown) > room; --n) shown[n - 1] = 0;
    fonts.small.draw(r, x, textY, shown, kText);
    return bar;
}

void drawPlayhead(SDL_Renderer* r, const Playhead& head, Uint32 nowMs, int x, int y, int w, int h) {
    const float at = head.at(nowMs);
    if (at >= 0.f) fill(r, x + static_cast<int>(at * static_cast<float>(w - 2)), y, 2, h, kBrass);
}

namespace {
constexpr float kArrowW = 11.f; // an arrow and the room after it
constexpr float kStrokeGap = 2.f; // either side of the "/" between two words: close, to save room (the owner's, 2026-11-11)

// What a hint does, with every " / " drawn close: "level/nudge", a hair apart.
template <typename Each>
void strokeParts(const char* text, Each each) {
    const char* at = text;
    for (;;) {
        const char* cut = std::strstr(at, " / ");
        each(cut ? std::string(at, cut) : std::string(at), cut != nullptr);
        if (!cut) break;
        at = cut + 3;
    }
}
float doesWidth(const Fonts& fonts, const char* text) {
    float w = 0.f;
    strokeParts(text, [&](const std::string& part, bool stroke) {
        w += fonts.small.width(part.c_str());
        if (stroke) w += 2.f * kStrokeGap + fonts.small.width("/");
    });
    return w;
}
void drawDoes(SDL_Renderer* r, const Fonts& fonts, float x, float y, const char* text, SDL_Color color) {
    strokeParts(text, [&](const std::string& part, bool stroke) {
        fonts.small.draw(r, x, y, part.c_str(), color);
        x += fonts.small.width(part.c_str());
        if (!stroke) return;
        fonts.small.draw(r, x + kStrokeGap, y, "/", color);
        x += 2.f * kStrokeGap + fonts.small.width("/");
    });
}

// A key's name a word at a time: `arrow` is 0..3 (up, down, left, right) for a direction, -1 for letters.
template <typename Each>
void keyWords(const char* key, Each each) {
    static const char* const kDirections[4] = {"UP", "DOWN", "LEFT", "RIGHT"};
    const char* at = key;
    bool afterArrow = false;
    while (*at) {
        while (*at == ' ') ++at;
        const char* end = at;
        while (*end && *end != ' ') ++end;
        if (end == at) break;
        std::string word(at, end);
        if (word == "HOLD") word = "HLD"; // shorter, and still read as "hold" (the owner's, 2026-11-11)
        int arrow = -1;
        for (int i = 0; i < 4; ++i)
            if (word == kDirections[i]) arrow = i;
        // The stroke between two directions ("UP / DOWN") goes: the arrows stand side by side.
        const bool stroke = word == "/" && afterArrow;
        if (!stroke) each(word, arrow);
        if (!stroke) afterArrow = arrow >= 0;
        at = end;
    }
}
} // namespace

float keyWidth(const Fonts& fonts, const char* key) {
    float w = 0.f;
    bool first = true, lastArrow = false, lastStroke = false;
    keyWords(key, [&](const std::string& word, int arrow) {
        const bool stroke = word == "/";
        if (!first && !(arrow >= 0 && lastArrow)) w += stroke || lastStroke ? kStrokeGap : fonts.small.width(" ");
        lastStroke = stroke;
        w += arrow >= 0 ? kArrowW : fonts.small.width(word.c_str());
        first = false;
        lastArrow = arrow >= 0;
    });
    return w;
}

void drawKey(SDL_Renderer* r, const Fonts& fonts, float x, float textY, const char* key, SDL_Color color) {
    bool first = true, lastArrow = false, lastStroke = false;
    const int midY = static_cast<int>(textY + fonts.small.capHeight() / 2.f);
    keyWords(key, [&](const std::string& word, int arrow) {
        const bool stroke = word == "/";
        if (!first && !(arrow >= 0 && lastArrow)) x += stroke || lastStroke ? kStrokeGap : fonts.small.width(" ");
        lastStroke = stroke;
        if (arrow < 0) {
            fonts.small.draw(r, x, textY, word.c_str(), color);
            x += fonts.small.width(word.c_str());
        } else {
            // A solid triangle, nine across, pointing the way.
            const int cx = static_cast<int>(x) + 4;
            for (int d = 0; d <= 4; ++d) {
                if (arrow == 0) fill(r, cx - d, midY - 2 + d, 2 * d + 1, 1, color);      // up
                else if (arrow == 1) fill(r, cx - d, midY + 2 - d, 2 * d + 1, 1, color); // down
                else if (arrow == 2) fill(r, cx - 2 + d, midY - d, 1, 2 * d + 1, color); // left
                else fill(r, cx + 2 - d, midY - d, 1, 2 * d + 1, color);                 // right
            }
            x += kArrowW;
        }
        first = false;
        lastArrow = arrow >= 0;
    });
}

namespace {
constexpr int kDie = 13; // a die's face, across
// "dice" as a picture, the way plugins show it: a die's face, five pips.
bool isDice(const char* does) { return std::strcmp(does, "dice") == 0; }
void drawDie(SDL_Renderer* r, int x, int y, SDL_Color color) {
    fill(r, x + 1, y, kDie - 2, kDie, color);
    fill(r, x, y + 1, kDie, kDie - 2, color); // (the corners off: a rounded square)
    static const int at[5][2] = {{3, 3}, {9, 3}, {6, 6}, {3, 9}, {9, 9}};
    for (const auto& pip : at) fill(r, x + pip[0] - 1, y + pip[1] - 1, 3, 3, kPanel);
}
} // namespace

void hints(SDL_Renderer* r, const Fonts& fonts, const Hint* list, int count) {
    const int y = kScreenH - kHintsH;
    fill(r, 0, y, kScreenW, kHintsH, kPanel);
    fill(r, 0, y, kScreenW, 1, kLine);
    const float textY = y + (kHintsH - fonts.small.capHeight()) / 2.f + 1.f;
    float x = 8.f;
    // Nothing is ever drawn past the right-hand end: a
    // hint that does not fit is left out, and so are the ones after it. The
    // lists are written most useful first.
    const float limit = kScreenW - 6.f;
    for (int i = 0; i < count; ++i) {
        if (!list[i].key || !list[i].does) continue;
        const float keyW = keyWidth(fonts, list[i].key) + 8.f;
        const bool die = isDice(list[i].does);
        const float doesW = die ? static_cast<float>(kDie) : doesWidth(fonts, list[i].does);
        if (x + keyW + 5.f + doesW > limit) break;
        fill(r, static_cast<int>(x), y + 5, static_cast<int>(keyW), kHintsH - 9, kText);
        drawKey(r, fonts, x + 4.f, textY, list[i].key, kBg);
        x += keyW + 5.f;
        if (die) drawDie(r, static_cast<int>(x), y + (kHintsH - kDie) / 2 + 1, kDim);
        else drawDoes(r, fonts, x, textY, list[i].does, kDim);
        x += doesW + 11.f;
    }
    // In what room is left at the end: how to see the help card.
    const char* help = gHelpHint;
    if (x + 6.f + fonts.small.width(help) <= limit) fonts.small.drawRight(r, limit, textY, help, faded(kDim, 170));
}

void valueBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value,
              float part, bool cursor, bool held, bool centre) {
    box(r, x, y, w, h, kPanel, kLine);
    const SDL_Color accent = held ? kBrass : kTeal;
    fonts.small.draw(r, x + 7.f, y + 7.f, name, kDim);
    fonts.large.draw(r, x + 7.f, y + 7.f + fonts.small.capHeight() + 8.f, value, cursor ? accent : kText);
    if (part >= 0.f) {
        const int bx = x + 7, bw = w - 14, by = y + h - 10;
        fill(r, bx, by, bw, 4, kLine);
        if (part > 1.f) part = 1.f;
        if (centre) {
            const int mid = bx + bw / 2, to = bx + static_cast<int>(std::round(bw * part));
            fill(r, to < mid ? to : mid, by, to < mid ? mid - to : to - mid, 4, cursor ? accent : kDim);
            fill(r, mid, by - 2, 1, 8, kDim);
        } else {
            fill(r, bx, by, static_cast<int>(std::round(bw * part)), 4, cursor ? accent : kDim);
        }
    }
    if (cursor) outline(r, x - 1, y - 1, w + 2, h + 2, accent, 2);
}

void scope(SDL_Renderer* r, const Engine& engine, int x, int y, int w, int h) {
    box(r, x, y, w, h, kPanel, kLine);
    fill(r, x + 1, y + h / 2, w - 2, 1, kLine);
    const int count = 512;
    float samples[512];
    engine.readScope(samples, count);
    SDL_SetRenderDrawColor(r, kTeal.r, kTeal.g, kTeal.b, 255);
    const float half = (h - 8) / 2.f;
    int lastX = x + 1, lastY = y + h / 2;
    for (int i = 0; i < count; ++i) {
        float s = samples[i] * 1.5f;
        s = s < -1.f ? -1.f : (s > 1.f ? 1.f : s);
        const int px = x + 1 + (w - 3) * i / (count - 1);
        const int py = y + h / 2 - static_cast<int>(std::round(s * half));
        if (i > 0) SDL_RenderDrawLine(r, lastX, lastY, px, py);
        lastX = px;
        lastY = py;
    }
}

void trackName(const Project& project, int track, char* out, int size) {
    const TrackKind kind = project.tracks[track].kind;
    if (project.tracks[track].name[0]) {
        std::snprintf(out, static_cast<std::size_t>(size), "%d %s", track + 1, project.tracks[track].name);
        return;
    }
    std::snprintf(out, static_cast<std::size_t>(size), "%d %s", track + 1, instrumentName(kind));
}

void noteName(int pitch, char* out, int size) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    std::snprintf(out, static_cast<std::size_t>(size), "%s%d", names[((pitch % 12) + 12) % 12], pitch / 12 - 1);
}

} // namespace ui
} // namespace apotheca
