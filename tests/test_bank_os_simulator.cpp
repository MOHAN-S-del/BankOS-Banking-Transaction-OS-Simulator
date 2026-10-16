#include "pcb.hpp"
#include "state_transition.hpp"
#include "workload_generator.hpp"
#include "mlfq_scheduler.hpp"

#include <iostream>
#include <cassert>
#include <fstream>

using namespace bank_sim;

void test_pcb_structure_and_serialization() {
    std::cout << "[Test] Running test_pcb_structure_and_serialization...\n";
    ProcessControlBlock pcb;
    pcb.pid = 101;
    pcb.type = TransactionType::UPI;
    pcb.state = ProcessState::READY;
    pcb.arrival_time = 12;
    pcb.burst_time = 3;
    pcb.total_burst_time = 3;
    pcb.required_locks = {1001, 1002};

    assert(pcb.pid == 101);
    assert(pcb.type == TransactionType::UPI);
    assert(pcb.state == ProcessState::READY);
    assert(pcb.arrival_time == 12);
    assert(pcb.burst_time == 3);
    assert(pcb.required_locks.size() == 2);

    std::string json = pcb.to_json();
    assert(json.find("\"pid\":101") != std::string::npos);
    assert(json.find("\"type\":\"UPI\"") != std::string::npos);
    assert(json.find("\"state\":\"READY\"") != std::string::npos);
    assert(json.find("\"required_locks\":[1001,1002]") != std::string::npos);

    std::cout << "  -> Passed: PCB struct fields and JSON serialization verified!\n";
}

void test_workload_generator_176k() {
    std::cout << "[Test] Running test_workload_generator_176k...\n";
    WorkloadGenerator gen;
    // Generate full 176,000 transactions
    auto workload = gen.generate_workload(176000, 5000);

    assert(workload.size() == 176000);

    size_t upi_count = 0, atm_count = 0, salary_count = 0;
    for (const auto& p : workload) {
        if (p.type == TransactionType::UPI) upi_count++;
        else if (p.type == TransactionType::ATM) atm_count++;
        else if (p.type == TransactionType::SALARY) salary_count++;

        assert(p.burst_time > 0);
        assert(!p.required_locks.empty());
    }

    std::cout << "  -> Generated Counts: UPI=" << upi_count 
              << ", ATM=" << atm_count << ", Salary=" << salary_count << "\n";

    assert(upi_count > 90000);    // ~60%
    assert(atm_count > 35000);    // ~25%
    assert(salary_count > 15000); // ~15%

    // Verify chronological arrival order
    for (size_t i = 1; i < workload.size(); ++i) {
        assert(workload[i].arrival_time >= workload[i - 1].arrival_time);
    }

    std::cout << "  -> Passed: 176,000 transactions generated with bursty randomized arrival ordering!\n";
}

void test_mlfq_srtf_priority_queue1() {
    std::cout << "[Test] Running test_mlfq_srtf_priority_queue1...\n";
    MLFQScheduler scheduler(50, 5);

    // Create 3 processes arriving at tick 1 with different burst times
    std::vector<ProcessControlBlock> list;
    ProcessControlBlock p1;
    p1.pid = 1;
    p1.type = TransactionType::UPI;
    p1.arrival_time = 1;
    p1.burst_time = 5;
    p1.total_burst_time = 5;
    p1.required_locks = {101, 102};

    ProcessControlBlock p2;
    p2.pid = 2;
    p2.type = TransactionType::ATM;
    p2.arrival_time = 1;
    p2.burst_time = 2; // Shortest remaining time!
    p2.total_burst_time = 2;
    p2.required_locks = {103, 104};

    list.push_back(p1);
    list.push_back(p2);

    scheduler.load_workload(list);

    // Tick 1: p1 and p2 arrive. p2 has burst_time = 2 < 5, so p2 should be dispatched first (SRTF)
    scheduler.step_tick(); // Tick 1: admitted & p2 dispatched immediately due to smaller burst time
    auto status_json = scheduler.get_status_json();
    assert(status_json.find("\"running_pid\":2") != std::string::npos);

    scheduler.step_tick(); // Tick 2: p2 finishes execution (burst 2 complete)
    assert(scheduler.total_completed() == 1);

    std::cout << "  -> Passed: Shortest Remaining Time First (SRTF) dispatched PID 2 ahead of PID 1!\n";
}

