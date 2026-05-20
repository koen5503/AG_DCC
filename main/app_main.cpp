/**
 * @file app_main.cpp
 * @brief Autonomous instrumentation and test runner for DCC RMT Transmitter.
 * @author Antigravity Refactoring
 * @date 2026-05-20
 * 
 * This test runner is compatible with both standard ESP32 and ESP32-C3 chips.
 * It provides a complex serial configuration for USB-Serial-JTAG and UART0,
 * detailed oscilloscope testing scenarios, and high-safety practices.
 * 
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "DccRmtTransmitter.hpp"
#include "WifiManager.hpp"
#include "WebServer.hpp"
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_vfs_dev.h>
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <cstring>

static const char* TAG = "DccAppMain";

// =============================================================================
// Hardware Mapping Config
// =============================================================================
#if CONFIG_IDF_TARGET_ESP32C3
  // ESP32-C3 default RMT pin
  #define DCC_GPIO_PIN  GPIO_NUM_8
#elif CONFIG_IDF_TARGET_ESP32S3
  // ESP32-S3 default RMT pin
  #define DCC_GPIO_PIN  GPIO_NUM_18
#else
  // Standard ESP32 default RMT pin
  #define DCC_GPIO_PIN  GPIO_NUM_21
#endif

// =============================================================================
// Instrumentation & Serial Logging Configuration
// =============================================================================
/**
 * @brief Configures serial logging properly for standard UART or ESP32-C3 USB-Serial-JTAG.
 * 
 * On the ESP32-C3, logging can be routed either through:
 * 1. The hardware UART0 (pins TX=GPIO21, RX=GPIO20 on typical modules).
 * 2. The integrated USB-Serial-JTAG controller (virtual COM port on native USB pins D-/D+ GPIO18/GPIO19).
 * 
 * To ensure logs are printed instantly without buffering or dropouts, we:
 * - Disable buffering on stdout/stderr.
 * - Configure the virtual terminal console.
 * - Force flushing of the serial queues.
 */
void configure_serial_logging() {
    // 1. Disable buffering on stdout/stderr to prevent logging latency or text truncations
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    // Logging is configured via native USB-Serial-JTAG
    // In this mode, no physical UART hardware initialization is needed,
    // as it is handled by the ESP32-C3 ROM and driver.
    // Initialize USB-Serial-JTAG driver for virtual terminal capabilities
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
#else
    // Logging is configured via physical UART0
    uart_vfs_dev_port_set_rx_line_endings(0, ESP_LINE_ENDINGS_CRLF);
    uart_vfs_dev_port_set_tx_line_endings(0, ESP_LINE_ENDINGS_CRLF);
#endif

    // Flush any leftover startup bootloader messages
    fflush(stdout);
    fsync(fileno(stdout));

    ESP_LOGI(TAG, "Serial console logging successfully configured!");
#if CONFIG_IDF_TARGET_ESP32C3
    ESP_LOGI(TAG, "Running on target: ESP32-C3 (RISC-V)");
#elif CONFIG_IDF_TARGET_ESP32S3
    ESP_LOGI(TAG, "Running on target: ESP32-S3 (Xtensa Dual-Core)");
#else
    ESP_LOGI(TAG, "Running on target: ESP32 Standard (Xtensa)");
#endif
}

// =============================================================================
// Utility Functions: DCC Checksum Generator
// =============================================================================
/**
 * @brief Calculate the standard DCC XOR Checksum
 * 
 * According to NMRA S-9.2, every DCC packet must end with an error detection byte.
 * This byte is calculated as the bitwise XOR of all preceding bytes.
 */
uint8_t calculate_dcc_checksum(const uint8_t* data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; ++i) {
        checksum ^= data[i];
    }
    return checksum;
}

