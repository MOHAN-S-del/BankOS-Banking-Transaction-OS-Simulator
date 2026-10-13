#pragma once

#include "common/types.hpp"
#include "storage/transaction.hpp"
#include <random>
#include <atomic>
#include <memory>
#include <vector>

namespace adaptive_os {

constexpr AccountId ACC_SETTLEMENT_RESERVE = 1000;
constexpr AccountId ACC_ESCROW_PRIMARY     = 1001;
constexpr AccountId ACC_CORP_BASE          = 2000;
constexpr size_t    NUM_CORP_ACCOUNTS      = 10;
constexpr AccountId ACC_RETAIL_BASE        = 10000;
constexpr size_t    NUM_RETAIL_ACCOUNTS    = 1000;

class WorkloadGenerator {
public:
    WorkloadGenerator() : next_tx_id_(100000) {}

    // Stream A: High-frequency retail payments (P2P micro-payments)
    // 2 operations (Debit Alice, Credit Bob), Queue 1
    std::shared_ptr<Transaction> generate_retail_p2p() {
        TxId id = next_tx_id_.fetch_add(1, std::memory_order_relaxed);
        AccountId from = get_random_retail();
        AccountId to = get_random_retail();
        while (to == from) {
            to = get_random_retail();
        }

        BalanceType amount = 100 + (id % 900); // 1.00 to 9.99

        std::vector<LedgerOperation> ops;
        ops.push_back({from, OpKind::DEBIT, amount, "Retail P2P Debit"});
        ops.push_back({to, OpKind::CREDIT, amount, "Retail P2P Credit"});

        auto tx = std::make_shared<Transaction>(id, TxType::RETAIL_P2P, std::move(ops),
                                                QueueTier::Q1_HIGH_INTERACTIVE);
        return tx;
    }

    // Stream B: Batch corporate payroll transfers (1-to-N bulk credits)
    // 1 debit from Corp + N credits to retail employees, Queue 3
    std::shared_ptr<Transaction> generate_batch_payroll(size_t employee_count = 15) {
        TxId id = next_tx_id_.fetch_add(1, std::memory_order_relaxed);
        AccountId corp = ACC_CORP_BASE + (id % NUM_CORP_ACCOUNTS);
        BalanceType per_emp_amount = 5000; // 50.00
        BalanceType total_debit = per_emp_amount * static_cast<BalanceType>(employee_count);

        std::vector<LedgerOperation> ops;
        ops.push_back({corp, OpKind::DEBIT, total_debit, "Payroll Corporate Disbursement"});

        for (size_t i = 0; i < employee_count; ++i) {
            AccountId emp = ACC_RETAIL_BASE + ((id * 31 + i * 17) % NUM_RETAIL_ACCOUNTS);
            ops.push_back({emp, OpKind::CREDIT, per_emp_amount, "Salary Credit"});
        }

        auto tx = std::make_shared<Transaction>(id, TxType::BATCH_PAYROLL, std::move(ops),
                                                QueueTier::Q3_LOW_BATCH);
        return tx;
    }

    // Stream C: Shared Central Settlement balancing (extreme hotspot)
    // All transactions contend heavily on ACC_SETTLEMENT_RESERVE!
    std::shared_ptr<Transaction> generate_settlement_hotspot() {
        TxId id = next_tx_id_.fetch_add(1, std::memory_order_relaxed);
        AccountId retail = get_random_retail();
        BalanceType amount = 25000; // 250.00

        std::vector<LedgerOperation> ops;
        // Central Settlement clearing
        if (id % 2 == 0) {
            ops.push_back({retail, OpKind::DEBIT, amount, "Bank Reserve Net Settlement Debit"});
            ops.push_back({ACC_SETTLEMENT_RESERVE, OpKind::CREDIT, amount, "Central Settlement Credit"});
        } else {
            ops.push_back({ACC_SETTLEMENT_RESERVE, OpKind::DEBIT, amount, "Central Settlement Debit"});
            ops.push_back({retail, OpKind::CREDIT, amount, "Bank Reserve Net Settlement Credit"});
        }

        auto tx = std::make_shared<Transaction>(id, TxType::CENTRAL_SETTLEMENT, std::move(ops),
                                                QueueTier::Q2_MED_STANDARD);
        tx->set_touches_hotspot(true);
        return tx;
    }

    // Generate transaction according to workload mixture weights
    // e.g. retail_pct = 70, payroll_pct = 10, settlement_pct = 20
    std::shared_ptr<Transaction> generate_mixed_transaction(int retail_pct = 60,
                                                           int payroll_pct = 10,
                                                           int settlement_pct = 30) {
        (void)settlement_pct;
        int r = get_random_int(1, 100);
        if (r <= retail_pct) {
            return generate_retail_p2p();
        } else if (r <= retail_pct + payroll_pct) {
            return generate_batch_payroll(12);
        } else {
            return generate_settlement_hotspot();
        }
    }

private:
    AccountId get_random_retail() {
        return ACC_RETAIL_BASE + get_random_int(0, NUM_RETAIL_ACCOUNTS - 1);
    }

    int get_random_int(int low, int high) {
        thread_local std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(low, high);
        return dist(rng);
    }

    std::atomic<TxId> next_tx_id_;
};

} // namespace adaptive_os
