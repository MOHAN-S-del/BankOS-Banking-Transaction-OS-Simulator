# Banking Transaction OS Simulator

A concurrent banking transaction system designed to demonstrate **Operating System and DBMS concepts** through a realistic digital banking environment.

The simulator models multiple users performing banking operations concurrently and demonstrates how threads are created and managed, how CPU time is allocated using **Round Robin scheduling**, how shared bank accounts are protected using **locks**, and how financial transactions are executed safely using **atomicity**.

---

## 🎯 Project Objective

The primary objective of this project is to combine theoretical **Operating System (OS)** and **Database Management System (DBMS)** concepts into a practical digital banking transaction simulator.

The project is built around four core concepts:

- **Thread Handling** — Each banking operation is represented as a concurrent thread.
- **Round Robin Scheduling** — Transaction threads are managed using a Round Robin scheduling model with a fixed time quantum.
- **Synchronization using Locks** — Locks protect shared bank-account resources and prevent race conditions during concurrent transactions.
- **Transaction Atomicity** — A money transfer consists of both **debit and credit operations**. Both operations must succeed for the transaction to commit; if either operation fails, the complete transaction is rolled back.

Instead of treating a money transfer as a simple function call, the project models it as a complete concurrent transaction that must be **scheduled, synchronized, and executed atomically**.

### Core Workflow

```text
Banking Operation
       ↓
     Thread
       ↓
Round Robin Scheduler
       ↓
    Acquire Lock
       ↓
Execute Transaction
       ↓
 ┌─────┴─────┐
 ↓           ↓
Success    Failure
 ↓           ↓
Commit     Rollback
