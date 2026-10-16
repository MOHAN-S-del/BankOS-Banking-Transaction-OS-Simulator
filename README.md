# BankOS: Banking Transaction OS Simulation Engine
### Autonomous Concurrency-Regulated & MLFQ Scheduled Transaction Processing

A high-performance C++20 simulation engine that models banking transactions (UPI, ATM, Salary) as operating system processes scheduled using a Multi-Level Feedback Queue (MLFQ) with Shortest Remaining Time First (SRTF), Round Robin (RR), and anti-starvation Aging.

---

## 🏛 Architectural Highlights

### 1. Process Control Block (PCB) Model
Every banking transaction is instantiated as a standard OS process managed through a `ProcessControlBlock` (PCB) struct:
- **`pid`**: Unique process identifier.
- **`type`**: Transaction classification (`UPI`, `ATM`, `Salary`).
- **`state`**: Standard OS process lifecycle states (`NEW`, `READY`, `RUNNING`, `WAITING`, `TERMINATED`).
- **`arrival_time`**: Discrete simulation tick when the banking request arrives at the switch.
- **`burst_time`**: Remaining CPU processing cycles (ticks) needed for completion.
- **`required_locks`**: Target account identifiers requiring strict 2PL mutual exclusion.
- **`queue2_ticks`**: Duration spent in Queue 2 to power anti-starvation aging.

### 2. Workload Generator (176,000 Transactions)
The `WorkloadGenerator` simulates **176,000 banking requests** reflecting real-world banking switches:
- **UPI (60%)**: Lightweight micro-payments (1–4 ticks burst), requiring sender & receiver retail account locks.
- **ATM (25%)**: Cash withdrawals & deposits (2–6 ticks burst), locking retail account + ATM cash vault reserve.
- **Salary (15%)**: Corporate payroll bulk disbursements (10–30 ticks burst), locking corporate treasury + multiple employee retail accounts.
- **Bursty Traffic Distribution**: Transactions cluster in burst waves (emulating peak retail shopping hours, end-of-month payroll runs, and evening UPI rushes) with Poisson/normal arrival jitter.

### 3. Multi-Level Feedback Queue (MLFQ) CPU Scheduler
- **Queue 1 (High Priority — SRTF)**:
  - Uses **Shortest Remaining Time First** for interactive retail payments (UPI & ATM).
  - Preempts Queue 2 whenever a shorter retail job arrives or becomes unblocked.
- **Queue 2 (Low Priority — Round Robin)**:
  - Dedicated to long-running batch Salary transactions.
  - Slices CPU time using a configurable **time quantum** ($Q = 5$ ticks).
  - Processes exceeding their quantum yield the CPU and rotate to the back of Queue 2.
- **Anti-Starvation Aging Mechanism**:
  - Solves the classic OS priority inversion / starvation dilemma:
  - If a Salary process remains in Queue 2 for more than **$X$ ticks** (default $X = 50$), the scheduler automatically **promotes it to Queue 1**.
  - In Queue 1, it competes with SRTF priority, guaranteeing bounded wait times.

### 4. Concurrency & Lock Arbitration (2PL)
- Accounts referenced in `required_locks` must be locked before execution.
- If a contending process holds any lock, the candidate transitions from `READY` to `WAITING`.
- Upon process termination or preemption, held locks are released and blocked processes in `WAITING` transition back to `READY`.

### 5. State Transition Logging & Web Dashboard
- All state transitions (`NEW -> READY -> RUNNING -> WAITING -> TERMINATED`) are logged with simulation ticks and transition reasons.
- Transitions can be dumped to `state_transitions.json`.
- A built-in **cpp-httplib** server serves REST endpoints and an interactive web visualizer on `http://localhost:8080`.

---

## 🚀 Building & Running

### Prerequisites
- Modern C++ compiler (`clang++` or `g++`) supporting C++20.
- Make build system.

### Build the Simulator
```bash
make clean && make all
```

### Run Unit Tests
```bash
make test
```
The test suite validates:
1. `ProcessControlBlock` fields and JSON serialization.
2. `WorkloadGenerator` generation of 176,000 bursty transactions.
3. Queue 1 Shortest Remaining Time First (SRTF) dispatching.
4. Queue 2 Round Robin time quantum preemption.
5. Anti-starvation Aging mechanism promotion from Q2 to Q1.
6. 2PL mutual exclusion lock arbitration and `WAITING` transitions.
7. State transitions JSON file export.

### Launch Live Localhost Simulation
```bash
./bank_os_sim --port 8080 --aging 50 --quantum 5
```

Open your browser at:
```
http://localhost:8080
```

### Headless Fast Execution Mode
To run full batch execution without HTTP server:
```bash
./bank_os_sim --headless
```
This generates `state_transitions.json` containing the transition audit log.

---

## 📡 REST API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/api/status` | `GET` | Current simulation tick, queue sizes, completed count, aging promotions |
| `/api/queues` | `GET` | Snapshot of running PCB, Queue 1 (SRTF), Queue 2 (RR), and Waiting PCBs |
| `/api/transitions` | `GET` | Live stream of recent state transitions with reason metadata |
| `/api/export` | `GET` | Trigger export of state transitions to `state_transitions.json` |
| `/api/control/pause` | `GET` | Pause discrete tick execution |
| `/api/control/resume` | `GET` | Resume discrete tick execution |
