#pragma once

#include "pcb.hpp"
#include "state_transition.hpp"
#include "middleware.hpp"
#include <vector>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <memory>
#include <sstream>

namespace bank_sim {

/**
 * Multi-Level Feedback Queue (MLFQ) Scheduler with MD-ACC Middleware
 *
 * Architecture:
 * - Queue 1: High Priority, Shortest Remaining Time First (SRTF) for interactive UPI & ATM.
 * - Queue 2: Low Priority, Round Robin (RR) with time slice quantum for batch Salary.
 * - Aging Mechanism: Promotes Salary processes from Q2 to Q1 if they wait > X ticks.
 * - Resource Locking: Bounded 2PL concurrency locking with WAITING state.
 * - MD-ACC Middleware: Conflict tracking, 100-tick contention scoring, and Exponential Backoff.
 * - State Transition Logging: Logs lifecycle events to memory and JSON file.
 */
class MLFQScheduler {
public:
    MLFQScheduler(int aging_threshold = 50, int time_quantum = 5, bool mdacc_enabled = true)
        : aging_threshold_(aging_threshold),
          time_quantum_(time_quantum),
          current_tick_(0),
          next_arrival_idx_(0),
          running_pid_(-1),
          running_quantum_used_(0),
          total_aging_promotions_(0),
          total_completed_(0),
          total_turnaround_time_(0),
          total_waiting_time_(0),
          middleware_(std::make_shared<Middleware>(mdacc_enabled)) {}

    /**
     * Load the workload into the scheduler.
     */
    void load_workload(std::vector<ProcessControlBlock> workload) {
        std::lock_guard<std::mutex> lock(mtx_);
        workload_ = std::move(workload);
        all_pcbs_.clear();
        all_pcbs_.reserve(workload_.size() + 1);

        // Populate lookup table by PID
        for (const auto& pcb : workload_) {
            all_pcbs_[pcb.pid] = pcb;
        }

        current_tick_ = 0;
        next_arrival_idx_ = 0;
        running_pid_ = -1;
        running_quantum_used_ = 0;
        q1_ready_pids_.clear();
        q2_ready_pids_.clear();
        waiting_pids_.clear();
        locked_accounts_.clear();
        transitions_.clear();
        total_aging_promotions_ = 0;
        total_completed_ = 0;
        if (middleware_) {
            middleware_->reset();
        }
    }

    /**
     * Advance the simulation by a single tick.
     * Executes admittance, aging, SRTF/RR dispatching, and lock arbitration.
     */
    bool step_tick() {
        std::lock_guard<std::mutex> lock(mtx_);
        return step_tick_unlocked();
    }

    /**
     * Run simulation until all processes terminate or max_ticks is reached.
     */
    void run_simulation(int max_ticks = 100000, bool verbose = false) {
        std::cout << "[MLFQScheduler] Starting simulation with Aging Threshold X = " 
                  << aging_threshold_ << " ticks, Queue 2 Quantum = " << time_quantum_ << " ticks...\n";

        while (current_tick_ < max_ticks) {
            bool has_more = step_tick();
            if (!has_more && running_pid_ == -1 && q1_ready_pids_.empty() 
                && q2_ready_pids_.empty() && waiting_pids_.empty() 
                && next_arrival_idx_ >= workload_.size()) {
                break;
            }

            if (verbose && current_tick_ % 500 == 0) {
                std::cout << "[Tick " << current_tick_ << "] Q1: " << q1_ready_pids_.size()
                          << " | Q2: " << q2_ready_pids_.size() 
                          << " | Waiting: " << waiting_pids_.size()
                          << " | Completed: " << total_completed_ 
                          << " | Aging Promotions: " << total_aging_promotions_ << "\n";
            }
        }

        std::cout << "[MLFQScheduler] Simulation finished at Tick " << current_tick_ 
                  << ". Total completed: " << total_completed_ 
                  << ", Aging promotions: " << total_aging_promotions_ << "\n";
    }

    /**
     * Export all recorded state transitions to a JSON file.
     */
    void export_transitions_to_json(const std::string& filename, size_t max_records = 50000) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "[MLFQScheduler] Failed to open " << filename << " for writing\n";
            return;
        }

