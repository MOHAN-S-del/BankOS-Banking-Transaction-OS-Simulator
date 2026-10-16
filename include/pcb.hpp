#pragma once

#include <string>
#include <vector>
#include <sstream>

namespace bank_sim {

/**
 * Transaction types in the banking switch
 */
enum class TransactionType {
    UPI,    // Retail instant payment (SRTF, high priority, 2 account locks)
    ATM,    // Cash withdrawal/deposit (SRTF, high priority, 1-2 account locks)
    SALARY  // Batch payroll corporate disbursement (Round Robin, low priority, multiple locks)
};

inline std::string transactionTypeToString(TransactionType type) {
    switch (type) {
        case TransactionType::UPI: return "UPI";
        case TransactionType::ATM: return "ATM";
        case TransactionType::SALARY: return "Salary";
        default: return "Unknown";
    }
}

inline TransactionType stringToTransactionType(const std::string& str) {
    if (str == "UPI") return TransactionType::UPI;
    if (str == "ATM") return TransactionType::ATM;
    return TransactionType::SALARY;
}

/**
 * Standard OS Process States
 */
enum class ProcessState {
    NEW,        // Created but not yet arrived or admitted into ready queues
    READY,      // Ready in MLFQ queue waiting for CPU dispatch
    RUNNING,    // Currently executing on CPU
    WAITING,    // Blocked waiting for required account lock acquisition
    TERMINATED  // Execution completed and locks released
};

inline std::string processStateToString(ProcessState state) {
    switch (state) {
        case ProcessState::NEW: return "NEW";
        case ProcessState::READY: return "READY";
        case ProcessState::RUNNING: return "RUNNING";
        case ProcessState::WAITING: return "WAITING";
        case ProcessState::TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

/**
 * Process Control Block (PCB)
 * Represents a banking transaction scheduled as an operating system process.
 */
struct ProcessControlBlock {
    // Required fields as per specification
    int pid{0};                                // Unique Process ID
    TransactionType type{TransactionType::UPI};// UPI, ATM, or Salary
    ProcessState state{ProcessState::NEW};     // Current process state
    int arrival_time{0};                       // Simulation tick when request arrives
    int burst_time{0};                         // Remaining CPU burst time (ticks)
    std::vector<int> required_locks{};         // Account IDs needed before execution

    // Supplementary scheduling and accounting metadata
    int total_burst_time{0};                   // Initial allocated burst time
    int wait_time{0};                          // Total ticks spent in READY or WAITING states
    int queue2_ticks{0};                       // Ticks spent in Queue 2 (used by Aging Mechanism)
    int current_queue{1};                      // 1 = Queue 1 (High/SRTF), 2 = Queue 2 (Low/RR)
    bool aged_promoted{false};                 // True if promoted from Q2 to Q1 via aging
    int start_time{-1};                        // Tick when first dispatched to CPU
    int completion_time{-1};                   // Tick when finished (TERMINATED)

    // MD-ACC Middleware and Exponential Backoff metadata
    int retry_count{0};                        // Number of retries/conflicts encountered
    int backoff_remaining{0};                  // Remaining ticks in exponential backoff wait-state
    int abort_count{0};                        // Number of times transaction aborted under baseline

    // Helper to format required locks as JSON array string
    std::string locks_to_json() const {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < required_locks.size(); ++i) {
            if (i > 0) oss << ",";
            oss << required_locks[i];
        }
        oss << "]";
        return oss.str();
    }

    // Serialize PCB to JSON string
    std::string to_json() const {
        std::ostringstream oss;
        oss << "{"
            << "\"pid\":" << pid << ","
            << "\"type\":\"" << transactionTypeToString(type) << "\","
            << "\"state\":\"" << processStateToString(state) << "\","
            << "\"arrival_time\":" << arrival_time << ","
            << "\"burst_time\":" << burst_time << ","
            << "\"total_burst_time\":" << total_burst_time << ","
            << "\"wait_time\":" << wait_time << ","
            << "\"queue2_ticks\":" << queue2_ticks << ","
            << "\"current_queue\":" << current_queue << ","
            << "\"aged_promoted\":" << (aged_promoted ? "true" : "false") << ","
            << "\"retry_count\":" << retry_count << ","
            << "\"backoff_remaining\":" << backoff_remaining << ","
            << "\"abort_count\":" << abort_count << ","
            << "\"start_time\":" << start_time << ","
            << "\"completion_time\":" << completion_time << ","
            << "\"required_locks\":" << locks_to_json()
            << "}";
        return oss.str();
    }
};

} // namespace bank_sim
