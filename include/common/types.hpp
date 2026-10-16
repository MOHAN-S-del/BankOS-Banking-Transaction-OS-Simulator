#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace adaptive_os {

using AccountId = uint64_t;
using TxId = uint64_t;
using BalanceType = int64_t; // In cents / base financial units (avoid floating point errors)
using HighResClock = std::chrono::steady_clock;
using TimePoint = HighResClock::time_point;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

enum class AccountType : uint8_t {
    RETAIL = 0,
    CORPORATE = 1,
    CENTRAL_SETTLEMENT = 2,
    ESCROW = 3
};

inline const char* accountTypeToString(AccountType type) {
    switch (type) {
        case AccountType::RETAIL: return "RETAIL";
        case AccountType::CORPORATE: return "CORPORATE";
        case AccountType::CENTRAL_SETTLEMENT: return "CENTRAL_SETTLEMENT";
        case AccountType::ESCROW: return "ESCROW";
        default: return "UNKNOWN";
    }
}

enum class TxType : uint8_t {
    RETAIL_P2P = 0,
    BATCH_PAYROLL = 1,
    CENTRAL_SETTLEMENT = 2,
    BALANCE_LOOKUP = 3
};

inline const char* txTypeToString(TxType type) {
    switch (type) {
        case TxType::RETAIL_P2P: return "RETAIL_P2P";
        case TxType::BATCH_PAYROLL: return "BATCH_PAYROLL";
        case TxType::CENTRAL_SETTLEMENT: return "CENTRAL_SETTLEMENT";
        case TxType::BALANCE_LOOKUP: return "BALANCE_LOOKUP";
        default: return "UNKNOWN";
    }
}

enum class TxStatus : uint8_t {
    PENDING = 0,
    ADMITTED = 1,
    IN_FLIGHT = 2,
    COMMITTED = 3,
    ABORTED_LOCK_TIMEOUT = 4,
    ABORTED_DEADLOCK = 5,
    ABORTED_INSUFFICIENT_FUNDS = 6,
    ABORTED_FATAL = 7,
    DROPPED_OVERLOAD = 8
};

inline const char* txStatusToString(TxStatus status) {
    switch (status) {
        case TxStatus::PENDING: return "PENDING";
        case TxStatus::ADMITTED: return "ADMITTED";
        case TxStatus::IN_FLIGHT: return "IN_FLIGHT";
        case TxStatus::COMMITTED: return "COMMITTED";
        case TxStatus::ABORTED_LOCK_TIMEOUT: return "ABORTED_LOCK_TIMEOUT";
        case TxStatus::ABORTED_DEADLOCK: return "ABORTED_DEADLOCK";
        case TxStatus::ABORTED_INSUFFICIENT_FUNDS: return "ABORTED_INSUFFICIENT_FUNDS";
        case TxStatus::ABORTED_FATAL: return "ABORTED_FATAL";
        case TxStatus::DROPPED_OVERLOAD: return "DROPPED_OVERLOAD";
        default: return "UNKNOWN";
    }
}

enum class QueueTier : uint8_t {
    Q1_HIGH_INTERACTIVE = 0, // 10ms quantum, SRTF
    Q2_MED_STANDARD = 1,     // 50ms quantum, FIFO
    Q3_LOW_BATCH = 2         // 200ms quantum, FIFO
};

inline const char* queueTierToString(QueueTier tier) {
    switch (tier) {
        case QueueTier::Q1_HIGH_INTERACTIVE: return "Q1_INTERACTIVE(10ms)";
        case QueueTier::Q2_MED_STANDARD: return "Q2_STANDARD(50ms)";
        case QueueTier::Q3_LOW_BATCH: return "Q3_BATCH(200ms)";
        default: return "UNKNOWN";
    }
}

enum class HysteresisState : uint8_t {
    NORMAL = 0,
    ELEVATED = 1,
    HIGH_CONTENTION = 2
};

inline const char* hysteresisStateToString(HysteresisState state) {
    switch (state) {
        case HysteresisState::NORMAL: return "NORMAL";
        case HysteresisState::ELEVATED: return "ELEVATED";
        case HysteresisState::HIGH_CONTENTION: return "HIGH_CONTENTION";
        default: return "UNKNOWN";
    }
}

enum class ExecutionMode : uint8_t {
    BASELINE_2PL = 0,      // Unregulated 2PL + standard FIFO thread pool
    ADAPTIVE_LEDGER_OS = 1 // MLFQ + SRTF + MD-ACC Dynamic Concurrency Regulation
};

// Operation within a transaction
enum class OpKind : uint8_t {
    DEBIT = 0,
    CREDIT = 1,
    READ_BALANCE = 2
};

struct LedgerOperation {
    AccountId account_id;
    OpKind kind;
    BalanceType amount; // 0 for READ_BALANCE
    std::string memo;
};

// Telemetry Snapshot
struct TelemetrySnapshot {
    TimePoint timestamp;
    double conflict_rate{0.0};       // CR_hat
    double abort_rate{0.0};          // AR_hat
    double lock_queue_depth{0.0};    // QL_hat
    double retry_intensity{0.0};     // RI_hat
    double composite_index{0.0};     // C_t
    HysteresisState state{HysteresisState::NORMAL};
    double effective_admission_rate{10000.0}; // lambda_eff
    uint64_t admitted_tx_count{0};
    uint64_t committed_tx_count{0};
    uint64_t aborted_tx_count{0};
    uint64_t conflict_count{0};
    uint64_t retry_count{0};
    uint64_t q1_depth{0};
    uint64_t q2_depth{0};
    uint64_t q3_depth{0};
    uint64_t active_workers{0};
    double avg_latency_ms{0.0};
    double p95_latency_ms{0.0};
    double p99_latency_ms{0.0};
    double throughput_tps{0.0};
    uint64_t starvation_migrations{0};
    uint64_t hotspot_demotions{0};

    // Predictive Machine Learning & Proactive MLFQ Quotas
    double predicted_contention{0.0};
    double forecast_horizon_ms{30.0};
    std::string proactive_policy_mode{"BASELINE_STABLE"};
    std::string anticipated_regime{"NORMAL_RETAIL"};
    uint64_t proactive_interventions{0};
    double q1_preallocated_quota{50.0};
    double q3_quantum_allocated_ms{200.0};
};

// Benchmark Summary Report
struct BenchmarkReport {
    ExecutionMode mode;
    std::string mode_name;
    double duration_seconds{0.0};
    uint64_t total_submitted{0};
    uint64_t total_committed{0};
    uint64_t total_aborted{0};
    uint64_t total_retries{0};
    double throughput_tps{0.0};
    double abort_rate_percent{0.0};
    double p50_latency_ms{0.0};
    double p95_latency_ms{0.0};
    double p99_latency_ms{0.0};
    double max_latency_ms{0.0};
    double peak_contention_index{0.0};
    double avg_contention_index{0.0};
    uint64_t starvation_migrations{0};
    uint64_t hotspot_demotions{0};
    uint64_t context_switches_estimated{0};
    double retail_p95_latency_ms{0.0};
    double batch_p95_latency_ms{0.0};
    double settlement_p95_latency_ms{0.0};
};

} // namespace adaptive_os
