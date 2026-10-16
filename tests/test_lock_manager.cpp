#if __has_include("common/types.hpp")
#include "common/types.hpp"
#include "concurrency/lock_manager.hpp"
#else
#include "../include/common/types.hpp"
#include "../include/concurrency/lock_manager.hpp"
#endif
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace adaptive_os;

void test_lock_acquire_and_release() {
    std::cout << "[Test] Running test_lock_acquire_and_release...\n";
    LockManager mgr;
    bool conflict = false;

    // Tx 1 acquires account 100
    bool ok1 = mgr.acquire_lock(100, 1, Milliseconds(50), conflict);
    assert(ok1);
    assert(!conflict);

    // Same Tx 1 reentrant acquire
    bool ok_re = mgr.acquire_lock(100, 1, Milliseconds(50), conflict);
    assert(ok_re);

    // Release
    mgr.release_lock(100, 1);

    // Tx 2 acquires account 100 now
    bool ok2 = mgr.acquire_lock(100, 2, Milliseconds(50), conflict);
    assert(ok2);
    assert(!conflict);
    mgr.release_lock(100, 2);

    std::cout << "  -> Passed: Basic lock acquisition and release verified!\n";
}

void test_bounded_wait_timeout() {
    std::cout << "[Test] Running test_bounded_wait_timeout...\n";
    LockManager mgr;
    bool conflict = false;

    // Tx 1 holds lock on account 200
    bool ok1 = mgr.acquire_lock(200, 1, Milliseconds(50), conflict);
    assert(ok1);

    // Tx 2 tries to acquire with short bounded timeout Tw = 20ms
    bool ok2 = mgr.acquire_lock(200, 2, Milliseconds(20), conflict);
    assert(!ok2); // Must time out!
    assert(conflict); // Must register as conflict!

    // Clean up
    mgr.release_lock(200, 1);
    std::cout << "  -> Passed: Bounded lock wait timeout Tw enforcement verified!\n";
}

void test_concurrency_and_queue_depth() {
    std::cout << "[Test] Running test_concurrency_and_queue_depth...\n";
    LockManager mgr;
    bool c1 = false;

    // Tx 1 holds lock on account 300
    mgr.acquire_lock(300, 1, Milliseconds(200), c1);

    std::atomic<bool> thread_waiting(false);
    std::thread t([&]() {
        bool c2 = false;
        thread_waiting = true;
        // Bounded wait 80ms
        mgr.acquire_lock(300, 2, Milliseconds(80), c2);
    });

    while (!thread_waiting) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(Milliseconds(10));

    // Queue depth should be at least 1
    assert(mgr.current_aggregate_queue_depth() >= 1);
    assert(mgr.is_hotspot(300, 1));

    // Release lock to unblock waiter
    mgr.release_lock(300, 1);
    t.join();

    mgr.release_lock(300, 2);
    std::cout << "  -> Passed: Lock queue depth and hotspot identification verified!\n";
}

void test_strict_lock_ordering_deadlock_prevention() {
    std::cout << "[Test] Running test_strict_lock_ordering_deadlock_prevention...\n";
    LockManager mgr;

    // Concurrently execute reciprocal transfers:
    // Thread 1: 500 -> 600
    // Thread 2: 600 -> 500 (reversed order)
    // Strict lock ordering guarantees min(500, 600) is locked first, preventing circular wait!
    std::atomic<int> completed(0);
    const int N = 300;

    std::thread t1([&]() {
        for (int i = 0; i < N; ++i) {
            bool conflict = false;
            bool ok = mgr.acquire_transfer_locks(500, 600, 101, Milliseconds(100), conflict);
            assert(ok);
            std::this_thread::yield();
            mgr.release_transfer_locks(500, 600, 101);
            completed.fetch_add(1);
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < N; ++i) {
            bool conflict = false;
            // 600 -> 500: sender ID > receiver ID
            bool ok = mgr.acquire_transfer_locks(600, 500, 202, Milliseconds(100), conflict);
            assert(ok);
            std::this_thread::yield();
            mgr.release_transfer_locks(600, 500, 202);
            completed.fetch_add(1);
        }
    });

    t1.join();
    t2.join();

    assert(completed.load() == N * 2);
    std::cout << "  -> Passed: " << completed.load() 
              << " concurrent reciprocal transfers completed without deadlocks (strict lock ordering enforced)!\n";
}

int main() {
    std::cout << "=== RUNNING LOCK MANAGER TEST SUITE ===\n";
    test_lock_acquire_and_release();
    test_bounded_wait_timeout();
    test_concurrency_and_queue_depth();
    test_strict_lock_ordering_deadlock_prevention();
    std::cout << "=== ALL LOCK MANAGER TESTS PASSED! ===\n\n";
    return 0;
}
