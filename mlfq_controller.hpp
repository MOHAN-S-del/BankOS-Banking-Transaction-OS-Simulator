// ============================================================================
// BankOS Middleware: OS-Driven Concurrent Banking Transaction Middleware
// File: mlfq_controller.hpp
//
// Module 3: Multi-Level Feedback Queue (MLFQ) Controller & Aging Engine
//
// Manages the feedback loops between the three priority tiers:
//
// 1. TOP-DOWN HIERARCHICAL DISPATCH:
//    - High Priority Queue 1 (Priority, q=2s) executes first.
//    - Medium Priority Queue 2 (SJF, q=4s) executes only when Q1 is empty.
//    - Low Priority Queue 3 (FCFS) executes only when Q1 and Q2 are both empty.
//
// 2. TIME-QUANTUM EXPIRATION & DEMOTION:
//    - If a process in Q1 exceeds its 2s time-quantum without finishing:
//      -> Preempted, demoted to Queue 2 (SJF), and time slice reset.
//    - If a process in Q2 exceeds its 4s time-quantum without finishing:
//      -> Preempted, demoted to Queue 3 (FCFS), and time slice reset.
//
// 3. DYNAMIC AGING STARVATION PREVENTION:
//    - Low-priority jobs in Queue 3 that wait >= 6s are automatically
//      promoted to Queue 2 (SJF) with event note "Aged Promotion".
//    - Jobs in Queue 2 that wait >= 5s are promoted to Queue 1 (Priority).
//
// 4. METRICS & STATE-TRANSITION AUDIT TRAIL:
//    - Logs every state change for MySQL synchronization (Table 7: transaction_logs).
//    - Computes real-time turnaround time (T_turnaround = T_completion - T_arrival)
//      and waiting time (T_wait = T_turnaround - T_burst) (Table 8: scheduler_metrics).
// ============================================================================

#pragma once

#include "pcb.hpp"
#include "queue_algorithms.hpp"

#include <mutex>
#include <deque>
#include <memory>
#include <fstream>
#include <iostream>
#include <cmath>

namespace bankos {

class MLFQController {
public:
    MLFQController() {
        // Initialize baseline metrics matching schema.sql seeds
        metrics_q1_.queue_level = "Q1_PRIORITY";
        metrics_q1_.total_processed = 2;
        metrics_q1_.total_wait_time = 0.90;
        metrics_q1_.total_turnaround_time = 3.90;
        metrics_q1_.starvation_rescued = 0;

        metrics_q2_.queue_level = "Q2_SJF";
        metrics_q2_.total_processed = 1;
        metrics_q2_.total_wait_time = 1.20;
        metrics_q2_.total_turnaround_time = 4.20;
        metrics_q2_.starvation_rescued = 0;

        metrics_q3_.queue_level = "Q3_FCFS";
        metrics_q3_.total_processed = 1;
        metrics_q3_.total_wait_time = 3.50;
        metrics_q3_.total_turnaround_time = 10.50;
        metrics_q3_.starvation_rescued = 1;

        starvation_prevented_count_ = 1;
    }

    // ------------------------------------------------------------------------
    // Admit New Transaction into MLFQ Architecture
    // ------------------------------------------------------------------------
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
        
        // Automatic Burst Weight Calculation (Module 1)
        p.auto_burst_time = assignAutoBurstTime(type, amount);
        p.remaining_burst = p.auto_burst_time;
        p.current_quantum_used = 0;
        
        // Initial Queue Assignment
        p.current_queue = determineInitialQueue(type, p.auto_burst_time);
        p.initial_queue = p.current_queue;
        p.status = "READY";
        p.state = ProcessState::READY;
        p.is_test = is_test;
        p.priority_score = determineInitialPriority(type, p.current_queue);
        p.wait_time_sec = 0;
        p.time_in_current_queue = 0;
        p.progress_pct = 0;
        p.created_at = get_iso_timestamp();
        p.arrival_tp = std::chrono::steady_clock::now();

