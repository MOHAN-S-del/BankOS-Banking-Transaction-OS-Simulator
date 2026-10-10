#include "scheduler.hpp"
#include "process_manager.hpp"
#include "lock_manager.hpp"
#include "db_manager.hpp"
#include <iostream>
#include <random>

namespace BankOS {

Scheduler& Scheduler::getInstance() {
    static Scheduler instance;
    return instance;
}

Scheduler::Scheduler()
    : m_is_running_continuous(false),
      m_time_quantum(2),
      m_quantum_slice_used(0),
      m_clock_tick(0),
      m_total_cpu_busy_ticks(0),
      m_completed_count(0),
      m_failed_count(0),
      m_current_pid("") 
{
    addLog("SYS", "Bank OS Kernel Initialized. Round Robin Scheduler ready.", "ok");
}

Scheduler::~Scheduler() {
    stopContinuous();
}

void Scheduler::setTimeQuantum(int q) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (q >= 1 && q <= 10) {
        m_time_quantum = q;
        addLog("SCHED", "Time Quantum updated to " + std::to_string(q) + " ticks.", "info");
    }
}

int Scheduler::getTimeQuantum() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_time_quantum;
}

void Scheduler::addLog(const std::string& pid, const std::string& msg, const std::string& type) {
    // Note: Mutex should already be held if calling internally, or this can be called directly
    EventLogEntry entry;
    entry.tick = m_clock_tick;
    entry.pid = pid;
    entry.message = msg;
    entry.type = type;

    m_event_logs.insert(m_event_logs.begin(), entry);
    if (m_event_logs.size() > 200) {
        m_event_logs.pop_back();
    }
    DBManager::getInstance().logSchedulerEvent(pid, type, m_clock_tick, msg);
}

void Scheduler::enqueueProcess(const std::string& pid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto pcb = ProcessManager::getInstance().getProcess(pid);
    if (!pcb) return;

    pcb->arrival_time = m_clock_tick;
    m_ready_queue.push_back(pid);
    addLog(pid, "Enqueued to Ready Queue at T+" + std::to_string(m_clock_tick), "info");
}

void Scheduler::generateBatchWorkload(int count) {
    std::vector<std::string> new_pids;
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::vector<std::string> accs = {"A101", "A102", "A103"};
        std::vector<double> amounts = {1000.0, 2500.0, 5000.0, 7500.0, 10000.0, 15000.0};

        for (int i = 0; i < count; ++i) {
            int t = gen() % 3;
            std::string from = accs[gen() % accs.size()];
            std::string to = accs[gen() % accs.size()];
            if (t == 2 && from == to) {
                // Ensure distinct for transfer
                to = accs[(accs[0] == from ? 1 : 0)];
            }
            double amt = amounts[gen() % amounts.size()];

            TransactionType type = (t == 0) ? TransactionType::DEPOSIT :
                                   (t == 1) ? TransactionType::WITHDRAW : 
                                              TransactionType::TRANSFER;

            auto pcb = ProcessManager::getInstance().createProcess(type, 
                (type == TransactionType::DEPOSIT ? "" : from),
                (type == TransactionType::WITHDRAW ? "" : to),
                amt);
            
            new_pids.push_back(pcb->pid);
        }
    }

    for (const auto& pid : new_pids) {
        enqueueProcess(pid);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    addLog("SYS", "Batch workload of " + std::to_string(count) + " transactions submitted to OS queue.", "ok");
}

void Scheduler::wakeUpEligibleWaiters() {
    // Check if any process in waiting list can now acquire locks
    auto it = m_waiting_list.begin();
    while (it != m_waiting_list.end()) {
        std::string pid = *it;
        auto pcb = ProcessManager::getInstance().getProcess(pid);
        if (!pcb) {
            it = m_waiting_list.erase(it);
            continue;
        }

        std::vector<std::string> needed;
        if (!pcb->source_account.empty()) needed.push_back(pcb->source_account);
        if (!pcb->destination_account.empty()) needed.push_back(pcb->destination_account);

        // Check if resources are free (without holding them yet, or wake them up to ready queue)
        bool can_run = true;
        for (const auto& r : needed) {
            if (LockManager::getInstance().isResourceLocked(r)) {
                can_run = false;
                break;
            }
        }

        if (can_run) {
            // Wake up! WAITING -> READY
            pcb->state = ProcessState::READY;
            pcb->status_message = "Resource freed; unblocked to Ready Queue";
            m_ready_queue.push_back(pid);
            addLog(pid, "UNBLOCKED: Resource free, moved from WAITING to READY queue", "info");
            it = m_waiting_list.erase(it);
        } else {
            ++it;
        }
    }
}

