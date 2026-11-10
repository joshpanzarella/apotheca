#include "app.hpp"

#include <cstdio>

namespace apotheca {

App::App(Engine& engine)
    : project_(new Project), session_(engine, *project_), song_(session_), drums_(session_), notes_(session_),
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
        } else if (event.down && event.button == Button::Start && !input.held(Button::Select)) {
            session_.togglePlay(false);
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
        else drawKit(renderer, fonts);
        break;
    }
}

// The drum kit has no settings of its own yet: each drum's volume lives on
// the Pattern screen. This says so, and shows the kit's output.
void App::drawKit(SDL_Renderer* r, const Fonts& fonts) const {
    using namespace ui;
    char text[48], name[24];
    trackName(*project_, project_->track, name, sizeof name);
    std::snprintf(text, sizeof text, "%s / KIT", name);
    topBar(r, fonts, View::Device, text, *project_);

    fonts.medium.draw(r, 8.f, 40.f, "THE BUILT-IN KIT", kText);
    for (int lane = 0; lane < kDrumLanes; ++lane) {
        const int x = 8 + (lane % 4) * 157, y = 66 + (lane / 4) * 62;
        const float level = project_->current().laneLevel[lane];
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(level * 100.f + 0.5f));
        valueBox(r, fonts, x, y, 153, 56, DrumKit::laneName(lane), text, level, false, false);
    }
    fonts.small.draw(r, 8.f, 198.f, "Each drum's volume is set on the PATTERN screen: put the cursor on its name.", kDim);
    fonts.small.draw(r, 8.f, 214.f, "Loading your own samples into the kit is not built yet.", kDim);
    fonts.small.draw(r, 8.f, 246.f, "LIVE OUTPUT", kDim);
    scope(r, session_.engine, 8, 262, 624, 184);

    const Hint list[] = {{"L2", "pattern"}, {"START", project_->playing ? "stop" : "play"}};
    hints(r, fonts, list, 2);
}

} // namespace apotheca
