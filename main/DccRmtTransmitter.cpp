/**
 * @file DccRmtTransmitter.cpp
 * @brief Standalone, modular ESP32 Low-Level GDMA DCC Transmitter.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-23
 * 
 * Bypasses high-level ESP-IDF v5 RMT queues and software ISRs.
 * Linked DMA descriptors continuously loop DCC packets in hardware.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "DccRmtTransmitter.hpp"
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <cstring>
#include "esp_cache.h"
#include "esp_private/gdma.h"
#include "hal/rmt_ll.h"
#include "soc/rmt_struct.h"

// Override troublesome __noreturn macro for standard toolchain headers included by rmt_private.h
#undef __noreturn
#define __noreturn __attribute__((__noreturn__))
#include <stdatomic.h>
#undef _Atomic
#define _Atomic
#include "rmt_private.h"
#undef _Atomic
#undef TAG
#include "gdma_priv.h"
#include "hal/gdma_ll.h"

// Define to 1 to enable register-aligned (K+2)%3 injection, 0 for classic round-robin
#define USE_REGISTER_ALIGNED_INJECTION 1

static const char* TAG = "DccRmtTx";



namespace dcc {
namespace rmt {

DccRmtTransmitter::DccRmtTransmitter()
    : m_rmt_channel(nullptr),
      m_dma_chan(nullptr),
      m_dma_descriptors(nullptr),
      m_current_command_desc_idx(-1),
      m_write_idx(0),
      m_cleanup_timer(nullptr),
      m_mutex(nullptr),
      m_scope_trigger_enabled(true),
      m_initialized(false) {
    m_dma_buffers[0] = nullptr;
    m_dma_buffers[1] = nullptr;
    m_dma_buffers[2] = nullptr;
    m_dma_idle_buffer = nullptr;
    m_idle_symbol_count = 0;
}

DccRmtTransmitter::~DccRmtTransmitter() {
    deinit();
}

bool DccRmtTransmitter::init(const TransmitterConfig& config) {
    if (m_initialized) {
        ESP_LOGW(TAG, "Transmitter already initialized, deinitializing first");
        deinit();
    }

    m_config = config;

    ESP_LOGI(TAG, "Initializing DCC Low-Level GDMA Transmitter on GPIO %d...", m_config.gpio_num);
    ESP_LOGI(TAG, "Settings: Preamble: %d, BiDi: %s, Timings (1/0/End): %d/%d/%d µs",
             m_config.num_preamble,
             m_config.enable_bidi ? "ENABLED" : "DISABLED",
             m_config.bit1_duration,
             m_config.bit0_duration,
             m_config.endbit_duration);

    m_mutex = xSemaphoreCreateMutex();
    if (m_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    // 1. Configure and allocate RMT TX Channel
    // resolution_hz = 1MHz -> 1 tick = 1 µs
    rmt_tx_channel_config_t rmt_chan_config = {};
    rmt_chan_config.gpio_num = static_cast<gpio_num_t>(m_config.gpio_num);
    rmt_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_chan_config.resolution_hz = 1000000; 
    rmt_chan_config.mem_block_symbols = 64;  
    rmt_chan_config.trans_queue_depth = 4;   
    rmt_chan_config.intr_priority = 0;
    rmt_chan_config.flags.invert_out = 0;
    rmt_chan_config.flags.with_dma = 1; // Force the driver to allocate RMT channel with DMA support (Channel 3 on ESP32-S3)

    esp_err_t err = rmt_new_tx_channel(&rmt_chan_config, &m_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
        deinit();
        return false;
    }

    err = rmt_enable(m_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(err));
        deinit();
        return false;
    }

    // Extract the internal RMT hardware channel index and the allocated GDMA channel handle
    rmt_channel_t* chan_internal = (rmt_channel_t*)m_rmt_channel;
    int channel_id = chan_internal->channel_id;
    m_dma_chan = chan_internal->dma_chan;
    
    ESP_LOGI(TAG, "Allocated hardware RMT channel ID: %d", channel_id);
    if (m_dma_chan == nullptr) {
        ESP_LOGE(TAG, "No GDMA channel allocated by RMT driver!");
        deinit();
        return false;
    }

    // Software Disarming: Safely overwrite default RMT driver GDMA callbacks directly in the channel structure (bypasses failing register API calls)
    gdma_tx_channel_t* tx_chan = (gdma_tx_channel_t*)m_dma_chan;
    tx_chan->cbs.on_trans_eof = nullptr;
    tx_chan->cbs.on_descr_err = nullptr;
    tx_chan->user_data = nullptr;

    // Hardware Disarming: Completely mask the GDMA TX interrupts to prevent useless context switches and achieve absolute 0% CPU overhead
    int configured_dma_ch = tx_chan->base.pair->pair_id;
    gdma_ll_tx_enable_interrupt(&GDMA, configured_dma_ch, GDMA_LL_EVENT_TX_TOTAL_EOF | GDMA_LL_EVENT_TX_EOF, false);
    
    // Clear all TX raw interrupts (including the descriptor owner error bit) using portable LL wrapper
    gdma_ll_tx_clear_interrupt_status(&GDMA, configured_dma_ch, 0x1F); 

    // Disable automatic descriptor writeback at hardware register level using portable LL wrapper
    // This stops GDMA from setting owner = 0 in SRAM when it completes a descriptor
    gdma_ll_tx_enable_auto_write_back(&GDMA, configured_dma_ch, false);



    // 5. Allocate 3 lldesc_t DMA descriptors in internal DMA-capable SRAM
    m_dma_descriptors = (lldesc_t*)heap_caps_malloc(3 * sizeof(lldesc_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (m_dma_descriptors == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate DMA descriptors");
        deinit();
        return false;
    }
    std::memset(m_dma_descriptors, 0, 3 * sizeof(lldesc_t));

    // 6. Allocate 3 RMT symbol buffers in internal DMA-capable SRAM (each 128 symbols capacity)
    for (int i = 0; i < 3; i++) {
        m_dma_buffers[i] = (rmt_symbol_word_t*)heap_caps_malloc(128 * sizeof(rmt_symbol_word_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (m_dma_buffers[i] == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate DMA symbol buffer %d", i);
            deinit();
            return false;
        }
        std::memset(m_dma_buffers[i], 0, 128 * sizeof(rmt_symbol_word_t));
    }

    m_dma_idle_buffer = (rmt_symbol_word_t*)heap_caps_malloc(128 * sizeof(rmt_symbol_word_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (m_dma_idle_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate DMA idle buffer");
        deinit();
        return false;
    }

    // 7. Format standard NMRA DCC Idle Packets [0xFF, 0x00, 0xFF] into the idle buffer
    static const uint8_t idle_packet_data[] = { 0xFF, 0x00, 0xFF };
    size_t num_idle_symbols = 0;
    formatPacketToRmtSymbols(idle_packet_data, sizeof(idle_packet_data), m_dma_idle_buffer, &num_idle_symbols);
    m_idle_symbol_count = num_idle_symbols;
    esp_cache_msync(m_dma_idle_buffer, num_idle_symbols * sizeof(rmt_symbol_word_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

    for (int i = 0; i < 3; i++) {
        m_dma_descriptors[i].size = num_idle_symbols * sizeof(rmt_symbol_word_t);
        m_dma_descriptors[i].length = num_idle_symbols * sizeof(rmt_symbol_word_t);
        m_dma_descriptors[i].offset = 0;
        m_dma_descriptors[i].sosf = 0;
        m_dma_descriptors[i].eof = 0; // Keep EOF=0 to prevent GDMA from clearing owner bit
        m_dma_descriptors[i].owner = 1; // Hardware owned
        m_dma_descriptors[i].buf = (uint8_t*)m_dma_idle_buffer;

        // Flush descriptors to physical SRAM for DMA accessibility
        esp_cache_msync(&m_dma_descriptors[i], sizeof(lldesc_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }

    // 8. Chain descriptors circularly: A -> B -> C -> A
    m_dma_descriptors[0].qe.stqe_next = &m_dma_descriptors[1];
    m_dma_descriptors[1].qe.stqe_next = &m_dma_descriptors[2];
    m_dma_descriptors[2].qe.stqe_next = &m_dma_descriptors[0];

    m_cleanup_timer = xTimerCreate(
        "dcc_tx_timer",
        pdMS_TO_TICKS(50), // 50 ms (transmits command ~5-8 times for noise immunity before restoring idle)
        pdFALSE,           // One-shot timer
        this,              // Timer ID
        cleanup_timer_callback
    );
    if (m_cleanup_timer == nullptr) {
        ESP_LOGE(TAG, "Failed to create cleanup timer");
        deinit();
        return false;
    }

    m_current_command_desc_idx = -1;
    m_write_idx = 0;

    // 9. Start the GDMA hardware transfer and trigger the RMT Transmitter FSM to begin signal generation
    err = gdma_start(m_dma_chan, (intptr_t)&m_dma_descriptors[0]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start GDMA engine: %s", esp_err_to_name(err));
        deinit();
        return false;
    }

    // Trigger the RMT FSM to start reading from the DMA FIFO and driving GPIO 5
    rmt_ll_tx_start(&RMT, channel_id);


    // Configure GPIO 4 as oscilloscope trigger output
    gpio_reset_pin(GPIO_NUM_4);
    gpio_config_t trigger_pin_conf = {};
    trigger_pin_conf.intr_type = GPIO_INTR_DISABLE;
    trigger_pin_conf.mode = GPIO_MODE_OUTPUT;
    trigger_pin_conf.pin_bit_mask = (1ULL << GPIO_NUM_4);
    trigger_pin_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    trigger_pin_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&trigger_pin_conf);
    gpio_set_level(GPIO_NUM_4, 0);

    m_initialized = true;
    ESP_LOGI(TAG, "Low-Level DMA DCC Transmitter successfully initialized!");
    return true;
}

void DccRmtTransmitter::deinit() {
    m_initialized = false;

    if (m_cleanup_timer != nullptr) {
        xTimerDelete(m_cleanup_timer, 0);
        m_cleanup_timer = nullptr;
    }

    if (m_dma_chan != nullptr) {
        gdma_stop(m_dma_chan);
        // Note: we do NOT call gdma_del_channel(m_dma_chan) since it belongs to the RMT driver!
        m_dma_chan = nullptr;
    }

    if (m_rmt_channel != nullptr) {
        rmt_disable(m_rmt_channel);
        rmt_del_channel(m_rmt_channel);
        m_rmt_channel = nullptr;
    }

    if (m_dma_descriptors != nullptr) {
        free(m_dma_descriptors);
        m_dma_descriptors = nullptr;
    }

    for (int i = 0; i < 3; i++) {
        if (m_dma_buffers[i] != nullptr) {
            free(m_dma_buffers[i]);
            m_dma_buffers[i] = nullptr;
        }
    }

    if (m_dma_idle_buffer != nullptr) {
        free(m_dma_idle_buffer);
        m_dma_idle_buffer = nullptr;
    }

    if (m_mutex != nullptr) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }

    ESP_LOGI(TAG, "DCC RMT Transmitter deinitialized");
}

bool DccRmtTransmitter::sendPacket(const uint8_t* payload, size_t length, uint32_t wait_ms) {
    if (!m_initialized || payload == nullptr || length == 0) {
        return false;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        return false;
    }

    TickType_t start_tick = xTaskGetTickCount();
    
    // Wait until any active command finishes
    while (m_current_command_desc_idx != -1) {
        if (wait_ms == 0 || (xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(wait_ms)) {
            xSemaphoreGive(m_mutex);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

#if USE_REGISTER_ALIGNED_INJECTION
    // Query active descriptor index from GDMA registers to write to the safe (K+2)%3 slot
    int target_idx;
    {
        gdma_tx_channel_t* tx_chan = (gdma_tx_channel_t*)m_dma_chan;
        int configured_dma_ch = tx_chan->base.pair->pair_id;
        uint32_t active_addr = gdma_ll_tx_get_prefetched_desc_addr(&GDMA, configured_dma_ch);
        
        int active_idx = 0;
        for (int i = 0; i < 3; i++) {
            if ((uint32_t)&m_dma_descriptors[i] == active_addr) {
                active_idx = i;
                break;
            }
        }
        // Write to the descriptor that is TWO steps ahead in the ring (guarantees a full 6.3ms buffer window)
        target_idx = (active_idx + 2) % 3;
        
        ESP_LOGD(TAG, "GDMA active desc: 0x%08lx (idx: %d), target write index: %d", (unsigned long)active_addr, active_idx, target_idx);
    }
#else
    // Write to the next round-robin descriptor index (classic tested version)
    int target_idx = m_write_idx;
    m_write_idx = (m_write_idx + 1) % 3;
#endif

    // Format into temporary symbols first to prevent GDMA from reading half-written data
    size_t num_symbols = 0;
    rmt_symbol_word_t temp_symbols[128];
    formatPacketToRmtSymbols(payload, length, temp_symbols, &num_symbols);
 
    // Copy to active buffer (keep owner=1 at all times so GDMA never encounters owner=0)
    std::memcpy(m_dma_buffers[target_idx], temp_symbols, num_symbols * sizeof(rmt_symbol_word_t));
    m_dma_descriptors[target_idx].buf = (uint8_t*)m_dma_buffers[target_idx];
    m_dma_descriptors[target_idx].size = num_symbols * sizeof(rmt_symbol_word_t);
    m_dma_descriptors[target_idx].length = num_symbols * sizeof(rmt_symbol_word_t);
    m_dma_descriptors[target_idx].eof = 0;  // Ensure EOF remains 0

    // Flush modified buffer and descriptor to physical SRAM for DMA accessibility
    esp_cache_msync(m_dma_buffers[target_idx], num_symbols * sizeof(rmt_symbol_word_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    esp_cache_msync(&m_dma_descriptors[target_idx], sizeof(lldesc_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

    m_current_command_desc_idx = target_idx;

    // Clear any latched GDMA TX interrupts using portable LL wrapper
    gdma_tx_channel_t* tx_chan = (gdma_tx_channel_t*)m_dma_chan;
    int configured_dma_ch = tx_chan->base.pair->pair_id;
    gdma_ll_tx_clear_interrupt_status(&GDMA, configured_dma_ch, 0x1F);

    // Pulse oscilloscope trigger HIGH
    if (m_scope_trigger_enabled) {
        gpio_set_level(GPIO_NUM_4, 1);
    }

    // Start the cleanup timer
    xTimerStart(m_cleanup_timer, 0);

    if (m_callback) {
        m_callback(payload, length, m_callback_arg);
    }

    xSemaphoreGive(m_mutex);
    return true;
}

void DccRmtTransmitter::registerCallback(PacketSentCallback callback, void* arg) {
    m_callback = callback;
    m_callback_arg = arg;
}

void IRAM_ATTR DccRmtTransmitter::formatPacketToRmtSymbols(const uint8_t* payload, size_t length, rmt_symbol_word_t* rmt_buffer, size_t* out_num_symbols) {
    rmt_symbol_word_t one_symbol = {
        .duration0 = static_cast<uint16_t>(m_config.bit1_duration),
        .level0 = static_cast<uint16_t>(m_config.flags.level0),
        .duration1 = static_cast<uint16_t>(m_config.bit1_duration),
        .level1 = static_cast<uint16_t>(!m_config.flags.level0)
    };

    rmt_symbol_word_t zero_symbol = {
        .duration0 = static_cast<uint16_t>(m_config.bit0_duration),
        .level0 = static_cast<uint16_t>(m_config.flags.level0),
        .duration1 = static_cast<uint16_t>(m_config.bit0_duration),
        .level1 = static_cast<uint16_t>(!m_config.flags.level0)
    };

    rmt_symbol_word_t end_symbol = {
        .duration0 = static_cast<uint16_t>(m_config.bit1_duration),
        .level0 = static_cast<uint16_t>(m_config.flags.level0),
        .duration1 = static_cast<uint16_t>(m_config.endbit_duration ? m_config.endbit_duration : m_config.bit1_duration),
        .level1 = static_cast<uint16_t>(!m_config.flags.level0)
    };

    size_t idx = 0;

    // 1. BiDi Cutout
    if (m_config.enable_bidi && m_config.bidibit_duration > 0) {
        rmt_symbol_word_t bidi_symbol = {
            .duration0 = static_cast<uint16_t>(m_config.bidibit_duration),
            .level0 = static_cast<uint16_t>(m_config.flags.level0),
            .duration1 = static_cast<uint16_t>(m_config.bidibit_duration),
            .level1 = static_cast<uint16_t>(!m_config.flags.level0)
        };
        for (int i = 0; i < 4; i++) {
            rmt_buffer[idx++] = bidi_symbol;
        }
    }

    // 2. ZIMO 0 Prefix
    if (m_config.flags.zimo0) {
        rmt_buffer[idx++] = zero_symbol;
    }

    // 3. Preamble
    for (int i = 0; i < m_config.num_preamble; i++) {
        rmt_buffer[idx++] = one_symbol;
    }

    // 4. Start/Separator & Bytes
    rmt_buffer[idx++] = zero_symbol; // Start bit

    for (size_t b = 0; b < length; b++) {
        if (b > 0) {
            rmt_buffer[idx++] = zero_symbol; // Separator bit between bytes
        }
        uint8_t byte_val = payload[b];
        for (int bit = 7; bit >= 0; bit--) {
            if ((byte_val >> bit) & 1) {
                rmt_buffer[idx++] = one_symbol;
            } else {
                rmt_buffer[idx++] = zero_symbol;
            }
        }
    }

    // 5. End bit
    rmt_buffer[idx++] = end_symbol;

    *out_num_symbols = idx;
}

void DccRmtTransmitter::cleanup_timer_callback(TimerHandle_t xTimer) {
    DccRmtTransmitter* self = static_cast<DccRmtTransmitter*>(pvTimerGetTimerID(xTimer));
    if (!self) {
        return;
    }

    int completed_idx = self->m_current_command_desc_idx;
    if (completed_idx != -1) {
        // Pulled HIGH when queued, pull LOW after the packet has been sent a few times
        if (self->m_scope_trigger_enabled) {
            gpio_set_level(GPIO_NUM_4, 0);
        }

        // Update the descriptor pointer to point to the pre-formatted IDLE buffer seamlessly
        self->m_dma_descriptors[completed_idx].buf = (uint8_t*)self->m_dma_idle_buffer;
        self->m_dma_descriptors[completed_idx].size = self->m_idle_symbol_count * sizeof(rmt_symbol_word_t);
        self->m_dma_descriptors[completed_idx].length = self->m_idle_symbol_count * sizeof(rmt_symbol_word_t);
        self->m_dma_descriptors[completed_idx].eof = 0; // Keep EOF=0
 
        // Flush modified descriptor to physical SRAM for DMA accessibility
        esp_cache_msync(&self->m_dma_descriptors[completed_idx], sizeof(lldesc_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

        self->m_current_command_desc_idx = -1;
    }
}

} // namespace rmt
} // namespace dcc