bool Scheduler::step() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Step A: Wake up any blocked processes whose resources are now released
    wakeUpEligibleWaiters();

    // Step B: If no process is currently on CPU, dispatch one from Ready Queue
    if (m_current_pid.empty()) {
        if (m_ready_queue.empty()) {
            // CPU is idle
            if (m_waiting_list.empty()) {
                return false; // Everything done!
            } else {
                // Deadlock check or waiting for resource
                m_clock_tick++;
                addLog("CPU", "Idle (all active processes are WAITING on held locks)", "warn");
                return true;
            }
        }

        m_current_pid = m_ready_queue.front();
        m_ready_queue.pop_front();
        m_quantum_slice_used = 0;

        auto pcb = ProcessManager::getInstance().getProcess(m_current_pid);
        if (pcb) {
            pcb->state = ProcessState::RUNNING;
            if (pcb->first_run_time < 0) {
                pcb->first_run_time = m_clock_tick;
                pcb->response_time = pcb->first_run_time - pcb->arrival_time;
            }
            addLog(m_current_pid, "DISPATCHED to CPU Core by Round Robin", "info");
        }
    }

    // Step C: Execute 1 tick of the current running process
    executeCurrentProcessTick();
    return true;
}

void Scheduler::executeCurrentProcessTick() {
    auto pcb = ProcessManager::getInstance().getProcess(m_current_pid);
    if (!pcb) {
        m_current_pid = "";
        m_quantum_slice_used = 0;
        return;
    }

    // Attempt lock acquisition if in stage 0 (entering critical section)
    if (pcb->execution_stage == 0) {
        std::vector<std::string> needed;
        if (!pcb->source_account.empty()) needed.push_back(pcb->source_account);
        if (!pcb->destination_account.empty()) needed.push_back(pcb->destination_account);

        std::string blocked_on = "";
        bool acquired = LockManager::getInstance().tryAcquireLocks(pcb->pid, needed, blocked_on);
        if (!acquired) {
            // Cannot get lock: RUNNING -> WAITING (Yield CPU immediately!)
            pcb->state = ProcessState::WAITING;
            pcb->blocked_on_resource = blocked_on;
            pcb->status_message = "WAITING: Account " + blocked_on + " held by " + 
                                  LockManager::getInstance().getResourceOwner(blocked_on);
            
            m_waiting_list.push_back(pcb->pid);
            addLog(pcb->pid, "BLOCKED: Yielded CPU due to lock contention on " + blocked_on, "warn");

            m_current_pid = "";
            m_quantum_slice_used = 0;
            m_clock_tick++;
            return;
        } else {
            pcb->execution_stage = 1;
            pcb->held_locks = needed;
            addLog(pcb->pid, "CRITICAL SECTION: Acquired mutex lock for accounts", "info");
        }
    }

    // Consume 1 CPU Tick
    m_clock_tick++;
    m_total_cpu_busy_ticks++;
    m_quantum_slice_used++;
    pcb->remaining_burst--;
    pcb->total_cpu_used++;

    // Update Gantt History
    if (!m_gantt_history.empty() && 
        m_gantt_history.back().pid == pcb->pid && 
        m_gantt_history.back().end_tick == m_clock_tick - 1) {
        m_gantt_history.back().end_tick = m_clock_tick;
    } else {
        GanttEntry g;
        g.start_tick = m_clock_tick - 1;
        g.end_tick = m_clock_tick;
        g.pid = pcb->pid;
        g.tx_type = txTypeToString(pcb->tx_type);
        m_gantt_history.push_back(g);
        if (m_gantt_history.size() > 40) {
            m_gantt_history.erase(m_gantt_history.begin());
        }
    }

    // Increment waiting time for all other READY processes
    for (const auto& waiting_pid : m_ready_queue) {
        auto other_pcb = ProcessManager::getInstance().getProcess(waiting_pid);
        if (other_pcb) {
            other_pcb->waiting_time++;
        }
    }

    // Check if process finished its burst
    if (pcb->remaining_burst <= 0) {
        // Execute the database transaction
        std::string db_msg = "";
        bool ok = false;
        if (pcb->tx_type == TransactionType::DEPOSIT) {
            ok = DBManager::getInstance().executeDeposit(pcb->destination_account, pcb->amount, db_msg);
        } else if (pcb->tx_type == TransactionType::WITHDRAW) {
            ok = DBManager::getInstance().executeWithdraw(pcb->source_account, pcb->amount, db_msg);
        } else if (pcb->tx_type == TransactionType::TRANSFER) {
            ok = DBManager::getInstance().executeTransfer(pcb->source_account, pcb->destination_account, pcb->amount, db_msg);
        }

        // Release all locks
        LockManager::getInstance().releaseLocks(pcb->pid);
        pcb->held_locks.clear();

        // Terminate process
        if (ok) {
            m_completed_count++;
            ProcessManager::getInstance().recordCompletion(pcb->pid, m_clock_tick, true, db_msg);
            addLog(pcb->pid, "COMMIT SUCCESS: " + db_msg, "ok");
        } else {
            m_failed_count++;
            ProcessManager::getInstance().recordCompletion(pcb->pid, m_clock_tick, false, db_msg);
            addLog(pcb->pid, "ROLLBACK: " + db_msg, "warn");
        }

        // Clear CPU
        m_current_pid = "";
        m_quantum_slice_used = 0;
        return;
    }

    // If not finished, check if time quantum slice expired
    if (m_quantum_slice_used >= m_time_quantum) {
        // Preempt! RUNNING -> READY
        pcb->state = ProcessState::READY;
        pcb->status_message = "Preempted by Round Robin (Quantum expired)";
        m_ready_queue.push_back(pcb->pid);
        addLog(pcb->pid, "PREEMPTED: Time quantum slice expired; re-inserted to Ready Queue", "info");

        // Note: In Bank OS, process continues to hold its lock across preemption to preserve critical section atomicity,
        // or can yield lock if two-phase locking requires it. Holding mutex guarantees sequential transaction serialization!

        m_current_pid = "";
        m_quantum_slice_used = 0;
    }
}

