// Tests for the controls setup: learning what a device sends for each
// button, keeping it, and reading buttons through it. The "device" is a
// stream of the events SDL would deliver.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "controls.hpp"
#include "input.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

SDL_Event joyButton(int button, bool down) {
    SDL_Event e = {};
    e.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
    e.jbutton.button = static_cast<Uint8>(button);
    return e;
}
SDL_Event hat(int value, int which = 0) {
    SDL_Event e = {};
    e.type = SDL_JOYHATMOTION;
    e.jhat.hat = static_cast<Uint8>(which);
    e.jhat.value = static_cast<Uint8>(value);
    return e;
}
SDL_Event axis(int which, int value) {
    SDL_Event e = {};
    e.type = SDL_JOYAXISMOTION;
    e.jaxis.axis = static_cast<Uint8>(which);
    e.jaxis.value = static_cast<Sint16>(value);
    return e;
}
SDL_Event keyEvent(SDL_Keycode code, bool down) {
    SDL_Event e = {};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = code;
    return e;
}

// Answer the step the learner is on with this press, and let go.
void answer(ControlLearner& learner, const SDL_Event& press, const SDL_Event& release) {
    learner.handle(press);
    learner.handle(release);
}

// A handheld like the one this is for, as its firmware might present it: the
// D-pad as a hat, face and shoulder buttons numbered, the two triggers as
// axes that rest at one end, the stick on axes 0 and 1.
ControlMap learnTheHandheld() {
    ControlLearner learner;
    learner.update(1000);
    // In the first moments nothing is an answer; an axis that speaks is saying where it rests.
    learner.handle(axis(2, -32768));
    learner.handle(axis(5, -32768));
    learner.handle(joyButton(3, true));
    learner.handle(joyButton(3, false));
    CHECK(learner.step() == 0);
    learner.update(2000);
    CHECK(std::string(learner.asking()) == "UP" && !learner.done() && learner.step() == 0);
    answer(learner, hat(SDL_HAT_UP), hat(SDL_HAT_CENTERED));
    CHECK(std::string(learner.asking()) == "DOWN");
    answer(learner, hat(SDL_HAT_DOWN), hat(SDL_HAT_CENTERED));
    answer(learner, hat(SDL_HAT_LEFT), hat(SDL_HAT_CENTERED));
    answer(learner, hat(SDL_HAT_RIGHT), hat(SDL_HAT_CENTERED));
    CHECK(std::string(learner.asking()) == "A");
    const int faces[4] = {3, 4, 6, 5}; // A, B, X, Y as this firmware numbers them
    for (int b : faces) answer(learner, joyButton(b, true), joyButton(b, false));
    CHECK(std::string(learner.asking()) == "L1");
    answer(learner, joyButton(7, true), joyButton(7, false));
    answer(learner, joyButton(8, true), joyButton(8, false));
    // The triggers: axes that sit at the bottom of their travel and go to the top.
    CHECK(std::string(learner.asking()) == "L2");
    learner.handle(axis(2, -32768));
    CHECK(std::string(learner.asking()) == "L2"); // resting is not a press
    answer(learner, axis(2, 32767), axis(2, -32768));
    answer(learner, axis(5, 32767), axis(5, -32768));
    CHECK(std::string(learner.asking()) == "START");
    answer(learner, joyButton(10, true), joyButton(10, false));
    // On screen it is SEL, as everywhere. In controls.txt the word stays SELECT: files already saved must read.
    CHECK(std::string(learner.asking()) == "SEL");
    CHECK(std::string(buttonLabel(Button::Select)) == "SEL" && std::string(buttonName(Button::Select)) == "SELECT");
    CHECK(std::string(buttonLabel(Button::Start)) == "START" && std::string(buttonLabel(Button::L1)) == "L1");
    answer(learner, joyButton(9, true), joyButton(9, false));
    CHECK(learner.map().text().find("SELECT") != std::string::npos);
    CHECK(std::string(learner.asking()) == "MENU" && learner.canSkip());
    answer(learner, joyButton(11, true), joyButton(11, false));
    CHECK(std::string(learner.asking()) == "THE STICK, TO THE RIGHT");
    answer(learner, axis(0, 30000), axis(0, 0));
    CHECK(std::string(learner.asking()) == "THE STICK, DOWN" && !learner.done());
    answer(learner, axis(1, 30000), axis(1, 0));
    CHECK(learner.done());
    return learner.map();
}

