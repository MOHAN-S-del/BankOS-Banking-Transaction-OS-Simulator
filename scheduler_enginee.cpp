// ============================================================================
// BankOS Middleware: OS-Driven Concurrent Banking Transaction Middleware
// File: scheduler_engine.cpp
//
// Module 4: Main Execution Loop & Dual IPC Bridge (Python/MySQL Interop)
//
// Combines:
// - pcb.hpp (Data Structures & Auto Burst Calculation)
// - queue_algorithms.hpp (PriorityQueueQ1, SJFQueueQ2, FCFSQueueQ3)
// - mlfq_controller.hpp (Multi-Level Feedback Queue & Dynamic Aging Engine)
//
// Dual-Bridge OS Architecture:
// 1. High-Performance TCP Socket Server on Port 8765
// 2. Resilient Atomic File Bridge (state.json & inbox.jsonl)
//
// Compile Command:
// g++ -O2 -std=c++17 scheduler_engine.cpp -o scheduler -pthread
// ============================================================================

#include "pcb.hpp"
#include "queue_algorithms.hpp"
#include "mlfq_controller.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cstring>
#include <atomic>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace bankos {

// ----------------------------------------------------------------------------
// Embedded High-Speed TCP Socket Bridge (Port 8765)
// ----------------------------------------------------------------------------
class TcpBridge {
public:
    TcpBridge(MLFQController& controller, int port = 8765)
        : controller_(controller), port_(port) {}

    ~TcpBridge() {
        stop();
    }

    void start() {
        running_ = true;
        thread_ = std::thread(&TcpBridge::run_server, this);
    }

    void stop() {
        running_ = false;
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    MLFQController& controller_;
    int port_;
    std::atomic<bool> running_{false};
    int server_fd_ = -1;
    std::thread thread_;

    void run_server() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "[C++ Socket] Failed to create socket\n";
            return;
        }

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[C++ Socket] Port " << port_ << " bind failed; continuing with atomic file bridge.\n";
            close(server_fd_);
            server_fd_ = -1;
            return;
        }

        if (listen(server_fd_, 16) < 0) {
            std::cerr << "[C++ Socket] Listen failed\n";
            close(server_fd_);
            server_fd_ = -1;
            return;
        }

        std::cout << "[C++ Socket] ✓ BankOS Middleware TCP Server running on port " << port_ << "\n";

        while (running_) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (!running_) break;
                continue;
            }

            char buffer[4096] = {0};
            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                std::string req(buffer, bytes_read);
                handle_http_request(client_fd, req);
            }
            close(client_fd);
        }
    }

    void handle_http_request(int client_fd, const std::string& req) {
        if (req.find("GET /state") != std::string::npos || req.find("GET / ") != std::string::npos) {
            std::string body = controller_.get_state_json();
            send_http_response(client_fd, 200, "application/json", body);
        } else if (req.find("POST /inject") != std::string::npos) {
            // Fast ping from Python bridge
            std::string body = "{\"status\":\"ok\"}";
            send_http_response(client_fd, 200, "application/json", body);
        } else {
            std::string body = controller_.get_state_json();
            send_http_response(client_fd, 200, "application/json", body);
        }
    }

    void send_http_response(int client_fd, int code, const std::string& mime, const std::string& body) {
        std::ostringstream ss;
        ss << "HTTP/1.1 " << code << " OK\r\n";
        ss << "Content-Type: " << mime << "\r\n";
        ss << "Content-Length: " << body.length() << "\r\n";
        ss << "Access-Control-Allow-Origin: *\r\n";
        ss << "Connection: close\r\n\r\n";
        ss << body;

        std::string res = ss.str();
        write(client_fd, res.data(), res.size());
    }
};

// ----------------------------------------------------------------------------
// File Bridge & Atomic State Persistence (state.json / inbox.jsonl)
// ----------------------------------------------------------------------------
inline void dump_atomic_state(MLFQController& controller) {
    std::string json_str = controller.get_state_json();
    std::ofstream tmp_f("state.json.tmp");
    if (tmp_f.is_open()) {
        tmp_f << json_str;
        tmp_f.close();
        std::rename("state.json.tmp", "state.json");
    }
}

