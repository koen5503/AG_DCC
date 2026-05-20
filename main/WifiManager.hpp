/**
 * @file WifiManager.hpp
 * @brief Standalone C++ Wi-Fi Provisioning and Access Point Manager.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-20
 * 
 * Manages ESP32 Non-Volatile Storage (NVS) for Wi-Fi credentials,
 * handles Station connection attempts, Access Point (AP) fallbacks, 
 * and scanning of surrounding networks.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#pragma once

#include <esp_err.h>
#include <esp_wifi.h>
#include <string>
#include <vector>

namespace dcc {
namespace wifi {

struct ScannedNetwork {
    std::string ssid;
    int rssi;
    int channel;
    wifi_auth_mode_t auth_mode;
};

class WifiManager {
public:
    WifiManager();
    ~WifiManager() = default;

    // Prevent copying
    WifiManager(const WifiManager&) = delete;
    WifiManager& operator=(const WifiManager&) = delete;

    /**
     * @brief Initialize NVS, load saved credentials, and connect or boot AP.
     */
    void init();

    /**
     * @brief Scan nearby Wi-Fi networks and return a structured vector.
     */
    std::vector<ScannedNetwork> scanNetworks();

    /**
     * @brief Scan nearby networks and return a JSON-formatted string.
     */
    std::string scanNetworksJson();

    /**
     * @brief Save SSID and password into ESP32 NVS.
     * @return true if saved successfully, false otherwise.
     */
    bool saveCredentials(const std::string& ssid, const std::string& password);

    /**
     * @brief Clear saved Wi-Fi credentials in NVS.
     */
    void clearCredentials();

    /**
     * @brief Check if currently connected to a home router in Station mode.
     */
    bool isConnected() const { return m_connected; }

    /**
     * @brief Check if currently running as an Access Point (AP).
     */
    bool isApMode() const { return m_ap_mode; }

    /**
     * @brief Get the current IP address string (either STA or AP default).
     */
    std::string getIpAddress() const { return m_ip_address; }

    /**
     * @brief Get the SSID of the currently connected network (in STA mode).
     */
    std::string getConnectedSsid() const { return m_connected_ssid; }

    /**
     * @brief Schedule a system reboot after a specific delay in milliseconds.
     */
    void scheduleReboot(uint32_t delay_ms);

private:
    // Wi-Fi Event Handler static wrapper
    static void wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    void startAccessPoint();
    bool tryConnectStation(const std::string& ssid, const std::string& password);
    void readCredentials(std::string& ssid, std::string& password);

    bool m_connected;
    bool m_ap_mode;
    std::string m_ip_address;
    std::string m_connected_ssid;
};

} // namespace wifi
} // namespace dcc
