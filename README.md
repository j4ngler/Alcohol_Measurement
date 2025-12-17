# Alcohol Measurement System - Source Code

Repository chứa source code cho hệ thống đo nồng độ cồn, bao gồm firmware ESP32 và dashboard server.

## 📁 Cấu trúc thư mục

### 1. `Electronic-Nose/`
**Firmware ESP-IDF** cho thiết bị đo nồng độ cồn:
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

### 2. `EnvironmentMonitorProject/EMPortableServer/`
**Dashboard Server** (Node.js + Express + WebSocket + MongoDB):
- Real-time monitoring: Hiển thị dữ liệu từ ESP32
- WebSocket server: Nhận dữ liệu từ ESP32 và gửi đến frontend
- Firmware OTA: Upload và quản lý firmware cho ESP32
- MongoDB: Lưu trữ dữ liệu sensor và firmware
- Charts: Biểu đồ real-time, hourly, daily

**Tech Stack:**
- Backend: Node.js + Express + WebSocket
- Frontend: HTML/CSS/JS (Highcharts, ProgressBar, Flatpickr)
- Database: MongoDB

## 🚀 Sử dụng

### Firmware (Electronic-Nose)
```bash
cd Electronic-Nose
idf.py build
idf.py flash
idf.py monitor
```

### Dashboard Server (EMPortableServer)
```bash
cd EnvironmentMonitorProject/EMPortableServer
npm install
node Server.js
# Server chạy trên port 3000 (HTTP) và 8080 (WebSocket)
# Truy cập: http://localhost:3000/
```

## 📋 Yêu cầu

### Firmware
- **ESP-IDF** v5.1 hoặc mới hơn
- **ESP32** development board
- **ADS1115** ADC module
- **DS3231** RTC module
- **SD Card** module (SPI interface)

### Dashboard
- **Node.js** >= 18
- **MongoDB** >= 4.0 (local hoặc cloud)
- Modern browser (Chrome/Firefox/Edge)

## 📊 Dữ liệu

Firmware ESP32:
- Đọc 4 kênh ADC từ ADS1115
- Lưu dữ liệu với timestamp từ DS3231
- Gửi dữ liệu qua WebSocket lên dashboard server

Dashboard:
- Nhận dữ liệu real-time từ ESP32 qua WebSocket
- Lưu trữ vào MongoDB
- Hiển thị biểu đồ và thống kê

## 👤 Tác giả

**j4ngler**

## 📄 License

Xem file LICENSE trong từng component để biết thêm chi tiết.

