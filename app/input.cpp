#include "input.hpp"

#include "controls.hpp"

namespace apotheca {

namespace {

// SDL names gamepad buttons by POSITION, using the Xbox layout: its "A" is the
// bottom button. Anbernic prints the Nintendo layout: A is on the right, B at
// the bottom, X on top, Y on the left. So the letters swap here, once.
Button fromPad(Uint8 b) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return Button::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Button::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Button::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Button::Right;
    case SDL_CONTROLLER_BUTTON_B: return Button::A; // right
    case SDL_CONTROLLER_BUTTON_A: return Button::B; // bottom
    case SDL_CONTROLLER_BUTTON_Y: return Button::X; // top
    case SDL_CONTROLLER_BUTTON_X: return Button::Y; // left
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Button::L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Button::R1;
    case SDL_CONTROLLER_BUTTON_START: return Button::Start;
    case SDL_CONTROLLER_BUTTON_BACK: return Button::Select;
    case SDL_CONTROLLER_BUTTON_GUIDE: return Button::Menu;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return Button::L3;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return Button::R3;
    default: return Button::None;
    }
}

// Keyboard stand-ins, for testing without the handheld.
Button fromKey(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return Button::Up;
    case SDLK_DOWN: return Button::Down;
    case SDLK_LEFT: return Button::Left;
    case SDLK_RIGHT: return Button::Right;
    case SDLK_x: return Button::A;
    case SDLK_z: return Button::B;
    case SDLK_s: return Button::X;
    case SDLK_a: return Button::Y;
    case SDLK_q: return Button::L1;
    case SDLK_w: return Button::R1;
    case SDLK_1: return Button::L2;
    case SDLK_2: return Button::R2;
    case SDLK_RETURN: return Button::Start;
    case SDLK_BACKSPACE: return Button::Select;
    case SDLK_TAB: return Button::Menu;
    default: return Button::None;
    }
}

// The stick on I J K L. Returns the axis index (0 = x, 1 = y) or -1.
int stickKey(SDL_Keycode k, float& direction) {
    switch (k) {
    case SDLK_j: direction = -1.f; return 0;
    case SDLK_l: direction = 1.f; return 0;
    case SDLK_i: direction = -1.f; return 1;
    case SDLK_k: direction = 1.f; return 1;
    default: return -1;
    }
}

} // namespace

bool Input::set(Button button, bool down, ButtonEvent& out) {
    if (button == Button::None) return false;
    bool& state = held_[static_cast<int>(button)];
    if (state == down) return false;
    state = down;
    out.button = button;
    out.down = down;
    return true;
}

float Input::shaped(float v) {
    const float dead = 0.2f;
    if (v > dead) return (v - dead) / (1.f - dead);
    if (v < -dead) return (v + dead) / (1.f - dead);
    return 0.f;
}

