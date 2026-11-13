#pragma once
// Does a screen still hold together at another TEXT SIZE? A frame is drawn once at LARGE (what
// every screen was laid out at) and once at the size asked about, and what each drew is written
// down (DrawLog in text.hpp: every text with where its capitals are and how wide it is, every
// rectangle). LARGE is the owner's layout, so it is the measure: at another size
//
//   - the same texts are drawn, the same words, in the same order (nothing cut short, nothing
//     left out; a name that LARGE cuts to fit may show more of itself in smaller lettering),
//     and the same number of rectangles;
//   - a text that is on the screen at LARGE is on the screen;
//   - a text that is inside a rectangle at LARGE (its box, its row, its chip, its panel) is
//     inside that rectangle: it has not left its box;
//   - two texts that are clear of each other at LARGE (not overlapping, and with a space
//     between them if they are side by side on a line) are clear of each other: it has not
//     run into its neighbour;
//   - a text that is clear of a rectangle at LARGE (the bar beside a value, the line a label
//     stands on, the next box along) has not grown into it;
//   - a text that sits in the middle, up and down, of a box snug round it at LARGE (a row, a
//     chip) still sits in the middle of it.
//
// What is drawn over something else (the help card over a grid) is another layer: `order` says
// what was painted over what, and a text is not held to the boxes and texts under its layer.
//
// The texts and rectangles of the two frames are matched by the order they were drawn in.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "text.hpp"

