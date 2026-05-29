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
#include "DccDecoder.hpp"

namespace dcc {
namespace wt {
class WiThrottleServer;
class TestGenerator;
}

namespace web {

class WebServer {
public:
#ifdef CONFIG_BUILD_TEST_GENERATOR
    /**
     * @brief Construct a WebServer instance for the Test Generator Client.
     * @param test_generator Pointer to the active TestGenerator.
     * @param wifi_manager Pointer to the active WifiManager.
     */
    WebServer(dcc::wt::TestGenerator* test_generator, dcc::wifi::WifiManager* wifi_manager);
#else
    /**
     * @brief Construct a WebServer instance for the DCC Command Center.
     * @param transmitter Pointer to the initialized DccRmtTransmitter.
     * @param wifi_manager Pointer to the initialized WifiManager.
     * @param decoder Pointer to the initialized DccDecoder (optional).
     * @param decoder_pin The hardware input GPIO pin connected to the DCC signal.
     * @param withrottle_server Pointer to the active WiThrottleServer (optional).
     */
    WebServer(dcc::rmt::DccRmtTransmitter* transmitter, 
              dcc::wifi::WifiManager* wifi_manager, 
              dcc::rx::DccDecoder* decoder = nullptr, 
              int decoder_pin = -1,
              dcc::wt::WiThrottleServer* withrottle_server = nullptr);
#endif
    
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
    static esp_err_t decoderGetHandler(httpd_req_t* req);
    static esp_err_t decoderTogglePostHandler(httpd_req_t* req);
    static esp_err_t triggerPostHandler(httpd_req_t* req);
    static esp_err_t withrottleGetHandler(httpd_req_t* req);

private:
    dcc::rmt::DccRmtTransmitter* m_transmitter;
public:
    dcc::wifi::WifiManager* m_wifi_manager;
private:
    dcc::rx::DccDecoder* m_decoder;
    httpd_handle_t m_server_handle;
    int m_decoder_pin;

    int m_last_loco_address;
    int m_last_loco_speed;
    bool m_last_loco_direction;
    bool m_last_f_states[9];

    dcc::wt::WiThrottleServer* m_withrottle_server;
public:
    dcc::wt::TestGenerator* m_test_generator;
};

} // namespace web
} // namespace dcc
