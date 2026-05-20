/**
 * @file WebServer.cpp
 * @brief Implementation of the Embedded Web Server for DCC and Wi-Fi Control.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-20
 * 
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "WebServer.hpp"
#include "index_html.h"
#include <esp_log.h>
#include <cJSON.h>
#include <sstream>
#include <algorithm>
#include <cstring>

static const char* TAG = "WebServer";

namespace dcc {
namespace web {

static std::string read_post_body(httpd_req_t* req) {
    char buf[512];
    int ret, remaining = req->content_len;
    std::string body = "";
    
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, std::min(remaining, (int)sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return "";
        }
        body.append(buf, ret);
        remaining -= ret;
    }
    return body;
}

WebServer::WebServer(dcc::rmt::DccRmtTransmitter* transmitter, dcc::wifi::WifiManager* wifi_manager)
    : m_transmitter(transmitter),
      m_wifi_manager(wifi_manager),
      m_server_handle(nullptr) {
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    if (m_server_handle != nullptr) {
        ESP_LOGW(TAG, "Server is already running.");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Allow more open sockets or stack if needed
    config.stack_size = 4096;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting web server on port: %d", config.server_port);
    if (httpd_start(&m_server_handle, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server.");
        return false;
    }

    // Register GET / (Serve Dashboard)
    httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = indexGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &index_uri);

    // Register GET /api/wifi/scan
    httpd_uri_t wifi_scan_uri = {
        .uri      = "/api/wifi/scan",
        .method   = HTTP_GET,
        .handler  = wifiScanGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &wifi_scan_uri);

    // Register POST /api/wifi/config
    httpd_uri_t wifi_config_uri = {
        .uri      = "/api/wifi/config",
        .method   = HTTP_POST,
        .handler  = wifiConfigPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &wifi_config_uri);

    // Register GET/POST /api/loco
    httpd_uri_t loco_uri = {
        .uri      = "/api/loco",
        .method   = static_cast<http_method>(HTTP_ANY), // Handles GET and POST
        .handler  = locoGetPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &loco_uri);

    // Register POST /api/accessory
    httpd_uri_t accessory_uri = {
        .uri      = "/api/accessory",
        .method   = HTTP_POST,
        .handler  = accessoryPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &accessory_uri);

    ESP_LOGI(TAG, "Web server started successfully and endpoints registered.");
    return true;
}

void WebServer::stop() {
    if (m_server_handle != nullptr) {
        ESP_LOGI(TAG, "Stopping web server.");
        httpd_stop(m_server_handle);
        m_server_handle = nullptr;
    }
}

esp_err_t WebServer::indexGetHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    // Serve our elegant glassmorphic dashboard
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::wifiScanGetHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string scan_result = self->m_wifi_manager->scanNetworksJson();
    
    httpd_resp_set_type(req, "application/json");
    // Prevent client caching of scan results
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_sendstr(req, scan_result.c_str());
}

esp_err_t WebServer::wifiConfigPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON body");
        return ESP_FAIL;
    }

    cJSON* ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON* pass_item = cJSON_GetObjectItem(json, "password");

    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID must be a string");
        return ESP_FAIL;
    }

    std::string ssid = ssid_item->valuestring;
    std::string password = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Received Wi-Fi provisioning credentials. SSID: %s", ssid.c_str());
    
    bool saved = self->m_wifi_manager->saveCredentials(ssid, password);
    if (!saved) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write credentials to NVS");
        return ESP_FAIL;
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    char* resp_str = cJSON_PrintUnformatted(resp);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);

    // Schedule system reboot in 1.5 seconds to attempt station connection
    self->m_wifi_manager->scheduleReboot(1500);
    return ESP_OK;
}

esp_err_t WebServer::locoGetPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);

    if (req->method == HTTP_GET) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "ok");
        cJSON_AddStringToObject(root, "mode", self->m_wifi_manager->isApMode() ? "AP Mode" : "Station Mode");
        cJSON_AddStringToObject(root, "ssid", self->m_wifi_manager->getConnectedSsid().c_str());
        
        char* rendered = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_sendstr(req, rendered);
        
        cJSON_free(rendered);
        cJSON_Delete(root);
        return ESP_OK;
    } 
    else if (req->method == HTTP_POST) {
        std::string body = read_post_body(req);
        cJSON* json = cJSON_Parse(body.c_str());
        if (!json) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            return ESP_FAIL;
        }

        cJSON* addr_item = cJSON_GetObjectItem(json, "address");
        cJSON* speed_item = cJSON_GetObjectItem(json, "speed");
        cJSON* dir_item = cJSON_GetObjectItem(json, "direction");
        cJSON* funcs_item = cJSON_GetObjectItem(json, "functions");

        if (!addr_item || !speed_item || !dir_item) {
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
            return ESP_FAIL;
        }

        int address = addr_item->valueint;
        int speed = speed_item->valueint;
        bool direction = cJSON_IsTrue(dir_item);
        bool f_states[9] = {false};

        if (funcs_item && cJSON_IsArray(funcs_item)) {
            int sz = cJSON_GetArraySize(funcs_item);
            for (int i = 0; i < std::min(sz, 9); ++i) {
                cJSON* item = cJSON_GetArrayItem(funcs_item, i);
                if (item) {
                    f_states[i] = cJSON_IsTrue(item);
                }
            }
        }
        cJSON_Delete(json);

        // 1. Generate 128-Speed Step Packet
        // Direction is MSB (Bit 7). Rest is 7-bit speed (0 = stop, 1 = e-stop, 2-127 = speed steps 1-126)
        uint8_t speed_byte = (direction ? 0x80 : 0x00) | (speed == 0 ? 0 : (speed + 1));
        uint8_t pkt_speed[8];
        size_t len_speed = 0;
        
        if (address < 128) {
            pkt_speed[0] = address;
            pkt_speed[1] = 0x3F; // 128 speed step control instruction prefix
            pkt_speed[2] = speed_byte;
            pkt_speed[3] = pkt_speed[0] ^ pkt_speed[1] ^ pkt_speed[2];
            len_speed = 4;
        } else {
            pkt_speed[0] = (address >> 8) | 0xC0; // Standard 2-byte long address prefix
            pkt_speed[1] = address & 0xFF;
            pkt_speed[2] = 0x3F;
            pkt_speed[3] = speed_byte;
            pkt_speed[4] = pkt_speed[0] ^ pkt_speed[1] ^ pkt_speed[2] ^ pkt_speed[3];
            len_speed = 5;
        }
        self->m_transmitter->sendPacket(pkt_speed, len_speed);

        // 2. Generate Function Group 1 Packet (F0, F4, F3, F2, F1)
        // Instruction byte: 0x80 | (F0<<4) | (F4<<3) | (F3<<2) | (F2<<1) | F1
        uint8_t f0_4_byte = 0x80 | 
                           (f_states[0] ? 0x10 : 0) | 
                           (f_states[4] ? 0x08 : 0) | 
                           (f_states[3] ? 0x04 : 0) | 
                           (f_states[2] ? 0x02 : 0) | 
                           (f_states[1] ? 0x01 : 0);
        uint8_t pkt_f0_4[8];
        size_t len_f0_4 = 0;
        if (address < 128) {
            pkt_f0_4[0] = address;
            pkt_f0_4[1] = f0_4_byte;
            pkt_f0_4[2] = pkt_f0_4[0] ^ pkt_f0_4[1];
            len_f0_4 = 3;
        } else {
            pkt_f0_4[0] = (address >> 8) | 0xC0;
            pkt_f0_4[1] = address & 0xFF;
            pkt_f0_4[2] = f0_4_byte;
            pkt_f0_4[3] = pkt_f0_4[0] ^ pkt_f0_4[1] ^ pkt_f0_4[2];
            len_f0_4 = 4;
        }
        self->m_transmitter->sendPacket(pkt_f0_4, len_f0_4);

        // 3. Generate Function Group 2 Packet (F8, F7, F6, F5)
        // Instruction byte: 0xB0 | (F8<<3) | (F7<<2) | (F6<<1) | F5
        uint8_t f5_8_byte = 0xB0 | 
                           (f_states[8] ? 0x08 : 0) | 
                           (f_states[7] ? 0x04 : 0) | 
                           (f_states[6] ? 0x02 : 0) | 
                           (f_states[5] ? 0x01 : 0);
        uint8_t pkt_f5_8[8];
        size_t len_f5_8 = 0;
        if (address < 128) {
            pkt_f5_8[0] = address;
            pkt_f5_8[1] = f5_8_byte;
            pkt_f5_8[2] = pkt_f5_8[0] ^ pkt_f5_8[1];
            len_f5_8 = 3;
        } else {
            pkt_f5_8[0] = (address >> 8) | 0xC0;
            pkt_f5_8[1] = address & 0xFF;
            pkt_f5_8[2] = f5_8_byte;
            pkt_f5_8[3] = pkt_f5_8[0] ^ pkt_f5_8[1] ^ pkt_f5_8[2];
            len_f5_8 = 4;
        }
        self->m_transmitter->sendPacket(pkt_f5_8, len_f5_8);

        // Return primary Speed Packet bytes for frontend console telemetry logs
        cJSON* resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON* bytes_arr = cJSON_CreateArray();
        for (size_t i = 0; i < len_speed; ++i) {
            cJSON_AddItemToArray(bytes_arr, cJSON_CreateNumber(pkt_speed[i]));
        }
        cJSON_AddItemToObject(resp, "bytes", bytes_arr);

        char* resp_str = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, resp_str);
        
        cJSON_free(resp_str);
        cJSON_Delete(resp);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t WebServer::accessoryPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* addr_item = cJSON_GetObjectItem(json, "address");
    cJSON* str_item = cJSON_GetObjectItem(json, "straight");

    if (!addr_item || !str_item) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
        return ESP_FAIL;
    }

    int turnout_addr = addr_item->valueint;
    bool isStraight = cJSON_IsTrue(str_item);
    cJSON_Delete(json);

    // accessory turnout address format mapping
    uint16_t dcc_addr = (turnout_addr - 1) / 4;
    uint8_t pair = (turnout_addr - 1) % 4;
    uint8_t direction = isStraight ? 1 : 0;
    uint8_t aaa = (~dcc_addr >> 6) & 0x07;

    // 1. Turnout ON packet (C = 1)
    uint8_t pkt_on[3];
    pkt_on[0] = 0x80 | (dcc_addr & 0x3F);
    pkt_on[1] = 0x80 | (aaa << 4) | (1 << 3) | (pair << 1) | direction;
    pkt_on[2] = pkt_on[0] ^ pkt_on[1];
    self->m_transmitter->sendPacket(pkt_on, sizeof(pkt_on));

    // 2. Turnout OFF packet (C = 0) so the solenoid coil releases
    uint8_t pkt_off[3];
    pkt_off[0] = pkt_on[0];
    pkt_off[1] = 0x80 | (aaa << 4) | (0 << 3) | (pair << 1) | direction;
    pkt_off[2] = pkt_off[0] ^ pkt_off[1];
    self->m_transmitter->sendPacket(pkt_off, sizeof(pkt_off));

    // Return the ON packet bytes to the frontend telemetry dashboard
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON* bytes_arr = cJSON_CreateArray();
    for (size_t i = 0; i < sizeof(pkt_on); ++i) {
        cJSON_AddItemToArray(bytes_arr, cJSON_CreateNumber(pkt_on[i]));
    }
    cJSON_AddItemToObject(resp, "bytes", bytes_arr);

    char* resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

} // namespace web
} // namespace dcc
