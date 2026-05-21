/**
 * @file DccRmtTransmitter.hpp
 * @brief Standalone, modular ESP32 RMT DCC (Digital Command Control) Transmitter.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-20
 * 
 * This module is written for the ESP-IDF v5.x RMT (Remote Control) peripheral driver.
 * It is completely isolated from all external C++ templates, ZTL libraries, and
 * compile-time CMake defines, making it drop-in ready for any ESP32 project 
 * (including ESP32, ESP32-S3, ESP32-C3, etc.).
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#pragma once

#include <driver/rmt_tx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstdint>
#include <cstddef>

namespace dcc {
namespace rmt {

// =============================================================================
// Standard DCC Timing Constants (in Microseconds) based on NMRA S-9.1
// =============================================================================
#define DCC_TX_MIN_PREAMBLE_BITS    17
#define DCC_TX_MAX_PREAMBLE_BITS    30
#define DCC_TX_MIN_BIT_1_TIMING     56
#define DCC_TX_MAX_BIT_1_TIMING     60
#define DCC_TX_MIN_BIT_0_TIMING     97
#define DCC_TX_MAX_BIT_0_TIMING     114
#define DCC_TX_MIN_BIDI_BIT_TIMING  57
#define DCC_TX_MAX_BIDI_BIT_TIMING  61

/**
 * @brief Configuration structure for the DCC RMT Transmitter
 */
struct TransmitterConfig {
    int gpio_num;                     ///< GPIO Pin for the DCC output signal
    uint8_t num_preamble = 18;        ///< Number of preamble '1' bits [17, 30]
    bool enable_bidi = false;         ///< Toggle standard BiDi (Bidirectional) cutout
    uint8_t bidibit_duration = 60;    ///< Optional duration of BiDi cutout bit [57, 61] (in µs)
    uint8_t bit1_duration = 58;       ///< Duration of '1' bit [56, 60] (in µs)
    uint8_t bit0_duration = 100;      ///< Duration of '0' bit [97, 114] (in µs)
    
    /// Duration of end bit [0, 60] (in µs)
    /// Workaround for ESP-IDF RMT hardware issues (e.g. #13003)
    /// Typical setting is bit1_duration - 24 (e.g. 58 - 24 = 34)
    uint8_t endbit_duration = 34;

    struct {
        bool level0 = false;          ///< True if output signal is inverted (starts high vs starts low)
        bool zimo0 = true;            ///< Enable ZIMO 0 packet prefix behavior
    } flags;

    size_t queue_size = 8;            ///< Capacity of the FreeRTOS packet queue
};

/**
 * @brief Thread-safe, asynchronous C++ class to drive DCC signals using ESP32 RMT
 */
class DccRmtTransmitter {
public:
    /**
     * @brief Construct a new DccRmtTransmitter instance
     */
    DccRmtTransmitter();

    /**
     * @brief Destroy the DccRmtTransmitter instance (and clean up resources)
     */
    ~DccRmtTransmitter();

    // Prevent copying to avoid multiple tasks running on same RMT channel
    DccRmtTransmitter(const DccRmtTransmitter&) = delete;
    DccRmtTransmitter& operator=(const DccRmtTransmitter&) = delete;

    /**
     * @brief Initialize RMT peripheral on a specific GPIO pin and start the background task
     * 
     * @param config Configuration options (GPIO pin, timings, BiDi toggle, queue size, etc.)
     * @return true Initialization succeeded
     * @return false Initialization failed (out of memory, invalid parameters, or RMT channel busy)
     */
    bool init(const TransmitterConfig& config);

    /**
     * @brief Queue a raw DCC packet to be sent asynchronously to the track
     * 
     * @param payload Pointer to the raw bytes buffer (e.g. standard DCC packet [Addr, Data, Checksum])
     * @param length Number of bytes in the payload (typically between 3 and 6, max 18)
     * @param wait_ms Maximum time (in ms) to wait if the queue is full (default is non-blocking: 0ms)
     * @return true Packet was successfully queued for transmission
     * @return false Queue was full or transmitter is not initialized
     */
    bool sendPacket(const uint8_t* payload, size_t length, uint32_t wait_ms = 0);

    /**
     * @brief Stop the transmission, delete background tasks, and release RMT hardware channels
     */
    void deinit();

    /**
     * @brief Check if the transmitter is currently initialized and running
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get the current transmitter configuration
     */
    const TransmitterConfig& getConfig() const { return m_config; }

private:
    /// Maximum size of a single DCC packet in bytes (based on DCC standards)
    static constexpr size_t MAX_DCC_PACKET_SIZE = 18;

    /// Representation of a queued DCC packet
    struct QueuedPacket {
        uint8_t data[MAX_DCC_PACKET_SIZE];
        uint8_t length;
    };

    /**
     * @brief Static entrypoint for the background FreeRTOS task
     */
    static void txTask(void* pvParameters);

    /**
     * @brief Background loop which feeds the RMT hardware continuously
     */
    void txTaskLoop();

    // Transmitter State Variables
    TransmitterConfig m_config;
    rmt_channel_handle_t m_rmt_channel;
    rmt_encoder_handle_t m_dcc_encoder;
    QueueHandle_t m_packet_queue;
    TaskHandle_t m_task_handle;
    bool m_initialized;
};

} // namespace rmt
} // namespace dcc
