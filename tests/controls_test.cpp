// Tests for the controls setup: learning what a device sends for each
// button, keeping it, and reading buttons through it. The "device" is a
// stream of the events SDL would deliver. And the rest between two passes
// (pass_wait.hpp: a press ends it, nothing else does) and a press's own time
// (ButtonEvent::ms, for TAP TEMPO): gesture audit G06.
//
// `controls_test --measure` times main.cpp's loop as a model on this desk: the rest
// alone (one SDL_Delay(16) before, the four naps now) and how long a press waits
// to be read, pressed at random beside passes of 1.7 ms of work.

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "controls.hpp"
#include "input.hpp"
#include "joysticks.hpp"
#include "pass_wait.hpp"

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
        while (input.pending(press)) out.push_back(press); // as main.cpp asks after every event
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
    // The D-pad: a hat. Moving from one way straight to another lets go of the first, then presses
    // the second, from the one event (a hat sends no second event for it).
    CHECK(is(feed(input, {hat(SDL_HAT_UP)}), {{Button::Up, true}}));
    CHECK(is(feed(input, {hat(SDL_HAT_RIGHT)}), {{Button::Up, false}, {Button::Right, true}}));
    CHECK(feed(input, {hat(SDL_HAT_RIGHT)}).empty());
    CHECK(is(feed(input, {hat(SDL_HAT_CENTERED)}), {{Button::Right, false}}));
    // A diagonal: both pressed, and both let go when it comes back to the middle (RIGHT stayed held).
    CHECK(is(feed(input, {hat(SDL_HAT_RIGHTUP)}), {{Button::Up, true}, {Button::Right, true}}));
    CHECK(is(feed(input, {hat(SDL_HAT_CENTERED)}), {{Button::Up, false}, {Button::Right, false}}) && !input.held(Button::Right));
    CHECK(is(feed(input, {hat(SDL_HAT_LEFTDOWN), hat(SDL_HAT_RIGHTUP)}),
             {{Button::Down, true}, {Button::Left, true}, {Button::Down, false}, {Button::Left, false}, {Button::Up, true}, {Button::Right, true}}));
    feed(input, {hat(SDL_HAT_CENTERED)});
    // Every button let go at once (a gamepad gone): each that was held is said, the stick at rest.
    feed(input, {joyButton(3, true), hat(SDL_HAT_LEFT), axis(0, 30000)});
    CHECK(input.held(Button::A) && input.held(Button::Left) && input.stickX() > 0.5f);
    ButtonEvent ups[static_cast<int>(Button::Count)];
    int many = 0;
    input.letGoOfAll(ups, many);
    CHECK(many == 2 && !input.anyHeld() && input.stickX() == 0.f);
    CHECK(feed(input, {hat(SDL_HAT_CENTERED)}).empty()); // (the hat is at rest too: nothing to let go again)
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

// ---- a press's own time, and the rest between passes (gesture audit G06) ------------------------

SDL_Event stamped(SDL_Event e, Uint32 ms) {
    e.common.timestamp = ms;
    return e;
}
SDL_Event padButton(int button, bool down) {
    SDL_Event e = {};
    e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
    e.cbutton.button = static_cast<Uint8>(button);
    return e;
}
SDL_Event padAxis(int which, int value) {
    SDL_Event e = {};
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.axis = static_cast<Uint8>(which);
    e.caxis.value = static_cast<Sint16>(value);
    return e;
}

