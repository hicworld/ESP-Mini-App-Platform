#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM (1u << 0)
#define MALLOC_CAP_8BIT (1u << 1)
static inline void *heap_caps_malloc(size_t size,unsigned caps) { (void)caps; return malloc(size); }
