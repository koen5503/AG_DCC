/**
 * @file WebServer.cpp
 * @brief Implementation of the Embedded Web Server for DCC and Wi-Fi Control.
 * @author Vincent Hamp / Antigravity Refactoring
 * @date 2026-05-20
 * 
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "WebServer.hpp"
#include "index_html.h"
#include "WiThrottleServer.hpp"
#include <esp_log.h>
#include <cJSON.h>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_freertos_hooks.h"

static const char* TAG = "WebServer";

#ifdef CONFIG_BUILD_TEST_GENERATOR

#include "TestGenerator.hpp"

// Beautiful, responsive glassmorphic Test Generator HTML Control Panel
static const char* INDEX_HTML_TEST_GEN = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-S3 WiThrottle Test Generator</title>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0f172a;
            --panel-bg: rgba(30, 41, 59, 0.45);
            --primary: #06b6d4;
            --primary-hover: #0891b2;
            --success: #10b981;
            --danger: #ef4444;
            --text: #f8fafc;
            --text-secondary: #94a3b8;
            --border: rgba(255, 255, 255, 0.08);
        }
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: 'Outfit', sans-serif;
            -webkit-font-smoothing: antialiased;
        }
        body {
            background: radial-gradient(circle at top right, #1e1b4b, var(--bg-color));
            color: var(--text);
            min-height: 100vh;
            padding: 2rem 1rem;
            display: flex;
            justify-content: center;
        }
        .container {
            width: 100%;
            max-width: 1100px;
            display: grid;
            grid-template-columns: 1fr 1.2fr;
            gap: 2rem;
        }
        @media (max-width: 850px) {
            .container {
                grid-template-columns: 1fr;
            }
        }
        header {
            grid-column: 1 / -1;
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 1rem;
            border-bottom: 1px solid var(--border);
        }
        h1 {
            font-size: 2rem;
            font-weight: 800;
            background: linear-gradient(to right, #22d3ee, #818cf8);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .glass-panel {
            background: var(--panel-bg);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid var(--border);
            border-radius: 20px;
            padding: 2rem;
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
            display: flex;
            flex-direction: column;
            gap: 1.5rem;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        h2 {
            font-size: 1.4rem;
            font-weight: 600;
            color: var(--primary);
            border-left: 4px solid var(--primary);
            padding-left: 0.75rem;
            line-height: 1.2;
        }
        .form-group {
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
        }
        label {
            font-size: 0.9rem;
            font-weight: 600;
            color: var(--text-secondary);
        }
        input, select {
            background: rgba(15, 23, 42, 0.6);
            border: 1px solid var(--border);
            border-radius: 10px;
            padding: 0.75rem 1rem;
            color: var(--text);
            font-size: 1rem;
            outline: none;
            transition: border-color 0.2s;
        }
        input:focus, select:focus {
            border-color: var(--primary);
        }
        .btn {
            background: var(--primary);
            color: #0f172a;
            border: none;
            border-radius: 10px;
            padding: 0.75rem 1.5rem;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: background 0.2s, transform 0.1s;
            text-align: center;
        }
        .btn:hover {
            background: var(--primary-hover);
        }
        .btn:active {
            transform: scale(0.98);
        }
        .btn-danger {
            background: var(--danger);
            color: white;
        }
        .btn-danger:hover {
            background: #dc2626;
        }
        .row {
            display: flex;
            gap: 1rem;
        }
        .slider-container {
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .slider-container input[type="range"] {
            flex: 1;
            cursor: pointer;
        }
        .slider-val {
            font-size: 1.2rem;
            font-weight: 800;
            color: var(--primary);
            min-width: 30px;
            text-align: right;
        }
        .func-grid {
            display: grid;
            grid-template-columns: repeat(5, 1fr);
            gap: 0.5rem;
        }
        .func-btn {
            background: rgba(255,255,255,0.05);
            border: 1px solid var(--border);
            color: var(--text);
            border-radius: 8px;
            padding: 0.5rem;
            font-size: 0.85rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
        }
        .func-btn.active {
            background: var(--primary);
            color: #0f172a;
            border-color: var(--primary);
            box-shadow: 0 0 10px rgba(6, 182, 212, 0.4);
        }
        .metrics-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 1rem;
        }
        .metric-card {
            background: rgba(15, 23, 42, 0.4);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 0.25rem;
        }
        .metric-val {
            font-size: 1.8rem;
            font-weight: 800;
            color: var(--primary);
        }
        .metric-label {
            font-size: 0.8rem;
            font-weight: 600;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .console {
            background: rgba(15, 23, 42, 0.8);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            font-family: 'Courier New', Courier, monospace;
            font-size: 0.85rem;
            color: #10b981;
            height: 250px;
            overflow-y: auto;
            white-space: pre-wrap;
        }
        .status-badge {
            padding: 0.35rem 0.85rem;
            border-radius: 50px;
            font-size: 0.85rem;
            font-weight: 600;
            display: inline-block;
        }
        .status-active {
            background: rgba(16, 185, 129, 0.15);
            color: var(--success);
            border: 1px solid var(--success);
        }
        .status-idle {
            background: rgba(239, 68, 68, 0.15);
            color: var(--danger);
            border: 1px solid var(--danger);
        }

        /* Modal styling */
        .modal-overlay {
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(15, 23, 42, 0.9);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 1000;
            opacity: 0;
            pointer-events: none;
            padding: 20px;
            transition: opacity 0.2s;
        }
        .modal-overlay.active {
            opacity: 1;
            pointer-events: auto;
        }
        .modal-content {
            background: #1e293b;
            border: 1px solid var(--border);
            border-radius: 20px;
            width: 100%;
            max-width: 480px;
            padding: 2rem;
            display: flex;
            flex-direction: column;
            gap: 1.25rem;
            box-shadow: 0 20px 50px rgba(0,0,0,0.5);
        }
        .modal-header {
            font-size: 1.3rem;
            font-weight: 700;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--border);
            padding-bottom: 0.75rem;
        }
        .modal-close {
            background: none;
            border: none;
            color: var(--text-secondary);
            font-size: 1.8rem;
            cursor: pointer;
            line-height: 1;
        }
        .network-list {
            max-height: 150px;
            overflow-y: auto;
            background: rgba(15, 23, 42, 0.6);
            border: 1px solid var(--border);
            border-radius: 10px;
            display: flex;
            flex-direction: column;
        }
        .network-item {
            padding: 0.75rem 1rem;
            display: flex;
            justify-content: space-between;
            align-items: center;
            cursor: pointer;
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
            font-size: 0.95rem;
            font-weight: 600;
        }
        .network-item:hover {
            background: rgba(255, 255, 255, 0.05);
        }
        .network-item.selected {
            background: rgba(6, 182, 212, 0.2);
            color: #22d3ee;
            border-left: 3px solid var(--primary);
        }
        .rssi-indicator {
            display: flex;
            align-items: center;
            gap: 6px;
            font-size: 0.8rem;
            color: var(--text-secondary);
        }
        .spinner {
            width: 24px;
            height: 24px;
            border: 3px solid rgba(255,255,255,0.1);
            border-radius: 50%;
            border-top-color: var(--primary);
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
            color: #f59e0b;
        }
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>WiThrottle Client Generator</h1>
            <div style="display: flex; gap: 10px; align-items: center;">
                <button class="btn" style="padding: 8px 15px; font-size: 13px;" onclick="openWifiModal()">🌐 Wi-Fi Setup</button>
                <div class="status-badge" style="background: rgba(6, 182, 212, 0.15); color: var(--primary); border: 1px solid var(--primary); font-size: 13px;" id="wifi-status">Checking AP/STA...</div>
                <div id="connection-status" class="status-badge status-idle">DISCONNECTED</div>
            </div>
        </header>

        <!-- Left Column: Settings and Manual Cabs -->
        <div class="glass-panel">
            <h2>WiThrottle Server Configuration</h2>
            <div class="form-group">
                <label for="server-ip">Target IP Address</label>
                <div class="row">
                    <input type="text" id="server-ip" value="192.168.4.1" style="flex: 1;">
                    <button class="btn" onclick="saveConfig()">Apply</button>
                </div>
            </div>

            <h2>Manual Locomotive Throttle</h2>
            <div class="row">
                <div class="form-group" style="flex: 1;">
                    <label for="loco-addr">Loco Address</label>
                    <input type="number" id="loco-addr" value="3" min="1" max="9999">
                </div>
                <div class="form-group" style="flex: 1;">
                    <label for="loco-type">Type</label>
                    <select id="loco-type">
                        <option value="S">Short (0-127)</option>
                        <option value="L" selected>Long (128-9999)</option>
                    </select>
                </div>
            </div>
            <div class="row">
                <button class="btn" style="flex: 1;" onclick="acquireLoco()">Acquire</button>
                <button class="btn btn-danger" style="flex: 1;" onclick="releaseLoco()">Release</button>
            </div>

            <div class="form-group">
                <label>Speed Step (0-126)</label>
                <div class="slider-container">
                    <input type="range" id="speed-slider" min="0" max="126" value="0" oninput="updateSpeedVal(this.value)" onchange="sendSpeed(this.value)">
                    <div id="speed-val" class="slider-val">0</div>
                </div>
            </div>

            <div class="row">
                <button class="btn" style="flex: 1;" onclick="sendDirection(1)">Forward</button>
                <button class="btn btn-danger" style="flex: 1;" onclick="sendDirection(0)">Reverse</button>
                <button class="btn btn-danger" style="flex: 1; background: #dc2626;" onclick="sendEStop()">E-STOP</button>
            </div>

            <div class="form-group">
                <label>Function Keys (F0 - F9)</label>
                <div class="func-grid">
                    <button class="func-btn" id="f-0" onclick="toggleFunc(0)">F0</button>
                    <button class="func-btn" id="f-1" onclick="toggleFunc(1)">F1</button>
                    <button class="func-btn" id="f-2" onclick="toggleFunc(2)">F2</button>
                    <button class="func-btn" id="f-3" onclick="toggleFunc(3)">F3</button>
                    <button class="func-btn" id="f-4" onclick="toggleFunc(4)">F4</button>
                    <button class="func-btn" id="f-5" onclick="toggleFunc(5)">F5</button>
                    <button class="func-btn" id="f-6" onclick="toggleFunc(6)">F6</button>
                    <button class="func-btn" id="f-7" onclick="toggleFunc(7)">F7</button>
                    <button class="func-btn" id="f-8" onclick="toggleFunc(8)">F8</button>
                    <button class="func-btn" id="f-9" onclick="toggleFunc(9)">F9</button>
                </div>
            </div>

            <h2>Accessory Turnouts</h2>
            <div class="row">
                <div class="form-group" style="flex: 1.2;">
                    <label for="turnout-addr">Turnout ID</label>
                    <input type="number" id="turnout-addr" value="10" min="1" max="2048">
                </div>
                <button class="btn" style="flex: 1; align-self: flex-end;" onclick="sendTurnout(true)">Close</button>
                <button class="btn btn-danger" style="flex: 1; align-self: flex-end;" onclick="sendTurnout(false)">Throw</button>
            </div>
        </div>

        <!-- Right Column: Automated Test Harness Metrics and Console -->
        <div class="glass-panel">
            <h2>Autonomous Testing Orchestrator</h2>
            <div class="row">
                <div class="form-group" style="flex: 1.5;">
                    <label for="scenario-select">Select Test Scenario</label>
                    <select id="scenario-select">
                        <option value="1">Scenario 1: Speed & Dir Sweep (Loco 3)</option>
                        <option value="2">Scenario 2: Rapid Turnout Switching Storm</option>
                        <option value="3">Scenario 3: Consist & High-Priority Concurrency</option>
                    </select>
                </div>
                <button class="btn" style="flex: 1; align-self: flex-end; background: var(--success); color: white;" onclick="launchTest()">Launch Test</button>
            </div>

            <div class="metrics-grid">
                <div class="metric-card">
                    <div class="metric-val" id="total-tests">0</div>
                    <div class="metric-label">Total Executed</div>
                </div>
                <div class="metric-card">
                    <div class="metric-val" id="success-rate">0.0%</div>
                    <div class="metric-label">Success Rate</div>
                </div>
                <div class="metric-card">
                    <div class="metric-val" id="avg-latency">0.0 ms</div>
                    <div class="metric-label">Avg DCC Latency</div>
                </div>
                <div class="metric-card">
                    <div class="metric-val" id="packets-lost">0</div>
                    <div class="metric-label">Packets Lost</div>
                </div>
            </div>

            <h2>Automated Verification Logs</h2>
            <div class="console" id="console-log">Idle. Waiting for test execution...</div>
        </div>
    </div>

    <!-- Wi-Fi Setup Modal -->
    <div class="modal-overlay" id="wifi-modal">
        <div class="modal-content" id="modal-body-content">
            <div class="modal-header">
                <span style="font-weight: 700;">🌐 Wi-Fi Configuration</span>
                <button class="modal-close" onclick="closeWifiModal()">×</button>
            </div>
            
            <div style="font-size: 0.9rem; color: var(--text-secondary); line-height: 1.4;">
                Select a local 2.4GHz Wi-Fi network to connect the Test Generator. In Station mode, the device will request an IP from your router and communicate with the Command Center.
            </div>
            
            <div class="form-group">
                <label>Scanned Networks</label>
                <div class="network-list" id="network-list">
                    <div style="padding: 15px; text-align: center; color: var(--text-secondary); font-size: 0.85rem;">
                        Loading networks...
                    </div>
                </div>
            </div>
            
            <button class="btn" id="btn-scan" style="padding: 10px; font-size: 0.85rem; background: rgba(255,255,255,0.05);" onclick="scanWifiNetworks()">🔄 Refresh Scan</button>
            <div class="spinner" id="scan-loading" style="display: none; margin: 10px auto;"></div>
            
            <div class="form-group" id="manual-ssid-group" style="display: none;">
                <label for="wifi-ssid">Manual SSID</label>
                <input type="text" id="wifi-ssid" placeholder="Enter SSID manually">
            </div>
            
            <div class="form-group">
                <label for="wifi-pass">WPA2 Password</label>
                <input type="password" id="wifi-pass" placeholder="Enter network password">
            </div>
            
            <button class="btn" style="background: var(--success); color: #0f172a; margin-top: 10px;" onclick="applyWifiConfig()">💾 Save and Connect</button>
        </div>
    </div>

    <script>
        let functions = Array(10).fill(false);
        let hasLoadedConfig = false;
        let selectedSsid = '';

        function updateSpeedVal(val) {
            document.getElementById('speed-val').innerText = val;
        }

        async function saveConfig() {
            const ip = document.getElementById('server-ip').value;
            const res = await fetch('/api/test/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ server_ip: ip })
            });
            alert(res.ok ? 'Target IP successfully updated!' : 'Failed to update target IP.');
        }

        async function acquireLoco() {
            const addr = parseInt(document.getElementById('loco-addr').value);
            const type = document.getElementById('loco-type').value;
            const res = await fetch('/api/throttle/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ address: addr, is_long: (type === 'L'), acquire: true })
            });
            if (res.ok) {
                document.getElementById('console-log').innerText += `\nLocomotive address ${addr} successfully acquired.\n`;
            }
        }

        async function releaseLoco() {
            const addr = parseInt(document.getElementById('loco-addr').value);
            const type = document.getElementById('loco-type').value;
            const res = await fetch('/api/throttle/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ address: addr, is_long: (type === 'L'), release: true })
            });
            if (res.ok) {
                document.getElementById('console-log').innerText += `\nLocomotive address ${addr} released.\n`;
            }
        }

        async function sendSpeed(speed) {
            const addr = parseInt(document.getElementById('loco-addr').value);
            const type = document.getElementById('loco-type').value;
            await fetch('/api/throttle/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ address: addr, is_long: (type === 'L'), speed: parseInt(speed) })
            });
        }

        async function sendDirection(dir) {
            const addr = parseInt(document.getElementById('loco-addr').value);
            const type = document.getElementById('loco-type').value;
            await fetch('/api/throttle/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ address: addr, is_long: (type === 'L'), direction: (dir === 1) })
            });
        }

        async function sendEStop() {
            const addr = parseInt(document.getElementById('loco-addr').value);
            const type = document.getElementById('loco-type').value;
            await fetch('/api/throttle/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ address: addr, is_long: (type === 'L'), speed: -1 })
            });
        }

        async function toggleFunc(num) {
            const addr = parseInt(document.getElementById('loco-addr').value);
            const type = document.getElementById('loco-type').value;
            functions[num] = !functions[num];
            const btn = document.getElementById(`f-${num}`);
            if (functions[num]) btn.classList.add('active');
            else btn.classList.remove('active');

            await fetch('/api/throttle/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ address: addr, is_long: (type === 'L'), func: num, func_state: functions[num] })
            });
        }

        async function sendTurnout(straight) {
            const addr = parseInt(document.getElementById('turnout-addr').value);
            await fetch('/api/turnout/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ address: addr, straight: straight })
            });
        }

        async function launchTest() {
            const scenario = parseInt(document.getElementById('scenario-select').value);
            const res = await fetch('/api/test/launch', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ scenario: scenario })
            });
            if (res.ok) {
                document.getElementById('console-log').innerText = `[INFO] Launched Scenario ${scenario} in the background. Monitoring...\n`;
            }
        }

        async function pollResults() {
            try {
                const res = await fetch('/api/test/results');
                if (res.ok) {
                    const data = await res.json();
                    
                    // On first load, populate Target IP textbox with loaded value from NVS
                    if (!hasLoadedConfig && data.server_ip) {
                        document.getElementById('server-ip').value = data.server_ip;
                        hasLoadedConfig = true;
                    }

                    // Dynamically update Wi-Fi status badge
                    const wifiBadge = document.getElementById('wifi-status');
                    if (wifiBadge) {
                        if (data.wifi_mode === 'AP Mode') {
                            wifiBadge.innerText = 'AP Mode';
                        } else {
                            wifiBadge.innerText = `STA: ${data.wifi_ssid} (${data.wifi_ip})`;
                        }
                    }

                    document.getElementById('total-tests').innerText = data.total_tests;
                    document.getElementById('avg-latency').innerText = data.avg_latency_ms.toFixed(1) + ' ms';
                    document.getElementById('packets-lost').innerText = data.packets_lost;
                    
                    const rate = data.total_tests > 0 ? (data.success_count * 100 / data.total_tests) : 0.0;
                    document.getElementById('success-rate').innerText = rate.toFixed(1) + '%';
                    
                    if (data.is_running) {
                        document.getElementById('console-log').innerText = data.last_log;
                        document.getElementById('connection-status').innerText = "TEST ACTIVE";
                        document.getElementById('connection-status').className = "status-badge status-active";
                    } else {
                        if (data.last_log && data.last_log !== "No test run yet.") {
                            document.getElementById('console-log').innerText = data.last_log;
                        }
                        document.getElementById('connection-status').innerText = "CONNECTED";
                        document.getElementById('connection-status').className = "status-badge status-active";
                    }
                }
            } catch (e) {
                document.getElementById('connection-status').innerText = "DISCONNECTED";
                document.getElementById('connection-status').className = "status-badge status-idle";
            }
        }

        // Wi-Fi Setup Modal controllers
        function openWifiModal() {
            document.getElementById('wifi-modal').className = 'modal-overlay active';
            scanWifiNetworks();
        }

        function closeWifiModal() {
            document.getElementById('wifi-modal').className = 'modal-overlay';
        }

        async function scanWifiNetworks() {
            const list = document.getElementById('network-list');
            list.innerHTML = '<div style="padding: 15px; text-align: center; color: var(--text-secondary); font-size: 0.85rem;">Scanning networks...</div>';
            document.getElementById('btn-scan').style.display = 'none';
            document.getElementById('scan-loading').style.display = 'block';
            
            try {
                const res = await fetch('/api/wifi/scan');
                document.getElementById('scan-loading').style.display = 'none';
                document.getElementById('btn-scan').style.display = 'block';
                
                if (res.ok) {
                    const networks = await res.json();
                    list.innerHTML = `<div class="network-item selected" onclick="selectNetwork('Manual')">Enter SSID Manually...</div>`;
                    
                    if (networks.length === 0) {
                        return;
                    }
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
                } else {
                    list.innerHTML = '<div style="padding: 15px; text-align: center; color: var(--danger); font-size: 0.85rem;">Failed to scan networks</div>';
                }
            } catch (e) {
                document.getElementById('scan-loading').style.display = 'none';
                document.getElementById('btn-scan').style.display = 'block';
                list.innerHTML = '<div style="padding: 15px; text-align: center; color: var(--danger); font-size: 0.85rem;">Error scanning networks</div>';
            }
        }

        function selectNetwork(ssid) {
            selectedSsid = ssid;
            const items = document.querySelectorAll('.network-item');
            items.forEach(i => i.classList.remove('selected'));
            
            if (ssid === 'Manual') {
                document.getElementById('manual-ssid-group').style.display = 'flex';
                document.getElementById('wifi-ssid').value = '';
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

        async function applyWifiConfig() {
            let ssid = document.getElementById('wifi-ssid').value.trim();
            let pass = document.getElementById('wifi-pass').value.trim();

            if (!ssid) {
                alert('SSID cannot be empty!');
                return;
            }

            try {
                const res = await fetch('/api/wifi/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid: ssid, password: pass })
                });
                if (res.ok) {
                    showRebootCountdown(ssid);
                } else {
                    alert('Failed to save Wi-Fi configuration.');
                }
            } catch (e) {
                alert('Error sending Wi-Fi configuration.');
            }
        }

        function showRebootCountdown(ssid) {
            const body = document.getElementById('modal-body-content');
            body.innerHTML = `
                <div class="reboot-screen">
                    <h2 style="font-weight: 700; color: #f59e0b;">Saving & Rebooting</h2>
                    <p style="font-size: 0.9rem; color: var(--text-secondary); line-height: 1.4;">
                        The Test Generator has stored credentials for <strong>${ssid}</strong> and is rebooting to connect.
                    </p>
                    <div class="spinner"></div>
                    <div class="reboot-countdown" id="reboot-timer">15</div>
                    <p style="font-size: 0.8rem; color: var(--text-secondary);">
                        Reconnecting... Connect your device to the same Wi-Fi network and reload this page.
                    </p>
                </div>
            `;

            let timeLeft = 15;
            const timer = setInterval(() => {
                timeLeft--;
                document.getElementById('reboot-timer').innerText = timeLeft;
                if (timeLeft <= 0) {
                    clearInterval(timer);
                    window.location.reload();
                }
            }, 1000);
        }

        setInterval(pollResults, 1000);
        pollResults();
    </script>
</body>
</html>
)rawhtml";

