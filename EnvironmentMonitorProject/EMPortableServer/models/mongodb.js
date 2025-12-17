const mongoose = require("mongoose");
mongoose.connect("mongodb://localhost:27017/EnvironmentMonitor");

const FirmwareSchema = new mongoose.Schema({
  ID: String,
  Version: String, // Changed from Number to String to support version names like "v1.2.3"
  DataHex: String,
  Description: String, // Added description field
  FileName: String, // Added original filename
  FileSize: Number, // Added file size
  UploadDate: { type: Date, default: Date.now }, // Added upload date
  Checksum: String // Added checksum for file integrity
});

const FirmwareModel = mongoose.model("Firmware", FirmwareSchema, "FirmwareOTA");

async function getAllVersions() {
  const docs = await FirmwareModel.find({}, "Version Description UploadDate FileSize").sort({ UploadDate: -1 });
  return docs.map(d => ({
    version: d.Version,
    description: d.Description,
    uploadDate: d.UploadDate,
    fileSize: d.FileSize
  }));
}

async function getDataFirmware(version) {
  try {
    const doc = await FirmwareModel.findOne({ Version: version }, "DataHex");

    if (!doc) {
      console.log(`Không tìm thấy version = ${version}`);
      return null;
    }

    return doc.DataHex;
  } catch (error) {
    console.error("Lỗi khi truy vấn DataHex:", error);
    throw error;
  }
}

async function saveFirmware(version, dataHex, description, fileName, fileSize, checksum) {
  try {
    const firmware = new FirmwareModel({
      ID: "Firmware",
      Version: version,
      DataHex: dataHex,
      Description: description,
      FileName: fileName,
      FileSize: fileSize,
      Checksum: checksum,
      UploadDate: new Date()
    });

    await firmware.save();
    console.log(`✅ Firmware ${version} đã được lưu vào database`);
    return firmware;
  } catch (error) {
    console.error("❌ Lỗi khi lưu firmware:", error.message);
    throw error;
  }
}

async function getFirmwareByVersion(version) {
  try {
    const doc = await FirmwareModel.findOne({ Version: version });
    return doc;
  } catch (error) {
    console.error("Lỗi khi truy vấn firmware:", error);
    throw error;
  }
}

async function deleteFirmwareByVersion(version) {
  try {
    const result = await FirmwareModel.deleteOne({ Version: version });
    return { deletedCount: result.deletedCount };
  } catch (error) {
    console.error("Lỗi khi xóa firmware:", error);
    throw error;
  }
}

const Real_Data = new mongoose.Schema({
  ID: String,
  Time: String,
  Temperature: Number,
  Humidity: Number,
  Pressure: Number,
  PM1: Number,
  PM25: Number,
  PM10: Number
});


const realTimeData = mongoose.model("Data", Real_Data, "RealTimeData");

async function saveRealTimeData(jsonString) {
  try {
    const parsed = JSON.parse(jsonString); // ✅ chuyển chuỗi thành object

    // ✅ Cập nhật theo ID nếu đã có, hoặc tạo mới
    await realTimeData.create({
      ID: parsed.ID,
      Time: parsed.Time,
      Temperature: parsed.Temperature,
      Humidity: parsed.Humidity,
      Pressure: parsed.Pressure,
      PM1: parsed.PM1,
      PM25: parsed.PM25,
      PM10: parsed.PM10
    });

    console.log("✅ Dữ liệu đã được cập nhật vào database:", parsed);
  } catch (error) {
    console.error("❌ JSON không hợp lệ hoặc lỗi lưu:", error.message);
  }
}

async function getRealTimeData() {
  try {
    const result = await realTimeData.find({ ID: "Data" });
    return result;
  } catch (error) {
    console.error('Error getting real-time data:', error.message);
    throw error;
  }
}

module.exports = { 
  getAllVersions, 
  saveRealTimeData, 
  getDataFirmware, 
  getRealTimeData, 
  saveFirmware, 
  getFirmwareByVersion,
  deleteFirmwareByVersion 
}