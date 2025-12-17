#include "WifiManager.h"

// WiFi variables
EventGroupHandle_t wifi_event_group;
bool ap_mode_active = false;
char pending_ssid[33];
char pending_password[65];
bool wifi_connect_pending = false;
bool wifi_initialized = false;
esp_netif_t *sta_netif = NULL;
esp_netif_t *ap_netif = NULL;
SemaphoreHandle_t wifi_mutex = NULL;
int retry_num = 0;
httpd_handle_t server = NULL;
TaskHandle_t dns_server_task_handle = NULL;
static const char *TAG = "WiFiManager";

// Function declarations
void wifi_init_ap(void);
esp_err_t wifi_init_common(void);
void dns_server_task(void *pvParameters);

// Web server functions
esp_err_t start_webserver(void);
esp_err_t stop_webserver(void);
esp_err_t root_handler(httpd_req_t *req);
esp_err_t wifi_config_handler(httpd_req_t *req);
esp_err_t wifi_status_handler(httpd_req_t *req);
esp_err_t redirect_handler(httpd_req_t *req);
esp_err_t generate_204_handler(httpd_req_t *req);
esp_err_t common_get_handler(httpd_req_t *req, httpd_err_code_t error);
esp_err_t captive_portal_handler(httpd_req_t *req);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (retry_num < CONFIG_MAX_RETRY) {
      esp_wifi_disconnect();
      esp_wifi_connect();
      retry_num++;
      ESP_LOGI(TAG, "Retry to connect to the AP");
    } else {
      xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "Connect to the AP failed");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    retry_num = 0;
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

esp_err_t wifi_init_common(void) {
  if (wifi_initialized) {
    ESP_LOGI(TAG, "WiFi already initialized");
    return ESP_OK;
  }

  // Initialize TCP/IP stack and event loop only once
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create both netifs if they don't exist
  if (sta_netif == NULL) {
    sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
      ESP_LOGE(TAG, "Failed to create STA netif");
      return ESP_FAIL;
    }
  }

  if (ap_netif == NULL) {
    ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
      ESP_LOGE(TAG, "Failed to create AP netif");
      return ESP_FAIL;
    }
  }

  // Initialize WiFi
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, NULL));

  wifi_initialized = true;
  return ESP_OK;
}

void wifi_init_sta() {
  wifi_event_group = xEventGroupCreate();
  wifi_mutex = xSemaphoreCreateMutex();

  wifi_init_common();

  // Kiểm tra xem có cấu hình WiFi mặc định không
  if (strlen(CONFIG_WIFI_SSID) > 0) {
    ESP_LOGI(TAG, "Attempting to connect to default WiFi: %s",
             CONFIG_WIFI_SSID);

    // Thử kết nối với cấu hình mặc định
    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = CONFIG_WIFI_SSID,
                .password = CONFIG_WIFI_PASS,
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg = {.capable = true, .required = false},
            },
    };

    if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
      ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
      ESP_ERROR_CHECK(esp_wifi_start());
      xSemaphoreGive(wifi_mutex);
    }

    // Chờ kết nối thành công hoặc thất bại
    if (xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                           pdFALSE, pdFALSE, pdMS_TO_TICKS(10000)) &
        WIFI_CONNECTED_BIT) {
      ESP_LOGI(TAG, "Connected to SSID:%s", CONFIG_WIFI_SSID);
      return;
    } else {
      ESP_LOGI(TAG, "Failed to connect to default SSID:%s", CONFIG_WIFI_SSID);
    }
  } else {
    ESP_LOGI(TAG, "No default WiFi configuration found");
  }

  // Chuyển sang chế độ AP nếu không có cấu hình mặc định hoặc kết nối thất bại
  if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(500));
    wifi_init_ap();
    xSemaphoreGive(wifi_mutex);
  }
}
void dns_server_task(void *pvParameters) {
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  uint8_t rx_buffer[DNS_MAX_LEN];
  uint8_t tx_buffer[DNS_MAX_LEN];
  char *ip_addr = "192.168.4.1";

  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket");
    vTaskDelete(NULL);
    return;
  }

  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(DNS_PORT);

  if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind socket");
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    socklen_t socklen = sizeof(client_addr);
    int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0,
                       (struct sockaddr *)&client_addr, &socklen);

    if (len > 0) {
      // Copy header and question from request
      memcpy(tx_buffer, rx_buffer, len);

      // Set response flags
      tx_buffer[2] |= 0x80; // Set QR bit
      tx_buffer[3] = 0x80;  // No error
      tx_buffer[7] = 1;     // One answer

      // Skip question section
      int question_len = strlen((char *)(rx_buffer + 12)) + 1;
      int offset = 12 + question_len + 4;

      // Add answer section
      tx_buffer[offset++] = 0xC0; // Name pointer to question
      tx_buffer[offset++] = 0x0C;
      tx_buffer[offset++] = 0x00; // Type A
      tx_buffer[offset++] = 0x01;
      tx_buffer[offset++] = 0x00; // Class IN
      tx_buffer[offset++] = 0x01;
      tx_buffer[offset++] = 0x00; // TTL (4 bytes)
      tx_buffer[offset++] = 0x00;
      tx_buffer[offset++] = 0x00;
      tx_buffer[offset++] = 0x3C;
      tx_buffer[offset++] = 0x00; // Data length
      tx_buffer[offset++] = 0x04;

      // Add IP address
      inet_pton(AF_INET, ip_addr, tx_buffer + offset);
      offset += 4;

      // Send response
      sendto(sock, tx_buffer, offset, 0, (struct sockaddr *)&client_addr,
             sizeof(client_addr));
    }
  }
}


