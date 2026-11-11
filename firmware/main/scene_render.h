#pragma once
#include "../../sdk/include/scene.h"
#include <stdbool.h>
#include <stddef.h>
bool scene_validate(const scene_cmd_t *cmd, unsigned count);
/* RGB565 packed native-endian; stride measured in pixels, at least 480.
 * Caller must validate first. Text is rendered separately by the LVGL host. */
void scene_shape(uint16_t *pixels, size_t stride, const scene_cmd_t *cmd);
