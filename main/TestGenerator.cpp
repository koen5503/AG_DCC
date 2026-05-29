/**
 * @file TestGenerator.cpp
 * @brief Standalone WiThrottle Client and Autonomous Test Orchestrator.
 * @author Antigravity Refactoring
 * @date 2026-05-27
 * 
 * Implements TCP WiThrottle socket connection, automated testing loops, 
 * and HTTP REST API packet decoding verification.
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 */

#include "TestGenerator.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <cstring>
#include <nvs_flash.h>
#include <nvs.h>
#include <sstream>
#include <iomanip>

static const char* TAG = "TestGenerator";

// HTTP Event Handler to gather response body in a std::string
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            std::string* response = static_cast<std::string*>(evt->user_data);
            if (response && evt->data && evt->data_len > 0) {
                response->append((char*)evt->data, evt->data_len);
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

namespace dcc {
namespace wt {

TestGenerator::TestGenerator()
    : m_server_ip("192.168.4.1"),
      m_server_port(12090),
      m_running(false),
      m_socket_fd(-1),
      m_connected(false),
      m_client_task_handle(nullptr),
      m_test_task_handle(nullptr) {
    m_results.test_name = "None";
    m_results.last_log = "No test run yet.";
}

TestGenerator::~TestGenerator() {
    stop();
}

bool TestGenerator::init(const std::string& server_ip, int server_port) {
    m_server_port = server_port;
    
    // Load Target IP from NVS
    nvs_handle_t nvs_handle;
    char ip_buf[64] = {0};
    size_t ip_len = sizeof(ip_buf);
    bool loaded = false;
    
    if (nvs_open("test_gen", NVS_READONLY, &nvs_handle) == ESP_OK) {
        if (nvs_get_str(nvs_handle, "target_ip", ip_buf, &ip_len) == ESP_OK) {
            m_server_ip = ip_buf;
            loaded = true;
            ESP_LOGI(TAG, "Loaded persisted Target Server IP from NVS: %s", m_server_ip.c_str());
        }
        nvs_close(nvs_handle);
    }
    
    if (!loaded) {
        m_server_ip = server_ip;
        ESP_LOGI(TAG, "No Target IP in NVS. Using default target: %s:%d", m_server_ip.c_str(), m_server_port);
    } else {
        ESP_LOGI(TAG, "Initialized TestGenerator target to %s:%d", m_server_ip.c_str(), m_server_port);
    }
    return true;
}

void TestGenerator::start() {
    if (m_running) return;
    
    m_running = true;
    
    BaseType_t ret = xTaskCreatePinnedToCore(
        clientTaskWrapper,
        "wt_client_task",
        4096,
        this,
        8,
        &m_client_task_handle,
        0 // Pinned to Core 0 (Wi-Fi and lwIP core)
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Client Task.");
        m_running = false;
    }
}

void TestGenerator::stop() {
    if (!m_running) return;
    
    m_running = false;
    
    // Close socket to force-close any blocking operations
    {
        std::lock_guard<std::mutex> lock(m_socket_mutex);
        if (m_socket_fd != -1) {
            close(m_socket_fd);
            m_socket_fd = -1;
        }
        m_connected = false;
    }

    if (m_client_task_handle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100));
        m_client_task_handle = nullptr;
    }

    if (m_test_task_handle != nullptr) {
        vTaskDelete(m_test_task_handle);
        m_test_task_handle = nullptr;
    }

    ESP_LOGI(TAG, "Test Generator stopped.");
}

void TestGenerator::setServerIp(const std::string& ip) {
    if (m_server_ip != ip) {
        m_server_ip = ip;
        ESP_LOGI(TAG, "Target Server IP updated to: %s", m_server_ip.c_str());
        
        // Save to NVS
        nvs_handle_t nvs_handle;
        if (nvs_open("test_gen", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            nvs_set_str(nvs_handle, "target_ip", m_server_ip.c_str());
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
            ESP_LOGI(TAG, "Saved new Target Server IP to NVS.");
        }
        
        // Reconnect client by toggling socket
        std::lock_guard<std::mutex> lock(m_socket_mutex);
        if (m_socket_fd != -1) {
            close(m_socket_fd);
            m_socket_fd = -1;
        }
        m_connected = false;
    }
}

bool TestGenerator::sendRawCommand(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(m_socket_mutex);
    if (m_socket_fd == -1 || !m_connected) {
        return false;
    }
    
    std::string line = cmd + "\n";
    int len = send(m_socket_fd, line.c_str(), line.length(), 0);
    if (len <= 0) {
        ESP_LOGW(TAG, "Socket write failed.");
        return false;
    }
    
    ESP_LOGD(TAG, "[TX Socket %d]: %s", m_socket_fd.load(), cmd.c_str());
    
    std::lock_guard<std::mutex> r_lock(m_results_mutex);
    m_results.packets_sent++;
    return true;
}

// =============================================================================
// Throttle Helper APIs
// =============================================================================
bool TestGenerator::acquireLocomotive(int address, bool is_long) {
    std::string prefix = is_long ? "L" : "S";
    return sendRawCommand("M0+" + prefix + std::to_string(address) + "<;>" + prefix + std::to_string(address));
}

bool TestGenerator::releaseLocomotive(int address, bool is_long) {
    std::string prefix = is_long ? "L" : "S";
    return sendRawCommand("M0-" + prefix + std::to_string(address) + "<;>r");
}

bool TestGenerator::setSpeed(int address, bool is_long, int speed) {
    std::string prefix = is_long ? "L" : "S";
    return sendRawCommand("M0A" + prefix + std::to_string(address) + "<;>V" + std::to_string(speed));
}

bool TestGenerator::setDirection(int address, bool is_long, bool forward) {
    std::string prefix = is_long ? "L" : "S";
    return sendRawCommand("M0A" + prefix + std::to_string(address) + "<;>R" + (forward ? "1" : "0"));
}

bool TestGenerator::setFunction(int address, bool is_long, int func_num, bool state) {
    std::string prefix = is_long ? "L" : "S";
    return sendRawCommand("M0A" + prefix + std::to_string(address) + "<;>F" + (state ? "1" : "0") + std::to_string(func_num));
}

bool TestGenerator::setTurnout(int turnout_addr, bool straight) {
    return sendRawCommand("PTA" + std::string(straight ? "C" : "T") + std::to_string(turnout_addr));
}

// =============================================================================
// TCP Client Connection Management Task
// =============================================================================
void TestGenerator::clientTaskWrapper(void* arg) {
    static_cast<TestGenerator*>(arg)->runClientTask();
}

void TestGenerator::runClientTask() {
    char rx_buf[256];

    while (m_running) {
        if (m_socket_fd == -1) {
            ESP_LOGI(TAG, "Connecting to WiThrottle Server at %s:%d...", m_server_ip.c_str(), m_server_port);
            
            int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (sock < 0) {
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            // Configure TCP Keep-Alive and No-Delay (disable Nagle's algorithm)
            int keepalive = 1;
            int keepidle = 5;      // 5 seconds of idle before starting probes
            int keepintvl = 2;     // 2 seconds between probes
            int keepcnt = 3;       // 3 failed probes (closed after 11 seconds total)
            int nodelay = 1;       // Instant transmission
            setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

            struct sockaddr_in dest_addr;
            dest_addr.sin_addr.s_addr = inet_addr(m_server_ip.c_str());
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(m_server_port);

            // Set socket to non-blocking mode for connection phase
            int flags = fcntl(sock, F_GETFL, 0);
            fcntl(sock, F_SETFL, flags | O_NONBLOCK);

            // Connect to server
            int res = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (res < 0 && errno == EINPROGRESS) {
                fd_set write_fds;
                FD_ZERO(&write_fds);
                FD_SET(sock, &write_fds);

                struct timeval tv;
                tv.tv_sec = 3;  // 3 seconds timeout
                tv.tv_usec = 0;

                int sel_res = select(sock + 1, NULL, &write_fds, NULL, &tv);
                if (sel_res > 0) {
                    int so_error = 0;
                    socklen_t len = sizeof(so_error);
                    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0) {
                        if (so_error == 0) {
                            res = 0; // Connected successfully!
                        } else {
                            res = -1;
                            errno = so_error;
                        }
                    } else {
                        res = -1;
                    }
                } else {
                    res = -1;
                    if (sel_res == 0) {
                        errno = ETIMEDOUT;
                    }
                }
            }

            if (res != 0) {
                ESP_LOGW(TAG, "Socket connection failed (errno: %d). Retrying in 3 seconds...", errno);
                close(sock);
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }

            // Restore socket to blocking mode for regular operation
            fcntl(sock, F_SETFL, flags);

            ESP_LOGI(TAG, "Successfully connected to WiThrottle Server. Initiating handshake...");
            
            // Handshake info
            std::lock_guard<std::mutex> lock(m_socket_mutex);
            m_socket_fd = sock;
            m_connected = true;

            // Send Name & Device ID
            std::string name_cmd = "NAutomated_Test_Generator\n";
            send(m_socket_fd, name_cmd.c_str(), name_cmd.length(), 0);
            
            std::string id_cmd = "HUTestGenClientDevice\n";
            send(m_socket_fd, id_cmd.c_str(), id_cmd.length(), 0);
        }

        // Maintain socket read thread
        int len = recv(m_socket_fd, rx_buf, sizeof(rx_buf) - 1, 0);
        if (len <= 0) {
            ESP_LOGW(TAG, "Socket disconnected or read error.");
            std::lock_guard<std::mutex> lock(m_socket_mutex);
            if (m_socket_fd != -1) {
                close(m_socket_fd);
                m_socket_fd = -1;
            }
            m_connected = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            rx_buf[len] = '\0';
            // Elevated log level to Info for full serial console handshake visibility
            ESP_LOGI(TAG, "[RX Socket %d]: %s", m_socket_fd.load(), rx_buf);
        }
    }

    vTaskDelete(NULL);
}

// =============================================================================
// Autonomous End-to-End Test Orchestrator
// =============================================================================
struct TestTaskParams {
    TestGenerator* generator;
    int scenario_id;
};

void TestGenerator::testTaskWrapper(void* arg) {
    auto* params = static_cast<TestTaskParams*>(arg);
    TestGenerator* gen = params->generator;
    int scenario_id = params->scenario_id;
    delete params;
    
    gen->runTestScenario(scenario_id);
}

bool TestGenerator::launchTestScenario(int scenario_id) {
    if (m_results.is_running) {
        ESP_LOGW(TAG, "A test scenario is already active.");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_results_mutex);
        m_results.is_running = true;
        m_results.total_tests = 0;
        m_results.success_count = 0;
        m_results.failure_count = 0;
        m_results.packets_lost = 0;
        m_results.avg_latency_ms = 0.0f;
        m_log_stream.str("");
        m_log_stream.clear();
    }

    auto* params = new TestTaskParams();
    params->generator = this;
    params->scenario_id = scenario_id;

    BaseType_t ret = xTaskCreatePinnedToCore(
        testTaskWrapper,
        "wt_test_task",
        6144,
        params,
        6,
        &m_test_task_handle,
        1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn test execution task.");
        delete params;
        m_results.is_running = false;
        return false;
    }

    return true;
}

void TestGenerator::runTestScenario(int scenario_id) {
    ESP_LOGI(TAG, "Launching Autonomous Test Scenario %d...", scenario_id);
    
    std::string test_name = "Speed Sweep Scenario";
    if (scenario_id == 2) test_name = "Turnout Storm Scenario";
    else if (scenario_id == 3) test_name = "Multi-Loco Consisting";

    {
        std::lock_guard<std::mutex> lock(m_results_mutex);
        m_results.test_name = test_name;
        m_results.is_running = true;
    }

    m_log_stream << "=== Starting Test Scenario: " << test_name << " ===\n";
    m_log_stream << "Server Target: http://" << m_server_ip << "/api/decoder\n\n";

    if (!m_connected) {
        m_log_stream << "ERROR: WiThrottle Server not connected. Aborting test.\n";
        {
            std::lock_guard<std::mutex> lock(m_results_mutex);
            m_results.is_running = false;
            m_results.last_log = m_log_stream.str();
        }
        m_test_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    float total_latency_ms = 0.0f;
    int tests_completed = 0;

    // Helper to evaluate a test case
    auto run_test_case = [&](const std::string& cmd_desc, const std::string& search_str, std::function<bool()> trigger_func) {
        m_log_stream << "[TEST] " << cmd_desc << " -> ";
        
        {
            std::lock_guard<std::mutex> lock(m_results_mutex);
            m_results.total_tests++;
        }

        // Trigger command
        if (!trigger_func()) {
            m_log_stream << "FAIL (Socket write failed)\n";
            std::lock_guard<std::mutex> lock(m_results_mutex);
            m_results.failure_count++;
            m_results.packets_lost++;
            return;
        }

        float latency = 0.0f;
        // Verify via HTTP GET /api/decoder (up to 3 seconds timeout)
        bool success = verifyDccDecode(search_str, 3000, latency);

        if (success) {
            m_log_stream << "PASS (Latency: " << std::fixed << std::setprecision(1) << latency << " ms)\n";
            std::lock_guard<std::mutex> lock(m_results_mutex);
            m_results.success_count++;
            total_latency_ms += latency;
            tests_completed++;
        } else {
            m_log_stream << "FAIL (DCC Packet mismatch or timeout)\n";
            std::lock_guard<std::mutex> lock(m_results_mutex);
            m_results.failure_count++;
            m_results.packets_lost++;
        }
        
        // Wait 300ms between commands to avoid track signal noise storms
        vTaskDelay(pdMS_TO_TICKS(300));
    };

    // =============================================================================
    // Scenario 1: Speed / Direction Sweep
    // =============================================================================
    if (scenario_id == 1) {
        run_test_case("Acquire Loco 3 (Short Address)", "", [&]() { return acquireLocomotive(3, false); });
        
        // Speed Steps tests
        run_test_case("Loco 3 FWD Speed 10", "Loco 3 | Speed 128-step: 10 (steps 1-126) FWD", [&]() { return setSpeed(3, false, 10); });
        run_test_case("Loco 3 FWD Speed 40", "Loco 3 | Speed 128-step: 40 (steps 1-126) FWD", [&]() { return setSpeed(3, false, 40); });
        run_test_case("Loco 3 FWD Speed 90", "Loco 3 | Speed 128-step: 90 (steps 1-126) FWD", [&]() { return setSpeed(3, false, 90); });
        
        // Direction test
        run_test_case("Loco 3 Change Direction (REV)", "Loco 3 | Speed 128-step: 90 (steps 1-126) REV", [&]() { return setDirection(3, false, false); });
        
        // Functions tests
        run_test_case("Loco 3 Headlight ON (F0)", "Loco 3 | Func F0-F4: F0(Lght):ON", [&]() { return setFunction(3, false, 0, true); });
        run_test_case("Loco 3 Bell ON (F1)", "F1(Bell):ON", [&]() { return setFunction(3, false, 1, true); });
        run_test_case("Loco 3 Bell OFF (F1)", "F1(Bell):OFF", [&]() { return setFunction(3, false, 1, false); });
        
        // Emergency Stop
        run_test_case("Loco 3 EMERGENCY STOP", "Loco 3 | Speed 128-step: EMERGENCY STOP", [&]() { return sendRawCommand("M0AL3<;>X"); });
        
        run_test_case("Release Loco 3", "", [&]() { return releaseLocomotive(3, false); });
    }

    // =============================================================================
    // Scenario 2: Turnout Accessory Storm
    // =============================================================================
    else if (scenario_id == 2) {
        // Test rapid switching on Turnouts 10, 11, and 12
        run_test_case("Turnout 10 STRAIGHT (Closed)", "Turnout Addr: 10 | STRAIGHT (ACTIVATE)", [&]() { return setTurnout(10, true); });
        run_test_case("Turnout 10 CURVED (Thrown)", "Turnout Addr: 10 | CURVED (ACTIVATE)", [&]() { return setTurnout(10, false); });
        
        run_test_case("Turnout 11 STRAIGHT (Closed)", "Turnout Addr: 11 | STRAIGHT (ACTIVATE)", [&]() { return setTurnout(11, true); });
        run_test_case("Turnout 11 CURVED (Thrown)", "Turnout Addr: 11 | CURVED (ACTIVATE)", [&]() { return setTurnout(11, false); });

        run_test_case("Turnout 12 STRAIGHT (Closed)", "Turnout Addr: 12 | STRAIGHT (ACTIVATE)", [&]() { return setTurnout(12, true); });
        run_test_case("Turnout 12 CURVED (Thrown)", "Turnout Addr: 12 | CURVED (ACTIVATE)", [&]() { return setTurnout(12, false); });
    }

    // =============================================================================
    // Scenario 3: Multi-Loco Consisting & High-Priority Multiplexing
    // =============================================================================
    else if (scenario_id == 3) {
        run_test_case("Acquire Loco 3 (Short Address)", "", [&]() { return acquireLocomotive(3, false); });
        run_test_case("Acquire Loco 1234 (Long Address)", "", [&]() { return acquireLocomotive(1234, true); });

        // Alternate commands to stress-test multiplexing
        run_test_case("Loco 3 FWD Speed 20", "Loco 3 | Speed 128-step: 20 (steps 1-126) FWD", [&]() { return setSpeed(3, false, 20); });
        run_test_case("Loco 1234 FWD Speed 55", "Loco 1234 | Speed 128-step: 55 (steps 1-126) FWD", [&]() { return setSpeed(1234, true, 55); });
        
        run_test_case("Loco 3 Bell ON (F1)", "Loco 3 | Func F0-F4: F1(Bell):ON", [&]() { return setFunction(3, false, 1, true); });
        run_test_case("Loco 1234 Light ON (F0)", "Loco 1234 | Func F0-F4: F0(Lght):ON", [&]() { return setFunction(1234, true, 0, true); });
        
        run_test_case("Loco 3 STOP", "Loco 3 | Speed 128-step: STOP", [&]() { return setSpeed(3, false, 0); });
        run_test_case("Loco 1234 STOP", "Loco 1234 | Speed 128-step: STOP", [&]() { return setSpeed(1234, true, 0); });

        run_test_case("Release Loco 3", "", [&]() { return releaseLocomotive(3, false); });
        run_test_case("Release Loco 1234", "", [&]() { return releaseLocomotive(1234, true); });
    }

    // Wrap up results
    float final_avg_latency = 0.0f;
    if (tests_completed > 0) {
        final_avg_latency = total_latency_ms / tests_completed;
    }

    m_log_stream << "\n=== Test Scenario Completed ===\n";
    m_log_stream << "Total Executed: " << m_results.total_tests << "\n";
    m_log_stream << "Successful:     " << m_results.success_count << "\n";
    m_log_stream << "Failed:         " << m_results.failure_count << "\n";
    m_log_stream << "Success Rate:   " << std::fixed << std::setprecision(1) 
                 << (m_results.total_tests > 0 ? (float)m_results.success_count * 100.0f / m_results.total_tests : 0.0f) << "%\n";
    m_log_stream << "Avg Latency:    " << std::fixed << std::setprecision(1) << final_avg_latency << " ms\n";

    {
        std::lock_guard<std::mutex> lock(m_results_mutex);
        m_results.avg_latency_ms = final_avg_latency;
        m_results.is_running = false;
        m_results.last_log = m_log_stream.str();
    }

    ESP_LOGI(TAG, "Autonomous Test Scenario successfully finished.");
    m_test_task_handle = nullptr;
    vTaskDelete(NULL);
}

// =============================================================================
// HTTP Verification Routine
// =============================================================================
static std::string http_get_request(const std::string& url) {
    std::string response = "";
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = _http_event_handler;
    config.user_data = &response;
    config.timeout_ms = 1500;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return "";
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK) {
        return response;
    }
    return "";
}

bool TestGenerator::verifyDccDecode(const std::string& expected_substring, uint32_t timeout_ms, float& out_latency_ms) {
    if (expected_substring.empty()) {
        // Skip verification (used for acquire/release where no physical packet is sent to DCC tracks)
        out_latency_ms = 0.0f;
        return true;
    }

    uint64_t start_time = esp_timer_get_time(); // microseconds
    uint64_t timeout_us = timeout_ms * 1000;
    std::string url = "http://" + m_server_ip + "/api/decoder";

    while ((esp_timer_get_time() - start_time) < timeout_us) {
        std::string response_body = http_get_request(url);
        if (!response_body.empty()) {
            cJSON* root = cJSON_Parse(response_body.c_str());
            if (root) {
                cJSON* packets_arr = cJSON_GetObjectItem(root, "packets");
                if (packets_arr && cJSON_IsArray(packets_arr)) {
                    int sz = cJSON_GetArraySize(packets_arr);
                    for (int i = 0; i < sz; ++i) {
                        cJSON* p_obj = cJSON_GetArrayItem(packets_arr, i);
                        if (p_obj) {
                            cJSON* text_item = cJSON_GetObjectItem(p_obj, "text");
                            if (text_item && cJSON_IsString(text_item)) {
                                std::string packet_text = text_item->valuestring;
                                if (packet_text.find(expected_substring) != std::string::npos) {
                                    // Found! Calculate latency
                                    uint64_t end_time = esp_timer_get_time();
                                    out_latency_ms = static_cast<float>(end_time - start_time) / 1000.0f;
                                    cJSON_Delete(root);
                                    return true;
                                }
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
        }
        // Poll every 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    out_latency_ms = -1.0f;
    return false;
}

std::string TestGenerator::getResultsJson() {
    std::lock_guard<std::mutex> lock(m_results_mutex);
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "test_name", m_results.test_name.c_str());
    cJSON_AddBoolToObject(obj, "is_running", m_results.is_running);
    cJSON_AddNumberToObject(obj, "total_tests", m_results.total_tests);
    cJSON_AddNumberToObject(obj, "success_count", m_results.success_count);
    cJSON_AddNumberToObject(obj, "failure_count", m_results.failure_count);
    cJSON_AddNumberToObject(obj, "packets_sent", m_results.packets_sent);
    cJSON_AddNumberToObject(obj, "packets_lost", m_results.packets_lost);
    cJSON_AddNumberToObject(obj, "avg_latency_ms", m_results.avg_latency_ms);
    cJSON_AddStringToObject(obj, "last_log", m_results.last_log.c_str());

    char* rendered = cJSON_PrintUnformatted(obj);
    std::string json_str = rendered ? rendered : "{}";
    
    cJSON_free(rendered);
    cJSON_Delete(obj);
    return json_str;
}

} // namespace wt
} // namespace dcc
