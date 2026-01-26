#include "dht.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

// Timeout chờ mức HIGH/LOW (µs). Nâng cao để dễ bắt xung hơn.
#define DHT_TIMEOUT_US 20000

static const char *TAG = "dht";

static esp_err_t dht_wait_level(gpio_num_t gpio, int level, uint32_t timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) == level) {
        if ((esp_timer_get_time() - start) > timeout_us) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t dht_read_once(gpio_num_t gpio, dht_type_t type, float *humidity, float *temperature)
{
    uint8_t data[5] = {0};

    // Start signal
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
    gpio_set_level(gpio, 0);
    if (type == DHT_TYPE_DHT11) {
        esp_rom_delay_us(18000); // 18 ms
    } else {
        esp_rom_delay_us(5000); // DHT22: giữ mức thấp 5 ms để chắc chắn
    }
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(80); // tăng thời gian kéo lên trước khi chờ đáp ứng
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);

    // Response
    if (dht_wait_level(gpio, 1, DHT_TIMEOUT_US) != ESP_OK) return ESP_ERR_TIMEOUT;
    if (dht_wait_level(gpio, 0, DHT_TIMEOUT_US) != ESP_OK) return ESP_ERR_TIMEOUT;
    if (dht_wait_level(gpio, 1, DHT_TIMEOUT_US) != ESP_OK) return ESP_ERR_TIMEOUT;

    // Read 40 bits
    for (int i = 0; i < 40; i++) {
        if (dht_wait_level(gpio, 0, DHT_TIMEOUT_US) != ESP_OK) return ESP_ERR_TIMEOUT;
        int64_t start = esp_timer_get_time();
        if (dht_wait_level(gpio, 1, DHT_TIMEOUT_US) != ESP_OK) return ESP_ERR_TIMEOUT;
        int64_t dur = esp_timer_get_time() - start;
        data[i / 8] <<= 1;
        if (dur > 60) data[i / 8] |= 1; // nâng ngưỡng lên 60us để phân biệt bit '1'
    }

    // Checksum
    uint8_t sum = data[0] + data[1] + data[2] + data[3];
    if ((sum & 0xFF) != data[4]) {
        ESP_LOGW(TAG, "DHT checksum fail");
        return ESP_ERR_INVALID_CRC;
    }

    if (type == DHT_TYPE_DHT11) {
        if (humidity) *humidity = data[0];
        if (temperature) *temperature = data[2];
    } else { // DHT22
        int16_t hum = (data[0] << 8) | data[1];
        int16_t temp = (data[2] << 8) | data[3];
        if (temp & 0x8000) temp = -(temp & 0x7FFF);
        if (humidity) *humidity = hum / 10.0f;
        if (temperature) *temperature = temp / 10.0f;
    }
    return ESP_OK;
}

esp_err_t dht_read_float(gpio_num_t gpio, dht_type_t type, float *humidity, float *temperature)
{
    // Thử đọc tối đa 3 lần, mỗi lần cách nhau 80 ms nếu lỗi
    for (int attempt = 0; attempt < 3; ++attempt) {
        esp_err_t err = dht_read_once(gpio, type, humidity, temperature);
        if (err == ESP_OK) return err;
        if (attempt == 2) return err;
        esp_rom_delay_us(80000); // 80 ms giữa các lần thử
    }
    return ESP_FAIL;
}

