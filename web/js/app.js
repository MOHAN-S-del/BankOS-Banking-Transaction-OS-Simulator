// ==========================================================================
// BANK OS MISSION CONTROL: DASHBOARD CONTROLLER & REAL-TIME RENDERER
// ==========================================================================

const $ = id => document.getElementById(id);
const formatMoney = n => '₹ ' + Number(n).toLocaleString('en-IN', { maximumFractionDigits: 0 });
const pad = n => String(n).padStart(2, '0');

let isRunning = false;
let pollTimer = null;
let currentQuantum = 2;

// API Base URL (empty string for same origin, or localhost:8080)
const API_BASE = window.location.origin.includes('http') ? window.location.origin : 'http://localhost:8080';

async function fetchState() {
  try {
    const res = await fetch(`${API_BASE}/api/state`);
    if (!res.ok) throw new Error('API offline');
    const data = await res.json();
    renderAll(data);
  } catch (err) {
    // If backend isn't reached, try to inform user on the badge
    if ($('dbStatusLabel')) {
      $('dbStatusLabel').textContent = 'CONNECTING TO C++ ENGINE...';
    }
  }
}

async function sendAction(endpoint, method = 'POST') {
  try {
    const res = await fetch(`${API_BASE}${endpoint}`, { method });
    if (res.ok) {
      const data = await res.json();
      renderAll(data);
    }
  } catch (err) {
    console.error('API action failed:', err);
  }
}

function renderAll(data) {
  if (!data) return;

  const { stats, cpu, ready_queue, process_table, locks, balances, gantt, logs, db_connected } = data;

  // 1. Connection & DB Badge
  if ($('dbStatusLabel')) {
    $('dbStatusLabel').textContent = db_connected ? 'MYSQL 8.X CONNECTED' : 'SIMULATOR ENGINE ONLINE';
  }
  if ($('dbTypeBadge')) {
    $('dbTypeBadge').textContent = db_connected ? 'MYSQL INNODB PERSISTENT' : 'IN-MEMORY DBMS MIRROR';
  }

  // 2. Metrics Bar
  if (stats) {
    $('mTotal').textContent = pad(stats.total_transactions);
    $('mDone').textContent = pad(stats.completed_transactions);
    $('mFailed').textContent = stats.failed_transactions;
    $('mWait').textContent = stats.avg_waiting_time > 0 ? stats.avg_waiting_time.toFixed(1) : '—';
    $('mTat').textContent = stats.avg_turnaround_time > 0 ? stats.avg_turnaround_time.toFixed(1) : '—';
    $('mCpu').textContent = Math.round(stats.cpu_utilization) + '%';
    $('mThroughput').textContent = stats.throughput.toFixed(2);
    $('clockTickVal').textContent = stats.clock_tick;

    isRunning = stats.is_running;
    $('btnRun').textContent = isRunning ? 'Ⅱ Pause Engine' : '▶ Run Engine';
    if (isRunning) {
      $('btnRun').classList.remove('btn-primary');
      $('btnRun').style.background = 'var(--amber)';
      $('btnRun').style.color = '#000';
    } else {
      $('btnRun').classList.add('btn-primary');
      $('btnRun').style.background = '';
      $('btnRun').style.color = '';
    }
  }

  // 3. CPU Core Unit
  if (cpu) {
    const isActive = cpu.is_active;
    $('cpuStatusBadge').textContent = isActive ? 'RUNNING' : 'IDLE';
    $('cpuStatusBadge').className = 'badge-status ' + (isActive ? 'running' : 'idle');
    $('cpuPid').textContent = isActive ? cpu.pid : '—';
    $('cpuBurstLabel').textContent = isActive ? `${cpu.remaining} ticks left (slice: ${cpu.slice_remaining})` : 'No active process';
    $('cpuTaskDetails').textContent = isActive ? 
      `${cpu.type} · ${cpu.source || '—'} ${cpu.type === 'Transfer' ? '➔ ' + cpu.destination : ''} · ${formatMoney(cpu.amount)}` : 
      'Awaiting process dispatch from Ready Queue';

    const burstPct = isActive && cpu.burst ? ((cpu.burst - cpu.remaining) / cpu.burst * 100) : 0;
    $('burstBar').style.width = burstPct + '%';
  }

  // 4. Ready Queue
  if (ready_queue) {
    $('queueLengthBadge').textContent = `${ready_queue.length} processes`;
    if (ready_queue.length === 0) {
      $('readyQueueBox').innerHTML = `<span style="color:var(--text-dim); font-size:12px;">Queue is empty — submit a transaction or click Batch to add work.</span>`;
    } else {
      $('readyQueueBox').innerHTML = ready_queue.map(p => `
        <div class="queue-chip">
          <span>${p.pid}</span>
          <small>${p.type || ''} · ${p.remaining} ticks</small>
        </div>
      `).join('');
    }
  }

  // 5. Gantt Timeline
  if (gantt && gantt.length > 0) {
    $('ganttBox').innerHTML = gantt.map(g => `
      <div class="gantt-slice" title="PID ${g.pid} (${g.tx_type}): T+${g.start} to T+${g.end}">
        ${g.pid} [${g.start}-${g.end}]
      </div>
    `).join('');
    // Auto-scroll to right
    $('ganttBox').scrollLeft = $('ganttBox').scrollWidth;
  }

  // 6. PCB Table
  if (process_table) {
    $('pcbCountBadge').textContent = `${process_table.length} PCBs ACTIVE`;
    $('pcbTableBody').innerHTML = process_table.slice().reverse().map(p => {
      const route = p.type === 'Deposit' ? `➔ ${p.to}` :
                    p.type === 'Withdraw' ? `${p.from} ➔ Cash` :
                    `${p.from} ➔ ${p.to}`;
      return `
        <tr>
          <td style="color:var(--cyan); font-weight:700;">${p.pid}</td>
          <td>${p.type}</td>
          <td>${route}</td>
          <td style="font-weight:700;">${formatMoney(p.amount)}</td>
          <td><span class="state-tag ${p.state}">${p.state}</span></td>
          <td>${p.burst}</td>
          <td>${p.remaining}</td>
          <td>${p.waiting_time}</td>
          <td>${p.turnaround_time > 0 ? p.turnaround_time : '—'}</td>
          <td style="font-size:11px; color:var(--text-muted); max-width:200px; overflow:hidden; text-overflow:ellipsis;">${p.status}</td>
        </tr>
      `;
    }).join('');
  }

  // 7. Lock Matrix
  if (locks) {
    $('lockMatrixBox').innerHTML = locks.map(l => `
      <div class="lock-card ${l.is_locked ? 'locked' : ''}">
        <div class="lock-card-header">
          <strong>${l.resource}</strong>
          <span class="lock-status-badge ${l.is_locked ? 'held' : 'free'}">
            ${l.is_locked ? '🔒 LOCKED' : '● AVAILABLE'}
          </span>
        </div>
        <div style="font-size:11px; color:var(--text-dim);">
          ${l.is_locked ? 'Held by ' + l.owner : 'No active contention'}
        </div>
      </div>
    `).join('');
  }

  // 8. Account Balances
  if (balances) {
    const names = { A101: 'Aarav Sharma (Savings)', A102: 'Diya Patel (Savings)', A103: 'Rohan Mehta (Current)' };
    $('balanceListBox').innerHTML = Object.entries(balances).map(([acc, bal]) => `
      <div class="balance-item">
        <div>
          <strong style="color:var(--text-main); font-size:13px;">${acc}</strong>
          <div style="font-size:10px; color:var(--text-dim);">${names[acc] || 'Account'}</div>
        </div>
        <div class="balance-amount">${formatMoney(bal)}</div>
      </div>
    `).join('');
  }

  // 9. Kernel Event Logs
  if (logs) {
    $('kernelLogBox').innerHTML = logs.map(l => `
      <div class="log-entry">
        <span class="log-tick">T+${pad(l.tick)}</span>
        <span class="log-pid">[${l.pid}]</span>
        <span class="log-msg-${l.type}">${l.msg}</span>
      </div>
    `).join('') || '<div style="color:var(--text-dim); font-size:11px;">No kernel events recorded yet.</div>';
  }
}