        // Enqueue into appropriate algorithm tier
        if (p.current_queue == "Q1_PRIORITY") {
            queue_1_.push(p);
        } else if (p.current_queue == "Q2_SJF") {
            queue_2_.push(p);
        } else {
            queue_3_.push(p);
        }

        total_admitted_++;

        // Audit Trail: NEW -> READY
        log_transition_unlocked(p.txn_id, "NEW", "READY", p.current_queue,
            "Admitted to MLFQ " + p.current_queue + " (Auto burst: " + std::to_string(p.auto_burst_time) + "s)");

        return p;
    }

    // ------------------------------------------------------------------------
    // Dynamic Aging Engine (Starvation Prevention)
    // Called once per second by the aging background thread:
    // - Increments waiting times for all ready processes in Q1, Q2, and Q3.
    // - Promotes jobs waiting >= 6s in Q3 to Q2 (SJF).
    // - Promotes jobs waiting >= 5s in Q2 to Q1 (Priority).
    // ------------------------------------------------------------------------
    bool check_aging_and_update_wait() {
        std::lock_guard<std::mutex> lock(mtx_);
        bool state_changed = false;

        // Increment wait times and mark starvation status
        for (auto& p : queue_1_) {
            p.wait_time_sec++;
            p.time_in_current_queue++;
        }
        for (auto& p : queue_2_) {
            p.wait_time_sec++;
            p.time_in_current_queue++;
            p.is_starving = (p.wait_time_sec >= 4);
        }
        for (auto& p : queue_3_) {
            p.wait_time_sec++;
            p.time_in_current_queue++;
            p.is_starving = (p.wait_time_sec >= 4);
        }

        // Starvation Check: Q3 -> Q2 (Threshold: 6 seconds)
        std::vector<size_t> q3_promotions;
        for (size_t i = 0; i < queue_3_.size(); ++i) {
            if (queue_3_[i].time_in_current_queue >= 6) {
                q3_promotions.push_back(i);
            }
        }

        if (!q3_promotions.empty()) {
            for (auto it = q3_promotions.rbegin(); it != q3_promotions.rend(); ++it) {
                Process p = queue_3_.erase_at(*it);
                p.current_queue = "Q2_SJF";
                p.time_in_current_queue = 0;
                p.priority_score = 5;
                p.is_starving = false;
                p.was_aged_rescued = true;
                starvation_prevented_count_++;
                metrics_q3_.starvation_rescued++;
                queue_2_.push(p);

                rescued_history_.push_front(p);
                while (rescued_history_.size() > 30) {
                    rescued_history_.pop_back();
                }

                log_transition_unlocked(p.txn_id, "Q3_FCFS", "Q2_SJF", "Q2_SJF",
                    "AGED_PROMOTION: Dynamic Aging: Starvation threshold exceeded (6s wait in Q3); promoted to Queue 2 (SJF)");
                state_changed = true;
            }
        }

        // High Wait Check: Q2 -> Q1 (Threshold: 5 seconds)
        std::vector<size_t> q2_promotions;
        for (size_t i = 0; i < queue_2_.size(); ++i) {
            if (queue_2_[i].time_in_current_queue >= 5) {
                q2_promotions.push_back(i);
            }
        }

        if (!q2_promotions.empty()) {
            for (auto it = q2_promotions.rbegin(); it != q2_promotions.rend(); ++it) {
                Process p = queue_2_.erase_at(*it);
                p.current_queue = "Q1_PRIORITY";
                p.time_in_current_queue = 0;
                p.priority_score = 9;
                p.is_starving = false;
                p.was_aged_rescued = true;
                starvation_prevented_count_++;
                metrics_q2_.starvation_rescued++;
                queue_1_.push(p);

                rescued_history_.push_front(p);
                while (rescued_history_.size() > 30) {
                    rescued_history_.pop_back();
                }

                log_transition_unlocked(p.txn_id, "Q2_SJF", "Q1_PRIORITY", "Q1_PRIORITY",
                    "AGED_PROMOTION: Dynamic Aging: High wait threshold exceeded (5s wait in Q2); promoted to Queue 1 (Priority)");
                state_changed = true;
            }
        }

        return state_changed;
    }

