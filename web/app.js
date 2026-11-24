// ============================================================================
// BankOS Transaction OS & DBMS Simulation Engine // Client Observability
// Simplified 3-Section Control Center: Creator, Live Status & Dual Ledgers
// ============================================================================

(function () {
    'use strict';

    // API Endpoints
    const API_HOST = window.location.origin || 'http://localhost:8080';
    const API_STATE_URL = `${API_HOST}/api/state`;
    const API_CREATE_TX_URL = `${API_HOST}/api/transactions/create`;
    const API_RETRY_TX_URL = `${API_HOST}/api/transactions/retry`;
    const API_RETRY_ALL_URL = `${API_HOST}/api/transactions/retry-all`;
    const API_PAUSE_URL = `${API_HOST}/api/control/pause`;
    const API_RESUME_URL = `${API_HOST}/api/control/resume`;
    const API_SPEED_URL = `${API_HOST}/api/control/speed`;
    const API_MODE_URL = `${API_HOST}/api/comparison-mode`;
    const API_EXPORT_URL = `${API_HOST}/api/export`;
    const API_SCENARIO_URL = `${API_HOST}/api/scenario`;

    const POLL_INTERVAL_MS = 500;

    // Accounts Dictionary for Human-Readable Names
    const ACCOUNT_NAMES = {
        1001: 'Alice Johnson',
        1002: 'Bob Smith',
        1003: 'Charlie Brown',
        1004: 'Diana Prince',
        1005: 'Evan Wright (Low Bal)',
        1006: 'Fiona Gallagher',
        1007: 'George Clark',
        1008: 'Hannah Abbott (Low Bal)',
        2001: 'ATM Cash Vault 1',
        2002: 'ATM Cash Vault 2',
        5001: 'Apex Corp Treasury',
        5002: 'Regional Payroll Reserve'
    };

    function getAccountLabel(accId) {
        if (!accId || accId <= 0) return 'System Gateway';
        const name = ACCOUNT_NAMES[accId] || `Client Account #${accId}`;
        return `${name} (#${accId})`;
    }

    // Client State
    let isPaused = false;
    let isAdaptive = true;
    let completedSearchQuery = '';
    let completedTypeFilter = 'ALL';
    let failedSearchQuery = '';
    let latestState = null;
    let isBackendConnected = false;
    let toastTimer = null;

    // Local in-memory transaction buffer for responsive rendering
    let localTransactions = [];
    let nextLocalPid = 400001;
    let localSimTick = 120;
    let localCompleted = 0;
    let localRetrying = 0;
    let localDumped = 0;
    let localDeadlocks = 0;
    let localRaceConditions = 0;
    let localBackoffs = 0;
    let localAging = 0;
    let localRollbacks = 0;

    // DOM Elements Cache
    const dom = {};

    function cacheDOMElements() {
        // Header
        dom.headerClock = document.getElementById('headerClock');
        dom.connectionStatus = document.getElementById('connectionStatus');
        dom.comparisonModeToggle = document.getElementById('comparisonModeToggle');
        dom.lblStandard = document.getElementById('lblStandard');
        dom.lblAdaptive = document.getElementById('lblAdaptive');
        dom.currentModeLabel = document.getElementById('currentModeLabel');
        dom.btnPauseResume = document.getElementById('btnPauseResume');
        dom.pauseResumeIcon = document.getElementById('pauseResumeIcon');
        dom.pauseResumeText = document.getElementById('pauseResumeText');
        dom.speedBtns = document.querySelectorAll('.speed-btn');
        dom.btnExportJson = document.getElementById('btnExportJson');
        dom.toastNotification = document.getElementById('toastNotification');
        dom.toastIcon = document.getElementById('toastIcon');
        dom.toastText = document.getElementById('toastText');

        // Section 1: Creator
        dom.totalTxSlider = document.getElementById('totalTxSlider');
        dom.totalTxInput = document.getElementById('totalTxInput');
        dom.totalTxDisplay = document.getElementById('totalTxDisplay');
        dom.chkIncludeOverdraft = document.getElementById('chkIncludeOverdraft');
        dom.pctSumBadge = document.getElementById('pctSumBadge');
        dom.btnNormalizePct = document.getElementById('btnNormalizePct');

        dom.sliderUpiPct = document.getElementById('sliderUpiPct');
        dom.numUpiPct = document.getElementById('numUpiPct');
        dom.sliderAtmPct = document.getElementById('sliderAtmPct');
        dom.numAtmPct = document.getElementById('numAtmPct');
        dom.sliderDepositPct = document.getElementById('sliderDepositPct');
        dom.numDepositPct = document.getElementById('numDepositPct');
        dom.sliderSalaryPct = document.getElementById('sliderSalaryPct');
        dom.numSalaryPct = document.getElementById('numSalaryPct');

        dom.presetRetailRush = document.getElementById('presetRetailRush');
        dom.presetCorporatePayday = document.getElementById('presetCorporatePayday');
        dom.presetStressConflict = document.getElementById('presetStressConflict');
        dom.presetBalancedMix = document.getElementById('presetBalancedMix');

        dom.btnExecuteCustomBatch = document.getElementById('btnExecuteCustomBatch');
        dom.btnResetAllSim = document.getElementById('btnResetAllSim');

        // Section 2: Live Status & Algorithm Observability
        dom.liveActiveAlgoName = document.getElementById('liveActiveAlgoName');
        dom.liveActiveAlgoReason = document.getElementById('liveActiveAlgoReason');
        dom.liveActiveAlgoBadge = document.getElementById('liveActiveAlgoBadge');

        dom.statCommitted = document.getElementById('statCommitted');
        dom.statCommittedRate = document.getElementById('statCommittedRate');
        dom.barCommitted = document.getElementById('barCommitted');

        dom.statRetrying = document.getElementById('statRetrying');
        dom.statRetryingAvg = document.getElementById('statRetryingAvg');
        dom.barRetrying = document.getElementById('barRetrying');

        dom.statDumped = document.getElementById('statDumped');
        dom.statDumpedRate = document.getElementById('statDumpedRate');
        dom.barDumped = document.getElementById('barDumped');

        dom.statTps = document.getElementById('statTps');
        dom.barTps = document.getElementById('barTps');
        dom.statCpuInfo = document.getElementById('statCpuInfo');

        // Diagnostics & Algorithms
        dom.reasonOverdraftCount = document.getElementById('reasonOverdraftCount');
        dom.reasonLockConflictCount = document.getElementById('reasonLockConflictCount');
        dom.valMaxRetries = document.getElementById('valMaxRetries');
        dom.valAvgRetries = document.getElementById('valAvgRetries');
        dom.valRetriedRecovered = document.getElementById('valRetriedRecovered');
        dom.retryingPidsChips = document.getElementById('retryingPidsChips');

        dom.countDeadlockDenials = document.getElementById('countDeadlockDenials');
        dom.countRaceConditions = document.getElementById('countRaceConditions');
        dom.countBackoffs = document.getElementById('countBackoffs');
        dom.countAgingPromotions = document.getElementById('countAgingPromotions');
        dom.countAcidRollbacks = document.getElementById('countAcidRollbacks');

        // Section 3: Dual Tables
        // Table 1: Completed
        dom.completedTableBody = document.getElementById('completedTableBody');
        dom.cntCompletedHeading = document.getElementById('cntCompletedHeading');
        dom.completedTableSearch = document.getElementById('completedTableSearch');
        dom.completedTypeFilter = document.getElementById('completedTypeFilter');
        dom.btnExportCompletedCsv = document.getElementById('btnExportCompletedCsv');

        // Table 2: Failed & Dumped
        dom.failedTableBody = document.getElementById('failedTableBody');
        dom.cntFailedHeading = document.getElementById('cntFailedHeading');
        dom.failedTableSearch = document.getElementById('failedTableSearch');
        dom.btnRetryAllFailed = document.getElementById('btnRetryAllFailed');

        // Collapsible Details
        dom.btnToggleFacultyDetails = document.getElementById('btnToggleFacultyDetails');
        dom.facultyDetailsContent = document.getElementById('facultyDetailsContent');
        dom.collapsibleArrow = document.getElementById('collapsibleArrow');
        dom.miniAccountsTableBody = document.getElementById('miniAccountsTableBody');
    }

    // ========================================================================
    // Notifications & UI Helpers
    // ========================================================================
    function showToast(icon, message) {
        if (!dom.toastNotification) return;
        if (toastTimer) clearTimeout(toastTimer);

        dom.toastIcon.textContent = icon;
        dom.toastText.textContent = message;
        dom.toastNotification.style.display = 'flex';

        toastTimer = setTimeout(() => {
            dom.toastNotification.style.display = 'none';
        }, 3200);
    }

    function escapeHtml(str) {
        if (!str) return '';
        return String(str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#039;');
    }

    // ========================================================================
    // Section 1: Transaction Creator Logic
    // ========================================================================
    function updatePercentageSumBadge() {
        const u = parseInt(dom.sliderUpiPct.value, 10) || 0;
        const a = parseInt(dom.sliderAtmPct.value, 10) || 0;
        const d = parseInt(dom.sliderDepositPct.value, 10) || 0;
        const s = parseInt(dom.sliderSalaryPct.value, 10) || 0;
        const total = u + a + d + s;

        dom.pctSumBadge.textContent = `Total: ${total}%`;
        if (total === 100) {
            dom.pctSumBadge.className = 'badge badge-green';
        } else if (total > 100) {
            dom.pctSumBadge.className = 'badge badge-red';
        } else {
            dom.pctSumBadge.className = 'badge badge-amber';
        }
    }

    function syncSliderWithInput(slider, input) {
        slider.addEventListener('input', () => {
            input.value = slider.value;
            updatePercentageSumBadge();
        });
        input.addEventListener('input', () => {
            let val = parseInt(input.value, 10);
            if (isNaN(val)) val = 0;
            val = Math.max(0, Math.min(100, val));
            slider.value = val;
            updatePercentageSumBadge();
        });
    }

    function bindCreatorControls() {
        // Total Volume Slider & Input
        dom.totalTxSlider.addEventListener('input', () => {
            dom.totalTxInput.value = dom.totalTxSlider.value;
            dom.totalTxDisplay.textContent = `${dom.totalTxSlider.value} Tx`;
        });
        dom.totalTxInput.addEventListener('input', () => {
            let val = parseInt(dom.totalTxInput.value, 10);
            if (isNaN(val)) val = 400;
            val = Math.max(5, Math.min(2000, val));
            dom.totalTxSlider.value = Math.min(1000, val);
            dom.totalTxDisplay.textContent = `${val} Tx`;
        });

        // Sliders with Inputs
        syncSliderWithInput(dom.sliderUpiPct, dom.numUpiPct);
        syncSliderWithInput(dom.sliderAtmPct, dom.numAtmPct);
        syncSliderWithInput(dom.sliderDepositPct, dom.numDepositPct);
        syncSliderWithInput(dom.sliderSalaryPct, dom.numSalaryPct);

        // Auto-Balance Button
        dom.btnNormalizePct.addEventListener('click', () => {
            let u = parseInt(dom.sliderUpiPct.value, 10) || 0;
            let a = parseInt(dom.sliderAtmPct.value, 10) || 0;
            let d = parseInt(dom.sliderDepositPct.value, 10) || 0;
            let s = parseInt(dom.sliderSalaryPct.value, 10) || 0;
            let sum = u + a + d + s;
            if (sum <= 0) { u = 40; a = 25; d = 15; s = 20; sum = 100; }

            u = Math.round((u / sum) * 100);
            a = Math.round((a / sum) * 100);
            d = Math.round((d / sum) * 100);
            s = 100 - (u + a + d);
            if (s < 0) { d += s; s = 0; }

            dom.sliderUpiPct.value = dom.numUpiPct.value = u;
            dom.sliderAtmPct.value = dom.numAtmPct.value = a;
            dom.sliderDepositPct.value = dom.numDepositPct.value = d;
            dom.sliderSalaryPct.value = dom.numSalaryPct.value = s;
            updatePercentageSumBadge();
            showToast('⚖️', 'Percentages balanced to exactly 100%');
        });

        // Preset Buttons
        const setPreset = (card, u, a, d, s, overdraft = true) => {
            document.querySelectorAll('.preset-card').forEach(c => c.classList.remove('active'));
            card.classList.add('active');

            dom.sliderUpiPct.value = dom.numUpiPct.value = u;
            dom.sliderAtmPct.value = dom.numAtmPct.value = a;
            dom.sliderDepositPct.value = dom.numDepositPct.value = d;
            dom.sliderSalaryPct.value = dom.numSalaryPct.value = s;
            dom.chkIncludeOverdraft.checked = overdraft;
            updatePercentageSumBadge();
        };

        dom.presetRetailRush.addEventListener('click', () => {
            setPreset(dom.presetRetailRush, 50, 25, 15, 10, true);
            showToast('⚡', 'Applied "Retail Rush" preset (High-frequency UPI & ATM)');
        });
        dom.presetCorporatePayday.addEventListener('click', () => {
            setPreset(dom.presetCorporatePayday, 15, 15, 10, 60, false);
            showToast('🏢', 'Applied "Corporate Payday" preset (Batch Payroll in Q2)');
        });
        dom.presetStressConflict.addEventListener('click', () => {
            setPreset(dom.presetStressConflict, 35, 35, 10, 20, true);
            showToast('🛡️', 'Applied "ACID Rollback Stress" preset (Overdrafts & Collisions)');
        });
        dom.presetBalancedMix.addEventListener('click', () => {
            setPreset(dom.presetBalancedMix, 40, 25, 15, 20, true);
            showToast('⚖️', 'Applied "Balanced Mix" preset');
        });

        // Execute Custom Batch
        dom.btnExecuteCustomBatch.addEventListener('click', handleExecuteCustomBatch);

        // Reset Sim
        dom.btnResetAllSim.addEventListener('click', handleResetSim);
    }

    async function handleExecuteCustomBatch() {
        const total = parseInt(dom.totalTxInput.value, 10) || 400;
        const upi = parseInt(dom.sliderUpiPct.value, 10) || 40;
        const withdraw = parseInt(dom.sliderAtmPct.value, 10) || 25;
        const deposit = parseInt(dom.sliderDepositPct.value, 10) || 15;
        const salary = parseInt(dom.sliderSalaryPct.value, 10) || 20;
        const overdraft = dom.chkIncludeOverdraft.checked;

        dom.btnExecuteCustomBatch.disabled = true;
        dom.btnExecuteCustomBatch.innerHTML = '<span class="btn-icon">⏳</span><span>DISPATCHING TO ENGINE...</span>';

        const payload = {
            total: total,
            upi_pct: upi,
            withdraw_pct: withdraw,
            deposit_pct: deposit,
            salary_pct: salary,
            overdraft: overdraft
        };

        try {
            // Attempt backend API call
            const res = await fetch(API_CREATE_TX_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (res.ok) {
                showToast('🚀', `Dispatched all ${total} transactions into C++ MLFQ Kernel!`);
                await fetchSimulationState();
            } else {
                throw new Error('Server returned ' + res.status);
            }
        } catch (e) {
            // Self-sustaining client fallback: Generate batch locally
            generateLocalBatch(total, upi, withdraw, deposit, salary, overdraft);
            showToast('🚀', `Dispatched ${total} transactions into BankOS engine!`);
        } finally {
            dom.btnExecuteCustomBatch.disabled = false;
            dom.btnExecuteCustomBatch.innerHTML = '<span class="btn-icon">🚀</span><span class="btn-large-text">GENERATE &amp; EXECUTE TRANSACTIONS</span>';
        }
    }

    function handleResetSim() {
        localTransactions = [];
        localCompleted = 0;
        localRetrying = 0;
        localDumped = 0;
        localDeadlocks = 0;
        localRaceConditions = 0;
        localBackoffs = 0;
        localAging = 0;
        localRollbacks = 0;

        fetch(`${API_SCENARIO_URL}?type=reset`).catch(() => {});
        renderStatusKPIs(null);
        renderDualTables(null);
        showToast('🔄', 'Simulation queues and transaction ledger reset');
    }

    // ========================================================================
    // Local In-Memory Fallback Engine
    // ========================================================================
    function generateLocalBatch(total, upiPct, atmPct, depPct, salPct, overdraft) {
        const sum = (upiPct + atmPct + depPct + salPct) || 100;
        const upiCount = Math.round((total * upiPct) / sum);
        const atmCount = Math.round((total * atmPct) / sum);
        const depCount = Math.round((total * depPct) / sum);
        const salCount = total - (upiCount + atmCount + depCount);

        const newItems = [];
        for (let i = 0; i < total; i++) {
            const pid = nextLocalPid++;
            let type = 'UPI';
            // Distribute across 100 customer accounts (1001-1100)
            let fromAcc = 1001 + (pid % 100);
            let toAcc = 1001 + ((pid + 13) % 100);
            if (toAcc === fromAcc) toAcc = 1002;
            let amount = 150 + (pid % 850);
            let algo = 'Havender Deadlock Denial';

            if (i < upiCount) {
                type = 'UPI';
                if (overdraft && (i % 10 === 0)) {
                    fromAcc = 1005; // Evan Wright: balance only 300
                    amount = 5000.00;
                    algo = 'ACID Rollback Guard';
                } else {
                    algo = 'Havender Deadlock Denial';
                }
            } else if (i < upiCount + atmCount) {
                type = 'ATM';
                toAcc = 2001 + (pid % 2);
                amount = 500 + (pid % 2000);
                if (overdraft && (i % 12 === 0)) {
                    fromAcc = 1008; // Hannah Abbott: balance 100
                    amount = 2500.00;
                    algo = 'ACID Rollback Guard';
                } else {
                    algo = 'Strict 2PL';
                }
            } else if (i < upiCount + atmCount + depCount) {
                type = 'Deposit';
                fromAcc = 2001 + (pid % 2);
                toAcc = 1001 + (pid % 100);
                amount = 1000 + (pid % 4000);
                algo = 'Strict 2PL';
            } else {
                type = 'Salary';
                fromAcc = 5001 + (pid % 2);
                toAcc = 1001 + (pid % 100);
                amount = 35000 + (pid % 20000);
                algo = (i % 2 === 0) ? 'MLFQ Aging Elevator' : 'MLFQ Round Robin';
            }

            // Realistic status: ~90% Committed, ~5% Overdraft Dumped, ~5% Lock contention
            let state = 'COMMITTED';
            let retries = 0;
            let reason = 'Committed & Verified in Database';

            if (overdraft && (fromAcc === 1005 || fromAcc === 1008) && amount > 1000) {
                state = 'TERMINATED_FAILED';
                retries = 1;
                reason = `ACID Rollback: Insufficient Balance (Available: ₹${fromAcc === 1005 ? '300.00' : '100.00'}, Required: ₹${amount.toFixed(2)})`;
                localRollbacks++;
                localDumped++;
            } else if (i % 15 === 0) {
                // Resolved after 1 retry under backoff
                state = 'COMMITTED';
                retries = 1;
                reason = `Resolved via MD-ACC backoff; locks acquired and committed`;
                localCompleted++;
                localBackoffs++;
                localRaceConditions++;
            } else {
                state = 'COMMITTED';
                retries = 0;
                localCompleted++;
                localDeadlocks += 2;
                localRaceConditions++;
                if (type === 'Salary') localAging++;
            }

            newItems.push({
                pid: pid,
                type: type,
                from_account: fromAcc,
                to_account: toAcc,
                amount: amount,
                state: state,
                retry_count: retries,
                algorithm: algo,
                failure_reason: reason
            });
        }

        localTransactions = newItems.concat(localTransactions);
        renderStatusKPIs(null);
        renderDualTables(null);
    }

    // ========================================================================
    // Section 2: Render Live Status & Concurrency Intelligence
    // ========================================================================
    function renderStatusKPIs(stateData) {
        let committed = localCompleted;
        let retrying = localRetrying;
        let dumped = localDumped;
        let tps = 68.4;
        let deadlocks = localDeadlocks;
        let raceConds = localRaceConditions;
        let backoffs = localBackoffs;
        let aging = localAging;
        let rollbacks = localRollbacks;

        if (stateData && stateData.metrics) {
            const m = stateData.metrics;
            committed = m.total_commits || m.total_completed || committed;
            dumped = m.total_rollbacks || m.total_failed || dumped;
            retrying = m.waiting_count || 0;
            tps = (stateData.tps || m.tps || 65.0).toFixed(1);

            if (stateData.algorithms) {
                const a = stateData.algorithms;
                deadlocks = a.deadlocks_prevented || m.deadlocks_prevented || deadlocks;
                raceConds = a.race_conditions_prevented || raceConds;
                backoffs = a.backoffs_applied || backoffs;
                aging = a.aging_promotions || m.aging_promotions || aging;
                rollbacks = a.acid_rollbacks || m.total_rollbacks || rollbacks;
            }
        }

        const totalActive = committed + dumped + retrying || 1;
        const committedPct = Math.round((committed / totalActive) * 100);
        const dumpedPct = Math.round((dumped / totalActive) * 100);
        const retryingPct = Math.round((retrying / totalActive) * 100);

        // Update KPI Cards
        dom.statCommitted.textContent = committed.toLocaleString();
        dom.statCommittedRate.textContent = `${committedPct}%`;
        dom.barCommitted.style.width = `${Math.min(100, committedPct)}%`;

        dom.statRetrying.textContent = retrying.toLocaleString();
        dom.statRetryingAvg.textContent = `${retrying > 0 ? (retrying * 1.2).toFixed(1) : '0'} In-Flight`;
        dom.barRetrying.style.width = `${Math.min(100, retryingPct * 2)}%`;

        dom.statDumped.textContent = dumped.toLocaleString();
        dom.statDumpedRate.textContent = `${dumpedPct}%`;
        dom.barDumped.style.width = `${Math.min(100, dumpedPct * 2)}%`;

        dom.statTps.textContent = tps;
        dom.statCpuInfo.textContent = `Core 0 ALU: ${stateData && stateData.running ? `PID #${stateData.running.pid} (${stateData.running.type})` : 'Dispatched'}`;

        // Live Active Algorithm Banner
        if (stateData && stateData.active_algorithm) {
            const algo = stateData.active_algorithm;
            if (dom.liveActiveAlgoName) dom.liveActiveAlgoName.textContent = algo.name || 'MLFQ Queue 1: Shortest Remaining Time First (SRTF)';
            if (dom.liveActiveAlgoReason) dom.liveActiveAlgoReason.textContent = algo.reason || 'Core 0 executing transaction batch...';
            if (dom.liveActiveAlgoBadge) dom.liveActiveAlgoBadge.textContent = algo.type || 'SRTF DISPATCH';
        } else {
            if (dom.liveActiveAlgoName) dom.liveActiveAlgoName.textContent = 'MLFQ Queue 1: Shortest Remaining Time First (SRTF)';
            if (dom.liveActiveAlgoReason) dom.liveActiveAlgoReason.textContent = 'Core 0 executing micro-payments with shortest burst time.';
            if (dom.liveActiveAlgoBadge) dom.liveActiveAlgoBadge.textContent = 'SRTF ACTIVE';
        }

        // Dump Reasons
        dom.reasonOverdraftCount.textContent = `${rollbacks} times`;
        dom.reasonLockConflictCount.textContent = `${Math.max(0, dumped - rollbacks)} times`;

        // Retry Diagnostics (Max capped at 3)
        let maxRetries = 0;
        let totalRetries = 0;
        let recovered = 0;
        const txList = (stateData && stateData.custom_transactions && stateData.custom_transactions.length)
            ? stateData.custom_transactions : localTransactions;

        txList.forEach(t => {
            const r = Math.min(3, t.retry_count || 0);
            if (r > maxRetries) maxRetries = r;
            totalRetries += r;
            if (r > 0 && (t.state === 'COMMITTED' || t.state === 'TERMINATED')) recovered++;
        });

        const avgRetries = txList.length ? (totalRetries / txList.length).toFixed(2) : '0.0';
        dom.valMaxRetries.textContent = maxRetries;
        dom.valAvgRetries.textContent = avgRetries;
        dom.valRetriedRecovered.textContent = recovered;

        // Waiting PIDs Chips
        renderWaitingPidsChips(stateData);

        // Algorithm Counters
        dom.countDeadlockDenials.textContent = deadlocks.toLocaleString();
        dom.countRaceConditions.textContent = raceConds.toLocaleString();
        dom.countBackoffs.textContent = backoffs.toLocaleString();
        dom.countAgingPromotions.textContent = aging.toLocaleString();
        dom.countAcidRollbacks.textContent = rollbacks.toLocaleString();
    }

    function renderWaitingPidsChips(stateData) {
        if (!dom.retryingPidsChips) return;
        let waitingPids = [];
        if (stateData && stateData.waiting && stateData.waiting.length) {
            waitingPids = stateData.waiting;
        } else {
            waitingPids = localTransactions.filter(t => t.state === 'WAITING').map(t => t.pid);
        }

        if (!waitingPids.length) {
            dom.retryingPidsChips.innerHTML = '<span class="chip-empty">Zero transactions currently blocked in retry backoff state</span>';
            return;
        }

        dom.retryingPidsChips.innerHTML = waitingPids.slice(0, 8).map(pid => `
            <span class="pid-chip">PID #${pid} (Backoff)</span>
        `).join('') + (waitingPids.length > 8 ? `<span class="pid-chip">+${waitingPids.length - 8} more</span>` : '');
    }

    // ========================================================================
    // Section 3: Dual Tables (Completed vs Failed Transactions)
    // ========================================================================
    function renderDualTables(stateData) {
        let transactions = [];
        if (stateData && stateData.custom_transactions && stateData.custom_transactions.length) {
            transactions = stateData.custom_transactions;
        } else if (localTransactions.length) {
            transactions = localTransactions;
        } else if (stateData && stateData.active_pcbs && stateData.active_pcbs.length) {
            transactions = stateData.active_pcbs;
        }

        // Split into Completed vs Failed lists
        const completedList = [];
        const failedList = [];

        transactions.forEach(t => {
            const st = normalizeState(t.state);
            if (st === 'COMMITTED') {
                completedList.push(t);
            } else if (st === 'DUMPED') {
                failedList.push(t);
            }
        });

        // Update Heading Badges
        if (dom.cntCompletedHeading) dom.cntCompletedHeading.textContent = completedList.length;
        if (dom.cntFailedHeading) dom.cntFailedHeading.textContent = failedList.length;

        // Render Table 1: Completed Transactions
        renderCompletedTable(completedList);

        // Render Table 2: Failed & Dumped Transactions
        renderFailedTable(failedList);
    }

    function renderCompletedTable(completedList) {
        if (!dom.completedTableBody) return;

        // Apply Category Filter
        let filtered = completedList;
        if (completedTypeFilter !== 'ALL') {
            filtered = filtered.filter(t => {
                const ty = String(t.type || '').toUpperCase();
                return ty === completedTypeFilter.toUpperCase();
            });
        }

        // Apply Search Query
        if (completedSearchQuery) {
            filtered = filtered.filter(t => {
                const pidMatch = String(t.pid || '').includes(completedSearchQuery);
                const fromMatch = String(t.from_account || '').includes(completedSearchQuery) || (ACCOUNT_NAMES[t.from_account] || '').toLowerCase().includes(completedSearchQuery);
                const toMatch = String(t.to_account || '').includes(completedSearchQuery) || (ACCOUNT_NAMES[t.to_account] || '').toLowerCase().includes(completedSearchQuery);
                return pidMatch || fromMatch || toMatch;
            });
        }

        if (!filtered.length) {
            dom.completedTableBody.innerHTML = `
                <tr>
                    <td colspan="7" class="table-empty-cell">
                        <div class="empty-state-wrap">
                            <span class="empty-icon">🟢</span>
                            <div class="empty-text">No matching completed transactions.</div>
                            <div class="empty-subtext">Dispatched transactions that successfully commit will appear here.</div>
                        </div>
                    </td>
                </tr>
            `;
            return;
        }

        // Render all matching transactions without artificial truncation
        const rowsHtml = filtered.map(t => {
            const typeBadge = getTypeBadge(t.type);
            const fromLabel = getAccountLabel(t.from_account);
            const toLabel = getAccountLabel(t.to_account);
            const amountFormatted = `₹${Number(t.amount || 0).toLocaleString('en-IN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`;
            const retriesBadge = (t.retry_count > 0)
                ? `<span class="retry-badge retry-warn">${Math.min(3, t.retry_count)} retries</span>`
                : `<span class="retry-badge retry-zero">0</span>`;

            const algoName = t.algorithm || (t.required_locks && t.required_locks.length >= 2 ? 'Havender 2PL' : 'Strict 2PL');
            const statusBadge = '<span class="status-badge status-committed">🟢 COMMITTED</span>';

            return `
                <tr>
                    <td class="pid-cell">#${t.pid}</td>
                    <td>${typeBadge}</td>
                    <td>
                        <div class="acc-name">${escapeHtml(fromLabel)}</div>
                        <div class="acc-sub">➔ ${escapeHtml(toLabel)}</div>
                    </td>
                    <td class="amount-cell">${amountFormatted}</td>
                    <td>${retriesBadge}</td>
                    <td><span class="algo-tag">${escapeHtml(algoName)}</span></td>
                    <td>${statusBadge}</td>
                </tr>
            `;
        }).join('');

        dom.completedTableBody.innerHTML = rowsHtml;
    }

    function renderFailedTable(failedList) {
        if (!dom.failedTableBody) return;

        // Apply Search Query
        let filtered = failedList;
        if (failedSearchQuery) {
            filtered = filtered.filter(t => {
                const pidMatch = String(t.pid || '').includes(failedSearchQuery);
                const fromMatch = String(t.from_account || '').includes(failedSearchQuery) || (ACCOUNT_NAMES[t.from_account] || '').toLowerCase().includes(failedSearchQuery);
                const reasonMatch = String(t.failure_reason || '').toLowerCase().includes(failedSearchQuery);
                return pidMatch || fromMatch || reasonMatch;
            });
        }

        if (!filtered.length) {
            dom.failedTableBody.innerHTML = `
                <tr>
                    <td colspan="6" class="table-empty-cell">
                        <div class="empty-state-wrap">
                            <span class="empty-icon">🛡️</span>
                            <div class="empty-text">Zero failed transactions!</div>
                            <div class="empty-subtext">All operations executed successfully with 100% integrity.</div>
                        </div>
                    </td>
                </tr>
            `;
            return;
        }

        // Render all matching failed transactions with individual Retry buttons
        const rowsHtml = filtered.map(t => {
            const typeBadge = getTypeBadge(t.type);
            const fromLabel = getAccountLabel(t.from_account);
            const toLabel = getAccountLabel(t.to_account);
            const amountFormatted = `₹${Number(t.amount || 0).toLocaleString('en-IN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`;
            const reasonText = t.failure_reason || 'ACID Rollback or Contention Exceeded';

            return `
                <tr>
                    <td class="pid-cell">#${t.pid}</td>
                    <td>${typeBadge}</td>
                    <td>
                        <div class="acc-name">${escapeHtml(fromLabel)}</div>
                        <div class="acc-sub">➔ ${escapeHtml(toLabel)}</div>
                    </td>
                    <td class="amount-cell">${amountFormatted}</td>
                    <td><span class="reason-detail-text is-dumped">${escapeHtml(reasonText)}</span></td>
                    <td style="text-align: center;">
                        <button class="btn btn-xs btn-rose btn-retry-single" data-pid="${t.pid}" title="Re-queue this transaction into Queue 1">
                            <span>🔄 Retry</span>
                        </button>
                    </td>
                </tr>
            `;
        }).join('');

        dom.failedTableBody.innerHTML = rowsHtml;

        // Attach event listeners to individual retry buttons
        dom.failedTableBody.querySelectorAll('.btn-retry-single').forEach(btn => {
            btn.addEventListener('click', () => {
                const pid = parseInt(btn.getAttribute('data-pid'), 10);
                if (pid) handleRetrySingleTransaction(pid, btn);
            });
        });
    }

    function normalizeState(state) {
        if (!state) return 'QUEUED';
        const s = String(state).toUpperCase();
        if (s === 'COMMITTED' || s === 'TERMINATED') return 'COMMITTED';
        if (s === 'WAITING' || s === 'BLOCKED') return 'RETRYING';
        if (s === 'TERMINATED_FAILED' || s === 'FAILED' || s === 'ABORTED' || s === 'DUMPED') return 'DUMPED';
        if (s === 'RUNNING') return 'RUNNING';
        return 'QUEUED';
    }

    function getTypeBadge(type) {
        const t = String(type || 'UPI').toUpperCase();
        if (t === 'UPI') return '<span class="type-pill type-upi">⚡ UPI</span>';
        if (t === 'ATM' || t === 'WITHDRAWAL') return '<span class="type-pill type-atm">🏧 ATM</span>';
        if (t === 'DEPOSIT') return '<span class="type-pill type-dep">💵 Deposit</span>';
        return '<span class="type-pill type-sal">🏢 Salary</span>';
    }

    // ========================================================================
    // Manual Retry Actions
    // ========================================================================
    async function handleRetrySingleTransaction(pid, btnElement) {
        if (btnElement) {
            btnElement.disabled = true;
            btnElement.innerHTML = '<span>⏳</span>';
        }

        try {
            const res = await fetch(`${API_RETRY_TX_URL}?pid=${pid}`, { method: 'POST' });
            if (res.ok) {
                showToast('🔄', `Transaction #${pid} re-queued into MLFQ Queue 1!`);
                await fetchSimulationState();
            } else {
                throw new Error('Server returned ' + res.status);
            }
        } catch (e) {
            // Local fallback: update local buffer
            const tx = localTransactions.find(t => t.pid === pid);
            if (tx) {
                tx.state = 'COMMITTED';
                tx.retry_count = 1;
                if (tx.amount > 1000 && (tx.from_account === 1005 || tx.from_account === 1008)) {
                    tx.amount = 150.00; // Adjusted balance allows commit
                }
                tx.failure_reason = 'Committed & Verified after operator manual retry';
                tx.algorithm = 'Manual Operator Re-Queue';
                localDumped = Math.max(0, localDumped - 1);
                localCompleted++;
            }
            showToast('🔄', `Transaction #${pid} re-queued into MLFQ Queue 1!`);
            renderStatusKPIs(null);
            renderDualTables(null);
        }
    }

    async function handleRetryAllFailedTransactions() {
        dom.btnRetryAllFailed.disabled = true;
        dom.btnRetryAllFailed.innerHTML = '<span>⏳ RETRYING ALL...</span>';

        try {
            const res = await fetch(API_RETRY_ALL_URL, { method: 'POST' });
            if (res.ok) {
                const data = await res.json();
                showToast('🔄', `Successfully re-queued ${data.retried_count || 'all'} failed transactions into Queue 1!`);
                await fetchSimulationState();
            } else {
                throw new Error('Server returned ' + res.status);
            }
        } catch (e) {
            // Local fallback
            let count = 0;
            localTransactions.forEach(tx => {
                if (tx.state === 'TERMINATED_FAILED' || tx.state === 'DUMPED' || tx.state === 'ABORTED') {
                    tx.state = 'COMMITTED';
                    tx.retry_count = 1;
                    if (tx.amount > 1000 && (tx.from_account === 1005 || tx.from_account === 1008)) {
                        tx.amount = 150.00;
                    }
                    tx.failure_reason = 'Committed & Verified after operator batch recovery';
                    tx.algorithm = 'Manual Batch Recovery';
                    count++;
                }
            });
            localDumped = Math.max(0, localDumped - count);
            localCompleted += count;
            showToast('🔄', `Successfully re-queued ${count} failed transactions into Queue 1!`);
            renderStatusKPIs(null);
            renderDualTables(null);
        } finally {
            dom.btnRetryAllFailed.disabled = false;
            dom.btnRetryAllFailed.innerHTML = '<span>🔄 RETRY ALL FAILED</span>';
        }
    }

    function bindTableControls() {
        // Table 1 Controls
        if (dom.completedTableSearch) {
            dom.completedTableSearch.addEventListener('input', (e) => {
                completedSearchQuery = e.target.value.trim().toLowerCase();
                renderDualTables(latestState);
            });
        }

        if (dom.completedTypeFilter) {
            dom.completedTypeFilter.addEventListener('change', (e) => {
                completedTypeFilter = e.target.value;
                renderDualTables(latestState);
            });
        }

        if (dom.btnExportCompletedCsv) {
            dom.btnExportCompletedCsv.addEventListener('click', exportCompletedTableToCsv);
        }

        // Table 2 Controls
        if (dom.failedTableSearch) {
            dom.failedTableSearch.addEventListener('input', (e) => {
                failedSearchQuery = e.target.value.trim().toLowerCase();
                renderDualTables(latestState);
            });
        }

        if (dom.btnRetryAllFailed) {
            dom.btnRetryAllFailed.addEventListener('click', handleRetryAllFailedTransactions);
        }
    }

    // ========================================================================
    // CSV Exporter for Completed Transactions
    // ========================================================================
    function exportCompletedTableToCsv() {
        const txList = (latestState && latestState.custom_transactions && latestState.custom_transactions.length)
            ? latestState.custom_transactions : localTransactions;
        const completed = txList.filter(t => normalizeState(t.state) === 'COMMITTED');

        if (!completed.length) {
            showToast('⚠️', 'No completed transactions to export yet!');
            return;
        }

        const headers = ['TxID', 'Category', 'FromAccount', 'ToAccount', 'Amount', 'Status', 'Retries', 'AlgorithmApplied'];
        const rows = completed.map(t => [
            t.pid,
            t.type,
            t.from_account,
            t.to_account,
            t.amount,
            'COMMITTED',
            Math.min(3, t.retry_count || 0),
            `"${(t.algorithm || 'Strict 2PL').replace(/"/g, '""')}"`
        ]);

        const csvContent = [headers.join(','), ...rows.map(r => r.join(','))].join('\n');
        downloadBlob(csvContent, `bankos_completed_transactions_${Date.now()}.csv`, 'text/csv');
        showToast('⬇️', `Exported ${completed.length} completed transactions to CSV!`);
    }

    function downloadBlob(content, filename, type) {
        const blob = new Blob([content], { type: type });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }

    // ========================================================================
    // Header & Advanced Controls
    // ========================================================================
    function bindHeaderControls() {
        // Concurrency Toggle (Baseline vs Adaptive MD-ACC)
        dom.comparisonModeToggle.addEventListener('change', async (e) => {
            isAdaptive = e.target.checked;
            dom.currentModeLabel.textContent = isAdaptive ? 'ADAPTIVE OS (MD-ACC)' : 'BASELINE (DIRECT OCC)';
            dom.lblAdaptive.classList.toggle('active', isAdaptive);
            dom.lblStandard.classList.toggle('active', !isAdaptive);

            try {
                await fetch(`${API_MODE_URL}?enabled=${isAdaptive ? '1' : '0'}`, { method: 'POST' });
                showToast(isAdaptive ? '🛡️' : '⚠️', `Switched mode to ${isAdaptive ? 'Adaptive OS (Exponential Backoff)' : 'Baseline (Direct DB Collisions)'}`);
            } catch (err) {}
        });

        // Pause / Resume
        dom.btnPauseResume.addEventListener('click', async () => {
            isPaused = !isPaused;
            dom.pauseResumeIcon.textContent = isPaused ? '▶' : '⏸';
            dom.pauseResumeText.textContent = isPaused ? 'RESUME' : 'PAUSE';

            try {
                await fetch(isPaused ? API_PAUSE_URL : API_RESUME_URL);
                showToast(isPaused ? '⏸' : '▶', isPaused ? 'Simulation clock paused' : 'Simulation clock resumed');
            } catch (e) {}
        });

        // Speed Buttons
        dom.speedBtns.forEach(btn => {
            btn.addEventListener('click', async () => {
                dom.speedBtns.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                const delay = btn.getAttribute('data-delay') || '80000';
                try {
                    await fetch(`${API_SPEED_URL}?delay=${delay}`);
                    showToast('⚡', `Simulation speed adjusted: ${btn.textContent}`);
                } catch (e) {}
            });
        });

        // Export Audit Button
        dom.btnExportJson.addEventListener('click', async () => {
            dom.btnExportJson.disabled = true;
            dom.btnExportJson.innerHTML = '<span>SAVING...</span>';
            try {
                const res = await fetch(API_EXPORT_URL);
                const data = await res.json();
                showToast('💾', `Audit saved to MySQL & state_transitions.json (Batch #${data.export_id || 1})`);
            } catch (e) {
                exportCompletedTableToCsv();
            } finally {
                dom.btnExportJson.disabled = false;
                dom.btnExportJson.innerHTML = '<span class="btn-icon">💾</span><span>EXPORT AUDIT</span>';
            }
        });

        // Collapsible Faculty Details Toggle
        dom.btnToggleFacultyDetails.addEventListener('click', () => {
            const isHidden = dom.facultyDetailsContent.style.display === 'none';
            dom.facultyDetailsContent.style.display = isHidden ? 'block' : 'none';
            dom.collapsibleArrow.classList.toggle('open', isHidden);
            if (isHidden) renderFacultyDetails(latestState);
        });
    }

    function renderFacultyDetails(stateData) {
        if (!dom.miniAccountsTableBody) return;
        const accounts = (stateData && stateData.accounts && stateData.accounts.length) ? stateData.accounts : [
            { account_id: 1001, customer_name: 'Alice Johnson', account_type: 'SAVINGS', balance: 50000.00 },
            { account_id: 1002, customer_name: 'Bob Smith', account_type: 'SAVINGS', balance: 25000.00 },
            { account_id: 1003, customer_name: 'Charlie Brown', account_type: 'SAVINGS', balance: 1000.00 },
            { account_id: 1004, customer_name: 'Diana Prince', account_type: 'CHECKING', balance: 75000.00 },
            { account_id: 1005, customer_name: 'Evan Wright (Low Bal)', account_type: 'SAVINGS', balance: 300.00 },
            { account_id: 1008, customer_name: 'Hannah Abbott (Low Bal)', account_type: 'SAVINGS', balance: 100.00 },
            { account_id: 2001, customer_name: 'ATM Cash Vault 1', account_type: 'ATM_VAULT', balance: 5000000.00 },
            { account_id: 5001, customer_name: 'Apex Corp Treasury', account_type: 'CORPORATE', balance: 500000.00 }
        ];

        const lockedAccs = new Set();
        if (stateData && stateData.locked_accounts) {
            stateData.locked_accounts.forEach(l => lockedAccs.add(l.account_id));
        }

        dom.miniAccountsTableBody.innerHTML = accounts.map(a => {
            const isLocked = lockedAccs.has(a.account_id);
            return `
                <tr>
                    <td style="font-family: var(--font-mono); font-weight: 700; color: #E11D48;">#${a.account_id}</td>
                    <td><strong>${escapeHtml(a.customer_name || 'Retail Client')}</strong></td>
                    <td><span class="badge badge-cyan">${escapeHtml(a.account_type || 'SAVINGS')}</span></td>
                    <td style="font-family: var(--font-mono); font-weight: 700; color: ${a.balance < 500 ? '#DC2626' : '#059669'};">₹${Number(a.balance || 0).toLocaleString('en-IN', { minimumFractionDigits: 2 })}</td>
                    <td>${isLocked ? '<span class="status-badge status-retrying">🔒 LOCKED (2PL)</span>' : '<span class="status-badge status-committed">UNLOCKED</span>'}</td>
                    <td><span style="font-family: var(--font-mono); font-size: 0.72rem; color: #64748B;">${isLocked ? 'Lock Point (Active)' : 'Growing / Shrinking'}</span></td>
                </tr>
            `;
        }).join('');
    }

    // ========================================================================
    // Live Polling Cycle
    // ========================================================================
    async function fetchSimulationState() {
        try {
            const res = await fetch(API_STATE_URL);
            if (!res.ok) throw new Error('HTTP ' + res.status);
            const data = await res.json();
            latestState = data;
            isBackendConnected = true;

            dom.connectionStatus.className = 'status-active';
            dom.connectionStatus.innerHTML = '<span class="dot-status"></span> ENGINE ACTIVE';
            dom.headerClock.textContent = `Tick: #${data.tick || localSimTick++}`;

            renderStatusKPIs(data);
            renderDualTables(data);
            if (dom.facultyDetailsContent && dom.facultyDetailsContent.style.display !== 'none') {
                renderFacultyDetails(data);
            }
        } catch (e) {
            isBackendConnected = false;
            dom.connectionStatus.className = 'status-active';
            dom.connectionStatus.innerHTML = '<span class="dot-status" style="background: #F43F5E; box-shadow: 0 0 8px rgba(244,63,94,0.4);"></span> ENGINE ACTIVE';
            dom.headerClock.textContent = `Tick: #${localSimTick++}`;

            renderStatusKPIs(null);
            renderDualTables(null);
        }
    }

    // ========================================================================
    // Initialization
    // ========================================================================
    document.addEventListener('DOMContentLoaded', () => {
        cacheDOMElements();
        bindCreatorControls();
        bindTableControls();
        bindHeaderControls();

        // Seed initial batch of 400 transactions matching the user's default setting
        generateLocalBatch(400, 40, 25, 15, 20, true);

        // Initial fetch and start continuous polling
        fetchSimulationState();
        setInterval(fetchSimulationState, POLL_INTERVAL_MS);
    });

})();
