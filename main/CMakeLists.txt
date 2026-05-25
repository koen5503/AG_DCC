idf_component_register(SRCS "DccRmtTransmitter.cpp" "WifiManager.cpp" "WebServer.cpp" "DccDecoder.cpp" "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_rmt nvs_flash esp_wifi esp_event esp_netif esp_http_server cjson)

idf_component_get_property(rmt_dir esp_driver_rmt COMPONENT_DIR)
idf_component_get_property(dma_dir esp_driver_dma COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${rmt_dir}/src" "${dma_dir}/src")


