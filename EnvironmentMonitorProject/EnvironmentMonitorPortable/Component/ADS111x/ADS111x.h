/*
 * ADS111x driver header copied from Electronic-Nose project
 * Path gốc: Source_Code/Electronic-Nose/component/ADS111x/ADS111x.h
 */
#ifndef __ADS111X_H__
#define __ADS111X_H__

#include <stdbool.h>
#include <esp_err.h>
#include <i2cdev.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADS111X_ADDR_GND 0x48 //!< I2C device address with ADDR pin connected to ground
#define ADS111X_ADDR_VCC 0x49 //!< I2C device address with ADDR pin connected to VCC
#define ADS111X_ADDR_SDA 0x4a //!< I2C device address with ADDR pin connected to SDA
#define ADS111X_ADDR_SCL 0x4b //!< I2C device address with ADDR pin connected to SCL

#define ADS111X_MAX_VALUE 0x7fff //!< Maximum ADC value
#define ADS101X_MAX_VALUE 0x7ff

// ADS101X overrides
#define ADS101X_DATA_RATE_128      ADS111X_DATA_RATE_8
#define ADS101X_DATA_RATE_250      ADS111X_DATA_RATE_16
#define ADS101X_DATA_RATE_490      ADS111X_DATA_RATE_32
#define ADS101X_DATA_RATE_920      ADS111X_DATA_RATE_64
#define ADS101X_DATA_RATE_1600     ADS111X_DATA_RATE_128
#define ADS101X_DATA_RATE_2400     ADS111X_DATA_RATE_250
#define ADS101X_DATA_RATE_3300     ADS111X_DATA_RATE_475

typedef enum
{
    ADS111X_GAIN_6V144 = 0, //!< +-6.144V
    ADS111X_GAIN_4V096,     //!< +-4.096V
    ADS111X_GAIN_2V048,     //!< +-2.048V (default)
    ADS111X_GAIN_1V024,     //!< +-1.024V
    ADS111X_GAIN_0V512,     //!< +-0.512V
    ADS111X_GAIN_0V256,     //!< +-0.256V
    ADS111X_GAIN_0V256_2,   //!< +-0.256V (same as ADS111X_GAIN_0V256)
    ADS111X_GAIN_0V256_3,   //!< +-0.256V (same as ADS111X_GAIN_0V256)
} ads111x_gain_t;

extern const float ads111x_gain_values[];

typedef enum
{
    ADS111X_MUX_0_1 = 0,
    ADS111X_MUX_0_3,
    ADS111X_MUX_1_3,
    ADS111X_MUX_2_3,
    ADS111X_MUX_0_GND,
    ADS111X_MUX_1_GND,
    ADS111X_MUX_2_GND,
    ADS111X_MUX_3_GND,
} ads111x_mux_t;

typedef enum
{
    ADS111X_DATA_RATE_8 = 0,
    ADS111X_DATA_RATE_16,
    ADS111X_DATA_RATE_32,
    ADS111X_DATA_RATE_64,
    ADS111X_DATA_RATE_128,
    ADS111X_DATA_RATE_250,
    ADS111X_DATA_RATE_475,
    ADS111X_DATA_RATE_860
} ads111x_data_rate_t;

typedef enum
{
    ADS111X_MODE_CONTINUOUS = 0,
    ADS111X_MODE_SINGLE_SHOT
} ads111x_mode_t;

typedef enum
{
    ADS111X_COMP_MODE_NORMAL = 0,
    ADS111X_COMP_MODE_WINDOW
} ads111x_comp_mode_t;

typedef enum
{
    ADS111X_COMP_POLARITY_LOW = 0,
    ADS111X_COMP_POLARITY_HIGH
} ads111x_comp_polarity_t;

typedef enum
{
    ADS111X_COMP_LATCH_DISABLED = 0,
    ADS111X_COMP_LATCH_ENABLED
} ads111x_comp_latch_t;

typedef enum
{
    ADS111X_COMP_QUEUE_1 = 0,
    ADS111X_COMP_QUEUE_2,
    ADS111X_COMP_QUEUE_4,
    ADS111X_COMP_QUEUE_DISABLED
} ads111x_comp_queue_t;

esp_err_t ads111x_init_desc(i2c_dev_t *dev, uint8_t addr, i2c_port_t port,
        gpio_num_t sda_gpio, gpio_num_t scl_gpio);
esp_err_t ads111x_free_desc(i2c_dev_t *dev);
esp_err_t ads111x_is_busy(i2c_dev_t *dev, bool *busy);
esp_err_t ads111x_start_conversion(i2c_dev_t *dev);
esp_err_t ads111x_get_value(i2c_dev_t *dev, int16_t *value);
esp_err_t ads101x_get_value(i2c_dev_t *dev, int16_t *value);
esp_err_t ads111x_get_gain(i2c_dev_t *dev, ads111x_gain_t *gain);
esp_err_t ads111x_set_gain(i2c_dev_t *dev, ads111x_gain_t gain);
esp_err_t ads111x_get_input_mux(i2c_dev_t *dev, ads111x_mux_t *mux);
esp_err_t ads111x_set_input_mux(i2c_dev_t *dev, ads111x_mux_t mux);
esp_err_t ads111x_get_mode(i2c_dev_t *dev, ads111x_mode_t *mode);
esp_err_t ads111x_set_mode(i2c_dev_t *dev, ads111x_mode_t mode);
esp_err_t ads111x_get_data_rate(i2c_dev_t *dev, ads111x_data_rate_t *rate);
esp_err_t ads111x_set_data_rate(i2c_dev_t *dev, ads111x_data_rate_t rate);
esp_err_t ads111x_get_comp_mode(i2c_dev_t *dev, ads111x_comp_mode_t *mode);
esp_err_t ads111x_set_comp_mode(i2c_dev_t *dev, ads111x_comp_mode_t mode);
esp_err_t ads111x_get_comp_polarity(i2c_dev_t *dev, ads111x_comp_polarity_t *polarity);
esp_err_t ads111x_set_comp_polarity(i2c_dev_t *dev, ads111x_comp_polarity_t polarity);
esp_err_t ads111x_get_comp_latch(i2c_dev_t *dev, ads111x_comp_latch_t *latch);
esp_err_t ads111x_set_comp_latch(i2c_dev_t *dev, ads111x_comp_latch_t latch);
esp_err_t ads111x_get_comp_queue(i2c_dev_t *dev, ads111x_comp_queue_t *queue);
esp_err_t ads111x_set_comp_queue(i2c_dev_t *dev, ads111x_comp_queue_t queue);
esp_err_t ads111x_get_comp_low_thresh(i2c_dev_t *dev, int16_t *th);
esp_err_t ads111x_set_comp_low_thresh(i2c_dev_t *dev, int16_t th);
esp_err_t ads111x_get_comp_high_thresh(i2c_dev_t *dev, int16_t *th);
esp_err_t ads111x_set_comp_high_thresh(i2c_dev_t *dev, int16_t th);

#ifdef __cplusplus
}
#endif

#endif /* __ADS111X_H__ */


