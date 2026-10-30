# BankOS: Banking Transaction OS & DBMS Simulation Engine
### Autonomous Concurrency Control, Multi-Level Feedback Queue (MLFQ) & ACID Bank Database Pipeline

A high-performance C++20 simulation engine that models banking transactions (**UPI**, **ATM**, **Salary Payroll**) as operating system processes. Features a dual-tier Multi-Level Feedback Queue (MLFQ) scheduler with Shortest Remaining Time First (SRTF), Round Robin (RR), Anti-Starvation Aging, strict Two-Phase Locking (2PL) with Havender Deadlock Denial, and an end-to-end transactional pipeline dispatching scheduled operations to a central Bank Database and destination account servers.

---

## 🏛 System Architecture & End-to-End Pipeline

BankOS bridges the gap between **Operating Systems process scheduling** and **DBMS transactional concurrency control**:

```
[TRANSACTION INPUTS]            [ADAPTIVE BANK OS]               [BANK DATABASE]                 [BANK CORE SERVERS]
┌──────────────────┐            ┌──────────────────────┐         ┌──────────────────────┐        ┌─────────────────────────┐
│ • UPI (60%)      │────BUS 1──>│ • Queue 1 (SRTF)     │───BUS 2>│ • Scheduled Buffer  │──BUS 3>│ • Bank Server 1 (Retail)│
│ • Withdraw (25%) │  (Packets) │ • ▲ Aging Elevator   │ (Queued)│ • ACID Integrity Gate│ (Split)│   Accounts #1001–#1008  │
│ • Payroll (15%)  │            │ • Queue 2 (RR, Q=5)  │         │   - A: Atomicity     │        ├─────────────────────────┤
└──────────────────┘            │ • 2PL Lock Arbiter   │         │   - C: Consistency   │        │ • Bank Server 2 (ATM)   │
                                │ • CPU Core (ALU)     │         │   - I: Isolation     │        │   Vault Acc #2001–#2002 │
                                └──────────────────────┘         │   - D: Durability    │        ├─────────────────────────┤
                                                                 │ • MySQL Commits/Rolls│        │ • Bank Server 3 (Payroll│
                                                                 └──────────────────────┘        │   Treasury Acc #5001    │
                                                                                                 └─────────────────────────┘
```

### Pipeline Stages Explained in Simple Language:
1. **Transaction Inputs (Step 1):**
   * **UPI (60%):** High-frequency instant mobile payments (₹50–₹500). Fast 1–4 CPU ticks burst, requiring sender and receiver retail account locks.
   * **ATM Withdrawals (25%):** Cash withdrawals (₹500–₹2,500). 2–6 CPU ticks burst, locking customer account + ATM physical cash vault.
   * **Payroll / Salary (15%):** Bulk corporate disbursements (₹35,000–₹60,000). Heavy 10–30 CPU ticks burst, locking corporate treasury + employee accounts.
2. **Adaptive Bank OS (Step 2):**
   * **Queue 1 (High Priority — SRTF):** Shortest Remaining Time First priority for retail UPI and ATM micro-payments. Preempts Queue 2.
   * **Queue 2 (Low Priority — Round Robin):** Dedicated to long payroll runs sliced into 5-tick time quantums ($Q=5$) to prevent monopolizing the CPU.
   * **Anti-Starvation Aging Elevator:** If a Salary process waits $\ge 50$ ticks in Queue 2, it is automatically promoted to Queue 1.
   * **Two-Phase Locking (2PL) Arbiter:** Locks resources in strict monotonic order $\min(A,B) < \max(A,B)$, mathematically eliminating circular wait conditions (**0% Deadlocks**).
   * **CPU Dispatcher Core:** The virtual CPU executing transactions tick by tick.
3. **Scheduled Bank Database (Step 3):**
   * **Scheduled Commit Buffer:** Sequences validated transactions in serializable order.
   * **ACID Integrity Gate:**
     * **A - Atomicity:** All-or-nothing execution. Automatic rollback on overdraft / insufficient funds.
     * **C - Consistency:** Invariants preserved ($Balance \ge 0.00$, sender debit == receiver credit).
     * **I - Isolation:** Strict 2PL serializability prevents dirty reads, non-repeatable reads, and lost updates.
     * **D - Durability:** Persistent commit log in MySQL InnoDB engine (`schema.sql`).
4. **Bank Destination Servers (Step 4):**
   * **Bank Server 1 (Retail Switch):** Manages retail checking/savings accounts (1001–1008: Alice, Bob, Charlie, Diana, etc.).
   * **Bank Server 2 (ATM Network & Vault):** Coordinates physical cash vault reserves (2001–2002) and cash hardware balances.
   * **Bank Server 3 (Corporate Payroll Core):** Manages corporate treasury (5001–5002: Apex Corp) and bulk employee splits.

