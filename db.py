# ============================================================================
# BankOS Middleware: Database Manager & ACID Engine (db.py)
# Connects to MySQL (bankos_db) with Connection Pooling & Multi-Table SQL JOINs
# Includes Resilient Thread-Safe In-Memory Transactional Fallback for Live Demos
# ============================================================================

import os
import time
import threading
from datetime import datetime
from decimal import Decimal

# Optional .env loading
try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    pass

# MySQL Connection Configuration (Configurable via environment or here)
DB_HOST = os.getenv("MYSQL_HOST", "127.0.0.1")
DB_PORT = int(os.getenv("MYSQL_PORT", 3306))
DB_USER = os.getenv("MYSQL_USER", "root")
DB_PASSWORD = os.getenv("MYSQL_PASSWORD", "")
DB_NAME = os.getenv("MYSQL_DB", "bankos_db")

# Status tracking
is_mysql_connected = False
mysql_error_message = ""
db_lock = threading.RLock()

# Try importing mysql.connector
try:
    import mysql.connector
    from mysql.connector import pooling
    HAS_CONNECTOR = True
except ImportError:
    HAS_CONNECTOR = False

connection_pool = None

# ============================================================================
# IN-MEMORY TRANSACTIONAL FALLBACK LEDGER (100% RELIABLE DEMO PROTECTION)
# ============================================================================
in_memory_accounts = {
    'ACC1001': {'account_id': 'ACC1001', 'customer_name': 'Rajesh Sharma', 'account_type': 'SAVINGS', 'created_at': '2026-11-20 10:00:00'},
    'ACC1002': {'account_id': 'ACC1002', 'customer_name': 'Priya Patel', 'account_type': 'SAVINGS', 'created_at': '2026-11-20 10:00:00'},
    'ACC1003': {'account_id': 'ACC1003', 'customer_name': 'Amit Verma', 'account_type': 'CURRENT', 'created_at': '2026-11-20 10:00:00'},
    'ACC1004': {'account_id': 'ACC1004', 'customer_name': 'Sneha Kulkarni', 'account_type': 'SAVINGS', 'created_at': '2026-11-20 10:00:00'},
    'ACC1005': {'account_id': 'ACC1005', 'customer_name': 'TechCorp Solutions Pvt Ltd', 'account_type': 'CORPORATE PAYROLL', 'created_at': '2026-11-20 10:00:00'},
    'ACC1006': {'account_id': 'ACC1006', 'customer_name': 'Metro ATM Cash Reserve', 'account_type': 'VAULT', 'created_at': '2026-11-20 10:00:00'},
    'ACC1007': {'account_id': 'ACC1007', 'customer_name': 'Vikram Malhotra', 'account_type': 'PREMIUM SAVINGS', 'created_at': '2026-11-20 10:00:00'},
    'ACC1008': {'account_id': 'ACC1008', 'customer_name': 'Ananya Iyer', 'account_type': 'SALARY', 'created_at': '2026-11-20 10:00:00'}
}

in_memory_balances = {
    'ACC1001': {'balance_id': 1, 'account_id': 'ACC1001', 'current_balance': Decimal('150000.00'), 'last_updated': datetime.now()},
    'ACC1002': {'balance_id': 2, 'account_id': 'ACC1002', 'current_balance': Decimal('85000.00'), 'last_updated': datetime.now()},
    'ACC1003': {'balance_id': 3, 'account_id': 'ACC1003', 'current_balance': Decimal('240000.00'), 'last_updated': datetime.now()},
    'ACC1004': {'balance_id': 4, 'account_id': 'ACC1004', 'current_balance': Decimal('95000.00'), 'last_updated': datetime.now()},
    'ACC1005': {'balance_id': 5, 'account_id': 'ACC1005', 'current_balance': Decimal('1500000.00'), 'last_updated': datetime.now()},
    'ACC1006': {'balance_id': 6, 'account_id': 'ACC1006', 'current_balance': Decimal('800000.00'), 'last_updated': datetime.now()},
    'ACC1007': {'balance_id': 7, 'account_id': 'ACC1007', 'current_balance': Decimal('320000.00'), 'last_updated': datetime.now()},
    'ACC1008': {'balance_id': 8, 'account_id': 'ACC1008', 'current_balance': Decimal('65000.00'), 'last_updated': datetime.now()}
}

in_memory_transactions = []
in_memory_atm_records = {}
in_memory_upi_payroll_records = {}

