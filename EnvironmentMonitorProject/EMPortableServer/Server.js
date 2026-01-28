const express = require('express');
const path = require('path');
const WebSocket = require('ws');
const multer = require('multer');
const crypto = require('crypto');
const fs = require('fs');
const csv = require('csv-parser');
const http = require('http');

const app = express();

let TemperatureValue = 50, HumidityValue = 0, EtOH1Value = 0, EtOH2Value = 0, EtOH3Value = 0, EtOH4Value = 0, Time = "";
const {
  getAllVersions,
  getDataFirmware,
  getRealTimeData,
  saveRealTimeData,
  saveFirmware,
  getFirmwareByVersion,
  deleteFirmwareByVersion
} = require("./models/mongodb");
const { Console } = require('console');

app.use(express.json());

// Logging middleware để debug requests
app.use((req, res, next) => {
  if (req.path.startsWith('/api/')) {
    console.log(`📡 [${new Date().toISOString()}] ${req.method} ${req.path}`);
  }
  next();
});

// Configure multer for file uploads
const storage = multer.memoryStorage();
const upload = multer({ 
  storage: storage,
  limits: {
    fileSize: 10 * 1024 * 1024 // 10MB limit
  },
  fileFilter: (req, file, cb) => {
    if (file.mimetype === 'application/octet-stream' || file.originalname.endsWith('.bin')) {
      cb(null, true);
    } else {
      cb(new Error('Chỉ cho phép file .bin'), false);
    }
  }
});

app.get('/', (req, res) => {
  res.sendFile(path.resolve(__dirname, 'pages/index.html'));
});

