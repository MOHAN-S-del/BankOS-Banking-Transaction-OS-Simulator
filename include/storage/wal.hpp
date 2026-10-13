#pragma once

#include "common/types.hpp"
#include <vector>
#include <mutex>
#include <atomic>
#include <fstream>
#include <chrono>

namespace adaptive_os {

enum class WalRecordType : uint8_t {
    TX_BEGIN = 0,
    TX_UPDATE = 1,
    TX_COMMIT = 2,
    TX_ABORT = 3
};

inline const char* walRecordTypeToString(WalRecordType type) {
    switch (type) {
        case WalRecordType::TX_BEGIN: return "BEGIN";
        case WalRecordType::TX_UPDATE: return "UPDATE";
        case WalRecordType::TX_COMMIT: return "COMMIT";
        case WalRecordType::TX_ABORT: return "ABORT";
        default: return "UNKNOWN";
    }
}

struct WalRecord {
    uint64_t lsn;
    TxId tx_id;
    WalRecordType type;
    AccountId account_id;
    OpKind op_kind;
    BalanceType prev_balance;
    BalanceType new_balance;
    uint64_t timestamp_ns;
};

class WriteAheadLog {
public:
    explicit WriteAheadLog(size_t ring_buffer_capacity = 65536)
        : next_lsn_(1), flushed_lsn_(0), capacity_(ring_buffer_capacity),
          ring_buffer_(ring_buffer_capacity), head_(0), total_flushes_(0) {}

    uint64_t append_record(TxId tx_id, WalRecordType type, AccountId account_id = 0,
                           OpKind op_kind = OpKind::READ_BALANCE,
                           BalanceType prev_bal = 0, BalanceType new_bal = 0) {
        std::lock_guard<std::mutex> lock(wal_mutex_);
        uint64_t lsn = next_lsn_++;
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            HighResClock::now().time_since_epoch()).count();

        WalRecord record{
            lsn,
            tx_id,
            type,
            account_id,
            op_kind,
            prev_bal,
            new_bal,
            static_cast<uint64_t>(now_ns)
        };

        size_t idx = head_ % capacity_;
        ring_buffer_[idx] = record;
        head_++;

        return lsn;
    }

    void flush(uint64_t target_lsn) {
        std::lock_guard<std::mutex> lock(wal_mutex_);
        if (target_lsn > flushed_lsn_) {
            flushed_lsn_ = target_lsn;
            total_flushes_++;
        }
    }

    uint64_t current_lsn() const {
        return next_lsn_.load(std::memory_order_relaxed);
    }

    uint64_t flushed_lsn() const {
        return flushed_lsn_.load(std::memory_order_relaxed);
    }

    uint64_t total_flushes() const {
        return total_flushes_.load(std::memory_order_relaxed);
    }

    std::vector<WalRecord> get_recent_records(size_t limit = 50) const {
        std::lock_guard<std::mutex> lock(wal_mutex_);
        std::vector<WalRecord> result;
        size_t start = (head_ > limit) ? (head_ - limit) : 0;
        for (size_t i = start; i < head_; ++i) {
            result.push_back(ring_buffer_[i % capacity_]);
        }
        return result;
    }

private:
    mutable std::mutex wal_mutex_;
    std::atomic<uint64_t> next_lsn_;
    std::atomic<uint64_t> flushed_lsn_;
    size_t capacity_;
    std::vector<WalRecord> ring_buffer_;
    size_t head_;
    std::atomic<uint64_t> total_flushes_;
};

} // namespace adaptive_os
