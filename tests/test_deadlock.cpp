#include "lock_manager.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace BankOS;

int main() {
    std::cout << "[Test: LockManager] Starting Deadlock Prevention & Resource Ordering Test...\n";

    LockManager::getInstance().reset();

    // Test 1: T1 requests transfer from A101 to A102
    std::string blocked_on = "";
    bool ok1 = LockManager::getInstance().tryAcquireLocks("T01", {"A101", "A102"}, blocked_on);
    assert(ok1);
    assert(LockManager::getInstance().isResourceLocked("A101"));
    assert(LockManager::getInstance().isResourceLocked("A102"));
    std::cout << "  ✓ T01 acquired ordered locks on A101 and A102\n";

    // Test 2: T02 concurrently requests reverse transfer from A102 to A101
    bool ok2 = LockManager::getInstance().tryAcquireLocks("T02", {"A102", "A101"}, blocked_on);
    assert(!ok2);
    assert(blocked_on == "A101" || blocked_on == "A102");
    std::cout << "  ✓ T02 contention handled cleanly without circular wait (blocked on: " << blocked_on << ")\n";

    // Test 3: T01 releases locks upon completion
    auto released = LockManager::getInstance().releaseLocks("T01");
    assert(released.size() == 2);
    assert(!LockManager::getInstance().isResourceLocked("A101"));
    assert(!LockManager::getInstance().isResourceLocked("A102"));
    std::cout << "  ✓ T01 safely released locks; resources now available\n";

    // Test 4: T02 can now successfully acquire
    bool ok3 = LockManager::getInstance().tryAcquireLocks("T02", {"A102", "A101"}, blocked_on);
    assert(ok3);
    std::cout << "  ✓ T02 successfully unblocked and acquired resources\n";

    std::cout << "[Test: LockManager] ALL DEADLOCK PREVENTION ASSERTIONS PASSED!\n";
    return 0;
}
