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
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&family=JetBrains+Mono:wght@400;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-primary: #0b0c15;
            --bg-secondary: #131525;
            --glass-bg: rgba(25, 27, 44, 0.55);
            --glass-border: rgba(255, 255, 255, 0.08);
            --accent-glow: #6366f1;
            --accent-success: #10b981;
            --accent-warning: #f59e0b;
            --accent-danger: #ef4444;
            --text-primary: #f8fafc;
            --text-secondary: #94a3b8;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Outfit', sans-serif;
            -webkit-tap-highlight-color: transparent;
        }

        body {
            background-color: var(--bg-primary);
            background-image: 
                radial-gradient(at 10% 20%, rgba(99, 102, 241, 0.15) 0px, transparent 50%),
                radial-gradient(at 90% 80%, rgba(16, 185, 129, 0.1) 0px, transparent 50%);
            background-attachment: fixed;
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
            background: var(--glass-bg);
            backdrop-filter: blur(16px);
            border: 1px solid var(--glass-border);
            border-radius: 18px;
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.3);
        }

        .logo-area {
            display: flex;
            align-items: center;
            gap: 12px;
        }

        .logo-icon {
            width: 32px;
            height: 32px;
            background: linear-gradient(135deg, var(--accent-glow), var(--accent-success));
            border-radius: 8px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: 700;
            font-size: 18px;
            box-shadow: 0 0 15px rgba(99, 102, 241, 0.4);
        }

        .logo-text h1 {
            font-size: 20px;
            font-weight: 700;
            letter-spacing: 0.5px;
            background: linear-gradient(135deg, #fff, #a5b4fc);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
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
            background: rgba(255, 255, 255, 0.05);
            border-radius: 20px;
            font-size: 13px;
            font-weight: 600;
            border: 1px solid rgba(255, 255, 255, 0.05);
        }

        .status-dot {
            width: 8px;
            height: 8px;
            background-color: var(--accent-success);
            border-radius: 50%;
            box-shadow: 0 0 8px var(--accent-success);
            animation: pulse 2s infinite;
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
            background: var(--glass-bg);
            backdrop-filter: blur(16px);
            border: 1px solid var(--glass-border);
            border-radius: 24px;
            padding: 25px;
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.3);
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
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
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
            background: rgba(0, 0, 0, 0.25);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 12px;
            padding: 12px;
            color: var(--text-primary);
            font-size: 16px;
            font-weight: 600;
            outline: none;
            transition: all 0.3s;
        }

        .input-group input:focus {
            border-color: var(--accent-glow);
            box-shadow: 0 0 10px rgba(99, 102, 241, 0.25);
        }

        /* Throttle UI styling */
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
            border-radius: 50%;
            background: radial-gradient(circle, rgba(19, 21, 37, 0.8) 0%, rgba(10, 12, 21, 0.9) 100%);
            border: 4px solid rgba(255, 255, 255, 0.05);
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            box-shadow: 0 10px 25px rgba(0, 0, 0, 0.5), inset 0 2px 10px rgba(255, 255, 255, 0.05);
            position: relative;
        }

        .speed-gauge::after {
            content: '';
            position: absolute;
            top: -4px; left: -4px; right: -4px; bottom: -4px;
            border-radius: 50%;
            border: 4px solid transparent;
            border-top-color: var(--accent-glow);
            transform: rotate(calc(var(--speed-percent, 0) * 2.7deg - 135deg));
            transition: transform 0.1s linear;
        }

        .speed-value {
            font-size: 38px;
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
            height: 10px;
            border-radius: 5px;
            background: rgba(0, 0, 0, 0.35);
            outline: none;
            transition: background 0.3s;
        }

        .slider-wrapper input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 24px;
            height: 24px;
            border-radius: 50%;
            background: linear-gradient(135deg, var(--accent-glow), #4f46e5);
            cursor: pointer;
            box-shadow: 0 0 12px rgba(99, 102, 241, 0.6);
            transition: transform 0.1s;
        }

        .slider-wrapper input[type="range"]::-webkit-slider-thumb:active {
            transform: scale(1.2);
        }

        .dir-button-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            width: 100%;
        }

        .btn {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid rgba(255, 255, 255, 0.05);
            border-radius: 14px;
            padding: 14px;
            color: var(--text-primary);
            font-size: 15px;
            font-weight: 700;
            cursor: pointer;
            transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
            outline: none;
        }

        .btn:hover {
            background: rgba(255, 255, 255, 0.1);
            transform: translateY(-2px);
        }

        .btn:active {
            transform: translateY(1px);
        }

        .btn.active-fwd {
            background: linear-gradient(135deg, rgba(99, 102, 241, 0.25), rgba(99, 102, 241, 0.1));
            border-color: var(--accent-glow);
            color: #c7d2fe;
            box-shadow: 0 0 15px rgba(99, 102, 241, 0.25);
        }

        .btn.active-rev {
            background: linear-gradient(135deg, rgba(245, 158, 11, 0.25), rgba(245, 158, 11, 0.1));
            border-color: var(--accent-warning);
            color: #fef3c7;
            box-shadow: 0 0 15px rgba(245, 158, 11, 0.25);
        }

        .btn-stop {
            background: var(--accent-danger);
            border: none;
            color: white;
            font-size: 16px;
            font-weight: 700;
            box-shadow: 0 6px 20px rgba(239, 68, 68, 0.4);
            animation: pulse-danger-idle 2s infinite;
        }

        .btn-stop:hover {
            background: #f87171;
            box-shadow: 0 6px 24px rgba(239, 68, 68, 0.6);
        }

        /* Tactile Function grid */
        .func-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 10px;
        }

        .btn-func {
            background: rgba(0, 0, 0, 0.2);
            border: 1px solid rgba(255, 255, 255, 0.05);
            font-size: 13px;
            font-weight: 600;
            padding: 10px;
            border-radius: 10px;
        }

        .btn-func.active {
            background: linear-gradient(135deg, rgba(16, 185, 129, 0.2), rgba(16, 185, 129, 0.05));
            border-color: var(--accent-success);
            color: #d1fae5;
            box-shadow: 0 0 10px rgba(16, 185, 129, 0.15);
        }

        /* Accessory styles */
        .accessory-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
        }

        .acc-panel {
            background: rgba(0, 0, 0, 0.15);
            border: 1px solid rgba(255, 255, 255, 0.04);
            border-radius: 16px;
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
            border-radius: 8px;
        }

        .btn-acc.active-str {
            background: linear-gradient(135deg, rgba(16, 185, 129, 0.25), rgba(16, 185, 129, 0.1));
            border-color: var(--accent-success);
            color: #a7f3d0;
        }

        .btn-acc.active-div {
            background: linear-gradient(135deg, rgba(245, 158, 11, 0.25), rgba(245, 158, 11, 0.1));
            border-color: var(--accent-warning);
            color: #fde68a;
        }

        /* Realtime console */
        .console-container {
            flex-grow: 1;
            display: flex;
            flex-direction: column;
            gap: 10px;
        }

        .console-box {
            background: rgba(0, 0, 0, 0.4);
            border: 1px solid rgba(255, 255, 255, 0.05);
            border-radius: 16px;
            padding: 15px;
            font-family: 'JetBrains Mono', monospace;
            font-size: 12px;
            height: 130px;
            overflow-y: auto;
            color: #38bdf8;
            display: flex;
            flex-direction: column;
            gap: 4px;
            box-shadow: inset 0 2px 8px rgba(0, 0, 0, 0.8);
        }

        .console-line {
            display: flex;
            gap: 10px;
        }

        .console-time {
            color: var(--text-secondary);
        }

        .console-hex {
            color: var(--accent-success);
            font-weight: 700;
        }

        /* Wi-Fi Provisioning modal styles */
        .modal-overlay {
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(5, 6, 11, 0.8);
            backdrop-filter: blur(8px);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 1000;
            opacity: 0;
            pointer-events: none;
            transition: opacity 0.3s;
            padding: 20px;
        }

        .modal-overlay.active {
            opacity: 1;
            pointer-events: auto;
        }

        .modal-content {
            background: var(--bg-secondary);
            border: 1px solid var(--glass-border);
            border-radius: 24px;
            width: 100%;
            max-width: 480px;
            padding: 25px;
            box-shadow: 0 20px 50px rgba(0, 0, 0, 0.5);
            display: flex;
            flex-direction: column;
            gap: 15px;
            transform: scale(0.9);
            transition: transform 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
        }

        .modal-overlay.active .modal-content {
            transform: scale(1);
        }

        .modal-header {
            font-size: 20px;
            font-weight: 700;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
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
            background: rgba(0, 0, 0, 0.25);
            border: 1px solid rgba(255, 255, 255, 0.05);
            border-radius: 12px;
            display: flex;
            flex-direction: column;
        }

        .network-item {
            padding: 12px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            cursor: pointer;
            border-bottom: 1px solid rgba(255, 255, 255, 0.03);
            font-size: 14px;
            font-weight: 600;
            transition: background 0.2s;
        }

        .network-item:hover {
            background: rgba(255, 255, 255, 0.03);
        }

        .network-item.selected {
            background: rgba(99, 102, 241, 0.15);
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

        /* Animations */
        @keyframes pulse {
            0% { box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.5); }
            70% { box-shadow: 0 0 0 10px rgba(16, 185, 129, 0); }
            100% { box-shadow: 0 0 0 0 rgba(16, 185, 129, 0); }
        }

        @keyframes pulse-danger-idle {
            0% { box-shadow: 0 4px 15px rgba(239, 68, 68, 0.4); }
            50% { box-shadow: 0 4px 22px rgba(239, 68, 68, 0.7); }
            100% { box-shadow: 0 4px 15px rgba(239, 68, 68, 0.4); }
        }

        @keyframes spin {
            to { transform: rotate(360deg); }
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
                <div class="panel-title" style="margin-bottom: 10px; border-bottom: 1px solid rgba(255, 255, 255, 0.1); padding-bottom: 5px; font-size: 14px;">
                    Diagnostics & Test Scenarios
                    <span>Select an autonomous routine to probe the track on your scope</span>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px;">
                    <button class="btn btn-func" style="font-size: 11px; padding: 6px 4px; background: rgba(59, 130, 246, 0.1); border-color: rgba(59, 130, 246, 0.25);" onclick="triggerTest(1)">🔍 Idle Packets (5s)</button>
                    <button class="btn btn-func" style="font-size: 11px; padding: 6px 4px; background: rgba(59, 130, 246, 0.1); border-color: rgba(59, 130, 246, 0.25);" onclick="triggerTest(2)">⚡ Speed Sweep (5s)</button>
                    <button class="btn btn-func" style="font-size: 11px; padding: 6px 4px; background: rgba(59, 130, 246, 0.1); border-color: rgba(59, 130, 246, 0.25);" onclick="triggerTest(3)">🎛️ Turnout Toggles (3s)</button>
                    <button class="btn btn-func" style="font-size: 11px; padding: 6px 4px; background: rgba(59, 130, 246, 0.1); border-color: rgba(59, 130, 246, 0.25);" onclick="triggerTest(4)">📡 BiDi Cutout (5s)</button>
                </div>
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
                <div class="panel-title" style="margin-bottom: 15px; border-bottom: 1px solid rgba(255, 255, 255, 0.1); padding-bottom: 5px; font-size: 14px;">
                    DCC Decoder Monitor
                    <span id="decoder-badge-container">
                        <span class="status-badge" style="background: rgba(239, 68, 68, 0.1); border-color: rgba(239, 68, 68, 0.25); color: #ef4444; font-size: 10px; padding: 2px 6px;">
                            <span class="status-dot" style="background-color: #ef4444; box-shadow: 0 0 8px #ef4444; width: 6px; height: 6px;"></span>
                            <span id="decoder-status-txt" style="margin-left: 4px;">NO SIGNAL</span>
                        </span>
                    </span>
                </div>

                <!-- Decoder Pin and Stats Grid -->
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 15px;">
                    <div style="background: rgba(255, 255, 255, 0.03); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 12px; padding: 10px; text-align: center;">
                        <div style="font-size: 10px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.5px;">Decoder Pin</div>
                        <div id="decoder-pin-val" style="font-size: 16px; font-weight: 700; color: var(--accent-blue); margin-top: 4px;">--</div>
                    </div>
                    <div style="background: rgba(255, 255, 255, 0.03); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 12px; padding: 10px; text-align: center;">
                        <div style="font-size: 10px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.5px;">CRC Errors</div>
                        <div id="decoder-errors-val" style="font-size: 16px; font-weight: 700; color: #ef4444; margin-top: 4px;">0</div>
                    </div>
                </div>

                <div style="background: rgba(255, 255, 255, 0.03); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 12px; padding: 10px; text-align: center; margin-bottom: 15px;">
                    <div style="font-size: 10px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.5px;">Total Decoded Packets</div>
                    <div id="decoder-success-val" style="font-size: 20px; font-weight: 700; color: var(--accent-success); margin-top: 4px;">0</div>
                </div>

                <!-- Decoded Locomotive HUD -->
                <div style="background: rgba(59, 130, 246, 0.05); border: 1px solid rgba(59, 130, 246, 0.15); border-radius: 16px; padding: 15px; margin-bottom: 15px;">
                    <div style="font-size: 11px; color: var(--accent-blue); font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 8px; border-bottom: 1px solid rgba(59, 130, 246, 0.15); padding-bottom: 4px;">
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
                    <div id="hud-funcs" style="font-size: 11px; font-family: monospace; color: var(--text-secondary); background: rgba(0,0,0,0.2); padding: 6px; border-radius: 6px; text-align: center; letter-spacing: 1px;">
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

            <button class="btn btn-stop" style="background: linear-gradient(135deg, var(--accent-success), #059669); box-shadow: 0 4px 15px rgba(16, 185, 129, 0.3); animation: none;" onclick="applyWifiConfig()">💾 Save Credentials & Reboot</button>
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

                pinVal.innerText = `GPIO ${data.pin}`;
                successVal.innerText = data.success_count;
                errorsVal.innerText = data.error_count;

                if (data.status === "Active") {
                    badge.innerHTML = `
                        <span class="status-badge" style="background: rgba(16, 185, 129, 0.1); border-color: rgba(16, 185, 129, 0.25); color: #10b981; font-size: 10px; padding: 2px 6px;">
                            <span class="status-dot" style="background-color: #10b981; box-shadow: 0 0 8px #10b981; width: 6px; height: 6px;"></span>
                            <span id="decoder-status-txt" style="margin-left: 4px;">ACTIVE</span>
                        </span>
                    `;
                } else {
                    badge.innerHTML = `
                        <span class="status-badge" style="background: rgba(239, 68, 68, 0.1); border-color: rgba(239, 68, 68, 0.25); color: #ef4444; font-size: 10px; padding: 2px 6px;">
                            <span class="status-dot" style="background-color: #ef4444; box-shadow: 0 0 8px #ef4444; width: 6px; height: 6px;"></span>
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