void Scheduler::startContinuous(int interval_ms) {
    if (m_is_running_continuous) return;
    m_is_running_continuous = true;

    m_worker_thread = std::make_unique<std::thread>([this, interval_ms]() {
        while (m_is_running_continuous) {
            bool has_more = step();
            if (!has_more) {
                m_is_running_continuous = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    });
}

void Scheduler::stopContinuous() {
    if (!m_is_running_continuous) return;
    m_is_running_continuous = false;
    if (m_worker_thread && m_worker_thread->joinable()) {
        m_worker_thread->join();
    }
    m_worker_thread.reset();
}

bool Scheduler::isRunningContinuous() const {
    return m_is_running_continuous;
}

void Scheduler::reset() {
    stopContinuous();
    std::lock_guard<std::mutex> lock(m_mutex);

    m_clock_tick = 0;
    m_quantum_slice_used = 0;
    m_total_cpu_busy_ticks = 0;
    m_completed_count = 0;
    m_failed_count = 0;
    m_current_pid = "";

    m_ready_queue.clear();
    m_waiting_list.clear();
    m_gantt_history.clear();
    m_event_logs.clear();

    ProcessManager::getInstance().reset();
    LockManager::getInstance().reset();
    DBManager::getInstance().resetToInitialState();

    addLog("SYS", "Simulator reset to initial state. Database balances restored.", "ok");
}

std::vector<std::string> Scheduler::getReadyQueue() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::vector<std::string>(m_ready_queue.begin(), m_ready_queue.end());
}

std::vector<std::string> Scheduler::getWaitingList() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_waiting_list;
}

std::string Scheduler::getCurrentRunningPID() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_pid;
}

int Scheduler::getRemainingQuantumSlice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::max(0, m_time_quantum - m_quantum_slice_used);
}

int Scheduler::getClockTick() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clock_tick;
}

SchedulerStats Scheduler::getStats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    SchedulerStats s;
    s.clock_tick = m_clock_tick;
    s.total_transactions = (int)ProcessManager::getInstance().getProcessCount();
    s.completed_transactions = m_completed_count;
    s.failed_transactions = m_failed_count;
    s.active_ready_count = (int)m_ready_queue.size();
    s.active_waiting_count = (int)m_waiting_list.size();
    s.time_quantum = m_time_quantum;

    auto all_procs = ProcessManager::getInstance().getAllProcesses();
    int finished_count = 0;
    double total_wt = 0;
    double total_tat = 0;
    double total_rt = 0;
    int rt_count = 0;

    for (const auto& p : all_procs) {
        if (p->state == ProcessState::TERMINATED) {
            finished_count++;
            total_wt += p->waiting_time;
            total_tat += p->turnaround_time;
        }
        if (p->response_time >= 0) {
            rt_count++;
            total_rt += p->response_time;
        }
    }

    s.avg_waiting_time = (finished_count > 0) ? (total_wt / finished_count) : 0.0;
    s.avg_turnaround_time = (finished_count > 0) ? (total_tat / finished_count) : 0.0;
    s.avg_response_time = (rt_count > 0) ? (total_rt / rt_count) : 0.0;
    s.cpu_utilization = (m_clock_tick > 0) ? 
        std::min(100.0, (double)m_total_cpu_busy_ticks / m_clock_tick * 100.0) : 0.0;
    s.throughput = (m_clock_tick > 0) ? 
        (double)(m_completed_count + m_failed_count) / m_clock_tick : 0.0;

    return s;
}

std::vector<GanttEntry> Scheduler::getGanttHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_gantt_history;
}

std::vector<EventLogEntry> Scheduler::getRecentLogs(size_t limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_event_logs.size() <= limit) return m_event_logs;
    return std::vector<EventLogEntry>(m_event_logs.begin(), m_event_logs.begin() + limit);
}

} // namespace BankOS
