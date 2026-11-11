/* Bounce: a minimal freestanding C++ application for ABI v1.
 *
 * Demonstrates classes, member functions, templates and constexpr on the
 * platform, while keeping every object with static storage duration a POD.
 * That last point is a hard requirement, not a style choice: a global whose
 * dynamic initialization cannot be folded into a constant would force the
 * linker to emit a constructor function, and the runtime rejects modules
 * exporting constructors (see "Automatic constructor exports are not
 * permitted" in firmware/main/runtime.c). All per-event objects live on the
 * stack and are built inside app_init/app_event.
 */
extern "C" {
#include "platform.h"
}

namespace {

constexpr uint32_t kBackground = 0x101820u;
constexpr uint32_t kBlockColor = 0x40c8f0u;
constexpr uint32_t kTextColor = 0xa8c8e8u;

constexpr int kScreenW = 480;
constexpr int kScreenH = 400;
constexpr int kBlock = 24;
constexpr uint32_t kFrameMs = 40;

/* POD: statically zero-initialized, so no dynamic initializer is emitted. */
struct State {
    int x;
    int y;
    int vx;
    int vy;
};

State g_state;
uint32_t g_last_ms = 0;
scene_cmd_t g_commands[2];

template <typename T>
constexpr T clamp_value(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* A real C++ class: constructed on the stack per event, never at load time. */
class Bounce {
public:
    explicit Bounce(const State &s) : s_(s) {}

    State stepped() const {
        State next = s_;
        next.x += next.vx;
        next.y += next.vy;
        if (next.x <= 0) {
            next.x = 0;
            next.vx = -next.vx;
        } else if (next.x >= kScreenW - kBlock) {
            next.x = kScreenW - kBlock;
            next.vx = -next.vx;
        }
        if (next.y <= 0) {
            next.y = 0;
            next.vy = -next.vy;
        } else if (next.y >= kScreenH - kBlock) {
            next.y = kScreenH - kBlock;
            next.vy = -next.vy;
        }
        return next;
    }

    static State placed(int center_x, int center_y) {
        State s;
        s.x = clamp_value(center_x - kBlock / 2, 0, kScreenW - kBlock);
        s.y = clamp_value(center_y - kBlock / 2, 0, kScreenH - kBlock);
        s.vx = 3;
        s.vy = 2;
        return s;
    }

private:
    State s_;
};

void draw_frame(void) {
    g_commands[0].kind = SCENE_RECT;
    g_commands[0].rgb = kBlockColor;
    g_commands[0].x = g_state.x;
    g_commands[0].y = g_state.y;
    g_commands[0].w = kBlock;
    g_commands[0].h = kBlock;

    constexpr char label[] = "C++ app - tap to move the block";
    static_assert(sizeof(label) <= sizeof(g_commands[1].text), "scene label must fit inline text");
    g_commands[1].kind = SCENE_TEXT;
    g_commands[1].rgb = kTextColor;
    g_commands[1].x = 12;
    g_commands[1].y = 12;
    g_commands[1].w = 360;
    g_commands[1].h = 24;
    for (unsigned i = 0; i < sizeof(label); ++i) g_commands[1].text[i] = label[i];
    (void)platform_scene(g_commands, 2, kBackground);
}

}  // namespace

extern "C" void app_init(void) {
    g_state = Bounce::placed(kScreenW / 2, kScreenH / 2);
    g_last_ms = platform_millis();
    draw_frame();
}

extern "C" void app_event(int32_t type, int32_t a, int32_t b) {
    if (type == 1) {
        const uint32_t now = static_cast<uint32_t>(a);
        /* Subtraction stays correct across the 2^32 wrap of the tick counter. */
        if (now - g_last_ms < kFrameMs) return;
        g_last_ms = now;
        g_state = Bounce(g_state).stepped();
        draw_frame();
    } else if (type == 2) {
        g_state = Bounce::placed(a, b);
        draw_frame();
    }
}
