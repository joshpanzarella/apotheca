#include "app.hpp"

#include <cstdio>

namespace apotheca {

namespace {
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
    // The notice is up and the buttons are let go: the next press only puts
    // it away. It might have been an accident; that press must not also
    // place a hit or start the song.
    if (panicNote_ && !panicking_ && !shoulder && event.down) {
        panicNote_ = false;
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

void App::update(const Input& input, Uint32 nowMs) {
    now_ = nowMs;
    if (panicNote_ && !panicking_ && nowMs >= panicNoteUntil_) panicNote_ = false;
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

    if (panicNote_) {
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
