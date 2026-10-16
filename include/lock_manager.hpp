#pragma once

#include "pcb.hpp"
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

namespace bank_sim {

/**
 * Account Lock Entry
 * Per-account mutual exclusion state.
 */
struct AccountLockEntry {
    std::mutex mtx;
    std::condition_variable cv;
    int holder_pid{0};
    bool is_locked{false};
    uint64_t wait_queue_depth{0};
    uint64_t conflict_count{0};
    std::chrono::steady_clock::time_point last_acquired_time;
};

/**
 * LockManager
 * Handles mutual exclusion and strict lock ordering for deadlock prevention.
 *
 * DEADLOCK PREVENTION PRINCIPLE:
 * Implements strict global lock ordering (Havender's principle / Coffman condition #4 denial).
 * When a process transfers funds between FromAccount and ToAccount, the system
 * MUST always lock the smaller Account ID first:
 *      FirstLock  = min(FromAccount, ToAccount)
 *      SecondLock = max(FromAccount, ToAccount)
 * Because all transactions acquire resources in monotonically increasing ID order,
 * circular wait conditions (A waits for B while B waits for A) are mathematically impossible.
 */
class LockManager {
public:
    LockManager()
        : total_lock_attempts_(0),
          total_conflicts_(0),
          total_timeouts_(0),
          total_ordered_acquisitions_(0),
          deadlocks_prevented_(0) {}