namespace apotheca {
namespace textfit {

struct Text { int order; float cap, x, y, w, h; std::string words; };
struct Box { int order; bool solid; float x, y, w, h; };
struct Frame {
    std::vector<Text> texts;
    std::vector<Box> boxes;
};

inline Frame frameOf(const DrawLog& log) {
    Frame f;
    for (const DrawLog::Text& t : log.texts) f.texts.push_back({t.order, t.font->capHeight(), t.x, t.y, t.w, t.h, t.words});
    for (const DrawLog::Box& b : log.boxes)
        f.boxes.push_back({b.order, b.solid, static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)});
    return f;
}

// A text's letters are drawn on whole pixels: a text "at" 7.4 with a box edge at 7 is inside it.
constexpr float kSlack = 0.5f;
inline bool inside(const Text& t, const Box& b) {
    return t.x >= b.x - kSlack && t.y >= b.y - kSlack && t.x + t.w <= b.x + b.w + kSlack && t.y + t.h <= b.y + b.h + kSlack;
}
inline bool inside(const Box& a, const Box& b) { return a.x >= b.x && a.y >= b.y && a.x + a.w <= b.x + b.w && a.y + a.h <= b.y + b.h; }
// Two texts are clear of each other when they do not overlap, and, if they are side by side on
// one line (each shares half its capitals' height or more with the other), there is at least
// a space between them ("UNSORTED" ending where "FOLDER 1 OF 3" begins is one word to the
// eye). A space is as wide as the lettering makes it: about a third of the height of the
// smaller text's capitals (kGap of it is asked for; a space itself is a little more).
constexpr float kGap = 0.3f;
inline bool clear(const Text& a, const Text& b) {
    const float cap = a.cap < b.cap ? a.cap : b.cap;
    const float shared = (a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h) - (a.y > b.y ? a.y : b.y); // how much of a line they share
    if (shared <= kSlack) return true; // one is above the other
    const float gap = shared >= cap / 2.f ? kGap * cap : 0.f;
    return a.x + a.w + gap <= b.x + kSlack || b.x + b.w + gap <= a.x + kSlack;
}
inline bool touches(const Text& t, const Box& b) { return t.x < b.x + b.w && b.x < t.x + t.w && t.y < b.y + b.h && b.y < t.y + t.h; }
inline bool onScreen(const Text& t) {
    const Box screen = {0, true, 0.f, 0.f, 640.f, 480.f};
    return inside(t, screen);
}
// Something drawn over something else (the help card over a grid, a list over a screen) is another
// layer: a text on the card has nothing to do with the boxes and texts of the grid under it.
// Is box `b` one this text sits in? It is inside it, the box was drawn first, and no solid
// rectangle that is not itself part of the box was painted across it before the text went on top.
inline bool boxOf(const Frame& f, const Text& t, std::size_t b) {
    const Box& box = f.boxes[b];
    if (!inside(t, box) || box.order > t.order) return false;
    for (const Box& over : f.boxes)
        if (over.solid && over.order > box.order && over.order < t.order && inside(t, over) && !inside(over, box)) return false;
    return true;
}
// How much of a rectangle (x, y, w, h) another covers, 0..1.
inline float covered(float x, float y, float w, float h, const Box& by) {
    const float left = x > by.x ? x : by.x, right = x + w < by.x + by.w ? x + w : by.x + by.w;
    const float top = y > by.y ? y : by.y, bottom = y + h < by.y + by.h ? y + h : by.y + by.h;
    return right <= left || bottom <= top || w <= 0.f || h <= 0.f ? 0.f : (right - left) * (bottom - top) / (w * h);
}
// "Painted over": a quarter of it or more is under a solid rectangle. (A mark that only touches it, the end of a line, is not a layer.)
constexpr float kPaintedOver = 0.25f;
// Are two texts side by side on one layer? Not if the first of them was painted over before the second was drawn.
inline bool neighbours(const Frame& f, const Text& a, const Text& b) {
    const Text& first = a.order < b.order ? a : b;
    const Text& second = a.order < b.order ? b : a;
    for (const Box& over : f.boxes)
        if (over.solid && over.order > first.order && over.order < second.order && covered(first.x, first.y, first.w, first.h, over) >= kPaintedOver) return false;
    return true;
}
// Is a rectangle beside a text on its layer? Not if whichever came first was painted over before the other was drawn.
inline bool sameLayer(const Frame& f, const Text& t, std::size_t b) {
    const Box& box = f.boxes[b];
    const int first = t.order < box.order ? t.order : box.order, second = t.order < box.order ? box.order : t.order;
    for (const Box& over : f.boxes) {
        if (!over.solid || over.order <= first || over.order >= second) continue;
        if ((t.order < box.order ? covered(t.x, t.y, t.w, t.h, over) : covered(box.x, box.y, box.w, box.h, over)) >= kPaintedOver) return false;
    }
    return true;
}
// A rectangle is not "beside" a text when one is laid over the other on purpose: a panel painted
// after the text (a list opened over a screen: big), or the text's own solid ground
// painted after the rectangle (a name's chip that reaches over the next cell).
constexpr float kPanelArea = 20000.f;
inline bool laidOver(const Frame& f, const Text& t, std::size_t b) {
    const Box& box = f.boxes[b];
    if (box.order > t.order) return box.w * box.h >= kPanelArea; // (filled, or the edge drawn round one that was)
    for (const Box& ground : f.boxes)
        if (ground.solid && ground.order > box.order && ground.order < t.order && inside(t, ground)) return true;
    return false;
}
// The smallest box a text sits in; -1 if none.
inline int smallestAround(const Frame& f, const Text& t) {
    int best = -1;
    for (std::size_t j = 0; j < f.boxes.size(); ++j) {
        if (!boxOf(f, t, j)) continue;
        if (best < 0 || f.boxes[j].w * f.boxes[j].h < f.boxes[static_cast<std::size_t>(best)].w * f.boxes[static_cast<std::size_t>(best)].h) best = static_cast<int>(j);
    }
    return best;
}
// How far a text's middle is from a box's, up and down.
inline float offMiddleY(const Text& t, const Box& b) { return std::fabs(t.y + t.h / 2.f - (b.y + b.h / 2.f)); }
constexpr float kMiddle = 1.f;       // "in the middle" at LARGE: within a pixel
constexpr float kSnug = 3.f;         // of a box no taller than three of its text's capitals
constexpr float kStillMiddle = 1.5f; // and still, at another size: a capital's height is a whole number, so half a pixel more

// Every fault of `now` measured against `large`, a line each (ending with what was drawn where). Empty: it holds.
inline std::vector<std::string> faults(const Frame& large, const Frame& now) {
    std::vector<std::string> out;
    char line[400];
    const auto say = [&](const char* what, const Text& t, const char* more = "") {
        std::snprintf(line, sizeof line, "%s: \"%s\" at %.0f,%.0f, %.0f wide, %.0f tall%s", what, t.words.c_str(), static_cast<double>(t.x), static_cast<double>(t.y),
                      static_cast<double>(t.w), static_cast<double>(t.h), more);
        out.push_back(line);
    };
    if (large.texts.size() != now.texts.size()) {
        std::snprintf(line, sizeof line, "%d texts are drawn, LARGE draws %d", static_cast<int>(now.texts.size()), static_cast<int>(large.texts.size()));
        out.push_back(line);
        return out;
    }
    if (large.boxes.size() != now.boxes.size()) {
        std::snprintf(line, sizeof line, "%d rectangles are drawn, LARGE draws %d", static_cast<int>(now.boxes.size()), static_cast<int>(large.boxes.size()));
        out.push_back(line);
        return out;
    }
    for (std::size_t i = 0; i < now.texts.size(); ++i) {
        // (A name cut to fit its room shows more of itself in smaller lettering: that is the one way the words may differ.)
        const bool more = now.texts[i].cap < large.texts[i].cap && now.texts[i].words.compare(0, large.texts[i].words.size(), large.texts[i].words) == 0;
        if ((now.texts[i].words != large.texts[i].words && !more) || now.texts[i].order != large.texts[i].order) {
            say("other words than at LARGE", now.texts[i], (" (LARGE: \"" + large.texts[i].words + "\")").c_str());
            return out; // the two frames cannot be matched text for text
        }
    }
    for (std::size_t i = 0; i < now.texts.size(); ++i) {
        const Text& was = large.texts[i];
        const Text& is = now.texts[i];
        if (onScreen(was) && !onScreen(is)) say("off the screen", is);
        for (std::size_t j = 0; j < now.boxes.size(); ++j) {
            if (!boxOf(large, was, j) || inside(is, now.boxes[j])) continue;
            const Box& b = now.boxes[j];
            std::snprintf(line, sizeof line, ", out of its box at %.0f,%.0f, %.0f wide, %.0f tall", static_cast<double>(b.x), static_cast<double>(b.y), static_cast<double>(b.w), static_cast<double>(b.h));
            say("left its box", is, std::string(line).c_str());
            break; // (one line a text: the boxes around that one say the same)
        }
        // A text that is clear of a rectangle at LARGE (a bar beside it, a line under it, the next
        // box along) has not grown into it.
        for (std::size_t j = 0; j < now.boxes.size(); ++j) {
            if (touches(was, large.boxes[j]) || !touches(is, now.boxes[j]) || !sameLayer(large, was, j) || laidOver(now, is, j)) continue;
            const Box& b = now.boxes[j];
            std::snprintf(line, sizeof line, ", into the rectangle at %.0f,%.0f, %.0f wide, %.0f tall", static_cast<double>(b.x), static_cast<double>(b.y), static_cast<double>(b.w), static_cast<double>(b.h));
            say("ran into what is beside it", is, std::string(line).c_str());
            break;
        }
        for (std::size_t k = i + 1; k < now.texts.size(); ++k) {
            if (!clear(was, large.texts[k]) || clear(is, now.texts[k]) || !neighbours(large, was, large.texts[k])) continue;
            const Text& other = now.texts[k];
            std::snprintf(line, sizeof line, ", into \"%s\" at %.0f,%.0f", other.words.c_str(), static_cast<double>(other.x), static_cast<double>(other.y));
            say("ran into its neighbour", is, std::string(line).c_str());
        }
        // A text in the middle of a box that is snug round it, a row or a chip, stays in the middle.
        // (Up and down only: side to side a text is either drawn about its middle, and stays there,
        // or from its left end, and was never meant to be. In a tall box a text hangs from the top.)
        const int around = smallestAround(large, was);
        if (around >= 0) {
            const Box& before = large.boxes[static_cast<std::size_t>(around)];
            const Box& after = now.boxes[static_cast<std::size_t>(around)];
            if (before.h <= kSnug * was.h && offMiddleY(was, before) <= kMiddle && offMiddleY(is, after) > kStillMiddle) say("no longer in the middle, up and down", is);
        }
    }
    return out;
}

// What "--texts FILE" wrote: a frame for each size. Gives how many sizes were read.
inline int readFrames(const char* path, Frame frames[4]) {
    FILE* in = std::fopen(path, "r");
    if (!in) return 0;
    int size = -1, read = 0;
    char line[600];
    while (std::fgets(line, sizeof line, in)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        int n = 0, used = 0;
        if (std::sscanf(s.c_str(), "SIZE %d", &n) == 1 && n >= 0 && n < 4) {
            size = n;
            frames[size] = Frame();
            ++read;
        } else if (size >= 0 && s.compare(0, 2, "T ") == 0) {
            Text t;
            if (std::sscanf(s.c_str(), "T %d %f %f %f %f %f %n", &t.order, &t.cap, &t.x, &t.y, &t.w, &t.h, &used) >= 6) {
                t.words = used > 0 && used <= static_cast<int>(s.size()) ? s.substr(static_cast<std::size_t>(used)) : "";
                frames[size].texts.push_back(t);
            }
        } else if (size >= 0 && s.compare(0, 2, "B ") == 0) {
            Box b;
            int solid = 0;
            if (std::sscanf(s.c_str(), "B %d %d %f %f %f %f", &b.order, &solid, &b.x, &b.y, &b.w, &b.h) == 6) b.solid = solid != 0, frames[size].boxes.push_back(b);
        }
    }
    std::fclose(in);
    return read;
}

} // namespace textfit
} // namespace apotheca
