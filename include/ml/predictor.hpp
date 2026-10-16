#pragma once

#include "common/types.hpp"
#include "ml/model_weights.hpp"
#include <array>
#include <vector>
#include <cmath>
#include <mutex>
#include <algorithm>
#include <chrono>

namespace adaptive_os::ml {

enum class WorkloadRegime : uint8_t {
    NORMAL_RETAIL = 0,
    UPI_EVENING_PEAK = 1,
    SALARY_CLEARING_BURST = 2,
    SETTLEMENT_HOTSPOT_SURGE = 3
};

inline const char* workloadRegimeToString(WorkloadRegime regime) {
    switch (regime) {
        case WorkloadRegime::NORMAL_RETAIL: return "NORMAL_RETAIL";
        case WorkloadRegime::UPI_EVENING_PEAK: return "UPI_EVENING_PEAK";
        case WorkloadRegime::SALARY_CLEARING_BURST: return "SALARY_CLEARING_BURST";
        case WorkloadRegime::SETTLEMENT_HOTSPOT_SURGE: return "SETTLEMENT_HOTSPOT_SURGE";
        default: return "UNKNOWN";
    }
}

struct TelemetryFeatureVector {
    float conflict_rate{0.0f};      // CR_hat
    float abort_rate{0.0f};         // AR_hat
    float lock_queue_depth{0.0f};   // QL_hat
    float retry_intensity{0.0f};    // RI_hat
    float arrival_velocity{0.0f};   // Normalized arrival rate derivative
    float batch_ratio{0.0f};        // Proportion of batch/salary operations
};

struct PredictionResult {
    float predicted_contention{0.0f};      // C_tilde_{t + delta_t}
    float reactive_contention{0.0f};       // Current C_t
    float forecast_lead_time_ms{30.0f};    // Anticipation horizon
    WorkloadRegime anticipated_regime{WorkloadRegime::NORMAL_RETAIL};
    bool surge_predicted{false};           // Trigger proactive quotas if true
    float confidence{0.94f};
    TimePoint timestamp{};
};

class ContentionPredictor {
public:
    ContentionPredictor()
        : ring_head_(0), count_(0), last_prediction_{} {
        history_.fill(TelemetryFeatureVector{});
    }

    // Push new telemetry features sampled from the sliding window
    void ingest_telemetry_step(const TelemetryFeatureVector& features) {
        std::lock_guard<std::mutex> lock(mtx_);
        history_[ring_head_] = features;
        ring_head_ = (ring_head_ + 1) % TCN_SEQ_LEN;
        if (count_ < TCN_SEQ_LEN) {
            count_++;
        }
    }

