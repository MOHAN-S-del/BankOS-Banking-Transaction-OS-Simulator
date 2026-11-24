// ============================================================================
// BankOS Middleware: OS-Driven Concurrent Banking Transaction Middleware
// File: pcb.hpp
//
// Module 1: Data Structures, Process Control Block (PCB), & Auto Burst Engine
//
// Core Responsibilities:
// 1. ProcessState enumeration (NEW, READY, RUNNING, TERMINATED)
// 2. Struct Process (PCB): Models every banking transaction as an OS process
// 3. Struct StateTransition: Audit trail record for OS PCB lifecycle events
// 4. Struct QueueTelemetry: Real-time telemetry per queue level
// 5. assignAutoBurstTime(): Automatic CPU burst weight calculator (No manual input)
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace bankos {

// ----------------------------------------------------------------------------
// Process Lifecycle States
// ----------------------------------------------------------------------------
enum class ProcessState {
    NEW,        // Transaction admitted to system; PCB allocated in memory
    READY,      // Waiting in one of the MLFQ ready queues (Q1, Q2, or Q3)
    RUNNING,    // Actively executing CPU burst slice on Core 0
    TERMINATED  // Execution finished; ready for atomic MySQL commit
};

inline std::string to_string(ProcessState s) {
    switch (s) {
        case ProcessState::NEW:        return "NEW";
        case ProcessState::READY:      return "READY";
        case ProcessState::RUNNING:    return "RUNNING";
        case ProcessState::TERMINATED: return "TERMINATED";
        default:                       return "UNKNOWN";
    }
}

// ----------------------------------------------------------------------------
// Timestamp Helper Utility (ISO YYYY-MM-DD HH:MM:SS)
// ----------------------------------------------------------------------------
inline std::string get_iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now_c);
#else
    localtime_r(&now_c, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ----------------------------------------------------------------------------
// State Transition Audit Record
// Captures full OS lifecycle transitions (NEW -> READY -> RUNNING -> TERMINATED)
// ----------------------------------------------------------------------------
struct StateTransition {
    std::string txn_id;
    std::string from_state;
    std::string to_state;
    std::string queue_level;
    std::string event_note;
    std::string logged_at;

    std::string to_json() const {
        std::ostringstream ss;
        ss << "{"
           << "\"txn_id\":\"" << txn_id << "\","
           << "\"from_state\":\"" << from_state << "\","
           << "\"to_state\":\"" << to_state << "\","
           << "\"queue_level\":\"" << queue_level << "\","
           << "\"event_note\":\"" << event_note << "\","
           << "\"logged_at\":\"" << logged_at << "\""
           << "}";
        return ss.str();
    }
};

// ----------------------------------------------------------------------------
// Process Control Block (PCB)
// Represents an incoming financial transaction modeled as an OS Process
// ----------------------------------------------------------------------------
struct Process {
    std::string txn_id;                // Unique Process / Transaction Identifier
    std::string sender_id;             // Sender Account ID (FK to accounts)
    std::string receiver_id;           // Receiver Account ID (FK to accounts)
    std::string txn_type;              // ATM, UPI, PAYROLL, STANDARD
    double amount = 0.0;               // Transaction amount in INR (₹)
    
    // CPU Burst & Execution Attributes
    int auto_burst_time = 1;           // Total required CPU burst in seconds
    int remaining_burst = 1;           // Remaining burst seconds needed
    int current_quantum_used = 0;      // Seconds spent in current queue slice
    
    // Queue Level Placement & Scheduling State
    std::string current_queue = "Q1_PRIORITY"; // Q1_PRIORITY, Q2_SJF, Q3_FCFS
    std::string initial_queue = "Q1_PRIORITY";
    std::string status = "READY";      // String representation for frontend
    ProcessState state = ProcessState::READY;
    
    // Priority & Starvation Prevention Metrics
    int priority_score = 10;           // Dynamic priority score (Higher = more urgent)
    int wait_time_sec = 0;             // Total seconds spent waiting in ready state
    int time_in_current_queue = 0;     // Seconds spent in current queue level (for aging)
    int progress_pct = 0;              // Progress 0% to 100%
    
    // Test Tracking Flag (Highlights manual injections in UI)
    bool is_test = false;              // True if manually injected via test panel
    
    std::string created_at;            // ISO Arrival Timestamp
    std::chrono::steady_clock::time_point arrival_tp; // High-precision arrival clock

    // Serializes PCB to JSON for server.py IPC bridge
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
           << "\"current_quantum_used\":" << current_quantum_used << ","
           << "\"assigned_queue\":\"" << current_queue << "\","
           << "\"queue_level\":\"" << current_queue << "\","
           << "\"status\":\"" << status << "\","
           << "\"is_test\":" << (is_test ? "true" : "false") << ","
           << "\"priority_score\":" << priority_score << ","
           << "\"wait_time_sec\":" << wait_time_sec << ","
           << "\"time_in_current_queue\":" << time_in_current_queue << ","
           << "\"progress_pct\":" << progress_pct << ","
           << "\"created_at\":\"" << created_at << "\""
           << "}";
        return ss.str();
    }
};