std::vector<ButtonEvent> feed(Input& input, const std::vector<SDL_Event>& events) {
    std::vector<ButtonEvent> out;
    for (const SDL_Event& e : events) {
        ButtonEvent press;
        if (input.handle(e, press)) out.push_back(press);
    }
    return out;
}
bool is(const std::vector<ButtonEvent>& got, std::vector<std::pair<Button, bool>> want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < got.size(); ++i)
        if (got[i].button != want[i].first || got[i].down != want[i].second) return false;
    return true;
}

void testLearningAndUsingTheHandheld() {
    const ControlMap map = learnTheHandheld();
    Input input;
    input.useMap(&map);
    // Every button comes out as the one it was learned as.
    CHECK(is(feed(input, {joyButton(3, true), joyButton(3, false)}), {{Button::A, true}, {Button::A, false}}));
    CHECK(is(feed(input, {joyButton(4, true), joyButton(6, true), joyButton(5, true)}), {{Button::B, true}, {Button::X, true}, {Button::Y, true}}));
    CHECK(input.held(Button::B) && input.held(Button::X) && input.held(Button::Y) && !input.held(Button::A));
    feed(input, {joyButton(4, false), joyButton(6, false), joyButton(5, false)});
    CHECK(is(feed(input, {joyButton(7, true), joyButton(8, true), joyButton(10, true), joyButton(9, true), joyButton(11, true)}),
             {{Button::L1, true}, {Button::R1, true}, {Button::Start, true}, {Button::Select, true}, {Button::Menu, true}}));
    feed(input, {joyButton(7, false), joyButton(8, false), joyButton(10, false), joyButton(9, false), joyButton(11, false)});
    // The D-pad: a hat. Moving from one way straight to another lets go of the first.
    CHECK(is(feed(input, {hat(SDL_HAT_UP)}), {{Button::Up, true}}));
    CHECK(is(feed(input, {hat(SDL_HAT_RIGHT)}), {{Button::Up, false}}));
    CHECK(is(feed(input, {hat(SDL_HAT_RIGHT)}), {{Button::Right, true}}));
    CHECK(is(feed(input, {hat(SDL_HAT_CENTERED)}), {{Button::Right, false}}));
    CHECK(is(feed(input, {hat(SDL_HAT_DOWN), hat(SDL_HAT_CENTERED), hat(SDL_HAT_LEFT), hat(SDL_HAT_CENTERED)}),
             {{Button::Down, true}, {Button::Down, false}, {Button::Left, true}, {Button::Left, false}}));
    // The triggers: pressed past half-way, let go below a quarter, so a half press cannot chatter.
    CHECK(feed(input, {axis(2, -32768), axis(2, -20000)}).empty());
    CHECK(is(feed(input, {axis(2, -10000)}), {{Button::L2, true}}));
    CHECK(feed(input, {axis(2, -20000)}).empty() && input.held(Button::L2));
    CHECK(is(feed(input, {axis(2, -30000)}), {{Button::L2, false}}));
    CHECK(is(feed(input, {axis(5, 32767), axis(5, -32768)}), {{Button::R2, true}, {Button::R2, false}}));
    // The stick: right and down are positive, with the dead zone taken out.
    feed(input, {axis(0, 32767), axis(1, -32767)});
    CHECK(std::fabs(input.stickX() - 1.f) < 0.01f && std::fabs(input.stickY() + 1.f) < 0.01f);
    feed(input, {axis(0, 3000), axis(1, 0)});
    CHECK(input.stickX() == 0.f && input.stickY() == 0.f);
    // A button nobody was asked about does nothing. Nor does SDL's own gamepad reading now.
    CHECK(feed(input, {joyButton(15, true), joyButton(15, false)}).empty());
    SDL_Event pad = {};
    pad.type = SDL_CONTROLLERBUTTONDOWN;
    pad.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    CHECK(feed(input, {pad}).empty());
    // The keyboard stand-ins still work, for a desk.
    CHECK(is(feed(input, {keyEvent(SDLK_x, true), keyEvent(SDLK_x, false)}), {{Button::A, true}, {Button::A, false}}));

    // Kept as text and read back, it is the same map.
    const std::string text = map.text();
    ControlMap back;
    CHECK(back.parse(text) && back.text() == text);
    Input again;
    again.useMap(&back);
    CHECK(is(feed(again, {joyButton(3, true), hat(SDL_HAT_UP), axis(2, -32768), axis(2, 32767)}), {{Button::A, true}, {Button::Up, true}, {Button::L2, true}}));
    // What is not a map, or cannot move and act, is refused and changes nothing.
    ControlMap same = back;
    CHECK(!same.parse("") && !same.parse("nonsense") && !same.parse("apotheca-controls 1\n"));
    CHECK(!same.parse("apotheca-controls 1\nA button 1 0 0\nB button 2 0 0\n")); // no D-pad
    CHECK(same.text() == text);
    CHECK(same.parse("apotheca-controls 1\nUP key 1 0 0\nDOWN key 2 0 0\nLEFT key 3 0 0\nRIGHT key 4 0 0\nA key 5 0 0\nB key 6 0 0\njunk\nX axis 3 7 0\nY button 999 0 0\n"));
    CHECK(same.button[static_cast<int>(Button::X)].kind == RawControl::None && same.button[static_cast<int>(Button::Y)].kind == RawControl::None);
}

