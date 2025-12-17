/**
 * @file test_i2c_devices.c
 * @brief Test file để kiểm tra DS3231 và ADS111x trên cùng bus I2C
 * @author Nguyen Nhu Hai Long
 * @version 1.0
 * @date 2024
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "DS3231Time.h"
#include "ds3231.h"
#include "ADS111x.h"
#include "i2cdev.h"
#include "test_i2c_devices.h"

static const char *TAG = "I2C_TEST";

// Device descriptors
static i2c_dev_t ds3231_device = {0};
static i2c_dev_t ads111x_device = {0};

// I2C Configuration - Dùng chung cho cả 2 thiết bị
#define I2C_PORT            CONFIG_RTC_I2C_PORT
#define I2C_SDA_PIN         CONFIG_RTC_PIN_NUM_SDA
#define I2C_SCL_PIN         CONFIG_RTC_PIN_NUM_SCL
#define I2C_FREQ_HZ         400000

// ADS111x address - có thể thay đổi tùy theo cách nối ADDR pin
// Scan I2C bus sẽ cho biết địa chỉ thực tế của thiết bị
#define ADS111X_ADDRESS     ADS111X_ADDR_GND  // 0x48 (ADDR nối GND) - Đã scan thấy ở địa chỉ này

/**
 * @brief Scan I2C bus để tìm các thiết bị
 */
void i2c_scan_bus(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scanning I2C bus on port %d", I2C_PORT);
    ESP_LOGI(TAG, "SDA: GPIO%d, SCL: GPIO%d", I2C_SDA_PIN, I2C_SCL_PIN);
    ESP_LOGI(TAG, "========================================");
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,  // 100kHz để scan an toàn
    };
    
    esp_err_t ret = i2c_param_config(I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(ret));
        return;
    }
    
    uint8_t address;
    int devices_found = 0;
    
    ESP_LOGI(TAG, "Scanning addresses 0x08-0x77...");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    printf("00:                                          ");
    
    for (address = 1; address < 127; address++) {
        if (address % 16 == 0) {
            printf("\n%02x:", address);
        }
        
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            printf(" %02x", address);
            devices_found++;
            
            // In thông tin đặc biệt cho các thiết bị đã biết
            if (address == 0x68) {
                ESP_LOGI(TAG, "  -> Found DS3231 RTC at 0x%02X", address);
            } else if (address == 0x48) {
                ESP_LOGI(TAG, "  -> Found ADS111x (ADDR_GND) at 0x%02X", address);
            } else if (address == 0x49) {
                ESP_LOGI(TAG, "  -> Found ADS111x (ADDR_VCC) at 0x%02X", address);
            } else if (address == 0x4A) {
                ESP_LOGI(TAG, "  -> Found ADS111x (ADDR_SDA) at 0x%02X", address);
            } else if (address == 0x4B) {
                ESP_LOGI(TAG, "  -> Found ADS111x (ADDR_SCL) at 0x%02X", address);
            }
        } else {
            printf(" --");
        }
    }
    
    printf("\n");
    ESP_LOGI(TAG, "========================================");
    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found!");
        ESP_LOGW(TAG, "Check wiring and pull-up resistors!");
    } else {
        ESP_LOGI(TAG, "Total devices found: %d", devices_found);
    }
    ESP_LOGI(TAG, "========================================\n");
    
    i2c_driver_delete(I2C_PORT);
    vTaskDelay(500 / portTICK_PERIOD_MS);
}

/**
 * @brief Test DS3231 RTC
 */
