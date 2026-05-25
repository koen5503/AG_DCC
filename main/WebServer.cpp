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
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_freertos_hooks.h"

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

WebServer::WebServer(dcc::rmt::DccRmtTransmitter* transmitter, dcc::wifi::WifiManager* wifi_manager, dcc::rx::DccDecoder* decoder, int decoder_pin)
    : m_transmitter(transmitter),
      m_wifi_manager(wifi_manager),
      m_decoder(decoder),
      m_server_handle(nullptr),
      m_decoder_pin(decoder_pin) {
    m_last_loco_address = -1;
    m_last_loco_speed = -1;
    m_last_loco_direction = false;
    std::memset(m_last_f_states, 0, sizeof(m_last_f_states));
}

static volatile uint64_t s_idle_count_core0 = 0;
static volatile uint64_t s_idle_count_core1 = 0;

static bool IRAM_ATTR idle_hook_core0(void) {
    s_idle_count_core0 = s_idle_count_core0 + 1;
    return true;
}

static bool IRAM_ATTR idle_hook_core1(void) {
    s_idle_count_core1 = s_idle_count_core1 + 1;
    return true;
}

static float s_cpu_load_core0 = 0.0f;
static float s_cpu_load_core1 = 0.0f;

static void cpuMonitorTask(void* param) {
    // Register the idle hooks for each CPU
    esp_register_freertos_idle_hook_for_cpu(idle_hook_core0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle_hook_core1, 1);

    uint64_t last_idle_core0 = 0;
    uint64_t last_idle_core1 = 0;
    uint64_t max_delta_core0 = 1; 
    uint64_t max_delta_core1 = 1;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Sample every 1 second

        uint64_t curr_idle0 = s_idle_count_core0;
        uint64_t curr_idle1 = s_idle_count_core1;

        uint64_t delta0 = curr_idle0 - last_idle_core0;
        uint64_t delta1 = curr_idle1 - last_idle_core1;

        last_idle_core0 = curr_idle0;
        last_idle_core1 = curr_idle1;

        // Auto-calibrate: dynamically track the maximum idle count in any 1s window
        if (delta0 > max_delta_core0) max_delta_core0 = delta0;
        if (delta1 > max_delta_core1) max_delta_core1 = delta1;

        // Calculate load percentage
        float load0 = 100.0f - (static_cast<float>(delta0) * 100.0f / static_cast<float>(max_delta_core0));
        float load1 = 100.0f - (static_cast<float>(delta1) * 100.0f / static_cast<float>(max_delta_core1));

        // Clamp
        if (load0 < 0.0f) load0 = 0.0f;
        if (load0 > 100.0f) load0 = 100.0f;
        if (load1 < 0.0f) load1 = 0.0f;
        if (load1 > 100.0f) load1 = 100.0f;

        s_cpu_load_core0 = load0;
        s_cpu_load_core1 = load1;
    }
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
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting web server on port: %d", config.server_port);
    if (httpd_start(&m_server_handle, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server.");
        return false;
    }

    // Start CPU load monitoring task on Core 0 (tracks both Core 0 & Core 1)
    xTaskCreatePinnedToCore(cpuMonitorTask, "cpu_monitor", 2048, nullptr, 1, NULL, 0);

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

    // Register POST /api/test
    httpd_uri_t test_uri = {
        .uri      = "/api/test",
        .method   = HTTP_POST,
        .handler  = testPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &test_uri);

    // Register GET /api/decoder
    httpd_uri_t decoder_uri = {
        .uri      = "/api/decoder",
        .method   = HTTP_GET,
        .handler  = decoderGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &decoder_uri);

    // Register POST /api/decoder/toggle
    httpd_uri_t decoder_toggle_uri = {
        .uri      = "/api/decoder/toggle",
        .method   = HTTP_POST,
        .handler  = decoderTogglePostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &decoder_toggle_uri);

    // Register POST /api/trigger
    httpd_uri_t trigger_uri = {
        .uri      = "/api/trigger",
        .method   = HTTP_POST,
        .handler  = triggerPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &trigger_uri);

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

        // Differential transmission (Strategy A): Only dispatch packet types whose target states have actually mutated
        bool send_speed = (self->m_last_loco_address != address) || 
                          (self->m_last_loco_speed != speed) || 
                          (self->m_last_loco_direction != direction);
        
        bool send_f0_4 = (self->m_last_loco_address != address) || 
                         (self->m_last_f_states[0] != f_states[0]) || 
                         (self->m_last_f_states[1] != f_states[1]) || 
                         (self->m_last_f_states[2] != f_states[2]) || 
                         (self->m_last_f_states[3] != f_states[3]) || 
                         (self->m_last_f_states[4] != f_states[4]);

        bool send_f5_8 = (self->m_last_loco_address != address) || 
                         (self->m_last_f_states[5] != f_states[5]) || 
                         (self->m_last_f_states[6] != f_states[6]) || 
                         (self->m_last_f_states[7] != f_states[7]) || 
                         (self->m_last_f_states[8] != f_states[8]);

        if (send_speed) {
            self->m_transmitter->sendPacket(pkt_speed, len_speed);
            self->m_last_loco_speed = speed;
            self->m_last_loco_direction = direction;
        }

        if (send_f0_4) {
            self->m_transmitter->sendPacket(pkt_f0_4, len_f0_4);
            self->m_last_f_states[0] = f_states[0];
            self->m_last_f_states[1] = f_states[1];
            self->m_last_f_states[2] = f_states[2];
            self->m_last_f_states[3] = f_states[3];
            self->m_last_f_states[4] = f_states[4];
        }

        if (send_f5_8) {
            self->m_transmitter->sendPacket(pkt_f5_8, len_f5_8);
            self->m_last_f_states[5] = f_states[5];
            self->m_last_f_states[6] = f_states[6];
            self->m_last_f_states[7] = f_states[7];
            self->m_last_f_states[8] = f_states[8];
        }

        if (send_speed || send_f0_4 || send_f5_8) {
            self->m_last_loco_address = address;
        }

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

static std::atomic<bool> s_test_running{false};

struct TestTaskParams {
    int scenario_id;
    dcc::rmt::DccRmtTransmitter* transmitter;
};

static void test_scenario_task(void* pvParameters) {
    auto* params = static_cast<TestTaskParams*>(pvParameters);
    int scenario = params->scenario_id;
    auto* transmitter = params->transmitter;

    ESP_LOGI("DccTestRunner", "Starting autonomous test scenario %d...", scenario);

    if (scenario == 1) {
        // Continuous Idle Packets (Baseline) for 5 seconds
        // RMT Transmitter automatically transmits continuous DCC Idle Packets when queue is empty.
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    else if (scenario == 2) {
        // Locomotive Speed Packets (Varying Speed/Direction)
        uint8_t speed_step = 1;
        bool direction_forward = true;
        for (int i = 0; i < 50; ++i) {
            uint8_t speed_byte = 0b01000000;
            if (direction_forward) {
                speed_byte |= 0b00100000;
            }
            speed_byte |= (speed_step & 0x0F);
            
            uint8_t raw_payload[4];
            raw_payload[0] = 0x03;                       // Address 3
            raw_payload[1] = 0x3F;                       // 128 speed prefix
            raw_payload[2] = speed_byte;                 // Speed data
            raw_payload[3] = raw_payload[0] ^ raw_payload[1] ^ raw_payload[2];

            transmitter->sendPacket(raw_payload, sizeof(raw_payload));

            speed_step++;
            if (speed_step > 28) {
                speed_step = 1;
                direction_forward = !direction_forward;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    else if (scenario == 3) {
        // Accessory / Switch Commands
        for (int i = 0; i < 6; ++i) {
            uint8_t raw_payload[3];
            raw_payload[0] = 0x80;
            raw_payload[1] = (i % 2 == 0) ? 0xF8 : 0xF0;
            raw_payload[2] = raw_payload[0] ^ raw_payload[1];

            transmitter->sendPacket(raw_payload, sizeof(raw_payload));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    else if (scenario == 4) {
        // BiDi (Bidirectional) Cutout Timing Probe
        if (transmitter->isInitialized()) {
            auto original_config = transmitter->getConfig();
            
            // Reinitialize with BiDi enabled
            auto bidi_config = original_config;
            bidi_config.enable_bidi = true;
            transmitter->deinit();
            transmitter->init(bidi_config);

            // Feed standard packets
            for (int i = 0; i < 15; ++i) {
                uint8_t raw_payload[4] = { 0x03, 0x3F, 0x1A, 0x00 };
                raw_payload[3] = raw_payload[0] ^ raw_payload[1] ^ raw_payload[2];
                transmitter->sendPacket(raw_payload, sizeof(raw_payload));
                vTaskDelay(pdMS_TO_TICKS(150));
            }

            // Restore/Reinitialize with BiDi disabled
            transmitter->deinit();
            transmitter->init(original_config);

            // Feed standard packets
            for (int i = 0; i < 15; ++i) {
                uint8_t raw_payload[4] = { 0x03, 0x3F, 0x1A, 0x00 };
                raw_payload[3] = raw_payload[0] ^ raw_payload[1] ^ raw_payload[2];
                transmitter->sendPacket(raw_payload, sizeof(raw_payload));
                vTaskDelay(pdMS_TO_TICKS(150));
            }
        }
    }
    else if (scenario == 5) {
        // Single DCC Command to trigger the scope
        uint8_t raw_payload[4] = { 0x03, 0x3F, 0x1F, 0x00 }; // Address 3, Speed 30
        raw_payload[3] = raw_payload[0] ^ raw_payload[1] ^ raw_payload[2];
        transmitter->sendPacket(raw_payload, sizeof(raw_payload));
        // Add a small delay so telemetry has time to register
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI("DccTestRunner", "Autonomous test scenario %d completed successfully.", scenario);
    s_test_running = false;
    delete params;
    vTaskDelete(NULL);
}

esp_err_t WebServer::testPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* sc_item = cJSON_GetObjectItem(json, "scenario");
    if (!sc_item || !cJSON_IsNumber(sc_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid scenario ID");
        return ESP_FAIL;
    }

    int scenario = sc_item->valueint;
    cJSON_Delete(json);

    if (scenario < 1 || scenario > 5) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Scenario ID must be between 1 and 5");
        return ESP_FAIL;
    }

    bool expected = false;
    if (!s_test_running.compare_exchange_strong(expected, true)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "A test scenario is already active");
        return ESP_FAIL;
    }

    auto* params = new TestTaskParams();
    params->scenario_id = scenario;
    params->transmitter = self->m_transmitter;

    BaseType_t task_ret = xTaskCreatePinnedToCore(
        test_scenario_task,
        "dcc_test_task",
        3072,
        params,
        10, // Priority 10 is safe and won't starve high-priority system tasks
        NULL,
        1 // Pinned to Core 1 (isolated from Core 0 Wi-Fi and Web Server)
    );

    if (task_ret != pdPASS) {
        s_test_running = false;
        delete params;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to launch test scenario task");
        return ESP_FAIL;
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "message", "Test scenario successfully launched in background");
    char* resp_str = cJSON_PrintUnformatted(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

esp_err_t WebServer::decoderGetHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    // Set headers
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON object");
        return ESP_FAIL;
    }

    if (self->m_decoder == nullptr) {
        cJSON_AddBoolToObject(root, "active", false);
        cJSON_AddStringToObject(root, "status", "Not Integrated");
        cJSON_AddNumberToObject(root, "pin", -1);
        cJSON_AddNumberToObject(root, "success_count", 0);
        cJSON_AddNumberToObject(root, "error_count", 0);
        cJSON_AddNumberToObject(root, "idle_packet_count", 0);
        cJSON_AddNumberToObject(root, "cpu_load_core0", 0);
        cJSON_AddNumberToObject(root, "cpu_load_core1", 0);
        cJSON_AddArrayToObject(root, "packets");
    } else {
        bool signal_active = self->m_decoder->isSignalActive();
        cJSON_AddBoolToObject(root, "active", true);
        cJSON_AddStringToObject(root, "status", signal_active ? "Active" : "No Signal");
        cJSON_AddNumberToObject(root, "pin", self->m_decoder->getGpioNum());
        cJSON_AddNumberToObject(root, "success_count", self->m_decoder->getSuccessCount());
        cJSON_AddNumberToObject(root, "error_count", self->m_decoder->getErrorCount());
        cJSON_AddNumberToObject(root, "idle_packet_count", self->m_decoder->getIdlePacketCount());
        cJSON_AddNumberToObject(root, "cpu_load_core0", static_cast<int>(s_cpu_load_core0));
        cJSON_AddNumberToObject(root, "cpu_load_core1", static_cast<int>(s_cpu_load_core1));

        cJSON* packets_arr = cJSON_CreateArray();
        std::vector<dcc::rx::DecodedPacket> packets = self->m_decoder->getRecentPackets(10);
        
        for (const auto& packet : packets) {
            cJSON* p_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(p_obj, "timestamp", packet.timestamp);
            cJSON_AddBoolToObject(p_obj, "valid", packet.is_valid);
            cJSON_AddStringToObject(p_obj, "text", packet.human_readable.c_str());

            // Add raw hex data representation
            char hex_buf[64] = "";
            for (int i = 0; i < packet.length; ++i) {
                char byte_hex[8];
                std::sprintf(byte_hex, "0x%02X ", packet.payload[i]);
                std::strcat(hex_buf, byte_hex);
            }
            cJSON_AddStringToObject(p_obj, "hex", hex_buf);
            
            cJSON_AddItemToArray(packets_arr, p_obj);
        }
        cJSON_AddItemToObject(root, "packets", packets_arr);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to print JSON");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return res;
}

static void web_on_packet_transmitted(const uint8_t* payload, size_t length, void* arg) {
    auto* dec = static_cast<dcc::rx::DccDecoder*>(arg);
    bool is_valid = true;
    if (length >= 3) {
        uint8_t checksum = 0;
        for (size_t i = 0; i < length - 1; ++i) {
            checksum ^= payload[i];
        }
        is_valid = (checksum == payload[length - 1]);
    }
    dec->injectPacket(payload, length, is_valid);
}

esp_err_t WebServer::decoderTogglePostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* enabled_item = cJSON_GetObjectItem(json, "enabled");
    if (!enabled_item) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'enabled' parameter");
        return ESP_FAIL;
    }

    bool enabled = cJSON_IsTrue(enabled_item);
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Setting Decoder hardware state to %s", enabled ? "ENABLED" : "DISABLED");
    self->m_decoder->deinit();
    bool init_ok = false;
    if (enabled) {
        self->m_transmitter->registerCallback(nullptr, nullptr); // Unregister software loopback
        init_ok = self->m_decoder->init(self->m_decoder_pin);
        if (!init_ok) {
            ESP_LOGE(TAG, "Failed to initialize hardware DCC decoder on pin %d. Reverting to loopback.", self->m_decoder_pin);
            // Revert to software loopback
            self->m_decoder->init(-1);
            self->m_transmitter->registerCallback(web_on_packet_transmitted, self->m_decoder);
            enabled = false;
        }
    } else {
        init_ok = self->m_decoder->init(-1);
        self->m_transmitter->registerCallback(web_on_packet_transmitted, self->m_decoder); // Re-register software loopback
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddBoolToObject(resp, "enabled", enabled);
    cJSON_AddNumberToObject(resp, "pin", self->m_decoder->getGpioNum());
    char* resp_str = cJSON_PrintUnformatted(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

esp_err_t WebServer::triggerPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* enabled_item = cJSON_GetObjectItem(json, "enabled");
    if (!enabled_item) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'enabled' parameter");
        return ESP_FAIL;
    }

    bool enabled = cJSON_IsTrue(enabled_item);
    cJSON_Delete(json);

    self->m_transmitter->setScopeTriggerEnabled(enabled);
    ESP_LOGI(TAG, "GPIO4 Scope Trigger set to %s", enabled ? "ENABLED" : "DISABLED");

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddBoolToObject(resp, "enabled", enabled);
    char* resp_str = cJSON_PrintUnformatted(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

} // namespace web
} // namespace dcc
