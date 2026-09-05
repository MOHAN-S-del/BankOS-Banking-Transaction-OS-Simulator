# 🏦 Bank Management System — DBMS + OS

A database-driven **Bank Management System** developed by combining concepts of **Database Management Systems (DBMS)** and **Operating Systems (OS)**. The system manages customers, accounts, and banking transactions while ensuring safe and consistent execution of concurrent transactions.

## 🚀 Project Overview

The project provides a centralized banking system where customers and authorized staff can perform and manage operations such as:

- 💰 Deposits
- 💸 Withdrawals
- 🔄 Fund Transfers
- 👤 Customer Management
- 🏦 Account Management
- 📜 Transaction History
- 🔐 Role-Based Access

The main focus of the project is to ensure that banking transactions are processed **safely, consistently, and without conflicts**, especially when multiple transactions occur at the same time.

## 🎯 Project Objective

To design and implement a banking system using **MySQL and Operating System concepts** that maintains data consistency and transaction safety during concurrent deposits, withdrawals, and transfers.

The system aims to prevent:

- Negative account balances
- Conflicting transactions
- Inconsistent database updates
- Unauthorized modifications
- Data loss during concurrent operations

## 🛠️ Technology Stack

| Technology | Purpose |
|------------|---------|
| MySQL | Database management |
| SQL | Queries and database operations |
| C++ | Application logic |
| Threads | Concurrent transaction processing |
| Locks | Prevent transaction conflicts |
| Message Queues | Communication between processes/threads |
| Round Robin | Transaction scheduling |
| Git & GitHub | Version control |

## 🧩 DBMS Concepts Used

- Relational Database
- ER Model
- Primary Keys
- Foreign Keys
- Constraints
- SQL Queries
- Transactions
- ACID Properties
- Normalization
- Joins
- Stored Procedures
- Database Integrity

## ⚙️ Operating System Concepts Used

### 🔹 Multithreading

Multiple banking transactions can be processed concurrently.

### 🔹 Locks

Locks are used to prevent two transactions from modifying the same account data incorrectly at the same time.

### 🔹 Message Queues

Message queues can be used to manage communication and transaction requests between concurrent components.

### 🔹 Round Robin Scheduling

Transactions are scheduled using Round Robin principles to provide fair processing among multiple transaction requests.

### 🔹 Synchronization

Synchronization mechanisms help maintain consistency when multiple threads access shared banking resources.

## 👥 User Roles

The system supports different levels of access:

### 👤 Customer

- View account details
- Check balance
- Deposit money
- Withdraw money
- Transfer funds
- View transaction history

### 👨‍💼 Staff

- Manage customer accounts
- Update authorized customer information
- Process banking operations
- View transaction records

### 👨‍💻 Admin

- View system information
- Manage users and roles
- Monitor accounts and transactions
- Access administrative reports

## 🔒 Transaction Safety

A major feature of this project is **safe concurrent transaction processing**.

For example, if two transactions attempt to withdraw money from the same account simultaneously, synchronization and locking mechanisms help ensure that:

```text
Available Balance = ₹10,000

Transaction A → Withdraw ₹7,000
Transaction B → Withdraw ₹5,000

Without synchronization:
Both may succeed incorrectly ❌

With proper transaction control:
Only valid transactions succeed ✅
Final balance remains consistent.
```

## 📊 System Architecture

```text
              ┌─────────────────────┐
              │        Users        │
              │ Customer / Staff /  │
              │        Admin        │
              └──────────┬──────────┘
                         │
                         ▼
              ┌─────────────────────┐
              │  Application Layer  │
              │                     │
              │ Banking Operations  │
              └──────────┬──────────┘
                         │
                         ▼
              ┌─────────────────────┐
              │    OS Mechanisms    │
              │                     │
              │ Threads             │
              │ Locks               │
              │ Scheduling          │
              │ Message Queues      │
              └──────────┬──────────┘
                         │
                         ▼
              ┌─────────────────────┐
              │      MySQL DB       │
              │                     │
              │ Customers           │
              │ Accounts            │
              │ Transactions        │
              │ Users / Roles       │
              └─────────────────────┘
```

## 📁 Project Structure

```text
Bank-Management-System/
│
├── database/
│   ├── schema.sql
│   ├── tables.sql
│   └── sample_data.sql
│
├── application/
│   ├── main.cpp
│   ├── customer.cpp
│   ├── account.cpp
│   └── transaction.cpp
│
├── os/
│   ├── threading/
│   ├── locking/
│   ├── scheduling/
│   └── message_queue/
│
├── diagrams/
│   ├── ER_Diagram.png
│   └── System_Architecture.png
│
├── docs/
│   └── project_report.pdf
│
└── README.md
```

## 🔄 Transaction Flow

```text
User Request
      │
      ▼
Validate Transaction
      │
      ▼
Acquire Lock
      │
      ▼
Process Transaction
      │
      ├────────── Invalid ──────────► Rollback
      │
      ▼
Update Database
      │
      ▼
Commit Transaction
      │
      ▼
Release Lock
      │
      ▼
Updated & Consistent Balance
```

## 🔐 Key Features

- Concurrent transaction processing
- Transaction synchronization
- Account balance validation
- Lock-based concurrency control
- Role-based access
- Database integrity
- Transaction commit and rollback
- OS-level scheduling concepts
- MySQL-based persistent storage

## 📈 Performance Analysis

The system can be evaluated using:

- CPU utilization
- Transaction waiting time
- Transaction completion time
- Number of concurrent transactions
- Lock/wait behavior
- Successful vs. failed transactions

## 🚀 Future Enhancements

- Online banking interface
- OTP-based authentication
- Transaction notifications
- Fraud detection
- Performance monitoring dashboard
- Deadlock detection and recovery
- Cloud deployment

## 👨‍💻 Project Type

**PBL Project — Database Management Systems + Operating Systems**

This project demonstrates how **DBMS transaction management** and **OS concurrency mechanisms** can work together to process banking operations safely while maintaining **data consistency, transaction integrity, and controlled access**.

## 📄 License

This project is developed for **academic/PBL purposes**.
