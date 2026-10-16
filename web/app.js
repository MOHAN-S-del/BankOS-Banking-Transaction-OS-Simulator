// BankOS: Banking Transaction OS Simulator Client

let isPaused = false;
let prevCompleted = 0;
let prevTimestamp = Date.now();

document.addEventListener('DOMContentLoaded', () => {
    const btnPauseResume = document.getElementById('btnPauseResume');
    const btnExportJson = document.getElementById('btnExportJson');
    const btnModeToggle = document.getElementById('btnModeToggle');

    // Poll status & queues regularly
    setInterval(fetchSimulationStatus, 400);
    setInterval(fetchQueuesAndRunning, 600);
    setInterval(fetchTransitions, 800);

    fetchSimulationStatus();
    fetchQueuesAndRunning();
    fetchTransitions();

    // Hook Mode Toggle (MD-ACC vs Baseline)
    if (btnModeToggle) {
        btnModeToggle.addEventListener('click', async () => {
            try {
                const res = await fetch('/api/middleware/toggle');
                if (res.ok) {
                    const mw = await res.json();
                    updateModeUI(mw.mdacc_enabled);
                    fetchSimulationStatus();
                }
            } catch (err) {
                console.error(err);
            }
        });
    }

    // Hook Pause/Resume
    btnPauseResume.addEventListener('click', async () => {
        isPaused = !isPaused;
        const endpoint = isPaused ? '/api/control/pause' : '/api/control/resume';
        btnPauseResume.innerText = isPaused ? 'Resume Simulation' : 'Pause Simulation';
        
        const badge = document.getElementById('simStatusBadge');
        const badgeText = document.getElementById('simStatusText');
        if (isPaused) {
            badge.classList.add('high-contention');
            badgeText.innerText = 'STATE: PAUSED';
        } else {
            badge.classList.remove('high-contention');
            badgeText.innerText = 'STATE: RUNNING';
        }

        try {
            await fetch(endpoint);
        } catch (e) {
            console.error(e);
        }
    });

    // Hook Export JSON
    btnExportJson.addEventListener('click', async () => {
        btnExportJson.disabled = true;
        btnExportJson.innerHTML = `<span>Saving JSON...</span>`;
        try {
            const res = await fetch('/api/export');
            const data = await res.json();
            alert(`State transitions successfully dumped to ${data.file}!`);
        } catch (err) {
            alert('Export error: ' + err.message);
        } finally {
            btnExportJson.disabled = false;
            btnExportJson.innerHTML = `
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                    <polyline points="7 10 12 15 17 10"></polyline>
                    <line x1="12" y1="15" x2="12" y2="3"></line>
                </svg>
                <span>Save state_transitions.json</span>
            `;
        }
    });
});

function updateModeUI(isMdacc) {
    const btnModeToggle = document.getElementById('btnModeToggle');
    const modeToggleText = document.getElementById('modeToggleText');
    if (!btnModeToggle || !modeToggleText) return;

    if (isMdacc) {
        btnModeToggle.classList.remove('mode-baseline');
        modeToggleText.innerText = 'MODE: MD-ACC ACTIVE';
    } else {
        btnModeToggle.classList.add('mode-baseline');
        modeToggleText.innerText = 'MODE: BASELINE 2PL/OCC';
    }
}