// A press carries the time SDL stamped on its event (TAP TEMPO times a tap by it); one made up has none.
void testAPressHasItsOwnTime() {
    Input keys;
    std::vector<ButtonEvent> got = feed(keys, {stamped(keyEvent(SDLK_x, true), 1234), stamped(keyEvent(SDLK_x, false), 1300)});
    CHECK(is(got, {{Button::A, true}, {Button::A, false}}) && got[0].ms == 1234 && got[1].ms == 1300);
    // SDL's own gamepad: a button, and a trigger that comes as an axis.
    Input pad;
    got = feed(pad, {stamped(padButton(SDL_CONTROLLER_BUTTON_B, true), 2000), stamped(padAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000), 2017)});
    CHECK(is(got, {{Button::A, true}, {Button::L2, true}}) && got[0].ms == 2000 && got[1].ms == 2017);
    // Learned controls: a hat that moved two at once hands the second on (pending) at the same time.
    const ControlMap map = learnTheHandheld();
    Input handheld;
    handheld.useMap(&map);
    got = feed(handheld, {stamped(hat(SDL_HAT_UP | SDL_HAT_RIGHT), 3000), stamped(hat(SDL_HAT_CENTERED), 3050), stamped(axis(5, 32767), 3070)});
    CHECK(is(got, {{Button::Up, true}, {Button::Right, true}, {Button::Up, false}, {Button::Right, false}, {Button::R2, true}}));
    CHECK(got.size() == 5 && got[0].ms == 3000 && got[1].ms == 3000 && got[2].ms == 3050 && got[3].ms == 3050 && got[4].ms == 3070);
    // A let-go made up for a gamepad gone has no time, and whenPressed then takes the pass's clock.
    ButtonEvent ups[static_cast<int>(Button::Count)];
    for (ButtonEvent& up : ups) up.ms = 99;
    int many = 0;
    handheld.letGoOfAll(ups, many);
    CHECK(many == 1 && ups[0].button == Button::R2 && ups[0].ms == 0);
    CHECK(whenPressed(ups[0], 777) == 777 && whenPressed(got[4], 777) == 3070);
}

// SDL as the rest sees it, with a pretend clock: an event due at a time is on the queue from then.
struct PretendQueue {
    Uint32 now = 0;
    std::vector<SDL_Event> due; // in the order they come, each stamped with when
    std::vector<Uint32> naps;
    int looks = 0;
    void at(Uint32 ms, SDL_Event e) { // (kept in time order, the same times in the order given)
        std::size_t i = due.size();
        while (i > 0 && due[i - 1].common.timestamp > ms) --i;
        due.insert(due.begin() + static_cast<std::ptrdiff_t>(i), stamped(e, ms));
    }
    int rest(PassWait& wait, const Input& input) {
        return wait.rest(
            input, [&](Uint32 ms) { naps.push_back(ms), now += ms; },
            [&](std::vector<SDL_Event>& into) {
                ++looks;
                while (!due.empty() && due.front().common.timestamp <= now) into.push_back(due.front()), due.erase(due.begin());
            });
    }
};
// What the next pass reads from the rest (at most `most`, so a fault cannot go round for ever).
std::vector<SDL_Event> readKept(PassWait& wait, std::size_t most = 100) {
    std::vector<SDL_Event> read;
    SDL_Event e;
    while (wait.waiting() > 0 && read.size() < most && wait.next(e)) read.push_back(e);
    return read;
}

