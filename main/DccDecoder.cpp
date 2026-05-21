/**
 * @file DccDecoder.cpp
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

#include "DccDecoder.hpp"
#include <esp_log.h>
#include <cstring>

static const char* TAG = "DccDecoder";

namespace dcc {
namespace rx {

DccDecoder::DccDecoder()
    : m_gpio_num(-1),
      m_initialized(false),
      m_duration_queue(nullptr),
      m_task_handle(nullptr),
      m_last_edge_time(0),
      m_half_state(STATE_EXPECTING_FIRST_HALF),
      m_first_half_type(HALF_INVALID),
      m_parser_state(STATE_PREAMBLE),
      m_preamble_count(0),
      m_current_byte(0),
      m_bit_count(0),
      m_packet_length(0),
      m_success_count(0),
      m_error_count(0),
      m_last_valid_packet_time(0),
      m_mutex(nullptr),
      m_history_head(0),
      m_history_count(0) {
    std::memset(m_packet_buffer, 0, sizeof(m_packet_buffer));
}

DccDecoder::~DccDecoder() {
    deinit();
}

bool DccDecoder::init(int gpio_num) {
    if (m_initialized) {
        return true;
    }

    m_gpio_num = gpio_num;
    ESP_LOGI(TAG, "Initializing DCC Hardware Decoder on GPIO %d...", m_gpio_num);

    // 1. Create a Mutex for the thread-safe history buffer
    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        ESP_LOGE(TAG, "Failed to create history mutex.");
        return false;
    }

    // 2. Create the FreeRTOS Queue for edge timings (durations in µs)
    // We size this large enough to buffer timings even during system load spikes
    m_duration_queue = xQueueCreate(128, sizeof(uint32_t));
    if (!m_duration_queue) {
        ESP_LOGE(TAG, "Failed to create duration queue.");
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    // 3. Configure the GPIO input pin
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;          // Capture both rising and falling edges
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << m_gpio_num);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;        // Pull up to prevent floating noise
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d. Error: 0x%X", m_gpio_num, err);
        vQueueDelete(m_duration_queue);
        m_duration_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    // 4. Install the GPIO ISR service (gracefully handle already-installed state)
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service. Error: 0x%X", err);
        vQueueDelete(m_duration_queue);
        m_duration_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    // 5. Register the edge transition ISR handler
    err = gpio_isr_handler_add(static_cast<gpio_num_t>(m_gpio_num), gpio_isr_handler, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler. Error: 0x%X", err);
        vQueueDelete(m_duration_queue);
        m_duration_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    // 6. Launch the background processing task (high priority for real-time parsing)
    BaseType_t res = xTaskCreatePinnedToCore(
        decoderTaskWrapper,
        "dcc_decoder_task",
        4096,
        this,
        configMAX_PRIORITIES - 2, // High priority
        &m_task_handle,
        1 // Core 1 (same as web server, leaving RMT on Core 0 or default core)
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create background decoder task.");
        gpio_isr_handler_remove(static_cast<gpio_num_t>(m_gpio_num));
        vQueueDelete(m_duration_queue);
        m_duration_queue = nullptr;
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
        return false;
    }

    m_last_edge_time = esp_timer_get_time();
    m_initialized = true;
    ESP_LOGI(TAG, "DCC Decoder successfully initialized and active on Core 1.");
    return true;
}

void DccDecoder::deinit() {
    if (!m_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Stopping DCC Decoder on GPIO %d...", m_gpio_num);

    // 1. Remove the GPIO ISR handler
    gpio_isr_handler_remove(static_cast<gpio_num_t>(m_gpio_num));

    // 2. Kill the background task
    if (m_task_handle) {
        vTaskDelete(m_task_handle);
        m_task_handle = nullptr;
    }

    // 3. Clear the queue
    if (m_duration_queue) {
        vQueueDelete(m_duration_queue);
        m_duration_queue = nullptr;
    }

    // 4. Delete the mutex
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }

    m_initialized = false;
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

// =============================================================================
// Interrupt Service Routine (ISR)
// =============================================================================
void IRAM_ATTR DccDecoder::gpio_isr_handler(void* arg) {
    auto* self = static_cast<DccDecoder*>(arg);
    uint64_t now = esp_timer_get_time();
    uint32_t duration = static_cast<uint32_t>(now - self->m_last_edge_time);
    self->m_last_edge_time = now;

    // Direct queue injection from ISR context
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(self->m_duration_queue, &duration, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// =============================================================================
// Background FreeRTOS Task and Parsing Engine
// =============================================================================
void DccDecoder::decoderTaskWrapper(void* arg) {
    static_cast<DccDecoder*>(arg)->runDecoderTask();
}

void DccDecoder::runDecoderTask() {
    uint32_t duration = 0;
    while (true) {
        // Block indefinitely until a pulse transition is queued from the ISR
        if (xQueueReceive(m_duration_queue, &duration, portMAX_DELAY) == pdTRUE) {
            processDuration(duration);
        }
    }
}

void DccDecoder::processDuration(uint32_t duration) {
    // NMRA S-9.1 Bit Specifications:
    // A DCC "1" bit half-cycle is nominally 58µs (valid range: 52µs to 64µs).
    // A DCC "0" bit half-cycle is >= 95µs (nominally 100µs to 9900µs).
    // To allow for slight hardware loopback rise/fall mismatches or hardware filters,
    // we expand the tolerances slightly:
    // - '1' half-cycle: 50µs to 72µs
    // - '0' half-cycle: 85µs to 12000µs
    
    HalfType type = HALF_INVALID;
    if (duration >= 50 && duration <= 72) {
        type = HALF_ONE;
    } else if (duration >= 85 && duration <= 12000) {
        type = HALF_ZERO;
    }

    if (type == HALF_INVALID) {
        // Noise or timing gap. Reset half-cycle expectations.
        m_half_state = STATE_EXPECTING_FIRST_HALF;
        return;
    }

    if (m_half_state == STATE_EXPECTING_FIRST_HALF) {
        // Record the first half and wait for the matching second half
        m_first_half_type = type;
        m_half_state = STATE_EXPECTING_SECOND_HALF;
    } else {
        // Verify that the second half matches the first half
        if (type == m_first_half_type) {
            // Emitted a clean DCC bit!
            processBit(type == HALF_ONE);
        } else {
            // Symmetry check failed. Treat this second half as the "first half" of a new bit.
            m_first_half_type = type;
        }
        m_half_state = STATE_EXPECTING_FIRST_HALF;
    }
}

void DccDecoder::processBit(bool bit) {
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

    if (is_valid) {
        m_success_count++;
        m_last_valid_packet_time = now;
    } else {
        m_error_count++;
        packet.human_readable += " [CHECKSUM ERROR]";
    }

    // Insert into history ring buffer
    if (m_mutex && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        m_history[m_history_head] = packet;
        m_history_head = (m_history_head + 1) % MAX_HISTORY;
        if (m_history_count < MAX_HISTORY) {
            m_history_count++;
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
        uint16_t turnout_addr = (addr_upper << 6) | addr_lower;
        
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
