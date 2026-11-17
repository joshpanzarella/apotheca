/* padprobe: what SDL sees of the joysticks, the way Apotheca reads them.
 *
 *   padprobe            list every joystick SDL finds, then print each button,
 *                       axis and hat as it moves, until 30 s pass quietly
 *   padprobe -l         list them and stop
 *   padprobe -t SECONDS stop after this long with nothing pressed
 *
 * The numbers printed are the ones controls.txt holds ("button 1", "axis 0").
 * For bring-up from the USB serial shell: press each button and read its
 * number. Needs no screen (SDL's joystick part alone). */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    int listOnly = 0, quietSeconds = 30;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-l") == 0) listOnly = 1;
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) quietSeconds = atoi(argv[++i]);
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "padprobe: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* Joysticks that appear later (a fake one made after we start) come as
     * SDL_JOYDEVICEADDED events. */
    Uint32 lastHeard = SDL_GetTicks();
    int listed = 0;
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_JOYDEVICEADDED: {
                SDL_Joystick* j = SDL_JoystickOpen(e.jdevice.which);
                if (!j) break;
                char guid[64];
                SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), guid, sizeof guid);
                printf("joystick %d: \"%s\" guid %s buttons %d axes %d hats %d\n", (int)SDL_JoystickInstanceID(j),
                       SDL_JoystickName(j), guid, SDL_JoystickNumButtons(j), SDL_JoystickNumAxes(j),
                       SDL_JoystickNumHats(j));
                fflush(stdout);
                ++listed;
                lastHeard = SDL_GetTicks();
                break;
            }
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                printf("button %d %s\n", e.jbutton.button, e.type == SDL_JOYBUTTONDOWN ? "down" : "up");
                fflush(stdout);
                lastHeard = SDL_GetTicks();
                break;
            case SDL_JOYAXISMOTION:
                printf("axis %d %d\n", e.jaxis.axis, e.jaxis.value);
                fflush(stdout);
                lastHeard = SDL_GetTicks();
                break;
            case SDL_JOYHATMOTION:
                printf("hat %d %d\n", e.jhat.hat, e.jhat.value);
                fflush(stdout);
                lastHeard = SDL_GetTicks();
                break;
            default:
                break;
            }
        }
        if (listOnly && SDL_GetTicks() > 1000) break;
        if (!listOnly && SDL_GetTicks() - lastHeard > (Uint32)quietSeconds * 1000) break;
        SDL_Delay(5);
    }
    if (listed == 0) printf("no joystick\n");
    SDL_Quit();
    return 0;
}
