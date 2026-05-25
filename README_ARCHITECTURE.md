# Low-level Hardware Architecture: DCC DMA & RMT (ESP32-S3 & ESP32-C3)

This document provides a highly technical, annotated architectural walkthrough of the standalone, hardware-driven DCC Transmitter (TX) and Receiver (RX) modules. It explains the low-level General DMA (GDMA) and Remote Control (RMT) configurations, memory layouts, register-level modifications, and how we completely eliminate the need for CPU Interrupt Service Routines (ISRs) for continuous signal generation.

---

## 1. Transmitter Architecture (DccRmtTransmitter)

The transmitter's design goal is to achieve **absolute 0% CPU overhead** during continuous DCC idle packet transmission, while allowing the CPU to safely inject command packets (locomotive speed steps, function groups, accessory turnouts) asynchronously and seamlessly.

### 1.1 The Circular Hardware Descriptor Chain
Rather than using high-level software queues, we build a **circular ring of three link-list descriptors (`lldesc_t`)** directly in internal DMA-capable SRAM:

```
[SRAM Chained Ring]
Descriptor 0 (buf: m_dma_buffers[0], owner: HW) 
     |
     v
Descriptor 1 (buf: m_dma_buffers[1], owner: HW) 
     |
     v
Descriptor 2 (buf: m_dma_buffers[2], owner: HW) 
     |
     +-------> (loops back to Descriptor 0)
```

* **The Loop**: Each descriptor's `qe.stqe_next` points to the next descriptor, closing the loop with the third descriptor pointing back to the first.
* **Continuous Streaming**: Once the GDMA engine is started via `gdma_start()`, it traverses this ring indefinitely. It continuously streams RMT symbols from the three SRAM buffers into the RMT TX FIFO.
* **Continuous Waveform**: Because the RMT transmitter FSM reads continuously from its FIFO, it outputs a perfectly continuous DCC square wave without a single microsecond of signal gap.

---

### 1.2 Eliminating the Need for CPU Interrupts (0% CPU Idle)
In a standard DMA setup, the CPU handles interrupts at the end of every descriptor transfer (EOF) to load the next block of data. In DCC, a single idle packet takes only $6.3\text{ ms}$. If we used standard interrupts, the CPU would be forced to handle **160 interrupts per second** even when the train controller is completely idle, consuming valuable clock cycles and causing context switches.

We achieve **0% CPU overhead** by combining three low-level hardware disarming techniques:

1. **Hardware Interrupt Masking**:
   We mask the GDMA TX interrupts at the peripheral register level during initialization:
   ```cpp
   gdma_ll_tx_enable_interrupt(&GDMA, configured_dma_ch, GDMA_LL_EVENT_TX_TOTAL_EOF | GDMA_LL_EVENT_TX_EOF, false);
   ```
   This tells the GDMA hardware **never** to signal the CPU when it completes a descriptor transfer. The GDMA controller loops silently in the background solely in hardware.

2. **Disabling Automatic Descriptor Writeback**:
   By default, the GDMA engine automatically writes the descriptor structure back to SRAM at the end of each transfer, changing the `owner` bit from `1` (Hardware-owned) to `0` (CPU-owned). If this occurred in our circular loop, the GDMA would halt as soon as it wrapped around to the first completed descriptor.
   We disable writeback at the hardware register level:
   ```cpp
   gdma_ll_tx_enable_auto_write_back(&GDMA, configured_dma_ch, false);
   ```
   This prevents the hardware from ever modifying the descriptor memory. The `owner` bit remains `1` (Hardware-owned) permanently, allowing the GDMA to loop forever.

3. **Disabling RMT Continuous Mode**:
   To prevent the RMT peripheral from isolating itself by looping its internal 64-symbol RAM block (which would lock out our SRAM buffer modifications), we explicitly disable RMT loop mode:
   ```cpp
   rmt_ll_tx_enable_loop(&RMT, channel_id, false);
   ```
   This forces the RMT transmitter FSM to always pull fresh symbol data from the GDMA FIFO, keeping the RMT directly synchronized with our SRAM buffers.

---

### 1.3 Safe, Race-Free Asynchronous Packet Injection
When the user triggers a turnout toggle or speed step, we must inject this command packet into the active, circulating ring. If we modify a descriptor that the GDMA hardware is currently reading or pre-fetching, we risk a corrupted transmission or an ownership collision.

We solve this using **Zero-Halt Register-Aligned Writing**:

1. **Read the GDMA Pre-fetch Register**:
   We query the hardware's internal register to find out which descriptor is currently active/pre-fetched:
   ```cpp
   uint32_t active_addr = gdma_ll_tx_get_prefetched_desc_addr(&GDMA, configured_dma_ch);
   ```
   We map this address to our descriptor index $K \in \{0, 1, 2\}$.