async function fetchSimulationStatus() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) return;
        const s = await res.json();

        // 1. Tick
        document.getElementById('valTick').innerText = (s.tick || 0).toLocaleString();

        // 2. Completed / 176,000
        const completed = s.completed_count || 0;
        const total = s.total_workload || 176000;
        document.getElementById('valCompleted').innerText = completed.toLocaleString();
        const pct = Math.min(100, (completed / Math.max(1, total)) * 100);
        document.getElementById('barProgress').style.width = pct + '%';
        document.getElementById('valProgressPct').innerText = `${pct.toFixed(2)}% of workload completed`;

        // 3. Aging Promotions
        document.getElementById('valAgingPromotions').innerText = (s.aging_promotions || 0).toLocaleString();

        // 4. Turnaround & Wait
        document.getElementById('valAvgTurnaround').innerText = Number(s.avg_turnaround_time || 0).toFixed(1);
        document.getElementById('valAvgWaitText').innerText = `Avg Wait Time: ${Number(s.avg_waiting_time || 0).toFixed(1)} ticks`;

        // 5. Counts
        document.getElementById('q1CountPill').innerText = `${s.q1_size || 0} Ready (SRTF)`;
        document.getElementById('q2CountPill').innerText = `${s.q2_size || 0} Ready (RR)`;
        document.getElementById('waitingCountPill').innerText = `${s.waiting_size || 0} Blocked`;

        // 6. MD-ACC Middleware & Comparison Mode Telemetry
        updateModeUI(s.mdacc_enabled);

        // Contention Score Badge
        const scoreBadge = document.getElementById('valContentionBadge');
        if (scoreBadge && s.contention_score) {
            scoreBadge.innerText = s.contention_score;
            scoreBadge.className = 'score-badge ' + (
                s.contention_score === 'HIGH' ? 'score-high' :
                (s.contention_score === 'MEDIUM' ? 'score-medium' : 'score-low')
            );
        }

        // Conflict Rate
        const conflictPct = ((s.conflict_rate || 0) * 100).toFixed(1);
        document.getElementById('valConflictRate').innerText = `${conflictPct}%`;
        const barConflict = document.getElementById('barConflict');
        if (barConflict) {
            barConflict.style.width = Math.min(100, conflictPct) + '%';
        }
        document.getElementById('valQueueLen').innerText = `Queue Depth: ${(s.q1_size || 0) + (s.q2_size || 0) + (s.waiting_size || 0)} processes`;

        // Abort Storm Rate
        const abortPct = ((s.abort_rate || 0) * 100).toFixed(1);
        const totalAborts = s.total_aborts || 0;
        document.getElementById('valAbortRate').innerText = `${abortPct}%`;
        document.getElementById('valTotalAbortsUnit').innerText = `(${totalAborts.toLocaleString()} aborts)`;
        const abortDesc = document.getElementById('valAbortDesc');
        if (abortDesc) {
            if (s.mdacc_enabled) {
                abortDesc.innerText = 'MD-ACC eliminates abort storms via backoff';
            } else {
                abortDesc.innerText = 'Direct DB collisions active: high intentional aborts!';
            }
        }

        // Real-Time TPS
        const now = Date.now();
        const dt = (now - prevTimestamp) / 1000.0;
        if (dt > 0.2) {
            const dComp = completed - prevCompleted;
            const tps = Math.max(0, Math.round(dComp / dt));
            document.getElementById('valTps').innerText = tps.toLocaleString();
            prevCompleted = completed;
            prevTimestamp = now;
        }

        // Backoffs Applied
        document.getElementById('valTotalBackoffs').innerText = `Backoffs Applied: ${(s.total_backoffs || 0).toLocaleString()} delays`;

    } catch (e) {
        // network retry
    }
}

