// The slice view's picture: one lane of the drum grid, with its recording
// drawn large and cut into slices. (What the buttons do there is in
// pattern_screen.cpp, with everything else the grid does.)
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "pattern_layout.hpp"
#include "pattern_screen.hpp"

namespace apotheca {

using namespace ui;
using namespace pattern_layout;

namespace {
// The recording: as wide as the sixteen steps under it.
constexpr int kWaveX = kGridLeft, kWaveW = kSteps * kStepW - 2, kWaveY = kGridTop, kWaveH = 150;
// The steps: taller than the grid's cells, with the slice's number above each hit.
constexpr int kStepsY = kWaveY + kWaveH + 44, kStepsH = 96, kHitFull = kStepsH - 30;

void line(SDL_Renderer* r, int x1, int y1, int x2, int y2, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r, x1, y1, x2, y2);
}

// Where a point of the recording (0 to 1) is across the picture.
int xOf(float start) { return kWaveX + static_cast<int>(std::round(start * kWaveW)); }
} // namespace

// A hit's step effects, as marks on its block and beside it. Inside: slits
// for a ratchet (the block in as many slivers as it has hits), a red edge on
// the right for a cut. Beside it: an arrow on the left for backwards, and on
// the right a dash that sits above the middle for a higher pitch and below
// it for a lower one. Above it: a short bar for a THROW, over its left half
// for ECHO, over its right for SPACE, one over each for both (the order the
// two sends have on the FX screen).
void PatternScreen::drawFxMarks(SDL_Renderer* r, const StepFx& fx, int x, int y, int w, int h, int middleY, float rise) {
    if (!fx.any()) return;
    for (int k = 1; k < fx.hits(); ++k) fill(r, x + w * k / fx.hits() - 1, y, 3, h, kBg);
    if (fx.cut) fill(r, x + w - 4, y, 4, h, kStop);
    if (fx.reverse)
        for (int dy = -5; dy <= 5; ++dy) fill(r, x - 6 + std::abs(dy), middleY + dy, 6 - std::abs(dy), 1, kPurple);
    if (fx.throwTo & kThrowEcho) fill(r, x, y - 3, w / 2 - 1, 2, kPurple);
    if (fx.throwTo & kThrowSpace) fill(r, x + w - (w / 2 - 1), y - 3, w / 2 - 1, 2, kPurple);
    if (fx.pitch) fill(r, x + w + 1, middleY - static_cast<int>(std::round(fx.pitch * rise)) - 2, 5, 5, kPurple);
}

