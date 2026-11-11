#pragma once
#include <stdint.h>
#include "scene.h"
#include "capabilities.h"
/* ABI v1: wasm32, no WASI, little endian, bounded synchronous drawing.
 * Parameter, permission, and return-value guide: docs/PLATFORM_API.md.
 * Each application exports app_init() and app_event(type, a, b).
 * Events: 1 = timer (a=elapsed ms modulo 2^32); 2 = short tap (a=x,b=y).
 * Since 0.5.0: 4 down, 5 move, 6 up, 7 long press (all x,y);
 * 8 swipe (a=direction 1 left/2 right/3 up/4 down, b=distance px).
 * A host-owned Home button is always available. No native pointers escape.
 */
#define IMPORT(name) __attribute__((import_module("platform"), import_name(name)))
#define PLATFORM_HW_DISPLAY (1u<<0)
#define PLATFORM_HW_TOUCH (1u<<1)
#define PLATFORM_HW_SPEAKER (1u<<2)
#define PLATFORM_HW_MOTION (1u<<3)
#define PLATFORM_HW_BUTTONS (1u<<4)
#define PLATFORM_HW_WIFI (1u<<5)
#define PLATFORM_HW_CAMERA (1u<<6)
#define PLATFORM_HW_MICROPHONE (1u<<7)
#define PLATFORM_HW_TEMPERATURE (1u<<8)
#define PLATFORM_HW_HUMIDITY (1u<<9)
#define PLATFORM_HW_LIGHT (1u<<10)
#define PLATFORM_HW_IMU (1u<<11)
#define PLATFORM_HW_MAGNETOMETER (1u<<12)
#define PLATFORM_HW_BATTERY (1u<<13)
#define PLATFORM_HW_HAPTIC (1u<<14)
#define PLATFORM_HW_LED (1u<<15)
/* Since 0.4.8: bit mask of hardware currently available to this app. */
IMPORT("hardware") uint32_t platform_hardware(void);
IMPORT("clear") void platform_clear(uint32_t rgb);
IMPORT("rect") void platform_rect(int x, int y, int w, int h, uint32_t rgb);
IMPORT("text") void platform_text(int x, int y, const char *utf8, int length, uint32_t rgb);
IMPORT("millis") uint32_t platform_millis(void);
/* Since 0.4.1: open host-owned Wi-Fi/location settings. Never passes secrets to WASM. */
IMPORT("settings") void platform_settings(void);
/* Since 0.4.3, audio permission. Nonblocking, fixed built-in sounds only.
 * kind 1: music 0 off / 1 day / 2 night; kind 2: effect 1 pet / 2 feed /
 * 3 water / 4 arrival / 5 wind. kind 3: volume 0..75, -1 reads current.
 * Returns -1 unavailable, -2 invalid argument, -3 rate limited; otherwise 0
 * (or volume for kind 3). Stop/crash always silences the app. */
IMPORT("audio") int32_t platform_audio(int32_t kind,int32_t value);
/* Returns 0 on success, -1 on unavailable display; invalid batches trap.
 * One scene call consumes the full drawing quota for this entry. */
IMPORT("scene") int32_t platform_scene(const scene_cmd_t *commands, int32_t count, uint32_t background);
/* RGB565 little-endian image, max 128x128 and eight calls per callback.
 * Draw after platform_scene; returns -1 if no scene canvas exists.
 * Embed generated pixels in the WASM: tools/image_to_c.py. */
IMPORT("image") int32_t platform_image(int32_t x,int32_t y,int32_t width,int32_t height,const uint16_t *pixels);
/* Requires "environment" permission; returns sizeof(environment_t). */
IMPORT("environment") int32_t platform_environment(environment_t *out, int32_t capacity);
/* Event 3: input (requires "input" permission): a=1 motion rising edge,
 * a=2 button A pressed, a=3 button B pressed; b=0 reserved. */
/* Storage requires the manifest permission "storage". Keys are 1..15 ASCII
 * characters; values are limited to 128 bytes. kv_get returns the byte count,
 * 0 when absent, -2 when the output buffer is too small, and -1 on error.
 * Keys beginning with __ are reserved. At most eight keys per app and four
 * changed writes per second across the runtime; set returns a nonnegative
 * value on success (device: value length; PC preview: 0), -3 on quota or
 * reserved space limits, and -1 on storage error. Unchanged values avoid
 * flash I/O. */
IMPORT("kv_get") int32_t platform_kv_get(const char *key, int32_t key_length,
                                          void *value, int32_t capacity);
IMPORT("kv_set") int32_t platform_kv_set(const char *key, int32_t key_length,
                                          const void *value, int32_t value_length);
/* Since platform 0.5.0. These require the "sensors" permission; success
 * returns the structure size, unavailable hardware returns -1. */
IMPORT("imu") int32_t platform_imu(platform_imu_sample_t *out, int32_t capacity);
IMPORT("orientation") int32_t platform_orientation(platform_orientation_t *out, int32_t capacity);
IMPORT("magnet") int32_t platform_magnet(int32_t sensor, platform_magnet_sample_t *out, int32_t capacity);
IMPORT("battery") int32_t platform_battery(platform_battery_sample_t *out, int32_t capacity);
/* Requires "feedback". kind 1 = motor, kind 2 = status LED.
 * duration_ms is 0 to turn off or 1..1000; intensity 0..100. */
IMPORT("feedback") int32_t platform_feedback(int32_t kind, int32_t intensity, int32_t duration_ms);
