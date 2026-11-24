// ============================================================================
// BankOS Middleware: Pure C++17 Multi-Level Feedback Queue (MLFQ) Engine
// File: scheduler.cpp
//
// Features:
// 1. Process Control Block (struct Process) modeling banking transactions.
// 2. Auto Burst Time Calculation:
//    - ATM: 1s
//    - UPI: 2s
//    - STANDARD: 3s (amount <= 10,000) or 5s (amount > 10,000)
//    - PAYROLL: 7s
// 3. 3-Level MLFQ Scheduling:
//    - Queue 1: Priority Scheduling (ATM, UPI)
//    - Queue 2: Shortest Job First (SJF) (Standard <= 3s)
//    - Queue 3: First-Come, First-Served (FCFS) (Payroll, Large > 3s)
// 4. Built-in Local Micro-HTTP Bridge for server.py communication.
//
// Compile: g++ -O2 -std=c++17 scheduler.cpp -o scheduler -pthread
// ============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

// POSIX Sockets for lightweight local IPC bridge
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace bankos {

// ----------------------------------------------------------------------------
// Process Structure (PCB)
// ----------------------------------------------------------------------------
struct Process {
    std::string txn_id;
    std::string sender_id;
    std::string receiver_id;
    std::string txn_type;       // ATM, UPI, PAYROLL, STANDARD
    double amount = 0.0;
    int auto_burst_time = 1;    // Calculated automatically in seconds
    int remaining_burst = 1;
    std::string assigned_queue; // Queue 1 (Priority), Queue 2 (SJF), Queue 3 (FCFS)
    std::string status = "QUEUED"; // QUEUED, RUNNING, COMPLETED
    bool is_test = false;       // True for user-injected test transactions
    int progress_pct = 0;
    double wait_time = 0.0;
    std::chrono::steady_clock::time_point arrival_tp;

    std::string to_json() const {
        std::ostringstream ss;
        ss << "{"
           << "\"txn_id\":\"" << txn_id << "\","
           << "\"sender_id\":\"" << sender_id << "\","
           << "\"receiver_id\":\"" << receiver_id << "\","
           << "\"txn_type\":\"" << txn_type << "\","
           << "\"amount\":" << std::fixed << std::setprecision(2) << amount << ","
           << "\"auto_burst_time\":" << auto_burst_time << ","
           << "\"remaining_burst\":" << remaining_burst << ","
           << "\"assigned_queue\":\"" << assigned_queue << "\","
           << "\"status\":\"" << status << "\","
           << "\"is_test\":" << (is_test ? "true" : "false") << ","
           << "\"progress_pct\":" << progress_pct << ","
           << "\"wait_time\":" << std::fixed << std::setprecision(2) << wait_time
           << "}";
        return ss.str();
    }
};

// ----------------------------------------------------------------------------
// Automatic Burst Time & Queue Assignment
// ----------------------------------------------------------------------------
int calculate_auto_burst_time(const std::string& type, double amount) {
    if (type == "ATM") return 1;
    if (type == "UPI") return 2;
    if (type == "STANDARD") {
        return (amount <= 10000.0) ? 3 : 5;
    }
    if (type == "PAYROLL") return 7;
    return 3;
}

std::string determine_assigned_queue(const std::string& type, int burst_time) {
    if (type == "ATM" || type == "UPI") {
        return "Queue 1 (Priority)";
    }
    if (type == "STANDARD" && burst_time <= 3) {
        return "Queue 2 (SJF)";
    }
    return "Queue 3 (FCFS)";
}

// ----------------------------------------------------------------------------
// MLFQ OS Scheduler Engine
// ----------------------------------------------------------------------------
class MLFQScheduler {
public:
    MLFQScheduler() = default;

    Process admit_transaction(const std::string& txn_id, const std::string& sender,
                              const std::string& receiver, const std::string& type,
                              double amount, bool is_test) {
        std::lock_guard<std::mutex> lock(mtx_);

        Process p;
        p.txn_id = txn_id;
        p.sender_id = sender;
        p.receiver_id = receiver;
        p.txn_type = type;
        p.amount = amount;
        p.auto_burst_time = calculate_auto_burst_time(type, amount);
        p.remaining_burst = p.auto_burst_time;
        p.assigned_queue = determine_assigned_queue(type, p.auto_burst_time);
        p.status = "QUEUED";
        p.is_test = is_test;
        p.arrival_tp = std::chrono::steady_clock::now();

        if (p.assigned_queue == "Queue 1 (Priority)") {
            queue_1_.push_back(p);
        } else if (p.assigned_queue == "Queue 2 (SJF)") {
            queue_2_.push_back(p);
            // Sort Q2 ascending by burst time (Shortest Job First)
            std::sort(queue_2_.begin(), queue_2_.end(), [](const Process& a, const Process& b) {
                return a.auto_burst_time < b.auto_burst_time;
            });
        } else {
            queue_3_.push_back(p);
        }

        total_admitted_++;
        return p;
    }

    void start_cpu_worker() {
        worker_running_ = true;
        worker_thread_ = std::thread(&MLFQScheduler::cpu_loop, this);
    }

    void stop() {
        worker_running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    std::string get_state_json() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ostringstream ss;
        ss << "{";

        // Active CPU process
        ss << "\"active_process\":";
        if (active_process_) {
            ss << active_process_->to_json();
        } else {
            ss << "null";
        }
        ss << ",";

        // Queue 1
        ss << "\"queue_1\":[";
        for (size_t i = 0; i < queue_1_.size(); ++i) {
            ss << queue_1_[i].to_json();
            if (i + 1 < queue_1_.size()) ss << ",";
        }
        ss << "],";

        // Queue 2
        ss << "\"queue_2\":[";
        for (size_t i = 0; i < queue_2_.size(); ++i) {
            ss << queue_2_[i].to_json();
            if (i + 1 < queue_2_.size()) ss << ",";
        }
        ss << "],";

        // Queue 3
        ss << "\"queue_3\":[";
        for (size_t i = 0; i < queue_3_.size(); ++i) {
            ss << queue_3_[i].to_json();
            if (i + 1 < queue_3_.size()) ss << ",";
        }
        ss << "],";

        // Completed ready for DB commit
        ss << "\"completed\":[";
        for (size_t i = 0; i < recent_completed_.size(); ++i) {
            ss << recent_completed_[i].to_json();
            if (i + 1 < recent_completed_.size()) ss << ",";
        }
        ss << "],";

        // Metrics
        ss << "\"stats\":{"
           << "\"total_admitted\":" << total_admitted_ << ","
           << "\"total_completed\":" << total_completed_
           << "}";

        ss << "}";
        return ss.str();
    }

    // Drain newly completed transactions so Python can update MySQL
    std::vector<Process> drain_completed() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<Process> res = std::move(ready_for_commit_);
        ready_for_commit_.clear();
        return res;
    }

private:
    void cpu_loop() {
        while (worker_running_) {
            Process current;
            bool found = false;

            {
                std::lock_guard<std::mutex> lock(mtx_);
                // 1. Queue 1 has absolute priority
                if (!queue_1_.empty()) {
                    current = queue_1_.front();
                    queue_1_.pop_front();
                    found = true;
                }
                // 2. Queue 2 (Shortest Job First)
                else if (!queue_2_.empty()) {
                    current = queue_2_.front();
                    queue_2_.erase(queue_2_.begin());
                    found = true;
                }
                // 3. Queue 3 (First-Come, First-Served)
                else if (!queue_3_.empty()) {
                    current = queue_3_.front();
                    queue_3_.pop_front();
                    found = true;
                }

                if (found) {
                    current.status = "RUNNING";
                    auto now = std::chrono::steady_clock::now();
                    std::chrono::duration<double> wait_d = now - current.arrival_tp;
                    current.wait_time = wait_d.count();
                    active_process_ = std::make_unique<Process>(current);
                }
            }

            if (!found) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            // Simulate Burst Execution in smooth ticks
            int burst = current.auto_burst_time;
            const int total_steps = 20;
            // 0.4s to 0.5s per burst second for responsive live visual updates
            double total_sim_ms = std::max(400.0, burst * 450.0);
            int step_ms = static_cast<int>(total_sim_ms / total_steps);

            for (int s = 1; s <= total_steps; ++s) {
                std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
                std::lock_guard<std::mutex> lock(mtx_);
                if (active_process_) {
                    active_process_->progress_pct = (s * 100) / total_steps;
                    active_process_->remaining_burst = std::max(0, burst - (s * burst) / total_steps);
                }
            }

            // Transaction execution completed
            {
                std::lock_guard<std::mutex> lock(mtx_);
                current.status = "COMPLETED";
                current.progress_pct = 100;
                current.remaining_burst = 0;

                total_completed_++;
                recent_completed_.push_front(current);
                if (recent_completed_.size() > 50) recent_completed_.pop_back();

                ready_for_commit_.push_back(current);
                active_process_.reset();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::mutex mtx_;
    std::deque<Process> queue_1_;
    std::vector<Process> queue_2_;
    std::deque<Process> queue_3_;
    std::unique_ptr<Process> active_process_;
    std::deque<Process> recent_completed_;
    std::vector<Process> ready_for_commit_;

    uint64_t total_admitted_ = 0;
    uint64_t total_completed_ = 0;
    bool worker_running_ = false;
    std::thread worker_thread_;
};

} // namespace bankos

// ----------------------------------------------------------------------------
// Local Micro-Bridge Server on Port 8765
// ----------------------------------------------------------------------------
static bankos::MLFQScheduler g_scheduler;

void run_ipc_bridge(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[scheduler] Socket creation error\n";
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[scheduler] Bind failed on port " << port << " (Port may be in use)\n";
        close(server_fd);
        return;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "[scheduler] Listen failed\n";
        close(server_fd);
        return;
    }

    std::cout << "[scheduler] C++ MLFQ OS Engine listening on 127.0.0.1:" << port << "\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd < 0) continue;

        char buffer[4096] = {0};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            std::string req(buffer, bytes_read);

            // Handle GET /state
            if (req.find("GET /state") != std::string::npos) {
                std::string body = g_scheduler.get_state_json();
                std::ostringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << body.size() << "\r\n\r\n"
                     << body;
                std::string resp_str = resp.str();
                write(client_fd, resp_str.c_str(), resp_str.size());
            }
            // Handle POST /inject
            else if (req.find("POST /inject") != std::string::npos) {
                // Extract parameters from URL query or simple body
                std::string txn_id = "TXN-AUTO";
                std::string sender = "ACC1001";
                std::string receiver = "ACC1002";
                std::string type = "UPI";
                double amount = 1000.0;
                bool is_test = false;

                auto parse_param = [&](const std::string& key) -> std::string {
                    std::string pattern = key + "=";
                    auto pos = req.find(pattern);
                    if (pos != std::string::npos) {
                        auto end = req.find_first_of("& \r\n", pos);
                        return req.substr(pos + pattern.size(), end - (pos + pattern.size()));
                    }
                    return "";
                };

                std::string p_txn = parse_param("txn_id");
                if (!p_txn.empty()) txn_id = p_txn;

                std::string p_snd = parse_param("sender_id");
                if (!p_snd.empty()) sender = p_snd;

                std::string p_rcv = parse_param("receiver_id");
                if (!p_rcv.empty()) receiver = p_rcv;

                std::string p_type = parse_param("txn_type");
                if (!p_type.empty()) type = p_type;

                std::string p_amt = parse_param("amount");
                if (!p_amt.empty()) {
                    try { amount = std::stod(p_amt); } catch (...) {}
                }

                std::string p_test = parse_param("is_test");
                if (p_test == "1" || p_test == "true") is_test = true;

                bankos::Process admitted = g_scheduler.admit_transaction(txn_id, sender, receiver, type, amount, is_test);

                std::string body = admitted.to_json();
                std::ostringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << body.size() << "\r\n\r\n"
                     << body;
                std::string resp_str = resp.str();
                write(client_fd, resp_str.c_str(), resp_str.size());
            }
            // Default 404
            else {
                std::string body = "{\"error\":\"Not Found\"}";
                std::ostringstream resp;
                resp << "HTTP/1.1 404 Not Found\r\n"
                     << "Content-Length: " << body.size() << "\r\n\r\n"
                     << body;
                std::string resp_str = resp.str();
                write(client_fd, resp_str.c_str(), resp_str.size());
            }
        }
        close(client_fd);
    }
    close(server_fd);
}

int main(int argc, char* argv[]) {
    int port = 8765;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    std::cout << "====================================================\n";
    std::cout << "   BankOS Middleware: C++17 MLFQ OS Scheduler      \n";
    std::cout << "   Auto Burst Time Engine (ATM/UPI/SJF/FCFS)       \n";
    std::cout << "====================================================\n";

    g_scheduler.start_cpu_worker();
    run_ipc_bridge(port);

    g_scheduler.stop();
    return 0;
}
