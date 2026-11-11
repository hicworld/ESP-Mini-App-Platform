idf_component_register(SRCS "main.c" "hal_mosaico.c" "hardware.c" "scene_render.c" "garden_font.c" "ui_language.c" "environment.c" "garden_input.c" "nand_storage.c" "download_cache.c" "ota_health.c" "runtime.c" "app_data.c" "app_manager.c" "app_verify.c" "network.c" "network_setup.c" "system_update.c" "platform_audio.c" "audio_synth.c" "launcher.c" "selftest.c" "diagnostics.c" "catalog.c" "upload.c" "store.c"
    INCLUDE_DIRS "."
    REQUIRES platform_runtime esp-mosaico-bsp esp_tinyusb esp_timer
        nvs_flash spiffs fatfs espressif__spi_nand_flash_fatfs cjson mbedtls esp_wifi esp_event esp_netif esp_http_client
        esp_https_ota app_update)
target_add_binary_data(${COMPONENT_LIB} "trusted_key.pem" TEXT)
target_add_binary_data(${COMPONENT_LIB} "builtin.app" BINARY)
target_add_binary_data(${COMPONENT_LIB} "touch.app" BINARY)
target_add_binary_data(${COMPONENT_LIB} "snake.app" BINARY)
target_compile_definitions(${COMPONENT_LIB} PRIVATE PLATFORM_VERSION="${PLATFORM_PROJECT_VERSION}")
