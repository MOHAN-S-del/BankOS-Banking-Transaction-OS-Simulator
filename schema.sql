-- ============================================================================
-- BankOS: Banking Transaction OS Simulator
-- Relational Database Schema (MySQL 8.x / InnoDB)
-- Entities: 
--   1. Customers (Identity, KYC, Customer Tier)
--   2. BankBranches (Financial Routing, IFSC, Branch Hub)
--   3. Accounts (Ledger Balances, 2PL Exclusive Locking, Account Type)
--   4. Transactions (Sender & Receiver Ledger, ACID Commits & Rollbacks)
--   5. AuditLogs (Security Telemetry, Deadlock Avoidance, Regulatory Trail)
-- ============================================================================

CREATE DATABASE IF NOT EXISTS bank_db;
USE bank_db;

-- ----------------------------------------------------------------------------
-- Table 1: Customers
-- Stores client profiles and identity records (KYC).
-- Supports 1:N relationship with Accounts.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Customers (
    CustomerID INT PRIMARY KEY AUTO_INCREMENT,
    FullName VARCHAR(100) NOT NULL,
    Email VARCHAR(100) UNIQUE NOT NULL,
    Phone VARCHAR(20) DEFAULT '+1-555-0100',
    CustomerType ENUM('RETAIL', 'CORPORATE', 'SYSTEM_RESERVE') DEFAULT 'RETAIL',
    Status ENUM('ACTIVE', 'SUSPENDED', 'BLOCKED') DEFAULT 'ACTIVE',
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Table 2: BankBranches
-- Stores banking branch routing codes, IFSC, and territorial hubs.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS BankBranches (
    BranchID INT PRIMARY KEY,
    BranchName VARCHAR(100) NOT NULL,
    IFSCCode VARCHAR(20) UNIQUE NOT NULL,
    City VARCHAR(50) NOT NULL,
    ReserveBalance DECIMAL(15, 2) NOT NULL DEFAULT 10000000.00
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Table 3: Accounts
-- Stores financial balances for retail, corporate, and reserve accounts.
-- Enforces integrity with CHECK (Balance >= 0.00) and links to Customer & Branch.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Accounts (
    AccountID INT PRIMARY KEY,
    CustomerID INT NULL,
    BranchID INT NULL,
    AccountType ENUM('SAVINGS', 'CHECKING', 'CORPORATE_TREASURY', 'ATM_VAULT') DEFAULT 'SAVINGS',
    Balance DECIMAL(15, 2) NOT NULL DEFAULT 0.00,
    Status ENUM('ACTIVE', 'FROZEN', 'DORMANT') DEFAULT 'ACTIVE',
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UpdatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT chk_balance CHECK (Balance >= 0.00),
    FOREIGN KEY (CustomerID) REFERENCES Customers(CustomerID) ON DELETE SET NULL,
    FOREIGN KEY (BranchID) REFERENCES BankBranches(BranchID) ON DELETE SET NULL
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Table 4: Transactions
-- Immutable ledger recording persistent transaction history and status.
-- Sender (FromAccount) and Receiver (ToAccount) reference Accounts(AccountID).
-- Status values: 'COMMITTED', 'TERMINATED_FAILED', 'ROLLED_BACK'
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS Transactions (
    TxID INT PRIMARY KEY,
    FromAccount INT NOT NULL,
    ToAccount INT NOT NULL,
    Amount DECIMAL(15, 2) NOT NULL,
    TransactionType ENUM('UPI_TRANSFER', 'ATM_WITHDRAWAL', 'SALARY_BATCH', 'REVERSAL') DEFAULT 'UPI_TRANSFER',
    Status VARCHAR(32) NOT NULL,
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_from_account (FromAccount),
    INDEX idx_to_account (ToAccount),
    INDEX idx_status (Status),
    FOREIGN KEY (FromAccount) REFERENCES Accounts(AccountID) ON DELETE RESTRICT,
    FOREIGN KEY (ToAccount) REFERENCES Accounts(AccountID) ON DELETE RESTRICT
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Table 5: AuditLogs
-- Security and OS telemetry log capturing rate limit trips, deadlock checks,
-- and rollback events for regulatory compliance and diagnostics.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS AuditLogs (
    LogID INT PRIMARY KEY AUTO_INCREMENT,
    TxID INT NULL,
    AccountID INT NULL,
    EventType VARCHAR(50) NOT NULL,    -- 'RATE_LIMIT_EXCEEDED', 'INSUFFICIENT_FUNDS', 'DEADLOCK_AVOIDED', 'SYSTEM_STARTUP', 'EXPORT_AUDIT_SAVED'
    Severity ENUM('INFO', 'WARNING', 'CRITICAL') DEFAULT 'INFO',
    Description TEXT NOT NULL,
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_audit_account (AccountID),
    INDEX idx_event_type (EventType),
    FOREIGN KEY (AccountID) REFERENCES Accounts(AccountID) ON DELETE SET NULL
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Table 6: AuditExports
-- Stores persistent audit batches generated when "EXPORT AUDIT" is clicked.
-- Contains aggregate metrics, timestamp, and audit trail metadata.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS AuditExports (
    ExportID INT PRIMARY KEY AUTO_INCREMENT,
    ExportTime TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    TotalTransitions INT NOT NULL,
    TotalCompleted INT NOT NULL,
    TotalCommits INT NOT NULL,
    TotalRollbacks INT NOT NULL,
    DeadlocksPrevented INT NOT NULL,
    Status VARCHAR(32) DEFAULT 'SAVED_TO_DB',
    SummaryText TEXT NOT NULL
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Table 7: StateTransitionAudits
-- High-resolution state transition log linked to an AuditExport batch.
-- Stores OldState, NewState, Tick, PID, and scheduling decisions.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS StateTransitionAudits (
    AuditID INT PRIMARY KEY AUTO_INCREMENT,
    ExportID INT NOT NULL,
    PID INT NOT NULL,
    TransactionType VARCHAR(32) NOT NULL,
    OldState VARCHAR(32) NOT NULL,
    NewState VARCHAR(32) NOT NULL,
    Tick INT NOT NULL,
    Reason TEXT,
    CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_export (ExportID),
    INDEX idx_pid (PID),
    FOREIGN KEY (ExportID) REFERENCES AuditExports(ExportID) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- Seed Data: BankBranches
-- ----------------------------------------------------------------------------
INSERT INTO BankBranches (BranchID, BranchName, IFSCCode, City, ReserveBalance) VALUES
(1, 'Downtown Financial Center', 'BNKOS0001NYC', 'New York', 25000000.00),
(2, 'Tech Park Corporate Hub', 'BNKOS0002SFO', 'San Francisco', 50000000.00),
(3, 'Metro Cash & Vault Distribution', 'BNKOS0003CHI', 'Chicago', 15000000.00)
ON DUPLICATE KEY UPDATE BranchName = VALUES(BranchName);

-- ----------------------------------------------------------------------------
-- Seed Data: Customers (Senders & Receivers)
-- ----------------------------------------------------------------------------
INSERT INTO Customers (CustomerID, FullName, Email, Phone, CustomerType, Status) VALUES
(1, 'Alice Johnson', 'alice.johnson@bankos.local', '+1-555-0101', 'RETAIL', 'ACTIVE'),
(2, 'Bob Smith', 'bob.smith@bankos.local', '+1-555-0102', 'RETAIL', 'ACTIVE'),
(3, 'Charlie Brown', 'charlie.brown@bankos.local', '+1-555-0103', 'RETAIL', 'ACTIVE'),
(4, 'Diana Prince', 'diana.prince@bankos.local', '+1-555-0104', 'RETAIL', 'ACTIVE'),
(5, 'Evan Wright', 'evan.wright@bankos.local', '+1-555-0105', 'RETAIL', 'ACTIVE'),
(6, 'Fiona Gallagher', 'fiona.g@bankos.local', '+1-555-0106', 'RETAIL', 'ACTIVE'),
(7, 'George Clark', 'george.clark@bankos.local', '+1-555-0107', 'RETAIL', 'ACTIVE'),
(8, 'Hannah Abbott', 'hannah.abbott@bankos.local', '+1-555-0108', 'RETAIL', 'ACTIVE'),
(9, 'Apex Corp Global Treasury', 'treasury@apexcorp.com', '+1-555-0900', 'CORPORATE', 'ACTIVE'),
(10, 'Central ATM Reserve Ops', 'vault.ops@bankos.local', '+1-555-0000', 'SYSTEM_RESERVE', 'ACTIVE')
ON DUPLICATE KEY UPDATE FullName = VALUES(FullName);

-- ----------------------------------------------------------------------------
-- Seed Data: Initial Account Balances (Linked to Customers & Branches)
-- ----------------------------------------------------------------------------
INSERT INTO Accounts (AccountID, CustomerID, BranchID, AccountType, Balance, Status) VALUES
(1001, 1, 1, 'SAVINGS', 50000.00, 'ACTIVE'),
(1002, 2, 1, 'SAVINGS', 25000.00, 'ACTIVE'),
(1003, 3, 1, 'SAVINGS', 1000.00, 'ACTIVE'),
(1004, 4, 1, 'CHECKING', 75000.00, 'ACTIVE'),
(1005, 5, 2, 'SAVINGS', 300.00, 'ACTIVE'),     -- Low balance account for insufficient funds rollback tests
(1006, 6, 2, 'CHECKING', 12000.00, 'ACTIVE'),
(1007, 7, 2, 'SAVINGS', 45000.00, 'ACTIVE'),
(1008, 8, 2, 'SAVINGS', 100.00, 'ACTIVE'),     -- Low balance account for insufficient funds rollback tests
(2001, 9, 2, 'CORPORATE_TREASURY', 5000000.00, 'ACTIVE'), -- Corporate Treasury Account
(2002, 9, 2, 'CORPORATE_TREASURY', 2500000.00, 'ACTIVE'), -- Corporate Payroll Reserve
(5001, 10, 3, 'ATM_VAULT', 500000.00, 'ACTIVE'), -- ATM Cash Vault Reserve 1
(5002, 10, 3, 'ATM_VAULT', 500000.00, 'ACTIVE')  -- ATM Cash Vault Reserve 2
ON DUPLICATE KEY UPDATE Balance = VALUES(Balance);

-- ----------------------------------------------------------------------------
-- Seed Data: Initial System & Security Audit Events
-- ----------------------------------------------------------------------------
INSERT INTO AuditLogs (AccountID, EventType, Severity, Description) VALUES
(NULL, 'SYSTEM_STARTUP', 'INFO', 'BankOS Transaction Processing Engine initialized with MySQL persistent storage.'),
(1005, 'INSUFFICIENT_FUNDS', 'WARNING', 'Transfer attempted with amount exceeding available balance ($300.00). Transaction rolled back under ACID guarantees.'),
(1001, 'RATE_LIMIT_EXCEEDED', 'WARNING', 'Incoming burst exceeded Token Bucket rate limit threshold (10 req/s). Request temporarily deferred.'),
(2001, 'DEADLOCK_AVOIDED', 'INFO', 'LockManager enforced strict ascending lock hierarchy on accounts 2001 and 2002, eliminating circular wait condition.');

-- ----------------------------------------------------------------------------
-- Seed Data: AuditExports & StateTransitionAudits (Demo Baseline Data)
-- ----------------------------------------------------------------------------
INSERT INTO AuditExports (ExportID, ExportTime, TotalTransitions, TotalCompleted, TotalCommits, TotalRollbacks, DeadlocksPrevented, Status, SummaryText) VALUES
(1, NOW() - INTERVAL 15 MINUTE, 48, 14, 12, 2, 4, 'SAVED_TO_DB', 'Snapshot at Tick #150 | Completed: 14 | Commits: 12 | Rollbacks: 2 | Deadlocks Prevented: 4 | Total Transitions: 48'),
(2, NOW() - INTERVAL 3 MINUTE, 125, 38, 33, 5, 9, 'SAVED_TO_DB', 'Snapshot at Tick #480 | Completed: 38 | Commits: 33 | Rollbacks: 5 | Deadlocks Prevented: 9 | Total Transitions: 125')
ON DUPLICATE KEY UPDATE Status = VALUES(Status);

INSERT INTO StateTransitionAudits (AuditID, ExportID, PID, TransactionType, OldState, NewState, Tick, Reason) VALUES
(1, 1, 101, 'TRANSFER', 'NEW', 'READY', 1, 'Transaction queued into MLFQ Level 0 (Interactive)'),
(2, 1, 101, 'TRANSFER', 'READY', 'RUNNING', 2, 'Dispatched to CPU Core 0 under Preemptive Round Robin'),
(3, 1, 101, 'TRANSFER', 'RUNNING', 'BLOCKED', 5, 'Acquiring Strict 2PL exclusive lock on Account 1001'),
(4, 1, 101, 'TRANSFER', 'BLOCKED', 'RUNNING', 8, 'Lock granted following ascending account hierarchy (Havender Deadlock Denial)'),
(5, 1, 101, 'TRANSFER', 'RUNNING', 'COMMITTED', 12, 'ACID commit validated; balance updated and written to WAL'),
(6, 1, 102, 'TRANSFER', 'NEW', 'READY', 15, 'High-frequency burst queued into MLFQ Level 0'),
(7, 1, 102, 'TRANSFER', 'READY', 'RUNNING', 16, 'Dispatched to CPU Core 1'),
(8, 1, 102, 'TRANSFER', 'RUNNING', 'ABORTED', 19, 'ACID Rollback: Insufficient balance on Account 1005 ($300.00). Constraints verified.')
ON DUPLICATE KEY UPDATE Reason = VALUES(Reason);

-- ============================================================================
-- SQL JOIN QUERIES FOR FACULTY DEMONSTRATION
-- ============================================================================

-- ----------------------------------------------------------------------------
-- JOIN QUERY 1: Multi-Table INNER JOIN across Transactions, Accounts, and Customers
-- Shows: TxID, Amount, ACID Status, Sender Name & Account, Receiver Name & Account
-- ----------------------------------------------------------------------------
CREATE OR REPLACE VIEW View_Sender_Receiver_Transactions AS
SELECT 
    t.TxID,
    t.Amount,
    t.Status AS TransactionStatus,
    sender_cust.FullName AS SenderName,
    t.FromAccount AS SenderAccount,
    receiver_cust.FullName AS ReceiverName,
    t.ToAccount AS ReceiverAccount,
    branch.BranchName AS SenderBranch,
    t.CreatedAt AS ExecutionTimestamp
FROM Transactions t
INNER JOIN Accounts sender_acc ON t.FromAccount = sender_acc.AccountID
INNER JOIN Customers sender_cust ON sender_acc.CustomerID = sender_cust.CustomerID
LEFT JOIN BankBranches branch ON sender_acc.BranchID = branch.BranchID
INNER JOIN Accounts receiver_acc ON t.ToAccount = receiver_acc.AccountID
INNER JOIN Customers receiver_cust ON receiver_acc.CustomerID = receiver_cust.CustomerID
ORDER BY t.TxID DESC;

-- ----------------------------------------------------------------------------
-- JOIN QUERY 2: LEFT JOIN Customer Portfolio Aggregation
-- Aggregates customer total balance, accounts held, and KYC status
-- ----------------------------------------------------------------------------
CREATE OR REPLACE VIEW View_Customer_Portfolios AS
SELECT 
    c.CustomerID,
    c.FullName,
    c.CustomerType,
    COUNT(a.AccountID) AS TotalAccounts,
    COALESCE(SUM(a.Balance), 0.00) AS TotalNetWorth,
    c.Status AS AccountStatus
FROM Customers c
LEFT JOIN Accounts a ON c.CustomerID = a.CustomerID
GROUP BY c.CustomerID, c.FullName, c.CustomerType, c.Status;

-- ----------------------------------------------------------------------------
-- JOIN QUERY 3: Audit Export Details Multi-Table JOIN
-- Relates Export ID with individual transition events and PIDs
-- ----------------------------------------------------------------------------
CREATE OR REPLACE VIEW View_Audit_Export_Transitions AS
SELECT 
    e.ExportID,
    e.ExportTime,
    s.AuditID,
    s.PID,
    s.TransactionType,
    s.OldState,
    s.NewState,
    s.Tick,
    s.Reason
FROM AuditExports e
INNER JOIN StateTransitionAudits s ON e.ExportID = s.ExportID
ORDER BY s.AuditID DESC;