void wifi_init_ap() {
  ESP_LOGI(TAG, "Initializing WiFi AP mode");
  if (!wifi_initialized) {
    wifi_init_common();
  }

  // Stop DHCP server
  ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));

  // Configure IP info
  esp_netif_ip_info_t ip_info = {.ip.addr = ipaddr_addr("192.168.4.1"),
                                 .netmask.addr = ipaddr_addr("255.255.255.0"),
                                 .gw.addr = ipaddr_addr("192.168.4.1")};
  ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));

  // Configure DNS
  esp_netif_dns_info_t dns_info = {0};
  dns_info.ip.u_addr.ip4.addr = ipaddr_addr("192.168.4.1");
  ESP_ERROR_CHECK(
      esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info));

  // Restart DHCP server with new configuration
  ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

  wifi_config_t wifi_config = {
      .ap =
          {
              .ssid = AP_SSID,
              .ssid_len = strlen(AP_SSID),
              .password = "",
              .max_connection = AP_MAX_CONN,
              .authmode = WIFI_AUTH_OPEN,
              .channel = AP_CHANNEL,
          },
  };

  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
  esp_wifi_start();

  ESP_LOGI(TAG, "WiFi AP started with SSID: %s (No password required)",
           AP_SSID);
  ESP_LOGI(TAG, "AP IP address: 192.168.4.1");

  // Start DNS server for captive portal
  xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5,
              &dns_server_task_handle);

  // Start web server for captive portal
  start_webserver();

  ap_mode_active = true;
  xEventGroupSetBits(wifi_event_group, WIFI_AP_MODE_BIT);
}

