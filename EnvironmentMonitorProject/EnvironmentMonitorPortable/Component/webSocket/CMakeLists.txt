idf_component_register(SRCS "esp_websocket_client.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event tcp_transport http_parser)
