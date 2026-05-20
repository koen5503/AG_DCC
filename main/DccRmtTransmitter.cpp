/**
 * @file DccRmtTransmitter.cpp
 * @brief Implementation of the standalone ESP32 RMT DCC Transmitter.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-20
 * 
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "DccRmtTransmitter.hpp"
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <limits.h>
#include <cstring>

static const char* TAG = "DccRmtTx";

// =============================================================================
// Isolated RMT DCC Encoder (originally in rmt_dcc_encoder.c)
// =============================================================================

#if defined(CONFIG_RMT_TX_ISR_HANDLER_IN_IRAM)
#  define RMT_IRAM_ATTR IRAM_ATTR
#else
#  define RMT_IRAM_ATTR
#endif

/// Local DCC encoder configuration struct matching TransmitterConfig
typedef struct {
  uint8_t num_preamble;
  uint8_t bidibit_duration;
  uint8_t bit1_duration;
  uint8_t bit0_duration;
  uint8_t endbit_duration;
  struct {
    bool level0 : 1;
    bool zimo0 : 1;
  } flags;
} local_dcc_encoder_config_t;

/// DCC custom encoder state structure
typedef struct {
  rmt_encoder_t base;
  rmt_encoder_t* copy_encoder;
  rmt_encoder_t* bytes_encoder;
  rmt_symbol_word_t bidi_symbol;
  rmt_symbol_word_t one_symbol;
  rmt_symbol_word_t zero_symbol;
  rmt_symbol_word_t end_symbol;
  size_t num_preamble_symbols;
  size_t num_symbols;
  enum { BiDi, Zimo0, Preamble, Start, Data, End } state;
  struct {
    bool zimo0 : 1;
  } flags;
} rmt_dcc_encoder_t;

static size_t RMT_IRAM_ATTR
rmt_encode_dcc_bit(rmt_dcc_encoder_t* dcc_encoder,
                   rmt_channel_handle_t channel,
                   rmt_encode_state_t* ret_state,
                   rmt_symbol_word_t const* symbol) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encoder_handle_t copy_encoder = dcc_encoder->copy_encoder;
  encoded_symbols += copy_encoder->encode(
    copy_encoder, channel, symbol, sizeof(rmt_symbol_word_t), &state);
  *ret_state = state;
  return encoded_symbols;
}

static size_t RMT_IRAM_ATTR rmt_encode_dcc_bidi(rmt_dcc_encoder_t* dcc_encoder,
                                                rmt_channel_handle_t channel,
                                                rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encoder_handle_t copy_encoder = dcc_encoder->copy_encoder;

  // Skip if duration is 0
  if (!dcc_encoder->bidi_symbol.duration0) {
    state |= RMT_ENCODING_COMPLETE;
    dcc_encoder->state = Zimo0;
  }
  // Encode 4 BiDi cutout symbols
  else {
    while (dcc_encoder->state == BiDi) {
      size_t const tmp = copy_encoder->encode(copy_encoder,
                                              channel,
                                              &dcc_encoder->bidi_symbol,
                                              sizeof(rmt_symbol_word_t),
                                              &state);
      encoded_symbols += tmp;
      dcc_encoder->num_symbols += tmp;
      if (state & RMT_ENCODING_COMPLETE &&
          dcc_encoder->num_symbols >= 8u / 2u) {
        dcc_encoder->num_symbols = 0u;
        dcc_encoder->state = Zimo0;
      }
      if (state & RMT_ENCODING_MEM_FULL) break;
    }
  }

  *ret_state = state;
  return encoded_symbols;
}

static size_t RMT_IRAM_ATTR
rmt_encode_dcc_zimo0(rmt_dcc_encoder_t* dcc_encoder,
                     rmt_channel_handle_t channel,
                     rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;

  // Skip
  if (!dcc_encoder->flags.zimo0) {
    state |= RMT_ENCODING_COMPLETE;
    dcc_encoder->state = Preamble;
  }
  // Encode ZIMO 0
  else {
    encoded_symbols += rmt_encode_dcc_bit(
      dcc_encoder, channel, &state, &dcc_encoder->zero_symbol);
    if (state & RMT_ENCODING_COMPLETE) dcc_encoder->state = Preamble;
  }

  *ret_state = state;
  return encoded_symbols;
}

static size_t RMT_IRAM_ATTR
rmt_encode_dcc_preamble(rmt_dcc_encoder_t* dcc_encoder,
                        rmt_channel_handle_t channel,
                        rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encoder_handle_t copy_encoder = dcc_encoder->copy_encoder;

  while (dcc_encoder->state == Preamble) {
    size_t const tmp = copy_encoder->encode(copy_encoder,
                                            channel,
                                            &dcc_encoder->one_symbol,
                                            sizeof(rmt_symbol_word_t),
                                            &state);
    encoded_symbols += tmp;
    dcc_encoder->num_symbols += tmp;
    if (state & RMT_ENCODING_COMPLETE &&
        dcc_encoder->num_symbols >= dcc_encoder->num_preamble_symbols) {
      dcc_encoder->num_symbols = 0u;
      dcc_encoder->state = Start;
    }
    if (state & RMT_ENCODING_MEM_FULL) break;
  }

  *ret_state = state;
  return encoded_symbols;
}

static size_t RMT_IRAM_ATTR
rmt_encode_dcc_start(rmt_dcc_encoder_t* dcc_encoder,
                     rmt_channel_handle_t channel,
                     rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  encoded_symbols +=
    rmt_encode_dcc_bit(dcc_encoder, channel, &state, &dcc_encoder->zero_symbol);
  if (state & RMT_ENCODING_COMPLETE) dcc_encoder->state = Data;
  *ret_state = state;
  return encoded_symbols;
}

static size_t RMT_IRAM_ATTR rmt_encode_dcc_data(rmt_dcc_encoder_t* dcc_encoder,
                                                rmt_channel_handle_t channel,
                                                void const* primary_data,
                                                size_t data_size,
                                                rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encoder_handle_t bytes_encoder = dcc_encoder->bytes_encoder;
  uint8_t const* data = (uint8_t const*)primary_data;

  size_t const tmp =
    bytes_encoder->encode(bytes_encoder,
                          channel,
                          &data[dcc_encoder->num_symbols / CHAR_BIT],
                          sizeof(uint8_t),
                          &state);
  encoded_symbols += tmp;
  dcc_encoder->num_symbols += tmp;
  if (state & RMT_ENCODING_COMPLETE &&
      dcc_encoder->num_symbols >= data_size * CHAR_BIT) {
    dcc_encoder->num_symbols = 0u;
    dcc_encoder->state = End;
  }

  *ret_state = state;
  return encoded_symbols;
}

static size_t RMT_IRAM_ATTR rmt_encode_dcc_end(rmt_dcc_encoder_t* dcc_encoder,
                                               rmt_channel_handle_t channel,
                                               rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  encoded_symbols +=
    rmt_encode_dcc_bit(dcc_encoder, channel, &state, &dcc_encoder->end_symbol);
  if (state & RMT_ENCODING_COMPLETE) {
    dcc_encoder->num_symbols = 0u;
    dcc_encoder->state = BiDi;
  }
  *ret_state = state;
  return encoded_symbols;
}

static size_t RMT_IRAM_ATTR rmt_encode_dcc(rmt_encoder_t* encoder,
                                           rmt_channel_handle_t channel,
                                           void const* primary_data,
                                           size_t data_size,
                                           rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encode_state_t session_state = RMT_ENCODING_RESET;
  rmt_dcc_encoder_t* dcc_encoder =
    __containerof(encoder, rmt_dcc_encoder_t, base);

  switch (dcc_encoder->state) {
    case BiDi:
      encoded_symbols +=
        rmt_encode_dcc_bidi(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough

    case Zimo0:
      encoded_symbols +=
        rmt_encode_dcc_zimo0(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough

    case Preamble:
      encoded_symbols +=
        rmt_encode_dcc_preamble(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough

    case Start:
    start:
      encoded_symbols +=
        rmt_encode_dcc_start(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough

    case Data:
      encoded_symbols += rmt_encode_dcc_data(
        dcc_encoder, channel, primary_data, data_size, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      if (dcc_encoder->state < End) goto start;
      // fallthrough

    case End:
      encoded_symbols +=
        rmt_encode_dcc_end(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_COMPLETE) state |= RMT_ENCODING_COMPLETE;
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough
  }

  *ret_state = state;
  return encoded_symbols;
}

static esp_err_t rmt_del_dcc_encoder(rmt_encoder_t* encoder) {
  rmt_dcc_encoder_t* dcc_encoder =
    __containerof(encoder, rmt_dcc_encoder_t, base);
  rmt_del_encoder(dcc_encoder->copy_encoder);
  rmt_del_encoder(dcc_encoder->bytes_encoder);
  free(dcc_encoder);
  return ESP_OK;
}

static esp_err_t RMT_IRAM_ATTR rmt_dcc_encoder_reset(rmt_encoder_t* encoder) {
  rmt_dcc_encoder_t* dcc_encoder =
    __containerof(encoder, rmt_dcc_encoder_t, base);
  rmt_encoder_reset(dcc_encoder->copy_encoder);
  rmt_encoder_reset(dcc_encoder->bytes_encoder);
  dcc_encoder->num_symbols = 0u;
  dcc_encoder->state = Zimo0;
  return ESP_OK;
}

static esp_err_t rmt_new_dcc_encoder(local_dcc_encoder_config_t const* config,
                                     rmt_encoder_handle_t* ret_encoder) {
  esp_err_t ret = ESP_OK;
  rmt_dcc_encoder_t* dcc_encoder = NULL;
  
  ESP_GOTO_ON_FALSE(
    config && ret_encoder &&                                        //
      config->num_preamble >= DCC_TX_MIN_PREAMBLE_BITS &&           //
      config->num_preamble <= DCC_TX_MAX_PREAMBLE_BITS &&           //
      (!config->bidibit_duration ||                                 //
       (config->bidibit_duration >= DCC_TX_MIN_BIDI_BIT_TIMING &&   //
        config->bidibit_duration <= DCC_TX_MAX_BIDI_BIT_TIMING)) && //
      config->bit1_duration >= DCC_TX_MIN_BIT_1_TIMING &&           //
      config->bit1_duration <= DCC_TX_MAX_BIT_1_TIMING &&           //
      config->bit0_duration >= DCC_TX_MIN_BIT_0_TIMING &&           //
      config->bit0_duration <= DCC_TX_MAX_BIT_0_TIMING &&           //
      config->endbit_duration <= DCC_TX_MAX_BIT_1_TIMING,           //
    ESP_ERR_INVALID_ARG,
    err,
    TAG,
    "invalid argument");

  // Allocate RMT encoder memory (using generic ESP-IDF helper)
  dcc_encoder = (rmt_dcc_encoder_t*)rmt_alloc_encoder_mem(sizeof(rmt_dcc_encoder_t));
  ESP_GOTO_ON_FALSE(
    dcc_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for dcc encoder");

  dcc_encoder->base.encode = rmt_encode_dcc;
  dcc_encoder->base.del = rmt_del_dcc_encoder;
  dcc_encoder->base.reset = rmt_dcc_encoder_reset;

  rmt_copy_encoder_config_t copy_encoder_config = {};
  ESP_GOTO_ON_ERROR(
    rmt_new_copy_encoder(&copy_encoder_config, &dcc_encoder->copy_encoder),
    err,
    TAG,
    "create copy encoder failed");

  // Number of preamble symbols
  dcc_encoder->num_preamble_symbols = config->num_preamble;

  // Setup RMT symbols
  if (config->bidibit_duration) {
    dcc_encoder->bidi_symbol = (rmt_symbol_word_t){
      .duration0 = config->bidibit_duration,
      .level0 = config->flags.level0,
      .duration1 = config->bidibit_duration,
      .level1 = static_cast<uint16_t>(!config->flags.level0),
    };
  } else {
    dcc_encoder->bidi_symbol = (rmt_symbol_word_t){0};
  }

  dcc_encoder->one_symbol = (rmt_symbol_word_t){
    .duration0 = config->bit1_duration,
    .level0 = config->flags.level0,
    .duration1 = config->bit1_duration,
    .level1 = static_cast<uint16_t>(!config->flags.level0),
  };
  dcc_encoder->zero_symbol = (rmt_symbol_word_t){
    .duration0 = config->bit0_duration,
    .level0 = config->flags.level0,
    .duration1 = config->bit0_duration,
    .level1 = static_cast<uint16_t>(!config->flags.level0),
  };
  dcc_encoder->end_symbol = (rmt_symbol_word_t){
    .duration0 = config->bit1_duration,
    .level0 = config->flags.level0,
    .duration1 = static_cast<uint16_t>(config->endbit_duration ? config->endbit_duration : config->bit1_duration),
    .level1 = static_cast<uint16_t>(!config->flags.level0),
  };

  // Initial state
  dcc_encoder->state = Zimo0;

  // Flags
  dcc_encoder->flags.zimo0 = config->flags.zimo0;

  rmt_bytes_encoder_config_t bytes_encoder_config = {
    .bit1 = dcc_encoder->one_symbol,
    .bit0 = dcc_encoder->zero_symbol,
    .flags = { .msb_first = true }
  };
  
  ESP_GOTO_ON_ERROR(
    rmt_new_bytes_encoder(&bytes_encoder_config, &dcc_encoder->bytes_encoder),
    err,
    TAG,
    "create bytes encoder failed");

  *ret_encoder = &dcc_encoder->base;
  return ESP_OK;

err:
  if (dcc_encoder) {
    if (dcc_encoder->copy_encoder) rmt_del_encoder(dcc_encoder->copy_encoder);
    if (dcc_encoder->bytes_encoder) rmt_del_encoder(dcc_encoder->bytes_encoder);
    free(dcc_encoder);
  }
  return ret;
}

// =============================================================================
// C++ Wrapper Class: dcc::rmt::DccRmtTransmitter Implementation
// =============================================================================

namespace dcc {
namespace rmt {

DccRmtTransmitter::DccRmtTransmitter()
    : m_rmt_channel(nullptr),
      m_dcc_encoder(nullptr),
      m_packet_queue(nullptr),
      m_task_handle(nullptr),
      m_initialized(false) {
    std::memset(&m_config, 0, sizeof(m_config));
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

    ESP_LOGI(TAG, "Initializing DCC RMT Transmitter on GPIO %d...", m_config.gpio_num);
    ESP_LOGI(TAG, "Settings: Preamble: %d, BiDi: %s, Timings (1/0/End): %d/%d/%d µs",
             m_config.num_preamble,
             m_config.enable_bidi ? "ENABLED" : "DISABLED",
             m_config.bit1_duration,
             m_config.bit0_duration,
             m_config.endbit_duration);

    // 1. Allocate RMT TX Channel
    // We configure a 1MHz clock so that 1 tick = 1 microsecond.
    // 64 memory symbols is sufficient and extremely safe for all chips (including ESP32-C3).
    rmt_tx_channel_config_t rmt_chan_config = {
        .gpio_num = static_cast<gpio_num_t>(m_config.gpio_num),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1 MHz -> 1 tick = 1 µs
        .mem_block_symbols = 64,  // Safe memory block size
        .trans_queue_depth = 2,   // Allows double-buffering (gapless transmit)
        .intr_priority = 0,
        .flags = {
            .invert_out = 0,
            .with_dma = 0
        }
    };

    esp_err_t err = rmt_new_tx_channel(&rmt_chan_config, &m_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
        return false;
    }

    // Enable RMT Channel
    err = rmt_enable(m_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(err));
        deinit();
        return false;
    }

    // 2. Create the DCC Custom RMT Encoder
    local_dcc_encoder_config_t encoder_cfg = {
        .num_preamble = m_config.num_preamble,
        
        // Pass the BiDi duration if enabled, else 0 (disables the local cutout encoder loop)
        .bidibit_duration = static_cast<uint8_t>(m_config.enable_bidi ? m_config.bidibit_duration : 0),
        
        .bit1_duration = m_config.bit1_duration,
        .bit0_duration = m_config.bit0_duration,
        .endbit_duration = m_config.endbit_duration,
        .flags = {
            .level0 = m_config.flags.level0,
            .zimo0 = m_config.flags.zimo0
        }
    };

    err = rmt_new_dcc_encoder(&encoder_cfg, &m_dcc_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DCC RMT Encoder: %s", esp_err_to_name(err));
        deinit();
        return false;
    }

    // 3. Create FreeRTOS packet queue
    m_packet_queue = xQueueCreate(m_config.queue_size, sizeof(QueuedPacket));
    if (m_packet_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS packet queue");
        deinit();
        return false;
    }

    m_initialized = true;

    // 4. Start the background continuous transmission task
    // We pin it to Core 1 (or let OS decide on single-core C3) to keep track feed smooth.
    BaseType_t task_err;
#if CONFIG_FREERTOS_UNICORE
    task_err = xTaskCreate(
        txTask,
        "dcc_rmt_tx_task",
        3072,
        this,
        configMAX_PRIORITIES - 1, // Run at very high priority to prevent track timing jitter
        &m_task_handle
    );
#else
    task_err = xTaskCreatePinnedToCore(
        txTask,
        "dcc_rmt_tx_task",
        3072,
        this,
        configMAX_PRIORITIES - 1,
        &m_task_handle,
        1 // Pin to Core 1 (applications usually run on Core 0 or let scheduler balance)
    );
#endif

    if (task_err != pdPASS) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS transmission task");
        deinit();
        return false;
    }

    ESP_LOGI(TAG, "DCC RMT Transmitter successfully initialized!");
    return true;
}

bool DccRmtTransmitter::sendPacket(const uint8_t* payload, size_t length, uint32_t wait_ms) {
    if (!m_initialized || payload == nullptr || length == 0) {
        return false;
    }

    if (length > MAX_DCC_PACKET_SIZE) {
        ESP_LOGW(TAG, "Packet length %d exceeds maximum allowed size %d", length, MAX_DCC_PACKET_SIZE);
        return false;
    }

    QueuedPacket packet;
    std::memcpy(packet.data, payload, length);
    packet.length = static_cast<uint8_t>(length);

    BaseType_t ret = xQueueSend(m_packet_queue, &packet, pdMS_TO_TICKS(wait_ms));
    return (ret == pdTRUE);
}

void DccRmtTransmitter::deinit() {
    m_initialized = false;

    // Stop background task
    if (m_task_handle != nullptr) {
        vTaskDelete(m_task_handle);
        m_task_handle = nullptr;
    }

    // Release Queue
    if (m_packet_queue != nullptr) {
        vQueueDelete(m_packet_queue);
        m_packet_queue = nullptr;
    }

    // Release RMT encoder
    if (m_dcc_encoder != nullptr) {
        rmt_del_encoder(m_dcc_encoder);
        m_dcc_encoder = nullptr;
    }

    // Release RMT TX Channel
    if (m_rmt_channel != nullptr) {
        rmt_disable(m_rmt_channel);
        rmt_del_channel(m_rmt_channel);
        m_rmt_channel = nullptr;
    }

    ESP_LOGI(TAG, "DCC RMT Transmitter deinitialized");
}

void DccRmtTransmitter::txTask(void* pvParameters) {
    auto* instance = static_cast<DccRmtTransmitter*>(pvParameters);
    instance->txTaskLoop();
}

void DccRmtTransmitter::txTaskLoop() {
    QueuedPacket packet;
    
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0, // Transmit once
        .flags = {
            .eot_level = 0,
            .queue_nonblocking = 0 // Block until RMT channel has space in its hardware queue
        }
    };

    // Pre-allocated NMRA Standard DCC Idle Packet:
    // [0xFF (Address), 0x00 (Data), 0xFF (Checksum)]
    static const uint8_t idle_packet_data[] = { 0xFF, 0x00, 0xFF };

    ESP_LOGI(TAG, "Background transmission loop started.");

    while (m_initialized) {
        // We poll the queue with a 5 millisecond timeout.
        // If a user packet is available, we transmit it.
        // If the queue is empty, we fall back to transmitting an Idle Packet.
        if (xQueueReceive(m_packet_queue, &packet, pdMS_TO_TICKS(5)) == pdTRUE) {
            esp_err_t err = rmt_transmit(
                m_rmt_channel,
                m_dcc_encoder,
                packet.data,
                packet.length,
                &transmit_config
            );
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "RMT Transmit user packet failed: %s", esp_err_to_name(err));
            }
        } else {
            // Queue is empty, send DCC Idle packet to maintain track voltage and timing carrier
            esp_err_t err = rmt_transmit(
                m_rmt_channel,
                m_dcc_encoder,
                idle_packet_data,
                sizeof(idle_packet_data),
                &transmit_config
            );
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "RMT Transmit idle packet failed: %s", esp_err_to_name(err));
            }
        }
    }

    vTaskDelete(nullptr);
}

} // namespace rmt
} // namespace dcc
