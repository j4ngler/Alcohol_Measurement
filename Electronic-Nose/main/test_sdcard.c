/**
 * @file test_sdcard.c
 * @brief Test SD card: mount, ghi/đọc file mẫu, unmount
 * Test với chân: CS=5, CLK=18, MOSI=19, MISO=21 (giống code Arduino)
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdcard.h"
#include "test_sdcard.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

static const char *TAG = "SD_TEST";

// ===== CHÂN KẾT NỐI SPI CHO SD CARD =====
// Giống với code Arduino: CS=5, CLK=18, MOSI=19, MISO=21
#define SD_CS   5
#define SD_CLK  18
#define SD_MOSI 19
#define SD_MISO 21

static void sdcard_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "---- SD Card Test ----");

    // Cấu hình mount
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Cấu hình SPI bus với chân custom (giống code Arduino)
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Cấu hình host và slot
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // Giảm tốc độ SPI nếu gặp lỗi (mặc định 20MHz, có thể giảm xuống 10MHz hoặc 5MHz)
    host.max_freq_khz = 10000; // 10MHz - giảm tốc độ để tăng độ ổn định
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS;
    slot_config.host_id = host.slot;

    sdmmc_card_t *card = NULL;

    // Khởi tạo SPI bus
    ESP_LOGI(TAG, "Initializing SPI bus...");
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, 1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "⚠️ Failed to initialize SPI bus! Error: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    // Mount SD card
    ESP_LOGI(TAG, "Mounting SD card...");
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "⚠️ Failed to mount filesystem.");
        }
        else
        {
            ESP_LOGE(TAG, "⚠️ SD Card Mount Failed! Error: %s", esp_err_to_name(ret));
        }
        spi_bus_free(host.slot);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "✅ SD Card Initialized");

    // In thông tin card và dung lượng
    uint64_t card_size = ((uint64_t)card->csd.capacity) * card->csd.sector_size;
    uint64_t card_size_mb = card_size / (1024 * 1024);
    
    // Xác định loại card dựa trên dung lượng
    ESP_LOGI(TAG, "Card Type: ");
    if (card_size > (2ULL * 1024 * 1024 * 1024))
    {
        ESP_LOGI(TAG, "SDHC/SDXC");
    }
    else
    {
        ESP_LOGI(TAG, "SDSC");
    }
    
    ESP_LOGI(TAG, "SD Card Size: %llu MB", card_size_mb);

    // In thông tin chi tiết
    ESP_LOGI(TAG, "SDCard properties:");
    sdmmc_card_print_info(stdout, card);

    // ====== TEST GHI FILE ======
    const char *path = MOUNT_POINT "/test.txt";
    FILE *f = fopen(path, "w");
    if (!f)
    {
        ESP_LOGE(TAG, "⚠️ Failed to open test.txt for writing!");
        goto cleanup;
    }
    
    fprintf(f, "Hello ESP32 SD Card!\n");
    fclose(f);
    ESP_LOGI(TAG, "✅ File written");

    // ====== TEST ĐỌC FILE ======
    f = fopen(path, "r");
    if (!f)
    {
        ESP_LOGE(TAG, "⚠️ Failed to open test.txt for reading!");
        goto cleanup;
    }

    ESP_LOGI(TAG, "----- FILE CONTENT -----");
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), f) != NULL)
    {
        ESP_LOGI(TAG, "%s", buffer);
    }
    ESP_LOGI(TAG, "------------------------");
    fclose(f);

cleanup:
    // Unmount và giải phóng bus
    ESP_LOGI(TAG, "Unmounting SD card...");
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    spi_bus_free(host.slot);
    ESP_LOGI(TAG, "SD card test done.");
    vTaskDelete(NULL);
}

void start_sdcard_test(void)
{
    xTaskCreate(sdcard_test_task, "sdcard_test_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "SD card test task created");
}

