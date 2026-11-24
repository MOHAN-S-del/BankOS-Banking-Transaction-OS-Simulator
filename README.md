# ⚡ BankOS Middleware — OS Scheduler & ACID Transaction Ledger

A full-stack banking middleware engine built with **Python (Flask)**, **MySQL (`mysql-connector-python`)**, and modern **HTML/CSS/JavaScript**. It models high-volume incoming banking transactions as Operating System Processes (**Process Control Blocks / PCBs**) scheduled through a thread-safe **3-Level Multi-Level Feedback Queue (MLFQ)** before committing atomically to MySQL storage.

---

## 🚀 Quickstart: Exact 3-Step Setup

### Step 1: Initialize Database in MySQL
Log into MySQL and execute `schema.sql` to initialize `bankos_db` and seed 8 customer accounts with ₹50,000+ balances:
```bash
mysql -u root -p < schema.sql
```
*(If MySQL has no password, simply run `mysql -u root < schema.sql`. If MySQL is unavailable, the application automatically activates an In-Memory ACID engine so live demonstrations never fail).*

### Step 2: Install Python Dependencies
```bash
pip install -r requirements.txt
```
*(Packages required: `flask`, `mysql-connector-python`)*

### Step 3: Launch BankOS Middleware
```bash
python app.py
```
Open your browser at **[http://127.0.0.1:5000](http://127.0.0.1:5000)**.

---

## 🏛️ System Architecture

### 1. Relational Database Schema (`schema.sql` & `bankos_db`)
The system enforces strict relational normalization across **5 linked tables**:
1. `accounts` (`account_id` PK, `customer_name`, `account_type`, `created_at`)
2. `balance` (`balance_id` PK, `account_id` FK -> accounts, `current_balance`, `last_updated`)
3. `transactions` (`txn_id` PK, `sender_id` FK, `receiver_id` FK, `txn_category`, `amount`, `burst_time`, `assigned_queue`, `status`, `created_at`)
4. `atm_transactions` (`atm_id` PK, `txn_id` FK -> transactions, `atm_location`, `card_last4`)
5. `upi_payroll_transactions` (`channel_id` PK, `txn_id` FK -> transactions, `channel_mode`, `reference_handle`)

#### Mandatory Multi-Table SQL JOIN
All transaction views and history are fetched via multi-table relational `JOIN` queries:
```sql
SELECT 
    t.txn_id,
    t.sender_id, s.customer_name AS sender_name, sb.current_balance AS sender_balance,
    t.receiver_id, r.customer_name AS receiver_name, rb.current_balance AS receiver_balance,
    t.txn_category, t.amount, t.burst_time, t.assigned_queue, t.status, t.created_at,
    atm.atm_location, atm.card_last4,
    up.channel_mode, up.reference_handle
FROM transactions t
INNER JOIN accounts s ON t.sender_id = s.account_id
INNER JOIN balance sb ON s.account_id = sb.account_id
INNER JOIN accounts r ON t.receiver_id = r.account_id
INNER JOIN balance rb ON r.account_id = rb.account_id
LEFT JOIN atm_transactions atm ON t.txn_id = atm.txn_id
LEFT JOIN upi_payroll_transactions up ON t.txn_id = up.txn_id
ORDER BY t.created_at DESC;
```

#### ACID Concurrency & Verification
When a transaction finishes its CPU burst, balances are updated inside an isolated MySQL transaction:
```sql
START TRANSACTION;
SELECT current_balance FROM balance WHERE account_id = ? FOR UPDATE;
-- Validate balance >= amount
UPDATE balance SET current_balance = current_balance - ? WHERE account_id = ?;
UPDATE balance SET current_balance = current_balance + ? WHERE account_id = ?;
UPDATE transactions SET status = 'COMPLETED' WHERE txn_id = ?;
COMMIT; -- If insufficient balance: ROLLBACK and mark status = 'FAILED'
```

---

### 2. OS Scheduler Engine: 3-Level MLFQ (`scheduler.py`)
Transactions enter the middleware as **Process Control Blocks (PCBs)** with an arrival time, burst time (1s to 8s), and category:

| Queue | Priority | Algorithm | Workload Filter | Description |
|---|---|---|---|---|
| **Queue 1** | **High** | **Preemptive Priority** | `ATM` & `UPI` | Instant micro-payments. Preempts Q2 and Q3. |
| **Queue 2** | **Medium** | **Shortest Job First (SJF)** | `STANDARD` (`burst <= 3s`) | Small wire transfers. Sorted ascending by burst time. |
| **Queue 3** | **Low** | **First-Come, First-Served (FCFS)** | `PAYROLL` & `burst > 3s` | Batch salary runs & heavy transfers. Executed in arrival order when Q1 & Q2 are empty. |

* **Background Traffic Generator:** Continuously generates realistic ATM, UPI, and Payroll transactions between accounts every 3–4 seconds to keep the queues dynamic. Can be toggled on/off via the **Traffic** button.

---

### 3. Frontend: 3-Slide Horizontal Viewport (`templates/index.html`)
The user interface avoids vertical scrolling by using a clean **3-Slide Horizontal Carousel** styled in a sober, minimalist light theme (White `#FFFFFF`, Blush `#FFF5F7`, and Rose-Pink `#E11D48`):

* **Slide 1: Live Middleware & Testing Console:**
  * **Left Column:** Manual Test Transaction Injector with preset buttons (Test ATM, Test UPI, Test SJF, Test Payroll, Test Overdraft). Injected transactions are marked with `is_test_txn = True`.
  * **Right Column:** 
    * **Tracked Test Transaction Banner:** Dedicated glowing pink badge (`★ DEMO TEST`) and 3-step progress pipeline (`Queued in MLFQ ➔ Executing on CPU Core 0 ➔ Committed to MySQL (ACID Passed)`).
    * **Active CPU Core 0 ALU Worker Card** with animated progress bar and remaining burst display.
    * **Queue 1, Queue 2, and Queue 3 Monitors** displaying live PCBs with glowing highlights for demo transactions.
* **Slide 2: MySQL Joined Ledger & Channel Logs:**
  * Live multi-table joined table showing customer names, channel metadata, burst time, assigned queue, status, and post-transaction balance.
  * Category filter pills (`ALL`, `ATM`, `UPI`, `STANDARD`, `PAYROLL`), search bar, and CSV export.
* **Slide 3: System Architecture & OS Metrics:**
  * Performance KPIs: Total Processed, Queue 1/2/3 Average Wait Times, and Throughput.
  * Interactive MLFQ Routing Architecture Diagram.
  * Real-Time Verified Customer Balances Table.

---

## 🧪 Live Demonstration Guide (For Evaluators & Faculty)

1. **Demonstrate Queue 1 Priority (ATM/UPI Preemption):**
   * On Slide 1, click **"Test ATM (Q1)"** or **"Test UPI (Q1)"** and click **"Proceed & Inject"**.
   * Notice the transaction enters **Queue 1** and is highlighted with a glowing pink **`★ DEMO TEST`** card.
   * Watch it immediately preempt lower queues and execute on **CPU Core 0**.

2. **Demonstrate Queue 2 Shortest Job First (SJF):**
   * Click **"Test Small (Q2 SJF)"** (Burst: 2s).
   * Notice it enters **Queue 2**, sorting ahead of longer standard jobs.

3. **Demonstrate Queue 3 First-Come, First-Served (FCFS):**
   * Click **"Test Payroll (Q3 FCFS)"** (Burst: 6s).
   * Notice it is routed to **Queue 3** as batch payroll and executes in arrival order once Q1 and Q2 are cleared.

4. **Demonstrate ACID Rollback (Insufficient Balance):**
   * Click **"Test Overdraft (ACID)"** (transfers ₹500,000 from Ananya Iyer who only has ₹65,000).
   * Watch the CPU worker execute the burst, invoke MySQL `SELECT FOR UPDATE`, detect the constraint violation, and trigger a safe **ACID ROLLBACK** (Status: `FAILED`, Reason: `ACID Rollback: Insufficient Balance`).

5. **Demonstrate Multi-Table SQL JOINs:**
   * Slide over to **Slide 2: MySQL Joined Ledger**.
   * Show that each transaction displays the sender and receiver's actual customer name, their post-transfer balance, and specific channel metadata (`ATM Location` or `UPI Virtual Payment Address`).

6. **Demonstrate Keyboard Slide Navigation:**
   * Use the **Left Arrow (←)** and **Right Arrow (→)** keys to smoothly slide between the 3 views.
