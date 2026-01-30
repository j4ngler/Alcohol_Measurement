/**
 * @file main.c
 * @author Nguyen Nhu Hai Long ( @long27032002 )
 * @brief Main file of Electronic-Nose firmware
 * @version 0.1
 * @date 2023-01-04
 *
 * @copyright (c) 2024 Nguyen Nhu Hai Long <long27032002@gmail.com>
 *
 */

/*------------------------------------ INCLUDE LIBRARY ------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <sys/param.h>
#include <sys/time.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_cpu.h"
#include "esp_mem.h"
#include "esp_event.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_pm.h"

#include "esp_flash.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "esp_mac.h"
#include "esp_attr.h"
#include <spi_flash_mmap.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_ota_ops.h"
#include "esp_smartconfig.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "dht.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_common.h"
#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#include "sdcard.h"
#include "DS3231Time.h"
#include "datamanager.h"
#include "sntp_sync.h"
#include "ADS111x.h"
#include "button.h"
#include "FileServer.h"
#include "test_i2c_devices.h"
#include "test_sdcard.h"

/*------------------------------------ DEFINE ------------------------------------ */

// Uncomment dòng dưới để chạy test I2C devices thay vì chạy ứng dụng chính
//#define ENABLE_I2C_TEST_MODE

// Uncomment dòng dưới để chạy test SD card thay vì chạy ứng dụng chính
//#define ENABLE_SDCARD_TEST_MODE

__attribute__((unused)) static const char *TAG = "Main";

// Dashboard config (loaded from NVS or use CONFIG defaults)
#if CONFIG_DASHBOARD_ENABLED
// Forward declarations for dashboard config functions
static esp_err_t load_dashboard_config_from_nvs(char *host, size_t host_size, int *port);

static char dashboard_host[64] = CONFIG_DASHBOARD_HOST;
static int dashboard_port = CONFIG_DASHBOARD_PORT;

/**
 * @brief Get dashboard hostname/IP and port (from NVS or CONFIG)
 * 
 * @param host Output buffer for hostname or IP address
 * @param host_size Size of host buffer
 * @param port Output pointer for port
 */
static void get_dashboard_config(char *host, size_t host_size, int *port)
{
    // Try to load from NVS first
    if (load_dashboard_config_from_nvs(host, host_size, port) == ESP_OK) {
        // Update global variables
        strncpy(dashboard_host, host, sizeof(dashboard_host) - 1);
        dashboard_host[sizeof(dashboard_host) - 1] = '\0';
        dashboard_port = *port;
        return;
    }
    
    // Fallback to CONFIG values
    strncpy(host, CONFIG_DASHBOARD_HOST, host_size - 1);
    host[host_size - 1] = '\0';
    *port = CONFIG_DASHBOARD_PORT;
    strncpy(dashboard_host, CONFIG_DASHBOARD_HOST, sizeof(dashboard_host) - 1);
    dashboard_host[sizeof(dashboard_host) - 1] = '\0';
    dashboard_port = CONFIG_DASHBOARD_PORT;
}
#endif

// Forward declarations
static void check_wifi_status(void);
#if CONFIG_DASHBOARD_ENABLED
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
esp_err_t save_dashboard_config_to_nvs(const char *host, int port);  // Not static - used by FileServer.c
#endif
// Always declare this function to ensure linking works, even when CONFIG_DASHBOARD_ENABLED is disabled
void trigger_dashboard_registration_main(void);  // Not static - used by FileServer.c wrapper to trigger re-registration

// Chu kỳ đọc cảm biến (DHT22 yêu cầu tối thiểu ~2s giữa 2 lần đọc)
#define PERIOD_GET_DATA_FROM_SENSOR (TickType_t)(2000 / portTICK_PERIOD_MS)
#define PERIOD_SAVE_DATA_SENSOR_TO_SDCARD (TickType_t)(50 / portTICK_PERIOD_MS)
#define SAMPLING_TIMME  (TickType_t)(300000 / portTICK_PERIOD_MS)

#define NO_WAIT (TickType_t)(0)
#define WAIT_10_TICK (TickType_t)(10 / portTICK_PERIOD_MS)
#define WAIT_100_TICK (TickType_t)(100 / portTICK_PERIOD_MS)

#define QUEUE_SIZE 10U
#define DATA_SENSOR_MIDLEWARE_QUEUE_SIZE 20


#define BUTTON_PRESSED_BIT BIT1
#define START_SAMPLING_BIT BIT1  // Bit để signal start sampling (dùng cho HTTP/UART command)

TaskHandle_t getDataFromSensorTask_handle = NULL;
TaskHandle_t saveDataSensorToSDcardTask_handle = NULL;
TaskHandle_t sntp_syncTimeTask_handle = NULL;
TaskHandle_t allocateDataForMultipleQueuesTask_handle = NULL;
TaskHandle_t smartConfigTask_handle = NULL;

SemaphoreHandle_t getDataSensor_semaphore = NULL;
SemaphoreHandle_t SDcard_semaphore = NULL;

QueueHandle_t dataSensorSentToSD_queue = NULL;
QueueHandle_t dataSensorSentToDashboard_queue = NULL; // Queue để gửi dữ liệu đến dashboard qua HTTP POST

#if CONFIG_DHT_USE
#if CONFIG_DHT_TYPE_DHT11
#define DHT_TYPE  DHT_TYPE_DHT11
#else
#define DHT_TYPE  DHT_TYPE_DHT22
#endif
#define DHT_GPIO  ((gpio_num_t)CONFIG_DHT_GPIO)
#endif
// Flag to track SD card mount status
static bool sdcard_mounted = false;
// QueueHandle_t moduleError_queue = NULL;

//static EventGroupHandle_t fileStore_eventGroup;
static EventGroupHandle_t button_event;
EventGroupHandle_t sampling_control_event;  // Event group để control sampling (HTTP/UART) - exported for FileServer.c
static char nameFileSaveData[21] = "file";
static const char base_path[] = MOUNT_POINT;
static sdmmc_card_t *g_sdcard = NULL;

// WiFi retry counter - tự động chuyển sang SmartConfig sau nhiều lần retry thất bại
static int wifi_retry_count = 0;
static const int MAX_WIFI_RETRY = 5;  // Sau 5 lần retry thất bại, chuyển sang SmartConfig

/*------------------------------------ Define devices ------------------------------------ */
static i2c_dev_t ds3231_device = {0};
static i2c_dev_t ads111x_devices[CONFIG_ADS111X_DEVICE_COUNT] = {0};

// static i2c_dev_t pcf8574_device = {0};
//static i2c_dev_t pcf8575_device = {0};

// I2C addresses for ADS1115
// Đã scan và xác nhận thiết bị ở địa chỉ 0x48 (ADDR_GND)
const uint8_t addresses[CONFIG_ADS111X_DEVICE_COUNT] = {
    ADS111X_ADDR_GND   // 0x48 - Đã xác nhận hoạt động
#if CONFIG_ADS111X_DEVICE_COUNT > 1
    , ADS111X_ADDR_VCC   // 0x49 - Dự phòng nếu có thiết bị thứ 2
#endif
};

/*------------------------------------ WIFI ------------------------------------ */

// NVS namespace và keys cho WiFi config
#define NVS_NAMESPACE_WIFI "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"

// NVS namespace và keys cho Dashboard config
#define NVS_NAMESPACE_DASHBOARD "dashboard"
#define NVS_KEY_DASHBOARD_HOST "host"
#define NVS_KEY_DASHBOARD_PORT "port"

// NVS namespace và keys cho Static IP config
#define NVS_NAMESPACE_STATIC_IP "staticip"
#define NVS_KEY_STATIC_IP_ENABLED "enabled"
#define NVS_KEY_STATIC_IP "ip"
#define NVS_KEY_STATIC_NETMASK "netmask"
#define NVS_KEY_STATIC_GATEWAY "gateway"

/**
 * @brief Lưu SSID và password vào NVS
 * 
 * @param ssid SSID của WiFi
 * @param password Password của WiFi
 * @return esp_err_t ESP_OK nếu thành công
 */
static esp_err_t save_wifi_config_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving SSID to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving password to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "✅ WiFi config saved to NVS: SSID=%s", ssid);
    }
    
    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Đọc SSID và password từ NVS
 * 
 * @param ssid Buffer để lưu SSID (tối đa 33 bytes)
 * @param password Buffer để lưu password (tối đa 65 bytes)
 * @return esp_err_t ESP_OK nếu thành công, ESP_ERR_NOT_FOUND nếu không tìm thấy
 */
static esp_err_t load_wifi_config_from_nvs(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS namespace '%s' not found or error: %s", NVS_NAMESPACE_WIFI, esp_err_to_name(err));
        return err;
    }
    
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &ssid_size);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "SSID not found in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, password, &password_size);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Password not found in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    ESP_LOGI(TAG, "✅ WiFi config loaded from NVS: SSID=%s", ssid);
    nvs_close(nvs_handle);
    return ESP_OK;
}

