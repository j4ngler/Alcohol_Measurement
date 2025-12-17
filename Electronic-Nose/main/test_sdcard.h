/**
 * @file test_sdcard.h
 * @brief Hàm test SD card (mount, ghi/đọc file)
 */

#ifndef TEST_SDCARD_H
#define TEST_SDCARD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bắt đầu test SD card (tạo task riêng)
 */
void start_sdcard_test(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_SDCARD_H */