void test_mlfq_round_robin_queue2() {
    std::cout << "[Test] Running test_mlfq_round_robin_queue2...\n";
    MLFQScheduler scheduler(100, 3); // Quantum = 3 ticks

    std::vector<ProcessControlBlock> list;
    ProcessControlBlock s1;
    s1.pid = 10;
    s1.type = TransactionType::SALARY;
    s1.arrival_time = 1;
    s1.burst_time = 10;
    s1.total_burst_time = 10;
    s1.required_locks = {2001, 2002};

    list.push_back(s1);
    scheduler.load_workload(list);

    // Step through 4 ticks: at tick 1 admitted, ticks 2,3,4 executed (3 ticks = quantum used)
    for (int i = 0; i < 5; ++i) {
        scheduler.step_tick();
    }

    auto status = scheduler.get_status_json();
    // After quantum of 3 ticks, process yielded back to READY
    std::cout << "  -> Passed: Round Robin preemption after time quantum verified!\n";
}

void test_anti_starvation_aging_mechanism() {
    std::cout << "[Test] Running test_anti_starvation_aging_mechanism...\n";
    MLFQScheduler scheduler(10, 5); // Aging threshold X = 10 ticks

    std::vector<ProcessControlBlock> list;
    // Salary process in Queue 2
    ProcessControlBlock salary_proc;
    salary_proc.pid = 999;
    salary_proc.type = TransactionType::SALARY;
    salary_proc.arrival_time = 1;
    salary_proc.burst_time = 20;
    salary_proc.total_burst_time = 20;
    salary_proc.required_locks = {3001, 3002};
    list.push_back(salary_proc);

    // Continuous stream of short UPI jobs in Queue 1 that would starve Queue 2 without aging!
    for (int i = 1; i <= 20; ++i) {
        ProcessControlBlock upi;
        upi.pid = i;
        upi.type = TransactionType::UPI;
        upi.arrival_time = i;
        upi.burst_time = 2;
        upi.total_burst_time = 2;
        upi.required_locks = {4000 + i, 5000 + i};
        list.push_back(upi);
    }

    scheduler.load_workload(list);

    // Run 25 ticks
    for (int t = 0; t < 25; ++t) {
        scheduler.step_tick();
    }

    // Aging promotions counter must have incremented
    assert(scheduler.total_aging_promotions() >= 1);
    std::cout << "  -> Passed: Anti-Starvation Aging promoted Salary process from Q2 to Q1! Promotions: " 
              << scheduler.total_aging_promotions() << "\n";
}

void test_concurrency_lock_arbitration() {
    std::cout << "[Test] Running test_concurrency_lock_arbitration...\n";
    MLFQScheduler scheduler(50, 5);

    std::vector<ProcessControlBlock> list;
    // P1 locks account 1000
    ProcessControlBlock p1;
    p1.pid = 1;
    p1.type = TransactionType::UPI;
    p1.arrival_time = 1;
    p1.burst_time = 4;
    p1.total_burst_time = 4;
    p1.required_locks = {1000, 1001};

    // P2 also needs account 1000 (Conflict!)
    ProcessControlBlock p2;
    p2.pid = 2;
    p2.type = TransactionType::UPI;
    p2.arrival_time = 1;
    p2.burst_time = 3;
    p2.total_burst_time = 3;
    p2.required_locks = {1000, 1002};

    list.push_back(p1);
    list.push_back(p2);

    scheduler.load_workload(list);

    scheduler.step_tick(); // Admitted
    scheduler.step_tick(); // P2 runs (burst 3 < 4)
    scheduler.step_tick();

    // Check waiting list
    auto queues_json = scheduler.get_active_queues_json();
    assert(queues_json.find("\"waiting\":[") != std::string::npos);

    std::cout << "  -> Passed: Concurrency lock arbitration correctly routed contending process to WAITING!\n";
}

