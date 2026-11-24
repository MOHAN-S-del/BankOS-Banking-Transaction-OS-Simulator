-- ============================================================================
-- BankOS Middleware: MySQL Database & Normalized Schema
-- Database: bankos_db
-- 5 Normalized Relational Tables with Foreign Key Constraints & Seed Data
-- ============================================================================

CREATE DATABASE IF NOT EXISTS bankos_db;
USE bankos_db;

-- Drop in reverse dependency order for clean migrations
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
    account_type VARCHAR(20) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 2. balance: Atomic Balance Ledger
-- ----------------------------------------------------------------------------
CREATE TABLE balance (
    balance_id INT AUTO_INCREMENT PRIMARY KEY,
    account_id VARCHAR(10) NOT NULL UNIQUE,
    current_balance DECIMAL(12,2) NOT NULL DEFAULT 0.00,
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
    status ENUM('QUEUED', 'RUNNING', 'COMPLETED') NOT NULL DEFAULT 'QUEUED',
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
-- SEED INITIAL TRANSACTIONS (Enables Instant Multi-Table JOIN Demo)
-- ============================================================================
INSERT INTO transactions (txn_id, sender_id, receiver_id, txn_type, amount, auto_burst_time, assigned_queue, status, created_at) VALUES
('TXN-1001', 'ACC1001', 'ACC1006', 'ATM', 2500.00, 1, 'Queue 1 (Priority)', 'COMPLETED', NOW() - INTERVAL 15 MINUTE),
('TXN-1002', 'ACC1002', 'ACC1004', 'UPI', 1800.00, 2, 'Queue 1 (Priority)', 'COMPLETED', NOW() - INTERVAL 10 MINUTE),
('TXN-1003', 'ACC1003', 'ACC1001', 'STANDARD', 8500.00, 3, 'Queue 2 (SJF)', 'COMPLETED', NOW() - INTERVAL 6 MINUTE),
('TXN-1004', 'ACC1005', 'ACC1002', 'PAYROLL', 65000.00, 7, 'Queue 3 (FCFS)', 'COMPLETED', NOW() - INTERVAL 2 MINUTE);

INSERT INTO atm_transactions (txn_id, atm_location, card_last4) VALUES
('TXN-1001', 'Connaught Place South ATM #04', '4821');

INSERT INTO upi_payroll_transactions (txn_id, channel_mode, reference_id) VALUES
('TXN-1002', 'UPI', 'priya@okhdfcbank'),
('TXN-1004', 'PAYROLL', 'BATCH-DEC-TECHCORP-01');
