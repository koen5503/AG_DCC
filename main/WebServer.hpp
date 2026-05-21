/**
 * @file WebServer.hpp
 * @brief Embedded Web Server for DCC and Wi-Fi Control.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-20
 * 
 * Sets up the HTTP server using esp_http_server, serves the index HTML
 * page, and implements REST endpoints for Wi-Fi scanning/config
 * and DCC locomotive / accessory command queuing.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#pragma once

#include <esp_http_server.h>
#include "DccRmtTransmitter.hpp"
#include "WifiManager.hpp"

namespace dcc {
namespace web {

class WebServer {
public:
    /**
     * @brief Construct a WebServer instance.
     * @param transmitter Pointer to the initialized DccRmtTransmitter.
     * @param wifi_manager Pointer to the initialized WifiManager.
     */
    WebServer(dcc::rmt::DccRmtTransmitter* transmitter, dcc::wifi::WifiManager* wifi_manager);
    
    /**
     * @brief Destroy the WebServer instance (stops server if running).
     */
    ~WebServer();

    // Prevent copying
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    /**
     * @brief Start the HTTP web server on port 80.
     * @return true if successfully started, false otherwise.
     */
    bool start();

    /**
     * @brief Stop the HTTP web server.
     */
    void stop();

private:
    // URI Handlers
    static esp_err_t indexGetHandler(httpd_req_t* req);
    static esp_err_t wifiScanGetHandler(httpd_req_t* req);
    static esp_err_t wifiConfigPostHandler(httpd_req_t* req);
    static esp_err_t locoGetPostHandler(httpd_req_t* req);
    static esp_err_t accessoryPostHandler(httpd_req_t* req);
    static esp_err_t testPostHandler(httpd_req_t* req);

    dcc::rmt::DccRmtTransmitter* m_transmitter;
    dcc::wifi::WifiManager* m_wifi_manager;
    httpd_handle_t m_server_handle;
};

} // namespace web
} // namespace dcc