// ----------------------------------------------------------------------------
// Queue Telemetry Data
// Real-time metrics for scheduler_metrics table
// ----------------------------------------------------------------------------
struct QueueTelemetry {
    std::string queue_level;
    int total_processed = 0;
    double total_wait_time = 0.0;
    double total_turnaround_time = 0.0;
    double cpu_utilization_pct = 0.0;
    int total_cpu_busy_seconds = 0;

    double get_avg_wait() const {
        return total_processed > 0 ? (total_wait_time / total_processed) : 0.0;
    }

    double get_avg_turnaround() const {
        return total_processed > 0 ? (total_turnaround_time / total_processed) : 0.0;
    }

    std::string to_json() const {
        std::ostringstream ss;
        ss << "{"
           << "\"queue_level\":\"" << queue_level << "\","
           << "\"total_processed\":" << total_processed << ","
           << "\"avg_wait_time\":" << std::fixed << std::setprecision(2) << get_avg_wait() << ","
           << "\"avg_turnaround_time\":" << std::fixed << std::setprecision(2) << get_avg_turnaround() << ","
           << "\"cpu_utilization_pct\":" << std::fixed << std::setprecision(2) << cpu_utilization_pct
           << "}";
        return ss.str();
    }
};

// ----------------------------------------------------------------------------
// AUTOMATIC BURST TIME CALCULATOR
//
// In an OS, processes do not specify their own execution duration arbitrarily.
// Instead, the CPU scheduler estimates or derives burst time from process
// complexity, IO patterns, and operational weight:
//
// 1. ATM Cash Withdrawal (1s):
//    Lightweight single-account cash debit verification and cash dispenser IO.
//
// 2. UPI Peer Transfer (2s):
//    Fast cryptographic signature verification, mobile device token check,
//    and immediate double-entry ledger lock.
//
// 3. STANDARD Bank Transfer (3s or 5s):
//    - For amounts <= ₹10,000: Standard account transfer audit (3s).
//    - For amounts > ₹10,000: Extended compliance, AML check & limit validation (5s).
//
// 4. PAYROLL Corporate Batch (7s):
//    Heavy bulk employee compensation computation, tax withholding calculation,
//    and multi-ledger institutional disbursement verification.
// ----------------------------------------------------------------------------
inline int assignAutoBurstTime(const std::string& type, double amount) {
    if (type == "ATM") {
        return 1;
    }
    if (type == "UPI") {
        return 2;
    }
    if (type == "STANDARD") {
        return (amount <= 10000.0) ? 3 : 5;
    }
    if (type == "PAYROLL") {
        return 7;
    }
    return 3; // Default fallback
}

// ----------------------------------------------------------------------------
// Initial Queue Placement
// ----------------------------------------------------------------------------
inline std::string determineInitialQueue(const std::string& type, int burst_time) {
    if (type == "ATM" || type == "UPI") {
        return "Q1_PRIORITY";
    }
    if (type == "STANDARD" && burst_time <= 3) {
        return "Q2_SJF";
    }
    return "Q3_FCFS";
}

// ----------------------------------------------------------------------------
// Initial Priority Score
// ----------------------------------------------------------------------------
inline int determineInitialPriority(const std::string& type, const std::string& queue) {
    if (type == "ATM") return 10;
    if (type == "UPI") return 8;
    if (queue == "Q2_SJF") return 5;
    return 2;
}

} // namespace bankos
