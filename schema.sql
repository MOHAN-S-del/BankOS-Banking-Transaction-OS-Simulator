-- ============================================================================
-- BankOS: Banking Transaction OS Simulator
-- Relational Database Schema (MySQL 8.x)
-- Entities: Customers, Accounts, Transactions, AuditLogs
-- ============================================================================

CREATE DATABASE IF NOT EXISTS bank_db;
USE bank_db;

-- ----------------------------------------------------------------------------
-- Table 1: Customers
-- Stores client profile and identity records (KYC).
-- Supports 1:N relationship with Accounts (One customer can hold multiple accounts).
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Customers (
    CustomerID INT PRIMARY KEY AUTO_INCREMENT,
    FullName VARCHAR(100) NOT NULL,
    Email VARCHAR(100) UNIQUE NOT NULL,
    CustomerType ENUM('RETAIL', 'CORPORATE', 'SYSTEM_RESERVE') DEFAULT 'RETAIL',
    Status ENUM('ACTIVE', 'SUSPENDED', 'BLOCKED') DEFAULT 'ACTIVE',
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Table 2: Accounts
-- Stores financial balances for retail, corporate, and reserve accounts.
-- Enforces integrity with CHECK (Balance >= 0.00).
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Accounts (
    AccountID INT PRIMARY KEY,
    CustomerID INT NULL,
    Balance DECIMAL(15, 2) NOT NULL DEFAULT 0.00,
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UpdatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT chk_balance CHECK (Balance >= 0.00),
    FOREIGN KEY (CustomerID) REFERENCES Customers(CustomerID) ON DELETE SET NULL
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Table 3: Transactions
-- Immutable ledger recording persistent transaction history and status.
-- Status values: 'COMMITTED', 'TERMINATED_FAILED', 'ROLLED_BACK'
-- ----------------------------------------------------------------------------
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

-- ----------------------------------------------------------------------------
-- Table 4: AuditLogs
-- Security and OS telemetry log capturing rate limit trips, deadlock checks,
-- and rollback events for regulatory compliance and diagnostics.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS AuditLogs (
    LogID INT PRIMARY KEY AUTO_INCREMENT,
    AccountID INT NULL,
    EventType VARCHAR(50) NOT NULL,    -- 'RATE_LIMIT_EXCEEDED', 'INSUFFICIENT_FUNDS', 'DEADLOCK_AVOIDED', 'SYSTEM_STARTUP'
    Severity ENUM('INFO', 'WARNING', 'CRITICAL') DEFAULT 'INFO',
    Description TEXT NOT NULL,
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_audit_account (AccountID),
    INDEX idx_event_type (EventType),
    FOREIGN KEY (AccountID) REFERENCES Accounts(AccountID) ON DELETE SET NULL
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Seed Data: Customers
-- ----------------------------------------------------------------------------
INSERT INTO Customers (CustomerID, FullName, Email, CustomerType, Status) VALUES
(1, 'Alice Johnson', 'alice.johnson@bankos.local', 'RETAIL', 'ACTIVE'),
(2, 'Bob Smith', 'bob.smith@bankos.local', 'RETAIL', 'ACTIVE'),
(3, 'Charlie Brown', 'charlie.brown@bankos.local', 'RETAIL', 'ACTIVE'),
(4, 'Diana Prince', 'diana.prince@bankos.local', 'RETAIL', 'ACTIVE'),
(5, 'Evan Wright', 'evan.wright@bankos.local', 'RETAIL', 'ACTIVE'),
(6, 'Fiona Gallagher', 'fiona.g@bankos.local', 'RETAIL', 'ACTIVE'),
(7, 'George Clark', 'george.clark@bankos.local', 'RETAIL', 'ACTIVE'),
(8, 'Hannah Abbott', 'hannah.abbott@bankos.local', 'RETAIL', 'ACTIVE'),
(9, 'Apex Corp Treasury', 'treasury@apexcorp.com', 'CORPORATE', 'ACTIVE'),
(10, 'Central ATM Reserve Ops', 'vault.ops@bankos.local', 'SYSTEM_RESERVE', 'ACTIVE')
ON DUPLICATE KEY UPDATE FullName = VALUES(FullName);

-- ----------------------------------------------------------------------------
-- Seed Data: Initial Account Balances (Linked to Customers)
-- ----------------------------------------------------------------------------
INSERT INTO Accounts (AccountID, CustomerID, Balance) VALUES
(1001, 1, 50000.00),
(1002, 2, 25000.00),
(1003, 3, 1000.00),
(1004, 4, 75000.00),
(1005, 5, 300.00),     -- Low balance account for insufficient funds rollback tests
(1006, 6, 12000.00),
(1007, 7, 45000.00),
(1008, 8, 100.00),     -- Low balance account for insufficient funds rollback tests
(2001, 9, 5000000.00), -- Corporate Treasury Account
(2002, 9, 2500000.00), -- Corporate Payroll Reserve (same corporate customer)
(5001, 10, 500000.00), -- ATM Cash Vault Reserve 1
(5002, 10, 500000.00)  -- ATM Cash Vault Reserve 2
ON DUPLICATE KEY UPDATE Balance = VALUES(Balance);

-- ----------------------------------------------------------------------------
-- Seed Data: Initial System & Security Audit Events
-- ----------------------------------------------------------------------------
INSERT INTO AuditLogs (AccountID, EventType, Severity, Description) VALUES
(NULL, 'SYSTEM_STARTUP', 'INFO', 'BankOS Transaction Processing Engine initialized with MySQL persistent storage.'),
(1005, 'INSUFFICIENT_FUNDS', 'WARNING', 'Transfer attempted with amount exceeding available balance ($300.00). Transaction rolled back under ACID guarantees.'),
(1001, 'RATE_LIMIT_EXCEEDED', 'WARNING', 'Incoming burst exceeded Token Bucket rate limit threshold (10 req/s). Request temporarily deferred.'),
(2001, 'DEADLOCK_AVOIDED', 'INFO', 'LockManager enforced strict ascending lock hierarchy on accounts 2001 and 2002, eliminating circular wait condition.');