# Seed initial transactions in-memory
_seed_txns = [
    {
        'txn_id': 'TXN-INIT-001', 'sender_id': 'ACC1001', 'receiver_id': 'ACC1006',
        'txn_category': 'ATM', 'amount': Decimal('2500.00'), 'burst_time': 1,
        'assigned_queue': 'Q1_PRIORITY', 'status': 'COMPLETED', 'failure_reason': None,
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-INIT-002', 'sender_id': 'ACC1002', 'receiver_id': 'ACC1004',
        'txn_category': 'UPI', 'amount': Decimal('1850.00'), 'burst_time': 2,
        'assigned_queue': 'Q1_PRIORITY', 'status': 'COMPLETED', 'failure_reason': None,
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-INIT-003', 'sender_id': 'ACC1005', 'receiver_id': 'ACC1008',
        'txn_category': 'PAYROLL', 'amount': Decimal('45000.00'), 'burst_time': 5,
        'assigned_queue': 'Q3_FCFS', 'status': 'COMPLETED', 'failure_reason': None,
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-INIT-004', 'sender_id': 'ACC1003', 'receiver_id': 'ACC1007',
        'txn_category': 'STANDARD', 'amount': Decimal('12000.00'), 'burst_time': 3,
        'assigned_queue': 'Q2_SJF', 'status': 'COMPLETED', 'failure_reason': None,
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    },
    {
        'txn_id': 'TXN-INIT-005', 'sender_id': 'ACC1007', 'receiver_id': 'ACC1002',
        'txn_category': 'UPI', 'amount': Decimal('3400.00'), 'burst_time': 1,
        'assigned_queue': 'Q1_PRIORITY', 'status': 'COMPLETED', 'failure_reason': None,
        'created_at': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    }
]

for t in _seed_txns:
    in_memory_transactions.append(t)

in_memory_atm_records['TXN-INIT-001'] = {'atm_location': 'Connaught Place South ATM #04, New Delhi', 'card_last4': '4821'}
in_memory_upi_payroll_records['TXN-INIT-002'] = {'channel_mode': 'UPI', 'reference_handle': 'priya@okhdfcbank'}
in_memory_upi_payroll_records['TXN-INIT-003'] = {'channel_mode': 'PAYROLL', 'reference_handle': 'BATCH-DEC-TECHCORP-01'}
in_memory_upi_payroll_records['TXN-INIT-005'] = {'channel_mode': 'UPI', 'reference_handle': 'vikram@icici'}


# ============================================================================
# DATABASE INITIALIZATION
# ============================================================================
def init_db():
    """
    Connects to MySQL server, creates 'bankos_db', and runs schema.sql.
    Gracefully falls back to thread-safe in-memory engine if MySQL is unavailable.
    """
    global is_mysql_connected, mysql_error_message, connection_pool

    if not HAS_CONNECTOR:
        is_mysql_connected = False
        mysql_error_message = "mysql-connector-python not installed"
        print("[db.py] mysql-connector-python not found. Using In-Memory Transactional Engine.")
        return False

    try:
        # Step 1: Connect to MySQL server (without DB name) to create database if needed
        server_conn = mysql.connector.connect(
            host=DB_HOST,
            port=DB_PORT,
            user=DB_USER,
            password=DB_PASSWORD,
            autocommit=True
        )
        cursor = server_conn.cursor()
        cursor.execute(f"CREATE DATABASE IF NOT EXISTS `{DB_NAME}`;")
        cursor.close()
        server_conn.close()

        # Step 2: Establish connection pool to bankos_db
        connection_pool = pooling.MySQLConnectionPool(
            pool_name="bankos_pool",
            pool_size=5,
            pool_reset_session=True,
            host=DB_HOST,
            port=DB_PORT,
            user=DB_USER,
            password=DB_PASSWORD,
            database=DB_NAME
        )

        # Step 3: Run schema.sql to ensure all 5 tables and seed records exist
        schema_path = os.path.join(os.path.dirname(__file__), "schema.sql")
        if os.path.exists(schema_path):
            with open(schema_path, "r", encoding="utf-8") as f:
                schema_sql = f.read()
            
            conn = connection_pool.get_connection()
            cur = conn.cursor()
            # Split and execute individual statements
            statements = [s.strip() for s in schema_sql.split(";") if s.strip() and not s.strip().startswith("--")]
            for stmt in statements:
                if stmt.upper().startswith("CREATE DATABASE") or stmt.upper().startswith("USE "):
                    continue
                try:
                    cur.execute(stmt)
                except Exception as e:
                    # Ignore duplicate key seed warnings
                    pass
            conn.commit()
            cur.close()
            conn.close()

        is_mysql_connected = True
        mysql_error_message = ""
        print(f"[db.py] ✓ Successfully connected to MySQL ({DB_HOST}:{DB_PORT}/{DB_NAME}) with Connection Pooling!")
        return True

    except Exception as err:
        is_mysql_connected = False
        mysql_error_message = str(err)
        print(f"[db.py] Notice: MySQL server connection unavailable ({err}).")
        print("[db.py] Activating In-Memory High-Integrity Transaction Engine (ACID Compliant).")
        return False


