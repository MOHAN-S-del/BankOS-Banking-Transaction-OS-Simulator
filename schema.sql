-- ============================================================================
-- BankOS Middleware: MySQL Database & Normalized Schema
-- Database: bankos_db
-- 8 Synchronized Relational & OS-Telemetry Tables
-- ============================================================================

CREATE DATABASE IF NOT EXISTS bankos_db;
USE bankos_db;

-- Drop in reverse dependency order for clean migrations
DROP TABLE IF EXISTS transaction_logs;
DROP TABLE IF EXISTS transaction_queue;
DROP TABLE IF EXISTS scheduler_metrics;
DROP TABLE IF EXISTS upi_payroll_transactions;
DROP TABLE IF EXISTS atm_transactions;
DROP TABLE IF EXISTS transactions;
DROP TABLE IF EXISTS balance;
DROP TABLE IF EXISTS accounts;

-- ----------------------------------------------------------------------------
-- 1. accounts: Master Customer & Institutional Account Records
-- ----------------------------------------------------------------------------
CREATE TABLE accounts (
    account_id VARCHAR(10) PRIMARY KEY,
    customer_name VARCHAR(50) NOT NULL,
    account_type VARCHAR(20) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 2. balance: Atomic Balance Ledger
-- ----------------------------------------------------------------------------
CREATE TABLE balance (
    balance_id INT AUTO_INCREMENT PRIMARY KEY,
    account_id VARCHAR(10) NOT NULL UNIQUE,
    current_balance DECIMAL(12,2) NOT NULL DEFAULT 0.00,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_balance_account FOREIGN KEY (account_id) REFERENCES accounts(account_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 3. transactions: Central OS-Scheduled Transaction Register
-- ----------------------------------------------------------------------------
CREATE TABLE transactions (
    txn_id VARCHAR(15) PRIMARY KEY,
    sender_id VARCHAR(10) NOT NULL,
    receiver_id VARCHAR(10) NOT NULL,
    txn_type ENUM('ATM', 'UPI', 'PAYROLL', 'STANDARD') NOT NULL,
    amount DECIMAL(12,2) NOT NULL,
    auto_burst_time INT NOT NULL,
    assigned_queue VARCHAR(25) NOT NULL,
    status ENUM('QUEUED', 'RUNNING', 'COMPLETED', 'FAILED') NOT NULL DEFAULT 'QUEUED',
    is_test BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_txn_sender FOREIGN KEY (sender_id) REFERENCES accounts(account_id),
    CONSTRAINT fk_txn_receiver FOREIGN KEY (receiver_id) REFERENCES accounts(account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 4. atm_transactions: ATM Specific Channel Logs
-- ----------------------------------------------------------------------------
CREATE TABLE atm_transactions (
    atm_id INT AUTO_INCREMENT PRIMARY KEY,
    txn_id VARCHAR(15) NOT NULL UNIQUE,
    atm_location VARCHAR(50) NOT NULL,
    card_last4 VARCHAR(4) NOT NULL,
    CONSTRAINT fk_atm_txn FOREIGN KEY (txn_id) REFERENCES transactions(txn_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 5. upi_payroll_transactions: UPI & Corporate Payroll Channel Logs
-- ----------------------------------------------------------------------------
CREATE TABLE upi_payroll_transactions (
    channel_id INT AUTO_INCREMENT PRIMARY KEY,
    txn_id VARCHAR(15) NOT NULL UNIQUE,
    channel_mode ENUM('UPI', 'PAYROLL') NOT NULL,
    reference_id VARCHAR(50) NOT NULL,
    CONSTRAINT fk_channel_txn FOREIGN KEY (txn_id) REFERENCES transactions(txn_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 6. transaction_queue: Live Snapshot of Processes Waiting in MLFQ RAM Queues
-- ----------------------------------------------------------------------------
CREATE TABLE transaction_queue (
    queue_entry_id INT AUTO_INCREMENT PRIMARY KEY,
    txn_id VARCHAR(15) NOT NULL UNIQUE,
    queue_level ENUM('Q1_PRIORITY', 'Q2_SJF', 'Q3_FCFS') NOT NULL,
    priority_score INT NOT NULL DEFAULT 0,
    remaining_burst INT NOT NULL DEFAULT 1,
    wait_time_sec INT NOT NULL DEFAULT 0,
    enqueued_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_queue_txn FOREIGN KEY (txn_id) REFERENCES transactions(txn_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 7. transaction_logs: Complete OS PCB State-Transition Audit Trail
-- ----------------------------------------------------------------------------
CREATE TABLE transaction_logs (
    log_id INT AUTO_INCREMENT PRIMARY KEY,
    txn_id VARCHAR(15) NOT NULL,
    from_state VARCHAR(20) NOT NULL,
    to_state VARCHAR(20) NOT NULL,
    queue_level VARCHAR(25) NOT NULL,
    event_note VARCHAR(120),
    logged_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_log_txn FOREIGN KEY (txn_id) REFERENCES transactions(txn_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 8. scheduler_metrics: Live & Historical MLFQ Performance Telemetry
-- ----------------------------------------------------------------------------
CREATE TABLE scheduler_metrics (
    metric_id INT AUTO_INCREMENT PRIMARY KEY,
    queue_level ENUM('Q1_PRIORITY', 'Q2_SJF', 'Q3_FCFS') NOT NULL UNIQUE,
    total_processed INT NOT NULL DEFAULT 0,
    avg_wait_time DECIMAL(6,2) NOT NULL DEFAULT 0.00,
    avg_turnaround_time DECIMAL(6,2) NOT NULL DEFAULT 0.00,
    starvation_rescued INT NOT NULL DEFAULT 0,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============================================================================
-- SEED DATA: 6 Realistic Accounts with ₹50,000+ Balances
-- ============================================================================
INSERT INTO accounts (account_id, customer_name, account_type) VALUES
('ACC1001', 'Rajesh Sharma', 'SAVINGS'),
('ACC1002', 'Priya Patel', 'SAVINGS'),
('ACC1003', 'Amit Verma', 'CURRENT'),
('ACC1004', 'Sneha Kulkarni', 'SAVINGS'),
('ACC1005', 'TechCorp Ltd', 'CORPORATE'),
('ACC1006', 'Metro ATM Vault', 'VAULT');

INSERT INTO balance (account_id, current_balance) VALUES
('ACC1001', 150000.00),
('ACC1002', 85000.00),
('ACC1003', 240000.00),
('ACC1004', 95000.00),
('ACC1005', 1500000.00),
('ACC1006', 800000.00);

-- ============================================================================
-- SEED INITIAL METRICS FOR 3 QUEUES
-- ============================================================================
INSERT INTO scheduler_metrics (queue_level, total_processed, avg_wait_time, avg_turnaround_time, starvation_rescued) VALUES
('Q1_PRIORITY', 2, 0.45, 1.95, 0),
('Q2_SJF', 1, 1.20, 4.20, 0),
('Q3_FCFS', 1, 3.50, 10.50, 1);

-- ============================================================================
-- SEED INITIAL TRANSACTIONS (Enables Instant Multi-Table JOIN Demo)
-- ============================================================================
INSERT INTO transactions (txn_id, sender_id, receiver_id, txn_type, amount, auto_burst_time, assigned_queue, status, created_at) VALUES
('TXN-1001', 'ACC1001', 'ACC1006', 'ATM', 2500.00, 1, 'Q1_PRIORITY', 'COMPLETED', NOW() - INTERVAL 15 MINUTE),
('TXN-1002', 'ACC1002', 'ACC1004', 'UPI', 1800.00, 2, 'Q1_PRIORITY', 'COMPLETED', NOW() - INTERVAL 10 MINUTE),
('TXN-1003', 'ACC1003', 'ACC1001', 'STANDARD', 8500.00, 3, 'Q2_SJF', 'COMPLETED', NOW() - INTERVAL 6 MINUTE),
('TXN-1004', 'ACC1005', 'ACC1002', 'PAYROLL', 65000.00, 7, 'Q3_FCFS', 'COMPLETED', NOW() - INTERVAL 2 MINUTE);

INSERT INTO atm_transactions (txn_id, atm_location, card_last4) VALUES
('TXN-1001', 'Connaught Place South ATM #04', '4821');

INSERT INTO upi_payroll_transactions (txn_id, channel_mode, reference_id) VALUES
('TXN-1002', 'UPI', 'priya@okhdfcbank'),
('TXN-1004', 'PAYROLL', 'BATCH-DEC-TECHCORP-01');

-- Seed State-Transition Logs for Initial Completed Transactions
INSERT INTO transaction_logs (txn_id, from_state, to_state, queue_level, event_note, logged_at) VALUES
('TXN-1001', 'NEW', 'READY', 'Q1_PRIORITY', 'Admitted to Queue 1 (Priority) via ATM gateway', NOW() - INTERVAL 15 MINUTE),
('TXN-1001', 'READY', 'RUNNING', 'Q1_PRIORITY', 'Dispatched to CPU Core 0 for 1s debit execution', NOW() - INTERVAL 15 MINUTE + INTERVAL 1 SECOND),
('TXN-1001', 'RUNNING', 'TERMINATED', 'Q1_PRIORITY', 'Burst finished - MySQL ACID balance deduction committed', NOW() - INTERVAL 14 MINUTE),

('TXN-1002', 'NEW', 'READY', 'Q1_PRIORITY', 'Admitted to Queue 1 (Priority) via UPI gateway', NOW() - INTERVAL 10 MINUTE),
('TXN-1002', 'READY', 'RUNNING', 'Q1_PRIORITY', 'Dispatched to CPU Core 0 for 2s peer transfer', NOW() - INTERVAL 10 MINUTE + INTERVAL 1 SECOND),
('TXN-1002', 'RUNNING', 'TERMINATED', 'Q1_PRIORITY', 'Burst finished - MySQL ACID balance transfer committed', NOW() - INTERVAL 9 MINUTE),

('TXN-1003', 'NEW', 'READY', 'Q2_SJF', 'Admitted to Queue 2 (SJF) for lightweight standard transfer', NOW() - INTERVAL 6 MINUTE),
('TXN-1003', 'READY', 'RUNNING', 'Q2_SJF', 'Dispatched to CPU Core 0 (Shortest remaining burst = 3s)', NOW() - INTERVAL 6 MINUTE + INTERVAL 1 SECOND),
('TXN-1003', 'RUNNING', 'TERMINATED', 'Q2_SJF', 'Burst finished - MySQL ACID balance transfer committed', NOW() - INTERVAL 5 MINUTE),

('TXN-1004', 'NEW', 'READY', 'Q3_FCFS', 'Admitted to Queue 3 (FCFS) for heavy corporate payroll batch', NOW() - INTERVAL 2 MINUTE),
('TXN-1004', 'READY', 'RUNNING', 'Q3_FCFS', 'Dispatched to CPU Core 0 for 7s batch verification', NOW() - INTERVAL 2 MINUTE + INTERVAL 2 SECOND),
('TXN-1004', 'RUNNING', 'TERMINATED', 'Q3_FCFS', 'Batch burst finished - MySQL ACID bulk balance credit committed', NOW() - INTERVAL 1 MINUTE);
