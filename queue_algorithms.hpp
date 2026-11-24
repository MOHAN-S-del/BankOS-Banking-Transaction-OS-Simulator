// ============================================================================
// BankOS Middleware: OS-Driven Concurrent Banking Transaction Middleware
// File: queue_algorithms.hpp
//
// Module 2: Individual OS Scheduling Queue Algorithms
//
// Encapsulates the 3 core CPU scheduling algorithms required by the OS:
//
// 1. PriorityQueueQ1 (Queue 1: Preemptive Priority Scheduling, q = 2s)
//    - High-urgency transactional queue handling ATM and UPI.
//    - Always dispatches the ready process with the highest priority score.
//    - Quantum limit q = 2s. If burst > 2s, process is demoted to Queue 2.
//
// 2. SJFQueueQ2 (Queue 2: Shortest Job First / SJF, q = 4s)
//    - Medium-urgency queue handling lightweight Standard transfers (<= 3s),
//      as well as processes demoted from Q1 or promoted from Q3 via aging.
//    - Sorts dynamically by shortest remaining burst time (SJF / SRTF).
//    - Quantum limit q = 4s. If remaining burst > 0, demoted to Queue 3.
//
// 3. FCFSQueueQ3 (Queue 3: First-Come, First-Served / FCFS)
//    - Low-urgency queue handling corporate Payroll batches and heavy jobs (> 3s).
//    - Strictly preserves arrival order (FIFO).
//    - Protected by dynamic aging: processes waiting >= 6s are promoted to Q2.
// ============================================================================

#pragma once

#include "pcb.hpp"
#include <deque>
#include <algorithm>
#include <sstream>

namespace bankos {

// ============================================================================
// QUEUE 1: PREEMPTIVE PRIORITY SCHEDULING (Time Quantum q = 2s)
//
// OS Principle:
// In financial computing, retail cash withdrawals (ATM) and instant peer-to-peer
// payments (UPI) demand immediate turnaround. Queue 1 applies Priority Scheduling
// where each transaction is assigned a priority score (ATM = 10, UPI = 8).
// When Core 0 is ready for dispatch, Q1 always yields the highest priority task.
// Ties are broken by longest wait time (FIFO within identical priority).
// ============================================================================
class PriorityQueueQ1 {
public:
    PriorityQueueQ1() = default;

    // Enqueues a process and sorts by priority score descending
    void push(const Process& p) {
        queue_.push_back(p);
        sort();
    }

    // Sorts ready queue: Highest priority score first, then longest waiting
    void sort() {
        std::sort(queue_.begin(), queue_.end(), [](const Process& a, const Process& b) {
            if (a.priority_score != b.priority_score) {
                return a.priority_score > b.priority_score;
            }
            return a.wait_time_sec > b.wait_time_sec;
        });
    }

    // Returns true if queue has no waiting processes
    bool empty() const {
        return queue_.empty();
    }

    // Number of waiting processes
    size_t size() const {
        return queue_.size();
    }

    // Peeks at highest-priority ready process
    const Process& front() const {
        return queue_.front();
    }

    Process& front() {
        return queue_.front();
    }

    // Removes and returns the highest-priority ready process for CPU dispatch
    Process pop() {
        Process p = queue_.front();
        queue_.pop_front();
        return p;
    }

    // Erases process at specific index (used during migration/demotion)
    Process erase_at(size_t index) {
        Process p = queue_[index];
        queue_.erase(queue_.begin() + index);
        return p;
    }

    void clear() {
        queue_.clear();
    }

    // Iterators for dynamic aging and wait time increments
    auto begin() { return queue_.begin(); }
    auto end() { return queue_.end(); }
    auto begin() const { return queue_.begin(); }
    auto end() const { return queue_.end(); }

    Process& operator[](size_t idx) { return queue_[idx]; }
    const Process& operator[](size_t idx) const { return queue_[idx]; }

    // JSON serialization for telemetry
    std::string to_json() const {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < queue_.size(); ++i) {
            ss << queue_[i].to_json();
            if (i + 1 < queue_.size()) ss << ",";
        }
        ss << "]";
        return ss.str();
    }

private:
    std::deque<Process> queue_;
};

