#include "process_manager.hpp"
#include "db_manager.hpp"
#include <iomanip>
#include <sstream>
#include <random>

namespace BankOS {

ProcessManager& ProcessManager::getInstance() {
    static ProcessManager instance;
    return instance;
}

ProcessManager::ProcessManager() : m_pid_counter(1) {}

std::string ProcessManager::generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dis(0, 15);
    const char* hex = "0123456789abcdef";

    std::string uuid = "tx_";
    for (int i = 0; i < 8; ++i) uuid += hex[dis(gen)];
    uuid += "_";
    for (int i = 0; i < 4; ++i) uuid += hex[dis(gen)];
    return uuid;
}

int ProcessManager::estimateBurstTime(TransactionType type) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    switch (type) {
        case TransactionType::DEPOSIT: {
            // Deposits need validation and single account credit: 2 to 3 ticks
            std::uniform_int_distribution<int> dis(2, 3);
            return dis(gen);
        }
        case TransactionType::WITHDRAW: {
            // Withdrawals need balance verification, debit, lock check: 3 to 4 ticks
            std::uniform_int_distribution<int> dis(3, 4);
            return dis(gen);
        }
        case TransactionType::TRANSFER: {
            // Transfers need dual locks, dual balance updates, 2PC atomicity: 4 to 5 ticks
            std::uniform_int_distribution<int> dis(4, 5);
            return dis(gen);
        }
    }
    return 3;
}

std::shared_ptr<ProcessControlBlock> ProcessManager::createProcess(
    TransactionType type,
    const std::string& from_account,
    const std::string& to_account,
    double amount,
    int custom_burst) 
{
    std::lock_guard<std::mutex> lock(m_mutex);

    int id_num = m_pid_counter.fetch_add(1);
    std::ostringstream pid_stream;
    pid_stream << "T" << std::setw(2) << std::setfill('0') << id_num;
    std::string pid = pid_stream.str();

    auto pcb = std::make_shared<ProcessControlBlock>();
    pcb->pid = pid;
    pcb->transaction_id = generateUUID();
    pcb->tx_type = type;
    pcb->source_account = from_account;
    pcb->destination_account = to_account;
    pcb->amount = amount;
    pcb->state = ProcessState::READY; // Placed into READY state immediately
    pcb->execution_stage = 0;
    
    int burst = (custom_burst > 0) ? custom_burst : estimateBurstTime(type);
    pcb->burst_time = burst;
    pcb->remaining_burst = burst;
    pcb->arrival_time = 0; // Updated when inserted into scheduler at current clock tick
    pcb->status_message = "PCB allocated; Enqueued into Ready Queue";

    m_process_table[pid] = pcb;
    m_creation_order.push_back(pid);

    // Record initial transaction in DB layer
    DBManager::getInstance().recordTransaction(
        pcb->transaction_id, from_account, to_account, amount, 
        txTypeToString(type), "PENDING");
    
    DBManager::getInstance().syncPCB(*pcb);

    return pcb;
}

std::shared_ptr<ProcessControlBlock> ProcessManager::getProcess(const std::string& pid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_process_table.find(pid);
    if (it != m_process_table.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ProcessControlBlock>> ProcessManager::getAllProcesses() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<ProcessControlBlock>> list;
    for (const auto& pid : m_creation_order) {
        auto it = m_process_table.find(pid);
        if (it != m_process_table.end()) {
            list.push_back(it->second);
        }
    }
    return list;
}

void ProcessManager::updateProcessState(const std::string& pid, ProcessState new_state, const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_process_table.find(pid);
    if (it != m_process_table.end()) {
        it->second->state = new_state;
        if (!reason.empty()) {
            it->second->status_message = reason;
        }
        DBManager::getInstance().syncPCB(*(it->second));
    }
}

void ProcessManager::recordCompletion(const std::string& pid, int clock_tick, bool committed, const std::string& status_msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_process_table.find(pid);
    if (it != m_process_table.end()) {
        auto pcb = it->second;
        pcb->state = ProcessState::TERMINATED;
        pcb->completion_time = clock_tick;
        pcb->was_committed = committed;
        pcb->status_message = status_msg;
        
        // Finalize OS Metrics formulas:
        pcb->turnaround_time = pcb->completion_time - pcb->arrival_time;
        // Waiting time = Turnaround Time - Burst Time (standard OS formula)
        pcb->waiting_time = std::max(0, pcb->turnaround_time - pcb->burst_time);
        if (pcb->first_run_time >= 0) {
            pcb->response_time = pcb->first_run_time - pcb->arrival_time;
        }

        DBManager::getInstance().syncPCB(*pcb);
        DBManager::getInstance().recordTransaction(
            pcb->transaction_id, pcb->source_account, pcb->destination_account, 
            pcb->amount, txTypeToString(pcb->tx_type), committed ? "COMMITTED" : "ROLLED_BACK");
    }
}

void ProcessManager::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_process_table.clear();
    m_creation_order.clear();
    m_pid_counter = 1;
}

size_t ProcessManager::getProcessCount() const {
    return m_process_table.size();
}

} // namespace BankOS