// With learned controls: whatever the device sends is looked up in the map.
// One event can only report one button changing, so the screens get the
// first that changed; a hat moving from one direction straight to another
// lets go of the old one first and presses the new one on the next event.
bool Input::handleMapped(const SDL_Event& e, ButtonEvent& out) {
    const ControlMap& m = *map_;
    const int count = static_cast<int>(Button::Count);
    if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) {
        for (int i = 0; i < count; ++i)
            if (m.button[i].kind == RawControl::JoyButton && m.button[i].index == e.jbutton.button)
                return set(static_cast<Button>(i), e.type == SDL_JOYBUTTONDOWN, out);
        return false;
    }
    if (e.type == SDL_JOYHATMOTION) {
        if (e.jhat.hat < 8) hat_[e.jhat.hat] = e.jhat.value;
        bool changed = false;
        for (int i = 0; i < count && !changed; ++i)
            if (m.button[i].kind == RawControl::JoyHat && m.button[i].index == e.jhat.hat && !(e.jhat.value & m.button[i].value))
                changed = set(static_cast<Button>(i), false, out); // let go first
        for (int i = 0; i < count && !changed; ++i)
            if (m.button[i].kind == RawControl::JoyHat && m.button[i].index == e.jhat.hat && (e.jhat.value & m.button[i].value))
                changed = set(static_cast<Button>(i), true, out);
        return changed;
    }
    if (e.type == SDL_JOYAXISMOTION) {
        // The stick.
        if (m.stickRight.kind == RawControl::JoyAxis && m.stickRight.index == e.jaxis.axis) axes_[0] = static_cast<float>(e.jaxis.value - m.stickRight.rest) / 32767.f * static_cast<float>(m.stickRight.value);
        if (m.stickDown.kind == RawControl::JoyAxis && m.stickDown.index == e.jaxis.axis) axes_[1] = static_cast<float>(e.jaxis.value - m.stickDown.rest) / 32767.f * static_cast<float>(m.stickDown.value);
        // An axis that is a button (a trigger, or a D-pad that reports as two axes).
        bool changed = false;
        for (int i = 0; i < count && !changed; ++i) {
            const RawControl& c = m.button[i];
            if (c.kind != RawControl::JoyAxis || c.index != e.jaxis.axis) continue;
            const int moved = (e.jaxis.value - c.rest) * c.value;
            const Button b = static_cast<Button>(i);
            changed = set(b, held(b) ? moved > 9000 : moved > 16000, out); // two thresholds, so a half press cannot chatter
        }
        return changed;
    }
    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        if (e.key.repeat) return false;
        for (int i = 0; i < count; ++i)
            if (m.button[i].kind == RawControl::Key && m.button[i].index == static_cast<int>(e.key.keysym.sym))
                return set(static_cast<Button>(i), e.type == SDL_KEYDOWN, out);
    }
    return false;
}

bool Input::handle(const SDL_Event& event, ButtonEvent& out) {
    if (map_) {
        if (handleMapped(event, out)) return true;
        // Nothing learned for it. SDL's own gamepad reading is ignored now
        // (it would press things twice); the keyboard stand-ins still work.
        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) return false;
    }
    switch (event.type) {
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        return set(fromPad(event.cbutton.button), event.type == SDL_CONTROLLERBUTTONDOWN, out);

    case SDL_CONTROLLERAXISMOTION: {
        const float v = event.caxis.value / 32767.f;
        switch (event.caxis.axis) {
        case SDL_CONTROLLER_AXIS_LEFTX: axes_[0] = v; break;
        case SDL_CONTROLLER_AXIS_LEFTY: axes_[1] = v; break;
        case SDL_CONTROLLER_AXIS_RIGHTX: axes_[2] = v; break;
        case SDL_CONTROLLER_AXIS_RIGHTY: axes_[3] = v; break;
        // L2 and R2 arrive as axes. Two thresholds so a half press cannot chatter.
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            return set(Button::L2, held(Button::L2) ? v > 0.3f : v > 0.6f, out);
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            return set(Button::R2, held(Button::R2) ? v > 0.3f : v > 0.6f, out);
        default: break;
        }
        return false;
    }

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (event.key.repeat) return false;
        const bool down = event.type == SDL_KEYDOWN;
        float direction = 0.f;
        const int axis = stickKey(event.key.keysym.sym, direction);
        if (axis >= 0) {
            if (down) axes_[axis] = direction;
            else if (axes_[axis] == direction) axes_[axis] = 0.f;
            return false;
        }
        return set(fromKey(event.key.keysym.sym), down, out);
    }

    default:
        return false;
    }
}

const char* buttonName(Button button) {
    switch (button) {
    case Button::Up: return "UP";
    case Button::Down: return "DOWN";
    case Button::Left: return "LEFT";
    case Button::Right: return "RIGHT";
    case Button::A: return "A";
    case Button::B: return "B";
    case Button::X: return "X";
    case Button::Y: return "Y";
    case Button::L1: return "L1";
    case Button::R1: return "R1";
    case Button::L2: return "L2";
    case Button::R2: return "R2";
    case Button::Start: return "START";
    case Button::Select: return "SELECT";
    case Button::Menu: return "MENU";
    case Button::L3: return "L3";
    case Button::R3: return "R3";
    default: return "";
    }
}

} // namespace apotheca
