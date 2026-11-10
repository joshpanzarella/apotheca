#include "ui.hpp"

#include <cmath>
#include <cstdio>

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

TopBar topBar(SDL_Renderer* r, const Fonts& fonts, View view, const char* context, const Project& project) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, 0, 0, kScreenW, kScreenH, kBg);
    fill(r, 0, 0, kScreenW, kTopBarH, kPanel);
    fill(r, 0, kTopBarH, kScreenW, 1, kLine);
    const float textY = (kTopBarH - fonts.small.capHeight()) / 2.f;

    // L2 < SONG PATTERN DEVICE > R2
    float x = 6.f;
    fonts.small.draw(r, x, textY, "L2", kDim);
    x += fonts.small.width("L2") + 6.f;
    const char* tabs[] = {"SONG", "PATTERN", "DEVICE"};
    for (int i = 0; i < 3; ++i) {
        const float w = fonts.small.width(tabs[i]) + 14.f;
        const bool here = static_cast<int>(view) == i;
        if (here) fill(r, static_cast<int>(x), 2, static_cast<int>(w), kTopBarH - 4, kTeal);
        fonts.small.draw(r, x + 7.f, textY, tabs[i], here ? kBg : kDim);
        x += w + 2.f;
    }
    x += 4.f;
    fonts.small.draw(r, x, textY, "R2", kDim);
    x += fonts.small.width("R2") + 12.f;
    fonts.small.draw(r, x, textY, context, kText);

    // Tempo, then a triangle while playing or a square when stopped.
    const int markX = kScreenW - 20;
    SDL_SetRenderDrawColor(r, kPurple.r, kPurple.g, kPurple.b, 255);
    if (project.playing) {
        for (int i = 0; i <= 12; ++i) SDL_RenderDrawLine(r, markX, 7 + i, markX + (i <= 6 ? i : 12 - i) * 2, 7 + i);
    } else {
        fill(r, markX, 8, 10, 10, kDim);
    }
    char text[16];
    std::snprintf(text, sizeof text, "%d", static_cast<int>(project.tempo));
    TopBar bar;
    bar.tempoRight = markX - 14.f;
    bar.tempoNumberRight = bar.tempoRight - fonts.small.width("BPM") - 5.f;
    fonts.small.drawRight(r, bar.tempoRight, textY, "BPM", kDim);
    fonts.small.drawRight(r, bar.tempoNumberRight, textY, text, kText);
    return bar;
}

void hints(SDL_Renderer* r, const Fonts& fonts, const Hint* list, int count) {
    const int y = kScreenH - kHintsH;
    fill(r, 0, y, kScreenW, kHintsH, kPanel);
    fill(r, 0, y, kScreenW, 1, kLine);
    const float textY = y + (kHintsH - fonts.small.capHeight()) / 2.f + 1.f;
    float x = 8.f;
    for (int i = 0; i < count; ++i) {
        if (!list[i].key || !list[i].does) continue;
        const float keyW = fonts.small.width(list[i].key) + 8.f;
        fill(r, static_cast<int>(x), y + 5, static_cast<int>(keyW), kHintsH - 9, kText);
        fonts.small.draw(r, x + 4.f, textY, list[i].key, kBg);
        x += keyW + 5.f;
        fonts.small.draw(r, x, textY, list[i].does, kDim);
        x += fonts.small.width(list[i].does) + 11.f;
    }
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
    std::snprintf(out, static_cast<std::size_t>(size), "%d %s", track + 1,
                  kind == TrackKind::Drums ? "DRUMS" : kind == TrackKind::Phazed ? "PHAZED" : "");
}

void noteName(int pitch, char* out, int size) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    std::snprintf(out, static_cast<std::size_t>(size), "%s%d", names[((pitch % 12) + 12) % 12], pitch / 12 - 1);
}

} // namespace ui
} // namespace apotheca
