#include "common/types.hpp"
#include "ml/predictor.hpp"
#include "ml/proactive_quota_manager.hpp"
#include "scheduler/mlfq_queue.hpp"
#include "scheduler/aging_engine.hpp"
#include "concurrency/token_bucket.hpp"
#include "telemetry/sliding_window.hpp"
#include "telemetry/hysteresis.hpp"
#include "telemetry/metrics_sampler.hpp"
#include "storage/ledger_engine.hpp"
#include "storage/wal.hpp"
#include "scheduler/adaptive_scheduler.hpp"

#include <iostream>
#include <cassert>
#include <memory>

using namespace adaptive_os;
using namespace adaptive_os::ml;

void test_baseline_allocations() {
    std::cout << "[Test] Running test_baseline_allocations...\n";
    ProactiveQuotaManager mgr;
    auto mlfq = std::make_shared<MLFQQueue>();
    auto aging = std::make_shared<AntiStarvationAgingEngine>(mlfq, 100.0);
    auto tb = std::make_shared<DynamicTokenBucket>(10000.0, 2000.0);

    PredictionResult pred;
    pred.predicted_contention = 0.20f;
    pred.reactive_contention = 0.15f;
    pred.anticipated_regime = WorkloadRegime::NORMAL_RETAIL;

    mgr.evaluate_and_apply(pred, mlfq, aging, tb);

    assert(mgr.current_state() == ProactivePolicyState::BASELINE_STABLE);
    auto alloc = mgr.current_allocation();
    assert(alloc.q1_worker_share == 0.50f);
    assert(alloc.q3_quantum_ms == Milliseconds(200));
    assert(alloc.aging_threshold_ms == 100.0);
    assert(mlfq->get_tier_quantum(QueueTier::Q3_LOW_BATCH) == Milliseconds(200));
    std::cout << "  -> Passed: Baseline allocations verified (Q1: 50%, Q3: 200ms, Aging: 100ms)\n";
}

void test_salary_clearing_proactive_preemption() {
    std::cout << "[Test] Running test_salary_clearing_proactive_preemption...\n";
    ProactiveQuotaManager mgr;
    auto mlfq = std::make_shared<MLFQQueue>();
    auto aging = std::make_shared<AntiStarvationAgingEngine>(mlfq, 100.0);
    auto tb = std::make_shared<DynamicTokenBucket>(10000.0, 2000.0);

    PredictionResult pred;
    pred.predicted_contention = 0.72f; // High predicted contention
    pred.reactive_contention = 0.30f;  // Reactive contention hasn't spiked yet! (30ms lag)
    pred.anticipated_regime = WorkloadRegime::SALARY_CLEARING_BURST;

    mgr.evaluate_and_apply(pred, mlfq, aging, tb);

    assert(mgr.current_state() == ProactivePolicyState::PROACTIVE_SALARY_CLEARING);
    auto alloc = mgr.current_allocation();
    assert(alloc.q1_worker_share == 0.85f);            // 85% retail capacity locked
    assert(alloc.q3_quantum_ms == Milliseconds(40));   // Q3 compressed to 40ms to prevent convoying
    assert(alloc.aging_threshold_ms == 25.0);          // Proactive aggressive aging sweep
    assert(aging->aging_threshold_ms() == 25.0);
    assert(mlfq->get_tier_quantum(QueueTier::Q3_LOW_BATCH) == Milliseconds(40));
    assert(mgr.total_interventions() == 1);

    std::cout << "  -> Passed: Proactive Salary Clearing pre-empted 30ms lag! (Q3: 40ms, Q1: 85%)\n";
}

