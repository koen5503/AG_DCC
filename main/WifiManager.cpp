/**
 * @file WifiManager.cpp
 * @brief Implementation of the Wi-Fi Provisioning and AP Manager.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-20
 * 
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "WifiManager.hpp"
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_mac.h>
#include <esp_event.h>
#include <esp_vfs.h>
#include <cstring>
#include <sstream>

static const char* TAG = "WifiMgr";

// Event Group bits for Station connection
static EventGroupHandle_t s_wifi_event_group = nullptr;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

namespace dcc {
namespace wifi {

WifiManager::WifiManager()
    : m_connected(false),
      m_ap_mode(false),
      m_ip_address(""),
      m_connected_ssid("") {
}

static void reboot_task(void* pvParameters) {
    uint32_t delay = (uint32_t)(uintptr_t)pvParameters;
    ESP_LOGI("WifiMgr", "Rebooting system in %d ms...", delay);
    vTaskDelay(pdMS_TO_TICKS(delay));
    esp_restart();
}

void WifiManager::scheduleReboot(uint32_t delay_ms) {
    xTaskCreate(reboot_task, "reboot_task", 2048, (void*)(uintptr_t)delay_ms, 1, nullptr);
}

void WifiManager::wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* self = static_cast<WifiManager*>(arg);
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!self->m_connected && !self->m_ap_mode) {
            ESP_LOGW(TAG, "Failed to connect. Retrying...");
            esp_wifi_connect();
            // In a production/robust STA-only manager we would count retries, 
            // but we rely on a 10s wait timeout in tryConnectStation before setting the FAIL bit.
        } else if (self->m_connected) {
            ESP_LOGW(TAG, "Connection lost! Attempting to reconnect...");
            self->m_connected = false;
            self->m_ip_address = "";
            esp_wifi_connect();
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        char ip_str[32];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        
        self->m_ip_address = ip_str;
        self->m_connected = true;
        ESP_LOGI(TAG, "Got IP address: %s", ip_str);
        
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

void WifiManager::init() {
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize TCP/IP and Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi_event_group = xEventGroupCreate();

    // 3. Load Wi-Fi credentials from NVS
    std::string ssid = "";
    std::string password = "";
    readCredentials(ssid, password);

    // 4. Try to connect to saved Station credentials (if any)
    if (!ssid.empty()) {
        ESP_LOGI(TAG, "Saved credentials found. Attempting connection to SSID: %s", ssid.c_str());
        if (tryConnectStation(ssid, password)) {
            ESP_LOGI(TAG, "Successfully connected to Wi-Fi network in Station mode.");
            return;
        }
        ESP_LOGW(TAG, "Connection to SSID %s failed or timed out. Falling back to AP Mode.", ssid.c_str());
    } else {
        ESP_LOGI(TAG, "No saved credentials found. Starting in Access Point Mode.");
    }

    // 5. Fallback: Start secure AP Mode
    startAccessPoint();
}

void WifiManager::readCredentials(std::string& ssid, std::string& password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_creds", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return;
    }

    char ssid_buf[64] = {0};
    char pass_buf[64] = {0};
    size_t ssid_size = sizeof(ssid_buf);
    size_t pass_size = sizeof(pass_buf);

    if (nvs_get_str(nvs_handle, "ssid", ssid_buf, &ssid_size) == ESP_OK) {
        ssid = ssid_buf;
    }
    if (nvs_get_str(nvs_handle, "password", pass_buf, &pass_size) == ESP_OK) {
        password = pass_buf;
    }

    nvs_close(nvs_handle);
}

bool WifiManager::saveCredentials(const std::string& ssid, const std::string& password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_creds", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(nvs_handle, "ssid", ssid.c_str());
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, "password", password.c_str());
    }
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write Wi-Fi credentials to NVS: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Wi-Fi credentials saved to NVS successfully.");
    return true;
}

void WifiManager::clearCredentials() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_creds", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Cleared Wi-Fi credentials from NVS.");
    }
}

bool WifiManager::tryConnectStation(const std::string& ssid, const std::string& password) {
    m_ap_mode = false;
    m_connected = false;
    m_connected_ssid = ssid;

    // Create netif station
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();

    // Start Wi-Fi subsystem
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register Wi-Fi and IP events
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifiEventHandler,
                                                        this,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifiEventHandler,
                                                        this,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    std::strncpy((char*)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid));
    std::strncpy((char*)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for IP address (timeout: 10s)...");
    
    // Wait for the connection bit (or time out after 10s)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP successfully! IP: %s", m_ip_address.c_str());
        return true;
    } 
    
    // Connection timed out or failed. Tear down Station mode interfaces so we can boot in AP mode.
    ESP_LOGW(TAG, "Connection failed or timed out. Tearing down Station netif.");
    esp_wifi_stop();
    esp_wifi_deinit();
    
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    esp_netif_destroy(sta_netif);
    
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    return false;
}

void WifiManager::startAccessPoint() {
    m_ap_mode = true;
    m_connected = false;
    m_ip_address = "192.168.4.1";
    m_connected_ssid = "";

    // 1. Create default AP netif
    esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
    (void)ap_netif;
    // Also create station netif so we can scan inside AP mode concurrently (APSTA Mode)
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
    (void)sta_netif;

    // 2. Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers (useful if anyone connects/disconnects, and for station scanning)
    esp_event_handler_instance_t instance_any_id;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifiEventHandler,
                                                        this,
                                                        &instance_any_id));

    // Get MAC address to append to SSID for uniqueness
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_ssid[32];
    std::snprintf(ap_ssid, sizeof(ap_ssid), "ESP32-DCC-Controller-%02X%02X", mac[4], mac[5]);

    wifi_config_t wifi_config = {};
    std::strncpy((char*)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid));
    std::strncpy((char*)wifi_config.ap.password, "dcccontrol", sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = std::strlen(ap_ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.channel = 1;

    // Use APSTA mode so station scanning can occur while hosting the SoftAP!
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP Started! SSID: %s | Pass: dcccontrol | IP: 192.168.4.1", ap_ssid);
}

std::vector<ScannedNetwork> WifiManager::scanNetworks() {
    std::vector<ScannedNetwork> networks;

    ESP_LOGI(TAG, "Scanning for surrounding Wi-Fi networks...");

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = true;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    // Trigger active scan and block until complete
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi scan: %s", esp_err_to_name(err));
        return networks;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        ESP_LOGI(TAG, "No Wi-Fi networks found in range.");
        return networks;
    }

    auto* ap_list = (wifi_ap_record_t*)std::malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        ESP_LOGE(TAG, "Failed to allocate memory for Wi-Fi scan records.");
        return networks;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));

    networks.reserve(ap_count);
    for (int i = 0; i < ap_count; ++i) {
        // Skip empty SSIDs
        if (std::strlen((char*)ap_list[i].ssid) == 0) {
            continue;
        }

        // Avoid adding duplicates
        bool duplicate = false;
        for (const auto& net : networks) {
            if (net.ssid == (char*)ap_list[i].ssid) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        ScannedNetwork net;
        net.ssid = (char*)ap_list[i].ssid;
        net.rssi = ap_list[i].rssi;
        net.channel = ap_list[i].primary;
        net.auth_mode = ap_list[i].authmode;
        networks.push_back(net);
    }

    std::free(ap_list);
    ESP_LOGI(TAG, "Wi-Fi scan completed. Found %d unique networks.", networks.size());
    return networks;
}

std::string WifiManager::scanNetworksJson() {
    auto networks = scanNetworks();
    
    std::stringstream json;
    json << "[";
    for (size_t i = 0; i < networks.size(); ++i) {
        json << "{";
        json << "\"ssid\":\"" << networks[i].ssid << "\",";
        json << "\"rssi\":" << networks[i].rssi << ",";
        json << "\"channel\":" << networks[i].channel << ",";
        json << "\"secure\":" << (networks[i].auth_mode != WIFI_AUTH_OPEN ? "true" : "false");
        json << "}";
        if (i < networks.size() - 1) {
            json << ",";
        }
    }
    json << "]";
    return json.str();
}

} // namespace wifi
} // namespace dcc
