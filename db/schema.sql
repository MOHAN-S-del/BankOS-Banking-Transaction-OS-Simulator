-- ========================================================================
-- Bank OS Simulator: Relational Database Schema (MySQL 8.x - InnoDB Engine)
-- ========================================================================

CREATE DATABASE IF NOT EXISTS bank_os_db
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE bank_os_db;

-- Disable foreign key checks during schema creation / reset
SET FOREIGN_KEY_CHECKS = 0;

DROP TABLE IF EXISTS lock_event_log;
DROP TABLE IF EXISTS scheduler_log;
DROP TABLE IF EXISTS process_pcb;
DROP TABLE IF EXISTS transaction;
DROP TABLE IF EXISTS account;
DROP TABLE IF EXISTS customer;

SET FOREIGN_KEY_CHECKS = 1;

-- 1. CUSTOMER TABLE
CREATE TABLE customer (
    customer_id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(100) UNIQUE NOT NULL,
    phone VARCHAR(20) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 2. ACCOUNT TABLE
CREATE TABLE account (
    account_id VARCHAR(20) PRIMARY KEY,     -- e.g. 'A101', 'A102', 'A103'
    customer_id INT NOT NULL,
    balance DECIMAL(15, 2) NOT NULL DEFAULT 0.00,
    account_type ENUM('SAVINGS', 'CURRENT') DEFAULT 'SAVINGS',
    status ENUM('ACTIVE', 'FROZEN', 'CLOSED') DEFAULT 'ACTIVE',
    version INT NOT NULL DEFAULT 0,         -- Optimistic concurrency tracking
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_account_customer FOREIGN KEY (customer_id) 
        REFERENCES customer(customer_id) ON DELETE CASCADE,
    CONSTRAINT chk_positive_balance CHECK (balance >= 0.00)
) ENGINE=InnoDB;

-- 3. BANK TRANSACTION TABLE
CREATE TABLE transaction (
    transaction_id VARCHAR(64) PRIMARY KEY,
    from_account VARCHAR(20) NULL,
    to_account VARCHAR(20) NULL,
    amount DECIMAL(15, 2) NOT NULL,
    transaction_type ENUM('DEPOSIT', 'WITHDRAW', 'TRANSFER') NOT NULL,
    status ENUM('PENDING', 'COMMITTED', 'ROLLED_BACK') NOT NULL DEFAULT 'PENDING',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_tx_from FOREIGN KEY (from_account) REFERENCES account(account_id),
    CONSTRAINT fk_tx_to FOREIGN KEY (to_account) REFERENCES account(account_id)
) ENGINE=InnoDB;

-- 4. OS PROCESS CONTROL BLOCK (PCB) AUDIT TABLE
CREATE TABLE process_pcb (
    process_id VARCHAR(20) PRIMARY KEY,    -- e.g. 'PID_T001'
    transaction_id VARCHAR(64) NOT NULL,
    state ENUM('NEW', 'READY', 'RUNNING', 'WAITING', 'TERMINATED') NOT NULL,
    arrival_time INT NOT NULL,              -- Simulator clock tick
    burst_time INT NOT NULL,                -- Total CPU ticks required
    remaining_burst INT NOT NULL,           -- Remaining CPU ticks
    completion_time INT NULL,               -- Simulator tick at termination
    waiting_time INT NOT NULL DEFAULT 0,    -- Total ticks spent in READY state
    turnaround_time INT NOT NULL DEFAULT 0, -- completion_time - arrival_time
    response_time INT NOT NULL DEFAULT -1,  -- first_run_time - arrival_time
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_pcb_tx FOREIGN KEY (transaction_id) REFERENCES transaction(transaction_id)
) ENGINE=InnoDB;

-- 5. SCHEDULER EVENT LOG TABLE
CREATE TABLE scheduler_log (
    log_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    process_id VARCHAR(20) NOT NULL,
    event_type ENUM('ENQUEUE', 'DISPATCH', 'PREEMPT', 'BLOCK', 'UNBLOCK', 'TERMINATE') NOT NULL,
    clock_tick INT NOT NULL,
    description VARCHAR(255) NOT NULL,
    logged_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 6. RESOURCE LOCK AUDIT TABLE
CREATE TABLE lock_event_log (
    event_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    process_id VARCHAR(20) NOT NULL,
    resource_id VARCHAR(20) NOT NULL,       -- Account ID locked
    action ENUM('REQUESTED', 'ACQUIRED', 'WAITING', 'RELEASED') NOT NULL,
    clock_tick INT NOT NULL,
    logged_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
