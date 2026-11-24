# BankOS Middleware ⚡🏦
### OS-Driven Concurrent Banking Transaction Engine & Real-Time Telemetry

---

## 📌 What is BankOS Middleware?
**BankOS Middleware** is a high-performance banking transaction orchestrator that solves the critical problem of transaction contention, system lag, and starvation in financial systems. When thousands of urgent retail payments (ATM cash withdrawals, UPI instant payments) arrive alongside heavy corporate transfers (bulk payroll disbursements, inter-bank settlements), ordinary FIFO queues cause retail users to wait for seconds while heavy jobs hog the system. BankOS bridges a custom **C++17 Multi-Level Feedback Queue (MLFQ) OS Scheduler Engine** with a **MySQL ACID Relational Database** and a responsive **Liquid Glass Dashboard**, ensuring instant turnaround for everyday transactions while completely preventing starvation for heavy background batches using dynamic aging.

---

## ⚙️ How It Works (The 4 Simple Steps)

```
[ Incoming Banking Transaction ]
               │
               ▼
   1. Auto Burst Weight Assignment
      ATM = 1s | UPI = 2s | STANDARD = 3s/5s | PAYROLL = 7s
               │
               ▼
   2. 3-Level MLFQ Scheduling
      Queue 1: Preemptive Priority (q = 2s)  ➔ Urgent ATM & UPI
      Queue 2: Shortest Job First (q = 4s)    ➔ Small Standard Transfers
      Queue 3: First-Come, First-Served (FIFO) ➔ Corporate Payroll Batches
               │
               ▼
   3. Starvation Prevention via Dynamic Aging
      • Wait ≥ 4s: Flagged as "Facing Starvation" (Live Warning)
      • Wait ≥ 6s in Q3: Automatically promoted to Q2 (SJF)
      • Wait ≥ 5s in Q2: Automatically promoted to Q1 (Priority)
               │
               ▼
   4. MySQL ACID Atomic Commit & Live Telemetry
      Atomic balance transfers committed in MySQL InnoDB with audit trail logging
```

1. **Auto Burst Assignment:** The system automatically assigns realistic CPU burst execution times based on transaction type and amount (ATM = 1s, UPI = 2s, Standard ≤ ₹10k = 3s, Standard > ₹10k = 5s, Payroll = 7s). Users never manually input burst times.
2. **3-Level MLFQ Scheduling:** Transactions enter a hierarchical multi-level feedback queue. Urgent retail jobs enter Queue 1 and preempt lower queues. Demoted or standard jobs enter Queue 2 (SJF), and heavy payroll batches enter Queue 3 (FCFS).
3. **Starvation Prevention via Dynamic Aging:** Low-priority batch jobs trapped in Queue 3 because of continuous incoming UPI/ATM traffic are protected by Dynamic Aging. Any job waiting $\ge 4\text{s}$ is flagged as *Facing Starvation*; if its wait reaches $6\text{s}$, the C++ engine immediately promotes it to Queue 2, and subsequently to Queue 1 if needed.
4. **MySQL ACID Commit:** Once execution completes on Core 0, the transaction is atomically committed across MySQL database tables (`accounts`, `balance`, `transactions`, `transaction_logs`), guaranteeing zero money creation or loss.

---

## 📁 Project File Guide

| File | Purpose |
| :--- | :--- |
| `schema.sql` | Crystal-clear MySQL schema creating database `bankos_db` and 8 synchronized tables with seed accounts and balances. |
| `pcb.hpp` | Clean C++ header defining the Process Control Block (`Process`), OS lifecycle states (`NEW`, `READY`, `RUNNING`, `TERMINATED`), and the auto burst calculator. |
| `queue_algorithms.hpp` | Cleanly isolated implementations of all 3 scheduling algorithms: `PriorityQueueQ1`, `SJFQueueQ2`, and `FCFSQueueQ3`. |
| `mlfq_controller.hpp` | Multi-Level Feedback Queue controller managing top-down hierarchical CPU dispatch, time quantum demotions, and the dynamic aging engine. |
| `scheduler_engine.cpp` | Main C++ executable loop providing dual-bridge inter-process communication (TCP socket port 8765 + atomic file bridge). |
| `server.py` | Lightweight Python Flask application that connects the C++ scheduler binary, local MySQL database, and the frontend web dashboard. |
| `templates/index.html` | High-fidelity 3-slide web dashboard featuring live MLFQ core execution, starvation watch monitor, joined transaction ledger, and verified balances. |
| `demo.sql` | Ready-to-run demonstration SQL queries for MySQL Workbench / CLI showing 5-table JOINs, state logs, and starvation rescue telemetry. |

---

## 🚀 Quick 3-Step Run Instructions

### Step 1: Initialize MySQL Database
Make sure your local MySQL service is running, then load the database schema:
```bash
mysql -u root -p bankos_db < schema.sql
```
*(Or let `server.py` auto-initialize the database using your `.env` credentials).*

### Step 2: Compile the C++ MLFQ Scheduler Engine
Compile the modular C++17 scheduler engine into an optimized binary:
```bash
g++ -O2 -std=c++17 scheduler_engine.cpp -o scheduler -pthread
```

### Step 3: Start the BankOS Middleware Server
Launch the Python Flask server:
```bash
python server.py
```
Open your browser and navigate to:
```
http://127.0.0.1:5001/
```

---

## 🎯 Viva Demonstration Checklist
During your presentation or viva, use these 3 slides:
- **Slide 1 (MLFQ Core & Starvation Watch):** Click **"Inject Test Transaction"** to inject a ₹75,000 Payroll batch. Show how high-priority UPI traffic keeps Core 0 busy while the **Starvation Watch Monitor** shows the Payroll job ticking toward the $6\text{s}$ threshold and getting promoted with an `AGED RESCUED` badge.
- **Slide 2 (Joined Ledger & PCB Logs):** Switch between the **Joined Transaction Ledger** (5-table relational JOIN) and the **OS State-Transition Logs** to demonstrate the audit trail (`NEW ➔ READY ➔ RUNNING ➔ TERMINATED`).
- **Slide 3 (Live Telemetry & ACID Balances):** Show the prominent **"Transactions Saved from Starvation"** card updating live, and verify that customer balances in MySQL update atomically without concurrency anomalies. Run `demo.sql` in MySQL Workbench to show evaluators the database JOINs live.