void test_upi_peak_proactive_allocation() {
    std::cout << "[Test] Running test_upi_peak_proactive_allocation...\n";
    ProactiveQuotaManager mgr;
    auto mlfq = std::make_shared<MLFQQueue>();
    auto aging = std::make_shared<AntiStarvationAgingEngine>(mlfq, 100.0);
    auto tb = std::make_shared<DynamicTokenBucket>(10000.0, 2000.0);

    PredictionResult pred;
    pred.predicted_contention = 0.52f;
    pred.reactive_contention = 0.20f;
    pred.anticipated_regime = WorkloadRegime::UPI_EVENING_PEAK;

    mgr.evaluate_and_apply(pred, mlfq, aging, tb);

    assert(mgr.current_state() == ProactivePolicyState::PROACTIVE_UPI_PEAK);
    auto alloc = mgr.current_allocation();
    assert(alloc.q1_worker_share == 0.75f);
    assert(alloc.q3_quantum_ms == Milliseconds(80));
    assert(alloc.aging_threshold_ms == 40.0);
    assert(aging->aging_threshold_ms() == 40.0);
    assert(mlfq->get_tier_quantum(QueueTier::Q1_HIGH_INTERACTIVE) == Milliseconds(10));

    std::cout << "  -> Passed: Proactive UPI Peak Quota verified (Q1: 75%, Aging: 40ms)\n";
}

void test_token_bucket_proactive_throttling() {
    std::cout << "[Test] Running test_token_bucket_proactive_throttling...\n";
    ProactiveQuotaManager mgr;
    auto mlfq = std::make_shared<MLFQQueue>();
    auto aging = std::make_shared<AntiStarvationAgingEngine>(mlfq, 100.0);
    auto tb = std::make_shared<DynamicTokenBucket>(10000.0, 2000.0);

    double initial_rate = tb->current_rate();

    PredictionResult pred;
    pred.predicted_contention = 0.80f;
    pred.reactive_contention = 0.10f; // Reactive score is low, but surge is imminent
    pred.anticipated_regime = WorkloadRegime::SETTLEMENT_HOTSPOT_SURGE;

    mgr.evaluate_and_apply(pred, mlfq, aging, tb);

    // Rate should be proactively scaled down by (1 - 0.80) = 0.20
    double throttled_rate = tb->current_rate();
    assert(throttled_rate < initial_rate * 0.5);
    std::cout << "  -> Passed: Token bucket proactively throttled admission before lock explosion! "
              << initial_rate << " -> " << throttled_rate << " tx/s\n";
}

void test_end_to_end_proactive_scheduler_integration() {
    std::cout << "[Test] Running test_end_to_end_proactive_scheduler_integration...\n";
    auto lock_mgr = std::make_shared<LockManager>();
    auto wal = std::make_shared<WriteAheadLog>();
    auto ledger = std::make_shared<LedgerEngine>(lock_mgr, wal);
    auto mlfq = std::make_shared<MLFQQueue>();
    auto aging = std::make_shared<AntiStarvationAgingEngine>(mlfq, 100.0);
    auto sliding = std::make_shared<SlidingWindowTelemetry>(10, Milliseconds(100));
    auto hysteresis = std::make_shared<HysteresisGate>(0.70, 0.50, 3);
    auto tb = std::make_shared<DynamicTokenBucket>(10000.0, 2000.0);
    auto sampler = std::make_shared<MetricsSampler>(sliding, hysteresis, lock_mgr, tb);

    // Record high batch payroll activity in telemetry
    for (int i = 0; i < 20; ++i) {
        sliding->record_tx_attempt(true); // batch tx
        sliding->record_conflict();
    }
    sliding->record_queue_depth(15);

    // Execute sample step
    auto snap = sampler->sample_step(10, 5, 20, 4, 2, 1, mlfq, aging);

    assert(snap.forecast_horizon_ms == 30.0);
    assert(snap.predicted_contention > 0.0);
    assert(mlfq->get_tier_quantum(QueueTier::Q3_LOW_BATCH).count() <= 200);

    std::cout << "  -> Passed: End-to-end integration verified! Predicted Ct: "
              << snap.predicted_contention << " | Policy Mode: " << snap.proactive_policy_mode
              << " | Q1 Quota: " << snap.q1_preallocated_quota << "%\n";
}

int main() {
    std::cout << "===================================================\n";
    std::cout << "   PROACTIVE MLFQ PRIORITY QUOTA UNIT TESTS         \n";
    std::cout << "===================================================\n";

    test_baseline_allocations();
    test_salary_clearing_proactive_preemption();
    test_upi_peak_proactive_allocation();
    test_token_bucket_proactive_throttling();
    test_end_to_end_proactive_scheduler_integration();

    std::cout << "\n[SUCCESS] All Proactive MLFQ tests passed cleanly!\n\n";
    return 0;
}
