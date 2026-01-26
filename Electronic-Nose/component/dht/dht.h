#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

typedef enum {
    DHT_TYPE_DHT11 = 0,
    DHT_TYPE_DHT22 = 1
} dht_type_t;

esp_err_t dht_read_float(gpio_num_t gpio, dht_type_t type, float *humidity, float *temperature);