namespace dcc {
namespace web {

WebServer::WebServer(dcc::wt::TestGenerator* test_generator, dcc::wifi::WifiManager* wifi_manager)
    : m_transmitter(nullptr),
      m_wifi_manager(wifi_manager),
      m_decoder(nullptr),
      m_server_handle(nullptr),
      m_decoder_pin(-1),
      m_last_loco_address(-1),
      m_last_loco_speed(-1),
      m_last_loco_direction(false),
      m_withrottle_server(nullptr),
      m_test_generator(test_generator) {
    std::memset(m_last_f_states, 0, sizeof(m_last_f_states));
}

WebServer::~WebServer() {
    stop();
}

esp_err_t WebServer::indexGetHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML_TEST_GEN, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t testConfigPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    // Read POST body
    char buf[256];
    int ret = httpd_req_recv(req, buf, std::min(static_cast<int>(req->content_len), static_cast<int>(sizeof(buf)) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON* json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* ip_item = cJSON_GetObjectItem(json, "server_ip");
    if (ip_item && cJSON_IsString(ip_item)) {
        self->m_test_generator->setServerIp(ip_item->valuestring);
    }
    cJSON_Delete(json);

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    char* resp_str = cJSON_PrintUnformatted(resp);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t throttleSetPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, std::min(static_cast<int>(req->content_len), static_cast<int>(sizeof(buf)) - 1));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON* json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    cJSON* addr_item = cJSON_GetObjectItem(json, "address");
    cJSON* long_item = cJSON_GetObjectItem(json, "is_long");
    
    if (!addr_item) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    int address = addr_item->valueint;
    bool is_long = cJSON_IsTrue(long_item);

    if (cJSON_GetObjectItem(json, "acquire")) {
        self->m_test_generator->acquireLocomotive(address, is_long);
    } 
    else if (cJSON_GetObjectItem(json, "release")) {
        self->m_test_generator->releaseLocomotive(address, is_long);
    } 
    else if (cJSON_GetObjectItem(json, "speed")) {
        int speed = cJSON_GetObjectItem(json, "speed")->valueint;
        self->m_test_generator->setSpeed(address, is_long, speed);
    } 
    else if (cJSON_GetObjectItem(json, "direction")) {
        bool direction = cJSON_IsTrue(cJSON_GetObjectItem(json, "direction"));
        self->m_test_generator->setDirection(address, is_long, direction);
    } 
    else if (cJSON_GetObjectItem(json, "func")) {
        int func = cJSON_GetObjectItem(json, "func")->valueint;
        bool state = cJSON_IsTrue(cJSON_GetObjectItem(json, "func_state"));
        self->m_test_generator->setFunction(address, is_long, func, state);
    }

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t turnoutSetPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, std::min(static_cast<int>(req->content_len), static_cast<int>(sizeof(buf)) - 1));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON* json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    cJSON* addr_item = cJSON_GetObjectItem(json, "address");
    cJSON* str_item = cJSON_GetObjectItem(json, "straight");

