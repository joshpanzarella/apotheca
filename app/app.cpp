#include "app.hpp"

#include <cstdio>

namespace apotheca {

App::App(Engine& engine)
    : project_(new Project), session_(engine, *project_), song_(session_), drums_(session_), notes_(session_), kit_(session_),
      phazed_(session_, phazedSpec()), twin_(session_, twinSpec()) {}

void App::onButton(const ButtonEvent& event, const Input& input) {
    // The shoulder triggers walk along the tabs. Nothing else uses them.
    if (event.button == Button::L2 || event.button == Button::R2) {
        if (!event.down || input.held(Button::A) || input.held(Button::Y)) return;
        const int now = static_cast<int>(session_.view);
        const int next = now + (event.button == Button::R2 ? 1 : -1);
        if (next >= 0 && next <= static_cast<int>(View::Device)) session_.view = static_cast<View>(next);
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
        } else {
            kit_.onButton(event, input);
        }
        break;
    }
}

void App::update(const Input& input, Uint32 nowMs) {
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
        else kit_.update(input, nowMs);
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
        else kit_.draw(renderer, fonts);
        break;
    }
}

} // namespace apotheca
