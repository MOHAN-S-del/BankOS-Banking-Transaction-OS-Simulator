#pragma once

#include "common/types.hpp"
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <string>

namespace adaptive_os {

class Account {
public:
    Account(AccountId id, AccountType type, BalanceType initial_balance, std::string owner)
        : id_(id), type_(type), balance_(initial_balance), hold_balance_(0),
          version_(1), owner_(std::move(owner)), lock_waiters_count_(0),
          access_count_(0), contention_hits_(0) {}

    AccountId id() const { return id_; }
    AccountType type() const { return type_; }
    std::string owner() const { return owner_; }

    BalanceType balance() const {
        std::shared_lock<std::shared_mutex> lock(latch_);
        return balance_;
    }

    BalanceType available_balance() const {
        std::shared_lock<std::shared_mutex> lock(latch_);
        return balance_ - hold_balance_;
    }

    uint64_t version() const {
        std::shared_lock<std::shared_mutex> lock(latch_);
        return version_;
    }

    uint64_t lock_waiters() const {
        return lock_waiters_count_.load(std::memory_order_relaxed);
    }

    void increment_waiters() {
        lock_waiters_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void decrement_waiters() {
        lock_waiters_count_.fetch_sub(1, std::memory_order_relaxed);
    }

    void record_access() {
        access_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_contention_hit() {
        contention_hits_.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t contention_hits() const {
        return contention_hits_.load(std::memory_order_relaxed);
    }

    uint64_t access_count() const {
        return access_count_.load(std::memory_order_relaxed);
    }

    // Direct mutators (invoked while holding the transaction/account lock)
    bool apply_debit(BalanceType amount) {
        if (type_ != AccountType::CENTRAL_SETTLEMENT && balance_ < amount) {
            // Central settlement accounts may have credit facility / central bank reserve
            return false;
        }
        balance_ -= amount;
        version_++;
        return true;
    }

    void apply_credit(BalanceType amount) {
        balance_ += amount;
        version_++;
    }

    std::shared_mutex& latch() const { return latch_; }

private:
    AccountId id_;
    AccountType type_;
    BalanceType balance_;
    BalanceType hold_balance_;
    uint64_t version_;
    std::string owner_;

    mutable std::shared_mutex latch_;
    std::atomic<uint64_t> lock_waiters_count_;
    std::atomic<uint64_t> access_count_;
    std::atomic<uint64_t> contention_hits_;
};

} // namespace adaptive_os
