#if __has_include("common/types.hpp")
#include "common/types.hpp"
#include "storage/ledger_engine.hpp"
#include "storage/wal.hpp"
#include "concurrency/lock_manager.hpp"
#else
#include "../include/common/types.hpp"
#include "../include/storage/ledger_engine.hpp"
#include "../include/storage/wal.hpp"
#include "../include/concurrency/lock_manager.hpp"
#endif

#include <cassert>
#include <iostream>

using namespace adaptive_os;

void test_acid_atomicity_and_rollback() {
    std::cout << "[Test] Running test_acid_atomicity_and_rollback...\n";
    auto lock_mgr = std::make_shared<LockManager>();
    auto wal = std::make_shared<WriteAheadLog>();
    auto ledger = std::make_shared<LedgerEngine>(lock_mgr, wal);

    // Create 3 accounts:
    // Acc 1: 5000 cents
    // Acc 2: 1000 cents (insufficient for 2000 debit)
    // Acc 3: 0 cents
    ledger->create_account(1, AccountType::RETAIL, 5000, "Alice");
    ledger->create_account(2, AccountType::RETAIL, 1000, "Bob");
    ledger->create_account(3, AccountType::RETAIL, 0, "Charlie");

    // Multi-account transfer:
    // Op 1: Debit 1000 from Acc 1 (succeeds)
    // Op 2: Credit 1000 to Acc 3 (succeeds)
    // Op 3: Debit 2000 from Acc 2 (FAILS because Bob only has 1000!)
    std::vector<LedgerOperation> ops;
    ops.push_back({1, OpKind::DEBIT, 1000, "Debit Alice"});
    ops.push_back({3, OpKind::CREDIT, 1000, "Credit Charlie"});
    ops.push_back({2, OpKind::DEBIT, 2000, "Debit Bob (Fail)"});

    auto tx = std::make_shared<Transaction>(1001, TxType::RETAIL_P2P, ops, QueueTier::Q1_HIGH_INTERACTIVE);
    bool conflict = false;
    bool committed = ledger->execute_transaction(tx, Milliseconds(50), conflict);

    assert(!committed);
    assert(tx->status() == TxStatus::ABORTED_INSUFFICIENT_FUNDS);

    // Atomicity Check: Alice's balance must be unchanged at 5000, Charlie at 0, Bob at 1000!
    assert(ledger->get_account(1)->balance() == 5000);
    assert(ledger->get_account(2)->balance() == 1000);
    assert(ledger->get_account(3)->balance() == 0);

    std::cout << "  -> Passed: ACID atomicity rollback preserved account balance invariants!\n";
}

void test_successful_commit_and_wal() {
    std::cout << "[Test] Running test_successful_commit_and_wal...\n";
    auto lock_mgr = std::make_shared<LockManager>();
    auto wal = std::make_shared<WriteAheadLog>();
    auto ledger = std::make_shared<LedgerEngine>(lock_mgr, wal);

    ledger->create_account(10, AccountType::RETAIL, 10000, "Alice");
    ledger->create_account(20, AccountType::RETAIL, 5000, "Bob");

    std::vector<LedgerOperation> ops;
    ops.push_back({10, OpKind::DEBIT, 2500, "Transfer to Bob"});
    ops.push_back({20, OpKind::CREDIT, 2500, "Receive from Alice"});

    auto tx = std::make_shared<Transaction>(2001, TxType::RETAIL_P2P, ops, QueueTier::Q1_HIGH_INTERACTIVE);
    bool conflict = false;
    bool committed = ledger->execute_transaction(tx, Milliseconds(50), conflict);

    assert(committed);
    assert(tx->status() == TxStatus::COMMITTED);

    // Balances
    assert(ledger->get_account(10)->balance() == 7500);
    assert(ledger->get_account(20)->balance() == 7500);

    // Total conserved balance = 15000
    assert(ledger->get_account(10)->balance() + ledger->get_account(20)->balance() == 15000);

    // Verify WAL contains BEGIN, UPDATE, UPDATE, COMMIT records
    auto wal_records = wal->get_recent_records(10);
    assert(wal_records.size() >= 4);

    bool found_begin = false, found_commit = false;
    for (const auto& rec : wal_records) {
        if (rec.tx_id == 2001 && rec.type == WalRecordType::TX_BEGIN) found_begin = true;
        if (rec.tx_id == 2001 && rec.type == WalRecordType::TX_COMMIT) found_commit = true;
    }
    assert(found_begin);
    assert(found_commit);

    std::cout << "  -> Passed: Successful ACID commit and WAL sequence logging verified!\n";
}

int main() {
    std::cout << "=== RUNNING ACID STORAGE & WAL TEST SUITE ===\n";
    test_acid_atomicity_and_rollback();
    test_successful_commit_and_wal();
    std::cout << "=== ALL ACID STORAGE TESTS PASSED! ===\n\n";
    return 0;
}
