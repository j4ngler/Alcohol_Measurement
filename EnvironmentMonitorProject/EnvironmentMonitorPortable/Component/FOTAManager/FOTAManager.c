#include "FOTAManager.h"
static const char *TAG = "FOTAManager";

static inline int compare_version(const char *v1, const char *v2) {
  int maj1, min1, patch1;
  int maj2, min2, patch2;
  sscanf(v1, "%d.%d.%d", &maj1, &min1, &patch1);
  sscanf(v2, "%d.%d.%d", &maj2, &min2, &patch2);
  if (maj1 != maj2)
    return maj1 - maj2;
  if (min1 != min2)
    return min1 - min2;
  return patch1 - patch2;
}

esp_err_t check_firmware_version(dataManager_t *DataManager) {
  char url[128];
  snprintf(url, sizeof(url), "http://%s:%d/firmware/version",
           CONFIG_HOST_IP_ADDR, CONFIG_HTTP_PORT);
  ESP_LOGI(TAG, "URL: %s", url);
  esp_http_client_config_t config = {
      .url = url,
      .timeout_ms = 10000,
      .keep_alive_enable = true,
      .buffer_size = 1024,
      .buffer_size_tx = 1024,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    return ESP_FAIL;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return err;
  }

  int content_length = esp_http_client_fetch_headers(client);
  ESP_LOGI(TAG, "Content-Length: %d", content_length);

  char buffer[128] = {0};
  int read_len =
      esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
  ESP_LOGI(TAG, "Read len: %d", read_len);
  ESP_LOGI(TAG, "JSON Response: %s", buffer);

  if (read_len > 0) {
    cJSON *root = cJSON_Parse(buffer);
    if (root) {
      cJSON *version = cJSON_GetObjectItem(root, "version");
      cJSON *filename = cJSON_GetObjectItem(root, "filename");
      if (cJSON_IsString(version) && version->valuestring &&
          cJSON_IsString(filename) && filename->valuestring) {
        strncpy(DataManager->firmwareFileName, filename->valuestring,
                sizeof(DataManager->firmwareFileName) - 1);
        strncpy(DataManager->NewfirmwareVersion, version->valuestring,
                sizeof(DataManager->NewfirmwareVersion) - 1);

        // Kiểm tra version format hợp lệ
        int maj, min, patch;
        if (sscanf(DataManager->NewfirmwareVersion, "%d.%d.%d", &maj, &min,
                   &patch) == 3) {
          if (compare_version(DataManager->CurfirmwareVersion,
                              DataManager->NewfirmwareVersion) < 0) {
            // Chỉ set isNewFirmware = true nếu chưa được set
            if (!DataManager->isNewFirmware) {
              DataManager->isNewFirmware = true;
              ESP_LOGI(TAG, "New firmware detected, will update on next cycle");
            }
            ESP_LOGI(TAG, "Firmware version: %s",
                     DataManager->NewfirmwareVersion);

            // Lưu filename đầy đủ (bao gồm version)
            snprintf(DataManager->firmwareUrl, sizeof(DataManager->firmwareUrl),
                     "%s-%s.bin", DataManager->firmwareFileName,
                     DataManager->NewfirmwareVersion);
            ESP_LOGI(TAG, "Firmware filename: %s", DataManager->firmwareUrl);
          } else {
            DataManager->isNewFirmware = false;
            ESP_LOGI(TAG, "Current firmware is up to date");
          }
        } else {
          ESP_LOGE(TAG, "Invalid version format: %s",
                   DataManager->NewfirmwareVersion);
          DataManager->isNewFirmware = false;
        }

        ESP_LOGI(TAG, "Firmware version: %s", DataManager->CurfirmwareVersion);
        ESP_LOGI(TAG, "Firmware filename: %s", DataManager->firmwareFileName);
      }
      cJSON_Delete(root);
    } else {
      ESP_LOGE(TAG, "JSON parse error");
    }
  } else {
    ESP_LOGE(TAG, "No data or error!");
  }

  esp_http_client_cleanup(client);
  return ESP_OK;
}

