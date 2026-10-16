#include "pcb.hpp"
#include "workload_generator.hpp"
#include "mlfq_scheduler.hpp"
#include "httplib.h"

#include <iostream>
#include <iomanip>
#include <memory>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>

using namespace bank_sim;

std::atomic<bool> g_running(true);

void signal_handler(int) {
    g_running.store(false);
}

void print_banner() {
    std::cout << "\n"
              << "========================================================================================\n"
              << "          BANK TRANSACTION OS SIMULATOR: MULTI-LEVEL FEEDBACK QUEUE (MLFQ)             \n"
              << "         Measurement-Driven Adaptive Concurrency Control (MD-ACC) Engine                \n"
              << "========================================================================================\n"
              << " [WORKLOAD]  176,000 Banking Requests (UPI ~60%, ATM ~25%, Salary ~15%) Bursty Traffic   \n"
              << " [QUEUE 1]   High Priority: Shortest Remaining Time First (SRTF) for Retail UPI & ATM   \n"
              << " [QUEUE 2]   Low Priority: Round Robin (RR, Quantum = 5 ticks) for Corporate Salary     \n"
              << " [AGING]     Starvation Prevention: Promotes Salary processes to Q1 when waiting > X ticks \n"
              << " [MD-ACC]    Measurement-Driven Adaptive Concurrency Control Middleware Layer            \n"
              << " [BACKOFF]   Exponential Backoff (B_0 * 2^retry) wait-state under HIGH contention      \n"
              << " [COMPARE]   Comparison Mode: Baseline (High Aborts) vs MD-ACC (Adaptive Throttling)    \n"
              << " [METRICS]   Live Dynamic Terminal Telemetry: Conflict Rate, Abort Rate, TPS            \n"
              << " [HTTP API]  cpp-httplib Server exposing live state transitions and metrics             \n"
              << "========================================================================================\n\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    print_banner();

    int port = 8080;
    int aging_threshold = 50;  // X ticks aging threshold
    int time_quantum = 5;      // Queue 2 Round Robin slice
    size_t total_requests = 176000;
    bool fast_headless = false;
    bool mdacc_enabled = true; // MD-ACC enabled by default; --baseline disables it

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--aging" && i + 1 < argc) {
            aging_threshold = std::stoi(argv[++i]);
        } else if (arg == "--quantum" && i + 1 < argc) {
            time_quantum = std::stoi(argv[++i]);
        } else if (arg == "--headless") {
            fast_headless = true;
        } else if (arg == "--baseline") {
            mdacc_enabled = false;
        } else if (arg == "--mdacc") {
            mdacc_enabled = true;
        }
    }

    std::cout << "[Config] Mode: " << (mdacc_enabled ? "MD-ACC (Adaptive Backoff)" : "BASELINE (Direct DB / 2PL OCC)") << "\n";

    // 1. Generate 176,000 bursty banking transactions as OS processes
    WorkloadGenerator generator;
    auto workload = generator.generate_workload(total_requests, 10000);

    // 2. Instantiate and load MLFQ Scheduler with MD-ACC Middleware
    auto scheduler = std::make_shared<MLFQScheduler>(aging_threshold, time_quantum, mdacc_enabled);
    scheduler->load_workload(std::move(workload));

    if (fast_headless) {
        std::cout << "[Simulator] Running fast headless mode...\n";
        scheduler->run_simulation(200000, true);
        scheduler->export_transitions_to_json("state_transitions.json", 100000);
        return 0;
    }

    // 3. Setup cpp-httplib Server
    httplib::Server svr;
    svr.set_mount_point("/", "web");

    // REST API endpoints
    svr.Get("/api/status", [scheduler](const httplib::Request&, httplib::Response& res) {
        res.set_content(scheduler->get_status_json(), "application/json");
    });

    svr.Get("/api/transitions", [scheduler](const httplib::Request&, httplib::Response& res) {
        res.set_content(scheduler->get_recent_transitions_json(100), "application/json");
    });

    svr.Get("/api/queues", [scheduler](const httplib::Request&, httplib::Response& res) {
        res.set_content(scheduler->get_active_queues_json(), "application/json");
    });

    svr.Get("/api/export", [scheduler](const httplib::Request&, httplib::Response& res) {
        scheduler->export_transitions_to_json("state_transitions.json", 50000);
        res.set_content("{\"status\":\"exported\",\"file\":\"state_transitions.json\"}", "application/json");
    });

    // MD-ACC Middleware & Comparison Mode REST API endpoints
    svr.Get("/api/middleware", [scheduler](const httplib::Request&, httplib::Response& res) {
        res.set_content(scheduler->middleware()->to_json(), "application/json");
    });

    svr.Get("/api/middleware/toggle", [scheduler](const httplib::Request&, httplib::Response& res) {
        scheduler->middleware()->toggle_mode();
        res.set_content(scheduler->middleware()->to_json(), "application/json");
    });

    svr.Post("/api/middleware/toggle", [scheduler](const httplib::Request&, httplib::Response& res) {
        scheduler->middleware()->toggle_mode();
        res.set_content(scheduler->middleware()->to_json(), "application/json");
    });

    svr.Get("/api/middleware/mode", [scheduler](const httplib::Request& req, httplib::Response& res) {
        auto pos = req.path.find("enabled=");
        if (pos != std::string::npos) {
            std::string val = req.path.substr(pos + 8);
            bool en = (val == "1" || val == "true");
            scheduler->set_comparison_mode(en);
        }
        res.set_content(scheduler->middleware()->to_json(), "application/json");
    });

    svr.Post("/api/middleware/mode", [scheduler](const httplib::Request& req, httplib::Response& res) {
        auto pos = req.path.find("enabled=");
        if (pos != std::string::npos) {
            std::string val = req.path.substr(pos + 8);
            bool en = (val == "1" || val == "true");
            scheduler->set_comparison_mode(en);
        }
        res.set_content(scheduler->middleware()->to_json(), "application/json");
    });

    // Run HTTP server in background thread
    std::thread server_thread([&svr, port]() {
        svr.listen("0.0.0.0", port);
    });

    // 4. Background Simulation Worker Thread
    std::atomic<bool> sim_paused(false);
    std::atomic<int> sim_speed_delay_us(1000); // 1ms delay per tick

    svr.Get("/api/control/pause", [&sim_paused](const httplib::Request&, httplib::Response& res) {
        sim_paused.store(true);
        res.set_content("{\"status\":\"paused\"}", "application/json");
    });

    svr.Get("/api/control/resume", [&sim_paused](const httplib::Request&, httplib::Response& res) {
        sim_paused.store(false);
        res.set_content("{\"status\":\"resumed\"}", "application/json");
    });

    svr.Get("/api/control/speed", [&sim_speed_delay_us](const httplib::Request& req, httplib::Response& res) {
        auto pos = req.path.find("delay=");
        if (pos != std::string::npos) {
            int d = std::stoi(req.path.substr(pos + 6));
            sim_speed_delay_us.store(std::max(0, d));
        }
        res.set_content("{\"status\":\"ok\",\"delay_us\":" + std::to_string(sim_speed_delay_us.load()) + "}", "application/json");
    });

    std::thread sim_thread([scheduler, &sim_paused, &sim_speed_delay_us]() {
        while (g_running.load()) {
            if (!sim_paused.load()) {
                scheduler->step_tick();
            }
            int delay = sim_speed_delay_us.load();
            if (delay > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(delay));
            }
        }
    });

    std::cout << "\n[System Online] Simulation running. Web Dashboard live at http://localhost:" << port << "\n"
              << "[Logging] Periodic state transition snapshots writing to state_transitions.json\n"
              << "Press Ctrl+C to terminate simulation.\n\n";

    // Periodic terminal monitoring log: dynamic TPS, Abort Rate, Conflict Rate, Contention Score
    int counter = 0;
    uint64_t prev_completed = 0;
    auto prev_time = std::chrono::steady_clock::now();

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        counter++;

        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - prev_time).count();
        prev_time = now;

        uint64_t cur_completed = scheduler->total_completed();
        double tps = elapsed_sec > 0 ? static_cast<double>(cur_completed - prev_completed) / elapsed_sec : 0.0;
        prev_completed = cur_completed;

        auto mw = scheduler->middleware();
        bool mdacc = mw->is_mdacc_enabled();
        ContentionLevel lvl = mw->contention_level();
        std::string lvl_str = contentionLevelToString(lvl);
        double conflict_pct = mw->conflict_rate() * 100.0;
        double abort_pct = mw->abort_rate() * 100.0;
        uint64_t total_aborts = mw->total_aborts();
        uint64_t total_backoffs = mw->total_backoffs_applied();

        // Terminal ANSI color formatting
        std::string mode_tag = mdacc ? "\033[1;32m[MD-ACC ENABLED]\033[0m" : "\033[1;31m[BASELINE 2PL/OCC]\033[0m";
        std::string score_color = (lvl == ContentionLevel::HIGH) ? "\033[1;31m" : ((lvl == ContentionLevel::MEDIUM) ? "\033[1;33m" : "\033[1;32m");

        std::cout << "[T+" << std::setw(2) << counter << "s] "
                  << "Mode: " << mode_tag << " | "
                  << "Score: " << score_color << std::setw(6) << lvl_str << "\033[0m | "
                  << "Conflict: " << std::fixed << std::setprecision(1) << std::setw(4) << conflict_pct << "% | "
                  << "Abort Rate: " << std::fixed << std::setprecision(1) << std::setw(4) << abort_pct << "% (" << total_aborts << " abs) | "
                  << "Backoffs: " << std::setw(5) << total_backoffs << " | "
                  << "TPS: \033[1;36m" << std::fixed << std::setprecision(0) << std::setw(5) << tps << " tx/s\033[0m | "
                  << "Done: " << std::setw(6) << cur_completed << "/" << total_requests << "\n";

        // Periodically write state transitions
        if (counter % 10 == 0) {
            scheduler->export_transitions_to_json("state_transitions.json", 10000);
        }
    }

    std::cout << "\n[Shutdown] Stopping simulation and saving final state transitions...\n";
    g_running.store(false);
    svr.stop();

    if (sim_thread.joinable()) sim_thread.join();
    if (server_thread.joinable()) server_thread.join();

    scheduler->export_transitions_to_json("state_transitions.json", 50000);
    std::cout << "[Shutdown] Clean shutdown completed. State transitions saved to state_transitions.json.\n";

    return 0;
}
