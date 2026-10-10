#include "db_manager.hpp"
#include <iostream>
#include <sstream>

#ifdef HAVE_MYSQL
#include <mysql.h>
#endif

namespace BankOS {

DBManager& DBManager::getInstance() {
    static DBManager instance;
    return instance;
}

DBManager::DBManager() : m_mysql_connected(false)
#ifdef HAVE_MYSQL
, m_mysql_conn(nullptr)
#endif
{
    setupDefaultAccounts();
}

DBManager::~DBManager() {
    shutdown();
}

void DBManager::setupDefaultAccounts() {
    m_accounts.clear();
    m_accounts["A101"] = {"A101", 1, 50000.00, "SAVINGS", "ACTIVE", 0};
    m_accounts["A102"] = {"A102", 2, 35000.00, "SAVINGS", "ACTIVE", 0};
    m_accounts["A103"] = {"A103", 3, 22000.00, "CURRENT", "ACTIVE", 0};
}

bool DBManager::initialize(const std::string& host,
                          const std::string& user,
                          const std::string& pass,
                          const std::string& db,
                          unsigned int port) {
    std::lock_guard<std::mutex> lock(m_db_mutex);

#ifdef HAVE_MYSQL
    m_mysql_conn = mysql_init(nullptr);
    if (!m_mysql_conn) {
        std::cerr << "[DBManager] mysql_init failed. Using in-memory fallback mode.\n";
        m_mysql_connected = false;
        return false;
    }

    // Set connection timeout (3 seconds)
    unsigned int timeout = 3;
    mysql_options(m_mysql_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (mysql_real_connect(m_mysql_conn, host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, nullptr, 0)) {
        m_mysql_connected = true;
        std::cout << "[DBManager] Successfully connected to MySQL database: " << db << " on " << host << ":" << port << "\n";
        return true;
    } else {
        std::cout << "[DBManager] MySQL connection failed (" << mysql_error(m_mysql_conn) 
                  << "). Operating in resilient in-memory simulated mode.\n";
        mysql_close(m_mysql_conn);
        m_mysql_conn = nullptr;
        m_mysql_connected = false;
        return false;
    }
#else
    std::cout << "[DBManager] Compiled in standard in-memory simulated DBMS mode.\n";
    m_mysql_connected = false;
    return true;
#endif
}

void DBManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_db_mutex);
#ifdef HAVE_MYSQL
    if (m_mysql_conn) {
        mysql_close(m_mysql_conn);
        m_mysql_conn = nullptr;
    }
#endif
    m_mysql_connected = false;
}

bool DBManager::resetToInitialState() {
    std::lock_guard<std::mutex> lock(m_db_mutex);
    setupDefaultAccounts();

#ifdef HAVE_MYSQL
    if (m_mysql_connected && m_mysql_conn) {
        const char* reset_sql = 
            "UPDATE account SET balance = 50000.00 WHERE account_id = 'A101';"
            "UPDATE account SET balance = 35000.00 WHERE account_id = 'A102';"
            "UPDATE account SET balance = 22000.00 WHERE account_id = 'A103';";
        mysql_query(m_mysql_conn, reset_sql);
    }
#endif
    return true;
}

double DBManager::getAccountBalance(const std::string& account_id) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
    auto it = m_accounts.find(account_id);
    if (it != m_accounts.end()) {
        return it->second.balance;
    }
    return -1.0;
}

std::map<std::string, double> DBManager::getAllBalances() {
    std::lock_guard<std::mutex> lock(m_db_mutex);
    std::map<std::string, double> result;
    for (const auto& [id, record] : m_accounts) {
        result[id] = record.balance;
    }
    return result;
}

bool DBManager::accountExists(const std::string& account_id) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
    return m_accounts.find(account_id) != m_accounts.end();
}

bool DBManager::executeDeposit(const std::string& to_account, double amount, std::string& out_msg) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
    auto it = m_accounts.find(to_account);
    if (it == m_accounts.end()) {
        out_msg = "Destination account does not exist: " + to_account;
        return false;
    }
    if (amount <= 0) {
        out_msg = "Invalid deposit amount (must be > 0)";
        return false;
    }

    it->second.balance += amount;
    it->second.version++;

#ifdef HAVE_MYSQL
    if (m_mysql_connected && m_mysql_conn) {
        std::ostringstream ss;
        ss << "UPDATE account SET balance = balance + " << amount 
           << ", version = version + 1 WHERE account_id = '" << to_account << "';";
        mysql_query(m_mysql_conn, ss.str().c_str());
    }
#endif

    out_msg = "Deposit committed successfully";
    return true;
}

bool DBManager::executeWithdraw(const std::string& from_account, double amount, std::string& out_msg) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
    auto it = m_accounts.find(from_account);
    if (it == m_accounts.end()) {
        out_msg = "Source account does not exist: " + from_account;
        return false;
    }
    if (amount <= 0) {
        out_msg = "Invalid withdrawal amount (must be > 0)";
        return false;
    }
    if (it->second.balance < amount) {
        out_msg = "INSUFFICIENT FUNDS: Available ₹" + std::to_string((int)it->second.balance) + 
                  ", Requested ₹" + std::to_string((int)amount) + " -> Transaction Rolled Back";
        return false;
    }

    it->second.balance -= amount;
    it->second.version++;

#ifdef HAVE_MYSQL
    if (m_mysql_connected && m_mysql_conn) {
        std::ostringstream ss;
        ss << "UPDATE account SET balance = balance - " << amount 
           << ", version = version + 1 WHERE account_id = '" << from_account << "';";
        mysql_query(m_mysql_conn, ss.str().c_str());
    }
