# ============================================================================
# BankOS Middleware: Python Flask Bridge (server.py)
# Connects C++ MLFQ OS Engine, MySQL (bankos_db), and 3-Slide Web Dashboard
# ============================================================================

import os
import time
import json
import random
import urllib.request
import urllib.parse
import subprocess
import threading
from decimal import Decimal
from datetime import datetime
from flask import Flask, render_template, request, jsonify

# ----------------------------------------------------------------------------
# MySQL Database Configuration
# ----------------------------------------------------------------------------
DB_HOST = os.getenv("MYSQL_HOST", "127.0.0.1")
DB_PORT = int(os.getenv("MYSQL_PORT", 3306))
DB_USER = os.getenv("MYSQL_USER", "root")
DB_PASSWORD = os.getenv("MYSQL_PASSWORD", "")
DB_NAME = os.getenv("MYSQL_DB", "bankos_db")

CPP_SCHEDULER_PORT = 8765
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

# In-memory transactional fallback store (ensures 100% demo uptime)
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
        'assigned_queue': 'Queue 1 (Priority)', 'status': 'COMPLETED',
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-1002', 'sender_id': 'ACC1002', 'receiver_id': 'ACC1004',
        'txn_type': 'UPI', 'amount': Decimal('1800.00'), 'auto_burst_time': 2,
        'assigned_queue': 'Queue 1 (Priority)', 'status': 'COMPLETED',
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-1003', 'sender_id': 'ACC1003', 'receiver_id': 'ACC1001',
        'txn_type': 'STANDARD', 'amount': Decimal('8500.00'), 'auto_burst_time': 3,
        'assigned_queue': 'Queue 2 (SJF)', 'status': 'COMPLETED',
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-1004', 'sender_id': 'ACC1005', 'receiver_id': 'ACC1002',
        'txn_type': 'PAYROLL', 'amount': Decimal('65000.00'), 'auto_burst_time': 7,
        'assigned_queue': 'Queue 3 (FCFS)', 'status': 'COMPLETED',
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    }
]

in_memory_atm = {
    'TXN-1001': {'atm_location': 'Connaught Place South ATM #04', 'card_last4': '4821'}
}

in_memory_upi_payroll = {
    'TXN-1002': {'channel_mode': 'UPI', 'reference_id': 'priya@okhdfcbank'},
    'TXN-1004': {'channel_mode': 'PAYROLL', 'reference_id': 'BATCH-DEC-TECHCORP-01'}
}

# ----------------------------------------------------------------------------
# Database Connection & Initialization
# ----------------------------------------------------------------------------
def init_mysql():
    global is_mysql_connected, mysql_pool
    if not HAS_CONNECTOR:
        print("[MySQL] mysql-connector-python not found. In-memory engine active.")
        return False

    try:
        # Create database if it does not exist
        srv = mysql.connector.connect(host=DB_HOST, port=DB_PORT, user=DB_USER, password=DB_PASSWORD)
        cur = srv.cursor()
        cur.execute(f"CREATE DATABASE IF NOT EXISTS `{DB_NAME}`;")
        cur.close()
        srv.close()

        # Create connection pool
        mysql_pool = pooling.MySQLConnectionPool(
            pool_name="bankos_pool",
            pool_size=5,
            host=DB_HOST,
            port=DB_PORT,
            user=DB_USER,
            password=DB_PASSWORD,
            database=DB_NAME
        )

        # Run schema.sql
        schema_file = os.path.join(os.path.dirname(__file__), "schema.sql")
        if os.path.exists(schema_file):
            conn = mysql_pool.get_connection()
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

        is_mysql_connected = True
        print(f"[MySQL] ✓ Connected to MySQL ({DB_HOST}:{DB_PORT}/{DB_NAME}) with pooling.")
        return True
    except Exception as e:
        is_mysql_connected = False
        print(f"[MySQL] Notice: MySQL offline ({e}). Using In-Memory ACID engine.")
        return False

def get_db():
    if is_mysql_connected and mysql_pool:
        try:
            return mysql_pool.get_connection()
        except Exception:
            return None
    return None

# ----------------------------------------------------------------------------
# Mandatory SQL JOINs for Ledger
# ----------------------------------------------------------------------------
def query_joined_ledger(category=None, search=None, limit=100):
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
                'created_at': str(t['created_at']),
                'atm_location': atm_row.get('atm_location'),
                'card_last4': atm_row.get('card_last4'),
                'channel_mode': up_row.get('channel_mode'),
                'reference_id': up_row.get('reference_id')
            }

            if search:
                s_l = search.lower()
                matches = (
                    s_l in row['txn_id'].lower() or
                    s_l in row['sender_name'].lower() or
                    s_l in row['receiver_name'].lower() or
                    (row['atm_location'] and s_l in row['atm_location'].lower()) or
                    (row['reference_id'] and s_l in row['reference_id'].lower())
                )
                if not matches:
                    continue

            res.append(row)
            if len(res) >= limit:
                break
        return res

