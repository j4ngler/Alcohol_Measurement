#include "TaskManager.h"

dataManager_t DataManager = {0};
i2c_dev_t ds3231_device;
struct tm timeDS3231;

// ADS111x (ADC đo nồng độ cồn) - dùng 1 chip ADS1115
static i2c_dev_t ads111x_device = {0};

// Queue cho giao tiếp giữa các task
static QueueHandle_t sensor_data_queue = NULL;  // Producer -> Distributor
static QueueHandle_t display_data_queue = NULL; // Distributor -> Display
static QueueHandle_t savedata_queue = NULL;     // Distributor -> SaveData

// TaskH
static TaskHandle_t readAllSensorsTaskHandle = NULL;
static TaskHandle_t sensorDataDistributorHandle = NULL;
static TaskHandle_t displayAndSendDataTaskHandle = NULL;
static TaskHandle_t syncTimeSNTPHandle = NULL;
static TaskHandle_t SaveDataHandle = NULL;
static TaskHandle_t wifi_manager_taskHandle = NULL;
static TaskHandle_t wifi_connect_taskHandle = NULL;
static TaskHandle_t WebSocket_HandlerHandle = NULL;

void sntpGetTime(void) {
  time_t timeNow = 0;
  struct tm timeInfo = {0};
  sntp_init_func();
  ESP_ERROR_CHECK_WITHOUT_ABORT(sntp_setTime(&timeInfo, &timeNow));
  mktime(&timeInfo);
  if (timeInfo.tm_year < 130 && timeInfo.tm_year > 120) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(ds3231_set_time(&ds3231_device, &timeInfo));
  }
  vTaskDelete(NULL);
}

void syncTimeSNTP(void *pvParameter) {
  if (is_wifi_connected()) {
    sntpGetTime();
  }
}

