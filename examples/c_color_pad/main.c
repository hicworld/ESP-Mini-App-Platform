#include "platform.h"

/* The app canvas is 480 x 400. The system owns the Home area above it. */
static const uint32_t colors[4] = {0xed7b2fu, 0x667153u, 0x497fb8u, 0xe9c46au};
static unsigned selected;

static void draw(void) {
    static const char title[] = "Color Pad - tap a tile";
    static const char hint[] = "A touch sends event 2 with canvas coordinates";
    platform_clear(0x18231du);
    platform_text(24, 32, title, (int)(sizeof(title) - 1), 0xf7f5e9u);
    platform_text(24, 72, hint, (int)(sizeof(hint) - 1), 0xc9d7cbu);
    for (int i = 0; i < 4; ++i) {
        const int x = 24 + i * 112;
        platform_rect(x, 140, 104, 130, colors[i]);
        if ((unsigned)i == selected) {
            platform_rect(x, 284, 104, 8, 0xffffffu);
        }
    }
}

void app_init(void) {
    selected = 0;
    draw();
}

void app_event(int type, int a, int b) {
    if (type != 2 || b < 140 || b >= 270) return;
    for (int i = 0; i < 4; ++i) {
        const int left = 24 + i * 112;
        if (a >= left && a < left + 104) {
            selected = (unsigned)i;
            draw();
            return;
        }
    }
}
