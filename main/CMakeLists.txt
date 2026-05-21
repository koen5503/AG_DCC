idf_component_register(SRCS "DccRmtTransmitter.cpp" "WifiManager.cpp" "WebServer.cpp" "DccDecoder.cpp" "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_rmt nvs_flash esp_wifi esp_event esp_netif esp_http_server cjson)