def query_accounts():
    conn = get_db()
    if conn:
        try:
            cur = conn.cursor(dictionary=True)
            cur.execute("""
                SELECT 
                    a.account_id,
                    a.customer_name,
                    a.account_type,
                    b.current_balance
                FROM accounts a
                INNER JOIN balance b ON a.account_id = b.account_id
                ORDER BY a.account_id ASC;
            """)
            rows = cur.fetchall()
            cur.close()
            conn.close()
            for r in rows:
                r['current_balance'] = float(r['current_balance'])
            return rows
        except Exception:
            pass

    with db_lock:
        return [
            {
                'account_id': aid,
                'customer_name': acc['customer_name'],
                'account_type': acc['account_type'],
                'current_balance': float(in_memory_balances.get(aid, Decimal('0.00')))
            }
            for aid, acc in sorted(in_memory_accounts.items())
        ]

# ----------------------------------------------------------------------------
# ACID Balance Transfer Execution
# ----------------------------------------------------------------------------
def execute_acid_transfer(txn_id, sender_id, receiver_id, amount):
    amt = Decimal(str(amount))
    conn = get_db()
    if conn:
        try:
            conn.autocommit = False
            cur = conn.cursor(dictionary=True)

            # START TRANSACTION & SELECT FOR UPDATE
            cur.execute("SELECT current_balance FROM balance WHERE account_id = %s FOR UPDATE;", (sender_id,))
            s_row = cur.fetchone()
            if not s_row:
                conn.rollback()
                cur.close()
                conn.close()
                return False

            sender_bal = Decimal(str(s_row['current_balance']))
            # Deduct balance if sufficient funds (or if sender is vault)
            if sender_id != 'ACC1006' and sender_bal < amt:
                conn.rollback()
                cur.close()
                conn.close()
                return False

            # Deduct sender, credit receiver
            cur.execute("UPDATE balance SET current_balance = current_balance - %s WHERE account_id = %s;", (amt, sender_id))
            cur.execute("UPDATE balance SET current_balance = current_balance + %s WHERE account_id = %s;", (amt, receiver_id))
            cur.execute("UPDATE transactions SET status = 'COMPLETED' WHERE txn_id = %s;", (txn_id,))

            conn.commit()
            cur.close()
            conn.close()
            return True
        except Exception as e:
            try:
                conn.rollback()
                conn.close()
            except Exception:
                pass
            return False

    with db_lock:
        s_bal = in_memory_balances.get(sender_id, Decimal('0.00'))
        if sender_id != 'ACC1006' and s_bal < amt:
            return False
        in_memory_balances[sender_id] -= amt
        in_memory_balances[receiver_id] = in_memory_balances.get(receiver_id, Decimal('0.00')) + amt
        for t in in_memory_transactions:
            if t['txn_id'] == txn_id:
                t['status'] = 'COMPLETED'
                break
        return True

# ----------------------------------------------------------------------------
# C++ Scheduler Subprocess & IPC Bridge
# ----------------------------------------------------------------------------
cpp_process = None

