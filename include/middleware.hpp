#pragma once

#include "pcb.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <mutex>
#include <cmath>

namespace bank_sim {

/**
 * Composite Contention Level
 */
enum class ContentionLevel {
    LOW,
    MEDIUM,
    HIGH
};

inline std::string contentionLevelToString(ContentionLevel lvl) {
    switch (lvl) {
        case ContentionLevel::LOW: return "LOW";
        case ContentionLevel::MEDIUM: return "MEDIUM";
        case ContentionLevel::HIGH: return "HIGH";
        default: return "UNKNOWN";
    }
}

/**
 * Measurement-Driven Adaptive Concurrency Control (MD-ACC) Middleware
 *
 * Responsibilities:
 * 1. Intercepts processes before CPU execution / lock acquisition.
 * 2. Tracks real-time telemetry: conflict rate (account collisions) and current queue length.
 * 3. Calculates composite contention score (LOW, MEDIUM, HIGH) every 100 ticks.
 * 4. Implements Exponential Backoff: Delays conflicting processes when contention is HIGH.
 * 5. Comparison Mode Flag (MD-ACC vs Baseline 2PL/OCC):
 *    - Baseline (FALSE): Transactions hit DB directly without backoff -> intentional abort storms.
 *    - MD-ACC (TRUE): Dynamic throttling & exponential backoff -> low aborts & stabilized throughput.
 */
class Middleware {
public:
    Middleware(bool mdacc_enabled = true)
        : mdacc_enabled_(mdacc_enabled),
          contention_level_(ContentionLevel::LOW),
          window_attempts_(0),
          window_conflicts_(0),
          window_aborts_(0),
          conflict_rate_(0.0),
          abort_rate_(0.0),
          current_queue_length_(0),
          base_backoff_ticks_(2),
          max_backoff_ticks_(32),
          total_admitted_(0),
          total_conflicts_(0),
          total_aborts_(0),
          total_backoffs_applied_(0),
          last_calculated_tick_(0) {}

    // Comparison Mode Toggle Flag
    bool is_mdacc_enabled() const { return mdacc_enabled_; }
    void set_mdacc_enabled(bool enabled) { mdacc_enabled_ = enabled; }
    void toggle_mode() { mdacc_enabled_ = !mdacc_enabled_; }

    void reset() {
        std::lock_guard<std::mutex> lock(mtx_);
        contention_level_ = ContentionLevel::LOW;
        window_attempts_ = 0;
        window_conflicts_ = 0;
        window_aborts_ = 0;
        conflict_rate_ = 0.0;
        abort_rate_ = 0.0;
        current_queue_length_ = 0;
        total_admitted_ = 0;
        total_conflicts_ = 0;
        total_aborts_ = 0;
        total_backoffs_applied_ = 0;
        last_calculated_tick_ = 0;
    }

    /**
     * Intercepts a process before it executes.
     * @param pcb The process control block being intercepted.
     * @param has_conflict True if any required account lock is currently busy.
     * @param current_queue_len Current total ready/waiting queue length.
     * @return Backoff delay in ticks to enforce before re-entering Ready queue.
     */
    int intercept(ProcessControlBlock& pcb, bool has_conflict, int current_queue_len) {
        std::lock_guard<std::mutex> lock(mtx_);
        current_queue_length_ = current_queue_len;
        window_attempts_++;
        total_admitted_++;

        if (has_conflict) {
            window_conflicts_++;
            total_conflicts_++;
            pcb.retry_count++;

            if (!mdacc_enabled_) {
                // ===============================================================
                // BASELINE MODE (2PL / OCC):
                // Transactions hit the DB directly, ignoring exponential backoff.
                // High concurrency on shared accounts causes immediate aborts!
                // ===============================================================
                window_aborts_++;
                total_aborts_++;
                pcb.abort_count++;
                return 0; // No backoff delay; immediate retry/collision -> high aborts
            } else {
                // ===============================================================
                // MD-ACC MIDDLEWARE MODE:
                // If contention is HIGH, apply Exponential Backoff algorithm:
                // Backoff = Base * 2^retry_count (clamped) + deterministic jitter.
                // ===============================================================
                if (contention_level_ == ContentionLevel::HIGH) {
                    // Exponential backoff multiplier: 2^retry_count
                    int power = std::min(pcb.retry_count, 5); // Cap 2^5 = 32
                    int backoff = base_backoff_ticks_ * (1 << power);
                    // Add small jitter using PID to prevent thundering herd
                    backoff += (pcb.pid % 3);
                    backoff = std::min(backoff, max_backoff_ticks_);

                    pcb.backoff_remaining = backoff;
                    total_backoffs_applied_++;
                    return backoff;

                } else if (contention_level_ == ContentionLevel::MEDIUM) {
                    // Moderate backoff under medium contention
                    int backoff = 1 + (pcb.pid % 2);
                    pcb.backoff_remaining = backoff;
                    total_backoffs_applied_++;
                    return backoff;

                } else {
                    // LOW contention: no backoff needed
                    pcb.backoff_remaining = 0;
                    return 0;
                }
            }
        }

        return 0; // No conflict detected
    }

