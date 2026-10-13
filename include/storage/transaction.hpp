#pragma once

#include "common/types.hpp"
#include <vector>
#include <algorithm>
#include <chrono>

namespace adaptive_os {

struct UndoRecord {
    AccountId account_id;
    OpKind original_kind;
    BalanceType amount;
};

class Transaction {
public:
    Transaction(TxId id, TxType type, std::vector<LedgerOperation> ops, QueueTier initial_tier)
        : id_(id), type_(type), operations_(std::move(ops)),
          status_(TxStatus::PENDING), current_tier_(initial_tier),
          estimated_ops_(operations_.size()), remaining_ops_(operations_.size()),
          arrival_time_(HighResClock::now()), last_scheduled_time_(arrival_time_),
          retry_count_(0), allocated_quantum_ms_(quantum_for_tier(initial_tier)),
          touches_hotspot_(false), demoted_by_hotspot_(false) {
        
        extract_involved_accounts();
    }

    TxId id() const { return id_; }
    TxType type() const { return type_; }
    TxStatus status() const { return status_; }
    void set_status(TxStatus status) { status_ = status; }

    const std::vector<LedgerOperation>& operations() const { return operations_; }
    std::vector<LedgerOperation>& operations() { return operations_; }

    QueueTier current_tier() const { return current_tier_; }
    void set_tier(QueueTier tier) {
        current_tier_ = tier;
        allocated_quantum_ms_ = quantum_for_tier(tier);
    }

    size_t estimated_operations() const { return estimated_ops_; }
    size_t remaining_operations() const { return remaining_ops_; }
    void set_remaining_operations(size_t ops) { remaining_ops_ = ops; }

    TimePoint arrival_time() const { return arrival_time_; }
    TimePoint admission_time() const { return admission_time_; }
    void set_admission_time(TimePoint t) { admission_time_ = t; }

    TimePoint last_scheduled_time() const { return last_scheduled_time_; }
    void set_last_scheduled_time(TimePoint t) { last_scheduled_time_ = t; }

    TimePoint completion_time() const { return completion_time_; }
    void mark_completed() { completion_time_ = HighResClock::now(); }

    uint32_t retry_count() const { return retry_count_; }
    void increment_retry() { retry_count_++; }

    Milliseconds allocated_quantum() const { return allocated_quantum_ms_; }
    void set_allocated_quantum(Milliseconds q) { allocated_quantum_ms_ = q; }

    bool touches_hotspot() const { return touches_hotspot_; }
    void set_touches_hotspot(bool val) { touches_hotspot_ = val; }

    bool demoted_by_hotspot() const { return demoted_by_hotspot_; }
    void set_demoted_by_hotspot(bool val) { demoted_by_hotspot_ = val; }

    const std::vector<AccountId>& involved_accounts() const { return involved_accounts_; }

    void add_undo_record(AccountId acc_id, OpKind original_kind, BalanceType amount) {
        undo_log_.push_back({acc_id, original_kind, amount});
    }

    const std::vector<UndoRecord>& undo_log() const { return undo_log_; }
    void clear_undo_log() { undo_log_.clear(); }

    double total_latency_ms() const {
        auto end = (completion_time_ == TimePoint{}) ? HighResClock::now() : completion_time_;
        return std::chrono::duration<double, std::milli>(end - arrival_time_).count();
    }

    double wait_time_ms() const {
        return std::chrono::duration<double, std::milli>(HighResClock::now() - last_scheduled_time_).count();
    }

    static Milliseconds quantum_for_tier(QueueTier tier) {
        switch (tier) {
            case QueueTier::Q1_HIGH_INTERACTIVE: return Milliseconds(10);
            case QueueTier::Q2_MED_STANDARD:     return Milliseconds(50);
            case QueueTier::Q3_LOW_BATCH:        return Milliseconds(200);
            default:                             return Milliseconds(10);
        }
    }

private:
    void extract_involved_accounts() {
        involved_accounts_.clear();
        for (const auto& op : operations_) {
            involved_accounts_.push_back(op.account_id);
        }
        // Canonical sorted order to prevent 2PL deadlocks across multi-account transfers
        std::sort(involved_accounts_.begin(), involved_accounts_.end());
        involved_accounts_.erase(std::unique(involved_accounts_.begin(), involved_accounts_.end()), involved_accounts_.end());
    }

    TxId id_;
    TxType type_;
    std::vector<LedgerOperation> operations_;
    TxStatus status_;
    QueueTier current_tier_;
    size_t estimated_ops_;
    size_t remaining_ops_;

    TimePoint arrival_time_;
    TimePoint admission_time_{};
    TimePoint last_scheduled_time_;
    TimePoint completion_time_{};

    uint32_t retry_count_;
    Milliseconds allocated_quantum_ms_;
    bool touches_hotspot_;
    bool demoted_by_hotspot_;

    std::vector<AccountId> involved_accounts_;
    std::vector<UndoRecord> undo_log_;
};

} // namespace adaptive_os
