/**
 * @file index_html.h
 * @brief Embedded Premium Web Interface Dashboard for the DCC RMT Controller.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-20
 * 
 * Stored as a raw C++ string literal (`INDEX_HTML`) to compile directly
 * into the ESP32 instruction flash, avoiding filesystem overhead.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#pragma once

const char INDEX_HTML[] = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 DCC RMT Command Center</title>
    <style>
        :root {
            --bg-primary: #0a0b10;
            --bg-secondary: #12131a;
            --panel-bg: #161822;
            --panel-border: #242736;
            --accent-glow: #5850ec;
            --accent-success: #057a55;
            --accent-warning: #b45309;
            --accent-danger: #9b1c1c;
            --text-primary: #f9fafb;
            --text-secondary: #9ca3af;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            -webkit-tap-highlight-color: transparent;
        }

        body {
            background-color: var(--bg-primary);
            color: var(--text-primary);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            padding: 20px;
        }

        header {
            max-width: 1200px;
            width: 100%;
            margin: 0 auto 20px auto;
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px 25px;
            background: var(--panel-bg);
            border: 1px solid var(--panel-border);
            border-radius: 12px;
        }

        .logo-area {
            display: flex;
            align-items: center;
            gap: 12px;
        }

        .logo-icon {
            width: 32px;
            height: 32px;
            background: #242736;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: 700;
            font-size: 18px;
        }

        .logo-text h1 {
            font-size: 20px;
            font-weight: 700;
            letter-spacing: 0.5px;
            color: var(--text-primary);
        }

        .logo-text p {
            font-size: 11px;
            color: var(--text-secondary);
        }

        .status-badge {
            display: flex;
            align-items: center;
            gap: 8px;
            padding: 6px 14px;
            background: #12131a;
            border-radius: 12px;
            font-size: 13px;
            font-weight: 600;
            border: 1px solid var(--panel-border);
        }

        .status-dot {
            width: 8px;
            height: 8px;
            background-color: #10b981;
            border-radius: 50%;
        }

        .main-container {
            max-width: 1600px;
            width: 100%;
            margin: 0 auto;
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            gap: 20px;
            flex-grow: 1;
        }

        @media (max-width: 1200px) {
            .main-container {
                grid-template-columns: 1fr;
            }
        }

        .panel {
            background: var(--panel-bg);
            border: 1px solid var(--panel-border);
            border-radius: 12px;
            padding: 25px;
            display: flex;
            flex-direction: column;
            gap: 20px;
        }

        .panel-title {
            font-size: 18px;
            font-weight: 700;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--panel-border);
            padding-bottom: 12px;
        }

        .panel-title span {
            color: var(--text-secondary);
            font-size: 13px;
            font-weight: 400;
        }

        .control-row {
            display: flex;
            gap: 15px;
            align-items: center;
        }

        .input-group {
            display: flex;
            flex-direction: column;
            gap: 6px;
            flex-grow: 1;
        }

        .input-group label {
            font-size: 12px;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            color: var(--text-secondary);
        }

        .input-group input, .input-group select {
            background: #12131a;
            border: 1px solid var(--panel-border);
            border-radius: 8px;
            padding: 12px;
            color: var(--text-primary);
            font-size: 16px;
            font-weight: 600;
            outline: none;
        }

        .input-group input:focus {
            border-color: var(--accent-glow);
        }

        .throttle-container {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 15px;
            padding: 10px 0;
        }

        .speed-gauge {
            width: 140px;
            height: 140px;
            border-radius: 12px;
            background: #12131a;
            border: 2px solid var(--panel-border);
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
        }

        .speed-value {
            font-size: 42px;
            font-weight: 700;
            color: var(--text-primary);
            line-height: 1;
        }

        .speed-unit {
            font-size: 12px;
            font-weight: 600;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-top: 4px;
        }

        .slider-wrapper {
            width: 100%;
            padding: 10px 0;
            display: flex;
            align-items: center;
            gap: 15px;
        }

        .slider-icon {
            font-size: 20px;
            color: var(--text-secondary);
        }

        .slider-wrapper input[type="range"] {
            -webkit-appearance: none;
            width: 100%;
            height: 8px;
            border-radius: 4px;
            background: #12131a;
            outline: none;
        }

        .slider-wrapper input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: var(--accent-glow);
            cursor: pointer;
        }

        .dir-button-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            width: 100%;
        }

        .btn {
            background: #12131a;
            border: 1px solid var(--panel-border);
            border-radius: 8px;
            padding: 14px;
            color: var(--text-primary);
            font-size: 15px;
            font-weight: 700;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
            outline: none;
        }

        .btn:hover {
            background: #1e202f;
        }

        .btn.active-fwd {
            background: var(--accent-glow);
            border-color: var(--accent-glow);
            color: white;
        }

        .btn.active-rev {
            background: var(--accent-warning);
            border-color: var(--accent-warning);
            color: white;
        }

        .btn-stop {
            background: var(--accent-danger);
            border: none;
            color: white;
            font-size: 16px;
            font-weight: 700;
        }

        .btn-stop:hover {
            background: #b91c1c;
        }

        .func-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 10px;
        }

        .btn-func {
            background: #12131a;
            border: 1px solid var(--panel-border);
            font-size: 13px;
            font-weight: 600;
            padding: 10px;
            border-radius: 6px;
        }

        .btn-func.active {
            background: var(--accent-success);
            border-color: var(--accent-success);
            color: white;
        }

        .accessory-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
        }

        .acc-panel {
            background: #12131a;
            border: 1px solid var(--panel-border);
            border-radius: 8px;
            padding: 15px;
            display: flex;
            flex-direction: column;
            gap: 10px;
        }

        .acc-title {
            font-size: 13px;
            font-weight: 600;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .acc-btns {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 8px;
        }

        .btn-acc {
            font-size: 12px;
            padding: 8px;
            border-radius: 6px;
        }

        .btn-acc.active-str {
            background: var(--accent-success);
            border-color: var(--accent-success);
            color: white;
        }

        .btn-acc.active-div {
            background: var(--accent-warning);
            border-color: var(--accent-warning);
            color: white;
        }

        .console-container {
            flex-grow: 1;
            display: flex;
            flex-direction: column;
            gap: 10px;
        }

        .console-box {
            background: #0a0b10;
            border: 1px solid var(--panel-border);
            border-radius: 8px;
            padding: 15px;
            font-family: ui-monospace, SFMono-Regular, Consolas, Monaco, monospace;
            font-size: 12px;
            height: 130px;
            overflow-y: auto;
            color: #38bdf8;
            display: flex;
            flex-direction: column;
            gap: 4px;
        }

        .console-line {
            display: flex;
            gap: 10px;
        }

        .console-time {
            color: var(--text-secondary);
        }

        .console-hex {
            color: #10b981;
            font-weight: 700;
        }

        .modal-overlay {
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(5, 6, 11, 0.95);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 1000;
            opacity: 0;
            pointer-events: none;
            padding: 20px;
        }

        .modal-overlay.active {
            opacity: 1;
            pointer-events: auto;
        }

        .modal-content {
            background: var(--panel-bg);
            border: 1px solid var(--panel-border);
            border-radius: 12px;
            width: 100%;
            max-width: 480px;
            padding: 25px;
            display: flex;
            flex-direction: column;
            gap: 15px;
        }

        .modal-header {
            font-size: 20px;
            font-weight: 700;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--panel-border);
            padding-bottom: 12px;
        }

        .modal-close {
            background: none;
            border: none;
            color: var(--text-secondary);
            font-size: 24px;
            cursor: pointer;
        }

        .network-list {
            max-height: 150px;
            overflow-y: auto;
            background: #12131a;
            border: 1px solid var(--panel-border);
            border-radius: 8px;
            display: flex;
            flex-direction: column;
        }

        .network-item {
            padding: 12px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            cursor: pointer;
            border-bottom: 1px solid var(--panel-border);
            font-size: 14px;
            font-weight: 600;
        }

        .network-item:hover {
            background: #1e202f;
        }

        .network-item.selected {
            background: rgba(88, 80, 236, 0.2);
            color: #a5b4fc;
            border-left: 3px solid var(--accent-glow);
        }

        .rssi-indicator {
            display: flex;
            align-items: center;
            gap: 6px;
            font-size: 11px;
            color: var(--text-secondary);
        }

        .spinner {
            width: 24px;
            height: 24px;
            border: 3px solid rgba(255,255,255,0.1);
            border-radius: 50%;
            border-top-color: var(--accent-glow);
            animation: spin 1s ease-in-out infinite;
            margin: 15px auto;
        }

        .reboot-screen {
            text-align: center;
            padding: 30px 10px;
            display: flex;
            flex-direction: column;
            gap: 15px;
            align-items: center;
        }

        .reboot-countdown {
            font-size: 48px;
            font-weight: 700;
            color: var(--accent-warning);
        }

        @keyframes spin {
            to { transform: rotate(360deg); }
        }

        /* Switch/Toggle Slider */
        .switch {
            position: relative;
            display: inline-block;
            width: 44px;
            height: 24px;
        }

        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }

        .switch-slider {
            position: absolute;
            cursor: pointer;
            top: 0; left: 0; right: 0; bottom: 0;
            background-color: #242736;
            border-radius: 34px;
            transition: .2s;
        }

        .switch-slider:before {
            position: absolute;
            content: "";
            height: 18px;
            width: 18px;
            left: 3px;
            bottom: 3px;
            background-color: white;
            border-radius: 50%;
            transition: .2s;
        }

        input:checked + .switch-slider {
            background-color: var(--accent-glow);
        }

        input:checked + .switch-slider:before {
            transform: translateX(20px);
        }
    </style>