    if (addr_item && str_item) {
        self->m_test_generator->setTurnout(addr_item->valueint, cJSON_IsTrue(str_item));
    }

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t testLaunchPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, std::min(static_cast<int>(req->content_len), static_cast<int>(sizeof(buf)) - 1));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON* json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    cJSON* sc_item = cJSON_GetObjectItem(json, "scenario");
    if (sc_item) {
        self->m_test_generator->launchTestScenario(sc_item->valueint);
    }

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t testResultsGetHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    std::string results = self->m_test_generator->getResultsJson();
    
    // Dynamically inject Wi-Fi and NVS telemetry into the results JSON using cJSON
    cJSON* json = cJSON_Parse(results.c_str());
    if (json) {
        cJSON_AddStringToObject(json, "server_ip", self->m_test_generator->getServerIp().c_str());
        cJSON_AddStringToObject(json, "wifi_mode", self->m_wifi_manager->isApMode() ? "AP Mode" : "Station Mode");
        cJSON_AddStringToObject(json, "wifi_ssid", self->m_wifi_manager->getConnectedSsid().c_str());
        cJSON_AddStringToObject(json, "wifi_ip", self->m_wifi_manager->getIpAddress().c_str());
        
        char* rendered = cJSON_PrintUnformatted(json);
        if (rendered) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, rendered);
            cJSON_free(rendered);
        } else {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, results.c_str());
        }
        cJSON_Delete(json);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, results.c_str());
    }
    return ESP_OK;
}

