#pragma once

#include "pcb.hpp"
#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

namespace BankOS {

struct GanttEntry {
    int start_tick;
    int end_tick;
    std::string pid;
    std::string tx_type;
};

struct EventLogEntry {
    int tick;
    std::string pid;
    std::string message;
    std::string type; // "info", "warn", "ok"
};

struct SchedulerStats {
    int clock_tick;
    int total_transactions;
    int completed_transactions;
    int failed_transactions;
    int active_ready_count;
    int active_waiting_count;
    double avg_waiting_time;
    double avg_turnaround_time;
    double avg_response_time;
    double cpu_utilization;
    double throughput;
    int time_quantum;
};

class Scheduler {
public:
    static Scheduler& getInstance();

    // Configuration
    void setTimeQuantum(int q);
    int getTimeQuantum() const;

    // Simulation Execution Control
    bool step();                    // Runs 1 discrete scheduling tick
    void startContinuous(int interval_ms = 700);
    void stopContinuous();
    bool isRunningContinuous() const;
    void reset();

    // Adding processes to Ready Queue
    void enqueueProcess(const std::string& pid);
    void generateBatchWorkload(int count);

    // Queries for GUI Dashboard
    std::vector<std::string> getReadyQueue();
    std::vector<std::string> getWaitingList();
    std::string getCurrentRunningPID();
    int getRemainingQuantumSlice() const;
    int getClockTick() const;
    SchedulerStats getStats();
    std::vector<GanttEntry> getGanttHistory();
    std::vector<EventLogEntry> getRecentLogs(size_t limit = 100);

    // Helper logging
    void addLog(const std::string& pid, const std::string& msg, const std::string& type = "info");

private:
    Scheduler();
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    mutable std::mutex m_mutex;
    std::atomic<bool> m_is_running_continuous;
    std::unique_ptr<std::thread> m_worker_thread;

    int m_time_quantum;
    int m_quantum_slice_used;
    int m_clock_tick;
    int m_total_cpu_busy_ticks;
    int m_completed_count;
    int m_failed_count;

    std::string m_current_pid;
    std::deque<std::string> m_ready_queue;
    std::vector<std::string> m_waiting_list;
    std::vector<GanttEntry> m_gantt_history;
    std::vector<EventLogEntry> m_event_logs;

    void wakeUpEligibleWaiters();
    void executeCurrentProcessTick();
};

} // namespace BankOS
