#pragma once

#include "common/types.hpp"
#include "storage/ledger_engine.hpp"
#include "storage/transaction.hpp"
#include "scheduler/mlfq_queue.hpp"
#include "scheduler/aging_engine.hpp"
#include "telemetry/metrics_sampler.hpp"
#include "concurrency/backoff.hpp"
#include "concurrency/token_bucket.hpp"
#include "concurrency/lock_manager.hpp"

#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <iostream>
#include <mutex>

namespace adaptive_os {

class AdaptiveScheduler {
public:
    AdaptiveScheduler(size_t num_workers,
                      std::shared_ptr<LedgerEngine> ledger,
                      std::shared_ptr<LockManager> lock_mgr,
                      std::shared_ptr<MLFQQueue> mlfq,
                      std::shared_ptr<AntiStarvationAgingEngine> aging_engine,
                      std::shared_ptr<MetricsSampler> sampler,
                      std::shared_ptr<DynamicTokenBucket> token_bucket,
                      std::shared_ptr<AdaptiveBackoffController> backoff_ctrl,
                      std::shared_ptr<SlidingWindowTelemetry> telemetry)
        : num_workers_(num_workers),
          ledger_(std::move(ledger)),
          lock_mgr_(std::move(lock_mgr)),
          mlfq_(std::move(mlfq)),
          aging_engine_(std::move(aging_engine)),
          sampler_(std::move(sampler)),
          token_bucket_(std::move(token_bucket)),
          backoff_ctrl_(std::move(backoff_ctrl)),
          telemetry_(std::move(telemetry)),
          running_(false),
          active_workers_count_(0),
          total_submitted_(0),
          total_committed_(0),
          total_aborted_(0),
          total_retries_(0),
          total_throttled_(0),
          total_hotspot_demotions_(0),
          estimated_context_switches_(0) {}

    ~AdaptiveScheduler() {
        stop();
    }

    void start() {
        if (running_.exchange(true)) return;

        aging_engine_->start();

        // Start worker pool
        workers_.reserve(num_workers_);
        for (size_t i = 0; i < num_workers_; ++i) {
            workers_.emplace_back(&AdaptiveScheduler::worker_loop, this, i);
        }

        // Start background feedback regulator thread (every 20ms)
        feedback_thread_ = std::thread(&AdaptiveScheduler::feedback_loop, this);
    }

    void stop() {
        if (!running_.exchange(false)) return;

        mlfq_->stop();
        aging_engine_->stop();

        if (feedback_thread_.joinable()) {
            feedback_thread_.join();
        }

        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
        workers_.clear();
    }

    // Submit a transaction to the scheduler
    bool submit(std::shared_ptr<Transaction> tx) {
        total_submitted_.fetch_add(1, std::memory_order_relaxed);

        // Admission throttling policy: check Token Bucket lambda_eff
        if (!token_bucket_->try_acquire(1.0)) {
            total_throttled_.fetch_add(1, std::memory_order_relaxed);
            tx->set_status(TxStatus::DROPPED_OVERLOAD);
            tx->mark_completed();
            return false;
        }

        tx->set_status(TxStatus::ADMITTED);
        tx->set_admission_time(HighResClock::now());

        telemetry_->record_tx_attempt();
        mlfq_->push(tx);
        return true;
    }

    // Stats
    size_t num_workers() const { return num_workers_; }
    uint64_t active_workers() const { return active_workers_count_.load(std::memory_order_relaxed); }
    uint64_t total_submitted() const { return total_submitted_.load(std::memory_order_relaxed); }
    uint64_t total_committed() const { return total_committed_.load(std::memory_order_relaxed); }
    uint64_t total_aborted() const { return total_aborted_.load(std::memory_order_relaxed); }
    uint64_t total_retries() const { return total_retries_.load(std::memory_order_relaxed); }
    uint64_t total_throttled() const { return total_throttled_.load(std::memory_order_relaxed); }
    uint64_t total_hotspot_demotions() const { return total_hotspot_demotions_.load(std::memory_order_relaxed); }
    uint64_t context_switches() const { return estimated_context_switches_.load(std::memory_order_relaxed); }

