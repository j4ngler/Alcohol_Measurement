/**
 * @file test_i2c_devices.h
 * @brief Header file cho I2C devices test
 * @author Nguyen Nhu Hai Long
 * @date 2024
 */

#ifndef TEST_I2C_DEVICES_H
#define TEST_I2C_DEVICES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bắt đầu test I2C devices (DS3231 và ADS111x)
 * 
 * Hàm này sẽ tạo một task để test cả hai thiết bị trên cùng bus I2C
 */
void start_i2c_test(void);

#ifdef __cplusplus
}
#endif

#endif // TEST_I2C_DEVICES_H