    // ------------------------------------------------------------------------
    // CPU Execution Tick (Called every 250ms / 0.25s)
    // - Dispatches highest-priority ready process if Core 0 is idle.
    // - Decrements remaining burst and tracks time slice.
    // - Handles completion & demotion upon quantum expiry.
    // ------------------------------------------------------------------------
    bool tick_cpu() {
        std::lock_guard<std::mutex> lock(mtx_);

        if (active_process_) {
            static int sub_tick = 0;
            sub_tick++;
            if (sub_tick >= 4) { // 4 * 250ms = 1.0 full execution second
                sub_tick = 0;
                active_process_->remaining_burst = std::max(0, active_process_->remaining_burst - 1);
                active_process_->current_quantum_used++;

                int executed = active_process_->auto_burst_time - active_process_->remaining_burst;
                active_process_->progress_pct = (executed * 100) / active_process_->auto_burst_time;

                // 1. Process Finished Execution
                if (active_process_->remaining_burst <= 0) {
                    active_process_->status = "COMPLETED";
                    active_process_->state = ProcessState::TERMINATED;
                    active_process_->progress_pct = 100;

                    if (active_process_->was_aged_rescued) {
                        for (auto& r : rescued_history_) {
                            if (r.txn_id == active_process_->txn_id) {
                                r.status = "COMPLETED";
                                r.state = ProcessState::TERMINATED;
                                r.progress_pct = 100;
                                break;
                            }
                        }
                    }

                    double turnaround = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - active_process_->arrival_tp
                    ).count();
                    double wait = std::max(0.0, turnaround - active_process_->auto_burst_time);

                    // Update queue telemetry
                    if (active_process_->current_queue == "Q1_PRIORITY") {
                        metrics_q1_.total_processed++;
                        metrics_q1_.total_wait_time += wait;
                        metrics_q1_.total_turnaround_time += turnaround;
                    } else if (active_process_->current_queue == "Q2_SJF") {
                        metrics_q2_.total_processed++;
                        metrics_q2_.total_wait_time += wait;
                        metrics_q2_.total_turnaround_time += turnaround;
                    } else {
                        metrics_q3_.total_processed++;
                        metrics_q3_.total_wait_time += wait;
                        metrics_q3_.total_turnaround_time += turnaround;
                    }

                    log_transition_unlocked(active_process_->txn_id, "RUNNING", "TERMINATED", active_process_->current_queue,
                        "CPU execution completed (" + std::to_string(active_process_->auto_burst_time) + "s total); dispatched to MySQL for ACID commit");

                    completed_.push_front(*active_process_);
                    while (completed_.size() > 50) {
                        completed_.pop_back();
                    }
                    total_completed_++;

                    active_process_.reset();
                    return true;
                }

                // 2. Time-Quantum Expiration Check & Demotion
                bool demote = false;
                std::string target_queue;
                std::string reason;

                if (active_process_->current_queue == "Q1_PRIORITY" && active_process_->current_quantum_used >= QUANTUM_Q1) {
                    demote = true;
                    target_queue = "Q2_SJF";
                    reason = "Time quantum expired (2s in Q1); preempted & demoted to Queue 2 (SJF)";
                } else if (active_process_->current_queue == "Q2_SJF" && active_process_->current_quantum_used >= QUANTUM_Q2) {
                    demote = true;
                    target_queue = "Q3_FCFS";
                    reason = "Time quantum expired (4s in Q2); preempted & demoted to Queue 3 (FCFS)";
                }

                if (demote) {
                    Process demoted = *active_process_;
                    demoted.status = "READY";
                    demoted.state = ProcessState::READY;
                    demoted.current_queue = target_queue;
                    demoted.current_quantum_used = 0;
                    demoted.time_in_current_queue = 0;

                    if (target_queue == "Q2_SJF") {
                        demoted.priority_score = 5;
                        queue_2_.push(demoted);
                    } else {
                        demoted.priority_score = 2;
                        queue_3_.push(demoted);
                    }

                    log_transition_unlocked(demoted.txn_id, "RUNNING", "READY", target_queue, reason);

                    active_process_.reset();
                    return true;
                }

                return true;
            }
        } else {
            // Core 0 is idle; dispatch next top-down process
            return dispatch_next_process_unlocked();
        }

