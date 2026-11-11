#pragma once
#include <stdbool.h>
static inline bool bsp_display_lock(int timeout) {(void)timeout;return true;}
static inline void bsp_display_unlock(void) {}
