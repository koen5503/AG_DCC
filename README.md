# Isolated ESP32 RMT DCC Transmitter

A high-performance, standalone, and modular DCC (Digital Command Control) signal transmitter for **ESP32** and **ESP32-C3** microcontrollers, written specifically for the modern **ESP-IDF v5.x RMT (Remote Control)** peripheral driver.

This module provides a thread-safe, double-buffered C++ class that handles real-time DCC signal encoding and continuously drives the track without consuming main-thread CPU resources.

---

## 🚀 Features

- **Pure ESP-IDF v5.x RMT Driver Architecture**: Leverages the new v5.x sub-encoder model for optimal resource management and future compatibility.
- **Double-Buffered Gapless Transmit**: Utilizes a dual-slot hardware transaction queue to ensure there are **zero gaps** between sequential DCC packets, preserving track waveform integrity.
- **Automatic Idle Feeding**: Embedded H-bridge decoders (locomotives) require constant AC-like power to avoid rebooting. This module features a background FreeRTOS task that automatically transmits standard DCC idle packets (`[0xFF, 0x00, 0xFF]`) whenever the user packet queue is empty.
- **Configurable BiDi Cutout**: Supports a configurable 240 µs Bidirectional (BiDi) cutout zone (NMRA S-9.2.1 / RCN-218) after each packet, which can be toggled on or off at runtime via a simple configuration flag.
- **Target Chip Independence**: Safe, conservative memory constraints make this code completely compatible out-of-the-box with both Xtensa dual-core chips (standard ESP32, ESP32-S3) and single-core RISC-V chips (ESP32-C3, ESP32-C6).
- **No External Dependencies**: 100% stripped of all templates, custom static math headers, or Zimo Template Library (ZTL) vectors. Accepts standard `uint8_t` buffers.

---

## ⚖️ License Notices & Architectural Evolution