/**
 * @brief Lưu Dashboard config (host và port) vào NVS
 * 
 * @param host Dashboard server hostname hoặc IP address (ví dụ: "dashboard.local" hoặc "192.168.1.100")
 *             ESP-IDF HTTP client sẽ tự động resolve hostname thành IP qua DNS
 * @param port Dashboard server port
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t save_dashboard_config_to_nvs(const char *host, int port)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    char port_str[16];
    
    err = nvs_open(NVS_NAMESPACE_DASHBOARD, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle for dashboard: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(nvs_handle, NVS_KEY_DASHBOARD_HOST, host);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving dashboard host to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    snprintf(port_str, sizeof(port_str), "%d", port);
    err = nvs_set_str(nvs_handle, NVS_KEY_DASHBOARD_PORT, port_str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving dashboard port to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing dashboard config to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    ESP_LOGI(TAG, "✅ Dashboard config saved to NVS: %s:%d", host, port);
    
    // Update global variables immediately after commit
    strncpy(dashboard_host, host, sizeof(dashboard_host) - 1);
    dashboard_host[sizeof(dashboard_host) - 1] = '\0';
    dashboard_port = port;
    
    nvs_close(nvs_handle);
    
    // Small delay to ensure NVS is fully flushed
    vTaskDelay(pdMS_TO_TICKS(100));
    
    return ESP_OK;
}

/**
 * @brief Đọc Dashboard config từ NVS
 * 
 * @param host Buffer để lưu host IP (tối đa 64 bytes)
 * @param host_size Size của host buffer
 * @param port Pointer để lưu port
 * @return esp_err_t ESP_OK nếu thành công, ESP_ERR_NOT_FOUND nếu không tìm thấy
 */
static esp_err_t load_dashboard_config_from_nvs(char *host, size_t host_size, int *port)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t port_str_size = 16;
    char port_str[16];
    
    err = nvs_open(NVS_NAMESPACE_DASHBOARD, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Dashboard config not found in NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_get_str(nvs_handle, NVS_KEY_DASHBOARD_HOST, host, &host_size);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Dashboard host not found in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_get_str(nvs_handle, NVS_KEY_DASHBOARD_PORT, port_str, &port_str_size);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Dashboard port not found in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    *port = atoi(port_str);
    ESP_LOGI(TAG, "✅ Dashboard config loaded from NVS: %s:%d", host, *port);
    nvs_close(nvs_handle);
    return ESP_OK;
}

/**
 * @brief Lưu Static IP config vào NVS
 * 
 * @param enabled 1 để bật static IP, 0 để tắt (dùng DHCP)
 * @param ip IP address (ví dụ: "192.168.0.122")
 * @param netmask Netmask (ví dụ: "255.255.255.0")
 * @param gateway Gateway (ví dụ: "192.168.0.1")
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t save_static_ip_config_to_nvs(uint8_t enabled, const char *ip, const char *netmask, const char *gateway)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE_STATIC_IP, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle for static IP: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_blob(nvs_handle, NVS_KEY_STATIC_IP_ENABLED, &enabled, sizeof(enabled));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving static IP enabled flag: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    if (enabled) {
        err = nvs_set_str(nvs_handle, NVS_KEY_STATIC_IP, ip);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving static IP: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
        
        err = nvs_set_str(nvs_handle, NVS_KEY_STATIC_NETMASK, netmask);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving static netmask: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
        
        err = nvs_set_str(nvs_handle, NVS_KEY_STATIC_GATEWAY, gateway);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving static gateway: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing static IP config to NVS: %s", esp_err_to_name(err));
    } else {
        if (enabled) {
            ESP_LOGI(TAG, "✅ Static IP config saved to NVS: %s/%s/%s", ip, netmask, gateway);
        } else {
            ESP_LOGI(TAG, "✅ Static IP disabled, will use DHCP");
        }
    }
    
    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Xóa WiFi config từ NVS
 * 
 * @return esp_err_t ESP_OK nếu thành công
 */
static esp_err_t clear_wifi_config_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_erase_key(nvs_handle, NVS_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error erasing SSID from NVS: %s", esp_err_to_name(err));
    }
    
    err = nvs_erase_key(nvs_handle, NVS_KEY_PASSWORD);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error erasing password from NVS: %s", esp_err_to_name(err));
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "✅ WiFi config cleared from NVS");
    }
    
    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief SmartConfig task với timeout và cải thiện logging
 * 
 * @param parameter 
 */
static void smartConfig_task(void * parameter)
{
    const int SMARTCONFIG_TIMEOUT_MS = 60000; // 60 giây timeout
    TickType_t start_time = xTaskGetTickCount();
    
    ESP_LOGI(TAG, "========== SMARTCONFIG STARTED ==========");
    ESP_LOGI(TAG, "📡 Waiting for ESP-Touch configuration...");
    ESP_LOGI(TAG, "💡 Please use ESP-Touch app to send WiFi credentials");
    ESP_LOGI(TAG, "⏱️  Timeout: %d seconds", SMARTCONFIG_TIMEOUT_MS / 1000);
    
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t smartConfig_config = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&smartConfig_config));
    
    for(;;)
    {
        // Kiểm tra timeout
        TickType_t elapsed = xTaskGetTickCount() - start_time;
        if (elapsed > pdMS_TO_TICKS(SMARTCONFIG_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "⏱️  SmartConfig timeout after %d seconds", SMARTCONFIG_TIMEOUT_MS / 1000);
            ESP_LOGW(TAG, "🔄 Stopping SmartConfig. Please restart device to try again.");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
            return;
        }
        
        // Đợi notification từ event handler
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "✅ SmartConfig completed successfully!");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
            return;
        }
        
        // Log progress mỗi 10 giây
        if ((elapsed % pdMS_TO_TICKS(10000)) < pdMS_TO_TICKS(1000)) {
            int remaining = (SMARTCONFIG_TIMEOUT_MS - (elapsed * portTICK_PERIOD_MS)) / 1000;
            ESP_LOGI(TAG, "⏳ Still waiting for SmartConfig... (%d seconds remaining)", remaining);
        }
    }
}

/**
 * @brief Set time from system time to DS3231 RTC
 */
// Forward declaration
static void set_ds3231_time_from_system(void)
{
    struct tm timeinfo;
    time_t now;
    
    // Get current system time
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Check if system time is valid (after 2020-01-01)
    // If system time is invalid, log warning and return (will be updated by SNTP later)
    if (now < 1577836800) { // 2020-01-01 00:00:00 UTC
        ESP_LOGW(TAG, "System time is invalid (%lld), waiting for SNTP sync...", (long long)now);
        return;
    }
    
    // Set time to DS3231 from system time
    esp_err_t ret = ds3231_setTime(&ds3231_device, &timeinfo);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 time set successfully from system: %02d/%02d/%04d %02d:%02d:%02d",
                 timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        ESP_LOGE(TAG, "Failed to set DS3231 time: %s", esp_err_to_name(ret));
    }
}

