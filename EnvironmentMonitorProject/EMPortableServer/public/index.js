// Constants and State Variables
const API_KEY = 'ec224bde787c4001b0281007251802';
const LOCATION = 'Hanoi';
const socket = new WebSocket('ws://localhost:8080');
let TemperatureValue = 50, HumidityValue = 0, EtOH1Value = 0, EtOH2Value = 0, EtOH3Value = 0, EtOH4Value = 0;
let SecondlyChartStatus = false;
let HourlyChartStatus = false;
let DailyChartStatus = false;
let hourlyAverages;
let dailyAverages;
let HourlyData;
let DailyData;
let timePickerInstance = null;
let isUpdatingUI = false;
let month, day, hour, year;
let modeChart = 0;
let currentChart = null; // Store chart instance

// Proxy for Status Tracking
const statusProxy = new Proxy({
  StatusOTA: false
}, {
  set(target, key, value) {
    if (target[key] !== value) {
      console.log(`${key} changed from ${target[key]} to ${value}`);
      target[key] = value;
      if (!isUpdatingUI) {
        updateUI();
      }

    }
    isUpdatingUI = false;
    return true;
  }
});

// DOM and UI
let DOM = null;

const initDOM = () => {
  DOM = {
    weatherContainer: document.getElementById('weather-container'),
    fotaBtn: document.querySelector('.fota-btn'),
    startSamplingBtn: document.getElementById('start-sampling-btn'),
    loadingIcon: document.getElementById("Icon_Loading"),
    fotaModal: document.getElementById('fota-modal'),
    fotaCloseBtn: document.getElementsByClassName('close')[0],
    versionList: document.getElementById('versionList'),
    HourlyChart: document.querySelector('.hourly-btn'),
    DailyChart: document.querySelector('.daily-btn'),
    SecondlyChart: document.querySelector('.second-btn'),
    timeChart: document.querySelector('.time-chart'),
    exportChartBtn: document.getElementById('export-chart-btn'),
    exportReportBtn: document.getElementById('export-report-btn'),
    fotaContent: document.querySelector('#fota-modal .modal-content'),
    // Upload dialog elements
    uploadForm: document.getElementById('firmware-upload-form'),
    uploadProgress: document.getElementById('upload-progress'),
    progressFill: document.querySelector('.progress-fill'),
    progressText: document.querySelector('.progress-text'),
    tabBtns: document.querySelectorAll('.tab-btn'),
    tabContents: document.querySelectorAll('.tab-content')
  };
  if (!DOM.fotaModal) console.warn('fotaModal not found (optional)');
  if (!DOM.fotaBtn) console.warn('fotaBtn not found (optional - removed from UI)');
  if (!DOM.fotaCloseBtn) console.warn('fotaCloseBtn not found (optional)');
  if (!DOM.versionList) console.error('versionlist not found');
  if (!DOM.startSamplingBtn) console.error('startSamplingBtn not found');

  console.log('DOM initialized:', DOM);
};

const updateUI = () => {
  if (!DOM) return;
  // Toggle trạng thái nút FOTA nếu tồn tại
  if (DOM.fotaBtn) {
    DOM.fotaBtn.classList.toggle('active', statusProxy.StatusOTA);
  }
  // Hiển thị icon loading nếu tồn tại
  if (DOM.loadingIcon && DOM.loadingIcon.style) {
    DOM.loadingIcon.style.display = "block";
  }
  
  // Mở/đóng modal nếu tồn tại
  if (DOM.fotaModal) {
    DOM.fotaModal.style.display = statusProxy.StatusOTA ? "block" : "none";
    
    // Reset về tab upload và ẩn progress khi đóng modal
    if (!statusProxy.StatusOTA) {
      if (typeof switchTab === 'function') switchTab('upload');
      if (DOM.uploadProgress && DOM.uploadProgress.style) {
        DOM.uploadProgress.style.display = 'none';
      }
    }
  }
};

// Upload Functions
const uploadFirmware = async (formData) => {
  try {
    DOM.uploadProgress.style.display = 'block';
    DOM.progressFill.style.width = '0%';
    DOM.progressText.textContent = '0%';

    const xhr = new XMLHttpRequest();
    
    xhr.upload.addEventListener('progress', (e) => {
      if (e.lengthComputable) {
        const percentComplete = (e.loaded / e.total) * 100;
        DOM.progressFill.style.width = percentComplete + '%';
        DOM.progressText.textContent = Math.round(percentComplete) + '%';
      }
    });

    xhr.addEventListener('load', () => {
      if (xhr.status === 200) {
        const response = JSON.parse(xhr.responseText);
        if (response.success) {
          DOM.progressFill.style.width = '100%';
          DOM.progressText.textContent = '100%';
          setTimeout(() => {
            DOM.uploadProgress.style.display = 'none';
            DOM.uploadForm.reset();
            // Refresh version list
            fetchFirmwareVersions();
            // Switch to versions tab
            switchTab('versions');
          }, 1000);
          console.log('✅ Firmware uploaded successfully:', response);
        } else {
          throw new Error(response.message);
        }
      } else {
        throw new Error(`HTTP ${xhr.status}: ${xhr.statusText}`);
      }
    });

    xhr.addEventListener('error', () => {
      throw new Error('Network error during upload');
    });

    xhr.open('POST', '/api/firmware/upload');
    xhr.send(formData);

  } catch (error) {
    console.error('❌ Upload error:', error);
    DOM.uploadProgress.style.display = 'none';
    alert('Lỗi khi tải lên firmware: ' + error.message);
  }
};

const switchTab = (tabName) => {
  // Remove active class from all tabs and contents
  DOM.tabBtns.forEach(btn => btn.classList.remove('active'));
  DOM.tabContents.forEach(content => content.classList.remove('active'));
  
  // Add active class to selected tab and content
  const activeBtn = document.querySelector(`[data-tab="${tabName}"]`);
  const activeContent = document.getElementById(`${tabName}-tab`);
  
  if (activeBtn) activeBtn.classList.add('active');
  if (activeContent) activeContent.classList.add('active');
  
  // If switching to versions tab, refresh the list
  if (tabName === 'versions') {
    fetchFirmwareVersions();
  }
};

const downloadFirmware = (version) => {
  const link = document.createElement('a');
  link.href = `/api/firmware/download/${encodeURIComponent(version)}`;
  link.download = `${version}.bin`;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  console.log(`📥 Downloading firmware: ${version}`);
};