void testATriggerWhoseRestSdlKnows() {
    // SDL can usually say where an axis rests. Then the first thing the
    // trigger ever sends is the press itself, and it is still read right.
    ControlLearner learner;
    learner.setAxisRest(2, -32768);
    learner.update(100);
    learner.update(1000);
    answer(learner, axis(2, 32767), axis(2, -32768));
    CHECK(learner.step() == 1 && learner.map().button[static_cast<int>(Button::Up)].rest == -32768 && learner.map().button[static_cast<int>(Button::Up)].value == 1);
    // Without being told, an axis is taken to rest in the middle: a D-pad
    // axis that jumps straight to one end is that direction.
    answer(learner, axis(6, 32767), axis(6, 0));
    CHECK(learner.step() == 2 && learner.map().button[static_cast<int>(Button::Down)].rest == 0);
    learner.setAxisRest(99, 5); // no such axis: ignored
}

// The bug that left L2 "held for ever": triggers that rest at one end, on a
// device that does not say so beforehand.
void testTriggersNobodyToldUsAbout() {
    const auto begin = [](ControlLearner& learner) {
        learner.update(100);
        learner.update(1000);
    };
    const auto pressed = [](const ControlMap& map, std::vector<SDL_Event> events) {
        Input input;
        input.useMap(&map);
        std::vector<ButtonEvent> all = feed(input, events);
        return all;
    };
    // An analog trigger: it creeps up from the bottom of its travel to the top and back.
    {
        ControlLearner learner;
        begin(learner);
        for (int v : {-30000, -20000, 0, 20000, 32767, 20000, -10000, -32768}) learner.handle(axis(2, v));
        CHECK(learner.step() == 1);
        const RawControl up = learner.map().button[static_cast<int>(Button::Up)];
        CHECK(up.kind == RawControl::JoyAxis && up.rest == -32768 && up.value == 1);
        // In use: at rest it is NOT held; pressed it is; let go it is not again.
        const ControlMap map = learner.map();
        CHECK(pressed(map, {axis(2, -32768)}).empty());
        CHECK(is(pressed(map, {axis(2, -32768), axis(2, 32767), axis(2, -32768), axis(2, 32767)}),
                 {{Button::Up, true}, {Button::Up, false}, {Button::Up, true}}));
    }
    // A digital trigger: it jumps straight from one end to the other. Its
    // first word is the pressed end; where it goes next is where it rests.
    {
        ControlLearner learner;
        begin(learner);
        learner.handle(axis(5, 32767));
        CHECK(learner.step() == 0); // not yet: it could rest in the middle or at the other end
        learner.handle(axis(5, 32767)); // said twice, as some devices do: still pressed, not let go
        learner.handle(axis(5, 30000));
        CHECK(learner.step() == 0);
        learner.handle(axis(5, -32768));
        CHECK(learner.step() == 1);
        const RawControl up = learner.map().button[static_cast<int>(Button::Up)];
        CHECK(up.rest == -32768 && up.value == 1);
        // The press is over, so the very next press of something else is taken.
        answer(learner, joyButton(1, true), joyButton(1, false));
        CHECK(learner.step() == 2);
        const ControlMap map = learner.map();
        CHECK(is(pressed(map, {axis(5, -32768), axis(5, 32767), axis(5, -32768)}), {{Button::Up, true}, {Button::Up, false}}));
    }
    // A D-pad that reports as an axis: jumps to an end, and comes back to the middle.
    {
        ControlLearner learner;
        begin(learner);
        learner.handle(axis(6, -32768));
        learner.handle(axis(6, 0));
        CHECK(learner.step() == 1);
        const RawControl up = learner.map().button[static_cast<int>(Button::Up)];
        CHECK(up.rest == 0 && up.value == -1);
        // The other way on the same axis, now that its rest is known: at once.
        learner.handle(axis(6, 32767));
        CHECK(learner.step() == 2 && learner.map().button[static_cast<int>(Button::Down)].value == 1);
        learner.handle(axis(6, 0));
        const ControlMap map = learner.map();
        CHECK(is(pressed(map, {axis(6, -32768), axis(6, 0), axis(6, 32767), axis(6, 0)}),
                 {{Button::Up, true}, {Button::Up, false}, {Button::Down, true}, {Button::Down, false}}));
    }
    // A trigger resting at the TOP of its travel, creeping down: the same, the other way up.
    {
        ControlLearner learner;
        begin(learner);
        for (int v : {30000, 0, -32768, 0, 32767}) learner.handle(axis(3, v));
        const RawControl up = learner.map().button[static_cast<int>(Button::Up)];
        CHECK(learner.step() == 1 && up.rest == 32767 && up.value == -1);
    }
}

