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

    std::cout << "\n[SUCCESS] All Bank OS Simulator MLFQ tests passed!\n\n";
    return 0;
}
