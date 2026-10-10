-- ========================================================================
-- Bank OS Simulator: Seed Data (Initial Customers and Accounts)
-- ========================================================================

USE bank_os_db;

-- Insert default demo customers
INSERT INTO customer (customer_id, name, email, phone) VALUES
(1, 'Aarav Sharma', 'aarav.sharma@bankos.internal', '+91 98765 43210'),
(2, 'Diya Patel',   'diya.patel@bankos.internal',   '+91 98765 43211'),
(3, 'Rohan Mehta',  'rohan.mehta@bankos.internal',  '+91 98765 43212')
ON DUPLICATE KEY UPDATE name=VALUES(name);

-- Insert default demo accounts matching the simulator defaults:
-- A101: ₹ 50,000.00
-- A102: ₹ 35,000.00
-- A103: ₹ 22,000.00
INSERT INTO account (account_id, customer_id, balance, account_type, status) VALUES
('A101', 1, 50000.00, 'SAVINGS', 'ACTIVE'),
('A102', 2, 35000.00, 'SAVINGS', 'ACTIVE'),
('A103', 3, 22000.00, 'CURRENT', 'ACTIVE')
ON DUPLICATE KEY UPDATE balance=VALUES(balance);
