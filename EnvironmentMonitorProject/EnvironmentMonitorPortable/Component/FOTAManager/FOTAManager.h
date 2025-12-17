#ifndef FOTA_MANAGER_H
#define FOTA_MANAGER_H

#include "DataManager.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "TaskManager.h"

// Function declarations
esp_err_t check_firmware_version(dataManager_t *DataManager);
esp_err_t do_manual_http_ota(const char *url);
esp_err_t save_firmware_info(const char *version, const char *filename,dataManager_t *DataManager);
esp_err_t load_firmware_info(char *version, char *filename,dataManager_t *DataManager);
esp_err_t init_firmware_info_partition(dataManager_t *DataManager);
void FOTA_task(void *pvParameters);
// Hàm debug partition info
void print_partition_info(void);

#endif // FOTA_MANAGER_H