esp_err_t WebServer::wifiScanGetHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string scan_result = self->m_wifi_manager->scanNetworksJson();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_sendstr(req, scan_result.c_str());
}

esp_err_t WebServer::wifiConfigPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, std::min(static_cast<int>(req->content_len), static_cast<int>(sizeof(buf)) - 1));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON* json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON body");
        return ESP_FAIL;
    }

    cJSON* ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON* pass_item = cJSON_GetObjectItem(json, "password");

    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID must be a string");
        return ESP_FAIL;
    }

    std::string ssid = ssid_item->valuestring;
    std::string password = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Received Wi-Fi provisioning credentials. SSID: %s", ssid.c_str());
    
    bool saved = self->m_wifi_manager->saveCredentials(ssid, password);
    if (!saved) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write credentials to NVS");
        return ESP_FAIL;
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    char* resp_str = cJSON_PrintUnformatted(resp);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);

    // Schedule system reboot in 1.5 seconds to attempt station connection
    self->m_wifi_manager->scheduleReboot(1500);
    return ESP_OK;
}

bool WebServer::start() {
    ESP_LOGI(TAG, "Starting Web Server in Test Generator Client Mode on Port 80...");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32769; // avoid conflicts

    esp_err_t err = httpd_start(&m_server_handle, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return false;
    }

    // Register handlers
    httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = indexGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &index_uri);

    httpd_uri_t wifi_scan_uri = {
        .uri      = "/api/wifi/scan",
        .method   = HTTP_GET,
        .handler  = wifiScanGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &wifi_scan_uri);

    httpd_uri_t wifi_config_uri = {
        .uri      = "/api/wifi/config",
        .method   = HTTP_POST,
        .handler  = wifiConfigPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &wifi_config_uri);

    httpd_uri_t config_uri = {
        .uri      = "/api/test/config",
        .method   = HTTP_POST,
        .handler  = testConfigPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &config_uri);

    httpd_uri_t throttle_uri = {
        .uri      = "/api/throttle/set",
        .method   = HTTP_POST,
        .handler  = throttleSetPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &throttle_uri);

    httpd_uri_t turnout_uri = {
        .uri      = "/api/turnout/set",
        .method   = HTTP_POST,
        .handler  = turnoutSetPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &turnout_uri);

    httpd_uri_t launch_uri = {
        .uri      = "/api/test/launch",
        .method   = HTTP_POST,
        .handler  = testLaunchPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &launch_uri);

    httpd_uri_t results_uri = {
        .uri      = "/api/test/results",
        .method   = HTTP_GET,
        .handler  = testResultsGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &results_uri);

    return true;
}