esp_err_t test_ds3231(void)
{
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "TESTING DS3231 RTC");
    ESP_LOGI(TAG, "----------------------------------------");
    
    // Initialize DS3231
    esp_err_t ret = ds3231_initialize(&ds3231_device, I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 initialization FAILED: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "DS3231 initialization OK");
    
    // Read time from DS3231
    struct tm current_time;
    ret = ds3231_get_time(&ds3231_device, &current_time);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 read time FAILED: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "DS3231 Time: %02d:%02d:%02d", 
             current_time.tm_hour, current_time.tm_min, current_time.tm_sec);
    ESP_LOGI(TAG, "DS3231 Date: %02d/%02d/%04d", 
             current_time.tm_mday, current_time.tm_mon + 1, current_time.tm_year + 1900);
    
    // Read temperature from DS3231
    float temp;
    ret = ds3231_get_temp_float(&ds3231_device, &temp);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 Temperature: %.2f°C", temp);
    } else {
        ESP_LOGW(TAG, "DS3231 read temperature failed: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "DS3231 test: PASSED\n");
    return ESP_OK;
}

/**
 * @brief Test ADS111x ADC
 */
esp_err_t test_ads111x(void)
{
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "TESTING ADS111x ADC");
    ESP_LOGI(TAG, "----------------------------------------");
    
    // Initialize ADS111x descriptor
    esp_err_t ret = ads111x_init_desc(&ads111x_device, ADS111X_ADDRESS, 
                                      I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADS111x init_desc FAILED: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Address used: 0x%02X", ADS111X_ADDRESS);
        ESP_LOGI(TAG, "Try checking ADDR pin connection:");
        ESP_LOGI(TAG, "  - ADDR to GND  -> 0x48");
        ESP_LOGI(TAG, "  - ADDR to VCC  -> 0x49 (current)");
        ESP_LOGI(TAG, "  - ADDR to SDA  -> 0x4A");
        ESP_LOGI(TAG, "  - ADDR to SCL  -> 0x4B");
        return ret;
    }
    ESP_LOGI(TAG, "ADS111x init_desc OK (Address: 0x%02X)", ADS111X_ADDRESS);
    
    // Configure ADS111x
    ret = ads111x_set_mode(&ads111x_device, ADS111X_MODE_CONTINUOUS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADS111x set_mode FAILED: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ADS111x set_mode OK (Continuous)");
    
    ret = ads111x_set_data_rate(&ads111x_device, ADS111X_DATA_RATE_128);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADS111x set_data_rate FAILED: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ADS111x set_data_rate OK (128 SPS)");
    
    ret = ads111x_set_gain(&ads111x_device, ads111x_gain_values[ADS111X_GAIN_2V048]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADS111x set_gain FAILED: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ADS111x set_gain OK (2.048V)");
    
    // Wait for conversion
    vTaskDelay(200 / portTICK_PERIOD_MS);
    
    // Read all 4 channels
    ESP_LOGI(TAG, "Reading all 4 channels...");
    for (int channel = 0; channel < 4; channel++) {
        // Set input mux (AINx to GND)
        ret = ads111x_set_input_mux(&ads111x_device, (ads111x_mux_t)(channel + 4));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Channel %d: set_input_mux FAILED: %s", channel, esp_err_to_name(ret));
            continue;
        }
        
        // Wait for conversion
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        // Read value
        int16_t adc_value = 0;
        ret = ads111x_get_value(&ads111x_device, &adc_value);
        if (ret == ESP_OK) {
            float voltage = ads111x_gain_values[ADS111X_GAIN_2V048] / ADS111X_MAX_VALUE * adc_value;
            ESP_LOGI(TAG, "Channel %d: ADC=%6d, Voltage=%.4fV", channel, adc_value, voltage);
        } else {
            ESP_LOGE(TAG, "Channel %d: read FAILED: %s", channel, esp_err_to_name(ret));
        }
    }
    
    ESP_LOGI(TAG, "ADS111x test: PASSED\n");
    return ESP_OK;
}

/**
 * @brief Test task - chạy test liên tục
 */
void test_i2c_devices_task(void *pvParameters)
{
    ESP_LOGI(TAG, "\n\n");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "I2C DEVICES TEST STARTED");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize i2cdev library
    esp_err_t ret = i2cdev_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2cdev_init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "i2cdev library initialized");
    
    // Step 1: Scan I2C bus
    i2c_scan_bus();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Step 2: Test DS3231
    ret = test_ds3231();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 test FAILED!");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Step 3: Test ADS111x
    ret = test_ads111x();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADS111x test FAILED!");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Step 4: Test cả 2 thiết bị cùng lúc (loop)
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "CONTINUOUS TEST - Reading both devices");
    ESP_LOGI(TAG, "========================================");
    
    int test_count = 0;
    while (1) {
        test_count++;
        ESP_LOGI(TAG, "\n--- Test cycle #%d ---", test_count);
        
        // Read DS3231
        struct tm time;
        ret = ds3231_get_time(&ds3231_device, &time);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "DS3231: %02d:%02d:%02d %02d/%02d/%04d", 
                     time.tm_hour, time.tm_min, time.tm_sec,
                     time.tm_mday, time.tm_mon + 1, time.tm_year + 1900);
        } else {
            ESP_LOGE(TAG, "DS3231 read failed: %s", esp_err_to_name(ret));
        }
        
        // Read ADS111x channel 0
        ret = ads111x_set_input_mux(&ads111x_device, ADS111X_MUX_0_GND);
        if (ret == ESP_OK) {
            vTaskDelay(50 / portTICK_PERIOD_MS);
            int16_t adc_value = 0;
            ret = ads111x_get_value(&ads111x_device, &adc_value);
            if (ret == ESP_OK) {
                float voltage = ads111x_gain_values[ADS111X_GAIN_2V048] / ADS111X_MAX_VALUE * adc_value;
                ESP_LOGI(TAG, "ADS111x Ch0: ADC=%6d, Voltage=%.4fV", adc_value, voltage);
            } else {
                ESP_LOGE(TAG, "ADS111x read failed: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "ADS111x set_input_mux failed: %s", esp_err_to_name(ret));
        }
        
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Main test function - gọi từ app_main()
 */
void start_i2c_test(void)
{
    xTaskCreate(test_i2c_devices_task, "i2c_test_task", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "I2C test task created");
}

