# ============================================================================
# BankOS Middleware: 3-Level Multi-Level Feedback Queue (MLFQ) Engine
# Module: scheduler.py
# Models Transactions as OS Processes (PCBs) with Priority, SJF & FCFS Queues
# ============================================================================

import time
import uuid
import random
import threading
from collections import deque
from datetime import datetime

import db

class PCB:
    """
    Process Control Block (PCB) representing an active banking transaction.
    """
    def __init__(self, txn_id, sender_id, receiver_id, txn_category, amount, burst_time, 
                 is_test_txn=False, channel_meta=None):
        self.txn_id = txn_id
        self.sender_id = sender_id
        self.receiver_id = receiver_id
        self.txn_category = txn_category.upper()  # 'ATM', 'UPI', 'PAYROLL', 'STANDARD'
        self.amount = float(amount)
        self.burst_time = int(burst_time)
        self.remaining_burst = int(burst_time)
        self.is_test_txn = bool(is_test_txn)
        self.channel_meta = channel_meta or {}
        
        self.arrival_time = time.time()
        self.assigned_queue = self.determine_queue()
        self.status = "QUEUED"  # 'QUEUED', 'RUNNING', 'COMPLETED', 'FAILED'
        self.failure_reason = ""
        self.start_time = None
        self.finish_time = None
        self.wait_time = 0.0
        self.created_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    def determine_queue(self):
        """
        3-Level MLFQ Routing Logic:
        - Queue 1 (High Priority): ATM & UPI transactions (urgent micro-bursts).
        - Queue 2 (Medium Priority): Small STANDARD transfers (burst_time <= 3, SJF).
        - Queue 3 (Low Priority): PAYROLL batches & heavy transfers (burst_time > 3, FCFS).
        """
        cat = self.txn_category
        if cat in ('ATM', 'UPI'):
            return "Q1_PRIORITY"
        elif cat == 'STANDARD' and self.burst_time <= 3:
            return "Q2_SJF"
        else:
            return "Q3_FCFS"

    def to_dict(self):
        return {
            "txn_id": self.txn_id,
            "sender_id": self.sender_id,
            "receiver_id": self.receiver_id,
            "txn_category": self.txn_category,
            "amount": self.amount,
            "burst_time": self.burst_time,
            "remaining_burst": self.remaining_burst,
            "assigned_queue": self.assigned_queue,
            "status": self.status,
            "failure_reason": self.failure_reason,
            "is_test_txn": self.is_test_txn,
            "channel_meta": self.channel_meta,
            "wait_time": round(self.wait_time, 2),
            "created_at": self.created_at
        }


