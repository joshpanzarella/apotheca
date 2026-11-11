#include "synth_screen.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace apotheca {

using namespace ui;
using namespace phazed;

namespace {

constexpr float kChoicesPerSecond = 5.f; // the stick held over on a choice
constexpr Uint32 kRepeatDelayMs = ui::kCursorDelayMs, kRepeatEveryMs = ui::kCursorEveryMs;
constexpr float kStickStopsPerSecond = 16.f;
constexpr float kMacroPerSecond = 2.f;    // stick fully over: bottom to top in half a second
constexpr float kPresetsPerSecond = 10.f;


} // namespace

// ---- what is on each page --------------------------------------------------------

int SynthScreen::itemCount() const { return page_ == 0 ? kMainStops : spec_.itemCount(page_); }

SynthScreen::Item SynthScreen::item(int index) const {
    Item it = {0, ""};
    spec_.item(page_, index, it.id, it.name);
    return it;
}

int SynthScreen::columns() const { return envelopePage() ? kEnvSteps : 4; }

int SynthScreen::settingHere() const { return page_ == 0 ? -1 : item(cursor_).id; }

void SynthScreen::goTo(int page, int cursor) {
    page_ = page < 0 ? 0 : (page >= spec_.pages ? spec_.pages - 1 : page);
    const int count = itemCount();
    cursor_ = cursor < 0 ? 0 : (cursor >= count ? count - 1 : cursor);
}

// ---- the cursor --------------------------------------------------------------------

void SynthScreen::moveCursor(Button direction) {
    if (page_ == 0) {
        // Four rows: folder and preset; two rows of four macros; ADVANCED.
        const int row = cursor_ < kFirstMacro ? 0 : cursor_ == kAdvanced ? 3 : 1 + (cursor_ - kFirstMacro) / 4;
        const int col = cursor_ < kFirstMacro ? cursor_ * 2 : cursor_ == kAdvanced ? 0 : (cursor_ - kFirstMacro) % 4;
        int r = row, c = col;
        if (direction == Button::Up) r = row > 0 ? row - 1 : 3;
        if (direction == Button::Down) r = row < 3 ? row + 1 : 0;
        if (direction == Button::Left) c = col - 1;
        if (direction == Button::Right) c = col + 1;
        if (r == 0) {
            if (direction == Button::Left || direction == Button::Right) cursor_ = cursor_ == kFolder ? kPreset : kFolder;
            else cursor_ = col >= 2 ? kPreset : kFolder;
        } else if (r == 3) {
            cursor_ = kAdvanced;
        } else {
            c = ((c % 4) + 4) % 4;
            cursor_ = kFirstMacro + (r - 1) * 4 + c;
        }
        return;
    }
    const int count = itemCount(), cols = columns();
    int next = cursor_;
    if (direction == Button::Left) next = cursor_ - 1;
    if (direction == Button::Right) next = cursor_ + 1;
    if (direction == Button::Up) next = cursor_ - cols;
    if (direction == Button::Down) next = cursor_ + cols;
    if (envelopePage() && direction == Button::Down && cursor_ >= kEnvSteps && cursor_ < 2 * kEnvSteps) {
        next = cursor_ < kEnvSteps + 4 ? 2 * kEnvSteps : 2 * kEnvSteps + 1; // from the levels down to HOLD AT or LAST STEP
    }
    if (envelopePage() && direction == Button::Up && cursor_ >= 2 * kEnvSteps) {
        next = cursor_ == 2 * kEnvSteps ? kEnvSteps : 2 * kEnvSteps - 1;
    }
    if (next >= 0 && next < count) cursor_ = next;
}

// ---- changing the sound ----------------------------------------------------------------

void SynthScreen::hear() {
    if (!project_.playing) session_.audition(project_.track, 60, 0.8f);
}

void SynthScreen::choosePreset(int index) {
    const int count = spec_.presetCount();
    index = ((index % count) + count) % count;
    if (index == trk().preset) return;
    session_.chooseSynthPreset(project_.track, index);
    hear();
}

void SynthScreen::stepPreset(int by) { choosePreset(trk().preset + by); }

