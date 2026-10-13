#pragma once

#include "common/types.hpp"
#include <random>
#include <algorithm>
#include <cmath>

namespace adaptive_os {

class AdaptiveBackoffController {
public:
    AdaptiveBackoffController(double base_delay_ms = 2.0, double alpha = 3.0, double max_backoff_ms = 500.0)
        : base_delay_ms_(base_delay_ms), alpha_(alpha), max_backoff_ms_(max_backoff_ms) {}

    // Calculate retry delay: Backoff = B0 * (1 + alpha * Ct) * 2^r + jitter
    Milliseconds calculate_delay(uint32_t retry_count, double contention_index) {
        double c_clamped = std::clamp(contention_index, 0.0, 1.0);
        
        // Base scaled factor
        double scaled_base = base_delay_ms_ * (1.0 + alpha_ * c_clamped);

        // Exponential multiplier: 2^r (with cap on exponent to avoid overflow)
        uint32_t r = std::min(retry_count, 10u);
        double exp_factor = std::pow(2.0, static_cast<double>(r));

        double deterministic_part = scaled_base * exp_factor;

        // Jitter uniformly distributed in [0, scaled_base]
        double jitter = get_jitter(scaled_base);

        double total_delay_ms = std::min(max_backoff_ms_, deterministic_part + jitter);

        return Milliseconds(static_cast<int64_t>(total_delay_ms));
    }

    double base_delay_ms() const { return base_delay_ms_; }
    double alpha() const { return alpha_; }

private:
    double get_jitter(double max_jitter) {
        thread_local std::mt19937_64 rng(std::random_device{}());
        std::uniform_real_distribution<double> dist(0.0, max_jitter);
        return dist(rng);
    }

    double base_delay_ms_;
    double alpha_;
    double max_backoff_ms_;
};

} // namespace adaptive_os
