#include "input.hpp"

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

// Left stick on T F G H, right stick on I J K L. Returns the axis index
// (0 = left x, 1 = left y, 2 = right x, 3 = right y) or -1.
int stickKey(SDL_Keycode k, float& direction) {
    switch (k) {
    case SDLK_f: direction = -1.f; return 0;
    case SDLK_h: direction = 1.f; return 0;
    case SDLK_t: direction = -1.f; return 1;
    case SDLK_g: direction = 1.f; return 1;
    case SDLK_j: direction = -1.f; return 2;
    case SDLK_l: direction = 1.f; return 2;
    case SDLK_i: direction = -1.f; return 3;
    case SDLK_k: direction = 1.f; return 3;
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

bool Input::handle(const SDL_Event& event, ButtonEvent& out) {
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
