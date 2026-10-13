#if __has_include("common/types.hpp")
#include "common/types.hpp"
#include "scheduler/mlfq_queue.hpp"
#include "scheduler/aging_engine.hpp"
#else
#include "../include/common/types.hpp"
#include "../include/scheduler/mlfq_queue.hpp"
#include "../include/scheduler/aging_engine.hpp"
#endif
#include <cassert>
#include <iostream>
#include <thread>

using namespace adaptive_os;

void test_mlfq_priority_ordering() {
    std::cout << "[Test] Running test_mlfq_priority_ordering...\n";
    MLFQQueue mlfq;

    // Create Tx for Q3 (batch)
    std::vector<LedgerOperation> ops3(10, {10001, OpKind::READ_BALANCE, 0, ""});
    auto tx3 = std::make_shared<Transaction>(3, TxType::BATCH_PAYROLL, ops3, QueueTier::Q3_LOW_BATCH);

    // Create Tx for Q2 (standard)
    std::vector<LedgerOperation> ops2(4, {10002, OpKind::READ_BALANCE, 0, ""});
    auto tx2 = std::make_shared<Transaction>(2, TxType::RETAIL_P2P, ops2, QueueTier::Q2_MED_STANDARD);

    // Create Tx for Q1 (interactive)
    std::vector<LedgerOperation> ops1(2, {10003, OpKind::READ_BALANCE, 0, ""});
    auto tx1 = std::make_shared<Transaction>(1, TxType::RETAIL_P2P, ops1, QueueTier::Q1_HIGH_INTERACTIVE);

    // Push out of order: Q3 first, then Q2, then Q1
    mlfq.push(tx3);
    mlfq.push(tx2);
    mlfq.push(tx1);

    // Dequeue: Q1 must pop first
    auto popped1 = mlfq.pop();
    assert(popped1 != nullptr && popped1->id() == 1);
    assert(popped1->current_tier() == QueueTier::Q1_HIGH_INTERACTIVE);

    // Q2 must pop second
    auto popped2 = mlfq.pop();
    assert(popped2 != nullptr && popped2->id() == 2);
    assert(popped2->current_tier() == QueueTier::Q2_MED_STANDARD);

    // Q3 must pop last
    auto popped3 = mlfq.pop();
    assert(popped3 != nullptr && popped3->id() == 3);
    assert(popped3->current_tier() == QueueTier::Q3_LOW_BATCH);

    std::cout << "  -> Passed: MLFQ strict queue priority ordering verified!\n";
}

void test_srtf_within_q1() {
    std::cout << "[Test] Running test_srtf_within_q1...\n";
    MLFQQueue mlfq;

    // Tx A: 5 remaining operations
    std::vector<LedgerOperation> opsA(5, {10001, OpKind::READ_BALANCE, 0, ""});
    auto txA = std::make_shared<Transaction>(101, TxType::RETAIL_P2P, opsA, QueueTier::Q1_HIGH_INTERACTIVE);

    // Tx B: 2 remaining operations (should pop first under SRTF)
    std::vector<LedgerOperation> opsB(2, {10002, OpKind::READ_BALANCE, 0, ""});
    auto txB = std::make_shared<Transaction>(102, TxType::RETAIL_P2P, opsB, QueueTier::Q1_HIGH_INTERACTIVE);

    // Tx C: 8 remaining operations (should pop last)
    std::vector<LedgerOperation> opsC(8, {10003, OpKind::READ_BALANCE, 0, ""});
    auto txC = std::make_shared<Transaction>(103, TxType::RETAIL_P2P, opsC, QueueTier::Q1_HIGH_INTERACTIVE);

    mlfq.push(txC);
    mlfq.push(txA);
    mlfq.push(txB);

    auto first = mlfq.pop();
    assert(first->id() == 102); // Shortest (2 ops)

    auto second = mlfq.pop();
    assert(second->id() == 101); // Middle (5 ops)

    auto third = mlfq.pop();
    assert(third->id() == 103); // Longest (8 ops)

    std::cout << "  -> Passed: SRTF remaining operation ordering in Queue 1 verified!\n";
}

void test_anti_starvation_aging() {
    std::cout << "[Test] Running test_anti_starvation_aging...\n";
    auto mlfq = std::make_shared<MLFQQueue>();

    std::vector<LedgerOperation> ops(10, {10001, OpKind::READ_BALANCE, 0, ""});
    auto tx_batch = std::make_shared<Transaction>(201, TxType::BATCH_PAYROLL, ops, QueueTier::Q3_LOW_BATCH);
    mlfq->push(tx_batch);

    assert(mlfq->q3_size() == 1);
    assert(mlfq->q2_size() == 0);
    assert(mlfq->q1_size() == 0);

    // Sleep for 60ms
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // Run aging with threshold = 40ms -> should migrate Q3 to Q2
    uint64_t migrated1 = mlfq->run_aging(40.0);
    assert(migrated1 == 1);
    assert(mlfq->q3_size() == 0);
    assert(mlfq->q2_size() == 1);
    assert(tx_batch->current_tier() == QueueTier::Q2_MED_STANDARD);

    // Sleep again for 60ms
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // Run aging with threshold = 40ms -> should migrate Q2 to Q1
    uint64_t migrated2 = mlfq->run_aging(40.0);
    assert(migrated2 == 1);
    assert(mlfq->q2_size() == 0);
    assert(mlfq->q1_size() == 1);
    assert(tx_batch->current_tier() == QueueTier::Q1_HIGH_INTERACTIVE);

    std::cout << "  -> Passed: Anti-starvation dynamic priority migration (Q3 -> Q2 -> Q1) verified!\n";
}

void test_hotspot_demotion() {
    std::cout << "[Test] Running test_hotspot_demotion...\n";
    MLFQQueue mlfq;

    // Tx touching account 1000 (Central Settlement Hotspot)
    std::vector<LedgerOperation> opsHot;
    opsHot.push_back({1000, OpKind::DEBIT, 500, ""});
    opsHot.push_back({2001, OpKind::CREDIT, 500, ""});

    auto tx_hot = std::make_shared<Transaction>(301, TxType::CENTRAL_SETTLEMENT, opsHot, QueueTier::Q1_HIGH_INTERACTIVE);
    mlfq.push(tx_hot);

    assert(mlfq.q1_size() == 1);

    // Apply hotspot feedback demotion for account 1000
    uint64_t demoted = mlfq.apply_hotspot_demotion({1000});
    assert(demoted == 1);
    assert(mlfq.q1_size() == 0);
    assert(mlfq.q2_size() == 1);
    assert(tx_hot->demoted_by_hotspot());
    assert(tx_hot->current_tier() == QueueTier::Q2_MED_STANDARD);

    std::cout << "  -> Passed: Hotspot demotion feedback successfully throttled convoying job!\n";
}

int main() {
    std::cout << "=== RUNNING MLFQ SCHEDULER & AGING TEST SUITE ===\n";
    test_mlfq_priority_ordering();
    test_srtf_within_q1();
    test_anti_starvation_aging();
    test_hotspot_demotion();
    std::cout << "=== ALL MLFQ SCHEDULER TESTS PASSED! ===\n\n";
    return 0;
}
