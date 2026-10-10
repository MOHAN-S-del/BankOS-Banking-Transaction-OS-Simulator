#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace BankOS {

enum class ProcessState {
    NEW,
    READY,
    RUNNING,
    WAITING,
    TERMINATED
};

enum class TransactionType {
    DEPOSIT,
    WITHDRAW,
    TRANSFER
};

inline std::string stateToString(ProcessState state) {
    switch (state) {
        case ProcessState::NEW: return "NEW";
        case ProcessState::READY: return "READY";
        case ProcessState::RUNNING: return "RUNNING";
        case ProcessState::WAITING: return "WAITING";
        case ProcessState::TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

inline ProcessState stringToState(const std::string& str) {
    if (str == "NEW") return ProcessState::NEW;
    if (str == "READY") return ProcessState::READY;
    if (str == "RUNNING") return ProcessState::RUNNING;
    if (str == "WAITING") return ProcessState::WAITING;
    if (str == "TERMINATED") return ProcessState::TERMINATED;
    return ProcessState::NEW;
}

inline std::string txTypeToString(TransactionType type) {
    switch (type) {
        case TransactionType::DEPOSIT: return "DEPOSIT";
        case TransactionType::WITHDRAW: return "WITHDRAW";
        case TransactionType::TRANSFER: return "TRANSFER";
        default: return "UNKNOWN";
    }
}

inline TransactionType stringToTxType(const std::string& str) {
    if (str == "DEPOSIT" || str == "Deposit") return TransactionType::DEPOSIT;
    if (str == "WITHDRAW" || str == "Withdraw") return TransactionType::WITHDRAW;
    return TransactionType::TRANSFER;
}

// Process Control Block (PCB) representing a banking transaction in the OS
struct ProcessControlBlock {
    // Process Identity
    std::string pid;                  // e.g. "T01", "T02"
    std::string transaction_id;       // UUID / DB Transaction ID
    TransactionType tx_type;          // DEPOSIT, WITHDRAW, TRANSFER
    std::string source_account;       // e.g. "A101" (for withdraw/transfer)
    std::string destination_account;  // e.g. "A102" (for deposit/transfer)
    double amount;                    // Currency amount in INR

    // OS Process State & Program Counter
    ProcessState state;
    int execution_stage;              // 0=Init, 1=Lock Acquired / Critical Section, 2=DB Ops, 3=Committed

    // CPU Burst & Timing Parameters (in discrete simulation ticks)
    int arrival_time;                 // Tick when process was created
    int burst_time;                   // Total CPU clock ticks needed
    int remaining_burst;              // Remaining CPU ticks to finish
    int first_run_time;               // Tick when process first was scheduled on CPU (-1 if never)
    int completion_time;              // Tick when process reached TERMINATED (-1 if not yet)

    // Derived Performance Metrics
    int waiting_time;                 // Total ticks spent in READY queue waiting for CPU
    int turnaround_time;              // completion_time - arrival_time
    int response_time;                // first_run_time - arrival_time
    int total_cpu_used;               // Actual ticks executed on CPU

    // Resource Management (Account Mutexes)
    std::vector<std::string> held_locks;
    std::string blocked_on_resource;  // Account ID that caused process to wait

    // Operational Status
    std::string status_message;
    bool was_committed;

    ProcessControlBlock()
        : pid(""),
          transaction_id(""),
          tx_type(TransactionType::DEPOSIT),
          source_account(""),
          destination_account(""),
          amount(0.0),
          state(ProcessState::NEW),
          execution_stage(0),
          arrival_time(0),
          burst_time(0),
          remaining_burst(0),
          first_run_time(-1),
          completion_time(-1),
          waiting_time(0),
          turnaround_time(0),
          response_time(-1),
          total_cpu_used(0),
          blocked_on_resource(""),
          status_message("Initialized"),
          was_committed(false) {}
};

} // namespace BankOS
