# Environment Monitor Dashboard

![Node.js](https://img.shields.io/badge/Node.js-18%2B-green?logo=node.js)
![MongoDB](https://img.shields.io/badge/MongoDB-%3E%3D4.0-green?logo=mongodb)
![WebSocket](https://img.shields.io/badge/WebSocket-supported-blue)
![ESP32](https://img.shields.io/badge/ESP32-supported-blue)

---

**Environment Monitor Dashboard** is a comprehensive environmental monitoring system that collects and displays real-time sensor data from ESP32 devices, featuring OTA firmware updates, device control, and advanced data visualization.

The project includes:
- **Backend**: Node.js + Express + WebSocket + MongoDB
- **Frontend**: Pure HTML/CSS/JS, real-time via WebSocket, using UI libraries such as Highcharts, ProgressBar, Flatpickr...
- **Database**: MongoDB stores sensor data, firmware, and device configurations.

---

## 🚩 Key Features

- **Real-time Environmental Monitoring**:  
  - Displays temperature, humidity, pressure, PM1.0, PM2.5, PM10 data from ESP32 sensors
  - Interactive charts with real-time, hourly, and daily data views
- **Firmware Management**:
  - Upload firmware files (.bin) through web interface
  - OTA (Over-The-Air) updates for ESP32 devices
  - Version management and download APIs
- **Data Visualization**:
  - Real-time charts with Highcharts
  - Export charts and reports
  - Historical data analysis
- **ESP32-CAM Integration** (optional)
- **Weather API Integration**
- **Responsive Design** with modern glassmorphism UI

---

## 🏗 Project Structure

```
EMPortableServer/
├── Server.js              # Main Express + WebSocket server
├── models/
│   └── mongodb.js         # MongoDB operations and schemas
├── public/
│   ├── index.js          # Frontend JavaScript
│   ├── style.css         # Styling and UI components
│   └── imgs/             # Images and icons
├── pages/
│   └── index.html        # Main dashboard interface
├── controllers/           # (Available for future use)
├── routers/              # (Available for future use)
├── configs/              # (Available for future use)
└── README.md
```

- **Server.js**: REST API and WebSocket server, handles ESP32 data, manages multi-client connections
- **mongodb.js**: Database schemas and CRUD operations for sensor data and firmware
- **public/index.js**: WebSocket client, DOM manipulation, chart rendering, UI controls
- **index.html**: Modern dashboard interface with upload capabilities

---

## ⚡️ Quick Setup & Run

### 1. System Requirements

- Node.js >= 18
- MongoDB >= 4.0 (local or cloud)
- ESP32 with environmental sensors
- Modern browser (Chrome/Firefox/Edge)

### 2. Installation & Setup

```bash
# Clone the repository
git clone https://github.com/yourusername/EnvironmentMonitor.git
cd EnvironmentMonitor

# Install dependencies
npm install

# Start MongoDB (default localhost:27017)
# Configure connection in ./models/mongodb.js if using cloud MongoDB

# Start the server
node Server.js
# Server runs on port 3000 (HTTP) and 8080 (WebSocket)

# Access the dashboard
http://localhost:3000/
```

### 3. Basic Configuration

**MongoDB Database**: `EnvironmentMonitor`

**Collections**:
- `FirmwareOTA`: Stores firmware versions and binary data
- `RealTimeData`: Stores sensor readings and timestamps

**Ports**:
- HTTP: `http://localhost:3000`
- WebSocket: `ws://localhost:8080`

---

## 🧩 API Endpoints

### Firmware Management
- `POST /api/firmware/upload` - Upload new firmware
- `GET /api/firmware/download/:version` - Download firmware for ESP32
- `GET /api/firmware/info/:version` - Get firmware information

### WebSocket Communication
**Client Registration**:
```javascript
{ type: 'register', clientType: 'frontend' | 'esp32' }
```

**Message Types**:
| Message Type | From | To | Description |
|-------------|------|----|-----------| 
| `status-all` | ESP32 | Frontend | Send sensor data |
| `firmware-versions` | Frontend | Backend | Get firmware list |
| `ota` | Frontend | ESP32 | Send firmware update |
| `get-real-time-data-hourly` | Frontend | Backend | Get hourly data |
| `get-real-time-data-daily` | Frontend | Backend | Get daily data |

---

## 📊 Dashboard Features

- **Real-time Sensor Display**: Live updates of environmental data
- **Interactive Charts**: Switch between real-time, hourly, and daily views
- **Firmware Upload**: Drag-and-drop .bin file upload with progress tracking
- **Version Management**: View, download, and deploy firmware versions
- **Data Export**: Export charts and generate reports
- **Weather Integration**: Current weather conditions display
- **Responsive Design**: Works on desktop and mobile devices

---

## 🔧 ESP32 Integration

### Sensor Data Format
```json
{
  "type": "DataFromESP32",
  "Time": "2024-01-15T10:30:00Z",
  "Temperature": 25.5,
  "Humidity": 60.2,
  "Pressure": 1013.25,
  "PM1": 15.3,
  "PM25": 22.1,
  "PM10": 35.7
}
```

### Firmware Download
```cpp
// ESP32 code example
HTTPClient http;
http.begin("http://YOUR_SERVER_IP:3000/api/firmware/download/v1.2.3");
int httpCode = http.GET();
if (httpCode == HTTP_CODE_OK) {
  // Process firmware data for OTA update
}
```

---

## 🚀 Advanced Features

- **Multi-client Support**: Handle multiple ESP32 devices simultaneously
- **Real-time Synchronization**: Bidirectional data flow between devices and dashboard
- **File Integrity**: MD5 checksum verification for firmware files
- **Error Handling**: Comprehensive error management and user feedback
- **Scalable Architecture**: Easy to extend with additional sensors and features

---

## 🔒 Security & Validation

- File upload validation (.bin files only)
- File size limits (10MB maximum)
- MD5 checksum verification
- Input sanitization and validation
- Error handling and logging

---

## 📈 Future Development

- [ ] User authentication and authorization
- [ ] Multi-user support with role-based access
- [ ] Advanced analytics and machine learning
- [ ] Mobile app development
- [ ] Cloud deployment options
- [ ] Integration with IoT platforms
- [ ] Automated alerting and notifications

---

## 📄 License

Open source, free for educational and research purposes.

---

## 👨‍💻 Contact & Contribution

For suggestions, bug reports, or feature requests, please create an Issue or Pull Request.

**Environment Monitor Dashboard** - Monitoring the world around us, one sensor at a time! 🌍📊