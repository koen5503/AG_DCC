/**
 * @file DccRmtTransmitter.hpp
 * @brief Standalone, modular ESP32 Low-Level GDMA DCC Transmitter.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-23
 * 
 * Bypasses high-level ESP-IDF v5 RMT queues and software ISRs.
 * Linked DMA descriptors continuously loop DCC packets in hardware.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#pragma once

#include <driver/rmt_tx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include <cstdint>
#include <cstddef>
#include "esp_private/gdma.h"
#include "soc/lldesc.h"

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
    uint8_t endbit_duration = 58;     ///< Symmetrical packet end-bit duration (in µs)

    struct {
        bool level0 = false;          ///< True if output signal is inverted (starts high vs starts low)
        bool zimo0 = true;            ///< Enable ZIMO 0 packet prefix behavior
    } flags;

    size_t queue_size = 8;            ///< Legacy parameter (retained for backward compatibility)
};

/**
 * @brief Thread-safe, hardware-driven low-level DMA DCC Transmitter
 */
class DccRmtTransmitter {
public:
    DccRmtTransmitter();
    ~DccRmtTransmitter();

    // Prevent copying
    DccRmtTransmitter(const DccRmtTransmitter&) = delete;
    DccRmtTransmitter& operator=(const DccRmtTransmitter&) = delete;

    /**
     * @brief Initialize low-level GDMA linked ring buffer and hook to RMT channel.
     */
    bool init(const TransmitterConfig& config);

    /**
     * @brief Inject command packet asynchronously into the active circular hardware linked-list.
     */
    bool sendPacket(const uint8_t* payload, size_t length, uint32_t wait_ms = 100);

    /**
     * @brief Stop the GDMA transfer, release RMT and DMA hardware channel resources.
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

    // Callback definition for software-based loopback decoding
    typedef void (*PacketSentCallback)(const uint8_t* payload, size_t length, void* arg);

    /**
     * @brief Register a callback to be invoked whenever a DCC packet is transmitted.
     *        This is used for high-reliability software loopback telemetry under RTOS.
     */
    void registerCallback(PacketSentCallback callback, void* arg);

    /**
     * @brief Enable or disable the oscilloscope trigger output on GPIO4
     */
    void setScopeTriggerEnabled(bool enabled) { m_scope_trigger_enabled = enabled; }

    /**
     * @brief Check if the oscilloscope trigger output on GPIO4 is enabled
     */
    bool isScopeTriggerEnabled() const { return m_scope_trigger_enabled; }

private:
    /**
     * @brief Formats a raw DCC payload into RMT timing symbols in memory.
     */
    void formatPacketToRmtSymbols(const uint8_t* payload, size_t length, rmt_symbol_word_t* rmt_buffer, size_t* out_num_symbols);

    /**
     * @brief Software timer callback to restore the Idle Packet in the command descriptor buffer.
     */
    static void cleanup_timer_callback(TimerHandle_t xTimer);

    // Transmitter State Variables
    TransmitterConfig m_config;
    rmt_channel_handle_t m_rmt_channel;
    
    // Low-Level GDMA Resources
    gdma_channel_handle_t m_dma_chan;
    lldesc_t* m_dma_descriptors;
    rmt_symbol_word_t* m_dma_buffers[3];
    rmt_symbol_word_t* m_dma_idle_buffer;
    size_t m_idle_symbol_count;
    
    volatile int m_current_command_desc_idx;
    volatile int m_write_idx;
    TimerHandle_t m_cleanup_timer;
    SemaphoreHandle_t m_mutex;

    PacketSentCallback m_callback = nullptr;
    void* m_callback_arg = nullptr;
    bool m_scope_trigger_enabled;
    bool m_initialized;
};

} // namespace rmt
} // namespace dcc
