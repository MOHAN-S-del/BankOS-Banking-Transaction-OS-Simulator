#if __has_include("common/types.hpp")
#include "common/types.hpp"
#include "storage/ledger_engine.hpp"
#include "storage/wal.hpp"
#include "concurrency/lock_manager.hpp"
#include "concurrency/token_bucket.hpp"
#include "concurrency/backoff.hpp"
#include "telemetry/sliding_window.hpp"
#include "telemetry/hysteresis.hpp"
#include "telemetry/metrics_sampler.hpp"
#include "scheduler/mlfq_queue.hpp"
#include "scheduler/aging_engine.hpp"
#include "scheduler/adaptive_scheduler.hpp"
#include "workload/generator.hpp"
#include "suite/benchmark_runner.hpp"
#else
#include "../include/common/types.hpp"
#include "../include/storage/ledger_engine.hpp"
#include "../include/storage/wal.hpp"
#include "../include/concurrency/lock_manager.hpp"
#include "../include/concurrency/token_bucket.hpp"
#include "../include/concurrency/backoff.hpp"
#include "../include/telemetry/sliding_window.hpp"
#include "../include/telemetry/hysteresis.hpp"
#include "../include/telemetry/metrics_sampler.hpp"
#include "../include/scheduler/mlfq_queue.hpp"
#include "../include/scheduler/aging_engine.hpp"
#include "../include/scheduler/adaptive_scheduler.hpp"
#include "../include/workload/generator.hpp"
#include "../include/suite/benchmark_runner.hpp"
#endif

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace adaptive_os;

int main() {
    std::cout << "=== RUNNING STRESS CONCURRENCY & HOTSPOT TEST ===\n";

    auto lock_mgr = std::make_shared<LockManager>();
    auto wal = std::make_shared<WriteAheadLog>();
    auto ledger = std::make_shared<LedgerEngine>(lock_mgr, wal);
    BenchmarkRunner::initialize_test_ledger(*ledger);

    auto mlfq = std::make_shared<MLFQQueue>();
    auto aging = std::make_shared<AntiStarvationAgingEngine>(mlfq, 80.0, Milliseconds(20));
    auto sliding = std::make_shared<SlidingWindowTelemetry>(10, Milliseconds(50));
    auto hysteresis = std::make_shared<HysteresisGate>(0.70, 0.50, 3);
    auto token_bucket = std::make_shared<DynamicTokenBucket>(10000.0, 2000.0);
    auto sampler = std::make_shared<MetricsSampler>(sliding, hysteresis, lock_mgr, token_bucket);
    auto backoff = std::make_shared<AdaptiveBackoffController>(2.0, 3.0, 200.0);

    auto scheduler = std::make_shared<AdaptiveScheduler>(
        8, ledger, lock_mgr, mlfq, aging, sampler, token_bucket, backoff, sliding
    );

    scheduler->start();
    WorkloadGenerator generator;

    std::cout << "[Stress] Launching 10 concurrent burst threads hammering central settlement hotspot...\n";
    std::atomic<bool> stop_flag(false);
    std::vector<std::thread> producers;

    for (int t = 0; t < 10; ++t) {
        producers.emplace_back([&]() {
            while (!stop_flag.load(std::memory_order_relaxed)) {
                // Heavy settlement hotspot transactions
                auto tx = generator.generate_settlement_hotspot();
                scheduler->submit(tx);
                std::this_thread::sleep_for(std::chrono::microseconds(150));
            }
        });
    }

    // Let it run for 1.5 seconds under extreme contention
    std::this_thread::sleep_for(Milliseconds(1500));
    stop_flag.store(true);

    for (auto& p : producers) {
        p.join();
    }

    // Drain queue
    std::this_thread::sleep_for(Milliseconds(500));
    scheduler->stop();

    auto snap = sampler->latest_snapshot();
    std::cout << "[Stress Results]\n"
              << "  - Total Submitted: " << scheduler->total_submitted() << "\n"
              << "  - Total Committed: " << scheduler->total_committed() << "\n"
              << "  - Total Aborted:   " << scheduler->total_aborted() << "\n"
              << "  - Total Retries:   " << scheduler->total_retries() << "\n"
              << "  - Hotspot Demotions: " << scheduler->total_hotspot_demotions() << "\n"
              << "  - Contention Index Ct: " << snap.composite_index << "\n";

    assert(scheduler->total_committed() > 0);
    std::cout << "=== STRESS CONCURRENCY TEST PASSED! ===\n\n";
    return 0;
}