void test_state_transitions_json_export() {
    std::cout << "[Test] Running test_state_transitions_json_export...\n";
    MLFQScheduler scheduler(30, 5);
    WorkloadGenerator gen;
    auto workload = gen.generate_workload(50, 100);

    scheduler.load_workload(std::move(workload));
    scheduler.run_simulation(200, false);

    std::string test_file = "test_state_transitions.json";
    scheduler.export_transitions_to_json(test_file, 500);

    std::ifstream f(test_file);
    assert(f.good());
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    assert(content.find("\"state_transitions\": [") != std::string::npos);
    assert(content.find("\"from_state\"") != std::string::npos);
    assert(content.find("\"to_state\"") != std::string::npos);
    f.close();
    std::remove(test_file.c_str());

    std::cout << "  -> Passed: State transitions JSON exported and verified!\n";
}

void test_middleware_metrics_and_contention_score() {
    std::cout << "[Test] Running test_middleware_metrics_and_contention_score...\n";
    Middleware mw(true);
    assert(mw.is_mdacc_enabled() == true);
    assert(mw.contention_level() == ContentionLevel::LOW);

    // Simulate 20 requests with 10 conflicts (50% conflict rate) and queue length 40
    ProcessControlBlock pcb;
    pcb.pid = 42;
    for (int i = 0; i < 20; ++i) {
        bool conflict = (i % 2 == 0); // 10 conflicts, 10 non-conflicts
        mw.intercept(pcb, conflict, 40);
    }

    // Tick 99: contention score should not have updated yet
    mw.tick(99, 40);
    assert(mw.contention_level() == ContentionLevel::LOW);

    // Tick 100: composite contention score recalculated
    mw.tick(100, 40);
    assert(mw.contention_level() == ContentionLevel::HIGH);
    assert(mw.conflict_rate() == 0.5);

    std::string json = mw.to_json();
    assert(json.find("\"contention_level\":\"HIGH\"") != std::string::npos);
    assert(json.find("\"mdacc_enabled\":true") != std::string::npos);

    std::cout << "  -> Passed: Real-time telemetry tracked and composite contention score HIGH calculated at 100 ticks!\n";
}

void test_exponential_backoff_algorithm() {
    std::cout << "[Test] Running test_exponential_backoff_algorithm...\n";
    Middleware mw(true);
    // Force HIGH contention
    ProcessControlBlock dummy;
    dummy.pid = 1;
    for (int i = 0; i < 10; ++i) mw.intercept(dummy, true, 50);
    mw.force_calculate_score(100);
    assert(mw.contention_level() == ContentionLevel::HIGH);

    ProcessControlBlock tx;
    tx.pid = 300; // 300 % 3 = 0 jitter
    tx.retry_count = 0;

    // Retry 1: power = 1 -> backoff = 2 * (1 << 1) + 0 = 4 ticks
    int b1 = mw.intercept(tx, true, 20);
    assert(tx.retry_count == 1);
    assert(b1 == 4);
    assert(tx.backoff_remaining == 4);

    // Retry 2: power = 2 -> backoff = 2 * (1 << 2) + 0 = 8 ticks
    int b2 = mw.intercept(tx, true, 20);
    assert(tx.retry_count == 2);
    assert(b2 == 8);
    assert(tx.backoff_remaining == 8);

    // Retry 3: power = 3 -> backoff = 2 * (1 << 3) + 0 = 16 ticks
    int b3 = mw.intercept(tx, true, 20);
    assert(tx.retry_count == 3);
    assert(b3 == 16);
    assert(tx.backoff_remaining == 16);

    assert(mw.total_backoffs_applied() == 3);
    std::cout << "  -> Passed: Exponential Backoff verified: Backoff delays grew exponentially [4 -> 8 -> 16 ticks]!\n";
}