static void sntp_syncTime_task(void *parameter)
{
    ESP_LOGI(TAG, "========== SNTP SYNC TASK STARTED ==========");
    
    // Wait for WiFi to be connected and IP obtained
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // Set timezone to Vietnam
    sntp_setTimmeZoneToVN();
    
    // Print server information
    sntp_printServerInformation();
    
    // Sync time with SNTP servers (có thể mất vài giây)
    int retry_count = 0;
    const int max_retries = 10;
    esp_err_t sync_result = ESP_FAIL;
    
    while (retry_count < max_retries && sync_result != ESP_OK) {
        sync_result = sntp_syncTime();
        if (sync_result != ESP_OK) {
            retry_count++;
            ESP_LOGW(TAG, "SNTP sync failed, retrying... (%d/%d)", retry_count, max_retries);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
    
    if (sync_result == ESP_OK) {
        ESP_LOGI(TAG, "Time synchronized successfully with SNTP");
        
        // Đợi thêm một chút để đảm bảo system time đã được cập nhật
        vTaskDelay(500 / portTICK_PERIOD_MS);
        
        // Set time to DS3231 RTC after SNTP sync
        set_ds3231_time_from_system();
        
        // Log thời gian đã sync
        struct tm timeinfo;
        time_t now;
        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "DS3231 updated with real-time: %02d/%02d/%04d %02d:%02d:%02d",
                 timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        
        // Sau khi SNTP sync thành công, tạo file mới với thời gian thực
        // Kiểm tra xem getDataFromSensor_task đã được tạo chưa
        if (getDataFromSensorTask_handle != NULL) {
            ESP_LOGI(TAG, "SNTP sync completed! Creating new file with real-time...");
            
            // Cập nhật tên file với thời gian thực từ DS3231 (đã được cập nhật từ system time)
            ESP_ERROR_CHECK_WITHOUT_ABORT(ds3231_convertTimeToString(&ds3231_device, nameFileSaveData, 14));
            ESP_LOGI(TAG, "New file name with real-time: %s.csv", nameFileSaveData);
            
            // Tạo header cho file CSV mới với thời gian thực
            if (SDcard_semaphore != NULL) {
                if (xSemaphoreTake(SDcard_semaphore, pdMS_TO_TICKS(5000)) == pdTRUE) {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(sdcard_writeDataToFile(nameFileSaveData, "%s,%s,%s,%s,%s,%s,%s\n",
                                                                          "STT", "Temperature", "Humidity", "Sensor1", "Sensor2", "Sensor3", "Sensor4"));
                    ESP_LOGI(TAG, "✅ Created new file %s.csv with real-time after SNTP sync!", nameFileSaveData);
                    xSemaphoreGive(SDcard_semaphore);
                } else {
                    ESP_LOGW(TAG, "Failed to get SD card semaphore to create new file");
                }
            }
        } else {
            ESP_LOGW(TAG, "getDataFromSensor_task not created yet, file will be created in next sampling cycle");
        }
    } else {
        ESP_LOGE(TAG, "Failed to synchronize time after %d retries", max_retries);
    }
    
    // Task completed, delete itself
    vTaskDelete(NULL);
}
static void WiFi_eventHandler( void *argument,  esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
        {
            char saved_ssid[33] = {0};
            char saved_password[65] = {0};
            bool use_saved_config = false;
            
            // Thử đọc WiFi config từ NVS trước
            if (load_wifi_config_from_nvs(saved_ssid, sizeof(saved_ssid), saved_password, sizeof(saved_password)) == ESP_OK) {
                ESP_LOGI(__func__, "📂 Found saved WiFi config in NVS: SSID=%s", saved_ssid);
                use_saved_config = true;
            }
            
            // Kiểm tra config từ menuconfig hoặc NVS
            if (strlen(CONFIG_SSID) > 0 && strlen(CONFIG_PASSWORD) > 0) {
                ESP_LOGI(__func__, "📡 Using WiFi config from menuconfig: SSID=%s", CONFIG_SSID);
                esp_wifi_connect();
            } else if (use_saved_config) {
                // Sử dụng config từ NVS
                wifi_config_t wifi_config = {0};
                memcpy(wifi_config.sta.ssid, saved_ssid, strlen(saved_ssid));
                memcpy(wifi_config.sta.password, saved_password, strlen(saved_password));
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                ESP_LOGI(__func__, "📡 Connecting with saved WiFi config: SSID=%s", saved_ssid);
                esp_wifi_connect();
            } else {
                // Không có config nào, start SmartConfig
                ESP_LOGI(__func__, "📡 No WiFi config found, starting SmartConfig (ESP-Touch)...");
                ESP_LOGI(__func__, "💡 Please use ESP-Touch app to configure WiFi");
                if (smartConfigTask_handle == NULL) {
                    xTaskCreate(smartConfig_task, "smartconfig_task", 1024 * 4, NULL, 15, &smartConfigTask_handle);
                }
            }
            break;
        }
        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*) event_data;
            ESP_LOGI(__func__, "========== WiFi CONNECTED ==========");
            ESP_LOGI(__func__, "SSID: %s", event->ssid);
            ESP_LOGI(__func__, "Channel: %d", event->channel);
            ESP_LOGI(__func__, "Auth mode: %d", event->authmode);
            // Reset retry counter khi kết nối thành công
            wifi_retry_count = 0;
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
            ESP_LOGW(__func__, "========== WiFi DISCONNECTED ==========");
            ESP_LOGW(__func__, "SSID: %s", event->ssid);
            ESP_LOGW(__func__, "Reason: %d", event->reason);
            
            // Giải thích reason code
            switch (event->reason) {
                case WIFI_REASON_NO_AP_FOUND:
                    ESP_LOGW(__func__, "Reason: NO_AP_FOUND - Cannot find AP with SSID '%s'", event->ssid);
                    ESP_LOGW(__func__, "   → Check if WiFi router is powered on and SSID is correct");
                    break;
                case WIFI_REASON_AUTH_FAIL:
                    ESP_LOGW(__func__, "Reason: AUTH_FAIL - Authentication failed. Check password!");
                    ESP_LOGW(__func__, "   → Password may be incorrect or WiFi security type changed");
                    break;
                case WIFI_REASON_ASSOC_FAIL:
                    ESP_LOGW(__func__, "Reason: ASSOC_FAIL - Association failed");
                    ESP_LOGW(__func__, "   → Router may be rejecting connection or signal too weak");
                    break;
                case WIFI_REASON_HANDSHAKE_TIMEOUT:
                    ESP_LOGW(__func__, "Reason: HANDSHAKE_TIMEOUT - Handshake timeout");
                    ESP_LOGW(__func__, "   → Network may be busy or signal too weak");
                    break;
                case 15:  // WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
                    ESP_LOGW(__func__, "Reason: 4WAY_HANDSHAKE_TIMEOUT (15) - 4-way handshake timeout");
                    ESP_LOGW(__func__, "   → Password may be incorrect or WiFi security mismatch");
                    ESP_LOGW(__func__, "   → Try: Check password, move ESP32 closer to router");
                    break;
                case 205:  // WIFI_REASON_BEACON_TIMEOUT
                    ESP_LOGW(__func__, "Reason: BEACON_TIMEOUT (205) - No beacon received from AP");
                    ESP_LOGW(__func__, "   → Signal too weak or router too far");
                    ESP_LOGW(__func__, "   → Try: Move ESP32 closer to router, check router power");
                    break;
                default:
                    ESP_LOGW(__func__, "Reason: Unknown (%d)", event->reason);
                    ESP_LOGW(__func__, "   → Check ESP-IDF documentation for reason code %d", event->reason);
                    break;
            }
            
            // Kiểm tra xem có config từ menuconfig hoặc NVS không
            char saved_ssid[33] = {0};
            char saved_password[65] = {0};
            bool has_config = false;
            
            if (strlen(CONFIG_SSID) > 0 && strlen(CONFIG_PASSWORD) > 0) {
                has_config = true;
            } else if (load_wifi_config_from_nvs(saved_ssid, sizeof(saved_ssid), saved_password, sizeof(saved_password)) == ESP_OK) {
                has_config = true;
            }
            
            // Nếu có config nhưng retry quá nhiều lần, chuyển sang SmartConfig
            if (has_config) {
                wifi_retry_count++;
                ESP_LOGW(__func__, "WiFi retry count: %d/%d", wifi_retry_count, MAX_WIFI_RETRY);
                
                if (wifi_retry_count >= MAX_WIFI_RETRY) {
                    ESP_LOGW(__func__, "⚠️  WiFi connection failed after %d retries", MAX_WIFI_RETRY);
                    ESP_LOGW(__func__, "🔄 Clearing saved WiFi config and starting SmartConfig...");
                    
                    // Xóa config từ NVS (không thể xóa menuconfig, nhưng sẽ bỏ qua nó)
                    clear_wifi_config_from_nvs();
                    
                    // Reset retry counter
                    wifi_retry_count = 0;
                    
                    // Start SmartConfig
                    if (smartConfigTask_handle == NULL) {
                        ESP_LOGI(__func__, "📡 Starting SmartConfig (ESP-Touch)...");
                        ESP_LOGI(__func__, "💡 Please use ESP-Touch app to configure WiFi");
                        xTaskCreate(smartConfig_task, "smartconfig_task", 1024 * 4, NULL, 15, &smartConfigTask_handle);
                    }
                } else {
                    // Retry kết nối với delay để tránh spam
                    ESP_LOGI(__func__, "Retrying connect to AP SSID:%s (attempt %d/%d)", 
                             strlen(CONFIG_SSID) > 0 ? CONFIG_SSID : saved_ssid, 
                             wifi_retry_count, MAX_WIFI_RETRY);
                    ESP_LOGI(__func__, "   → Waiting %d seconds before retry...", (wifi_retry_count * 2));
                    
                    // Delay tăng dần: 2s, 4s, 6s, 8s, 10s
                    vTaskDelay((wifi_retry_count * 2000) / portTICK_PERIOD_MS);
                    esp_wifi_connect();
                }
            } else {
                // Không có config nào, start SmartConfig ngay
                wifi_retry_count = 0;  // Reset counter
                if (smartConfigTask_handle == NULL) {
                    ESP_LOGI(__func__, "📡 No WiFi config found, starting SmartConfig (ESP-Touch)...");
                    ESP_LOGI(__func__, "💡 Please use ESP-Touch app to configure WiFi");
                    xTaskCreate(smartConfig_task, "smartconfig_task", 1024 * 4, NULL, 15, &smartConfigTask_handle);
                }
            }
            break;
        }
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "========== WiFi GOT IP ADDRESS ==========");
            ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
            ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
            ESP_LOGI(TAG, "WiFi connection is READY for SNTP sync!");

            start_file_server(base_path);

