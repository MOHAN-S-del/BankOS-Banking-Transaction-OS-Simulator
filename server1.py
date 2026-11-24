# ============================================================================
# BankOS Middleware: OS-Driven Concurrent Banking Transaction Middleware
# File: server.py
# Connects C++ MLFQ OS Engine, MySQL (bankos_db), and Liquid Glass Dashboard
# Synchronizes 8 Relational & OS-Telemetry Tables in Real-Time
# ============================================================================

import os
import io
import csv
import time
import json
import random
import socket
import urllib.request
import urllib.parse
import subprocess
import threading
from decimal import Decimal
from datetime import datetime
from flask import Flask, render_template, request, jsonify, Response

# ----------------------------------------------------------------------------
# MySQL Database Configuration (Auto-reads .env or system environment)
# ----------------------------------------------------------------------------
env_path = os.path.join(os.path.dirname(__file__), ".env")
if os.path.exists(env_path):
    try:
        with open(env_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#') and '=' in line:
                    k, v = line.split('=', 1)
                    os.environ.setdefault(k.strip(), v.strip())
    except Exception:
        pass

in_memory_atm = {
    'TXN-1001': {'atm_location': 'Connaught Place South ATM #04', 'card_last4': '4821'}
}

CPP_SCHEDULER_URL = f"http://127.0.0.1:{CPP_SCHEDULER_PORT}"

is_mysql_connected = False
db_conn = None
db_lock = threading.RLock()


try:
    import mysql.connector
    from mysql.connector import pooling
    HAS_CONNECTOR = True
except ImportError:
    HAS_CONNECTOR = False

mysql_pool = None

# ----------------------------------------------------------------------------
# In-Memory Transactional Fallback Store (Ensures 100% Demo Reliability)
# Synchronizes all 8 tables in RAM if MySQL service is unavailable
# ----------------------------------------------------------------------------
in_memory_accounts = {
    'ACC1001': {'customer_name': 'Rajesh Sharma', 'account_type': 'SAVINGS'},
    'ACC1002': {'customer_name': 'Priya Patel', 'account_type': 'SAVINGS'},
    'ACC1003': {'customer_name': 'Amit Verma', 'account_type': 'CURRENT'},
    'ACC1004': {'customer_name': 'Sneha Kulkarni', 'account_type': 'SAVINGS'},
    'ACC1005': {'customer_name': 'TechCorp Ltd', 'account_type': 'CORPORATE'},
    'ACC1006': {'customer_name': 'Metro ATM Vault', 'account_type': 'VAULT'}
}

in_memory_balances = {
    'ACC1001': Decimal('150000.00'),
    'ACC1002': Decimal('85000.00'),
    'ACC1003': Decimal('240000.00'),
    'ACC1004': Decimal('95000.00'),
    'ACC1005': Decimal('1500000.00'),
    'ACC1006': Decimal('800000.00')
}

in_memory_transactions = [
    {
        'txn_id': 'TXN-1001', 'sender_id': 'ACC1001', 'receiver_id': 'ACC1006',
        'txn_type': 'ATM', 'amount': Decimal('2500.00'), 'auto_burst_time': 1,
        'assigned_queue': 'Q1_PRIORITY', 'status': 'COMPLETED',
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-1002', 'sender_id': 'ACC1002', 'receiver_id': 'ACC1004',
        'txn_type': 'UPI', 'amount': Decimal('1800.00'), 'auto_burst_time': 2,
        'assigned_queue': 'Q1_PRIORITY', 'status': 'COMPLETED',
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-1003', 'sender_id': 'ACC1003', 'receiver_id': 'ACC1001',
        'txn_type': 'STANDARD', 'amount': Decimal('8500.00'), 'auto_burst_time': 3,
        'assigned_queue': 'Q2_SJF', 'status': 'COMPLETED',
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-1004', 'sender_id': 'ACC1005', 'receiver_id': 'ACC1002',
        'txn_type': 'PAYROLL', 'amount': Decimal('65000.00'), 'auto_burst_time': 7,
        'assigned_queue': 'Q3_FCFS', 'status': 'COMPLETED',
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    }
]


in_memory_upi_payroll = {
    'TXN-1002': {'channel_mode': 'UPI', 'reference_id': 'priya@okhdfcbank'},
    'TXN-1004': {'channel_mode': 'PAYROLL', 'reference_id': 'BATCH-DEC-TECHCORP-01'}
}

in_memory_atm = {
    'TXN-1001': {'atm_location': 'Connaught Place South ATM #04', 'card_last4': '4821'}
}

# Table 6: transaction_queue (RAM snapshot of waiting processes)
in_memory_queue = {}

# Table 7: transaction_logs (OS state-transition audit trail)
in_memory_logs = [
    {
        'log_id': 1, 'txn_id': 'TXN-1001', 'from_state': 'NEW', 'to_state': 'READY',
        'queue_level': 'Q1_PRIORITY', 'event_note': 'Admitted to Queue 1 (Priority) via ATM gateway',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 2, 'txn_id': 'TXN-1001', 'from_state': 'READY', 'to_state': 'RUNNING',
        'queue_level': 'Q1_PRIORITY', 'event_note': 'Dispatched to CPU Core 0 for 1s debit execution',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 3, 'txn_id': 'TXN-1001', 'from_state': 'RUNNING', 'to_state': 'TERMINATED',
        'queue_level': 'Q1_PRIORITY', 'event_note': 'Burst finished; MySQL ACID balance deduction committed',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 4, 'txn_id': 'TXN-1002', 'from_state': 'NEW', 'to_state': 'READY',
        'queue_level': 'Q1_PRIORITY', 'event_note': 'Admitted to Queue 1 (Priority) via UPI gateway',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 5, 'txn_id': 'TXN-1002', 'from_state': 'READY', 'to_state': 'RUNNING',
        'queue_level': 'Q1_PRIORITY', 'event_note': 'Dispatched to CPU Core 0 for 2s peer transfer',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 6, 'txn_id': 'TXN-1002', 'from_state': 'RUNNING', 'to_state': 'TERMINATED',
        'queue_level': 'Q1_PRIORITY', 'event_note': 'Burst finished; MySQL ACID balance transfer committed',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 7, 'txn_id': 'TXN-1003', 'from_state': 'NEW', 'to_state': 'READY',
        'queue_level': 'Q2_SJF', 'event_note': 'Admitted to Queue 2 (SJF) for lightweight standard transfer',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 8, 'txn_id': 'TXN-1003', 'from_state': 'READY', 'to_state': 'RUNNING',
        'queue_level': 'Q2_SJF', 'event_note': 'Dispatched to CPU Core 0 (Shortest remaining burst = 3s)',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 9, 'txn_id': 'TXN-1003', 'from_state': 'RUNNING', 'to_state': 'TERMINATED',
        'queue_level': 'Q2_SJF', 'event_note': 'Burst finished; MySQL ACID balance transfer committed',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 10, 'txn_id': 'TXN-1004', 'from_state': 'NEW', 'to_state': 'READY',
        'queue_level': 'Q3_FCFS', 'event_note': 'Admitted to Queue 3 (FCFS) for heavy corporate payroll batch',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 11, 'txn_id': 'TXN-1004', 'from_state': 'READY', 'to_state': 'RUNNING',
        'queue_level': 'Q3_FCFS', 'event_note': 'Dispatched to CPU Core 0 for 7s batch verification',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'log_id': 12, 'txn_id': 'TXN-1004', 'from_state': 'RUNNING', 'to_state': 'TERMINATED',
        'queue_level': 'Q3_FCFS', 'event_note': 'Batch burst finished; MySQL ACID bulk balance credit committed',
        'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    }
]

# Table 8: scheduler_metrics (Live & historical MLFQ performance telemetry)
in_memory_metrics = {
    'Q1_PRIORITY': {
        'queue_level': 'Q1_PRIORITY', 'total_processed': 2,
        'avg_wait_time': Decimal('0.45'), 'avg_turnaround_time': Decimal('1.95'),
        'starvation_rescued': 0,
        'last_updated': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    'Q2_SJF': {
        'queue_level': 'Q2_SJF', 'total_processed': 1,
        'avg_wait_time': Decimal('1.20'), 'avg_turnaround_time': Decimal('4.20'),
        'starvation_rescued': 0,
        'last_updated': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    'Q3_FCFS': {
        'queue_level': 'Q3_FCFS', 'total_processed': 1,
        'avg_wait_time': Decimal('3.50'), 'avg_turnaround_time': Decimal('10.50'),
        'starvation_rescued': 1,
        'last_updated': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    }
}

seen_synced_transitions = set()
committed_txns = set(['TXN-1001', 'TXN-1002', 'TXN-1003', 'TXN-1004'])

# ----------------------------------------------------------------------------
# Database Connection & Initialization
# ----------------------------------------------------------------------------
def init_mysql():
    global is_mysql_connected, mysql_pool
    if not HAS_CONNECTOR:
        print("[MySQL] mysql-connector-python not found. In-memory engine active.")
        return False

    try:
        srv = mysql.connector.connect(host=DB_HOST, port=DB_PORT, user=DB_USER, password=DB_PASSWORD)
        cur = srv.cursor()
        cur.execute(f"CREATE DATABASE IF NOT EXISTS `{DB_NAME}`;")
        cur.close()
        srv.close()

        mysql_pool = pooling.MySQLConnectionPool(
            pool_name="bankos_pool",
            pool_size=5,
            host=DB_HOST,
            port=DB_PORT,
            user=DB_USER,
            password=DB_PASSWORD,
            database=DB_NAME
        )



        in_memory_atm = {
    'TXN-1001': {'atm_location': 'Connaught Place South ATM #04', 'card_last4': '4821'}
}

                            pass
            try:
                cur.execute("ALTER TABLE scheduler_metrics ADD COLUMN starvation_rescued INT NOT NULL DEFAULT 0;")
            except Exception:
                pass
            conn.commit()
            cur.close()
            conn.close()

        is_mysql_connected = True
        print(f"[MySQL] ✓ Connected to MySQL ({DB_HOST}:{DB_PORT}/{DB_NAME}) with 8 synchronized tables.")
        return True
    except Exception as e:
        is_mysql_connected = False
        print(f"[MySQL] Notice: MySQL service offline ({e}). Using In-Memory ACID engine.")
        return False

def get_db():
    if is_mysql_connected and mysql_pool:
        try:
            return mysql_pool.get_connection()
        except Exception:
            return None
    return None

# ----------------------------------------------------------------------------
# Helper: Auto Burst Time and Initial Queue Determination
# ----------------------------------------------------------------------------
def calculate_auto_burst_time(txn_type, amount):
    if txn_type == 'ATM':
        return 1
    elif txn_type == 'UPI':
        return 2
    elif txn_type == 'STANDARD':
        return 3 if amount <= 10000.0 else 5
    elif txn_type == 'PAYROLL':
        return 7
    return 3

def determine_queue(txn_type, burst_time):
    if txn_type in ('ATM', 'UPI'):
        return 'Q1_PRIORITY'
    if txn_type == 'STANDARD' and burst_time <= 3:
        return 'Q2_SJF'
    return 'Q3_FCFS'

def determine_priority_score(txn_type, queue_level):
    if txn_type == 'ATM': return 10
    if txn_type == 'UPI': return 8
    if queue_level == 'Q2_SJF': return 5
    return 2

# ----------------------------------------------------------------------------
# Mandatory SQL JOINs for Ledger (Supports Live Status & Test Tracking)
# ----------------------------------------------------------------------------
def query_joined_ledger(category=None, search=None, test_only=False, limit=100):
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor(dictionary=True)
            sql = """
                SELECT 
                    t.txn_id,
                    t.sender_id,
                    s.customer_name AS sender_name,
                    sb.current_balance AS sender_balance,
                    t.receiver_id,
                    r.customer_name AS receiver_name,
                    rb.current_balance AS receiver_balance,
                    t.txn_type,
                    t.amount,
                    t.auto_burst_time,
                    t.assigned_queue,
                    t.status,
                    t.is_test,
                    t.created_at,
                    atm.atm_location,
                    atm.card_last4,
                    up.channel_mode,
                    up.reference_id
                FROM transactions t
                INNER JOIN accounts s ON t.sender_id = s.account_id
                INNER JOIN balance sb ON s.account_id = sb.account_id
                INNER JOIN accounts r ON t.receiver_id = r.account_id
                INNER JOIN balance rb ON r.account_id = rb.account_id
                LEFT JOIN atm_transactions atm ON t.txn_id = atm.txn_id
                LEFT JOIN upi_payroll_transactions up ON t.txn_id = up.txn_id
                WHERE 1=1
            """
            params = []
            if category and category.upper() != "ALL":
                sql += " AND t.txn_type = %s"
                params.append(category.upper())

            if test_only:
                sql += " AND t.is_test = 1"

            if search:
                sql += """ AND (
                    t.txn_id LIKE %s OR 
                    s.customer_name LIKE %s OR 
                    r.customer_name LIKE %s OR 
                    atm.atm_location LIKE %s OR 
                    up.reference_id LIKE %s
                )"""
                term = f"%{search}%"
                params.extend([term, term, term, term, term])

            sql += " ORDER BY t.created_at DESC LIMIT %s;"
            params.append(limit)

            cur.execute(sql, tuple(params))
            rows = cur.fetchall()
            cur.close()
            conn.close()

            for r in rows:
                r['amount'] = float(r['amount'])
                r['sender_balance'] = float(r['sender_balance'])
                r['receiver_balance'] = float(r['receiver_balance'])
                r['is_test'] = bool(r.get('is_test', 0))
                if isinstance(r.get('created_at'), datetime):
                    r['created_at'] = r['created_at'].strftime('%Y-%m-%d %H:%M:%S')
            return rows
        except Exception as e:
            print("[SQL] Query joined ledger error:", e)

    # In-memory fallback
    with db_lock:
        res = []
        for t in reversed(in_memory_transactions):
            if category and category.upper() != "ALL" and t['txn_type'].upper() != category.upper():
                continue

            if test_only and not t.get('is_test', False):
                continue

            s_name = in_memory_accounts.get(t['sender_id'], {}).get('customer_name', 'Unknown')
            r_name = in_memory_accounts.get(t['receiver_id'], {}).get('customer_name', 'Unknown')
            s_bal = float(in_memory_balances.get(t['sender_id'], Decimal('0.00')))
            r_bal = float(in_memory_balances.get(t['receiver_id'], Decimal('0.00')))

            atm_row = in_memory_atm.get(t['txn_id'], {})
            up_row = in_memory_upi_payroll.get(t['txn_id'], {})

            row = {
                'txn_id': t['txn_id'],
                'sender_id': t['sender_id'],
                'sender_name': s_name,
                'sender_balance': s_bal,
                'receiver_id': t['receiver_id'],
                'receiver_name': r_name,
                'receiver_balance': r_bal,
                'txn_type': t['txn_type'],
                'amount': float(t['amount']),
                'auto_burst_time': t['auto_burst_time'],
                'assigned_queue': t['assigned_queue'],
                'status': t['status'],
                'is_test': bool(t.get('is_test', False)),
                'created_at': t['created_at'],
                'atm_location': atm_row.get('atm_location'),
                'card_last4': atm_row.get('card_last4'),
                'channel_mode': up_row.get('channel_mode'),
                'reference_id': up_row.get('reference_id')
            }

            if search:
                term = search.lower()
                matched = (
                    term in row['txn_id'].lower() or
                    term in row['sender_name'].lower() or
                    term in row['receiver_name'].lower() or
                    (row['atm_location'] and term in row['atm_location'].lower()) or
                    (row['reference_id'] and term in row['reference_id'].lower())
                )
                if not matched:
                    continue

            res.append(row)
            if len(res) >= limit:
                break
        return res

# ----------------------------------------------------------------------------
# Query Live State-Transition Logs (Table 7: transaction_logs)
# ----------------------------------------------------------------------------
def query_transaction_logs(limit=100):
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor(dictionary=True)
            cur.execute("""
                SELECT log_id, txn_id, from_state, to_state, queue_level, event_note, logged_at
                FROM transaction_logs
                ORDER BY log_id DESC
                LIMIT %s;
            """, (limit,))
            rows = cur.fetchall()
            cur.close()
            conn.close()
            for r in rows:
                if isinstance(r.get('logged_at'), datetime):
                    r['logged_at'] = r['logged_at'].strftime('%Y-%m-%d %H:%M:%S')
            return rows
        except Exception as e:
            print("[SQL] Query transaction_logs error:", e)

    with db_lock:
        logs_copy = list(reversed(in_memory_logs))[:limit]
        return logs_copy

# ----------------------------------------------------------------------------
# Query Live Scheduler Metrics (Table 8: scheduler_metrics)
# ----------------------------------------------------------------------------
def query_scheduler_metrics():
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor(dictionary=True)
            cur.execute("""
                SELECT metric_id, queue_level, total_processed, avg_wait_time, avg_turnaround_time, starvation_rescued, last_updated
                FROM scheduler_metrics
                ORDER BY metric_id ASC;
            """)
            rows = cur.fetchall()
            cur.close()
            conn.close()
            for r in rows:
                r['avg_wait_time'] = float(r['avg_wait_time'])
                r['avg_turnaround_time'] = float(r['avg_turnaround_time'])
                r['starvation_rescued'] = int(r.get('starvation_rescued', 0))
                if isinstance(r.get('last_updated'), datetime):
                    r['last_updated'] = r['last_updated'].strftime('%Y-%m-%d %H:%M:%S')
            return rows
        except Exception as e:
            print("[SQL] Query scheduler_metrics error:", e)

    with db_lock:
        res = []
        for k in ['Q1_PRIORITY', 'Q2_SJF', 'Q3_FCFS']:
            m = in_memory_metrics.get(k)
            if m:
                res.append({
                    'queue_level': m['queue_level'],
                    'total_processed': m['total_processed'],
                    'avg_wait_time': float(m['avg_wait_time']),
                    'avg_turnaround_time': float(m['avg_turnaround_time']),
                    'starvation_rescued': int(m.get('starvation_rescued', 0)),
                    'last_updated': m['last_updated']
                })
        return res

# ----------------------------------------------------------------------------
# Query Accounts & Balances (accounts JOIN balance)
# ----------------------------------------------------------------------------
def query_accounts_and_balances():
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor(dictionary=True)
            cur.execute("""
                SELECT 
                    a.account_id,
                    a.customer_name,
                    a.account_type,
                    b.current_balance,
                    b.last_updated
                FROM accounts a
                INNER JOIN balance b ON a.account_id = b.account_id
                ORDER BY a.account_id ASC;
            """)
            rows = cur.fetchall()
            cur.close()
            conn.close()
            for r in rows:
                r['current_balance'] = float(r['current_balance'])
                if isinstance(r.get('last_updated'), datetime):
                    r['last_updated'] = r['last_updated'].strftime('%Y-%m-%d %H:%M:%S')
            return rows
        except Exception as e:
            print("[SQL] Query accounts error:", e)

    with db_lock:
        res = []
        for aid, acc in in_memory_accounts.items():
            res.append({
                'account_id': aid,
                'customer_name': acc['customer_name'],
                'account_type': acc['account_type'],
                'current_balance': float(in_memory_balances.get(aid, Decimal('0.00'))),
                'last_updated': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            })
        return res

# ----------------------------------------------------------------------------
# Commit ACID Balance Transfer on Completion
# ----------------------------------------------------------------------------
def commit_acid_transfer(txn_id, sender_id, receiver_id, amount):
    amount_dec = Decimal(str(amount))
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor()
            cur.execute("START TRANSACTION;")

            # Check if transaction exists in database
            cur.execute("SELECT txn_id FROM transactions WHERE txn_id = %s;", (txn_id,))
            if not cur.fetchone():
                cur.execute("ROLLBACK;")
                cur.close()
                conn.close()
                return False, f"Transaction {txn_id} not in transactions table"

            # Lock sender & receiver balance rows
            cur.execute("SELECT current_balance FROM balance WHERE account_id = %s FOR UPDATE;", (sender_id,))
            s_row = cur.fetchone()
            cur.execute("SELECT current_balance FROM balance WHERE account_id = %s FOR UPDATE;", (receiver_id,))
            r_row = cur.fetchone()

            if not s_row or not r_row:
                cur.execute("ROLLBACK;")
                cur.close()
                conn.close()
                return False, "Account balance record missing"

            # Execute atomic transfer
            cur.execute("UPDATE balance SET current_balance = current_balance - %s WHERE account_id = %s;", (amount_dec, sender_id))
            cur.execute("UPDATE balance SET current_balance = current_balance + %s WHERE account_id = %s;", (amount_dec, receiver_id))

            # Mark transaction completed
            cur.execute("UPDATE transactions SET status = 'COMPLETED' WHERE txn_id = %s;", (txn_id,))

            # Remove from waiting queue table
            cur.execute("DELETE FROM transaction_queue WHERE txn_id = %s;", (txn_id,))

            # Append completed state transition log
            cur.execute("""
                INSERT INTO transaction_logs (txn_id, from_state, to_state, queue_level, event_note)
                VALUES (%s, 'RUNNING', 'TERMINATED', 'COMMITTED', 'ACID balance deduction and credit committed to MySQL');
            """, (txn_id,))

            cur.execute("COMMIT;")
            cur.close()
            conn.close()
            return True, "MySQL ACID transfer committed"
        except Exception as e:
            if conn:
                try:
                    conn.rollback()
                    conn.close()
                except Exception:
                    pass
            print(f"[ACID] Transfer failed for {txn_id}: {e}")

    # In-memory ACID transfer
    with db_lock:
        if sender_id in in_memory_balances and receiver_id in in_memory_balances:
            in_memory_balances[sender_id] -= amount_dec
            in_memory_balances[receiver_id] += amount_dec

            for t in in_memory_transactions:
                if t['txn_id'] == txn_id:
                    t['status'] = 'COMPLETED'
                    break

            in_memory_queue.pop(txn_id, None)

            in_memory_logs.append({
                'log_id': len(in_memory_logs) + 1,
                'txn_id': txn_id,
                'from_state': 'RUNNING',
                'to_state': 'TERMINATED',
                'queue_level': 'COMMITTED',
                'event_note': f"ACID balance transfer of ₹{amount_dec:,.2f} executed atomically",
                'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            })
            return True, "In-memory ACID transfer committed"
        return False, "Invalid sender or receiver"

# ----------------------------------------------------------------------------
# Insert Transaction into Database & Queue
# ----------------------------------------------------------------------------
def insert_admitted_transaction(p_data):
    txn_id = p_data['txn_id']
    sender_id = p_data['sender_id']
    receiver_id = p_data['receiver_id']
    txn_type = p_data['txn_type']
    amount = Decimal(str(p_data['amount']))
    burst = int(p_data.get('auto_burst_time', 1))
    queue = p_data.get('assigned_queue', 'Q1_PRIORITY')
    priority = int(p_data.get('priority_score', 5))

    conn = get_db()
    if conn:
        try:
            cur = conn.cursor()
            is_test_val = 1 if p_data.get('is_test', False) else 0
            cur.execute("""
                INSERT INTO transactions (txn_id, sender_id, receiver_id, txn_type, amount, auto_burst_time, assigned_queue, status, is_test)
                VALUES (%s, %s, %s, %s, %s, %s, %s, 'QUEUED', %s);
            """, (txn_id, sender_id, receiver_id, txn_type, amount, burst, queue, is_test_val))

            if txn_type == 'ATM':
                atm_loc = p_data.get('atm_location', 'Connaught Place South ATM #04')
                card = p_data.get('card_last4', '4821')
                cur.execute("INSERT INTO atm_transactions (txn_id, atm_location, card_last4) VALUES (%s, %s, %s);", (txn_id, atm_loc, card))
            else:
                mode = 'PAYROLL' if txn_type == 'PAYROLL' else 'UPI'
                ref = p_data.get('reference_id', f"TXN-{random.randint(100000, 999999)}")
                cur.execute("INSERT INTO upi_payroll_transactions (txn_id, channel_mode, reference_id) VALUES (%s, %s, %s);", (txn_id, mode, ref))

            cur.execute("""
                INSERT INTO transaction_queue (txn_id, queue_level, priority_score, remaining_burst, wait_time_sec)
                VALUES (%s, %s, %s, %s, 0)
                ON DUPLICATE KEY UPDATE remaining_burst = VALUES(remaining_burst);
            """, (txn_id, queue, priority, burst))

            cur.execute("""
                INSERT INTO transaction_logs (txn_id, from_state, to_state, queue_level, event_note)
                VALUES (%s, 'NEW', 'READY', %s, %s);
            """, (txn_id, queue, f"Admitted to MLFQ {queue} (Auto burst: {burst}s)"))

            conn.commit()
            cur.close()
            conn.close()
            return True
        except Exception as e:
            if conn:
                try: conn.close()
                except Exception: pass
            print(f"[SQL] Error inserting transaction {txn_id}: {e}")

    # In-memory storage
    with db_lock:
        in_memory_transactions.append({
            'txn_id': txn_id,
            'sender_id': sender_id,
            'receiver_id': receiver_id,
            'txn_type': txn_type,
            'amount': amount,
            'auto_burst_time': burst,
            'assigned_queue': queue,
            'status': 'QUEUED',
            'is_test': bool(p_data.get('is_test', False)),
            'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        })

        if txn_type == 'ATM':
            in_memory_atm[txn_id] = {
                'atm_location': p_data.get('atm_location', 'Connaught Place South ATM #04'),
                'card_last4': p_data.get('card_last4', '4821')
            }
        else:
            in_memory_upi_payroll[txn_id] = {
                'channel_mode': 'PAYROLL' if txn_type == 'PAYROLL' else 'UPI',
                'reference_id': p_data.get('reference_id', f"TXN-{random.randint(100000, 999999)}")
            }

        in_memory_queue[txn_id] = {
            'txn_id': txn_id, 'queue_level': queue, 'priority_score': priority,
            'remaining_burst': burst, 'wait_time_sec': 0
        }

        in_memory_logs.append({
            'log_id': len(in_memory_logs) + 1,
            'txn_id': txn_id,
            'from_state': 'NEW',
            'to_state': 'READY',
            'queue_level': queue,
            'event_note': f"Admitted to MLFQ {queue} (Auto burst: {burst}s)",
            'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        })
    return True

# ----------------------------------------------------------------------------
# Real-Time Telemetry & Sync Loop (C++ State <-> MySQL 8 Tables)
# ----------------------------------------------------------------------------
def mark_transaction_running(txn_id):
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor()
            cur.execute("UPDATE transactions SET status = 'RUNNING' WHERE txn_id = %s AND status = 'QUEUED';", (txn_id,))
            conn.commit()
            cur.close()
            conn.close()
        except Exception:
            if conn:
                try: conn.close()
                except Exception: pass
    with db_lock:
        for t in in_memory_transactions:
            if t['txn_id'] == txn_id and t['status'] == 'QUEUED':
                t['status'] = 'RUNNING'
                break

def sync_scheduler_telemetry():
    while True:
        try:
            state = fetch_cpp_state()
            if state:
                # 0. Sync Active Running Process status
                active_p = state.get('active_process')
                if active_p and active_p.get('txn_id'):
                    mark_transaction_running(active_p['txn_id'])

                # 1. Sync State Transitions into transaction_logs
                transitions = state.get('transitions', [])
                for tr in reversed(transitions):
                    key = f"{tr['txn_id']}:{tr['from_state']}:{tr['to_state']}:{tr['event_note']}"
                    if key not in seen_synced_transitions:
                        seen_synced_transitions.add(key)
                        log_entry(tr['txn_id'], tr['from_state'], tr['to_state'], tr['queue_level'], tr['event_note'])

                # 2. Sync Completed Transactions & Execute ACID Transfers
                completed_list = state.get('completed', [])
                for cp in completed_list:
                    cid = cp['txn_id']
                    if cid not in committed_txns:
                        committed_txns.add(cid)
                        commit_acid_transfer(cid, cp['sender_id'], cp['receiver_id'], cp['amount'])

                # 3. Sync Active Waiting Processes in transaction_queue
                waiting_pcbs = []
                for qname in ('queue_1', 'queue_2', 'queue_3'):
                    for item in state.get(qname, []):
                        waiting_pcbs.append(item)

                sync_waiting_queue_table(waiting_pcbs)

                # 4. Sync scheduler_metrics table
                metrics_list = state.get('metrics', [])
                if metrics_list:
                    sync_metrics_table(metrics_list)

        except Exception as e:
            pass
        time.sleep(0.5)

def log_entry(txn_id, from_st, to_st, queue, note):
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor()
            cur.execute("SELECT txn_id FROM transactions WHERE txn_id = %s;", (txn_id,))
            if not cur.fetchone():
                cur.close()
                conn.close()
                return

            cur.execute("""
                INSERT INTO transaction_logs (txn_id, from_state, to_state, queue_level, event_note)
                VALUES (%s, %s, %s, %s, %s);
            """, (txn_id, from_st, to_st, queue, note))
            conn.commit()
            cur.close()
            conn.close()
            return
        except Exception:
            if conn:
                try: conn.close()
                except Exception: pass

    with db_lock:
        in_memory_logs.append({
            'log_id': len(in_memory_logs) + 1,
            'txn_id': txn_id,
            'from_state': from_st,
            'to_state': to_st,
            'queue_level': queue,
            'event_note': note,
            'logged_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        })

def sync_waiting_queue_table(waiting_pcbs):
    active_ids = {p['txn_id'] for p in waiting_pcbs}
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor()
            for p in waiting_pcbs:
                cur.execute("""
                    INSERT INTO transaction_queue (txn_id, queue_level, priority_score, remaining_burst, wait_time_sec)
                    VALUES (%s, %s, %s, %s, %s)
                    ON DUPLICATE KEY UPDATE 
                        queue_level = VALUES(queue_level),
                        priority_score = VALUES(priority_score),
                        remaining_burst = VALUES(remaining_burst),
                        wait_time_sec = VALUES(wait_time_sec);
                """, (p['txn_id'], p.get('queue_level', 'Q1_PRIORITY'), p.get('priority_score', 0), p.get('remaining_burst', 1), p.get('wait_time_sec', 0)))

            if active_ids:
                format_strings = ','.join(['%s'] * len(active_ids))
                cur.execute(f"DELETE FROM transaction_queue WHERE txn_id NOT IN ({format_strings});", tuple(active_ids))
            else:
                cur.execute("DELETE FROM transaction_queue;")

            conn.commit()
            cur.close()
            conn.close()
            return
        except Exception:
            if conn:
                try: conn.close()
                except Exception: pass

    with db_lock:
        in_memory_queue.clear()
        for p in waiting_pcbs:
            in_memory_queue[p['txn_id']] = {
                'txn_id': p['txn_id'],
                'queue_level': p.get('queue_level', 'Q1_PRIORITY'),
                'priority_score': p.get('priority_score', 0),
                'remaining_burst': p.get('remaining_burst', 1),
                'wait_time_sec': p.get('wait_time_sec', 0)
            }

def sync_metrics_table(metrics_list):
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor()
            for m in metrics_list:
                cur.execute("""
                    INSERT INTO scheduler_metrics (queue_level, total_processed, avg_wait_time, avg_turnaround_time, starvation_rescued)
                    VALUES (%s, %s, %s, %s, %s)
                    ON DUPLICATE KEY UPDATE
                        total_processed = VALUES(total_processed),
                        avg_wait_time = VALUES(avg_wait_time),
                        avg_turnaround_time = VALUES(avg_turnaround_time),
                        starvation_rescued = VALUES(starvation_rescued);
                """, (m['queue_level'], m['total_processed'], m['avg_wait_time'], m['avg_turnaround_time'], m.get('starvation_rescued', 0)))
            conn.commit()
            cur.close()
            conn.close()
            return
        except Exception:
            if conn:
                try: conn.close()
                except Exception: pass

    with db_lock:
        for m in metrics_list:
            k = m['queue_level']
            in_memory_metrics[k] = {
                'queue_level': k,
                'total_processed': m['total_processed'],
                'avg_wait_time': Decimal(str(m['avg_wait_time'])),
                'avg_turnaround_time': Decimal(str(m['avg_turnaround_time'])),
                'starvation_rescued': int(m.get('starvation_rescued', 0)),
                'last_updated': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            }

# ----------------------------------------------------------------------------
# C++ Scheduler Subprocess & IPC Communication
# ----------------------------------------------------------------------------
cpp_process = None

def ensure_cpp_scheduler_running():
    global cpp_process
    bin_path = os.path.join(os.path.dirname(__file__), "scheduler")
    if not os.path.exists(bin_path):
        src_path = os.path.join(os.path.dirname(__file__), "scheduler_engine.cpp")
        if os.path.exists(src_path):
            print("[C++ Engine] Compiling modular scheduler_engine.cpp...")
            subprocess.run(["g++", "-O2", "-std=c++17", src_path, "-o", bin_path, "-pthread"], check=True)

    try:
        # Check if already running on port
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.3)
        res = s.connect_ex(('127.0.0.1', CPP_SCHEDULER_PORT))
        s.close()
        if res == 0:
            return
    except Exception:
        pass

    try:
        cpp_process = subprocess.Popen([bin_path, str(CPP_SCHEDULER_PORT)],
                                       stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
        time.sleep(0.4)
        print("[C++ Engine] ✓ Launched background scheduler binary.")
    except Exception as e:
        print("[C++ Engine] Notice: Could not launch binary directly:", e)

def fetch_cpp_state():
    # Attempt 1: Fast TCP Socket
    try:
        req = urllib.request.Request(f"{CPP_SCHEDULER_URL}/state")
        with urllib.request.urlopen(req, timeout=0.4) as response:
            if response.status == 200:
                data = json.loads(response.read().decode('utf-8'))
                return data
    except Exception:
        pass

    # Attempt 2: Resilient Atomic File Bridge (state.json)
    try:
        state_file = os.path.join(os.path.dirname(__file__), "state.json")
        if os.path.exists(state_file):
            with open(state_file, 'r', encoding='utf-8') as f:
                data = json.load(f)
                if 'metrics' not in data or not data['metrics']:
                    data['metrics'] = query_scheduler_metrics()
                if 'transitions' not in data:
                    data['transitions'] = []
                if 'starving_transactions' not in data:
                    data['starving_transactions'] = []
                if 'rescued_transactions' not in data:
                    data['rescued_transactions'] = []
                if 'starvation_prevented_count' not in data:
                    data['starvation_prevented_count'] = sum(int(m.get('starvation_rescued', 0)) for m in data.get('metrics', []))
                return data
    except Exception:
        pass

    metrics_rows = query_scheduler_metrics()
    starvation_rescued_sum = sum(int(m.get('starvation_rescued', 0)) for m in metrics_rows)
    return {
        "active_process": None,
        "queue_1": [],
        "queue_2": [],
        "queue_3": [],
        "completed": [],
        "transitions": [],
        "starving_transactions": [],
        "rescued_transactions": [],
        "starvation_prevented_count": starvation_rescued_sum,
        "metrics": metrics_rows,
        "stats": {"total_admitted": 0, "total_completed": 0, "starvation_prevented_count": starvation_rescued_sum, "cpu_active": False}
    }

def send_txn_to_cpp(txn_data):
    # Send via file inbox for guaranteed sandbox delivery
    try:
        inbox_file = os.path.join(os.path.dirname(__file__), "inbox.jsonl")
        line = (f"cmd=inject;txn_id={txn_data['txn_id']};sender={txn_data['sender_id']};"
                f"receiver={txn_data['receiver_id']};type={txn_data['txn_type']};"
                f"amount={txn_data['amount']};is_test={str(txn_data.get('is_test', False)).lower()}\n")
        with open(inbox_file, 'a', encoding='utf-8') as f:
            f.write(line)
    except Exception as e:
        print("[IPC] Inbox write error:", e)

    # Also notify TCP bridge if reachable
    try:
        req = urllib.request.Request(f"{CPP_SCHEDULER_URL}/inject", data=b"{}", headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=0.2):
            pass
    except Exception:
        pass

# ----------------------------------------------------------------------------
# Background Automated Traffic Simulation
# ----------------------------------------------------------------------------
is_traffic_active = False
traffic_thread = None

def traffic_worker():
    global is_traffic_active
    accounts = ['ACC1001', 'ACC1002', 'ACC1003', 'ACC1004', 'ACC1005', 'ACC1006']
    categories = ['ATM', 'UPI', 'STANDARD', 'PAYROLL']
    weights = [0.35, 0.40, 0.18, 0.07]

    while is_traffic_active:
        time.sleep(random.uniform(2.5, 5.0))
        if not is_traffic_active:
            break

        cat = random.choices(categories, weights=weights, k=1)[0]
        sender = random.choice(accounts)
        receiver = random.choice([a for a in accounts if a != sender])

        if cat == 'ATM':
            amount = round(random.uniform(500, 10000), 2)
            receiver = 'ACC1006' # Vault
        elif cat == 'UPI':
            amount = round(random.uniform(100, 5000), 2)
        elif cat == 'STANDARD':
            amount = round(random.uniform(5000, 25000), 2)
        else: # PAYROLL
            sender = 'ACC1005' # TechCorp Ltd
            amount = round(random.uniform(30000, 95000), 2)

        burst = calculate_auto_burst_time(cat, amount)
        queue = determine_queue(cat, burst)
        priority = determine_priority_score(cat, queue)
        txn_id = f"TXN-{random.randint(1000, 9999)}"

        p_data = {
            'txn_id': txn_id,
            'sender_id': sender,
            'receiver_id': receiver,
            'txn_type': cat,
            'amount': amount,
            'auto_burst_time': burst,
            'assigned_queue': queue,
            'priority_score': priority,
            'is_test': False
        }

        if cat == 'ATM':
            p_data['atm_location'] = random.choice(['Indira Nagar ATM #02', 'Bandra West ATM #11', 'Salt Lake Sector V ATM'])
            p_data['card_last4'] = str(random.randint(1000, 9999))
        else:
            p_data['reference_id'] = f"{cat.lower()}_{random.randint(100000, 999999)}"

        insert_admitted_transaction(p_data)
        send_txn_to_cpp(p_data)

# ----------------------------------------------------------------------------
# Flask Web Application & Endpoints
# ----------------------------------------------------------------------------
app = Flask(__name__, template_folder="templates")

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/state', methods=['GET'])
def get_state():
    state = fetch_cpp_state()
    state['db_connected'] = is_mysql_connected
    state['traffic_active'] = is_traffic_active
    if 'starvation_prevented_count' not in state or state['starvation_prevented_count'] is None:
        state['starvation_prevented_count'] = state.get('stats', {}).get('starvation_prevented_count', 0)
    return jsonify(state)

@app.route('/api/inject', methods=['POST'])
def inject_transaction():
    try:
        data = request.get_json(force=True)
        sender_id = data.get('sender_id', 'ACC1001')
        receiver_id = data.get('receiver_id', 'ACC1002')
        txn_type = data.get('txn_type', 'UPI').upper()
        amount = float(data.get('amount', 1000.0))

        if sender_id == receiver_id:
            return jsonify({'success': False, 'error': 'Sender and receiver cannot be identical'}), 400

        auto_burst = calculate_auto_burst_time(txn_type, amount)
        assigned_q = determine_queue(txn_type, auto_burst)
        priority = determine_priority_score(txn_type, assigned_q)
        txn_id = f"TXN-{random.randint(1000, 9999)}"

        p_data = {
            'txn_id': txn_id,
            'sender_id': sender_id,
            'receiver_id': receiver_id,
            'txn_type': txn_type,
            'amount': amount,
            'auto_burst_time': auto_burst,
            'assigned_queue': assigned_q,
            'priority_score': priority,
            'is_test': True,
            'atm_location': data.get('atm_location', 'Connaught Place South ATM #04'),
            'card_last4': data.get('card_last4', '4821'),
            'reference_id': data.get('reference_id', f"TEST-{random.randint(100000, 999999)}")
        }

        insert_admitted_transaction(p_data)
        send_txn_to_cpp(p_data)

        return jsonify({
            'success': True,
            'txn_id': txn_id,
            'auto_burst_time': auto_burst,
            'assigned_queue': assigned_q,
            'priority_score': priority,
            'message': f'Transaction {txn_id} admitted with auto-burst {auto_burst}s into {assigned_q}'
        })
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/ledger', methods=['GET'])
def get_ledger():
    cat = request.args.get('category', 'ALL')
    search = request.args.get('search', '')
    test_only = request.args.get('test_only', 'false').lower() == 'true'
    limit = int(request.args.get('limit', 100))
    rows = query_joined_ledger(category=cat, search=search, test_only=test_only, limit=limit)
    return jsonify({'success': True, 'count': len(rows), 'transactions': rows})

@app.route('/api/export_audit', methods=['GET'])
def export_audit_csv():
    try:
        output = io.StringIO()
        writer = csv.writer(output)
        
        # CSV Column Headers
        writer.writerow([
            "Transaction ID",
            "Sender Account",
            "Sender Customer",
            "Sender Balance (INR)",
            "Receiver Account",
            "Receiver Customer",
            "Receiver Balance (INR)",
            "Category",
            "Amount (INR)",
            "Auto Burst Time (s)",
            "Assigned MLFQ Queue",
            "Execution Status",
            "Is Manual Test Txn",
            "Channel Reference",
            "Channel Detail",
            "Admitted Timestamp",
            "State Transition Audit Trail"
        ])
        
        # Query up to 1000 transactions with full joins
        txns = query_joined_ledger(limit=1000)
        all_logs = query_transaction_logs(limit=2500)
        
        logs_by_txn = {}
        for l in all_logs:
            tid = l['txn_id']
            step = f"[{l['from_state']}->{l['to_state']} ({l['queue_level']}): {l['event_note']}]"
            logs_by_txn.setdefault(tid, []).append(step)

        for t in txns:
            tid = t['txn_id']
            t_logs = " | ".join(logs_by_txn.get(tid, ["Committed to MySQL"]))
            channel_ref = t.get('atm_location') or t.get('reference_id') or 'N/A'
            channel_det = t.get('card_last4') or t.get('channel_mode') or 'N/A'
            
            writer.writerow([
                t['txn_id'],
                t['sender_id'],
                t.get('sender_name', t['sender_id']),
                f"{t.get('sender_balance', 0.0):.2f}",
                t['receiver_id'],
                t.get('receiver_name', t['receiver_id']),
                f"{t.get('receiver_balance', 0.0):.2f}",
                t['txn_type'],
                f"{t['amount']:.2f}",
                t['auto_burst_time'],
                t['assigned_queue'],
                t['status'],
                "YES (Test Txn)" if t.get('is_test') else "NO (Simulated)",
                channel_ref,
                channel_det,
                t.get('created_at', ''),
                t_logs
            ])
        
        output.seek(0)
        timestamp_str = datetime.now().strftime('%Y%m%d_%H%M%S')
        filename = f"bankos_middleware_audit_log_{timestamp_str}.csv"
        
        return Response(
            output.getvalue(),
            mimetype="text/csv",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/logs', methods=['GET'])
def get_logs():
    limit = int(request.args.get('limit', 100))
    rows = query_transaction_logs(limit=limit)
    return jsonify({'success': True, 'count': len(rows), 'logs': rows})

@app.route('/api/metrics', methods=['GET'])
def get_metrics():
    rows = query_scheduler_metrics()
    return jsonify({'success': True, 'metrics': rows})

@app.route('/api/accounts', methods=['GET'])
def get_accounts():
    rows = query_accounts_and_balances()
    return jsonify({'success': True, 'accounts': rows})

@app.route('/api/traffic/toggle', methods=['POST'])
def toggle_traffic():
    global is_traffic_active, traffic_thread
    is_traffic_active = not is_traffic_active
    if is_traffic_active:
        traffic_thread = threading.Thread(target=traffic_worker, daemon=True)
        traffic_thread.start()
    return jsonify({'success': True, 'traffic_active': is_traffic_active})

@app.route('/api/reset', methods=['POST'])
def reset_database():
    global is_traffic_active, in_memory_transactions, in_memory_logs, in_memory_queue, committed_txns, seen_synced_transitions
    is_traffic_active = False

    conn = get_db()
    if conn:
        try:
            schema_file = os.path.join(os.path.dirname(__file__), "schema.sql")
            cur = conn.cursor()
            with open(schema_file, 'r', encoding='utf-8') as f:
                statements = f.read().split(';')
                for stmt in statements:
                    stmt = stmt.strip()
                    if stmt and not stmt.upper().startswith("CREATE DATABASE") and not stmt.upper().startswith("USE "):
                        try:
                            cur.execute(stmt)
                        except Exception:
                            pass
            conn.commit()
            cur.close()
            conn.close()
        except Exception as e:
            print("[Reset] SQL reset error:", e)

    with db_lock:
        in_memory_balances['ACC1001'] = Decimal('150000.00')
        in_memory_balances['ACC1002'] = Decimal('85000.00')
        in_memory_balances['ACC1003'] = Decimal('240000.00')
        in_memory_balances['ACC1004'] = Decimal('95000.00')
        in_memory_balances['ACC1005'] = Decimal('1500000.00')
        in_memory_balances['ACC1006'] = Decimal('800000.00')
        in_memory_queue.clear()
        committed_txns = set(['TXN-1001', 'TXN-1002', 'TXN-1003', 'TXN-1004'])
        seen_synced_transitions.clear()

    # Reset C++ scheduler state via inbox
    try:
        with open("inbox.jsonl", "a", encoding="utf-8") as f:
            f.write("cmd=reset\n")
    except Exception:
        pass

    return jsonify({'success': True, 'message': 'System state and tables reset to seed defaults'})

# ----------------------------------------------------------------------------
# Port Detection & Server Initialization
# ----------------------------------------------------------------------------
def find_available_port(ports=[5001, 5000, 5050, 8080]):
    for port in ports:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            if s.connect_ex(('127.0.0.1', port)) != 0:
                return port
    return 5001

if __name__ == '__main__':
    print("========================================================")
    print(" BankOS Middleware: Python Bridge Launching...")
    print(" Synchronizing 8 MySQL Relational & Telemetry Tables")
    print("========================================================")

    init_mysql()
    ensure_cpp_scheduler_running()

    # Start telemetry background sync thread
    sync_thread = threading.Thread(target=sync_scheduler_telemetry, daemon=True)
    sync_thread.start()

    run_port = find_available_port([5001, 5000, 5050, 8080])
    print(f"[BankOS] ✓ Web Dashboard active at: http://127.0.0.1:{run_port}")
    app.run(host='0.0.0.0', port=run_port, debug=False)
