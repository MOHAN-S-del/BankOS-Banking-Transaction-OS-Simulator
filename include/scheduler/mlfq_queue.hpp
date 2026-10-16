#pragma once

#include "common/types.hpp"
#include "storage/transaction.hpp"
#include <deque>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <algorithm>

namespace adaptive_os {

// Comparator for Shortest Remaining Time First (SRTF) in Queue 1
struct SRTFComparator {
    bool operator()(const std::shared_ptr<Transaction>& a, const std::shared_ptr<Transaction>& b) const {
        if (a->remaining_operations() != b->remaining_operations()) {
            return a->remaining_operations() > b->remaining_operations(); // Smaller remaining ops first
        }
        return a->arrival_time() > b->arrival_time(); // Tie-break: FIFO
    }
};

class MLFQQueue {
public:
    MLFQQueue()
        : stopped_(false),
          q1_quantum_ms_(10),
          q2_quantum_ms_(50),
          q3_quantum_ms_(200) {}

    // Configure dynamic tier quanta for proactive allocation
    void set_tier_quanta(Milliseconds q1, Milliseconds q2, Milliseconds q3) {
        q1_quantum_ms_.store(q1.count(), std::memory_order_relaxed);
        q2_quantum_ms_.store(q2.count(), std::memory_order_relaxed);
        q3_quantum_ms_.store(q3.count(), std::memory_order_relaxed);
    }

    Milliseconds get_tier_quantum(QueueTier tier) const {
        switch (tier) {
            case QueueTier::Q1_HIGH_INTERACTIVE:
                return Milliseconds(q1_quantum_ms_.load(std::memory_order_relaxed));
            case QueueTier::Q2_MED_STANDARD:
                return Milliseconds(q2_quantum_ms_.load(std::memory_order_relaxed));
            case QueueTier::Q3_LOW_BATCH:
                return Milliseconds(q3_quantum_ms_.load(std::memory_order_relaxed));
            default:
                return Milliseconds(10);
        }
    }

    // Enqueue transaction into its specified tier
    void push(std::shared_ptr<Transaction> tx) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopped_) return;

        tx->set_last_scheduled_time(HighResClock::now());
        tx->set_allocated_quantum(get_tier_quantum(tx->current_tier()));

        switch (tx->current_tier()) {
            case QueueTier::Q1_HIGH_INTERACTIVE:
                q1_.push(tx);
                break;
            case QueueTier::Q2_MED_STANDARD:
                q2_.push_back(tx);
                break;
            case QueueTier::Q3_LOW_BATCH:
                q3_.push_back(tx);
                break;
        }