// User Event Listeners
const initEventListeners = () => {
  function initTimePicker(mode = 'hourly') {
    if (timePickerInstance) {
      timePickerInstance.destroy();
    }
    const config = {
      appendTo: document.body,
      onChange: function (selectedDates, dateStr, instance) {
        if (!dateStr) return;
        if (mode === 'hourly') {
          const [y, m, d] = dateStr.split('-').map(Number);
          year = y; month = m; day = d;
          const hourlyData = generateChartDataFromHourlyAllMetrics();
          console.log('Hourly data:', hourlyData);
          modeChart = 1;
          renderChart(modeChart);
        }
        if (mode === 'daily') {
          const [y, m] = dateStr.split('-').map(Number);
          year = y; month = m;
          const dailyData = generateChartDataFromDailyAllMetrics();
          console.log('Daily data:', dailyData);
          modeChart = 2;
          renderChart(modeChart);
        }
      }
    };
    if (mode === 'hourly') {
      Object.assign(config, {
        enableTime: false,
        noCalendar: false,
        dateFormat: 'Y-m-d'
      });
    }
    if (mode === 'daily') {
      Object.assign(config, {
        plugins: [new monthSelectPlugin({ shorthand: true, dateFormat: "Y-m", altFormat: "F Y" })],
        dateFormat: 'Y-m'
      });
    }
    timePickerInstance = flatpickr("#Time-chart", config);
  }
  if (!DOM) return;
  console.log('Initializing event listeners...');
  
  // Start Sampling Button
  if (DOM.startSamplingBtn) {
    // ESP32 Configuration Modal
  const esp32ConfigModal = document.getElementById('esp32-config-modal');
  const esp32ConfigBtn = document.getElementById('config-esp32-btn');
  const closeEsp32Config = document.getElementById('close-esp32-config');
  const cancelEsp32Config = document.getElementById('cancel-esp32-config');
  const esp32ConfigForm = document.getElementById('esp32-config-form');
  const esp32ConfigStatus = document.getElementById('esp32-config-status');
  
  // Open ESP32 config page directly
  if (esp32ConfigBtn) {
    esp32ConfigBtn.addEventListener('click', async () => {
      try {
        // Get current ESP32 IP
        const response = await fetch('/api/esp32/config');
        
        // Check if response is OK
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        
        // Check content type
        const contentType = response.headers.get('content-type');
        if (!contentType || !contentType.includes('application/json')) {
          throw new Error('Response is not JSON');
        }
        
        const data = await response.json();
        
        if (data.success && data.esp32IP) {
          // Open ESP32 config page in new tab
          const esp32ConfigUrl = `http://${data.esp32IP}/config`;
          window.open(esp32ConfigUrl, '_blank');
          console.log(`✅ Opening ESP32 config page: ${esp32ConfigUrl}`);
        } else {
          // Show error message if ESP32 not connected
          alert(`❌ ${data.message || 'ESP32 not connected. Please ensure ESP32 has sent data at least once.'}`);
        }
      } catch (error) {
        alert(`❌ Error: ${error.message}. Make sure ESP32 has sent data to dashboard at least once.`);
        console.error('ESP32 config error:', error);
      }
    });
  }
  
  // Close modal
  if (closeEsp32Config) {
    closeEsp32Config.addEventListener('click', () => {
      esp32ConfigModal.style.display = 'none';
      esp32ConfigStatus.style.display = 'none';
    });
  }
  
  if (cancelEsp32Config) {
    cancelEsp32Config.addEventListener('click', () => {
      esp32ConfigModal.style.display = 'none';
      esp32ConfigStatus.style.display = 'none';
    });
  }
  
  // Close modal when clicking outside
  window.addEventListener('click', (event) => {
    if (event.target === esp32ConfigModal) {
      esp32ConfigModal.style.display = 'none';
      esp32ConfigStatus.style.display = 'none';
    }
  });
  
  // Handle form submission
  if (esp32ConfigForm) {
    esp32ConfigForm.addEventListener('submit', async (e) => {
      e.preventDefault();
      
      const host = document.getElementById('dashboard-host').value;
      const port = parseInt(document.getElementById('dashboard-port').value);
      
      if (!host || !port) {
        esp32ConfigStatus.className = 'error-msg';
        esp32ConfigStatus.textContent = '❌ Please fill in all fields';
        esp32ConfigStatus.style.display = 'block';
        return;
      }
      
      esp32ConfigStatus.className = 'info-msg';
      esp32ConfigStatus.textContent = '⏳ Updating ESP32 configuration...';
      esp32ConfigStatus.style.display = 'block';
      
      try {
        const response = await fetch('/api/esp32/config', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify({ host, port })
        });
        
        const data = await response.json();
        
        if (data.success) {
          esp32ConfigStatus.className = 'success-msg';
          esp32ConfigStatus.textContent = `✅ ${data.message || 'Configuration updated successfully!'}`;
          esp32ConfigStatus.style.display = 'block';
          
          // Close modal after 2 seconds
          setTimeout(() => {
            esp32ConfigModal.style.display = 'none';
            esp32ConfigStatus.style.display = 'none';
          }, 2000);
        } else {
          esp32ConfigStatus.className = 'error-msg';
          esp32ConfigStatus.textContent = `❌ ${data.message || 'Failed to update configuration'}`;
          esp32ConfigStatus.style.display = 'block';
        }
      } catch (error) {
        esp32ConfigStatus.className = 'error-msg';
        esp32ConfigStatus.textContent = '❌ Network error: ' + error.message;
        esp32ConfigStatus.style.display = 'block';
      }
    });
  }

  DOM.startSamplingBtn.addEventListener('click', async () => {
      await startSampling();
    });
  }
  
  // Tab switching
  DOM.tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      const tabName = btn.getAttribute('data-tab');
      switchTab(tabName);
    });
  });
  
  // Upload form submission
  if (DOM.uploadForm) {
    DOM.uploadForm.addEventListener('submit', (e) => {
      e.preventDefault();
      
      const formData = new FormData(DOM.uploadForm);
      const versionName = formData.get('versionName');
      const firmwareFile = formData.get('firmwareFile');
      
      if (!versionName.trim()) {
        alert('Vui lòng nhập tên phiên bản');
        return;
      }
      
      if (!firmwareFile || firmwareFile.size === 0) {
        alert('Vui lòng chọn file firmware');
        return;
      }
      
      if (!firmwareFile.name.endsWith('.bin')) {
        alert('Chỉ cho phép file .bin');
        return;
      }
      
      uploadFirmware(formData);
    });
  }
  
  // FOTA button (optional - removed from UI but keep code for compatibility)
  if (DOM.fotaBtn) {
    DOM.fotaBtn.addEventListener('click', () => {
      isUpdatingUI = true;
      statusProxy.StatusOTA = !statusProxy.StatusOTA;
      DOM.fotaBtn.classList.toggle('active', statusProxy.StatusOTA);
      if (DOM.fotaModal) {
        DOM.fotaModal.style.display = statusProxy.StatusOTA ? "block" : "none";
        if (statusProxy.StatusOTA) {
          // Mở modal ở tab upload khi click FOTA
          switchTab('upload');
          fetchFirmwareVersions();
        }
      } else {
        console.error('fotaModal is null in updateUI');
      }
    });
  }
  
  if (DOM.fotaCloseBtn) {
    DOM.fotaCloseBtn.addEventListener('click', () => {
      // if (isUploadingFirmware) {
      //   console.warn("🚫 Không thể đóng modal khi OTA đang diễn ra.");
      //   return;
      // }
      isUpdatingUI = true;
      statusProxy.StatusOTA = false;
      if (DOM.fotaBtn) DOM.fotaBtn.classList.remove('active');
      updateUI();
    });
  }
  
  window.addEventListener('click', (event) => {
    // Click ra ngoài vùng modal-content sẽ đóng modal
    if (DOM.fotaModal && DOM.fotaContent && event.target === DOM.fotaModal) {
      // if (isUploadingFirmware) {
      //   console.warn("🚫 Không thể đóng modal khi OTA đang diễn ra.");
      //   return;
      // }
      isUpdatingUI = true;
      statusProxy.StatusOTA = false;
      if (DOM.fotaBtn) DOM.fotaBtn.classList.remove('active');
      updateUI();
    }
  });
  const updateChartStatus = (selected) => {
    SecondlyChartStatus = selected === 'secondly';
    HourlyChartStatus = selected === 'hourly';
    DailyChartStatus = selected === 'daily';
    if (DOM.SecondlyChart) DOM.SecondlyChart.classList.toggle('active', SecondlyChartStatus);
    if (DOM.HourlyChart) DOM.HourlyChart.classList.toggle('active', HourlyChartStatus);
    if (DOM.DailyChart) DOM.DailyChart.classList.toggle('active', DailyChartStatus);
    if (DOM.timeChart) {
      if (HourlyChartStatus || DailyChartStatus) {
        DOM.timeChart.style.display = 'flex';
        initTimePicker(HourlyChartStatus ? 'hourly' : 'daily');
      } else {
        DOM.timeChart.style.display = 'none';
      }
    }
  };
  if (DOM.SecondlyChart) {
    DOM.SecondlyChart.addEventListener('click', () => {
      console.log("Secondly chart clicked");
      updateChartStatus('secondly');
      modeChart = 0;
      renderChart(modeChart);
    });
  } else {
    console.error('SecondlyChart not found in DOM');
  }
  if (DOM.HourlyChart) {
    DOM.HourlyChart.addEventListener('click', () => {
      console.log("Hourly chart clicked");
      updateChartStatus('hourly');
      // Đặt mặc định ngày hôm nay nếu chưa chọn ngày
      const now = new Date();
      year = now.getFullYear();
      month = now.getMonth() + 1;
      day = now.getDate();
      fetchRealTimeDataHourly();
      modeChart = 1;
      // Chờ dữ liệu về sẽ render trong onmessage; nhưng nếu đã có sẵn thì render ngay
      if (typeof hourlyAverages !== 'undefined') {
        renderChart(modeChart);
      }
    });
  } else {
    console.error('HourlyChart not found in DOM');
  }
  if (DOM.DailyChart) {
    DOM.DailyChart.addEventListener('click', () => {
      console.log("Daily chart clicked");
      updateChartStatus('daily');
      // Đặt mặc định tháng hiện tại nếu chưa chọn tháng
      const now = new Date();
      year = now.getFullYear();
      month = now.getMonth() + 1;
      fetchRealTimeDataDaily();
      modeChart = 2;
      if (typeof dailyAverages !== 'undefined') {
        renderChart(modeChart);
      }
    });
  } else {
    console.error('DailyChart not found in DOM');
  }
  
  // Export Chart Button
  if (DOM.exportChartBtn) {
    DOM.exportChartBtn.addEventListener('click', () => {
      exportChart();
    });
  } else {
    console.error('exportChartBtn not found in DOM');
  }
  
  // Export Report Button
  if (DOM.exportReportBtn) {
    DOM.exportReportBtn.addEventListener('click', () => {
      exportReport();
    });
  } else {
    console.error('exportReportBtn not found in DOM');
  }
  
  updateChartStatus('secondly');
};

