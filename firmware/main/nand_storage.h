#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define NAND_STORAGE_PATH "/nand"

/* Initialize the NAND device and mount an existing FAT filesystem.
 * This function never formats or erases the device. */
esp_err_t nand_storage_init(void);

/* Print device geometry and filesystem capacity to the USB console. */
esp_err_t nand_storage_status(void);

/* Return mounted FAT filesystem capacity. */
esp_err_t nand_storage_info(uint64_t *total, uint64_t *free_bytes);

/* Explicit first-use/recovery operation. This may destroy all NAND data. */
esp_err_t nand_storage_format(void);

/* Write, read back, and remove a small temporary file on the mounted NAND. */
esp_err_t nand_storage_selftest(void);

bool nand_storage_initialized(void);
bool nand_storage_ready(void);
esp_err_t nand_storage_mount_result(void);