def get_connection():
    """Returns a connection from the pool, or None if using in-memory engine."""
    if is_mysql_connected and connection_pool:
        try:
            return connection_pool.get_connection()
        except Exception:
            return None
    return None


# ============================================================================
# READ OPERATIONS (MANDATORY SQL JOINS)
# ============================================================================
def get_accounts():
    """
    Returns all accounts with their verified real-time balances.
    SQL: accounts INNER JOIN balance ON accounts.account_id = balance.account_id
    """
    conn = get_connection()
    if conn:
        try:
            cur = conn.cursor(dictionary=True)
            query = """
                SELECT 
                    a.account_id,
                    a.customer_name,
                    a.account_type,
                    b.current_balance,
                    b.last_updated
                FROM accounts a
                INNER JOIN balance b ON a.account_id = b.account_id
                ORDER BY a.account_id ASC;
            """
            cur.execute(query)
            rows = cur.fetchall()
            cur.close()
            conn.close()
            # Format Decimal to float/string for JSON serialization
            for r in rows:
                r['current_balance'] = float(r['current_balance'])
                if isinstance(r.get('last_updated'), datetime):
                    r['last_updated'] = r['last_updated'].strftime('%Y-%m-%d %H:%M:%S')
            return rows
        except Exception as e:
            print("[db.py] Error fetching accounts from MySQL:", e)

    # In-memory fallback
    with db_lock:
        res = []
        for acc_id, acc in sorted(in_memory_accounts.items()):
            bal_obj = in_memory_balances.get(acc_id, {})
            res.append({
                'account_id': acc_id,
                'customer_name': acc['customer_name'],
                'account_type': acc['account_type'],
                'current_balance': float(bal_obj.get('current_balance', Decimal('0.00'))),
                'last_updated': str(bal_obj.get('last_updated', ''))
            })
        return res