        cv_.notify_one();
    }

    // Try pop the highest-priority transaction (blocking or non-blocking)
    std::shared_ptr<Transaction> pop(Milliseconds timeout = Milliseconds(50)) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (empty_unlocked()) {
            cv_.wait_for(lock, timeout, [this]() {
                return !empty_unlocked() || stopped_;
            });
        }

        if (stopped_ && empty_unlocked()) {
            return nullptr;
        }

        // Tier 1: High Priority (SRTF)
        if (!q1_.empty()) {
            auto tx = q1_.top();
            q1_.pop();
            return tx;
        }

        // Tier 2: Medium Priority (FIFO)
        if (!q2_.empty()) {
            auto tx = q2_.front();
            q2_.pop_front();
            return tx;
        }

        // Tier 3: Low Priority Batch (FIFO)
        if (!q3_.empty()) {
            auto tx = q3_.front();
            q3_.pop_front();
            return tx;
        }

        return nullptr;
    }

    // Anti-starvation aging: migrate jobs exceeding aging_threshold_ms
    // Q3 -> Q2 -> Q1
    uint64_t run_aging(double aging_threshold_ms) {
        std::lock_guard<std::mutex> lock(mtx_);
        uint64_t migrated = 0;
        auto now = HighResClock::now();

        // 1. Check Q3 -> migrate to Q2
        std::deque<std::shared_ptr<Transaction>> remaining_q3;
        while (!q3_.empty()) {
            auto tx = q3_.front();
            q3_.pop_front();
            double wait_ms = std::chrono::duration<double, std::milli>(now - tx->last_scheduled_time()).count();
            if (wait_ms >= aging_threshold_ms) {
                tx->set_tier(QueueTier::Q2_MED_STANDARD);
                tx->set_last_scheduled_time(now);
                q2_.push_back(tx);
                migrated++;
            } else {
                remaining_q3.push_back(tx);
            }
        }
        q3_ = std::move(remaining_q3);

        // 2. Check Q2 -> migrate to Q1
        std::deque<std::shared_ptr<Transaction>> remaining_q2;
        while (!q2_.empty()) {
            auto tx = q2_.front();
            q2_.pop_front();
            double wait_ms = std::chrono::duration<double, std::milli>(now - tx->last_scheduled_time()).count();
            if (wait_ms >= aging_threshold_ms) {
                tx->set_tier(QueueTier::Q1_HIGH_INTERACTIVE);
                tx->set_last_scheduled_time(now);
                q1_.push(tx);
                migrated++;
            } else {
                remaining_q2.push_back(tx);
            }
        }
        q2_ = std::move(remaining_q2);

        if (migrated > 0) {
            cv_.notify_all();
        }

        return migrated;
    }

    // Hotspot demotion feedback: if transaction touches hot account, demote or throttle quantum
    uint64_t apply_hotspot_demotion(const std::vector<AccountId>& hot_accounts) {
        if (hot_accounts.empty()) return 0;

        std::lock_guard<std::mutex> lock(mtx_);
        uint64_t demoted_count = 0;

        // Check Q1 items that touch hotspots and demote to Q2 to prevent convoying
        std::vector<std::shared_ptr<Transaction>> temp_q1;
        while (!q1_.empty()) {
            temp_q1.push_back(q1_.top());
            q1_.pop();
        }

        for (auto& tx : temp_q1) {
            bool touches_hot = false;
            for (AccountId hot_id : hot_accounts) {
                const auto& inv = tx->involved_accounts();
                if (std::find(inv.begin(), inv.end(), hot_id) != inv.end()) {
                    touches_hot = true;
                    break;
                }
            }

            if (touches_hot && tx->type() != TxType::RETAIL_P2P) {
                // Demote to Q2 and reduce quantum to prevent worker convoying
                tx->set_tier(QueueTier::Q2_MED_STANDARD);
                tx->set_touches_hotspot(true);
                tx->set_demoted_by_hotspot(true);
                tx->set_allocated_quantum(Milliseconds(15)); // Throttled quantum
                q2_.push_back(tx);
                demoted_count++;
            } else {
                q1_.push(tx);
            }
        }

        return demoted_count;
    }

    size_t q1_size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return q1_.size();
    }

    size_t q2_size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return q2_.size();
    }

    size_t q3_size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return q3_.size();
    }

    size_t total_size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return q1_.size() + q2_.size() + q3_.size();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mtx_);
        stopped_ = false;
        while (!q1_.empty()) q1_.pop();
        q2_.clear();
        q3_.clear();
    }

private:
    bool empty_unlocked() const {
        return q1_.empty() && q2_.empty() && q3_.empty();
    }

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool stopped_;

    // Tier 1: High Priority (SRTF)
    std::priority_queue<std::shared_ptr<Transaction>,
                        std::vector<std::shared_ptr<Transaction>>,
                        SRTFComparator> q1_;

    // Tier 2: Medium Priority (FIFO)
    std::deque<std::shared_ptr<Transaction>> q2_;

    // Tier 3: Low Priority Batch (FIFO)
    std::deque<std::shared_ptr<Transaction>> q3_;

    // Configurable MLFQ Priority Quotas
    std::atomic<int64_t> q1_quantum_ms_;
    std::atomic<int64_t> q2_quantum_ms_;
    std::atomic<int64_t> q3_quantum_ms_;
};

} // namespace adaptive_os
