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
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_attr.h"
#include <spi_flash_mmap.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_ota_ops.h"
#include "esp_smartconfig.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_common.h"

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

__attribute__((unused)) static const char *TAG = "Main";

#define PERIOD_GET_DATA_FROM_SENSOR (TickType_t)(1000 / portTICK_PERIOD_MS)
#define PERIOD_SAVE_DATA_SENSOR_TO_SDCARD (TickType_t)(50 / portTICK_PERIOD_MS)
#define SAMPLING_TIMME  (TickType_t)(300000 / portTICK_PERIOD_MS)

#define NO_WAIT (TickType_t)(0)
#define WAIT_10_TICK (TickType_t)(10 / portTICK_PERIOD_MS)
#define WAIT_100_TICK (TickType_t)(100 / portTICK_PERIOD_MS)

#define QUEUE_SIZE 10U
#define DATA_SENSOR_MIDLEWARE_QUEUE_SIZE 20


#define BUTTON_PRESSED_BIT BIT1

TaskHandle_t getDataFromSensorTask_handle = NULL;
TaskHandle_t saveDataSensorToSDcardTask_handle = NULL;
TaskHandle_t sntp_syncTimeTask_handle = NULL;
TaskHandle_t allocateDataForMultipleQueuesTask_handle = NULL;
TaskHandle_t smartConfigTask_handle = NULL;

SemaphoreHandle_t getDataSensor_semaphore = NULL;
SemaphoreHandle_t SDcard_semaphore = NULL;

QueueHandle_t dataSensorSentToSD_queue = NULL;
// QueueHandle_t moduleError_queue = NULL;

//static EventGroupHandle_t fileStore_eventGroup;
static EventGroupHandle_t button_event;
static char nameFileSaveData[21] = "file";
static const char base_path[] = MOUNT_POINT;

/*------------------------------------ Define devices ------------------------------------ */
static i2c_dev_t ds3231_device = {0};
static i2c_dev_t ads111x_devices[CONFIG_ADS111X_DEVICE_COUNT] = {0};

// static i2c_dev_t pcf8574_device = {0};
//static i2c_dev_t pcf8575_device = {0};

// I2C addresses for ADS1115
// Đã scan và xác nhận thiết bị ở địa chỉ 0x48 (ADDR_GND)
const uint8_t addresses[CONFIG_ADS111X_DEVICE_COUNT] = {
    ADS111X_ADDR_GND,  // 0x48 - Đã xác nhận hoạt động
    ADS111X_ADDR_VCC   // 0x49 - Dự phòng nếu có thiết bị thứ 2
};

/*------------------------------------ WIFI ------------------------------------ */

/**
 * @brief SmartConfig task
 * 
 * @param parameter 
 */
static void smartConfig_task(void * parameter)
{
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t smartConfig_config = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&smartConfig_config));
    for(;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "smartconfig over");
        esp_smartconfig_stop();
        vTaskDelete(NULL);
    }
}

/**
 * @brief Set time from system time to DS3231 RTC
 */
static void set_ds3231_time_from_system(void)
{
    struct tm timeinfo;
    time_t now;
    
    // Get current system time
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Set time to DS3231
    esp_err_t ret = ds3231_setTime(&ds3231_device, &timeinfo);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 time set successfully: %02d/%02d/%04d %02d:%02d:%02d",
                 timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        ESP_LOGE(TAG, "Failed to set DS3231 time: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Set time manually to DS3231 RTC
 * 
 * @param year Year (e.g., 2024)
 * @param month Month (1-12)
 * @param day Day (1-31)
 * @param hour Hour (0-23)
 * @param minute Minute (0-59)
 * @param second Second (0-59)
 * @param wday Weekday (0=Sunday, 1=Monday, ..., 6=Saturday)
 */
static void set_ds3231_time_manual(int year, int month, int day, int hour, int minute, int second, int wday)
{
    struct tm timeinfo = {
        .tm_year = year - 1900,  // tm_year is years since 1900
        .tm_mon = month - 1,      // tm_mon is 0-11
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = second,
        .tm_wday = wday,
        .tm_isdst = 0
    };
    
    esp_err_t ret = ds3231_setTime(&ds3231_device, &timeinfo);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 time set manually: %02d/%02d/%04d %02d:%02d:%02d",
                 day, month, year, hour, minute, second);
    } else {
        ESP_LOGE(TAG, "Failed to set DS3231 time manually: %s", esp_err_to_name(ret));
    }
}

