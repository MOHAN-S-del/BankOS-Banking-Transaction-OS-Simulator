#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>

namespace BankOS {

struct LockInfo {
    std::string resource_id;  // Account ID (e.g. "A101")
    bool is_locked;
    std::string owner_pid;    // Process holding the lock (e.g. "T01")
};

class LockManager {
public:
    static LockManager& getInstance();

    // Register known account resources
    void registerResource(const std::string& account_id);

    // Deadlock-free atomic lock acquisition
    // Uses Dijkstra's Resource Ordering Principle to prevent Circular Wait
    bool tryAcquireLocks(const std::string& pid, 
                         std::vector<std::string> resources, 
                         std::string& out_blocked_resource);

    // Releases all locks held by a process upon preemption or termination
    std::vector<std::string> releaseLocks(const std::string& pid);

    // Query lock states for dashboard visualization
    std::vector<LockInfo> getLockStatus();
    bool isResourceLocked(const std::string& account_id) const;
    std::string getResourceOwner(const std::string& account_id) const;

    void reset();

private:
    LockManager();
    ~LockManager() = default;
    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    mutable std::mutex m_mutex;
    std::map<std::string, std::string> m_resource_owner; // resource_id -> owner_pid (empty if free)
    std::vector<std::string> m_known_resources;
};

} // namespace BankOS
