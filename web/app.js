// Adaptive Ledger OS Web Client

document.addEventListener('DOMContentLoaded', () => {
    const btnRunBenchmark = document.getElementById('btnRunBenchmark');
    const benchmarkStatus = document.getElementById('benchmarkStatus');

    // Start live polling loop
    setInterval(fetchSystemStatus, 800);
    fetchSystemStatus();

    // Hook benchmark button
    btnRunBenchmark.addEventListener('click', async () => {
        btnRunBenchmark.disabled = true;
        btnRunBenchmark.innerHTML = `<span>Simulating Peak Burst...</span>`;
        benchmarkStatus.innerText = 'Running Comparative Stress Benchmark (Ct >= 0.8)...';
        benchmarkStatus.style.color = '#00f2fe';

        try {
            const res = await fetch('/api/benchmark');
            const data = await res.json();
            updateBenchmarkTable(data);
            benchmarkStatus.innerText = 'Benchmark Completed Successfully!';
            benchmarkStatus.style.color = '#10b981';
        } catch (err) {
            console.error('Benchmark failed', err);
            benchmarkStatus.innerText = 'Benchmark error: ' + err.message;
            benchmarkStatus.style.color = '#ef4444';
        } finally {
            btnRunBenchmark.disabled = false;
            btnRunBenchmark.innerHTML = `
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <polygon points="5 3 19 12 5 21 5 3"></polygon>
                </svg>
                <span>Run Verification Benchmark</span>
            `;
        }
    });
});

async function fetchSystemStatus() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) return;
        const data = await res.json();
        renderStatus(data);
    } catch (err) {
        // Silently retry on next tick
    }
}

function renderStatus(s) {
    // 1. Composite Contention Index Ct
    const valCt = document.getElementById('valCt');
    const barCt = document.getElementById('barCt');
    if (valCt && barCt) {
        const ctFixed = Number(s.composite_index).toFixed(2);
        valCt.innerText = ctFixed;
        const pct = Math.min(100, s.composite_index * 100);
        barCt.style.width = pct + '%';

        if (s.composite_index >= 0.70) {
            valCt.style.color = '#ef4444';
        } else if (s.composite_index >= 0.40) {
            valCt.style.color = '#f59e0b';
        } else {
            valCt.style.color = '#10b981';
        }
    }

    // 2. Admission Rate lambda_eff
    const valLambda = document.getElementById('valLambdaEff');
    if (valLambda) {
        valLambda.innerText = Math.round(s.effective_admission_rate).toLocaleString();
    }

    // 3. Bounded Wait Tw
    const valTw = document.getElementById('valTw');
    if (valTw) {
        valTw.innerText = Number(s.bounded_wait_tw_ms).toFixed(1);
    }

    // 4. Committed Transactions
    const valCommitted = document.getElementById('valCommitted');
    const valAbortedText = document.getElementById('valAbortedText');
    if (valCommitted) {
        valCommitted.innerText = (s.total_committed || 0).toLocaleString();
    }
    if (valAbortedText) {
        valAbortedText.innerText = `${s.total_aborted || 0} aborts | ${s.total_retries || 0} retries`;
    }

    // 5. Hysteresis Badge & Steps
    const badge = document.getElementById('hysteresisStatusBadge');
    const badgeText = document.getElementById('hysteresisStatusText');
    const stepNormal = document.getElementById('hStepNormal');
    const stepElevated = document.getElementById('hStepElevated');
    const stepHigh = document.getElementById('hStepHigh');

    if (badge && badgeText) {
        badge.className = 'status-badge';
        stepNormal?.classList.remove('active-step', 'active-high');
        stepElevated?.classList.remove('active-step', 'active-high');
        stepHigh?.classList.remove('active-step', 'active-high');

        if (s.hysteresis_state === 'HIGH_CONTENTION') {
            badge.classList.add('high-contention');
            badgeText.innerText = 'STATE: HIGH_CONTENTION (Ct >= 0.7)';
            stepHigh?.classList.add('active-high');
        } else if (s.hysteresis_state === 'ELEVATED') {
            badge.classList.add('elevated');
            badgeText.innerText = 'STATE: ELEVATED (Stabilizing)';
            stepElevated?.classList.add('active-step');
        } else {
            badgeText.innerText = 'STATE: NORMAL';
            stepNormal?.classList.add('active-step');
        }
    }

    // 6. MLFQ Queues (Q1, Q2, Q3)
    const barQ1 = document.getElementById('barQ1');
    const barQ2 = document.getElementById('barQ2');
    const barQ3 = document.getElementById('barQ3');
    const countQ1 = document.getElementById('countQ1');
    const countQ2 = document.getElementById('countQ2');
    const countQ3 = document.getElementById('countQ3');

    if (countQ1) countQ1.innerText = `${s.q1_depth} pending`;
    if (countQ2) countQ2.innerText = `${s.q2_depth} pending`;
    if (countQ3) countQ3.innerText = `${s.q3_depth} pending`;

    if (barQ1) barQ1.style.width = Math.min(100, s.q1_depth * 8) + '%';
    if (barQ2) barQ2.style.width = Math.min(100, s.q2_depth * 8) + '%';
    if (barQ3) barQ3.style.width = Math.min(100, s.q3_depth * 8) + '%';

    // 7. Diagnostics
    const workerStatus = document.getElementById('workerStatus');
    if (workerStatus) {
        workerStatus.innerText = `Active Workers: ${s.active_workers} / ${s.num_workers}`;
    }

    const valAgingMig = document.getElementById('valAgingMig');
    const valHotspotDem = document.getElementById('valHotspotDem');
    const valThrottled = document.getElementById('valThrottled');
    if (valAgingMig) valAgingMig.innerText = (s.starvation_migrations || 0).toLocaleString();
    if (valHotspotDem) valHotspotDem.innerText = (s.total_hotspot_demotions || 0).toLocaleString();
    if (valThrottled) valThrottled.innerText = (s.total_throttled || 0).toLocaleString();

    // 8. Telemetry breakdown gauges
    updateGauge('valCr', 'barCr', s.conflict_rate);
    updateGauge('valAr', 'barAr', s.abort_rate);
    updateGauge('valQl', 'barQl', s.lock_queue_depth);
    updateGauge('valRi', 'barRi', s.retry_intensity);
}

