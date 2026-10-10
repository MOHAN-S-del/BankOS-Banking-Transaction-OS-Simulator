#include "db_manager.hpp"
#include "process_manager.hpp"
#include "lock_manager.hpp"
#include "scheduler.hpp"
#include "http_server.hpp"
#include "bank_os_api.hpp"

#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

std::atomic<bool> g_shutdown_requested(false);

void handleSigint(int) {
    std::cout << "\n[BankOS] Shutting down simulation engine cleanly...\n";
    g_shutdown_requested = true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handleSigint);
    std::signal(SIGTERM, handleSigint);

    int port = 8080;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {}
    }

    std::cout << "========================================================================\n";
    std::cout << "      BANK OS: INTEGRATED FUND & TRANSACTION MANAGEMENT SYSTEM         \n";
    std::cout << "        Operating System & Relational Database Simulation Engine       \n";
    std::cout << "========================================================================\n";

    // 1. Initialize Database Layer
    std::cout << "[Init] Initializing Database Subsystem...\n";
    // Attempt connection with default root / localhost or environment
    BankOS::DBManager::getInstance().initialize("127.0.0.1", "root", "", "bank_os_db", 3306);

    // 2. Load Initial Default Transactions (T1 through T5)
    std::cout << "[Init] Initializing Seed Workload (T01 - T05)...\n";
    struct SeedTx {
        BankOS::TransactionType type;
        std::string from;
        std::string to;
        double amount;
        int burst;
    };

    std::vector<SeedTx> seed = {
        {BankOS::TransactionType::WITHDRAW, "A101", "",     10000.0, 4},
        {BankOS::TransactionType::DEPOSIT,  "",     "A102", 20000.0, 3},
        {BankOS::TransactionType::TRANSFER, "A101", "A102",  5000.0, 4},
        {BankOS::TransactionType::WITHDRAW, "A101", "",     15000.0, 3},
        {BankOS::TransactionType::TRANSFER, "A102", "A103",  8000.0, 5}
    };

    for (const auto& item : seed) {
        auto pcb = BankOS::ProcessManager::getInstance().createProcess(
            item.type, item.from, item.to, item.amount, item.burst);
        BankOS::Scheduler::getInstance().enqueueProcess(pcb->pid);
    }

    // 3. Start Embedded C++ Web Server
    BankOS::HttpServer server(port);
    server.setStaticDir("web");
    BankOS::BankOSApi::registerRoutes(server);

    if (!server.start()) {
        std::cerr << "[Error] Failed to start HTTP server on port " << port << "\n";
        return 1;
    }

    std::cout << "\n>>> DASHBOARD READY: Open http://localhost:" << port << " in your web browser.\n";
    std::cout << ">>> Press Ctrl+C in terminal to stop.\n\n";

    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    server.stop();
    BankOS::Scheduler::getInstance().stopContinuous();
    BankOS::DBManager::getInstance().shutdown();

    std::cout << "[BankOS] Engine stopped successfully.\n";
    return 0;
}