#if CONFIG_DASHBOARD_ENABLED
            // Đăng ký IP với dashboard server ngay khi nhận được IP
            // Điều này cho phép dashboard biết IP của ESP32 trước khi sampling bắt đầu
            // Lưu ý: Đăng ký IP sẽ được retry khi gửi data nếu thất bại ở đây
            // Load dashboard config từ NVS (hoặc dùng CONFIG default)
            char dashboard_host_temp[64];
            int dashboard_port_temp;
            get_dashboard_config(dashboard_host_temp, sizeof(dashboard_host_temp), &dashboard_port_temp);
            
            ESP_LOGI(TAG, "📡 Registering ESP32 IP with dashboard server...");
            ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Dashboard server: %s:%d", dashboard_host_temp, dashboard_port_temp);
            
            char register_url[128];
            char register_payload[128];
            snprintf(register_url, sizeof(register_url), "http://%s:%d/api/esp32/register", 
                     dashboard_host_temp, dashboard_port_temp);
            snprintf(register_payload, sizeof(register_payload),
                     "{\"ip\":\"" IPSTR "\"}", IP2STR(&event->ip_info.ip));
            
            ESP_LOGI(TAG, "Registration URL: %s", register_url);
            ESP_LOGI(TAG, "Registration payload: %s", register_payload);
            
            // Thử đăng ký với retry (tối đa 2 lần, delay 2 giây giữa các lần)
            bool registration_success = false;
            for (int retry = 0; retry < 2 && !registration_success; retry++) {
                if (retry > 0) {
                    ESP_LOGI(TAG, "Retrying registration (attempt %d/2)...", retry + 1);
                    vTaskDelay(pdMS_TO_TICKS(2000)); // Delay 2 giây trước khi retry
                }
                
                esp_http_client_config_t config = {
                    .url = register_url,
                    .event_handler = http_event_handler,
                    .timeout_ms = 15000, // Tăng timeout lên 15 giây
                    .skip_cert_common_name_check = true, // Bỏ qua kiểm tra certificate (nếu dùng HTTPS)
                };
                esp_http_client_handle_t client = esp_http_client_init(&config);
                
                if (client != NULL) {
                    // Thêm User-Agent header để debug
                    esp_http_client_set_header(client, "User-Agent", "ESP32-Client/1.0");
                    esp_http_client_set_method(client, HTTP_METHOD_POST);
                    esp_http_client_set_header(client, "Content-Type", "application/json");
                    esp_http_client_set_post_field(client, register_payload, strlen(register_payload));
                    
                    ESP_LOGI(TAG, "Sending registration request...");
                    ESP_LOGI(TAG, "   URL: %s", register_url);
                    ESP_LOGI(TAG, "   Payload: %s", register_payload);
                    
                    esp_err_t err = esp_http_client_perform(client);
                    if (err == ESP_OK) {
                        int status_code = esp_http_client_get_status_code(client);
                        ESP_LOGI(TAG, "Registration response status: %d", status_code);
                        if (status_code == 200 || status_code == 201) {
                            ESP_LOGI(TAG, "✅ ESP32 IP registered with dashboard: " IPSTR, IP2STR(&event->ip_info.ip));
                            registration_success = true;
                        } else {
                            ESP_LOGW(TAG, "⚠️ Dashboard registration warning: Status=%d", status_code);
                            // Đọc response body để debug
                            int content_length = esp_http_client_get_content_length(client);
                            if (content_length > 0) {
                                char *buffer = malloc(content_length + 1);
                                if (buffer) {
                                    int data_read = esp_http_client_read_response(client, buffer, content_length);
                                    if (data_read > 0) {
                                        buffer[data_read] = '\0';
                                        ESP_LOGW(TAG, "   Response: %s", buffer);
                                    }
                                    free(buffer);
                                }
                            }
                        }
                    } else {
                        ESP_LOGW(TAG, "❌ Dashboard registration failed (attempt %d/2): %s", 
                                retry + 1, esp_err_to_name(err));
                        ESP_LOGW(TAG, "   Error code: 0x%x", err);
                        
                        if (err == ESP_ERR_HTTP_CONNECT) {
                            ESP_LOGW(TAG, "   → Cannot connect to %s:%d", dashboard_host_temp, dashboard_port_temp);
                            ESP_LOGW(TAG, "   → Please check:");
                            ESP_LOGW(TAG, "     1. Dashboard server is running on port %d", dashboard_port_temp);
                            ESP_LOGW(TAG, "     2. Hostname/IP is correct (current: %s)", dashboard_host_temp);
                            ESP_LOGW(TAG, "        → If using hostname, ensure DNS resolution works");
                            ESP_LOGW(TAG, "        → If using IP, verify it's accessible");
                            ESP_LOGW(TAG, "     3. Server is accessible from ESP32 network");
                            ESP_LOGW(TAG, "     4. Firewall is not blocking port %d", dashboard_port_temp);
                            ESP_LOGW(TAG, "     5. Update config via: http://" IPSTR "/config", IP2STR(&event->ip_info.ip));
                            ESP_LOGW(TAG, "   → Test from computer: curl http://%s:%d/api/esp32/register", 
                                    dashboard_host_temp, dashboard_port_temp);
                        } else {
                            // Log other error types
                            ESP_LOGW(TAG, "   → HTTP error type: %s (0x%x)", esp_err_to_name(err), err);
                        }
                    }
                    esp_http_client_cleanup(client);
                } else {
                    ESP_LOGE(TAG, "❌ Failed to initialize HTTP client for IP registration");
                }
            }
            
            if (!registration_success) {
                ESP_LOGW(TAG, "⚠️ IP registration failed after retries. Will retry when sending sensor data.");
            }
#endif

/**
 * @brief Trigger dashboard registration after config update
 * This function can be called from FileServer.c after saving new config
 * Note: Renamed to trigger_dashboard_registration_main to avoid conflict with wrapper in FileServer.c
 * Always defined to ensure linking works, even when CONFIG_DASHBOARD_ENABLED is disabled
 * This function will override the weak stub in FileServer.c
 * Note: Function is called via wrapper in FileServer.c, so compiler may show unused warning (false positive)
 */
