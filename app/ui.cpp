#include "ui.hpp"

#include "fx_picture.hpp"
#include "icons.hpp"
#include "instruments.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace apotheca {
static_assert(Session::kThemeCount == kThemes, "the SET page's THEME steps through every theme");
static_assert(Session::kThemeAsBuilt == kThemeBrown, "nothing kept: the look as it was built");
namespace ui {

void fill(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    if (gDrawLog) gDrawLog->boxes.push_back({gDrawLog->drawn++, c.a == 255, x, y, w, h});
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

void outline(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c, int thickness) {
    if (gDrawLog) gDrawLog->boxes.push_back({gDrawLog->drawn++, false, x, y, w, h});
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

void macroBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value,
              float part, bool cursor, bool held, float mark) {
    const Icon* picture = iconFor(name);
    const SDL_Color family = !picture ? kDim : picture->family == IconFamily::Sound ? kTeal : picture->family == IconFamily::Movement ? kPurple : kText;
    const bool turned = std::fabs(part - 0.5f) > 0.005f; // away from "as the preset has it"
    const SDL_Color accent = held ? kBrass : kTeal;
    const bool tall = h >= kMacroDialTallH;
    const MacroDial dial = macroDialIn(x, y, w, h);
    box(r, x, y, w, h, turned ? kPanel2 : kPanel, kLine);
    // (A name or a value too wide for the box at XL is held at LARGE: Fonts::within. The picture has the name's last 27 pixels.)
    const float textX = tall ? x + 7.f : static_cast<float>(dial.cx + dial.radius + 12);
    const float room = x + w - 7.f - textX;
    // (A short box keeps its picture on the value's row, where a value is short: the name has the row to itself.)
    fonts.within(&Fonts::small, name, picture && tall ? room - 27.f : room).draw(r, textX, y + 7.f, name, turned || cursor ? kText : kDim);
    const SDL_Color valueColour = cursor ? accent : (turned ? kText : kDim);
    if (tall) {
        const Font& font = fonts.within(&Fonts::large, value, w - 14.f);
        font.drawCentered(r, static_cast<float>(dial.cx), static_cast<float>(y + h - 12) - font.capHeight(), value, valueColour);
    } else {
        const Font& font = fonts.fit(value, picture ? room - 29.f : room);
        font.draw(r, textX, static_cast<float>(y + h - 9) - font.capHeight(), value, valueColour);
    }
    if (picture) drawIcon(r, *picture, x + w - 27, tall ? y + 7 : y + h - 21, turned || cursor);
    // The dial, lit from the middle in the macro's own colour (the cursor's when it has the cursor).
    drawDial(r, dial.cx, dial.cy, dial.radius, dial.ring, part, 0, 0, cursor ? accent : (picture ? family : kText), mark, true);
    if (cursor) outline(r, x - 1, y - 1, w + 2, h + 2, accent, 2);
}

const SDL_Color& tabAccent(View view) {
    switch (view) {
    case View::Song: return kPurple;
    case View::Pattern: return kTeal;
    case View::Device: return kBrass;
    case View::Effects: return kTeal;
    case View::Mods: return kPurple;
    case View::Settings: return kText;
    }
    return kTeal;
}

void tempoCursor(SDL_Renderer* r, const Fonts& chosen, const TopBar& top, float exact, float tempo, SDL_Color cursor) {
    // The tight rows are always at LARGE, whatever TEXT SIZE is: the tempo's box in the top bar is one.
    const Fonts& tight = chosen.fixed();
    const int bx = static_cast<int>(top.tempoNumberRight - tight.small.width("240") - 6.f);
    const int bw = static_cast<int>(top.tempoRight) - bx + 6;
    const float part = (exact - kMinTempo) / (kMaxTempo - kMinTempo);
    fill(r, bx, 3, bw, kTopBarH - 6, kPanel2);
    fill(r, bx, 3, static_cast<int>(std::round(bw * part)), kTopBarH - 6, faded(cursor, 90));
    outline(r, bx - 1, 2, bw + 2, kTopBarH - 4, cursor, 2);
    const float textY = (kTopBarH - tight.small.capHeight()) / 2.f;
    char text[16];
    std::snprintf(text, sizeof text, "%d", static_cast<int>(tempo));
    tight.small.drawRight(r, top.tempoRight, textY, "BPM", kText);
    tight.small.drawRight(r, top.tempoNumberRight, textY, text, kText);
}

TopBar topBar(SDL_Renderer* r, const Fonts& chosen, View view, const char* context, const Project& project) {
    const Fonts& fonts = chosen.fixed(); // a tight row: laid out to the pixel, always at LARGE
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, 0, 0, kScreenW, kScreenH, kBg);
    fill(r, 0, 0, kScreenW, kTopBarH, kPanel);
    fill(r, 0, kTopBarH, kScreenW, 1, tabAccent(view)); // the bar's foot: the screen's accent (it was kLine)
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
    const char* tabs[] = {"SONG", "PTRN", "DVICE", "FX", "MOD", "SET"}; // View's order (PTRN and DVICE: owner, 2026-11-18, to give the tabs their room back)
    for (int i = 0; i < 6; ++i) {
        // (5 pixels either side of a tab's word again: it was 3 while the tabs read PATTERN and DEVICE. The four
        // letters saved pay for it and leave the screen's own words a little more room than they had.)
        const float w = fonts.small.width(tabs[i]) + 2.f * kTabPad;
        const bool here = static_cast<int>(view) == i;
        if (here) fill(r, static_cast<int>(x), 2, static_cast<int>(w), kTopBarH - 4, tabAccent(view)); // (every tab was teal)
        fonts.small.draw(r, x + kTabPad, textY, tabs[i], here ? kBg : kDim);
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
    } else if (project.playing && project.playScene >= 0 && project.playScene < kMaxScenes) {
        const int right = static_cast<int>(room) - 6, w = 46;
        const int plays = project.repeats[project.playScene]; // a looped scene's too: where its FILL comes
        const int bars = project.sceneBars(project.playScene), meter = project.meterOf(project.playScene);
        sceneProgress(r, right - w, 9, w, 8, plays, project.roundPlaying(), project.playBar * meter + project.playStep, bars * meter);
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
    bar.wordsX = x;
    bar.wordsRoom = room - x;
    bar.wordsWidth = fonts.small.width(shown);
    return bar;
}

void drawPlayhead(SDL_Renderer* r, const Playhead& head, Uint32 nowMs, int x, int y, int w, int h) {
    const float at = head.at(nowMs);
    if (at >= 0.f) fill(r, x + static_cast<int>(at * static_cast<float>(w - 2)), y, 2, h, kBrass);
}

void wavePicture(SDL_Renderer* r, const std::vector<float>& peaks, int x, int y, int w, int h, float start, float end, const std::vector<float>* lit) {
    const int count = static_cast<int>(peaks.size());
    for (int i = 0; i < count; ++i) {
        const float at = static_cast<float>(i) / count;
        const bool played = at >= start && at < end;
        const float height = played && lit ? (*lit)[static_cast<std::size_t>(i)] : peaks[static_cast<std::size_t>(i)];
        const int ph = 1 + static_cast<int>(height * (h - 1));
        fill(r, x + w * i / count, y + (h - ph) / 2, w / count, ph, played ? kText : kLine); // what is played, lit
    }
    fill(r, x + static_cast<int>(start * w), y - 2, 2, h + 4, kPurple); // where it begins
    fill(r, x + waveEndMarkX(end, w), y - 2, 2, h + 4, kPurple);        // and where it ends
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
    // SELECT is drawn as SEL (the owner's, 2026-11-12), with whatever follows it ("SELECT," on the help card).
    const auto shorter = [](std::string word) {
        if (word.compare(0, 6, "SELECT") == 0 && (word.size() == 6 || !std::isalpha(static_cast<unsigned char>(word[6])))) word.erase(3, 3);
        return word;
    };
    const auto isDirectionWord = [&](const std::string& word) {
        for (const char* d : kDirections)
            if (word == d) return true;
        return false;
    };
    while (*at) {
        while (*at == ' ') ++at;
        const char* end = at;
        while (*end && *end != ' ') ++end;
        if (end == at) break;
        std::string word = shorter(std::string(at, end));
        // HOLD is drawn HLD (the owner's, 2026-11-11), and joined to its key by a hyphen, one word: HLD-A, HLD-R1,
        // HLD-SEL (owner, 2026-11-28: "HLD A" read as HLD and A, two keys). A direction after it stays an arrow.
        if (word == "HOLD") {
            word = "HLD";
            const char* next = end;
            while (*next == ' ') ++next;
            const char* nextEnd = next;
            while (*nextEnd && *nextEnd != ' ') ++nextEnd;
            const std::string key = shorter(std::string(next, nextEnd));
            if (!key.empty() && key != "/" && key != "+" && !isDirectionWord(key)) word += "-" + key, end = nextEnd;
        }
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

std::string cutToRoom(std::string line, const Font& font, float room) {
    if (font.width(line.c_str()) <= room) return line;
    const std::size_t open = line.find('"'), space = line.rfind(' ');
    if (line.size() > 2 && line.back() == '"' && open != std::string::npos && space != std::string::npos && space > open + 1) {
        std::string head = line.substr(0, space);
        const std::string tail = ".." + line.substr(space);
        while (head.size() > open + 1 && font.width((head + tail).c_str()) > room) head.pop_back();
        while (head.size() > open + 1 && head.back() == ' ') head.pop_back(); // (no space before the dots)
        return head + tail;
    }
    while (line.size() > 1 && font.width(line.c_str()) > room) line.pop_back();
    return line;
}

std::string keyShown(const char* key) {
    static const char* const kNames[4] = {"UP", "DOWN", "LEFT", "RIGHT"};
    std::string shown;
    keyWords(key, [&](const std::string& word, int arrow) {
        if (!shown.empty()) shown += ' ';
        shown += arrow >= 0 ? kNames[arrow] : word;
    });
    return shown;
}

namespace {
// A hold, HLD-A: HLD and its key with the hyphen in a space's room, so a hold's chip is exactly as wide as it was
// drawn "HLD A" (2026-11-28: drawn at its own width the hyphen put bars that had fitted to the pixel 1 to 2 over).
// The hyphen's own sides overlap HLD's and the key's a little; its ink stands clear of both.
bool isHold(const std::string& word) { return word.compare(0, 4, "HLD-") == 0 && word.size() > 4; }
float wordWidth(const Fonts& fonts, const std::string& word) {
    if (!isHold(word)) return fonts.small.width(word.c_str());
    return fonts.small.width("HLD") + fonts.small.width(" ") + fonts.small.width(word.c_str() + 4);
}
void drawWord(SDL_Renderer* r, const Fonts& fonts, float x, float textY, const std::string& word, SDL_Color color) {
    if (!isHold(word)) return fonts.small.draw(r, x, textY, word.c_str(), color);
    const float hld = fonts.small.width("HLD"), room = fonts.small.width(" ");
    fonts.small.draw(r, x, textY, "HLD", color);
    fonts.small.draw(r, x + hld + (room - fonts.small.width("-")) / 2.f, textY, "-", color);
    fonts.small.draw(r, x + hld + room, textY, word.c_str() + 4, color);
}
} // namespace

float keyWidth(const Fonts& fonts, const char* key) {
    float w = 0.f;
    bool first = true, lastArrow = false, lastStroke = false;
    keyWords(key, [&](const std::string& word, int arrow) {
        const bool stroke = word == "/";
        if (!first && !(arrow >= 0 && lastArrow)) w += stroke || lastStroke ? kStrokeGap : fonts.small.width(" ");
        lastStroke = stroke;
        w += arrow >= 0 ? kArrowW : wordWidth(fonts, word);
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
            drawWord(r, fonts, x, textY, word, color);
            x += wordWidth(fonts, word);
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

namespace {
constexpr float kHintsLimit = kScreenW - 6.f; // nothing in the bar is drawn past this
// The bar itself, empty. Gives where its lettering goes.
float hintsBar(SDL_Renderer* r, const Fonts& fonts) {
    const int y = kScreenH - kHintsH;
    fill(r, 0, y, kScreenW, kHintsH, kPanel);
    fill(r, 0, y, kScreenW, 1, kLine);
    return y + (kHintsH - fonts.small.capHeight()) / 2.f + 1.f;
}
// A button's name in its cream box (a chip), starting at x. Gives how wide the box is.
float hintChipWidth(const Fonts& fonts, const char* key) { return keyWidth(fonts, key) + 8.f; }
void hintChip(SDL_Renderer* r, const Fonts& fonts, float x, float textY, const char* key) {
    fill(r, static_cast<int>(x), kScreenH - kHintsH + 5, static_cast<int>(hintChipWidth(fonts, key)), kHintsH - 9, kText);
    drawKey(r, fonts, x + 4.f, textY, key, kBg);
}
void hintDie(SDL_Renderer* r, float x) { drawDie(r, static_cast<int>(x), kScreenH - kHintsH + (kHintsH - kDie) / 2 + 1, kDim); }
// In what room is left at the end, after what ends at x: how to see the help card.
void hintHelp(SDL_Renderer* r, const Fonts& fonts, float x, float textY) {
    if (gUpdateReady) { // always there: the hints left it its room
        fonts.small.drawRight(r, kHintsLimit, textY, kUpdateReadyHint, kBrass);
        return;
    }
    const char* help = gHelpHint;
    if (x + 6.f + fonts.small.width(help) <= kHintsLimit) fonts.small.drawRight(r, kHintsLimit, textY, help, faded(kDim, 170));
}
// Where the hints must end: the bar's end, or short of the update's words.
float hintsEnd(const Fonts& fonts) {
    return gUpdateReady ? kHintsLimit - 6.f - fonts.small.width(kUpdateReadyHint) : kHintsLimit;
}
} // namespace

void hints(SDL_Renderer* r, const Fonts& chosen, const Hint* list, int count) {
    const Fonts& fonts = chosen.fixed(); // a tight row: what fits was worked out at LARGE
    const float textY = hintsBar(r, fonts);
    float x = kHintsLeft;
    // Nothing is ever drawn past the right-hand end: a
    // hint that does not fit is left out, and so are the ones after it. The
    // lists are written most useful first.
    float whole = kHintsLeft; // where the bar would end with all of it in, the help too
    bool cut = false;
    for (int i = 0; i < count; ++i) {
        if (!list[i].key || !list[i].does) continue;
        const float keyW = hintChipWidth(fonts, list[i].key);
        const bool die = isDice(list[i].does);
        const float doesW = die ? static_cast<float>(kDie) : doesWidth(fonts, list[i].does);
        whole += keyW + 5.f + doesW + 11.f;
        if (cut || x + keyW + 5.f + doesW > hintsEnd(fonts)) {
            cut = true;
            continue;
        }
        hintChip(r, fonts, x, textY, list[i].key);
        x += keyW + 5.f;
        if (die) hintDie(r, x);
        else drawDoes(r, fonts, x, textY, list[i].does, kDim);
        x += doesW + 11.f;
    }
    hintHelp(r, fonts, x, textY);
    const float over = whole + 6.f + fonts.small.width(gHelpHint) - kHintsLimit;
    gHintsOver = over > 0.f ? over : 0.f;
}

float hintDiceEndX(const Fonts& chosen, const char* dice) {
    const Fonts& fonts = chosen.fixed(); // the bar's own lettering, always LARGE
    return std::floor(hintsEnd(fonts) - kDie - 5.f - hintChipWidth(fonts, dice)); // (short of RESTART TO UPDATE when it is there)
}

void hintsUnder(SDL_Renderer* r, const Fonts& chosen, const char* dice, const HintUnder* list, int count, bool diceAtEnd) {
    const Fonts& fonts = chosen.fixed(); // a tight row: each chip stands under a field of a strip that is itself at LARGE
    const float textY = hintsBar(r, fonts);
    float end = kHintsLeft - kHintUnderGap; // where the last thing drawn ends
    const float limit = dice && diceAtEnd ? hintDiceEndX(fonts, dice) - kHintUnderGap : hintsEnd(fonts);
    if (dice && diceAtEnd) {
        const float x = hintDiceEndX(fonts, dice);
        hintChip(r, fonts, x, textY, dice);
        hintDie(r, x + hintChipWidth(fonts, dice) + 5.f);
    } else if (dice) {
        const float keyW = hintChipWidth(fonts, dice);
        hintChip(r, fonts, kHintsLeft, textY, dice);
        hintDie(r, kHintsLeft + keyW + 5.f);
        end = kHintsLeft + keyW + 5.f + kDie;
    }
    for (int i = 0; i < count; ++i) {
        const float keyW = hintChipWidth(fonts, list[i].key);
        // One that would touch the one before it, or pass the right-hand end, is left out.
        if (list[i].x < end + kHintUnderGap || list[i].x + keyW > limit) continue;
        hintChip(r, fonts, list[i].x, textY, list[i].key);
        end = list[i].x + keyW;
    }
    if (!(dice && diceAtEnd) || gUpdateReady) hintHelp(r, fonts, end + 5.f, textY); // (eleven clear of the last chip)
}

void valueBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value,
              float part, bool cursor, bool held, bool centre, const Icon* icon) {
    box(r, x, y, w, h, kPanel, kLine);
    const SDL_Color accent = held ? kBrass : kTeal;
    // (With an icon, the name has the top row less the icon's 27 pixels at its right.)
    fonts.within(&Fonts::small, name, icon ? w - 14.f - 27.f : w - 14.f).draw(r, x + 7.f, y + 7.f, name, kDim);
    if (icon) drawIcon(r, *icon, x + w - 27, y + 7, cursor);
    fonts.within(&Fonts::large, value, w - 14.f).draw(r, x + 7.f, y + 7.f + fonts.small.capHeight() + 8.f + boxValueDrop(h), value, cursor ? accent : kText);
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

void displayBox(SDL_Renderer* r, const Fonts& fonts, int x, int y, int w, int h, const char* name, const char* value, bool cursor, bool held) {
    box(r, x, y, w, h, kBg, kLine);
    for (int yy = y + 3; yy < y + h - 2; yy += 3) fill(r, x + 2, yy, w - 4, 1, kPanel); // the scan lines
    const SDL_Color accent = held ? kBrass : kTeal;
    fonts.within(&Fonts::small, name, w - 14.f).draw(r, x + 7.f, y + 7.f, name, kDim);
    fonts.within(&Fonts::large, value, w - 14.f).draw(r, x + 7.f, y + 7.f + fonts.small.capHeight() + 8.f + boxValueDrop(h), value, cursor ? accent : kText);
    if (cursor) outline(r, x - 1, y - 1, w + 2, h + 2, accent, 2);
}

void scopeStrip(SDL_Renderer* r, const Fonts& fonts, const Engine& engine) {
    const int y = kScopeY, h = kScopeH;
    box(r, kScopeX, y, kScopeW, h, kPanel, kLine);
    const char* label = "SIGNAL";
    const Font& font = fonts.within(&Fonts::small, label, kScopeLabelW - 14.f);
    font.draw(r, kScopeX + 7.f, y + (h - font.capHeight()) / 2.f, label, kDim);
    // The line, in the rest of the strip.
    const int x = kScopeX + kScopeLabelW, w = kScopeW - kScopeLabelW;
    fill(r, x - 1, y + 1, 1, h - 2, kLine); // the label's room ends
    fill(r, x + 1, y + h / 2, w - 2, 1, kLine);
    const int count = kScopeSamples;
    float samples[kScopeSamples];
    engine.readScope(samples, count);
    SDL_SetRenderDrawColor(r, kTeal.r, kTeal.g, kTeal.b, 255);
    int lastX = x + 1, lastY = kScopeMiddle;
    for (int i = 0; i < count; ++i) {
        const int px = x + 1 + (w - 3) * i / (count - 1);
        const int py = scopeLineY(samples[i]);
        if (i > 0) SDL_RenderDrawLine(r, lastX, lastY, px, py);
        lastX = px;
        lastY = py;
    }
}

void scopeBox(SDL_Renderer* r, const Fonts& fonts, const Engine& engine, int x, int y, int w, int h) {
    box(r, x, y, w, h, kBg, kLine);
    // The graticule: eight columns, four rows, the middle line stronger.
    for (int k = 1; k < 8; ++k) fill(r, x + w * k / 8, y + 2, 1, h - 4, kPanel);
    for (int k = 1; k < 4; ++k) fill(r, x + 2, y + h * k / 4, w - 4, 1, k == 2 ? kLine : kPanel);
    const char* label = "SIGNAL";
    fonts.fixed().small.draw(r, x + 8.f, y + 7.f, label, kDim);
    float samples[kScopeSamples];
    engine.readScope(samples, kScopeSamples);
    SDL_SetRenderDrawColor(r, kTeal.r, kTeal.g, kTeal.b, 255);
    int lastX = x + 2, lastY = y + h / 2;
    for (int i = 0; i < kScopeSamples; ++i) {
        const int px = x + 2 + (w - 5) * i / (kScopeSamples - 1);
        const int py = scopeBoxLineY(samples[i], y, h);
        if (i > 0) {
            SDL_RenderDrawLine(r, lastX, lastY, px, py);
            SDL_RenderDrawLine(r, lastX, lastY + 1, px, py + 1); // two pixels: a screen this size wants a heavier line
        }
        lastX = px;
        lastY = py;
    }
}

int scopeBoxLineY(float sample, int y, int h) {
    const float half = (h - 2 * kScopeBoxInset) / 2.f;
    return y + h / 2 - static_cast<int>(std::round(scopeBent(sample) * half));
}

// The line's height is the sample's size bent (to the power kScopeBend), not to scale: to scale, the
// strip's 14 pixels a side showed a quiet recording as half a pixel (owner, 2026-11-14). Bent, a sound
// 30 dB down stands 3 pixels and one 20 dB down 5, a louder sound is still taller, full scale is still
// the edge and silence is still the middle line.
float scopeBent(float sample) {
    const float s = sample < -1.f ? -1.f : (sample > 1.f ? 1.f : sample);
    const float size = std::fabs(s);
    if (!(size > 0.f)) return 0.f; // (not a number: the middle too)
    const float bent = std::pow(size, kScopeBend);
    return s < 0.f ? -bent : bent;
}

int scopeLineY(float sample) { return kScopeMiddle - static_cast<int>(std::round(scopeBent(sample) * kScopeHalf)); }

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