def ensure_cpp_scheduler_running():
    global cpp_process
    # Check if C++ scheduler is already answering
    try:
        req = urllib.request.Request(f"{CPP_SCHEDULER_URL}/state")
        with urllib.request.urlopen(req, timeout=0.8) as resp:
            if resp.status == 200:
                return True
    except Exception:
        pass

    # Compile if binary does not exist
    binary = "./scheduler"
    if not os.path.exists(binary):
        print("[C++ Engine] Compiling scheduler.cpp -> ./scheduler...")
        subprocess.run(["g++", "-O2", "-std=c++17", "scheduler.cpp", "-o", "scheduler", "-pthread"], check=True)

    # Launch binary as subprocess
    print(f"[C++ Engine] Starting ./scheduler on port {CPP_SCHEDULER_PORT}...")
    cpp_process = subprocess.Popen([binary, str(CPP_SCHEDULER_PORT)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.4)
    return True

# Track the most recent user demo test transaction
tracked_test_txn = None

def get_cpp_scheduler_state():
    try:
        req = urllib.request.Request(f"{CPP_SCHEDULER_URL}/state")
        with urllib.request.urlopen(req, timeout=1.0) as resp:
            data = json.loads(resp.read().decode('utf-8'))
            return data
    except Exception as e:
        return {
            "active_process": None,
            "queue_1": [],
            "queue_2": [],
            "queue_3": [],
            "completed": [],
            "stats": {"total_admitted": 0, "total_completed": 0}
        }

def inject_into_cpp_scheduler(txn_id, sender, receiver, txn_type, amount, is_test):
    query = urllib.parse.urlencode({
        "txn_id": txn_id,
        "sender_id": sender,
        "receiver_id": receiver,
        "txn_type": txn_type,
        "amount": amount,
        "is_test": "1" if is_test else "0"
    })
    url = f"{CPP_SCHEDULER_URL}/inject?{query}"
    try:
        req = urllib.request.Request(url, method="POST")
        with urllib.request.urlopen(req, timeout=1.2) as resp:
            return json.loads(resp.read().decode('utf-8'))
    except Exception as e:
        # Fallback local calculation
        burst = 1 if txn_type == 'ATM' else (2 if txn_type == 'UPI' else (3 if amount <= 10000 else (7 if txn_type == 'PAYROLL' else 5)))
        queue = "Queue 1 (Priority)" if txn_type in ('ATM', 'UPI') else ("Queue 2 (SJF)" if burst <= 3 else "Queue 3 (FCFS)")
        return {
            "txn_id": txn_id,
            "sender_id": sender,
            "receiver_id": receiver,
            "txn_type": txn_type,
            "amount": amount,
            "auto_burst_time": burst,
            "assigned_queue": queue,
            "status": "QUEUED",
            "is_test": is_test
        }

# ----------------------------------------------------------------------------
# Background Worker: Syncs C++ Completed Transactions to MySQL ACID Balances
# ----------------------------------------------------------------------------
processed_completed_ids = set()

def background_db_sync_loop():
    global tracked_test_txn
    while True:
        try:
            state = get_cpp_scheduler_state()
            completed_list = state.get("completed", [])

            for p in completed_list:
                tid = p["txn_id"]
                if tid not in processed_completed_ids:
                    processed_completed_ids.add(tid)
                    # Execute ACID balance transfer
                    execute_acid_transfer(tid, p["sender_id"], p["receiver_id"], p["amount"])

                    # Update tracked test status if applicable
                    if tracked_test_txn and tracked_test_txn["txn_id"] == tid:
                        tracked_test_txn["phase"] = "Committed to MySQL"
                        tracked_test_txn["status"] = "COMPLETED"
                        tracked_test_txn["progress"] = 100

            # Update tracked test if it is running in C++
            active = state.get("active_process")
            if tracked_test_txn:
                if active and active["txn_id"] == tracked_test_txn["txn_id"]:
                    tracked_test_txn["phase"] = "Running in C++ CPU"
                    tracked_test_txn["status"] = "RUNNING"
                    tracked_test_txn["progress"] = active.get("progress_pct", 50)
                elif tracked_test_txn["status"] == "QUEUED":
                    tracked_test_txn["phase"] = f"Queued in {tracked_test_txn['assigned_queue']}"

        except Exception:
            pass
        time.sleep(0.3)

# ----------------------------------------------------------------------------
# Background Simulation Traffic Generator
# ----------------------------------------------------------------------------
is_traffic_active = True

def background_traffic_generator():
    acc_keys = ['ACC1001', 'ACC1002', 'ACC1003', 'ACC1004', 'ACC1005', 'ACC1006']
    while True:
        if not is_traffic_active:
            time.sleep(1.0)
            continue

        try:
            state = get_cpp_scheduler_state()
            q_total = len(state.get("queue_1", [])) + len(state.get("queue_2", [])) + len(state.get("queue_3", []))

            if q_total < 8:
                roll = random.random()
                txn_id = f"TXN-{random.randint(2000, 9999)}"

                if roll < 0.45:
                    # UPI -> Queue 1 (2s)
                    sender = random.choice(['ACC1001', 'ACC1002', 'ACC1003', 'ACC1004'])
                    receiver = random.choice([a for a in acc_keys if a != sender and a != 'ACC1006'])
                    amt = round(random.uniform(250.0, 4800.0), 2)
                    ttype = 'UPI'
                    channel_meta = {'channel_mode': 'UPI', 'reference_id': f"{sender.lower()}@upi"}

                elif roll < 0.70:
                    # ATM -> Queue 1 (1s)
                    sender = random.choice(['ACC1001', 'ACC1002', 'ACC1003', 'ACC1004'])
                    receiver = 'ACC1006'
                    amt = float(random.choice([500, 1000, 2000, 4000]))
                    ttype = 'ATM'
                    channel_meta = {'atm_location': 'Cyber City Branch ATM #02', 'card_last4': str(random.randint(1000, 9999))}

                elif roll < 0.90:
                    # STANDARD -> Queue 2 (3s if <= 10k)
                    sender = random.choice(['ACC1001', 'ACC1003', 'ACC1004'])
                    receiver = random.choice([a for a in acc_keys if a != sender and a != 'ACC1006'])
                    amt = round(random.uniform(2000.0, 9500.0), 2)
                    ttype = 'STANDARD'
                    channel_meta = {}

                else:
                    # PAYROLL -> Queue 3 (7s)
                    sender = 'ACC1005'
                    receiver = random.choice(['ACC1001', 'ACC1002', 'ACC1004'])
                    amt = round(random.uniform(40000.0, 80000.0), 2)
                    ttype = 'PAYROLL'
                    channel_meta = {'channel_mode': 'PAYROLL', 'reference_id': 'BATCH-CORP-PAYROLL'}

                # Inject into C++ MLFQ
                proc = inject_into_cpp_scheduler(txn_id, sender, receiver, ttype, amt, False)

                # Record in Database
                record_transaction_in_db(proc, channel_meta)

            time.sleep(random.uniform(2.5, 4.0))

        except Exception:
            time.sleep(1.0)

def record_transaction_in_db(proc, channel_meta):
    tid = proc['txn_id']
    sender = proc['sender_id']
    receiver = proc['receiver_id']
    ttype = proc['txn_type']
    amt = Decimal(str(proc['amount']))
    burst = proc['auto_burst_time']
    queue = proc['assigned_queue']

    conn = get_db()
    if conn:
        try:
            cur = conn.cursor()
            cur.execute("""
                INSERT INTO transactions (txn_id, sender_id, receiver_id, txn_type, amount, auto_burst_time, assigned_queue, status, created_at)
                VALUES (%s, %s, %s, %s, %s, %s, %s, 'QUEUED', NOW());
            """, (tid, sender, receiver, ttype, amt, burst, queue))

            if ttype == 'ATM':
                loc = channel_meta.get('atm_location', 'Connaught Place South ATM #04')
                card = channel_meta.get('card_last4', '1234')
                cur.execute("INSERT INTO atm_transactions (txn_id, atm_location, card_last4) VALUES (%s, %s, %s);", (tid, loc, card))
            elif ttype in ('UPI', 'PAYROLL'):
                mode = channel_meta.get('channel_mode', ttype)
                ref = channel_meta.get('reference_id', f"{sender.lower()}@channel")
                cur.execute("INSERT INTO upi_payroll_transactions (txn_id, channel_mode, reference_id) VALUES (%s, %s, %s);", (tid, mode, ref))

            conn.commit()
            cur.close()
            conn.close()
            return
        except Exception:
            pass

    with db_lock:
        in_memory_transactions.append({
            'txn_id': tid, 'sender_id': sender, 'receiver_id': receiver,
            'txn_type': ttype, 'amount': amt, 'auto_burst_time': burst,
            'assigned_queue': queue, 'status': 'QUEUED',
            'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        })
        if ttype == 'ATM':
            in_memory_atm[tid] = {
                'atm_location': channel_meta.get('atm_location', 'Branch ATM Cash Kiosk'),
                'card_last4': channel_meta.get('card_last4', '5678')
            }
        elif ttype in ('UPI', 'PAYROLL'):
            in_memory_upi_payroll[tid] = {
                'channel_mode': ttype,
                'reference_id': channel_meta.get('reference_id', f"{sender.lower()}@vpa")
            }

# ----------------------------------------------------------------------------
# Flask Web Application & REST API
# ----------------------------------------------------------------------------
app = Flask(__name__, template_folder="templates")

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/api/inject", methods=["POST"])
def api_inject_transaction():
    global tracked_test_txn
    data = request.get_json(force=True) or {}
    sender_id = data.get("sender_id", "ACC1001").strip()
    receiver_id = data.get("receiver_id", "ACC1002").strip()
    txn_type = data.get("txn_type", "UPI").strip().upper()
    amount = float(data.get("amount", 2000.0))

    # Clean unique Txn ID for test
    txn_id = f"TEST-{random.randint(100, 999)}"

    # Inject into C++ Scheduler (C++ auto-computes auto_burst_time and assigned_queue)
    proc = inject_into_cpp_scheduler(txn_id, sender_id, receiver_id, txn_type, amount, is_test=True)

    # Channel metadata
    channel_meta = {}
    if txn_type == "ATM":
        channel_meta = {'atm_location': 'Connaught Place South ATM #04', 'card_last4': '9988'}
    elif txn_type == "UPI":
        channel_meta = {'channel_mode': 'UPI', 'reference_id': f"{sender_id.lower()}@demo"}
    elif txn_type == "PAYROLL":
        channel_meta = {'channel_mode': 'PAYROLL', 'reference_id': 'BATCH-TECHCORP-DEMO'}

    # Save to Database as QUEUED
    record_transaction_in_db(proc, channel_meta)

    # Set as active tracked test transaction
    tracked_test_txn = {
        "txn_id": proc["txn_id"],
        "sender_id": proc["sender_id"],
        "receiver_id": proc["receiver_id"],
        "txn_type": proc["txn_type"],
        "amount": proc["amount"],
        "auto_burst_time": proc["auto_burst_time"],
        "assigned_queue": proc["assigned_queue"],
        "phase": f"Queued in {proc['assigned_queue']}",
        "status": "QUEUED",
        "progress": 0
    }

    return jsonify({"status": "success", "process": proc}), 201

@app.route("/api/state", methods=["GET"])
def api_scheduler_state():
    state = get_cpp_scheduler_state()
    state["tracked_test_txn"] = tracked_test_txn
    state["traffic_active"] = is_traffic_active
    state["mysql_connected"] = is_mysql_connected
    return jsonify(state)

@app.route("/api/ledger", methods=["GET"])
def api_ledger():
    cat = request.args.get("category", "ALL")
    search = request.args.get("search", "").strip()
    rows = query_joined_ledger(category=cat, search=search, limit=100)
    return jsonify({"status": "success", "transactions": rows})

@app.route("/api/accounts", methods=["GET"])
def api_accounts():
    accs = query_accounts()
    return jsonify({"status": "success", "accounts": accs})

@app.route("/api/traffic/toggle", methods=["POST"])
def api_traffic_toggle():
    global is_traffic_active
    is_traffic_active = not is_traffic_active
    return jsonify({"status": "success", "traffic_active": is_traffic_active})

@app.route("/api/reset", methods=["POST"])
def api_reset():
    global tracked_test_txn, processed_completed_ids
    tracked_test_txn = None
    processed_completed_ids.clear()

    with db_lock:
        in_memory_balances['ACC1001'] = Decimal('150000.00')
        in_memory_balances['ACC1002'] = Decimal('85000.00')
        in_memory_balances['ACC1003'] = Decimal('240000.00')
        in_memory_balances['ACC1004'] = Decimal('95000.00')
        in_memory_balances['ACC1005'] = Decimal('1500000.00')
        in_memory_balances['ACC1006'] = Decimal('800000.00')

    conn = get_db()
    if conn:
        try:
            cur = conn.cursor()
            cur.execute("UPDATE balance SET current_balance = 150000.00 WHERE account_id = 'ACC1001';")
            cur.execute("UPDATE balance SET current_balance = 85000.00 WHERE account_id = 'ACC1002';")
            cur.execute("UPDATE balance SET current_balance = 240000.00 WHERE account_id = 'ACC1003';")
            cur.execute("UPDATE balance SET current_balance = 95000.00 WHERE account_id = 'ACC1004';")
            cur.execute("UPDATE balance SET current_balance = 1500000.00 WHERE account_id = 'ACC1005';")
            cur.execute("UPDATE balance SET current_balance = 800000.00 WHERE account_id = 'ACC1006';")
            conn.commit()
            cur.close()
            conn.close()
        except Exception:
            pass

    return jsonify({"status": "success", "message": "Reset completed."})

# ----------------------------------------------------------------------------
# Startup & Main Execution
# ----------------------------------------------------------------------------
def find_available_port(preferred=5000):
    import socket
    for p in [preferred, 5001, 5050, 8080]:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                s.bind(('0.0.0.0', p))
                return p
            except OSError:
                continue
    return preferred

if __name__ == "__main__":
    init_mysql()
    ensure_cpp_scheduler_running()

    # Start background synchronization threads
    t_sync = threading.Thread(target=background_db_sync_loop, daemon=True)
    t_sync.start()

    t_traffic = threading.Thread(target=background_traffic_generator, daemon=True)
    t_traffic.start()

    port = find_available_port(5000)
    print("====================================================================")
    print(f"   BANKOS MIDDLEWARE ONLINE: http://127.0.0.1:{port}               ")
    print("   Bridging C++17 MLFQ Engine & MySQL Relational Ledger             ")
    print("====================================================================")
    app.run(host="0.0.0.0", port=port, debug=False, threaded=True)
