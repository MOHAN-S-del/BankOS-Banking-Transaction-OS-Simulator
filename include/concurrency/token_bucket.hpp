#pragma once

#include "common/types.hpp"
#include <mutex>
#include <chrono>
#include <algorithm>
#include <random>

namespace adaptive_os {

class DynamicTokenBucket {
public:
    DynamicTokenBucket(double max_rate_per_sec = 5000.0, double burst_capacity = 2000.0)
        : max_rate_(max_rate_per_sec),
          current_rate_(max_rate_per_sec),
          capacity_(burst_capacity),
          tokens_(burst_capacity),
          last_refill_time_(HighResClock::now()),
          total_admitted_(0),
          total_throttled_(0) {}

    // Update effective admission rate based on composite contention index Ct
    // lambda_eff = lambda_max * (1 - Ct)
    void update_contention_index(double contention_index) {
        std::lock_guard<std::mutex> lock(mtx_);
        double c_clamped = std::clamp(contention_index, 0.0, 1.0);
        
        // Ensure a minimum trickle rate (e.g. 5% of max) so system does not freeze entirely
        double min_rate = std::max(10.0, max_rate_ * 0.05);
        current_rate_ = std::max(min_rate, max_rate_ * (1.0 - c_clamped));
    }

    // Attempt to acquire token (non-blocking)
    bool try_acquire(double requested_tokens = 1.0) {
        std::lock_guard<std::mutex> lock(mtx_);
        refill();

        if (tokens_ >= requested_tokens) {
            tokens_ -= requested_tokens;
            total_admitted_++;
            return true;
        }

        total_throttled_++;
        return false;
    }

    double current_rate() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return current_rate_;
    }

    double max_rate() const { return max_rate_; }

    uint64_t total_admitted() const {
        return total_admitted_.load(std::memory_order_relaxed);
    }

    uint64_t total_throttled() const {
        return total_throttled_.load(std::memory_order_relaxed);
    }

    void reset_counters() {
        total_admitted_.store(0, std::memory_order_relaxed);
        total_throttled_.store(0, std::memory_order_relaxed);
    }

private:
    void refill() {
        auto now = HighResClock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_refill_time_).count();
        if (elapsed_sec > 0.0) {
            double new_tokens = elapsed_sec * current_rate_;
            tokens_ = std::min(capacity_, tokens_ + new_tokens);
            last_refill_time_ = now;
        }
    }

    mutable std::mutex mtx_;
    double max_rate_;
    double current_rate_;
    double capacity_;
    double tokens_;
    TimePoint last_refill_time_;

    std::atomic<uint64_t> total_admitted_;
    std::atomic<uint64_t> total_throttled_;
};

} // namespace adaptive_os
