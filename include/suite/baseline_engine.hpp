#pragma once

#include "common/types.hpp"
#include "storage/ledger_engine.hpp"
#include "storage/transaction.hpp"
#include "concurrency/lock_manager.hpp"

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>

namespace adaptive_os {

class BaselineEngine {
public:
    BaselineEngine(size_t num_workers,
                   std::shared_ptr<LedgerEngine> ledger,
                   std::shared_ptr<LockManager> lock_mgr,
                   Milliseconds static_lock_timeout = Milliseconds(100))
        : num_workers_(num_workers),
          ledger_(std::move(ledger)),
          lock_mgr_(std::move(lock_mgr)),
          static_lock_timeout_(static_lock_timeout),
          running_(false),
          active_workers_count_(0),
          total_submitted_(0),
          total_committed_(0),
          total_aborted_(0),
          total_retries_(0),
          total_conflicts_(0),
          estimated_context_switches_(0) {}

    ~BaselineEngine() {
        stop();
    }

    void start() {
        if (running_.exchange(true)) return;

        workers_.reserve(num_workers_);
        for (size_t i = 0; i < num_workers_; ++i) {
            workers_.emplace_back(&BaselineEngine::worker_loop, this, i);
        }
    }

    void stop() {
        if (!running_.exchange(false)) return;

        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
        workers_.clear();
    }

    // Submit to simple standard FIFO queue
    bool submit(std::shared_ptr<Transaction> tx) {
        total_submitted_.fetch_add(1, std::memory_order_relaxed);
        tx->set_status(TxStatus::ADMITTED);
        tx->set_admission_time(HighResClock::now());

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.push(tx);
        }
        cv_.notify_one();
        return true;
    }

    uint64_t total_submitted() const { return total_submitted_.load(std::memory_order_relaxed); }
    uint64_t total_committed() const { return total_committed_.load(std::memory_order_relaxed); }
    uint64_t total_aborted() const { return total_aborted_.load(std::memory_order_relaxed); }
    uint64_t total_retries() const { return total_retries_.load(std::memory_order_relaxed); }
    uint64_t total_conflicts() const { return total_conflicts_.load(std::memory_order_relaxed); }
    uint64_t context_switches() const { return estimated_context_switches_.load(std::memory_order_relaxed); }

    const std::vector<double>& recorded_latencies() const {
        std::lock_guard<std::mutex> lock(lat_mutex_);
        return latencies_ms_;
    }

    void reset() {
        total_submitted_.store(0, std::memory_order_relaxed);
        total_committed_.store(0, std::memory_order_relaxed);
        total_aborted_.store(0, std::memory_order_relaxed);
        total_retries_.store(0, std::memory_order_relaxed);
        total_conflicts_.store(0, std::memory_order_relaxed);
        estimated_context_switches_.store(0, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!queue_.empty()) queue_.pop();
        }
        {
            std::lock_guard<std::mutex> lock(lat_mutex_);
            latencies_ms_.clear();
        }
    }

private:
    void worker_loop(size_t worker_id) {
        (void)worker_id;
        while (running_.load(std::memory_order_relaxed)) {
            std::shared_ptr<Transaction> tx = nullptr;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                cv_.wait_for(lock, Milliseconds(30), [this]() {
                    return !queue_.empty() || !running_.load(std::memory_order_relaxed);
                });

                if (!running_.load(std::memory_order_relaxed) && queue_.empty()) {
                    break;
                }

                if (!queue_.empty()) {
                    tx = queue_.front();
                    queue_.pop();
                }
            }

            if (!tx) continue;

            active_workers_count_.fetch_add(1, std::memory_order_relaxed);
            estimated_context_switches_.fetch_add(1, std::memory_order_relaxed);

            execute_baseline_job(tx);

            active_workers_count_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    void execute_baseline_job(std::shared_ptr<Transaction> tx) {
        tx->set_status(TxStatus::IN_FLIGHT);

        bool had_conflict = false;
        // Naive 2PL with static lock timeout
        bool success = ledger_->execute_transaction(tx, static_lock_timeout_, had_conflict);

        if (had_conflict) {
            total_conflicts_.fetch_add(1, std::memory_order_relaxed);
        }

        if (success) {
            total_committed_.fetch_add(1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(lat_mutex_);
                latencies_ms_.push_back(tx->total_latency_ms());
            }
        } else {
            // Eager abort / naive static retry without dynamic backoff (causes abort storms)
            total_aborted_.fetch_add(1, std::memory_order_relaxed);

            constexpr uint32_t MAX_RETRIES = 5;
            if (tx->retry_count() < MAX_RETRIES &&
                tx->status() == TxStatus::ABORTED_LOCK_TIMEOUT) {
                
                tx->increment_retry();
                total_retries_.fetch_add(1, std::memory_order_relaxed);

                // Naive static 1ms sleep without contention adaptation
                std::this_thread::sleep_for(Milliseconds(1));

                std::lock_guard<std::mutex> lock(queue_mutex_);
                queue_.push(tx);
                cv_.notify_one();
            }
        }
    }

    size_t num_workers_;
    std::shared_ptr<LedgerEngine> ledger_;
    std::shared_ptr<LockManager> lock_mgr_;
    Milliseconds static_lock_timeout_;

    std::atomic<bool> running_;
    std::atomic<uint64_t> active_workers_count_;
    std::atomic<uint64_t> total_submitted_;
    std::atomic<uint64_t> total_committed_;
    std::atomic<uint64_t> total_aborted_;
    std::atomic<uint64_t> total_retries_;
    std::atomic<uint64_t> total_conflicts_;
    std::atomic<uint64_t> estimated_context_switches_;

    std::queue<std::shared_ptr<Transaction>> queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;

    mutable std::mutex lat_mutex_;
    std::vector<double> latencies_ms_;

    std::vector<std::thread> workers_;
};

} // namespace adaptive_os
