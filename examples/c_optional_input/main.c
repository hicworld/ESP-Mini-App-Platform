#include "platform.h"

static uint32_t count;
static uint32_t available;
static const char *last_input;

static int text_length(const char *value) {
    int length = 0;
    while (value[length]) ++length;
    return length;
}

static void number(char out[11], uint32_t value) {
    char reverse[10];
    unsigned length = 0;
    do {
        reverse[length++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && length < 10);
    for (unsigned i = 0; i < length; ++i) out[i] = reverse[length - 1 - i];
    out[length] = 0;
}

static void draw(void) {
    static const char title[] = "Optional Input";
    static const char fallback[] = "Tap anywhere: always available";
    static const char motion[] = "Motion: available";
    static const char no_motion[] = "Motion: absent (tap still works)";
    static const char buttons[] = "Buttons: available";
    static const char no_buttons[] = "Buttons: absent (tap still works)";
    char digits[11];
    number(digits, count);
    platform_clear(0x16252cu);
    platform_text(28, 30, title, (int)(sizeof(title) - 1), 0xf7f5e9u);
    platform_text(28, 85, fallback, (int)(sizeof(fallback) - 1), 0xcbd9d0u);
    if (available & PLATFORM_HW_MOTION)
        platform_text(28, 130, motion, (int)(sizeof(motion) - 1), 0x78dda5u);
    else
        platform_text(28, 130, no_motion, (int)(sizeof(no_motion) - 1), 0xe9c46au);
    if (available & PLATFORM_HW_BUTTONS)
        platform_text(28, 170, buttons, (int)(sizeof(buttons) - 1), 0x78dda5u);
    else
        platform_text(28, 170, no_buttons, (int)(sizeof(no_buttons) - 1), 0xe9c46au);
    platform_text(28, 225, last_input, text_length(last_input), 0xf7f5e9u);
    platform_text(28, 275, digits, text_length(digits), 0xed7b2fu);
}

void app_init(void) {
    available = platform_hardware();
    count = 0;
    last_input = "Last: none";
    draw();
}

void app_event(int type, int a, int b) {
    (void)b;
    const uint32_t current = platform_hardware();
    if (type == 1) {
        /* Optional devices can change; redraw only when the mask changes. */
        if (current != available) {
            available = current;
            draw();
        }
        return;
    }
    available = current;
    if (type == 2) {
        last_input = "Last: touch";
    } else if (type == 3 && a == 1 && (available & PLATFORM_HW_MOTION)) {
        last_input = "Last: motion";
    } else if (type == 3 && (a == 2 || a == 3) && (available & PLATFORM_HW_BUTTONS)) {
        last_input = a == 2 ? "Last: button A" : "Last: button B";
    } else {
        return;
    }
    ++count;
    draw();
}
