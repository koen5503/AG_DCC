/**
 * @file WiThrottleServer.cpp
 * @brief Lightweight, asynchronous WiThrottle TCP Server for DCC Control.
 * @author Antigravity Refactoring
 * @date 2026-05-27
 * 
 * Sockets are managed via BSD select() within a single task to prevent timing jitter.
 * Handles locomotive speed, direction, function toggles, and turnout accessory commands.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "WiThrottleServer.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include <algorithm>
#include <sstream>

static const char* TAG = "WiThrottleServer";

static bool safe_stoi(const std::string& str, int& out_val) {
    if (str.empty()) return false;
    char* endptr = nullptr;
    long val = std::strtol(str.c_str(), &endptr, 10);
    if (endptr == str.c_str()) {
        return false;
    }
    out_val = static_cast<int>(val);
    return true;
}

namespace dcc {
namespace wt {

WiThrottleServer::WiThrottleServer(dcc::rmt::DccRmtTransmitter* transmitter)
    : m_transmitter(transmitter),
      m_port(12090),
      m_server_fd(-1),
      m_running(false),
      m_task_handle(nullptr) {
}

WiThrottleServer::~WiThrottleServer() {
    stop();
}

bool WiThrottleServer::start(int port) {
    if (m_running) {
        ESP_LOGW(TAG, "Server already running.");
        return true;
    }

    m_port = port;
    m_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        serverTaskWrapper,
        "wi_throttle_server",
        4096,
        this,
        8, // Priority 8 is perfectly safe and won't starve high-priority RMT task (16)
        &m_task_handle,
        0 // Pinned to Core 0 (Wi-Fi and lwIP core)
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS server task.");
        m_running = false;
        return false;
    }

    ESP_LOGI(TAG, "WiThrottle server task spawned successfully on port %d.", m_port);
    return true;
}

void WiThrottleServer::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;
    ESP_LOGI(TAG, "Stopping WiThrottle Server...");

    // Close server socket to break select loop
    if (m_server_fd != -1) {
        close(m_server_fd);
        m_server_fd = -1;
    }

    // Close client sockets under lock
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        for (auto& client : m_clients) {
            if (client.socket_fd != -1) {
                close(client.socket_fd);
            }
        }
        m_clients.clear();
    }

    if (m_task_handle != nullptr) {
        // Wait briefly for task to finish cleanup
        vTaskDelay(pdMS_TO_TICKS(100));
        m_task_handle = nullptr;
    }

    ESP_LOGI(TAG, "WiThrottle Server stopped successfully.");
}

void WiThrottleServer::serverTaskWrapper(void* arg) {
    static_cast<WiThrottleServer*>(arg)->runServerTask();
}

void WiThrottleServer::runServerTask() {
    ESP_LOGI(TAG, "Starting TCP socket binding on Port %d...", m_port);

    m_server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (m_server_fd < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        m_running = false;
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Make server socket non-blocking
    fcntl(m_server_fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(m_port);

    if (bind(m_server_fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(m_server_fd);
        m_server_fd = -1;
        m_running = false;
        vTaskDelete(NULL);
        return;
    }

    if (listen(m_server_fd, 4) < 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(m_server_fd);
        m_server_fd = -1;
        m_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "WiThrottle TCP Server successfully bound and listening on Port %d.", m_port);

    // Keep track of partial received data per client to handle stream fragmentation
    std::vector<std::string> client_buffers;
    client_buffers.resize(FD_SETSIZE, "");

    char rx_buffer[512];

    while (m_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_server_fd, &readfds);
        int max_fd = m_server_fd;

        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            for (const auto& client : m_clients) {
                if (client.socket_fd != -1) {
                    FD_SET(client.socket_fd, &readfds);
                    if (client.socket_fd > max_fd) {
                        max_fd = client.socket_fd;
                    }
                }
            }
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500 ms select timeout

        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) {
            ESP_LOGE(TAG, "Select error occurred: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (!m_running) break;

        // 1. Accept new incoming connection
        if (FD_ISSET(m_server_fd, &readfds)) {
            struct sockaddr_in source_addr;
            socklen_t addr_len = sizeof(source_addr);
            int new_socket = accept(m_server_fd, (struct sockaddr *)&source_addr, &addr_len);
            
            if (new_socket >= 0) {
                // Configure TCP Keep-Alive and No-Delay (disable Nagle's algorithm)
                int keepalive = 1;
                int keepidle = 5;      // 5 seconds of idle before starting probes
                int keepintvl = 2;     // 2 seconds between probes
                int keepcnt = 3;       // 3 failed probes (closed after 11 seconds total)
                int nodelay = 1;       // Instant transmission
                setsockopt(new_socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
                setsockopt(new_socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
                setsockopt(new_socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
                setsockopt(new_socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
                setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                // Limit maximum clients to 4 to conserve memory
                bool allowed = false;
                std::string ip_str = inet_ntoa(source_addr.sin_addr);

                {
                    std::lock_guard<std::mutex> lock(m_clients_mutex);
                    if (m_clients.size() < 4) {
                        ClientSession session;
                        session.socket_fd = new_socket;
                        session.ip_address = ip_str;
                        session.device_name = "WiThrottle Client";
                        session.device_id = "Unknown";
                        session.has_loco = false;
                        
                        m_clients.push_back(session);
                        allowed = true;
                    }
                }

                if (allowed) {
                    ESP_LOGI(TAG, "New WiThrottle client connected from IP: %s (Socket %d)", ip_str.c_str(), new_socket);
                    
                    // Send initial handshake banner
                    // VN2.0: Version 2.0
                    // *0: Heartbeat disabled (clients don't need to spam heartbeat)
                    // HTESP32-DCC: Server Type
                    // PPA1: Track Power ON
                    const char* banner = "VN2.0\n*0\nHTESP32-DCC\nHtESP32-DCC Command Station\nRL0\nPPA1\n";
                    send(new_socket, banner, std::strlen(banner), 0);
                    
                    client_buffers[new_socket] = "";
                } else {
                    ESP_LOGW(TAG, "Max client capacity reached. Rejecting client from IP: %s", ip_str.c_str());
                    close(new_socket);
                }
            }
        }

        // 2. Poll data from existing client sockets
        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            auto it = m_clients.begin();
            while (it != m_clients.end()) {
                int fd = it->socket_fd;
                if (fd != -1 && FD_ISSET(fd, &readfds)) {
                    int len = recv(fd, rx_buffer, sizeof(rx_buffer) - 1, 0);
                    
                    if (len <= 0) {
                        // Connection closed or error
                        ESP_LOGI(TAG, "Client disconnected from IP: %s (Socket %d)", it->ip_address.c_str(), fd);
                        close(fd);
                        client_buffers[fd] = "";
                        it = m_clients.erase(it);
                        continue;
                    } else {
                        rx_buffer[len] = '\0';
                        client_buffers[fd] += rx_buffer;
                        
                        // Parse stream lines split by newline
                        std::string& buf = client_buffers[fd];
                        size_t pos;
                        while ((pos = buf.find('\n')) != std::string::npos) {
                            std::string line = buf.substr(0, pos);
                            buf.erase(0, pos + 1);
                            
                            // Remove trailing carriage returns
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                            }
                            
                            if (!line.empty()) {
                                handleClientData(*it, line);
                            }
                        }
                    }
                }
                ++it;
            }
        }
    }

    ESP_LOGI(TAG, "Cleaning up server resources...");
    if (m_server_fd != -1) {
        close(m_server_fd);
        m_server_fd = -1;
    }
    vTaskDelete(NULL);
}

void WiThrottleServer::handleClientData(ClientSession& session, const std::string& data) {
    ESP_LOGD(TAG, "[RX Socket %d]: %s", session.socket_fd, data.c_str());
    parseCommandLine(session, data);
}

void WiThrottleServer::parseCommandLine(ClientSession& session, const std::string& line) {
    if (line.empty()) return;

    char cmd_prefix = line[0];
    
    // Set device ID
    if (line.rfind("HU", 0) == 0) {
        session.device_id = line.substr(2);
        ESP_LOGI(TAG, "Client registered Device ID: %s", session.device_id.c_str());
        return;
    }
    
    // Set device name
    if (cmd_prefix == 'N') {
        session.device_name = line.substr(1);
        ESP_LOGI(TAG, "Client registered Throttle Name: %s", session.device_name.c_str());
        return;
    }

    // Heartbeat check (just send a heartbeat back if they request)
    if (cmd_prefix == '*') {
        // Echo back a heartbeat to keep client socket happy if necessary
        const char* hb = "*\n";
        send(session.socket_fd, hb, std::strlen(hb), 0);
        return;
    }

    // Quit command
    if (cmd_prefix == 'Q') {
        ESP_LOGI(TAG, "Client issued quit command: %s", session.device_name.c_str());
        return;
    }

    // Multi-throttle commands
    if (cmd_prefix == 'M') {
        if (line.length() < 3) return;
        char throttle_key = line[1];
        char action_key = line[2];
        
        // Find separator "<;>"
        size_t sep_pos = line.find("<;>");
        if (sep_pos == std::string::npos) return;
        
        std::string loco_id = line.substr(3, sep_pos - 3);
        std::string additional = line.substr(sep_pos + 3);

        if (action_key == '+') {
            // Acquire locomotive
            acquireLocomotive(session, throttle_key, loco_id);
        } 
        else if (action_key == '-') {
            // Release locomotive
            releaseLocomotive(session, throttle_key, loco_id);
        } 
        else if (action_key == 'A') {
            // Execute locomotive action (speed, dir, functions)
            executeLocoAction(session, throttle_key, loco_id, additional);
        }
        return;
    }

    // Turnout Commands
    if (line.rfind("PTA", 0) == 0) {
        executeTurnoutAction(session, line.substr(3));
        return;
    }
}

void WiThrottleServer::acquireLocomotive(ClientSession& session, char throttle_key, const std::string& loco_id) {
    if (loco_id.empty()) return;
    
    char addr_type = loco_id[0]; // 'L' (long) or 'S' (short)
    int address = 0;
    if (!safe_stoi(loco_id.substr(1), address)) {
        ESP_LOGE(TAG, "Failed to parse loco address: %s", loco_id.c_str());
        return;
    }

    session.has_loco = true;
    session.loco.address = address;
    session.loco.is_long = (addr_type == 'L');
    session.loco.speed = 0;
    session.loco.direction = true; // default forward
    std::memset(session.loco.f_states, 0, sizeof(session.loco.f_states));

    ESP_LOGI(TAG, "Client [%s] acquired Loco address: %d (%s)", 
             session.device_name.c_str(), address, session.loco.is_long ? "Long" : "Short");

    // Send locomotive status blocks back to the client to enable control sliders
    sendLocoStatus(session, throttle_key);
}

void WiThrottleServer::releaseLocomotive(ClientSession& session, char throttle_key, const std::string& loco_id) {
    ESP_LOGI(TAG, "Client [%s] released Loco address: %d", session.device_name.c_str(), session.loco.address);
    
    // Send confirmation back
    std::string response = "M" + std::string(1, throttle_key) + "-" + loco_id + "<;>\n";
    send(session.socket_fd, response.c_str(), response.length(), 0);

    session.has_loco = false;
    session.loco.address = 0;
}

void WiThrottleServer::executeLocoAction(ClientSession& session, char throttle_key, const std::string& loco_id, const std::string& action) {
    if (!session.has_loco || action.empty()) return;

    char action_prefix = action[0];

    // 1. Velocity (Speed) Command
    if (action_prefix == 'V') {
        int wt_speed = 0;
        if (!safe_stoi(action.substr(1), wt_speed)) {
            return;
        }

        // Clip speed
        wt_speed = std::max(0, std::min(126, wt_speed));
        session.loco.speed = wt_speed;

        ESP_LOGI(TAG, "Client [%s] set Loco %d Speed to: %d", session.device_name.c_str(), session.loco.address, wt_speed);
        
        // Dispatch Speed Packet
        dispatchDccSpeed(session.loco);
        
        // Echo speed back to client to confirm slider position
        std::string echo = "M" + std::string(1, throttle_key) + "A" + loco_id + "<;>V" + std::to_string(wt_speed) + "\n";
        send(session.socket_fd, echo.c_str(), echo.length(), 0);
    }
    
    // 2. Idle Command
    else if (action_prefix == 'I') {
        session.loco.speed = 0;
        ESP_LOGI(TAG, "Client [%s] set Loco %d to IDLE", session.device_name.c_str(), session.loco.address);
        
        dispatchDccSpeed(session.loco);

        std::string echo = "M" + std::string(1, throttle_key) + "A" + loco_id + "<;>V0\n";
        send(session.socket_fd, echo.c_str(), echo.length(), 0);
    }

    // 3. Emergency Stop
    else if (action_prefix == 'X') {
        session.loco.speed = -1; // -1 represents Emergency Stop in our state
        ESP_LOGI(TAG, "Client [%s] issued EMERGENCY STOP on Loco %d", session.device_name.c_str(), session.loco.address);
        
        dispatchDccSpeed(session.loco);

        std::string echo = "M" + std::string(1, throttle_key) + "A" + loco_id + "<;>V-1\n";
        send(session.socket_fd, echo.c_str(), echo.length(), 0);
    }

    // 4. Direction Command
    else if (action_prefix == 'R') {
        int dir = 0;
        if (!safe_stoi(action.substr(1), dir)) {
            return;
        }

        session.loco.direction = (dir == 1);
        ESP_LOGI(TAG, "Client [%s] set Loco %d Direction to: %s", 
                 session.device_name.c_str(), session.loco.address, session.loco.direction ? "Forward" : "Reverse");

        dispatchDccSpeed(session.loco);

        std::string echo = "M" + std::string(1, throttle_key) + "A" + loco_id + "<;>R" + std::to_string(dir) + "\n";
        send(session.socket_fd, echo.c_str(), echo.length(), 0);
    }

    // 5. Function Command (Toggle)
    else if (action_prefix == 'F' || action_prefix == 'f') {
        if (action.length() < 3) return;
        int state = action[1] - '0'; // 0 or 1
        int func_num = 0;
        if (!safe_stoi(action.substr(2), func_num)) {
            return;
        }

        if (func_num >= 0 && func_num <= 28) {
            session.loco.f_states[func_num] = (state == 1);
            ESP_LOGI(TAG, "Client [%s] set Loco %d Function F%d to: %s", 
                     session.device_name.c_str(), session.loco.address, func_num, state == 1 ? "ON" : "OFF");
            
            // Map function to NMRA groups
            int group = 1;
            if (func_num >= 0 && func_num <= 4) group = 1;
            else if (func_num >= 5 && func_num <= 8) group = 2;
            else if (func_num >= 9 && func_num <= 12) group = 3;
            else if (func_num >= 13 && func_num <= 20) group = 4;
            else if (func_num >= 21 && func_num <= 28) group = 5;

            dispatchDccFunctions(session.loco, group);

            // Echo function state back
            std::string echo = "M" + std::string(1, throttle_key) + "A" + loco_id + "<;>F" + std::to_string(state) + std::to_string(func_num) + "\n";
            send(session.socket_fd, echo.c_str(), echo.length(), 0);
        }
    }
}

void WiThrottleServer::executeTurnoutAction(ClientSession& session, const std::string& turnout_cmd) {
    if (turnout_cmd.empty()) return;

    char action = turnout_cmd[0]; // 'C' (Closed/Straight), 'T' (Thrown/Curved), '2' (Toggle)
    
    // Find first digit in the rest of the string to parse turnout address
    int turnout_addr = 0;
    size_t i = 1;
    while (i < turnout_cmd.length() && !std::isdigit(turnout_cmd[i])) {
        i++;
    }
    if (i < turnout_cmd.length()) {
        if (!safe_stoi(turnout_cmd.substr(i), turnout_addr)) {
            return;
        }
    }

    if (turnout_addr <= 0) return;

    bool isStraight = (action == 'C' || action == '2'); // Assume straight/closed by default or on toggle

    ESP_LOGI(TAG, "Client [%s] triggered Turnout %d to: %s", 
             session.device_name.c_str(), turnout_addr, isStraight ? "CLOSED/STRAIGHT" : "THROWN/CURVED");

    // Formulate turnout address mapping according to NMRA S-9.2.1
    uint16_t dcc_addr = (turnout_addr - 1) / 4;
    uint8_t pair = (turnout_addr - 1) % 4;
    uint8_t direction = isStraight ? 1 : 0;
    uint8_t aaa = (~dcc_addr >> 6) & 0x07;

    // 1. Solenoid ON Packet (C = 1)
    uint8_t pkt_on[3];
    pkt_on[0] = 0x80 | (dcc_addr & 0x3F);
    pkt_on[1] = 0x80 | (aaa << 4) | (1 << 3) | (pair << 1) | direction;
    pkt_on[2] = pkt_on[0] ^ pkt_on[1];
    m_transmitter->sendPacket(pkt_on, sizeof(pkt_on));

    // 2. Solenoid OFF Packet (C = 0) sent after a small 50ms delay to de-energize the coil
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t pkt_off[3];
    pkt_off[0] = pkt_on[0];
    pkt_off[1] = 0x80 | (aaa << 4) | (0 << 3) | (pair << 1) | direction;
    pkt_off[2] = pkt_off[0] ^ pkt_off[1];
    m_transmitter->sendPacket(pkt_off, sizeof(pkt_off));

    // Send status notification back to WiThrottle client to update turnout icon state
    std::string notify = "PTA" + std::string(1, isStraight ? 'C' : 'T') + std::to_string(turnout_addr) + "\n";
    send(session.socket_fd, notify.c_str(), notify.length(), 0);
}

void WiThrottleServer::sendLocoStatus(ClientSession& session, char throttle_key) {
    std::string prefix = "M" + std::string(1, throttle_key) + (session.loco.is_long ? "L" : "S") + std::to_string(session.loco.address) + "<;>";
    
    // 1. Confirm locomotive acquisition
    std::string response = "M" + std::string(1, throttle_key) + "+" + (session.loco.is_long ? "L" : "S") + std::to_string(session.loco.address) + "<;>\n";
    send(session.socket_fd, response.c_str(), response.length(), 0);

    // 2. Send locomotive function labels (required for Engine Driver function keys layout)
    std::string f_labels = "M" + std::string(1, throttle_key) + "L" + (session.loco.is_long ? "L" : "S") + std::to_string(session.loco.address) + "<;>" +
                           "]\\[F0]\\[F1]\\[F2]\\[F3]\\[F4]\\[F5]\\[F6]\\[F7]\\[F8]\\[F9]\\[F10]\\[F11]\\[F12]\\[F13]\\[F14]\\[F15]\\[F16]\\[F17]\\[F18]\\[F19]\\[F20]\\[F21]\\[F22]\\[F23]\\[F24]\\[F25]\\[F26]\\[F27]\\[F28]\\\n";
    send(session.socket_fd, f_labels.c_str(), f_labels.length(), 0);

    // 3. Send default speed, direction, and speed step mode (s1 = 128 steps)
    std::string states = prefix + "V0\n" +
                         prefix + "R1\n" +
                         prefix + "s1\n";
    send(session.socket_fd, states.c_str(), states.length(), 0);
}

void WiThrottleServer::dispatchDccSpeed(const LocomotiveState& loco) {
    uint8_t speed_byte = 0;
    if (loco.speed == -1) {
        // DCC Emergency Stop: speed byte is 1!
        speed_byte = (loco.direction ? 0x80 : 0x00) | 1;
    } else {
        // Translate WiThrottle (0-126) to DCC 128-step speed (0-126). 
        // 0 is stop, 1 is e-stop (handled above), 2-127 represent speed steps 1-126.
        int dcc_speed = (loco.speed == 0) ? 0 : (loco.speed + 1);
        speed_byte = (loco.direction ? 0x80 : 0x00) | dcc_speed;
    }

    uint8_t pkt[8];
    size_t len = 0;

    if (!loco.is_long && loco.address < 128) {
        pkt[0] = loco.address;
        pkt[1] = 0x3F; // 128 speed step control instruction prefix
        pkt[2] = speed_byte;
        pkt[3] = pkt[0] ^ pkt[1] ^ pkt[2];
        len = 4;
    } else {
        pkt[0] = (loco.address >> 8) | 0xC0; // Long address prefix
        pkt[1] = loco.address & 0xFF;
        pkt[2] = 0x3F;
        pkt[3] = speed_byte;
        pkt[4] = pkt[0] ^ pkt[1] ^ pkt[2] ^ pkt[3];
        len = 5;
    }

    m_transmitter->sendPacket(pkt, len);
}

void WiThrottleServer::dispatchDccFunctions(const LocomotiveState& loco, int func_group) {
    uint8_t instruction = 0;
    uint8_t pkt[8];
    size_t len = 0;

    // Group 1: F0, F4, F3, F2, F1
    // Format: 0x80 | (F0 << 4) | (F4 << 3) | (F3 << 2) | (F2 << 1) | F1
    if (func_group == 1) {
        instruction = 0x80 |
                      (loco.f_states[0] ? 0x10 : 0) |
                      (loco.f_states[4] ? 0x08 : 0) |
                      (loco.f_states[3] ? 0x04 : 0) |
                      (loco.f_states[2] ? 0x02 : 0) |
                      (loco.f_states[1] ? 0x01 : 0);
    }
    // Group 2: F8, F7, F6, F5
    // Format: 0xB0 | (F8 << 3) | (F7 << 2) | (F6 << 1) | F5
    else if (func_group == 2) {
        instruction = 0xB0 |
                      (loco.f_states[8] ? 0x08 : 0) |
                      (loco.f_states[7] ? 0x04 : 0) |
                      (loco.f_states[6] ? 0x02 : 0) |
                      (loco.f_states[5] ? 0x01 : 0);
    }
    // Group 3: F12, F11, F10, F9
    // Format: 0xA0 | (F12 << 3) | (F11 << 2) | (F10 << 1) | F9
    else if (func_group == 3) {
        instruction = 0xA0 |
                      (loco.f_states[12] ? 0x08 : 0) |
                      (loco.f_states[11] ? 0x04 : 0) |
                      (loco.f_states[10] ? 0x02 : 0) |
                      (loco.f_states[9]  ? 0x01 : 0);
    }
    // Group 4 (F13 - F20): Instruction 0xDE followed by data byte F20-F13
    else if (func_group == 4) {
        uint8_t data_byte = 0;
        for (int i = 13; i <= 20; ++i) {
            if (loco.f_states[i]) {
                data_byte |= (1 << (i - 13));
            }
        }
        
        if (!loco.is_long && loco.address < 128) {
            pkt[0] = loco.address;
            pkt[1] = 0xDE; // Function expansion instruction
            pkt[2] = data_byte;
            pkt[3] = pkt[0] ^ pkt[1] ^ pkt[2];
            m_transmitter->sendPacket(pkt, 4);
        } else {
            pkt[0] = (loco.address >> 8) | 0xC0;
            pkt[1] = loco.address & 0xFF;
            pkt[2] = 0xDE;
            pkt[3] = data_byte;
            pkt[4] = pkt[0] ^ pkt[1] ^ pkt[2] ^ pkt[3];
            m_transmitter->sendPacket(pkt, 5);
        }
        return;
    }
    // Group 5 (F21 - F28): Instruction 0xDF followed by data byte F28-F21
    else if (func_group == 5) {
        uint8_t data_byte = 0;
        for (int i = 21; i <= 28; ++i) {
            if (loco.f_states[i]) {
                data_byte |= (1 << (i - 21));
            }
        }
        
        if (!loco.is_long && loco.address < 128) {
            pkt[0] = loco.address;
            pkt[1] = 0xDF; // Function expansion instruction
            pkt[2] = data_byte;
            pkt[3] = pkt[0] ^ pkt[1] ^ pkt[2];
            m_transmitter->sendPacket(pkt, 4);
        } else {
            pkt[0] = (loco.address >> 8) | 0xC0;
            pkt[1] = loco.address & 0xFF;
            pkt[2] = 0xDF;
            pkt[3] = data_byte;
            pkt[4] = pkt[0] ^ pkt[1] ^ pkt[2] ^ pkt[3];
            m_transmitter->sendPacket(pkt, 5);
        }
        return;
    }

    // Dispatch Standard Group 1-3 packet
    if (!loco.is_long && loco.address < 128) {
        pkt[0] = loco.address;
        pkt[1] = instruction;
        pkt[2] = pkt[0] ^ pkt[1];
        len = 3;
    } else {
        pkt[0] = (loco.address >> 8) | 0xC0;
        pkt[1] = loco.address & 0xFF;
        pkt[2] = instruction;
        pkt[3] = pkt[0] ^ pkt[1] ^ pkt[2];
        len = 4;
    }

    m_transmitter->sendPacket(pkt, len);
}

std::string WiThrottleServer::getClientsJson() {
    cJSON* array = cJSON_CreateArray();
    
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        for (const auto& client : m_clients) {
            cJSON* obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "socket", client.socket_fd);
            cJSON_AddStringToObject(obj, "ip", client.ip_address.c_str());
            cJSON_AddStringToObject(obj, "name", client.device_name.c_str());
            cJSON_AddStringToObject(obj, "device_id", client.device_id.c_str());
            
            cJSON_AddBoolToObject(obj, "has_loco", client.has_loco);
            if (client.has_loco) {
                cJSON* loco_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(loco_obj, "address", client.loco.address);
                cJSON_AddBoolToObject(loco_obj, "is_long", client.loco.is_long);
                cJSON_AddNumberToObject(loco_obj, "speed", client.loco.speed);
                cJSON_AddBoolToObject(loco_obj, "direction", client.loco.direction);
                
                // Add first 9 function states for UI display
                cJSON* funcs = cJSON_CreateArray();
                for (int i = 0; i < 9; ++i) {
                    cJSON_AddItemToArray(funcs, cJSON_CreateBool(client.loco.f_states[i]));
                }
                cJSON_AddItemToObject(loco_obj, "functions", funcs);
                
                cJSON_AddItemToObject(obj, "locomotive", loco_obj);
            }
            cJSON_AddItemToArray(array, obj);
        }
    }

    char* rendered = cJSON_PrintUnformatted(array);
    std::string json_str = rendered ? rendered : "[]";
    
    cJSON_free(rendered);
    cJSON_Delete(array);
    
    return json_str;
}

} // namespace wt
} // namespace dcc
