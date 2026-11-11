//! Bounce: a minimal freestanding Rust application for ABI v1.
//!
//! Every `static` here is plain data that the linker can initialize without
//! running code. A `static` needing dynamic initialization would force the
//! linker to emit a constructor export, and the runtime refuses modules that
//! export constructors.

#![no_std]

use miniapp::{now_ms, submit_scene, SceneCommand, SCREEN_H, SCREEN_W};

const BLOCK: i32 = 24;
const FRAME_MS: u32 = 40;
const BACKGROUND: u32 = 0x101820;
const BLOCK_COLOR: u32 = 0xf0a040;
const TEXT_COLOR: u32 = 0xa8c8e8;

#[derive(Clone, Copy)]
struct State {
    x: i32,
    y: i32,
    vx: i32,
    vy: i32,
}

static mut STATE: State = State { x: 0, y: 0, vx: 0, vy: 0 };
static mut LAST_MS: u32 = 0;

fn clamp(v: i32, lo: i32, hi: i32) -> i32 {
    if v < lo {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

/// Centre a block of `BLOCK` pixels on (center_x, center_y), clamped to the canvas.
fn placed(center_x: i32, center_y: i32) -> State {
    State {
        x: clamp(center_x - BLOCK / 2, 0, SCREEN_W - BLOCK),
        y: clamp(center_y - BLOCK / 2, 0, SCREEN_H - BLOCK),
        vx: 3,
        vy: 2,
    }
}

fn step(s: &mut State) {
    s.x += s.vx;
    s.y += s.vy;
    if s.x <= 0 {
        s.x = 0;
        s.vx = -s.vx;
    } else if s.x >= SCREEN_W - BLOCK {
        s.x = SCREEN_W - BLOCK;
        s.vx = -s.vx;
    }
    if s.y <= 0 {
        s.y = 0;
        s.vy = -s.vy;
    } else if s.y >= SCREEN_H - BLOCK {
        s.y = SCREEN_H - BLOCK;
        s.vy = -s.vy;
    }
}

fn draw(s: State) {
    let commands = [
        SceneCommand::rect(s.x, s.y, BLOCK, BLOCK, BLOCK_COLOR),
        SceneCommand::text(12, 12, 360, 24, b"Rust app - tap to move block", TEXT_COLOR),
    ];
    let _ = submit_scene(&commands, BACKGROUND);
}

#[no_mangle]
pub extern "C" fn app_init() {
    unsafe {
        STATE = placed(SCREEN_W / 2, SCREEN_H / 2);
        LAST_MS = now_ms();
        draw(STATE);
    }
}

#[no_mangle]
pub extern "C" fn app_event(kind: i32, a: i32, b: i32) {
    if kind == 1 {
        let now = a as u32;
        /* Wrapping subtraction keeps the 2^32 rollover of the tick counter correct. */
        if unsafe { now.wrapping_sub(LAST_MS) } < FRAME_MS {
            return;
        }
        unsafe {
            LAST_MS = now;
            let mut s = STATE;
            step(&mut s);
            STATE = s;
            draw(STATE);
        }
    } else if kind == 2 {
        unsafe {
            let mut s = STATE;
            s.x = clamp(a - BLOCK / 2, 0, SCREEN_W - BLOCK);
            s.y = clamp(b - BLOCK / 2, 0, SCREEN_H - BLOCK);
            STATE = s;
            draw(STATE);
        }
    }
}

/* A panic cannot unwind here: there is no runtime to catch it. Trapping is the
 * honest outcome, and the host then reports the application as crashed. */
#[panic_handler]
fn panic_handler(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