void wifi_connect_task(void *pvParameters) {
  while (1) {
    if (wifi_connect_pending) {
      ESP_LOGI(TAG, "Attempting to connect to WiFi: SSID=%s", pending_ssid);

      // Take mutex to ensure exclusive access to WiFi initialization
      if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        // Stop AP mode and web server
        stop_webserver();
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait longer for WiFi to stop
        ap_mode_active = false;
        xEventGroupClearBits(wifi_event_group, WIFI_AP_MODE_BIT);

        // Clear any previous connection bits
        xEventGroupClearBits(wifi_event_group,
                             WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        retry_num = 0;

        // Configure STA mode with new credentials
        wifi_config_t wifi_config = {
            .sta =
                {
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                    .pmf_cfg = {.capable = true, .required = false},
                },
        };

        strncpy((char *)wifi_config.sta.ssid, pending_ssid,
                sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, pending_password,
                sizeof(wifi_config.sta.password) - 1);

        // Make sure WiFi is initialized
        if (!wifi_initialized) {
          wifi_init_common();
        }

        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_start();

        // Wait for connection
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE,
            pdFALSE, pdMS_TO_TICKS(10000));

        if (bits & WIFI_CONNECTED_BIT) {
          ESP_LOGI(TAG, "WiFi connected successfully");
        } else {
          ESP_LOGE(TAG, "WiFi connection failed");
          // Restart AP mode if connection fails
          wifi_init_ap();
        }

        wifi_connect_pending = false;
        xSemaphoreGive(wifi_mutex); // Release mutex
      } else {
        ESP_LOGE(TAG, "Failed to acquire WiFi mutex");
        wifi_connect_pending = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Check less frequently
  }
}

void wifi_manager_task(void *pvParameters) {
  while (1) {
    if (xSemaphoreTake(wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      EventBits_t bits = xEventGroupGetBits(wifi_event_group);

      // If we're in AP mode and WiFi gets connected, switch back to STA mode
      if ((bits & WIFI_AP_MODE_BIT) && (bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGI(TAG, "WiFi connected while in AP mode, switching to STA mode");
        stop_webserver();
        esp_wifi_stop();
        ap_mode_active = false;
        xEventGroupClearBits(wifi_event_group, WIFI_AP_MODE_BIT);

        // Restart STA mode
        wifi_init_sta();
      }

      // If WiFi is disconnected and not in AP mode, start AP mode
      if (!(bits & WIFI_CONNECTED_BIT) && !(bits & WIFI_AP_MODE_BIT) &&
          !wifi_connect_pending) {
        ESP_LOGI(TAG, "WiFi disconnected, starting AP mode");
        esp_wifi_stop();
        wifi_init_ap();
        start_webserver();
      }

      xSemaphoreGive(wifi_mutex);
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Check more frequently
  }
}

bool is_wifi_connected(void) {
  EventBits_t bits = xEventGroupGetBits(wifi_event_group);
  return (bits & WIFI_CONNECTED_BIT) == WIFI_CONNECTED_BIT;
}



esp_err_t root_handler(httpd_req_t *req) {
  FILE *file = fopen("/spiffs/index.html", "r");
  if (file == NULL) {
    ESP_LOGE(TAG, "Failed to open index.html");
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Allocate buffer for file content
  char *buffer = malloc(file_size + 1);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for HTML file");
    fclose(file);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // Read file content
  size_t bytes_read = fread(buffer, 1, file_size, file);
  buffer[bytes_read] = '\0';
  fclose(file);

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, buffer, bytes_read);

  free(buffer);
  return ESP_OK;
}

esp_err_t wifi_config_handler(httpd_req_t *req) {
  char content[256];
  int recv_len = httpd_req_recv(req, content, sizeof(content) - 1);
  if (recv_len <= 0) {
    return ESP_FAIL;
  }
  content[recv_len] = '\0';

  cJSON *root = cJSON_Parse(content);
  if (root == NULL) {
    return ESP_FAIL;
  }

  cJSON *ssid_json = cJSON_GetObjectItem(root, "ssid");
  cJSON *password_json = cJSON_GetObjectItem(root, "password");

  if (cJSON_IsString(ssid_json) && cJSON_IsString(password_json)) {
    char ssid[33];
    char password[65];
    strncpy(ssid, ssid_json->valuestring, sizeof(ssid) - 1);
    strncpy(password, password_json->valuestring, sizeof(password) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    password[sizeof(password) - 1] = '\0';

    ESP_LOGI(TAG, "Configuring WiFi: SSID=%s", ssid);

    // Store credentials for connection task
    strncpy(pending_ssid, ssid, sizeof(pending_ssid) - 1);
    strncpy(pending_password, password, sizeof(pending_password) - 1);
    pending_ssid[sizeof(pending_ssid) - 1] = '\0';
    pending_password[sizeof(pending_password) - 1] = '\0';
    wifi_connect_pending = true;

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "Attempting to connect...");

    char *response_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);
  }

  cJSON_Delete(root);
  return ESP_OK;
}

esp_err_t wifi_status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ap_mode", ap_mode_active);
  cJSON_AddStringToObject(root, "ap_ssid", AP_SSID);
  cJSON_AddStringToObject(root, "ap_ip", "192.168.4.1");

  char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, strlen(json_str));

  free(json_str);
  cJSON_Delete(root);
  return ESP_OK;
}

esp_err_t redirect_handler(httpd_req_t *req) {
  FILE *file = fopen("/spiffs/redirect.html", "r");
  if (file == NULL) {
    ESP_LOGE(TAG, "Failed to open redirect.html");
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Allocate buffer for file content
  char *buffer = malloc(file_size + 1);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for redirect HTML file");
    fclose(file);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // Read file content
  size_t bytes_read = fread(buffer, 1, file_size, file);
  buffer[bytes_read] = '\0';
  fclose(file);

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, buffer, bytes_read);

  free(buffer);
  return ESP_OK;
}

esp_err_t generate_204_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

esp_err_t common_get_handler(httpd_req_t *req, httpd_err_code_t error) {
  ESP_LOGI(TAG, "Handling URI: %s", req->uri);
  return captive_portal_handler(req);
}

esp_err_t captive_portal_handler(httpd_req_t *req) {
  return root_handler(req);
}

esp_err_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  config.max_uri_handlers = 10;

  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Error starting web server");
    return ESP_FAIL;
  }

  // Trang chủ
  httpd_uri_t root_uri = {.uri = "/",
                          .method = HTTP_GET,
                          .handler = root_handler,
                          .user_ctx = NULL};
  httpd_register_uri_handler(server, &root_uri);

  // Cấu hình WiFi
  httpd_uri_t config_uri = {.uri = "/configure",
                            .method = HTTP_POST,
                            .handler = wifi_config_handler,
                            .user_ctx = NULL};
  httpd_register_uri_handler(server, &config_uri);

  // Captive portal cho Android
  httpd_uri_t generate_204 = {.uri = "/generate_204",
                              .method = HTTP_GET,
                              .handler = generate_204_handler,
                              .user_ctx = NULL};
  httpd_register_uri_handler(server, &generate_204);

  // Captive portal cho iOS
  httpd_uri_t hotspot_detect = {.uri = "/hotspot-detect.html",
                                .method = HTTP_GET,
                                .handler = captive_portal_handler,
                                .user_ctx = NULL};
  httpd_register_uri_handler(server, &hotspot_detect);

  // Handler mặc định cho captive portal
  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, common_get_handler);

  ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
  return ESP_OK;
}

esp_err_t stop_webserver(void) {
  if (server) {
    httpd_stop(server);
    server = NULL;
    ESP_LOGI(TAG, "Web server stopped");
  }
  return ESP_OK;
}