// ========== ESP32 CONFIGURATION ENDPOINTS (must be before static files) ==========
// Endpoint để lấy ESP32 config status
app.get('/api/esp32/config', async (req, res) => {
  console.log(`📥 [${new Date().toISOString()}] GET /api/esp32/config received`);
  try {
    let esp32IP = esp32HTTPIP;
    
    if (!esp32IP || esp32IP === 'unknown') {
      return res.json({ 
        success: false, 
        message: 'ESP32 not connected. Please ensure ESP32 has sent data at least once.',
        esp32IP: null
      });
    }
    
    const cleanIP = esp32IP.replace(/^::ffff:/, '');
    
    // Simply return ESP32 IP if found (no need to ping ESP32)
    res.json({ 
      success: true, 
      esp32IP: cleanIP,
      message: 'ESP32 IP found'
    });
    
  } catch (error) {
    console.error('❌ Get ESP32 config error:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// Endpoint để set ESP32 IP manually (khi ESP32 chưa tự đăng ký được)
app.post('/api/esp32/set-ip', express.json(), (req, res) => {
  console.log(`📥 [${new Date().toISOString()}] POST /api/esp32/set-ip received`);
  try {
    const { ip } = req.body;
    
    if (!ip) {
      return res.status(400).json({ 
        success: false, 
        message: 'Missing IP address in request body' 
      });
    }
    
    // Validate IP format (simple validation)
    const ipRegex = /^(\d{1,3}\.){3}\d{1,3}$/;
    if (!ipRegex.test(ip)) {
      return res.status(400).json({ 
        success: false, 
        message: 'Invalid IP address format' 
      });
    }
    
    const cleanIP = ip.replace(/^::ffff:/, '');
    esp32HTTPIP = cleanIP;
    console.log(`✅ ESP32 IP manually set to: ${esp32HTTPIP}`);
    
    res.json({ 
      success: true, 
      message: 'ESP32 IP set successfully',
      esp32IP: cleanIP
    });
    
  } catch (error) {
    console.error('❌ Set ESP32 IP error:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// Endpoint để cấu hình ESP32 dashboard IP từ dashboard server
app.post('/api/esp32/config', async (req, res) => {
  console.log(`📥 [${new Date().toISOString()}] POST /api/esp32/config received`);
  try {
    const { host, port } = req.body;
    
    if (!host || !port) {
      return res.status(400).json({ 
        success: false, 
        message: 'Missing host or port in request body' 
      });
    }
    
    // Tìm ESP32 IP
    let esp32IP = esp32HTTPIP;
    
    if (!esp32IP || esp32IP === 'unknown') {
      return res.status(404).json({ 
        success: false, 
        message: 'ESP32 not connected. Please ensure ESP32 has sent data at least once.',
        hint: 'ESP32 will register its IP automatically when it connects to WiFi.'
      });
    }
    
    // Remove IPv6 prefix if present
    const cleanIP = esp32IP.replace(/^::ffff:/, '');
    
    // Forward request đến ESP32
    const esp32ConfigUrl = `http://${cleanIP}/api/config/dashboard`;
    const configPayload = JSON.stringify({ host, port });
    
    console.log(`📤 Forwarding config request to ESP32: ${esp32ConfigUrl}`);
    console.log(`   Payload: ${configPayload}`);
    
    const http = require('http');
    const url = require('url');
    const parsedUrl = url.parse(esp32ConfigUrl);
    
    const options = {
      hostname: parsedUrl.hostname,
      port: parsedUrl.port || 80,
      path: parsedUrl.path,
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(configPayload)
      },
      timeout: 10000
    };
    
    const esp32Req = http.request(options, (esp32Res) => {
      let data = '';
      
      esp32Res.on('data', (chunk) => {
        data += chunk;
      });
      
      esp32Res.on('end', () => {
        console.log(`✅ ESP32 config response: ${data}`);
        try {
          const esp32Response = JSON.parse(data);
          if (esp32Response.status === 'success') {
            res.json({ 
              success: true, 
              message: `ESP32 dashboard config updated successfully`,
              esp32IP: cleanIP,
              config: { host, port }
            });
          } else {
            res.status(500).json({ 
              success: false, 
              message: `ESP32 config update failed: ${esp32Response.message || 'Unknown error'}` 
            });
          }
        } catch (parseError) {
          console.error('❌ Failed to parse ESP32 response:', parseError);
          res.status(500).json({ 
            success: false, 
            message: 'Failed to parse ESP32 response' 
          });
        }
      });
    });
    
    esp32Req.on('error', (error) => {
      console.error(`❌ ESP32 config request error: ${error.message}`);
      res.status(500).json({ 
        success: false, 
        message: `Failed to connect to ESP32: ${error.message}`,
        esp32IP: cleanIP
      });
    });
    
    esp32Req.on('timeout', () => {
      console.error(`❌ ESP32 config request timeout`);
      esp32Req.destroy();
      res.status(504).json({ 
        success: false, 
        message: 'Request to ESP32 timed out',
        esp32IP: cleanIP
      });
    });
    
    esp32Req.write(configPayload);
    esp32Req.end();
    
  } catch (error) {
    console.error('❌ ESP32 config error:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

app.use(express.static('public'));

// Upload firmware endpoint
app.post('/api/firmware/upload', upload.single('firmwareFile'), async (req, res) => {
  try {
    if (!req.file) {
      return res.status(400).json({ success: false, message: 'Không có file được tải lên' });
    }

    const { versionName, description } = req.body;
    
    if (!versionName) {
      return res.status(400).json({ success: false, message: 'Tên phiên bản là bắt buộc' });
    }

    // Convert buffer to hex string
    const hexData = req.file.buffer.toString('hex');
    
    // Calculate checksum
    const checksum = crypto.createHash('md5').update(req.file.buffer).digest('hex');
    
    // Save to database
    await saveFirmware(
      versionName,
      hexData,
      description || '',
      req.file.originalname,
      req.file.size,
      checksum
    );

    console.log(`✅ Firmware ${versionName} uploaded successfully`);
    res.json({ 
      success: true, 
      message: 'Firmware đã được tải lên thành công',
      version: versionName,
      fileSize: req.file.size,
      checksum: checksum
    });

  } catch (error) {
    console.error('❌ Upload error:', error);
    res.status(500).json({ 
      success: false, 
      message: 'Lỗi khi tải lên firmware: ' + error.message 
    });
  }
});

// Download firmware endpoint for ESP32
app.get('/api/firmware/download/:version', async (req, res) => {
  try {
    const { version } = req.params;
    
    const firmware = await getFirmwareByVersion(version);
    
    if (!firmware) {
      return res.status(404).json({ 
        success: false, 
        message: `Không tìm thấy firmware version: ${version}` 
      });
    }

    // Convert hex string back to buffer
    const buffer = Buffer.from(firmware.DataHex, 'hex');
    
    // Set headers for file download
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Disposition', `attachment; filename="${firmware.FileName || version}.bin"`);
    res.setHeader('Content-Length', buffer.length);
    res.setHeader('X-Firmware-Version', firmware.Version);
    res.setHeader('X-Firmware-Checksum', firmware.Checksum);
    res.setHeader('X-Firmware-Size', firmware.FileSize);
    
    console.log(`📥 Firmware ${version} downloaded by ESP32`);
    res.send(buffer);

  } catch (error) {
    console.error('❌ Download error:', error);
    res.status(500).json({ 
      success: false, 
      message: 'Lỗi khi tải xuống firmware: ' + error.message 
    });
  }
});

// Get firmware info endpoint
app.get('/api/firmware/info/:version', async (req, res) => {
  try {
    const { version } = req.params;
    
    const firmware = await getFirmwareByVersion(version);
    
    if (!firmware) {
      return res.status(404).json({ 
        success: false, 
        message: `Không tìm thấy firmware version: ${version}` 
      });
    }

    res.json({
      success: true,
      firmware: {
        version: firmware.Version,
        description: firmware.Description,
        fileName: firmware.FileName,
        fileSize: firmware.FileSize,
        uploadDate: firmware.UploadDate,
        checksum: firmware.Checksum
      }
    });

  } catch (error) {
    console.error('❌ Info error:', error);
    res.status(500).json({ 
      success: false, 
      message: 'Lỗi khi lấy thông tin firmware: ' + error.message 
    });
  }
});

// Delete firmware by version
app.delete('/api/firmware/:version', async (req, res) => {
  try {
    const { version } = req.params;
    const { deletedCount } = await deleteFirmwareByVersion(version);
    if (deletedCount === 0) {
      return res.status(404).json({ success: false, message: `Không tìm thấy firmware version: ${version}` });
    }
    console.log(`🗑️ Deleted firmware version: ${version}`);
    res.json({ success: true, message: 'Đã xóa firmware', version });
  } catch (error) {
    console.error('❌ Delete firmware error:', error);
    res.status(500).json({ success: false, message: 'Lỗi khi xóa firmware: ' + error.message });
  }
});

// ========== CSV DATA READING ENDPOINTS ==========

// Folder chứa file CSV từ SD card
const CSV_DATA_FOLDER = path.join(__dirname, 'data');

// Đảm bảo folder tồn tại
if (!fs.existsSync(CSV_DATA_FOLDER)) {
  fs.mkdirSync(CSV_DATA_FOLDER, { recursive: true });
  console.log(`📁 Created data folder: ${CSV_DATA_FOLDER}`);
}

// Endpoint để lấy danh sách file CSV
app.get('/api/csv/list', (req, res) => {
  try {
    const files = fs.readdirSync(CSV_DATA_FOLDER)
      .filter(file => file.endsWith('.csv'))
      .map(file => {
        const filePath = path.join(CSV_DATA_FOLDER, file);
        const stats = fs.statSync(filePath);
        return {
          filename: file,
          size: stats.size,
          created: stats.birthtime,
          modified: stats.mtime
        };
      })
      .sort((a, b) => b.modified - a.modified); // Sắp xếp theo thời gian sửa đổi mới nhất
    
    res.json({ success: true, files });
  } catch (error) {
    console.error('❌ Error reading CSV list:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// Endpoint để đọc dữ liệu từ file CSV
app.get('/api/csv/read/:filename', (req, res) => {
  try {
    const { filename } = req.params;
    const filePath = path.join(CSV_DATA_FOLDER, filename);
    
    if (!fs.existsSync(filePath)) {
      return res.status(404).json({ success: false, message: 'File không tồn tại' });
    }
    
    const results = [];
    fs.createReadStream(filePath)
      .pipe(csv())
      .on('data', (data) => {
        // Parse dữ liệu từ CSV (format: STT,Temperature,Humidity,Sensor1,Sensor2,Sensor3,Sensor4)
        const row = {
          STT: parseInt(data.STT) || 0,
          Temperature: parseFloat(data.Temperature) || 0,
          Humidity: parseFloat(data.Humidity) || 0,
          EtOH1: parseInt(data.Sensor1 || data.ADC0 || data.EtOH1) || 0,
          EtOH2: parseInt(data.Sensor2 || data.ADC1 || data.EtOH2) || 0,
          EtOH3: parseInt(data.Sensor3 || data.ADC2 || data.EtOH3) || 0,
          EtOH4: parseInt(data.Sensor4 || data.ADC3 || data.EtOH4) || 0,
          Timestamp: data.Timestamp || data.Time || ''
        };
        results.push(row);
      })
      .on('end', () => {
        res.json({ success: true, filename, data: results, count: results.length });
      })
      .on('error', (error) => {
        console.error('❌ Error reading CSV:', error);
        res.status(500).json({ success: false, message: error.message });
      });
  } catch (error) {
    console.error('❌ Error reading CSV file:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// Endpoint để upload file CSV
const csvUpload = multer({
  dest: CSV_DATA_FOLDER,
  fileFilter: (req, file, cb) => {
    if (file.mimetype === 'text/csv' || file.originalname.endsWith('.csv')) {
      cb(null, true);
    } else {
      cb(new Error('Chỉ cho phép file CSV'), false);
    }
  },
  limits: { fileSize: 50 * 1024 * 1024 } // 50MB
});

app.post('/api/csv/upload', csvUpload.single('csvFile'), (req, res) => {
  try {
    if (!req.file) {
      return res.status(400).json({ success: false, message: 'Không có file được tải lên' });
    }
    
    // Đổi tên file về tên gốc
    const originalName = req.file.originalname || `data_${Date.now()}.csv`;
    const newPath = path.join(CSV_DATA_FOLDER, originalName);
    fs.renameSync(req.file.path, newPath);
    
    console.log(`✅ CSV file uploaded: ${originalName}`);
    res.json({ 
      success: true, 
      message: 'File CSV đã được tải lên thành công',
      filename: originalName,
      size: req.file.size
    });
  } catch (error) {
    console.error('❌ Upload CSV error:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// Endpoint để gửi dữ liệu CSV qua WebSocket đến frontend
app.post('/api/csv/send-to-dashboard/:filename', (req, res) => {
  try {
    const { filename } = req.params;
    const filePath = path.join(CSV_DATA_FOLDER, filename);
    
    if (!fs.existsSync(filePath)) {
      return res.status(404).json({ success: false, message: 'File không tồn tại' });
    }
    
    const results = [];
    fs.createReadStream(filePath)
      .pipe(csv())
      .on('data', (data) => {
        const row = {
          STT: parseInt(data.STT) || 0,
          Temperature: parseFloat(data.Temperature) || 0,
          Humidity: parseFloat(data.Humidity) || 0,
          EtOH1: parseInt(data.Sensor1 || data.ADC0 || data.EtOH1) || 0,
          EtOH2: parseInt(data.Sensor2 || data.ADC1 || data.EtOH2) || 0,
          EtOH3: parseInt(data.Sensor3 || data.ADC2 || data.EtOH3) || 0,
          EtOH4: parseInt(data.Sensor4 || data.ADC3 || data.EtOH4) || 0,
          Timestamp: data.Timestamp || data.Time || ''
        };
        results.push(row);
      })
      .on('end', () => {
        // Gửi dữ liệu đến tất cả frontend clients qua WebSocket
        clients.forEach((clientType, client) => {
          if (client.readyState === WebSocket.OPEN && clientType === 'frontend') {
            client.send(JSON.stringify({
              type: 'csv-data',
              filename: filename,
              data: results,
              count: results.length
            }));
          }
        });
        
        console.log(`✅ Sent CSV data to dashboard: ${filename} (${results.length} rows)`);
        res.json({ success: true, message: `Đã gửi ${results.length} dòng dữ liệu đến dashboard`, count: results.length });
      })
      .on('error', (error) => {
        console.error('❌ Error reading CSV:', error);
        res.status(500).json({ success: false, message: error.message });
      });
  } catch (error) {
    console.error('❌ Error sending CSV to dashboard:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// ========== ESP32 REGISTRATION ENDPOINT ==========
// Endpoint để ESP32 đăng ký IP khi khởi động (trước khi sampling)
app.post('/api/esp32/register', express.json(), (req, res) => {
  try {
    const data = req.body;
    let clientIP = null;
    
    // Ưu tiên IP từ body, nếu không thì lấy từ request
    if (data.ip) {
      clientIP = data.ip;
      console.log(`📡 ESP32 IP from registration body: ${clientIP}`);
    } else {
      clientIP = req.ip || 
                 req.connection?.remoteAddress || 
                 req.socket?.remoteAddress ||
                 (req.headers['x-forwarded-for'] ? req.headers['x-forwarded-for'].split(',')[0].trim() : null) ||
                 (req.headers['x-real-ip'] ? req.headers['x-real-ip'] : null);
    }
    
    if (clientIP && clientIP !== 'unknown' && clientIP !== '::1' && clientIP !== '127.0.0.1') {
      const cleanIP = clientIP.replace(/^::ffff:/, '');
      esp32HTTPIP = cleanIP;
      console.log(`✅ ESP32 registered with IP: ${esp32HTTPIP}`);
      res.json({ 
        success: true, 
        message: 'ESP32 registered successfully',
        ip: esp32HTTPIP 
      });
    } else {
      console.warn(`⚠️  Could not determine ESP32 IP from registration. IP: ${clientIP}`);
      res.status(400).json({ 
        success: false, 
        message: 'Could not determine ESP32 IP' 
      });
    }
  } catch (error) {
    console.error('❌ Error processing ESP32 registration:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// ========== HTTP POST ENDPOINT FOR ESP32 DATA ==========
// Endpoint để ESP32 gửi dữ liệu sensor qua HTTP POST (không cần SD card)
app.post('/api/esp32/data', express.json(), (req, res) => {
  try {
    const data = req.body;
    
    // Lưu IP của ESP32 từ HTTP request (ESP32 không dùng WebSocket)
    // Ưu tiên IP từ body nếu ESP32 gửi, nếu không thì lấy từ request
    let clientIP = null;
    if (data.ip) {
      // ESP32 có thể gửi IP trong body
      clientIP = data.ip;
      console.log(`📡 ESP32 IP from message body: ${clientIP}`);
    } else {
      // Lấy IP từ request headers/socket
      clientIP = req.ip || 
                 req.connection?.remoteAddress || 
                 req.socket?.remoteAddress ||
                 (req.headers['x-forwarded-for'] ? req.headers['x-forwarded-for'].split(',')[0].trim() : null) ||
                 (req.headers['x-real-ip'] ? req.headers['x-real-ip'] : null);
      
      if (clientIP) {
        console.log(`📡 ESP32 IP from request: ${clientIP}`);
      }
    }
    
    if (clientIP && clientIP !== 'unknown' && clientIP !== '::1' && clientIP !== '127.0.0.1') {
      const cleanIP = clientIP.replace(/^::ffff:/, '');
      if (cleanIP !== esp32HTTPIP) {
        esp32HTTPIP = cleanIP;
        console.log(`✅ ESP32 IP stored from HTTP POST: ${esp32HTTPIP}`);
      }
    } else {
      console.warn(`⚠️  Could not determine ESP32 IP from request. IP: ${clientIP}`);
    }
    
    console.log('📥 Received data from ESP32:', data);
    
    // Parse dữ liệu từ ESP32
    const receivedTime = data.Time || new Date().toISOString();
    const receivedTemp = Number(parseFloat(data.Temperature ?? 0));
    const receivedHum = Number(parseFloat(data.Humidity ?? 0));
    const receivedEtOH1 = Number(parseFloat(data.EtOH1 ?? data.ADC1 ?? data.ADC_Value?.[0] ?? 0));
    const receivedEtOH2 = Number(parseFloat(data.EtOH2 ?? data.ADC2 ?? data.ADC_Value?.[1] ?? 0));
    const receivedEtOH3 = Number(parseFloat(data.EtOH3 ?? data.ADC3 ?? data.ADC_Value?.[2] ?? 0));
    const receivedEtOH4 = Number(parseFloat(data.EtOH4 ?? data.ADC4 ?? data.ADC_Value?.[3] ?? 0));
    
    // Cập nhật giá trị global
    TemperatureValue = receivedTemp;
    HumidityValue = receivedHum;
    EtOH1Value = receivedEtOH1;
    EtOH2Value = receivedEtOH2;
    EtOH3Value = receivedEtOH3;
    EtOH4Value = receivedEtOH4;
    Time = receivedTime;
    
    // Gửi dữ liệu đến tất cả frontend clients qua WebSocket
    const fullStatus = {
      type: "status-all",
      data: {
        Temperature: TemperatureValue,
        Humidity: HumidityValue,
        EtOH1: EtOH1Value,
        EtOH2: EtOH2Value,
        EtOH3: EtOH3Value,
        EtOH4: EtOH4Value
      }
    };
    
    clients.forEach((clientType, client) => {
      if (client.readyState === WebSocket.OPEN && clientType === 'frontend') {
        client.send(JSON.stringify(fullStatus));
      }
    });
    
    console.log(`✅ Sent data to dashboard: ${JSON.stringify(fullStatus.data, null, 2)}`);
    
    // Lưu vào database nếu cần
    const DataRealTime = {
      ID: "Data",
      Time: Time,
      Temperature: TemperatureValue,
      Humidity: HumidityValue,
      EtOH1: EtOH1Value,
      EtOH2: EtOH2Value,
      EtOH3: EtOH3Value,
      EtOH4: EtOH4Value
    };
    saveRealTimeData(JSON.stringify(DataRealTime));
    
    res.json({ success: true, message: 'Data received and sent to dashboard' });
  } catch (error) {
    console.error('❌ Error processing ESP32 data:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// ========== PROXY ENDPOINTS FOR ESP32 SAMPLING CONTROL ==========
// Proxy endpoint để forward request đến ESP32
app.post('/api/esp32/start-sampling', async (req, res) => {
  console.log(`📥 [${new Date().toISOString()}] POST /api/esp32/start-sampling received`);
  try {
    // Tìm ESP32 IP từ HTTP POST requests (ESP32 không dùng WebSocket)
    let esp32IP = null;
    
    console.log(`🔍 Looking for ESP32 IP...`);
    console.log(`   ESP32 HTTP IP: ${esp32HTTPIP || 'not stored'}`);
    console.log(`   Total WebSocket clients: ${clients.size}`);
    console.log(`   Total ESP32 WebSocket IPs: ${esp32IPs.size}`);
    
    // Ưu tiên dùng IP từ HTTP POST (ESP32 chính)
    if (esp32HTTPIP && esp32HTTPIP !== 'unknown') {
      esp32IP = esp32HTTPIP;
      console.log(`   ✅ Using ESP32 HTTP IP: ${esp32IP}`);
    } else {
      // Fallback: Tìm từ WebSocket connections (nếu có ESP32 khác dùng WebSocket)
      esp32IPs.forEach((ip, ws) => {
        const clientType = clients.get(ws);
        const isOpen = ws.readyState === WebSocket.OPEN;
        console.log(`   Checking WebSocket: IP=${ip}, Type=${clientType}, Open=${isOpen}`);
        
        if (isOpen && clientType === 'esp32' && !esp32IP) {
          esp32IP = ip;
          console.log(`   ✅ Found ESP32 via WebSocket with IP: ${ip}`);
        }
      });
    }

    if (!esp32IP || esp32IP === 'unknown') {
      console.log(`❌ ESP32 IP not found.`);
      console.log(`   Available WebSocket clients:`);
      clients.forEach((clientType, ws) => {
        console.log(`   - Type: ${clientType}, Open: ${ws.readyState === WebSocket.OPEN}, IP: ${esp32IPs.get(ws) || 'not stored'}`);
      });
      
      return res.status(404).json({ 
        success: false, 
        message: 'ESP32 not connected. Please ensure ESP32 has sent data via HTTP POST to /api/esp32/data at least once, or ESP32 is currently sampling and sending data.',
        hint: 'ESP32 will send its IP automatically when it starts sampling. Make sure ESP32 is powered on and WiFi is connected.'
      });
    }

    // Remove IPv6 prefix if present (::ffff:192.168.1.100 -> 192.168.1.100)
    const cleanIP = esp32IP.replace(/^::ffff:/, '');
    const esp32Url = `http://${cleanIP}/api/start`;

    console.log(`📡 Forwarding start sampling request to ESP32 at ${esp32Url}`);

    // Forward request to ESP32
    const options = {
      hostname: cleanIP,
      port: 80,
      path: '/api/start',
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': 0
      },
      timeout: 5000
    };

    const esp32Req = http.request(options, (esp32Res) => {
      let data = '';
      esp32Res.on('data', (chunk) => {
        data += chunk;
      });
      esp32Res.on('end', () => {
        try {
          const response = JSON.parse(data);
          res.json({ 
            success: true, 
            message: 'Sampling started successfully',
            esp32Response: response 
          });
          console.log(`✅ ESP32 responded: ${data}`);
        } catch (e) {
          res.json({ 
            success: true, 
            message: 'Request sent to ESP32',
            rawResponse: data 
          });
        }
      });
    });

    esp32Req.on('error', (error) => {
      console.error(`❌ Error forwarding to ESP32: ${error.message}`);
      res.status(500).json({ 
        success: false, 
        message: `Failed to connect to ESP32: ${error.message}` 
      });
    });

    esp32Req.on('timeout', () => {
      esp32Req.destroy();
      res.status(504).json({ 
        success: false, 
        message: 'ESP32 request timeout' 
      });
    });

    esp32Req.end();

  } catch (error) {
    console.error('❌ Error in start-sampling proxy:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

// Proxy endpoint để check ESP32 status
app.get('/api/esp32/status', async (req, res) => {
  try {
    let esp32IP = null;
    esp32IPs.forEach((ip, ws) => {
      if (ws.readyState === WebSocket.OPEN && clients.get(ws) === 'esp32') {
        esp32IP = ip;
      }
    });

    if (!esp32IP || esp32IP === 'unknown') {
      return res.json({ 
        success: false, 
        connected: false,
        message: 'ESP32 not connected' 
      });
    }

    const cleanIP = esp32IP.replace(/^::ffff:/, '');
    const esp32Url = `http://${cleanIP}/api/status`;

    const options = {
      hostname: cleanIP,
      port: 80,
      path: '/api/status',
      method: 'GET',
      timeout: 3000
    };

    const esp32Req = http.request(options, (esp32Res) => {
      let data = '';
      esp32Res.on('data', (chunk) => {
        data += chunk;
      });
      esp32Res.on('end', () => {
        try {
          const response = JSON.parse(data);
          res.json({ 
            success: true, 
            connected: true,
            esp32IP: cleanIP,
            esp32Response: response 
          });
        } catch (e) {
          res.json({ 
            success: true, 
            connected: true,
            esp32IP: cleanIP,
            rawResponse: data 
          });
        }
      });
    });

    esp32Req.on('error', () => {
      res.json({ 
        success: false, 
        connected: false,
        message: 'Cannot reach ESP32' 
      });
    });

    esp32Req.on('timeout', () => {
      esp32Req.destroy();
      res.json({ 
        success: false, 
        connected: false,
        message: 'ESP32 timeout' 
      });
    });

    esp32Req.end();

  } catch (error) {
    console.error('❌ Error in status proxy:', error);
    res.status(500).json({ success: false, message: error.message });
  }
});

const wss = new WebSocket.Server({ port: 8080 });
const clients = new Map();
// Store ESP32 IP addresses (key: WebSocket connection, value: IP address)
const esp32IPs = new Map();
// Store ESP32 IP from HTTP POST requests (ESP32 không dùng WebSocket, chỉ dùng HTTP POST)
let esp32HTTPIP = null;

const port = 3000;

// Get local IP address for logging
const os = require('os');
const networkInterfaces = os.networkInterfaces();
let localIP = 'localhost';
for (const interfaceName in networkInterfaces) {
  const interfaces = networkInterfaces[interfaceName];
  for (const iface of interfaces) {
    // Skip internal (loopback) and non-IPv4 addresses
    if (iface.family === 'IPv4' && !iface.internal) {
      localIP = iface.address;
      break;
    }
  }
  if (localIP !== 'localhost') break;
}

// Listen on all network interfaces (0.0.0.0) to accept connections from ESP32
app.listen(port, '0.0.0.0', () => {
  console.log(`🚀 Server is running at http://localhost:${port}`);
  console.log(`🌐 Server is accessible at http://${localIP}:${port}`);
  console.log(`📁 CSV data folder: ${CSV_DATA_FOLDER}`);
  console.log(`✅ ESP32 endpoints registered:`);
  console.log(`   - POST /api/esp32/start-sampling`);
  console.log(`   - GET  /api/esp32/status`);
  console.log(`   - POST /api/esp32/data`);
  console.log(`   - POST /api/esp32/config (configure ESP32 dashboard IP)`);
  console.log(`   - GET  /api/esp32/config (get ESP32 connection status)`);
  console.log(`   - POST /api/esp32/register (ESP32 IP registration)`);
  console.log(`\n💡 Configure ESP32 to use: http://${localIP}:${port}`);
});

wss.on('connection', (ws) => {
  // Get client IP address from socket
  let clientIP = 'unknown';
  try {
    if (ws._socket && ws._socket.remoteAddress) {
      clientIP = ws._socket.remoteAddress;
    } else if (ws._socket && ws._socket.remoteAddress) {
      clientIP = ws._socket.remoteAddress;
    }
  } catch (e) {
    console.warn('Could not get client IP:', e.message);
  }
  console.log(`New client connected from IP: ${clientIP}`);

  ws.on('message', async (message) => {
    try {
      const data = JSON.parse(message);
      console.log('Received:', data);

      // Auto-assign clientType from incoming payload if not registered yet
      if (!ws.clientType && data.clientType) {
        ws.clientType = data.clientType;
        clients.set(ws, ws.clientType);
        console.log(`Client auto-registered as: ${ws.clientType}`);
        
        // Store ESP32 IP address when clientType is detected
        if (data.clientType === 'esp32' && !esp32IPs.has(ws)) {
          const esp32IP = data.ip || clientIP;
          esp32IPs.set(ws, esp32IP);
          console.log(`📡 ESP32 IP stored: ${esp32IP} (from ${data.ip ? 'message' : 'socket'})`);
        }
      }

      if (data.type === 'register') {
        ws.clientType = data.clientType;
        clients.set(ws, ws.clientType);
        // Store ESP32 IP address (use IP from message if provided, otherwise use socket IP)
        if (data.clientType === 'esp32') {
          const esp32IP = data.ip || clientIP;
          esp32IPs.set(ws, esp32IP);
          console.log(`📡 ESP32 registered with IP: ${esp32IP}`);
        }
        console.log(`Client registered as: ${ws.clientType}`);
        return;
      }
      
      // Nếu ESP32 gửi data nhưng chưa có IP được lưu, lưu IP ngay
      if (ws.clientType === 'esp32' && !esp32IPs.has(ws)) {
        const esp32IP = data.ip || clientIP;
        esp32IPs.set(ws, esp32IP);
        console.log(`📡 ESP32 IP stored from data message: ${esp32IP}`);
      }

      if (ws.clientType === 'frontend') {
        // Xử lý tin nhắn từ Frontend
        switch (data.type) {
          case 'firmware-versions':
            try {
              const versions = await getAllVersions();
              ws.send(JSON.stringify({ type: 'firmware-versions', success: true, versions }));
            } catch (error) {
              console.error('Error fetching firmware versions:', error);
              ws.send(JSON.stringify({ type: 'firmware-versions', success: false, message: 'Error fetching firmware versions' }));
            }
            break;

          case 'ota':
            console.log("OTA event received. Version:", data.version);

            // Gửi thông tin OTA đến ESP32 - chỉ gửi version
            const otaStartMessage = {
              type: 'ota-start',
              version: data.version
            };

            // Gửi tới ESP32
            clients.forEach((clientType, client) => {
              if (client.readyState === WebSocket.OPEN && clientType === 'esp32') {
                client.send(JSON.stringify(otaStartMessage));
                console.log(`📤 Sent OTA start message to ESP32: ${JSON.stringify(otaStartMessage)}`);
              }
            });

            // Gửi tới frontend để hiển thị
            clients.forEach((clientType, client) => {
              if (client.readyState === WebSocket.OPEN && clientType === 'frontend') {
                client.send(JSON.stringify({ type: 'ota-start', version: data.version }));
              }
            });

            break;

          case 'ota-upload':
            console.log("OTA upload event received. Version:", data.version);

            (async () => {
              try {
                const data_Firmware = await getDataFirmware(data.version);
                const lines = data_Firmware.trim()
                  .split('\n')
                  .map(line => line.trim())
                  .filter(line => line !== '');

                console.log("Total lines:", lines.length);

                for (let index = 0; index < lines.length; index++) {
                  const line = lines[index];
                  const Percent = parseFloat((((index + 1) / lines.length) * 100).toFixed(2));

                  const message = {
                    type: 'ota',
                    index,
                    Percent,
                    line
                  };

                  // Gửi tới ESP32
                  clients.forEach((clientType, client) => {
                    if (client.readyState === WebSocket.OPEN && clientType === 'esp32') {
                      client.send(JSON.stringify(message));
                    }
                  });

                  // Gửi tới frontend
                  clients.forEach((clientType, client) => {
                    if (client.readyState === WebSocket.OPEN && clientType === 'frontend') {
                      client.send(JSON.stringify(message));
                    }
                  });

                  // ⏱ Chờ 50ms hoặc điều chỉnh tùy tốc độ OTA thực tế
                  await new Promise(res => setTimeout(res, 40));
                }
                const junkMessage = {
                  type: 'junk'
                };
                clients.forEach((clientType, client) => {
                  if (client.readyState === WebSocket.OPEN && clientType === 'esp32') {
                    client.send(JSON.stringify(junkMessage));
                  }
                });

              } catch (error) {
                console.error("OTA send error:", error);
              }
            })();

            break;

          case 'sync-request':
            // Trả về trạng thái hiện tại từ Backend tới Frontend
            ws.send(JSON.stringify({
              type: 'status-all',
              data: {
                Temperature: Number(TemperatureValue),
                Humidity: Number(HumidityValue),
                Pressure: Number(PressureValue),
                PM1: Number(PM1Value),
                PM25: Number(PM25Value),
                PM10: Number(PM10Value)
              }
            }));
            console.log('📡 Sent current status to Frontend for sync-request');
            break;

          case 'get-real-time-data-hourly':
            const realHourlyData = await getRealTimeData();
            ws.send(JSON.stringify({ type: 'get-real-time-data-hourly', success: true, realHourlyData }));
            break;

          case 'get-real-time-data-daily':
            const realDailyData = await getRealTimeData();
            ws.send(JSON.stringify({ type: 'get-real-time-data-daily', success: true, realDailyData }));
            break;

          default:
            console.warn('Unknown Frontend message type:', data.type);
        }
      } else if (ws.clientType === 'esp32') {
        // Nếu ESP32 gửi data nhưng chưa có IP được lưu, lưu IP ngay
        if (!esp32IPs.has(ws)) {
          const esp32IP = data.ip || clientIP;
          esp32IPs.set(ws, esp32IP);
          console.log(`📡 ESP32 IP stored from data message: ${esp32IP}`);
        }

        switch (data.type) {
          case 'DataFromESP32':
            Time = data.Time || new Date().toISOString();
            TemperatureValue = Number(parseFloat((data.Temperature ?? TemperatureValue)));
            HumidityValue = Number(parseFloat((data.Humidity ?? HumidityValue)));
            PressureValue = Number(parseFloat((data.Pressure ?? PressureValue)));
            // Map EtOH values from Electronic-Nose (ADC_Value[0-3] or EtOH1-4 or ADC1-4)
            EtOH1Value = Number(parseFloat((data.EtOH1 ?? data.ADC1 ?? data.ADC_Value?.[0] ?? EtOH1Value)));
            EtOH2Value = Number(parseFloat((data.EtOH2 ?? data.ADC2 ?? data.ADC_Value?.[1] ?? EtOH2Value)));
            EtOH3Value = Number(parseFloat((data.EtOH3 ?? data.ADC3 ?? data.ADC_Value?.[2] ?? EtOH3Value)));
            EtOH4Value = Number(parseFloat((data.EtOH4 ?? data.ADC4 ?? data.ADC_Value?.[3] ?? EtOH4Value)));

            const fullStatus = {
              type: "status-all",
              data: {
                Temperature: Number(TemperatureValue),
                Humidity: Number(HumidityValue),
                Pressure: Number(PressureValue),
                EtOH1: Number(EtOH1Value),
                EtOH2: Number(EtOH2Value),
                EtOH3: Number(EtOH3Value),
                EtOH4: Number(EtOH4Value)
              }
            };

            // Gửi tới tất cả Frontend
            clients.forEach((clientType, client) => {
              if (client.readyState === WebSocket.OPEN && clientType === 'frontend') {
                client.send(JSON.stringify(fullStatus));
              }
            });
            console.log(`[ESP32] Updated status: ${JSON.stringify(fullStatus.data, null, 2)}`);
            break;
        }
        const DataRealTime = {
          ID: "Data",
          Time: Time,
          Temperature: TemperatureValue,
          Humidity: HumidityValue,
          Pressure: PressureValue,
          EtOH1: EtOH1Value,
          EtOH2: EtOH2Value,
          EtOH3: EtOH3Value,
          EtOH4: EtOH4Value
        };
        saveRealTimeData(JSON.stringify(DataRealTime));
      }
    } catch (error) {
      console.error('Error parsing message:', error);
    }
  });

  ws.on('close', () => {
    const clientType = clients.get(ws);
    const ip = esp32IPs.get(ws);
    console.log(`Client disconnected: Type=${clientType}, IP=${ip || 'unknown'}`);
    clients.delete(ws);
    esp32IPs.delete(ws); // Cleanup ESP32 IP
  });

  ws.on('error', (error) => {
    console.error('WebSocket error:', error);
  });
});

console.log('WebSocket server is running on ws://localhost:8080');