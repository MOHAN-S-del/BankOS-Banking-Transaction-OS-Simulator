# Banking Transaction OS Simulator

A concurrent banking transaction system designed to demonstrate Operating System and DBMS concepts using a realistic digital banking scenario. The project simulates multiple users performing banking operations concurrently while demonstrating how an operating system can manage threads, CPU scheduling, synchronization, and safe transaction execution.

---

## 🎯 Project Objective

The goal of this project is to connect theoretical **OS and DBMS concepts** with a practical digital banking system.

The system focuses on four major concepts:

### 1. Thread Handling

Each banking operation is represented as a separate thread, allowing multiple users to perform transactions concurrently.

### 2. Round Robin CPU Scheduling

Transactions are managed using **Round Robin scheduling**, where each transaction receives a fixed time slice (time quantum) before the CPU moves to another transaction.

### 3. Synchronization using Locks

Locks are used to prevent multiple threads from modifying the same bank account simultaneously, avoiding race conditions and maintaining data consistency.

### 4. Transaction Atomicity

A money transfer is treated as an **atomic transaction**. Either the complete transaction succeeds, or all changes are rolled back so that the system never remains in an inconsistent state.

---

Instead of treating a money transfer as a simple function call, the project models it as a **concurrent transaction that must be scheduled, synchronized, and completed safely**.