#endif

    out_msg = "Withdrawal committed successfully";
    return true;
}

bool DBManager::executeTransfer(const std::string& from_account, const std::string& to_account, 
                                double amount, std::string& out_msg) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
    auto from_it = m_accounts.find(from_account);
    auto to_it = m_accounts.find(to_account);

    if (from_it == m_accounts.end() || to_it == m_accounts.end()) {
        out_msg = "Invalid transfer account specification";
        return false;
    }
    if (from_account == to_account) {
        out_msg = "Source and destination accounts must be distinct";
        return false;
    }
    if (amount <= 0) {
        out_msg = "Transfer amount must be strictly positive";
        return false;
    }
    if (from_it->second.balance < amount) {
        out_msg = "INSUFFICIENT FUNDS: Account " + from_account + " balance ₹" + 
                  std::to_string((int)from_it->second.balance) + " is less than requested ₹" + 
                  std::to_string((int)amount) + " -> Atomic Rollback";
        return false;
    }

    // Atomic debit & credit
    from_it->second.balance -= amount;
    from_it->second.version++;
    to_it->second.balance += amount;
    to_it->second.version++;

#ifdef HAVE_MYSQL
    if (m_mysql_connected && m_mysql_conn) {
        // Wrap in explicit ACID transaction
        mysql_query(m_mysql_conn, "START TRANSACTION;");
        std::ostringstream q1, q2;
        q1 << "UPDATE account SET balance = balance - " << amount 
           << ", version = version + 1 WHERE account_id = '" << from_account << "';";
        q2 << "UPDATE account SET balance = balance + " << amount 
           << ", version = version + 1 WHERE account_id = '" << to_account << "';";
        
        if (mysql_query(m_mysql_conn, q1.str().c_str()) == 0 &&
            mysql_query(m_mysql_conn, q2.str().c_str()) == 0) {
            mysql_query(m_mysql_conn, "COMMIT;");
        } else {
            mysql_query(m_mysql_conn, "ROLLBACK;");
            out_msg = "MySQL transaction failed: " + std::string(mysql_error(m_mysql_conn));
            return false;
        }
    }
#endif

    out_msg = "Transfer committed atomically";
    return true;
}

bool DBManager::recordTransaction(const std::string& tx_id, const std::string& from_acc, 
                                  const std::string& to_acc, double amount, 
                                  const std::string& type, const std::string& status) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
#ifdef HAVE_MYSQL
    if (m_mysql_connected && m_mysql_conn) {
        std::ostringstream ss;
        ss << "INSERT INTO transaction (transaction_id, from_account, to_account, amount, transaction_type, status) "
           << "VALUES ('" << tx_id << "', "
           << (from_acc.empty() ? "NULL" : ("'" + from_acc + "'")) << ", "
           << (to_acc.empty() ? "NULL" : ("'" + to_acc + "'")) << ", "
           << amount << ", '" << type << "', '" << status << "') "
           << "ON DUPLICATE KEY UPDATE status = VALUES(status);";
        mysql_query(m_mysql_conn, ss.str().c_str());
    }
#endif
    return true;
}

bool DBManager::syncPCB(const ProcessControlBlock& pcb) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
#ifdef HAVE_MYSQL
    if (m_mysql_connected && m_mysql_conn) {
        std::ostringstream ss;
        ss << "INSERT INTO process_pcb (process_id, transaction_id, state, arrival_time, burst_time, "
           << "remaining_burst, completion_time, waiting_time, turnaround_time, response_time) "
           << "VALUES ('" << pcb.pid << "', '" << pcb.transaction_id << "', '" 
           << stateToString(pcb.state) << "', " << pcb.arrival_time << ", " 
           << pcb.burst_time << ", " << pcb.remaining_burst << ", " 
           << (pcb.completion_time >= 0 ? std::to_string(pcb.completion_time) : "NULL") << ", "
           << pcb.waiting_time << ", " << pcb.turnaround_time << ", " << pcb.response_time << ") "
           << "ON DUPLICATE KEY UPDATE "
           << "state = VALUES(state), remaining_burst = VALUES(remaining_burst), "
           << "completion_time = VALUES(completion_time), waiting_time = VALUES(waiting_time), "
           << "turnaround_time = VALUES(turnaround_time), response_time = VALUES(response_time);";
        mysql_query(m_mysql_conn, ss.str().c_str());
    }
#endif
    return true;
}

bool DBManager::logSchedulerEvent(const std::string& pid, const std::string& event_type, 
                                  int tick, const std::string& desc) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
#ifdef HAVE_MYSQL
    if (m_mysql_connected && m_mysql_conn) {
        std::ostringstream ss;
        ss << "INSERT INTO scheduler_log (process_id, event_type, clock_tick, description) "
           << "VALUES ('" << pid << "', '" << event_type << "', " << tick << ", '" << desc << "');";
        mysql_query(m_mysql_conn, ss.str().c_str());
    }
#endif
    return true;
}

bool DBManager::logLockEvent(const std::string& pid, const std::string& resource_id, 
                             const std::string& action, int tick) {
    std::lock_guard<std::mutex> lock(m_db_mutex);
#ifdef HAVE_MYSQL
    if (m_mysql_connected && m_mysql_conn) {
        std::ostringstream ss;
        ss << "INSERT INTO lock_event_log (process_id, resource_id, action, clock_tick) "
           << "VALUES ('" << pid << "', '" << resource_id << "', '" << action << "', " << tick << ");";
        mysql_query(m_mysql_conn, ss.str().c_str());
    }
#endif
    return true;
}

} // namespace BankOS
