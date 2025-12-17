#include "main.h"



void app_main(void) {

  // Initialize NVS
  ESP_ERROR_CHECK(initNVS());
  ESP_ERROR_CHECK(init_spiffs());
  wifi_init_sta();
  esp_log_level_set(TAG, ESP_LOG_INFO);
  print_partition_info();

  TaskManagerInit();
  AllTasksRun();
}
