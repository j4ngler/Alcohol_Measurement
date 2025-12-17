# 🔧 Hướng Dẫn API Firmware Upload & Download

## 📋 Tổng Quan

Hệ thống đã được cập nhật với tính năng upload firmware và API download cho ESP32. Bạn có thể:

- ✅ Upload file firmware .bin qua giao diện web
- ✅ Download firmware từ xa cho ESP32
- ✅ Quản lý nhiều phiên bản firmware
- ✅ Kiểm tra checksum và thông tin file

## 🚀 Tính Năng Mới

### 1. Upload Dialog
- **Tab Upload**: Tải lên firmware mới với tên phiên bản và mô tả
- **Tab Versions**: Xem danh sách firmware đã upload
- **Progress Bar**: Hiển thị tiến trình upload real-time
- **Validation**: Kiểm tra file .bin và tên phiên bản

### 2. API Endpoints

#### Upload Firmware
```http
POST /api/firmware/upload
Content-Type: multipart/form-data

Form Data:
- versionName: string (required) - Tên phiên bản, VD: "v1.2.3"
- description: string (optional) - Mô tả firmware
- firmwareFile: file (required) - File .bin
```

**Response:**
```json
{
  "success": true,
  "message": "Firmware đã được tải lên thành công",
  "version": "v1.2.3",
  "fileSize": 1048576,
  "checksum": "a1b2c3d4e5f6..."
}
```

#### Download Firmware (cho ESP32)
```http
GET /api/firmware/download/:version
```

**Response Headers:**
```
Content-Type: application/octet-stream
Content-Disposition: attachment; filename="v1.2.3.bin"
Content-Length: 1048576
X-Firmware-Version: v1.2.3
X-Firmware-Checksum: a1b2c3d4e5f6...
X-Firmware-Size: 1048576
```

#### Get Firmware Info
```http
GET /api/firmware/info/:version
```

**Response:**
```json
{
  "success": true,
  "firmware": {
    "version": "v1.2.3",
    "description": "Fixed bugs and improved performance",
    "fileName": "firmware_v1.2.3.bin",
    "fileSize": 1048576,
    "uploadDate": "2024-01-15T10:30:00.000Z",
    "checksum": "a1b2c3d4e5f6..."
  }
}
```

## 🔧 Cách Sử Dụng

### 1. Upload Firmware qua Web Interface

1. Mở dashboard tại `http://localhost:3000`
2. Click nút **FOTA**
3. Chọn tab **Upload Firmware**
4. Điền thông tin:
   - **Version Name**: `v1.2.3`
   - **Firmware File**: Chọn file `.bin`
   - **Description**: Mô tả (tùy chọn)
5. Click **Upload Firmware**
6. Chờ upload hoàn tất

### 2. Download Firmware cho ESP32

#### Cách 1: HTTP Request
```cpp
// ESP32 Arduino Code
#include <HTTPClient.h>
#include <WiFi.h>

void downloadFirmware(String version) {
  HTTPClient http;
  
  String url = "http://YOUR_SERVER_IP:3000/api/firmware/download/" + version;
  http.begin(url);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    // Get file size from header
    String fileSize = http.header("X-Firmware-Size");
    String checksum = http.header("X-Firmware-Checksum");
    
    // Get firmware data
    WiFiClient* stream = http.getStreamPtr();
    
    // Process firmware data here
    // ... OTA update logic ...
    
    Serial.println("Firmware downloaded successfully");
  } else {
    Serial.println("Download failed: " + String(httpCode));
  }
  
  http.end();
}
```

#### Cách 2: Curl Command
```bash
# Download firmware
curl -O "http://localhost:3000/api/firmware/download/v1.2.3"

# Get firmware info
curl "http://localhost:3000/api/firmware/info/v1.2.3"
```

### 3. Kiểm Tra Firmware Info

```bash
# Lấy thông tin firmware
curl "http://localhost:3000/api/firmware/info/v1.2.3"
```

## 🗄️ Database Schema

### FirmwareOTA Collection
```javascript
{
  _id: ObjectId,
  ID: "Firmware",
  Version: "v1.2.3",           // String - Tên phiên bản
  DataHex: "a1b2c3d4...",      // String - Dữ liệu hex của file
  Description: "Fixed bugs...", // String - Mô tả
  FileName: "firmware.bin",    // String - Tên file gốc
  FileSize: 1048576,           // Number - Kích thước file (bytes)
  UploadDate: Date,            // Date - Ngày upload
  Checksum: "a1b2c3d4..."      // String - MD5 checksum
}
```

## 🔒 Bảo Mật & Validation

### File Upload Validation
- ✅ Chỉ cho phép file `.bin`
- ✅ Giới hạn kích thước 10MB
- ✅ Kiểm tra MIME type
- ✅ Tính toán MD5 checksum

### API Security
- ✅ Validation input parameters
- ✅ Error handling đầy đủ
- ✅ File size limits
- ✅ Checksum verification

## 🚨 Xử Lý Lỗi

### Upload Errors
```json
{
  "success": false,
  "message": "Chỉ cho phép file .bin"
}
```

### Download Errors
```json
{
  "success": false,
  "message": "Không tìm thấy firmware version: v1.2.3"
}
```

## 📱 ESP32 Integration Example

```cpp
// Complete ESP32 firmware update example
#include <HTTPClient.h>
#include <WiFi.h>
#include <Update.h>

class FirmwareUpdater {
private:
  String serverUrl = "http://YOUR_SERVER_IP:3000";
  
public:
  bool checkForUpdate(String currentVersion) {
    HTTPClient http;
    http.begin(serverUrl + "/api/firmware/info/latest");
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      // Parse JSON to get latest version
      // Compare with currentVersion
      return true; // Update available
    }
    return false;
  }
  
  bool downloadAndUpdate(String version) {
    HTTPClient http;
    http.begin(serverUrl + "/api/firmware/download/" + version);
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      int fileSize = http.getSize();
      String checksum = http.header("X-Firmware-Checksum");
      
      WiFiClient* stream = http.getStreamPtr();
      
      if (Update.begin(fileSize)) {
        size_t written = Update.writeStream(*stream);
        if (written == fileSize) {
          if (Update.end()) {
            Serial.println("OTA Success!");
            return true;
          }
        }
      }
    }
    return false;
  }
};
```

## 🎯 Best Practices

1. **Version Naming**: Sử dụng semantic versioning (v1.2.3)
2. **File Size**: Giữ firmware dưới 10MB
3. **Checksum**: Luôn verify checksum trước khi flash
4. **Backup**: Giữ backup firmware cũ trước khi update
5. **Testing**: Test firmware trên thiết bị test trước khi deploy

## 🔧 Troubleshooting

### Upload Issues
- Kiểm tra file có đúng định dạng `.bin`
- Đảm bảo file size < 10MB
- Kiểm tra kết nối mạng

### Download Issues
- Verify server IP và port
- Kiểm tra version name chính xác
- Check ESP32 memory đủ cho firmware

### Database Issues
- Kiểm tra MongoDB connection
- Verify database permissions
- Check disk space

---

**🎉 Chúc bạn sử dụng thành công hệ thống firmware upload/download!**