    void reset_counters() {
        total_submitted_.store(0, std::memory_order_relaxed);
        total_committed_.store(0, std::memory_order_relaxed);
        total_aborted_.store(0, std::memory_order_relaxed);
        total_retries_.store(0, std::memory_order_relaxed);
        total_throttled_.store(0, std::memory_order_relaxed);
        total_hotspot_demotions_.store(0, std::memory_order_relaxed);
        estimated_context_switches_.store(0, std::memory_order_relaxed);
        mlfq_->reset();
        aging_engine_->reset();
    }

private:
    void feedback_loop() {
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(Milliseconds(20));
            if (!running_.load(std::memory_order_relaxed)) break;

            // 1. Telemetry sampling step
            sampler_->sample_step(mlfq_->q1_size(), mlfq_->q2_size(), mlfq_->q3_size(),
                                  active_workers_count_.load(std::memory_order_relaxed),
                                  aging_engine_->total_migrations(),
                                  total_hotspot_demotions_.load(std::memory_order_relaxed));

            // 2. Hotspot Demotion Feedback:
            // Check identified hot accounts from LockManager
            auto hot_accounts = lock_mgr_->get_hotspots(3);
            if (!hot_accounts.empty()) {
                uint64_t demoted = mlfq_->apply_hotspot_demotion(hot_accounts);
                if (demoted > 0) {
                    total_hotspot_demotions_.fetch_add(demoted, std::memory_order_relaxed);
                }
            }
        }
    }

    void worker_loop(size_t worker_id) {
        (void)worker_id;
        while (running_.load(std::memory_order_relaxed)) {
            auto tx = mlfq_->pop(Milliseconds(30));
            if (!tx) {
                continue;
            }

            active_workers_count_.fetch_add(1, std::memory_order_relaxed);
            estimated_context_switches_.fetch_add(1, std::memory_order_relaxed);

            execute_job(tx);

            active_workers_count_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    void execute_job(std::shared_ptr<Transaction> tx) {
        tx->set_status(TxStatus::IN_FLIGHT);

        // Bounded lock waiting Tw = 2 * AvgLatency from dynamic sampler
        Milliseconds tw = sampler_->bounded_wait_timeout();
        bool had_conflict = false;

        bool success = ledger_->execute_transaction(tx, tw, had_conflict);

        if (had_conflict) {
            telemetry_->record_conflict();
        }

        if (success) {
            total_committed_.fetch_add(1, std::memory_order_relaxed);
            telemetry_->record_latency(tx->total_latency_ms());
        } else {
            // Execution failed / lock timed out -> controlled rollback happened in ledger
            telemetry_->record_abort();
            total_aborted_.fetch_add(1, std::memory_order_relaxed);

            // Check retry eligibility
            constexpr uint32_t MAX_RETRIES = 5;
            if (tx->retry_count() < MAX_RETRIES &&
                tx->status() == TxStatus::ABORTED_LOCK_TIMEOUT) {
                
                tx->increment_retry();
                total_retries_.fetch_add(1, std::memory_order_relaxed);
                telemetry_->record_retry();

                // Calculate jittered exponential backoff
                // Backoff = B0 * (1 + alpha * Ct) * 2^r + jitter
                double ct = sampler_->current_contention_index();
                Milliseconds delay = backoff_ctrl_->calculate_delay(tx->retry_count(), ct);

                // Backoff sleep
                std::this_thread::sleep_for(delay);

                // Re-enqueue into MLFQ (keep priority or demote if hotspot)
                if (tx->touches_hotspot() && tx->current_tier() == QueueTier::Q1_HIGH_INTERACTIVE) {
                    tx->set_tier(QueueTier::Q2_MED_STANDARD);
                }

                mlfq_->push(tx);
            }
        }
    }

    size_t num_workers_;
    std::shared_ptr<LedgerEngine> ledger_;
    std::shared_ptr<LockManager> lock_mgr_;
    std::shared_ptr<MLFQQueue> mlfq_;
    std::shared_ptr<AntiStarvationAgingEngine> aging_engine_;
    std::shared_ptr<MetricsSampler> sampler_;
    std::shared_ptr<DynamicTokenBucket> token_bucket_;
    std::shared_ptr<AdaptiveBackoffController> backoff_ctrl_;
    std::shared_ptr<SlidingWindowTelemetry> telemetry_;

    std::atomic<bool> running_;
    std::atomic<uint64_t> active_workers_count_;
    std::atomic<uint64_t> total_submitted_;
    std::atomic<uint64_t> total_committed_;
    std::atomic<uint64_t> total_aborted_;
    std::atomic<uint64_t> total_retries_;
    std::atomic<uint64_t> total_throttled_;
    std::atomic<uint64_t> total_hotspot_demotions_;
    std::atomic<uint64_t> estimated_context_switches_;

    std::vector<std::thread> workers_;
    std::thread feedback_thread_;
};

} // namespace adaptive_os
