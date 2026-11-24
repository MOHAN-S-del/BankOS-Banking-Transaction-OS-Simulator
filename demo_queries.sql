-- ============================================================================
-- BankOS Middleware: Viva Demonstration & Evaluation SQL Script
-- File: demo_queries.sql
-- Run this in MySQL Workbench or MySQL Command Line during your presentation.
-- ============================================================================

USE bankos_db;

-- ----------------------------------------------------------------------------
-- QUERY 1: Show All 8 Synchronized Tables
-- Demonstrates full relational normalization and OS-telemetry tables
-- ----------------------------------------------------------------------------
SHOW TABLES;

-- ----------------------------------------------------------------------------
-- QUERY 2: Verified Customer & Balances (accounts INNER JOIN balance)
-- Shows all customer accounts with real-time atomic balance records
-- ----------------------------------------------------------------------------
SELECT 
    a.account_id, 
    a.customer_name, 
    a.account_type, 
    b.current_balance,
    b.last_updated
FROM accounts a 
INNER JOIN balance b ON a.account_id = b.account_id
ORDER BY a.account_id ASC;

-- ----------------------------------------------------------------------------
-- QUERY 3: Full 5-Table Master Transaction Ledger & Channel Join
-- Combines transactions, accounts (sender & receiver), balance, and channel
-- metadata (atm_transactions, upi_payroll_transactions) in a single query
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
    COALESCE(atm.card_last4, up.channel_mode, 'N/A') AS channel_detail,
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
-- QUERY 4: Live OS Ready Queue & Transaction Join
-- Demonstrates processes currently scheduled in MLFQ RAM ready queues
-- ----------------------------------------------------------------------------
SELECT 
    q.queue_level, 
    q.txn_id, 
    t.txn_type, 
    t.amount, 
    q.priority_score,
    q.remaining_burst, 
    q.wait_time_sec,
    q.enqueued_at
FROM transaction_queue q 
INNER JOIN transactions t ON q.txn_id = t.txn_id 
ORDER BY q.queue_level ASC, q.priority_score DESC;

-- ----------------------------------------------------------------------------
-- QUERY 5: PCB State-Transition Audit Trail (transaction_logs Join)
-- Shows complete OS lifecycle for every transaction:
-- NEW -> READY -> RUNNING -> TERMINATED (including starvation aging promotions)
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
LIMIT 25;

-- ----------------------------------------------------------------------------
-- QUERY 6: Live MLFQ Scheduler Metrics
-- Shows OS telemetry: total processed, CPU utilization %, average wait time,
-- and turnaround time across Q1 (Priority), Q2 (SJF), and Q3 (FCFS)
-- ----------------------------------------------------------------------------
SELECT 
    queue_level,
    total_processed,
    avg_wait_time,
    avg_turnaround_time,
    cpu_utilization_pct,
    last_updated
FROM scheduler_metrics
ORDER BY metric_id ASC;