// Button Bindings
$('btnStep').onclick = () => sendAction('/api/scheduler/step');
$('btnRun').onclick = () => {
  if (isRunning) {
    sendAction('/api/scheduler/pause');
  } else {
    sendAction('/api/scheduler/run');
  }
};
$('btnReset').onclick = () => sendAction('/api/scheduler/reset');

$('quantumRange').oninput = e => {
  const q = e.target.value;
  $('quantumVal').textContent = q;
  $('quantumBadge').textContent = `TIME QUANTUM: q = ${q}`;
  sendAction(`/api/scheduler/quantum?q=${q}`);
};

$('btnAddTx').onclick = () => {
  const type = $('txType').value;
  const from = $('txFrom').value;
  const to = $('txTo').value;
  const amount = $('txAmount').value;

  if (type === 'Transfer' && from === to) {
    alert('Source and destination accounts must be distinct for transfers.');
    return;
  }
  if (!amount || Number(amount) <= 0) {
    alert('Please enter a valid positive transaction amount.');
    return;
  }

  sendAction(`/api/transaction?type=${encodeURIComponent(type)}&from=${encodeURIComponent(from)}&to=${encodeURIComponent(to)}&amount=${encodeURIComponent(amount)}`);
};

$('btnBatch10').onclick = () => sendAction('/api/batch?count=10');
$('btnBatch50').onclick = () => sendAction('/api/batch?count=50');

$('btnClearLogs').onclick = () => {
  $('kernelLogBox').innerHTML = '<div style="color:var(--text-dim); font-size:11px;">Logs cleared.</div>';
};

// Comparison Modal Controls
function openComparisonModal() {
  $('compareModal').classList.add('open');
}

function closeComparisonModal() {
  $('compareModal').classList.remove('open');
}

window.onclick = e => {
  if (e.target === $('compareModal')) closeComparisonModal();
};

// Initial Poll & Loop
fetchState();
pollTimer = setInterval(fetchState, 500);
