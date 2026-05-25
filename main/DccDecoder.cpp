/**
 * @file DccDecoder.cpp
 * @brief Hardware DCC Receiver and Decoder utilizing the ESP32 RMT and DMA peripherals.
 * @author Antigravity Refactoring
 * @date 2026-05-21
 * 
 * Measures edge transition intervals in microseconds on a GPIO pin,
 * classifies them into DCC "0" and "1" bits, reconstructs packets
 * according to NMRA S-9.1 / S-9.2, and exposes the decoded instructions.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "DccDecoder.hpp"
#include <esp_log.h>
#include <cstring>

static const char* TAG = "DccDecoder";

namespace dcc {
namespace rx {

DccDecoder::DccDecoder()
    : m_gpio_num(-1),
      m_initialized(false),
      m_rx_channel(nullptr),
      m_symbol_ready_queue(nullptr),
      m_task_handle(nullptr),
      m_active_buffer(0),
      m_half_state(STATE_EXPECTING_FIRST_HALF),
      m_first_half_type(HALF_INVALID),
      m_last_edge_time(0),
      m_parser_state(STATE_PREAMBLE),
      m_preamble_count(0),
      m_consecutive_ones(0),
      m_current_byte(0),
      m_bit_count(0),
      m_packet_length(0),
      m_success_count(0),
      m_error_count(0),
      m_idle_packet_count(0),
      m_last_valid_packet_time(0),
      m_mutex(nullptr),
      m_history_head(0),
      m_history_count(0) {
    std::memset(m_packet_buffer, 0, sizeof(m_packet_buffer));
    std::memset(m_rx_buffer, 0, sizeof(m_rx_buffer));
}

DccDecoder::~DccDecoder() {
    deinit();
}

bool DccDecoder::init(int gpio_num) {
    if (m_initialized) {
        return true;
    }

    m_gpio_num = gpio_num;

    if (m_gpio_num == -1) {
        ESP_LOGI(TAG, "Initializing DCC Decoder in Software Loopback Mode (Zero Interrupts)...");
        m_mutex = xSemaphoreCreateMutex();
        if (!m_mutex) {
            ESP_LOGE(TAG, "Failed to create history mutex.");
            return false;
        }
        m_initialized = true;
        ESP_LOGI(TAG, "DCC Decoder successfully initialized in Software Loopback Mode.");
        return true;
    }

    ESP_LOGI(TAG, "Initializing DCC Hardware RMT Decoder on GPIO %d...", m_gpio_num);

    // 1. Create a Mutex for the thread-safe history buffer
    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        ESP_LOGE(TAG, "Failed to create history mutex.");
        return false;
    }

    // 2. Create the FreeRTOS Queue for signaling ready symbols
    m_symbol_ready_queue = xQueueCreate(16, sizeof(SymbolMsg));
    if (!m_symbol_ready_queue) {
        ESP_LOGE(TAG, "Failed to create symbol ready queue.");
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    m_active_buffer = 0;

    // 3. Configure and create RMT RX channel
    rmt_rx_channel_config_t rx_chan_config = {};
    rx_chan_config.gpio_num = static_cast<gpio_num_t>(m_gpio_num);
    rx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rx_chan_config.resolution_hz = 1000000;          // 1 MHz resolution (1 tick = 1 µs)
#if CONFIG_IDF_TARGET_ESP32C3
    rx_chan_config.mem_block_symbols = 64;           // ESP32-C3 normal mode block size
    rx_chan_config.flags.with_dma = false;           // ESP32-C3 fallback (no DMA)
#else
    rx_chan_config.mem_block_symbols = 256;          // ESP32-S3 DMA size
    rx_chan_config.flags.with_dma = true;            // ESP32-S3 uses DMA
#endif

    esp_err_t err = rmt_new_rx_channel(&rx_chan_config, &m_rx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT RX channel: %s", esp_err_to_name(err));
        vQueueDelete(m_symbol_ready_queue);
        m_symbol_ready_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    // 4. Register the RMT RX callbacks
    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done = on_recv_done;
    err = rmt_rx_register_event_callbacks(m_rx_channel, &cbs, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register RMT RX callbacks: %s", esp_err_to_name(err));
        rmt_del_channel(m_rx_channel);
        m_rx_channel = nullptr;
        vQueueDelete(m_symbol_ready_queue);
        m_symbol_ready_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    // 5. Enable the RMT RX channel
    err = rmt_enable(m_rx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT RX channel: %s", esp_err_to_name(err));
        rmt_del_channel(m_rx_channel);
        m_rx_channel = nullptr;
        vQueueDelete(m_symbol_ready_queue);
        m_symbol_ready_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    // 6. Launch the background processing task
    BaseType_t res = xTaskCreatePinnedToCore(
        decoderTaskWrapper,
        "dcc_decoder_task",
        4096,
        this,
        16, // Priority 16 is higher than HTTP server to avoid preemption drops
        &m_task_handle,
        1 // Core 1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create background decoder task.");
        rmt_disable(m_rx_channel);
        rmt_del_channel(m_rx_channel);
        m_rx_channel = nullptr;
        vQueueDelete(m_symbol_ready_queue);
        m_symbol_ready_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    // 7. Initiate the first asynchronous receiver capture
    rmt_receive_config_t recv_cfg = {};
    recv_cfg.signal_range_min_ns = 3000;             // 3 µs minimum noise filter (max supported by hardware is 3187 ns)
    recv_cfg.signal_range_max_ns = 12000000;         // 12 ms idle threshold timeout

#if CONFIG_IDF_TARGET_ESP32C3
    recv_cfg.flags.en_partial_rx = false;
    err = rmt_receive(m_rx_channel, m_rx_buffer[m_active_buffer], 128 * sizeof(rmt_symbol_word_t), &recv_cfg);
#else
    recv_cfg.flags.en_partial_rx = true;             // Enable Partial RX mode to handle continuous loopback streaming
    err = rmt_receive(m_rx_channel, m_rx_buffer[m_active_buffer], 1024 * sizeof(rmt_symbol_word_t), &recv_cfg);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate RMT receive: %s", esp_err_to_name(err));
        vTaskDelete(m_task_handle);
        m_task_handle = nullptr;
        rmt_disable(m_rx_channel);
        rmt_del_channel(m_rx_channel);
        m_rx_channel = nullptr;
        vQueueDelete(m_symbol_ready_queue);
        m_symbol_ready_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "DCC Hardware RMT Decoder successfully initialized and active on Core 1.");
    return true;
}

void DccDecoder::deinit() {
    if (!m_initialized) {
        return;
    }

    if (m_gpio_num == -1) {
        ESP_LOGI(TAG, "Stopping DCC Decoder (Software Loopback)...");
        if (m_mutex) {
            vSemaphoreDelete(m_mutex);
            m_mutex = nullptr;
        }
        m_initialized = false;
        return;
    }

    ESP_LOGI(TAG, "Stopping DCC Hardware RMT Decoder on GPIO %d...", m_gpio_num);

    m_initialized = false;

    // 1. Disable and delete RMT RX channel first to prevent any new interrupts
    if (m_rx_channel) {
        rmt_disable(m_rx_channel);
        rmt_del_channel(m_rx_channel);
        m_rx_channel = nullptr;
    }

    // 2. Kill the background task
    if (m_task_handle) {
        vTaskDelete(m_task_handle);
        m_task_handle = nullptr;
    }

    // 3. Delete FreeRTOS queue
    if (m_symbol_ready_queue) {
        vQueueDelete(m_symbol_ready_queue);
        m_symbol_ready_queue = nullptr;
    }

    // 4. Delete the mutex
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

bool DccDecoder::isSignalActive() const {
    if (!m_initialized) {
        return false;
    }
    // Signal is considered active if we parsed a valid packet within the last 500ms
    uint64_t now = esp_timer_get_time();
    uint64_t last = m_last_valid_packet_time.load();
    return (now - last) < 500000ULL;
}

std::vector<DecodedPacket> DccDecoder::getRecentPackets(size_t limit) {
    std::vector<DecodedPacket> result;
    if (!m_mutex || xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return result;
    }

    size_t count = (limit < m_history_count) ? limit : m_history_count;
    result.reserve(count);

    // Retrieve from history in reverse-chronological order (newest first)
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (m_history_head + MAX_HISTORY - 1 - i) % MAX_HISTORY;
        result.push_back(m_history[idx]);
    }

    xSemaphoreGive(m_mutex);
    return result;
}

void DccDecoder::injectPacket(const uint8_t* payload, size_t length, bool is_valid) {
    if (!m_initialized || payload == nullptr || length == 0) {
        return;
    }
    registerDecodedPacket(payload, length, is_valid);
}

// =============================================================================
// RMT RX Done Callback (runs in ISR context)
// =============================================================================
bool IRAM_ATTR DccDecoder::on_recv_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* edata, void* user_ctx) {
    auto* self = static_cast<DccDecoder*>(user_ctx);
    if (!self || !self->m_initialized || !self->m_symbol_ready_queue) {
        return false;
    }
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    size_t num_symbols = edata->num_symbols;
    bool is_last = edata->flags.is_last;

    // Swap active buffer in ISR if transaction ended
    if (is_last) {
        self->m_active_buffer = 1 - self->m_active_buffer;
    }

    // Zero-copy queueing: directly send DMA buffer segment pointer, count, and is_last flag
    SymbolMsg msg = {
        .symbols = edata->received_symbols,
        .count = static_cast<uint32_t>(num_symbols),
        .is_last = is_last
    };
    xQueueSendFromISR(self->m_symbol_ready_queue, &msg, &xHigherPriorityTaskWoken);

    return xHigherPriorityTaskWoken == pdTRUE;
}

// =============================================================================
// Background FreeRTOS Task and Parsing Engine
// =============================================================================
void DccDecoder::decoderTaskWrapper(void* arg) {
    static_cast<DccDecoder*>(arg)->runDecoderTask();
}

void DccDecoder::runDecoderTask() {
    SymbolMsg msg = {};
    ESP_LOGI(TAG, "Background RMT RX Decoder task started.");

    while (m_initialized) {
        // Block indefinitely until the ISR signals a completed buffer or packet gap
        if (xQueueReceive(m_symbol_ready_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.count > 0 && msg.symbols != nullptr) {
                processSymbols(msg.symbols, msg.count);
            }

            // Re-arm RMT RX safely from task context if the transaction completed
            if (msg.is_last && m_initialized) {
                rmt_receive_config_t recv_cfg = {};
                recv_cfg.signal_range_min_ns = 3000;
                recv_cfg.signal_range_max_ns = 12000000;

                size_t buffer_size = 0;
#if CONFIG_IDF_TARGET_ESP32C3
                recv_cfg.flags.en_partial_rx = false;
                buffer_size = 128 * sizeof(rmt_symbol_word_t);
#else
                recv_cfg.flags.en_partial_rx = true;
                buffer_size = 1024 * sizeof(rmt_symbol_word_t);
#endif

                esp_err_t err = rmt_receive(m_rx_channel, m_rx_buffer[m_active_buffer], buffer_size, &recv_cfg);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to re-arm RMT RX: %s", esp_err_to_name(err));
                }
            }
        }
    }

    vTaskDelete(nullptr);
}

void DccDecoder::processSymbols(const rmt_symbol_word_t* symbols, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        // Reconstruct high/low pulse transitions sequentially
        processHalfCycle(symbols[i].duration0);
        processHalfCycle(symbols[i].duration1);
    }
}

void DccDecoder::processHalfCycle(uint32_t duration) {
    // Ignore very short glitch pulses (less than 15 µs) entirely to avoid resetting timing state on ring noise
    if (duration < 15) {
        return;
    }

    HalfType type = HALF_INVALID;
    // '1' half-cycle: Nominal 58 µs (allowed range 48 µs to 72 µs for high noise immunity)
    // '0' half-cycle: Nominal >= 100 µs (allowed range 85 µs to 12000 µs)
    if (duration >= 48 && duration <= 72) {
        type = HALF_ONE;
    } else if (duration >= 85 && duration <= 12000) {
        type = HALF_ZERO;
    }

    if (type == HALF_INVALID) {
        // Noise or transaction boundary gap. Reset state.
        m_half_state = STATE_EXPECTING_FIRST_HALF;
        return;
    }

    if (m_half_state == STATE_EXPECTING_FIRST_HALF) {
        // Record the first half-cycle and wait for the matching second half-cycle
        m_first_half_type = type;
        m_half_state = STATE_EXPECTING_SECOND_HALF;
    } else {
        // Symmetrical checks: Verify that the second half matches the first half
        bool is_match = (type == m_first_half_type);
        
        // Relax symmetry check for stretched end-of-packet bits on loopback (due to RMT inter-packet gaps)
        if (!is_match && m_first_half_type == HALF_ONE && type == HALF_ZERO && m_parser_state == STATE_SEPARATOR && m_packet_length >= 3) {
            is_match = true;
        }

        if (is_match) {
            // Emitted a clean, reconstructed DCC bit!
            processBit(m_first_half_type == HALF_ONE);
            m_half_state = STATE_EXPECTING_FIRST_HALF;
        } else {
            // Symmetry check failed. Treat this second half as the "first half" of a new bit,
            // and expect its second half next to self-align the phase.
            m_first_half_type = type;
            m_half_state = STATE_EXPECTING_SECOND_HALF;
        }
    }
}

void DccDecoder::processBit(bool bit) {
    if (bit) {
        m_consecutive_ones++;
        if (m_consecutive_ones >= 10 && m_parser_state != STATE_PREAMBLE) {
            // Self-healing preamble detection: force reset to PREAMBLE state
            m_parser_state = STATE_PREAMBLE;
            m_preamble_count = m_consecutive_ones;
            return;
        }
    } else {
        m_consecutive_ones = 0;
    }

    switch (m_parser_state) {
        case STATE_PREAMBLE:
            if (bit) {
                m_preamble_count++;
            } else {
                // A '0' bit signifies the end of the preamble.
                // According to NMRA S-9.2, a valid packet start requires at least 10 '1' bits in the preamble.
                if (m_preamble_count >= 10) {
                    // Transition to reading bytes
                    m_parser_state = STATE_BYTE_READ;
                    m_current_byte = 0;
                    m_bit_count = 0;
                    m_packet_length = 0;
                } else {
                    // Insufficient preamble, start over
                    m_preamble_count = 0;
                }
            }
            break;

        case STATE_BYTE_READ:
            // Shift the bit into our current byte
            m_current_byte = (m_current_byte << 1) | (bit ? 1 : 0);
            m_bit_count++;

            if (m_bit_count == 8) {
                // Store the parsed byte
                if (m_packet_length < sizeof(m_packet_buffer)) {
                    m_packet_buffer[m_packet_length++] = m_current_byte;
                }
                m_parser_state = STATE_SEPARATOR;
            }
            break;

        case STATE_SEPARATOR:
            // The separator bit following a byte determines the packet state:
            // '0' = Packet continues, read another byte.
            // '1' = Packet ends, validate and reset.
            if (!bit) {
                // Read another byte
                m_parser_state = STATE_BYTE_READ;
                m_current_byte = 0;
                m_bit_count = 0;
            } else {
                // End of packet! Validate XOR Checksum.
                // Standard DCC packets require at least 3 bytes (Addr, Data, Checksum).
                if (m_packet_length >= 3) {
                    uint8_t calculated_xor = 0;
                    for (uint32_t i = 0; i < m_packet_length - 1; ++i) {
                        calculated_xor ^= m_packet_buffer[i];
                    }
                    bool is_checksum_valid = (calculated_xor == m_packet_buffer[m_packet_length - 1]);
                    
                    registerDecodedPacket(m_packet_buffer, m_packet_length, is_checksum_valid);
                }
                
                // Reset parser for next preamble train
                m_parser_state = STATE_PREAMBLE;
                m_preamble_count = 0;
            }
            break;
    }
}

void DccDecoder::registerDecodedPacket(const uint8_t* payload, size_t length, bool is_valid) {
    uint64_t now = esp_timer_get_time();

    DecodedPacket packet = {};
    std::memcpy(packet.payload, payload, length);
    packet.length = static_cast<uint8_t>(length);
    packet.timestamp = now;
    packet.is_valid = is_valid;
    packet.human_readable = parsePacketToHuman(payload, length);

    bool is_idle = (payload[0] == 0xFF && payload[1] == 0x00);

    if (is_valid) {
        m_success_count++;
        m_last_valid_packet_time = now;
        if (is_idle) {
            m_idle_packet_count++;
        }
    } else {
        m_error_count++;
        packet.human_readable += " [CHECKSUM ERROR]";
    }

    // Insert into history ring buffer
    if (m_mutex && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool should_add_to_history = true;

        if (is_idle) {
            // NEVER show standard idle packets in the active console history list
            should_add_to_history = false;
        }

        if (should_add_to_history) {
            m_history[m_history_head] = packet;
            m_history_head = (m_history_head + 1) % MAX_HISTORY;
            if (m_history_count < MAX_HISTORY) {
                m_history_count++;
            }
        }
        xSemaphoreGive(m_mutex);
    }
}

// =============================================================================
// Human-Readable Packet Translation
// =============================================================================
std::string DccDecoder::parsePacketToHuman(const uint8_t* payload, size_t length) {
    if (length < 3) {
        return "Malformed (Too Short)";
    }

    // 1. Idle Packet detection: [0xFF, 0x00, 0xFF]
    if (payload[0] == 0xFF && payload[1] == 0x00) {
        return "Idle Packet";
    }

    // 2. Decode Locomotive Address (supports Short and Long Addresses)
    uint32_t loco_address = 0;
    size_t data_start_index = 1;
    bool is_long_address = false;

    // S-9.2.1: Long address is defined by 1st byte range [192, 231] (0xC0 to 0xE7)
    if (payload[0] >= 192 && payload[0] <= 231) {
        loco_address = ((payload[0] & 0x3F) << 8) | payload[1];
        data_start_index = 2;
        is_long_address = true;
    } else if (payload[0] > 0 && payload[0] < 128) {
        loco_address = payload[0];
    } else if (payload[0] >= 128 && payload[0] <= 191) {
        // Multi-function decoder sub-group accessory address
    }

    // If it's a loco packet, let's parse the commands
    if (loco_address > 0 && data_start_index < length - 1) {
        uint8_t inst_byte = payload[data_start_index];
        std::string desc = "Loco " + std::to_string(loco_address) + " | ";

        // A. 128-Speed Step Command: Instruction byte is 0x3F (00111111)
        if (inst_byte == 0x3F && data_start_index + 1 < length - 1) {
            uint8_t speed_byte = payload[data_start_index + 1];
            bool dir = (speed_byte & 0x80) != 0;
            uint8_t speed_val = speed_byte & 0x7F;

            desc += "Speed 128-step: ";
            if (speed_val == 0) {
                desc += "STOP";
            } else if (speed_val == 1) {
                desc += "EMERGENCY STOP";
            } else {
                desc += std::to_string(speed_val - 1) + " (steps 1-126)";
            }
            desc += dir ? " FWD" : " REV";
            return desc;
        }

        // B. 28-Speed Step Command: Instruction byte format 0b01DCSSSS
        if ((inst_byte & 0xC0) == 0x40) {
            bool dir = (inst_byte & 0x20) != 0;
            // 28-step mapping: SSSS (bits 0-3) and C (bit 4, LSB of speed)
            uint8_t ssss = inst_byte & 0x0F;
            uint8_t c = (inst_byte & 0x10) >> 4;
            
            // Map 28 speed steps into a simple value
            uint8_t speed_28 = 0;
            if (ssss == 0) {
                speed_28 = 0; // stop
            } else if (ssss == 1) {
                speed_28 = 29; // emergency stop
            } else {
                // Table mapping for standard 28-step speed bytes
                // Bits are re-ordered: C, SSSS
                // ssss value ranges from 2 to 15
                speed_28 = ((ssss - 2) * 2) + c + 1;
            }

            desc += "Speed 28-step: ";
            if (speed_28 == 0) {
                desc += "STOP";
            } else if (speed_28 == 29) {
                desc += "EMERGENCY STOP";
            } else {
                desc += std::to_string(speed_28) + "/28";
            }
            desc += dir ? " FWD" : " REV";
            return desc;
        }

        // C. DCC Functions Group 1 (F0 - F4): Instruction format 0b100DDDDD
        if ((inst_byte & 0xE0) == 0x80) {
            bool f0 = (inst_byte & 0x10) != 0;
            bool f1 = (inst_byte & 0x01) != 0;
            bool f2 = (inst_byte & 0x02) != 0;
            bool f3 = (inst_byte & 0x04) != 0;
            bool f4 = (inst_byte & 0x08) != 0;

            desc += "Func F0-F4: ";
            desc += std::string("F0(Lght):") + (f0 ? "ON" : "OFF") + " | ";
            desc += std::string("F1(Bell):") + (f1 ? "ON" : "OFF") + " | ";
            desc += std::string("F2(Horn):") + (f2 ? "ON" : "OFF") + " | ";
            desc += std::string("F3:") + (f3 ? "ON" : "OFF") + " | ";
            desc += std::string("F4:") + (f4 ? "ON" : "OFF");
            return desc;
        }

        // D. DCC Functions Group 2 (F5 - F8): Instruction format 0b1011DDDD
        if ((inst_byte & 0xF0) == 0xB0) {
            bool f5 = (inst_byte & 0x01) != 0;
            bool f6 = (inst_byte & 0x02) != 0;
            bool f7 = (inst_byte & 0x04) != 0;
            bool f8 = (inst_byte & 0x08) != 0;

            desc += "Func F5-F8: ";
            desc += std::string("F5:") + (f5 ? "ON" : "OFF") + " | ";
            desc += std::string("F6:") + (f6 ? "ON" : "OFF") + " | ";
            desc += std::string("F7:") + (f7 ? "ON" : "OFF") + " | ";
            desc += std::string("F8:") + (f8 ? "ON" : "OFF");
            return desc;
        }

        // E. DCC Functions Group 2 (F9 - F12): Instruction format 0b1010DDDD
        if ((inst_byte & 0xF0) == 0xA0) {
            bool f9 = (inst_byte & 0x01) != 0;
            bool f10 = (inst_byte & 0x02) != 0;
            bool f11 = (inst_byte & 0x04) != 0;
            bool f12 = (inst_byte & 0x08) != 0;

            desc += "Func F9-F12: ";
            desc += std::string("F9:") + (f9 ? "ON" : "OFF") + " | ";
            desc += std::string("F10:") + (f10 ? "ON" : "OFF") + " | ";
            desc += std::string("F11:") + (f11 ? "ON" : "OFF") + " | ";
            desc += std::string("F12:") + (f12 ? "ON" : "OFF");
            return desc;
        }

        return desc + "Raw Inst: 0x" + std::string(1, "0123456789ABCDEF"[(inst_byte >> 4) & 0x0F]) + "0123456789ABCDEF"[inst_byte & 0x0F];
    }

    // 3. Decode Accessory / Turnout Command
    // Format: [10AAAAAA] [1AAACDDD]
    if ((payload[0] & 0xC0) == 0x80 && (payload[1] & 0x80) == 0x80) {
        // Reconstruct turnout address
        // The first byte holds the lower 6 bits AAAAAA of the address.
        // The second byte holds the inverse of the upper 3 bits AAA (bits 4-6).
        uint16_t addr_lower = payload[0] & 0x3F;
        uint16_t addr_upper = (~payload[1] & 0x70) >> 4;
        uint16_t dcc_addr = (addr_upper << 6) | addr_lower;
        // The individual output pair selector is bits 1-2 of the second byte
        uint8_t pair = (payload[1] & 0x06) >> 1;
        uint16_t turnout_addr = (dcc_addr * 4) + pair + 1;
        
        bool activate = (payload[1] & 0x08) != 0; // C bit (bit 3) determines activate vs release
        bool direction = (payload[1] & 0x01) != 0; // D bit determines direction (straight vs curved)
        
        std::string desc = "Turnout Addr: " + std::to_string(turnout_addr) + " | ";
        desc += direction ? "STRAIGHT" : "CURVED";
        desc += activate ? " (ACTIVATE)" : " (RELEASE)";
        return desc;
    }

    // Generic Hex fallback
    std::string hex_str = "Hex: [";
    for (size_t i = 0; i < length; ++i) {
        hex_str += "0x" + std::string(1, "0123456789ABCDEF"[(payload[i] >> 4) & 0x0F]) + "0123456789ABCDEF"[payload[i] & 0x0F];
        if (i < length - 1) hex_str += ", ";
    }
    hex_str += "]";
    return hex_str;
}

} // namespace rx
} // namespace dcc
