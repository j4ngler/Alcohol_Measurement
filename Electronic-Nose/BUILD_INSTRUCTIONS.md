# Hướng dẫn Build và Flash Code ESP-IDF

## Yêu cầu
- ESP-IDF đã được cài đặt và cấu hình
- ESP32 board đã kết nối qua USB
- Đã bật test mode trong `main/main.c` (uncomment `#define ENABLE_I2C_TEST_MODE`)

## Các bước thực hiện

### 1. Mở ESP-IDF Command Prompt
- Windows: Tìm "ESP-IDF Command Prompt" trong Start Menu
- Hoặc mở PowerShell và chạy:
  ```powershell
  . $HOME/esp/esp-idf/export.ps1
  ```
  (Thay đường dẫn theo nơi bạn cài ESP-IDF)

### 2. Di chuyển đến thư mục dự án
```bash
cd D:\esp5.1\Alcohol_measurement\Source_Code\Electronic-Nose
```

### 3. Cấu hình dự án (nếu chưa có sdkconfig)
```bash
idf.py menuconfig
```
- Nhấn `Q` để thoát nếu không cần thay đổi gì

### 4. Build dự án
```bash
idf.py build
```
Hoặc đơn giản:
```bash
idf.py
```

### 5. Flash code lên ESP32
```bash
idf.py flash
```

### 6. Xem log (monitor serial)
```bash
idf.py monitor
```
Hoặc flash và monitor cùng lúc:
```bash
idf.py flash monitor
```

### 7. Thoát monitor
Nhấn `Ctrl+]` để thoát monitor

## Các lệnh kết hợp thường dùng

### Build + Flash + Monitor cùng lúc:
```bash
idf.py build flash monitor
```

### Chỉ flash app (không flash bootloader):
```bash
idf.py app-flash monitor
```

### Xóa flash và flash lại:
```bash
idf.py erase-flash flash monitor
```

### Clean build (xóa build cũ):
```bash
idf.py fullclean
idf.py build
```

## Xử lý lỗi

### Lỗi không tìm thấy port COM:
- Kiểm tra ESP32 đã kết nối USB chưa
- Kiểm tra driver USB-to-Serial đã cài chưa
- Chỉ định port thủ công:
  ```bash
  idf.py -p COM3 flash monitor
  ```
  (Thay COM3 bằng port của bạn)

### Lỗi build:
- Chạy `idf.py fullclean` rồi build lại
- Kiểm tra ESP-IDF version: `idf.py --version`

## Kết quả mong đợi khi chạy test

Khi code chạy thành công, bạn sẽ thấy trong monitor:
1. Scan I2C bus và hiển thị các thiết bị tìm thấy
2. Test DS3231 - đọc thời gian và nhiệt độ
3. Test ADS111x - đọc giá trị từ 4 kênh ADC
4. Continuous test - đọc cả 2 thiết bị liên tục mỗi 2 giây

