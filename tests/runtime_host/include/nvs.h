#pragma once
/* Stand-in for the IDF NVS API. The double in main.c keeps blobs in memory and
 * can fail allocation the same way a full partition would. */
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef unsigned nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_TYPE_BLOB 0x42u

struct nvs_opaque_iterator;
typedef struct nvs_opaque_iterator *nvs_iterator_t;

typedef struct {
    size_t used_entries, free_entries, total_entries, namespace_count;
} nvs_stats_t;

esp_err_t nvs_open(const char *namespace_name,int mode,nvs_handle_t *out);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_get_stats(const char *partition_name,nvs_stats_t *stats);
esp_err_t nvs_entry_find_in_handle(nvs_handle_t handle,unsigned type,nvs_iterator_t *out);
esp_err_t nvs_entry_next(nvs_iterator_t *it);
void nvs_release_iterator(nvs_iterator_t it);
