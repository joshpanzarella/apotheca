#pragma once
// A screen to draw on in a test: 640x480 in memory, no window, with the
// program's own lettering. For checks of what is DRAWN (a word the bar writes
// shorter), which a check of the text alone cannot see.

#include <SDL.h>

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
        ok = fonts->small.load(r, ttf, 14.5f) && fonts->medium.load(r, ttf, 17.f) && fonts->large.load(r, ttf, 20.f); // as main.cpp
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

} // namespace apotheca