void WebServer::stop() {
    if (m_server_handle != nullptr) {
        httpd_stop(m_server_handle);
        m_server_handle = nullptr;
    }
}

} // namespace web
} // namespace dcc

#else // CONFIG_BUILD_TEST_GENERATOR (Original DCC Command Center Web Server)

namespace dcc {
namespace web {

static std::string read_post_body(httpd_req_t* req) {
    char buf[512];
    int ret, remaining = req->content_len;
    std::string body = "";
    
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, std::min(remaining, (int)sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return "";
        }
        body.append(buf, ret);
        remaining -= ret;
    }
    return body;
}

WebServer::WebServer(dcc::rmt::DccRmtTransmitter* transmitter, 
                     dcc::wifi::WifiManager* wifi_manager, 
                     dcc::rx::DccDecoder* decoder, 
                     int decoder_pin,
                     dcc::wt::WiThrottleServer* withrottle_server)
    : m_transmitter(transmitter),
      m_wifi_manager(wifi_manager),
      m_decoder(decoder),
      m_server_handle(nullptr),
      m_decoder_pin(decoder_pin),
      m_withrottle_server(withrottle_server),
      m_test_generator(nullptr) {
    m_last_loco_address = -1;
    m_last_loco_speed = -1;
    m_last_loco_direction = false;
    std::memset(m_last_f_states, 0, sizeof(m_last_f_states));
}

static volatile uint64_t s_idle_count_core0 = 0;
static volatile uint64_t s_idle_count_core1 = 0;

static bool IRAM_ATTR idle_hook_core0(void) {
    s_idle_count_core0 = s_idle_count_core0 + 1;
    return true;
}

static bool IRAM_ATTR idle_hook_core1(void) {
    s_idle_count_core1 = s_idle_count_core1 + 1;
    return true;
}

static float s_cpu_load_core0 = 0.0f;
static float s_cpu_load_core1 = 0.0f;

static void cpuMonitorTask(void* param) {
    // Register the idle hooks for each CPU
    esp_register_freertos_idle_hook_for_cpu(idle_hook_core0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle_hook_core1, 1);

    uint64_t last_idle_core0 = 0;
    uint64_t last_idle_core1 = 0;
    uint64_t max_delta_core0 = 1; 
    uint64_t max_delta_core1 = 1;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Sample every 1 second

        uint64_t curr_idle0 = s_idle_count_core0;
        uint64_t curr_idle1 = s_idle_count_core1;

        uint64_t delta0 = curr_idle0 - last_idle_core0;
        uint64_t delta1 = curr_idle1 - last_idle_core1;

        last_idle_core0 = curr_idle0;
        last_idle_core1 = curr_idle1;

        // Auto-calibrate: dynamically track the maximum idle count in any 1s window
        if (delta0 > max_delta_core0) max_delta_core0 = delta0;
        if (delta1 > max_delta_core1) max_delta_core1 = delta1;

        // Calculate load percentage
        float load0 = 100.0f - (static_cast<float>(delta0) * 100.0f / static_cast<float>(max_delta_core0));
        float load1 = 100.0f - (static_cast<float>(delta1) * 100.0f / static_cast<float>(max_delta_core1));

        // Clamp
        if (load0 < 0.0f) load0 = 0.0f;
        if (load0 > 100.0f) load0 = 100.0f;
        if (load1 < 0.0f) load1 = 0.0f;
        if (load1 > 100.0f) load1 = 100.0f;

        s_cpu_load_core0 = load0;
        s_cpu_load_core1 = load1;
    }
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    if (m_server_handle != nullptr) {
        ESP_LOGW(TAG, "Server is already running.");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Allow more open sockets or stack if needed
    config.stack_size = 4096;
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting web server on port: %d", config.server_port);
    if (httpd_start(&m_server_handle, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server.");
        return false;
    }

    // Start CPU load monitoring task on Core 0 (tracks both Core 0 & Core 1)
    xTaskCreatePinnedToCore(cpuMonitorTask, "cpu_monitor", 2048, nullptr, 1, NULL, 0);

    // Register GET / (Serve Dashboard)
    httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = indexGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &index_uri);

    // Register GET /api/wifi/scan
    httpd_uri_t wifi_scan_uri = {
        .uri      = "/api/wifi/scan",
        .method   = HTTP_GET,
        .handler  = wifiScanGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &wifi_scan_uri);

    // Register POST /api/wifi/config
    httpd_uri_t wifi_config_uri = {
        .uri      = "/api/wifi/config",
        .method   = HTTP_POST,
        .handler  = wifiConfigPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &wifi_config_uri);

    // Register GET/POST /api/loco
    httpd_uri_t loco_uri = {
        .uri      = "/api/loco",
        .method   = static_cast<http_method>(HTTP_ANY), // Handles GET and POST
        .handler  = locoGetPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &loco_uri);

    // Register POST /api/accessory
    httpd_uri_t accessory_uri = {
        .uri      = "/api/accessory",
        .method   = HTTP_POST,
        .handler  = accessoryPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &accessory_uri);

    // Register POST /api/test
    httpd_uri_t test_uri = {
        .uri      = "/api/test",
        .method   = HTTP_POST,
        .handler  = testPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &test_uri);

    // Register GET /api/decoder
    httpd_uri_t decoder_uri = {
        .uri      = "/api/decoder",
        .method   = HTTP_GET,
        .handler  = decoderGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &decoder_uri);

    // Register POST /api/decoder/toggle
    httpd_uri_t decoder_toggle_uri = {
        .uri      = "/api/decoder/toggle",
        .method   = HTTP_POST,
        .handler  = decoderTogglePostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &decoder_toggle_uri);

    // Register POST /api/trigger
    httpd_uri_t trigger_uri = {
        .uri      = "/api/trigger",
        .method   = HTTP_POST,
        .handler  = triggerPostHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &trigger_uri);

    // Register GET /api/withrottle
    httpd_uri_t withrottle_uri = {
        .uri      = "/api/withrottle",
        .method   = HTTP_GET,
        .handler  = withrottleGetHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(m_server_handle, &withrottle_uri);

    ESP_LOGI(TAG, "Web server started successfully and endpoints registered.");
    return true;
}

void WebServer::stop() {
    if (m_server_handle != nullptr) {
        ESP_LOGI(TAG, "Stopping web server.");
        httpd_stop(m_server_handle);
        m_server_handle = nullptr;
    }
}

esp_err_t WebServer::indexGetHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    // Serve our elegant glassmorphic dashboard
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::wifiScanGetHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string scan_result = self->m_wifi_manager->scanNetworksJson();
    
    httpd_resp_set_type(req, "application/json");
    // Prevent client caching of scan results
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_sendstr(req, scan_result.c_str());
}

esp_err_t WebServer::wifiConfigPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON body");
        return ESP_FAIL;
    }

    cJSON* ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON* pass_item = cJSON_GetObjectItem(json, "password");

    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID must be a string");
        return ESP_FAIL;
    }

    std::string ssid = ssid_item->valuestring;
    std::string password = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Received Wi-Fi provisioning credentials. SSID: %s", ssid.c_str());
    
    bool saved = self->m_wifi_manager->saveCredentials(ssid, password);
    if (!saved) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write credentials to NVS");
        return ESP_FAIL;
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    char* resp_str = cJSON_PrintUnformatted(resp);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);

    // Schedule system reboot in 1.5 seconds to attempt station connection
    self->m_wifi_manager->scheduleReboot(1500);
    return ESP_OK;
}