// =============================================================================
// Oscilloscope Probe Safety Warning
// =============================================================================
void print_oscilloscope_safety_warning() {
    printf("\n");
    ESP_LOGW(TAG, "===============================================================");
    ESP_LOGW(TAG, "                 OSCILLOSCOPE PROBING WARNING                  ");
    ESP_LOGW(TAG, "===============================================================");
    ESP_LOGI(TAG, "1. PROBE DIRECTLY ON THE ESP32 GPIO PIN (%d):", DCC_GPIO_PIN);
    ESP_LOGI(TAG, "   Connect your scope's Channel 1 probe tip to GPIO pin %d.", DCC_GPIO_PIN);
    ESP_LOGI(TAG, "   Connect the ground clip to the ESP32 GND pin.");
    ESP_LOGI(TAG, "   At this point, you will see clean 3.3V logic-level signals.");
    ESP_LOGW(TAG, "2. DO NOT CONNECT YOUR SCOPE DIRECTLY TO THE RAILWAY TRACKS!");
    ESP_LOGW(TAG, "   The physical DCC signal on the track is ±15V to ±22V AC.");
    ESP_LOGW(TAG, "   Connecting standard oscilloscope ground clips to H-bridge rails");
    ESP_LOGW(TAG, "   will cause a SHORT-CIRCUIT and may destroy the scope or ESP32!");
    ESP_LOGW(TAG, "   If you want to probe the tracks directly, you MUST use a");
    ESP_LOGW(TAG, "   DIFFERENTIAL PROBE or run your oscilloscope fully isolated.");
    ESP_LOGW(TAG, "===============================================================\n");
}

// =============================================================================
// Application Entrypoint
// =============================================================================
extern "C" void app_main() {
    // Initialize and optimize console output
    configure_serial_logging();
    
    // Print oscilloscope setup instructions
    print_oscilloscope_safety_warning();

    // 1. Initialize Wi-Fi and Non-Volatile Storage (NVS)
    ESP_LOGI(TAG, "Initializing Wi-Fi Manager...");
    static dcc::wifi::WifiManager wifi_manager;
    wifi_manager.init();

    // 2. Create and configure our standalone transmitter
    ESP_LOGI(TAG, "Initializing DCC RMT Transmitter...");
    static dcc::rmt::DccRmtTransmitter transmitter;
    dcc::rmt::TransmitterConfig config;
    
    config.gpio_num = DCC_GPIO_PIN;
    config.num_preamble = 18;        // NMRA standard minimum is 14; 18 is highly reliable
    config.enable_bidi = false;      // Start with BiDi cutout disabled
    config.bidibit_duration = 60;    // 60 µs per BiDi cutout bit (4 bits total = 240 µs)
    config.bit1_duration = 58;       // 58 µs high / 58 µs low
    config.bit0_duration = 100;      // 100 µs high / 100 µs low
    config.endbit_duration = 34;     // Workaround for timing issues (58 - 24)
    config.flags.level0 = false;     // Standard polarity (starts low)
    config.flags.zimo0 = true;       // Enable ZIMO 0 packet prefix behavior

    // Initialize RMT driver and start background thread
    if (!transmitter.init(config)) {
        ESP_LOGE(TAG, "Failed to initialize DCC RMT Transmitter. Aborting.");
        return;
    }

    // 3. Initialize and start Embedded Web Server
    ESP_LOGI(TAG, "Initializing Embedded Web Server...");
    static dcc::web::WebServer web_server(&transmitter, &wifi_manager);
    if (!web_server.start()) {
        ESP_LOGE(TAG, "Failed to start Web Server. Aborting.");
        return;
    }

    ESP_LOGI(TAG, "===============================================================");
    ESP_LOGI(TAG, "          DCC RMT WEB COMMAND CENTER RUNNING!");
    ESP_LOGI(TAG, "===============================================================");
    if (wifi_manager.isApMode()) {
        ESP_LOGI(TAG, "  Mode:   Access Point (AP)");
        ESP_LOGI(TAG, "  SSID:   ESP32-DCC-Controller-[MAC]");
        ESP_LOGI(TAG, "  Pass:   dcccontrol");
    } else {
        ESP_LOGI(TAG, "  Mode:   Station (STA)");
        ESP_LOGI(TAG, "  SSID:   %s", wifi_manager.getConnectedSsid().c_str());
    }
    ESP_LOGI(TAG, "  URL:    http://%s/", wifi_manager.getIpAddress().c_str());
    ESP_LOGI(TAG, "===============================================================");

    // Heartbeat loop
    while (true) {
        ESP_LOGI(TAG, "System Heartbeat | IP: %s | Mode: %s", 
                 wifi_manager.getIpAddress().c_str(),
                 wifi_manager.isApMode() ? "APSTA" : "STA");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