void SaveData(void *pvParameter) {
  dataManager_t sensor_data;

  while (1) {
    // Kiểm tra queue có dữ liệu trước khi nhận (bỏ qua nếu queue rỗng)
    if (uxQueueMessagesWaiting(savedata_queue) > 0) {
      // Nhận dữ liệu từ SaveData queue (non-blocking vì đã check có data)
      if (xQueueReceive(savedata_queue, &sensor_data, 0) == pdTRUE) {
        // Parse timestamp từ sensor_data.Timestamp
        // Format: "YYYY-MM-DDTHH:MM:SS"
        struct tm saveTime;
        if (sscanf(sensor_data.Timestamp, "%d-%d-%dT%d:%d:%d",
                   &saveTime.tm_year, &saveTime.tm_mon, &saveTime.tm_mday,
                   &saveTime.tm_hour, &saveTime.tm_min,
                   &saveTime.tm_sec) == 6) {
          saveTime.tm_year -= 1900; // Adjust for struct tm
          saveTime.tm_mon -= 1;

          snprintf(DataManager.NameFileSD, sizeof(DataManager.NameFileSD),
                   "%02d-%02d-%02d.txt", saveTime.tm_year % 100,
                   saveTime.tm_mon + 1, saveTime.tm_mday);

          snprintf(
              DataManager.FormatDataToSD, sizeof(DataManager.FormatDataToSD),
              "%d:%d:%d-T%fH%fP%fM1%fM2%fM3%fS%s", saveTime.tm_hour,
              saveTime.tm_min, saveTime.tm_sec, sensor_data.Temperature_Value,
              sensor_data.Humidity_Value, sensor_data.Pressure_Value,
              sensor_data.PM1_0_Value, sensor_data.PM2_5_Value,
              sensor_data.PM10_Value, sensor_data.statusDevice);

          if (writeFinalFileSD_Card(DataManager.NameFileSD,
                                    DataManager.FormatDataToSD) == ESP_OK) {
            // ESP_LOGI(TAG_TASK_MANAGER, "Data saved to SD card
            // successfully!");
          } else {
            ESP_LOGW(TAG_TASK_MANAGER, "Failed to save data to SD card");
          }
        } else {
          ESP_LOGE(TAG_TASK_MANAGER, "Failed to parse timestamp");
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Small delay để không tốn CPU
  }
}

void sensorDataDistributor(void *pvParameter) {
  dataManager_t sensor_data;

  ESP_LOGI(TAG_TASK_MANAGER, "Sensor data distributor task started");

  while (1) {
    // Kiểm tra sensor queue có dữ liệu trước khi nhận (bỏ qua nếu queue rỗng)
    if (uxQueueMessagesWaiting(sensor_data_queue) > 0) {
      // Nhận dữ liệu từ sensor queue (non-blocking vì đã check có data)
      if (xQueueReceive(sensor_data_queue, &sensor_data, 0) == pdTRUE) {
        // Phân phối đến display queue
        // Kiểm tra số message còn lại trong queue
        if (uxQueueMessagesWaiting(display_data_queue) >= 4) {
          // ESP_LOGW(TAG_TASK_MANAGER, "Display queue almost full: %d/5",
          //          uxQueueMessagesWaiting(display_data_queue));
        }

        if (xQueueSend(display_data_queue, &sensor_data, 0) != pdTRUE) {
          // ESP_LOGW(TAG_TASK_MANAGER, "Display queue full");
        }

        // Phân phối đến SaveData queue
        // Kiểm tra số message còn lại trong queue
        if (uxQueueMessagesWaiting(savedata_queue) >= 4) {
          // ESP_LOGW(TAG_TASK_MANAGER, "SaveData queue almost full: %d/5",
          //          uxQueueMessagesWaiting(savedata_queue));
        }

        if (xQueueSend(savedata_queue, &sensor_data, 0) != pdTRUE) {
          // ESP_LOGW(TAG_TASK_MANAGER, "SaveData queue full");
        }

        // ESP_LOGI(TAG_TASK_MANAGER, "Data distributed to all consumers");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(500)); // Small delay để không tốn CPU
  }
}

void displayAndSendDataTask(void *pvParameter) {
  dataManager_t sensor_data;

  while (1) {
    // Kiểm tra queue có dữ liệu trước khi nhận (bỏ qua nếu queue rỗng)
    if (uxQueueMessagesWaiting(display_data_queue) > 0) {
      // Nhận dữ liệu từ display queue (non-blocking vì đã check có data)
      if (xQueueReceive(display_data_queue, &sensor_data, 0) == pdTRUE) {
        // Gửi dữ liệu qua WebSocket (không hiển thị OLED)
        DataToSever(&sensor_data);
        ESP_LOGI(TAG_TASK_MANAGER, "Data sent to WebSocket");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Small delay để không tốn CPU
  }
}

void readAllSensorsTask(void *pvParameter) {
  dataManager_t sensor_data;
  int16_t adc_raw = 0;

  // Khởi tạo sensor_data
  memset(&sensor_data, 0, sizeof(dataManager_t));
  while (1) {
    // 1. Cập nhật timestamp từ DS3231
    ds3231_get_time(&ds3231_device, &timeDS3231);
    ds3231_get_time_str(&timeDS3231, sensor_data.Timestamp,
                        sizeof(sensor_data.Timestamp));

    // 2. Không dùng BME/PMS nữa, chỉ đọc ADC từ ADS111x
    for (int ch = 0; ch < 3; ch++) {
      ads111x_set_input_mux(&ads111x_device,
                            (ads111x_mux_t)(ADS111X_MUX_0_GND + ch));
      vTaskDelay(pdMS_TO_TICKS(50));

      if (ads111x_get_value(&ads111x_device, &adc_raw) == ESP_OK) {
        switch (ch) {
        case 0:
          sensor_data.PM1_0_Value = (float)adc_raw;
          break;
        case 1:
          sensor_data.PM2_5_Value = (float)adc_raw;
          break;
        case 2:
          sensor_data.PM10_Value = (float)adc_raw;
          break;
        default:
          break;
        }
      } else {
        switch (ch) {
        case 0:
          sensor_data.PM1_0_Value = 0;
          break;
        case 1:
          sensor_data.PM2_5_Value = 0;
          break;
        case 2:
          sensor_data.PM10_Value = 0;
          break;
        default:
          break;
        }
      }
    }

    // 3. Gửi dữ liệu vào queue cho Distributor task
    if (uxQueueMessagesWaiting(sensor_data_queue) >= 4) {
      // queue gần đầy, có thể log nếu cần
    }

    if (xQueueSend(sensor_data_queue, &sensor_data, 0) != pdTRUE) {
      ESP_LOGW(TAG_TASK_MANAGER,
               "Failed to send sensor data to distributor queue - queue full");
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Đọc cảm biến mỗi 1 giây
  }
}

void TaskManagerInit(void) {

  // Khởi tạo các queues
  sensor_data_queue = xQueueCreate(5, sizeof(dataManager_t));
  if (sensor_data_queue == NULL) {
    ESP_LOGE(TAG_TASK_MANAGER, "Failed to create sensor data queue");
    return;
  }
  ESP_LOGI(TAG_TASK_MANAGER, "Sensor data queue created");

  display_data_queue = xQueueCreate(5, sizeof(dataManager_t));
  if (display_data_queue == NULL) {
    ESP_LOGE(TAG_TASK_MANAGER, "Failed to create display data queue");
    return;
  }
  ESP_LOGI(TAG_TASK_MANAGER, "Display data queue created");

  savedata_queue = xQueueCreate(5, sizeof(dataManager_t));
  if (savedata_queue == NULL) {
    ESP_LOGE(TAG_TASK_MANAGER, "Failed to create SaveData queue");
    return;
  }
  ESP_LOGI(TAG_TASK_MANAGER, "SaveData queue created");

  ESP_ERROR_CHECK(i2cdev_init());
  memset(&ds3231_device, 0, sizeof(i2c_dev_t));
  ESP_ERROR_CHECK(ds3231_init_desc(&ds3231_device, CONFIG_RTC_I2C_PORT,
                                   CONFIG_RTC_PIN_NUM_SDA,
                                   CONFIG_RTC_PIN_NUM_SCL));

  // Khởi tạo ADS111x (đo ADC cho cảm biến nồng độ cồn)
  memset(&ads111x_device, 0, sizeof(ads111x_device));
  ESP_ERROR_CHECK(ads111x_init_desc(&ads111x_device, ADS111X_ADDR_GND,
                                    CONFIG_RTC_I2C_PORT, CONFIG_RTC_PIN_NUM_SDA,
                                    CONFIG_RTC_PIN_NUM_SCL));
  ESP_ERROR_CHECK(ads111x_set_mode(&ads111x_device, ADS111X_MODE_CONTINUOUS));
  ESP_ERROR_CHECK(
      ads111x_set_data_rate(&ads111x_device, ADS111X_DATA_RATE_128));
  ESP_ERROR_CHECK(
      ads111x_set_gain(&ads111x_device, ADS111X_GAIN_2V048));

  // Không dùng OLED nữa - bỏ qua phần khởi tạo ssd1306
  initSDCard();
}
void suspendAllTask(void) {
  vTaskSuspend(readAllSensorsTaskHandle);
  vTaskSuspend(sensorDataDistributorHandle);
  vTaskSuspend(displayAndSendDataTaskHandle);
  vTaskSuspend(syncTimeSNTPHandle);
  vTaskSuspend(SaveDataHandle);
  vTaskSuspend(wifi_manager_taskHandle);
  vTaskSuspend(wifi_connect_taskHandle);
  ESP_LOGI(TAG_TASK_MANAGER, "All tasks suspended");
}

void resumeAllTask(void) {
  vTaskResume(readAllSensorsTaskHandle);
  vTaskResume(sensorDataDistributorHandle);
  vTaskResume(displayAndSendDataTaskHandle);
  vTaskResume(syncTimeSNTPHandle);
  vTaskResume(SaveDataHandle);
  vTaskResume(wifi_manager_taskHandle);
  vTaskResume(wifi_connect_taskHandle);
  ESP_LOGI(TAG_TASK_MANAGER, "All tasks resumed");
}

void AllTasksRun(void) {
  //   printf("Capacity heap: %ld bytes\n", esp_get_free_heap_size());
  // Tạo các tasks theo thứ tự priority
  // Priority 7: Read sensors (Producer)
  // Priority 6: Distributor (Router)
  // Priority 5: Display (Consumer)
  // Priority 8: SaveData (Consumer)
  xTaskCreate(readAllSensorsTask, "ReadAllSensors", 4096, NULL, 7,
              &readAllSensorsTaskHandle);
  xTaskCreate(sensorDataDistributor, "DataDistributor", 4096, NULL, 6,
              &sensorDataDistributorHandle);
    xTaskCreate(displayAndSendDataTask, "DisplayAndSendData", 4096, NULL, 5,
    &displayAndSendDataTaskHandle);
  xTaskCreate(syncTimeSNTP, "Sync Time SNTP", 4096, NULL, 12,
              &syncTimeSNTPHandle);
  xTaskCreate(WebSocket_Handler, "WebSocket Handler", 4096, NULL, 11, &WebSocket_HandlerHandle);
  xTaskCreate(SaveData, "Save Data to SD Card", 4096, NULL, 8, &SaveDataHandle);
  xTaskCreate(wifi_manager_task, "WiFi Manager", 4096, NULL, 9,
              &wifi_manager_taskHandle);
  xTaskCreate(wifi_connect_task, "WiFi Connect", 4096, NULL, 10,
              &wifi_connect_taskHandle);
  ESP_LOGI(TAG_TASK_MANAGER, "All tasks created successfully");
  vTaskDelete(NULL);
}