esp_err_t WebServer::locoGetPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);

    if (req->method == HTTP_GET) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "ok");
        cJSON_AddStringToObject(root, "mode", self->m_wifi_manager->isApMode() ? "AP Mode" : "Station Mode");
        cJSON_AddStringToObject(root, "ssid", self->m_wifi_manager->getConnectedSsid().c_str());
        
        char* rendered = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_sendstr(req, rendered);
        
        cJSON_free(rendered);
        cJSON_Delete(root);
        return ESP_OK;
    } 
    else if (req->method == HTTP_POST) {
        std::string body = read_post_body(req);
        cJSON* json = cJSON_Parse(body.c_str());
        if (!json) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            return ESP_FAIL;
        }

        cJSON* addr_item = cJSON_GetObjectItem(json, "address");
        cJSON* speed_item = cJSON_GetObjectItem(json, "speed");
        cJSON* dir_item = cJSON_GetObjectItem(json, "direction");
        cJSON* funcs_item = cJSON_GetObjectItem(json, "functions");

        if (!addr_item || !speed_item || !dir_item) {
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
            return ESP_FAIL;
        }

        int address = addr_item->valueint;
        int speed = speed_item->valueint;
        bool direction = cJSON_IsTrue(dir_item);
        bool f_states[9] = {false};

        if (funcs_item && cJSON_IsArray(funcs_item)) {
            int sz = cJSON_GetArraySize(funcs_item);
            for (int i = 0; i < std::min(sz, 9); ++i) {
                cJSON* item = cJSON_GetArrayItem(funcs_item, i);
                if (item) {
                    f_states[i] = cJSON_IsTrue(item);
                }
            }
        }
        cJSON_Delete(json);

        // 1. Generate 128-Speed Step Packet
        // Direction is MSB (Bit 7). Rest is 7-bit speed (0 = stop, 1 = e-stop, 2-127 = speed steps 1-126)
        uint8_t speed_byte = (direction ? 0x80 : 0x00) | (speed == 0 ? 0 : (speed + 1));
        uint8_t pkt_speed[8];
        size_t len_speed = 0;
        
        if (address < 128) {
            pkt_speed[0] = address;
            pkt_speed[1] = 0x3F; // 128 speed step control instruction prefix
            pkt_speed[2] = speed_byte;
            pkt_speed[3] = pkt_speed[0] ^ pkt_speed[1] ^ pkt_speed[2];
            len_speed = 4;
        } else {
            pkt_speed[0] = (address >> 8) | 0xC0; // Standard 2-byte long address prefix
            pkt_speed[1] = address & 0xFF;
            pkt_speed[2] = 0x3F;
            pkt_speed[3] = speed_byte;
            pkt_speed[4] = pkt_speed[0] ^ pkt_speed[1] ^ pkt_speed[2] ^ pkt_speed[3];
            len_speed = 5;
        }

        // 2. Generate Function Group 1 Packet (F0, F4, F3, F2, F1)
        // Instruction byte: 0x80 | (F0<<4) | (F4<<3) | (F3<<2) | (F2<<1) | F1
        uint8_t f0_4_byte = 0x80 | 
                           (f_states[0] ? 0x10 : 0) | 
                           (f_states[4] ? 0x08 : 0) | 
                           (f_states[3] ? 0x04 : 0) | 
                           (f_states[2] ? 0x02 : 0) | 
                           (f_states[1] ? 0x01 : 0);
        uint8_t pkt_f0_4[8];
        size_t len_f0_4 = 0;
        if (address < 128) {
            pkt_f0_4[0] = address;
            pkt_f0_4[1] = f0_4_byte;
            pkt_f0_4[2] = pkt_f0_4[0] ^ pkt_f0_4[1];
            len_f0_4 = 3;
        } else {
            pkt_f0_4[0] = (address >> 8) | 0xC0;
            pkt_f0_4[1] = address & 0xFF;
            pkt_f0_4[2] = f0_4_byte;
            pkt_f0_4[3] = pkt_f0_4[0] ^ pkt_f0_4[1] ^ pkt_f0_4[2];
            len_f0_4 = 4;
        }

        // 3. Generate Function Group 2 Packet (F8, F7, F6, F5)
        // Instruction byte: 0xB0 | (F8<<3) | (F7<<2) | (F6<<1) | F5
        uint8_t f5_8_byte = 0xB0 | 
                           (f_states[8] ? 0x08 : 0) | 
                           (f_states[7] ? 0x04 : 0) | 
                           (f_states[6] ? 0x02 : 0) | 
                           (f_states[5] ? 0x01 : 0);
        uint8_t pkt_f5_8[8];
        size_t len_f5_8 = 0;
        if (address < 128) {
            pkt_f5_8[0] = address;
            pkt_f5_8[1] = f5_8_byte;
            pkt_f5_8[2] = pkt_f5_8[0] ^ pkt_f5_8[1];
            len_f5_8 = 3;
        } else {
            pkt_f5_8[0] = (address >> 8) | 0xC0;
            pkt_f5_8[1] = address & 0xFF;
            pkt_f5_8[2] = f5_8_byte;
            pkt_f5_8[3] = pkt_f5_8[0] ^ pkt_f5_8[1] ^ pkt_f5_8[2];
            len_f5_8 = 4;
        }

        // Differential transmission (Strategy A): Only dispatch packet types whose target states have actually mutated
        bool send_speed = (self->m_last_loco_address != address) || 
                          (self->m_last_loco_speed != speed) || 
                          (self->m_last_loco_direction != direction);
        
        bool send_f0_4 = (self->m_last_loco_address != address) || 
                         (self->m_last_f_states[0] != f_states[0]) || 
                         (self->m_last_f_states[1] != f_states[1]) || 
                         (self->m_last_f_states[2] != f_states[2]) || 
                         (self->m_last_f_states[3] != f_states[3]) || 
                         (self->m_last_f_states[4] != f_states[4]);

        bool send_f5_8 = (self->m_last_loco_address != address) || 
                         (self->m_last_f_states[5] != f_states[5]) || 
                         (self->m_last_f_states[6] != f_states[6]) || 
                         (self->m_last_f_states[7] != f_states[7]) || 
                         (self->m_last_f_states[8] != f_states[8]);

        if (send_speed) {
            self->m_transmitter->sendPacket(pkt_speed, len_speed);
            self->m_last_loco_speed = speed;
            self->m_last_loco_direction = direction;
        }

        if (send_f0_4) {
            self->m_transmitter->sendPacket(pkt_f0_4, len_f0_4);
            self->m_last_f_states[0] = f_states[0];
            self->m_last_f_states[1] = f_states[1];
            self->m_last_f_states[2] = f_states[2];
            self->m_last_f_states[3] = f_states[3];
            self->m_last_f_states[4] = f_states[4];
        }

        if (send_f5_8) {
            self->m_transmitter->sendPacket(pkt_f5_8, len_f5_8);
            self->m_last_f_states[5] = f_states[5];
            self->m_last_f_states[6] = f_states[6];
            self->m_last_f_states[7] = f_states[7];
            self->m_last_f_states[8] = f_states[8];
        }

        if (send_speed || send_f0_4 || send_f5_8) {
            self->m_last_loco_address = address;
        }

        // Return primary Speed Packet bytes for frontend console telemetry logs
        cJSON* resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON* bytes_arr = cJSON_CreateArray();
        for (size_t i = 0; i < len_speed; ++i) {
            cJSON_AddItemToArray(bytes_arr, cJSON_CreateNumber(pkt_speed[i]));
        }
        cJSON_AddItemToObject(resp, "bytes", bytes_arr);

        char* resp_str = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, resp_str);
        
        cJSON_free(resp_str);
        cJSON_Delete(resp);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t WebServer::accessoryPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* addr_item = cJSON_GetObjectItem(json, "address");
    cJSON* str_item = cJSON_GetObjectItem(json, "straight");

    if (!addr_item || !str_item) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
        return ESP_FAIL;
    }

    int turnout_addr = addr_item->valueint;
    bool isStraight = cJSON_IsTrue(str_item);
    cJSON_Delete(json);

    // accessory turnout address format mapping
    uint16_t dcc_addr = (turnout_addr - 1) / 4;
    uint8_t pair = (turnout_addr - 1) % 4;
    uint8_t direction = isStraight ? 1 : 0;
    uint8_t aaa = (~dcc_addr >> 6) & 0x07;

    // 1. Turnout ON packet (C = 1)
    uint8_t pkt_on[3];
    pkt_on[0] = 0x80 | (dcc_addr & 0x3F);
    pkt_on[1] = 0x80 | (aaa << 4) | (1 << 3) | (pair << 1) | direction;
    pkt_on[2] = pkt_on[0] ^ pkt_on[1];
    self->m_transmitter->sendPacket(pkt_on, sizeof(pkt_on));

    // 2. Turnout OFF packet (C = 0) so the solenoid coil releases
    uint8_t pkt_off[3];
    pkt_off[0] = pkt_on[0];
    pkt_off[1] = 0x80 | (aaa << 4) | (0 << 3) | (pair << 1) | direction;
    pkt_off[2] = pkt_off[0] ^ pkt_off[1];
    self->m_transmitter->sendPacket(pkt_off, sizeof(pkt_off));

    // Return the ON packet bytes to the frontend telemetry dashboard
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON* bytes_arr = cJSON_CreateArray();
    for (size_t i = 0; i < sizeof(pkt_on); ++i) {
        cJSON_AddItemToArray(bytes_arr, cJSON_CreateNumber(pkt_on[i]));
    }
    cJSON_AddItemToObject(resp, "bytes", bytes_arr);

    char* resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

