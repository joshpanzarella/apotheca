#include "app.hpp"

#include <cstdio>

namespace apotheca {

namespace {
constexpr Uint32 kUpdateHoldMs = 1000; // A held this long accepts an update
constexpr Uint32 kPanicNoteMs = 4000; // how long the notice stays once the buttons are let go
}

App::App(Engine& engine)
    : project_(new Project), session_(engine, *project_), song_(session_), drums_(session_), notes_(session_), kit_(session_), fx_(session_), sampler_(session_),
      phazed_(session_, phazedSpec()), twin_(session_, twinSpec()) {}

void App::onButton(const ButtonEvent& event, const Input& input) {
    // All four shoulder buttons at once: panic. Silence now, whatever screen
    // this is. It lasts as long as they are held.
    const Button b = event.button;
    const bool shoulder = b == Button::L1 || b == Button::R1 || b == Button::L2 || b == Button::R2;
    if (shoulder && event.down) {
        const int down = input.held(Button::L1) + input.held(Button::R1) + input.held(Button::L2) + input.held(Button::R2);
        if (down == 1) viewBeforeShoulders_ = session_.view; // the first of them: the triggers may turn the tabs on the way
        if (down == 4) {
            panicking_ = true;
            panicNote_ = true;
            session_.view = viewBeforeShoulders_;
            session_.panic(true);
            return;
        }
        // With both triggers down the bumpers are on their way to a panic, not painting.
        if ((b == Button::L1 || b == Button::R1) && input.held(Button::L2) && input.held(Button::R2)) return;
    } else if (shoulder && panicking_) {
        panicking_ = false;
        panicNoteUntil_ = now_ + kPanicNoteMs;
        session_.panic(false);
    }
    // An update has arrived over WiFi and wants an answer. Nothing else
    // happens until it has one: hold A for a second to accept, B to refuse.
    if (session_.updateWaiting()) {
        if (b == Button::A) updateHeld_ = event.down, updateSince_ = 0;
        else if (b == Button::B && event.down) session_.discardUpdate(), updateHeld_ = false;
        return;
    }
    updateHeld_ = false;

    // The notice is up and the buttons are let go: the next press only puts
    // it away. It might have been an accident; that press must not also
    // place a hit or start the song.
    if (panicNote_ && !panicking_ && !shoulder && event.down) {
        panicNote_ = false;
        return;
    }

    // SELECT held with L1 or R1: quieter or louder, from any screen. (The
    // handheld's own volume buttons belong to its firmware and may not
    // reach a running program.)
    if ((b == Button::L1 || b == Button::R1) && input.held(Button::Select) && !panicking_) {
        if (event.down) {
            changeVolume(b == Button::R1 ? 0.1f : -0.1f);
            song_.selectWasUsed();
        }
        return;
    }

    // SELECT held with the D-pad: the tempo, from any screen. Up and down a
    // beat a minute, left and right ten.
    const bool direction = b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right;
    if (direction && input.held(Button::Select)) {
        if (event.down) {
            const float by = b == Button::Up ? 1.f : b == Button::Down ? -1.f : b == Button::Right ? 10.f : -10.f;
            session_.setTempo(project_->tempo + by);
            song_.selectWasUsed();
        }
        return;
    }

    // The shoulder triggers walk along the tabs. Nothing else uses them.
    if (event.button == Button::L2 || event.button == Button::R2) {
        // With A held on a grid they nudge what A has hold of, early or late.
        if (input.held(Button::A) && session_.view == View::Pattern && !panicking_) {
            if (isSynth(project_->current().kind)) notes_.onButton(event, input);
            else drums_.onButton(event, input);
            return;
        }
        if (!event.down || input.held(Button::A) || input.held(Button::Y)) return;
        const int now = static_cast<int>(session_.view);
        const int next = now + (event.button == Button::R2 ? 1 : -1);
        if (next >= 0 && next <= static_cast<int>(View::Effects)) session_.view = static_cast<View>(next);
        return;
    }
    const TrackKind kind = project_->current().kind;
    switch (session_.view) {
    case View::Song:
        song_.onButton(event, input);
        break;
    case View::Pattern:
        if (isSynth(kind)) notes_.onButton(event, input);
        else drums_.onButton(event, input);
        break;
    case View::Device:
        if (kind == TrackKind::Phazed) {
            phazed_.onButton(event, input);
        } else if (kind == TrackKind::Twin) {
            twin_.onButton(event, input);
        } else if (kind == TrackKind::Sampler) {
            sampler_.onButton(event, input);
        } else {
            kit_.onButton(event, input);
        }
        break;
    case View::Effects:
        fx_.onButton(event, input);
        break;
    }
}

void App::changeVolume(float by) {
    session_.setVolume(session_.volume() + by);
    volumeShownUntil_ = now_ + 1500;
}

void App::update(const Input& input, Uint32 nowMs) {
    now_ = nowMs;
    if (panicNote_ && !panicking_ && nowMs >= panicNoteUntil_) panicNote_ = false;
    // A held for a whole second on the update notice: accepted.
    if (session_.updateWaiting() && updateHeld_) {
        if (updateSince_ == 0) updateSince_ = nowMs;
        if (nowMs - updateSince_ >= kUpdateHoldMs) {
            updateHeld_ = false;
            updateSaid_ = session_.acceptUpdate() ? 1 : 2;
            updateSaidUntil_ = nowMs + 5000;
        }
    }
    if (updateSaid_ != 0 && nowMs >= updateSaidUntil_) updateSaid_ = 0;
    session_.update(nowMs);
    session_.engine.setScopeTrack(project_->track);
    const TrackKind kind = project_->current().kind;
    switch (session_.view) {
    case View::Song:
        song_.update(input, nowMs);
        break;
    case View::Pattern:
        if (isSynth(kind)) notes_.update(input, nowMs);
        else drums_.update(input, nowMs);
        break;
    case View::Device:
        if (kind == TrackKind::Phazed) phazed_.update(input, nowMs);
        else if (kind == TrackKind::Twin) twin_.update(input, nowMs);
        else if (kind == TrackKind::Sampler) sampler_.update(input, nowMs);
        else kit_.update(input, nowMs);
        break;
    case View::Effects:
        fx_.update(input, nowMs);
        break;
    }
}

void App::draw(SDL_Renderer* renderer, const Fonts& fonts) const {
    const TrackKind kind = project_->current().kind;
    switch (session_.view) {
    case View::Song:
        song_.draw(renderer, fonts);
        break;
    case View::Pattern:
        if (isSynth(kind)) notes_.draw(renderer, fonts);
        else drums_.draw(renderer, fonts);
        break;
    case View::Device:
        if (kind == TrackKind::Phazed) phazed_.draw(renderer, fonts);
        else if (kind == TrackKind::Twin) twin_.draw(renderer, fonts);
        else if (kind == TrackKind::Sampler) sampler_.draw(renderer, fonts);
        else kit_.draw(renderer, fonts);
        break;
    case View::Effects:
        fx_.draw(renderer, fonts);
        break;
    }

    if (now_ < volumeShownUntil_) {
        // The volume, for a moment after it is changed.
        using namespace ui;
        const int w = 260, h = 46, x = (kScreenW - w) / 2, y = kScreenH - kHintsH - h - 12;
        box(renderer, x, y, w, h, kPanel, kLine);
        char text[32];
        std::snprintf(text, sizeof text, "VOLUME  %d%%", static_cast<int>(session_.volume() * 100.f + 0.5f));
        fonts.small.draw(renderer, x + 12.f, y + 9.f, text, kText);
        fill(renderer, x + 12, y + 28, w - 24, 8, kLine);
        fill(renderer, x + 12, y + 28, static_cast<int>((w - 24) * session_.volume()), 8, kTeal);
    }
    if (session_.updateWaiting() || updateSaid_ != 0) {
        using namespace ui;
        const int w = 470, h = 150, x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
        fill(renderer, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - 1, faded(kBg, 190));
        box(renderer, x, y, w, h, kPanel, kTeal);
        outline(renderer, x, y, w, h, kTeal, 2);
        if (updateSaid_ == 1) {
            fonts.large.draw(renderer, x + 18.f, y + 16.f, "UPDATE ACCEPTED", kTeal);
            fonts.medium.draw(renderer, x + 18.f, y + 46.f, "It will be used the next time Apotheca starts.", kText);
            fonts.small.draw(renderer, x + 18.f, y + 74.f, "The one running now is kept. If the new one does not", kDim);
            fonts.small.draw(renderer, x + 18.f, y + 90.f, "start, Apotheca goes back to this one by itself.", kDim);
        } else if (updateSaid_ == 2) {
            fonts.large.draw(renderer, x + 18.f, y + 16.f, "COULD NOT KEEP THE UPDATE", kBrass);
            fonts.medium.draw(renderer, x + 18.f, y + 46.f, "Nothing was changed. Is the card full?", kText);
        } else {
            char size[64];
            std::snprintf(size, sizeof size, "A new Apotheca was sent over WiFi (%.1f MB).", static_cast<double>(session_.updateMegabytes()));
            fonts.large.draw(renderer, x + 18.f, y + 16.f, "AN UPDATE HAS ARRIVED", kTeal);
            fonts.medium.draw(renderer, x + 18.f, y + 46.f, size, kText);
            fonts.small.draw(renderer, x + 18.f, y + 72.f, "Only accept it if you sent it yourself.", kBrass);
            fonts.small.draw(renderer, x + 18.f, y + 96.f, "HOLD A for a second to accept.    B to throw it away.", kText);
            // The hold, filling.
            fill(renderer, x + 18, y + 120, w - 36, 8, kLine);
            if (updateHeld_ && updateSince_ != 0) {
                const float part = static_cast<float>(now_ - updateSince_) / static_cast<float>(kUpdateHoldMs);
                fill(renderer, x + 18, y + 120, static_cast<int>((w - 36) * (part > 1.f ? 1.f : part)), 8, kTeal);
            }
        }
    } else if (panicNote_) {
        using namespace ui;
        const int w = 440, h = 132, x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
        fill(renderer, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - 1, faded(kBg, 170));
        box(renderer, x, y, w, h, kPanel, kBrass);
        outline(renderer, x, y, w, h, kBrass, 2);
        fonts.large.draw(renderer, x + 18.f, y + 16.f, "PANIC", kBrass);
        fonts.medium.draw(renderer, x + 18.f, y + 44.f, "The sound was shut off and the song stopped.", kText);
        fonts.small.draw(renderer, x + 18.f, y + 70.f, "All four shoulder buttons were held: L1, L2, R1 and R2.", kDim);
        fonts.small.draw(renderer, x + 18.f, y + 88.f, "Nothing in the song was changed.", kDim);
        fonts.small.draw(renderer, x + 18.f, y + 108.f, panicking_ ? "Let go to carry on." : "Press any button to carry on.", kText);
    }
}

} // namespace apotheca
