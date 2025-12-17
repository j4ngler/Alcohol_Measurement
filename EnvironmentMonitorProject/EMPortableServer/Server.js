const express = require('express');
const path = require('path');
const WebSocket = require('ws');
const multer = require('multer');
const crypto = require('crypto');

const app = express();

let TemperatureValue = 50, HumidityValue = 0, PressureValue = 0, EtOH1Value = 0, EtOH2Value = 0, EtOH3Value = 0, EtOH4Value = 0, Time = "";
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

const port = 3000;
app.listen(port, () => {
  console.log(`Server is running at http://localhost:${port}`);
});

const wss = new WebSocket.Server({ port: 8080 });
const clients = new Map();

wss.on('connection', (ws) => {
  console.log('New client connected');

  ws.on('message', async (message) => {
    try {
      const data = JSON.parse(message);
      console.log('Received:', data);

      // Auto-assign clientType from incoming payload if not registered yet
      if (!ws.clientType && data.clientType) {
        ws.clientType = data.clientType;
        clients.set(ws, ws.clientType);
        console.log(`Client auto-registered as: ${ws.clientType}`);
      }

      if (data.type === 'register') {
        ws.clientType = data.clientType;
        clients.set(ws, ws.clientType);
        console.log(`Client registered as: ${ws.clientType}`);
        return;
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
    console.log('Client disconnected');
    clients.delete(ws);
  });

  ws.on('error', (error) => {
    console.error('WebSocket error:', error);
  });
});

console.log('WebSocket server is running on ws://localhost:8080');