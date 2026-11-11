#include "download_cache.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void remove_cache_files(void)
{
    unlink(DOWNLOAD_CACHE_PART);
    unlink(DOWNLOAD_CACHE_READY);
}

esp_err_t download_cache_recover(void)
{
    remove_cache_files();
    return ESP_OK;
}

void download_cache_abort(download_cache_t *cache)
{
    if (cache && cache->file) {
        fclose(cache->file);
        cache->file = NULL;
    }
    if (cache) {
        cache->expected = 0;
        cache->received = 0;
    }
    remove_cache_files();
}

esp_err_t download_cache_begin(download_cache_t *cache, size_t expected)
{
    if (!cache || !expected) return ESP_ERR_INVALID_ARG;
    memset(cache, 0, sizeof(*cache));
    remove_cache_files();
    cache->file = fopen(DOWNLOAD_CACHE_PART, "wb");
    if (!cache->file) return ESP_FAIL;
    cache->expected = expected;
    return ESP_OK;
}

esp_err_t download_cache_append(download_cache_t *cache, const uint8_t *bytes, size_t size)
{
    if (!cache || !cache->file || (!bytes && size) || cache->received > cache->expected ||
        size > cache->expected - cache->received) {
        return ESP_ERR_INVALID_ARG;
    }
    if (size && fwrite(bytes, 1, size, cache->file) != size) {
        download_cache_abort(cache);
        return ESP_FAIL;
    }
    cache->received += size;
    return ESP_OK;
}

esp_err_t download_cache_status(void)
{
    struct stat part={0},ready={0};
    bool has_part=stat(DOWNLOAD_CACHE_PART,&part)==0;
    bool has_ready=stat(DOWNLOAD_CACHE_READY,&ready)==0;
    printf("DOWNLOAD CACHE part=%d part_bytes=%ld ready=%d ready_bytes=%ld\n",
           has_part,has_part?(long)part.st_size:0L,has_ready,has_ready?(long)ready.st_size:0L);
    return ESP_OK;
}

esp_err_t download_cache_commit(download_cache_t *cache)
{
    if (!cache || !cache->file || cache->received != cache->expected) {
        if (cache) download_cache_abort(cache);
        return ESP_ERR_INVALID_SIZE;
    }
    bool ok = fflush(cache->file) == 0;
    if (ok) ok = fsync(fileno(cache->file)) == 0;
    int close_result = fclose(cache->file);
    cache->file = NULL;
    if (close_result) ok = false;
    if (!ok || rename(DOWNLOAD_CACHE_PART, DOWNLOAD_CACHE_READY)) {
        download_cache_abort(cache);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t download_cache_load(uint8_t **bytes, size_t *size)
{
    if (!bytes || !size) return ESP_ERR_INVALID_ARG;
    *bytes = NULL;
    *size = 0;
    FILE *file = fopen(DOWNLOAD_CACHE_READY, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        remove_cache_files();
        return ESP_FAIL;
    }
    long length = ftell(file);
    rewind(file);
    if (length <= 0) {
        fclose(file);
        remove_cache_files();
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *buffer = malloc((size_t)length);
    if (!buffer) {
        fclose(file);
        remove_cache_files();
        return ESP_ERR_NO_MEM;
    }
    bool ok = fread(buffer, 1, (size_t)length, file) == (size_t)length;
    if (fclose(file)) ok = false;
    remove_cache_files();
    if (!ok) {
        free(buffer);
        return ESP_FAIL;
    }
    *bytes = buffer;
    *size = (size_t)length;
    return ESP_OK;
}
