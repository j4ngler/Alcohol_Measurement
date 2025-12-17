idf_component_register(SRCS "TaskManager.c"
                       INCLUDE_DIRS "."
                       REQUIRES DataManager Time BME280 PMS7003 SD_Card WifiManager SNTP_Sync ssd1306 WebSocKetHandle)
