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
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_vfs_dev.h>
#include <driver/uart.h>
#include <cstring>

static const char* TAG = "DccAppMain";

// =============================================================================
// Hardware Mapping Config
// =============================================================================
#if CONFIG_IDF_TARGET_ESP32C3
  // ESP32-C3 default RMT pin (Change this according to your board layout)
  #define DCC_GPIO_PIN  GPIO_NUM_8
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
    esp_vfs_dev_usb_serial_jtag_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
    esp_vfs_dev_usb_serial_jtag_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
#else
    // Logging is configured via physical UART0
    esp_vfs_dev_uart_port_set_rx_line_endings(0, ESP_LINE_ENDINGS_CRLF);
    esp_vfs_dev_uart_port_set_tx_line_endings(0, ESP_LINE_ENDINGS_CRLF);
#endif

    // Flush any leftover startup bootloader messages
    fflush(stdout);
    fsync(fileno(stdout));

    ESP_LOGI(TAG, "Serial console logging successfully configured!");
#if CONFIG_IDF_TARGET_ESP32C3
    ESP_LOGI(TAG, "Running on target: ESP32-C3 (RISC-V)");
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

    // Create and configure our standalone transmitter
    dcc::rmt::DccRmtTransmitter transmitter;
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

    ESP_LOGI(TAG, "Testing scenarios started. Observe GPIO %d on your oscilloscope.", DCC_GPIO_PIN);

    uint8_t speed_step = 0;
    bool direction_forward = true;

    while (true) {
        // ---------------------------------------------------------------------
        // TEST SCENARIO 1: Continuous Idle Packets (Baseline)
        // ---------------------------------------------------------------------
        ESP_LOGI(TAG, "[SCENARIO 1] Transmitting continuous DCC Idle Packets for 5 seconds...");
        ESP_LOGI(TAG, " -> Waveform expected: continuous [0xFF, 0x00, 0xFF] trains (with 18-bit preambles).");
        ESP_LOGI(TAG, " -> Trigger scope on negative edge of GPIO %d.", DCC_GPIO_PIN);
        
        // The FreeRTOS task handles this automatically when we send nothing to the queue.
        vTaskDelay(pdMS_TO_TICKS(5000));

        // ---------------------------------------------------------------------
        // TEST SCENARIO 2: Locomotive Speed Packets (Varying Speed/Direction)
        // ---------------------------------------------------------------------
        ESP_LOGI(TAG, "[SCENARIO 2] Transmitting varying Locomotive Speed Commands for 5 seconds...");
        for (int i = 0; i < 50; ++i) {
            // Speed packet payload for Locomotive Address 3 (Standard default DCC address)
            // Address = 0x03
            // Speed Instruction byte format: 0b01DCSSSS (D=Direction, C=Speed Step format, SSSS=Speed)
            uint8_t speed_byte = 0b01000000; // Base speed command
            if (direction_forward) {
                speed_byte |= 0b00100000; // Forward direction bit
            }
            
            // Map 28-step speed steps into standard DCC byte format
            speed_byte |= (speed_step & 0x0F); 
            
            uint8_t raw_payload[4];
            raw_payload[0] = 0x03;                       // Address
            raw_payload[1] = 0x3F;                       // Instruction Group (128 Speed Step prefix or 28 Step toggle)
            raw_payload[2] = speed_byte;                 // Speed step data
            raw_payload[3] = calculate_dcc_checksum(raw_payload, 3); // XOR Error Byte

            ESP_LOGI(TAG, "Sending speed packet: [0x%02X, 0x%02X, 0x%02X] Checksum: 0x%02X (Dir: %s, Speed: %d/28)",
                     raw_payload[0], raw_payload[1], raw_payload[2], raw_payload[3],
                     direction_forward ? "FWD" : "REV", speed_step);

            transmitter.sendPacket(raw_payload, sizeof(raw_payload));

            // Increment speed cycle
            speed_step++;
            if (speed_step > 28) {
                speed_step = 1;
                direction_forward = !direction_forward;
            }

            vTaskDelay(pdMS_TO_TICKS(100)); // Feed a packet every 100ms
        }

        // ---------------------------------------------------------------------
        // TEST SCENARIO 3: Accessory / Switch Commands
        // ---------------------------------------------------------------------
        ESP_LOGI(TAG, "[SCENARIO 3] Transmitting Accessory (Switch) Commands for 3 seconds...");
        for (int i = 0; i < 6; ++i) {
            // Accessory decoder packet format: [10AAAAAA, 1AAACDDD]
            // Turn switch on and off (CDDD toggles)
            uint8_t raw_payload[3];
            raw_payload[0] = 0x80; // Accessory base address (binary 10000000)
            raw_payload[1] = (i % 2 == 0) ? 0xF8 : 0xF0; // Alternates switch state
            raw_payload[2] = calculate_dcc_checksum(raw_payload, 2);

            ESP_LOGI(TAG, "Sending accessory packet: [0x%02X, 0x%02X] Checksum: 0x%02X (Switch: %s)",
                     raw_payload[0], raw_payload[1], raw_payload[2],
                     (i % 2 == 0) ? "ON" : "OFF");

            transmitter.sendPacket(raw_payload, sizeof(raw_payload));
            vTaskDelay(pdMS_TO_TICKS(500)); // Every 500ms
        }

        // ---------------------------------------------------------------------
        // TEST SCENARIO 4: BiDi (Bidirectional) Cutout Toggle
        // ---------------------------------------------------------------------
        ESP_LOGI(TAG, "[SCENARIO 4] Toggling BiDi Cutout to show timing changes on the oscilloscope...");
        
        // Reinitialize transmitter with BiDi ENABLED
        ESP_LOGI(TAG, " -> Re-initializing transmitter with BiDi cutout ENABLED...");
        config.enable_bidi = true;
        transmitter.init(config);
        
        // Feed locomotive speed packet to verify waveforms
        for (int i = 0; i < 20; ++i) {
            uint8_t raw_payload[4] = { 0x03, 0x3F, 0x1A, 0x00 };
            raw_payload[3] = calculate_dcc_checksum(raw_payload, 3);
            transmitter.sendPacket(raw_payload, sizeof(raw_payload));
            
            ESP_LOGI(TAG, "[BiDi ENABLED] Transmitting active packet. Scope will show a 240 µs flat/zero cutout zone after the end-bit.");
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        // Reinitialize transmitter with BiDi DISABLED
        ESP_LOGI(TAG, " -> Re-initializing transmitter with BiDi cutout DISABLED...");
        config.enable_bidi = false;
        transmitter.init(config);
        
        for (int i = 0; i < 20; ++i) {
            uint8_t raw_payload[4] = { 0x03, 0x3F, 0x1A, 0x00 };
            raw_payload[3] = calculate_dcc_checksum(raw_payload, 3);
            transmitter.sendPacket(raw_payload, sizeof(raw_payload));
            
            ESP_LOGI(TAG, "[BiDi DISABLED] Transmitting active packet. Scope will show normal end-bit immediately transitioning to idle/preamble.");
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }
}
