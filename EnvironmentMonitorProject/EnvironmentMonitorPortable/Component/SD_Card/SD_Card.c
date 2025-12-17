#include "SD_Card.h"

sdmmc_card_t *sdcard; // Toàn cục để hàm nào cũng dùng được
esp_err_t initSDCard(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = CONFIG_SPI_HOST_SDCARD; 

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_MOSI_SDCARD,
        .miso_io_num = CONFIG_MISO_SDCARD,
        .sclk_io_num = CONFIG_CLK_SDCARD,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Chỉ định DMA channel 2 cho VSPI
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, 2); 
    if (ret != ESP_OK)
    {
        printf("Failed to initialize VSPI bus (DMA ch 2). Err: 0x%x\n", ret);
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_CS_SDCARD;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &sdcard);
    if (ret != ESP_OK)
    {
        printf("Failed to mount filesystem. err: 0x%x\n", ret);
        spi_bus_free(host.slot);
        return ret;
    }

    printf("SD Card mounted successfully on VSPI (DMA ch 2)!\n");
    sdmmc_card_print_info(stdout, sdcard);
    return ESP_OK;
}

esp_err_t writeSD_Card(const char *filename, const char *data)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, filename);

    FILE *f = fopen(path, "w"); // Ghi đè file
    if (f == NULL)
    {
        printf("Failed to open file %s for writing\n", path);
        return ESP_FAIL;
    }
    fprintf(f, "%s", data);
    fclose(f);
    printf("Wrote to file %s\n", path);
    return ESP_OK;
}

esp_err_t writeFinalFileSD_Card(const char *filename, const char *data)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, filename);

    FILE *f = fopen(path, "a+"); // Ghi đè file
    if (f == NULL)
    {
        printf("Failed to open file %s for writing\n", path);
        return ESP_FAIL;
    }
    fprintf(f, "%s\n", data);
    fclose(f);
    // printf("Wrote to file %s\n", path);
    return ESP_OK;
}

esp_err_t readSD_Card(const char *filename, char *out_buf, size_t buf_size)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, filename);

    FILE *f = fopen(path, "r");
    if (f == NULL)
    {
        printf("Failed to open file %s for reading\n", path);
        return ESP_FAIL;
    }
    if (fgets(out_buf, buf_size, f) == NULL)
    {
        printf("File %s is empty or error.\n", path);
        fclose(f);
        return ESP_FAIL;
    }
    fclose(f);
    printf("Read from file %s: %s\n", path, out_buf);
    return ESP_OK;
}

esp_err_t renameSD_Card(const char *oldname, const char *newname)
{
    char oldpath[128];
    char newpath[128];
    snprintf(oldpath, sizeof(oldpath), "%s/%s", MOUNT_POINT, oldname);
    snprintf(newpath, sizeof(newpath), "%s/%s", MOUNT_POINT, newname);

    // Kiểm tra file gốc có tồn tại không
    struct stat st;
    if (stat(oldpath, &st) != 0)
    {
        printf("File %s không tồn tại, không thể rename!\n", oldpath);
        return ESP_FAIL;
    }

    // Nếu file mới đã tồn tại thì xóa đi
    if (stat(newpath, &st) == 0)
    {
        printf("File đích %s đã tồn tại, sẽ bị xóa!\n", newpath);
        unlink(newpath);
    }

    // Đổi tên file
    if (rename(oldpath, newpath) != 0)
    {
        printf("Đổi tên file thất bại!\n");
        return ESP_FAIL;
    }

    printf("Đã đổi tên file từ %s sang %s\n", oldpath, newpath);
    return ESP_OK;
}