// Sampling Control Functions
let isSampling = false;

const startSampling = async () => {
  if (isSampling) {
    console.log('⚠️ Sampling already in progress');
    return;
  }

  if (!DOM.startSamplingBtn) return;

  try {
    isSampling = true;
    DOM.startSamplingBtn.disabled = true;
    DOM.startSamplingBtn.classList.add('sampling');
    DOM.startSamplingBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Starting...';

    console.log('📡 Sending start sampling request to server...');

    const response = await fetch('/api/esp32/start-sampling', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      }
    });

    const result = await response.json();

    if (result.success) {
      console.log('✅ Sampling started successfully:', result);
      DOM.startSamplingBtn.innerHTML = '<i class="fas fa-check"></i> Sampling Started';
      DOM.startSamplingBtn.style.background = 'linear-gradient(45deg, #4CAF50, #45a049)';
      
      // Reset button after 3 seconds
      setTimeout(() => {
        resetSamplingButton();
      }, 3000);
    } else {
      throw new Error(result.message || 'Failed to start sampling');
    }
  } catch (error) {
    console.error('❌ Error starting sampling:', error);
    DOM.startSamplingBtn.innerHTML = '<i class="fas fa-exclamation-triangle"></i> Error';
    DOM.startSamplingBtn.style.background = 'linear-gradient(45deg, #F44336, #D32F2F)';
    
    // Show error message
    alert(`Lỗi khi bắt đầu đo: ${error.message}\n\nVui lòng kiểm tra:\n- ESP32 đã kết nối qua WebSocket\n- ESP32 đang chạy và sẵn sàng`);
    
    // Reset button after 3 seconds
    setTimeout(() => {
      resetSamplingButton();
    }, 3000);
  }
};

const resetSamplingButton = () => {
  if (!DOM.startSamplingBtn) return;
  isSampling = false;
  DOM.startSamplingBtn.disabled = false;
  DOM.startSamplingBtn.classList.remove('sampling');
  DOM.startSamplingBtn.innerHTML = '<i class="fas fa-play"></i> Start Sampling';
  DOM.startSamplingBtn.style.background = '';
};

// Export Functions
const exportChart = () => {
  exportCSV();
};

const exportReport = () => {
  exportCSV();
};