static void sntp_syncTime_task(void *parameter)
{
    ESP_LOGI(TAG, "SNTP sync task started");
    
    // Wait for WiFi to be connected
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // Set timezone to Vietnam
    sntp_setTimmeZoneToVN();
    
    // Print server information
    sntp_printServerInformation();
    
    // Sync time with SNTP servers
    if (sntp_syncTime() == ESP_OK) {
        ESP_LOGI(TAG, "Time synchronized successfully");
        
        // Set time to DS3231 RTC after SNTP sync
        set_ds3231_time_from_system();
    } else {
        ESP_LOGE(TAG, "Failed to synchronize time");
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
            xTaskCreate(smartConfig_task, "smartconfig_task", 1024 * 4, NULL, 15, &smartConfigTask_handle);
            ESP_LOGI(__func__, "Trying to connect with Wi-Fi...\n");
            esp_wifi_connect();
            break;
        }
        case WIFI_EVENT_STA_CONNECTED:
        {
            ESP_LOGI(__func__, "Wi-Fi connected AP SSID:%s password:%s.\n", CONFIG_SSID, CONFIG_PASSWORD);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            ESP_LOGI(__func__, "Wi-Fi disconnected: Retrying connect to AP SSID:%s password:%s", CONFIG_SSID, CONFIG_PASSWORD);
            esp_wifi_connect();
            break;
        }
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Got ip:" IPSTR, IP2STR(&event->ip_info.ip));

            start_file_server(base_path);

#ifdef CONFIG_RTC_TIME_SYNC
        if (sntp_syncTimeTask_handle == NULL)
        {
            if (sntp_initialize(NULL) == ESP_OK)
            {
                xTaskCreate(sntp_syncTime_task, "SNTP Get Time", (1024 * 4), NULL, (UBaseType_t)15, &sntp_syncTimeTask_handle);
            }
        }
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
            ESP_LOGI(__func__, "Got SSID and password.");

            smartconfig_event_got_ssid_pswd_t *smartconfig_event = (smartconfig_event_got_ssid_pswd_t *)event_data;
            wifi_config_t wifi_config;
            uint8_t ssid[33] = { 0 };
            uint8_t password[65] = { 0 };
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
            ESP_LOGI(TAG, "SSID:%s", ssid);
            ESP_LOGI(TAG, "PASSWORD:%s", password);
            if (smartconfig_event->type == SC_TYPE_ESPTOUCH_V2) {
                ESP_ERROR_CHECK_WITHOUT_ABORT( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
                ESP_LOGI(TAG, "RVD_DATA:");
                for (int i = 0; i < 33; i++) {
                    printf("%02x ", rvd_data[i]);
                }
                printf("\n");
            }

            ESP_ERROR_CHECK_WITHOUT_ABORT( esp_wifi_disconnect() );
            ESP_ERROR_CHECK_WITHOUT_ABORT( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
            esp_wifi_connect();
            break;
        }
        case SC_EVENT_SEND_ACK_DONE:
        {
            xTaskNotifyGive(smartConfigTask_handle);
            ESP_LOGI(__func__, "Send ACK done.");
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
void WIFI_initSTA(void)
{
    esp_netif_create_default_wifi_sta();

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
}

/*------------------------------------ BUTTON ------------------------------------ */

static IRAM_ATTR void button_Handle(void *parameters)
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


    // Setup button
    button_event = xEventGroupCreate();
    button_config_st button_config = {
        .io_config = {
            .intr_type = GPIO_INTR_POSEDGE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << 0),
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE
        },
        .gpio_num = 0,
    };

    ESP_ERROR_CHECK_WITHOUT_ABORT(button_init(&button_config, button_Handle, (void *)(&button_config)));
    // End setup for button

    for (;;)
    {
        // xTaskNotifyWait(0x00, ULONG_MAX, NULL, portMAX_DELAY);
        EventBits_t bits = xEventGroupWaitBits(button_event, BUTTON_PRESSED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & BUTTON_PRESSED_BIT)
        {
            ESP_LOGI(__func__, "Button pressed.");
            printf("Button pressed.\n");
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK_WITHOUT_ABORT(ds3231_convertTimeToString(&ds3231_device, nameFileSaveData, 14));
        ESP_ERROR_CHECK_WITHOUT_ABORT(sdcard_writeDataToFile(nameFileSaveData, "%s,%s,%s,%s,%s,%s,%s\n",   "STT", "Temperature", "Humidity", "Sensor1", "Sensor2", "Sensor3", "Sensor4"));
        finishTime = xTaskGetTickCount() + SAMPLING_TIMME;                                                                                                 // Thay ten tu etoh den nh3 bang ten các khi muc tieu cua cam bien ong dung
                                                                                                                                                        // Xem doc tu con nao dau tien
        dataSensorTemp.timeStamp = 0;
        do
        {
            task_lastWakeTime = xTaskGetTickCount();
            dataSensorTemp.timeStamp +=1;
            if (xSemaphoreTake(getDataSensor_semaphore, portMAX_DELAY))
            {
                dataSensorTemp.timeStamp +=1;
                //Thay day thanh ham doc gia tri dht11 xong roi luu ket qua vao bien nay dataSensorTemp.temperature vơi bien nay dataSensorTemp.humidity ung voi nhiet do va do am
                ESP_LOGI(__func__, "Temperature: %f, Humidity: %f", dataSensorTemp.temperature, dataSensorTemp.humidity);

          // Read 4 channels from single ADS1115
                for (size_t i = 0; i < 4; i++)
                {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(ads111x_set_input_mux(&ads111x_devices[0], (ads111x_mux_t)(i + 4)));
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    int16_t ADC_rawData = 0;
                    if (ads111x_get_value(&ads111x_devices[0], &ADC_rawData) == ESP_OK)
                    {
                        float voltage = ads111x_gain_values[ADS111X_GAIN_2V048] / ADS111X_MAX_VALUE * ADC_rawData;
                        ESP_LOGI(__func__, "Channel %d - Raw ADC value: %d, Voltage: %.04f Volts.", i, ADC_rawData, voltage);
                        dataSensorTemp.ADC_Value[i] = ADC_rawData;
                    }
                    else
                    {
                        ESP_LOGE(__func__, "Cannot read ADC value from channel %d.", i);
                    }
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
                dataSensorTemp.timeStamp +=1;
            };
            memset(&dataSensorTemp, 0, sizeof(struct dataSensor_st));
            vTaskDelayUntil(&task_lastWakeTime, PERIOD_GET_DATA_FROM_SENSOR);
            
        } while (task_lastWakeTime < finishTime);
        ESP_LOGI(__func__, "SAMPLING DONE");
        xEventGroupClearBits(button_event, BUTTON_PRESSED_BIT);
        button_enable(&button_config);
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

#ifdef ENABLE_I2C_TEST_MODE
    // ========== CHẠY TEST I2C DEVICES ==========
    ESP_LOGI(__func__, "I2C TEST MODE ENABLED - Starting I2C devices test...");
    start_i2c_test();
    
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

    sdmmc_card_t SDCARD;
    ESP_ERROR_CHECK_WITHOUT_ABORT(sdcard_initialize(&mount_config_t, &SDCARD, &host_t, &spi_bus_config_t, &slot_config));
    SDcard_semaphore = xSemaphoreCreateMutex();

#endif // CONFIG_USING_SDCARD

    ESP_ERROR_CHECK_WITHOUT_ABORT(i2cdev_init());
    
    // Initialize DS3231 RTC
    ESP_ERROR_CHECK_WITHOUT_ABORT(ds3231_initialize(&ds3231_device, CONFIG_RTC_I2C_PORT, CONFIG_RTC_PIN_NUM_SDA, CONFIG_RTC_PIN_NUM_SCL));
    
    // ========== SET THỜI GIAN CHO DS3231 (Nếu cần) ==========
    // Option 1: Set thời gian thủ công (uncomment và chỉnh sửa)
    // set_ds3231_time_manual(2024, 12, 5, 22, 15, 0, 4); // Năm, Tháng, Ngày, Giờ, Phút, Giây, Thứ (0=CN, 1=T2, ..., 6=T7)
    
    // Option 2: Thời gian sẽ được set tự động sau khi SNTP sync thành công (nếu có WiFi)
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

    // Create task to get data from sensor (32Kb stack memory| priority 25(max))
    // Period 5000ms
    xTaskCreate(getDataFromSensor_task, "GetDataSensor", (1024 * 32), NULL, 24, &getDataFromSensorTask_handle);

    // Create task to save data from sensor read by getDataFromSensor_task() to SD card (16Kb stack memory| priority 10)
    // Period 5000ms
    xTaskCreate(saveDataSensorToSDcard_task, "SaveDataSensor", (1024 * 16), NULL, (UBaseType_t)19, &saveDataSensorToSDcardTask_handle);

#if CONFIG_USING_WIFI
    WIFI_initSTA();
#endif

#endif // ENABLE_I2C_TEST_MODE
}