__attribute__((used)) void trigger_dashboard_registration_main(void)
{
#if CONFIG_DASHBOARD_ENABLED
    // Small delay to ensure NVS is fully committed before reading
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Load fresh config from NVS (force reload, don't use cached values)
    char dashboard_host_temp[64];
    int dashboard_port_temp;
    get_dashboard_config(dashboard_host_temp, sizeof(dashboard_host_temp), &dashboard_port_temp);
    
    ESP_LOGI(TAG, "📋 Loaded dashboard config for registration: %s:%d", dashboard_host_temp, dashboard_port_temp);
    
    // Get current ESP32 IP
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        ESP_LOGW(TAG, "⚠️ Cannot trigger registration: No IP address");
        return;
    }
    
    ESP_LOGI(TAG, "🔄 Triggering dashboard registration with new config...");
    ESP_LOGI(TAG, "   ESP32 IP: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "   Dashboard server: %s:%d", dashboard_host_temp, dashboard_port_temp);
    
    char register_url[128];
    char register_payload[128];
    snprintf(register_url, sizeof(register_url), "http://%s:%d/api/esp32/register", 
             dashboard_host_temp, dashboard_port_temp);
    snprintf(register_payload, sizeof(register_payload),
             "{\"ip\":\"" IPSTR "\"}", IP2STR(&ip_info.ip));
    
    esp_http_client_config_t config = {
        .url = register_url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    if (client != NULL) {
        esp_http_client_set_header(client, "User-Agent", "ESP32-Client/1.0");
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, register_payload, strlen(register_payload));
        
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code == 200 || status_code == 201) {
                ESP_LOGI(TAG, "✅ Dashboard registration successful with new config!");
            } else {
                ESP_LOGW(TAG, "⚠️ Dashboard registration warning: Status=%d", status_code);
            }
        } else {
            ESP_LOGW(TAG, "❌ Dashboard registration failed: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    } else {
        ESP_LOGE(TAG, "❌ Failed to initialize HTTP client for registration");
    }
#else
    // Stub function when dashboard is disabled - do nothing
    ESP_LOGD(TAG, "Dashboard registration triggered but CONFIG_DASHBOARD_ENABLED is not enabled");
#endif
}

#ifdef CONFIG_RTC_TIME_SYNC
        if (sntp_syncTimeTask_handle == NULL)
        {
            ESP_LOGI(TAG, "Initializing SNTP...");
            if (sntp_initialize(NULL) == ESP_OK)
            {
                ESP_LOGI(TAG, "SNTP initialized successfully, creating sync task...");
                xTaskCreate(sntp_syncTime_task, "SNTP Get Time", (1024 * 4), NULL, (UBaseType_t)15, &sntp_syncTimeTask_handle);
                ESP_LOGI(TAG, "SNTP sync task created!");
            } else {
                ESP_LOGE(TAG, "Failed to initialize SNTP!");
            }
        } else {
            ESP_LOGW(TAG, "SNTP sync task already exists!");
        }
#else
        ESP_LOGW(TAG, "CONFIG_RTC_TIME_SYNC is not enabled! SNTP will not sync.");
#endif
        }
    } else if (event_base == SC_EVENT) {
        switch (event_id)
        {
        case SC_EVENT_SCAN_DONE:
        {
            ESP_LOGI(__func__, "Scan done.");
            break;
        }
        case SC_EVENT_FOUND_CHANNEL:
        {
            ESP_LOGI(__func__, "Found channel.");
            break;
        }
        case SC_EVENT_GOT_SSID_PSWD:
        {
            ESP_LOGI(__func__, "========== SMARTCONFIG: GOT SSID & PASSWORD ==========");

            smartconfig_event_got_ssid_pswd_t *smartconfig_event = (smartconfig_event_got_ssid_pswd_t *)event_data;
            wifi_config_t wifi_config;
            char ssid[33] = { 0 };
            char password[65] = { 0 };
            uint8_t rvd_data[33] = { 0 };

            bzero(&wifi_config, sizeof(wifi_config_t));
            memcpy(wifi_config.sta.ssid, smartconfig_event->ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, smartconfig_event->password, sizeof(wifi_config.sta.password));
            wifi_config.sta.bssid_set = smartconfig_event->bssid_set;
            if (wifi_config.sta.bssid_set == true) {
                memcpy(wifi_config.sta.bssid, smartconfig_event->bssid, sizeof(wifi_config.sta.bssid));
            }

            memcpy(ssid, smartconfig_event->ssid, sizeof(smartconfig_event->ssid));
            memcpy(password, smartconfig_event->password, sizeof(smartconfig_event->password));
            
            ESP_LOGI(TAG, "📶 SSID: %s", ssid);
            ESP_LOGI(TAG, "🔑 Password: %s", (strlen(password) > 0) ? "***" : "(empty)");
            ESP_LOGI(TAG, "📡 SmartConfig Type: %s", 
                     (smartconfig_event->type == SC_TYPE_ESPTOUCH_V2) ? "ESP-Touch V2" : "ESP-Touch");
            
            if (smartconfig_event->type == SC_TYPE_ESPTOUCH_V2) {
                ESP_ERROR_CHECK_WITHOUT_ABORT( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
                ESP_LOGD(TAG, "RVD_DATA:");
                for (int i = 0; i < 33; i++) {
                    printf("%02x ", rvd_data[i]);
                }
                printf("\n");
            }

            // Lưu WiFi config vào NVS để không cần config lại sau khi reset
            esp_err_t nvs_err = save_wifi_config_to_nvs(ssid, password);
            if (nvs_err != ESP_OK) {
                ESP_LOGW(TAG, "⚠️  Failed to save WiFi config to NVS, but continuing...");
            }

            ESP_ERROR_CHECK_WITHOUT_ABORT( esp_wifi_disconnect() );
            ESP_ERROR_CHECK_WITHOUT_ABORT( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
            ESP_LOGI(TAG, "🔄 Connecting to WiFi: %s", ssid);
            esp_wifi_connect();
            break;
        }
        case SC_EVENT_SEND_ACK_DONE:
        {
            if (smartConfigTask_handle != NULL) {
                xTaskNotifyGive(smartConfigTask_handle);
            }
            ESP_LOGI(__func__, "✅ SmartConfig ACK sent successfully!");
            break;
        }
        default:
            break;
        }
    } else {
        ESP_LOGI(__func__, "Other event id:%" PRIi32 "", event_id);
    }

    return;
}

/**
 * 
 * @brief This function initialize wifi and create, start WiFi handle such as loop (low priority)
 * 
 */
/**
 * @brief Load và cấu hình Static IP nếu được bật
 */
static void configure_static_ip_if_enabled(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE_STATIC_IP, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Static IP config not found, using DHCP");
        return;
    }
    
    // Kiểm tra xem static IP có được bật không
    uint8_t enabled = 0;
    size_t enabled_size = sizeof(enabled);
    err = nvs_get_blob(nvs_handle, NVS_KEY_STATIC_IP_ENABLED, &enabled, &enabled_size);
    if (err != ESP_OK || enabled == 0) {
        ESP_LOGD(TAG, "Static IP disabled, using DHCP");
        nvs_close(nvs_handle);
        return;
    }
    
    // Đọc IP, netmask, gateway
    char ip_str[16] = {0};
    char netmask_str[16] = {0};
    char gateway_str[16] = {0};
    size_t str_size = sizeof(ip_str);
    
    err = nvs_get_str(nvs_handle, NVS_KEY_STATIC_IP, ip_str, &str_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Static IP address not found in NVS");
        nvs_close(nvs_handle);
        return;
    }
    
    str_size = sizeof(netmask_str);
    err = nvs_get_str(nvs_handle, NVS_KEY_STATIC_NETMASK, netmask_str, &str_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Static netmask not found in NVS");
        nvs_close(nvs_handle);
        return;
    }
    
    str_size = sizeof(gateway_str);
    err = nvs_get_str(nvs_handle, NVS_KEY_STATIC_GATEWAY, gateway_str, &str_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Static gateway not found in NVS");
        nvs_close(nvs_handle);
        return;
    }
    
    nvs_close(nvs_handle);
    
    // Parse IP addresses
    ip4_addr_t ip, netmask, gateway;
    if (inet_aton(ip_str, &ip) == 0 || inet_aton(netmask_str, &netmask) == 0 || inet_aton(gateway_str, &gateway) == 0) {
        ESP_LOGE(TAG, "Invalid static IP configuration format");
        return;
    }
    
    // Lấy netif handle
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        ESP_LOGE(TAG, "Failed to get netif handle");
        return;
    }
    
    // Cấu hình static IP
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ip.addr;
    ip_info.netmask.addr = netmask.addr;
    ip_info.gw.addr = gateway.addr;
    
    err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(err));
        return;
    }
    
    err = esp_netif_set_ip_info(netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(err));
        // Khởi động lại DHCP nếu set static IP thất bại
        esp_netif_dhcpc_start(netif);
        return;
    }
    
    ESP_LOGI(TAG, "✅ Static IP configured: " IPSTR ", Netmask: " IPSTR ", Gateway: " IPSTR,
             IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));
}

void WIFI_initSTA(void)
{
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    (void)netif;  // Suppress unused variable warning - netif is created but not directly used
    
    // Cấu hình static IP nếu được bật (trước khi start WiFi)
    configure_static_ip_if_enabled();

    wifi_init_config_t WIFI_initConfig = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_init(&WIFI_initConfig));

    esp_event_handler_instance_t instance_any_id_Wifi;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_any_id_SmartConfig;

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFi_eventHandler, NULL, &instance_any_id_Wifi));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFi_eventHandler, NULL, &instance_got_ip));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(SC_EVENT, ESP_EVENT_ANY_ID, &WiFi_eventHandler, NULL, &instance_any_id_SmartConfig));

    static wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_SSID,
            .password = CONFIG_PASSWORD,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_start());

    ESP_LOGI(__func__, "WIFI initialize STA finished.");
    
    // Log WiFi configuration
    ESP_LOGI(__func__, "WiFi Configuration:");
    ESP_LOGI(__func__, "  SSID: %s", CONFIG_SSID);
    ESP_LOGI(__func__, "  Password: %s", (strlen(CONFIG_PASSWORD) > 0) ? "***" : "(empty)");
}

/**
 * @brief Kiểm tra và log trạng thái WiFi hiện tại
 */
static void check_wifi_status(void)
{
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "========== WiFi STATUS: CONNECTED ==========");
        ESP_LOGI(TAG, "SSID: %s", ap_info.ssid);
        ESP_LOGI(TAG, "RSSI: %d dBm", ap_info.rssi);
        ESP_LOGI(TAG, "Channel: %d", ap_info.primary);
        ESP_LOGI(TAG, "Auth mode: %d", ap_info.authmode);
    } else {
        ESP_LOGW(TAG, "========== WiFi STATUS: NOT CONNECTED ==========");
        ESP_LOGW(TAG, "Error getting WiFi status: %s", esp_err_to_name(ret));
    }
    
    // Kiểm tra IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        ret = esp_netif_get_ip_info(netif, &ip_info);
        if (ret == ESP_OK && ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
            ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
            
            // Nếu WiFi đã có IP nhưng SNTP chưa được khởi động, tự động khởi động SNTP
#ifdef CONFIG_RTC_TIME_SYNC
            if (sntp_syncTimeTask_handle == NULL) {
                ESP_LOGI(TAG, "WiFi has IP but SNTP not started yet, initializing SNTP...");
                if (sntp_initialize(NULL) == ESP_OK) {
                    ESP_LOGI(TAG, "SNTP initialized successfully, creating sync task...");
                    xTaskCreate(sntp_syncTime_task, "SNTP Get Time", (1024 * 4), NULL, (UBaseType_t)15, &sntp_syncTimeTask_handle);
                    ESP_LOGI(TAG, "SNTP sync task created!");
                } else {
                    ESP_LOGE(TAG, "Failed to initialize SNTP!");
                }
            }
#endif
        } else {
            ESP_LOGW(TAG, "No IP address assigned yet");
        }
    }
}

/*------------------------------------ BUTTON ------------------------------------ */