class MLFQScheduler:
    """
    Thread-Safe Multi-Level Feedback Queue Scheduler with CPU Worker & Background Traffic
    """
    def __init__(self):
        self.lock = threading.RLock()
        
        # 3 MLFQ Levels
        self.queue_1 = deque()  # High Priority (ATM & UPI)
        self.queue_2 = []       # Medium Priority SJF (Sorted list)
        self.queue_3 = deque()  # Low Priority FCFS (PAYROLL & Heavy Standard)

        # Worker CPU State
        self.active_process = None
        self.active_progress_pct = 0
        self.active_time_elapsed = 0.0

        # Dedicated Tracking for the Presenter's Manual Test Transaction
        self.tracked_test_txn = None

        # Telemetry & Metrics
        self.total_submitted = 0
        self.total_completed = 0
        self.total_failed = 0
        self.total_wait_time_q1 = 0.0
        self.q1_completed_count = 0
        self.total_wait_time_q2 = 0.0
        self.q2_completed_count = 0
        self.total_wait_time_q3 = 0.0
        self.q3_completed_count = 0

        # Background Simulation Traffic State
        self.is_traffic_enabled = True
        self.traffic_interval = 3.5  # seconds between background injections
        self.cpu_tick_rate = 0.6     # seconds per burst second (smooth demo visibility)

        # Background threads
        self.worker_thread = threading.Thread(target=self._cpu_worker_loop, daemon=True)
        self.traffic_thread = threading.Thread(target=self._background_traffic_loop, daemon=True)

    def start(self):
        """Starts the scheduler CPU worker and background traffic thread."""
        self.worker_thread.start()
        self.traffic_thread.start()
        print("[scheduler.py] ✓ MLFQ Scheduler CPU Worker & Traffic Generator Online!")

    def inject_transaction(self, sender_id, receiver_id, txn_category, amount, burst_time, 
                           is_test_txn=False, channel_meta=None):
        """
        Admit a new banking transaction into the MLFQ.
        """
        with self.lock:
            # Generate Unique Transaction ID
            prefix = "TXN-TEST" if is_test_txn else "TXN-AUTO"
            random_num = random.randint(1000, 9999)
            txn_id = f"{prefix}-{random_num}"

            pcb = PCB(
                txn_id=txn_id,
                sender_id=sender_id,
                receiver_id=receiver_id,
                txn_category=txn_category,
                amount=amount,
                burst_time=burst_time,
                is_test_txn=is_test_txn,
                channel_meta=channel_meta
            )

            # Store in Database as QUEUED
            db.record_incoming_transaction(pcb)

            # Route to appropriate queue
            if pcb.assigned_queue == "Q1_PRIORITY":
                self.queue_1.append(pcb)
            elif pcb.assigned_queue == "Q2_SJF":
                self.queue_2.append(pcb)
                # Sort Q2 ascending by burst_time (Shortest Job First)
                self.queue_2.sort(key=lambda p: (p.burst_time, p.arrival_time))
            else:
                self.queue_3.append(pcb)

            self.total_submitted += 1

            # If it's a demo test transaction, track it immediately
            if is_test_txn:
                self.tracked_test_txn = {
                    "txn_id": pcb.txn_id,
                    "sender_id": pcb.sender_id,
                    "receiver_id": pcb.receiver_id,
                    "txn_category": pcb.txn_category,
                    "amount": pcb.amount,
                    "burst_time": pcb.burst_time,
                    "assigned_queue": pcb.assigned_queue,
                    "phase": f"Queued in {pcb.assigned_queue}",
                    "status": "QUEUED",
                    "progress": 0,
                    "failure_reason": ""
                }

            return pcb

    def _select_next_process(self):
        """
        MLFQ Priority Dispatcher:
        1. Queue 1 (Priority: ATM/UPI) has absolute precedence.
        2. Queue 2 (SJF: Small jobs) executes if Q1 is empty.
        3. Queue 3 (FCFS: Heavy Payroll/Large) executes only if Q1 & Q2 are empty.
        """
        with self.lock:
            # Check Queue 1
            if self.queue_1:
                return self.queue_1.popleft()
            
            # Check Queue 2 (Shortest Job First)
            if self.queue_2:
                # Top element is shortest job because list is kept sorted
                return self.queue_2.pop(0)

            # Check Queue 3 (First-Come, First-Served)
            if self.queue_3:
                return self.queue_3.popleft()

            return None

    def _cpu_worker_loop(self):
        """
        Simulated CPU Core 0 Worker executing transactions.
        """
        while True:
            pcb = self._select_next_process()
            
            if not pcb:
                time.sleep(0.15)
                continue

            with self.lock:
                pcb.status = "RUNNING"
                pcb.start_time = time.time()
                pcb.wait_time = pcb.start_time - pcb.arrival_time
                self.active_process = pcb
                self.active_progress_pct = 0
                self.active_time_elapsed = 0.0

                if pcb.is_test_txn and self.tracked_test_txn and self.tracked_test_txn["txn_id"] == pcb.txn_id:
                    self.tracked_test_txn["phase"] = "Executing on CPU Core 0"
                    self.tracked_test_txn["status"] = "RUNNING"

            # Update database status to RUNNING
            db.update_txn_status(pcb.txn_id, "RUNNING")

            # Simulate CPU Burst Execution in granular steps for smooth visual UI updates
            total_duration = max(0.8, pcb.burst_time * self.cpu_tick_rate)
            steps = 20
            step_duration = total_duration / steps

            for s in range(1, steps + 1):
                time.sleep(step_duration)
                with self.lock:
                    self.active_progress_pct = int((s / steps) * 100)
                    self.active_time_elapsed = round(s * step_duration, 1)
                    pcb.remaining_burst = max(0, pcb.burst_time - int((s / steps) * pcb.burst_time))
                    
                    if pcb.is_test_txn and self.tracked_test_txn and self.tracked_test_txn["txn_id"] == pcb.txn_id:
                        self.tracked_test_txn["progress"] = self.active_progress_pct

            # Execution Finished: Trigger ACID Commit/Rollback in Database
            success = db.execute_transaction_transfer(pcb)

            with self.lock:
                pcb.finish_time = time.time()
                if success:
                    self.total_completed += 1
                    # Record Queue Wait Metrics
                    if pcb.assigned_queue == "Q1_PRIORITY":
                        self.total_wait_time_q1 += pcb.wait_time
                        self.q1_completed_count += 1
                    elif pcb.assigned_queue == "Q2_SJF":
                        self.total_wait_time_q2 += pcb.wait_time
                        self.q2_completed_count += 1
                    else:
                        self.total_wait_time_q3 += pcb.wait_time
                        self.q3_completed_count += 1
                else:
                    self.total_failed += 1

                # Update Test Tracker Banner
                if pcb.is_test_txn and self.tracked_test_txn and self.tracked_test_txn["txn_id"] == pcb.txn_id:
                    self.tracked_test_txn["status"] = pcb.status
                    self.tracked_test_txn["progress"] = 100
                    if success:
                        self.tracked_test_txn["phase"] = "Committed to MySQL (ACID Passed)"
                    else:
                        self.tracked_test_txn["phase"] = "Rolled Back (ACID Overdraft Failed)"
                    self.tracked_test_txn["failure_reason"] = pcb.failure_reason

                self.active_process = None
                self.active_progress_pct = 0

            # Brief pause between process dispatches
            time.sleep(0.2)

    def _background_traffic_loop(self):
        """
        Generates realistic background ATM, UPI, and Payroll banking transactions
        so the MLFQ queues stay dynamic and alive during live presentations.
        """
        time.sleep(1.5)  # Initial stabilization
        accounts = ['ACC1001', 'ACC1002', 'ACC1003', 'ACC1004', 'ACC1005', 'ACC1006', 'ACC1007', 'ACC1008']

        while True:
            if not self.is_traffic_enabled:
                time.sleep(1.0)
                continue

            # Don't overload the queues if more than 15 transactions are already waiting
            with self.lock:
                total_queued = len(self.queue_1) + len(self.queue_2) + len(self.queue_3)
            
            if total_queued < 12:
                # Randomize traffic category: 45% UPI, 25% ATM, 20% Standard, 10% Payroll
                roll = random.random()
                
                if roll < 0.45:
                    # UPI Transfer (Queue 1: Priority)
                    sender = random.choice(['ACC1001', 'ACC1002', 'ACC1003', 'ACC1004', 'ACC1007'])
                    receiver = random.choice([a for a in accounts if a != sender and a != 'ACC1006'])
                    amt = round(random.uniform(200.0, 4500.0), 2)
                    burst = random.randint(1, 2)
                    meta = {'reference_handle': f"{sender.lower()}@upi"}
                    self.inject_transaction(sender, receiver, 'UPI', amt, burst, False, meta)

                elif roll < 0.70:
                    # ATM Cash Withdrawal (Queue 1: Priority)
                    sender = random.choice(['ACC1001', 'ACC1002', 'ACC1003', 'ACC1004', 'ACC1007', 'ACC1008'])
                    receiver = 'ACC1006'  # ATM Vault
                    amt = round(random.choice([500.0, 1000.0, 2000.0, 5000.0]), 2)
                    burst = random.randint(1, 2)
                    loc = random.choice([
                        'Connaught Place South ATM #04, New Delhi',
                        'Bandra Kurla Complex Kiosk, Mumbai',
                        'Indiranagar 100ft Rd Branch ATM, Bengaluru',
                        'Hitech City Cyber Gateway ATM, Hyderabad'
                    ])
                    meta = {'atm_location': loc, 'card_last4': str(random.randint(1000, 9999))}
                    self.inject_transaction(sender, receiver, 'ATM', amt, burst, False, meta)

                elif roll < 0.90:
                    # Small Standard Transfer (Queue 2: SJF)
                    sender = random.choice(['ACC1001', 'ACC1003', 'ACC1004', 'ACC1007'])
                    receiver = random.choice([a for a in accounts if a != sender and a != 'ACC1006'])
                    amt = round(random.uniform(5000.0, 25000.0), 2)
                    burst = random.randint(2, 3)
                    self.inject_transaction(sender, receiver, 'STANDARD', amt, burst, False, {})

                else:
                    # Company Payroll Batch (Queue 3: FCFS)
                    sender = 'ACC1005'  # TechCorp Solutions Pvt Ltd
                    receiver = random.choice(['ACC1001', 'ACC1002', 'ACC1004', 'ACC1008'])
                    amt = round(random.uniform(35000.0, 85000.0), 2)
                    burst = random.randint(4, 6)
                    meta = {'reference_handle': f"BATCH-{datetime.now().strftime('%b').upper()}-SALARY"}
                    self.inject_transaction(sender, receiver, 'PAYROLL', amt, burst, False, meta)

            # Sleep between traffic generation intervals
            jitter = random.uniform(0.8, 1.4)
            time.sleep(self.traffic_interval * jitter)

    def toggle_traffic(self, enabled=None):
        """Toggles background traffic generation."""
        with self.lock:
            if enabled is not None:
                self.is_traffic_enabled = bool(enabled)
            else:
                self.is_traffic_enabled = not self.is_traffic_enabled
            return self.is_traffic_enabled

    def get_state(self):
        """Returns snapshot of current scheduler, queues, and test transaction."""
        with self.lock:
            q1_list = [p.to_dict() for p in self.queue_1]
            q2_list = [p.to_dict() for p in self.queue_2]
            q3_list = [p.to_dict() for p in self.queue_3]

            avg_w_q1 = round(self.total_wait_time_q1 / self.q1_completed_count, 2) if self.q1_completed_count > 0 else 0.0
            avg_w_q2 = round(self.total_wait_time_q2 / self.q2_completed_count, 2) if self.q2_completed_count > 0 else 0.0
            avg_w_q3 = round(self.total_wait_time_q3 / self.q3_completed_count, 2) if self.q3_completed_count > 0 else 0.0

            active_dict = None
            if self.active_process:
                active_dict = self.active_process.to_dict()
                active_dict["progress_pct"] = self.active_progress_pct
                active_dict["time_elapsed"] = self.active_time_elapsed

            return {
                "active_process": active_dict,
                "queue_1": q1_list,
                "queue_2": q2_list,
                "queue_3": q3_list,
                "tracked_test_txn": self.tracked_test_txn,
                "traffic_enabled": self.is_traffic_enabled,
                "is_mysql_connected": db.is_mysql_connected,
                "metrics": {
                    "total_submitted": self.total_submitted,
                    "total_completed": self.total_completed,
                    "total_failed": self.total_failed,
                    "q1_completed": self.q1_completed_count,
                    "q2_completed": self.q2_completed_count,
                    "q3_completed": self.q3_completed_count,
                    "avg_wait_q1": avg_w_q1,
                    "avg_wait_q2": avg_w_q2,
                    "avg_wait_q3": avg_w_q3
                }
            }

    def reset(self):
        """Clears all queues and resets metrics."""
        with self.lock:
            self.queue_1.clear()
            self.queue_2.clear()
            self.queue_3.clear()
            self.active_process = None
            self.tracked_test_txn = None
            self.total_submitted = 0
            self.total_completed = 0
            self.total_failed = 0
            self.total_wait_time_q1 = 0.0
            self.q1_completed_count = 0
            self.total_wait_time_q2 = 0.0
            self.q2_completed_count = 0
            self.total_wait_time_q3 = 0.0
            self.q3_completed_count = 0
        db.reset_database()


# Singleton global scheduler instance
scheduler = MLFQScheduler()
