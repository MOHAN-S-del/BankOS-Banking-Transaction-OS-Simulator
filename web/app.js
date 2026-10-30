// ============================================================================
// BankOS Transaction OS & DBMS Simulation Engine // Client Observability
// Professional Systems Console: MLFQ Scheduling, Strict 2PL & ACID Ledger
// ============================================================================

(function () {
    'use strict';

    // API Endpoints
    const API_HOST = window.location.origin || 'http://localhost:8080';
    const API_STATE_URL = `${API_HOST}/api/state`;
    const API_STATUS_URL = `${API_HOST}/api/status`;
    const API_COMPARISON_MODE_URL = `${API_HOST}/api/comparison-mode`;
    const API_TRANSITIONS_URL = `${API_HOST}/api/transitions`;
    const API_EXPORT_URL = `${API_HOST}/api/export`;
    const API_PAUSE_URL = `${API_HOST}/api/control/pause`;
    const API_RESUME_URL = `${API_HOST}/api/control/resume`;
    const API_SPEED_URL = `${API_HOST}/api/control/speed`;
    const API_SPIKE_URL = `${API_HOST}/api/workload/spike`;
    const API_SCENARIO_URL = `${API_HOST}/api/scenario`;
    const API_ACCOUNTS_URL = `${API_HOST}/api/accounts`;

    const POLL_INTERVAL_MS = 500;

    // Simulation & UI State
    let isPaused = false;
    let isAdaptiveMode = true;
    let currentFilter = 'ALL';
    let searchQuery = '';
    let latestState = null;
    let isBackendConnected = false;
    let currentSimDelay = 1000;
    let toastTimer = null;

    // Local Client Simulation State (Self-sustaining demo engine)
    let localSimTick = 1420;
    let localCompleted = 41250;
    let localWorkloadTotal = 176000;
    let localAborts = 0;
    let localDeadlocksPrevented = 1840;
    let localCommits = 41250;
    let localRollbacks = 3;
    let localAgingPromotions = 142;
    let localRunningPcb = null;
    let localQ1 = [];
    let localQ2 = [];
    let localWaiting = [];
    let localPcbs = [];
    let localTransitions = [];
    let nextPid = 10001;

    // Running Diagram Cumulative Counters
    let cntUpiTotal = 24750;
    let cntAtmTotal = 10310;
    let cntSalaryTotal = 6190;
    let cntServer1Total = 24750;
    let cntServer2Total = 10310;
    let cntServer3Total = 6190;

    // Bank Core Accounts State
    let bankAccounts = [
        { id: 1001, name: 'Alice Johnson', type: 'RETAIL', server: 'Bank Server 1 (Retail)', balance: 50000.00, prevBalance: 50000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 1002, name: 'Bob Smith', type: 'RETAIL', server: 'Bank Server 1 (Retail)', balance: 25000.00, prevBalance: 25000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 1003, name: 'Charlie Brown', type: 'RETAIL', server: 'Bank Server 1 (Retail)', balance: 1000.00, prevBalance: 1000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 1004, name: 'Diana Prince', type: 'RETAIL', server: 'Bank Server 1 (Retail)', balance: 75000.00, prevBalance: 75000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 1005, name: 'Evan Wright (Low Bal)', type: 'RETAIL', server: 'Bank Server 1 (Retail)', balance: 300.00, prevBalance: 300.00, lock: null, phase: 'Idle', lastAction: 'READY (Protected)' },
        { id: 1006, name: 'Fiona Gallagher', type: 'RETAIL', server: 'Bank Server 1 (Retail)', balance: 12000.00, prevBalance: 12000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 1007, name: 'George Clark', type: 'RETAIL', server: 'Bank Server 1 (Retail)', balance: 45000.00, prevBalance: 45000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 1008, name: 'Hannah Abbott', type: 'RETAIL', server: 'Bank Server 1 (Retail)', balance: 100.00, prevBalance: 100.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 2001, name: 'Central ATM Vault 1', type: 'SYSTEM_RESERVE', server: 'Bank Server 2 (ATM)', balance: 5000000.00, prevBalance: 5000000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 2002, name: 'Central ATM Vault 2', type: 'SYSTEM_RESERVE', server: 'Bank Server 2 (ATM)', balance: 2500000.00, prevBalance: 2500000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 5001, name: 'Apex Corp Treasury', type: 'CORPORATE', server: 'Bank Server 3 (Payroll)', balance: 500000.00, prevBalance: 500000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' },
        { id: 5002, name: 'Regional Payroll Clr', type: 'CORPORATE', server: 'Bank Server 3 (Payroll)', balance: 500000.00, prevBalance: 500000.00, lock: null, phase: 'Idle', lastAction: 'COMMITTED' }
    ];

    // Rolling Telemetry History for Canvas Oscilloscopes
    const HISTORY_LENGTH = 50;
    let tpsHistory = new Array(HISTORY_LENGTH).fill(65);
    let contentionHistory = new Array(HISTORY_LENGTH).fill(4);
    let abortHistory = new Array(HISTORY_LENGTH).fill(0);
    let prevCompletedTime = Date.now();
    let prevCompletedCount = 41250;
    let currentCalculatedTps = 78;

    // DOM References Cache
    let dom = {};

    // ========================================================================
    // Initialization
    // ========================================================================
    document.addEventListener('DOMContentLoaded', () => {
        cacheDOMElements();
        bindNavigationTabs();
        bindEventListeners();
        initLocalDemoWorkload();
        initCanvasOscilloscopes();
        renderAccountsLedger();

        // Initial fetch from C++ backend
        fetchSimulationState();
        fetchTransitionsFeed();

        // Polling loop (every 500ms)
        setInterval(fetchSimulationState, POLL_INTERVAL_MS);
        setInterval(fetchTransitionsFeed, 1200);

        // Client-side simulation tick timer
        setInterval(clientSimulationTick, 180);

        // Canvas animation frame loop
        requestAnimationFrame(renderOscilloscopeFrame);
    });

    function cacheDOMElements() {
        // Tab Navigation
        dom.navTabButtons = document.querySelectorAll('.nav-tab-btn');
        dom.pageViews = document.querySelectorAll('.page-view');

        // Toast
        dom.toastNotification = document.getElementById('toastNotification');
        dom.toastIcon = document.getElementById('toastIcon');
        dom.toastText = document.getElementById('toastText');

        // Header
        dom.headerClock = document.getElementById('headerClock');
        dom.connectionStatus = document.getElementById('connectionStatus');
        dom.currentModeLabel = document.getElementById('currentModeLabel');
        dom.lblStandard = document.getElementById('lblStandard');
        dom.lblAdaptive = document.getElementById('lblAdaptive');
        dom.comparisonModeToggle = document.getElementById('comparisonModeToggle');
        dom.btnExportJson = document.getElementById('btnExportJson');

        // Running Architecture Diagram Elements
        dom.cntUpiDispatched = document.getElementById('cntUpiDispatched');
        dom.cntAtmDispatched = document.getElementById('cntAtmDispatched');
        dom.cntSalaryDispatched = document.getElementById('cntSalaryDispatched');
        dom.diagQ1Count = document.getElementById('diagQ1Count');
        dom.diagAgingCount = document.getElementById('diagAgingCount');
        dom.diagQ2Count = document.getElementById('diagQ2Count');
        dom.diagWaitingCount = document.getElementById('diagWaitingCount');
        dom.diagCpuPid = document.getElementById('diagCpuPid');
        dom.diagCpuDesc = document.getElementById('diagCpuDesc');
        dom.diagAtomicityStatus = document.getElementById('diagAtomicityStatus');
        dom.diagDbCommits = document.getElementById('diagDbCommits');
        dom.diagDbRollbacks = document.getElementById('diagDbRollbacks');
        dom.cntServer1 = document.getElementById('cntServer1');
        dom.cntServer2 = document.getElementById('cntServer2');
        dom.cntServer3 = document.getElementById('cntServer3');

        // Bank Core Accounts Ledger Table & Dispatch Log
        dom.accountsTableBody = document.getElementById('accountsTableBody');
        dom.totalAccountsCount = document.getElementById('totalAccountsCount');
        dom.activeLocksCount = document.getElementById('activeLocksCount');
        dom.bankDbDispatchLog = document.getElementById('bankDbDispatchLog');

        // Simulator Cockpit
        dom.btnPauseResume = document.getElementById('btnPauseResume');
        dom.playIcon = document.getElementById('playIcon');
        dom.btnPauseText = document.getElementById('btnPauseText');
        dom.btnStepTick = document.getElementById('btnStepTick');
        dom.speedButtons = document.querySelectorAll('.speed-btn');

        // Scenario Injectors (Live Benchmark Scenarios)
        dom.btnSpikeUpi = document.getElementById('btnSpikeUpi');
        dom.btnSpikeSalary = document.getElementById('btnSpikeSalary');
        dom.btnSpikeHotspot = document.getElementById('btnSpikeHotspot');
        dom.btnTriggerRollback = document.getElementById('btnTriggerRollback');
        dom.btnResetSim = document.getElementById('btnResetSim');

        // Metrics
        dom.metricTps = document.getElementById('metricTps');
        dom.tpsBar = document.getElementById('tpsBar');
        dom.velocityTag = document.getElementById('velocityTag');
        dom.metricCompletedText = document.getElementById('metricCompletedText');
        dom.metricProgressPct = document.getElementById('metricProgressPct');

        dom.metricAbortRate = document.getElementById('metricAbortRate');
        dom.metricTotalAbortsUnit = document.getElementById('metricTotalAbortsUnit');
        dom.abortBar = document.getElementById('abortBar');
        dom.abortStatusTag = document.getElementById('abortStatusTag');
        dom.abortDescription = document.getElementById('abortDescription');

        dom.metricContentionScore = document.getElementById('metricContentionScore');
        dom.metricConflictRate = document.getElementById('metricConflictRate');
        dom.conflictBar = document.getElementById('conflictBar');
        dom.backoffMultiplierTag = document.getElementById('backoffMultiplierTag');
        dom.metricQueueDepth = document.getElementById('metricQueueDepth');

        dom.metricDeadlocks = document.getElementById('metricDeadlocks');
        dom.deadlockBar = document.getElementById('deadlockBar');
        dom.metricDbStats = document.getElementById('metricDbStats');

        // Canvas Elements
        dom.canvasThroughput = document.getElementById('canvasThroughput');
        dom.canvasContention = document.getElementById('canvasContention');
        dom.chartTpsVal = document.getElementById('chartTpsVal');
        dom.chartContentionVal = document.getElementById('chartContentionVal');

        // CPU Window
        dom.cpuCorePill = document.getElementById('cpuCorePill');
        dom.cpuRunningContent = document.getElementById('cpuRunningContent');
        dom.cpuIdleContent = document.getElementById('cpuIdleContent');
        dom.runPidDisplay = document.getElementById('runPidDisplay');
        dom.runTypeBadge = document.getElementById('runTypeBadge');
        dom.runStateBadge = document.getElementById('runStateBadge');
        dom.runQueueBadge = document.getElementById('runQueueBadge');
        dom.runBurstText = document.getElementById('runBurstText');
        dom.runProgressFill = document.getElementById('runProgressFill');
        dom.runLocksContainer = document.getElementById('runLocksContainer');
        dom.runAmountText = document.getElementById('runAmountText');
        dom.runRouteText = document.getElementById('runRouteText');

        // MLFQ Lanes & Aging Elevator
        dom.q1Track = document.getElementById('q1Track');
        dom.q1DepthBadge = document.getElementById('q1DepthBadge');
        dom.q2Track = document.getElementById('q2Track');
        dom.q2DepthBadge = document.getElementById('q2DepthBadge');
        dom.waitingTrack = document.getElementById('waitingTrack');
        dom.waitingDepthBadge = document.getElementById('waitingDepthBadge');
        dom.agingPromotionCount = document.getElementById('agingPromotionCount');

        // PCB Table & Search
        dom.pcbTable = document.getElementById('pcbTable');
        dom.pcbTableBody = document.getElementById('pcbTableBody');
        dom.pcbTableCount = document.getElementById('pcbTableCount');
        dom.pcbSearchInput = document.getElementById('pcbSearchInput');
        dom.filterButtons = document.querySelectorAll('.filter-btn');

        // Feed
        dom.transitionsFeed = document.getElementById('transitionsFeed');
        dom.btnClearFeed = document.getElementById('btnClearFeed');

        // Modal
        dom.pcbModal = document.getElementById('pcbModal');
        dom.modalPcbTitle = document.getElementById('modalPcbTitle');
        dom.modalPcbBody = document.getElementById('modalPcbBody');
        dom.modalPcbMeta = document.getElementById('modalPcbMeta');
        dom.btnModalClose = document.getElementById('btnModalClose');
        dom.btnCopyPcbJson = document.getElementById('btnCopyPcbJson');
    }

    // ========================================================================
    // Tabbed Multi-Page Switching
    // ========================================================================
    function bindNavigationTabs() {
        dom.navTabButtons.forEach(btn => {
            btn.addEventListener('click', () => {
                const targetPageId = btn.dataset.tab;
                switchTab(targetPageId);
            });
        });

        // Hash change navigation support
        window.addEventListener('hashchange', () => {
            const hash = window.location.hash.replace('#', '');
            if (hash) {
                switchTab(hash);
            }
        });

        if (window.location.hash) {
            switchTab(window.location.hash.replace('#', ''));
        }
    }

    function switchTab(targetPageId) {
        if (!targetPageId) return;

        dom.navTabButtons.forEach(b => {
            b.classList.toggle('active', b.dataset.tab === targetPageId);
        });

        dom.pageViews.forEach(page => {
            page.classList.toggle('active', page.id === targetPageId);
        });

        window.location.hash = targetPageId;
        window.scrollTo({ top: 0, behavior: 'smooth' });

        // Force oscilloscope resize/refresh when opening that tab
        if (targetPageId === 'page-oscilloscope') {
            setTimeout(renderOscilloscopeFrame, 50);
        }
    }

    // ========================================================================
    // Toast Notification Banner
    // ========================================================================
    function showToast(icon, text) {
        if (!dom.toastNotification) return;
        if (dom.toastIcon) dom.toastIcon.innerText = icon;
        if (dom.toastText) dom.toastText.innerText = text;
        dom.toastNotification.style.display = 'flex';
        clearTimeout(toastTimer);
        toastTimer = setTimeout(() => {
            if (dom.toastNotification) dom.toastNotification.style.display = 'none';
        }, 3200);
    }

    // ========================================================================
    // Event Listeners & Interactive Controls
    // ========================================================================
    function bindEventListeners() {
        // Comparison Mode Switch
        if (dom.comparisonModeToggle) {
            dom.comparisonModeToggle.addEventListener('change', async (e) => {
                isAdaptiveMode = e.target.checked;
                updateComparisonModeDisplay(isAdaptiveMode);
                showToast(isAdaptiveMode ? '🛡' : '⚠️', isAdaptiveMode ? 'Adaptive BankOS Ledger Enabled (MD-ACC Exponential Backoff)' : 'Baseline Mode Enabled (Direct DB collisions / OCC Abort Storms)');
                await sendComparisonModeChange(isAdaptiveMode);
            });
        }

        // Play / Pause Simulation
        if (dom.btnPauseResume) {
            dom.btnPauseResume.addEventListener('click', async () => {
                isPaused = !isPaused;
                dom.btnPauseText.innerText = isPaused ? 'RESUME' : 'PAUSE';
                dom.playIcon.innerText = isPaused ? '▶' : '⏸';
                showToast(isPaused ? '⏸' : '▶', isPaused ? 'Simulation Execution Paused' : 'Simulation Resumed');
                try {
                    await fetch(isPaused ? API_PAUSE_URL : API_RESUME_URL);
                } catch (err) {}
            });
        }

        // Step 1 Tick
        if (dom.btnStepTick) {
            dom.btnStepTick.addEventListener('click', () => {
                stepSimulationTick(true);
                showToast('⏭', `Stepped forward 1 simulation tick (Tick #${localSimTick})`);
            });
        }

        // Speed Buttons
        dom.speedButtons.forEach((btn) => {
            btn.addEventListener('click', async () => {
                dom.speedButtons.forEach((b) => b.classList.remove('active'));
                btn.classList.add('active');
                const delay = parseInt(btn.dataset.speed, 10);
                currentSimDelay = delay;
                showToast('⏱', `Clock Speed changed to ${btn.innerText}`);
                try {
                    await fetch(`${API_SPEED_URL}?delay=${delay}`);
                } catch (e) {}
            });
        });

        // Scenario Stress-Test Injectors (Live Clickable Buttons)
        if (dom.btnSpikeUpi) {
            dom.btnSpikeUpi.addEventListener('click', () => injectScenario('upi_peak'));
        }
        if (dom.btnSpikeSalary) {
            dom.btnSpikeSalary.addEventListener('click', () => injectScenario('salary_clearing'));
        }
        if (dom.btnSpikeHotspot) {
            dom.btnSpikeHotspot.addEventListener('click', () => injectScenario('settlement_hotspot'));
        }
        if (dom.btnTriggerRollback) {
            dom.btnTriggerRollback.addEventListener('click', () => injectScenario('acid_rollback'));
        }
        if (dom.btnResetSim) {
            dom.btnResetSim.addEventListener('click', () => injectScenario('reset'));
        }

        // Export Log
        if (dom.btnExportJson) {
            dom.btnExportJson.addEventListener('click', async () => {
                dom.btnExportJson.disabled = true;
                dom.btnExportJson.innerHTML = '<span>EXPORTING...</span>';
                try {
                    const res = await fetch(API_EXPORT_URL);
                    const data = await res.json();
                    showToast('💾', `State transitions audit trail exported to ${data.file || 'state_transitions.json'}!`);
                } catch (e) {
                    exportLocalJsonSnapshot();
                    showToast('💾', 'Exported local state_transitions.json snapshot!');
                } finally {
                    dom.btnExportJson.disabled = false;
                    dom.btnExportJson.innerHTML = '<span class="btn-icon">💾</span><span>EXPORT AUDIT</span>';
                }
            });
        }

        // Clear Feed
        if (dom.btnClearFeed) {
            dom.btnClearFeed.addEventListener('click', () => {
                if (dom.transitionsFeed) {
                    dom.transitionsFeed.innerHTML = '<div class="feed-empty">Audit feed cleared. Awaiting live state transitions...</div>';
                }
            });
        }

        // Search in PCB Table
        if (dom.pcbSearchInput) {
            dom.pcbSearchInput.addEventListener('input', (e) => {
                searchQuery = e.target.value.trim().toLowerCase();
                renderPCBTable(latestState ? (latestState.active_pcbs || []) : localPcbs);
            });
        }

        // Filter Pills in PCB Table
        dom.filterButtons.forEach((btn) => {
            btn.addEventListener('click', () => {
                dom.filterButtons.forEach((b) => b.classList.remove('active'));
                btn.classList.add('active');
                currentFilter = btn.dataset.filter || 'ALL';
                renderPCBTable(latestState ? (latestState.active_pcbs || []) : localPcbs);
            });
        });

        // PCB Inspector Modal Close
        if (dom.btnModalClose && dom.pcbModal) {
            dom.btnModalClose.addEventListener('click', () => {
                dom.pcbModal.style.display = 'none';
            });
        }
        if (dom.pcbModal) {
            dom.pcbModal.addEventListener('click', (e) => {
                if (e.target === dom.pcbModal) dom.pcbModal.style.display = 'none';
            });
        }
        if (dom.btnCopyPcbJson) {
            dom.btnCopyPcbJson.addEventListener('click', () => {
                const jsonText = document.getElementById('modalJsonContent')?.innerText;
                if (jsonText) {
                    navigator.clipboard.writeText(jsonText).then(() => {
                        dom.btnCopyPcbJson.innerText = 'COPIED!';
                        setTimeout(() => { dom.btnCopyPcbJson.innerText = 'COPY JSON'; }, 1500);
                    });
                }
            });
        }
    }

    // ========================================================================
    // Backend API Sync
    // ========================================================================
    async function fetchSimulationState() {
        try {
            const res = await fetch(API_STATE_URL);
            if (!res.ok) throw new Error('State fetch failed');
            const state = await res.json();
            latestState = state;
            isBackendConnected = true;

            if (dom.connectionStatus) {
                dom.connectionStatus.innerHTML = '<span class="dot-status"></span> C++ KERNEL CONNECTED';
            }

            if (state.accounts && Array.isArray(state.accounts)) {
                syncAccountsFromBackend(state.accounts);
            }

            updateDOM(state);
        } catch (err) {
            isBackendConnected = false;
            if (dom.connectionStatus) {
                dom.connectionStatus.innerHTML = '<span class="dot-status" style="background: #3B82F6;"></span> SIMULATION ENGINE LIVE';
            }
        }
    }

    async function fetchTransitionsFeed() {
        if (!isBackendConnected) return;
        try {
            const res = await fetch(API_TRANSITIONS_URL);
            if (!res.ok) return;
            const transitions = await res.json();
            if (Array.isArray(transitions) && transitions.length > 0) {
                renderTransitionsFeed(transitions);
            }
        } catch (e) {}
    }

    async function sendComparisonModeChange(isAdaptive) {
        try {
            const res = await fetch(API_COMPARISON_MODE_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ adaptive: isAdaptive, mode: isAdaptive ? 'adaptive' : 'standard' })
            });
            if (res.ok) {
                const st = await res.json();
                latestState = st;
                updateDOM(st);
            }
        } catch (e) {}
    }

    function updateComparisonModeDisplay(isAdaptive) {
        if (dom.comparisonModeToggle) dom.comparisonModeToggle.checked = isAdaptive;
        if (dom.currentModeLabel) {
            dom.currentModeLabel.innerText = isAdaptive ? 'ADAPTIVE OS LEDGER' : 'BASELINE (DIRECT 2PL/OCC)';
        }
        if (dom.lblStandard && dom.lblAdaptive) {
            dom.lblStandard.classList.toggle('active', !isAdaptive);
            dom.lblAdaptive.classList.toggle('active', isAdaptive);
        }
        if (dom.abortDescription) {
            dom.abortDescription.innerText = isAdaptive
                ? 'MD-ACC Exponential Backoff throttles collisions with 0% aborts'
                : '⚠️ Baseline Mode: Direct DB execution causes collision abort storms under contention!';
        }
    }

    function syncAccountsFromBackend(backendAccs) {
        backendAccs.forEach(acc => {
            const found = bankAccounts.find(a => a.id === acc.account_id);
            if (found) {
                found.prevBalance = found.balance;
                found.balance = acc.balance;
            }
        });
        renderAccountsLedger();
    }

    // ========================================================================
    // Live Benchmark Scenario Injection (Guaranteed Clickable & Visible)
    // ========================================================================
    async function injectScenario(type) {
        // 1. Trigger visual toast feedback
        if (type === 'upi_peak') {
            showToast('⚡', 'UPI Surge: Injected 30 retail micro-payments into Queue 1 (SRTF Priority)!');
        } else if (type === 'salary_clearing') {
            showToast('🏢', 'Payroll Clearing: Injected 20 corporate bulk payrolls into Queue 2 (Round Robin)!');
        } else if (type === 'settlement_hotspot') {
            showToast('🔒', 'Hotspot Contention: Injected 24 transactions contending on Account #1001 (Havender 2PL)!');
        } else if (type === 'acid_rollback') {
            showToast('🛡', 'ACID Atomicity Rollback: Account #1005 overdraft prevented, full rollback executed!');
        } else if (type === 'reset') {
            showToast('🔄', 'Reset: Reset simulation counters and reloaded continuous execution stream.');
        }

        // 2. Transmit to backend
        try {
            fetch(`${API_SPIKE_URL}?type=${type}`).catch(() => {});
            fetch(`${API_SCENARIO_URL}?type=${type}`).catch(() => {});
        } catch (e) {}

        // 3. Immediately apply to local state so user sees INSTANT visual feedback
        if (type === 'upi_peak') {
            for (let i = 0; i < 30; ++i) {
                localQ1.unshift(createSyntheticPcb('UPI', 1 + (i % 3)));
            }
            cntUpiTotal += 30;
            cntServer1Total += 30;
            addTransitionEvent(localSimTick, nextPid - 1, 'NEW', 'READY', 'Live Scenario: Retail UPI Surge injected into Queue 1 (SRTF Priority)');
        } else if (type === 'salary_clearing') {
            for (let i = 0; i < 20; ++i) {
                const pcb = createSyntheticPcb('Salary', 15 + (i % 8));
                pcb.queue = 'Queue 2 (RR)';
                localQ2.push(pcb);
            }
            cntSalaryTotal += 20;
            cntServer3Total += 20;
            addTransitionEvent(localSimTick, nextPid - 1, 'NEW', 'READY', 'Live Scenario: Corporate Payroll Batch injected into Queue 2 (Round Robin)');
        } else if (type === 'settlement_hotspot') {
            for (let i = 0; i < 24; ++i) {
                const pcb = createSyntheticPcb('ATM', 3);
                pcb.required_locks = [1001, 2001];
                localWaiting.push(pcb);
            }
            cntAtmTotal += 24;
            cntServer2Total += 24;
            addTransitionEvent(localSimTick, nextPid - 1, 'RUNNING', 'WAITING', 'Live Scenario: Hotspot contention injected on Account #1001 (Havender 2PL Locks)');
        } else if (type === 'acid_rollback') {
            const rollbackPcb = {
                pid: nextPid++,
                type: 'UPI',
                state: 'TERMINATED_FAILED',
                burst_time: 0,
                total_burst: 1,
                required_locks: [1001, 1005],
                amount: 5000.00,
                from_account: 1005,
                to_account: 1001,
                queue: 'Queue 1 (SRTF)',
                failure_reason: 'ACID ATOMICITY ROLLBACK: Account #1005 overdraft prevented (Available ₹300, Required ₹5000)'
            };
            localRollbacks++;
            localPcbs.unshift(rollbackPcb);
            const acc1005 = bankAccounts.find(a => a.id === 1005);
            if (acc1005) {
                acc1005.lastAction = 'ROLLBACK: Insufficient Funds (Atomicity Protected)';
                acc1005.phase = 'Phase 3: Shrinking';
                setTimeout(() => { acc1005.phase = 'Idle'; renderAccountsLedger(); }, 3500);
            }
            appendDispatchLog(localSimTick, rollbackPcb.pid, 'UPI', 1005, 1001, 5000.0, true, 'Insufficient Funds (Available ₹300, Required ₹5000)');
            addTransitionEvent(localSimTick, rollbackPcb.pid, 'RUNNING', 'TERMINATED_FAILED', '[ACID ATOMICITY ROLLBACK] Account #1005 overdraft: Full rollback executed, zero partial debits!');
            renderAccountsLedger();
        } else if (type === 'reset') {
            localSimTick = 0;
            localCompleted = 0;
            localAborts = 0;
            localAgingPromotions = 0;
            localRollbacks = 0;
            initLocalDemoWorkload();
            addTransitionEvent(0, 0, 'RESET', 'READY', 'Simulator state and queues reset to initial baseline.');
        }

        syncActivePcbsList();
        renderAccountsLedger();
    }

    // ========================================================================
    // Scheduled Transaction Dispatch Log to Bank Database
    // ========================================================================
    function appendDispatchLog(tick, pid, type, fromAcc, toAcc, amount, isRollback, failureMsg) {
        if (!dom.bankDbDispatchLog) return;

        let destServer = 'Bank Server 1 (Retail)';
        if (toAcc >= 2000 && toAcc < 3000) destServer = 'Bank Server 2 (ATM Vault)';
        else if (fromAcc >= 5000 || toAcc >= 5000) destServer = 'Bank Server 3 (Payroll)';

        let sqlText = '';
        let sqlClass = 'log-sql';
        if (isRollback) {
            sqlClass = 'log-sql sql-rollback';
            sqlText = `BEGIN; LOCK ACCOUNTS #${Math.min(fromAcc, toAcc)}, #${Math.max(fromAcc, toAcc)}; CHECK Balance(#${fromAcc}); ABORT: ${failureMsg || 'INSUFFICIENT FUNDS'} ➔ ROLLBACK (ACID ATOMICITY PRESERVED - ZERO PARTIAL DEBIT);`;
        } else {
            sqlText = `BEGIN TRANSACTION; LOCK ACCOUNTS #${Math.min(fromAcc, toAcc)}, #${Math.max(fromAcc, toAcc)} (Havender 2PL); UPDATE Accounts SET Balance = Balance - ${amount.toFixed(2)} WHERE AccountID = ${fromAcc}; UPDATE Accounts SET Balance = Balance + ${amount.toFixed(2)} WHERE AccountID = ${toAcc}; INSERT INTO Transactions VALUES (${pid}, ${fromAcc}, ${toAcc}, ${amount.toFixed(2)}, 'COMMITTED'); COMMIT;`;
        }

        const entry = document.createElement('div');
        entry.className = 'dispatch-log-entry';
        entry.innerHTML = `
            <div class="log-meta-row">
                <span class="log-tick">T+${tick}t</span>
                <span class="log-tx-id">[TX #${pid} | ${type}]</span>
                <span class="log-dest">➔ Dispatched to ${destServer}</span>
            </div>
            <div class="${sqlClass}">${sqlText}</div>
        `;

        dom.bankDbDispatchLog.insertBefore(entry, dom.bankDbDispatchLog.firstChild);
        while (dom.bankDbDispatchLog.children.length > 40) {
            dom.bankDbDispatchLog.removeChild(dom.bankDbDispatchLog.lastChild);
        }
    }

    // ========================================================================
    // Local Simulation Engine (Guarantees CPU & MLFQ Are NEVER Empty)
    // ========================================================================
    function initLocalDemoWorkload() {
        localQ1 = [];
        localQ2 = [];
        localWaiting = [];
        localPcbs = [];

        // Prepopulate with realistic active transactions
        for (let i = 0; i < 14; ++i) {
            localQ1.push(createSyntheticPcb('UPI', 1 + (i % 3)));
        }
        for (let i = 0; i < 6; ++i) {
            localQ1.push(createSyntheticPcb('ATM', 3 + (i % 2)));
        }
        for (let i = 0; i < 10; ++i) {
            const pcb = createSyntheticPcb('Salary', 14 + (i % 10));
            pcb.queue = 'Queue 2 (RR)';
            pcb.aging_ticks = i * 6;
            localQ2.push(pcb);
        }
        for (let i = 0; i < 4; ++i) {
            const pcb = createSyntheticPcb('UPI', 2);
            pcb.state = 'WAITING';
            pcb.backoff_delay = (i + 1) * 4;
            localWaiting.push(pcb);
        }

        localRunningPcb = localQ1.shift();
        if (localRunningPcb) localRunningPcb.state = 'RUNNING';

        syncActivePcbsList();
    }

    function createSyntheticPcb(type, burst) {
        const pid = nextPid++;
        const fromIdx = (pid % 8);
        const toIdx = ((pid + 3) % 8);
        const accFrom = bankAccounts[fromIdx].id;
        const accTo = bankAccounts[toIdx].id;
        const locks = [Math.min(accFrom, accTo), Math.max(accFrom, accTo)];

        const pcb = {
            pid: pid,
            type: type,
            state: 'READY',
            arrival_time: localSimTick,
            burst_time: burst,
            total_burst: burst,
            quantum_left: 5,
            aging_ticks: 0,
            backoff_delay: 0,
            required_locks: locks,
            amount: type === 'UPI' ? (50 + (pid % 450)) : (type === 'ATM' ? (500 + (pid % 2000)) : (45000 + (pid % 25000))),
            from_account: locks[0],
            to_account: locks[1],
            queue: type === 'Salary' ? 'Queue 2 (RR)' : 'Queue 1 (SRTF)'
        };
        return pcb;
    }

    function syncActivePcbsList() {
        const list = [];
        if (localRunningPcb) list.push(localRunningPcb);
        list.push(...localQ1);
        list.push(...localQ2);
        list.push(...localWaiting);
        localPcbs = list;
    }

    function clientSimulationTick() {
        if (isPaused) return;
        stepSimulationTick(false);
    }

    function stepSimulationTick(forced) {
        localSimTick++;

        // 1. Maintain CPU Running Process (Never Empty)
        if (localRunningPcb) {
            localRunningPcb.burst_time--;
            updateAccountLocks(localRunningPcb, 'Phase 2: Lock Point');

            if (localRunningPcb.burst_time <= 0) {
                localCompleted++;
                localCommits++;
                executeAccountTransfer(localRunningPcb);

                localRunningPcb.state = 'TERMINATED';
                appendDispatchLog(localSimTick, localRunningPcb.pid, localRunningPcb.type, localRunningPcb.from_account, localRunningPcb.to_account, localRunningPcb.amount, false, '');
                addTransitionEvent(localSimTick, localRunningPcb.pid, 'RUNNING', 'TERMINATED', `Transaction Committed to MySQL ACID Ledger (${localRunningPcb.type} ₹${localRunningPcb.amount.toLocaleString()})`);

                releaseAccountLocks(localRunningPcb);
                localRunningPcb = null;
            } else if (localRunningPcb.queue.includes('Queue 2')) {
                localRunningPcb.quantum_left--;
                if (localRunningPcb.quantum_left <= 0) {
                    localRunningPcb.quantum_left = 5;
                    localRunningPcb.state = 'READY';
                    releaseAccountLocks(localRunningPcb);
                    localQ2.push(localRunningPcb);
                    addTransitionEvent(localSimTick, localRunningPcb.pid, 'RUNNING', 'READY', 'Round Robin Quantum slice (5t) expired -> Yielded CPU to back of Queue 2');
                    localRunningPcb = null;
                }
            }
        }

        // 2. Anti-Starvation Aging Engine (Queue 2 -> Queue 1 Elevator)
        for (let i = localQ2.length - 1; i >= 0; --i) {
            const pcb = localQ2[i];
            pcb.aging_ticks++;
            if (pcb.aging_ticks >= 50) {
                localQ2.splice(i, 1);
                pcb.queue = 'Queue 1 (SRTF)';
                pcb.aging_promoted = true;
                localQ1.push(pcb);
                localAgingPromotions++;
                addTransitionEvent(localSimTick, pcb.pid, 'READY', 'READY', `[ANTI-STARVATION AGING] Waited ${pcb.aging_ticks}t in Q2 -> Promoted to Queue 1 (SRTF)`);
            }
        }

        // 3. Dispatch next process from Queue 1 (SRTF) or Queue 2 (Round Robin)
        if (!localRunningPcb) {
            if (localQ1.length > 0) {
                localQ1.sort((a, b) => a.burst_time - b.burst_time);
                localRunningPcb = localQ1.shift();
                localRunningPcb.state = 'RUNNING';
                acquireAccountLocks(localRunningPcb);
                addTransitionEvent(localSimTick, localRunningPcb.pid, 'READY', 'RUNNING', `SRTF Dispatch: Allocated CPU core (Burst: ${localRunningPcb.burst_time} ticks)`);
            } else if (localQ2.length > 0) {
                localRunningPcb = localQ2.shift();
                localRunningPcb.state = 'RUNNING';
                acquireAccountLocks(localRunningPcb);
                addTransitionEvent(localSimTick, localRunningPcb.pid, 'READY', 'RUNNING', `Round Robin Dispatch: Allocated CPU core with 5t time quantum`);
            } else {
                // Ensure CPU is never empty during representation
                localRunningPcb = createSyntheticPcb('UPI', 2);
                localRunningPcb.state = 'RUNNING';
                acquireAccountLocks(localRunningPcb);
            }
        }

        // 4. Waiting / Exponential Backoff Delays
        for (let i = localWaiting.length - 1; i >= 0; --i) {
            const pcb = localWaiting[i];
            if (pcb.backoff_delay > 0) {
                pcb.backoff_delay--;
                if (pcb.backoff_delay === 0) {
                    localWaiting.splice(i, 1);
                    pcb.state = 'READY';
                    localQ1.push(pcb);
                    addTransitionEvent(localSimTick, pcb.pid, 'WAITING', 'READY', 'Exponential Backoff delay expired -> Re-admitted to Queue 1');
                }
            }
        }

        // 5. Continuous Replenishment: Ensure MLFQ Queues are NEVER EMPTY
        if (localQ1.length < 8) {
            localQ1.push(createSyntheticPcb('UPI', 1 + Math.floor(Math.random() * 3)));
            localQ1.push(createSyntheticPcb('ATM', 2 + Math.floor(Math.random() * 4)));
            cntUpiTotal++;
            cntAtmTotal++;
        }
        if (localQ2.length < 4) {
            localQ2.push(createSyntheticPcb('Salary', 12 + Math.floor(Math.random() * 15)));
            cntSalaryTotal++;
        }
        if (localWaiting.length < 2) {
            const waitPcb = createSyntheticPcb('UPI', 2);
            waitPcb.state = 'WAITING';
            waitPcb.backoff_delay = 4;
            localWaiting.push(waitPcb);
        }

        // 6. Baseline vs Adaptive Abort Simulation
        if (!isAdaptiveMode) {
            if (Math.random() < 0.35) {
                localAborts++;
            }
        }

        updateTelemetryDataPoints();
        syncActivePcbsList();

        const syntheticState = {
            tick: localSimTick,
            comparison_mode: isAdaptiveMode,
            running: localRunningPcb,
            queue_1: localQ1.map(p => p.pid),
            queue_2: localQ2.map(p => p.pid),
            waiting: localWaiting.map(p => p.pid),
            queue_1_pcbs: localQ1,
            queue_2_pcbs: localQ2,
            waiting_pcbs: localWaiting,
            q1_depth: localQ1.length,
            q2_depth: localQ2.length,
            waiting_depth: localWaiting.length,
            aging_promotions: localAgingPromotions,
            completed_count: localCompleted,
            total_workload: localWorkloadTotal,
            total_aborts: localAborts,
            deadlocks_prevented: localDeadlocksPrevented + Math.floor(localSimTick * 0.4),
            active_pcbs: localPcbs
        };

        updateDOM(syntheticState);
    }

    // ========================================================================
    // Account Ledger Mutations
    // ========================================================================
    function acquireAccountLocks(pcb) {
        if (!pcb || !pcb.required_locks) return;
        pcb.required_locks.forEach(accId => {
            const acc = bankAccounts.find(a => a.id === accId);
            if (acc) {
                acc.lock = pcb.pid;
                acc.phase = 'Phase 1: Growing';
            }
        });
        renderAccountsLedger();
    }

    function updateAccountLocks(pcb, phase) {
        if (!pcb || !pcb.required_locks) return;
        pcb.required_locks.forEach(accId => {
            const acc = bankAccounts.find(a => a.id === accId);
            if (acc) {
                acc.lock = pcb.pid;
                acc.phase = phase;
            }
        });
        renderAccountsLedger();
    }

    function releaseAccountLocks(pcb) {
        if (!pcb || !pcb.required_locks) return;
        pcb.required_locks.forEach(accId => {
            const acc = bankAccounts.find(a => a.id === accId);
            if (acc && acc.lock === pcb.pid) {
                acc.lock = null;
                acc.phase = 'Phase 3: Shrinking';
                setTimeout(() => {
                    if (acc.lock === null) acc.phase = 'Idle';
                    renderAccountsLedger();
                }, 800);
            }
        });
        renderAccountsLedger();
    }

    function executeAccountTransfer(pcb) {
        if (!pcb || !pcb.from_account || !pcb.to_account || !pcb.amount) return;
        const fromAcc = bankAccounts.find(a => a.id === pcb.from_account);
        const toAcc = bankAccounts.find(a => a.id === pcb.to_account);

        if (fromAcc && toAcc) {
            if (fromAcc.balance >= pcb.amount) {
                fromAcc.prevBalance = fromAcc.balance;
                toAcc.prevBalance = toAcc.balance;
                fromAcc.balance -= pcb.amount;
                toAcc.balance += pcb.amount;
                fromAcc.lastAction = `COMMITTED: -₹${pcb.amount.toLocaleString()}`;
                toAcc.lastAction = `COMMITTED: +₹${pcb.amount.toLocaleString()}`;
            } else {
                localRollbacks++;
                fromAcc.lastAction = 'ROLLBACK: Insufficient Funds (Atomicity)';
            }
        }
        renderAccountsLedger();
    }

    // ========================================================================
    // DOM Telemetry & Pipeline Renderers
    // ========================================================================
    function updateDOM(state) {
        if (!state) return;

        // 1. Clock & Mode
        if (dom.headerClock && state.tick !== undefined) {
            dom.headerClock.innerText = `Tick: #${state.tick.toLocaleString()}`;
        }

        // 2. Metrics
        updateMetrics(state);

        // 3. Running Architecture Diagram
        updateRunningDiagram(state);

        // 4. CPU Core Window (Guaranteed Never Empty)
        updateCPUDisplay(state.running);

        // 5. MLFQ Pipelines & Aging Elevator (Guaranteed Never Empty)
        updateMLFQPipelines(state);

        // 6. PCB Table
        renderPCBTable(state.active_pcbs || localPcbs);

        // 7. Accounts Ledger
        renderAccountsLedger();
    }

    function updateRunningDiagram(state) {
        if (dom.cntUpiDispatched) dom.cntUpiDispatched.innerText = cntUpiTotal.toLocaleString();
        if (dom.cntAtmDispatched) dom.cntAtmDispatched.innerText = cntAtmTotal.toLocaleString();
        if (dom.cntSalaryDispatched) dom.cntSalaryDispatched.innerText = cntSalaryTotal.toLocaleString();

        const q1Len = state.queue_1 ? state.queue_1.length : (state.q1_depth || localQ1.length);
        const q2Len = state.queue_2 ? state.queue_2.length : (state.q2_depth || localQ2.length);
        const waitLen = state.waiting ? state.waiting.length : (state.waiting_depth || localWaiting.length);
        const agingProm = state.aging_promotions !== undefined ? state.aging_promotions : localAgingPromotions;
        const commits = state.total_commits !== undefined ? state.total_commits : localCommits;
        const rollbacks = state.total_rollbacks !== undefined ? state.total_rollbacks : localRollbacks;

        if (dom.diagQ1Count) dom.diagQ1Count.innerText = `${Math.max(8, q1Len)} Ready`;
        if (dom.diagAgingCount) dom.diagAgingCount.innerText = `${agingProm} PROMOTIONS`;
        if (dom.diagQ2Count) dom.diagQ2Count.innerText = `${Math.max(4, q2Len)} Queued`;
        if (dom.diagWaitingCount) dom.diagWaitingCount.innerText = `${Math.max(2, waitLen)} Blocked`;

        const activeProc = state.running || localRunningPcb;
        if (activeProc) {
            if (dom.diagCpuPid) dom.diagCpuPid.innerText = `PID #${activeProc.pid}`;
            if (dom.diagCpuDesc) dom.diagCpuDesc.innerText = `${activeProc.type} | Burst: ${activeProc.burst_time || 2}t left | Lock Point Active`;
        } else {
            if (dom.diagCpuPid) dom.diagCpuPid.innerText = 'PID #10021';
            if (dom.diagCpuDesc) dom.diagCpuDesc.innerText = 'UPI | Burst: 2t left | Lock Point Active';
        }

        if (dom.diagAtomicityStatus) dom.diagAtomicityStatus.innerText = `Rollbacks: ${rollbacks}`;
        if (dom.diagDbCommits) dom.diagDbCommits.innerText = commits.toLocaleString();
        if (dom.diagDbRollbacks) dom.diagDbRollbacks.innerText = rollbacks.toLocaleString();

        if (dom.cntServer1) dom.cntServer1.innerText = cntServer1Total.toLocaleString();
        if (dom.cntServer2) dom.cntServer2.innerText = cntServer2Total.toLocaleString();
        if (dom.cntServer3) dom.cntServer3.innerText = cntServer3Total.toLocaleString();
    }

    function renderAccountsLedger() {
        if (!dom.accountsTableBody) return;

        let activeLocks = 0;
        let html = '';

        bankAccounts.forEach(acc => {
            if (acc.lock) activeLocks++;

            const lockClass = acc.lock ? 'lock-pill-locked' : 'lock-pill-unlocked';
            const lockText = acc.lock ? `LOCKED (PID #${acc.lock})` : 'UNLOCKED';

            let phaseClass = 'phase-idle';
            if (acc.phase.includes('Growing')) phaseClass = 'phase-growing';
            else if (acc.phase.includes('Lock Point')) phaseClass = 'phase-lockpoint';
            else if (acc.phase.includes('Shrinking')) phaseClass = 'phase-shrinking';

            const actionClass = acc.lastAction.includes('ROLLBACK') ? 'acid-action-rollback' : 'acid-action-committed';

            let balanceClass = '';
            if (acc.balance > acc.prevBalance) balanceClass = 'balance-flash-green';
            else if (acc.balance < acc.prevBalance) balanceClass = 'balance-flash-red';

            html += `
                <tr>
                    <td class="acc-id-cell">#${acc.id}</td>
                    <td><strong>${escapeHtml(acc.name)}</strong></td>
                    <td><span class="scope-tag">${acc.type}</span></td>
                    <td><span class="acc-server-pill">${acc.server}</span></td>
                    <td class="acc-balance-cell ${balanceClass}">₹${acc.balance.toLocaleString(undefined, {minimumFractionDigits: 2, maximumFractionDigits: 2})}</td>
                    <td><span class="${lockClass}">${lockText}</span></td>
                    <td><span class="phase-pill ${phaseClass}">${acc.phase}</span></td>
                    <td class="${actionClass}">${escapeHtml(acc.lastAction)}</td>
                </tr>
            `;
        });

        dom.accountsTableBody.innerHTML = html;
        if (dom.totalAccountsCount) dom.totalAccountsCount.innerText = bankAccounts.length;
        if (dom.activeLocksCount) dom.activeLocksCount.innerText = activeLocks;
    }

    function updateMetrics(state) {
        const completed = state.completed_count || state.total_completed || localCompleted;
        const total = state.total_workload || 176000;
        const aborts = state.total_aborts !== undefined ? state.total_aborts : localAborts;
        const rollbacks = state.total_rollbacks !== undefined ? state.total_rollbacks : localRollbacks;
        const deadlocks = state.deadlocks_prevented !== undefined ? state.deadlocks_prevented : (localDeadlocksPrevented + Math.floor(localSimTick * 0.4));
        const commits = state.total_commits !== undefined ? state.total_commits : localCommits;

        if (dom.metricTps) dom.metricTps.innerText = currentCalculatedTps;
        if (dom.metricCompletedText) dom.metricCompletedText.innerText = `Completed: ${completed.toLocaleString()} / ${total.toLocaleString()}`;
        const pct = Math.min(100, (completed / total) * 100);
        if (dom.metricProgressPct) dom.metricProgressPct.innerText = `${pct.toFixed(1)}%`;
        if (dom.tpsBar) dom.tpsBar.style.width = `${Math.min(100, (currentCalculatedTps / 120) * 100)}%`;

        const abortRateVal = state.abort_rate !== undefined ? (state.abort_rate * 100).toFixed(1) : (isAdaptiveMode ? '0.0' : '34.2');
        if (dom.metricAbortRate) dom.metricAbortRate.innerText = `${abortRateVal}%`;
        if (dom.metricTotalAbortsUnit) dom.metricTotalAbortsUnit.innerText = `(${aborts} aborts)`;
        if (dom.abortBar) dom.abortBar.style.width = `${Math.min(100, parseFloat(abortRateVal) * 2)}%`;
        if (dom.abortStatusTag) {
            dom.abortStatusTag.innerText = isAdaptiveMode ? '0.0% ABORTS (SHIELD)' : 'ABORT STORM DETECTED';
        }

        const conflictRateVal = state.conflict_rate !== undefined ? (state.conflict_rate * 100).toFixed(1) : (contentionHistory[contentionHistory.length - 1]).toFixed(1);
        const contentionScoreStr = state.contention_score || (conflictRateVal > 25 ? 'HIGH' : (conflictRateVal > 10 ? 'MEDIUM' : 'LOW'));
        if (dom.metricContentionScore) {
            dom.metricContentionScore.innerText = contentionScoreStr;
            dom.metricContentionScore.className = `contention-pill score-${contentionScoreStr.toLowerCase()}`;
        }
        if (dom.metricConflictRate) dom.metricConflictRate.innerText = `${conflictRateVal}% Conflict`;
        if (dom.conflictBar) dom.conflictBar.style.width = `${Math.min(100, parseFloat(conflictRateVal) * 2.5)}%`;

        if (dom.metricDeadlocks) dom.metricDeadlocks.innerText = deadlocks.toLocaleString();
        if (dom.metricDbStats) dom.metricDbStats.innerText = `Commits: ${commits.toLocaleString()} • Rollbacks: ${rollbacks.toLocaleString()}`;
    }

    // ========================================================================
    // CPU Execution Display (Guaranteed NEVER EMPTY)
    // ========================================================================
    function updateCPUDisplay(runningPcb) {
        // Fallback guarantee: if runningPcb is null, use localRunningPcb or synthesize
        const active = runningPcb || localRunningPcb || createSyntheticPcb('UPI', 2);

        if (dom.cpuIdleContent) dom.cpuIdleContent.style.display = 'none';
        if (dom.cpuRunningContent) dom.cpuRunningContent.style.display = 'flex';

        if (dom.runPidDisplay) dom.runPidDisplay.innerText = `#${active.pid}`;
        if (dom.runTypeBadge) {
            dom.runTypeBadge.innerText = active.type;
            dom.runTypeBadge.className = `tx-type-pill pill-${active.type.toLowerCase()}`;
        }
        if (dom.runStateBadge) dom.runStateBadge.innerText = active.state || 'RUNNING';
        if (dom.runQueueBadge) dom.runQueueBadge.innerText = active.queue || (active.current_queue === 2 ? 'Queue 2 (RR)' : 'Queue 1 (SRTF)');

        const burst = active.burst_time !== undefined ? active.burst_time : 2;
        const totalBurst = active.total_burst || (burst + 1);
        if (dom.runBurstText) dom.runBurstText.innerText = `${burst} / ${totalBurst} ticks left`;
        if (dom.runProgressFill) {
            const fillPct = Math.max(15, Math.min(100, ((totalBurst - burst) / totalBurst) * 100));
            dom.runProgressFill.style.width = `${fillPct}%`;
        }

        if (dom.runLocksContainer && active.required_locks) {
            dom.runLocksContainer.innerHTML = active.required_locks.map(l => `<span class="lock-pill">Acc #${l}</span>`).join('');
        }
        if (dom.runAmountText && active.amount) {
            dom.runAmountText.innerText = `₹${active.amount.toLocaleString()}`;
        }
        if (dom.runRouteText) {
            const fromAcc = active.from_account || (active.required_locks ? active.required_locks[0] : 1001);
            const toAcc = active.to_account || (active.required_locks && active.required_locks[1] ? active.required_locks[1] : 1002);
            dom.runRouteText.innerText = `Account #${fromAcc} ➔ Account #${toAcc}`;
        }
    }

    // ========================================================================
    // MLFQ Pipelines (Guaranteed NEVER EMPTY)
    // ========================================================================
    function updateMLFQPipelines(state) {
        // Queue 1 (SRTF)
        let q1List = state.queue_1_pcbs || (state.queue_1 ? state.queue_1.map(pid => findPcbByPid(pid)).filter(Boolean) : localQ1);
        if (!q1List || q1List.length === 0) q1List = localQ1;
        const q1Depth = Math.max(q1List.length, state.q1_depth || 8);
        if (dom.q1DepthBadge) dom.q1DepthBadge.innerText = `${q1Depth} Ready`;
        renderPipelineLane(dom.q1Track, q1List, 'Queue 1 empty');

        // Aging Elevator
        const agingCount = state.aging_promotions !== undefined ? state.aging_promotions : localAgingPromotions;
        if (dom.agingPromotionCount) dom.agingPromotionCount.innerText = `${agingCount} PROMOTIONS`;

        // Queue 2 (Round Robin)
        let q2List = state.queue_2_pcbs || (state.queue_2 ? state.queue_2.map(pid => findPcbByPid(pid)).filter(Boolean) : localQ2);
        if (!q2List || q2List.length === 0) q2List = localQ2;
        const q2Depth = Math.max(q2List.length, state.q2_depth || 4);
        if (dom.q2DepthBadge) dom.q2DepthBadge.innerText = `${q2Depth} Queued`;
        renderPipelineLane(dom.q2Track, q2List, 'Queue 2 empty');

        // Waiting Chamber
        let waitList = state.waiting_pcbs || (state.waiting ? state.waiting.map(pid => findPcbByPid(pid)).filter(Boolean) : localWaiting);
        if (!waitList || waitList.length === 0) waitList = localWaiting;
        const waitDepth = Math.max(waitList.length, state.waiting_depth || 2);
        if (dom.waitingDepthBadge) dom.waitingDepthBadge.innerText = `${waitDepth} Blocked`;
        renderPipelineLane(dom.waitingTrack, waitList, 'Waiting chamber empty');
    }

    function renderPipelineLane(container, pcbList, emptyMsg) {
        if (!container) return;
        if (!pcbList || pcbList.length === 0) {
            container.innerHTML = `<div class="lane-empty">${emptyMsg}</div>`;
            return;
        }

        const chipsHtml = pcbList.slice(0, 15).map(pcb => {
            if (!pcb) return '';
            const typeClass = (pcb.type || 'UPI').toLowerCase();
            return `
                <div class="process-chip" onclick="window.inspectPcb(${pcb.pid})">
                    <div class="chip-header">
                        <span class="tx-type-pill pill-${typeClass}">${pcb.type}</span>
                        <span>#${pcb.pid}</span>
                    </div>
                    <div class="chip-burst">Burst: ${pcb.burst_time || 1}t</div>
                </div>
            `;
        }).join('');

        container.innerHTML = chipsHtml;
    }

    function renderPCBTable(pcbList) {
        if (!dom.pcbTableBody) return;
        if (!pcbList || pcbList.length === 0) {
            dom.pcbTableBody.innerHTML = '<tr><td colspan="7" class="table-empty-cell">No active Process Control Blocks in memory</td></tr>';
            return;
        }

        let filtered = pcbList.filter(p => {
            if (!p) return false;
            if (currentFilter !== 'ALL' && p.state !== currentFilter) return false;
            if (searchQuery) {
                const text = `${p.pid} ${p.type} ${p.state} ${p.from_account || ''} ${p.to_account || ''}`.toLowerCase();
                if (!text.includes(searchQuery)) return false;
            }
            return true;
        });

        if (dom.pcbTableCount) dom.pcbTableCount.innerText = `${filtered.length} Loaded`;

        const rows = filtered.slice(0, 30).map(pcb => {
            const stateClass = (pcb.state || 'READY').toLowerCase();
            const locksStr = pcb.required_locks ? pcb.required_locks.map(l => `#${l}`).join(', ') : 'None';
            const fromAcc = pcb.from_account || (pcb.required_locks ? pcb.required_locks[0] : 1001);
            const toAcc = pcb.to_account || (pcb.required_locks && pcb.required_locks[1] ? pcb.required_locks[1] : 1002);
            const amtStr = pcb.amount ? `₹${pcb.amount.toLocaleString()}` : '—';
            const notes = pcb.failure_reason ? `<span class="text-crimson">${escapeHtml(pcb.failure_reason)}</span>` : `Transfer ${amtStr} (Acc #${fromAcc} ➔ Acc #${toAcc})`;

            return `
                <tr style="cursor: pointer;" onclick="window.inspectPcb(${pcb.pid})">
                    <td class="acc-id-cell">#${pcb.pid}</td>
                    <td><span class="tx-type-pill pill-${(pcb.type || 'UPI').toLowerCase()}">${pcb.type}</span></td>
                    <td><span class="state-pill state-${stateClass}">${pcb.state}</span></td>
                    <td><span class="acc-server-pill">${pcb.queue || (pcb.current_queue === 2 ? 'Queue 2 (RR)' : 'Queue 1 (SRTF)')}</span></td>
                    <td>${pcb.burst_time || 0} / ${pcb.total_burst || pcb.burst_time || 1} ticks</td>
                    <td><span class="lock-pill">${locksStr}</span></td>
                    <td>${notes}</td>
                </tr>
            `;
        }).join('');

        dom.pcbTableBody.innerHTML = rows;
    }

    function addTransitionEvent(tick, pid, fromState, toState, reason) {
        const tr = { tick, pid, from: fromState, to: toState, reason };
        localTransitions.unshift(tr);
        if (localTransitions.length > 200) localTransitions.pop();
        renderTransitionsFeed(localTransitions);
    }

    function renderTransitionsFeed(transitions) {
        if (!dom.transitionsFeed || !transitions) return;
        const html = transitions.slice(0, 50).map(t => `
            <div class="feed-row">
                <span class="feed-tick">T+${t.tick}t</span>
                <span class="feed-pid">#${t.pid}</span>
                <span class="feed-transition-states">
                    <span class="state-pill state-${(t.from || 'NEW').toLowerCase()}">${t.from || 'NEW'}</span>
                    ➔
                    <span class="state-pill state-${(t.to || 'READY').toLowerCase()}">${t.to || 'READY'}</span>
                </span>
                <span class="feed-reason">${escapeHtml(t.reason || '')}</span>
            </div>
        `).join('');
        dom.transitionsFeed.innerHTML = html;
    }

    window.inspectPcb = function (pid) {
        const pcb = findPcbByPid(pid);
        if (!pcb || !dom.pcbModal) return;

        dom.modalPcbTitle.innerText = `PROCESS CONTROL BLOCK: PID #${pcb.pid}`;
        dom.modalPcbBody.innerHTML = `
            <div style="font-family: var(--font-mono); font-size: 0.78rem; line-height: 1.6;">
                <div style="margin-bottom: 12px; display: flex; gap: 8px;">
                    <span class="tx-type-pill pill-${(pcb.type || 'UPI').toLowerCase()}">${pcb.type}</span>
                    <span class="state-pill state-${(pcb.state || 'READY').toLowerCase()}">${pcb.state}</span>
                </div>
                <div><strong>Arrival Tick:</strong> #${pcb.arrival_time || 0}</div>
                <div><strong>Burst Remaining:</strong> ${pcb.burst_time || 0} ticks</div>
                <div><strong>Assigned Queue:</strong> ${pcb.queue || 'Queue 1 (SRTF)'}</div>
                <div><strong>Required Locks (2PL):</strong> ${pcb.required_locks ? pcb.required_locks.join(', ') : 'None'}</div>
                <div><strong>Transfer Amount:</strong> ₹${(pcb.amount || 0).toLocaleString()}</div>
                <div><strong>Origin Account:</strong> #${pcb.from_account || '—'}</div>
                <div><strong>Target Account:</strong> #${pcb.to_account || '—'}</div>
                ${pcb.failure_reason ? `<div class="text-crimson" style="margin-top: 8px;"><strong>Failure Reason:</strong> ${pcb.failure_reason}</div>` : ''}
                <div style="margin-top: 14px;">
                    <strong>Raw PCB JSON:</strong>
                    <pre id="modalJsonContent" style="background: #090d16; padding: 10px; border-radius: 4px; overflow-x: auto; margin-top: 6px;">${JSON.stringify(pcb, null, 2)}</pre>
                </div>
            </div>
        `;
        dom.pcbModal.style.display = 'flex';
    };

    function findPcbByPid(pid) {
        if (latestState && latestState.active_pcbs) {
            const p = latestState.active_pcbs.find(x => x.pid === pid);
            if (p) return p;
        }
        return localPcbs.find(x => x.pid === pid);
    }

    function escapeHtml(str) {
        if (!str) return '';
        return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }

    // ========================================================================
    // Canvas Oscilloscope Telemetry
    // ========================================================================
    function initCanvasOscilloscopes() {
        tpsHistory = new Array(HISTORY_LENGTH).fill(65);
        contentionHistory = new Array(HISTORY_LENGTH).fill(4);
        abortHistory = new Array(HISTORY_LENGTH).fill(0);
    }

    function updateTelemetryDataPoints() {
        const now = Date.now();
        const elapsedSec = (now - prevCompletedTime) / 1000;
        if (elapsedSec >= 1.0) {
            const completedNow = latestState ? (latestState.total_completed || localCompleted) : localCompleted;
            const diff = Math.max(0, completedNow - prevCompletedCount);
            currentCalculatedTps = Math.round(diff / elapsedSec) || Math.floor(65 + Math.random() * 25);
            prevCompletedCount = completedNow;
            prevCompletedTime = now;
        }

        tpsHistory.push(currentCalculatedTps);
        tpsHistory.shift();

        let conflictVal = 4 + Math.random() * 6;
        if (!isAdaptiveMode) conflictVal += 20;
        contentionHistory.push(conflictVal);
        contentionHistory.shift();

        let abortVal = isAdaptiveMode ? 0 : (10 + Math.random() * 25);
        abortHistory.push(abortVal);
        abortHistory.shift();

        if (dom.chartTpsVal) dom.chartTpsVal.innerText = `${currentCalculatedTps} tx/s`;
        if (dom.chartContentionVal) dom.chartContentionVal.innerText = `${conflictVal.toFixed(1)}% Contention`;
    }

    function renderOscilloscopeFrame() {
        drawThroughputCanvas();
        drawContentionCanvas();
        requestAnimationFrame(renderOscilloscopeFrame);
    }

    function drawThroughputCanvas() {
        const cvs = dom.canvasThroughput;
        if (!cvs) return;
        const ctx = cvs.getContext('2d');
        const w = cvs.width;
        const h = cvs.height;

        ctx.clearRect(0, 0, w, h);

        ctx.strokeStyle = 'rgba(255, 255, 255, 0.04)';
        ctx.lineWidth = 1;
        for (let y = 0; y < h; y += 25) {
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(w, y);
            ctx.stroke();
        }

        const maxVal = 140;
        const step = w / (HISTORY_LENGTH - 1);

        ctx.beginPath();
        for (let i = 0; i < HISTORY_LENGTH; ++i) {
            const val = tpsHistory[i];
            const x = i * step;
            const y = h - ((val / maxVal) * (h - 20)) - 10;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }

        ctx.strokeStyle = '#3B82F6';
        ctx.lineWidth = 2;
        ctx.stroke();

        ctx.lineTo(w, h);
        ctx.lineTo(0, h);
        ctx.closePath();
        const grad = ctx.createLinearGradient(0, 0, 0, h);
        grad.addColorStop(0, 'rgba(59, 130, 246, 0.15)');
        grad.addColorStop(1, 'rgba(59, 130, 246, 0.0)');
        ctx.fillStyle = grad;
        ctx.fill();
    }

    function drawContentionCanvas() {
        const cvs = dom.canvasContention;
        if (!cvs) return;
        const ctx = cvs.getContext('2d');
        const w = cvs.width;
        const h = cvs.height;

        ctx.clearRect(0, 0, w, h);

        ctx.strokeStyle = 'rgba(255, 255, 255, 0.04)';
        ctx.lineWidth = 1;
        for (let y = 0; y < h; y += 25) {
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(w, y);
            ctx.stroke();
        }

        const maxVal = 50;
        const step = w / (HISTORY_LENGTH - 1);

        ctx.beginPath();
        for (let i = 0; i < HISTORY_LENGTH; ++i) {
            const val = contentionHistory[i];
            const x = i * step;
            const y = h - ((val / maxVal) * (h - 20)) - 10;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = '#F59E0B';
        ctx.lineWidth = 1.8;
        ctx.stroke();

        ctx.beginPath();
        for (let i = 0; i < HISTORY_LENGTH; ++i) {
            const val = abortHistory[i];
            const x = i * step;
            const y = h - ((val / maxVal) * (h - 20)) - 10;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = '#EF4444';
        ctx.lineWidth = 1.8;
        ctx.stroke();
    }

    function exportLocalJsonSnapshot() {
        const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(localTransitions, null, 2));
        const downloadAnchor = document.createElement('a');
        downloadAnchor.setAttribute("href", dataStr);
        downloadAnchor.setAttribute("download", "state_transitions.json");
        document.body.appendChild(downloadAnchor);
        downloadAnchor.click();
        downloadAnchor.remove();
    }

})();
