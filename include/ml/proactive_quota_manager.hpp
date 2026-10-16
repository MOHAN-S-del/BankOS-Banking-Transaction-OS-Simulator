#pragma once

#include "common/types.hpp"
#include "ml/predictor.hpp"
#include "scheduler/mlfq_queue.hpp"
#include "scheduler/aging_engine.hpp"
#include "concurrency/token_bucket.hpp"

#include <atomic>
#include <mutex>
#include <chrono>

namespace adaptive_os::ml {

enum class ProactivePolicyState : uint8_t {
    BASELINE_STABLE = 0,
    PROACTIVE_UPI_PEAK = 1,
    PROACTIVE_SALARY_CLEARING = 2,
    PROACTIVE_HOTSPOT_PROTECT = 3
};

inline const char* proactivePolicyToString(ProactivePolicyState state) {
    switch (state) {
        case ProactivePolicyState::BASELINE_STABLE: return "BASELINE_STABLE";
        case ProactivePolicyState::PROACTIVE_UPI_PEAK: return "PROACTIVE_UPI_PEAK (75% Q1 Quota)";
        case ProactivePolicyState::PROACTIVE_SALARY_CLEARING: return "PROACTIVE_SALARY_CLEARING (Anti-Convoy Pre-emption)";
        case ProactivePolicyState::PROACTIVE_HOTSPOT_PROTECT: return "PROACTIVE_HOTSPOT_PROTECT (Pre-emptive Bounded Tw)";
        default: return "UNKNOWN";
    }
}

struct ProactiveQuotaAllocation {
    float q1_worker_share{0.50f};     // 50% default
    float q2_worker_share{0.30f};     // 30% default
    float q3_worker_share{0.20f};     // 20% default
    Milliseconds q1_quantum_ms{10};
    Milliseconds q2_quantum_ms{50};
    Milliseconds q3_quantum_ms{200};
    double aging_threshold_ms{100.0};
    double token_bucket_headroom_scale{1.0};
};

class ProactiveQuotaManager {
public:
    ProactiveQuotaManager()
        : current_state_(ProactivePolicyState::BASELINE_STABLE),
          proactive_interventions_(0),
          last_switch_time_(HighResClock::now()) {
        allocation_ = ProactiveQuotaAllocation{};
    }

    // Evaluate ML forecast ahead of time and proactively pre-allocate MLFQ priority quotas
    void evaluate_and_apply(const PredictionResult& prediction,
                            std::shared_ptr<MLFQQueue> mlfq,
                            std::shared_ptr<AntiStarvationAgingEngine> aging,
                            std::shared_ptr<DynamicTokenBucket> token_bucket) {
        std::lock_guard<std::mutex> lock(mtx_);

        float predicted_ct = prediction.predicted_contention;
        ProactivePolicyState new_state = ProactivePolicyState::BASELINE_STABLE;

        if (predicted_ct >= 0.65f) {
            if (prediction.anticipated_regime == WorkloadRegime::SALARY_CLEARING_BURST) {
                new_state = ProactivePolicyState::PROACTIVE_SALARY_CLEARING;
            } else if (prediction.anticipated_regime == WorkloadRegime::UPI_EVENING_PEAK) {
                new_state = ProactivePolicyState::PROACTIVE_UPI_PEAK;
            } else {
                new_state = ProactivePolicyState::PROACTIVE_HOTSPOT_PROTECT;
            }
        } else if (predicted_ct >= 0.45f) {
            new_state = ProactivePolicyState::PROACTIVE_UPI_PEAK;
        }

        if (new_state != current_state_) {
            proactive_interventions_++;
            current_state_ = new_state;
            last_switch_time_ = HighResClock::now();
        }

        // Apply proactive pre-allocation quotas based on predicted regime
        switch (current_state_) {
            case ProactivePolicyState::PROACTIVE_SALARY_CLEARING: {
                // End-of-month salary clearing anticipated:
                // Pre-emptively compress Q3 batch quantum from 200ms down to 40ms to prevent worker convoying
                allocation_.q1_worker_share = 0.85f; // Lock 85% capacity for retail P2P
                allocation_.q2_worker_share = 0.10f;
                allocation_.q3_worker_share = 0.05f;
                allocation_.q3_quantum_ms = Milliseconds(40);
                allocation_.aging_threshold_ms = 25.0; // Proactive aggressive aging sweep

                if (aging) {
                    aging->set_aging_threshold_ms(25.0);
                }
                // Proactive admission regulation ahead of reactive window
                if (token_bucket) {
                    token_bucket->update_contention_index(std::max(prediction.reactive_contention, predicted_ct));
                }
                break;
            }

            case ProactivePolicyState::PROACTIVE_UPI_PEAK: {
                // Evening UPI peak anticipated:
                // Pre-allocate 75% worker priority to Q1 micro-payments (SRTF)
                allocation_.q1_worker_share = 0.75f;
                allocation_.q2_worker_share = 0.20f;
                allocation_.q3_worker_share = 0.05f;
                allocation_.q3_quantum_ms = Milliseconds(80);
                allocation_.aging_threshold_ms = 40.0;

                if (aging) {
                    aging->set_aging_threshold_ms(40.0);
                }
                if (token_bucket) {
                    token_bucket->update_contention_index(std::max(prediction.reactive_contention, predicted_ct));
                }
                break;
            }

            case ProactivePolicyState::PROACTIVE_HOTSPOT_PROTECT: {
                // Shared settlement account contention anticipated
                allocation_.q1_worker_share = 0.70f;
                allocation_.q2_worker_share = 0.20f;
                allocation_.q3_worker_share = 0.10f;
                allocation_.q3_quantum_ms = Milliseconds(60);
                allocation_.aging_threshold_ms = 50.0;

                if (aging) {
                    aging->set_aging_threshold_ms(50.0);
                }
                if (token_bucket) {
                    token_bucket->update_contention_index(std::max(prediction.reactive_contention, predicted_ct));
                }
                break;
            }

            case ProactivePolicyState::BASELINE_STABLE:
            default: {
                // Stable normal baseline quotas
                allocation_.q1_worker_share = 0.50f;
                allocation_.q2_worker_share = 0.30f;
                allocation_.q3_worker_share = 0.20f;
                allocation_.q3_quantum_ms = Milliseconds(200);
                allocation_.aging_threshold_ms = 100.0;

                if (aging) {
                    aging->set_aging_threshold_ms(100.0);
                }
                break;
            }
        }

        if (mlfq) {
            mlfq->set_tier_quanta(allocation_.q1_quantum_ms, allocation_.q2_quantum_ms, allocation_.q3_quantum_ms);
        }
    }

    ProactivePolicyState current_state() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return current_state_;
    }

    ProactiveQuotaAllocation current_allocation() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return allocation_;
    }

    uint64_t total_interventions() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return proactive_interventions_;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mtx_);
        current_state_ = ProactivePolicyState::BASELINE_STABLE;
        allocation_ = ProactiveQuotaAllocation{};
        proactive_interventions_ = 0;
    }

private:
    ProactivePolicyState current_state_;
    ProactiveQuotaAllocation allocation_;
    uint64_t proactive_interventions_;
    TimePoint last_switch_time_;
    mutable std::mutex mtx_;
};

} // namespace adaptive_os::ml
