/**
 * @file TestGenerator.hpp
 * @brief Standalone WiThrottle Client and Autonomous Test Orchestrator.
 * @author Antigravity Refactoring
 * @date 2026-05-27
 * 
 * Actively connects to the WiThrottle Server (Task 1) over TCP,
 * simulates locomotive control and accessory turnout commands,
 * and executes autonomous end-to-end verification loops.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <sstream>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace dcc {
namespace wt {

/**
 * @brief Representation of autonomous test results.
 */
struct TestResults {
    int total_tests = 0;          ///< Total tests executed in scenario
    int success_count = 0;        ///< Successful end-to-end verifications
    int failure_count = 0;        ///< Mismatches or timeouts
    int packets_sent = 0;         ///< Number of WiThrottle commands sent
    int packets_lost = 0;         ///< Commands not registered by decoder
    float avg_latency_ms = 0.0f;  ///< Average latency from socket write to DCC decode
    std::string last_log;         ///< Detailed log of the last test execution
    std::string test_name;        ///< Name of active/last scenario
    bool is_running = false;      ///< True if test scenario is active
};

/**
 * @brief Standalone WiThrottle Client & Automated Test Agent.
 */
class TestGenerator {
public:
    TestGenerator();
    ~TestGenerator();

    // Prevent copying
    TestGenerator(const TestGenerator&) = delete;
    TestGenerator& operator=(const TestGenerator&) = delete;

    /**
     * @brief Initialize Wi-Fi connection and client settings.
     */
    bool init(const std::string& server_ip = "192.168.4.1", int server_port = 12090);

    /**
     * @brief Start background tasks (TCP Client & test runners).
     */
    void start();

    /**
     * @brief Stop all tasks and close client socket.
     */
    void stop();

    /**
     * @brief Configure the WiThrottle target Server IP.
     */
    void setServerIp(const std::string& ip);
    std::string getServerIp() const { return m_server_ip; }

    /**
     * @brief Send a raw WiThrottle network command.
     */
    bool sendRawCommand(const std::string& cmd);

    // Throttle client helper APIs
    bool acquireLocomotive(int address, bool is_long);
    bool releaseLocomotive(int address, bool is_long);
    bool setSpeed(int address, bool is_long, int speed);
    bool setDirection(int address, bool is_long, bool forward);
    bool setFunction(int address, bool is_long, int func_num, bool state);
    bool setTurnout(int turnout_addr, bool straight);

    /**
     * @brief Launch a specific autonomous testing scenario in the background.
     * @param scenario_id Scenario ID (1 = Speed/Dir Sweep, 2 = Turnout Storm, 3 = Multi-Loco Consist)
     * @return true if successfully launched, false if another test is active.
     */
    bool launchTestScenario(int scenario_id);

    /**
     * @brief Query active test metrics as a JSON string.
     */
    std::string getResultsJson();

private:
    static void clientTaskWrapper(void* arg);
    void runClientTask();

    static void testTaskWrapper(void* arg);
    void runTestScenario(int scenario_id);

    // End-to-end HTTP API Verification helper
    bool verifyDccDecode(const std::string& expected_substring, uint32_t timeout_ms, float& out_latency_ms);

    std::string m_server_ip;
    int m_server_port;
    
    std::atomic<bool> m_running;
    std::atomic<int> m_socket_fd;
    std::atomic<bool> m_connected;

    TaskHandle_t m_client_task_handle;
    TaskHandle_t m_test_task_handle;
    
    std::mutex m_socket_mutex;

    // Test Orchestrator state variables
    TestResults m_results;
    std::mutex m_results_mutex;
    std::stringstream m_log_stream;
};

} // namespace wt
} // namespace dcc
