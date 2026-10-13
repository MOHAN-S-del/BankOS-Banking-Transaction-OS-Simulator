#if __has_include("common/types.hpp")
#include "common/types.hpp"
#include "telemetry/sliding_window.hpp"
#include "telemetry/hysteresis.hpp"
#include "concurrency/token_bucket.hpp"
#include "concurrency/backoff.hpp"
#else
#include "../include/common/types.hpp"
#include "../include/telemetry/sliding_window.hpp"
#include "../include/telemetry/hysteresis.hpp"
#include "../include/concurrency/token_bucket.hpp"
#include "../include/concurrency/backoff.hpp"
#endif

#include <cassert>
#include <iostream>
#include <cmath>

using namespace adaptive_os;

void test_composite_contention_index_calculation() {
    std::cout << "[Test] Running test_composite_contention_index_calculation...\n";
    SlidingWindowTelemetry window(10, Milliseconds(500));

    // Simulate 100 total transaction attempts:
    // 40 conflicts -> CR_hat = 40/100 = 0.40
    // 20 aborts    -> AR_hat = 20/100 = 0.20
    // Queue depths -> average depth 5.0 -> QL_hat = 5.0 / 10.0 = 0.50
    // 30 retries   -> RI_hat = (30/100) / 2.0 = 0.15

    for (int i = 0; i < 100; ++i) {
        window.record_tx_attempt();
    }
    for (int i = 0; i < 40; ++i) {
        window.record_conflict();
    }
    for (int i = 0; i < 20; ++i) {
        window.record_abort();
    }
    for (int i = 0; i < 30; ++i) {
        window.record_retry();
    }
    window.record_queue_depth(5);

    double cr = 0, ar = 0, ql = 0, ri = 0, ct = 0, avg_lat = 0;
    window.compute_metrics(cr, ar, ql, ri, ct, avg_lat);

    assert(std::abs(cr - 0.40) < 0.01);
    assert(std::abs(ar - 0.20) < 0.01);
    assert(std::abs(ql - 0.50) < 0.01);
    assert(std::abs(ri - 0.15) < 0.01);

    // Expected Ct = 0.30*0.40 + 0.30*0.20 + 0.20*0.50 + 0.20*0.15
    //             = 0.12 + 0.06 + 0.10 + 0.03 = 0.31
    double expected_ct = 0.30 * 0.40 + 0.30 * 0.20 + 0.20 * 0.50 + 0.20 * 0.15;
    assert(std::abs(ct - expected_ct) < 0.01);

    std::cout << "  -> Passed: MD-ACC Composite Index Ct formula verified (" << ct << ")!\n";
}

void test_hysteresis_gate_state_machine() {
    std::cout << "[Test] Running test_hysteresis_gate_state_machine...\n";
    HysteresisGate gate(0.70, 0.50, 3); // 3 consecutive required

    assert(gate.current_state() == HysteresisState::NORMAL);

    // Window 1: Ct = 0.75 (high, but only 1 window -> should be ELEVATED, not yet HIGH_CONTENTION)
    auto s1 = gate.update(0.75);
    assert(s1 == HysteresisState::ELEVATED);
    assert(gate.consecutive_high_count() == 1);

    // Window 2: Ct = 0.80 (high, 2 windows -> still ELEVATED)
    auto s2 = gate.update(0.80);
    assert(s2 == HysteresisState::ELEVATED);
    assert(gate.consecutive_high_count() == 2);

    // Window 3: Ct = 0.72 (3rd consecutive high window -> transition to HIGH_CONTENTION!)
    auto s3 = gate.update(0.72);
    assert(s3 == HysteresisState::HIGH_CONTENTION);
    assert(gate.consecutive_high_count() == 3);

    // Window 4: Brief dip to 0.60 (in deadband [0.50, 0.70] -> must retain HIGH_CONTENTION to prevent flapping!)
    auto s4 = gate.update(0.60);
    assert(s4 == HysteresisState::HIGH_CONTENTION);

    // Recovery test: need 3 consecutive windows < 0.50
    gate.update(0.40); // 1st low window
    assert(gate.current_state() == HysteresisState::ELEVATED);

    gate.update(0.45); // 2nd low window
    assert(gate.current_state() == HysteresisState::ELEVATED);

    auto s_rec = gate.update(0.35); // 3rd low window -> back to NORMAL!
    assert(s_rec == HysteresisState::NORMAL);

    std::cout << "  -> Passed: Hysteresis gate anti-flapping state transitions verified!\n";
}

void test_dynamic_token_bucket() {
    std::cout << "[Test] Running test_dynamic_token_bucket...\n";
    DynamicTokenBucket bucket(1000.0, 100.0);

    // When Ct = 0.0 -> lambda_eff = 1000.0
    bucket.update_contention_index(0.0);
    assert(std::abs(bucket.current_rate() - 1000.0) < 1.0);

    // When Ct = 0.50 -> lambda_eff = 1000.0 * (1 - 0.50) = 500.0
    bucket.update_contention_index(0.50);
    assert(std::abs(bucket.current_rate() - 500.0) < 1.0);

    // When Ct = 0.80 -> lambda_eff = 1000.0 * (1 - 0.80) = 200.0
    bucket.update_contention_index(0.80);
    assert(std::abs(bucket.current_rate() - 200.0) < 1.0);

    std::cout << "  -> Passed: Token bucket dynamic admission throttling verified!\n";
}

void test_jittered_backoff_formula() {
    std::cout << "[Test] Running test_jittered_backoff_formula...\n";
    AdaptiveBackoffController backoff(2.0, 3.0, 500.0); // B0 = 2ms, alpha = 3.0

    // At Ct = 0.0, r = 0: Backoff = 2 * (1 + 0) * 1 + jitter = 2ms + jitter in [0, 2]
    auto d0 = backoff.calculate_delay(0, 0.0);
    assert(d0.count() >= 2 && d0.count() <= 5);

    // At Ct = 1.0, r = 2:
    // scaled_base = 2 * (1 + 3 * 1.0) = 8ms
    // exp = 2^2 = 4 -> deterministic = 32ms
    // jitter in [0, 8ms] -> total in [32ms, 40ms]
    auto d1 = backoff.calculate_delay(2, 1.0);
    assert(d1.count() >= 32 && d1.count() <= 41);

    std::cout << "  -> Passed: Jittered exponential backoff formula verified!\n";
}

int main() {
    std::cout << "=== RUNNING MD-ACC TELEMETRY & CONCURRENCY TEST SUITE ===\n";
    test_composite_contention_index_calculation();
    test_hysteresis_gate_state_machine();
    test_dynamic_token_bucket();
    test_jittered_backoff_formula();
    std::cout << "=== ALL TELEMETRY & MD-ACC TESTS PASSED! ===\n\n";
    return 0;
}
