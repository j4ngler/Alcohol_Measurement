# Alcohol Measurement System - Source Code

Repository chứa toàn bộ source code cho hệ thống đo nồng độ cồn, bao gồm:

## 📁 Cấu trúc thư mục

### 1. `Electronic-Nose/`
Firmware ESP-IDF ban đầu cho thiết bị đo nồng độ cồn:
- Đọc ADC từ ADS1115 (4 kênh)
- Sử dụng DS3231 RTC
- Lưu dữ liệu vào SD Card
- WiFi và Web Server
- SmartConfig cho cấu hình WiFi

**Components:**
- ADS111x: Driver ADC 16-bit
- DS3231: RTC module
- Button: GPIO interrupt handler
- DataManager: Quản lý dữ liệu sensor
- FileManager: Quản lý SD Card
- WebServer: HTTP server cho cấu hình
- SNTP_Sync: Đồng bộ thời gian

### 2. `EnvironmentMonitorProject/EnvironmentMonitorPortable/`
Firmware ESP-IDF được tối ưu hóa và refactor từ Electronic-Nose:
- Kiến trúc modular với TaskManager
- Chỉ tập trung vào đo nồng độ cồn (đã loại bỏ BME280, PMS7003, OLED)
- WebSocket client để gửi dữ liệu real-time
- WiFi Manager với captive portal
- FOTA support

**Components:**
- ADS111x: Driver ADC cho cảm biến nồng độ cồn
- DS3231: RTC module
- SD_Card: Lưu trữ dữ liệu
- TaskManager: Quản lý FreeRTOS tasks
- WifiManager: Quản lý WiFi (STA + AP mode)
- WebSocket: Gửi dữ liệu lên server
- SNTP_Sync: Đồng bộ thời gian
- FOTAManager: OTA updates

## 🎯 Sự khác biệt giữa 2 projects

| Tính năng | Electronic-Nose | EnvironmentMonitorPortable |
|-----------|----------------|---------------------------|
| Kiến trúc | Monolithic | Modular với TaskManager |
| Sensors | ADS1115 + DS3231 | ADS1115 + DS3231 |
| Display | Không | Không (đã bỏ OLED) |
| Communication | HTTP Server | WebSocket Client |
| WiFi Config | SmartConfig | Captive Portal |
| Data Storage | SD Card | SD Card |
| OTA | Không | Có (FOTA) |

## 🚀 Sử dụng

### Electronic-Nose
```bash
cd Electronic-Nose
idf.py build
idf.py flash
idf.py monitor
```

### EnvironmentMonitorPortable
```bash
cd EnvironmentMonitorProject/EnvironmentMonitorPortable
idf.py build
idf.py flash
idf.py monitor
```

## 📋 Yêu cầu

- **ESP-IDF** v5.1 hoặc mới hơn
- **ESP32** development board
- **ADS1115** ADC module
- **DS3231** RTC module
- **SD Card** module (SPI interface)

## 🔧 Cấu hình

Xem README.md trong từng project để biết chi tiết cấu hình:
- `Electronic-Nose/README.md` (nếu có)
- `EnvironmentMonitorProject/EnvironmentMonitorPortable/README.md`

## 📊 Dữ liệu

Cả 2 projects đều:
- Đọc 4 kênh ADC từ ADS1115
- Lưu dữ liệu với timestamp từ DS3231
- Gửi dữ liệu qua network (HTTP hoặc WebSocket)

## 👤 Tác giả

**j4ngler**

## 📄 License

Xem file LICENSE trong từng component để biết thêm chi tiết.