    /**
     * Ticking hook called by scheduler every tick.
     * Calculates the composite contention score every 100 ticks.
     */
    void tick(int current_tick, int current_queue_len) {
        std::lock_guard<std::mutex> lock(mtx_);
        current_queue_length_ = current_queue_len;

        // Requirement 3: Calculate composite contention score every 100 ticks
        if (current_tick > 0 && (current_tick - last_calculated_tick_) >= 100) {
            calculate_contention_score_unlocked(current_tick);
        }
    }

    /**
     * Calculate Composite Contention Score (LOW, MEDIUM, HIGH)
     */
    void calculate_contention_score_unlocked(int current_tick) {
        last_calculated_tick_ = current_tick;

        if (window_attempts_ > 0) {
            conflict_rate_ = static_cast<double>(window_conflicts_) / window_attempts_;
            abort_rate_ = static_cast<double>(window_aborts_) / window_attempts_;
        } else {
            conflict_rate_ = 0.0;
            abort_rate_ = 0.0;
        }

        // Composite Contention Score Evaluation:
        // Contention is HIGH if:
        //  - Conflict rate >= 40% (0.40) OR
        //  - Abort rate >= 25% (0.25) OR
        //  - Queue length >= 35 processes
        // Contention is MEDIUM if:
        //  - Conflict rate >= 15% (0.15) OR
        //  - Queue length >= 15 processes
        // Otherwise LOW.
        if (conflict_rate_ >= 0.40 || abort_rate_ >= 0.25 || current_queue_length_ >= 35) {
            contention_level_ = ContentionLevel::HIGH;
        } else if (conflict_rate_ >= 0.15 || current_queue_length_ >= 15) {
            contention_level_ = ContentionLevel::MEDIUM;
        } else {
            contention_level_ = ContentionLevel::LOW;
        }

        // Reset rolling window sample counters for next 100 ticks
        window_attempts_ = 0;
        window_conflicts_ = 0;
        window_aborts_ = 0;
    }

    // Force calculation (used by tests or on demand)
    void force_calculate_score(int current_tick) {
        std::lock_guard<std::mutex> lock(mtx_);
        calculate_contention_score_unlocked(current_tick);
    }

    // Telemetry and statistics getters
    ContentionLevel contention_level() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return contention_level_;
    }

    double conflict_rate() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return conflict_rate_;
    }

    double abort_rate() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return abort_rate_;
    }

    int current_queue_length() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return current_queue_length_;
    }

    uint64_t total_aborts() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return total_aborts_;
    }

    uint64_t total_conflicts() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return total_conflicts_;
    }

    uint64_t total_backoffs_applied() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return total_backoffs_applied_;
    }

    std::string to_json() const {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ostringstream oss;
        oss << "{"
            << "\"mdacc_enabled\":" << (mdacc_enabled_ ? "true" : "false") << ","
            << "\"contention_level\":\"" << contentionLevelToString(contention_level_) << "\","
            << "\"conflict_rate\":" << conflict_rate_ << ","
            << "\"abort_rate\":" << abort_rate_ << ","
            << "\"queue_length\":" << current_queue_length_ << ","
            << "\"total_aborts\":" << total_aborts_ << ","
            << "\"total_conflicts\":" << total_conflicts_ << ","
            << "\"total_backoffs\":" << total_backoffs_applied_
            << "}";
        return oss.str();
    }

private:
    bool mdacc_enabled_;
    ContentionLevel contention_level_;

    // 100-tick Rolling Window Metrics
    uint64_t window_attempts_;
    uint64_t window_conflicts_;
    uint64_t window_aborts_;

    double conflict_rate_;
    double abort_rate_;
    int current_queue_length_;

    // Exponential Backoff Parameters
    int base_backoff_ticks_;
    int max_backoff_ticks_;

    // Global Aggregate Counters
    uint64_t total_admitted_;
    uint64_t total_conflicts_;
    uint64_t total_aborts_;
    uint64_t total_backoffs_applied_;
    int last_calculated_tick_;

    mutable std::mutex mtx_;
};

} // namespace bank_sim
