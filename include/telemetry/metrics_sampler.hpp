#pragma once

#include "common/types.hpp"
#include "telemetry/sliding_window.hpp"
#include "telemetry/hysteresis.hpp"
#include "concurrency/lock_manager.hpp"
#include "concurrency/token_bucket.hpp"
#include "ml/predictor.hpp"
#include "ml/proactive_quota_manager.hpp"
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>

namespace adaptive_os {

class MetricsSampler {
public:
    MetricsSampler(std::shared_ptr<SlidingWindowTelemetry> sliding_window,
                   std::shared_ptr<HysteresisGate> hysteresis,
                   std::shared_ptr<LockManager> lock_mgr,
                   std::shared_ptr<DynamicTokenBucket> token_bucket,
                   std::shared_ptr<ml::ContentionPredictor> predictor = nullptr,
                   std::shared_ptr<ml::ProactiveQuotaManager> proactive_quota_mgr = nullptr)
        : sliding_window_(std::move(sliding_window)),
          hysteresis_(std::move(hysteresis)),
          lock_manager_(std::move(lock_mgr)),
          token_bucket_(std::move(token_bucket)),
          predictor_(predictor ? predictor : std::make_shared<ml::ContentionPredictor>()),
          proactive_quota_mgr_(proactive_quota_mgr ? proactive_quota_mgr : std::make_shared<ml::ProactiveQuotaManager>()),
          current_tw_ms_(10.0),
          last_c_t_(0.0),
          total_samples_taken_(0) {}

    // Run a sampling step (e.g. called periodically by background telemetry thread or on-demand)
    TelemetrySnapshot sample_step(uint64_t q1_size = 0, uint64_t q2_size = 0, uint64_t q3_size = 0,
                                  uint64_t active_workers = 0, uint64_t starvation_mig = 0,
                                  uint64_t hotspot_dem = 0,
                                  std::shared_ptr<MLFQQueue> mlfq = nullptr,
                                  std::shared_ptr<AntiStarvationAgingEngine> aging = nullptr) {
        std::lock_guard<std::mutex> lock(sample_mutex_);

        // Sample current lock queue depth from LockManager
        uint64_t current_qd = lock_manager_->current_aggregate_queue_depth();
        sliding_window_->record_queue_depth(current_qd);

        double cr_hat = 0.0, ar_hat = 0.0, ql_hat = 0.0, ri_hat = 0.0;
        double c_t = 0.0;
        double avg_lat = 0.0;
        float arrival_vel = 0.0f;
        float batch_ratio = 0.0f;

        sliding_window_->compute_extended_metrics(cr_hat, ar_hat, ql_hat, ri_hat, c_t, avg_lat,
                                                  arrival_vel, batch_ratio);

        // Feed features to ML predictor
        ml::TelemetryFeatureVector feat{
            static_cast<float>(cr_hat),
            static_cast<float>(ar_hat),
            static_cast<float>(ql_hat),
            static_cast<float>(ri_hat),
            arrival_vel,
            batch_ratio
        };
        predictor_->ingest_telemetry_step(feat);
        auto prediction = predictor_->forecast();

        // Proactively evaluate ML forecast and pre-allocate MLFQ priority quotas ahead of time
        proactive_quota_mgr_->evaluate_and_apply(prediction, mlfq, aging, token_bucket_);

        // Effective contention considers max of reactive Ct and predicted Ct ahead of time
        double effective_c_t = std::max(c_t, static_cast<double>(prediction.predicted_contention));

        // Update Hysteresis State using effective contention to eliminate 30ms reactive lag
        HysteresisState state = hysteresis_->update(effective_c_t);

        // Update Token Bucket admission rate proactively
        token_bucket_->update_contention_index(effective_c_t);

        // Calculate bounded waiting timeout: Tw = 2 * AvgLatency (clamped to [5ms, 500ms])
        double calculated_tw = std::clamp(2.0 * avg_lat, 5.0, 500.0);
        current_tw_ms_.store(calculated_tw, std::memory_order_relaxed);
        last_c_t_.store(effective_c_t, std::memory_order_relaxed);

        auto alloc = proactive_quota_mgr_->current_allocation();

        TelemetrySnapshot snap;
        snap.timestamp = HighResClock::now();
        snap.conflict_rate = cr_hat;
        snap.abort_rate = ar_hat;
        snap.lock_queue_depth = ql_hat;
        snap.retry_intensity = ri_hat;
        snap.composite_index = c_t;
        snap.state = state;
        snap.effective_admission_rate = token_bucket_->current_rate();
        snap.admitted_tx_count = token_bucket_->total_admitted();
        snap.committed_tx_count = 0;
        snap.aborted_tx_count = 0;
        snap.conflict_count = lock_manager_->total_conflicts();
        snap.retry_count = 0;
        snap.q1_depth = q1_size;
        snap.q2_depth = q2_size;
        snap.q3_depth = q3_size;
        snap.active_workers = active_workers;
        snap.avg_latency_ms = avg_lat;
        snap.starvation_migrations = starvation_mig;
        snap.hotspot_demotions = hotspot_dem;

        // Predictive ML & Proactive Quotas
        snap.predicted_contention = prediction.predicted_contention;
        snap.forecast_horizon_ms = prediction.forecast_lead_time_ms;
        snap.proactive_policy_mode = ml::proactivePolicyToString(proactive_quota_mgr_->current_state());
        snap.anticipated_regime = ml::workloadRegimeToString(prediction.anticipated_regime);
        snap.proactive_interventions = proactive_quota_mgr_->total_interventions();
        snap.q1_preallocated_quota = alloc.q1_worker_share * 100.0;
        snap.q3_quantum_allocated_ms = alloc.q3_quantum_ms.count();

        latest_snapshot_ = snap;
        total_samples_taken_++;

        return snap;
    }

    std::shared_ptr<ml::ContentionPredictor> predictor() const { return predictor_; }
    std::shared_ptr<ml::ProactiveQuotaManager> proactive_quota_manager() const { return proactive_quota_mgr_; }

    Milliseconds bounded_wait_timeout() const {
        return Milliseconds(static_cast<int64_t>(current_tw_ms_.load(std::memory_order_relaxed)));
    }

    double current_contention_index() const {
        return last_c_t_.load(std::memory_order_relaxed);
    }

    TelemetrySnapshot latest_snapshot() const {
        std::lock_guard<std::mutex> lock(sample_mutex_);
        return latest_snapshot_;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(sample_mutex_);
        hysteresis_->reset();
        token_bucket_->reset_counters();
        lock_manager_->reset_window_counters();
        proactive_quota_mgr_->reset();
        current_tw_ms_.store(10.0, std::memory_order_relaxed);
        last_c_t_.store(0.0, std::memory_order_relaxed);
        latest_snapshot_ = TelemetrySnapshot{};
    }

private:
    std::shared_ptr<SlidingWindowTelemetry> sliding_window_;
    std::shared_ptr<HysteresisGate> hysteresis_;
    std::shared_ptr<LockManager> lock_manager_;
    std::shared_ptr<DynamicTokenBucket> token_bucket_;
    std::shared_ptr<ml::ContentionPredictor> predictor_;
    std::shared_ptr<ml::ProactiveQuotaManager> proactive_quota_mgr_;

    std::atomic<double> current_tw_ms_;
    std::atomic<double> last_c_t_;
    uint64_t total_samples_taken_;
    TelemetrySnapshot latest_snapshot_;
    mutable std::mutex sample_mutex_;
};

} // namespace adaptive_os
