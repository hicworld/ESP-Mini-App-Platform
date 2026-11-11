#pragma once
#include <stdint.h>
/* ABI v1 extension, platform >= 0.4.0. All coordinates are app-local (480x400).
 * One complete, opaque frame; at most 96 commands and 1,000,000 bounding pixels.
 * RECT/ELLIPSE: x,y,w,h; RECT p=radius. TRIANGLE: (x,y),(w,h),(p,q).
 * TEXT: x,y,w,h bounding box, inline NUL-terminated UTF-8, p reserved (zero).
 * The host copies and validates the whole batch before displaying it. */
enum { SCENE_RECT=1, SCENE_ELLIPSE, SCENE_TRIANGLE, SCENE_TEXT };
typedef struct {
    uint32_t kind, rgb;
    int32_t x,y,w,h,p,q;
    char text[32];
} scene_cmd_t;
_Static_assert(sizeof(scene_cmd_t)==64,"scene ABI");
/* Environment is host-owned: app cannot choose a URL or access raw GPIO.
 * flags: bit0 clock valid, bit1 weather valid, bit2 weather stale,
 * bit3 configured location, bit4 hardware input available.
 * temperature = degrees C * 10; weather = WMO code; seconds = local day time.
 * daylight: -1 unknown, 0 night, 1 day. weather_age in seconds.
 */
typedef struct {
    uint32_t flags, seconds;
    int32_t temperature, weather, daylight;
    uint32_t weather_age;
    char location[32];
} environment_t;
_Static_assert(sizeof(environment_t)==56,"environment ABI");