__attribute__((unused)) static IRAM_ATTR void button_Handle(void *parameters)
{
    button_disable((button_config_st *)parameters);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // xTaskNotifyFromISR(getDataFromSensorTask_handle, ULONG_MAX, eNoAction, &high_task_wakeup);
    BaseType_t result = xEventGroupSetBitsFromISR(button_event, BUTTON_PRESSED_BIT, &xHigherPriorityTaskWoken);
    if (result != pdFAIL)
    {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/*------------------------------------ UART COMMAND HANDLER ------------------------------------ */

/**
 * @brief UART command handler task - listens for commands via serial port
 * Supported commands:
 *   - START: Start sensor sampling
 *   - STOP: Stop current sampling cycle (will complete current cycle)
 *   - STATUS: Get system status
 */
static void uart_command_task(void *pvParameters)
{
    uint8_t data[128];
    int len;
    extern EventGroupHandle_t sampling_control_event;
    
    ESP_LOGI(__func__, "UART command handler task started");
    
    for (;;) {
        len = uart_read_bytes(UART_NUM_0, data, sizeof(data) - 1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            data[len] = '\0'; // Null terminate
            
            // Remove trailing newline/carriage return
            while (len > 0 && (data[len-1] == '\n' || data[len-1] == '\r')) {
                data[--len] = '\0';
            }
            
            ESP_LOGI(__func__, "Received UART command: %s", data);
            
            // Convert to uppercase for case-insensitive comparison
            for (int i = 0; i < len; i++) {
                if (data[i] >= 'a' && data[i] <= 'z') {
                    data[i] = data[i] - 'a' + 'A';
                }
            }
            
            // Process commands
            if (strcmp((char *)data, "START") == 0) {
                if (sampling_control_event != NULL) {
                    xEventGroupSetBits(sampling_control_event, START_SAMPLING_BIT);
                    ESP_LOGI(__func__, "✅ START command received via UART");
                    uart_write_bytes(UART_NUM_0, "OK: Sampling started\n", 22);
                } else {
                    ESP_LOGW(__func__, "⚠️  Sampling control event not initialized");
                    uart_write_bytes(UART_NUM_0, "ERROR: System not ready\n", 25);
                }
            } else if (strcmp((char *)data, "STOP") == 0) {
                ESP_LOGI(__func__, "📊 STOP command received (sampling will complete current cycle)");
                uart_write_bytes(UART_NUM_0, "OK: Stop command received\n", 27);
            } else if (strcmp((char *)data, "STATUS") == 0) {
                char status_msg[128];
                int msg_len = snprintf(status_msg, sizeof(status_msg), 
                    "STATUS: System ready\nSampling: %s\n",
                    (sampling_control_event != NULL) ? "Waiting for command" : "Not initialized");
                uart_write_bytes(UART_NUM_0, status_msg, msg_len);
            } else {
                ESP_LOGW(__func__, "Unknown command: %s", data);
                uart_write_bytes(UART_NUM_0, "ERROR: Unknown command\n", 23);
            }
        }
    }
}

/*------------------------------------ GET DATA FROM SENSOR ------------------------------------ */

void getDataFromSensor_task(void *parameters)
{
    struct dataSensor_st dataSensorTemp = {0};
    TickType_t task_lastWakeTime;
    TickType_t finishTime;

    getDataSensor_semaphore = xSemaphoreCreateMutex();


  //Thay cho nay bang ham khoi tao dht11

    //Set up ADS1115 (only 1 device)
    memset(ads111x_devices, 0, sizeof(ads111x_devices));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ads111x_init_desc(&ads111x_devices[0], addresses[0], CONFIG_ADS111X_I2C_PORT, CONFIG_ADS111X_I2C_MASTER_SDA, CONFIG_ADS111X_I2C_MASTER_SCL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ads111x_set_mode(&ads111x_devices[0], ADS111X_MODE_CONTINUOUS));    // Continuous conversion mode
    ESP_ERROR_CHECK_WITHOUT_ABORT(ads111x_set_data_rate(&ads111x_devices[0], ADS111X_DATA_RATE_128)); // 128 samples per second
    ESP_ERROR_CHECK_WITHOUT_ABORT(ads111x_set_gain(&ads111x_devices[0], ads111x_gain_values[ADS111X_GAIN_2V048]));


    // Button setup (disabled - no button on board)
    // Use HTTP API or UART command instead
    button_event = xEventGroupCreate();  // Keep for compatibility but not used
    ESP_LOGI(__func__, "ℹ️  Button disabled - use HTTP API or UART command to start sampling");
    
    // Note: sampling_control_event đã được khởi tạo trong app_main()
    
    for (;;)
    {
        // Chờ command để bắt đầu đo (HTTP API hoặc UART)
        ESP_LOGI(__func__, "========================================");
        ESP_LOGI(__func__, "⏸️  SYSTEM READY - Waiting for start command...");
        ESP_LOGI(__func__, "📡 Send HTTP POST to: http://<ESP32_IP>/api/start");
        ESP_LOGI(__func__, "📟 Or send UART command: START");
        int sampling_minutes = (SAMPLING_TIMME * portTICK_PERIOD_MS) / (60 * 1000);
        ESP_LOGI(__func__, "⏱️  Sampling duration: %d minutes", sampling_minutes);
        ESP_LOGI(__func__, "========================================");
        
        // Chờ start command từ HTTP API hoặc UART (blocking call)
        EventBits_t bits = xEventGroupWaitBits(sampling_control_event, START_SAMPLING_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        
        if (bits & START_SAMPLING_BIT) {
            ESP_LOGI(__func__, "✅ Start command received! Starting sensor sampling...");
        } else {
            ESP_LOGW(__func__, "⚠️  Unexpected event state");
            continue;
        }
        
        // Kiểm tra và cập nhật DS3231 từ system time nếu SNTP đã sync thành công
        // Mỗi lần bắt đầu chu kỳ sampling, kiểm tra system time và cập nhật DS3231 nếu hợp lệ
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // Nếu system time hợp lệ (sau 2020-01-01), cập nhật DS3231 từ system time
        // Điều này đảm bảo DS3231 luôn có thời gian thực nhất khi SNTP sync thành công
        if (now >= 1577836800) { // 2020-01-01 00:00:00 UTC
            ESP_LOGI(__func__, "System time is valid, updating DS3231 from system time: %02d/%02d/%04d %02d:%02d:%02d",
                     timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            set_ds3231_time_from_system();
        } else {
            ESP_LOGW(__func__, "System time is invalid (%lld), using DS3231 time", (long long)now);
        }
        
        // Tạo tên file mới theo thời gian thực mỗi lần bắt đầu chu kỳ sampling
        ESP_ERROR_CHECK_WITHOUT_ABORT(ds3231_convertTimeToString(&ds3231_device, nameFileSaveData, 14));
        ESP_LOGI(__func__, "Creating new file with real-time name: %s.csv", nameFileSaveData);
        
        // Tạo header cho file CSV mới
        if (SDcard_semaphore != NULL) {
            if (xSemaphoreTake(SDcard_semaphore, pdMS_TO_TICKS(5000)) == pdTRUE) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(sdcard_writeDataToFile(nameFileSaveData, "%s,%s,%s,%s,%s,%s,%s\n",
                                                                    "STT", "Temperature", "Humidity", "Sensor1", "Sensor2", "Sensor3", "Sensor4"));
                xSemaphoreGive(SDcard_semaphore);
            } else {
                ESP_LOGW(__func__, "Failed to get SD card semaphore to create CSV header");
            }
        } else {
            ESP_LOGW(__func__, "SDcard_semaphore is NULL, cannot safely write CSV header");
        }
        
        finishTime = xTaskGetTickCount() + SAMPLING_TIMME;
        static int sample_counter = 0; // Biến static để đếm liên tục qua các chu kỳ
        do
        {
            task_lastWakeTime = xTaskGetTickCount();
            sample_counter++; // Tăng counter trước
            dataSensorTemp.timeStamp = sample_counter;
            
            if (xSemaphoreTake(getDataSensor_semaphore, portMAX_DELAY))
            {
                // Đọc cảm biến DHT (nếu bật)
#if CONFIG_DHT_USE
                {
                    float temp = 0, hum = 0;
                    esp_err_t dht_err = dht_read_float(DHT_GPIO, DHT_TYPE, &hum, &temp);
                    if (dht_err == ESP_OK) {
                        dataSensorTemp.temperature = temp;
                        dataSensorTemp.humidity = hum;
                    } else {
                        ESP_LOGW(__func__, "DHT read failed: %s", esp_err_to_name(dht_err));
                    }
                }
#endif

                ESP_LOGI(__func__, "Sample #%d - Temperature: %f, Humidity: %f", dataSensorTemp.timeStamp, dataSensorTemp.temperature, dataSensorTemp.humidity);

          // Read 4 channels from single ADS1115
                // Threshold để phát hiện khi không có cảm biến (ADC floating noise)
                // Giá trị noise thường nằm trong khoảng 11000-11200 (0.6875V - 0.7V) khi không có tín hiệu
                const int16_t ADC_NOISE_MIN = 11000;
                const int16_t ADC_NOISE_MAX = 11200;
                bool all_channels_noise = true;
                
                for (size_t i = 0; i < 4; i++)
                {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(ads111x_set_input_mux(&ads111x_devices[0], (ads111x_mux_t)(i + 4)));
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    int16_t ADC_rawData = 0;
                    if (ads111x_get_value(&ads111x_devices[0], &ADC_rawData) == ESP_OK)
                    {
                        float voltage = ads111x_gain_values[ADS111X_GAIN_2V048] / ADS111X_MAX_VALUE * ADC_rawData;
                        
                        // Kiểm tra xem giá trị có nằm trong khoảng noise không
                        if (ADC_rawData < ADC_NOISE_MIN || ADC_rawData > ADC_NOISE_MAX) {
                            all_channels_noise = false;
                        }
                        
                        ESP_LOGI(__func__, "Channel %d - Raw ADC value: %d, Voltage: %.04f Volts. %s", 
                                i, ADC_rawData, voltage, 
                                (ADC_rawData >= ADC_NOISE_MIN && ADC_rawData <= ADC_NOISE_MAX) ? "[NOISE?]" : "[SIGNAL]");
                        dataSensorTemp.ADC_Value[i] = ADC_rawData;
                    }
                    else
                    {
                        ESP_LOGE(__func__, "Cannot read ADC value from channel %d.", i);
                        all_channels_noise = false; // Nếu không đọc được, không phải noise
                        dataSensorTemp.ADC_Value[i] = 0;
                    }
                }
                
                // Cảnh báo nếu tất cả channel đều trong khoảng noise (có thể không có cảm biến)
                if (all_channels_noise) {
                    ESP_LOGW(__func__, "WARNING: All ADC channels show noise values (11000-11200). Sensors may not be connected!");
                }

                xSemaphoreGive(getDataSensor_semaphore); // Give mutex
                ESP_LOGI(__func__, "Read data from sensors completed!");

                if (xQueueSendToBack(dataSensorSentToSD_queue, (void *)&dataSensorTemp, WAIT_10_TICK * 10) != pdPASS)
                {
                    ESP_LOGE(__func__, "Failed to post the data sensor to dataSensorMidleware Queue.");
                }
                else
                {
                    ESP_LOGI(__func__, "Success to post the data sensor to dataSensorMidleware Queue.");
                }
                
                // Gửi dữ liệu đến dashboard queue (không cần SD card)
#if CONFIG_DASHBOARD_ENABLED
                if (dataSensorSentToDashboard_queue != NULL) {
                    if (xQueueSendToBack(dataSensorSentToDashboard_queue, (void *)&dataSensorTemp, WAIT_10_TICK * 10) != pdPASS) {
                        ESP_LOGW(__func__, "Failed to post data to dashboard queue.");
                    }
                }
#endif
            }
            
            // Reset ADC values, giữ lại temperature/humidity
            memset(dataSensorTemp.ADC_Value, 0, sizeof(dataSensorTemp.ADC_Value));
            
            vTaskDelayUntil(&task_lastWakeTime, PERIOD_GET_DATA_FROM_SENSOR);
            
        } while (task_lastWakeTime < finishTime);
        
        ESP_LOGI(__func__, "========================================");
        ESP_LOGI(__func__, "✅ SAMPLING CYCLE COMPLETED!");
        ESP_LOGI(__func__, "📊 Total samples collected: %d", sample_counter);
        ESP_LOGI(__func__, "💾 Data saved to: %s.csv", nameFileSaveData);
        ESP_LOGI(__func__, "========================================");
        
        // Clear sampling control event để chờ lần đo tiếp theo
        xEventGroupClearBits(sampling_control_event, START_SAMPLING_BIT);
        
        ESP_LOGI(__func__, "⏸️  System paused. Send start command again for next cycle...");
    }
};

/*------------------------------------ SAVE DATA ------------------------------------ */


/**
 * @brief Save data from SD queue to SD card
 * 
 * @param parameters 
 */
void saveDataSensorToSDcard_task(void *parameters)
{
    UBaseType_t message_stored = 0;
    struct dataSensor_st dataSensorReceiveFromQueue;

    for (;;)
    {
        message_stored = uxQueueMessagesWaiting(dataSensorSentToSD_queue);

        if (message_stored != 0) // Check if dataSensorSentToSD_queue not empty
        {
            if (xQueueReceive(dataSensorSentToSD_queue, (void *)&dataSensorReceiveFromQueue, WAIT_10_TICK * 50) == pdPASS) // Get data sesor from queue
            {
                ESP_LOGI(__func__, "Receiving data from queue successfully.");

                if (xSemaphoreTake(SDcard_semaphore, portMAX_DELAY) == pdTRUE)
                {
                    static esp_err_t errorCode_t;
                    // Create data string follow format
                    //dataSensorTemp.timeStamp +=1;        
                    errorCode_t = sdcard_writeDataToFile(nameFileSaveData, dataSensor_templateSaveToSDCard,
                                                        dataSensorReceiveFromQueue.timeStamp,
                                                        dataSensorReceiveFromQueue.temperature,
                                                        dataSensorReceiveFromQueue.humidity,
                                                        dataSensorReceiveFromQueue.ADC_Value[0],
                                                        dataSensorReceiveFromQueue.ADC_Value[1],
                                                        dataSensorReceiveFromQueue.ADC_Value[2],
                                                        dataSensorReceiveFromQueue.ADC_Value[3]);
                    ESP_LOGI(TAG, "Save task received mutex!");
                    xSemaphoreGive(SDcard_semaphore);
                    if (errorCode_t != ESP_OK)
                    {
                        ESP_LOGE(__func__, "sdcard_writeDataToFile(...) function returned error: 0x%.4X", errorCode_t);
                    }
                }
            }
            else
            {
                ESP_LOGI(__func__, "Receiving data from queue failed.");
                continue;
            }
        }

        vTaskDelay(PERIOD_SAVE_DATA_SENSOR_TO_SDCARD);
    }
};

#if CONFIG_DASHBOARD_ENABLED
/**
 * @brief HTTP event handler for dashboard POST requests
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Task để gửi dữ liệu sensor đến dashboard qua HTTP POST
 * Không cần SD card, gửi trực tiếp qua WiFi
 */
static void sendDataToDashboard_task(void *parameters)
{
    struct dataSensor_st dataSensorReceiveFromQueue;
    char url[128];
    char json_payload[512];
    struct tm timeinfo;
    time_t now;
    char time_str[64];
    
    // Load dashboard config từ NVS (hoặc dùng CONFIG default)
    char dashboard_host_temp[64];
    int dashboard_port_temp;
    get_dashboard_config(dashboard_host_temp, sizeof(dashboard_host_temp), &dashboard_port_temp);
    
    // Tạo URL từ cấu hình
    snprintf(url, sizeof(url), "http://%s:%d/api/esp32/data", 
             dashboard_host_temp, dashboard_port_temp);
    
    ESP_LOGI(TAG, "Dashboard HTTP POST task started. URL: %s", url);
    
    for (;;)
    {
        // Đợi dữ liệu từ queue
        if (xQueueReceive(dataSensorSentToDashboard_queue, (void *)&dataSensorReceiveFromQueue, 
                         pdMS_TO_TICKS(1000)) == pdPASS)
        {
            // Lấy thời gian hiện tại
            time(&now);
            localtime_r(&now, &timeinfo);
            strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
            
            // Lấy IP của ESP32 để gửi kèm trong payload (giúp server lưu IP)
            esp_netif_ip_info_t ip_info;
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            char ip_str[16] = "";
            if (netif != NULL) {
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
                }
            }
            
            // Tạo JSON payload (bao gồm IP nếu có)
            if (strlen(ip_str) > 0) {
                snprintf(json_payload, sizeof(json_payload),
                    "{"
                    "\"Time\":\"%s\","
                    "\"Temperature\":%.2f,"
                    "\"Humidity\":%.2f,"
                    "\"Pressure\":0,"
                    "\"EtOH1\":%d,"
                    "\"EtOH2\":%d,"
                    "\"EtOH3\":%d,"
                    "\"EtOH4\":%d,"
                    "\"ip\":\"%s\""
                    "}",
                    time_str,
                    dataSensorReceiveFromQueue.temperature,
                    dataSensorReceiveFromQueue.humidity,
                    dataSensorReceiveFromQueue.ADC_Value[0],
                    dataSensorReceiveFromQueue.ADC_Value[1],
                    dataSensorReceiveFromQueue.ADC_Value[2],
                    dataSensorReceiveFromQueue.ADC_Value[3],
                    ip_str);
            } else {
                snprintf(json_payload, sizeof(json_payload),
                    "{"
                    "\"Time\":\"%s\","
                    "\"Temperature\":%.2f,"
                    "\"Humidity\":%.2f,"
                    "\"Pressure\":0,"
                    "\"EtOH1\":%d,"
                    "\"EtOH2\":%d,"
                    "\"EtOH3\":%d,"
                    "\"EtOH4\":%d"
                    "}",
                    time_str,
                    dataSensorReceiveFromQueue.temperature,
                    dataSensorReceiveFromQueue.humidity,
                    dataSensorReceiveFromQueue.ADC_Value[0],
                    dataSensorReceiveFromQueue.ADC_Value[1],
                    dataSensorReceiveFromQueue.ADC_Value[2],
                    dataSensorReceiveFromQueue.ADC_Value[3]);
            }
            
            // Cấu hình HTTP client
            esp_http_client_config_t config = {
                .url = url,
                .event_handler = http_event_handler,
                .timeout_ms = 10000, // Tăng timeout lên 10 giây
                .skip_cert_common_name_check = true,
            };
            esp_http_client_handle_t client = esp_http_client_init(&config);
            
            if (client != NULL) {
                // Set headers
                esp_http_client_set_header(client, "User-Agent", "ESP32-Client/1.0");
                esp_http_client_set_method(client, HTTP_METHOD_POST);
                esp_http_client_set_header(client, "Content-Type", "application/json");
                esp_http_client_set_post_field(client, json_payload, strlen(json_payload));
                
                // Thực hiện POST request
                esp_err_t err = esp_http_client_perform(client);
                
                if (err == ESP_OK) {
                    int status_code = esp_http_client_get_status_code(client);
                    int content_length = esp_http_client_get_content_length(client);
                    
                    if (status_code == 200 || status_code == 201) {
                        ESP_LOGI(TAG, "✅ Dashboard POST success: Status=%d, Length=%d", status_code, content_length);
                    } else {
                        ESP_LOGW(TAG, "⚠️ Dashboard POST warning: Status=%d", status_code);
                    }
                } else {
                    ESP_LOGE(TAG, "❌ Dashboard POST failed: %s (0x%x)", esp_err_to_name(err), err);
                    if (err == ESP_ERR_HTTP_CONNECT) {
                        ESP_LOGE(TAG, "   → Cannot connect to dashboard server");
                        ESP_LOGE(TAG, "   → Will retry registration on next data send");
                        
                        // Thử đăng ký lại IP khi gửi data thất bại
                        // (có thể IP đã thay đổi hoặc server mới online)
                        esp_netif_ip_info_t ip_info;
                        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                        if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                            char retry_register_url[128];
                            char retry_register_payload[128];
                            snprintf(retry_register_url, sizeof(retry_register_url), "http://%s:%d/api/esp32/register", 
                                     dashboard_host_temp, dashboard_port_temp);
                            snprintf(retry_register_payload, sizeof(retry_register_payload),
                                     "{\"ip\":\"" IPSTR "\"}", IP2STR(&ip_info.ip));
                            
                            esp_http_client_config_t retry_config = {
                                .url = retry_register_url,
                                .event_handler = http_event_handler,
                                .timeout_ms = 5000,
                            };
                            esp_http_client_handle_t retry_client = esp_http_client_init(&retry_config);
                            if (retry_client != NULL) {
                                esp_http_client_set_method(retry_client, HTTP_METHOD_POST);
                                esp_http_client_set_header(retry_client, "Content-Type", "application/json");
                                esp_http_client_set_post_field(retry_client, retry_register_payload, strlen(retry_register_payload));
                                esp_err_t retry_err = esp_http_client_perform(retry_client);
                                if (retry_err == ESP_OK) {
                                    ESP_LOGI(TAG, "✅ IP re-registration successful during data send");
                                }
                                esp_http_client_cleanup(retry_client);
                            }
                        }
                    }
                }
                
                esp_http_client_cleanup(client);
            } else {
                ESP_LOGE(TAG, "❌ Failed to initialize HTTP client");
            }
        }
        
        // Đợi một chút trước khi kiểm tra lại
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

/*****************************************************************************************************/
/*-------------------------------  MAIN_APP DEFINE FUNCTIONS  ---------------------------------------*/
/*****************************************************************************************************/

static void initialize_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
}

void app_main(void)
{
    // esp_log_level_set("*", ESP_LOG_NONE);
    // Allow other core to finish initialization
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(__func__, "Starting app main.");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
        printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
            CONFIG_IDF_TARGET,
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
            (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
            (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    }
    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    ESP_LOGI(__func__, "Name device: %s.", CONFIG_NAME_DEVICE);

    // Initialize nvs partition
    ESP_LOGI(__func__, "Initialize nvs partition.");
    initialize_nvs();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_loop_create_default());
    // Wait a second for memory initialization
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    // Initialize UART for command interface (UART0 - USB Serial)
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_param_config(UART_NUM_0, &uart_config));
    ESP_LOGI(__func__, "✅ UART initialized for command interface");
    
    // Create UART command handler task
    xTaskCreate(uart_command_task, "UART_Command", (1024 * 4), NULL, 10, NULL);
    ESP_LOGI(__func__, "✅ UART command handler task created");

#ifdef ENABLE_I2C_TEST_MODE
    // ========== CHẠY TEST I2C DEVICES ==========
    ESP_LOGI(__func__, "I2C TEST MODE ENABLED - Starting I2C devices test...");
    start_i2c_test();
    
    // Giữ task chạy
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
#elif defined(ENABLE_SDCARD_TEST_MODE)
    // ========== CHẠY TEST SD CARD ==========
    ESP_LOGI(__func__, "SD CARD TEST MODE ENABLED - Starting SD card test...");
    start_sdcard_test();
    
    // Giữ task chạy
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
#else
    // ========== CHẠY ỨNG DỤNG CHÍNH ==========
#if (CONFIG_USING_SDCARD)
    // Initialize SPI Bus
    ESP_LOGI(__func__, "Initialize SD card with SPI interface.");
    esp_vfs_fat_mount_config_t mount_config_t = MOUNT_CONFIG_DEFAULT();
    spi_bus_config_t spi_bus_config_t = SPI_BUS_CONFIG_DEFAULT();
    sdmmc_host_t host_t = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_PIN_NUM_CS;
    slot_config.host_id = host_t.slot;

    ESP_ERROR_CHECK_WITHOUT_ABORT(sdcard_initialize(&mount_config_t, &g_sdcard, &host_t, &spi_bus_config_t, &slot_config));
    SDcard_semaphore = xSemaphoreCreateMutex();
    sdcard_mounted = (g_sdcard != NULL);

#endif // CONFIG_USING_SDCARD

    ESP_ERROR_CHECK_WITHOUT_ABORT(i2cdev_init());
    
    // Initialize DS3231 RTC
    ESP_ERROR_CHECK_WITHOUT_ABORT(ds3231_initialize(&ds3231_device, CONFIG_RTC_I2C_PORT, CONFIG_RTC_PIN_NUM_SDA, CONFIG_RTC_PIN_NUM_SCL));
    
    // ========== SET THỜI GIAN CHO DS3231 ==========
    // System time của ESP32 mặc định là epoch 0 (1970), sẽ được cập nhật từ SNTP sau khi WiFi kết nối
    // Kiểm tra system time và cập nhật DS3231 nếu hợp lệ (sau khi SNTP sync)
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (now >= 1577836800) { // Nếu system time hợp lệ (sau 2020-01-01)
        ESP_LOGI(TAG, "System time is valid, updating DS3231 from system time");
        set_ds3231_time_from_system();
    } else {
        ESP_LOGW(TAG, "System time is invalid, waiting for SNTP sync...");
    }
    
    // Thời gian sẽ được cập nhật tự động sau khi SNTP sync thành công (nếu có WiFi)
    // Xem hàm sntp_syncTime_task() để biết chi tiết
    
    // Create dataSensorQueue
    dataSensorSentToSD_queue = xQueueCreate(QUEUE_SIZE, sizeof(struct dataSensor_st));
    while (dataSensorSentToSD_queue == NULL)
    {
        ESP_LOGE(__func__, "Create dataSensorSentToSD Queue failed.");
        ESP_LOGI(__func__, "Retry to create dataSensorSentToSD Queue...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        dataSensorSentToSD_queue = xQueueCreate(QUEUE_SIZE, sizeof(struct dataSensor_st));
    };
    ESP_LOGI(__func__, "Create dataSensorSentToSD Queue success.");

#if CONFIG_DASHBOARD_ENABLED
    // Create queue để gửi dữ liệu đến dashboard
    dataSensorSentToDashboard_queue = xQueueCreate(QUEUE_SIZE, sizeof(struct dataSensor_st));
    while (dataSensorSentToDashboard_queue == NULL)
    {
        ESP_LOGE(__func__, "Create dataSensorSentToDashboard Queue failed.");
        ESP_LOGI(__func__, "Retry to create dataSensorSentToDashboard Queue...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        dataSensorSentToDashboard_queue = xQueueCreate(QUEUE_SIZE, sizeof(struct dataSensor_st));
    };
    ESP_LOGI(__func__, "Create dataSensorSentToDashboard Queue success.");
    
    // Load dashboard config từ NVS khi khởi động
    char dashboard_host_temp[64];
    int dashboard_port_temp;
    get_dashboard_config(dashboard_host_temp, sizeof(dashboard_host_temp), &dashboard_port_temp);
    
    // Create task để gửi dữ liệu đến dashboard qua HTTP POST (không cần SD card)
    xTaskCreate(sendDataToDashboard_task, "SendDataToDashboard", (1024 * 8), NULL, 15, NULL);
    ESP_LOGI(__func__, "Dashboard HTTP POST task created. Target: http://%s:%d/api/esp32/data", 
             dashboard_host_temp, dashboard_port_temp);
#endif

    // Khởi tạo sampling control event trước khi tạo task
    sampling_control_event = xEventGroupCreate();
    ESP_LOGI(__func__, "✅ Sampling control event initialized");
    
    // Create task to get data from sensor (32Kb stack memory| priority 25(max))
    // Period 5000ms
    xTaskCreate(getDataFromSensor_task, "GetDataSensor", (1024 * 32), NULL, 24, &getDataFromSensorTask_handle);

    // Create task to save data from sensor read by getDataFromSensor_task() to SD card (16Kb stack memory| priority 10)
    // Period 5000ms
    xTaskCreate(saveDataSensorToSDcard_task, "SaveDataSensor", (1024 * 16), NULL, (UBaseType_t)19, &saveDataSensorToSDcardTask_handle);

#if CONFIG_USING_WIFI
    WIFI_initSTA();
    
    // Đợi một chút rồi kiểm tra trạng thái WiFi
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    check_wifi_status();
    
    // Kiểm tra lại sau 5 giây để xem có kết nối được không
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    check_wifi_status();
#endif

#endif // ENABLE_I2C_TEST_MODE
}