        file << "{\n";
        file << "  \"simulation_summary\": {\n";
        file << "    \"total_ticks\": " << current_tick_ << ",\n";
        file << "    \"total_completed\": " << total_completed_ << ",\n";
        file << "    \"aging_promotions\": " << total_aging_promotions_ << ",\n";
        file << "    \"aging_threshold_ticks\": " << aging_threshold_ << ",\n";
        file << "    \"avg_turnaround_time\": " << (total_completed_ > 0 ? static_cast<double>(total_turnaround_time_) / total_completed_ : 0.0) << ",\n";
        file << "    \"avg_waiting_time\": " << (total_completed_ > 0 ? static_cast<double>(total_waiting_time_) / total_completed_ : 0.0) << "\n";
        file << "  },\n";
        file << "  \"state_transitions\": [\n";

        size_t count = std::min(transitions_.size(), max_records);
        for (size_t i = 0; i < count; ++i) {
            file << "    " << transitions_[i].to_json();
            if (i + 1 < count) file << ",";
            file << "\n";
        }

        file << "  ]\n";
        file << "}\n";
        file.close();

        std::cout << "[MLFQScheduler] Wrote " << count << " transitions to " << filename << "\n";
    }

    // JSON API queries for Web Dashboard
    std::string get_status_json() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ostringstream oss;
        oss << "{"
            << "\"tick\":" << current_tick_ << ","
            << "\"running_pid\":" << running_pid_ << ","
            << "\"q1_size\":" << q1_ready_pids_.size() << ","
            << "\"q2_size\":" << q2_ready_pids_.size() << ","
            << "\"waiting_size\":" << waiting_pids_.size() << ","
            << "\"completed_count\":" << total_completed_ << ","
            << "\"aging_promotions\":" << total_aging_promotions_ << ","
            << "\"aging_threshold\":" << aging_threshold_ << ","
            << "\"time_quantum\":" << time_quantum_ << ","
            << "\"total_workload\":" << workload_.size() << ","
            << "\"avg_turnaround_time\":" << (total_completed_ > 0 ? static_cast<double>(total_turnaround_time_) / total_completed_ : 0.0) << ","
            << "\"avg_waiting_time\":" << (total_completed_ > 0 ? static_cast<double>(total_waiting_time_) / total_completed_ : 0.0) << ","
            << "\"mdacc_enabled\":" << (middleware_->is_mdacc_enabled() ? "true" : "false") << ","
            << "\"contention_score\":\"" << contentionLevelToString(middleware_->contention_level()) << "\","
            << "\"conflict_rate\":" << middleware_->conflict_rate() << ","
            << "\"abort_rate\":" << middleware_->abort_rate() << ","
            << "\"total_aborts\":" << middleware_->total_aborts() << ","
            << "\"total_backoffs\":" << middleware_->total_backoffs_applied()
            << "}";
        return oss.str();
    }

    std::string get_recent_transitions_json(size_t limit = 100) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ostringstream oss;
        oss << "[";
        size_t start = (transitions_.size() > limit) ? transitions_.size() - limit : 0;
        bool first = true;
        for (size_t i = start; i < transitions_.size(); ++i) {
            if (!first) oss << ",";
            first = false;
            oss << transitions_[i].to_json();
        }
        oss << "]";
        return oss.str();
    }

    std::string get_active_queues_json() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::ostringstream oss;
        oss << "{"
            << "\"running\":";
        if (running_pid_ != -1 && all_pcbs_.find(running_pid_) != all_pcbs_.end()) {
            oss << all_pcbs_[running_pid_].to_json();
        } else {
            oss << "null";
        }

        oss << ",\"q1\":[";
        for (size_t i = 0; i < std::min<size_t>(q1_ready_pids_.size(), 15); ++i) {
            if (i > 0) oss << ",";
            oss << all_pcbs_[q1_ready_pids_[i]].to_json();
        }

        oss << "],\"q2\":[";
        for (size_t i = 0; i < std::min<size_t>(q2_ready_pids_.size(), 15); ++i) {
            if (i > 0) oss << ",";
            oss << all_pcbs_[q2_ready_pids_[i]].to_json();
        }

        oss << "],\"waiting\":[";
        for (size_t i = 0; i < std::min<size_t>(waiting_pids_.size(), 15); ++i) {
            if (i > 0) oss << ",";
            oss << all_pcbs_[waiting_pids_[i]].to_json();
        }
        oss << "]}";
        return oss.str();
    }

    int current_tick() const { return current_tick_; }
    uint64_t total_completed() const { return total_completed_; }
    uint64_t total_aging_promotions() const { return total_aging_promotions_; }
    int aging_threshold() const { return aging_threshold_; }
    void set_aging_threshold(int val) { aging_threshold_ = val; }

    // MD-ACC Middleware Accessors
    std::shared_ptr<Middleware> middleware() const { return middleware_; }
    void set_comparison_mode(bool mdacc_enabled) { middleware_->set_mdacc_enabled(mdacc_enabled); }
    bool is_mdacc_enabled() const { return middleware_->is_mdacc_enabled(); }
    uint64_t total_aborts() const { return middleware_->total_aborts(); }
    double conflict_rate() const { return middleware_->conflict_rate(); }
    double abort_rate() const { return middleware_->abort_rate(); }

