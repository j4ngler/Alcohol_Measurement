set(SOURCES WifiManager.c)


idf_component_register(SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_http_server esp_system spiffs json DataManager
    )
