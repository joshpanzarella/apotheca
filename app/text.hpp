#pragma once
// Text in the panel lettering (Routed Gothic Narrow), drawn from a texture
// that holds every printable ASCII letter at one size.

#include <SDL.h>

#include <vector>

namespace apotheca {

class Font {
public:
    ~Font();

    // `size` is the mockups' nominal size (12, 14, 16...). Returns false if
    // the font data cannot be read.
    bool load(SDL_Renderer* renderer, const std::vector<unsigned char>& ttf, float size);

    // `y` is the top of the capital letters.
    void draw(SDL_Renderer* renderer, float x, float y, const char* text, SDL_Color color) const;
    void drawCentered(SDL_Renderer* renderer, float centerX, float y, const char* text, SDL_Color color) const;
    void drawRight(SDL_Renderer* renderer, float rightX, float y, const char* text, SDL_Color color) const;
    float width(const char* text) const;
    float capHeight() const { return capHeight_; }

private:
    struct Glyph {
        SDL_Rect source;
        float offsetX, offsetY, advance;
    };
    SDL_Texture* texture_ = nullptr;
    Glyph glyphs_[95] = {};
    float capHeight_ = 0.f;
    float spacing_ = 0.f;
};

// Reads the font file that ships beside the program. Empty if not found.
std::vector<unsigned char> loadFontFile();

} // namespace apotheca