// To the first preset of the next or the previous folder.
void SynthScreen::stepFolder(int by) {
    const int count = spec_.presetCount();
    const char* here = spec_.presetCategory(trk().preset);
    int i = trk().preset;
    if (by > 0) {
        while (std::strcmp(spec_.presetCategory(i), here) == 0) i = (i + 1) % count;
    } else {
        // Back past the start of this folder, then to the start of the one before.
        while (std::strcmp(spec_.presetCategory((i + count - 1) % count), here) == 0) i = (i + count - 1) % count;
        i = (i + count - 1) % count;
        const char* before = spec_.presetCategory(i);
        while (std::strcmp(spec_.presetCategory((i + count - 1) % count), before) == 0) i = (i + count - 1) % count;
    }
    choosePreset(i);
}

// Through the session, so that with WRITE on and the song playing the move is recorded into the pattern.
void SynthScreen::setMacro(int macro, float value) { session_.setSynthMacro(project_.track, macro, value); }

void SynthScreen::setSetting(int id, int value) {
    value = value < spec_.settingMin(id) ? spec_.settingMin(id) : (value > spec_.settingMax(id) ? spec_.settingMax(id) : value);
    if (value == values()[id]) return;
    values()[id] = value;
    Command c;
    c.type = Command::SetSetting;
    c.track = project_.track;
    c.a = id;
    c.b = value;
    session_.engine.send(c);
}

bool SynthScreen::isChoice(int id) const { return spec_.settingMax(id) - spec_.settingMin(id) <= kChoiceRange; }

void SynthScreen::turn(float amount) {
    if (amount == 0.f) return;
    if (page_ == 0) {
        if (cursor_ >= kFirstMacro && cursor_ < kAdvanced) {
            setMacro(cursor_ - kFirstMacro, trk().macro[cursor_ - kFirstMacro] + amount * kMacroPerSecond);
            return;
        }
        if (cursor_ == kAdvanced) return;
        // Folder and preset step one at a time; up is the next one.
        saved_ += amount * kPresetsPerSecond;
        for (; saved_ >= 1.f; saved_ -= 1.f) cursor_ == kFolder ? stepFolder(1) : stepPreset(1);
        for (; saved_ <= -1.f; saved_ += 1.f) cursor_ == kFolder ? stepFolder(-1) : stepPreset(-1);
        return;
    }
    const int id = item(cursor_).id;
    // A setting with a handful of values (a wave, a type, on / off) is a choice: it clicks to the
    // next one at each push of the stick, and then steadily while the stick is held over.
    if (isChoice(id)) {
        if (!choicePushed_) {
            choicePushed_ = true;
            saved_ = 0.f;
            setSetting(id, values()[id] + (amount > 0.f ? 1 : -1));
            return;
        }
        saved_ += amount * kChoicesPerSecond;
        const int steps = static_cast<int>(saved_);
        if (steps != 0) {
            saved_ -= static_cast<float>(steps);
            setSetting(id, values()[id] + steps);
        }
        return;
    }
    // Any other setting: its whole range in about two thirds of a second.
    saved_ += amount * 1.5f * static_cast<float>(spec_.settingMax(id) - spec_.settingMin(id) + 1);
    const int whole = static_cast<int>(saved_);
    if (whole != 0) {
        saved_ -= static_cast<float>(whole);
        setSetting(id, values()[id] + whole);
    }
}

// The D-pad with A held. Up and down: one. Left and right: one as well, but on PRESET eight at
// a time, as in the list of recordings: there are dozens of presets and one at a time is slow.
void SynthScreen::stepHeld(Button direction) {
    const int way = direction == Button::Up || direction == Button::Right ? 1 : -1;
    const bool sideways = direction == Button::Left || direction == Button::Right;
    if (page_ == 0 && cursor_ == kPreset && sideways) {
        const int to = trk().preset + way * kPresetJump, last = spec_.presetCount() - 1;
        choosePreset(to < 0 ? 0 : (to > last ? last : to)); // the ends stop
    } else {
        stepExact(way);
    }
}

void SynthScreen::stepExact(int by) {
    if (page_ == 0) {
        if (cursor_ == kFolder) stepFolder(by);
        else if (cursor_ == kPreset) stepPreset(by);
        else if (cursor_ < kAdvanced) {
            // Twentieths, landing on them, so the middle is easy to find again.
            const float now = trk().macro[cursor_ - kFirstMacro] * 20.f;
            setMacro(cursor_ - kFirstMacro, (by > 0 ? std::floor(now + 1.001f) : std::ceil(now - 1.001f)) / 20.f);
        }
        return;
    }
    const int id = item(cursor_).id;
    setSetting(id, values()[id] + by);
}

