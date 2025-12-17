#include "WSHandle.h"
// WebSocket client handle
static esp_websocket_client_handle_t client = NULL;

#define TAG_WEBSOCKET "WebSocket Handler"

// Lưu giá trị cuối cùng nhận được từ WS
static char ws_last_type[32] = {0};
static char ws_last_version[32] = {0};

void SendSignalRegister(void) {
  cJSON *data = cJSON_CreateObject();
  cJSON_AddStringToObject(data, "type", "register");
  cJSON_AddStringToObject(data, "clientType", "esp32");

  char *json_str = cJSON_PrintUnformatted(data);
  printf("JSON: %s\n", json_str);

  esp_websocket_client_send_text(client, json_str, strlen(json_str),
                                 portMAX_DELAY);

  free(json_str);     // Free chuỗi được malloc bên trong cJSON
  cJSON_Delete(data); // Free cJSON object
}
void DataToSever(dataManager_t *Datamanager) {
  cJSON *data = cJSON_CreateObject();
  cJSON_AddStringToObject(data, "type", "DataFromESP32");
  cJSON_AddStringToObject(data, "clientType", "esp32");

  cJSON_AddStringToObject(data, "Time", Datamanager->Timestamp);
  cJSON_AddNumberToObject(data, "Temperature", Datamanager->Temperature_Value);
  cJSON_AddNumberToObject(data, "Humidity", Datamanager->Humidity_Value);
  cJSON_AddNumberToObject(data, "Pressure", Datamanager->Pressure_Value);
  cJSON_AddNumberToObject(data, "PM1_0", Datamanager->PM1_0_Value);
  cJSON_AddNumberToObject(data, "PM2_5", Datamanager->PM2_5_Value);
  cJSON_AddNumberToObject(data, "PM10", Datamanager->PM10_Value);

  char *json_str = cJSON_PrintUnformatted(data);

  esp_websocket_client_send_text(client, json_str, strlen(json_str),
                                 portMAX_DELAY);

  free(json_str);     // Free chuỗi được malloc bên trong cJSON
  cJSON_Delete(data); // Free cJSON object
}

static void websocket_event_handler(void *arg, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

  switch (event_id) {

  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG_WEBSOCKET, "Connected to server");
    SendSignalRegister();
    break;
  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGW(TAG_WEBSOCKET, "Disconnected from server");
    break;
  case WEBSOCKET_EVENT_DATA:
    ESP_LOGI(TAG_WEBSOCKET, "EVEN DATA\n");
    // for (int i = 0; i < data->data_len; i++) {
    //   printf("%c", (char)data->data_ptr[i]);
    // }
    // printf("\n");
    {
      size_t len = (size_t)data->data_len;
      char *json_buf = (char *)malloc(len + 1);
      if (json_buf) {
        memcpy(json_buf, data->data_ptr, len);
        json_buf[len] = '\0';

        cJSON *root = cJSON_Parse(json_buf);
        if (root) {
          const cJSON *j_type = cJSON_GetObjectItem(root, "type");
          const cJSON *j_version = cJSON_GetObjectItem(root, "version");
          if (cJSON_IsString(j_type)) {
            strncpy(ws_last_type, j_type->valuestring, sizeof(ws_last_type) - 1);
            ws_last_type[sizeof(ws_last_type) - 1] = '\0';
          }
          if (cJSON_IsString(j_version)) {
            strncpy(ws_last_version, j_version->valuestring, sizeof(ws_last_version) - 1);
            ws_last_version[sizeof(ws_last_version) - 1] = '\0';
            ESP_LOGI(TAG_WEBSOCKET, "Version: %s", ws_last_version);
            xTaskCreate(FOTA_task, "FOTA Task", 8192, ws_last_version, 10, NULL);
          }
          cJSON_Delete(root);
        }
        free(json_buf);
      }
    }
    break;
  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGE(TAG_WEBSOCKET, "WebSocket error occurred");
    break;
  default:
    break;
  }
}
void websocket_app_start(void) {
  esp_websocket_client_config_t websocket_cfg = {
      .uri = CONFIG_WS_URL,
  };

  client = esp_websocket_client_init(&websocket_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                websocket_event_handler, NULL);
  esp_websocket_client_start(client);
  // SendSignalRegister();
  ESP_LOGI(TAG_WEBSOCKET, "WebSocket start");
}
void WebSocket_Handler(void *pvParameter) {
  
  while (1) {
    if (is_wifi_connected()) {
      if (client == NULL) {
        websocket_app_start();
      }
    } else {
      if (client != NULL) {
        ESP_LOGE(TAG_WEBSOCKET, "WiFi not connected");
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy_on_exit(client);
        client = NULL;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