2. **Select the Safe Descriptor**:
   Since GDMA is busy reading descriptor $K$ (which takes at least $6.3\text{ ms}$), the next descriptor $(K + 1) \% 3$ is guaranteed to be completely idle and safe from GDMA access.
3. **Write and Flush**:
   We format our command packet symbols directly into the idle buffer, update its descriptor `size` and `length`, and flush the CPU cache:
   ```cpp
   esp_cache_msync(m_dma_buffers[target_idx], ...);
   esp_cache_msync(&m_dma_descriptors[target_idx], ...);
   ```
   Because `owner` remains `1` at all times, the GDMA hardware never stalls. It simply reads the updated packet size and symbols automatically on its next loop transition!

---

## 2. Receiver Architecture (DccDecoder)

The receiver's design goal is to capture high-speed edge transitions on a GPIO pin in real-time, measure pulse widths in microseconds, reconstruct DCC bits, and decode NMRA packets.

### 2.1 Double-Buffering with DMA Partial RX
* **Double Buffering**: We allocate two large buffers (`m_rx_buffer[0]` and `m_rx_buffer[1]`, each $1024$ symbols capacity) in cache-aligned internal SRAM:
  ```cpp
  alignas(32) rmt_symbol_word_t m_rx_buffer[2][1024];
  ```
* **Partial RX Mode**: On the ESP32-S3, we enable `en_partial_rx = true`. The RMT RX DMA engine continuously writes captured edge symbols into our active buffer and triggers `on_recv_done()` interrupts in chunks (partial receives) before the buffer fills up.

---

### 2.2 Task-Context Re-arming (ISR Safety & Robustness)
In a real-time system, executing complex driver calls inside an Interrupt Service Routine (ISR) is highly dangerous and unstable. Mutexes, memory allocations, and state modifications inside an ISR can cause silent deadlocks or driver failures.

We ensure absolute stability by moving the RMT receiver re-arming out of the ISR and into a high-priority FreeRTOS task:

```
[GPIO Pin 6] ---> [RMT RX DMA Engine]
                       |
                       v
            [on_recv_done ISR Callback]
                       |  (If transaction ended, swap m_active_buffer)
                       v
            [xQueueSendFromISR] ---> [symbol_ready_queue]
                                             |
                                             v
                                   [runDecoderTask (Core 1 Task)]
                                             | (Parses symbols & bits)
                                             | (If is_last == true)
                                             v
                                   [rmt_receive() Re-arm]
```

1. **Zero-Copy Queue Passing**:
   When the RMT RX DMA fills a segment of our active buffer, the `on_recv_done()` callback (ISR) is triggered. It packages the raw buffer segment pointer and the `is_last` flag into a `SymbolMsg` and pushes it to a FreeRTOS queue:
   ```cpp
   SymbolMsg msg = {
       .symbols = edata->received_symbols,
       .count = num_symbols,
       .is_last = edata->flags.is_last
   };
   xQueueSendFromISR(self->m_symbol_ready_queue, &msg, &xHigherPriorityTaskWoken);
   ```
2. **Safe Swap in ISR**:
   If the transaction has completed (`edata->flags.is_last == true`, indicating a 12ms packet gap timeout or a filled buffer), we immediately swap the active buffer index in the ISR so that the background task can parse the finished buffer safely:
   ```cpp
   self->m_active_buffer = 1 - self->m_active_buffer;
   ```
3. **Re-arm in Task Context**:
   The background decoding task (`runDecoderTask()`) blocks on the queue. When it pops a message, it parses the DCC pulse timings. If `msg.is_last` is `true`, the task safely calls the public RMT API `rmt_receive()` from the task context to re-initialize the DMA channel on our newly swapped buffer:
   ```cpp
   if (msg.is_last && m_initialized) {
       rmt_receive(m_rx_channel, m_rx_buffer[m_active_buffer], buffer_size, &recv_cfg);
   }
   ```

### 2.3 Why this is 100% Race-Free & Lossless
* **Zero Driver Lockouts**: By calling `rmt_receive()` exclusively in task context, the driver state machine is never violated, and deadlocks are physically impossible.
* **No Memory Overwrites**: Because `rmt_receive()` is not called until *after* the task processes the final message chunk from the queue, the DMA hardware cannot start writing new data over the old buffer until the parsing engine has fully finished reading it.
* **High-Priority Immediate Execution**: The background task runs at FreeRTOS priority `16` (extremely high, above the Web Server and Wi-Fi tasks). This ensures it re-arms the RMT RX within a few microseconds of the transaction completion, preventing any signal capture dropouts.