// ---- buttons ----------------------------------------------------------------------------

void SynthScreen::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    if (b == Button::A) {
        if (event.down && page_ == 0 && cursor_ == kAdvanced) {
            goTo(1, 0);
            return; // A opened the pages; it is not holding anything
        }
        holding_ = event.down;
        saved_ = 0.f;
        return;
    }
    // Y: ROLL, on a synth whose sounds are generated (QMOD). A new sound at once, and it is heard.
    if (b == Button::Y && event.down && spec_.roll && !holding_) {
        if (session_.rollSynth(project_.track, lastMs_ * 2654435761u + (++rolls_) * 40503u)) hear();
        return;
    }
    if (!event.down) {
        if (b == repeating_) repeating_ = Button::None;
        return;
    }
    if (isDirection(b)) {
        if (holding_) {
            stepHeld(b);
        } else {
            moveCursor(b);
        }
        repeating_ = b;
        repeatAt_ = 0;
        return;
    }
    if (b == Button::B && !holding_) {
        if (page_ > 0) goTo(0, kAdvanced); // back to MAIN, on the way in
        else if (cursor_ >= kFirstMacro && cursor_ < kAdvanced) {
            // Back to the middle, and whatever automation it had in this cell goes.
            const bool writing = session_.writing();
            session_.setWriting(false); // this is a reset, not a move to record
            session_.clearAutomation(project_.track, project_.scene, cursor_ - kFirstMacro);
            setMacro(cursor_ - kFirstMacro, 0.5f);
            session_.setWriting(writing);
        }
    } else if (b == Button::R1 && page_ > 0) {
        goTo(page_ == spec_.pages - 1 ? 1 : page_ + 1, 0);
    } else if (b == Button::L1 && page_ > 0) {
        goTo(page_ == 1 ? spec_.pages - 1 : page_ - 1, 0);
    } else if (b == Button::X) {
        hear();
    } else if (b == Button::Start && !input.held(Button::Select)) {
        session_.togglePlay(false);
    }
}

void SynthScreen::update(const Input& input, Uint32 nowMs) {
    // A button let go while another screen was showing (a song opened, a tab turned) never told this
    // screen so. What is held is asked afresh each frame, so nothing can stay "held" by mistake.
    if (!input.held(Button::A)) holding_ = false;

    float dt = lastMs_ == 0 ? 0.016f : static_cast<float>(nowMs - lastMs_) / 1000.f;
    if (dt > 0.05f) dt = 0.05f;
    dt *= ui::gStickSpeed; // everything the stick does, slower or faster, from one number
    lastMs_ = nowMs;
    const float x = input.stickX(), y = input.stickY();

    if (holding_) {
        const float push = -y;
        turn(push * std::fabs(push) * dt);
        if (push == 0.f) choicePushed_ = false; // back in the middle: the next push is a new click
        stickX_ = stickY_ = 0.f;
        stickWasX_ = stickWasY_ = false;
    } else {
        choicePushed_ = false;
        if (x == 0.f) stickX_ = 0.f, stickWasX_ = false;
        else {
            stickX_ += stickWasX_ ? std::fabs(x) * kStickStopsPerSecond * dt : 1.f;
            stickWasX_ = true;
            for (; stickX_ >= 1.f; stickX_ -= 1.f) moveCursor(x > 0.f ? Button::Right : Button::Left);
        }
        if (y == 0.f) stickY_ = 0.f, stickWasY_ = false;
        else {
            stickY_ += stickWasY_ ? std::fabs(y) * kStickStopsPerSecond * dt : 1.f;
            stickWasY_ = true;
            for (; stickY_ >= 1.f; stickY_ -= 1.f) moveCursor(y > 0.f ? Button::Down : Button::Up);
        }
    }

    if (repeating_ != Button::None && repeatAt_ == 0) {
        repeatAt_ = nowMs + kRepeatDelayMs;
    } else if (repeating_ != Button::None && input.held(repeating_) && nowMs >= repeatAt_) {
        repeatAt_ = nowMs + kRepeatEveryMs;
        if (holding_) stepHeld(repeating_);
        else moveCursor(repeating_);
    }
}

// ---- drawing ----------------------------------------------------------------------------

