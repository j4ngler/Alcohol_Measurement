/**
 * @file test_sdcard.c
 * @brief Test SD card: mount, ghi/đọc file mẫu, unmount
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdcard.h"
#include "test_sdcard.h"

static const char *TAG = "SD_TEST";

static void sdcard_test_task(void *pvParameters)
{
    esp_vfs_fat_mount_config_t mount_config = MOUNT_CONFIG_DEFAULT();
    spi_bus_config_t bus_cfg = SPI_BUS_CONFIG_DEFAULT();
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_PIN_NUM_CS;
    slot_config.host_id = host.slot;

    sdmmc_card_t card;

    ESP_LOGI(TAG, "Starting SD card test...");
    esp_err_t ret = sdcard_initialize(&mount_config, &card, &host, &bus_cfg, &slot_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD card initialize failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    const char *path = MOUNT_POINT "/sd_test.txt";
    const char *content = "SD card test OK\n";

    // Ghi file
    FILE *f = fopen(path, "w");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        goto cleanup;
    }
    size_t written = fwrite(content, 1, strlen(content), f);
    fclose(f);
    ESP_LOGI(TAG, "Wrote %d bytes to %s", (int)written, path);

    // Đọc file
    f = fopen(path, "r");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        goto cleanup;
    }
    char buf[64] = {0};
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ESP_LOGI(TAG, "Read from file: %s", buf);

cleanup:
    // Unmount và giải phóng bus
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
    spi_bus_free(host.slot);

    ESP_LOGI(TAG, "SD card test done.");
    vTaskDelete(NULL);
}

void start_sdcard_test(void)
{
    xTaskCreate(sdcard_test_task, "sdcard_test_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "SD card test task created");
}

