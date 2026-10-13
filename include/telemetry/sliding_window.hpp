#pragma once

#include "common/types.hpp"
#include <vector>
#include <mutex>
#include <algorithm>
#include <cmath>

namespace adaptive_os {

struct WindowBucket {
    uint64_t total_tx_attempts{0};
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

    void record_tx_attempt() {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();
        buckets_[current_bucket_idx_].total_tx_attempts++;
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

    // Compute composite contention index Ct
    // Ct = 0.30 * CR_hat + 0.30 * AR_hat + 0.20 * QL_hat + 0.20 * RI_hat
    void compute_metrics(double& cr_hat, double& ar_hat, double& ql_hat, double& ri_hat,
                         double& composite_c_t, double& avg_latency_ms) {
        std::lock_guard<std::mutex> lock(mtx_);
        check_and_rotate();

        uint64_t total_attempts = 0;
        uint64_t total_conflicts = 0;
        uint64_t total_aborts = 0;
        uint64_t total_retries = 0;
        uint64_t total_qd_sum = 0;
        uint64_t total_qd_samples = 0;
        double total_lat_sum = 0.0;
        uint64_t total_lat_samples = 0;

        for (const auto& b : buckets_) {
            total_attempts += b.total_tx_attempts;
            total_conflicts += b.conflict_events;
            total_aborts += b.abort_events;
            total_retries += b.retry_events;
            total_qd_sum += b.sum_queue_depth;
            total_qd_samples += b.queue_depth_samples;
            total_lat_sum += b.sum_latency_ms;
            total_lat_samples += b.latency_samples;
        }

        // 1. Normalized Conflict Rate (CR_hat)
        // Ratio of conflict events to total attempts (normalized to [0, 1])
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
        // Average lock wait queue depth normalized against scaling factor (e.g. 10.0 queue depth = 1.0)
        constexpr double MAX_EXPECTED_QUEUE_DEPTH = 10.0;
        if (total_qd_samples > 0) {
            double avg_qd = static_cast<double>(total_qd_sum) / static_cast<double>(total_qd_samples);
            ql_hat = std::min(1.0, avg_qd / MAX_EXPECTED_QUEUE_DEPTH);
        } else {
            ql_hat = 0.0;
        }

        // 4. Retry Intensity (RI_hat)
        // Ratio of retries to total transactions normalized against a factor of 2.0
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
