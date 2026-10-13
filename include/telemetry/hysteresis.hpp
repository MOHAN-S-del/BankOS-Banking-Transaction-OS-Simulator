#pragma once

#include "common/types.hpp"
#include <mutex>
#include <iostream>

namespace adaptive_os {

class HysteresisGate {
public:
    explicit HysteresisGate(double high_threshold = 0.70, double low_threshold = 0.50, uint32_t consecutive_req = 3)
        : high_threshold_(high_threshold),
          low_threshold_(low_threshold),
          consecutive_required_(consecutive_req),
          consecutive_high_count_(0),
          consecutive_low_count_(0),
          current_state_(HysteresisState::NORMAL) {}

    // Feed latest Ct and update state with hysteresis filter
    HysteresisState update(double composite_c_t) {
        std::lock_guard<std::mutex> lock(mtx_);

        if (composite_c_t >= high_threshold_) {
            consecutive_high_count_++;
            consecutive_low_count_ = 0;
            if (consecutive_high_count_ >= consecutive_required_) {
                current_state_ = HysteresisState::HIGH_CONTENTION;
            } else if (current_state_ == HysteresisState::NORMAL) {
                current_state_ = HysteresisState::ELEVATED;
            }
        } else if (composite_c_t < low_threshold_) {
            consecutive_low_count_++;
            consecutive_high_count_ = 0;
            if (consecutive_low_count_ >= consecutive_required_) {
                current_state_ = HysteresisState::NORMAL;
            } else if (current_state_ == HysteresisState::HIGH_CONTENTION) {
                current_state_ = HysteresisState::ELEVATED;
            }
        } else {
            // In the deadband [low_threshold, high_threshold]
            // Maintain current state to prevent flapping!
            consecutive_high_count_ = 0;
            consecutive_low_count_ = 0;
        }

        return current_state_;
    }

    HysteresisState current_state() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return current_state_;
    }

    uint32_t consecutive_high_count() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return consecutive_high_count_;
    }

    uint32_t consecutive_low_count() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return consecutive_low_count_;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mtx_);
        current_state_ = HysteresisState::NORMAL;
        consecutive_high_count_ = 0;
        consecutive_low_count_ = 0;
    }

private:
    double high_threshold_;
    double low_threshold_;
    uint32_t consecutive_required_;
    uint32_t consecutive_high_count_;
    uint32_t consecutive_low_count_;
    HysteresisState current_state_;
    mutable std::mutex mtx_;
};

} // namespace adaptive_os
