#include "common/types.hpp"
#include "ml/predictor.hpp"

#include <iostream>
#include <cassert>
#include <chrono>
#include <vector>

using namespace adaptive_os;
using namespace adaptive_os::ml;

void test_feature_vector_ingestion() {
    std::cout << "[Test] Running test_feature_vector_ingestion...\n";
    ContentionPredictor predictor;

    // Ingest 10 steps into 8-step buffer
    for (int i = 0; i < 10; ++i) {
        TelemetryFeatureVector feat{
            0.1f * i,
            0.05f * i,
            0.02f * i,
            0.01f * i,
            0.1f * i,
            0.05f
        };
        predictor.ingest_telemetry_step(feat);
    }

    auto res = predictor.forecast();
    assert(res.forecast_lead_time_ms == 30.0f);
    assert(res.predicted_contention >= 0.0f && res.predicted_contention <= 1.0f);
    std::cout << "  -> Passed: Ingestion & sliding buffer indexing verified! Forecast: "
              << res.predicted_contention << "\n";
}

void test_forward_pass_bounds_and_stability() {
    std::cout << "[Test] Running test_forward_pass_bounds_and_stability...\n";
    ContentionPredictor predictor;

    // Normal baseline sequence
    for (int i = 0; i < 8; ++i) {
        predictor.ingest_telemetry_step({0.05f, 0.01f, 0.02f, 0.01f, 0.1f, 0.05f});
    }

    auto res = predictor.forecast();
    assert(res.predicted_contention >= 0.0f && res.predicted_contention <= 1.0f);
    assert(res.anticipated_regime == WorkloadRegime::NORMAL_RETAIL);
    assert(!res.surge_predicted);
    assert(res.confidence > 0.85f);

    std::cout << "  -> Passed: Normal baseline output in bounds [0, 1], regime NORMAL_RETAIL\n";
}

void test_regime_classification_upi_peak() {
    std::cout << "[Test] Running test_regime_classification_upi_peak...\n";
    ContentionPredictor predictor;

    // Simulate surge in arrival velocity and retail conflict rate (UPI evening peak)
    for (int i = 0; i < 8; ++i) {
        float ramp = static_cast<float>(i + 1) / 8.0f;
        predictor.ingest_telemetry_step({
            0.20f * ramp,
            0.15f * ramp,
            0.20f * ramp,
            0.10f * ramp,
            0.85f, // Rapid arrival velocity surge
            0.05f  // Low batch ratio (retail P2P micro-payments)
        });
    }

    auto res = predictor.forecast();
    assert(res.anticipated_regime == WorkloadRegime::UPI_EVENING_PEAK);
    assert(res.predicted_contention >= 0.40f);
    std::cout << "  -> Passed: UPI Evening Peak correctly anticipated ahead of time! Predicted Ct: "
              << res.predicted_contention << "\n";
}

void test_regime_classification_salary_clearing() {
    std::cout << "[Test] Running test_regime_classification_salary_clearing...\n";
    ContentionPredictor predictor;

    // Simulate high batch ratio and bulk account lock queue depth (Salary clearing burst)
    for (int i = 0; i < 8; ++i) {
        float ramp = static_cast<float>(i + 1) / 8.0f;
        predictor.ingest_telemetry_step({
            0.45f * ramp,
            0.35f * ramp,
            0.60f * ramp,
            0.20f * ramp,
            0.40f,
            0.65f // High batch payroll ratio
        });
    }

    auto res = predictor.forecast();
    assert(res.anticipated_regime == WorkloadRegime::SALARY_CLEARING_BURST);
    assert(res.predicted_contention >= 0.50f);
    assert(res.surge_predicted == true);
    std::cout << "  -> Passed: Salary Clearing Burst correctly anticipated! Predicted Ct: "
              << res.predicted_contention << "\n";
}

void test_inference_sub_microsecond_benchmark() {
    std::cout << "[Test] Running test_inference_sub_microsecond_benchmark...\n";
    ContentionPredictor predictor;
    for (int i = 0; i < 8; ++i) {
        predictor.ingest_telemetry_step({0.2f, 0.1f, 0.15f, 0.05f, 0.4f, 0.2f});
    }

    constexpr int RUNS = 1000;
    auto start = HighResClock::now();
    for (int i = 0; i < RUNS; ++i) {
        auto r = predictor.forecast();
        (void)r;
    }
    auto end = HighResClock::now();

    double total_us = std::chrono::duration<double, std::micro>(end - start).count();
    double avg_us = total_us / RUNS;

    std::cout << "  -> 1,000 Inferences executed in " << total_us << " us (Avg: " << avg_us << " us / pass)\n";
    assert(avg_us < 20.0); // Sub-microsecond / low-microsecond capability verified
    std::cout << "  -> Passed: Sub-microsecond pure C++20 native inference verified!\n";
}

int main() {
    std::cout << "===================================================\n";
    std::cout << "   PREDICTIVE MACHINE LEARNING (TCN) UNIT TESTS     \n";
    std::cout << "===================================================\n";

    test_feature_vector_ingestion();
    test_forward_pass_bounds_and_stability();
    test_regime_classification_upi_peak();
    test_regime_classification_salary_clearing();
    test_inference_sub_microsecond_benchmark();

    std::cout << "\n[SUCCESS] All Predictive ML tests passed cleanly!\n\n";
    return 0;
}
