#include "bank_os_api.hpp"
#include "scheduler.hpp"
#include "process_manager.hpp"
#include "lock_manager.hpp"
#include "db_manager.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace BankOS {

static std::string escapeJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\b') o << "\\b";
        else if (c == '\f') o << "\\f";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else if (c == '\t') o << "\\t";
        else o << c;
    }
    return o.str();
}

std::string BankOSApi::buildStateJson() {
    auto stats = Scheduler::getInstance().getStats();
    auto running_pid = Scheduler::getInstance().getCurrentRunningPID();
    auto ready_queue = Scheduler::getInstance().getReadyQueue();
    auto waiting_list = Scheduler::getInstance().getWaitingList();
    auto all_procs = ProcessManager::getInstance().getAllProcesses();
    auto lock_status = LockManager::getInstance().getLockStatus();
    auto balances = DBManager::getInstance().getAllBalances();
    auto gantt = Scheduler::getInstance().getGanttHistory();
    auto logs = Scheduler::getInstance().getRecentLogs(60);
    bool db_connected = DBManager::getInstance().isConnectedToMySQL();

    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    json << "{\n";

    // Stats
    json << "  \"stats\": {\n"
         << "    \"clock_tick\": " << stats.clock_tick << ",\n"
         << "    \"time_quantum\": " << stats.time_quantum << ",\n"
         << "    \"total_transactions\": " << stats.total_transactions << ",\n"
         << "    \"completed_transactions\": " << stats.completed_transactions << ",\n"
         << "    \"failed_transactions\": " << stats.failed_transactions << ",\n"
         << "    \"active_ready_count\": " << stats.active_ready_count << ",\n"
         << "    \"active_waiting_count\": " << stats.active_waiting_count << ",\n"
         << "    \"avg_waiting_time\": " << stats.avg_waiting_time << ",\n"
         << "    \"avg_turnaround_time\": " << stats.avg_turnaround_time << ",\n"
         << "    \"avg_response_time\": " << stats.avg_response_time << ",\n"
         << "    \"cpu_utilization\": " << stats.cpu_utilization << ",\n"
         << "    \"throughput\": " << stats.throughput << ",\n"
         << "    \"is_running\": " << (Scheduler::getInstance().isRunningContinuous() ? "true" : "false") << "\n"
         << "  },\n";

    // CPU state
    json << "  \"cpu\": {\n"
         << "    \"is_active\": " << (!running_pid.empty() ? "true" : "false") << ",\n"
         << "    \"pid\": \"" << escapeJson(running_pid) << "\",\n"
         << "    \"slice_remaining\": " << Scheduler::getInstance().getRemainingQuantumSlice() << ",\n";
    if (!running_pid.empty()) {
        auto running_pcb = ProcessManager::getInstance().getProcess(running_pid);
        if (running_pcb) {
            json << "    \"type\": \"" << txTypeToString(running_pcb->tx_type) << "\",\n"
                 << "    \"source\": \"" << escapeJson(running_pcb->source_account) << "\",\n"
                 << "    \"destination\": \"" << escapeJson(running_pcb->destination_account) << "\",\n"
                 << "    \"amount\": " << running_pcb->amount << ",\n"
                 << "    \"burst\": " << running_pcb->burst_time << ",\n"
                 << "    \"remaining\": " << running_pcb->remaining_burst << ",\n"
                 << "    \"status\": \"" << escapeJson(running_pcb->status_message) << "\"\n";
        } else {
            json << "    \"type\": \"IDLE\"\n";
        }
    } else {
        json << "    \"type\": \"IDLE\"\n";
    }
    json << "  },\n";

    // Ready Queue
    json << "  \"ready_queue\": [\n";
    for (size_t i = 0; i < ready_queue.size(); ++i) {
        auto pcb = ProcessManager::getInstance().getProcess(ready_queue[i]);
        json << "    {\n"
             << "      \"pid\": \"" << ready_queue[i] << "\"";
        if (pcb) {
            json << ",\n      \"type\": \"" << txTypeToString(pcb->tx_type) << "\",\n"
                 << "      \"remaining\": " << pcb->remaining_burst << ",\n"
                 << "      \"amount\": " << pcb->amount << "\n";
        } else {
            json << "\n";
        }
        json << "    }" << (i + 1 < ready_queue.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    // Waiting List
    json << "  \"waiting_list\": [\n";
    for (size_t i = 0; i < waiting_list.size(); ++i) {
        auto pcb = ProcessManager::getInstance().getProcess(waiting_list[i]);
        json << "    {\n"
             << "      \"pid\": \"" << waiting_list[i] << "\"";
        if (pcb) {
            json << ",\n      \"blocked_on\": \"" << escapeJson(pcb->blocked_on_resource) << "\",\n"
                 << "      \"reason\": \"" << escapeJson(pcb->status_message) << "\"\n";
        } else {
            json << "\n";
        }
        json << "    }" << (i + 1 < waiting_list.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    // Process Table (PCB List)
    json << "  \"process_table\": [\n";
    for (size_t i = 0; i < all_procs.size(); ++i) {
        const auto& p = all_procs[i];
        json << "    {\n"
             << "      \"pid\": \"" << p->pid << "\",\n"
             << "      \"tx_id\": \"" << p->transaction_id << "\",\n"
             << "      \"type\": \"" << txTypeToString(p->tx_type) << "\",\n"
             << "      \"from\": \"" << escapeJson(p->source_account) << "\",\n"
             << "      \"to\": \"" << escapeJson(p->destination_account) << "\",\n"
             << "      \"amount\": " << p->amount << ",\n"
             << "      \"state\": \"" << stateToString(p->state) << "\",\n"
             << "      \"arrival\": " << p->arrival_time << ",\n"
             << "      \"burst\": " << p->burst_time << ",\n"
             << "      \"remaining\": " << p->remaining_burst << ",\n"
             << "      \"cpu\": " << p->total_cpu_used << ",\n"
             << "      \"waiting_time\": " << p->waiting_time << ",\n"
             << "      \"turnaround_time\": " << p->turnaround_time << ",\n"
             << "      \"response_time\": " << p->response_time << ",\n"
             << "      \"status\": \"" << escapeJson(p->status_message) << "\"\n"
             << "    }" << (i + 1 < all_procs.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    // Lock Status Matrix
    json << "  \"locks\": [\n";
    for (size_t i = 0; i < lock_status.size(); ++i) {
        json << "    {\n"
             << "      \"resource\": \"" << lock_status[i].resource_id << "\",\n"
             << "      \"is_locked\": " << (lock_status[i].is_locked ? "true" : "false") << ",\n"
             << "      \"owner\": \"" << escapeJson(lock_status[i].owner_pid) << "\"\n"
             << "    }" << (i + 1 < lock_status.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    // Account Balances
    json << "  \"balances\": {\n";
    size_t b_idx = 0;
    for (const auto& [acc, bal] : balances) {
        json << "    \"" << acc << "\": " << bal
             << (++b_idx < balances.size() ? ",\n" : "\n");
    }
    json << "  },\n";

    // Gantt Chart History
    json << "  \"gantt\": [\n";
    for (size_t i = 0; i < gantt.size(); ++i) {
        json << "    {\n"
             << "      \"start\": " << gantt[i].start_tick << ",\n"
             << "      \"end\": " << gantt[i].end_tick << ",\n"
             << "      \"pid\": \"" << gantt[i].pid << "\",\n"
             << "      \"type\": \"" << gantt[i].tx_type << "\"\n"
             << "    }" << (i + 1 < gantt.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    // Event Logs
    json << "  \"logs\": [\n";
    for (size_t i = 0; i < logs.size(); ++i) {
        json << "    {\n"
             << "      \"tick\": " << logs[i].tick << ",\n"
             << "      \"pid\": \"" << escapeJson(logs[i].pid) << "\",\n"
             << "      \"msg\": \"" << escapeJson(logs[i].message) << "\",\n"
             << "      \"type\": \"" << logs[i].type << "\"\n"
             << "    }" << (i + 1 < logs.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    json << "  \"db_connected\": " << (db_connected ? "true" : "false") << "\n";
    json << "}\n";

    return json.str();
}

std::string BankOSApi::buildComparisonDemoJson() {
    std::ostringstream json;
    json << "{\n"
         << "  \"scenario\": \"Concurrent Withdrawal Overdraft Test\",\n"
         << "  \"initial_balance\": 50000,\n"
         << "  \"transaction_1\": {\"pid\": \"T1\", \"type\": \"WITHDRAW\", \"amount\": 40000},\n"
         << "  \"transaction_2\": {\"pid\": \"T2\", \"type\": \"WITHDRAW\", \"amount\": 30000},\n"
         << "  \"normal_banking\": {\n"
         << "    \"description\": \"Naive concurrent requests without OS process scheduling & mutual exclusion locks.\",\n"
         << "    \"t1_observed_balance\": 50000,\n"
         << "    \"t2_observed_balance\": 50000,\n"
         << "    \"result\": \"CRITICAL RACE CONDITION! Both succeed. Total withdrawn: ₹70,000 from ₹50,000.\",\n"
         << "    \"final_balance\": -20000,\n"
         << "    \"status\": \"CORRUPTED_INCONSISTENT\"\n"
         << "  },\n"
         << "  \"bank_os\": {\n"
         << "    \"description\": \"OS Process Control: Ready Queue -> Round Robin CPU -> Account Mutex -> Commit/Rollback.\",\n"
         << "    \"t1_flow\": \"T1 acquires mutex on A101 -> Checks 50,000 >= 40,000 -> Debits 40,000 -> Balance becomes 10,000 -> Releases Lock.\",\n"
         << "    \"t2_flow\": \"T2 attempts lock -> BLOCKED into WAITING queue until T1 finishes -> Wakes up -> Reads updated ₹10,000 -> Rejects ₹30,000 withdrawal (ROLLBACK).\",\n"
         << "    \"final_balance\": 10000,\n"
         << "    \"status\": \"CONSISTENT_SAFE\"\n"
         << "  }\n"
         << "}\n";
    return json.str();
}

void BankOSApi::registerRoutes(HttpServer& server) {
    // GET /api/state
    server.get("/api/state", [](const HttpRequest&) {
        return HttpResponse::json(BankOSApi::buildStateJson());
    });

    // POST /api/scheduler/step
    server.post("/api/scheduler/step", [](const HttpRequest&) {
        Scheduler::getInstance().step();
        return HttpResponse::json(BankOSApi::buildStateJson());
    });

    // POST /api/scheduler/run
    server.post("/api/scheduler/run", [](const HttpRequest&) {
        Scheduler::getInstance().startContinuous(600);
        return HttpResponse::json(BankOSApi::buildStateJson());
    });

    // POST /api/scheduler/pause
    server.post("/api/scheduler/pause", [](const HttpRequest&) {
        Scheduler::getInstance().stopContinuous();
        return HttpResponse::json(BankOSApi::buildStateJson());
    });

    // POST /api/scheduler/reset
    server.post("/api/scheduler/reset", [](const HttpRequest&) {
        Scheduler::getInstance().reset();
        return HttpResponse::json(BankOSApi::buildStateJson());
    });

    // POST /api/scheduler/quantum?q=3
    server.post("/api/scheduler/quantum", [](const HttpRequest& req) {
        auto it = req.params.find("q");
        if (it != req.params.end()) {
            try {
                int q = std::stoi(it->second);
                Scheduler::getInstance().setTimeQuantum(q);
            } catch (...) {}
        }
        return HttpResponse::json(BankOSApi::buildStateJson());
    });

    // POST /api/transaction?type=Deposit&from=A101&to=A102&amount=5000
    server.post("/api/transaction", [](const HttpRequest& req) {
        std::string type_str = "Transfer";
        std::string from = "A101";
        std::string to = "A102";
        double amount = 5000.0;

        if (req.params.count("type")) type_str = req.params.at("type");
        if (req.params.count("from")) from = req.params.at("from");
        if (req.params.count("to")) to = req.params.at("to");
        if (req.params.count("amount")) {
            try { amount = std::stod(req.params.at("amount")); } catch (...) {}
        }

        TransactionType t = stringToTxType(type_str);
        auto pcb = ProcessManager::getInstance().createProcess(
            t, 
            (t == TransactionType::DEPOSIT ? "" : from),
            (t == TransactionType::WITHDRAW ? "" : to),
            amount);
        Scheduler::getInstance().enqueueProcess(pcb->pid);

        return HttpResponse::json(BankOSApi::buildStateJson());
    });

    // POST /api/batch?count=10
    server.post("/api/batch", [](const HttpRequest& req) {
        int count = 10;
        if (req.params.count("count")) {
            try { count = std::stoi(req.params.at("count")); } catch (...) {}
        }
        Scheduler::getInstance().generateBatchWorkload(count);
        return HttpResponse::json(BankOSApi::buildStateJson());
    });

    // GET /api/compare
    server.get("/api/compare", [](const HttpRequest&) {
        return HttpResponse::json(BankOSApi::buildComparisonDemoJson());
    });
}

} // namespace BankOS
