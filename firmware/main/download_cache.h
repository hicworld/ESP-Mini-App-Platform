#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

#define DOWNLOAD_CACHE_PART "/nand/download/app.part"
#define DOWNLOAD_CACHE_READY "/nand/download/app.ready"

typedef struct {
    FILE *file;
    size_t expected;
    size_t received;
} download_cache_t;

/* Remove incomplete or unconsumed files left by an interrupted download. */
esp_err_t download_cache_recover(void);
esp_err_t download_cache_begin(download_cache_t *cache, size_t expected);
esp_err_t download_cache_append(download_cache_t *cache, const uint8_t *bytes, size_t size);
esp_err_t download_cache_commit(download_cache_t *cache);
esp_err_t download_cache_load(uint8_t **bytes, size_t *size);
void download_cache_abort(download_cache_t *cache);
esp_err_t download_cache_status(void);
