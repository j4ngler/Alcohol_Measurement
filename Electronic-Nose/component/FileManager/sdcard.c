#include "sdcard.h"
#include <string.h>
#include <errno.h>

__attribute__((unused)) static const char *TAG = "SDcard";

// Define mount_point here (declared as extern in header)
const char mount_point[] = MOUNT_POINT;


esp_err_t sdcard_initialize(const esp_vfs_fat_mount_config_t *_mount_config, sdmmc_card_t **_out_sdcard,
                            const sdmmc_host_t *_host, const spi_bus_config_t *_bus_config, sdspi_device_config_t *_slot_config)
{
    esp_err_t err_code;
    ESP_LOGI(__func__, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ESP_LOGI(__func__, "Using SPI peripheral");

    if (_mount_config == NULL || _out_sdcard == NULL || _host == NULL || _bus_config == NULL || _slot_config == NULL) {
        ESP_LOGE(__func__, "Invalid argument(s)");
        return ESP_ERR_INVALID_ARG;
    }

    err_code = spi_bus_initialize(_host->slot, _bus_config, SPI_DMA_CHAN);
    if (err_code != ESP_OK)
    {
        ESP_LOGE(__func__, "Failed to initialize bus.");
        ESP_LOGE(__func__, "Failed to initialize the SDcard.");
        return ESP_ERROR_SD_INIT_FAILED;
    }
    _slot_config->gpio_cs = CONFIG_PIN_NUM_CS;
    _slot_config->host_id = _host->slot;

    ESP_LOGI(__func__, "Mounting filesystem");
    err_code = esp_vfs_fat_sdspi_mount(mount_point, _host, _slot_config, _mount_config, _out_sdcard);
    
    if (err_code != ESP_OK) {
        if (err_code == ESP_FAIL) {
            ESP_LOGE(__func__, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(__func__, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(err_code));
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_free(_host->slot));
        return err_code;
    }
    ESP_LOGI(__func__, "SDCard has been initialized.");
    ESP_LOGI(__func__, "Filesystem mounted");

    // Card has been initialized, print its properties
    ESP_LOGI(__func__, "SDCard properties.");
    sdmmc_card_print_info(stdout, *_out_sdcard);
    return ESP_OK;
}

esp_err_t sdcard_writeStringToFile(const char *nameFile, const char *dataString)
{
    char pathFile[64];
    snprintf(pathFile, sizeof(pathFile), "%s/%s.csv", mount_point, nameFile);

    ESP_LOGI(__func__, "Opening file %s...", pathFile);
    FILE *file = fopen(pathFile, "a");  // Use "a" (append) instead of "a+" for better buffering
    if (file == NULL)
    {
        ESP_LOGE(__func__, "Failed to open file for writing: %s (errno: %d)", pathFile, errno);
        return ESP_ERROR_SD_OPEN_FILE_FAILED;
    }

    int returnValue = 0;
    returnValue = fprintf(file, "%s", dataString);
    if (returnValue < 0)
    {
        ESP_LOGE(__func__, "Failed to write data to file %s.", pathFile);
        fclose(file);
        return ESP_ERROR_SD_WRITE_DATA_FAILED;
    }
    
    // Force flush buffer to ensure data is written to SD card
    // Note: fflush() may fail in some cases, but fsync() is more important
    // On some filesystems, fflush() may return error even when successful
    errno = 0;  // Clear errno before fflush
    int fflush_result = fflush(file);
    int fflush_errno = errno;  // Save errno immediately
    bool fflush_failed = (fflush_result != 0 && fflush_errno != 0 && fflush_errno != EINVAL);
    
    // Only log fflush() error if it's a real error (not just empty buffer or already synced)
    // EINVAL (22) = Invalid argument (can happen if buffer is already empty/synced)
    // EBADF (9) = Bad file descriptor (serious error)
    // EIO (5) = I/O error (serious error - but fsync() will verify if data was actually written)
    if (fflush_failed) {
        if (fflush_errno == EBADF || fflush_errno == EIO) {
            ESP_LOGW(__func__, "⚠️  fflush() error for file %s (errno: %d) - will verify with fsync()", pathFile, fflush_errno);
        } else {
            ESP_LOGD(__func__, "fflush() warning for file %s (errno: %d) - continuing with fsync()", pathFile, fflush_errno);
        }
    }
    
    // Force sync to SD card hardware (critical for data integrity)
    // fsync() is more important than fflush() as it ensures data is physically written
    // If fsync() succeeds, data is guaranteed to be on SD card even if fflush() failed
    int fd = fileno(file);
    bool data_written = false;
    if (fd >= 0) {
        errno = 0;  // Clear errno before fsync
        int fsync_result = fsync(fd);
        int fsync_errno = errno;  // Save errno immediately
        
        if (fsync_result == 0) {
            // fsync() succeeded - data is guaranteed to be written to SD card
            data_written = true;
            if (fflush_failed && (fflush_errno == EIO || fflush_errno == EBADF)) {
                // fflush() failed but fsync() succeeded - data is still safe
                ESP_LOGI(__func__, "✅ Data written successfully (fsync() succeeded despite fflush() error)");
            }
        } else if (fsync_errno != 0) {
            // fsync() failed - this is serious
            if (fsync_errno == EIO) {
                ESP_LOGE(__func__, "❌ fsync() I/O error for file %s (errno: %d) - DATA MAY NOT BE WRITTEN!", pathFile, fsync_errno);
                ESP_LOGE(__func__, "   Check SD card connection and health");
            } else if (fsync_errno == EBADF) {
                ESP_LOGE(__func__, "❌ fsync() bad file descriptor for file %s (errno: %d)", pathFile, fsync_errno);
            } else if (fsync_errno != EINVAL) {
                ESP_LOGW(__func__, "⚠️  fsync() warning for file %s (errno: %d)", pathFile, fsync_errno);
            }
        }
    } else {
        ESP_LOGW(__func__, "Cannot get file descriptor for %s", pathFile);
    }
    
    if (data_written || (!fflush_failed && fd >= 0)) {
        ESP_LOGI(__func__, "✅ Success to write data to file %s.", pathFile);
    } else {
        ESP_LOGW(__func__, "⚠️  Data write completed with warnings for file %s", pathFile);
    }
    fclose(file);
    return ESP_OK;
}

esp_err_t sdcard_writeDataToFile(const char *nameFile, const char *format, ...)
{
    char pathFile[64];
    snprintf(pathFile, sizeof(pathFile), "%s/%s.csv", mount_point, nameFile);

    ESP_LOGI(__func__, "Opening file %s...", pathFile);
    FILE *file = fopen(pathFile, "a");  // Use "a" (append) instead of "a+" for better buffering
    if (file == NULL)
    {
        ESP_LOGE(__func__, "Failed to open file for writing: %s (errno: %d)", pathFile, errno);
        return ESP_ERROR_SD_OPEN_FILE_FAILED;
    }
    
    char *dataString;
    int length;
    va_list argumentsList;
    va_list argumentsList_copy;
    va_start(argumentsList, format);
    va_copy(argumentsList_copy, argumentsList);
    length = vsnprintf(NULL, 0, format, argumentsList_copy);
    va_end(argumentsList_copy);

    if (length < 0) {
        ESP_LOGE(TAG, "Failed to format string data for writing.");
        va_end(argumentsList);
        fclose(file);
        return ESP_ERROR_SD_WRITE_DATA_FAILED;
    }

    dataString = (char*)malloc((size_t)length + 1);
    if(dataString == NULL) {
        ESP_LOGE(TAG, "Failed to create string data for writing.");
        va_end(argumentsList);
        fclose(file);
        return ESP_FAIL;
    }

    vsnprintf(dataString, (size_t)length + 1, format, argumentsList);
    ESP_LOGI(TAG, "Success to create string data(%d) for writing.", length);
    ESP_LOGI(TAG, "Writing data to file %s...", pathFile);
    ESP_LOGI(TAG, "%s;\n", dataString);

    int returnValue = 0;
    returnValue = fprintf(file, "%s", dataString);
    if (returnValue < 0)
    {
        ESP_LOGE(__func__, "Failed to write data to file %s.", pathFile);
        fclose(file);
        va_end(argumentsList);
        free(dataString);
        return ESP_ERROR_SD_WRITE_DATA_FAILED;
    }
    
    // Force flush buffer to ensure data is written to SD card
    // Note: fflush() may fail in some cases, but fsync() is more important
    // On some filesystems, fflush() may return error even when successful
    errno = 0;  // Clear errno before fflush
    int fflush_result = fflush(file);
    int fflush_errno = errno;  // Save errno immediately
    bool fflush_failed = (fflush_result != 0 && fflush_errno != 0 && fflush_errno != EINVAL);
    
    // Only log fflush() error if it's a real error (not just empty buffer or already synced)
    // EINVAL (22) = Invalid argument (can happen if buffer is already empty/synced)
    // EBADF (9) = Bad file descriptor (serious error)
    // EIO (5) = I/O error (serious error - but fsync() will verify if data was actually written)
    if (fflush_failed) {
        if (fflush_errno == EBADF || fflush_errno == EIO) {
            ESP_LOGW(__func__, "⚠️  fflush() error for file %s (errno: %d) - will verify with fsync()", pathFile, fflush_errno);
        } else {
            ESP_LOGD(__func__, "fflush() warning for file %s (errno: %d) - continuing with fsync()", pathFile, fflush_errno);
        }
    }
    
    // Force sync to SD card hardware (critical for data integrity)
    // fsync() is more important than fflush() as it ensures data is physically written
    // If fsync() succeeds, data is guaranteed to be on SD card even if fflush() failed
    int fd = fileno(file);
    bool data_written = false;
    if (fd >= 0) {
        errno = 0;  // Clear errno before fsync
        int fsync_result = fsync(fd);
        int fsync_errno = errno;  // Save errno immediately
        
        if (fsync_result == 0) {
            // fsync() succeeded - data is guaranteed to be written to SD card
            data_written = true;
            if (fflush_failed && (fflush_errno == EIO || fflush_errno == EBADF)) {
                // fflush() failed but fsync() succeeded - data is still safe
                ESP_LOGI(__func__, "✅ Data written successfully (fsync() succeeded despite fflush() error)");
            }
        } else if (fsync_errno != 0) {
            // fsync() failed - this is serious
            if (fsync_errno == EIO) {
                ESP_LOGE(__func__, "❌ fsync() I/O error for file %s (errno: %d) - DATA MAY NOT BE WRITTEN!", pathFile, fsync_errno);
                ESP_LOGE(__func__, "   Check SD card connection and health");
            } else if (fsync_errno == EBADF) {
                ESP_LOGE(__func__, "❌ fsync() bad file descriptor for file %s (errno: %d)", pathFile, fsync_errno);
            } else if (fsync_errno != EINVAL) {
                ESP_LOGW(__func__, "⚠️  fsync() warning for file %s (errno: %d)", pathFile, fsync_errno);
            }
        }
    } else {
        ESP_LOGW(__func__, "Cannot get file descriptor for %s", pathFile);
    }
    
    if (data_written || (!fflush_failed && fd >= 0)) {
        ESP_LOGI(__func__, "✅ Success to write data to file %s.", pathFile);
    } else {
        ESP_LOGW(__func__, "⚠️  Data write completed with warnings for file %s", pathFile);
    }
    fclose(file);
    va_end(argumentsList);
    free(dataString);
    return ESP_OK;
}


esp_err_t sdcard_readDataToFile(const char *nameFile, const char *format, ...)
{
    char pathFile[64];
    snprintf(pathFile, sizeof(pathFile), "%s/%s.csv", mount_point, nameFile);

    ESP_LOGI(__func__, "Opening file %s...", pathFile);
    FILE *file = fopen(pathFile, "r");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading.");
        return ESP_ERROR_SD_OPEN_FILE_FAILED;
    }

    // Read a string data from file
    char dataStr[256];
    char *returnPtr;
    returnPtr = fgets(dataStr, sizeof(dataStr), file);
    fclose(file);
    
    if (returnPtr == NULL)
    {
        ESP_LOGE(__func__, "Failed to read data from file %s.", pathFile);
        return ESP_ERROR_SD_READ_DATA_FAILED;
    }

    va_list argumentsList;
    va_start(argumentsList, format);
    int returnValue = 0;
    returnValue = vsscanf(dataStr, format, argumentsList);
    va_end(argumentsList);

    if (returnValue < 0)
    {
        ESP_LOGE(__func__, "Failed to read data from file %s.", pathFile);
        return ESP_ERROR_SD_READ_DATA_FAILED;
    }
    
    return ESP_OK;
}

esp_err_t sdcard_renameFile(const char *oldNameFile, char *newNameFile)
{
    // Check if destination file exists before renaming
    struct stat st;
    char _oldNameFile[64];
    char _newNameFile[64];
    snprintf(_oldNameFile, sizeof(_oldNameFile), "%s/%s.csv", mount_point, oldNameFile);
    snprintf(_newNameFile, sizeof(_newNameFile), "%s/%s.csv", mount_point, newNameFile);
    ESP_LOGI(__func__, "Update file name from %s to %s", _oldNameFile, _newNameFile);

    if (stat(_newNameFile, &st) == 0) {
        ESP_LOGE(__func__, "File \"%s\" exists.", _newNameFile);
        return ESP_ERROR_SD_RENAME_FILE_FAILED;
    }

    // Rename original file
    ESP_LOGI(__func__, "Renaming file %s to %s", _oldNameFile, _newNameFile);
    if (rename(_oldNameFile, _newNameFile) != 0) 
    {
        ESP_LOGE(__func__, "Rename failed");
        return ESP_ERROR_SD_RENAME_FILE_FAILED;
    } else {
        ESP_LOGI(__func__, "Rename successful");
        return ESP_OK;
    }
}

esp_err_t sdcard_removeFile(const char *nameFile)
{
    struct stat st;
    char _nameFile[64];
    snprintf(_nameFile, sizeof(_nameFile), "%s/%s.csv", mount_point, nameFile);
    
    // Check whether destination file exists or not
    if (stat(_nameFile, &st) != 0) {
        ESP_LOGE(__func__, "File \"%s\" doesn't exists.", _nameFile);
        return ESP_ERROR_SD_REMOVE_FILE_FAILED;
    }

    if (remove(_nameFile) != 0) 
    {
        ESP_LOGE(__func__, "Remove failed");
        return ESP_ERROR_SD_REMOVE_FILE_FAILED;
    } else {
        ESP_LOGW(__func__, "Remove successful");
        return ESP_OK;
    }

}


esp_err_t sdcard_deinitialize(const char* _mount_point, sdmmc_card_t *_sdcard, sdmmc_host_t *_host)
{
    ESP_LOGI(__func__, "Deinitializing SD card...");
    // Unmount partition and disable SPI peripheral.
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_fat_sdcard_unmount(_mount_point, _sdcard));
    ESP_LOGI(__func__, "Card unmounted.");

    //deinitialize the bus after all devices are removed
    ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_free(_host->slot));
    return ESP_OK;
}