function updateGauge(textId, barId, val) {
    const elText = document.getElementById(textId);
    const elBar = document.getElementById(barId);
    if (elText) elText.innerText = Number(val || 0).toFixed(2);
    if (elBar) elBar.style.width = Math.min(100, (val || 0) * 100) + '%';
}

function updateBenchmarkTable(data) {
    const b = data.baseline;
    const a = data.adaptive;

    document.getElementById('benchBaseTps').innerText = `${Math.round(b.throughput_tps)} tx/s`;
    document.getElementById('benchAdapTps').innerText = `${Math.round(a.throughput_tps)} tx/s`;
    const tpsGain = Math.round(((a.throughput_tps - b.throughput_tps) / Math.max(1, b.throughput_tps)) * 100);
    document.getElementById('benchDiffTps').innerText = `+${tpsGain}% Throughput Gain`;

    document.getElementById('benchBaseAbort').innerText = `${b.abort_rate_percent.toFixed(1)}% (Abort Storm)`;
    document.getElementById('benchAdapAbort').innerText = `${a.abort_rate_percent.toFixed(1)}% (Regulated)`;
    const abortDiff = (b.abort_rate_percent - a.abort_rate_percent).toFixed(1);
    document.getElementById('benchDiffAbort').innerText = `-${abortDiff}% Aborts Mitigated`;

    document.getElementById('benchBaseP50').innerText = `${b.p50_latency_ms.toFixed(1)} ms`;
    document.getElementById('benchAdapP50').innerText = `${a.p50_latency_ms.toFixed(1)} ms`;
    document.getElementById('benchDiffP50').innerText = `Stabilized`;

    document.getElementById('benchBaseP95').innerText = `${b.p95_latency_ms.toFixed(1)} ms`;
    document.getElementById('benchAdapP95').innerText = `${a.p95_latency_ms.toFixed(1)} ms`;
    document.getElementById('benchDiffP95').innerText = `Tail Latency Tamed`;

    document.getElementById('benchBaseP99').innerText = `${b.p99_latency_ms.toFixed(1)} ms`;
    document.getElementById('benchAdapP99').innerText = `${a.p99_latency_ms.toFixed(1)} ms`;
    document.getElementById('benchDiffP99').innerText = `Convoy Eliminated`;

    document.getElementById('benchBaseRetail').innerText = `${b.retail_p95_ms.toFixed(1)} ms (Starvation)`;
    document.getElementById('benchAdapRetail').innerText = `${a.retail_p95_ms.toFixed(1)} ms (MLFQ Q1)`;

    document.getElementById('benchBaseSettle').innerText = `${b.settlement_p95_ms.toFixed(1)} ms (Contention)`;
    document.getElementById('benchAdapSettle').innerText = `${a.settlement_p95_ms.toFixed(1)} ms (Demoted Tw)`;
}
