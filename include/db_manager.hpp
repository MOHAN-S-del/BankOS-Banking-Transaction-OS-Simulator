#pragma once

#include "pcb.hpp"
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <memory>

#ifdef HAVE_MYSQL
#include <mysql.h>
#endif

namespace BankOS {

struct AccountRecord {
    std::string account_id;
    int customer_id;
    double balance;
    std::string account_type;
    std::string status;
    int version;
};

class DBManager {
public:
    static DBManager& getInstance();

    // Lifecycle
    bool initialize(const std::string& host = "127.0.0.1",
                    const std::string& user = "root",
                    const std::string& pass = "",
                    const std::string& db = "bank_os_db",
                    unsigned int port = 3306);
    void shutdown();
    bool resetToInitialState();

    // Account & Banking Operations
    double getAccountBalance(const std::string& account_id);
    std::map<std::string, double> getAllBalances();
    bool accountExists(const std::string& account_id);

    // Atomic execution with rollback support
    bool executeDeposit(const std::string& to_account, double amount, std::string& out_msg);
    bool executeWithdraw(const std::string& from_account, double amount, std::string& out_msg);
    bool executeTransfer(const std::string& from_account, const std::string& to_account, double amount, std::string& out_msg);

    // Audit & OS logging
    bool recordTransaction(const std::string& tx_id, const std::string& from_acc, 
                           const std::string& to_acc, double amount, 
                           const std::string& type, const std::string& status);
    bool syncPCB(const ProcessControlBlock& pcb);
    bool logSchedulerEvent(const std::string& pid, const std::string& event_type, int tick, const std::string& desc);
    bool logLockEvent(const std::string& pid, const std::string& resource_id, const std::string& action, int tick);

    bool isConnectedToMySQL() const { return m_mysql_connected; }

private:
    DBManager();
    ~DBManager();
    DBManager(const DBManager&) = delete;
    DBManager& operator=(const DBManager&) = delete;

    std::mutex m_db_mutex;
    bool m_mysql_connected;

#ifdef HAVE_MYSQL
    MYSQL* m_mysql_conn;
#endif

    // High-performance in-memory state (mirrored or fallback)
    std::map<std::string, AccountRecord> m_accounts;
    void setupDefaultAccounts();
};

} // namespace BankOS