void testTheRestEndsOnAPressAndOnNothingElse() {
    const ControlMap map = learnTheHandheld(); // the D-pad a hat, A joystick button 3, the triggers axes 2 and 5, the stick 0 and 1
    Input handheld;
    handheld.useMap(&map);
    // Nothing comes: four naps of 4 ms, the 16 ms rest it always was (the owner's, 2026-11-15), looking before each.
    {
        PassWait wait;
        PretendQueue q;
        CHECK(q.rest(wait, handheld) == PassWait::kNaps);
        CHECK(q.naps == std::vector<Uint32>({4, 4, 4, 4}) && q.now == 16 && q.looks == 4);
    }
    // No press: the stick's motion (a resting stick that jitters, and one pushed hard), a trigger short of its
    // threshold, a key's repeat, the window, the mouse. The whole rest, and all of it kept for the pass, in order.
    {
        PassWait wait;
        PretendQueue q;
        for (Uint32 ms = 0; ms < 16; ++ms) q.at(ms, axis(static_cast<int>(ms % 2), ms % 3 == 0 ? 32000 : -300));
        q.at(5, axis(2, -20000)); // L2's trigger, resting at -32768: 12768 up, short of 16000
        SDL_Event repeat = keyEvent(SDLK_x, true);
        repeat.key.repeat = 1;
        q.at(6, repeat);
        SDL_Event window = {};
        window.type = SDL_WINDOWEVENT;
        q.at(7, window);
        SDL_Event mouse = {};
        mouse.type = SDL_MOUSEMOTION;
        q.at(9, mouse);
        CHECK(q.rest(wait, handheld) == PassWait::kNaps && q.now == 16);
        const std::size_t taken = 13 + 4; // the stick's at 0 to 12 (the last look is at 12), and the four others
        CHECK(wait.waiting() == taken && q.due.size() == 3); // (those at 13 to 15 the pass reads itself)
        const std::vector<SDL_Event> read = readKept(wait);
        bool inOrder = read.size() == taken;
        for (std::size_t i = 1; i < read.size(); ++i) inOrder = inOrder && read[i].common.timestamp >= read[i - 1].common.timestamp;
        CHECK(inOrder && read[6].type == SDL_JOYAXISMOTION && read[6].jaxis.axis == 2 && read[8].type == SDL_KEYDOWN && read[10].type == SDL_WINDOWEVENT &&
              read[13].type == SDL_MOUSEMOTION);
        CHECK(wait.waiting() == 0);
    }
    // A press 6 ms in: seen at the look after the second nap (8 ms), and the rest ends there. The pass reads it
    // then, with what came before it and beside it, in order; what has not come yet waits for SDL.
    {
        PassWait wait;
        PretendQueue q;
        q.at(1, axis(0, 20000));
        q.at(6, joyButton(3, true)); // A
        q.at(7, axis(1, 100));
        q.at(9, joyButton(3, false));
        CHECK(q.rest(wait, handheld) == 2 && q.now == 8 && q.looks == 3 && q.due.size() == 1);
        // The rest only looked: the program's Input has not been touched. The pass reads the press, at its time.
        CHECK(!handheld.held(Button::A));
        const std::vector<SDL_Event> read = readKept(wait);
        CHECK(read.size() == 3 && read[0].type == SDL_JOYAXISMOTION && read[1].type == SDL_JOYBUTTONDOWN && read[2].type == SDL_JOYAXISMOTION);
        const std::vector<ButtonEvent> got = feed(handheld, read);
        CHECK(is(got, {{Button::A, true}}) && got[0].ms == 6);
        // The next rest looks only at what is new: what the pass read is not a press again (A let go meanwhile).
        feed(handheld, {joyButton(3, false)});
        q.due.clear();
        CHECK(q.rest(wait, handheld) == PassWait::kNaps);
        feed(handheld, {joyButton(3, true)}); // (held again, for the next part)
    }
    // A press that came while the pass was at work (here a let-go: A is held now): no rest at all.
    {
        PassWait wait;
        PretendQueue q;
        q.at(0, joyButton(3, false));
        CHECK(q.rest(wait, handheld) == 0 && q.naps.empty() && q.looks == 1 && wait.waiting() == 1);
        CHECK(is(feed(handheld, readKept(wait)), {{Button::A, false}}));
    }
    // Every kind of press ends it: the D-pad's hat, a trigger past its threshold, a volume key, a keyboard
    // stand-in, and with no learned controls SDL's own gamepad (a button, a trigger).
    {
        Input sdlPad;
        SDL_Event volume = keyEvent(SDLK_VOLUMEDOWN, true);
        const std::vector<std::pair<const Input*, SDL_Event>> presses = {
            {&handheld, hat(SDL_HAT_LEFT)}, {&handheld, axis(5, 0)}, {&handheld, volume}, {&handheld, keyEvent(SDLK_RETURN, true)},
            {&sdlPad, padButton(SDL_CONTROLLER_BUTTON_START, true)}, {&sdlPad, padAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 25000)}};
        for (const auto& press : presses) {
            PassWait wait;
            PretendQueue q;
            q.at(10, press.second);
            CHECK(q.rest(wait, *press.first) == 3 && q.now == 12);
        }
        // And not those that are no press there: the stick on SDL's gamepad, R2 half pressed, a key let go that was not down.
        for (const SDL_Event& none : {padAxis(SDL_CONTROLLER_AXIS_LEFTX, 32000), padAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 12000), keyEvent(SDLK_z, false)}) {
            PassWait wait;
            PretendQueue q;
            q.at(10, none);
            CHECK(q.rest(wait, sdlPad) == PassWait::kNaps);
        }
    }
    // A let-go of a button the program holds ends it; the trigger easing off but still past its let-go threshold does not.
    {
        feed(handheld, {axis(2, 32767)}); // L2 held
        CHECK(handheld.held(Button::L2));
        PassWait wait;
        PretendQueue q;
        q.at(2, axis(2, -20000)); // 12768 up: held still (let go under 9000)
        q.at(5, axis(2, -32768)); // let go
        CHECK(q.rest(wait, handheld) == 2 && handheld.held(Button::L2));
    }
}

