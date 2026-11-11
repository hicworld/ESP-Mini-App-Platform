#pragma once
#include <stdio.h>
#define ESP_LOGE(tag,...) do { printf("E(%s) ",(tag)); printf(__VA_ARGS__); printf("\n"); } while (0)
