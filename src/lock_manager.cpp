#include "lock_manager.hpp"
#include "db_manager.hpp"
#include <iostream>

namespace BankOS {

LockManager& LockManager::getInstance() {
    static LockManager instance;
    return instance;
}

LockManager::LockManager() {
    registerResource("A101");
    registerResource("A102");
    registerResource("A103");
}

void LockManager::registerResource(const std::string& account_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_resource_owner.find(account_id) == m_resource_owner.end()) {
        m_resource_owner[account_id] = "";
        m_known_resources.push_back(account_id);
    }
}

bool LockManager::tryAcquireLocks(const std::string& pid, 
                                  std::vector<std::string> resources, 
                                  std::string& out_blocked_resource) {
    std::lock_guard<std::mutex> lock(m_mutex);
    out_blocked_resource = "";

    // Remove empty entries and duplicates
    resources.erase(std::remove_if(resources.begin(), resources.end(), 
                    [](const std::string& s) { return s.empty(); }), resources.end());
    std::sort(resources.begin(), resources.end());
    resources.erase(std::unique(resources.begin(), resources.end()), resources.end());

    if (resources.empty()) {
        return true; // No resources needed
    }

    // Step 1: Check availability for ALL requested resources (All-or-Nothing / No Hold-and-Wait)
    for (const auto& res : resources) {
        auto it = m_resource_owner.find(res);
        if (it != m_resource_owner.end() && !it->second.empty() && it->second != pid) {
            out_blocked_resource = res;
            return false; // Resource locked by another PID!
        }
    }

    // Step 2: All resources free! Acquire all locks following the sorted global order
    for (const auto& res : resources) {
        m_resource_owner[res] = pid;
        DBManager::getInstance().logLockEvent(pid, res, "ACQUIRED", 0);
    }

    return true;
}

std::vector<std::string> LockManager::releaseLocks(const std::string& pid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> released;

    for (auto& [res, owner] : m_resource_owner) {
        if (owner == pid) {
            owner = "";
            released.push_back(res);
            DBManager::getInstance().logLockEvent(pid, res, "RELEASED", 0);
        }
    }
    return released;
}

std::vector<LockInfo> LockManager::getLockStatus() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<LockInfo> list;
    for (const auto& res : m_known_resources) {
        auto it = m_resource_owner.find(res);
        bool locked = (it != m_resource_owner.end() && !it->second.empty());
        std::string owner = locked ? it->second : "";
        list.push_back({res, locked, owner});
    }
    return list;
}

bool LockManager::isResourceLocked(const std::string& account_id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resource_owner.find(account_id);
    return (it != m_resource_owner.end() && !it->second.empty());
}

std::string LockManager::getResourceOwner(const std::string& account_id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resource_owner.find(account_id);
    if (it != m_resource_owner.end()) {
        return it->second;
    }
    return "";
}

void LockManager::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [res, owner] : m_resource_owner) {
        owner = "";
    }
}

} // namespace BankOS
