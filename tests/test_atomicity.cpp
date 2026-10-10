#include "db_manager.hpp"
#include <cassert>
#include <iostream>

using namespace BankOS;

int main() {
    std::cout << "[Test: Atomicity] Starting ACID Transaction & Rollback Test...\n";

    DBManager::getInstance().resetToInitialState();

    // Initial balances: A101 = 50,000, A102 = 35,000
    assert(DBManager::getInstance().getAccountBalance("A101") == 50000.0);
    assert(DBManager::getInstance().getAccountBalance("A102") == 35000.0);
    std::cout << "  ✓ Initial database balances verified (A101: 50,000, A102: 35,000)\n";

    // Test 1: Valid transfer of 10,000 from A101 to A102
    std::string msg = "";
    bool ok1 = DBManager::getInstance().executeTransfer("A101", "A102", 10000.0, msg);
    assert(ok1);
    assert(DBManager::getInstance().getAccountBalance("A101") == 40000.0);
    assert(DBManager::getInstance().getAccountBalance("A102") == 45000.0);
    std::cout << "  ✓ Valid transfer committed (A101: 40,000, A102: 45,000)\n";

    // Test 2: Overdraft attempt of 60,000 from A101 (Available: 40,000)
    bool ok2 = DBManager::getInstance().executeTransfer("A101", "A102", 60000.0, msg);
    assert(!ok2); // Must fail!
    // Balances must remain strictly unchanged!
    assert(DBManager::getInstance().getAccountBalance("A101") == 40000.0);
    assert(DBManager::getInstance().getAccountBalance("A102") == 45000.0);
    std::cout << "  ✓ Atomic Rollback verified: Insufficient funds rejected with zero partial state corruption!\n";

    std::cout << "[Test: Atomicity] ALL ACID ATOMICITY ASSERTIONS PASSED!\n";
    return 0;
}
