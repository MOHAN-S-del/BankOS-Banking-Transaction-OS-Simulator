-- ============================================================================
-- BankOS Middleware: Database Schema & Seed Data
-- Database: bankos_db
-- Relational Model: 5 Linked Normalized Tables with Foreign Keys & ACID Balances
-- ============================================================================

CREATE DATABASE IF NOT EXISTS bankos_db;
USE bankos_db;

-- ----------------------------------------------------------------------------
-- Table 1: accounts (Core Customer & Institutional Entities)
-- ----------------------------------------------------------------------------
DROP TABLE IF EXISTS upi_payroll_transactions;
DROP TABLE IF EXISTS atm_transactions;
DROP TABLE IF EXISTS transactions;
DROP TABLE IF EXISTS balance;
DROP TABLE IF EXISTS accounts;

CREATE TABLE accounts (
    account_id VARCHAR(32) PRIMARY KEY,
    customer_name VARCHAR(100) NOT NULL,
    account_type VARCHAR(50) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 2: balance (ACID Ledger Balances with Strict Verification)
-- ----------------------------------------------------------------------------
CREATE TABLE balance (
    balance_id INT AUTO_INCREMENT PRIMARY KEY,
    account_id VARCHAR(32) NOT NULL UNIQUE,
    current_balance DECIMAL(12,2) NOT NULL DEFAULT 0.00,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_balance_account FOREIGN KEY (account_id) REFERENCES accounts(account_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 3: transactions (Central Log of All OS-Scheduled Transfers)
-- ----------------------------------------------------------------------------
CREATE TABLE transactions (
    txn_id VARCHAR(64) PRIMARY KEY,
    sender_id VARCHAR(32) NOT NULL,
    receiver_id VARCHAR(32) NOT NULL,
    txn_category ENUM('ATM', 'UPI', 'PAYROLL', 'STANDARD') NOT NULL,
    amount DECIMAL(12,2) NOT NULL,
    burst_time INT NOT NULL,
    assigned_queue VARCHAR(20) NOT NULL,
    status ENUM('QUEUED', 'RUNNING', 'COMPLETED', 'FAILED') NOT NULL DEFAULT 'QUEUED',
    failure_reason VARCHAR(255) NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_txn_sender FOREIGN KEY (sender_id) REFERENCES accounts(account_id),
    CONSTRAINT fk_txn_receiver FOREIGN KEY (receiver_id) REFERENCES accounts(account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 4: atm_transactions (ATM Channel Metadata)
-- ----------------------------------------------------------------------------
CREATE TABLE atm_transactions (
    atm_id INT AUTO_INCREMENT PRIMARY KEY,
    txn_id VARCHAR(64) NOT NULL UNIQUE,
    atm_location VARCHAR(150) NOT NULL,
    card_last4 VARCHAR(4) NOT NULL,
    CONSTRAINT fk_atm_txn FOREIGN KEY (txn_id) REFERENCES transactions(txn_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 5: upi_payroll_transactions (UPI Virtual Handles & Payroll Batches)
-- ----------------------------------------------------------------------------
CREATE TABLE upi_payroll_transactions (
    channel_id INT AUTO_INCREMENT PRIMARY KEY,
    txn_id VARCHAR(64) NOT NULL UNIQUE,
    channel_mode ENUM('UPI', 'PAYROLL') NOT NULL,
    reference_handle VARCHAR(100) NOT NULL,
    CONSTRAINT fk_channel_txn FOREIGN KEY (txn_id) REFERENCES transactions(txn_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============================================================================
-- SEED DATA: 8 Verified Customers with ₹50,000+ Balances
-- ============================================================================
INSERT INTO accounts (account_id, customer_name, account_type) VALUES
('ACC1001', 'Rajesh Sharma', 'SAVINGS'),
('ACC1002', 'Priya Patel', 'SAVINGS'),
('ACC1003', 'Amit Verma', 'CURRENT'),
('ACC1004', 'Sneha Kulkarni', 'SAVINGS'),
('ACC1005', 'TechCorp Solutions Pvt Ltd', 'CORPORATE PAYROLL'),
('ACC1006', 'Metro ATM Cash Reserve', 'VAULT'),
('ACC1007', 'Vikram Malhotra', 'PREMIUM SAVINGS'),
('ACC1008', 'Ananya Iyer', 'SALARY');

INSERT INTO balance (account_id, current_balance) VALUES
('ACC1001', 150000.00),
('ACC1002', 85000.00),
('ACC1003', 240000.00),
('ACC1004', 95000.00),
('ACC1005', 1500000.00),
('ACC1006', 800000.00),
('ACC1007', 320000.00),
('ACC1008', 65000.00);

-- ============================================================================
-- INITIAL SEED TRANSACTIONS (Demonstrates Multi-Table SQL JOINs)
-- ============================================================================
INSERT INTO transactions (txn_id, sender_id, receiver_id, txn_category, amount, burst_time, assigned_queue, status, created_at) VALUES
('TXN-INIT-001', 'ACC1001', 'ACC1006', 'ATM', 2500.00, 1, 'Q1_PRIORITY', 'COMPLETED', NOW() - INTERVAL 25 MINUTE),
('TXN-INIT-002', 'ACC1002', 'ACC1004', 'UPI', 1850.00, 2, 'Q1_PRIORITY', 'COMPLETED', NOW() - INTERVAL 18 MINUTE),
('TXN-INIT-003', 'ACC1005', 'ACC1008', 'PAYROLL', 45000.00, 5, 'Q3_FCFS', 'COMPLETED', NOW() - INTERVAL 12 MINUTE),
('TXN-INIT-004', 'ACC1003', 'ACC1007', 'STANDARD', 12000.00, 3, 'Q2_SJF', 'COMPLETED', NOW() - INTERVAL 8 MINUTE),
('TXN-INIT-005', 'ACC1007', 'ACC1002', 'UPI', 3400.00, 1, 'Q1_PRIORITY', 'COMPLETED', NOW() - INTERVAL 3 MINUTE);

-- Channel Metadata Links
INSERT INTO atm_transactions (txn_id, atm_location, card_last4) VALUES
('TXN-INIT-001', 'Connaught Place South ATM #04, New Delhi', '4821');

INSERT INTO upi_payroll_transactions (txn_id, channel_mode, reference_handle) VALUES
('TXN-INIT-002', 'UPI', 'priya@okhdfcbank'),
('TXN-INIT-003', 'PAYROLL', 'BATCH-DEC-TECHCORP-01'),
('TXN-INIT-005', 'UPI', 'vikram@icici');