void SynthScreen::draw(SDL_Renderer* r, const Fonts& fonts) const {
    char text[64], name[24];
    trackName(project_, project_.track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / %s", name, spec_.pageNames[page_]);
    topBar(r, fonts, View::Device, text, project_);
    const TrackData& t = trk();
    const SDL_Color cursorColor = holding_ ? kBrass : kTeal;

    Hint list[6];
    int count = 0;
    const auto hint = [&](const char* key, const char* does) { list[count].key = key; list[count].does = does; ++count; };

    if (page_ == 0) {
        // ---- MAIN: folder, preset, eight macros, ADVANCED, and the scope ----------
        valueBox(r, fonts, 8, 36, 200, 50, "FOLDER", spec_.presetCategory(t.preset), -1.f, cursor_ == kFolder, holding_);
        std::snprintf(text, sizeof text, "%s", spec_.presetName(t.preset));
        valueBox(r, fonts, 216, 36, 416, 50, "PRESET", text, -1.f, cursor_ == kPreset, holding_);
        std::snprintf(text, sizeof text, "%d / %d", t.preset + 1, spec_.presetCount());
        fonts.small.drawRight(r, 622.f, 43.f, text, kDim);

        for (int m = 0; m < kMacros; ++m) {
            const int x = 8 + (m % 4) * 157, y = 98 + (m / 4) * 76;
            // While the song plays an automated macro shows where the pattern has it, not where its knob rests.
            const float v = session_.automated(project_.track, m, t.macro[m]);
            spec_.macroText(m, v, text, sizeof text);
            macroBox(r, fonts, x, y, 153, 70, spec_.macroNames[m], text, v, cursor_ == kFirstMacro + m, holding_);
            // A purple bar along the bottom: this macro has automation in the cell being worked on.
            if (session_.hasAutomation(project_.track, project_.scene, m)) fill(r, x + 1, y + 70 - 3, 151, 2, kPurple);
        }
        box(r, 8, 252, 153, 40, kPanel2, kLine);
        fonts.medium.draw(r, 20.f, 252.f + (40 - fonts.medium.capHeight()) / 2.f, "ADVANCED  >", cursor_ == kAdvanced ? kTeal : kText);
        if (cursor_ == kAdvanced) outline(r, 7, 251, 155, 42, kTeal, 2);
        std::snprintf(text, sizeof text, "Every setting of this sound, on %d pages.", spec_.pages - 1);
        fonts.small.draw(r, 174.f, 258.f, text, kDim);
        fonts.small.draw(r, 174.f, 274.f, "The macros above bend the sound and leave it as it was made.", kDim);

        fonts.small.draw(r, 322.f, 306.f, "LIVE OUTPUT", kDim);
        scope(r, session_.engine, 322, 322, 310, 124);

        if (holding_) {
            if (cursor_ == kFolder) hint("UP / DOWN", "next / previous folder");
            else if (cursor_ == kPreset) { hint("UP / DOWN", "next / previous preset"); hint("LEFT / RIGHT", "8 at a time"); }
            else hint("UP / DOWN", "turn it");
        } else {
            if (cursor_ == kAdvanced) hint("A", "open");
            else hint("HOLD A", cursor_ < kFirstMacro ? "change" : "turn");
            if (cursor_ >= kFirstMacro && cursor_ < kAdvanced) hint("B", "back to the middle");
            if (spec_.roll) hint("Y", "roll a new sound");
            hint("START", project_.playing ? "stop" : "play");
        }
        hints(r, fonts, list, count);
        return;
    }

    // ---- ADVANCED ----------------------------------------------------------------------
    std::snprintf(text, sizeof text, "PAGE %d OF %d", page_, spec_.pages - 1);
    fonts.small.drawRight(r, 632.f, 38.f, text, kDim);
    fonts.medium.draw(r, 8.f, 36.f, spec_.pageNames[page_], kText);

    if (!envelopePage()) {
        for (int i = 0; i < itemCount(); ++i) {
            const Item it = item(i);
            const int x = 8 + (i % 4) * 157, y = 60 + (i / 4) * 76;
            const int value = values()[it.id];
            spec_.settingText(it.id, value, text, sizeof text);
            const int range = spec_.settingMax(it.id) - spec_.settingMin(it.id);
            const float part = isChoice(it.id) ? -1.f : range > 0 ? static_cast<float>(value - spec_.settingMin(it.id)) / range : 0.f; // a choice has no bar
            valueBox(r, fonts, x, y, 153, 70, it.name, text, part, i == cursor_, holding_);
        }
        scope(r, session_.engine, 322, 372, 310, 74);
    } else {
        // An envelope: eight steps, each a speed and a level to reach.
        const int line = (page_ - 4) / 3, env = (page_ - 4) % 3;
        const int end = t.patch.value[envEnd(line, env)], sustain = t.patch.value[envSustain(line, env)];
        const int px = 8, py = 58, pw = 624, ph = 150;
        box(r, px, py, pw, ph, kPanel, kLine);
        // Each step is as wide as it is slow, so the picture is in time.
        float widths[kEnvSteps], total = 0.f;
        for (int s = 0; s < end; ++s) total += widths[s] = 6.f + 0.9f * (99 - t.patch.value[envRate(line, env, s)]);
        const float scale = total > 0.f ? (pw - 24) / total : 0.f;
        float x = px + 12.f;
        int lastX = px + 12, lastY = py + ph - 12;
        for (int s = 0; s < end; ++s) {
            x += widths[s] * scale;
            const int nx = static_cast<int>(x);
            const int ny = py + ph - 12 - (ph - 24) * t.patch.value[envLevel(line, env, s)] / 99;
            const bool here = cursor_ == s || cursor_ == kEnvSteps + s;
            SDL_SetRenderDrawColor(r, here ? cursorColor.r : kText.r, here ? cursorColor.g : kText.g, here ? cursorColor.b : kText.b, 255);
            SDL_RenderDrawLine(r, lastX, lastY, nx, ny);
            SDL_RenderDrawLine(r, lastX, lastY + 1, nx, ny + 1);
            fill(r, nx - 2, ny - 2, 5, 5, here ? cursorColor : kText);
            if (sustain == s + 1) {
                fill(r, nx, py + 6, 1, ph - 12, kBrass); // the note holds here until the key is let go
                fonts.small.draw(r, nx + 4.f, py + 8.f, "HOLD", kBrass);
            }
            std::snprintf(text, sizeof text, "%d", s + 1);
            fonts.small.drawCentered(r, static_cast<float>(nx), py + ph - 10.f, text, kDim);
            lastX = nx;
            lastY = ny;
        }
        // Two rows of eight: how fast each step moves, and where it ends up.
        fonts.small.draw(r, 8.f, 220.f, "SPEED OF EACH STEP", kDim);
        fonts.small.draw(r, 8.f, 300.f, "LEVEL EACH STEP REACHES", kDim);
        for (int i = 0; i < 2 * kEnvSteps; ++i) {
            const Item it = item(i);
            const int step = i % kEnvSteps;
            const int x2 = 8 + step * 78, y2 = i < kEnvSteps ? 236 : 316;
            const bool used = step < end;
            std::snprintf(text, sizeof text, "%d", t.patch.value[it.id]);
            box(r, x2, y2, 74, 54, used ? kPanel : kBg, kLine);
            fonts.small.draw(r, x2 + 6.f, y2 + 6.f, it.name, kDim);
            fonts.large.draw(r, x2 + 6.f, y2 + 22.f, text, i == cursor_ ? cursorColor : (used ? kText : kDim));
            fill(r, x2 + 6, y2 + 46, 62, 3, kLine);
            fill(r, x2 + 6, y2 + 46, 62 * t.patch.value[it.id] / 99, 3, i == cursor_ ? cursorColor : kDim);
            if (i == cursor_) outline(r, x2 - 1, y2 - 1, 76, 56, cursorColor, 2);
        }
        for (int i = 2 * kEnvSteps; i < kEnvSettings; ++i) {
            const Item it = item(i);
            spec_.settingText(it.id, t.patch.value[it.id], text, sizeof text);
            if (i == 2 * kEnvSteps + 1) std::snprintf(text, sizeof text, "STEP %d", t.patch.value[it.id]);
            valueBox(r, fonts, 8 + (i - 2 * kEnvSteps) * 312, 384, 308, 58, it.name, text, -1.f, i == cursor_, holding_);
        }
    }

    if (holding_) {
        hint("STICK UP / DOWN", "change it");
        hint("D-PAD UP / DOWN", "by one");
    } else {
        hint("HOLD A", "change");
        hint("L1 R1", "page");
        hint("B", "main");
        hint("START", project_.playing ? "stop" : "play");
    }
    hints(r, fonts, list, count);
}

} // namespace apotheca
