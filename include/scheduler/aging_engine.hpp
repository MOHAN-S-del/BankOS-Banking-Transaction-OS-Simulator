#pragma once

#include "common/types.hpp"
#include "scheduler/mlfq_queue.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

namespace adaptive_os {

class AntiStarvationAgingEngine {
public:
    AntiStarvationAgingEngine(std::shared_ptr<MLFQQueue> mlfq,
                              double aging_threshold_ms = 150.0,
                              Milliseconds check_interval = Milliseconds(50))
        : mlfq_(std::move(mlfq)),
          aging_threshold_ms_(aging_threshold_ms),
          check_interval_(check_interval),
          running_(false),
          total_migrations_(0) {}

    ~AntiStarvationAgingEngine() {
        stop();
    }

    void start() {
        if (running_.exchange(true)) return;
        worker_thread_ = std::thread(&AntiStarvationAgingEngine::loop, this);
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    uint64_t total_migrations() const {
        return total_migrations_.load(std::memory_order_relaxed);
    }

    void set_aging_threshold_ms(double ms) {
        aging_threshold_ms_.store(ms, std::memory_order_relaxed);
    }

    double aging_threshold_ms() const {
        return aging_threshold_ms_.load(std::memory_order_relaxed);
    }

    void reset() {
        total_migrations_.store(0, std::memory_order_relaxed);
    }

private:
    void loop() {
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(check_interval_);
            if (!running_.load(std::memory_order_relaxed)) break;

            double thresh = aging_threshold_ms_.load(std::memory_order_relaxed);
            uint64_t migrated = mlfq_->run_aging(thresh);
            if (migrated > 0) {
                total_migrations_.fetch_add(migrated, std::memory_order_relaxed);
            }
        }
    }

    std::shared_ptr<MLFQQueue> mlfq_;
    std::atomic<double> aging_threshold_ms_;
    Milliseconds check_interval_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> total_migrations_;
    std::thread worker_thread_;
};

} // namespace adaptive_os
