#pragma once

#include "common/types.hpp"
#include "storage/account.hpp"
#include "storage/transaction.hpp"
#include "storage/wal.hpp"
#include "concurrency/lock_manager.hpp"
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <vector>
#include <thread>

namespace adaptive_os {

class LedgerEngine {
public:
    explicit LedgerEngine(std::shared_ptr<LockManager> lock_mgr,
                          std::shared_ptr<WriteAheadLog> wal)
        : lock_manager_(std::move(lock_mgr)), wal_(std::move(wal)),
          total_commits_(0), total_aborts_(0) {}

    // Account registration
    bool create_account(AccountId id, AccountType type, BalanceType initial_balance, std::string owner) {
        std::unique_lock<std::shared_mutex> lock(accounts_mutex_);
        if (accounts_.find(id) != accounts_.end()) {
            return false;
        }
        accounts_[id] = std::make_shared<Account>(id, type, initial_balance, std::move(owner));
        return true;
    }

    std::shared_ptr<Account> get_account(AccountId id) const {
        std::shared_lock<std::shared_mutex> lock(accounts_mutex_);
        auto it = accounts_.find(id);
        if (it != accounts_.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<Account>> get_all_accounts() const {
        std::shared_lock<std::shared_mutex> lock(accounts_mutex_);
        std::vector<std::shared_ptr<Account>> res;
        res.reserve(accounts_.size());
        for (const auto& [_, acc] : accounts_) {
            res.push_back(acc);
        }
        return res;
    }

    // Execute transaction under 2PL with bounded lock waiting Tw
    bool execute_transaction(std::shared_ptr<Transaction> tx, Milliseconds lock_timeout, bool& had_conflict) {
        had_conflict = false;
        const auto& involved = tx->involved_accounts();
        std::vector<AccountId> acquired_locks;
        acquired_locks.reserve(involved.size());

        // 1. Acquire locks in canonical sorted order to avoid deadlock
        for (AccountId acc_id : involved) {
            bool conflict = false;
            auto acc = get_account(acc_id);
            if (acc) {
                acc->increment_waiters();
            }

            bool ok = lock_manager_->acquire_lock(acc_id, tx->id(), lock_timeout, conflict);
            
            if (acc) {
                acc->decrement_waiters();
                acc->record_access();
                if (conflict) {
                    acc->record_contention_hit();
                }
            }

            if (conflict) {
                had_conflict = true;
            }

            if (!ok) {
                // Lock acquisition timed out (bounded waiting Tw expired) -> Rollback & Abort
                rollback_and_release(tx, acquired_locks, TxStatus::ABORTED_LOCK_TIMEOUT);
                return false;
            }
            acquired_locks.push_back(acc_id);
        }

        // 2. WAL BEGIN
        wal_->append_record(tx->id(), WalRecordType::TX_BEGIN);

        // 3. Execute operations
        tx->clear_undo_log();
        bool ops_successful = true;

        for (const auto& op : tx->operations()) {
            auto acc = get_account(op.account_id);
            if (!acc) {
                ops_successful = false;
                tx->set_status(TxStatus::ABORTED_FATAL);
                break;
            }

            // Simulate realistic banking switch processing work (e.g. cryptographic verification, RTGS settlement netting)
            if (simulate_work_.load(std::memory_order_relaxed)) {
                if (acc->type() == AccountType::CENTRAL_SETTLEMENT) {
                    std::this_thread::sleep_for(std::chrono::microseconds(60));
                } else if (tx->type() == TxType::BATCH_PAYROLL) {
                    std::this_thread::sleep_for(std::chrono::microseconds(4));
                }
            }

            BalanceType prev_bal = acc->balance();

            if (op.kind == OpKind::DEBIT) {
                if (!acc->apply_debit(op.amount)) {
                    // Insufficient funds
                    ops_successful = false;
                    tx->set_status(TxStatus::ABORTED_INSUFFICIENT_FUNDS);
                    break;
                }
                // Record undo action
                tx->add_undo_record(op.account_id, OpKind::DEBIT, op.amount);
                wal_->append_record(tx->id(), WalRecordType::TX_UPDATE, op.account_id,
                                    OpKind::DEBIT, prev_bal, acc->balance());
            } else if (op.kind == OpKind::CREDIT) {
                acc->apply_credit(op.amount);
                tx->add_undo_record(op.account_id, OpKind::CREDIT, op.amount);
                wal_->append_record(tx->id(), WalRecordType::TX_UPDATE, op.account_id,
                                    OpKind::CREDIT, prev_bal, acc->balance());
            } else if (op.kind == OpKind::READ_BALANCE) {
                // Non-modifying read
            }
        }

        if (!ops_successful) {
            rollback_and_release(tx, acquired_locks, tx->status());
            return false;
        }

        // 4. Commit: WAL COMMIT record + flush
        uint64_t commit_lsn = wal_->append_record(tx->id(), WalRecordType::TX_COMMIT);
        wal_->flush(commit_lsn);

        tx->set_status(TxStatus::COMMITTED);
        tx->mark_completed();
        total_commits_.fetch_add(1, std::memory_order_relaxed);

        // 5. Release locks
        lock_manager_->release_all_locks(acquired_locks, tx->id());
        return true;
    }

    void set_simulate_clearing_work(bool enable) {
        simulate_work_.store(enable, std::memory_order_relaxed);
    }

    bool is_simulate_clearing_work() const {
        return simulate_work_.load(std::memory_order_relaxed);
    }

    uint64_t total_commits() const { return total_commits_.load(std::memory_order_relaxed); }
    uint64_t total_aborts() const { return total_aborts_.load(std::memory_order_relaxed); }

    void reset_counters() {
        total_commits_.store(0, std::memory_order_relaxed);
        total_aborts_.store(0, std::memory_order_relaxed);
    }

private:
    void rollback_and_release(std::shared_ptr<Transaction> tx,
                              const std::vector<AccountId>& acquired_locks,
                              TxStatus status) {
        // Reverse undo operations
        const auto& undos = tx->undo_log();
        for (auto it = undos.rbegin(); it != undos.rend(); ++it) {
            auto acc = get_account(it->account_id);
            if (acc) {
                if (it->original_kind == OpKind::DEBIT) {
                    acc->apply_credit(it->amount); // Compensate debit
                } else if (it->original_kind == OpKind::CREDIT) {
                    acc->apply_debit(it->amount);  // Compensate credit
                }
            }
        }

        uint64_t abort_lsn = wal_->append_record(tx->id(), WalRecordType::TX_ABORT);
        wal_->flush(abort_lsn);

        tx->set_status(status);
        tx->mark_completed();
        total_aborts_.fetch_add(1, std::memory_order_relaxed);

        lock_manager_->release_all_locks(acquired_locks, tx->id());
    }

    std::shared_ptr<LockManager> lock_manager_;
    std::shared_ptr<WriteAheadLog> wal_;
    mutable std::shared_mutex accounts_mutex_;
    std::unordered_map<AccountId, std::shared_ptr<Account>> accounts_;

    std::atomic<uint64_t> total_commits_;
    std::atomic<uint64_t> total_aborts_;
    std::atomic<bool> simulate_work_{false};
};

} // namespace adaptive_os