inline void process_file_inbox(MLFQController& controller) {
    std::ifstream in("inbox.jsonl");
    if (!in.is_open()) return;

    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    in.close();

    if (!lines.empty()) {
        // Truncate inbox atomically
        std::ofstream out("inbox.jsonl", std::ios::trunc);
        out.close();

        for (const auto& l : lines) {
            std::istringstream ss(l);
            std::string part;
            std::string cmd = "inject";
            std::string txn_id = "TXN-AUTO";
            std::string sender = "ACC1001";
            std::string receiver = "ACC1002";
            std::string type = "UPI";
            double amount = 1000.0;
            bool is_test = false;

            while (std::getline(ss, part, ';')) {
                auto eq = part.find('=');
                if (eq != std::string::npos) {
                    std::string k = part.substr(0, eq);
                    std::string v = part.substr(eq + 1);
                    if (k == "cmd") cmd = v;
                    else if (k == "txn_id") txn_id = v;
                    else if (k == "sender") sender = v;
                    else if (k == "receiver") receiver = v;
                    else if (k == "type") type = v;
                    else if (k == "amount") {
                        try { amount = std::stod(v); } catch (...) {}
                    }
                    else if (k == "is_test") is_test = (v == "true" || v == "1");
                }
            }

            if (cmd == "reset") {
                controller.reset_state();
            } else {
                controller.admit_transaction(txn_id, sender, receiver, type, amount, is_test);
            }
        }
        dump_atomic_state(controller);
    }
}

} // namespace bankos

// ----------------------------------------------------------------------------
// Main Application Entry Point
// ----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    int port = 8765;
    if (argc > 1) {
        try { port = std::stoi(argv[1]); } catch (...) {}
    }

    std::cout << "========================================================\n";
    std::cout << " BankOS Middleware: OS-Driven Concurrent Banking\n";
    std::cout << " Multi-Level Feedback Queue (MLFQ) Engine Initializing...\n";
    std::cout << " Algorithms Active:\n";
    std::cout << "   - Queue 1: Preemptive Priority Scheduling (q = 2s)\n";
    std::cout << "   - Queue 2: Shortest Job First / SJF (q = 4s)\n";
    std::cout << "   - Queue 3: First-Come, First-Served / FCFS\n";
    std::cout << "   - Starvation Prevention: Dynamic Aging (6s & 5s)\n";
    std::cout << "========================================================\n";

    bankos::MLFQController controller;
    bankos::dump_atomic_state(controller);

    bankos::TcpBridge tcp_bridge(controller, port);
    tcp_bridge.start();

    std::atomic<bool> engine_running{true};

    // 1. CPU Execution Worker Thread (Ticking every 250ms = 0.25s)
    std::thread cpu_thread([&]() {
        while (engine_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            bool changed = controller.tick_cpu();
            if (changed) {
                bankos::dump_atomic_state(controller);
            }
        }
    });

    // 2. Dynamic Aging Worker Thread (Runs every 1.0 second)
    std::thread aging_thread([&]() {
        while (engine_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            bool changed = controller.check_aging_and_update_wait();
            if (changed) {
                bankos::dump_atomic_state(controller);
            }
        }
    });

    // 3. File Inbox Poller Thread (Runs every 100ms)
    std::thread inbox_thread([&]() {
        while (engine_running) {
            bankos::process_file_inbox(controller);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::cout << "[C++ Engine] ✓ BankOS Middleware MLFQ Engine operational.\n";
    std::cout << "[C++ Engine] Telemetry streaming to state.json & TCP Port " << port << "\n";

    // Keep main thread alive
    while (engine_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    engine_running = false;
    if (cpu_thread.joinable()) cpu_thread.join();
    if (aging_thread.joinable()) aging_thread.join();
    if (inbox_thread.joinable()) inbox_thread.join();
    tcp_bridge.stop();

    return 0;
}