// Hàm HTTP OTA thủ công
esp_err_t do_manual_http_ota(const char *url) {
  ESP_LOGI(TAG, "Starting manual HTTP OTA from: %s", url);
  esp_http_client_config_t config = {
      .url = url,
      .timeout_ms = 60000,
      .keep_alive_enable = true,
      .buffer_size = 2048,
      .buffer_size_tx = 2048,
  };

  ESP_LOGI(TAG, "HTTP client config: url=%s, timeout=%dms", config.url,
           config.timeout_ms);

  // Initialize HTTP client
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    // Khôi phục task nếu FOTA thất bại
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Reset trạng thái firmware để lần sau check lại
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "HTTP client initialized successfully");

  // Open connection
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Reset trạng thái firmware để lần sau check lại
    return err;
  }

  // Get content length
  int content_length = esp_http_client_fetch_headers(client);
  if (content_length <= 0) {
    ESP_LOGE(TAG, "Invalid content length: %d", content_length);
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Reset trạng thái firmware để lần sau check lại
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Content length: %d bytes", content_length);

  // Get OTA partition
  const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
  if (update == NULL) {
    ESP_LOGE(TAG, "No OTA partition available");
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Reset trạng thái firmware để lần sau check lại
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%" PRIx32,
           update->subtype, update->address);

  // Begin OTA update
  esp_ota_handle_t ota_handle;
  err = esp_ota_begin(update, content_length, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Reset trạng thái firmware để lần sau check lại
    return err;
  }

  // Download and write firmware
  char buffer[2048]; // Changed from uint8_t to char
  int total_read = 0;
  int read_len;

  while (total_read < content_length) {
    read_len = esp_http_client_read_response(client, buffer, sizeof(buffer));
    if (read_len <= 0) {
      ESP_LOGE(TAG, "Error reading response: %d", read_len);
      esp_ota_abort(ota_handle);
      esp_http_client_cleanup(client);
      vTaskDelay(pdMS_TO_TICKS(2000));
      // Reset trạng thái firmware để lần sau check lại
      return ESP_FAIL;
    }

    err = esp_ota_write(ota_handle, (const void *)buffer, read_len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
      esp_ota_abort(ota_handle);
      esp_http_client_cleanup(client);
      vTaskDelay(pdMS_TO_TICKS(2000));
      // Reset trạng thái firmware để lần sau check lại
      return err;
    }

    total_read += read_len;
    ESP_LOGI(TAG, "Downloaded %d/%d bytes (%.1f%%)", total_read, content_length,
             (float)total_read * 100 / content_length);
  }

  // End OTA update
  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Reset trạng thái firmware để lần sau check lại
    return err;
  }

  // Set boot partition để bootloader biết phải boot từ partition mới
  err = esp_ota_set_boot_partition(update);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
             esp_err_to_name(err));
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(2000));
    return err;
  }

  ESP_LOGI(TAG, "Boot partition set to: %s (subtype %d)", update->label,
           update->subtype);

  // Verify boot partition was set correctly
  const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
  if (boot_partition != NULL) {
    ESP_LOGI(TAG,
             "Verified boot partition: %s (subtype %d) at offset 0x%" PRIx32,
             boot_partition->label, boot_partition->subtype,
             boot_partition->address);
  } else {
    ESP_LOGE(TAG, "Failed to verify boot partition!");
  }

  esp_http_client_cleanup(client);
  ESP_LOGI(TAG, "Manual HTTP OTA successful. Rebooting...");

  // Thêm delay ngắn để đảm bảo log được in ra
  vTaskDelay(pdMS_TO_TICKS(1000));

  ESP_LOGI(TAG, "About to restart...");
  esp_restart();

  return ESP_OK;
}

// Hàm lưu thông tin firmware vào NVS chính
esp_err_t save_firmware_info(const char *version, const char *filename,
                             dataManager_t *DataManager) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("firmware", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS firmware namespace: %s",
             esp_err_to_name(err));
    return err;
  }

  // Lưu version
  err = nvs_set_str(nvs_handle, "version", version);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save version: %s", esp_err_to_name(err));
    nvs_close(nvs_handle);
    return err;
  }

  // Lưu filename
  err = nvs_set_str(nvs_handle, "filename", filename);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save filename: %s", esp_err_to_name(err));
    nvs_close(nvs_handle);
    return err;
  }

  // Commit thay đổi
  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit firmware info: %s", esp_err_to_name(err));
    nvs_close(nvs_handle);
    return err;
  }

  nvs_close(nvs_handle);
  ESP_LOGI(TAG, "Firmware info saved successfully: version=%s, filename=%s",
           version, filename);
  return ESP_OK;
}