static std::atomic<bool> s_test_running{false};

struct TestTaskParams {
    int scenario_id;
    dcc::rmt::DccRmtTransmitter* transmitter;
};

static void test_scenario_task(void* pvParameters) {
    auto* params = static_cast<TestTaskParams*>(pvParameters);
    int scenario = params->scenario_id;
    auto* transmitter = params->transmitter;

    ESP_LOGI("DccTestRunner", "Starting autonomous test scenario %d...", scenario);

    if (scenario == 1) {
        // Continuous Idle Packets (Baseline) for 5 seconds
        // RMT Transmitter automatically transmits continuous DCC Idle Packets when queue is empty.
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    else if (scenario == 2) {
        // Locomotive Speed Packets (Varying Speed/Direction)
        uint8_t speed_step = 1;
        bool direction_forward = true;
        for (int i = 0; i < 50; ++i) {
            uint8_t speed_byte = 0b01000000;
            if (direction_forward) {
                speed_byte |= 0b00100000;
            }
            speed_byte |= (speed_step & 0x0F);
            
            uint8_t raw_payload[4];
            raw_payload[0] = 0x03;                       // Address 3
            raw_payload[1] = 0x3F;                       // 128 speed prefix
            raw_payload[2] = speed_byte;                 // Speed data
            raw_payload[3] = raw_payload[0] ^ raw_payload[1] ^ raw_payload[2];

            transmitter->sendPacket(raw_payload, sizeof(raw_payload));

            speed_step++;
            if (speed_step > 28) {
                speed_step = 1;
                direction_forward = !direction_forward;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    else if (scenario == 3) {
        // Accessory / Switch Commands
        for (int i = 0; i < 6; ++i) {
            uint8_t raw_payload[3];
            raw_payload[0] = 0x80;
            raw_payload[1] = (i % 2 == 0) ? 0xF8 : 0xF0;
            raw_payload[2] = raw_payload[0] ^ raw_payload[1];

            transmitter->sendPacket(raw_payload, sizeof(raw_payload));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    else if (scenario == 4) {
        // BiDi (Bidirectional) Cutout Timing Probe
        if (transmitter->isInitialized()) {
            auto original_config = transmitter->getConfig();
            
            // Reinitialize with BiDi enabled
            auto bidi_config = original_config;
            bidi_config.enable_bidi = true;
            transmitter->deinit();
            transmitter->init(bidi_config);

            // Feed standard packets
            for (int i = 0; i < 15; ++i) {
                uint8_t raw_payload[4] = { 0x03, 0x3F, 0x1A, 0x00 };
                raw_payload[3] = raw_payload[0] ^ raw_payload[1] ^ raw_payload[2];
                transmitter->sendPacket(raw_payload, sizeof(raw_payload));
                vTaskDelay(pdMS_TO_TICKS(150));
            }

            // Restore/Reinitialize with BiDi disabled
            transmitter->deinit();
            transmitter->init(original_config);

            // Feed standard packets
            for (int i = 0; i < 15; ++i) {
                uint8_t raw_payload[4] = { 0x03, 0x3F, 0x1A, 0x00 };
                raw_payload[3] = raw_payload[0] ^ raw_payload[1] ^ raw_payload[2];
                transmitter->sendPacket(raw_payload, sizeof(raw_payload));
                vTaskDelay(pdMS_TO_TICKS(150));
            }
        }
    }
    else if (scenario == 5) {
        // Single DCC Command to trigger the scope
        uint8_t raw_payload[4] = { 0x03, 0x3F, 0x1F, 0x00 }; // Address 3, Speed 30
        raw_payload[3] = raw_payload[0] ^ raw_payload[1] ^ raw_payload[2];
        transmitter->sendPacket(raw_payload, sizeof(raw_payload));
        // Add a small delay so telemetry has time to register
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI("DccTestRunner", "Autonomous test scenario %d completed successfully.", scenario);
    s_test_running = false;
    delete params;
    vTaskDelete(NULL);
}

esp_err_t WebServer::testPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* sc_item = cJSON_GetObjectItem(json, "scenario");
    if (!sc_item || !cJSON_IsNumber(sc_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid scenario ID");
        return ESP_FAIL;
    }

    int scenario = sc_item->valueint;
    cJSON_Delete(json);

    if (scenario < 1 || scenario > 5) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Scenario ID must be between 1 and 5");
        return ESP_FAIL;
    }

    bool expected = false;
    if (!s_test_running.compare_exchange_strong(expected, true)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "A test scenario is already active");
        return ESP_FAIL;
    }

    auto* params = new TestTaskParams();
    params->scenario_id = scenario;
    params->transmitter = self->m_transmitter;

    BaseType_t task_ret = xTaskCreatePinnedToCore(
        test_scenario_task,
        "dcc_test_task",
        3072,
        params,
        10, // Priority 10 is safe and won't starve high-priority system tasks
        NULL,
        1 // Pinned to Core 1 (isolated from Core 0 Wi-Fi and Web Server)
    );

    if (task_ret != pdPASS) {
        s_test_running = false;
        delete params;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to launch test scenario task");
        return ESP_FAIL;
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "message", "Test scenario successfully launched in background");
    char* resp_str = cJSON_PrintUnformatted(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

esp_err_t WebServer::decoderGetHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    // Set headers
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON object");
        return ESP_FAIL;
    }

    if (self->m_decoder == nullptr) {
        cJSON_AddBoolToObject(root, "active", false);
        cJSON_AddStringToObject(root, "status", "Not Integrated");
        cJSON_AddNumberToObject(root, "pin", -1);
        cJSON_AddNumberToObject(root, "success_count", 0);
        cJSON_AddNumberToObject(root, "error_count", 0);
        cJSON_AddNumberToObject(root, "idle_packet_count", 0);
        cJSON_AddNumberToObject(root, "cpu_load_core0", 0);
        cJSON_AddNumberToObject(root, "cpu_load_core1", 0);
        cJSON_AddArrayToObject(root, "packets");
    } else {
        bool signal_active = self->m_decoder->isSignalActive();
        cJSON_AddBoolToObject(root, "active", true);
        cJSON_AddStringToObject(root, "status", signal_active ? "Active" : "No Signal");
        cJSON_AddNumberToObject(root, "pin", self->m_decoder->getGpioNum());
        cJSON_AddNumberToObject(root, "success_count", self->m_decoder->getSuccessCount());
        cJSON_AddNumberToObject(root, "error_count", self->m_decoder->getErrorCount());
        cJSON_AddNumberToObject(root, "idle_packet_count", self->m_decoder->getIdlePacketCount());
        cJSON_AddNumberToObject(root, "cpu_load_core0", static_cast<int>(s_cpu_load_core0));
        cJSON_AddNumberToObject(root, "cpu_load_core1", static_cast<int>(s_cpu_load_core1));

        cJSON* packets_arr = cJSON_CreateArray();
        std::vector<dcc::rx::DecodedPacket> packets = self->m_decoder->getRecentPackets(10);
        
        for (const auto& packet : packets) {
            cJSON* p_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(p_obj, "timestamp", packet.timestamp);
            cJSON_AddBoolToObject(p_obj, "valid", packet.is_valid);
            cJSON_AddStringToObject(p_obj, "text", packet.human_readable.c_str());

            // Add raw hex data representation
            char hex_buf[64] = "";
            for (int i = 0; i < packet.length; ++i) {
                char byte_hex[8];
                std::sprintf(byte_hex, "0x%02X ", packet.payload[i]);
                std::strcat(hex_buf, byte_hex);
            }
            cJSON_AddStringToObject(p_obj, "hex", hex_buf);
            
            cJSON_AddItemToArray(packets_arr, p_obj);
        }
        cJSON_AddItemToObject(root, "packets", packets_arr);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to print JSON");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return res;
}

static void web_on_packet_transmitted(const uint8_t* payload, size_t length, void* arg) {
    auto* dec = static_cast<dcc::rx::DccDecoder*>(arg);
    bool is_valid = true;
    if (length >= 3) {
        uint8_t checksum = 0;
        for (size_t i = 0; i < length - 1; ++i) {
            checksum ^= payload[i];
        }
        is_valid = (checksum == payload[length - 1]);
    }
    dec->injectPacket(payload, length, is_valid);
}

esp_err_t WebServer::decoderTogglePostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* enabled_item = cJSON_GetObjectItem(json, "enabled");
    if (!enabled_item) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'enabled' parameter");
        return ESP_FAIL;
    }

    bool enabled = cJSON_IsTrue(enabled_item);
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Setting Decoder hardware state to %s", enabled ? "ENABLED" : "DISABLED");
    self->m_decoder->deinit();
    bool init_ok = false;
    if (enabled) {
        self->m_transmitter->registerCallback(nullptr, nullptr); // Unregister software loopback
        ESP_LOGI(TAG, "Initializing DCC hardware decoder. Configured m_decoder_pin = %d", self->m_decoder_pin);
        init_ok = self->m_decoder->init(self->m_decoder_pin);
        if (!init_ok) {
            ESP_LOGE(TAG, "Failed to initialize hardware DCC decoder on pin %d. Reverting to loopback.", self->m_decoder_pin);
            // Revert to software loopback
            self->m_decoder->init(-1);
            self->m_transmitter->registerCallback(web_on_packet_transmitted, self->m_decoder);
            enabled = false;
        }
    } else {
        init_ok = self->m_decoder->init(-1);
        self->m_transmitter->registerCallback(web_on_packet_transmitted, self->m_decoder); // Re-register software loopback
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddBoolToObject(resp, "enabled", enabled);
    cJSON_AddNumberToObject(resp, "pin", self->m_decoder->getGpioNum());
    char* resp_str = cJSON_PrintUnformatted(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

esp_err_t WebServer::triggerPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    std::string body = read_post_body(req);
    
    cJSON* json = cJSON_Parse(body.c_str());
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* enabled_item = cJSON_GetObjectItem(json, "enabled");
    if (!enabled_item) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'enabled' parameter");
        return ESP_FAIL;
    }

    bool enabled = cJSON_IsTrue(enabled_item);
    cJSON_Delete(json);

    self->m_transmitter->setScopeTriggerEnabled(enabled);
    ESP_LOGI(TAG, "GPIO4 Scope Trigger set to %s", enabled ? "ENABLED" : "DISABLED");

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddBoolToObject(resp, "enabled", enabled);
    char* resp_str = cJSON_PrintUnformatted(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    cJSON_free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

esp_err_t WebServer::withrottleGetHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServer*>(req->user_ctx);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    if (self->m_withrottle_server == nullptr) {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    
    std::string clients_json = self->m_withrottle_server->getClientsJson();
    return httpd_resp_sendstr(req, clients_json.c_str());
}

} // namespace web
} // namespace dcc

#endif // CONFIG_BUILD_TEST_GENERATOR