// ============================================================================
// QUEUE 2: SHORTEST JOB FIRST / SJF (Time Quantum q = 4s)
//
// OS Principle:
// Shortest Job First minimizes the average waiting time across all ready processes.
// Queue 2 captures small standard transfers (burst <= 3s), processes demoted from
// Queue 1 whose burst was > 2s, and aged processes promoted from Queue 3.
// The queue is dynamically sorted by remaining_burst ascending.
// If a process exceeds its 4-second time quantum in Q2, it is demoted to Q3.
// ============================================================================
class SJFQueueQ2 {
public:
    SJFQueueQ2() = default;

    // Enqueues a process and sorts by shortest remaining burst
    void push(const Process& p) {
        queue_.push_back(p);
        sort();
    }

    // Sorts ready queue: Shortest remaining burst first, ties broken by wait time
    void sort() {
        std::sort(queue_.begin(), queue_.end(), [](const Process& a, const Process& b) {
            if (a.remaining_burst != b.remaining_burst) {
                return a.remaining_burst < b.remaining_burst;
            }
            return a.wait_time_sec > b.wait_time_sec;
        });
    }

    bool empty() const {
        return queue_.empty();
    }

    size_t size() const {
        return queue_.size();
    }

    const Process& front() const {
        return queue_.front();
    }

    Process& front() {
        return queue_.front();
    }

    // Removes and returns the shortest job for CPU dispatch
    Process pop() {
        Process p = queue_.front();
        queue_.pop_front();
        return p;
    }

    Process erase_at(size_t index) {
        Process p = queue_[index];
        queue_.erase(queue_.begin() + index);
        return p;
    }

    void clear() {
        queue_.clear();
    }

    auto begin() { return queue_.begin(); }
    auto end() { return queue_.end(); }
    auto begin() const { return queue_.begin(); }
    auto end() const { return queue_.end(); }

    Process& operator[](size_t idx) { return queue_[idx]; }
    const Process& operator[](size_t idx) const { return queue_[idx]; }

    std::string to_json() const {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < queue_.size(); ++i) {
            ss << queue_[i].to_json();
            if (i + 1 < queue_.size()) ss << ",";
        }
        ss << "]";
        return ss.str();
    }

private:
    std::deque<Process> queue_;
};

// ============================================================================
// QUEUE 3: FIRST-COME, FIRST-SERVED / FCFS
//
// OS Principle:
// Heavy batch jobs (such as Corporate Payroll disbursements and transfers > ₹10,000)
// require extended computational bursts (7 seconds). In a multi-level scheduler,
// they reside in Queue 3 where they are scheduled in strict arrival order (FIFO).
// To prevent starvation caused by a constant stream of new Q1/Q2 jobs,
// the MLFQ Aging controller inspects Queue 3 every second: any process waiting
// >= 6 seconds is automatically promoted to Queue 2.
// ============================================================================
class FCFSQueueQ3 {
public:
    FCFSQueueQ3() = default;

    // Strict FIFO arrival enqueue
    void push(const Process& p) {
        queue_.push_back(p);
    }

    bool empty() const {
        return queue_.empty();
    }

    size_t size() const {
        return queue_.size();
    }

    const Process& front() const {
        return queue_.front();
    }

    Process& front() {
        return queue_.front();
    }

    // Removes and returns the earliest arrived process (FIFO dispatch)
    Process pop() {
        Process p = queue_.front();
        queue_.pop_front();
        return p;
    }

    Process erase_at(size_t index) {
        Process p = queue_[index];
        queue_.erase(queue_.begin() + index);
        return p;
    }

    void clear() {
        queue_.clear();
    }

    auto begin() { return queue_.begin(); }
    auto end() { return queue_.end(); }
    auto begin() const { return queue_.begin(); }
    auto end() const { return queue_.end(); }

    Process& operator[](size_t idx) { return queue_[idx]; }
    const Process& operator[](size_t idx) const { return queue_[idx]; }

    std::string to_json() const {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < queue_.size(); ++i) {
            ss << queue_[i].to_json();
            if (i + 1 < queue_.size()) ss << ",";
        }
        ss << "]";
        return ss.str();
    }

private:
    std::deque<Process> queue_;
};

} // namespace bankos
