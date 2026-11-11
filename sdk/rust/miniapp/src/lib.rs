//! Minimal `no_std` bindings for platform ABI v1.
//!
//! The module is a reactor: it exports `app_init` and `app_event` and never
//! runs anything at load time. Everything the host offers arrives as a function
//! import from module "platform"; no WASI, no allocator, no threads.
//!
//! The final binary crate must supply its own `#[panic_handler]`, because a
//! library cannot install one for the crate that links it.

#![no_std]

/// Display width in pixels.
pub const SCREEN_W: i32 = 480;
/// Display height in pixels.
pub const SCREEN_H: i32 = 400;
pub const HW_DISPLAY: u32 = 1 << 0;
pub const HW_TOUCH: u32 = 1 << 1;
pub const HW_SPEAKER: u32 = 1 << 2;
pub const HW_MOTION: u32 = 1 << 3;
pub const HW_BUTTONS: u32 = 1 << 4;
pub const HW_WIFI: u32 = 1 << 5;
pub const HW_CAMERA: u32 = 1 << 6;
pub const HW_MICROPHONE: u32 = 1 << 7;
pub const HW_TEMPERATURE: u32 = 1 << 8;
pub const HW_HUMIDITY: u32 = 1 << 9;
pub const HW_LIGHT: u32 = 1 << 10;
pub const HW_IMU: u32 = 1 << 11;
pub const HW_MAGNETOMETER: u32 = 1 << 12;
pub const HW_BATTERY: u32 = 1 << 13;
pub const HW_HAPTIC: u32 = 1 << 14;
pub const HW_LED: u32 = 1 << 15;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ImuSample {
    pub ax_mg: i32, pub ay_mg: i32, pub az_mg: i32,
    pub gx_mdps: i32, pub gy_mdps: i32, pub gz_mdps: i32,
    pub sampled_ms: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Orientation {
    pub roll_md: i32, pub pitch_md: i32, pub heading_md: i32,
    pub flags: u32, pub sampled_ms: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct MagnetSample {
    pub x_ut100: i32, pub y_ut100: i32, pub z_ut100: i32,
    pub sampled_ms: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct BatterySample {
    pub voltage_mv: i32, pub current_ma: i32, pub percent: i32,
    pub flags: u32,
}
const _: [(); 28] = [(); core::mem::size_of::<ImuSample>()];
const _: [(); 20] = [(); core::mem::size_of::<Orientation>()];
const _: [(); 16] = [(); core::mem::size_of::<MagnetSample>()];
const _: [(); 16] = [(); core::mem::size_of::<BatterySample>()];

/// Scene command kinds shared with `sdk/include/scene.h`.
pub const SCENE_RECT: u32 = 1;
pub const SCENE_TEXT: u32 = 4;

/// One inline command for an atomic scene submission.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct SceneCommand {
    kind: u32,
    rgb: u32,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    p: i32,
    q: i32,
    text: [u8; 32],
}

const _: [(); 64] = [(); core::mem::size_of::<SceneCommand>()];

impl SceneCommand {
    /// Create a filled rectangle command.
    pub const fn rect(x: i32, y: i32, w: i32, h: i32, rgb: u32) -> Self {
        Self { kind: SCENE_RECT, rgb, x, y, w, h, p: 0, q: 0, text: [0; 32] }
    }

    /// Create an inline UTF-8 label. Text is truncated to 31 bytes.
    pub fn text(x: i32, y: i32, w: i32, h: i32, bytes: &[u8], rgb: u32) -> Self {
        let mut command = Self {
            kind: SCENE_TEXT, rgb, x, y, w, h, p: 0, q: 0, text: [0; 32],
        };
        let count = if bytes.len() < 31 { bytes.len() } else { 31 };
        let mut i = 0;
        while i < count {
            command.text[i] = bytes[i];
            i += 1;
        }
        command
    }
}

#[link(wasm_import_module = "platform")]
extern "C" {
    /// Return a bit mask describing hardware currently available on the device.
    pub fn hardware() -> u32;
    /// Fill the display with `rgb` (0xRRGGBB). Requires the "display" permission.
    pub fn clear(rgb: u32);
    /// Fill a rectangle. Invalid batches trap the application.
    pub fn rect(x: i32, y: i32, w: i32, h: i32, rgb: u32);
    /// Draw `length` bytes of UTF-8 at (x, y). `length` is limited to 160.
    pub fn text(x: i32, y: i32, utf8: *const u8, length: i32, rgb: u32);
    /// Submit one complete opaque frame. Commands are copied before returning.
    pub fn scene(commands: *const SceneCommand, count: i32, background: u32) -> i32;
    /// Overlay a little-endian RGB565 image after scene(). Maximum 128x128.
    pub fn image(x: i32,y: i32,width: i32,height: i32,pixels: *const u16) -> i32;
    /// Requires the "sensors" permission; returns the structure size or -1.
    pub fn imu(out: *mut ImuSample,capacity: i32) -> i32;
    pub fn orientation(out: *mut Orientation,capacity: i32) -> i32;
    pub fn magnet(sensor: i32,out: *mut MagnetSample,capacity: i32) -> i32;
    pub fn battery(out: *mut BatterySample,capacity: i32) -> i32;
    /// Requires "feedback". kind 1 = motor; kind 2 = status LED.
    pub fn feedback(kind: i32,intensity: i32,duration_ms: i32) -> i32;
    /// Milliseconds since boot, wrapping at 2^32.
    pub fn millis() -> u32;
    /// Read the key `key` into `value`. Returns the byte count, 0 when absent,
    /// -2 when `value` is too small and -1 on error. Needs the "storage" permission.
    pub fn kv_get(key: *const u8, key_length: i32, value: *mut u8, capacity: i32) -> i32;
    /// Write `value` under `key`. Nonnegative means success (device: value
    /// length; PC preview: 0). -3 means a quota or reserved-space limit.
    /// Needs the "storage" permission.
    pub fn kv_set(key: *const u8, key_length: i32, value: *const u8, value_length: i32) -> i32;
}

/// Hardware currently available. Optional devices can disappear without blocking launch.
pub fn available_hardware() -> u32 { unsafe { hardware() } }

/* The host validates every argument, so these wrappers are safe to expose: a bad
 * rectangle or an over-long string cannot corrupt memory, it traps the app. */
/// Fill the display with `rgb`.
pub fn fill(rgb: u32) {
    unsafe { clear(rgb) }
}

/// Fill a rectangle.
pub fn fill_rect(x: i32, y: i32, w: i32, h: i32, rgb: u32) {
    unsafe { rect(x, y, w, h, rgb) }
}

/// Milliseconds since boot, wrapping at 2^32.
pub fn now_ms() -> u32 {
    unsafe { millis() }
}

/// Draw a byte slice as UTF-8 at (x, y).
pub fn draw_text(x: i32, y: i32, bytes: &[u8], rgb: u32) {
    unsafe { text(x, y, bytes.as_ptr(), bytes.len() as i32, rgb) }
}

/// Submit a complete frame atomically. At most 96 commands are accepted.
pub fn submit_scene(commands: &[SceneCommand], background: u32) -> i32 {
    unsafe { scene(commands.as_ptr(), commands.len() as i32, background) }
}

/// Overlay an RGB565 image on the last scene. Dimensions must match the slice.
pub fn draw_image(x:i32,y:i32,width:i32,height:i32,pixels:&[u16]) -> i32 {
    if width<1 || height<1 || width>128 || height>128 || pixels.len()!=width as usize*height as usize { return -2; }
    unsafe { image(x,y,width,height,pixels.as_ptr()) }
}
pub fn read_imu(out:&mut ImuSample)->i32 { unsafe { imu(out,core::mem::size_of::<ImuSample>() as i32) } }
pub fn read_orientation(out:&mut Orientation)->i32 { unsafe { orientation(out,core::mem::size_of::<Orientation>() as i32) } }
pub fn read_magnet(sensor:i32,out:&mut MagnetSample)->i32 { unsafe { magnet(sensor,out,core::mem::size_of::<MagnetSample>() as i32) } }
pub fn read_battery(out:&mut BatterySample)->i32 { unsafe { battery(out,core::mem::size_of::<BatterySample>() as i32) } }
pub fn signal(kind:i32,intensity:i32,duration_ms:i32)->i32 { unsafe { feedback(kind,intensity,duration_ms) } }

/// Read a key into `out`. Returns the number of bytes written, or 0 when the
/// key is absent. Returns -2 when `out` is too small.
pub fn kv_read(key: &[u8], out: &mut [u8]) -> i32 {
    unsafe { kv_get(key.as_ptr(), key.len() as i32, out.as_mut_ptr(), out.len() as i32) }
}

/// Write `value` under `key`. Nonnegative means success; -3 means quota blocked it.
pub fn kv_write(key: &[u8], value: &[u8]) -> i32 {
    unsafe { kv_set(key.as_ptr(), key.len() as i32, value.as_ptr(), value.len() as i32) }
}

/// Milliseconds elapsed from `from` to `to`, correct across the 2^32 wrap.
pub fn elapsed(from: u32, to: u32) -> u32 {
    to.wrapping_sub(from)
}
