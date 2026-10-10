#include "pcb.hpp"
#include "process_manager.hpp"
#include "scheduler.hpp"
#include "lock_manager.hpp"
#include "db_manager.hpp"
#include <cassert>
#include <iostream>

using namespace BankOS;

int main() {
    std::cout << "[Test: Scheduler] Starting Round Robin Scheduler Unit Test...\n";

    // 1. Reset state
    Scheduler::getInstance().reset();
    Scheduler::getInstance().setTimeQuantum(2);

    // 2. Create 2 processes
    auto p1 = ProcessManager::getInstance().createProcess(TransactionType::DEPOSIT, "", "A101", 5000.0, 3);
    auto p2 = ProcessManager::getInstance().createProcess(TransactionType::WITHDRAW, "A102", "", 2000.0, 4);

    Scheduler::getInstance().enqueueProcess(p1->pid);
    Scheduler::getInstance().enqueueProcess(p2->pid);

    assert(Scheduler::getInstance().getReadyQueue().size() == 2);
    std::cout << "  ✓ Both processes successfully enqueued into Ready Queue\n";

    // 3. Step 1: P1 dispatched, executes tick 1
    Scheduler::getInstance().step();
    assert(Scheduler::getInstance().getCurrentRunningPID() == p1->pid);
    assert(p1->remaining_burst == 2);
    std::cout << "  ✓ P1 dispatched to CPU Core (Tick 1 executed)\n";

    // 4. Step 2: P1 executes tick 2. Slice limit (q=2) reached! Preempts back to Ready Queue
    Scheduler::getInstance().step();
    assert(p1->remaining_burst == 1);
    std::cout << "  ✓ P1 executed tick 2 (Slice limit reached)\n";

    // 5. Step 3: Round Robin preemption occurs, P2 dispatched
    Scheduler::getInstance().step();
    assert(Scheduler::getInstance().getCurrentRunningPID() == p2->pid);
    std::cout << "  ✓ Preemption verified: CPU context-switched to P2\n";

    std::cout << "[Test: Scheduler] ALL ASSERTIONS PASSED SUCCESSFULLY!\n";
    return 0;
}