        return false;
    }

    // ------------------------------------------------------------------------
    // Hierarchical Top-Down Dispatcher (Q1 -> Q2 -> Q3)
    // ------------------------------------------------------------------------
    bool dispatch_next_process_unlocked() {
        if (active_process_) return false;

        // Level 1: Queue 1 (Priority Scheduling)
        if (!queue_1_.empty()) {
            active_process_ = std::make_unique<Process>(queue_1_.pop());
            active_process_->status = "RUNNING";
            active_process_->state = ProcessState::RUNNING;
            active_process_->current_quantum_used = 0;

            log_transition_unlocked(active_process_->txn_id, "READY", "RUNNING", "Q1_PRIORITY",
                "Dispatched to CPU Core 0 from Queue 1 (Priority, q = 2s)");
            return true;
        }

        // Level 2: Queue 2 (Shortest Job First)
        if (!queue_2_.empty()) {
            active_process_ = std::make_unique<Process>(queue_2_.pop());
            active_process_->status = "RUNNING";
            active_process_->state = ProcessState::RUNNING;
            active_process_->current_quantum_used = 0;

            log_transition_unlocked(active_process_->txn_id, "READY", "RUNNING", "Q2_SJF",
                "Dispatched to CPU Core 0 from Queue 2 (SJF, shortest remaining burst: " +
                std::to_string(active_process_->remaining_burst) + "s, q = 4s)");
            return true;
        }

        // Level 3: Queue 3 (First-Come, First-Served)
        if (!queue_3_.empty()) {
            active_process_ = std::make_unique<Process>(queue_3_.pop());
            active_process_->status = "RUNNING";
            active_process_->state = ProcessState::RUNNING;
            active_process_->current_quantum_used = 0;

            log_transition_unlocked(active_process_->txn_id, "READY", "RUNNING", "Q3_FCFS",
                "Dispatched to CPU Core 0 from Queue 3 (FCFS order, burst: " +
                std::to_string(active_process_->remaining_burst) + "s)");
            return true;
        }

        return false;
    }

    // ------------------------------------------------------------------------
    // JSON Telemetry Serialization
    // ------------------------------------------------------------------------
    std::string get_state_json() {
        std::lock_guard<std::mutex> lock(mtx_);
        return serialize_state_unlocked();
    }

    void reset_state() {
        std::lock_guard<std::mutex> lock(mtx_);
        active_process_.reset();
        queue_1_.clear();
        queue_2_.clear();
        queue_3_.clear();
        completed_.clear();
        transitions_.clear();
        rescued_history_.clear();
        total_admitted_ = 0;
        total_completed_ = 0;
        starvation_prevented_count_ = 0;
        metrics_q1_.starvation_rescued = 0;
        metrics_q2_.starvation_rescued = 0;
        metrics_q3_.starvation_rescued = 0;
    }