---

## 📚 DBMS & Operating System Concepts Implemented

| Category | Concept | Implementation in BankOS |
|---|---|---|
| **OS** | **Process Control Block (PCB)** | Banking transactions modeled as OS processes with PID, state registers (`NEW`, `READY`, `RUNNING`, `WAITING`, `TERMINATED`), burst cycles, and lock dependencies. |
| **OS** | **MLFQ CPU Scheduling** | Dual-queue preemptive CPU scheduler balancing high-frequency retail micro-payments (SRTF) and batch corporate payrolls (Round Robin). |
| **OS** | **Anti-Starvation Aging** | Mathematical promotion elevator: if $WaitTime(P) \ge 50\text{ ticks}$, dynamically promotes $P$ to Queue 1. |
| **OS** | **Preemption & Context Switching** | Round Robin time quantum expiration ($Q=5$) and higher-priority SRTF job arrival preemption. |
| **DBMS** | **Two-Phase Locking (2PL)** | *Phase 1 (Growing):* Acquires all required account locks. *Phase 2 (Lock Point):* Active CPU balance mutation. *Phase 3 (Shrinking):* Releases all locks only at COMMIT or ROLLBACK. |
| **DBMS** | **Deadlock Prevention** | Havender’s Monotonic Ordering: Locks always acquired in $\min(From, To) < \max(From, To)$ order. Denies Coffman's circular wait condition ($0\%$ deadlocks). |
| **DBMS** | **Atomicity (ACID)** | Overdraft protection test: If Account #1005 (balance ₹300) attempts a ₹5,000 transfer, an automatic ROLLBACK aborts the transaction with zero partial state. |
| **DBMS** | **Consistency (ACID)** | Table constraints (`CHECK (Balance >= 0.00)`) and balance conservation ($Debit = Credit$). |
| **DBMS** | **Isolation (ACID)** | Strict 2PL guarantees serializable transaction execution without dirty reads. |
| **DBMS** | **Durability (ACID)** | Transactions and audit trails committed to persistent MySQL storage with Write-Ahead Logging (WAL). |

---

## 🖥 Professional Multi-Page Web Observability Console

The web console (`http://localhost:8080`) features a **clean, tabbed multi-page interface** designed for technical presentations:

1. **`1. ARCHITECTURE & RUNNING DIAGRAM`**:
   * Live animated packet flow matching the system sketch.
   * Simple-language explanation on every block.
   * **Bank Core Accounts Ledger Table:** Real-time balances with green/red credit/debit flashes, lock status, and 2PL phases (`Growing`, `Lock Point`, `Shrinking`).
   * **Bank Database Transaction Dispatch Log:** Live streaming of the exact transactional SQL commands dispatched to the central database.
2. **`2. CPU & MLFQ SCHEDULER`**:
   * CPU Dispatcher Core (Core 0 ALU) with active process registers (guaranteed **never empty**).
   * Multi-Level Feedback Queue visualizer (Queue 1 SRTF, Aging Elevator, Queue 2 Round Robin, Waiting Chamber).
3. **`3. OSCILLOSCOPE & BENCHMARKS`**:
   * Real-time canvas charts for Throughput Velocity (TPS), Contention Dynamics, and Abort Rate Shield.
   * **Interactive Benchmark Scenarios (Clickable Buttons):**
     * `⚡ UPI SURGE`: Injects 30 retail payments into Queue 1 (SRTF).
     * `🏢 PAYROLL BATCH`: Injects 20 corporate payroll runs into Queue 2 (Round Robin).
     * `🔒 HOTSPOT CONTENTION`: Injects 24 colliding transactions on Account #1001 (Havender 2PL).
     * `🛡 TEST ACID ROLLBACK`: Dispatches an overdraft on Account #1005 (₹300 balance vs ₹5,000 transfer) to trigger an immediate atomicity rollback.
     * `🔄 RESET`: Resets simulation counters and queues.
4. **`4. PCB TABLE & AUDIT FEED`**:
   * Searchable and filterable PCB table (`ALL`, `RUNNING`, `READY`, `WAITING`, `COMMITTED`, `FAILED`).
   * Click any row to inspect full 64-byte OS process registers and raw JSON.
   * Kernel state transition audit log.
5. **`5. DBMS & OS CONCEPTS`**:
   * Educational architecture cards with formulas, state machine diagrams, and explanations in plain language.

---

## 🚀 Building & Running

