/**
 * @file WiThrottleServer.hpp
 * @brief Lightweight, asynchronous WiThrottle TCP Server for DCC Control.
 * @author Antigravity Refactoring
 * @date 2026-05-27
 * 
 * Implements a BSD sockets TCP server running on port 12090.
 * Parses the WiThrottle network protocol commands (throttle speed, direction, 
 * functions F0-F28, turnouts) and translates them into DCC packets in real-time.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "DccRmtTransmitter.hpp"

namespace dcc {
namespace wt {

/**
 * @brief Representation of a locomotive state managed by a throttle.
 */
struct LocomotiveState {
    int address = 0;              ///< DCC address
    bool is_long = false;         ///< True if long address, false if short
    int speed = 0;                ///< Speed step (0-126)
    bool direction = true;        ///< True if Forward, false if Reverse
    bool f_states[29] = {false};  ///< Function states F0 - F28
};

/**
 * @brief WiThrottle Client Session tracking state.
 */
struct ClientSession {
    int socket_fd = -1;           ///< Socket file descriptor
    std::string ip_address;       ///< Client IP Address
    std::string device_name;      ///< Custom client name (from N command)
    std::string device_id;        ///< Client device hardware ID (from HU command)
    bool has_loco = false;        ///< True if locomotive is acquired
    LocomotiveState loco;         ///< Currently selected locomotive state
};

/**
 * @brief Thread-safe WiThrottle TCP Server.
 */
class WiThrottleServer {
public:
    /**
     * @brief Construct a WiThrottleServer instance.
     * @param transmitter Pointer to the active DCC RMT Transmitter.
     */
    WiThrottleServer(dcc::rmt::DccRmtTransmitter* transmitter);

    /**
     * @brief Destroy the WiThrottleServer instance (calls stop()).
     */
    ~WiThrottleServer();

    // Prevent copying
    WiThrottleServer(const WiThrottleServer&) = delete;
    WiThrottleServer& operator=(const WiThrottleServer&) = delete;

    /**
     * @brief Start the TCP server on the specified port.
     * @param port TCP port to bind to (defaults to 12090).
     * @return true if successfully started, false otherwise.
     */
    bool start(int port = 12090);

    /**
     * @brief Stop the server and close all client connections.
     */
    void stop();

    /**
     * @brief Check if the server is currently running.
     */
    bool isRunning() const { return m_running; }

    /**
     * @brief Query active WiThrottle clients and return status as a JSON string.
     */
    std::string getClientsJson();

private:
    static void serverTaskWrapper(void* arg);
    void runServerTask();

    // Network protocol parsing helpers
    void handleClientData(ClientSession& session, const std::string& data);
    void parseCommandLine(ClientSession& session, const std::string& line);
    
    // Command translation and dispatchers
    void acquireLocomotive(ClientSession& session, char throttle_key, const std::string& loco_id);
    void releaseLocomotive(ClientSession& session, char throttle_key, const std::string& loco_id);
    void executeLocoAction(ClientSession& session, char throttle_key, const std::string& loco_id, const std::string& action);
    void executeTurnoutAction(ClientSession& session, const std::string& turnout_cmd);
    
    void sendLocoStatus(ClientSession& session, char throttle_key);
    void dispatchDccSpeed(const LocomotiveState& loco);
    void dispatchDccFunctions(const LocomotiveState& loco, int func_group);

    dcc::rmt::DccRmtTransmitter* m_transmitter;
    
    int m_port;
    int m_server_fd;
    std::atomic<bool> m_running;
    TaskHandle_t m_task_handle;
    
    std::vector<ClientSession> m_clients;
    std::mutex m_clients_mutex;
};

} // namespace wt
} // namespace dcc
