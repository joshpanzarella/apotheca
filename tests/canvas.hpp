#pragma once
// A screen to draw on in a test: 640x480 in memory, no window, with the
// program's own lettering. For checks of what is DRAWN (a word the bar writes
// shorter), which a check of the text alone cannot see.

#include <SDL.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "text.hpp"
#include "ui.hpp"

namespace apotheca {

struct Canvas {
    SDL_Surface* surface = nullptr;
    SDL_Renderer* r = nullptr;
    std::unique_ptr<Fonts> fonts{new Fonts};
    bool ok = false; // false: no lettering was found, and nothing drawn here means anything

    Canvas() {
        surface = SDL_CreateRGBSurfaceWithFormat(0, ui::kScreenW, ui::kScreenH, 32, SDL_PIXELFORMAT_ARGB8888);
        r = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        if (!r) return;
        const std::vector<unsigned char> ttf = loadFontFile();
        ok = fonts->load(r, ttf, kTextLarge); // as main.cpp: LARGE, what the screens were laid out at
        clear();
    }
    ~Canvas() {
        fonts.reset(); // the letters are textures of the renderer: they go first
        if (r) SDL_DestroyRenderer(r);
        if (surface) SDL_FreeSurface(surface);
    }
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    void clear() { ui::fill(r, 0, 0, ui::kScreenW, ui::kScreenH, ui::kBg); }
    static Uint32 colour(SDL_Color c) { return 0xff000000u | (Uint32(c.r) << 16) | (Uint32(c.g) << 8) | Uint32(c.b); }
    Uint32 at(int x, int y) const {
        return 0xff000000u | static_cast<const Uint32*>(surface->pixels)[y * (surface->pitch / 4) + x];
    }
    bool is(int x, int y, SDL_Color c) const { return at(x, y) == colour(c); }
    // The pixels of a part of the screen, row by row.
    std::vector<Uint32> part(int x, int y, int w, int h) const {
        std::vector<Uint32> out;
        for (int row = y; row < y + h; ++row)
            for (int col = x; col < x + w; ++col) out.push_back(at(col, row));
        return out;
    }
    std::vector<Uint32> all() const { return part(0, 0, ui::kScreenW, ui::kScreenH); }
    // How many pixels in a row have this colour, going right from x.
    int run(int x, int y, SDL_Color c) const {
        int n = 0;
        while (x + n < ui::kScreenW && is(x + n, y, c)) ++n;
        return n;
    }
};

// Is `text` drawn at (x, y) in `colour`, at size 0 small, 1 medium, 2 large: every pixel of the full colour the
// lettering has (the body of a letter, not its soft edge) is on the canvas too, and there are at least six. The
// text is drawn into `scratch` to see where those pixels are. (One copy since 2026-11-24: app_test and
// copyscene_test each had their own.)
inline bool inked(const Canvas& canvas, Canvas& scratch, int size, float x, float y, const char* text, SDL_Color colour) {
    const Font& font = size == 0 ? scratch.fonts->small : (size == 1 ? scratch.fonts->medium : scratch.fonts->large);
    const int x0 = std::max(0, static_cast<int>(x) - 2), x1 = std::min(ui::kScreenW, static_cast<int>(x + font.width(text)) + 3);
    const int y0 = std::max(0, static_cast<int>(y) - 4), y1 = std::min(ui::kScreenH, static_cast<int>(y) + 30);
    ui::fill(scratch.r, x0, y0, x1 - x0, y1 - y0, SDL_Color{255, 0, 255, 255}); // a ground no lettering is drawn in
    font.draw(scratch.r, x, y, text, colour);
    int body = 0;
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            if (!scratch.is(px, py, colour)) continue;
            ++body;
            if (!canvas.is(px, py, colour)) return false;
        }
    return body >= 6;
}

} // namespace apotheca
