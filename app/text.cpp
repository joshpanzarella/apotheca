#include "text.hpp"

#include <cmath>
#include <string>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace apotheca {

namespace {
constexpr int kAtlasW = 512;
constexpr int kAtlasH = 256;
// The mockups set this face a little larger than its nominal size and
// letterspaced, to match the engraved look of the panels. Same numbers here.
constexpr float kSizeScale = 1.125f;
constexpr float kLetterSpacing = 0.06f; // of the nominal size
constexpr float kCapHeight = 736.f / 1024.f; // of the em, from the font's own tables
} // namespace

Font::~Font() {
    if (texture_) SDL_DestroyTexture(texture_);
}

bool Font::load(SDL_Renderer* renderer, const std::vector<unsigned char>& ttf, float size) {
    if (ttf.empty()) return false;
    const float em = size * kSizeScale;

    std::vector<unsigned char> alpha(kAtlasW * kAtlasH, 0);
    stbtt_packedchar packed[95];
    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, alpha.data(), kAtlasW, kAtlasH, 0, 1, nullptr)) return false;
    const int ok = stbtt_PackFontRange(&context, ttf.data(), 0, STBTT_POINT_SIZE(em), 32, 95, packed);
    stbtt_PackEnd(&context);
    if (!ok) return false;

    // White letters with the coverage in the alpha channel; the colour is
    // applied when drawing.
    std::vector<unsigned char> rgba(kAtlasW * kAtlasH * 4);
    for (int i = 0; i < kAtlasW * kAtlasH; ++i) {
        rgba[4 * i] = rgba[4 * i + 1] = rgba[4 * i + 2] = 255;
        rgba[4 * i + 3] = alpha[i];
    }
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kAtlasW, kAtlasH);
    if (!texture_) return false;
    SDL_UpdateTexture(texture_, nullptr, rgba.data(), kAtlasW * 4);
    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < 95; ++i) {
        const stbtt_packedchar& p = packed[i];
        glyphs_[i].source = {p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0};
        glyphs_[i].offsetX = p.xoff;
        glyphs_[i].offsetY = p.yoff; // from the baseline, negative is up
        glyphs_[i].advance = p.xadvance;
        if (i == '1' - 32) glyphs_[i].offsetX += 1.f, glyphs_[i].advance += 1.f; // room for its flag (see draw)
    }
    capHeight_ = std::round(em * kCapHeight);
    spacing_ = size * kLetterSpacing;
    return true;
}

float Font::width(const char* text) const {
    float w = 0.f;
    for (const char* c = text; *c; ++c) {
        if (*c < 32 || *c > 126) continue;
        w += glyphs_[*c - 32].advance + spacing_;
    }
    return w > 0.f ? w - spacing_ : 0.f;
}

void Font::draw(SDL_Renderer* renderer, float x, float y, const char* text, SDL_Color color) const {
    if (!texture_) return;
    SDL_SetTextureColorMod(texture_, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture_, color.a);
    const float baseline = y + capHeight_;
    for (const char* c = text; *c; ++c) {
        if (*c < 32 || *c > 126) continue;
        const Glyph& g = glyphs_[*c - 32];
        SDL_Rect target = {static_cast<int>(std::round(x + g.offsetX)), static_cast<int>(std::round(baseline + g.offsetY)),
                           g.source.w, g.source.h};
        SDL_RenderCopy(renderer, texture_, &g.source, &target);
        if (*c == '1') {
            // This face's 1 is a bare stroke, the same as its capital I and
            // small l. Give it the flag a 1 has: a short stroke falling away
            // to the left from the top of the stem. (The glyph was given one
            // pixel more room for it when the face was loaded.)
            const int n = capHeight_ >= 12.f ? 2 : 1; // small: the owner found a longer one strange
            const int stem = target.x + (target.w > 2 ? 1 : 0), top = target.y + (target.h > 4 ? 1 : 0);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            for (int k = 0; k <= n; ++k) SDL_RenderDrawLine(renderer, stem - k, top + k, stem - k, top + k + 1);
        }
        x += g.advance + spacing_;
    }
}

void Font::drawCentered(SDL_Renderer* renderer, float centerX, float y, const char* text, SDL_Color color) const {
    draw(renderer, centerX - width(text) / 2.f, y, text, color);
}

void Font::drawRight(SDL_Renderer* renderer, float rightX, float y, const char* text, SDL_Color color) const {
    draw(renderer, rightX - width(text), y, text, color);
}

std::vector<unsigned char> loadFontFile() {
    char* base = SDL_GetBasePath();
    const std::string root = base ? base : "";
    if (base) SDL_free(base);
    // Beside the program in the port; under assets/ when run from the build folder.
    const char* places[] = {"routed-gothic-narrow.ttf", "assets/fonts/routed-gothic-narrow.ttf",
                            "../assets/fonts/routed-gothic-narrow.ttf"};
    for (const char* place : places) {
        SDL_RWops* file = SDL_RWFromFile((root + place).c_str(), "rb");
        if (!file) continue;
        const Sint64 size = SDL_RWsize(file);
        std::vector<unsigned char> data(size > 0 ? static_cast<std::size_t>(size) : 0);
        const std::size_t got = data.empty() ? 0 : SDL_RWread(file, data.data(), 1, data.size());
        SDL_RWclose(file);
        if (got == data.size() && !data.empty()) return data;
    }
    return {};
}

} // namespace apotheca