private:
    bool step_tick_unlocked() {
        current_tick_++;

        // 0. Update Middleware telemetry (queue length & 100-tick contention recalculation)
        int current_queue_len = static_cast<int>(q1_ready_pids_.size() + q2_ready_pids_.size() + waiting_pids_.size());
        middleware_->tick(current_tick_, current_queue_len);

        // Process backoff countdown for waiting processes
        for (int pid : waiting_pids_) {
            auto& pcb = all_pcbs_[pid];
            if (pcb.backoff_remaining > 0) {
                pcb.backoff_remaining--;
            }
        }
        wake_up_waiting_processes();

        // 1. Admit newly arrived processes from workload
        while (next_arrival_idx_ < workload_.size() && 
               workload_[next_arrival_idx_].arrival_time <= current_tick_) {
            
            int pid = workload_[next_arrival_idx_].pid;
            auto& pcb = all_pcbs_[pid];
            next_arrival_idx_++;

            // Transition from NEW to READY
            record_transition(pcb.pid, pcb.type, ProcessState::NEW, ProcessState::READY, 
                              "Process arrived and admitted into MLFQ");
            pcb.state = ProcessState::READY;

            // Route to initial queue
            if (pcb.type == TransactionType::SALARY) {
                pcb.current_queue = 2; // Queue 2: Low Priority Round Robin
                q2_ready_pids_.push_back(pid);
            } else {
                pcb.current_queue = 1; // Queue 1: High Priority SRTF (UPI & ATM)
                q1_ready_pids_.push_back(pid);
            }
        }

        // 2. Anti-Starvation Aging Check for Queue 2
        // If a Salary process stays in Queue 2 for more than X ticks, promote it to Queue 1
        std::deque<int> remaining_q2;
        while (!q2_ready_pids_.empty()) {
            int pid = q2_ready_pids_.front();
            q2_ready_pids_.pop_front();
            auto& pcb = all_pcbs_[pid];

            if (pcb.queue2_ticks >= aging_threshold_) {
                // Promote to Queue 1!
                pcb.current_queue = 1;
                pcb.aged_promoted = true;
                total_aging_promotions_++;

                record_transition(pcb.pid, pcb.type, ProcessState::READY, ProcessState::READY,
                                  "Aging promotion: Stayed in Queue 2 for " + 
                                  std::to_string(pcb.queue2_ticks) + " ticks >= X. Promoted to Queue 1 (SRTF)!");

                q1_ready_pids_.push_back(pid);
            } else {
                remaining_q2.push_back(pid);
            }
        }
        q2_ready_pids_ = std::move(remaining_q2);

        // 3. Check Preemption:
        // If current running process is from Queue 2, but Queue 1 has ready processes,
        // Queue 1 (High Priority SRTF) preempts Queue 2!
        if (running_pid_ != -1) {
            auto& running_pcb = all_pcbs_[running_pid_];
            if (running_pcb.current_queue == 2 && !q1_ready_pids_.empty()) {
                // Preempt running Queue 2 process
                record_transition(running_pcb.pid, running_pcb.type, ProcessState::RUNNING, ProcessState::READY,
                                  "Preempted by higher priority Queue 1 arrival");
                running_pcb.state = ProcessState::READY;
                release_locks(running_pcb);
                q2_ready_pids_.push_front(running_pid_); // Put back in front of Q2
                running_pid_ = -1;
                running_quantum_used_ = 0;
            }
        }

        // 4. If CPU is idle, select next process to run
        if (running_pid_ == -1) {
            running_pid_ = select_next_process();
            if (running_pid_ != -1) {
                auto& pcb = all_pcbs_[running_pid_];
                record_transition(pcb.pid, pcb.type, ProcessState::READY, ProcessState::RUNNING,
                                  "Dispatched to CPU (Queue " + std::to_string(pcb.current_queue) + ")");
                pcb.state = ProcessState::RUNNING;
                if (pcb.start_time == -1) {
                    pcb.start_time = current_tick_;
                }
                running_quantum_used_ = 0;
            }
        }

        // 5. Execute 1 CPU cycle on the running process
        if (running_pid_ != -1) {
            auto& pcb = all_pcbs_[running_pid_];
            pcb.burst_time--;
            running_quantum_used_++;

            // Check if process finished
            if (pcb.burst_time <= 0) {
                // TERMINATED
                pcb.state = ProcessState::TERMINATED;
                pcb.completion_time = current_tick_;
                record_transition(pcb.pid, pcb.type, ProcessState::RUNNING, ProcessState::TERMINATED,
                                  "Transaction execution completed");

                release_locks(pcb);
                total_completed_++;
                total_turnaround_time_ += (pcb.completion_time - pcb.arrival_time);
                total_waiting_time_ += pcb.wait_time;

                wake_up_waiting_processes();
                running_pid_ = -1;
                running_quantum_used_ = 0;

            } else if (pcb.current_queue == 2 && running_quantum_used_ >= time_quantum_) {
                // Queue 2 Round Robin time quantum expired -> Preempt & yield to back of Q2
                record_transition(pcb.pid, pcb.type, ProcessState::RUNNING, ProcessState::READY,
                                  "Round Robin quantum expired (" + std::to_string(time_quantum_) + " ticks)");
                pcb.state = ProcessState::READY;
                release_locks(pcb);
                q2_ready_pids_.push_back(pcb.pid);
                wake_up_waiting_processes();
                running_pid_ = -1;
                running_quantum_used_ = 0;
            }
        }

        // 6. Update waiting times and Q2 aging counters for non-running processes
        for (int pid : q1_ready_pids_) {
            all_pcbs_[pid].wait_time++;
        }
        for (int pid : q2_ready_pids_) {
            all_pcbs_[pid].wait_time++;
            all_pcbs_[pid].queue2_ticks++; // Age Salary processes
        }
        for (int pid : waiting_pids_) {
            all_pcbs_[pid].wait_time++;
            if (all_pcbs_[pid].current_queue == 2) {
                all_pcbs_[pid].queue2_ticks++; // Also age waiting Salary processes
            }
        }

        return (next_arrival_idx_ < workload_.size() || running_pid_ != -1 ||
                !q1_ready_pids_.empty() || !q2_ready_pids_.empty() || !waiting_pids_.empty());
    }

    /**
     * Dispatcher: Select highest priority process available.
     * Queue 1: Shortest Remaining Time First (SRTF)
     * Queue 2: Round Robin (FIFO)
     */
    int select_next_process() {
        int qlen = static_cast<int>(q1_ready_pids_.size() + q2_ready_pids_.size() + waiting_pids_.size());

        // First check Queue 1 (SRTF)
        if (!q1_ready_pids_.empty()) {
            // Find process with shortest remaining burst time that can acquire locks
            auto best_it = q1_ready_pids_.end();
            int min_burst = 999999;

            for (auto it = q1_ready_pids_.begin(); it != q1_ready_pids_.end(); ++it) {
                const auto& pcb = all_pcbs_[*it];
                if (can_acquire_locks(pcb)) {
                    if (pcb.burst_time < min_burst) {
                        min_burst = pcb.burst_time;
                        best_it = it;
                    }
                }
            }

            if (best_it != q1_ready_pids_.end()) {
                int pid = *best_it;
                q1_ready_pids_.erase(best_it);
                acquire_locks(all_pcbs_[pid]);
                middleware_->intercept(all_pcbs_[pid], false, qlen);

                // Move any processes in Q1 that conflict with acquired locks to WAITING
                std::deque<int> remaining_q1;
                for (int ready_pid : q1_ready_pids_) {
                    auto& p = all_pcbs_[ready_pid];
                    if (!can_acquire_locks(p)) {
                        int backoff = middleware_->intercept(p, true, qlen);
                        p.state = ProcessState::WAITING;
                        p.backoff_remaining = backoff;
                        std::string reason = middleware_->is_mdacc_enabled()
                            ? ("MD-ACC intercepted: Account locked; exponential backoff applied (" + std::to_string(backoff) + " ticks)")
                            : ("Baseline OCC conflict: Hit DB directly, transaction aborted (abort #" + std::to_string(p.abort_count) + ")");
                        record_transition(p.pid, p.type, ProcessState::READY, ProcessState::WAITING, reason);
                        waiting_pids_.push_back(ready_pid);
                    } else {
                        remaining_q1.push_back(ready_pid);
                    }
                }
                q1_ready_pids_ = std::move(remaining_q1);

                return pid;
            }

            // If Q1 candidates could not acquire locks, move front candidate to WAITING
            if (!q1_ready_pids_.empty()) {
                int blocked_pid = q1_ready_pids_.front();
                q1_ready_pids_.pop_front();
                auto& pcb = all_pcbs_[blocked_pid];
                int backoff = middleware_->intercept(pcb, true, qlen);
                pcb.state = ProcessState::WAITING;
                pcb.backoff_remaining = backoff;
                std::string reason = middleware_->is_mdacc_enabled()
                    ? ("MD-ACC intercepted: Account locked; exponential backoff applied (" + std::to_string(backoff) + " ticks)")
                    : ("Baseline OCC conflict: Hit DB directly, transaction aborted (abort #" + std::to_string(pcb.abort_count) + ")");
                record_transition(pcb.pid, pcb.type, ProcessState::READY, ProcessState::WAITING, reason);
                waiting_pids_.push_back(blocked_pid);
            }
        }

        // Next check Queue 2 (Round Robin)
        while (!q2_ready_pids_.empty()) {
            int pid = q2_ready_pids_.front();
            q2_ready_pids_.pop_front();
            auto& pcb = all_pcbs_[pid];

            if (can_acquire_locks(pcb)) {
                acquire_locks(pcb);
                middleware_->intercept(pcb, false, qlen);

                // Move any conflicting Q2 processes to WAITING
                std::deque<int> remaining_q2;
                for (int ready_pid : q2_ready_pids_) {
                    auto& p = all_pcbs_[ready_pid];
                    if (!can_acquire_locks(p)) {
                        int backoff = middleware_->intercept(p, true, qlen);
                        p.state = ProcessState::WAITING;
                        p.backoff_remaining = backoff;
                        std::string reason = middleware_->is_mdacc_enabled()
                            ? ("MD-ACC intercepted: Account locked; exponential backoff applied (" + std::to_string(backoff) + " ticks)")
                            : ("Baseline OCC conflict: Hit DB directly, transaction aborted (abort #" + std::to_string(p.abort_count) + ")");
                        record_transition(p.pid, p.type, ProcessState::READY, ProcessState::WAITING, reason);
                        waiting_pids_.push_back(ready_pid);
                    } else {
                        remaining_q2.push_back(ready_pid);
                    }
                }
                q2_ready_pids_ = std::move(remaining_q2);

                return pid;
            } else {
                // Transition to WAITING
                int backoff = middleware_->intercept(pcb, true, qlen);
                pcb.state = ProcessState::WAITING;
                pcb.backoff_remaining = backoff;
                std::string reason = middleware_->is_mdacc_enabled()
                    ? ("MD-ACC intercepted: Account locked; exponential backoff applied (" + std::to_string(backoff) + " ticks)")
                    : ("Baseline OCC conflict: Hit DB directly, transaction aborted (abort #" + std::to_string(pcb.abort_count) + ")");
                record_transition(pcb.pid, pcb.type, ProcessState::READY, ProcessState::WAITING, reason);
                waiting_pids_.push_back(pid);
            }
        }

        return -1; // Idle CPU
    }

    bool can_acquire_locks(const ProcessControlBlock& pcb) const {
        for (int acc_id : pcb.required_locks) {
            if (locked_accounts_.find(acc_id) != locked_accounts_.end()) {
                return false;
            }
        }
        return true;
    }

    void acquire_locks(const ProcessControlBlock& pcb) {
        for (int acc_id : pcb.required_locks) {
            locked_accounts_.insert(acc_id);
        }
    }

    void release_locks(const ProcessControlBlock& pcb) {
        for (int acc_id : pcb.required_locks) {
            locked_accounts_.erase(acc_id);
        }
    }

    void wake_up_waiting_processes() {
        std::vector<int> remaining_waiting;
        for (int pid : waiting_pids_) {
            auto& pcb = all_pcbs_[pid];
            if (!middleware_->is_mdacc_enabled()) {
                // Baseline OCC/2PL: Transactions ignore backoff delay and immediately re-enter READY queue
                // This causes transactions to hit the DB directly, producing high abort rates!
                pcb.state = ProcessState::READY;
                record_transition(pcb.pid, pcb.type, ProcessState::WAITING, ProcessState::READY,
                                  "Baseline OCC: Re-entered READY queue immediately (no backoff delay)");
                if (pcb.current_queue == 1) {
                    q1_ready_pids_.push_back(pid);
                } else {
                    q2_ready_pids_.push_back(pid);
                }
            } else {
                // MD-ACC Mode: Must wait until exponential backoff expires AND locks are free
                if (pcb.backoff_remaining <= 0 && can_acquire_locks(pcb)) {
                    pcb.state = ProcessState::READY;
                    record_transition(pcb.pid, pcb.type, ProcessState::WAITING, ProcessState::READY,
                                      "MD-ACC backoff elapsed and locks available; returned to READY queue");
                    if (pcb.current_queue == 1) {
                        q1_ready_pids_.push_back(pid);
                    } else {
                        q2_ready_pids_.push_back(pid);
                    }
                } else {
                    remaining_waiting.push_back(pid);
                }
            }
        }
        waiting_pids_ = std::move(remaining_waiting);
    }

    void record_transition(int pid, TransactionType type, ProcessState from, ProcessState to, const std::string& reason) {
        transitions_.push_back({current_tick_, pid, type, from, to, reason});
    }

    // Configuration & parameters
    int aging_threshold_;
    int time_quantum_;

    // Simulation Clock & Queues
    int current_tick_;
    size_t next_arrival_idx_;
    int running_pid_;
    int running_quantum_used_;

    std::vector<ProcessControlBlock> workload_;
    std::unordered_map<int, ProcessControlBlock> all_pcbs_;

    std::deque<int> q1_ready_pids_;      // Queue 1: High Priority (SRTF)
    std::deque<int> q2_ready_pids_;      // Queue 2: Low Priority (Round Robin)
    std::vector<int> waiting_pids_;       // Blocked on locks
    std::unordered_set<int> locked_accounts_;

    // State Transitions log
    std::vector<StateTransition> transitions_;

    // Performance Metrics
    uint64_t total_aging_promotions_;
    uint64_t total_completed_;
    uint64_t total_turnaround_time_;
    uint64_t total_waiting_time_;

    mutable std::mutex mtx_;
    std::shared_ptr<Middleware> middleware_;
};

} // namespace bank_sim
