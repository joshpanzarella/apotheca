#pragma once
// Text in the panel lettering (Routed Gothic Narrow), drawn from a texture
// that holds every printable ASCII letter at one size.

#include <SDL.h>

#include <string>
#include <vector>

namespace apotheca {

class Font;
// What a frame drew, written down as it is drawn: every text (where its capitals are, and how
// wide it is) and every filled or outlined rectangle. Nothing is kept unless `gDrawLog` points
// at one: it is for the tests and for a still's "--texts" file, which check that at every text
// size each text stays in its box and clear of its neighbours.
struct DrawLog {
    // `order` counts everything drawn, so it can be told what was painted over what. A box is
    // `solid` when it was filled with a colour nothing shows through (not an outline, not a shade).
    struct Text { int order; const Font* font; float x, y, w, h; std::string words; };
    struct Box { int order; bool solid; int x, y, w, h; };
    std::vector<Text> texts;
    std::vector<Box> boxes;
    int drawn = 0;
    void clear() { texts.clear(), boxes.clear(), drawn = 0; }
};
inline DrawLog* gDrawLog = nullptr;

class Font {
public:
    ~Font();

    // `size` is the mockups' nominal size (12, 14, 16...). Returns false if
    // the font data cannot be read. Loading again (another size) lets the old letters go.
    bool load(SDL_Renderer* renderer, const std::vector<unsigned char>& ttf, float size);
    // Lets its letters go: before the renderer they were made with is taken away (a renderer
    // takes its textures with it, and letting one go after that is letting it go twice).
    void unload();
    // How many fonts hold letters just now (a texture each), in the whole program: a size
    // loaded over another must not leave the old one behind. For the tests.
    static int sheets();

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
