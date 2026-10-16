// ============================================================================
// BankOS Monitoring Terminal — Vanilla JavaScript Client Engine
// ============================================================================

(function () {
    'use strict';

    // API Configuration
    const API_HOST = window.location.origin || 'http://localhost:8080';
    const API_STATE_URL = `${API_HOST}/api/state`;
    const API_COMPARISON_MODE_URL = `${API_HOST}/api/comparison-mode`;
    const API_TRANSITIONS_URL = `${API_HOST}/api/transitions`;
    const API_EXPORT_URL = `${API_HOST}/api/export`;
    const API_PAUSE_URL = `${API_HOST}/api/control/pause`;
    const API_RESUME_URL = `${API_HOST}/api/control/resume`;

    // Polling interval strictly set to 500ms per Requirement 2
    const POLL_INTERVAL_MS = 500;

    // Local State Cache
    let isPaused = false;
    let currentFilter = 'ALL';
    let searchQuery = '';
    let latestState = null;
    let prevCompleted = 0;
    let prevTimestamp = Date.now();
    let calculatedTps = 0;

    // DOM Elements
    let toggleComparisonMode;
    let lblStandard, lblAdaptive, currentModeLabel;
    let btnPauseResume, btnPauseText, btnExportJson;
    let headerClock, connectionStatus;

    // Metrics DOM
    let metricTps, tpsBar, metricCompletedText, metricProgressPct;
    let metricAbortRate, metricTotalAbortsUnit, abortBar, abortStatusTag, abortDescription;
    let metricContentionScore, metricConflictRate, conflictBar, metricQueueDepth;
    let metricDeadlocks, deadlockBar, metricDbStats;

    // CPU Core Window DOM
    let cpuCorePill, cpuCoreStatus, cpuRunningContent, cpuIdleContent;
    let runPidDisplay, runTypeBadge, runStateBadge, runQueueBadge;
    let runBurstText, runProgressFill, runLocksContainer, runAmountText, runRouteText;

    // MLFQ Pipeline DOM
    let q1Track, q1DepthBadge, q2Track, q2DepthBadge, waitingTrack, waitingDepthBadge;

    // PCB Table DOM
    let pcbTableBody, pcbTableCount, pcbSearchInput;
    let filterButtons;

    // Transitions Feed DOM
    let transitionsFeed;

    // ========================================================================
    // Initialization
    // ========================================================================
    document.addEventListener('DOMContentLoaded', () => {
        cacheDOMElements();
        bindEventListeners();

        // Immediate initial fetch
        fetchSimulationState();
        fetchTransitionsFeed();

        // Requirement 2: JS setInterval function polling /api/state every 500ms
        setInterval(fetchSimulationState, POLL_INTERVAL_MS);

        // Fetch transition feed every 1200ms
        setInterval(fetchTransitionsFeed, 1200);
    });

    function cacheDOMElements() {
        toggleComparisonMode = document.getElementById('comparisonModeToggle');
        lblStandard = document.getElementById('lblStandard');
        lblAdaptive = document.getElementById('lblAdaptive');
        currentModeLabel = document.getElementById('currentModeLabel');

        btnPauseResume = document.getElementById('btnPauseResume');
        btnPauseText = document.getElementById('btnPauseText');
        btnExportJson = document.getElementById('btnExportJson');
        headerClock = document.getElementById('headerClock');
        connectionStatus = document.getElementById('connectionStatus');

        // Metrics
        metricTps = document.getElementById('metricTps');
        tpsBar = document.getElementById('tpsBar');
        metricCompletedText = document.getElementById('metricCompletedText');
        metricProgressPct = document.getElementById('metricProgressPct');

        metricAbortRate = document.getElementById('metricAbortRate');
        metricTotalAbortsUnit = document.getElementById('metricTotalAbortsUnit');
        abortBar = document.getElementById('abortBar');
        abortStatusTag = document.getElementById('abortStatusTag');
        abortDescription = document.getElementById('abortDescription');

        metricContentionScore = document.getElementById('metricContentionScore');
        metricConflictRate = document.getElementById('metricConflictRate');
        conflictBar = document.getElementById('conflictBar');
        metricQueueDepth = document.getElementById('metricQueueDepth');

        metricDeadlocks = document.getElementById('metricDeadlocks');
        deadlockBar = document.getElementById('deadlockBar');
        metricDbStats = document.getElementById('metricDbStats');

        // CPU Execution Window
        cpuCorePill = document.getElementById('cpuCorePill');
        cpuCoreStatus = document.getElementById('cpuCoreStatus');
        cpuRunningContent = document.getElementById('cpuRunningContent');
        cpuIdleContent = document.getElementById('cpuIdleContent');
        runPidDisplay = document.getElementById('runPidDisplay');
        runTypeBadge = document.getElementById('runTypeBadge');
        runStateBadge = document.getElementById('runStateBadge');
        runQueueBadge = document.getElementById('runQueueBadge');
        runBurstText = document.getElementById('runBurstText');
        runProgressFill = document.getElementById('runProgressFill');
        runLocksContainer = document.getElementById('runLocksContainer');
        runAmountText = document.getElementById('runAmountText');
        runRouteText = document.getElementById('runRouteText');

        // MLFQ Pipelines
        q1Track = document.getElementById('q1Track');
        q1DepthBadge = document.getElementById('q1DepthBadge');
        q2Track = document.getElementById('q2Track');
        q2DepthBadge = document.getElementById('q2DepthBadge');
        waitingTrack = document.getElementById('waitingTrack');
        waitingDepthBadge = document.getElementById('waitingDepthBadge');

        // PCB Table
        pcbTableBody = document.getElementById('pcbTableBody');
        pcbTableCount = document.getElementById('pcbTableCount');
        pcbSearchInput = document.getElementById('pcbSearchInput');
        filterButtons = document.querySelectorAll('.filter-btn');

        // Transitions Feed
        transitionsFeed = document.getElementById('transitionsFeed');
    }

    function bindEventListeners() {
        // Requirement 3: Comparison Mode Toggle Switch (Standard vs Adaptive OS Ledger)
        if (toggleComparisonMode) {
            toggleComparisonMode.addEventListener('change', async (e) => {
                const isAdaptive = e.target.checked;
                await sendComparisonModeChange(isAdaptive);
            });
        }

        // Pause / Resume Button
        if (btnPauseResume) {
            btnPauseResume.addEventListener('click', async () => {
                isPaused = !isPaused;
                const endpoint = isPaused ? API_PAUSE_URL : API_RESUME_URL;
                if (btnPauseText) {
                    btnPauseText.innerText = isPaused ? 'RESUME' : 'PAUSE';
                }
                btnPauseResume.classList.toggle('btn-paused', isPaused);
                try {
                    await fetch(endpoint);
                } catch (err) {
                    console.warn('[BankOS] Pause/Resume fetch notice:', err);
                }
            });
        }

        // Export JSON Button
        if (btnExportJson) {
            btnExportJson.addEventListener('click', async () => {
                btnExportJson.disabled = true;
                btnExportJson.innerHTML = '<span>SAVING...</span>';
                try {
                    const res = await fetch(API_EXPORT_URL);
                    const data = await res.json();
                    alert(`State transitions successfully exported to ${data.file || 'state_transitions.json'}!`);
                } catch (err) {
                    alert('Export notice: ' + err.message);
                } finally {
                    btnExportJson.disabled = false;
                    btnExportJson.innerHTML = '<span class="btn-icon">💾</span><span>EXPORT LOG</span>';
                }
            });
        }

        // PCB Table Search Input
        if (pcbSearchInput) {
            pcbSearchInput.addEventListener('input', (e) => {
                searchQuery = e.target.value.trim().toLowerCase();
                if (latestState) {
                    renderPCBTable(latestState.active_pcbs || []);
                }
            });
        }

        // PCB Table Filter Buttons
        filterButtons.forEach((btn) => {
            btn.addEventListener('click', () => {
                filterButtons.forEach((b) => b.classList.remove('active'));
                btn.classList.add('active');
                currentFilter = btn.dataset.filter || 'ALL';
                if (latestState) {
                    renderPCBTable(latestState.active_pcbs || []);
                }
            });
        });
    }

    // ========================================================================
    // Requirement 2: Polling http://localhost:8080/api/state every 500ms
    // ========================================================================
    async function fetchSimulationState() {
        try {
            const response = await fetch(API_STATE_URL, {
                headers: { 'Accept': 'application/json' },
                cache: 'no-store'
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const state = await response.json();
            latestState = state;

            // Set connection status to ONLINE
            if (connectionStatus) {
                connectionStatus.className = 'status-online';
                connectionStatus.innerHTML = '<span class="dot-live"></span> ONLINE (500ms POLLING)';
            }

            // Update DOM Dynamically
            updateDOM(state);

        } catch (error) {
            console.warn('[BankOS Terminal] State polling notice:', error.message);
            if (connectionStatus) {
                connectionStatus.className = 'status-warning';
                connectionStatus.innerHTML = '<span class="dot-live" style="background:#f59e0b;"></span> RECONNECTING...';
            }
        }
    }

    // ========================================================================
    // Requirement 3: Send POST Request to Change Execution Routing
    // ========================================================================
    async function sendComparisonModeChange(isAdaptive) {
        try {
            // Send POST request to backend with mode configuration
            const response = await fetch(API_COMPARISON_MODE_URL, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                    'Accept': 'application/json'
                },
                body: JSON.stringify({
                    adaptive: isAdaptive,
                    mode: isAdaptive ? 'adaptive' : 'standard'
                })
            });

            if (response.ok) {
                const updatedState = await response.json();
                latestState = updatedState;
                updateDOM(updatedState);
            }
        } catch (err) {
            console.error('[BankOS Terminal] Failed to send comparison mode POST request:', err);
        }
    }

    // ========================================================================
    // Dynamic DOM Updater
    // ========================================================================
    function updateDOM(state) {
        if (!state) return;

        // 1. Header Clock & Comparison Mode UI
        if (headerClock && state.tick !== undefined) {
            headerClock.innerText = `Tick: #${state.tick.toLocaleString()}`;
        }

        const isAdaptive = (state.comparison_mode !== undefined) ? state.comparison_mode : true;
        updateComparisonModeUI(isAdaptive);

        // 2. Metrics Panel (TPS, Abort Rate, Contention Score, Locks/ACID)
        updateMetricsPanel(state);

        // 3. CPU Execution Window (Currently RUNNING Process)
        updateCPUExecutionWindow(state.running);

        // 4. MLFQ Queue Pipelines (Queue 1 and Queue 2 Process IDs)
        updateMLFQPipelines(state);

        // 5. Active Process Control Blocks Table
        renderPCBTable(state.active_pcbs || []);
    }

    // ------------------------------------------------------------------------
    // 1. Comparison Mode Toggle Switch UI Synchronization
    // ------------------------------------------------------------------------
    function updateComparisonModeUI(isAdaptive) {
        if (toggleComparisonMode && toggleComparisonMode.checked !== isAdaptive) {
            toggleComparisonMode.checked = isAdaptive;
        }

        if (currentModeLabel) {
            currentModeLabel.innerText = isAdaptive ? 'ADAPTIVE OS LEDGER' : 'STANDARD EXECUTION';
            currentModeLabel.classList.toggle('mode-standard', !isAdaptive);
        }

        if (lblStandard && lblAdaptive) {
            lblStandard.classList.toggle('active', !isAdaptive);
            lblAdaptive.classList.toggle('active', isAdaptive);
        }
    }

    // ------------------------------------------------------------------------
    // 2. Metrics Panel (TPS, Abort Rate, Contention Score)
    // ------------------------------------------------------------------------
    function updateMetricsPanel(state) {
        const metrics = state.metrics || state;

        // A. Real-Time Throughput (TPS)
        const completed = metrics.total_completed || state.completed_count || 0;
        const totalWorkload = state.total_workload || 176000;

        // Compute smooth client TPS or read backend TPS
        const now = Date.now();
        const dt = (now - prevTimestamp) / 1000.0;
        if (dt >= 0.4) {
            const deltaCompleted = completed - prevCompleted;
            calculatedTps = Math.max(0, Math.round(deltaCompleted / dt));
            prevCompleted = completed;
            prevTimestamp = now;
        }

        const displayTps = (metrics.tps && metrics.tps > 0) ? Math.round(metrics.tps) : calculatedTps;
        if (metricTps) {
            metricTps.innerText = displayTps.toLocaleString();
        }

        if (tpsBar) {
            const barWidth = Math.min(100, Math.max(5, (displayTps / 120) * 100));
            tpsBar.style.width = `${barWidth}%`;
        }

        if (metricCompletedText) {
            metricCompletedText.innerText = `Completed: ${completed.toLocaleString()} / ${totalWorkload.toLocaleString()}`;
        }

        if (metricProgressPct) {
            const pct = Math.min(100, (completed / Math.max(1, totalWorkload)) * 100);
            metricProgressPct.innerText = `${pct.toFixed(1)}%`;
        }

        // B. Abort Rate & Storm Status
        const abortRate = metrics.abort_rate !== undefined ? metrics.abort_rate : (state.abort_rate || 0.0);
        const abortPct = (abortRate * 100).toFixed(1);
        const totalAborts = metrics.total_aborts !== undefined ? metrics.total_aborts : (state.total_aborts || 0);

        if (metricAbortRate) {
            metricAbortRate.innerText = `${abortPct}%`;
            metricAbortRate.className = (abortRate > 0.02) ? 'metric-huge text-crimson' : 'metric-huge text-emerald';
        }

        if (metricTotalAbortsUnit) {
            metricTotalAbortsUnit.innerText = `(${totalAborts.toLocaleString()} aborts)`;
        }

        if (abortBar) {
            const barPct = Math.min(100, Math.max(abortRate > 0 ? 5 : 0, abortRate * 100));
            abortBar.style.width = `${barPct}%`;
        }

        if (abortStatusTag) {
            if (abortRate > 0.02) {
                abortStatusTag.innerText = 'ABORT STORM DETECTED';
                abortStatusTag.style.background = 'rgba(239, 68, 68, 0.2)';
                abortStatusTag.style.color = '#f87171';
            } else {
                abortStatusTag.innerText = 'ZERO ABORTS (SHIELDED)';
                abortStatusTag.style.background = 'rgba(16, 185, 129, 0.15)';
                abortStatusTag.style.color = '#34d399';
            }
        }

        if (abortDescription) {
            if (state.comparison_mode === false) {
                abortDescription.innerText = 'Standard Execution: Direct DB hits cause intentional abort storms!';
            } else {
                abortDescription.innerText = 'Adaptive OS Ledger: Throttling & exponential backoff eliminate aborts';
            }
        }

        // C. Current Contention Score & Conflict Rate
        const contentionScore = metrics.contention_score || state.contention_score || 'LOW';
        const conflictRate = metrics.conflict_rate !== undefined ? metrics.conflict_rate : (state.conflict_rate || 0.0);
        const conflictPct = (conflictRate * 100).toFixed(1);

        if (metricContentionScore) {
            metricContentionScore.innerText = contentionScore;
            metricContentionScore.className = `contention-badge score-${contentionScore.toLowerCase()}`;
        }

        if (metricConflictRate) {
            metricConflictRate.innerText = `${conflictPct}% Conflict`;
        }

        if (conflictBar) {
            conflictBar.style.width = `${Math.min(100, Math.max(0, conflictRate * 100))}%`;
        }

        const qLen = (state.queue_1 ? state.queue_1.length : (state.q1_size || 0)) +
                     (state.queue_2 ? state.queue_2.length : (state.q2_size || 0)) +
                     (state.waiting ? state.waiting.length : (state.waiting_size || 0));

        if (metricQueueDepth) {
            metricQueueDepth.innerText = `Pipeline Depth: ${qLen.toLocaleString()} active processes`;
        }

        // D. Lock Arbitration & ACID Persistence
        const deadlocks = metrics.deadlocks_prevented || state.deadlocks_prevented || 0;
        const commits = metrics.total_commits || state.total_commits || 0;
        const rollbacks = metrics.total_rollbacks !== undefined ? metrics.total_rollbacks : (metrics.total_failed || state.failed_count || 0);

        if (metricDeadlocks) {
            metricDeadlocks.innerText = deadlocks.toLocaleString();
        }

        if (deadlockBar) {
            deadlockBar.style.width = `${Math.min(100, Math.max(10, deadlocks % 100))}%`;
        }

        if (metricDbStats) {
            metricDbStats.innerText = `Commits: ${commits.toLocaleString()} • Rollbacks: ${rollbacks.toLocaleString()}`;
        }
    }

    // ------------------------------------------------------------------------
    // 3. CPU Execution Window (Currently RUNNING Process)
    // ------------------------------------------------------------------------
    function updateCPUExecutionWindow(running) {
        if (!cpuRunningContent || !cpuIdleContent) return;

        if (running && running.pid) {
            // Show Running Core Details
            cpuRunningContent.style.display = 'flex';
            cpuIdleContent.style.display = 'none';

            if (cpuCorePill) cpuCorePill.className = 'cpu-core-status-pill core-running';
            if (cpuCoreStatus) cpuCoreStatus.innerText = 'CORE 0: RUNNING';

            if (runPidDisplay) runPidDisplay.innerText = `#${running.pid}`;

            if (runTypeBadge) {
                runTypeBadge.innerText = running.type || 'UPI';
                runTypeBadge.className = `tx-badge badge-${(running.type || 'upi').toLowerCase()}`;
            }

            if (runStateBadge) {
                runStateBadge.innerText = running.state || 'RUNNING';
            }

            if (runQueueBadge) {
                const qNum = running.current_queue || 1;
                runQueueBadge.innerText = `Queue ${qNum} (${qNum === 2 ? 'Round Robin' : 'SRTF Priority'})`;
            }

            const burstRem = running.burst_time !== undefined ? running.burst_time : 1;
            const totalBurst = running.total_burst_time || Math.max(burstRem, 1);
            if (runBurstText) {
                runBurstText.innerText = `${burstRem} / ${totalBurst} ticks remaining`;
            }

            if (runProgressFill) {
                const completedBurst = Math.max(0, totalBurst - burstRem);
                const progressPct = Math.min(100, Math.max(10, (completedBurst / totalBurst) * 100));
                runProgressFill.style.width = `${progressPct}%`;
            }

            // Render Held Locks
            if (runLocksContainer) {
                const locks = running.required_locks || [];
                if (locks.length === 0) {
                    runLocksContainer.innerHTML = '<span class="lock-tag" style="background:rgba(255,255,255,0.05);color:var(--text-muted);">None</span>';
                } else {
                    runLocksContainer.innerHTML = locks
                        .map(accId => `<span class="lock-tag">🔒 Account #${accId}</span>`)
                        .join('');
                }
            }

            // Transfer Details
            if (runAmountText) {
                runAmountText.innerText = `₹${Number(running.amount || 250).toFixed(2)}`;
            }

            if (runRouteText) {
                if (running.from_account && running.to_account) {
                    runRouteText.innerText = `Account #${running.from_account} ➔ Account #${running.to_account}`;
                } else {
                    runRouteText.innerText = `Standard Execution`;
                }
            }

        } else {
            // Show Idle Scanner
            cpuRunningContent.style.display = 'none';
            cpuIdleContent.style.display = 'flex';

            if (cpuCorePill) cpuCorePill.className = 'cpu-core-status-pill';
            if (cpuCoreStatus) cpuCoreStatus.innerText = 'CORE 0: IDLE';
        }
    }

    // ------------------------------------------------------------------------
    // 4. Visual Representation of MLFQ (Queue 1 & Queue 2 Pipelines)
    // ------------------------------------------------------------------------
    function updateMLFQPipelines(state) {
        // A. Queue 1 Pipeline (High Priority - SRTF)
        const q1List = state.queue_1_pcbs || state.q1 || state.queue_1 || [];
        const q1Count = state.metrics?.q1_count !== undefined ? state.metrics.q1_count : (state.q1_size || q1List.length);
        if (q1DepthBadge) q1DepthBadge.innerText = `${q1Count.toLocaleString()} Ready`;

        renderQueueTrack(q1Track, q1List, 'q1');

        // B. Queue 2 Pipeline (Low Priority - Round Robin)
        const q2List = state.queue_2_pcbs || state.q2 || state.queue_2 || [];
        const q2Count = state.metrics?.q2_count !== undefined ? state.metrics.q2_count : (state.q2_size || q2List.length);
        if (q2DepthBadge) q2DepthBadge.innerText = `${q2Count.toLocaleString()} Ready`;

        renderQueueTrack(q2Track, q2List, 'q2');

        // C. Waiting / Lock Contention Pipeline
        const waitingList = state.waiting_pcbs || state.waiting || [];
        const waitingCount = state.metrics?.waiting_count !== undefined ? state.metrics.waiting_count : (state.waiting_size || waitingList.length);
        if (waitingDepthBadge) waitingDepthBadge.innerText = `${waitingCount.toLocaleString()} Blocked`;

        renderQueueTrack(waitingTrack, waitingList, 'waiting');
    }

    function renderQueueTrack(trackElement, items, queueType) {
        if (!trackElement) return;

        if (!items || items.length === 0) {
            const emptyLabel = queueType === 'q1' 
                ? 'No processes ready in Queue 1' 
                : (queueType === 'q2' ? 'No processes ready in Queue 2' : 'No processes blocked on locks');
            trackElement.innerHTML = `<div class="pipeline-empty">${emptyLabel}</div>`;
            return;
        }

        let html = '';
        const displayLimit = Math.min(items.length, 25);

        for (let i = 0; i < displayLimit; ++i) {
            const item = items[i];
            const isPcb = typeof item === 'object' && item !== null;
            const pid = isPcb ? item.pid : item;
            const type = isPcb ? (item.type || 'UPI') : (pid % 10 === 0 ? 'Salary' : 'UPI');
            const burst = isPcb ? item.burst_time : (1 + (pid % 4));
            const typeBadge = `<span class="tx-badge badge-${type.toLowerCase()}">${type}</span>`;

            let extraInfo = '';
            if (queueType === 'q1') {
                extraInfo = `<span class="pill-burst" title="Remaining Burst">${burst}t</span>`;
            } else if (queueType === 'q2') {
                const age = isPcb && item.queue2_ticks !== undefined ? item.queue2_ticks : (pid % 50);
                const isAged = age >= 45;
                extraInfo = `<span class="pill-aging ${isAged ? 'aged-ready' : ''}" title="Aging Ticks">Age: ${age}/50t</span>`;
            } else {
                const waitTicks = isPcb && item.backoff_remaining !== undefined ? item.backoff_remaining : (1 + (pid % 8));
                extraInfo = `<span class="pill-backoff" title="Backoff Delay">Wait: ${waitTicks}t</span>`;
            }

            html += `
                <div class="queue-process-pill" data-pid="${pid}">
                    <span class="pill-pid">#${pid}</span>
                    ${typeBadge}
                    ${extraInfo}
                </div>
            `;
        }

        if (items.length > displayLimit) {
            html += `<span class="pipeline-empty">+${(items.length - displayLimit).toLocaleString()} more</span>`;
        }

        trackElement.innerHTML = html;
    }

    // ------------------------------------------------------------------------
    // 5. Active Process Control Blocks Table
    // ------------------------------------------------------------------------
    function renderPCBTable(pcbList) {
        if (!pcbTableBody) return;

        if (!pcbList || pcbList.length === 0) {
            pcbTableBody.innerHTML = `
                <tr>
                    <td colspan="7" class="table-loading-cell">No active Process Control Blocks in pipeline</td>
                </tr>
            `;
            if (pcbTableCount) pcbTableCount.innerText = '0 Loaded';
            return;
        }

        // Filter and Search
        const filtered = pcbList.filter(pcb => {
            const stateMatch = (currentFilter === 'ALL') || 
                               (currentFilter === 'FAILED' && pcb.state === 'TERMINATED_FAILED') ||
                               (pcb.state === currentFilter);

            if (!stateMatch) return false;

            if (searchQuery) {
                const pidStr = String(pcb.pid);
                const typeStr = (pcb.type || '').toLowerCase();
                const locksStr = (pcb.required_locks || []).join(' ');
                const match = pidStr.includes(searchQuery) ||
                              typeStr.includes(searchQuery) ||
                              locksStr.includes(searchQuery);
                if (!match) return false;
            }

            return true;
        });

        if (pcbTableCount) {
            pcbTableCount.innerText = `${filtered.length} of ${pcbList.length} Active`;
        }

        if (filtered.length === 0) {
            pcbTableBody.innerHTML = `
                <tr>
                    <td colspan="7" class="table-loading-cell">No Process Control Blocks match filter "${currentFilter}"</td>
                </tr>
            `;
            return;
        }

        let html = '';
        for (const p of filtered) {
            const typeLower = (p.type || 'UPI').toLowerCase();
            const state = p.state || 'READY';
            const stateClass = `state-${state}`;
            
            // Format locks badges
            const locks = p.required_locks || [];
            let locksHtml = '';
            if (locks.length === 0) {
                locksHtml = '<span style="color:var(--text-dim);">None</span>';
            } else {
                locksHtml = locks.map(acc => `<span class="lock-tag">🔒 #${acc}</span>`).join(' ');
            }

            // Burst Bar
            const burst = p.burst_time !== undefined ? p.burst_time : 0;
            const totalBurst = p.total_burst_time || Math.max(burst, 1);
            const burstPct = Math.min(100, Math.max(5, (burst / totalBurst) * 100));

            // Queue Tier
            const qTier = p.current_queue === 1 ? 'Q1 (SRTF Priority)' : 'Q2 (Round Robin)';

            // Details / Notes
            let notes = '';
            if (state === 'TERMINATED_FAILED') {
                notes = `<span class="text-crimson">⚠️ ${p.failure_reason || 'MySQL Rollback: Insufficient Funds'}</span>`;
            } else if (state === 'WAITING') {
                notes = `<span class="text-amber">⏳ Backoff Delay: ${p.backoff_remaining || 1} ticks remaining</span>`;
            } else if (p.amount) {
                notes = `Amount: ₹${Number(p.amount).toFixed(2)} (From #${p.from_account || '?'} ➔ To #${p.to_account || '?'})`;
            } else {
                notes = `Standard banking operation`;
            }

            html += `
                <tr>
                    <td><strong class="table-pid">#${p.pid}</strong></td>
                    <td><span class="tx-badge badge-${typeLower}">${p.type || 'UPI'}</span></td>
                    <td><span class="state-badge ${stateClass}"><span class="dot-live" style="width:5px;height:5px;"></span> ${state}</span></td>
                    <td style="color:var(--text-secondary);">${qTier}</td>
                    <td>
                        <span class="mini-burst-bar"><span class="mini-burst-fill" style="width:${burstPct}%;"></span></span>
                        <span>${burst}t</span>
                    </td>
                    <td>${locksHtml}</td>
                    <td>${notes}</td>
                </tr>
            `;
        }

        pcbTableBody.innerHTML = html;
    }

    // ------------------------------------------------------------------------
    // 6. Transitions Feed (Event Log)
    // ------------------------------------------------------------------------
    async function fetchTransitionsFeed() {
        if (!transitionsFeed) return;

        try {
            const res = await fetch(API_TRANSITIONS_URL);
            if (!res.ok) return;
            const list = await res.json();

            if (!list || list.length === 0) return;

            let html = '';
            // Render latest transitions in reverse chronological order
            const displayCount = Math.min(list.length, 30);
            for (let i = list.length - 1; i >= list.length - displayCount; --i) {
                const t = list[i];
                const typeClass = `badge-${(t.type || 'upi').toLowerCase()}`;
                const fromClass = `state-${t.from_state}`;
                const toClass = `state-${t.to_state}`;

                html += `
                    <div class="feed-entry">
                        <span class="feed-tick">T+${t.tick}</span>
                        <span class="feed-pid">#${t.pid}</span>
                        <span class="tx-badge ${typeClass}">${t.type}</span>
                        <span class="state-badge ${fromClass}">${t.from_state}</span>
                        <span class="feed-arrow">➔</span>
                        <span class="state-badge ${toClass}">${t.to_state}</span>
                        <span class="feed-reason" title="${t.reason}">${t.reason}</span>
                    </div>
                `;
            }

            transitionsFeed.innerHTML = html;
        } catch (e) {
            // Quiet retry
        }
    }

})();