// Hàm đọc thông tin firmware từ NVS chính
esp_err_t load_firmware_info(char *version, char *filename,
                             dataManager_t *DataManager) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("firmware", NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGI(TAG, "Firmware namespace not found (first boot)");
    } else {
      ESP_LOGE(TAG, "Failed to open NVS firmware namespace: %s",
               esp_err_to_name(err));
    }
    return err;
  }

  size_t version_len = 32;
  size_t filename_len = 128;

  // Đọc version
  err = nvs_get_str(nvs_handle, "version", version, &version_len);
  if (err != ESP_OK) {
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGI(TAG, "Version key not found in firmware info");
    } else {
      ESP_LOGE(TAG, "Failed to read version: %s", esp_err_to_name(err));
    }
    nvs_close(nvs_handle);
    return err;
  }

  // Đọc filename
  err = nvs_get_str(nvs_handle, "filename", filename, &filename_len);
  if (err != ESP_OK) {
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGI(TAG, "Filename key not found in firmware info");
    } else {
      ESP_LOGE(TAG, "Failed to read filename: %s", esp_err_to_name(err));
    }
    nvs_close(nvs_handle);
    return err;
  }

  nvs_close(nvs_handle);

  // Kiểm tra xem có phải giá trị mặc định (toàn bộ 0xFF) không
  bool is_default_version = true;
  bool is_default_filename = true;

  for (int i = 0; i < strlen(version); i++) {
    if ((unsigned char)version[i] != 0xFF) {
      is_default_version = false;
      break;
    }
  }

  for (int i = 0; i < strlen(filename); i++) {
    if ((unsigned char)filename[i] != 0xFF) {
      is_default_filename = false;
      break;
    }
  }

  if (is_default_version || is_default_filename) {
    ESP_LOGI(TAG, "Detected default values (0xFF) in firmware info");
    return ESP_ERR_NVS_NOT_FOUND; // Coi như chưa có dữ liệu
  }

  ESP_LOGI(TAG, "Firmware info loaded successfully: version=%s, filename=%s",
           version, filename);
  return ESP_OK;
}

// Hàm khởi tạo phân vùng firmware_info với giá trị mặc định
esp_err_t init_firmware_info_partition(dataManager_t *DataManager) {
  ESP_LOGI(TAG, "Initializing firmware_info partition with default values");

  // Giá trị mặc định
  const char *default_version = "1.0.0";
  const char *default_filename = "MeasureMotor";

  // Lưu giá trị mặc định vào phân vùng
  esp_err_t err =
      save_firmware_info(default_version, default_filename, DataManager);
  if (err == ESP_OK) {
    ESP_LOGI(TAG,
             "Firmware info partition initialized with defaults: version=%s, "
             "filename=%s",
             default_version, default_filename);

    // Cập nhật DataManager với giá trị mặc định
    strncpy(DataManager->CurfirmwareVersion, default_version,
            sizeof(DataManager->CurfirmwareVersion) - 1);
    strncpy(DataManager->firmwareFileName, default_filename,
            sizeof(DataManager->firmwareFileName) - 1);

    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to initialize firmware info partition: %s",
             esp_err_to_name(err));
    return err;
  }
}

// Hàm debug partition info
void print_partition_info(void) {
  ESP_LOGI(TAG, "=== Partition Information ===");

  // Get running partition
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running != NULL) {
    ESP_LOGI(TAG, "Running partition: %s (subtype %d) at offset 0x%" PRIx32,
             running->label, running->subtype, running->address);
  } else {
    ESP_LOGE(TAG, "Failed to get running partition!");
  }

  // Get boot partition
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  if (boot != NULL) {
    ESP_LOGI(TAG, "Boot partition: %s (subtype %d) at offset 0x%" PRIx32,
             boot->label, boot->subtype, boot->address);
  } else {
    ESP_LOGE(TAG, "Failed to get boot partition!");
  }

  // Get next update partition
  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  if (next != NULL) {
    ESP_LOGI(TAG, "Next update partition: %s (subtype %d) at offset 0x%" PRIx32,
             next->label, next->subtype, next->address);
  } else {
    ESP_LOGE(TAG, "Failed to get next update partition!");
  }

  ESP_LOGI(TAG, "=============================");
}

void FOTA_task(void *pvParameters) {
  char *firmwareVersion = (char *)pvParameters;
  while (1) {
    if (firmwareVersion) {
      // Tạo URL đầy đủ
      char full_url[256];
      snprintf(full_url, sizeof(full_url), "http://%s:%d/api/firmware/download/%s",
               CONFIG_HOST_IP_ADDR, CONFIG_HTTP_PORT, firmwareVersion);

      ESP_LOGI(TAG, "Updating firmware to version: %s",
               firmwareVersion);
      ESP_LOGI(TAG, "Full URL: %s", full_url);
      firmwareVersion = NULL;
      suspendAllTask();
      esp_err_t ret = do_manual_http_ota(full_url);
      if (ret == ESP_OK) {
        // Nếu FOTA thành công, ESP sẽ restart
        break;
      } else {
        // Nếu FOTA thất bại, reset flag và tiếp tục
        ESP_LOGE(TAG, "FOTA failed, will retry later");
        resumeAllTask();
        vTaskDelay(pdMS_TO_TICKS(30000)); // Đợi 30s trước khi thử lại
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Kiểm tra mỗi giây
  }
}