</head>
<body>

    <header>
        <div class="logo-area">
            <div class="logo-icon">🎛️</div>
            <div class="logo-text">
                <h1>DCC Command Center</h1>
                <p>ESP32 RMT DCC Track Controller</p>
            </div>
        </div>
        <div style="display: flex; gap: 10px; align-items: center;">
            <button class="btn" style="padding: 8px 15px; font-size: 13px;" onclick="openWifiModal()">🌐 Wi-Fi Setup</button>
            <div class="status-badge">
                <div class="status-dot"></div>
                <span id="connection-txt">AP Mode</span>
            </div>
        </div>
    </header>

    <div class="main-container">
        
        <!-- Left Panel: Locomotive Control -->
        <div class="panel">
            <div class="panel-title">
                Locomotive Control
                <span id="loco-status-addr">Addr: 3</span>
            </div>

            <!-- Address and Presets Row -->
            <div class="control-row">
                <div class="input-group">
                    <label for="loco-addr">Loco Address</label>
                    <input type="number" id="loco-addr" min="1" max="9999" value="3" onchange="updateLocoAddr()">
                </div>
            </div>

            <!-- Visual Throttle slider -->
            <div class="throttle-container">
                <div class="speed-gauge" id="speed-gauge">
                    <div class="speed-value" id="speed-val">0</div>
                    <div class="speed-unit">Speed</div>
                </div>
                
                <div class="slider-wrapper">
                    <span class="slider-icon">🛑</span>
                    <input type="range" id="speed-slider" min="0" max="126" value="0" oninput="onSpeedSlider(this.value)">
                    <span class="slider-icon">⚡</span>
                </div>
            </div>

            <!-- Tactile Direction and Stop Buttons -->
            <div class="dir-button-grid">
                <button class="btn active-fwd" id="btn-fwd" onclick="setDirection(true)">▶️ Forward</button>
                <button class="btn" id="btn-rev" onclick="setDirection(false)">◀️ Reverse</button>
            </div>
            
            <button class="btn btn-stop" onclick="emergencyStop()">⚠️ EMERGENCY STOP (F0)</button>

            <!-- DCC Functions Grid -->
            <div class="input-group">
                <label>Loco DCC Functions</label>
                <div class="func-grid">
                    <button class="btn btn-func" id="btn-f0" onclick="toggleFunction(0)">💡 F0 Light</button>
                    <button class="btn btn-func" id="btn-f1" onclick="toggleFunction(1)">🔔 F1 Bell</button>
                    <button class="btn btn-func" id="btn-f2" onclick="toggleFunction(2)">🎺 F2 Horn</button>
                    <button class="btn btn-func" id="btn-f3" onclick="toggleFunction(3)">⚙️ F3 Aux1</button>
                    <button class="btn btn-func" id="btn-f4" onclick="toggleFunction(4)">⚙️ F4 Aux2</button>
                    <button class="btn btn-func" id="btn-f5" onclick="toggleFunction(5)">⚙️ F5 Aux3</button>
                    <button class="btn btn-func" id="btn-f6" onclick="toggleFunction(6)">⚙️ F6 Aux4</button>
                    <button class="btn btn-func" id="btn-f7" onclick="toggleFunction(7)">⚙️ F7 Aux5</button>
                    <button class="btn btn-func" id="btn-f8" onclick="toggleFunction(8)">⚙️ F8 Aux6</button>
                </div>
            </div>
        </div>

        <!-- Right Panel: Accessories and Telemetry -->
        <div class="panel" style="display: flex; flex-direction: column; justify-content: space-between;">
            <div>
                <div class="panel-title" style="margin-bottom: 15px;">
                    Track Accessories (DCC Switches)
                </div>
                
                <div class="accessory-grid">
                    
                    <!-- Turnout 1 -->
                    <div class="acc-panel">
                        <div class="acc-title">Turnout 1 (Addr 1)</div>
                        <div class="acc-btns">
                            <button class="btn btn-acc active-str" id="btn-acc1-str" onclick="setAccessory(1, true)">Straight</button>
                            <button class="btn btn-acc" id="btn-acc1-div" onclick="setAccessory(1, false)">Curved</button>
                        </div>
                    </div>

                    <!-- Turnout 2 -->
                    <div class="acc-panel">
                        <div class="acc-title">Turnout 2 (Addr 2)</div>
                        <div class="acc-btns">
                            <button class="btn btn-acc active-str" id="btn-acc2-str" onclick="setAccessory(2, true)">Straight</button>
                            <button class="btn btn-acc" id="btn-acc2-div" onclick="setAccessory(2, false)">Curved</button>
                        </div>
                    </div>

                    <!-- Turnout 3 -->
                    <div class="acc-panel">
                        <div class="acc-title">Turnout 3 (Addr 3)</div>
                        <div class="acc-btns">
                            <button class="btn btn-acc active-str" id="btn-acc3-str" onclick="setAccessory(3, true)">Straight</button>
                            <button class="btn btn-acc" id="btn-acc3-div" onclick="setAccessory(3, false)">Curved</button>
                        </div>
                    </div>

                    <!-- Turnout 4 -->
                    <div class="acc-panel">
                        <div class="acc-title">Turnout 4 (Addr 4)</div>
                        <div class="acc-btns">
                            <button class="btn btn-acc active-str" id="btn-acc4-str" onclick="setAccessory(4, true)">Straight</button>
                            <button class="btn btn-acc" id="btn-acc4-div" onclick="setAccessory(4, false)">Curved</button>
                        </div>
                    </div>

                </div>
            </div>

            <!-- Diagnostics & Test Scenarios -->
            <div style="margin-top: 15px; margin-bottom: 15px;">
                <div class="panel-title" style="margin-bottom: 10px; border-bottom: 1px solid var(--panel-border); padding-bottom: 5px; font-size: 14px;">
                    Diagnostics & Test Scenarios
                    <span>Select an autonomous routine to probe the track on your scope</span>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px;">
                    <button class="btn btn-func" style="font-size: 11px; padding: 6px 4px; background: #12131a; border-color: var(--panel-border);" onclick="triggerTest(1)">🔍 Idle Packets (5s)</button>
                    <button class="btn btn-func" style="font-size: 11px; padding: 6px 4px; background: #12131a; border-color: var(--panel-border);" onclick="triggerTest(2)">⚡ Speed Sweep (5s)</button>
                    <button class="btn btn-func" style="font-size: 11px; padding: 6px 4px; background: #12131a; border-color: var(--panel-border);" onclick="triggerTest(3)">🎛️ Turnout Toggles (3s)</button>
                    <button class="btn btn-func" style="font-size: 11px; padding: 6px 4px; background: #12131a; border-color: var(--panel-border);" onclick="triggerTest(4)">📡 BiDi Cutout (5s)</button>
                </div>
            </div>

            <!-- Scope Trigger Configuration -->
            <div style="margin-top: 15px; margin-bottom: 15px;">
                <div class="panel-title" style="margin-bottom: 10px; border-bottom: 1px solid var(--panel-border); padding-bottom: 5px; font-size: 14px;">
                    GPIO4 Scope Trigger
                    <span>Configure digital pulse trigger for oscilloscope capturing</span>
                </div>
                <div style="display: flex; align-items: center; justify-content: space-between; background: #12131a; border: 1px solid var(--panel-border); border-radius: 8px; padding: 10px; margin-bottom: 8px;">
                    <span style="font-size: 12px; color: var(--text-secondary);">Enable GPIO4 Trigger:</span>
                    <label class="switch">
                        <input type="checkbox" id="scope-trigger-chk" checked onchange="toggleScopeTrigger(this.checked)">
                        <span class="switch-slider"></span>
                    </label>
                </div>
                <button class="btn btn-func" style="width: 100%; font-size: 11px; padding: 8px; background: var(--accent-success); border-color: var(--accent-success); color: white;" onclick="triggerTest(5)">🎯 Pulse Scope (Send DCC Command)</button>
            </div>

            <!-- Outgoing telemetry logs -->
            <div class="console-container">
                <div class="panel-title" style="border: none; padding-bottom: 0;">
                    Live Telemetry Console
                    <span>Outgoing Packet Hexadecimal</span>
                </div>
                <div class="console-box" id="console-box">
                    <div class="console-line">
                        <span class="console-time">[00:00:00]</span>
                        <span>Terminal initialized. Awaiting commands...</span>
                    </div>
                </div>
            </div>
        </div>

        <!-- Third Panel: DCC Decoder Monitor -->
        <div class="panel" style="display: flex; flex-direction: column; justify-content: space-between;">
            <div>
                <div class="panel-title" style="margin-bottom: 15px; border-bottom: 1px solid var(--panel-border); padding-bottom: 5px; font-size: 14px;">
                    DCC Decoder Monitor
                    <span id="decoder-badge-container">
                        <span class="status-badge" style="background: #12131a; border-color: var(--panel-border); color: #ef4444; font-size: 10px; padding: 2px 6px;">
                            <span class="status-dot" style="background-color: #ef4444; width: 6px; height: 6px;"></span>
                            <span id="decoder-status-txt" style="margin-left: 4px;">NO SIGNAL</span>
                        </span>
                    </span>
                </div>

                <!-- Hardware Decoder Enable Switch -->
                <div style="display: flex; align-items: center; justify-content: space-between; background: #12131a; border: 1px solid var(--panel-border); border-radius: 8px; padding: 10px; margin-bottom: 15px;">
                    <span style="font-size: 12px; color: var(--text-secondary);">Enable Hardware RMT RX:</span>
                    <label class="switch">
                        <input type="checkbox" id="decoder-hardware-chk" onchange="toggleHardwareDecoder(this.checked)">
                        <span class="switch-slider"></span>
                    </label>
                </div>

                <!-- Decoder Pin and Stats Grid -->
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 10px;">
                    <div style="background: #12131a; border: 1px solid var(--panel-border); border-radius: 8px; padding: 10px; text-align: center;">
                        <div style="font-size: 10px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.5px;">Decoder Pin</div>
                        <div id="decoder-pin-val" style="font-size: 16px; font-weight: 700; color: var(--accent-glow); margin-top: 4px;">--</div>
                    </div>
                    <div style="background: #12131a; border: 1px solid var(--panel-border); border-radius: 8px; padding: 10px; text-align: center;">
                        <div style="font-size: 10px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.5px;">CRC Errors</div>
                        <div id="decoder-errors-val" style="font-size: 16px; font-weight: 700; color: var(--accent-danger); margin-top: 4px;">0</div>
                    </div>
                </div>

                <!-- Processor Load and Idle Packets Grid -->
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 15px;">
                    <div style="background: #12131a; border: 1px solid var(--panel-border); border-radius: 8px; padding: 10px; text-align: center;">
                        <div style="font-size: 10px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.5px;">CPU0 / CPU1 Load</div>
                        <div id="cpu-load-val" style="font-size: 16px; font-weight: 700; color: #3b82f6; margin-top: 4px;">0% / 0%</div>
                    </div>
                    <div style="background: #12131a; border: 1px solid var(--panel-border); border-radius: 8px; padding: 10px; text-align: center;">
                        <div style="font-size: 10px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.5px;">Idle Packets</div>
                        <div id="decoder-idle-val" style="font-size: 16px; font-weight: 700; color: #a855f7; margin-top: 4px;">0</div>
                    </div>
                </div>

                <div style="background: #12131a; border: 1px solid var(--panel-border); border-radius: 8px; padding: 10px; text-align: center; margin-bottom: 15px;">
                    <div style="font-size: 10px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.5px;">Total Decoded Packets</div>
                    <div id="decoder-success-val" style="font-size: 20px; font-weight: 700; color: var(--accent-success); margin-top: 4px;">0</div>
                </div>

                <!-- Decoded Locomotive HUD -->
                <div style="background: #12131a; border: 1px solid var(--panel-border); border-radius: 8px; padding: 15px; margin-bottom: 15px;">
                    <div style="font-size: 11px; color: var(--accent-glow); font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 8px; border-bottom: 1px solid var(--panel-border); padding-bottom: 4px;">
                        Decoded Locomotive HUD
                    </div>
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                        <span style="font-size: 13px; color: var(--text-secondary);">Loco Address:</span>
                        <span id="hud-addr" style="font-size: 15px; font-weight: 700; color: white;">None</span>
                    </div>
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                        <span style="font-size: 13px; color: var(--text-secondary);">Decoded Speed:</span>
                        <span id="hud-speed" style="font-size: 15px; font-weight: 700; color: var(--accent-success);">--</span>
                    </div>
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                        <span style="font-size: 13px; color: var(--text-secondary);">Direction:</span>
                        <span id="hud-dir" style="font-size: 15px; font-weight: 700; color: white;">--</span>
                    </div>
                    <div style="font-size: 13px; color: var(--text-secondary); margin-bottom: 4px;">Function Group Flags:</div>
                    <div id="hud-funcs" style="font-size: 11px; font-family: monospace; color: var(--text-secondary); background: #0a0b10; padding: 6px; border-radius: 6px; text-align: center; letter-spacing: 1px;">
                        F0..F8: --
                    </div>
                </div>
            </div>

            <!-- Incoming Decoded Console -->
            <div class="console-container">
                <div class="panel-title" style="border: none; padding-bottom: 0;">
                    Incoming Decoded Console
                    <span>Parsed Loopback Packets</span>
                </div>
                <div class="console-box" id="decoder-console-box" style="height: 140px;">
                    <div class="console-line">
                        <span class="console-time">[00:00:00]</span>
                        <span>Awaiting loopback signal...</span>
                    </div>
                </div>
            </div>
        </div>

    </div>

    <!-- Wi-Fi Setup Modal -->
    <div class="modal-overlay" id="wifi-modal">
        <div class="modal-content" id="modal-body-content">
            <div class="modal-header">
                Configure Wi-Fi Client Mode
                <button class="modal-close" onclick="closeWifiModal()">&times;</button>
            </div>
            
            <p style="font-size: 13px; color: var(--text-secondary); line-height: 1.4;">
                Select your home network SSID from the scanned list, enter your password, and save. The controller will store credentials and reboot to connect to your network.
            </p>

            <div class="input-group">
                <label>Select SSID network</label>
                <button class="btn" style="padding: 10px; font-size: 13px;" id="btn-scan" onclick="scanWifiNetworks()">🔄 Scan Surrounding Networks</button>
                
                <div id="scan-loading" style="display: none;">
                    <div class="spinner"></div>
                    <p style="font-size: 12px; color: var(--text-secondary); text-align: center;">Scanning bands...</p>
                </div>

                <div class="network-list" id="network-list" style="margin-top: 5px;">
                    <div class="network-item selected" onclick="selectNetwork('Manual')">Enter SSID Manually...</div>
                </div>
            </div>

            <div class="input-group" id="manual-ssid-group">
                <label for="wifi-ssid">Manual Network SSID</label>
                <input type="text" id="wifi-ssid" placeholder="MyHomeNetwork">
            </div>

            <div class="input-group">
                <label for="wifi-pass">Network Password</label>
                <input type="password" id="wifi-pass" placeholder="••••••••">
            </div>

            <button class="btn btn-stop" style="background: var(--accent-success); border: none;" onclick="applyWifiConfig()">💾 Save Credentials & Reboot</button>
        </div>
    </div>

    <script>
        // Global variables for Loco State
        let locoAddr = 3;
        let locoSpeed = 0;
        let locoDirection = true; // true = Forward, false = Reverse
        let locoFunctions = Array(9).fill(false); // F0 to F8
        let selectedSsid = 'Manual';

        // Throttle helper variables
        let lastSentTime = 0;
        const MIN_INTERVAL_MS = 250; // Throttle sending packets to prevent congesting REST queue

        // Log a packet in the telemetry terminal
        function logTelemetry(type, details, hexBytes) {
            const consoleBox = document.getElementById('console-box');
            const now = new Date();
            const timeStr = now.toTimeString().split(' ')[0];

            const hexStr = hexBytes.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join(' ');

            const line = document.createElement('div');
            line.className = 'console-line';
            line.innerHTML = `
                <span class="console-time">[${timeStr}]</span>
                <span><strong>[${type}]</strong> ${details}</span>
                <span class="console-hex">${hexStr}</span>
            `;

            consoleBox.appendChild(line);
            consoleBox.scrollTop = consoleBox.scrollHeight;
        }

        // Update loco address selector
        function updateLocoAddr() {
            const input = document.getElementById('loco-addr');
            let val = parseInt(input.value);
            if (isNaN(val) || val < 1) val = 3;
            if (val > 9999) val = 9999;
            locoAddr = val;
            input.value = val;
            document.getElementById('loco-status-addr').innerText = `Addr: ${locoAddr}`;
        }

        // Set direction
        function setDirection(isFwd) {
            locoDirection = isFwd;
            document.getElementById('btn-fwd').className = isFwd ? 'btn active-fwd' : 'btn';
            document.getElementById('btn-rev').className = !isFwd ? 'btn active-rev' : 'btn';
            sendLocoCommand();
        }

        // Handle speed slider changes
        function onSpeedSlider(val) {
            locoSpeed = parseInt(val);
            
            // Update gauge UI
            document.getElementById('speed-val').innerText = locoSpeed;
            const percent = (locoSpeed / 126) * 100;
            document.getElementById('speed-gauge').style.setProperty('--speed-percent', percent);

            // Throttle network requests
            const now = Date.now();
            if (now - lastSentTime > MIN_INTERVAL_MS || locoSpeed === 0) {
                sendLocoCommand();
            }
        }

        // Emergency Stop
        function emergencyStop() {
            document.getElementById('speed-slider').value = 0;
            onSpeedSlider(0);
        }

        // Toggle DCC Loco Function
        function toggleFunction(fNum) {
            locoFunctions[fNum] = !locoFunctions[fNum];
            document.getElementById(`btn-f${fNum}`).className = locoFunctions[fNum] ? 'btn btn-func active' : 'btn btn-func';
            sendLocoCommand();
        }

        // Format and send JSON DCC Locomotive Packet
        function sendLocoCommand() {
            lastSentTime = Date.now();
            
            const payload = {
                address: locoAddr,
                speed: locoSpeed,
                direction: locoDirection,
                functions: locoFunctions
            };

            fetch('/api/loco', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            })
            .then(res => {
                if (!res.ok) throw new Error('HTTP status ' + res.status);
                return res.json();
            })
            .then(data => {
                if (data.status === 'ok') {
                    logTelemetry('LOCO', `Address ${locoAddr} | Speed: ${locoSpeed} | Dir: ${locoDirection ? 'FWD' : 'REV'}`, data.bytes);
                }
            })
            .catch(err => {
                console.error('Error sending loco command:', err);
            });
        }

        // Send JSON Accessory Switch Command
        function setAccessory(addr, isStraight) {
            // Update UI buttons
            document.getElementById(`btn-acc${addr}-str`).className = isStraight ? 'btn btn-acc active-str' : 'btn';
            document.getElementById(`btn-acc${addr}-div`).className = !isStraight ? 'btn btn-acc active-div' : 'btn';

            const payload = {
                address: addr,
                straight: isStraight
            };

            fetch('/api/accessory', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            })
            .then(res => {
                if (!res.ok) throw new Error('HTTP status ' + res.status);
                return res.json();
            })
            .then(data => {
                if (data.status === 'ok') {
                    logTelemetry('ACC', `Turnout ${addr} toggled to ${isStraight ? 'STRAIGHT' : 'CURVED'}`, data.bytes);
                }
            })
            .catch(err => {
                console.error('Error sending accessory command:', err);
            });
        }

        // Trigger autonomous diagnostics test scenarios
        function triggerTest(scenarioId) {
            logTelemetry('TEST', `Launching Test Scenario ${scenarioId} in background...`, []);
            
            fetch('/api/test', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ scenario: scenarioId })
            })
            .then(res => {
                if (!res.ok) {
                    return res.text().then(text => { throw new Error(text || 'Server error'); });
                }
                return res.json();
            })
            .then(data => {
                logTelemetry('TEST', `Success: ${data.message}`, []);
            })
            .catch(err => {
                logTelemetry('TEST', `Error: ${err.message}`, []);
            });
        }

        // Toggle GPIO4 Scope Trigger
        function toggleScopeTrigger(checked) {
            fetch('/api/trigger', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ enabled: checked })
            })
            .then(res => {
                if (!res.ok) throw new Error('HTTP status ' + res.status);
                return res.json();
            })
            .then(data => {
                logTelemetry('TEST', `Scope Trigger on GPIO4 set to ${data.enabled ? 'ENABLED' : 'DISABLED'}`, []);
            })
            .catch(err => {
                console.error('Error toggling scope trigger:', err);
                logTelemetry('TEST', `Error toggling scope trigger: ${err.message}`, []);
            });
        }

        // Wi-Fi Setup Modal controllers
        function openWifiModal() {
            document.getElementById('wifi-modal').className = 'modal-overlay active';
        }

        function closeWifiModal() {
            document.getElementById('wifi-modal').className = 'modal-overlay';
        }

        // Scan nearby Wi-Fi networks in range
        function scanWifiNetworks() {
            document.getElementById('btn-scan').style.display = 'none';
            document.getElementById('scan-loading').style.display = 'block';
            
            fetch('/api/wifi/scan')
            .then(res => {
                if (!res.ok) throw new Error('Scan HTTP failure: ' + res.status);
                return res.json();
            })
            .then(networks => {
                document.getElementById('scan-loading').style.display = 'none';
                document.getElementById('btn-scan').style.display = 'block';
                
                const list = document.getElementById('network-list');
                list.innerHTML = `<div class="network-item selected" onclick="selectNetwork('Manual')">Enter SSID Manually...</div>`;
                
                networks.forEach(net => {
                    const item = document.createElement('div');
                    item.className = 'network-item';
                    item.onclick = () => selectNetwork(net.ssid);
                    item.id = `net-${net.ssid}`;
                    item.innerHTML = `
                        <span>${net.ssid}</span>
                        <span class="rssi-indicator">${net.rssi} dBm ${net.secure ? '🔒' : '🔓'}</span>
                    `;
                    list.appendChild(item);
                });
            })
            .catch(err => {
                console.error('Scan error:', err);
                document.getElementById('scan-loading').style.display = 'none';
                document.getElementById('btn-scan').style.display = 'block';
                alert('Wi-Fi scan failed. Try again.');
            });
        }

        // Select Wi-Fi SSID
        function selectNetwork(ssid) {
            selectedSsid = ssid;
            
            // Clean selection styling
            const items = document.querySelectorAll('.network-item');
            items.forEach(i => i.classList.remove('selected'));
            
            if (ssid === 'Manual') {
                document.getElementById('manual-ssid-group').style.display = 'flex';
                document.getElementById('wifi-ssid').value = '';
                // Add manual select styling
                items[0].classList.add('selected');
            } else {
                document.getElementById('manual-ssid-group').style.display = 'none';
                document.getElementById('wifi-ssid').value = ssid;
                
                const selectedEl = document.getElementById(`net-${ssid}`);
                if (selectedEl) {
                    selectedEl.classList.add('selected');
                }
            }
        }

        // Post Wi-Fi credentials to ESP32
        function applyWifiConfig() {
            let ssid = document.getElementById('wifi-ssid').value.trim();
            let pass = document.getElementById('wifi-pass').value.trim();

            if (!ssid) {
                alert('SSID cannot be empty!');
                return;
            }

            const payload = {
                ssid: ssid,
                password: pass
            };

            fetch('/api/wifi/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            })
            .then(res => {
                if (!res.ok) throw new Error('Config failed with HTTP status: ' + res.status);
                return res.json();
            })
            .then(data => {
                if (data.status === 'ok') {
                    showRebootCountdown(ssid);
                }
            })
            .catch(err => {
                console.error('Config posting error:', err);
                alert('Failed to save Wi-Fi configuration: ' + err.message);
            });
        }

        // Show a countdown screen during the ESP32 reboot cycle
        function showRebootCountdown(ssid) {
            const body = document.getElementById('modal-body-content');
            body.innerHTML = `
                <div class="reboot-screen">
                    <h2 style="font-weight: 700;">Saving & Rebooting</h2>
                    <p style="font-size: 14px; color: var(--text-secondary); line-height: 1.4;">
                        The DCC Controller has stored credentials for network <strong>${ssid}</strong> and is rebooting to establish a Station connection.
                    </p>
                    <div class="spinner"></div>
                    <div class="reboot-countdown" id="reboot-timer">15</div>
                    <p style="font-size: 12px; color: var(--text-secondary);">
                        Reconnecting... Connect your device to the same Wi-Fi network and open the controller. If the connection fails, the controller will automatically return to AP mode.
                    </p>
                </div>
            `;

            let timeLeft = 15;
            const timer = setInterval(() => {
                timeLeft--;
                document.getElementById('reboot-timer').innerText = timeLeft;
                if (timeLeft <= 0) {
                    clearInterval(timer);
                    // Refresh browser
                    window.location.reload();
                }
            }, 1000);
        }

        let lastTimestamp = 0;
        let activeLocoState = { addr: "None", speed: "--", dir: "--", funcs: {} };
        let isTogglingDecoder = false;

        function pollDecoder() {
            fetch('/api/decoder')
            .then(res => res.json())
            .then(data => {
                if (!data.active) return;
                
                // 1. Update Status Badge
                const badge = document.getElementById('decoder-badge-container');
                const pinVal = document.getElementById('decoder-pin-val');
                const successVal = document.getElementById('decoder-success-val');
                const errorsVal = document.getElementById('decoder-errors-val');

                pinVal.innerText = data.pin === -1 ? "Software Loopback" : `GPIO ${data.pin}`;
                successVal.innerText = data.success_count;
                errorsVal.innerText = data.error_count;

                // Update processor load and idle packet counter
                const cpuVal = document.getElementById('cpu-load-val');
                const idleVal = document.getElementById('decoder-idle-val');
                if (cpuVal) {
                    cpuVal.innerText = `${data.cpu_load_core0}% / ${data.cpu_load_core1}%`;
                }
                if (idleVal) {
                    idleVal.innerText = data.idle_packet_count;
                }

                const chk = document.getElementById('decoder-hardware-chk');
                if (chk && !isTogglingDecoder) {
                    chk.checked = (data.pin !== -1);
                }

                if (data.status === "Active") {
                    badge.innerHTML = `
                        <span class="status-badge" style="background: #12131a; border-color: var(--panel-border); color: #10b981; font-size: 10px; padding: 2px 6px;">
                            <span class="status-dot" style="background-color: #10b981; width: 6px; height: 6px;"></span>
                            <span id="decoder-status-txt" style="margin-left: 4px;">ACTIVE</span>
                        </span>
                    `;
                } else {
                    badge.innerHTML = `
                        <span class="status-badge" style="background: #12131a; border-color: var(--panel-border); color: #ef4444; font-size: 10px; padding: 2px 6px;">
                            <span class="status-dot" style="background-color: #ef4444; width: 6px; height: 6px;"></span>
                            <span id="decoder-status-txt" style="margin-left: 4px;">NO SIGNAL</span>
                        </span>
                    `;
                }

                // 2. Parse recent packets and print in console
                if (data.packets && data.packets.length > 0) {
                    const consoleBox = document.getElementById('decoder-console-box');
                    
                    let newPackets = [];
                    for (let i = 0; i < data.packets.length; ++i) {
                        const p = data.packets[i];
                        if (p.timestamp > lastTimestamp) {
                            newPackets.push(p);
                        }
                    }
                    
                    if (newPackets.length > 0) {
                        newPackets.sort((a, b) => a.timestamp - b.timestamp);
                        
                        if (lastTimestamp === 0) {
                            consoleBox.innerHTML = '';
                        }
                        
                        newPackets.forEach(p => {
                            lastTimestamp = p.timestamp;
                            
                            const secondsTotal = Math.floor(p.timestamp / 1000000);
                            const minutes = Math.floor(secondsTotal / 60) % 60;
                            const seconds = secondsTotal % 60;
                            const timeStr = `[${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}]`;

                            const line = document.createElement('div');
                            line.className = 'console-line';
                            line.innerHTML = `
                                <span class="console-time">${timeStr}</span>
                                <span class="console-hex" style="color: ${p.valid ? '#10b981' : '#ef4444'}; font-size: 10px;">${p.hex}</span>
                                <span style="flex-grow: 1;">${p.text}</span>
                            `;
                            consoleBox.appendChild(line);
                            
                            if (p.valid) {
                                parsePacketForHUD(p.text);
                            }
                        });
                        
                        while (consoleBox.children.length > 50) {
                            consoleBox.removeChild(consoleBox.firstChild);
                        }
                        
                        consoleBox.scrollTop = consoleBox.scrollHeight;
                    }
                }
            })
            .catch(err => {
                console.warn('Decoder poll error:', err.message);
            });
        }

        function toggleHardwareDecoder(enabled) {
            isTogglingDecoder = true;
            fetch('/api/decoder/toggle', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ enabled: enabled })
            })
            .then(res => res.json())
            .then(data => {
                const pinVal = document.getElementById('decoder-pin-val');
                pinVal.innerText = data.pin === -1 ? "Software Loopback" : `GPIO ${data.pin}`;
                document.getElementById('decoder-success-val').innerText = '0';
                document.getElementById('decoder-errors-val').innerText = '0';
                document.getElementById('decoder-console-box').innerHTML = `
                    <div class="console-line">
                        <span class="console-time">[00:00:00]</span>
                        <span style="color: var(--accent-glow)">Decoder mode set to ${data.pin === -1 ? 'Software Loopback' : 'Hardware GPIO RX'}...</span>
                    </div>
                `;
                lastTimestamp = 0;

                const chk = document.getElementById('decoder-hardware-chk');
                if (chk) {
                    chk.checked = (data.pin !== -1);
                }
                isTogglingDecoder = false;
            })
            .catch(err => {
                console.error('Error toggling hardware decoder:', err);
                const chk = document.getElementById('decoder-hardware-chk');
                if (chk) {
                    chk.checked = !enabled;
                }
                isTogglingDecoder = false;
            });
        }

        function parsePacketForHUD(text) {
            if (text.includes("Loco ")) {
                const parts = text.split(" | ");
                const locoAddr = parts[0].replace("Loco ", "");
                document.getElementById('hud-addr').innerText = locoAddr;
                activeLocoState.addr = locoAddr;

                if (parts[1] && parts[1].includes("Speed")) {
                    let speedVal = "--";
                    let dirVal = "--";
                    
                    if (parts[1].includes("FWD")) dirVal = "▶️ Forward";
                    if (parts[1].includes("REV")) dirVal = "◀️ Reverse";
                    
                    const speedMatch = parts[1].match(/Speed \d+-step:\s*([A-Za-z0-9\/\s-]+)/);
                    if (speedMatch) {
                        speedVal = speedMatch[1].replace(" FWD", "").replace(" REV", "").trim();
                    }
                    
                    document.getElementById('hud-speed').innerText = speedVal;
                    document.getElementById('hud-dir').innerText = dirVal;
                    
                    activeLocoState.speed = speedVal;
                    activeLocoState.dir = dirVal;
                } else if (parts[1] && parts[1].includes("Func")) {
                    const funcString = parts[1].replace("Func ", "");
                    document.getElementById('hud-funcs').innerText = funcString;
                }
            } else if (text.includes("Turnout Addr: ")) {
                const turnoutText = text.replace("Turnout Addr: ", "");
                document.getElementById('hud-addr').innerText = "Acc Switch";
                document.getElementById('hud-speed').innerText = turnoutText.split(" | ")[1] || "--";
                document.getElementById('hud-dir').innerText = turnoutText.split(" | ")[0] || "--";
            }
        }

        // Query current connection mode on boot
        window.addEventListener('DOMContentLoaded', () => {
            fetch('/api/loco') // Just a get request to query current state if needed
            .then(res => res.json())
            .then(data => {
                if (data.mode) {
                    document.getElementById('connection-txt').innerText = data.mode;
                    if (data.ssid) {
                        document.getElementById('connection-txt').innerText += ` (${data.ssid})`;
                    }
                }
            })
            .catch(err => {
                console.warn('Initial state fetch error (normal for local testing):', err.message);
            });

            // Start decoder polling loop
            pollDecoder();
            setInterval(pollDecoder, 500);
        });
    </script>

</body>
</html>
)rawhtml";
