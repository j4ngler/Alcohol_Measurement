idf_component_register(SRCS "WSHandle.c"
                       INCLUDE_DIRS "."
                       REQUIRES webSocket DataManager WifiManager JsonHandle FOTAManager)
