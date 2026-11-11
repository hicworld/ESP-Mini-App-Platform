#include "nand_storage.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_vfs_fat_nand.h"
#include "spi_nand_flash.h"

static const char *TAG = "nand-storage";
static spi_nand_flash_device_t *s_flash;
static bool s_initialized;
static bool s_mounted;
static esp_err_t s_mount_result = ESP_ERR_INVALID_STATE;

static const esp_vfs_fat_mount_config_t s_mount_config = {
    .format_if_mount_failed = false,
    .max_files = 12,
    .allocation_unit_size = 16 * 1024,
};

static esp_err_t ensure_directory(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return ESP_OK;
    ESP_LOGE(TAG, "mkdir %s failed: errno=%d", path, errno);
    return ESP_FAIL;
}

static esp_err_t create_layout(void)
{
    static const char *paths[] = {
        NAND_STORAGE_PATH "/apps",
        NAND_STORAGE_PATH "/appdata",
        NAND_STORAGE_PATH "/cache",
        NAND_STORAGE_PATH "/download",
        NAND_STORAGE_PATH "/system",
        NAND_STORAGE_PATH "/assets",
        NAND_STORAGE_PATH "/temp",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        esp_err_t err = ensure_directory(paths[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static esp_err_t initialize_device(void)
{
    if (s_initialized && s_flash) return ESP_OK;
    s_flash = bsp_nand_flash_get_handle();
    esp_err_t err = s_flash ? ESP_OK : bsp_nand_flash_init(NULL, &s_flash);
    if (err == ESP_ERR_INVALID_STATE) {
        s_flash = bsp_nand_flash_get_handle();
        err = s_flash ? ESP_OK : err;
    }
    s_initialized = err == ESP_OK && s_flash;
    return s_initialized ? ESP_OK : err;
}

esp_err_t nand_storage_init(void)
{
    esp_err_t err = initialize_device();
    if (err != ESP_OK) {
        s_mount_result = err;
        return err;
    }
    if (s_mounted) return ESP_OK;

    s_mount_result = esp_vfs_fat_nand_mount(NAND_STORAGE_PATH, s_flash, &s_mount_config);
    s_mounted = s_mount_result == ESP_OK;
    if (!s_mounted) {
        ESP_LOGW(TAG, "existing FAT filesystem not mounted: %s; no format was performed",
                 esp_err_to_name(s_mount_result));
        return s_mount_result;
    }
    err = create_layout();
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "NAND FAT filesystem mounted at %s", NAND_STORAGE_PATH);
    return ESP_OK;
}

esp_err_t nand_storage_status(void)
{
    esp_err_t err = initialize_device();
    if (err != ESP_OK) {
        printf("NAND initialized=0 mounted=0 init_result=%s\n", esp_err_to_name(err));
        return err;
    }

    uint32_t pages = 0, page_size = 0, blocks = 0, block_size = 0;
    esp_err_t geometry = spi_nand_flash_get_page_count(s_flash, &pages);
    if (geometry == ESP_OK) geometry = spi_nand_flash_get_page_size(s_flash, &page_size);
    if (geometry == ESP_OK) geometry = spi_nand_flash_get_block_num(s_flash, &blocks);
    if (geometry == ESP_OK) geometry = spi_nand_flash_get_block_size(s_flash, &block_size);
    uint64_t bytes = (uint64_t)pages * page_size;
    printf("NAND initialized=1 mounted=%d mount_result=%s pages=%" PRIu32
           " page_size=%" PRIu32 " blocks=%" PRIu32 " block_size=%" PRIu32
           " capacity=%" PRIu64 "\n",
           s_mounted, esp_err_to_name(s_mount_result), pages, page_size, blocks,
           block_size, bytes);
    if (geometry != ESP_OK) return geometry;

    if (s_mounted) {
        uint64_t total = 0, free_bytes = 0;
        err = esp_vfs_fat_info(NAND_STORAGE_PATH, &total, &free_bytes);
        if (err != ESP_OK) return err;
        printf("NAND filesystem path=%s total=%" PRIu64 " free=%" PRIu64 "\n",
               NAND_STORAGE_PATH, total, free_bytes);
    }
    /* A readable device is a successful probe even if it has no FAT volume yet. */
    return ESP_OK;
}

esp_err_t nand_storage_info(uint64_t *total, uint64_t *free_bytes)
{
    if (!total || !free_bytes) return ESP_ERR_INVALID_ARG;
    if (!s_mounted) return ESP_ERR_INVALID_STATE;
    return esp_vfs_fat_info(NAND_STORAGE_PATH, total, free_bytes);
}

esp_err_t nand_storage_format(void)
{
    esp_err_t err = initialize_device();
    if (err != ESP_OK) return err;
    /* This command initializes missing/corrupt media. Refuse a mounted volume
     * so a repeated command can never erase or pretend to format healthy data. */
    if (s_mounted) return ESP_ERR_INVALID_STATE;

    const esp_vfs_fat_mount_config_t format_config = {
        .format_if_mount_failed = true,
        .max_files = 12,
        .allocation_unit_size = 16 * 1024,
    };
    s_mount_result = esp_vfs_fat_nand_mount(NAND_STORAGE_PATH, s_flash, &format_config);
    s_mounted = s_mount_result == ESP_OK;
    if (!s_mounted) return s_mount_result;
    err = create_layout();
    if (err == ESP_OK) ESP_LOGI(TAG, "NAND FAT filesystem initialized");
    return err;
}

esp_err_t nand_storage_selftest(void)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;

    static const char path[] = NAND_STORAGE_PATH "/temp/.selftest.tmp";
    static const uint8_t expected[] = {
        0x45, 0x53, 0x50, 0x2d, 0x4d, 0x49, 0x4e, 0x49,
        0x2d, 0x4e, 0x41, 0x4e, 0x44, 0x2d, 0x30, 0x32,
    };
    uint8_t actual[sizeof(expected)] = {0};

    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "self-test open for write failed: errno=%d", errno);
        return ESP_FAIL;
    }
    bool write_ok = fwrite(expected, 1, sizeof(expected), file) == sizeof(expected);
    bool close_ok = fclose(file) == 0;
    if (!write_ok || !close_ok) {
        ESP_LOGE(TAG, "self-test write failed: errno=%d", errno);
        remove(path);
        return ESP_FAIL;
    }

    file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "self-test open for read failed: errno=%d", errno);
        remove(path);
        return ESP_FAIL;
    }
    size_t read_size = fread(actual, 1, sizeof(actual), file);
    int extra = fgetc(file);
    close_ok = fclose(file) == 0;
    bool contents_ok = read_size == sizeof(actual) && extra == EOF &&
                       memcmp(actual, expected, sizeof(expected)) == 0;
    bool remove_ok = remove(path) == 0;
    if (!contents_ok || !close_ok || !remove_ok) {
        ESP_LOGE(TAG, "self-test verification failed: read=%u close=%d remove=%d errno=%d",
                 (unsigned)read_size, close_ok, remove_ok, errno);
        remove(path);
        return ESP_FAIL;
    }

    printf("NAND SELFTEST PASS bytes=%u path=%s\n", (unsigned)sizeof(expected), path);
    return ESP_OK;
}

bool nand_storage_initialized(void) { return s_initialized; }
bool nand_storage_ready(void) { return s_mounted; }
esp_err_t nand_storage_mount_result(void) { return s_mount_result; }
