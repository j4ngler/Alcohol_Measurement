# Alcohol Measurement System

Hệ thống đo nồng độ cồn sử dụng ESP32 với cảm biến ADC ADS1115, RTC DS3231, lưu trữ dữ liệu trên SD Card và giao tiếp qua WiFi/WebSocket.

## 📋 Tổng quan

Project này là firmware ESP-IDF cho thiết bị đo nồng độ cồn, sử dụng:
- **ESP32** làm vi xử lý chính
- **ADS1115** (ADC 16-bit) để đọc tín hiệu từ cảm biến nồng độ cồn (4 kênh)
- **DS3231** (RTC) để quản lý thời gian thực
- **SD Card** để lưu trữ dữ liệu đo
- **WiFi** và **WebSocket** để gửi dữ liệu lên server

## 🛠️ Kiến trúc

### Components chính

- **ADS111x**: Driver cho ADC ADS1115, đọc 4 kênh analog từ cảm biến
- **DS3231**: Driver cho RTC DS3231, quản lý thời gian
- **SD_Card**: Quản lý lưu trữ dữ liệu trên thẻ SD
- **TaskManager**: Quản lý các FreeRTOS tasks (đọc sensor, phân phối dữ liệu, gửi WebSocket, lưu SD)
- **WifiManager**: Quản lý kết nối WiFi (STA mode + AP mode cho cấu hình)
- **WebSocket**: Gửi dữ liệu đo được lên server qua WebSocket
- **SNTP_Sync**: Đồng bộ thời gian từ Internet
- **FOTAManager**: Hỗ trợ cập nhật firmware qua không dây (OTA)

### Luồng dữ liệu

```
readAllSensorsTask (đọc ADS1115 + DS3231)
    ↓
sensor_data_queue
    ↓
sensorDataDistributor
    ↓
    ├─→ display_data_queue → displayAndSendDataTask → WebSocket
    └─→ savedata_queue → SaveData → SD Card
```

## 📦 Yêu cầu

- **ESP-IDF** v5.1 hoặc mới hơn
- **ESP32** development board
- **ADS1115** ADC module
- **DS3231** RTC module
- **SD Card** module (SPI interface)
- Cảm biến nồng độ cồn kết nối với ADS1115

## 🔧 Cấu hình

### 1. Cấu hình I2C (DS3231 và ADS1115)

Trong `sdkconfig` hoặc menuconfig:
```
CONFIG_RTC_I2C_PORT=0
CONFIG_RTC_PIN_NUM_SDA=26
CONFIG_RTC_PIN_NUM_SCL=27
```

### 2. Cấu hình SD Card (SPI)

```
CONFIG_MISO_SDCARD=21
CONFIG_MOSI_SDCARD=19
CONFIG_CLK_SDCARD=18
CONFIG_CS_SDCARD=5
CONFIG_SPI_HOST_SDCARD=2
```

### 3. Cấu hình WiFi

```
CONFIG_WIFI_SSID="Your_WiFi_SSID"
CONFIG_WIFI_PASS="Your_WiFi_Password"
```

### 4. Cấu hình WebSocket Server

```
CONFIG_WS_URL="ws://your-server-ip:8080"
```

## 🚀 Build và Flash

### Build project

```bash
cd Source_Code/EnvironmentMonitorProject/EnvironmentMonitorPortable
idf.py build
```

### Flash firmware

```bash
idf.py flash
```

### Monitor serial output

```bash
idf.py monitor
```

## 📊 Dữ liệu

### Format dữ liệu gửi qua WebSocket

```json
{
  "type": "DataFromESP32",
  "clientType": "esp32",
  "Time": "2024-01-15T14:30:00",
  "PM1_0": 1234,    // ADC Channel 0 (raw value)
  "PM2_5": 2345,    // ADC Channel 1 (raw value)
  "PM10": 3456      // ADC Channel 2 (raw value)
}
```

### Format dữ liệu lưu trên SD Card

File được lưu theo format: `YY-MM-DD.txt`

Mỗi dòng:
```
HH:MM:SS-T0.00H0.00P0.00M11234M22345M33456SOK
```

Trong đó:
- `HH:MM:SS`: Thời gian
- `M1`, `M2`, `M3`: Giá trị ADC từ 3 kênh (tương ứng PM1_0, PM2_5, PM10)
- `S`: Trạng thái thiết bị

## 🔌 Kết nối phần cứng

### ADS1115
- VCC → 3.3V
- GND → GND
- SDA → GPIO 26
- SCL → GPIO 27
- ADDR → GND (địa chỉ I2C: 0x48)

### DS3231
- VCC → 3.3V
- GND → GND
- SDA → GPIO 26 (cùng bus I2C với ADS1115)
- SCL → GPIO 27

### SD Card (SPI)
- MISO → GPIO 21
- MOSI → GPIO 19
- CLK → GPIO 18
- CS → GPIO 5

## 📝 Ghi chú

- Project này được port từ `Electronic-Nose` và tối ưu hóa cho mục đích đo nồng độ cồn
- Đã loại bỏ các component không cần thiết: BME280, BMP280, PMS7003, SSD1306
- Dữ liệu được đọc liên tục mỗi 1 giây và gửi lên server qua WebSocket
- Hỗ trợ cấu hình WiFi qua captive portal (AP mode)

## 👤 Tác giả

**j4ngler**

## 📄 License

Xem file LICENSE trong từng component để biết thêm chi tiết.