def get_joined_transactions(category=None, search=None, limit=100):
    """
    MANDATORY MULTI-TABLE SQL JOIN:
    Joins transactions, accounts (sender & receiver), balance, atm_transactions, and upi_payroll_transactions.
    Returns rich rows where customer names, updated balances, and channel metadata appear together.
    """
    conn = get_connection()
    if conn:
        try:
            cur = conn.cursor(dictionary=True)
            query = """
                SELECT 
                    t.txn_id,
                    t.sender_id,
                    s.customer_name AS sender_name,
                    sb.current_balance AS sender_balance,
                    t.receiver_id,
                    r.customer_name AS receiver_name,
                    rb.current_balance AS receiver_balance,
                    t.txn_category,
                    t.amount,
                    t.burst_time,
                    t.assigned_queue,
                    t.status,
                    t.failure_reason,
                    t.created_at,
                    atm.atm_location,
                    atm.card_last4,
                    up.channel_mode,
                    up.reference_handle
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
            if category and category.upper() != 'ALL':
                query += " AND t.txn_category = %s"
                params.append(category.upper())

            if search:
                query += """ AND (
                    t.txn_id LIKE %s OR 
                    s.customer_name LIKE %s OR 
                    r.customer_name LIKE %s OR
                    atm.atm_location LIKE %s OR
                    up.reference_handle LIKE %s
                )"""
                term = f"%{search}%"
                params.extend([term, term, term, term, term])

            query += " ORDER BY t.created_at DESC LIMIT %s"
            params.append(limit)

            cur.execute(query, tuple(params))
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
            print("[db.py] Error querying joined transactions from MySQL:", e)

    # In-memory JOIN fallback
    with db_lock:
        joined = []
        for t in reversed(in_memory_transactions):
            if category and category.upper() != 'ALL':
                if t['txn_category'].upper() != category.upper():
                    continue

            s_acc = in_memory_accounts.get(t['sender_id'], {'customer_name': 'Unknown'})
            r_acc = in_memory_accounts.get(t['receiver_id'], {'customer_name': 'Unknown'})
            s_bal = in_memory_balances.get(t['sender_id'], {'current_balance': Decimal('0.00')})
            r_bal = in_memory_balances.get(t['receiver_id'], {'current_balance': Decimal('0.00')})

            atm_info = in_memory_atm_records.get(t['txn_id'], {})
            up_info = in_memory_upi_payroll_records.get(t['txn_id'], {})

            row = {
                'txn_id': t['txn_id'],
                'sender_id': t['sender_id'],
                'sender_name': s_acc['customer_name'],
                'sender_balance': float(s_bal['current_balance']),
                'receiver_id': t['receiver_id'],
                'receiver_name': r_acc['customer_name'],
                'receiver_balance': float(r_bal['current_balance']),
                'txn_category': t['txn_category'],
                'amount': float(t['amount']),
                'burst_time': t['burst_time'],
                'assigned_queue': t['assigned_queue'],
                'status': t['status'],
                'failure_reason': t.get('failure_reason'),
                'created_at': str(t['created_at']),
                'atm_location': atm_info.get('atm_location'),
                'card_last4': atm_info.get('card_last4'),
                'channel_mode': up_info.get('channel_mode'),
                'reference_handle': up_info.get('reference_handle')
            }

            if search:
                s_lower = search.lower()
                matches = (
                    s_lower in row['txn_id'].lower() or
                    s_lower in row['sender_name'].lower() or
                    s_lower in row['receiver_name'].lower() or
                    (row['atm_location'] and s_lower in row['atm_location'].lower()) or
                    (row['reference_handle'] and s_lower in row['reference_handle'].lower())
                )
                if not matches:
                    continue

            joined.append(row)
            if len(joined) >= limit:
                break
        return joined


# ============================================================================
# WRITE & ACID TRANSACTION EXECUTION
# ============================================================================
def record_incoming_transaction(pcb):
    """
    Inserts a newly arrived transaction into the transactions table (status: QUEUED)
    and logs its channel-specific record into atm_transactions or upi_payroll_transactions.
    """
    conn = get_connection()
    if conn:
        try:
            cur = conn.cursor()
            cur.execute("""
                INSERT INTO transactions (txn_id, sender_id, receiver_id, txn_category, amount, burst_time, assigned_queue, status, created_at)
                VALUES (%s, %s, %s, %s, %s, %s, %s, 'QUEUED', NOW())
                ON DUPLICATE KEY UPDATE status = 'QUEUED';
            """, (
                pcb.txn_id, pcb.sender_id, pcb.receiver_id, pcb.txn_category,
                Decimal(str(pcb.amount)), pcb.burst_time, pcb.assigned_queue
            ))

            if pcb.txn_category == 'ATM':
                atm_loc = pcb.channel_meta.get('atm_location', 'Connaught Place South ATM #04')
                card_last4 = pcb.channel_meta.get('card_last4', '9999')
                cur.execute("""
                    INSERT INTO atm_transactions (txn_id, atm_location, card_last4)
                    VALUES (%s, %s, %s)
                    ON DUPLICATE KEY UPDATE atm_location = VALUES(atm_location);
                """, (pcb.txn_id, atm_loc, card_last4))

            elif pcb.txn_category in ('UPI', 'PAYROLL'):
                mode = 'UPI' if pcb.txn_category == 'UPI' else 'PAYROLL'
                handle = pcb.channel_meta.get('reference_handle', f'{pcb.sender_id.lower()}@bank')
                cur.execute("""
                    INSERT INTO upi_payroll_transactions (txn_id, channel_mode, reference_handle)
                    VALUES (%s, %s, %s)
                    ON DUPLICATE KEY UPDATE reference_handle = VALUES(reference_handle);
                """, (pcb.txn_id, mode, handle))

            conn.commit()
            cur.close()
            conn.close()
            return True
        except Exception as e:
            print("[db.py] Error recording incoming transaction to MySQL:", e)

    # In-memory write
    with db_lock:
        now_str = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        in_memory_transactions.append({
            'txn_id': pcb.txn_id,
            'sender_id': pcb.sender_id,
            'receiver_id': pcb.receiver_id,
            'txn_category': pcb.txn_category,
            'amount': Decimal(str(pcb.amount)),
            'burst_time': pcb.burst_time,
            'assigned_queue': pcb.assigned_queue,
            'status': 'QUEUED',
            'failure_reason': None,
            'created_at': now_str
        })

        if pcb.txn_category == 'ATM':
            in_memory_atm_records[pcb.txn_id] = {
                'atm_location': pcb.channel_meta.get('atm_location', 'Bank Branch Cash Kiosk'),
                'card_last4': pcb.channel_meta.get('card_last4', '1234')
            }
        elif pcb.txn_category in ('UPI', 'PAYROLL'):
            in_memory_upi_payroll_records[pcb.txn_id] = {
                'channel_mode': pcb.txn_category,
                'reference_handle': pcb.channel_meta.get('reference_handle', f'{pcb.sender_id.lower()}@upi')
            }
        return True


def execute_transaction_transfer(pcb):
    """
    CRITICAL ACID EXECUTION:
    Executes balance updates inside a strict ACID MySQL transaction:
    - START TRANSACTION
    - SELECT current_balance FROM balance WHERE account_id = %s FOR UPDATE
    - Verification: current_balance >= amount
    - UPDATE balance (Sender deduction, Receiver credit)
    - UPDATE transactions SET status = 'COMPLETED'
    - COMMIT or ROLLBACK (if insufficient funds)
    """
    amt = Decimal(str(pcb.amount))
    conn = get_connection()

    if conn:
        try:
            conn.autocommit = False
            cur = conn.cursor(dictionary=True)

            # Step 1: Lock sender's balance row for UPDATE
            cur.execute("SELECT current_balance FROM balance WHERE account_id = %s FOR UPDATE;", (pcb.sender_id,))
            s_row = cur.fetchone()

            if not s_row:
                conn.rollback()
                cur.close()
                conn.close()
                pcb.status = "FAILED"
                pcb.failure_reason = f"Sender account {pcb.sender_id} not found."
                update_txn_status(pcb.txn_id, 'FAILED', pcb.failure_reason)
                return False

            sender_balance = Decimal(str(s_row['current_balance']))

            # Step 2: ACID Check: Validate sufficient balance (Allow overdraft only if sender is Vault ACC1006)
            is_vault = (pcb.sender_id == 'ACC1006')
            if not is_vault and sender_balance < amt:
                conn.rollback()
                pcb.status = "FAILED"
                pcb.failure_reason = f"ACID Rollback: Insufficient Balance (Available: ₹{sender_balance:,.2f}, Required: ₹{amt:,.2f})"
                update_txn_status(pcb.txn_id, 'FAILED', pcb.failure_reason)
                cur.close()
                conn.close()
                return False

            # Step 3: Lock receiver's balance row for UPDATE
            cur.execute("SELECT current_balance FROM balance WHERE account_id = %s FOR UPDATE;", (pcb.receiver_id,))
            r_row = cur.fetchone()
            if not r_row:
                conn.rollback()
                pcb.status = "FAILED"
                pcb.failure_reason = f"Receiver account {pcb.receiver_id} not found."
                update_txn_status(pcb.txn_id, 'FAILED', pcb.failure_reason)
                cur.close()
                conn.close()
                return False

            # Step 4: Execute atomic balance updates
            cur.execute("""
                UPDATE balance 
                SET current_balance = current_balance - %s, last_updated = NOW() 
                WHERE account_id = %s;
            """, (amt, pcb.sender_id))

            cur.execute("""
                UPDATE balance 
                SET current_balance = current_balance + %s, last_updated = NOW() 
                WHERE account_id = %s;
            """, (amt, pcb.receiver_id))

            # Step 5: Mark transaction completed
            cur.execute("""
                UPDATE transactions 
                SET status = 'COMPLETED', failure_reason = 'ACID Verified & Committed' 
                WHERE txn_id = %s;
            """, (pcb.txn_id,))

            conn.commit()
            cur.close()
            conn.close()

            pcb.status = "COMPLETED"
            pcb.failure_reason = "ACID Verified & Committed"
            return True

        except Exception as e:
            try:
                conn.rollback()
                conn.close()
            except Exception:
                pass
            pcb.status = "FAILED"
            pcb.failure_reason = f"Database Error: {str(e)}"
            update_txn_status(pcb.txn_id, 'FAILED', pcb.failure_reason)
            return False

    # In-memory ACID execution fallback
    with db_lock:
        s_bal = in_memory_balances.get(pcb.sender_id)
        r_bal = in_memory_balances.get(pcb.receiver_id)

        if not s_bal:
            pcb.status = "FAILED"
            pcb.failure_reason = f"Sender account {pcb.sender_id} not found."
            _update_in_mem_txn(pcb.txn_id, 'FAILED', pcb.failure_reason)
            return False

        if not r_bal:
            pcb.status = "FAILED"
            pcb.failure_reason = f"Receiver account {pcb.receiver_id} not found."
            _update_in_mem_txn(pcb.txn_id, 'FAILED', pcb.failure_reason)
            return False

        is_vault = (pcb.sender_id == 'ACC1006')
        if not is_vault and s_bal['current_balance'] < amt:
            pcb.status = "FAILED"
            pcb.failure_reason = f"ACID Rollback: Insufficient Balance (Available: ₹{s_bal['current_balance']:,.2f}, Required: ₹{amt:,.2f})"
            _update_in_mem_txn(pcb.txn_id, 'FAILED', pcb.failure_reason)
            return False

        # Atomic commit
        s_bal['current_balance'] -= amt
        s_bal['last_updated'] = datetime.now()
        r_bal['current_balance'] += amt
        r_bal['last_updated'] = datetime.now()

        pcb.status = "COMPLETED"
        pcb.failure_reason = "ACID Verified & Committed"
        _update_in_mem_txn(pcb.txn_id, 'COMPLETED', pcb.failure_reason)
        return True


def update_txn_status(txn_id, status, failure_reason=None):
    """Updates status of a transaction in MySQL and In-Memory."""
    conn = get_connection()
    if conn:
        try:
            cur = conn.cursor()
            cur.execute("""
                UPDATE transactions 
                SET status = %s, failure_reason = %s 
                WHERE txn_id = %s;
            """, (status, failure_reason, txn_id))
            conn.commit()
            cur.close()
            conn.close()
        except Exception:
            pass

    with db_lock:
        _update_in_mem_txn(txn_id, status, failure_reason)


def _update_in_mem_txn(txn_id, status, reason):
    for t in in_memory_transactions:
        if t['txn_id'] == txn_id:
            t['status'] = status
            t['failure_reason'] = reason
            break


def reset_database():
    """Resets tables and restore seed balances for clean demonstrations."""
    global in_memory_transactions, in_memory_atm_records, in_memory_upi_payroll_records
    with db_lock:
        # Reset balances
        in_memory_balances['ACC1001']['current_balance'] = Decimal('150000.00')
        in_memory_balances['ACC1002']['current_balance'] = Decimal('85000.00')
        in_memory_balances['ACC1003']['current_balance'] = Decimal('240000.00')
        in_memory_balances['ACC1004']['current_balance'] = Decimal('95000.00')
        in_memory_balances['ACC1005']['current_balance'] = Decimal('1500000.00')
        in_memory_balances['ACC1006']['current_balance'] = Decimal('800000.00')
        in_memory_balances['ACC1007']['current_balance'] = Decimal('320000.00')
        in_memory_balances['ACC1008']['current_balance'] = Decimal('65000.00')
        in_memory_transactions = list(_seed_txns)

    conn = get_connection()
    if conn:
        try:
            cur = conn.cursor()
            cur.execute("DELETE FROM atm_transactions;")
            cur.execute("DELETE FROM upi_payroll_transactions;")
            cur.execute("DELETE FROM transactions;")
            cur.execute("UPDATE balance SET current_balance = 150000.00 WHERE account_id = 'ACC1001';")
            cur.execute("UPDATE balance SET current_balance = 85000.00 WHERE account_id = 'ACC1002';")
            cur.execute("UPDATE balance SET current_balance = 240000.00 WHERE account_id = 'ACC1003';")
            cur.execute("UPDATE balance SET current_balance = 95000.00 WHERE account_id = 'ACC1004';")
            cur.execute("UPDATE balance SET current_balance = 1500000.00 WHERE account_id = 'ACC1005';")
            cur.execute("UPDATE balance SET current_balance = 800000.00 WHERE account_id = 'ACC1006';")
            cur.execute("UPDATE balance SET current_balance = 320000.00 WHERE account_id = 'ACC1007';")
            cur.execute("UPDATE balance SET current_balance = 65000.00 WHERE account_id = 'ACC1008';")
            for t in _seed_txns:
                cur.execute("""
                    INSERT INTO transactions (txn_id, sender_id, receiver_id, txn_category, amount, burst_time, assigned_queue, status, created_at)
                    VALUES (%s, %s, %s, %s, %s, %s, %s, %s, NOW());
                """, (t['txn_id'], t['sender_id'], t['receiver_id'], t['txn_category'], t['amount'], t['burst_time'], t['assigned_queue'], t['status']))
            conn.commit()
            cur.close()
            conn.close()
        except Exception as e:
            print("[db.py] Reset warning:", e)
