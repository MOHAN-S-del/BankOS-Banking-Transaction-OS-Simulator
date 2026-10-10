#pragma once

#include "pcb.hpp"
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

namespace BankOS {

class ProcessManager {
public:
    static ProcessManager& getInstance();

    // Lifecycle
    std::shared_ptr<ProcessControlBlock> createProcess(TransactionType type,
                                                       const std::string& from_account,
                                                       const std::string& to_account,
                                                       double amount,
                                                       int custom_burst = 0);

    std::shared_ptr<ProcessControlBlock> getProcess(const std::string& pid);
    std::vector<std::shared_ptr<ProcessControlBlock>> getAllProcesses();
    
    void updateProcessState(const std::string& pid, ProcessState new_state, const std::string& reason = "");
    void recordCompletion(const std::string& pid, int clock_tick, bool committed, const std::string& status_msg);

    void reset();
    size_t getProcessCount() const;

private:
    ProcessManager();
    ~ProcessManager() = default;
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    std::mutex m_mutex;
    std::atomic<int> m_pid_counter;
    std::map<std::string, std::shared_ptr<ProcessControlBlock>> m_process_table;
    std::vector<std::string> m_creation_order;

    std::string generateUUID();
    int estimateBurstTime(TransactionType type);
};

} // namespace BankOS
