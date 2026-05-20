idf_component_register(SRCS "DccRmtTransmitter.cpp" "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_rmt)
