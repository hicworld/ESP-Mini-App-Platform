#pragma once
#include <stdint.h>

/* Fixed-point, wasm32-safe snapshots. All fields are little-endian i32/u32. */
typedef struct {
    int32_t ax_mg, ay_mg, az_mg;
    int32_t gx_mdps, gy_mdps, gz_mdps;
    uint32_t sampled_ms;
} platform_imu_sample_t;
_Static_assert(sizeof(platform_imu_sample_t)==28,"IMU ABI");

typedef struct {
    int32_t roll_md, pitch_md, heading_md; /* millidegrees; heading -1 when unavailable */
    uint32_t flags; /* bit 0: tilt valid; bit 1: uncalibrated magnetic heading valid */
    uint32_t sampled_ms;
} platform_orientation_t;
_Static_assert(sizeof(platform_orientation_t)==20,"orientation ABI");

typedef struct {
    int32_t x_ut100, y_ut100, z_ut100;
    uint32_t sampled_ms;
} platform_magnet_sample_t;
_Static_assert(sizeof(platform_magnet_sample_t)==16,"magnetometer ABI");

typedef struct {
    int32_t voltage_mv, current_ma, percent;
    uint32_t flags; /* bit 0: external power/charging when known */
} platform_battery_sample_t;
_Static_assert(sizeof(platform_battery_sample_t)==16,"battery ABI");
