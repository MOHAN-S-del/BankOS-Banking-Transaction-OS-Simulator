#pragma once

#include "common/types.hpp"
#include "storage/account.hpp"
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <set>
#include <atomic>

namespace adaptive_os {

struct LockEntry {
    std::mutex mtx;
    std::condition_variable cv;
    TxId holder_tx_id{0};
    bool is_locked{false};
    uint64_t wait_queue_depth{0};
    uint64_t conflict_count{0};
    TimePoint last_acquired_time;
};

class LockManager {
public:
    explicit LockManager(size_t num_stripes = 1024)
        : total_lock_attempts_(0), total_conflicts_(0), total_timeouts_(0) {
        (void)num_stripes;
    }

    // Acquire lock on a single account with timeout Tw
    bool acquire_lock(AccountId account_id, TxId tx_id, Milliseconds timeout, bool& was_conflict) {
        total_lock_attempts_.fetch_add(1, std::memory_order_relaxed);
        auto& entry = get_entry(account_id);

        std::unique_lock<std::mutex> lock(entry.mtx);
        if (entry.is_locked && entry.holder_tx_id == tx_id) {
            // Reentrant for the same transaction
            was_conflict = false;
            return true;
        }

        if (entry.is_locked) {
            was_conflict = true;
            entry.conflict_count++;
            total_conflicts_.fetch_add(1, std::memory_order_relaxed);
            entry.wait_queue_depth++;

            auto deadline = HighResClock::now() + timeout;
            bool acquired = entry.cv.wait_until(lock, deadline, [&]() {
                return !entry.is_locked;
            });

            entry.wait_queue_depth--;
            if (!acquired) {
                total_timeouts_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        } else {
            was_conflict = false;
        }

        entry.is_locked = true;
        entry.holder_tx_id = tx_id;
        entry.last_acquired_time = HighResClock::now();
        return true;
    }

    // Release lock for account
    void release_lock(AccountId account_id, TxId tx_id) {
        auto& entry = get_entry(account_id);
        {
            std::lock_guard<std::mutex> lock(entry.mtx);
            if (entry.is_locked && entry.holder_tx_id == tx_id) {
                entry.is_locked = false;
                entry.holder_tx_id = 0;
            }
        }
        entry.cv.notify_one();
    }

    // Release all locks held by a transaction
    void release_all_locks(const std::vector<AccountId>& accounts, TxId tx_id) {
        for (AccountId acc_id : accounts) {
            release_lock(acc_id, tx_id);
        }
    }

    // Strict Lock Ordering Deadlock Prevention for Transfers:
    // Always lock the smaller Account ID first, regardless of sender/receiver.
    bool acquire_transfer_locks(AccountId from_acc, AccountId to_acc, TxId tx_id, 
                                Milliseconds timeout, bool& was_conflict) {
        AccountId first_id = std::min(from_acc, to_acc);
        AccountId second_id = std::max(from_acc, to_acc);

        bool c1 = false;
        if (!acquire_lock(first_id, tx_id, timeout, c1)) {
            was_conflict = c1;
            return false;
        }

        if (first_id == second_id) {
            was_conflict = c1;
            return true;
        }

        bool c2 = false;
        if (!acquire_lock(second_id, tx_id, timeout, c2)) {
            release_lock(first_id, tx_id);
            was_conflict = true;
            return false;
        }

        was_conflict = (c1 || c2);
        return true;
    }

    void release_transfer_locks(AccountId from_acc, AccountId to_acc, TxId tx_id) {
        AccountId first_id = std::min(from_acc, to_acc);
        AccountId second_id = std::max(from_acc, to_acc);
        if (first_id != second_id) {
            release_lock(second_id, tx_id);
        }
        release_lock(first_id, tx_id);
    }

    // Telemetry helpers
    uint64_t total_attempts() const { return total_lock_attempts_.load(std::memory_order_relaxed); }
    uint64_t total_conflicts() const { return total_conflicts_.load(std::memory_order_relaxed); }
    uint64_t total_timeouts() const { return total_timeouts_.load(std::memory_order_relaxed); }

    void reset_window_counters() {
        total_lock_attempts_.store(0, std::memory_order_relaxed);
        total_conflicts_.store(0, std::memory_order_relaxed);
        total_timeouts_.store(0, std::memory_order_relaxed);
    }

    // Measure total lock queue depth across all entries
    uint64_t current_aggregate_queue_depth() const {
        uint64_t depth = 0;
        for (const auto& entry_ptr : entries_pool_) {
            if (entry_ptr) {
                depth += entry_ptr->wait_queue_depth;
            }
        }
        return depth;
    }

    // Check if account is a known hotspot
    bool is_hotspot(AccountId account_id, uint64_t conflict_threshold = 5) const {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = entries_map_.find(account_id);
        if (it != entries_map_.end()) {
            return (it->second->wait_queue_depth > 0 || it->second->conflict_count >= conflict_threshold);
        }
        return false;
    }

    std::vector<AccountId> get_hotspots(uint64_t conflict_threshold = 5) const {
        std::vector<AccountId> hot;
        std::lock_guard<std::mutex> lock(pool_mutex_);
        for (const auto& [acc_id, entry_ptr] : entries_map_) {
            if (entry_ptr->wait_queue_depth > 0 || entry_ptr->conflict_count >= conflict_threshold) {
                hot.push_back(acc_id);
            }
        }
        return hot;
    }

private:
    LockEntry& get_entry(AccountId account_id) {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = entries_map_.find(account_id);
        if (it == entries_map_.end()) {
            auto entry_ptr = std::make_shared<LockEntry>();
            entries_map_[account_id] = entry_ptr;
            entries_pool_.push_back(entry_ptr);
            return *entry_ptr;
        }
        return *(it->second);
    }

    std::atomic<uint64_t> total_lock_attempts_;
    std::atomic<uint64_t> total_conflicts_;
    std::atomic<uint64_t> total_timeouts_;

    mutable std::mutex pool_mutex_;
    std::unordered_map<AccountId, std::shared_ptr<LockEntry>> entries_map_;
    std::vector<std::shared_ptr<LockEntry>> entries_pool_;
};

} // namespace adaptive_os
