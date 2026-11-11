#include "platform.h"

/* One scene call sends a complete opaque frame. Do not mix legacy drawing
 * calls with platform_scene() in the same app_init/app_event entry. */
static uint32_t last_ms;
static uint32_t seconds;
static int running;
static scene_cmd_t commands[3];

static void label(scene_cmd_t *command) {
    static const char prefix[] = "Timer: ";
    static const char suffix[] = " s (tap: toggle)";
    unsigned n = 0;
    command->kind = SCENE_TEXT;
    command->rgb = 0xf7f5e9u;
    command->x = 40;
    command->y = 70;
    command->w = 400;
    command->h = 32;
    for (unsigned i = 0; i < sizeof(prefix) - 1; ++i) command->text[n++] = prefix[i];
    command->text[n++] = (char)('0' + (seconds / 10u) % 10u);
    command->text[n++] = (char)('0' + seconds % 10u);
    for (unsigned i = 0; i < sizeof(suffix) - 1; ++i) command->text[n++] = suffix[i];
    command->text[n] = 0;
}

static void draw(void) {
    commands[0].kind = SCENE_RECT;
    commands[0].rgb = 0x3c4a40u;
    commands[0].x = 40; commands[0].y = 180;
    commands[0].w = 400; commands[0].h = 24;
    commands[1].kind = SCENE_RECT;
    commands[1].rgb = 0xed7b2fu;
    commands[1].x = 40; commands[1].y = 180;
    commands[1].w = 1 + (int)(seconds * 399u / 60u);
    commands[1].h = 24;
    label(&commands[2]);
    (void)platform_scene(commands, 3, 0x18231du);
}

void app_init(void) {
    seconds = 0;
    running = 1;
    last_ms = platform_millis();
    draw();
}

void app_event(int type, int a, int b) {
    (void)b;
    if (type == 2) {
        running = !running;
        last_ms = platform_millis();
        draw();
    } else if (type == 1 && running) {
        const uint32_t now = (uint32_t)a;
        /* Unsigned subtraction handles the 32-bit millisecond wrap. */
        if (now - last_ms < 1000u) return;
        const uint32_t whole_seconds = (now - last_ms) / 1000u;
        last_ms += whole_seconds * 1000u;
        seconds = (seconds + whole_seconds) % 61u;
        draw();
    }
}