void PatternScreen::drawSlices(SDL_Renderer* r, const Fonts& fonts) const {
    char text[32];
    const bool onName = step_ == kLaneName;
    const bool held = shaping_ || dicing_;
    const SDL_Color cursorColor = held || paint_ != Paint::None ? kBrass : kTeal;
    const int count = slices();
    const float sliceW = static_cast<float>(kWaveW) / static_cast<float>(count);
    const Session::LaneSound& sound = session_.laneSound(project_.track, lane_);
    // SLICES AT THE HITS: a lane of its own cuts has slices of their own widths (EVEN: as it always was drawn).
    const bool own = !trk().laneCuts[lane_].even();
    const auto sliceX = [&](int k) { return own ? xOf(cutAt(k)) : xOf(static_cast<float>(k) / count); };
    const auto sliceWidth = [&](int k) { return own ? (k + 1 < count ? xOf(cutAt(k + 1)) : kWaveX + kWaveW) - xOf(cutAt(k)) : static_cast<int>(std::ceil(sliceW)); };

    drawSliceLane(r, fonts, onName, cursorColor, count, own);

    // ---- which slices this bar's hits play, and which is playing ------------------------
    bool used[64] = {};
    for (int s = 0; s < kSteps; ++s)
        if (pat().level[lane_][s] > 0.f) used[sliceOf(s)] = true;
    const int cursorSlice = onName ? -1 : (hitHere() ? sliceOf(step_) : own ? session_.laneSliceAt(project_.track, lane_, ownStart(step_)) : static_cast<int>(std::round(ownStart(step_) * count)));
    const int playStep = project_.playScene == project_.scene && project_.cellBarPlaying(project_.track) == project_.bar ? project_.playStep : -1;
    int playSlice = -1;
    if (playStep >= 0) {
        // The hit that is sounding: the last one at or before the step being played. One slice of it,
        // or as far on as the recording has run since.
        for (int s = playStep; s >= 0 && playSlice < 0; --s) {
            if (pat().level[lane_][s] <= 0.f) continue;
            if (!trk().laneOneSlice[lane_] && own) { // (its own cuts: where a step at a time of the sound has got to; EVEN: a slice a step)
                const float got = pat().start[lane_][s] + static_cast<float>(playStep - s) / static_cast<float>(session_.evenSlices(project_.track, lane_));
                playSlice = got < 1.f ? session_.laneSliceAt(project_.track, lane_, got) : count;
            } else if (!trk().laneOneSlice[lane_]) playSlice = sliceOf(s) + (playStep - s);
            else if (s == playStep) playSlice = sliceOf(s);
            else break;
        }
        if (playSlice >= count) playSlice = -1; // it ran off the end of the recording
    }

    // ---- the recording ---------------------------------------------------------------------
    box(r, kWaveX, kWaveY, kWaveW, kWaveH, kPanel, kLine);
    if (cursorSlice >= 0) fill(r, sliceX(cursorSlice), kWaveY + 1, sliceWidth(cursorSlice), kWaveH - 2, faded(hitHere() ? cursorColor : kDim, 60));
    if (playSlice >= 0) fill(r, sliceX(playSlice), kWaveY + 1, sliceWidth(playSlice), kWaveH - 2, faded(kPurple, 76));
    const int peaks = static_cast<int>(sound.peaks.size());
    int peakSlice = 0; // (its own cuts: walked along as the peaks go)
    for (int i = 0; i < peaks; ++i) {
        const int px = kWaveX + 1 + (kWaveW - 2) * i / peaks;
        const int ph = 1 + static_cast<int>(sound.peaks[static_cast<std::size_t>(i)] * (kWaveH - 14));
        if (own)
            while (peakSlice + 1 < count && cutAt(peakSlice + 1) * peaks <= static_cast<float>(i)) ++peakSlice;
        const int slice = own ? peakSlice : i * count / peaks;
        fill(r, px, kWaveY + (kWaveH - ph) / 2, 1, ph, used[slice] ? kText : kDim); // brighter where a hit plays it
    }
    if (own) {
        // Its own cuts, each where it is (a hit, or where the hand put it), and their numbers where there is room
        // (the cursor's always; no number is drawn over another). A tight row: the same numbers at every text
        // size (fonts.fixed()).
        const Fonts& tight = fonts.fixed();
        const auto label = [&](int i, float& middle, float& half) {
            std::snprintf(text, sizeof text, "%d", i + 1);
            middle = sliceX(i) + sliceWidth(i) / 2.f;
            half = tight.small.width(text) / 2.f;
        };
        float cursorMiddle = -1000.f, cursorHalf = 0.f;
        if (cursorSlice >= 0) label(cursorSlice, cursorMiddle, cursorHalf);
        float clearFrom = 0.f;
        for (int i = 0; i < count; ++i) {
            if (i > 0) fill(r, sliceX(i), kWaveY + 1, 1, kWaveH - 2, kDim);
            float middle = 0.f, half = 0.f;
            label(i, middle, half);
            const bool cursor = i == cursorSlice;
            const bool clearOfCursor = middle + half + 4.f <= cursorMiddle - cursorHalf || middle - half >= cursorMiddle + cursorHalf + 4.f;
            if (cursor || (middle - half >= clearFrom && clearOfCursor)) {
                tight.small.drawCentered(r, middle, 36.f, text, cursor ? cursorColor : kText);
                clearFrom = middle + half + 4.f;
            }
        }
    } else {
        // The cuts, and their numbers: every one of sixteen, every other of thirty-two, every fourth of sixty-four.
        const int every = count / kSteps;
        for (int i = 0; i < count; ++i) {
            const int x = xOf(static_cast<float>(i) / count);
            const bool beat = i % (count / 4) == 0;
            if (i > 0) fill(r, x, kWaveY + 1, 1, kWaveH - 2, beat ? kDim : kLine);
            if (i % every == 0) {
                std::snprintf(text, sizeof text, "%d", i + 1);
                fonts.small.drawCentered(r, x + sliceW * every / 2.f, 36.f, text, i == cursorSlice ? cursorColor : (beat ? kText : kDim));
            }
        }
    }
    // Where the hit under the cursor comes in, exactly (it need not be on a cut).
    if (hitHere()) fill(r, xOf(pat().start[lane_][step_]), kWaveY - 2, 2, kWaveH + 4, turning_ == Turning::Start ? kBrass : cursorColor);

    drawSliceSteps(r, fonts, onName, held, cursorColor, playStep);
}

