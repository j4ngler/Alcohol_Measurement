set(SOURCES FOTAManager.c)


idf_component_register(SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES DataManager esp_http_client nvs_flash json esp_https_ota app_update TaskManager
    )
