-- ============================================================================
-- BankOS Middleware: Viva & Code Walkthrough Demonstration Queries
-- File: demo.sql
-- Run directly in MySQL Workbench or MySQL Command Line during your presentation.
-- Demonstrates Relational Normalization, Mandatory Multi-Table JOINs,
-- and Live OS PCB Telemetry (MLFQ Scheduling & Dynamic Aging Starvation Rescue).
-- ============================================================================

USE bankos_db;

-- ----------------------------------------------------------------------------
-- QUERY 1: Verify All 8 Synchronized Relational & OS-Telemetry Tables
-- ----------------------------------------------------------------------------
SHOW TABLES;

-- ----------------------------------------------------------------------------
-- QUERY 2: Customer & Live Balance (accounts INNER JOIN balance)
-- Shows all customer accounts paired with verified atomic ledger balances
-- ----------------------------------------------------------------------------
SELECT 
    a.account_id, 
    a.customer_name, 
    a.account_type, 
    b.current_balance AS verified_balance_inr,
    b.last_updated
FROM accounts a 
INNER JOIN balance b ON a.account_id = b.account_id
ORDER BY a.account_id ASC;

-- ----------------------------------------------------------------------------
-- QUERY 3: Master 5-Table Transaction & Channel JOIN
-- Combines transactions, accounts (sender & receiver names/balances),
-- and polymorphic channel metadata (atm_transactions, upi_payroll_transactions)
-- ----------------------------------------------------------------------------
SELECT 
    t.txn_id,
    s.customer_name AS sender_name,
    sb.current_balance AS sender_bal,
    r.customer_name AS receiver_name,
    rb.current_balance AS receiver_bal,
    t.txn_type,
    t.amount,
    t.auto_burst_time,
    t.assigned_queue,
    t.status,
    COALESCE(atm.atm_location, up.reference_id, 'N/A') AS channel_reference,
    COALESCE(atm.card_last4, up.channel_mode, 'N/A') AS channel_mode,
    t.created_at
FROM transactions t
INNER JOIN accounts s ON t.sender_id = s.account_id
INNER JOIN balance sb ON s.account_id = sb.account_id
INNER JOIN accounts r ON t.receiver_id = r.account_id
INNER JOIN balance rb ON r.account_id = rb.account_id
LEFT JOIN atm_transactions atm ON t.txn_id = atm.txn_id
LEFT JOIN upi_payroll_transactions up ON t.txn_id = up.txn_id
ORDER BY t.created_at DESC;

-- ----------------------------------------------------------------------------
-- QUERY 4: Live OS Ready Queue & Starvation Check JOIN
-- Inspects jobs currently in MLFQ RAM queues, ordered by wait_time_sec descending
-- Highlights processes approaching or exceeding the 4s Starvation Warning
-- ----------------------------------------------------------------------------
SELECT 
    q.queue_level, 
    q.txn_id, 
    t.txn_type, 
    t.amount, 
    q.priority_score,
    q.remaining_burst, 
    q.wait_time_sec,
    CASE 
        WHEN q.wait_time_sec >= 6 THEN 'AGED PROMOTION DUE (>=6s)'
        WHEN q.wait_time_sec >= 4 THEN 'AT-RISK: STARVING (>=4s)'
        ELSE 'NORMAL'
    END AS starvation_status,
    q.enqueued_at
FROM transaction_queue q 
INNER JOIN transactions t ON q.txn_id = t.txn_id 
ORDER BY q.wait_time_sec DESC, q.priority_score DESC;

-- ----------------------------------------------------------------------------
-- QUERY 5: Starvation & Dynamic Aging Rescue Audit JOIN
-- Proves Starvation Prevention directly in MySQL by filtering for AGED promotions
-- ----------------------------------------------------------------------------
SELECT 
    l.log_id, 
    l.txn_id, 
    t.txn_type, 
    t.amount, 
    l.from_state, 
    l.to_state, 
    l.queue_level, 
    l.event_note, 
    l.logged_at 
FROM transaction_logs l 
INNER JOIN transactions t ON l.txn_id = t.txn_id 
WHERE l.event_note LIKE '%AGED%' OR l.to_state LIKE '%Q1%'
ORDER BY l.log_id DESC;

-- ----------------------------------------------------------------------------
-- QUERY 6: Full PCB State-Transition Trail JOIN
-- Demonstrates complete OS lifecycle: NEW -> READY -> RUNNING -> TERMINATED
-- ----------------------------------------------------------------------------
SELECT 
    l.log_id, 
    l.txn_id, 
    t.txn_type, 
    l.from_state, 
    l.to_state, 
    l.queue_level, 
    l.event_note, 
    l.logged_at 
FROM transaction_logs l 
INNER JOIN transactions t ON l.txn_id = t.txn_id 
ORDER BY l.log_id DESC 
LIMIT 30;

-- ----------------------------------------------------------------------------
-- QUERY 7: Queue-Wise Aggregation (GROUP BY & COUNT)
-- Summarizes total transactions and average burst time per MLFQ tier
-- ----------------------------------------------------------------------------
SELECT 
    assigned_queue, 
    COUNT(*) AS total_transactions,
    ROUND(AVG(amount), 2) AS avg_transaction_amount,
    ROUND(AVG(auto_burst_time), 2) AS avg_burst_seconds,
    MIN(auto_burst_time) AS min_burst,
    MAX(auto_burst_time) AS max_burst
FROM transactions
GROUP BY assigned_queue
ORDER BY assigned_queue ASC;

-- ----------------------------------------------------------------------------
-- QUERY 8: Live MLFQ Scheduler Metrics (scheduler_metrics)
-- OS telemetry across Q1 (Priority), Q2 (SJF), and Q3 (FCFS)
-- Shows transactions saved from starvation via Dynamic Aging
-- ----------------------------------------------------------------------------
SELECT 
    metric_id,
    queue_level,
    total_processed,
    avg_wait_time,
    avg_turnaround_time,
    starvation_rescued AS saved_from_starvation,
    last_updated
FROM scheduler_metrics
ORDER BY metric_id ASC;
