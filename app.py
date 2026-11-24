# ============================================================================
# BankOS Middleware: Flask Application Entrypoint (app.py)
# Exposes REST API Endpoints for MLFQ Scheduling, Test Injection & SQL JOINs
# ============================================================================

import os
from flask import Flask, render_template, request, jsonify
import db
from scheduler import scheduler

app = Flask(__name__, template_folder="templates")

# Initialize database (MySQL with In-Memory fallback)
db.init_db()

# Start background MLFQ CPU worker and simulation traffic
scheduler.start()


# ============================================================================
# WEB PAGES
# ============================================================================
@app.route("/")
def index():
    """Renders the main 3-Slide BankOS Middleware Dashboard."""
    return render_template("index.html")


# ============================================================================
# REST API ENDPOINTS
# ============================================================================
@app.route("/api/transaction/inject", methods=["POST"])
def inject_test_transaction():
    """
    Endpoint for the Manual Test Transaction Injector.
    Injects a transaction with is_test_txn = True, ensuring distinct visual
    highlighting in MLFQ and real-time progress tracking.
    """
    try:
        data = request.get_json(force=True) or {}
        sender_id = data.get("sender_id", "ACC1001").strip()
        receiver_id = data.get("receiver_id", "ACC1002").strip()
        txn_category = data.get("txn_category", "UPI").strip().upper()
        amount = float(data.get("amount", 2500.00))
        burst_time = int(data.get("burst_time", 2))
        
        # Build channel metadata
        channel_meta = {}
        if txn_category == "ATM":
            channel_meta["atm_location"] = data.get("atm_location", "Main Branch Cyber Kiosk #01")
            channel_meta["card_last4"] = data.get("card_last4", "8821")
        elif txn_category in ("UPI", "PAYROLL"):
            channel_meta["reference_handle"] = data.get("reference_handle", f"{sender_id.lower()}@upi")

        # Inject into scheduler as a TEST transaction
        pcb = scheduler.inject_transaction(
            sender_id=sender_id,
            receiver_id=receiver_id,
            txn_category=txn_category,
            amount=amount,
            burst_time=burst_time,
            is_test_txn=True,
            channel_meta=channel_meta
        )

        return jsonify({
            "status": "success",
            "message": f"Test transaction {pcb.txn_id} admitted into {pcb.assigned_queue}",
            "transaction": pcb.to_dict()
        }), 201

    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 400


@app.route("/api/scheduler/state", methods=["GET"])
def get_scheduler_state():
    """
    Returns live scheduler state: active CPU process, Queue 1, Queue 2, Queue 3,
    and tracked test transaction status.
    """
    state = scheduler.get_state()
    return jsonify(state)


@app.route("/api/scheduler/traffic/toggle", methods=["POST"])
def toggle_traffic():
    """Toggles background simulation traffic ON / OFF."""
    data = request.get_json(silent=True) or {}
    enabled = data.get("enabled")
    new_state = scheduler.toggle_traffic(enabled)
    return jsonify({
        "status": "success",
        "traffic_enabled": new_state
    })


@app.route("/api/scheduler/reset", methods=["POST"])
def reset_scheduler():
    """Resets all queues, balances, and database records."""
    scheduler.reset()
    return jsonify({
        "status": "success",
        "message": "Queues, balances, and ledger reset to initial baseline."
    })


@app.route("/api/ledger", methods=["GET"])
def get_ledger():
    """
    MANDATORY SQL JOIN ENDPOINT:
    Returns multi-table joined transaction records from MySQL (or in-memory engine).
    Query params: category, search, limit.
    """
    category = request.args.get("category", "ALL")
    search = request.args.get("search", "").strip()
    limit = int(request.args.get("limit", 100))

    joined_rows = db.get_joined_transactions(category=category, search=search, limit=limit)
    return jsonify({
        "status": "success",
        "count": len(joined_rows),
        "transactions": joined_rows
    })


@app.route("/api/accounts", methods=["GET"])
def get_accounts_ledger():
    """Returns all accounts joined with current balances."""
    accounts = db.get_accounts()
    return jsonify({
        "status": "success",
        "accounts": accounts
    })


@app.route("/api/health", methods=["GET"])
def health_check():
    """Health and engine telemetry status."""
    return jsonify({
        "app": "BankOS Middleware",
        "status": "online",
        "database_engine": "MySQL 8.4 (bankos_db)" if db.is_mysql_connected else "In-Memory ACID Engine (Active)",
        "mysql_connected": db.is_mysql_connected,
        "mysql_error": db.mysql_error_message
    })


# ============================================================================
# ENTRYPOINT
# ============================================================================
def find_available_port(preferred_port=5000):
    import socket
    candidates = [preferred_port, 5001, 5050, 8088]
    for p in candidates:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                s.bind(("0.0.0.0", p))
                return p
            except OSError:
                continue
    return preferred_port


if __name__ == "__main__":
    requested_port = int(os.getenv("PORT", 5000))
    port = find_available_port(requested_port)
    print(f"\n====================================================================")
    print(f"       BANKOS MIDDLEWARE: OS TRANSACTION SCHEDULER ONLINE           ")
    print(f"       Dashboard URL: http://127.0.0.1:{port}                       ")
    print(f"       Database Target: {db.DB_HOST}:{db.DB_PORT}/{db.DB_NAME}      ")
    print(f"====================================================================\n")
    app.run(host="0.0.0.0", port=port, debug=False, threaded=True)