// The lane: its name, its volume, and what a hit plays.
void PatternScreen::drawSliceLane(SDL_Renderer* r, const Fonts& fonts, bool onName, SDL_Color cursorColor, int count, bool own) const {
    char text[32];
    // ---- the lane: its name, its volume, and what a hit plays ------------------------
    box(r, kNameX, kWaveY, kNameW, kWaveH, kBg, kLine);
    const int filled = static_cast<int>(std::round((kNameW - 2) * trk().laneLevel[lane_]));
    fill(r, kNameX + 1, kWaveY + 1, filled, kWaveH - 2, onName && shaping_ ? faded(kBrass, 110) : kPanel);
    if (onName) outline(r, kNameX - 1, kWaveY - 1, kNameW + 2, kWaveH + 2, cursorColor, 2);
    // Medium if it fits both at the size chosen and at LARGE, else small: so a smaller size never draws it bigger than
    // LARGE does (it did at MEDIUM once MEDIUM was made smaller: the name fitted there in medium, at LARGE it did not).
    (fonts.medium.width(laneLabel(lane_)) <= kNameW - 15 && fonts.fixed().medium.width(laneLabel(lane_)) <= kNameW - 15 ? fonts.medium : fonts.small).draw(r, static_cast<float>(kNameTextX), kWaveY + 10.f, laneLabel(lane_), kTeal);
    fonts.small.draw(r, static_cast<float>(kNameTextX), kWaveY + 44.f, "EACH HIT", kDim);
    fonts.small.draw(r, static_cast<float>(kNameTextX), kWaveY + 60.f, "PLAYS", kDim);
    const char* plays = trk().laneOneSlice[lane_] ? "ONE SLICE" : "RUNS ON";
    fonts.within(&Fonts::medium, plays, kNameW - 13.f).draw(r, static_cast<float>(kNameTextX), kWaveY + 78.f, plays, trk().laneOneSlice[lane_] ? kPurple : kText);
    std::snprintf(text, sizeof text, count == 1 ? "%d SLICE" : "%d SLICES", count);
    fonts.small.draw(r, static_cast<float>(kNameTextX), kWaveY + kWaveH - 22.f, text, kDim);
    // ...and how they are cut, above it: EVEN, AT HITS 5, BY HAND (SLICES AT THE HITS).
    cutsWord(text, sizeof text);
    fonts.within(&Fonts::small, text, kNameW - 13.f).draw(r, static_cast<float>(kNameTextX), kWaveY + kWaveH - 40.f, text, own ? kPurple : kDim);
    drawFeelMark(r, kWaveY, trk().laneFeel[lane_]); // the lane's feel, as on the grid
    drawSwingMark(r, kWaveY, trk().laneSwing[lane_]); // ...its SWING
    drawChokeMark(r, kWaveY, trk().laneChoke[lane_]); // ...and its CHOKE group
}