// The rest with SDL itself: its queue, its clock, a gamepad of its own make-believe read by SDL's pump.
void testTheRestWithSdl() {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::printf("  (no SDL events here: %s)\n", SDL_GetError());
        return;
    }
    SDL_Event e;
    const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1);
    SDL_GameController* pad = index >= 0 ? SDL_GameControllerOpen(index) : nullptr;
    CHECK(pad != nullptr);
    while (SDL_PollEvent(&e)) {} // what SDL said as it started, and the pad arriving
    Input input;
    PassWait wait;
    // A press already on the queue: no rest at all, and the pass reads it with SDL's stamp.
    SDL_Event press = keyEvent(SDLK_x, true);
    SDL_PushEvent(&press);
    CHECK(wait.rest(input) == 0);
    ButtonEvent got;
    CHECK(wait.next(e) && e.type == SDL_KEYDOWN && input.handle(e, got) && got.button == Button::A && got.ms == e.common.timestamp);
    CHECK(!wait.next(e));
    feed(input, {keyEvent(SDLK_x, false)}); // (A let go)
    if (pad) {
        SDL_Joystick* stick = SDL_GameControllerGetJoystick(pad);
        const SDL_GameControllerButtonBind leftX = SDL_GameControllerGetBindForAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        const SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForButton(pad, SDL_CONTROLLER_BUTTON_B); // the handheld's A
        CHECK(leftX.bindType == SDL_CONTROLLER_BINDTYPE_AXIS && b.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON);
        // The stick pushed: the whole rest (16 ms by SDL's clock), and its motion, read by the rest's own pump, kept for the pass.
        SDL_JoystickSetVirtualAxis(stick, leftX.value.axis, 30000);
        const Uint32 from = SDL_GetTicks();
        CHECK(wait.rest(input) == PassWait::kNaps);
        CHECK(SDL_GetTicks() - from >= PassWait::kRestMs);
        CHECK(wait.waiting() >= 1);
        bool moved = false;
        while (wait.next(e)) moved = moved || (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX && e.caxis.value == 30000);
        CHECK(moved);
        // A button pressed on it: the rest's pump reads it, and the rest ends at once.
        SDL_JoystickSetVirtualButton(stick, b.value.button, 1);
        CHECK(wait.rest(input) == 0);
        bool pressed = false;
        while (wait.next(e))
            if (input.handle(e, got) && got.button == Button::A && got.down) pressed = got.ms == e.common.timestamp && got.ms != 0;
        CHECK(pressed && input.held(Button::A));
        SDL_GameControllerClose(pad);
        SDL_JoystickDetachVirtual(index);
        while (SDL_PollEvent(&e)) {}
    }
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
#endif
}