private:
    std::mutex mtx_;

    // 3 MLFQ Priority Tiers (Module 2)
    PriorityQueueQ1 queue_1_; // Q1: Priority Scheduling (q = 2s)
    SJFQueueQ2 queue_2_;      // Q2: Shortest Job First (q = 4s)
    FCFSQueueQ3 queue_3_;     // Q3: First-Come, First-Served

    std::unique_ptr<Process> active_process_ = nullptr;
    std::deque<Process> completed_;
    std::deque<StateTransition> transitions_;
    std::deque<Process> rescued_history_;

    QueueTelemetry metrics_q1_;
    QueueTelemetry metrics_q2_;
    QueueTelemetry metrics_q3_;

    int total_admitted_ = 4;
    int total_completed_ = 4;
    int starvation_prevented_count_ = 1;

    const int QUANTUM_Q1 = 2; // 2 seconds
    const int QUANTUM_Q2 = 4; // 4 seconds

    void log_transition_unlocked(const std::string& txn_id, const std::string& from,
                                const std::string& to, const std::string& queue,
                                const std::string& note) {
        StateTransition st;
        st.txn_id = txn_id;
        st.from_state = from;
        st.to_state = to;
        st.queue_level = queue;
        st.event_note = note;
        st.logged_at = get_iso_timestamp();

        transitions_.push_front(st);
        while (transitions_.size() > 100) {
            transitions_.pop_back();
        }
    }

    std::string serialize_state_unlocked() {
        std::ostringstream ss;
        ss << "{";

        ss << "\"active_process\":";
        if (active_process_) {
            ss << active_process_->to_json();
        } else {
            ss << "null";
        }
        ss << ",";

        ss << "\"queue_1\":" << queue_1_.to_json() << ",";
        ss << "\"queue_2\":" << queue_2_.to_json() << ",";
        ss << "\"queue_3\":" << queue_3_.to_json() << ",";

        ss << "\"completed\":[";
        for (size_t i = 0; i < completed_.size(); ++i) {
            ss << completed_[i].to_json();
            if (i + 1 < completed_.size()) ss << ",";
        }
        ss << "],";

        ss << "\"transitions\":[";
        for (size_t i = 0; i < transitions_.size(); ++i) {
            ss << transitions_[i].to_json();
            if (i + 1 < transitions_.size()) ss << ",";
        }
        ss << "],";

        // Starvation Watch: At-risk transactions currently waiting >= 4s in Q3 or Q2
        std::vector<Process> starving_list;
        for (size_t i = 0; i < queue_3_.size(); ++i) {
            if (queue_3_[i].wait_time_sec >= 4) {
                starving_list.push_back(queue_3_[i]);
            }
        }
        for (size_t i = 0; i < queue_2_.size(); ++i) {
            if (queue_2_[i].wait_time_sec >= 4) {
                starving_list.push_back(queue_2_[i]);
            }
        }

        ss << "\"starving_transactions\":[";
        for (size_t i = 0; i < starving_list.size(); ++i) {
            ss << starving_list[i].to_json();
            if (i + 1 < starving_list.size()) ss << ",";
        }
        ss << "],";

        // Aging Rescues: Recently promoted transactions
        ss << "\"rescued_transactions\":[";
        for (size_t i = 0; i < rescued_history_.size(); ++i) {
            ss << rescued_history_[i].to_json();
            if (i + 1 < rescued_history_.size()) ss << ",";
        }
        ss << "],";

        ss << "\"metrics\":[";
        ss << metrics_q1_.to_json() << ",";
        ss << metrics_q2_.to_json() << ",";
        ss << metrics_q3_.to_json();
        ss << "],";

        ss << "\"stats\":{"
           << "\"total_admitted\":" << total_admitted_ << ","
           << "\"total_completed\":" << total_completed_ << ","
           << "\"starvation_prevented_count\":" << starvation_prevented_count_ << ","
           << "\"starvation_rescued\":" << starvation_prevented_count_ << ","
           << "\"cpu_active\":" << (active_process_ ? "true" : "false")
           << "}";

        ss << "}";
        return ss.str();
    }
};

} // namespace bankos