    /**
     * Acquire lock on a single account with timeout.
     */
    bool acquire_lock(int account_id, int pid, std::chrono::milliseconds timeout, bool& was_conflict) {
        total_lock_attempts_.fetch_add(1, std::memory_order_relaxed);
        auto entry = get_entry(account_id);

        std::unique_lock<std::mutex> lock(entry->mtx);
        if (entry->is_locked && entry->holder_pid == pid) {
            // Reentrant for same process
            was_conflict = false;
            return true;
        }

        if (entry->is_locked) {
            was_conflict = true;
            entry->conflict_count++;
            total_conflicts_.fetch_add(1, std::memory_order_relaxed);
            entry->wait_queue_depth++;

            auto deadline = std::chrono::steady_clock::now() + timeout;
            bool acquired = entry->cv.wait_until(lock, deadline, [&]() {
                return !entry->is_locked;
            });

            entry->wait_queue_depth--;
            if (!acquired) {
                total_timeouts_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        } else {
            was_conflict = false;
        }

        entry->is_locked = true;
        entry->holder_pid = pid;
        entry->last_acquired_time = std::chrono::steady_clock::now();
        return true;
    }

    /**
     * Non-blocking try-lock for a single account.
     */
    bool try_acquire_lock(int account_id, int pid) {
        auto entry = get_entry(account_id);
        std::unique_lock<std::mutex> lock(entry->mtx, std::try_to_lock);
        if (!lock.owns_lock()) return false;

        if (entry->is_locked && entry->holder_pid == pid) return true;
        if (entry->is_locked) return false;

        entry->is_locked = true;
        entry->holder_pid = pid;
        entry->last_acquired_time = std::chrono::steady_clock::now();
        return true;
    }

    /**
     * Release lock on a single account.
     */
    void release_lock(int account_id, int pid) {
        auto entry = get_entry(account_id);
        {
            std::lock_guard<std::mutex> lock(entry->mtx);
            if (entry->is_locked && entry->holder_pid == pid) {
                entry->is_locked = false;
                entry->holder_pid = 0;
            }
        }
        entry->cv.notify_one();
    }

    /**
     * STRICT LOCK ORDERING TRANSFER ACQUISITION
     * Locks both Account IDs for transfer execution.
     * Always acquires min(from, to) FIRST, then max(from, to).
     * Prevents circular wait condition and guarantees deadlock freedom.
     */
    bool acquire_transfer_locks(int from_account, int to_account, int pid, 
                                std::chrono::milliseconds timeout, bool& was_conflict) {
        int first_id = std::min(from_account, to_account);
        int second_id = std::max(from_account, to_account);

        // Track deadlock prevention potential
        if (from_account > to_account) {
            deadlocks_prevented_.fetch_add(1, std::memory_order_relaxed);
        }

        // 1. Lock smaller account ID first
        bool c1 = false;
        if (!acquire_lock(first_id, pid, timeout, c1)) {
            was_conflict = c1;
            return false;
        }

        if (first_id == second_id) {
            was_conflict = c1;
            total_ordered_acquisitions_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        // 2. Lock larger account ID second
        bool c2 = false;
        if (!acquire_lock(second_id, pid, timeout, c2)) {
            // Rollback first lock to avoid hold-and-wait!
            release_lock(first_id, pid);
            was_conflict = true;
            return false;
        }

        was_conflict = (c1 || c2);
        total_ordered_acquisitions_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /**
     * Non-blocking try-acquire with strict lock ordering.
     */
    bool try_acquire_transfer_locks(int from_account, int to_account, int pid) {
        int first_id = std::min(from_account, to_account);
        int second_id = std::max(from_account, to_account);

        if (from_account > to_account) {
            deadlocks_prevented_.fetch_add(1, std::memory_order_relaxed);
        }

        if (!try_acquire_lock(first_id, pid)) {
            return false;
        }

        if (first_id == second_id) {
            return true;
        }

        if (!try_acquire_lock(second_id, pid)) {
            release_lock(first_id, pid); // Release already held lock
            return false;
        }

        total_ordered_acquisitions_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /**
     * Release transfer locks for both accounts.
     */
    void release_transfer_locks(int from_account, int to_account, int pid) {
        int first_id = std::min(from_account, to_account);
        int second_id = std::max(from_account, to_account);

        if (first_id != second_id) {
            release_lock(second_id, pid);
        }
        release_lock(first_id, pid);
    }

    /**
     * Check if an account is currently locked.
     */
    bool is_locked(int account_id) {
        auto entry = get_entry(account_id);
        std::lock_guard<std::mutex> lock(entry->mtx);
        return entry->is_locked;
    }

    int get_holder(int account_id) {
        auto entry = get_entry(account_id);
        std::lock_guard<std::mutex> lock(entry->mtx);
        return entry->holder_pid;
    }

    // Telemetry and statistics
    uint64_t total_attempts() const { return total_lock_attempts_.load(std::memory_order_relaxed); }
    uint64_t total_conflicts() const { return total_conflicts_.load(std::memory_order_relaxed); }
    uint64_t total_timeouts() const { return total_timeouts_.load(std::memory_order_relaxed); }
    uint64_t total_ordered_acquisitions() const { return total_ordered_acquisitions_.load(std::memory_order_relaxed); }
    uint64_t deadlocks_prevented() const { return deadlocks_prevented_.load(std::memory_order_relaxed); }

    void reset() {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        for (auto& [id, entry] : entries_map_) {
            std::lock_guard<std::mutex> lk(entry->mtx);
            entry->is_locked = false;
            entry->holder_pid = 0;
            entry->wait_queue_depth = 0;
            entry->conflict_count = 0;
        }
        total_lock_attempts_.store(0);
        total_conflicts_.store(0);
        total_timeouts_.store(0);
        total_ordered_acquisitions_.store(0);
        deadlocks_prevented_.store(0);
    }

private:
    std::shared_ptr<AccountLockEntry> get_entry(int account_id) {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto it = entries_map_.find(account_id);
        if (it == entries_map_.end()) {
            auto entry = std::make_shared<AccountLockEntry>();
            entries_map_[account_id] = entry;
            return entry;
        }
        return it->second;
    }

    std::atomic<uint64_t> total_lock_attempts_;
    std::atomic<uint64_t> total_conflicts_;
    std::atomic<uint64_t> total_timeouts_;
    std::atomic<uint64_t> total_ordered_acquisitions_;
    std::atomic<uint64_t> deadlocks_prevented_;

    mutable std::mutex pool_mutex_;
    std::unordered_map<int, std::shared_ptr<AccountLockEntry>> entries_map_;
};

} // namespace bank_sim
