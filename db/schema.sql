-- ============================================================================
-- BankOS: Banking Transaction OS Simulator
-- Persistent Storage Schema for Accounts and Transactions (MySQL)
-- ============================================================================

CREATE DATABASE IF NOT EXISTS bank_db;
USE bank_db;

-- Table 1: Accounts
CREATE TABLE IF NOT EXISTS Accounts (
    AccountID INT PRIMARY KEY,
    Balance DECIMAL(15, 2) NOT NULL DEFAULT 0.00,
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UpdatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT chk_balance CHECK (Balance >= 0.00)
) ENGINE=InnoDB;

-- Table 2: Transactions
CREATE TABLE IF NOT EXISTS Transactions (
    TxID INT PRIMARY KEY,
    FromAccount INT NOT NULL,
    ToAccount INT NOT NULL,
    Amount DECIMAL(15, 2) NOT NULL,
    Status VARCHAR(32) NOT NULL,
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_from_account (FromAccount),
    INDEX idx_to_account (ToAccount),
    INDEX idx_status (Status),
    FOREIGN KEY (FromAccount) REFERENCES Accounts(AccountID) ON DELETE RESTRICT,
    FOREIGN KEY (ToAccount) REFERENCES Accounts(AccountID) ON DELETE RESTRICT
) ENGINE=InnoDB;

-- Seed Data
INSERT INTO Accounts (AccountID, Balance) VALUES
(1001, 50000.00),
(1002, 25000.00),
(1003, 1000.00),
(1004, 75000.00),
(1005, 300.00),
(1006, 12000.00),
(1007, 45000.00),
(1008, 100.00),
(2001, 5000000.00),
(2002, 2500000.00),
(5001, 500000.00),
(5002, 500000.00)
ON DUPLICATE KEY UPDATE Balance = VALUES(Balance);
