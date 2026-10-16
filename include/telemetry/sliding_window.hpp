#pragma once

#include "common/types.hpp"
#include <vector>
#include <mutex>
#include <algorithm>
#include <cmath>

namespace adaptive_os {

struct WindowBucket {
    uint64_t total_tx_attempts{0};
    uint64_t batch_tx_count{0};
    uint64_t conflict_events{0};
    uint64_t abort_events{0};
    uint64_t retry_events{0};
    uint64_t sum_queue_depth{0};
    uint64_t queue_depth_samples{0};
    double sum_latency_ms{0.0};
    uint64_t latency_samples{0};
};

class SlidingWindowTelemetry {
public:
    explicit SlidingWindowTelemetry(size_t window_buckets = 10, Milliseconds bucket_duration = Milliseconds(100))
        : num_buckets_(window_buckets), bucket_duration_(bucket_duration),
          current_bucket_idx_(0), last_rotate_time_(HighResClock::now()),
          buckets_(window_buckets) {}

    void record_tx_attempt(bool is_batch = false) {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();
        buckets_[current_bucket_idx_].total_tx_attempts++;
        if (is_batch) {
            buckets_[current_bucket_idx_].batch_tx_count++;
        }
    }

    void record_conflict() {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();
        buckets_[current_bucket_idx_].conflict_events++;
    }

    void record_abort() {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();
        buckets_[current_bucket_idx_].abort_events++;
    }

    void record_retry() {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();
        buckets_[current_bucket_idx_].retry_events++;
    }

    void record_queue_depth(uint64_t depth) {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();
        buckets_[current_bucket_idx_].sum_queue_depth += depth;
        buckets_[current_bucket_idx_].queue_depth_samples++;
    }

    void record_latency(double latency_ms) {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();
        buckets_[current_bucket_idx_].sum_latency_ms += latency_ms;
        buckets_[current_bucket_idx_].latency_samples++;
    }

    // Compute composite contention index Ct and extended ML features
    void compute_extended_metrics(double& cr_hat, double& ar_hat, double& ql_hat, double& ri_hat,
                                  double& composite_c_t, double& avg_latency_ms,
                                  float& arrival_velocity, float& batch_ratio) {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();

        uint64_t total_attempts = 0;
        uint64_t total_batches = 0;
        uint64_t total_conflicts = 0;
        uint64_t total_aborts = 0;
        uint64_t total_retries = 0;
        uint64_t total_qd_sum = 0;
        uint64_t total_qd_samples = 0;
        double total_lat_sum = 0.0;
        uint64_t total_lat_samples = 0;

        uint64_t recent_attempts = 0;
        uint64_t prior_attempts = 0;
        size_t half_window = num_buckets_ / 2;

        for (size_t i = 0; i < num_buckets_; ++i) {
            const auto& b = buckets_[i];
            total_attempts += b.total_tx_attempts;
            total_batches += b.batch_tx_count;
            total_conflicts += b.conflict_events;
            total_aborts += b.abort_events;
            total_retries += b.retry_events;
            total_qd_sum += b.sum_queue_depth;
            total_qd_samples += b.queue_depth_samples;
            total_lat_sum += b.sum_latency_ms;
            total_lat_samples += b.latency_samples;

            // Velocity: compare latest half vs older half
            if ((i >= current_bucket_idx_ && i < current_bucket_idx_ + half_window) ||
                (current_bucket_idx_ + half_window >= num_buckets_ && i < (current_bucket_idx_ + half_window) % num_buckets_)) {
                recent_attempts += b.total_tx_attempts;
            } else {
                prior_attempts += b.total_tx_attempts;
            }
        }

        // 1. Normalized Conflict Rate (CR_hat)
        if (total_attempts > 0) {
            cr_hat = std::min(1.0, static_cast<double>(total_conflicts) / static_cast<double>(total_attempts));
        } else {
            cr_hat = 0.0;
        }

        // 2. Abort/Rollback Rate (AR_hat)
        if (total_attempts > 0) {
            ar_hat = std::min(1.0, static_cast<double>(total_aborts) / static_cast<double>(total_attempts));
        } else {
            ar_hat = 0.0;
        }

        // 3. Lock Queue Depth (QL_hat)
        constexpr double MAX_EXPECTED_QUEUE_DEPTH = 10.0;
        if (total_qd_samples > 0) {
            double avg_qd = static_cast<double>(total_qd_sum) / static_cast<double>(total_qd_samples);
            ql_hat = std::min(1.0, avg_qd / MAX_EXPECTED_QUEUE_DEPTH);
        } else {
            ql_hat = 0.0;
        }

        // 4. Retry Intensity (RI_hat)
        if (total_attempts > 0) {
            double retry_ratio = static_cast<double>(total_retries) / static_cast<double>(total_attempts);
            ri_hat = std::min(1.0, retry_ratio / 2.0);
        } else {
            ri_hat = 0.0;
        }

        // Composite Contention Index Ct
        composite_c_t = (0.30 * cr_hat) + (0.30 * ar_hat) + (0.20 * ql_hat) + (0.20 * ri_hat);
        composite_c_t = std::clamp(composite_c_t, 0.0, 1.0);

        // Average Latency
        if (total_lat_samples > 0) {
            avg_latency_ms = total_lat_sum / static_cast<double>(total_lat_samples);
        } else {
            avg_latency_ms = 5.0; // Default floor
        }

        // Batch ratio for predictive feature vector
        if (total_attempts > 0) {
            batch_ratio = std::clamp(static_cast<float>(total_batches) / static_cast<float>(total_attempts), 0.0f, 1.0f);
        } else {
            batch_ratio = 0.0f;
        }

        // Arrival velocity: rate of change
        if (prior_attempts > 0) {
            float vel = static_cast<float>(recent_attempts) / static_cast<float>(prior_attempts);
            arrival_velocity = std::clamp(vel / 2.5f, 0.0f, 1.0f);
        } else if (recent_attempts > 0) {
            arrival_velocity = 0.5f;
        } else {
            arrival_velocity = 0.0f;
        }
    }

    void compute_metrics(double& cr_hat, double& ar_hat, double& ql_hat, double& ri_hat,
                         double& composite_c_t, double& avg_latency_ms) {
        float vel = 0.0f, batch = 0.0f;
        compute_extended_metrics(cr_hat, ar_hat, ql_hat, ri_hat, composite_c_t, avg_latency_ms, vel, batch);
    }

private:
    void check_and_rotate() {
        auto now = HighResClock::now();
        auto elapsed = std::chrono::duration_cast<Milliseconds>(now - last_rotate_time_);
        if (elapsed >= bucket_duration_) {
            size_t buckets_to_advance = std::min(num_buckets_, static_cast<size_t>(elapsed / bucket_duration_));
            for (size_t i = 0; i < buckets_to_advance; ++i) {
                current_bucket_idx_ = (current_bucket_idx_ + 1) % num_buckets_;
                buckets_[current_bucket_idx_] = WindowBucket{};
            }
            last_rotate_time_ = now;
        }
    }

    size_t num_buckets_;
    Milliseconds bucket_duration_;
    size_t current_bucket_idx_;
    TimePoint last_rotate_time_;
    std::vector<WindowBucket> buckets_;
    mutable std::mutex mtx_;
};

} // namespace adaptive_os