This repository was originally initialized as a **derivative work** branched from the official [ZIMO Elektronik DCC Repository](https://github.com/zimo-elektronik/dcc) and initially incorporated early ZIMO template structures.

### Current Architectural State:
* **NMRA-Compliant Global Standards**: The entire codebase has been completely refactored and rewritten. All DCC packet generation, byte layouts, XOR checksums, locomotive speeds, and stationary accessory (turnout) controls are now derived **directly from the official global NMRA S-9.2 and S-9.2.1 specifications**.
* **100% Stripped of ZTL**: The code is now completely stripped of all C++ templates, custom static mathematical acceleration headers, and specialized Zimo Template Library (ZTL) vectors/containers. It runs on clean, portable, and standard C/C++ primitives and standard ESP-IDF v5 APIs.
* **Low-Level Hardware Innovations**: The current transmitter architecture uses a custom low-level circular GDMA hardware descriptor ring ($0 \to 1 \to 2 \to 0$) in physical SRAM with disabled hardware writeback (`out_auto_wrback = false`), achieving **absolute 0% CPU overhead** for continuous idle signal generation.
* **Stable Task-Context RX**: The receiver has been updated with safe double-buffering ping-pong buffers and stable task-context DMA re-arming to ensure 100% crash-free operation.

### Legal Compliance & Attributions:
* The original low-level RMT custom DCC encoder concept was based on work by **Vincent Hamp** (dated 08/01/2023).
* In accordance with the **Mozilla Public License, Version 2.0 (MPL-2.0)**:
  * Modified source files retain original author copyrights where applicable and are licensed under the MPL-2.0.
  * A copy of the full MPL-2.0 text is provided in the `LICENSE` file.
  * This repository remains fully open-source and governed by the MPL-2.0, with links and credits back to the original upstream repository.

---

## 🛠️ Project Structure

The isolated modular component is composed of just three files:
- `DccRmtTransmitter.hpp`: Clean, object-oriented API and configurations in the `dcc::rmt` namespace.
- `DccRmtTransmitter.cpp`: Encapsulated C RMT custom encoder and C++ FreeRTOS background-task driver.
- `app_main.cpp`: A comprehensive testing and instrumentation runner loaded with oscilloscope diagnostic scenarios and multi-console logging configurations.

---

## 🔌 Probing & Oscilloscope Safety

> [!WARNING]
> **ELECTRICAL SAFETY WARNING:**
> When probing DCC signals with an oscilloscope:
> 1. Connect your oscilloscope's ground clip **ONLY to the ESP32 Ground (GND) pin**, and probe the direct **GPIO Pin (GPIO21 on standard ESP32, GPIO8 on ESP32-C3)**. At this stage, the signal is a safe 3.3V logic level.
> 2. **DO NOT connect your oscilloscope probes directly to the railway tracks!** Track boosters output high-voltage biphasic AC-like signals (±15V to ±22V). Probing this directly with a grounded oscilloscope will cause a dead short-circuit and may instantly destroy your oscilloscope, computer, and microcontroller.
> 3. To probe track outputs directly, you **MUST** use an isolated high-voltage differential probe or run your scope from a fully isolated battery/UPS supply.

---

## 📈 Oscilloscope Diagnostic Scenarios

The included `app_main.cpp` runs an endless cycle of 4 distinct test patterns designed to be easily analyzed on an oscilloscope screen (configure your scope trigger to **Negative Edge, Normal Trigger Mode**):

1. **Scenario 1: Steady Idle (5s)**: Continuous `[0xFF, 0x00, 0xFF]` NMRA idle trains. Excellent for calibrating standard bit durations (58 µs for '1' and 100 µs for '0').
2. **Scenario 2: Locomotive Speed sweep (5s)**: Sends active speed step instructions to Loco Address 3, sweeping speeds and toggling direction. You will see the visual width of data pulses changing live.
3. **Scenario 3: Accessory / Turnout Commands (3s)**: Toggles switch commands ON and OFF to verify accessory-decoder bit patterns.
4. **Scenario 4: BiDi Cutout Toggle**: Switches the BiDi cutout mode on and off every 5 seconds. Probing the end of the packet will show a flat **240 µs zero-current cutout** appearing and disappearing immediately following the end-bit.

---

## 📦 Integration

To drop this module into any ESP-IDF v5.x project:

1. Copy `DccRmtTransmitter.hpp` and `DccRmtTransmitter.cpp` into your project's `main/` or components directory.
2. In your `CMakeLists.txt`, register the source files and add `esp_driver_rmt` to the requirements:
   ```cmake
   idf_component_register(SRCS "main.cpp" "DccRmtTransmitter.cpp"
                          INCLUDE_DIRS "."
                          REQUIRES driver esp_driver_rmt)
   ```
3. Initialize and use the class:
   ```cpp
   #include "DccRmtTransmitter.hpp"
   
   dcc::rmt::DccRmtTransmitter transmitter;
   
   void app_main() {
       dcc::rmt::TransmitterConfig config;
       config.gpio_num = 21;       // Pin to drive
       config.enable_bidi = true;  // Enable cutout
       
       if (transmitter.init(config)) {
           // Queue packets asynchronously
           uint8_t pkt[3] = { 0x03, 0x0C, 0x0F }; // Addr 3, speed command, checksum
           transmitter.sendPacket(pkt, sizeof(pkt));
       }
   }
   ```

---

## 👽 Compiling with PlatformIO

This repository features out-of-the-box cross-platform compilation support for VS Code with the **PlatformIO** extension. The `platformio.ini` file in the root is pre-configured to build the project using the native ESP-IDF framework while pointing to the standard `main/` directory structure.

To compile, flash, and monitor using PlatformIO:

1. Install the **PlatformIO IDE** extension in VS Code.
2. Open VS Code, go to PlatformIO Home, and select **Open Project**.
3. Select this project's root folder (`AG_DCC`).
4. In the PlatformIO Environment Switcher (bottom toolbar), choose your target:
   - `env:esp32dev` (for standard ESP32 boards)
   - `env:esp32c3` (for ESP32-C3 dev boards)
5. Click **Build** (checkmark icon) or **Upload and Monitor** (arrow & plug icons) to flash and observe real-time console logs!
