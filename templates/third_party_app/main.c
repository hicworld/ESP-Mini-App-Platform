#include "platform.h"

static unsigned taps;

static void draw(void) {
    static const char title[] = "My First App";
    static const char hint[] = "Tap the screen";
    static const char response[] = "Hello from WASM!";
    platform_clear(0x101d32u);
    platform_text(40, 70, title, (int)(sizeof(title) - 1), 0xe9eff8u);
    platform_text(40, 120, hint, (int)(sizeof(hint) - 1), 0x63dfbdu);
    if (taps & 1u) {
        platform_text(40, 170, response, (int)(sizeof(response) - 1), 0xf6c65bu);
    }
}

void app_init(void) {
    taps = 0;
    draw();
}

void app_event(int type, int a, int b) {
    (void)a;
    (void)b;
    if (type == 2) {
        ++taps;
        draw();
    }
}