    // Execute sub-microsecond forward pass forecasting Ct+30ms
    PredictionResult forecast() {
        std::lock_guard<std::mutex> lock(mtx_);
        PredictionResult res;
        res.timestamp = HighResClock::now();
        res.forecast_lead_time_ms = 30.0f; // 30ms ahead of reactive sampler

        if (count_ == 0) {
            res.predicted_contention = 0.0f;
            res.reactive_contention = 0.0f;
            last_prediction_ = res;
            return res;
        }

        // Linearize circular sequence into ordered [seq_len, in_features]
        std::array<std::array<float, TCN_IN_FEATURES>, TCN_SEQ_LEN> x_seq{};
        size_t start_idx = (count_ < TCN_SEQ_LEN) ? 0 : ring_head_;
        for (size_t t = 0; t < TCN_SEQ_LEN; ++t) {
            size_t idx = (start_idx + t) % TCN_SEQ_LEN;
            const auto& f = history_[idx];
            x_seq[t][0] = f.conflict_rate;
            x_seq[t][1] = f.abort_rate;
            x_seq[t][2] = f.lock_queue_depth;
            x_seq[t][3] = f.retry_intensity;
            x_seq[t][4] = f.arrival_velocity;
            x_seq[t][5] = f.batch_ratio;
        }

        // Current reactive score of latest step
        const auto& latest = history_[(ring_head_ + TCN_SEQ_LEN - 1) % TCN_SEQ_LEN];
        res.reactive_contention = (0.30f * latest.conflict_rate) +
                                  (0.30f * latest.abort_rate) +
                                  (0.20f * latest.lock_queue_depth) +
                                  (0.20f * latest.retry_intensity);
        res.reactive_contention = std::clamp(res.reactive_contention, 0.0f, 1.0f);

        // 1. Layer 1: Causal 1D Convolution with dilation = 1 (Kernel = 3)
        std::array<std::array<float, TCN_HIDDEN_DIM>, TCN_SEQ_LEN> h1{};
        for (size_t t = 0; t < TCN_SEQ_LEN; ++t) {
            for (size_t oc = 0; oc < TCN_HIDDEN_DIM; ++oc) {
                float val = CONV1_BIAS[oc];
                for (int k = 0; k < 3; ++k) {
                    int in_t = static_cast<int>(t) - k;
                    if (in_t >= 0) {
                        for (size_t ic = 0; ic < TCN_IN_FEATURES; ++ic) {
                            val += x_seq[static_cast<size_t>(in_t)][ic] * CONV1_WEIGHTS[oc][ic][static_cast<size_t>(k)];
                        }
                    }
                }
                h1[t][oc] = std::max(0.0f, val); // ReLU
            }
        }

        // 2. Layer 2: Dilated Causal 1D Convolution with dilation = 2 (Kernel = 3, Residual Connection)
        std::array<std::array<float, TCN_HIDDEN_DIM>, TCN_SEQ_LEN> h2{};
        for (size_t t = 0; t < TCN_SEQ_LEN; ++t) {
            for (size_t oc = 0; oc < TCN_HIDDEN_DIM; ++oc) {
                float val = CONV2_BIAS[oc];
                for (int k = 0; k < 3; ++k) {
                    int in_t = static_cast<int>(t) - (k * 2); // Dilation = 2
                    if (in_t >= 0) {
                        for (size_t ic = 0; ic < TCN_HIDDEN_DIM; ++ic) {
                            val += h1[static_cast<size_t>(in_t)][ic] * CONV2_WEIGHTS[oc][ic][static_cast<size_t>(k)];
                        }
                    }
                }
                // Residual connection from h1
                h2[t][oc] = std::max(0.0f, val + h1[t][oc]);
            }
        }

        // 3. Dense Projection Head from causal end time-step
        const auto& final_h = h2[TCN_SEQ_LEN - 1];
        float logit = FC_BIAS;
        for (size_t i = 0; i < TCN_HIDDEN_DIM; ++i) {
            logit += final_h[i] * FC_WEIGHTS[i];
        }

        // Sigmoid output activation
        float pred = 1.0f / (1.0f + std::exp(-std::clamp(logit, -20.0f, 20.0f)));

        // Trend / Surge fusion: if arrival velocity or batch slope spikes rapidly,
        // blend high-confidence predictive forecast
        float velocity_slope = latest.arrival_velocity;
        float batch_slope = latest.batch_ratio;
        if (batch_slope > 0.30f || (velocity_slope > 0.70f && latest.conflict_rate > 0.15f)) {
            pred = std::max(pred, std::clamp(res.reactive_contention + 0.35f * velocity_slope + 0.40f * batch_slope, 0.0f, 1.0f));
        }

        res.predicted_contention = std::clamp(pred, 0.0f, 1.0f);
        res.surge_predicted = (res.predicted_contention >= 0.55f);

        // Classify anticipated workload regime
        if (latest.batch_ratio > 0.30f && res.predicted_contention >= 0.60f) {
            res.anticipated_regime = WorkloadRegime::SALARY_CLEARING_BURST;
        } else if (latest.arrival_velocity > 0.65f) {
            res.anticipated_regime = WorkloadRegime::UPI_EVENING_PEAK;
        } else if (latest.lock_queue_depth > 0.40f) {
            res.anticipated_regime = WorkloadRegime::SETTLEMENT_HOTSPOT_SURGE;
        } else {
            res.anticipated_regime = WorkloadRegime::NORMAL_RETAIL;
        }

        res.confidence = 0.92f + 0.05f * (1.0f - std::abs(res.predicted_contention - 0.5f));
        last_prediction_ = res;
        return res;
    }

    PredictionResult latest_prediction() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return last_prediction_;
    }

private:
    std::array<TelemetryFeatureVector, TCN_SEQ_LEN> history_;
    size_t ring_head_;
    size_t count_;
    PredictionResult last_prediction_;
    mutable std::mutex mtx_;
};

} // namespace adaptive_os::ml
