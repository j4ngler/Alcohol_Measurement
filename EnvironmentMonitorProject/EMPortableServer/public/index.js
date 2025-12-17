// Constants and State Variables
const API_KEY = 'ec224bde787c4001b0281007251802';
const LOCATION = 'Hanoi';
const socket = new WebSocket('ws://localhost:8080');
let TemperatureValue = 50, HumidityValue = 0, PressureValue = 0, PM1Value = 0, PM25Value = 0, PM10Value = 0;
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
  if (!DOM.fotaModal) console.error('fotaModal not found');
  if (!DOM.fotaBtn) console.error('fotaBtn not found');
  if (!DOM.fotaCloseBtn) console.error('fotaCloseBtn not found');
  if (!DOM.versionList) console.error('versionlist not found');

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
  DOM.fotaCloseBtn.addEventListener('click', () => {
    // if (isUploadingFirmware) {
    //   console.warn("🚫 Không thể đóng modal khi OTA đang diễn ra.");
    //   return;
    // }
    isUpdatingUI = true;
    statusProxy.StatusOTA = false;
    DOM.fotaBtn.classList.remove('active');
    updateUI();
  });
  window.addEventListener('click', (event) => {
    // Click ra ngoài vùng modal-content sẽ đóng modal
    if (DOM.fotaModal && DOM.fotaContent && event.target === DOM.fotaModal) {
      // if (isUploadingFirmware) {
      //   console.warn("🚫 Không thể đóng modal khi OTA đang diễn ra.");
      //   return;
      // }
      isUpdatingUI = true;
      statusProxy.StatusOTA = false;
      DOM.fotaBtn.classList.remove('active');
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

// Export Functions
const exportChart = () => {
  // Wait for Highcharts to be fully loaded
  if (typeof Highcharts === 'undefined') {
    alert('Chart library chưa được tải! Vui lòng đợi...');
    return;
  }
  
  // Check if exporting module is loaded
  if (!Highcharts.Chart.prototype.exportChart) {
    alert('Export module chưa được tải! Vui lòng tải lại trang.');
    console.error('Highcharts exporting module not available');
    return;
  }
  
  // Try multiple methods to get chart instance
  let chart = currentChart;
  
  // If not found, try to find from container
  if (!chart) {
    const container = document.getElementById('chart-container');
    if (container) {
      // Find chart by matching container
      chart = Highcharts.charts.find(ch => ch && ch.container && ch.container.id === 'chart-container');
    }
  }
  
  // If still not found, try first available chart
  if (!chart && Highcharts.charts && Highcharts.charts.length > 0) {
    chart = Highcharts.charts.find(ch => ch !== null && ch !== undefined && ch.renderTo);
  }
  
  if (!chart) {
    alert('Không tìm thấy biểu đồ để xuất!');
    console.error('Chart instance not found. Available charts:', Highcharts.charts);
    console.log('Current chart variable:', currentChart);
    console.log('Chart container:', document.getElementById('chart-container'));
    return;
  }
  
  // Verify chart is ready
  if (!chart.renderTo || !chart.series) {
    alert('Biểu đồ chưa sẵn sàng để xuất! Vui lòng đợi một chút.');
    console.error('Chart not ready:', chart);
    return;
  }
  
  // Get current date for filename
  const now = new Date();
  const dateStr = now.toISOString().split('T')[0];
  const timeStr = now.toTimeString().split(' ')[0].replace(/:/g, '-');
  
  // Get mode name for filename
  const modeName = modeChart === 0 ? 'Currently' : modeChart === 1 ? 'Hourly' : 'Daily';
  let modeSuffix = '';
  if (modeChart === 1 && year && month && day) {
    modeSuffix = `-${year}-${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
  } else if (modeChart === 2 && year && month) {
    modeSuffix = `-${year}-${String(month).padStart(2, '0')}`;
  }
  
  try {
    console.log('Exporting chart:', chart);
    console.log('Filename:', `sensor-chart-${modeName}${modeSuffix}-${dateStr}-${timeStr}`);
    
    // Export chart as PNG
    chart.exportChart({
      type: 'image/png',
      filename: `sensor-chart-${modeName}${modeSuffix}-${dateStr}-${timeStr}`,
      sourceWidth: chart.chartWidth || 1200,
      sourceHeight: chart.chartHeight || 600
    });
    console.log('Chart exported successfully!');
  } catch (error) {
    console.error('Error exporting chart:', error);
    alert('Lỗi khi xuất đồ thị: ' + error.message);
  }
};

const exportReport = async () => {
  // Get chart image first
  let chartImage = '';
  try {
    if (typeof Highcharts !== 'undefined') {
      // Try multiple methods to get chart instance
      let chart = currentChart;
      
      if (!chart) {
        const container = document.getElementById('chart-container');
        if (container) {
          chart = Highcharts.charts.find(ch => ch && ch.container && ch.container.id === 'chart-container');
        }
      }
      
      if (!chart && Highcharts.charts && Highcharts.charts.length > 0) {
        chart = Highcharts.charts.find(ch => ch !== null && ch !== undefined);
      }
      
      if (chart) {
        const svg = chart.getSVG();
        // Convert SVG to data URI (URL encoded for better compatibility)
        const svgEncoded = encodeURIComponent(svg);
        chartImage = 'data:image/svg+xml;charset=utf-8,' + svgEncoded;
      } else {
        console.warn('Chart instance not found for report export');
      }
    }
  } catch (error) {
    console.warn('Could not get chart image:', error);
  }
  
  // Get data based on current mode
  let sensorData = {};
  let modeName = '';
  let timeInfo = '';
  
  if (modeChart === 0) {
    // Currently mode - use real-time values
    modeName = 'Currently';
    timeInfo = 'Dữ liệu thời gian thực';
    sensorData = {
      mode: 'Currently',
      temperature: TemperatureValue,
      humidity: HumidityValue,
      pressure: PressureValue,
      pm1: PM1Value,
      pm25: PM25Value,
      pm10: PM10Value,
      timestamp: new Date().toISOString(),
      chartMode: modeName,
      timeInfo: timeInfo
    };
  } else if (modeChart === 1) {
    // Hourly mode - get hourly averages for selected day
    modeName = 'Hourly';
    if (!year || !month || !day) {
      alert('Vui lòng chọn ngày để xuất báo cáo!');
      return;
    }
    timeInfo = `Ngày ${day}/${month}/${year}`;
    
    // Get hourly data for the selected day
    const hourlyData = generateChartDataFromHourlyAllMetrics();
    const hourlyValues = [];
    
    for (let h = 0; h < 24; h++) {
      hourlyValues.push({
        hour: h,
        temperature: hourlyData.Temperature[h],
        humidity: hourlyData.Humidity[h],
        pressure: hourlyData.Pressure[h],
        pm1: hourlyData.PM1[h],
        pm25: hourlyData.PM25[h],
        pm10: hourlyData.PM10[h]
      });
    }
    
    // Calculate averages
    const avgTemp = hourlyValues.filter(v => v.temperature !== null).reduce((sum, v) => sum + v.temperature, 0) / hourlyValues.filter(v => v.temperature !== null).length || 0;
    const avgHumidity = hourlyValues.filter(v => v.humidity !== null).reduce((sum, v) => sum + v.humidity, 0) / hourlyValues.filter(v => v.humidity !== null).length || 0;
    const avgPressure = hourlyValues.filter(v => v.pressure !== null).reduce((sum, v) => sum + v.pressure, 0) / hourlyValues.filter(v => v.pressure !== null).length || 0;
    const avgPM1 = hourlyValues.filter(v => v.pm1 !== null).reduce((sum, v) => sum + v.pm1, 0) / hourlyValues.filter(v => v.pm1 !== null).length || 0;
    const avgPM25 = hourlyValues.filter(v => v.pm25 !== null).reduce((sum, v) => sum + v.pm25, 0) / hourlyValues.filter(v => v.pm25 !== null).length || 0;
    const avgPM10 = hourlyValues.filter(v => v.pm10 !== null).reduce((sum, v) => sum + v.pm10, 0) / hourlyValues.filter(v => v.pm10 !== null).length || 0;
    
    sensorData = {
      mode: 'Hourly',
      year: year,
      month: month,
      day: day,
      temperature: avgTemp,
      humidity: avgHumidity,
      pressure: avgPressure,
      pm1: avgPM1,
      pm25: avgPM25,
      pm10: avgPM10,
      timestamp: new Date().toISOString(),
      chartMode: modeName,
      timeInfo: timeInfo,
      hourlyValues: hourlyValues
    };
  } else if (modeChart === 2) {
    // Daily mode - get daily averages for selected month
    modeName = 'Daily';
    if (!year || !month) {
      alert('Vui lòng chọn tháng để xuất báo cáo!');
      return;
    }
    timeInfo = `Tháng ${month}/${year}`;
    
    // Get daily data for the selected month
    const dailyData = generateChartDataFromDailyAllMetrics();
    const dailyValues = [];
    
    for (let d = 1; d <= 31; d++) {
      dailyValues.push({
        day: d,
        temperature: dailyData.Temperature[d - 1],
        humidity: dailyData.Humidity[d - 1],
        pressure: dailyData.Pressure[d - 1],
        pm1: dailyData.PM1[d - 1],
        pm25: dailyData.PM25[d - 1],
        pm10: dailyData.PM10[d - 1]
      });
    }
    
    // Calculate averages
    const avgTemp = dailyValues.filter(v => v.temperature !== null).reduce((sum, v) => sum + v.temperature, 0) / dailyValues.filter(v => v.temperature !== null).length || 0;
    const avgHumidity = dailyValues.filter(v => v.humidity !== null).reduce((sum, v) => sum + v.humidity, 0) / dailyValues.filter(v => v.humidity !== null).length || 0;
    const avgPressure = dailyValues.filter(v => v.pressure !== null).reduce((sum, v) => sum + v.pressure, 0) / dailyValues.filter(v => v.pressure !== null).length || 0;
    const avgPM1 = dailyValues.filter(v => v.pm1 !== null).reduce((sum, v) => sum + v.pm1, 0) / dailyValues.filter(v => v.pm1 !== null).length || 0;
    const avgPM25 = dailyValues.filter(v => v.pm25 !== null).reduce((sum, v) => sum + v.pm25, 0) / dailyValues.filter(v => v.pm25 !== null).length || 0;
    const avgPM10 = dailyValues.filter(v => v.pm10 !== null).reduce((sum, v) => sum + v.pm10, 0) / dailyValues.filter(v => v.pm10 !== null).length || 0;
    
    sensorData = {
      mode: 'Daily',
      year: year,
      month: month,
      temperature: avgTemp,
      humidity: avgHumidity,
      pressure: avgPressure,
      pm1: avgPM1,
      pm25: avgPM25,
      pm10: avgPM10,
      timestamp: new Date().toISOString(),
      chartMode: modeName,
      timeInfo: timeInfo,
      dailyValues: dailyValues
    };
  }
  
  // Add chart image to sensor data
  sensorData.chartImage = chartImage;
  
  // Create report content
  const reportContent = generateReportContent(sensorData);
  
  // Create and download report
  downloadReport(reportContent, sensorData);
  
  console.log('Report exported successfully!');
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
          <td>${formatValue(hourData.pressure)}</td>
          <td>${formatValue(hourData.pm1)}</td>
          <td>${formatValue(hourData.pm25)}</td>
          <td>${formatValue(hourData.pm10)}</td>
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
            <td>${formatValue(dayData.pressure)}</td>
            <td>${formatValue(dayData.pm1)}</td>
            <td>${formatValue(dayData.pm25)}</td>
            <td>${formatValue(dayData.pm10)}</td>
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
              <td>PM1.0</td>
              <td>${formatValue(data.pm1)}</td>
              <td>μg/m³</td>
              <td>${data.pm1 > 25 ? 'Cao' : data.pm1 < 10 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
            <tr>
              <td>PM2.5</td>
              <td>${formatValue(data.pm25)}</td>
              <td>μg/m³</td>
              <td>${data.pm25 > 25 ? 'Cao' : data.pm25 < 10 ? 'Thấp' : 'Bình thường'}</td>
            </tr>
            <tr>
              <td>PM10</td>
              <td>${formatValue(data.pm10)}</td>
              <td>μg/m³</td>
              <td>${data.pm10 > 50 ? 'Cao' : data.pm10 < 20 ? 'Thấp' : 'Bình thường'}</td>
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
                <th>PM1.0 (μg/m³)</th>
                <th>PM2.5 (μg/m³)</th>
                <th>PM10 (μg/m³)</th>
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
                <th>PM1.0 (μg/m³)</th>
                <th>PM2.5 (μg/m³)</th>
                <th>PM10 (μg/m³)</th>
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
  const commonConfig = {
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
      bar.setText(value || '0');
      bar.text.style.color = state.color;
    }
  };
  try {
    progressBars = {
      temp: new ProgressBar.SemiCircle('#container_temperature', { ...commonConfig, text: { value: '', alignToBottom: false, className: 'progressbar_label' } }),
      humidity: new ProgressBar.Line('#container_humidity', { ...commonConfig, text: { value: '', className: 'humidity_label' } }),
      pressure: new ProgressBar.Line('#container_pressure', { ...commonConfig, text: { value: '', className: 'pressure_label' } }),
      pm1: new ProgressBar.Line('#container_pm1', { ...commonConfig, text: { value: '', className: 'pm1_label' } }),
      pm25: new ProgressBar.Line('#container_pm25', { ...commonConfig, text: { value: '', className: 'pm25_label' } }),
      pm10: new ProgressBar.Line('#container_pm10', { ...commonConfig, text: { value: '', className: 'pm10_label' } })
    };
    progressBars.temp.animate(TemperatureValue / 100);
    progressBars.humidity.animate(HumidityValue / 100);
    progressBars.pressure.animate(PressureValue / 100);
    progressBars.pm1.animate(PM1Value / 100);
    progressBars.pm25.animate(PM25Value / 100);
    progressBars.pm10.animate(PM10Value / 100);
  } catch (error) {
    console.error('Error initializing progress bars:', error);
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
      Pressure: [],
      PM1: [],
      PM25: [],
      PM10: []
    };
    const hourlyData = generateChartDataFromHourlyAllMetrics();
    for (let i = 0; i < 24; i++) {
      const x = startOfToday + i * 3600 * 1000;
      result.Temperature.push({ x, y: hourlyData.Temperature[i] ?? null });
      result.Humidity.push({ x, y: hourlyData.Humidity[i] ?? null });
      result.Pressure.push({ x, y: hourlyData.Pressure[i] ?? null });
      result.PM1.push({ x, y: hourlyData.PM1[i] ?? null });
      result.PM25.push({ x, y: hourlyData.PM25[i] ?? null });
      result.PM10.push({ x, y: hourlyData.PM10[i] ?? null });
    }
    return result;
  };
  const generateAll31DaySeriesData = () => {
    const result = {
      Temperature: [],
      Humidity: [],
      Pressure: [],
      PM1: [],
      PM25: [],
      PM10: []
    };
    const dailyData = generateChartDataFromDailyAllMetrics();
    const startDate = new Date(now.getFullYear(), now.getMonth(), 1).getTime();
    for (let i = 0; i < 31; i++) {
      const x = startDate + i * 24 * 3600 * 1000;
      result.Temperature.push({ x, y: dailyData.Temperature[i] ?? null });
      result.Humidity.push({ x, y: dailyData.Humidity[i] ?? null });
      result.Pressure.push({ x, y: dailyData.Pressure[i] ?? null });
      result.PM1.push({ x, y: dailyData.PM1[i] ?? null });
      result.PM25.push({ x, y: dailyData.PM25[i] ?? null });
      result.PM10.push({ x, y: dailyData.PM10[i] ?? null });
    }
    return result;
  };
  const generateLiveInitData = () => {
    const now = new Date().getTime();
    const result = {
      Temperature: [],
      Humidity: [],
      Pressure: [],
      PM1: [],
      PM25: [],
      PM10: []
    };
    for (let i = -19; i <= 0; i++) {
      const x = now + i * 1000;
      result.Temperature.push({ x, y: TemperatureValue });
      result.Humidity.push({ x, y: HumidityValue });
      result.Pressure.push({ x, y: PressureValue });
      result.PM1.push({ x, y: PM1Value });
      result.PM25.push({ x, y: PM25Value });
      result.PM10.push({ x, y: PM10Value });
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
        { seriesIndex: 2, y: PressureValue },
        { seriesIndex: 3, y: PM1Value },
        { seriesIndex: 4, y: PM25Value },
        { seriesIndex: 5, y: PM10Value }
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
        { name: 'Pressure (hPa)', lineWidth: 2, color: 'green', data: seriesDataFunc.Pressure, visible: false },
        { name: 'PM1.0 (μg/m³)', lineWidth: 2, color: 'yellow', data: seriesDataFunc.PM1, visible: false },
        { name: 'PM2.5 (μg/m³)', lineWidth: 2, color: 'purple', data: seriesDataFunc.PM25, visible: false },
        { name: 'PM10 (μg/m³)', lineWidth: 2, color: 'orange', data: seriesDataFunc.PM10, visible: false }
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
        Pressure: [],
        PM1: [],
        PM25: [],
        PM10: []
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
      monthlyDailyHourlyData[month][day][hour].Pressure.push(entry.Pressure);
      monthlyDailyHourlyData[month][day][hour].PM1.push(entry.PM1);
      monthlyDailyHourlyData[month][day][hour].PM25.push(entry.PM25);
      monthlyDailyHourlyData[month][day][hour].PM10.push(entry.PM10);
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
        Pressure: average(hourData.Pressure),
        PM1: average(hourData.PM1),
        PM25: average(hourData.PM25),
        PM10: average(hourData.PM10)
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
      Pressure: [],
      PM1: [],
      PM25: [],
      PM10: []
    }))
  );
  dataArray.forEach(entry => {
    const date = new Date(entry.Time);
    const month = date.getMonth() + 1;
    const day = date.getDate();
    if (month >= 1 && month <= 12 && day >= 1 && day <= 30) {
      monthlyDailyData[month][day].Temperature.push(entry.Temperature);
      monthlyDailyData[month][day].Humidity.push(entry.Humidity);
      monthlyDailyData[month][day].Pressure.push(entry.Pressure);
      monthlyDailyData[month][day].PM1.push(entry.PM1);
      monthlyDailyData[month][day].PM25.push(entry.PM25);
      monthlyDailyData[month][day].PM10.push(entry.PM10);
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
        Pressure: average(dayData.Pressure),
        PM1: average(dayData.PM1),
        PM25: average(dayData.PM25),
        PM10: average(dayData.PM10)
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
    Pressure: [],
    PM1: [],
    PM25: [],
    PM10: []
  };
  if (!year || !month || !day) return result;
  for (let h = 0; h < 24; h++) {
    ['Temperature', 'Humidity', 'Pressure', 'PM1', 'PM25', 'PM10'].forEach(key => {
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
    Pressure: [],
    PM1: [],
    PM25: [],
    PM10: []
  };
  if (!year || !month) return result;
  for (let d = 1; d <= 31; d++) {
    ['Temperature', 'Humidity', 'Pressure', 'PM1', 'PM25', 'PM10'].forEach(key => {
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
        if ('Pressure' in status) PressureValue = parseFloat(status.Pressure);
        if ('PM1' in status) PM1Value = parseFloat(status.PM1);
        if ('PM25' in status) PM25Value = parseFloat(status.PM25);
        if ('PM10' in status) PM10Value = parseFloat(status.PM10);
        if (progressBars) {
          progressBars.temp.animate(TemperatureValue / 100);
          progressBars.humidity.animate(HumidityValue / 100);
          progressBars.pressure.animate(PressureValue / 100);
          progressBars.pm1.animate(PM1Value / 100);
          progressBars.pm25.animate(PM25Value / 100);
          progressBars.pm10.animate(PM10Value / 100);
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