### Prerequisites
* Modern C++ compiler supporting **C++20** (`clang++` or `g++`).
* `make` build system.
* *(Optional)* MySQL 8.x Server with MySQL C++ Connector (X DevAPI). If MySQL is not running, BankOS automatically falls back to its embedded in-memory ACID transactional ledger engine.

### 1. Build the Simulator
```bash
make clean && make -j4
```

### 2. Run Comprehensive Test Suite
```bash
make test
```
The unit test suite validates:
1. `ProcessControlBlock` fields and JSON serialization.
2. `WorkloadGenerator` generation of 176,000 bursty transactions.
3. Queue 1 Shortest Remaining Time First (SRTF) dispatching.
4. Queue 2 Round Robin time quantum preemption.
5. Anti-starvation Aging mechanism promotion from Q2 to Q1.
6. 2PL mutual exclusion lock arbitration and `WAITING` transitions.
7. State transitions JSON file export.
8. Real-time telemetry tracking and contention scoring.
9. MD-ACC Exponential Backoff delays ($B_0 \times 2^{retry}$).
10. Comparison mode (Baseline collision aborts vs. Adaptive MD-ACC).
11. Strict lock ordering deadlock prevention (1,000 reciprocal transfers without deadlock).
12. MySQL transaction rollback on insufficient funds (ACID Atomicity).

### 3. Launch Live Simulation Dashboard
```bash
./bank_os_sim --port 8080
```
Open your browser at:
```
http://localhost:8080
```

### 4. Fast Headless Batch Mode
To run full batch execution without the HTTP server:
```bash
./bank_os_sim --headless
```
This generates `state_transitions.json` containing the full transition audit log.

---

## 📡 REST API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/api/status` | `GET` | Simulation tick, queue sizes, completed count, and aging promotions |
| `/api/state` | `GET` | Full simulation snapshot: running PCB, queues, active PCBs, bank accounts, and metrics |
| `/api/accounts` | `GET` | Live bank account balances, customer mapping, and 2PL lock states |
| `/api/queues` | `GET` | Snapshot of Queue 1 (SRTF), Queue 2 (RR), and Waiting Chamber |
| `/api/transitions` | `GET` | Live stream of recent state transitions with reason metadata |
| `/api/workload/spike` | `GET/POST` | Inject benchmark scenarios: `upi_peak`, `salary_clearing`, `settlement_hotspot`, `acid_rollback`, `reset` |
| `/api/comparison-mode` | `GET/POST` | Toggle between Baseline (Direct DB collisions) and Adaptive BankOS Ledger (MD-ACC) |
| `/api/db/status` | `GET` | MySQL persistent storage stats: connection status, queries, commits, rollbacks |
| `/api/lockmanager/status` | `GET` | 2PL Lock Manager stats: lock attempts, conflicts, timeouts, deadlocks prevented |
| `/api/control/pause` | `GET` | Pause discrete tick execution |
| `/api/control/resume` | `GET` | Resume discrete tick execution |
| `/api/control/speed` | `GET` | Adjust clock speed delay in microseconds (`?delay=80000`) |
| `/api/export` | `GET` | Trigger export of state transitions to `state_transitions.json` |

---

## 📁 Repository Structure

```
BankOS-Banking-Transaction-OS-Simulator/
├── include/
│   ├── pcb.hpp                     # Process Control Block struct & OS lifecycle states
│   ├── mlfq_scheduler.hpp          # MLFQ Scheduler (SRTF + RR + Aging + Auto-Stream)
│   ├── lock_manager.hpp            # 2PL Concurrency Control & Havender Deadlock Denial
│   ├── mysql_storage.hpp           # Persistent MySQL Storage (X DevAPI) & ACID rollback engine
│   ├── middleware.hpp              # MD-ACC Adaptive Concurrency & Exponential Backoff
│   ├── workload_generator.hpp      # Bursty 176k transaction workload generator
│   ├── state_transition.hpp        # Transition audit logging
│   └── httplib.h                   # Embedded C++ HTTP server
├── src/
│   └── main.cpp                    # Application entrypoint & REST API server
├── tests/
│   └── test_bank_os_simulator.cpp  # 12 unit tests validating OS & DBMS features
├── web/
│   ├── index.html                  # Multi-page tabbed dashboard with running diagram
│   ├── styles.css                  # Professional systems console styling (charcoal/slate)
│   └── app.js                      # Client engine, state polling, oscilloscopes & logs
├── db/
│   └── schema.sql                  # MySQL database schema (Customers, Accounts, Transactions, AuditLogs)
├── Makefile                        # Compilation & test runner
└── README.md                       # Architectural documentation
```
