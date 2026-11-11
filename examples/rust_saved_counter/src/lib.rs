//! Persistent counter: touch, drawing, and the per-app key-value store.
#![no_std]

use miniapp::{draw_text, fill, fill_rect, kv_read, kv_write};

// Plain integer and boolean statics need no constructor or allocator.
static mut COUNT: u32 = 0;
static mut SAVED: bool = true;

fn draw() {
    let count = unsafe { COUNT };
    let saved = unsafe { SAVED };
    let mut digits = [0u8; 10];
    let mut value = count;
    let mut start = digits.len();
    loop {
        start -= 1;
        digits[start] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 { break; }
    }

    fill(0x172633);
    draw_text(28, 35, b"Saved Counter - tap the screen", 0xf7f5e9);
    fill_rect(28, 100, 424, 120, 0x31506a);
    draw_text(48, 135, &digits[start..], 0xf4c46a);
    if saved {
        draw_text(28, 270, b"Saved in this app's storage", 0x80dfa5);
    } else {
        draw_text(28, 270, b"Save delayed: tap again to retry", 0xf4c46a);
    }
}

#[no_mangle]
pub extern "C" fn app_init() {
    let mut bytes = [0u8; 4];
    let read_result = kv_read(b"count", &mut bytes);
    let stored = if read_result == 4 {
        u32::from_le_bytes(bytes)
    } else {
        0
    };
    unsafe {
        COUNT = stored;
        SAVED = read_result >= 0;
    }
    draw();
}

#[no_mangle]
pub extern "C" fn app_event(kind: i32, _a: i32, _b: i32) {
    if kind != 2 { return; }
    let next = unsafe { COUNT }.wrapping_add(1);
    let saved = kv_write(b"count", &next.to_le_bytes()) >= 0;
    unsafe {
        COUNT = next;
        SAVED = saved;
    }
    draw();
}

#[panic_handler]
fn panic_handler(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
