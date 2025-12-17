# Hướng dẫn Set Thời Gian cho DS3231 RTC

## Tổng quan

DS3231 RTC cần được set thời gian để hoạt động chính xác. Có 2 cách để set thời gian:

## Cách 1: Set tự động qua SNTP (Khuyến nghị)

Khi ESP32 kết nối WiFi và sync thời gian từ SNTP server thành công, thời gian sẽ tự động được set vào DS3231.

**Điều kiện:**
- WiFi đã được cấu hình và kết nối
- SNTP sync thành công
- Code đã có `CONFIG_RTC_TIME_SYNC` enabled

**Cách hoạt động:**
- Sau khi SNTP sync thành công, hàm `set_ds3231_time_from_system()` sẽ tự động được gọi
- Thời gian từ hệ thống sẽ được copy vào DS3231

## Cách 2: Set thủ công (Manual)

Nếu không có WiFi hoặc muốn set thời gian ngay khi khởi động:

### Bước 1: Mở file `main/main.c`

### Bước 2: Tìm dòng sau khi khởi tạo DS3231 (khoảng dòng 628-632):

```c
// Initialize DS3231 RTC
ESP_ERROR_CHECK_WITHOUT_ABORT(ds3231_initialize(&ds3231_device, CONFIG_RTC_I2C_PORT, CONFIG_RTC_PIN_NUM_SDA, CONFIG_RTC_PIN_NUM_SCL));

// ========== SET THỜI GIAN CHO DS3231 (Nếu cần) ==========
// Option 1: Set thời gian thủ công (uncomment và chỉnh sửa)
// set_ds3231_time_manual(2024, 12, 5, 22, 15, 0, 4);
```

### Bước 3: Uncomment và chỉnh sửa dòng set thời gian

**Cú pháp:**
```c
set_ds3231_time_manual(year, month, day, hour, minute, second, weekday);
```

**Tham số:**
- `year`: Năm (ví dụ: 2024)
- `month`: Tháng (1-12)
- `day`: Ngày (1-31)
- `hour`: Giờ (0-23, định dạng 24h)
- `minute`: Phút (0-59)
- `second`: Giây (0-59)
- `weekday`: Thứ trong tuần (0=Chủ Nhật, 1=Thứ 2, ..., 6=Thứ 7)

**Ví dụ:**
```c
// Set thời gian: 5/12/2024 22:15:00 (Thứ 5)
set_ds3231_time_manual(2024, 12, 5, 22, 15, 0, 4);

// Set thời gian: 1/1/2025 00:00:00 (Thứ 4)
set_ds3231_time_manual(2025, 1, 1, 0, 0, 0, 3);
```

### Bước 4: Build và flash lại

```bash
idf.py build flash monitor
```

## Kiểm tra thời gian đã set

Sau khi set thời gian, bạn có thể kiểm tra bằng cách:

1. **Trong code:** Đọc thời gian từ DS3231 và in ra log
2. **Trong test mode:** Chạy test I2C devices để xem thời gian hiện tại

## Lưu ý

1. **Timezone:** DS3231 lưu thời gian theo UTC. Nếu bạn set thời gian local, cần tính toán offset.
2. **Battery:** DS3231 có pin backup để giữ thời gian khi mất điện. Đảm bảo pin đã được lắp.
3. **Độ chính xác:** DS3231 có độ chính xác cao (±2ppm), nhưng nên sync định kỳ với SNTP để đảm bảo độ chính xác.

## Troubleshooting

### Thời gian không được set
- Kiểm tra DS3231 đã được khởi tạo thành công chưa
- Kiểm tra kết nối I2C (SDA, SCL)
- Kiểm tra log để xem có lỗi không

### Thời gian bị sai
- Kiểm tra timezone đã được set đúng chưa
- Nếu dùng SNTP, kiểm tra WiFi đã kết nối và sync thành công chưa
- Kiểm tra pin backup của DS3231

### Thời gian reset về giá trị mặc định
- Pin backup có thể đã hết hoặc không được lắp
- Kiểm tra nguồn cấp cho DS3231

