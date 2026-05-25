/**
 * @file DccDecoder.hpp
 * @brief Hardware DCC Receiver and Decoder utilizing ESP32 GPIO edge interrupts.
 * @author Antigravity Refactoring
 * @date 2026-05-21
 * 
 * Measures edge transition intervals in microseconds on a GPIO pin,
 * classifies them into DCC "0" and "1" bits, reconstructs packets
 * according to NMRA S-9.1 / S-9.2, and exposes the decoded instructions.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#pragma once

#include <driver/gpio.h>
#include <driver/rmt_rx.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string>
#include <vector>
#include <atomic>

namespace dcc {
namespace rx {

/**
 * @brief Struct to represent a completed, decoded DCC packet.
 */
struct DecodedPacket {
    uint8_t payload[8];             ///< Raw packet bytes
    uint8_t length;                 ///< Number of bytes in packet (typically 3 to 6)
    uint64_t timestamp;             ///< Timestamp of reception (microseconds since boot)
    bool is_valid;                  ///< True if checksum passed
    std::string human_readable;     ///< Translated human description of the packet command
};


/**
 * @brief Queue message to pass RMT symbol buffers from ISR to background task.
 */
struct SymbolMsg {
    const rmt_symbol_word_t* symbols;
    uint32_t count;
    bool is_last;
};



/**
 * @brief Hardware-timed DCC Decoder module.
 */
class DccDecoder {
public:
    DccDecoder();
    ~DccDecoder();

    // Prevent copying
    DccDecoder(const DccDecoder&) = delete;
    DccDecoder& operator=(const DccDecoder&) = delete;

    /**
     * @brief Initialize the DCC Decoder on the specified GPIO pin.
     * @param gpio_num The input GPIO pin connected to the DCC signal.
     * @return true if successfully initialized, false otherwise.
     */
    bool init(int gpio_num);

    /**
     * @brief Deinitialize and clean up all interrupts and tasks.
     */
    void deinit();

    /**
     * @brief Get the total number of successfully decoded packets.
     */
    uint32_t getSuccessCount() const { return m_success_count.load(); }

    /**
     * @brief Get the total number of checksum errors.
     */
    uint32_t getErrorCount() const { return m_error_count.load(); }

    /**
     * @brief Get the total number of successfully decoded idle packets.
     */
    uint32_t getIdlePacketCount() const { return m_idle_packet_count.load(); }

    /**
     * @brief Get the current signal status (active/idle).
     * @return true if a valid DCC signal has been detected within the last 500ms.
     */
    bool isSignalActive() const;

    /**
     * @brief Get the configured GPIO pin.
     */
    int getGpioNum() const { return m_gpio_num; }

    /**
     * @brief Retrieve the list of recently decoded packets.
     * @param limit The maximum number of packets to retrieve.
     * @return Vector of decoded packets.
     */
    std::vector<DecodedPacket> getRecentPackets(size_t limit = 10);

    /**
     * @brief Inject a packet directly into the decoder history (software loopback).
     * @param payload Pointer to raw bytes.
     * @param length Number of bytes.
     * @param is_valid Whether the packet is valid.
     */
    void injectPacket(const uint8_t* payload, size_t length, bool is_valid = true);

    /**
     * @brief Translate a raw DCC packet payload into a human-readable text string.
     * @param payload Pointer to raw bytes.
     * @param length Number of bytes.
     * @return A descriptive std::string.
     */
    static std::string parsePacketToHuman(const uint8_t* payload, size_t length);

private:
    static bool IRAM_ATTR on_recv_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* edata, void* user_ctx);

    // Main background processor task
    static void decoderTaskWrapper(void* arg);
    void runDecoderTask();

    // Parse timing durations into bits and reconstruct packets
    enum HalfState {
        STATE_EXPECTING_FIRST_HALF,
        STATE_EXPECTING_SECOND_HALF
    };
    enum HalfType {
        HALF_INVALID,
        HALF_ONE,
        HALF_ZERO
    };

    void processSymbols(const rmt_symbol_word_t* symbols, size_t count);
    void processHalfCycle(uint32_t duration);
    void processBit(bool bit);
    void registerDecodedPacket(const uint8_t* payload, size_t length, bool is_valid);

    int m_gpio_num;
    bool m_initialized;

    // RMT and Task handles
    rmt_channel_handle_t m_rx_channel;
    QueueHandle_t m_symbol_ready_queue;
    TaskHandle_t m_task_handle;

    // Ping-pong double buffers in internal RAM (aligned for cache/DMA safety)
    alignas(32) rmt_symbol_word_t m_rx_buffer[2][1024];
    volatile uint8_t m_active_buffer;

    HalfState m_half_state;
    HalfType m_first_half_type;
    uint64_t m_last_edge_time;

    // NMRA Packet State Machine
    enum ParserState {
        STATE_PREAMBLE,
        STATE_BYTE_READ,
        STATE_SEPARATOR
    };
    ParserState m_parser_state;
    uint32_t m_preamble_count;
    uint32_t m_consecutive_ones;
    uint8_t m_current_byte;
    uint32_t m_bit_count;
    uint8_t m_packet_buffer[8];
    uint32_t m_packet_length;

    // Stats (atomic for thread safety)
    std::atomic<uint32_t> m_success_count;
    std::atomic<uint32_t> m_error_count;
    std::atomic<uint32_t> m_idle_packet_count;
    std::atomic<uint64_t> m_last_valid_packet_time;

    // Thread-safe history ring buffer
    mutable SemaphoreHandle_t m_mutex;
    static constexpr size_t MAX_HISTORY = 15;
    DecodedPacket m_history[MAX_HISTORY];
    size_t m_history_head;
    size_t m_history_count;
};

} // namespace rx
} // namespace dcc