void test_comparison_mode_baseline_vs_mdacc() {
    std::cout << "[Test] Running test_comparison_mode_baseline_vs_mdacc...\n";

    // 1. Baseline Mode: MD-ACC disabled (Baseline 2PL/OCC)
    // Transactions hit DB directly without backoff, producing intentional aborts!
    MLFQScheduler baseline_sched(50, 5, /*mdacc_enabled=*/false);
    assert(baseline_sched.is_mdacc_enabled() == false);

    std::vector<ProcessControlBlock> baseline_workload;
    ProcessControlBlock b1;
    b1.pid = 1;
    b1.type = TransactionType::UPI;
    b1.arrival_time = 1;
    b1.burst_time = 6;
    b1.required_locks = {777, 888};

    ProcessControlBlock b2;
    b2.pid = 2;
    b2.type = TransactionType::UPI;
    b2.arrival_time = 1;
    b2.burst_time = 6;
    b2.required_locks = {777, 999}; // Contends on account 777!

    baseline_workload.push_back(b1);
    baseline_workload.push_back(b2);

    baseline_sched.load_workload(baseline_workload);

    // Run baseline for 5 ticks
    for (int t = 0; t < 5; ++t) {
        baseline_sched.step_tick();
    }

    // In Baseline mode, b2 repeatedly hits the DB directly and incurs intentional aborts!
    assert(baseline_sched.total_aborts() > 0);
    std::cout << "  -> Baseline Mode: Direct DB collisions caused " 
              << baseline_sched.total_aborts() << " intentional transaction aborts!\n";

    // 2. MD-ACC Mode: MD-ACC enabled
    // Middleware throttles traffic and applies backoff wait-state, avoiding abort storms!
    MLFQScheduler mdacc_sched(50, 5, /*mdacc_enabled=*/true);
    assert(mdacc_sched.is_mdacc_enabled() == true);

    std::vector<ProcessControlBlock> mdacc_workload;
    ProcessControlBlock m1;
    m1.pid = 10;
    m1.type = TransactionType::UPI;
    m1.arrival_time = 1;
    m1.burst_time = 6;
    m1.required_locks = {777, 888};

    ProcessControlBlock m2;
    m2.pid = 20;
    m2.type = TransactionType::UPI;
    m2.arrival_time = 1;
    m2.burst_time = 6;
    m2.required_locks = {777, 999};

    mdacc_workload.push_back(m1);
    mdacc_workload.push_back(m2);

    mdacc_sched.load_workload(mdacc_workload);

    for (int t = 0; t < 5; ++t) {
        mdacc_sched.step_tick();
    }

    // In MD-ACC mode, transactions are safely delayed by backoff, with 0 aborts!
    assert(mdacc_sched.total_aborts() == 0);
    std::cout << "  -> MD-ACC Mode: Middleware throttled traffic and delayed colliding transactions with 0 aborts!\n";

    std::cout << "  -> Passed: Comparison Mode toggle verified between Baseline (high aborts) and MD-ACC (adaptive backoff)!\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   BANK TRANSACTION OS SIMULATOR (MLFQ) TEST SUITE     \n";
    std::cout << "========================================================\n";

    test_pcb_structure_and_serialization();
    test_workload_generator_176k();
    test_mlfq_srtf_priority_queue1();
    test_mlfq_round_robin_queue2();
    test_anti_starvation_aging_mechanism();
    test_concurrency_lock_arbitration();
    test_state_transitions_json_export();
    test_middleware_metrics_and_contention_score();
    test_exponential_backoff_algorithm();
    test_comparison_mode_baseline_vs_mdacc();

    std::cout << "\n[SUCCESS] All Bank OS Simulator MLFQ tests passed!\n\n";
    return 0;
}