// The steps under the recording, each hit with a line up to the slice it plays.
void PatternScreen::drawSliceSteps(SDL_Renderer* r, const Fonts& fonts, bool onName, bool held, SDL_Color cursorColor, int playStep) const {
    char text[32];
    const bool own = !trk().laneCuts[lane_].even();
    const float sliceW = static_cast<float>(kWaveW) / static_cast<float>(slices());
    // ---- the steps, and a line from each hit to its slice ----------------------------------
    for (int s = 0; s < kSteps; ++s) {
        const int x = kGridLeft + s * kStepW;
        box(r, x, kStepsY, kCellW, kStepsH, s % 4 == 0 ? kPanel2 : kPanel, kLine);
        if (s < project_.barSteps(project_.scene, sceneBar())) std::snprintf(text, sizeof text, "%d", sceneBar() * project_.meterOf(project_.scene) + s + 1);
        else std::snprintf(text, sizeof text, "-");
        fonts.small.drawCentered(r, x + kCellW / 2.f, kStepsY + kStepsH + 5.f, text, s % 4 == 0 ? kText : kDim);
    }
    const int right = kGridLeft + kSteps * kStepW - 2;
    for (int s = 0; s < kSteps; ++s) {
        const float level = pat().level[lane_][s];
        if (level <= 0.f) continue;
        const bool here = s == step_;
        const bool live = held && here;
        const float start = pat().start[lane_][s];
        const bool inOrder = std::fabs(start - ownStart(s)) < 0.0005f; // in order: the slice the step would play by itself
        int bx = kGridLeft + s * kStepW + 4 + static_cast<int>(std::round(pat().nudge[lane_][s] * kStepW));
        bx = bx < kGridLeft ? kGridLeft : (bx + kBlockW > right ? right - kBlockW : bx);
        int h = static_cast<int>(std::round(level * kHitFull));
        h = h < 1 ? 1 : h;
        // From the slice down to the hit. A slice in order drops straight down; one out of order crosses.
        const int half = own ? (xOf(start + slicePart(start)) - xOf(start)) / 2 : static_cast<int>(sliceW / 2.f);
        const int fromX = xOf(start) + half, toX = kGridLeft + s * kStepW + kCellW / 2;
        const SDL_Color wire = here ? cursorColor : (inOrder ? kLine : faded(kPurple, 170));
        line(r, fromX, kWaveY + kWaveH + 1, toX, kStepsY - 2, wire);
        if (here) line(r, fromX + 1, kWaveY + kWaveH + 1, toX + 1, kStepsY - 2, wire);

        SDL_Color body = live ? kTeal : kText;
        const float chance = pat().chance[lane_][s];
        body.a = static_cast<Uint8>(std::round(255.f * (0.12f + 0.88f * chance * chance)));
        const int by = kStepsY + kStepsH - 4 - h;
        if (live) outline(r, bx, kStepsY + kStepsH - 4 - kHitFull, kBlockW, kHitFull, kDim);
        fill(r, bx, by, kBlockW, h, body);
        if (chance < 1.f) outline(r, bx, by, kBlockW, h, live ? kTeal : kText);
        drawFxMarks(r, pat().fx[lane_][s], bx, by, kBlockW, h, kStepsY + 24 + kHitFull / 2, 2.2f);
        // Its slice's number: quiet when it is the step's own, purple when it has been moved.
        std::snprintf(text, sizeof text, "%d", sliceOf(s) + 1);
        fonts.small.drawCentered(r, kGridLeft + s * kStepW + kCellW / 2.f, kStepsY + 6.f, text, here ? cursorColor : (inOrder ? kDim : kPurple));
    }
    if (!onName) outline(r, kGridLeft + step_ * kStepW - 1, kStepsY - 1, kCellW + 2, kStepsH + 2, cursorColor, 2);
    if (playStep >= 0) fill(r, kGridLeft + playStep * kStepW, kStepsY, kCellW, kStepsH, faded(kPurple, 76));
    // A scene of fewer than sixteen steps: the steps past its end are never played, and are shaded.
    if (const int steps = project_.barSteps(project_.scene, sceneBar()); steps < kSteps)
        fill(r, kGridLeft + steps * kStepW - 1, kStepsY - 2, (kSteps - steps) * kStepW, kStepsH + 22, faded(kBg, 185));
}

} // namespace apotheca