void testTheSetupDoesNotTakeAWrongAnswer() {
    ControlLearner learner;
    learner.update(1000);
    learner.update(2000);
    // Something let go, or a diagonal, is not an answer.
    learner.handle(joyButton(3, false));
    learner.handle(hat(SDL_HAT_UP | SDL_HAT_RIGHT));
    CHECK(learner.step() == 0);
    CHECK(learner.saw().find("hat") != std::string::npos); // but it shows that the device was heard
    learner.handle(hat(SDL_HAT_CENTERED));
    answer(learner, hat(SDL_HAT_UP), hat(SDL_HAT_CENTERED));
    // The same control twice: refused, with a reason; a different one is then taken.
    learner.handle(hat(SDL_HAT_UP));
    CHECK(learner.step() == 1 && std::string(learner.complaint()).find("ALREADY") != std::string::npos);
    learner.handle(hat(SDL_HAT_CENTERED));
    answer(learner, hat(SDL_HAT_DOWN), hat(SDL_HAT_CENTERED));
    CHECK(learner.step() == 2 && learner.complaint()[0] == 0);
    // A press held down answers one step only: it must be let go before the next.
    learner.handle(joyButton(1, true));
    CHECK(learner.step() == 3);
    learner.handle(joyButton(2, true)); // another finger, while the first is still down
    CHECK(learner.step() == 3);
    learner.handle(joyButton(1, false));
    learner.handle(joyButton(2, false));
    answer(learner, joyButton(2, true), joyButton(2, false));
    CHECK(learner.step() == 4);
    // The rest by keyboard (a firmware that sends keys): any mix is fine.
    for (int i = 0; i < 10; ++i) answer(learner, keyEvent(SDLK_a + i, true), keyEvent(SDLK_a + i, false));
    CHECK(std::string(learner.asking()) == "MENU");
    // MENU is often kept by the firmware. Nothing happens, and after eight seconds it is left out.
    learner.update(2000 + 7900);
    CHECK(std::string(learner.asking()) == "MENU" && learner.skipIn() > 0.f);
    learner.update(1000 + 20000);
    CHECK(std::string(learner.asking()) == "THE STICK, TO THE RIGHT");
    // A button is not a stick; the same axis twice is not two ways.
    learner.handle(joyButton(9, true));
    learner.handle(joyButton(9, false));
    CHECK(std::string(learner.asking()) == "THE STICK, TO THE RIGHT" && std::string(learner.complaint()).find("STICK") != std::string::npos);
    answer(learner, axis(3, -30000), axis(3, 0)); // on this device, right is the negative way
    CHECK(std::string(learner.asking()) == "THE STICK, DOWN");
    learner.handle(axis(3, 30000));
    CHECK(!learner.done() && std::string(learner.complaint()).find("SAME WAY") != std::string::npos);
    learner.handle(axis(3, 0));
    answer(learner, axis(4, 30000), axis(4, 0));
    CHECK(learner.done());
    const ControlMap map = learner.map();
    CHECK(map.button[static_cast<int>(Button::Menu)].kind == RawControl::None);
    Input input;
    input.useMap(&map);
    feed(input, {axis(3, -32767)});
    CHECK(std::fabs(input.stickX() - 1.f) < 0.01f); // right is right, whichever way the device counts
    CHECK(is(feed(input, {keyEvent(SDLK_a, true)}), {{Button::A, true}}));
    CHECK(is(feed(input, {joyButton(1, true), joyButton(2, true)}), {{Button::Left, true}, {Button::Right, true}}));

    // A device with no stick: both stick steps are left out after the wait.
    ControlLearner noStick;
    noStick.update(500);
    noStick.update(1200);
    for (int i = 0; i < 15; ++i) answer(noStick, joyButton(i, true), joyButton(i, false));
    CHECK(std::string(noStick.asking()) == "THE STICK, TO THE RIGHT");
    noStick.update(1200 + 9000);
    CHECK(noStick.done() && noStick.map().stickRight.kind == RawControl::None);
    // A D-pad that reports as two axes is learned as four directions.
    ControlLearner axes;
    axes.update(500);
    axes.update(1200);
    answer(axes, axis(6, -32767), axis(6, 0));
    answer(axes, axis(6, 32767), axis(6, 0));
    answer(axes, axis(7, -32767), axis(7, 0));
    answer(axes, axis(7, 32767), axis(7, 0));
    CHECK(axes.step() == 4);
    for (int i = 0; i < 11; ++i) answer(axes, joyButton(i, true), joyButton(i, false));
    axes.update(1200 + 9000);
    CHECK(axes.done());
    const ControlMap padMap = axes.map();
    Input pad;
    pad.useMap(&padMap);
    CHECK(is(feed(pad, {axis(6, -32767), axis(6, 0), axis(6, 32767), axis(6, 0)}), {{Button::Up, true}, {Button::Up, false}, {Button::Down, true}, {Button::Down, false}}));
    CHECK(is(feed(pad, {axis(7, 32767)}), {{Button::Right, true}}));
}

} // namespace

int main(int, char**) {
    testLearningAndUsingTheHandheld();
    testATriggerWhoseRestSdlKnows();
    testTriggersNobodyToldUsAbout();
    testTheSetupDoesNotTakeAWrongAnswer();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("controls tests passed\n");
    return 0;
}