// --measure: main.cpp's loop as a model on this desk. Passes of `workMs` of work (the desk draws a screen in
// 1.6 to 1.7 ms, docs/dev/measured.md), each ending in the old rest (SDL_Delay(16)) or the new; a helper thread
// presses A at random. Says the pass's length with nothing pressed, and how long a press waits to be read.
void measureTheLoop() {
    if (SDL_InitSubSystem(SDL_INIT_EVENTS) != 0) {
        std::printf("no SDL events here: %s\n", SDL_GetError());
        return;
    }
    using Clock = std::chrono::steady_clock;
    const auto busy = [](double ms) {
        const Clock::time_point until = Clock::now() + std::chrono::microseconds(static_cast<long>(ms * 1000.0));
        while (Clock::now() < until) {}
    };
    const auto sinceMs = [](Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); };
    for (const double workMs : {1.7, 25.0}) {
        for (const bool naps : {false, true}) {
            Input input;
            PassWait wait;
            SDL_Event e;
            while (SDL_PollEvent(&e)) {}
            const auto pass = [&](std::vector<double>* waited, const std::vector<Clock::time_point>* pushed) {
                while (naps ? wait.next(e) : SDL_PollEvent(&e) == 1) {
                    ButtonEvent got;
                    if (input.handle(e, got) && got.down && waited && e.key.windowID < pushed->size())
                        waited->push_back(sinceMs((*pushed)[e.key.windowID]));
                }
                busy(workMs);
                if (naps) wait.rest(input);
                else SDL_Delay(16);
            };
            // Nothing pressed: the pass's length.
            const Clock::time_point idleFrom = Clock::now();
            const int idlePasses = 60;
            for (int i = 0; i < idlePasses; ++i) pass(nullptr, nullptr);
            const double passMs = sinceMs(idleFrom) / idlePasses;
            // A pressed and let go 150 times, 30 to 90 ms apart.
            const int presses = 150;
            std::vector<Clock::time_point> pushed(presses);
            std::vector<double> waited;
            std::atomic<bool> done{false};
            std::thread presser([&] {
                std::mt19937 dice(7);
                std::uniform_int_distribution<int> gap(30000, 90000);
                for (int i = 0; i < presses; ++i) {
                    std::this_thread::sleep_for(std::chrono::microseconds(gap(dice)));
                    SDL_Event down = keyEvent(SDLK_x, true), up = keyEvent(SDLK_x, false);
                    down.key.windowID = static_cast<Uint32>(i);
                    up.key.windowID = static_cast<Uint32>(presses);
                    pushed[static_cast<std::size_t>(i)] = Clock::now();
                    SDL_PushEvent(&down);
                    SDL_PushEvent(&up);
                }
                done = true;
            });
            while (!done) pass(&waited, &pushed);
            pass(&waited, &pushed);
            presser.join();
            double sum = 0.0, worst = 0.0;
            for (double w : waited) sum += w, worst = w > worst ? w : worst;
            std::printf("work %4.1f ms, %-26s a pass with nothing pressed %5.1f ms; a press read after %4.1f ms on average, %4.1f at worst (%d read)\n", workMs,
                        naps ? "the rest in four naps:" : "one SDL_Delay(16) (before):", passMs, waited.empty() ? 0.0 : sum / static_cast<double>(waited.size()), worst,
                        static_cast<int>(waited.size()));
        }
    }
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

} // namespace

// A gamepad that goes and comes back is read again (review 2026-11-23: opened once at the start, a
// gamepad that dropped out was never read again). SDL's own make-believe joystick, attached and taken
// away: each arrival opened once (SDL says ADDED again for one already open), each going closed.
static std::vector<Joysticks::Change> pump(Joysticks& sticks) {
    std::vector<Joysticks::Change> seen;
    SDL_PumpEvents();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        const Joysticks::Change c = sticks.handle(e);
        if (c != Joysticks::Change::None) seen.push_back(c);
    }
    return seen;
}
void testAGamepadComesBack() {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::printf("  (no joysticks here: %s)\n", SDL_GetError());
        return;
    }
    for (const bool raw : {true, false}) {
        Joysticks sticks;
        sticks.raw = raw;
        pump(sticks);
        const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1);
        CHECK(index >= 0);
        std::vector<Joysticks::Change> seen = pump(sticks);
        CHECK(seen.size() == 1 && seen[0] == Joysticks::Change::Added);
        CHECK(raw ? sticks.opened.size() == 1 : sticks.pad != nullptr);
        // SDL saying ADDED again for one that is open opens nothing.
        SDL_Event again = {};
        again.type = SDL_JOYDEVICEADDED;
        again.jdevice.which = index;
        CHECK(sticks.handle(again) == Joysticks::Change::None && (raw ? sticks.opened.size() == 1 : true));
        CHECK(SDL_JoystickDetachVirtual(index) == 0);
        seen = pump(sticks);
        CHECK(seen.size() == 1 && seen[0] == Joysticks::Change::Removed);
        CHECK(sticks.opened.empty() && sticks.pad == nullptr);
        // Back again: read again.
        const int back = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1);
        seen = pump(sticks);
        CHECK(seen.size() == 1 && seen[0] == Joysticks::Change::Added && (raw ? sticks.opened.size() == 1 : sticks.pad != nullptr));
        sticks.closeAll();
        SDL_JoystickDetachVirtual(back);
        pump(sticks);
    }
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
#endif
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
        measureTheLoop();
        return 0;
    }
    testLearningAndUsingTheHandheld();
    testATriggerWhoseRestSdlKnows();
    testTriggersNobodyToldUsAbout();
    testTheSetupDoesNotTakeAWrongAnswer();
    testAPressHasItsOwnTime();
    testTheRestEndsOnAPressAndOnNothingElse();
    testTheRestWithSdl();
    testAGamepadComesBack();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("controls tests passed\n");
    return 0;
}