// Export CSV cho dữ liệu đang hiển thị
const exportCSV = () => {
  const now = new Date();
  const dateStr = now.toISOString().split('T')[0];
  const timeStr = now.toTimeString().split(' ')[0].replace(/:/g, '-');
  const modeName = modeChart === 0 ? 'Currently' : modeChart === 1 ? 'Hourly' : 'Daily';
  let modeSuffix = '';
  if (modeChart === 1 && year && month && day) {
    modeSuffix = `-${year}-${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
  } else if (modeChart === 2 && year && month) {
    modeSuffix = `-${year}-${String(month).padStart(2, '0')}`;
  }

  const rows = [];
  rows.push(['Time', 'Temperature', 'Humidity', 'EtOH1', 'EtOH2', 'EtOH3', 'EtOH4']);

  if (modeChart === 0) {
    // Live mode: 1 dòng hiện tại
    rows.push([
      now.toISOString(),
      TemperatureValue,
      HumidityValue,
      EtOH1Value,
      EtOH2Value,
      EtOH3Value,
      EtOH4Value,
    ]);
  } else if (modeChart === 1) {
    // Hourly: 24 điểm
    const hourly = generateAll24HourSeriesData();
    for (let i = 0; i < hourly.Temperature.length; i++) {
      rows.push([
        new Date(hourly.Temperature[i].x).toISOString(),
        hourly.Temperature[i].y ?? '',
        hourly.Humidity[i].y ?? '',
        hourly.EtOH1[i].y ?? '',
        hourly.EtOH2[i].y ?? '',
        hourly.EtOH3[i].y ?? '',
        hourly.EtOH4[i].y ?? '',
      ]);
    }
  } else {
    // Daily: 31 điểm
    const daily = generateAll31DaySeriesData();
    for (let i = 0; i < daily.Temperature.length; i++) {
      rows.push([
        new Date(daily.Temperature[i].x).toISOString(),
        daily.Temperature[i].y ?? '',
        daily.Humidity[i].y ?? '',
        daily.EtOH1[i].y ?? '',
        daily.EtOH2[i].y ?? '',
        daily.EtOH3[i].y ?? '',
        daily.EtOH4[i].y ?? '',
      ]);
    }
  }

  const csvContent = rows.map(r => r.join(',')).join('\n');
  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const link = document.createElement('a');
  const filename = `sensor-data-${modeName}${modeSuffix}-${dateStr}-${timeStr}.csv`;
  if (navigator.msSaveBlob) {
    navigator.msSaveBlob(blob, filename);
  } else {
    const url = URL.createObjectURL(blob);
    link.setAttribute('href', url);
    link.setAttribute('download', filename);
    link.style.visibility = 'hidden';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  }
  console.log('CSV exported:', filename);
};

const generateReportContent = (data) => {
  const now = new Date();
  const dateStr = now.toLocaleDateString('vi-VN');
  const timeStr = now.toLocaleTimeString('vi-VN');
  
  // Helper function to format number with 2 decimals
  const formatValue = (value) => {
    if (value === null || value === undefined || isNaN(value)) return 'N/A';
    return value.toFixed(2);
  };
  
  // Generate hourly data table if in Hourly mode
  let hourlyTableRows = '';
  if (data.mode === 'Hourly' && data.hourlyValues) {
    data.hourlyValues.forEach(hourData => {
      hourlyTableRows += `
        <tr>
          <td>${String(hourData.hour).padStart(2, '0')}:00</td>
          <td>${formatValue(hourData.temperature)}</td>
          <td>${formatValue(hourData.humidity)}</td>
          <td>${formatValue(hourData.etoh1)}</td>
          <td>${formatValue(hourData.etoh2)}</td>
          <td>${formatValue(hourData.etoh3)}</td>
          <td>${formatValue(hourData.etoh4)}</td>
        </tr>
      `;
    });
  }
  
  // Generate daily data table if in Daily mode
  let dailyTableRows = '';
  if (data.mode === 'Daily' && data.dailyValues) {
    data.dailyValues.forEach(dayData => {
      if (dayData.temperature !== null || dayData.humidity !== null) {
        dailyTableRows += `
          <tr>
            <td>Ngày ${dayData.day}</td>
          <td>${formatValue(dayData.temperature)}</td>
          <td>${formatValue(dayData.humidity)}</td>
          <td>${formatValue(dayData.etoh1)}</td>
            <td>${formatValue(dayData.etoh2)}</td>
            <td>${formatValue(dayData.etoh3)}</td>
            <td>${formatValue(dayData.etoh4)}</td>
          </tr>
        `;
      }
    });
  }
  
  // Chart image HTML
  const chartImageHtml = data.chartImage ? `
    <div class="chart-image" style="margin: 20px 0; text-align: center;">
      <h3>Biểu Đồ Dữ Liệu</h3>
      <img src="${data.chartImage}" alt="Chart" style="max-width: 100%; height: auto; border: 1px solid #ddd; padding: 10px; background: white;" />
    </div>
  ` : '<p style="color: #999;">Không thể xuất hình ảnh biểu đồ</p>';
  
  return `
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="UTF-8">
      <title>Sensor Data Report</title>
      <style>
        body { font-family: Arial, sans-serif; margin: 20px; line-height: 1.6; }
        .header { text-align: center; margin-bottom: 30px; border-bottom: 3px solid #4CAF50; padding-bottom: 20px; }
        .header h1 { color: #2c3e50; margin-bottom: 10px; }
        .data-table { width: 100%; border-collapse: collapse; margin: 20px 0; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .data-table th, .data-table td { border: 1px solid #ddd; padding: 12px; text-align: left; }
        .data-table th { background-color: #4CAF50; color: white; font-weight: bold; }
        .data-table tr:nth-child(even) { background-color: #f9f9f9; }
        .data-table tr:hover { background-color: #f5f5f5; }
        .summary { background-color: #e8f5e9; padding: 20px; border-radius: 5px; margin: 20px 0; border-left: 4px solid #4CAF50; }
        .chart-info { margin: 20px 0; padding: 15px; background-color: #e3f2fd; border-radius: 5px; }
        .chart-image { page-break-inside: avoid; }
        .footer { margin-top: 30px; text-align: center; color: #666; padding-top: 20px; border-top: 1px solid #ddd; }
      </style>
    </head>
    <body>
      <div class="header">
        <h1>Báo Cáo Dữ Liệu Cảm Biến</h1>
        <p><strong>Ngày xuất báo cáo:</strong> ${dateStr} - ${timeStr}</p>
        <p><strong>Chế độ hiển thị:</strong> ${data.chartMode} - ${data.timeInfo || ''}</p>
      </div>
      
      <div class="summary">
        <h2>Tóm Tắt Dữ Liệu</h2>
        <p><strong>Thời gian xuất báo cáo:</strong> ${new Date(data.timestamp).toLocaleString('vi-VN')}</p>
        ${data.timeInfo ? `<p><strong>Khoảng thời gian:</strong> ${data.timeInfo}</p>` : ''}
        <table class="data-table" style="margin-top: 15px;">
          <thead>
            <tr>
              <th>Thông Số</th>
              <th>Giá Trị Trung Bình</th>
              <th>Đơn Vị</th>
              <th>Trạng Thái</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>Nhiệt Độ</td>
              <td>${formatValue(data.temperature)}</td>
              <td>°C</td>
              <td>${data.temperature > 30 ? 'Cao' : data.temperature < 10 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
            <tr>
              <td>Độ Ẩm</td>
              <td>${formatValue(data.humidity)}</td>
              <td>%</td>
              <td>${data.humidity > 80 ? 'Cao' : data.humidity < 30 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
            <tr>
              <td>Áp Suất</td>
              <td>${formatValue(data.pressure)}</td>
              <td>hPa</td>
              <td>${data.pressure > 1020 ? 'Cao' : data.pressure < 980 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
            <tr>
              <td>EtOH1</td>
              <td>${formatValue(data.etoh1)}</td>
              <td>Raw</td>
              <td>${data.etoh1 > 20000 ? 'Cao' : data.etoh1 < 5000 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
            <tr>
              <td>EtOH2</td>
              <td>${formatValue(data.etoh2)}</td>
              <td>Raw</td>
              <td>${data.etoh2 > 20000 ? 'Cao' : data.etoh2 < 5000 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
            <tr>
              <td>EtOH3</td>
              <td>${formatValue(data.etoh3)}</td>
              <td>Raw</td>
              <td>${data.etoh3 > 20000 ? 'Cao' : data.etoh3 < 5000 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
            <tr>
              <td>EtOH4</td>
              <td>${formatValue(data.etoh4)}</td>
              <td>Raw</td>
              <td>${data.etoh4 > 20000 ? 'Cao' : data.etoh4 < 5000 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
          </tbody>
        </table>
      </div>
      
      ${chartImageHtml}
      
      ${data.mode === 'Hourly' && hourlyTableRows ? `
        <div class="chart-info">
          <h3>Dữ Liệu Theo Giờ - Ngày ${data.day}/${data.month}/${data.year}</h3>
          <table class="data-table">
            <thead>
              <tr>
                <th>Giờ</th>
                <th>Nhiệt Độ (°C)</th>
                <th>Độ Ẩm (%)</th>
                <th>Áp Suất (hPa)</th>
                <th>EtOH1 (Raw)</th>
                <th>EtOH2 (Raw)</th>
                <th>EtOH3 (Raw)</th>
                <th>EtOH4 (Raw)</th>
              </tr>
            </thead>
            <tbody>
              ${hourlyTableRows}
            </tbody>
          </table>
        </div>
      ` : ''}
      
      ${data.mode === 'Daily' && dailyTableRows ? `
        <div class="chart-info">
          <h3>Dữ Liệu Theo Ngày - Tháng ${data.month}/${data.year}</h3>
          <table class="data-table">
            <thead>
              <tr>
                <th>Ngày</th>
                <th>Nhiệt Độ (°C)</th>
                <th>Độ Ẩm (%)</th>
                <th>Áp Suất (hPa)</th>
                <th>EtOH1 (Raw)</th>
                <th>EtOH2 (Raw)</th>
                <th>EtOH3 (Raw)</th>
                <th>EtOH4 (Raw)</th>
              </tr>
            </thead>
            <tbody>
              ${dailyTableRows}
            </tbody>
          </table>
        </div>
      ` : ''}
      
      <div class="chart-info">
        <h3>Thông Tin Biểu Đồ</h3>
        <p><strong>Chế độ:</strong> ${data.chartMode}</p>
        <p><strong>Khoảng thời gian:</strong> ${data.timeInfo || 'Dữ liệu thời gian thực'}</p>
      </div>
      
      <div class="footer">
        <p>Báo cáo được tạo tự động bởi hệ thống giám sát cảm biến môi trường</p>
        <p style="font-size: 12px; color: #999;">© ${new Date().getFullYear()} - Environment Monitor System</p>
      </div>
    </body>
    </html>
  `;
};

const downloadReport = (content, data) => {
  // Create blob with HTML content
  const blob = new Blob([content], { type: 'text/html' });
  const url = URL.createObjectURL(blob);
  
  // Generate filename based on mode
  const now = new Date();
  const dateStr = now.toISOString().split('T')[0];
  const timeStr = now.toTimeString().split(' ')[0].replace(/:/g, '-');
  
  let filename = `sensor-report-${data.chartMode}-`;
  if (data.mode === 'Hourly' && data.year && data.month && data.day) {
    filename += `${data.year}-${String(data.month).padStart(2, '0')}-${String(data.day).padStart(2, '0')}-`;
  } else if (data.mode === 'Daily' && data.year && data.month) {
    filename += `${data.year}-${String(data.month).padStart(2, '0')}-`;
  }
  filename += `${dateStr}-${timeStr}.html`;
  
  // Create download link
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  
  // Trigger download
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  
  // Clean up
  URL.revokeObjectURL(url);
};

// WebSocket Communication

const fetchFirmwareVersions = () => {
  if (socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ type: 'firmware-versions' }));
    console.log("📡 Sent firmware-versions request via WebSocket");
  } else {
    console.warn("⚠️ WebSocket not open, retrying in 500ms...");
    setTimeout(fetchFirmwareVersions, 500);
  }
};


const sendUploadEventToServer = (version) => {
  if (socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ type: 'ota', version }));
    console.log("📡 Sent OTA request via WebSocket: version =", version);
  } else {
    console.warn("⚠️ WebSocket not open, retrying in 500ms...");
    setTimeout(() => sendUploadEventToServer(version), 500);
  }
};



const fetchRealTimeDataHourly = () => {
  if (socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ type: 'get-real-time-data-hourly' }));
    console.log("📡 Sent get-real-time-data-hourly via WebSocket");
  }
};

const fetchRealTimeDataDaily = () => {
  if (socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ type: 'get-real-time-data-daily' }));
    console.log("📡 Sent get-real-time-data-daily via WebSocket");
  }
};

// Chart and Data Processing
let progressBars = null;
const initProgressBars = () => {
  if (!DOM) return;
  
  // Kiểm tra xem ProgressBar library đã load chưa
  if (typeof ProgressBar === 'undefined') {
    console.warn('⚠️ ProgressBar library not loaded. Retrying in 500ms...');
    setTimeout(initProgressBars, 500);
    return;
  }
  
  // Config cho Humidity (%)
  const humidityConfig = {
    strokeWidth: 12,
    color: 'white',
    trailColor: 'rgba(255,255,255, 0.4)',
    trailWidth: 12,
    easing: 'easeInOut',
    duration: 1400,
    svgStyle: { width: '100%', height: '100%' },
    step: (state, bar) => {
      bar.path.setAttribute('stroke', state.color);
      const value = Math.round(bar.value() * 100);
      bar.setText(value + '%' || '0%');
      bar.text.style.color = state.color;
    }
  };

  // Config cho Temperature (°C) - max 100°C
  const tempConfig = {
    strokeWidth: 12,
    color: 'white',
    trailColor: 'rgba(255,255,255, 0.4)',
    trailWidth: 12,
    easing: 'easeInOut',
    duration: 1400,
    svgStyle: { width: '100%', height: '100%' },
    step: (state, bar) => {
      bar.path.setAttribute('stroke', state.color);
      const value = Math.round(bar.value() * 100); // bar.value() là tỷ lệ 0-1, nhân 100 để ra °C
      bar.setText(value + '°C' || '0°C');
      bar.text.style.color = state.color;
    }
  };

  try {
    progressBars = {
      temp: new ProgressBar.SemiCircle('#container_temperature', { ...tempConfig, text: { value: '', alignToBottom: false, className: 'progressbar_label' } }),
      humidity: new ProgressBar.Line('#container_humidity', { ...humidityConfig, text: { value: '', className: 'humidity_label' } }),
    };
    // Temperature: giá trị trực tiếp là °C, chia cho 100 để normalize (max 100°C)
    progressBars.temp.animate(TemperatureValue / 100);
    // Humidity: giá trị trực tiếp là %, chia cho 100 để normalize
    progressBars.humidity.animate(HumidityValue / 100);
    console.log('✅ Progress bars initialized successfully');
  } catch (error) {
    console.error('❌ Error initializing progress bars:', error);
  }
};

const initHighcharts = () => {
  if (!DOM) return;
  
  console.log('Initializing Highcharts...');
  
  // Check if Highcharts is loaded
  if (typeof Highcharts === 'undefined') {
    console.error('Highcharts library not loaded!');
    return;
  }
  console.log('Highcharts library loaded successfully');
  
  const chartContainer = document.getElementById('chart-container');
  if (!chartContainer) {
    console.error('Chart container not found!');
    return;
  }
  console.log('Chart container found:', chartContainer);
  
  const now = new Date();
  const startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
  const endOfToday = startOfToday + 24 * 3600 * 1000;
  let chartInterval = null;
  const generateAll24HourSeriesData = () => {
    const result = {
      Temperature: [],
      Humidity: [],
      EtOH1: [],
      EtOH2: [],
      EtOH3: [],
      EtOH4: []
    };
    const hourlyData = generateChartDataFromHourlyAllMetrics();
    for (let i = 0; i < 24; i++) {
      const x = startOfToday + i * 3600 * 1000;
      result.Temperature.push({ x, y: hourlyData.Temperature[i] ?? null });
      result.Humidity.push({ x, y: hourlyData.Humidity[i] ?? null });
      result.EtOH1.push({ x, y: hourlyData.EtOH1[i] ?? null });
      result.EtOH2.push({ x, y: hourlyData.EtOH2[i] ?? null });
      result.EtOH3.push({ x, y: hourlyData.EtOH3[i] ?? null });
      result.EtOH4.push({ x, y: hourlyData.EtOH4[i] ?? null });
    }
    return result;
  };
  const generateAll31DaySeriesData = () => {
    const result = {
      Temperature: [],
      Humidity: [],
      EtOH1: [],
      EtOH2: [],
      EtOH3: [],
      EtOH4: []
    };
    const dailyData = generateChartDataFromDailyAllMetrics();
    const startDate = new Date(now.getFullYear(), now.getMonth(), 1).getTime();
    for (let i = 0; i < 31; i++) {
      const x = startDate + i * 24 * 3600 * 1000;
      result.Temperature.push({ x, y: dailyData.Temperature[i] ?? null });
      result.Humidity.push({ x, y: dailyData.Humidity[i] ?? null });
      result.EtOH1.push({ x, y: dailyData.EtOH1[i] ?? null });
      result.EtOH2.push({ x, y: dailyData.EtOH2[i] ?? null });
      result.EtOH3.push({ x, y: dailyData.EtOH3[i] ?? null });
      result.EtOH4.push({ x, y: dailyData.EtOH4[i] ?? null });
    }
    return result;
  };
  const generateLiveInitData = () => {
    const now = new Date().getTime();
    const result = {
      Temperature: [],
      Humidity: [],
      EtOH1: [],
      EtOH2: [],
      EtOH3: [],
      EtOH4: []
    };
    for (let i = -19; i <= 0; i++) {
      const x = now + i * 1000;
      result.Temperature.push({ x, y: TemperatureValue });
      result.Humidity.push({ x, y: HumidityValue });
      result.EtOH1.push({ x, y: EtOH1Value });
      result.EtOH2.push({ x, y: EtOH2Value });
      result.EtOH3.push({ x, y: EtOH3Value });
      result.EtOH4.push({ x, y: EtOH4Value });
    }
    return result;
  };
  const addConductivityEffect = (series, point) => {
    if (!series.pulse)
      series.pulse = series.chart.renderer.circle().attr({ r: 5, opacity: 0 }).add(series.markerGroup);
    setTimeout(() => {
      series.pulse
        .attr({
          x: series.xAxis.toPixels(point.x, true),
          y: series.yAxis.toPixels(point.y, true),
          r: 5,
          opacity: 1,
          fill: series.color
        })
        .animate({ r: 20, opacity: 0 }, { duration: 1000 });
    }, 1);
  };
  const onChartLoad = function () {
    const chart = this;
    if (chartInterval) clearInterval(chartInterval);
    chartInterval = setInterval(() => {
      const x = new Date().getTime();
      const newDataPoints = [
        { seriesIndex: 0, y: TemperatureValue },
        { seriesIndex: 1, y: HumidityValue },
        { seriesIndex: 2, y: EtOH1Value },
        { seriesIndex: 3, y: EtOH2Value },
        { seriesIndex: 4, y: EtOH3Value },
        { seriesIndex: 5, y: EtOH4Value },
      ];
      newDataPoints.forEach(dataPoint => {
        const seriesTarget = chart.series[dataPoint.seriesIndex];
        seriesTarget.addPoint([x, dataPoint.y], true, true);
        addConductivityEffect(seriesTarget, { x, y: dataPoint.y });
      });
    }, 1000);
  };
  Highcharts.addEvent(Highcharts.Series, 'addPoint', e => {
    const point = e.point, series = e.target;
    if (!series.pulse) series.pulse = series.chart.renderer.circle().add(series.markerGroup);
    setTimeout(() => {
      series.pulse
        .attr({
          x: series.xAxis.toPixels(point.x, true),
          y: series.yAxis.toPixels(point.y, true),
          r: series.options.marker.radius,
          opacity: 1,
          fill: series.color
        })
        .animate({ r: 20, opacity: 0 }, { duration: 1000 });
    }, 1);
  });
  window.renderChart = function (mode = 1) {
    console.log('Rendering chart with mode:', mode);
    
    if (chartInterval) {
      clearInterval(chartInterval);
      chartInterval = null;
    }
    let isLive = (mode === 0);
    let is24h = (mode === 1);
    let is31d = (mode === 2);
    let xAxisOptions = {
      type: 'datetime',
      labels: {
        style: { color: '#FFFFFF' },
        format: is31d ? '{value:%d}' : '{value:%H:%M}'
      },
      lineColor: '#FFFFFF',
      tickColor: '#FFFFFF'
    };
    let seriesDataFunc = generateLiveInitData();
    let title = '';
    if (isLive) {
      xAxisOptions = Object.assign(xAxisOptions, {});
      seriesDataFunc = generateLiveInitData();
      title = '';
    } else if (is31d) {
      const startDate = new Date(now.getFullYear(), now.getMonth(), 1).getTime();
      const endDate = new Date(now.getFullYear(), now.getMonth(), 31).getTime();
      xAxisOptions = Object.assign(xAxisOptions, {
        min: startDate,
        max: endDate,
        tickInterval: 24 * 3600 * 1000
      });
      seriesDataFunc = generateAll31DaySeriesData();
      title = '';
    } else {
      xAxisOptions = Object.assign(xAxisOptions, {
        min: startOfToday,
        max: endOfToday,
        tickInterval: 3600 * 1000,
        maxPadding: 0.1
      });
      seriesDataFunc = generateAll24HourSeriesData();
      title = '';
    }
    console.log('Creating Highcharts chart...');
    // Store chart instance
    currentChart = Highcharts.chart('chart-container', {
      chart: {
        type: 'spline',
        events: isLive ? { load: onChartLoad } : undefined
      },
      time: { useUTC: false },
      title: { text: title, style: { color: '#FFFFFF' } },
      xAxis: xAxisOptions,
      yAxis: {
        title: { text: 'Value', style: { color: '#FFFFFF' } },
        lineColor: '#FFFFFF',
        tickColor: '#FFFFFF',
        labels: { style: { color: '#FFFFFF' } },
        plotLines: [{ value: 0, width: 1, color: '#FFFFFF' }]
      },
      tooltip: {
        headerFormat: '<b>{series.name}</b><br/>',
        pointFormat: '{point.x:%Y-%m-%d}<br/>{point.y:.2f}'
      },
      legend: { enabled: true },
      exporting: { 
        enabled: true,
        buttons: {
          contextButton: {
            enabled: false // Hide default export button, we use custom button
          }
        }
      },
      series: [
        { name: 'Temperature (°C)', lineWidth: 2, color: 'red', data: seriesDataFunc.Temperature },
        { name: 'Humidity (%)', lineWidth: 2, color: 'blue', data: seriesDataFunc.Humidity, visible: false },
        { name: 'EtOH1 (Raw)', lineWidth: 2, color: 'yellow', data: seriesDataFunc.EtOH1, visible: false },
        { name: 'EtOH2 (Raw)', lineWidth: 2, color: 'purple', data: seriesDataFunc.EtOH2, visible: false },
        { name: 'EtOH3 (Raw)', lineWidth: 2, color: 'orange', data: seriesDataFunc.EtOH3, visible: false },
        { name: 'EtOH4 (Raw)', lineWidth: 2, color: 'cyan', data: seriesDataFunc.EtOH4, visible: false }
      ]
    });
    console.log('Chart created successfully!');
  };
  renderChart(0); // Start with live data mode
};

function getMonthlyDailyHourlyAverages(dataArray) {
  if (!Array.isArray(dataArray)) {
    console.error("❌ Dữ liệu không phải mảng.");
    return [];
  }
  const monthlyDailyHourlyData = Array.from({ length: 13 }, () =>
    Array.from({ length: 31 }, () =>
      Array.from({ length: 24 }, () => ({
        Temperature: [],
        Humidity: [],
        EtOH1: [],
        EtOH2: [],
        EtOH3: [],
        EtOH4: []
      }))
    )
  );
  dataArray.forEach(entry => {
    const date = new Date(entry.Time);
    const month = date.getMonth() + 1;
    const day = date.getDate();
    const hour = date.getHours();
    if (month >= 1 && month <= 12 && day >= 1 && day <= 30 && hour >= 0 && hour < 24) {
      monthlyDailyHourlyData[month][day][hour].Temperature.push(entry.Temperature);
      monthlyDailyHourlyData[month][day][hour].Humidity.push(entry.Humidity);
      monthlyDailyHourlyData[month][day][hour].EtOH1.push(entry.EtOH1);
      monthlyDailyHourlyData[month][day][hour].EtOH2.push(entry.EtOH2);
      monthlyDailyHourlyData[month][day][hour].EtOH3.push(entry.EtOH3);
      monthlyDailyHourlyData[month][day][hour].EtOH4.push(entry.EtOH4);
    }
  });
  const average = arr => arr.length ? arr.reduce((a, b) => a + b, 0) / arr.length : null;
  return monthlyDailyHourlyData.map((monthData, m) => {
    if (m === 0) return null;
    return monthData.map((dayData, d) => {
      if (d === 0) return null;
      return dayData.map((hourData, h) => ({
        month: m,
        day: d,
        hour: h,
        Temperature: average(hourData.Temperature),
        Humidity: average(hourData.Humidity),
        EtOH1: average(hourData.EtOH1),
        EtOH2: average(hourData.EtOH2),
        EtOH3: average(hourData.EtOH3),
        EtOH4: average(hourData.EtOH4)
      }));
    });
  });
}

function getMonthlyDailyAverages(dataArray) {
  if (!Array.isArray(dataArray)) {
    console.error("❌ Dữ liệu không phải mảng.");
    return [];
  }
  const monthlyDailyData = Array.from({ length: 13 }, () =>
    Array.from({ length: 31 }, () => ({
      Temperature: [],
      Humidity: [],
      EtOH1: [],
      EtOH2: [],
      EtOH3: [],
      EtOH4: []
    }))
  );
  dataArray.forEach(entry => {
    const date = new Date(entry.Time);
    const month = date.getMonth() + 1;
    const day = date.getDate();
    if (month >= 1 && month <= 12 && day >= 1 && day <= 30) {
      monthlyDailyData[month][day].Temperature.push(entry.Temperature);
      monthlyDailyData[month][day].Humidity.push(entry.Humidity);
      monthlyDailyData[month][day].EtOH1.push(entry.EtOH1);
      monthlyDailyData[month][day].EtOH2.push(entry.EtOH2);
      monthlyDailyData[month][day].EtOH3.push(entry.EtOH3);
      monthlyDailyData[month][day].EtOH4.push(entry.EtOH4);
    }
  });
  const average = arr => arr.length ? arr.reduce((a, b) => a + b, 0) / arr.length : null;
  return monthlyDailyData.map((monthData, monthIdx) => {
    if (monthIdx === 0) return null;
    return monthData.map((dayData, dayIdx) => {
      if (dayIdx === 0) return null;
      return {
        month: monthIdx,
        day: dayIdx,
        Temperature: average(dayData.Temperature),
        Humidity: average(dayData.Humidity),
        EtOH1: average(dayData.EtOH1),
        EtOH2: average(dayData.EtOH2),
        EtOH3: average(dayData.EtOH3),
        EtOH4: average(dayData.EtOH4)
      };
    });
  });
}

function getHourlyValue(month, day, hour, key) {
  if (!hourlyAverages?.[month]?.[day]?.[hour]) {
    console.warn(`❌ Không có dữ liệu cho ${day}/${month} giờ ${hour}`);
    return null;
  }
  return hourlyAverages[month][day][hour][key] ?? null;
}

function getDailyValue(month, day, key) {
  if (!dailyAverages?.[month]?.[day]) {
    console.warn(`❌ Không có dữ liệu cho ${day}/${month}`);
    return null;
  }
  return dailyAverages[month][day][key] ?? null;
}

function generateChartDataFromHourlyAllMetrics() {
  const result = {
    Temperature: [],
    Humidity: [],
    EtOH1: [],
    EtOH2: [],
    EtOH3: [],
    EtOH4: []
  };
  if (!year || !month || !day) return result;
  for (let h = 0; h < 24; h++) {
    ['Temperature', 'Humidity', 'EtOH1', 'EtOH2', 'EtOH3', 'EtOH4'].forEach(key => {
      const value = getHourlyValue(month, day, h, key);
      result[key].push(value);
    });
  }
  return result;
}

function generateChartDataFromDailyAllMetrics() {
  const result = {
    Temperature: [],
    Humidity: [],
    EtOH1: [],
    EtOH2: [],
    EtOH3: [],
    EtOH4: []
  };
  if (!year || !month) return result;
  for (let d = 1; d <= 31; d++) {
    ['Temperature', 'Humidity', 'EtOH1', 'EtOH2', 'EtOH3', 'EtOH4'].forEach(key => {
      const value = getDailyValue(month, d, key);
      result[key].push(value);
    });
  }
  return result;
}

// Utility Functions
const fetchWeather = async (location) => {
  if (!DOM || !DOM.weatherContainer) return;
  try {
    const response = await fetch(`https://api.weatherapi.com/v1/current.json?key=${API_KEY}&q=${location}&aqi=yes`);
    if (!response.ok) throw new Error(`${response.status}`);
    const data = await response.json();
    DOM.weatherContainer.querySelector('#location').textContent = `Weather in ${data.location.name}, ${data.location.country}: `;
    DOM.weatherContainer.querySelector('#temperature').textContent = `${data.current.temp_c}°C`;
    DOM.weatherContainer.querySelector('#wind').textContent = `${data.current.wind_kph} km/h`;
  } catch (error) {
    console.error('Error fetching weather data:', error.message);
  }
};

const initFullscreen = () => {
  const fullscreenButton = document.createElement("button");
  fullscreenButton.innerHTML = '<i class="fas fa-expand"></i>';
  fullscreenButton.className = "fullscreen-button";
  document.body.appendChild(fullscreenButton);
  let isFullscreen = false;
  function toggleFullscreen() {
    if (!isFullscreen) {
      if (document.documentElement.requestFullscreen) document.documentElement.requestFullscreen();
      else if (document.documentElement.mozRequestFullScreen) document.documentElement.mozRequestFullScreen();
      else if (document.documentElement.webkitRequestFullscreen) document.documentElement.webkitRequestFullscreen();
      else if (document.documentElement.msRequestFullscreen) document.documentElement.msRequestFullscreen();
      fullscreenButton.innerHTML = '<i class="fas fa-compress"></i>';
    } else {
      if (document.exitFullscreen) document.exitFullscreen();
      else if (document.mozCancelFullScreen) document.mozCancelFullScreen();
      else if (document.webkitExitFullscreen) document.webkitExitFullscreen();
      else if (document.msExitFullscreen) document.msExitFullscreen();
      fullscreenButton.innerHTML = '<i class="fas fa-expand"></i>';
    }
    isFullscreen = !isFullscreen;
  }
  fullscreenButton.addEventListener("click", toggleFullscreen);
  document.addEventListener("fullscreenchange", () => { isFullscreen = !!document.fullscreenElement; fullscreenButton.innerHTML = isFullscreen ? '<i class="fas fa-compress"></i>' : '<i class="fas fa-expand"></i>'; });
  document.addEventListener("webkitfullscreenchange", () => { isFullscreen = !!document.webkitFullscreenElement; fullscreenButton.innerHTML = isFullscreen ? '<i class="fas fa-compress"></i>' : '<i class="fas fa-expand"></i>'; });
  document.addEventListener("mozfullscreenchange", () => { isFullscreen = !!document.mozFullScreenElement; fullscreenButton.innerHTML = isFullscreen ? '<i class="fas fa-compress"></i>' : '<i class="fas fa-expand"></i>'; });
  document.addEventListener("MSFullscreenChange", () => { isFullscreen = !!document.msFullscreenElement; fullscreenButton.innerHTML = isFullscreen ? '<i class="fas fa-compress"></i>' : '<i class="fas fa-expand"></i>'; });
};

// Application Initialization
const init = () => {
  initDOM();
  if (!DOM) {
    console.error('DOM initialization failed');
    return;
  }
  initEventListeners();
  initProgressBars();
  
  // Delay chart initialization to ensure DOM and scripts are ready
  setTimeout(() => {
    initHighcharts();
  }, 500);
  
  fetchWeather(LOCATION);
};

// WebSocket Message Handling
socket.onmessage = (event) => {
  try {
    const data = JSON.parse(event.data);
    console.log("📦 Received WebSocket message:", data);
    switch (data.type) {
      case 'status-all':
        const status = data.data || {};
        // Chỉ cập nhật StatusOTA nếu backend gửi trường OTA, tránh auto-đóng modal
        if (Object.prototype.hasOwnProperty.call(status, 'OTA')) {
          statusProxy.StatusOTA = !!status.OTA;
        }
        if ('Temperature' in status) TemperatureValue = parseFloat(status.Temperature);
        if ('Humidity' in status) HumidityValue = parseFloat(status.Humidity);
        if ('EtOH1' in status) EtOH1Value = parseFloat(status.EtOH1);
        if ('EtOH2' in status) EtOH2Value = parseFloat(status.EtOH2);
        if ('EtOH3' in status) EtOH3Value = parseFloat(status.EtOH3);
        if ('EtOH4' in status) EtOH4Value = parseFloat(status.EtOH4);
        if (progressBars) {
          // Temperature: giá trị trực tiếp là °C, chia cho 100 để normalize (max 100°C)
          progressBars.temp.animate(TemperatureValue / 100);
          // Humidity: giá trị trực tiếp là %, chia cho 100 để normalize
          progressBars.humidity.animate(HumidityValue / 100);
        }
        updateUI();
        console.log('✅ Successfully applied data from server');
        break;
      case 'get-timers':
        if (data.success && DOM.TimerList) {
          const tbody = document.getElementById('TimerTableBody');
          if (tbody) {
            tbody.innerHTML = '';
            if (!data.timers || data.timers.length === 0) {
              const tr = document.createElement('tr');
              tr.innerHTML = `<td colspan="4">Không có timer nào.</td>`;
              tbody.appendChild(tr);
            } else {
              data.timers.forEach(timer => {
                const tr = document.createElement('tr');
                tr.innerHTML = `
                  <td>${timer.Device || 'Unknown'}</td>
                  <td>${timer.TimerStart || '00:00:00'}</td>
                  <td>${timer.TimerEnd || '00:00:00'}</td>
                  <td>
                    <button style="
                      background-color: #e74c3c;
                      color: white;
                      border: none;
                      padding: 5px 10px;
                      cursor: pointer;
                      border-radius: 3px;
                    ">Xóa</button>
                  </td>
                `;
                const deleteBtn = tr.querySelector('button');
                deleteBtn.addEventListener('click', () => {
                  if (confirm(`Xóa timer cho thiết bị "${timer.Device}"?`)) {
                    deleteTimer(timer);
                  }
                });
                tbody.appendChild(tr);
              });
            }
          } else {
            console.error('Timer table body not found');
          }
        } else {
          console.error('Failed to fetch timers:', data.message);
          const tbody = document.getElementById('TimerTableBody');
          if (tbody) {
            const tr = document.createElement('tr');
            tr.innerHTML = `<td colspan="4">Lỗi khi tải danh sách timer.</td>`;
            tbody.appendChild(tr);
          }
        }
        break;
      case 'firmware-versions':
        if (data.success && DOM.versionList) {
          DOM.versionList.innerHTML = '';
          if (data.versions.length === 0) {
            DOM.versionList.innerHTML = '<p>Không có phiên bản firmware nào.</p>';
          } else {
            const ul = document.createElement('ul');
            data.versions.forEach(firmware => {
              const li = document.createElement('li');
              
              // Version info
              const versionInfo = document.createElement('div');
              versionInfo.className = 'version-info';
              
              const versionName = document.createElement('div');
              versionName.className = 'version-name';
              versionName.textContent = firmware.version;
              
              const versionMeta = document.createElement('div');
              versionMeta.className = 'version-meta';
              const uploadDate = new Date(firmware.uploadDate).toLocaleDateString('vi-VN');
              const fileSize = (firmware.fileSize / 1024).toFixed(1);
              versionMeta.textContent = `${uploadDate} • ${fileSize} KB`;
              
              versionInfo.appendChild(versionName);
              versionInfo.appendChild(versionMeta);
              li.appendChild(versionInfo);
              
              // Action buttons
              const versionActions = document.createElement('div');
              versionActions.className = 'version-actions';
              
              const downloadButton = document.createElement('button');
              downloadButton.className = 'btn-download';
              downloadButton.innerHTML = '<i class="fas fa-download"></i> Download';
              downloadButton.addEventListener('click', () => {
                downloadFirmware(firmware.version);
              });

              const deleteButton = document.createElement('button');
              deleteButton.className = 'btn-upload';
              deleteButton.style.backgroundColor = '#e74c3c';
              deleteButton.innerHTML = '<i class="fas fa-trash"></i> Delete';
              deleteButton.addEventListener('click', async () => {
                if (!confirm(`Xóa firmware version "${firmware.version}"?`)) return;
                try {
                  const res = await fetch(`/api/firmware/${encodeURIComponent(firmware.version)}`, {
                    method: 'DELETE',
                    headers: { 'Accept': 'application/json' }
                  });
                  const contentType = res.headers.get('content-type') || '';
                  let payload;
                  if (contentType.includes('application/json')) {
                    payload = await res.json();
                  } else {
                    payload = await res.text();
                  }
                  if (!res.ok) {
                    const message = typeof payload === 'string' ? payload : (payload?.message || 'Xóa thất bại');
                    alert(message);
                    return;
                  }
                  const success = typeof payload === 'object' ? payload.success !== false : true;
                  if (success) {
                    fetchFirmwareVersions();
                  } else {
                    const message = typeof payload === 'object' ? (payload.message || 'Xóa thất bại') : 'Xóa thất bại';
                    alert(message);
                  }
                } catch (err) {
                  console.error('Delete firmware error:', err);
                  alert('Lỗi khi xóa firmware');
                }
              });
              
              const uploadButton = document.createElement('button');
              uploadButton.className = 'btn-upload';
              uploadButton.innerHTML = '<i class="fas fa-upload"></i> OTA';
              uploadButton.addEventListener('click', () => {
                isUploadingFirmware = true;
                console.log(`OTA clicked for Version ${firmware.version}`);
                sendUploadEventToServer(firmware.version);
                const uploadStatus = document.getElementById('upload-status');
                if (uploadStatus) {
                  uploadStatus.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Đang tiến hành OTA...';
                  uploadStatus.style.color = 'orange';
                }
                
                // Sau 10 giây tự động đóng modal FOTA
                setTimeout(() => {
                  isUpdatingUI = true;
                  statusProxy.StatusOTA = false;
                  if (DOM.fotaBtn) {
                    DOM.fotaBtn.classList.remove('active');
                  }
                  if (DOM.fotaModal) {
                    DOM.fotaModal.style.display = 'none';
                  }
                  // Reset dòng chữ "Đang tiến hành OTA..."
                  const uploadStatus = document.getElementById('upload-status');
                  if (uploadStatus) {
                    uploadStatus.innerHTML = '';
                    uploadStatus.style.color = '';
                  }
                  console.log('Modal FOTA đã tự động đóng sau 10 giây');
                }, 10000);
              });
              
              versionActions.appendChild(downloadButton);
              versionActions.appendChild(uploadButton);
              versionActions.appendChild(deleteButton);
              li.appendChild(versionActions);
              
              ul.appendChild(li);
            });
            DOM.versionList.appendChild(ul);
            DOM.versionList.style.minHeight = '100px';
          }
        } else if (!DOM.versionList) {
          console.error('versionList element not found in DOM');
        } else {
          console.error('Failed to load versions:', data.message);
          DOM.versionList.innerHTML = '<p>Lỗi khi tải phiên bản: ' + data.message + '</p>';
        }
        break;
      case 'ota':
        console.log('OTA update progress:', data);
        const uploadStatus = document.getElementById('upload-status');
        if (uploadStatus) {
          console.log('OTA update progress:', data.Percent);
          uploadStatus.innerHTML = `<i class="fas fa-spinner fa-spin"></i> OTA Progress: ${data.Percent}%`;
          if (data.Percent >= 100) {
            uploadStatus.innerHTML = 'OTA Complete!';
            uploadStatus.style.color = 'green';
            statusProxy.StatusOTA = false;
            DOM.fotaModal.style.display = statusProxy.StatusOTA ? "block" : "none";
          }
        }
        break;
      case 'get-real-time-data-daily':
        DailyData = data.realDailyData;
        dailyAverages = getMonthlyDailyAverages(DailyData);
        // Nếu đang ở chế độ daily, render lại biểu đồ ngay khi có dữ liệu
        if (modeChart === 2) {
          renderChart(modeChart);
        }
        break;
      case 'get-real-time-data-hourly':
        HourlyData = data.realHourlyData;
        hourlyAverages = getMonthlyDailyHourlyAverages(HourlyData);
        // Nếu đang ở chế độ hourly, render lại biểu đồ ngay khi có dữ liệu
        if (modeChart === 1) {
          renderChart(modeChart);
        }
        break;
      case 'csv-data':
        console.log(`📊 Received CSV data: ${data.filename} (${data.count} rows)`);
        // Xử lý dữ liệu CSV và cập nhật UI
        if (data.data && data.data.length > 0) {
          // Cập nhật giá trị hiện tại từ dữ liệu CSV (lấy giá trị cuối cùng)
          const lastRow = data.data[data.data.length - 1];
          if (lastRow.Temperature !== undefined) TemperatureValue = lastRow.Temperature;
          if (lastRow.Humidity !== undefined) HumidityValue = lastRow.Humidity;
          if (lastRow.EtOH1 !== undefined) EtOH1Value = lastRow.EtOH1;
          if (lastRow.EtOH2 !== undefined) EtOH2Value = lastRow.EtOH2;
          if (lastRow.EtOH3 !== undefined) EtOH3Value = lastRow.EtOH3;
          if (lastRow.EtOH4 !== undefined) EtOH4Value = lastRow.EtOH4;
          
          // Cập nhật progress bars
          if (progressBars) {
            // Temperature: giá trị trực tiếp là °C, chia cho 100 để normalize (max 100°C)
            progressBars.temp.animate(TemperatureValue / 100);
            // Humidity: giá trị trực tiếp là %, chia cho 100 để normalize
            progressBars.humidity.animate(HumidityValue / 100);
          }
          
          updateUI();
          console.log(`✅ CSV data loaded: ${data.filename}`);
          
          // Hiển thị thông báo
          if (DOM && DOM.statusMessage) {
            DOM.statusMessage.textContent = `Đã tải ${data.count} dòng dữ liệu từ ${data.filename}`;
            DOM.statusMessage.style.color = 'green';
            setTimeout(() => {
              if (DOM.statusMessage) DOM.statusMessage.textContent = '';
            }, 5000);
          }
        }
        break;
      default:
        console.warn("⚠️ Unknown message type:", data.type);
    }
  } catch (error) {
    console.error('❌ Failed to parse WebSocket message:', error);
  }
};

// WebSocket Connection
socket.onopen = () => {
  console.log('WebSocket connected, registering as Frontend');
  socket.send(JSON.stringify({ type: 'register', clientType: 'frontend' }));
};



// ESP32-CAM related code removed

// Page Load Event
document.addEventListener('DOMContentLoaded', init);