async function fetchQueuesAndRunning() {
    try {
        const res = await fetch('/api/queues');
        if (!res.ok) return;
        const q = await res.json();

        // 1. Active Running PCB
        const cpuCard = document.getElementById('cpuRunningCard');
        const runningBadge = document.getElementById('runningBadge');
        const runningDesc = document.getElementById('runningDesc');
        const runningDetails = document.getElementById('runningDetails');

        if (q.running) {
            const p = q.running;
            runningBadge.innerText = `PID ${p.pid} [RUNNING]`;
            runningBadge.className = 'running-pcb-badge';
            runningDesc.innerText = `Executing ${p.type} transaction (${p.burst_time} ticks remaining)`;
            runningDetails.style.display = 'grid';

            document.getElementById('runPid').innerText = p.pid;
            document.getElementById('runType').innerText = p.type;
            document.getElementById('runQueue').innerText = p.current_queue === 1 ? 'Q1 (SRTF)' : 'Q2 (Round Robin)';
            document.getElementById('runBurst').innerText = `${p.burst_time} / ${p.total_burst_time} ticks`;
            document.getElementById('runLocks').innerText = (p.required_locks && p.required_locks.length) ? p.required_locks.join(', ') : 'None';
        } else {
            runningBadge.innerText = 'CPU IDLE';
            runningBadge.className = 'running-pcb-badge';
            runningBadge.style.color = '#94a3b8';
            runningDesc.innerText = 'No ready process with free locks currently available';
            runningDetails.style.display = 'none';
        }

        // 2. Queue 1 (SRTF)
        renderProcessList('q1ProcessList', q.q1, 'Queue 1 is currently empty');

        // 3. Queue 2 (Round Robin)
        renderProcessList('q2ProcessList', q.q2, 'Queue 2 is currently empty', true);

        // 4. Waiting List
        renderProcessList('waitingProcessList', q.waiting, 'No processes currently blocked on locks');

    } catch (e) {
        // retry
    }
}

function renderProcessList(containerId, list, emptyText, showAging = false) {
    const container = document.getElementById(containerId);
    if (!container) return;

    if (!list || list.length === 0) {
        container.innerHTML = `<div class="empty-placeholder">${emptyText}</div>`;
        return;
    }

    let html = '';
    for (const p of list) {
        const typeClass = p.type === 'UPI' ? 'tag-upi' : (p.type === 'ATM' ? 'tag-atm' : 'tag-salary');
        const agedTag = p.aged_promoted ? `<span class="tag-aged">AGED PROMOTED</span>` : '';
        const agingInfo = (showAging && p.queue2_ticks !== undefined) ? `<span class="locks-indicator">Age: ${p.queue2_ticks}/50</span>` : '';
        const backoffTag = (p.backoff_remaining && p.backoff_remaining > 0) ? `<span class="tag-backoff">Wait: ${p.backoff_remaining} ticks</span>` : '';
        const abortTag = (p.abort_count && p.abort_count > 0) ? `<span class="tag-abort">Aborts: ${p.abort_count}</span>` : '';
        const locksStr = p.required_locks && p.required_locks.length ? `Locks: [${p.required_locks.join(', ')}]` : '';

        html += `
            <div class="pcb-card">
                <div class="pcb-left">
                    <span class="pcb-pid">#${p.pid}</span>
                    <span class="tx-badge ${typeClass}">${p.type}</span>
                    ${agedTag}
                    ${backoffTag}
                    ${abortTag}
                </div>
                <div class="pcb-right">
                    ${agingInfo}
                    <span class="burst-indicator">${p.burst_time} ticks rem</span>
                    <span class="locks-indicator">${locksStr}</span>
                </div>
            </div>
        `;
    }
    container.innerHTML = html;
}

async function fetchTransitions() {
    try {
        const res = await fetch('/api/transitions');
        if (!res.ok) return;
        const list = await res.json();

        const tbody = document.getElementById('transitionsTableBody');
        if (!tbody || !list || list.length === 0) return;

        let html = '';
        // Render in reverse chronological order (newest on top)
        for (let i = list.length - 1; i >= 0; --i) {
            const t = list[i];
            const typeClass = t.type === 'UPI' ? 'tag-upi' : (t.type === 'ATM' ? 'tag-atm' : 'tag-salary');
            html += `
                <tr>
                    <td style="color: var(--cyan); font-weight: 700;">T+${t.tick}</td>
                    <td style="font-weight: 700;">#${t.pid}</td>
                    <td><span class="tx-badge ${typeClass}">${t.type}</span></td>
                    <td><span class="state-badge state-${t.from_state}">${t.from_state}</span></td>
                    <td><span class="state-badge state-${t.to_state}">${t.to_state}</span></td>
                    <td style="color: var(--text-secondary);">${t.reason}</td>
                </tr>
            `;
        }
        tbody.innerHTML = html;
    } catch (e) {
        // retry
    }
}
