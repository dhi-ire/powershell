/* ============================================================
   App — routing, views, and workflow actions.
   ============================================================ */

const { ROLES, PO_ADMIN_THRESHOLD, MIN_PASSWORD, CATEGORIES, REQUEST_STATUS, PO_STATUS, ASSET_STATUS, poTotal } = Rules;

/* Data the server sent for the signed-in person (null when signed out). */
let STATE = null;
let needsSetup = false;

const S = () => STATE;
const me = () => (STATE ? STATE.me : null);
const can = (perm) => Rules.can(me(), perm);
const userById = (id) => S().users.find((u) => u.id === id);
const nameOf = (id) => userById(id)?.name || '—';
const initials = (name) => name.split(' ').map((p) => p[0]).slice(0, 2).join('').toUpperCase();
const teamIds = (u) => Rules.teamIds(S(), u);
const visibleAssets = (u) => (u.role === 'admin' ? S().assets : Rules.visibleAssets(S(), u));
const visibleRequests = (u) => Rules.visibleRequests(S(), u);
const visiblePOs = (u) => Rules.visiblePOs(S(), u);
const myQueue = (u) => Rules.myQueue(S(), u);

/* ---------- Formatting ---------- */
function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}
const money = (n) => new Intl.NumberFormat('en-IE', { style: 'currency', currency: 'EUR', maximumFractionDigits: 0 }).format(n || 0);
function timeAgo(iso) {
  const s = (Date.now() - new Date(iso).getTime()) / 1000;
  if (s < 60) return 'just now';
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`;
  return `${Math.floor(s / 86400)}d ago`;
}

/* ---------- Server calls ---------- */
async function api(path, body) {
  const opts = { credentials: 'same-origin', headers: {} };
  if (body !== undefined) {
    opts.method = 'POST';
    opts.headers = { 'Content-Type': 'application/json', 'X-Requested-With': 'AssetFlow' };
    opts.body = JSON.stringify(body);
  }
  let res;
  try { res = await fetch(path, opts); } catch { throw new Error('Can\'t reach the server. Check your connection and try again.'); }
  let data = {};
  try { data = await res.json(); } catch { /* empty body */ }
  if (res.status === 401 && path.startsWith('/api/actions/')) { STATE = null; closeModal(); route(); }
  if (!res.ok) throw new Error(data.error || 'Something went wrong. Try again.');
  return data;
}

/* Run a server action, then redraw with the fresh data. */
async function act(name, body) {
  try {
    const d = await api(`/api/actions/${name}`, body);
    STATE = d.state;
    closeModal();
    route(false);
    toast(d.message);
    return true;
  } catch (e) {
    toast(e.message);
    return false;
  }
}

/* ---------- Status badges ---------- */
const STATUS_CLASS = {
  Pending: 'b-warn', 'Pending Manager': 'b-warn', 'Pending Admin': 'b-warn',
  Approved: 'b-info', Ordered: 'b-accent',
  Fulfilled: 'b-ok', Received: 'b-ok', Available: 'b-ok',
  Assigned: 'b-info', Maintenance: 'b-warn',
  Rejected: 'b-bad', Retired: 'b-muted',
};
const badge = (s) => `<span class="badge ${STATUS_CLASS[s] || 'b-muted'}">${esc(s)}</span>`;

/* ---------- Icons ---------- */
const ICON = {
  user: '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="4"/><path d="M4 21c0-4.4 3.6-8 8-8s8 3.6 8 8"/></svg>',
  manager: '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><circle cx="9" cy="8" r="3.5"/><path d="M2 20c0-3.9 3.1-7 7-7s7 3.1 7 7"/><path d="M16 4.5a3.5 3.5 0 0 1 0 7"/><path d="M18 13.5c2.4.9 4 3.2 4 6.5"/></svg>',
  admin: '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2l8 3v6c0 5-3.4 9.4-8 11-4.6-1.6-8-6-8-11V5z"/><path d="M9 12l2 2 4-4"/></svg>',
};

/* ============================================================
   Router
   ============================================================ */
const NAV = {
  user:    [['dashboard', 'Dashboard'], ['assets', 'My Assets'], ['requests', 'Requests'], ['pos', 'Purchase Orders']],
  manager: [['dashboard', 'Dashboard'], ['approvals', 'Approvals'], ['assets', 'Team Assets'], ['requests', 'Requests'], ['pos', 'Purchase Orders']],
  admin:   [['dashboard', 'Dashboard'], ['approvals', 'Approvals'], ['assets', 'Assets'], ['requests', 'Requests'], ['pos', 'Purchase Orders'], ['users', 'Users']],
};

const VIEWS = { profile: viewProfile, dashboard: viewDashboard, approvals: viewApprovals, assets: viewAssets, requests: viewRequests, pos: viewPOs, users: viewUsers };

function route(scroll = true) {
  const u = me();
  const page = (location.hash.replace(/^#\/?/, '') || 'dashboard').split('?')[0];
  const root = document.getElementById('app');
  if (!u) { root.innerHTML = needsSetup ? viewSetup() : viewLanding(); return; }
  const allowed = [...NAV[u.role].map(([k]) => k), 'profile'];
  const key = allowed.includes(page) ? page : 'dashboard';
  root.innerHTML = shell(key, VIEWS[key](u));
  if (scroll) window.scrollTo(0, 0);
}

function shell(active, content) {
  const u = me();
  const q = myQueue(u).total;
  const links = NAV[u.role].map(([k, label]) =>
    `<a href="#${k}" class="${k === active ? 'active' : ''}">${label}${k === 'approvals' && q ? `<span class="count">${q}</span>` : ''}</a>`).join('');
  return `
    <header class="nav"><div class="container nav-inner">
      <a class="logo" href="#dashboard"><span class="logo-mark">A</span>AssetFlow</a>
      <nav class="nav-links">${links}</nav>
      <div class="nav-user">
        <span class="role-pill">${ROLES[u.role].label}</span>
        <span class="name" style="font-size:14px">${esc(u.name)}</span>
        <a class="avatar ${active === 'profile' ? 'avatar-active' : ''}" href="#profile" title="My profile (${esc(u.email)})">${initials(u.name)}</a>
        <button class="btn btn-ghost btn-sm" data-action="logout">Sign out</button>
      </div>
    </div></header>
    <main><div class="container">${content}</div></main>`;
}

/* ============================================================
   Landing — role picker, styled like a download page
   ============================================================ */
function viewLanding() {
  return `
    <header class="nav"><div class="container nav-inner">
      <a class="logo" href="#"><span class="logo-mark">A</span>AssetFlow</a>
    </div></header>
    <main><div class="container">
      <section class="hero">
        <span class="eyebrow"><span class="dot"></span>Assets · Requests · Purchase orders</span>
        <h1>Every asset tracked.<br><em>Every approval</em> in one click.</h1>
        <p class="lead">Request equipment, raise purchase orders and route them through approvals, with the right view for users, managers and admins.</p>
      </section>

      <section class="login-card card" aria-labelledby="login-title">
        <h2 id="login-title">Sign in</h2>
        <p class="hint">Use your work email. Ask an admin if you need an account or a password reset.</p>
        <form class="form" id="login-form" novalidate>
          <label>Email<input id="login-email" name="email" type="email" autocomplete="username" required placeholder="you@company.com"></label>
          <label>Password<input id="login-password" name="password" type="password" autocomplete="current-password" required></label>
          <p class="error-text" id="login-error" role="alert" hidden></p>
          <button class="btn btn-primary btn-lg" type="submit">Sign in</button>
        </form>
      </section>

      <div class="section-title"><h2>How approvals flow</h2><p>One pipeline for equipment requests and purchase orders.</p></div>
      <section class="flow">
        <div class="flow-step"><div class="n">1</div><h4>Submit</h4><p>A user requests an asset or fills a purchase order with line items.</p></div>
        <div class="flow-step"><div class="n">2</div><h4>Manager review</h4><p>The requester's manager approves or rejects with a note.</p></div>
        <div class="flow-step"><div class="n">3</div><h4>Admin sign-off</h4><p>POs above ${money(PO_ADMIN_THRESHOLD)} need admin approval before ordering.</p></div>
        <div class="flow-step"><div class="n">4</div><h4>Fulfil</h4><p>Admin assigns stock, or orders and receives it into inventory.</p></div>
      </section>
    </div></main>
    <footer class="footer"><div class="container"><span>© ${new Date().getFullYear()} AssetFlow</span><span>Asset management · Purchase orders · Approvals</span></div></footer>`;
}

/* First run: no users yet, so create the first admin. */
function viewSetup() {
  return `
    <header class="nav"><div class="container nav-inner"><a class="logo" href="#"><span class="logo-mark">A</span>AssetFlow</a></div></header>
    <main><div class="container">
      <section class="hero" style="padding-bottom:0">
        <span class="eyebrow"><span class="dot"></span>First-time setup</span>
        <h1>Create the <em>admin</em> account.</h1>
        <p class="lead">This account manages users, assets and final approvals. You'll add everyone else from the Users page.</p>
      </section>
      <section class="login-card card" aria-labelledby="setup-title">
        <h2 id="setup-title">Admin details</h2>
        <form class="form" id="setup-form" novalidate>
          <label>Full name<input id="setup-name" name="name" required autocomplete="name"></label>
          <label>Work email<input id="setup-email" name="email" type="email" required autocomplete="username"></label>
          <label>Department<input id="setup-dept" name="dept" value="IT" required></label>
          <label>Password<input id="setup-password" name="password" type="password" required minlength="${MIN_PASSWORD}" autocomplete="new-password"></label>
          <p class="hint">At least ${MIN_PASSWORD} characters.</p>
          <p class="error-text" id="setup-error" role="alert" hidden></p>
          <button class="btn btn-primary btn-lg" type="submit">Create admin and sign in</button>
        </form>
      </section>
    </div></main>`;
}

/* ============================================================
   Profile — every role
   ============================================================ */
function viewProfile(u) {
  const mine = S().assets.filter((a) => a.assignedTo === u.id);
  const team = S().users.filter((x) => x.managerId === u.id);
  return `
    <div class="page-head"><div><h1>${esc(u.name)}</h1><p>${ROLES[u.role].label} · ${esc(u.dept)}</p></div></div>
    <div class="grid-2">
      <div style="display:grid;gap:16px;align-content:start">
        <div class="card"><div class="card-head"><h3>Account</h3></div><div class="card-body">
          <dl class="kv" style="margin:0"><dt>Email</dt><dd>${esc(u.email)}</dd><dt>Role</dt><dd><span class="role-pill">${ROLES[u.role].label}</span></dd>
            <dt>Department</dt><dd>${esc(u.dept)}</dd><dt>Reports to</dt><dd>${esc(nameOf(u.managerId))}</dd>
            ${team.length ? `<dt>Team</dt><dd>${team.map((t) => esc(t.name)).join(', ')}</dd>` : ''}</dl>
        </div></div>
        <div class="card"><div class="card-head"><h3>Assets assigned to you</h3><span class="hint">${mine.length}</span></div>
          <div class="list">${mine.map((a) => `<div class="list-item"><div class="grow"><strong>${esc(a.name)}</strong><div class="meta mono">${esc(a.tag)} · ${esc(a.category)}</div></div>${badge(a.status)}</div>`).join('') || '<div class="empty">No assets assigned yet.</div>'}</div></div>
      </div>
      <div class="card" style="align-self:start"><div class="card-head"><h3>Change password</h3></div><div class="card-body">
        <form class="form" id="password-form" novalidate>
          <label>Current password<input id="pw-current" name="current" type="password" autocomplete="current-password" required></label>
          <label>New password<input id="pw-new" name="next" type="password" autocomplete="new-password" required minlength="${MIN_PASSWORD}"></label>
          <label>Confirm new password<input id="pw-confirm" name="confirm" type="password" autocomplete="new-password" required></label>
          <p class="hint">At least ${MIN_PASSWORD} characters.</p>
          <p class="error-text" id="pw-error" role="alert" hidden></p>
          <button class="btn btn-primary" type="submit">Update password</button>
        </form>
      </div></div>
    </div>`;
}

/* ============================================================
   Dashboard
   ============================================================ */
function viewDashboard(u) {
  const assets = visibleAssets(u);
  const reqs = visibleRequests(u);
  const pos = visiblePOs(u);
  const q = myQueue(u);
  const openReqs = reqs.filter((r) => r.status === 'Pending' || r.status === 'Approved').length;
  const openPOs = pos.filter((p) => !['Rejected', 'Received'].includes(p.status));
  const spend = openPOs.reduce((s, p) => s + poTotal(p), 0);

  const tiles = u.role === 'user'
    ? [['My assets', assets.length], ['Open requests', openReqs], ['Open POs', openPOs.length], ['Asset value', money(assets.reduce((s, a) => s + a.cost, 0))]]
    : [['Waiting on you', q.total], [u.role === 'admin' ? 'Total assets' : 'Team assets', assets.length], ['Open requests', openReqs], ['Open PO spend', money(spend)]];

  const activity = [
    ...reqs.flatMap((r) => r.history.map((h) => ({ ...h, ref: r.number, what: r.item }))),
    ...pos.flatMap((p) => p.history.map((h) => ({ ...h, ref: p.number, what: p.vendor }))),
  ].sort((a, b) => b.at.localeCompare(a.at)).slice(0, 8);

  const queueBlock = u.role === 'user'
    ? `<div class="card"><div class="card-head"><h3>Quick actions</h3></div><div class="card-body" style="display:grid;gap:10px">
         <button class="btn btn-primary" data-action="new-request">+ Request equipment</button>
         <button class="btn" data-action="new-po">+ Fill purchase order</button>
       </div></div>`
    : `<div class="card"><div class="card-head"><h3>Waiting on you</h3><a class="btn btn-sm" href="#approvals">Open inbox</a></div>
         <div class="list">${queueList(q, 5) || '<div class="empty"><div class="big">✓</div>All caught up</div>'}</div></div>`;

  return `
    <div class="page-head">
      <div><h1>Good ${greeting()}, ${esc(u.name.split(' ')[0])}</h1><p>${ROLES[u.role].label} · ${esc(u.dept)}</p></div>
      <div style="display:flex;gap:8px"><button class="btn" data-action="new-po">New PO</button><button class="btn btn-primary" data-action="new-request">New request</button></div>
    </div>
    <div class="stats">${tiles.map(([l, v], i) => `<div class="stat ${i === 0 ? 'accent' : ''}"><div class="label">${l}</div><div class="value">${v}</div></div>`).join('')}</div>
    <div class="grid-2">
      <div class="card"><div class="card-head"><h3>Recent activity</h3></div>
        <div class="list">${activity.map((a) => `
          <div class="list-item"><div class="avatar">${initials(nameOf(a.by))}</div>
            <div class="grow"><div><strong>${esc(nameOf(a.by))}</strong> ${esc(a.action.toLowerCase())} <span class="mono">${esc(a.ref)}</span></div>
            <div class="meta">${esc(a.what)} · ${timeAgo(a.at)}${a.note ? ` · “${esc(a.note)}”` : ''}</div></div></div>`).join('') || '<div class="empty">No activity yet</div>'}
        </div></div>
      ${queueBlock}
    </div>`;
}

function greeting() {
  const h = new Date().getHours();
  return h < 12 ? 'morning' : h < 18 ? 'afternoon' : 'evening';
}

function queueList(q, limit = Infinity) {
  const rows = [
    ...q.reqs.map((r) => ({ at: r.createdAt, html: `
      <div class="list-item"><div class="grow"><div><span class="mono">${r.number}</span> · ${esc(r.item)}</div>
      <div class="meta">${esc(nameOf(r.requesterId))} · ${esc(r.category)} · ${timeAgo(r.createdAt)}</div></div>
      <div class="row-actions">${badge(r.status)}<button class="btn btn-sm" data-action="view-request" data-id="${r.id}">Review</button></div></div>` })),
    ...q.pos.map((p) => ({ at: p.createdAt, html: `
      <div class="list-item"><div class="grow"><div><span class="mono">${p.number}</span> · ${esc(p.vendor)} · <strong>${money(poTotal(p))}</strong></div>
      <div class="meta">${esc(nameOf(p.requesterId))} · ${timeAgo(p.createdAt)}</div></div>
      <div class="row-actions">${badge(p.status)}<button class="btn btn-sm" data-action="view-po" data-id="${p.id}">Review</button></div></div>` })),
  ].sort((a, b) => b.at.localeCompare(a.at));
  return rows.slice(0, limit).map((r) => r.html).join('');
}

/* ============================================================
   Approvals inbox (manager / admin)
   ============================================================ */
function viewApprovals(u) {
  const q = myQueue(u);
  return `
    <div class="page-head"><div><h1>Approvals</h1><p>${q.total} item${q.total === 1 ? '' : 's'} waiting on you.</p></div></div>
    <div class="card"><div class="list">${queueList(q) || '<div class="empty"><div class="big">✓</div>Inbox zero. Nice.</div>'}</div></div>`;
}

/* ============================================================
   Assets
   ============================================================ */
const emptyFilters = () => ({ assets: { q: '', status: '', category: '' }, requests: { status: '' }, pos: { status: '' } });
let filters = emptyFilters();

function viewAssets(u) {
  const f = filters.assets;
  const rows = visibleAssets(u).filter((a) =>
    (!f.status || a.status === f.status) &&
    (!f.category || a.category === f.category) &&
    (!f.q || `${a.tag} ${a.name} ${a.serial} ${nameOf(a.assignedTo)}`.toLowerCase().includes(f.q.toLowerCase())));
  const admin = can('manage:assets');
  const title = u.role === 'admin' ? 'Assets' : u.role === 'manager' ? 'Team assets' : 'My assets';

  return `
    <div class="page-head">
      <div><h1>${title}</h1><p>${rows.length} of ${visibleAssets(u).length} shown</p></div>
      ${admin ? '<button class="btn btn-primary" data-action="new-asset">+ Add asset</button>' : '<button class="btn btn-primary" data-action="new-request">+ Request equipment</button>'}
    </div>
    <div class="toolbar">
      <input type="search" placeholder="Search tag, name, serial, owner…" value="${esc(f.q)}" data-filter="assets.q">
      <select data-filter="assets.category"><option value="">All categories</option>${CATEGORIES.map((c) => `<option ${c === f.category ? 'selected' : ''}>${c}</option>`).join('')}</select>
      <select data-filter="assets.status"><option value="">All statuses</option>${ASSET_STATUS.map((s) => `<option ${s === f.status ? 'selected' : ''}>${s}</option>`).join('')}</select>
    </div>
    <div class="card table-wrap">
      ${rows.length ? `<table><thead><tr><th>Asset</th><th>Category</th><th>Status</th><th>Assigned to</th><th>Location</th><th>Cost</th>${admin ? '<th></th>' : ''}</tr></thead><tbody>
        ${rows.map((a) => `<tr>
          <td><div><strong>${esc(a.name)}</strong></div><div class="sub mono">${esc(a.tag)} · SN ${esc(a.serial)}</div></td>
          <td>${esc(a.category)}</td><td>${badge(a.status)}</td>
          <td>${a.assignedTo ? esc(nameOf(a.assignedTo)) : '<span class="sub">—</span>'}</td>
          <td>${esc(a.location)}</td><td>${money(a.cost)}</td>
          ${admin ? `<td class="actions"><button class="btn btn-sm" data-action="edit-asset" data-id="${a.id}">Edit</button></td>` : ''}
        </tr>`).join('')}</tbody></table>` : '<div class="empty"><div class="big">📦</div>No assets match.</div>'}
    </div>`;
}

function assetForm(a = {}) {
  const users = S().users;
  return `<form class="form" id="asset-form">
    <div class="row"><label>Name<input name="name" required value="${esc(a.name)}"></label>
      <label>Category<select name="category">${CATEGORIES.map((c) => `<option ${c === a.category ? 'selected' : ''}>${c}</option>`).join('')}</select></label></div>
    <div class="row"><label>Serial number<input name="serial" value="${esc(a.serial)}"></label>
      <label>Location<input name="location" value="${esc(a.location || 'IT Store')}"></label></div>
    <div class="row"><label>Cost (EUR)<input name="cost" type="number" min="0" step="1" value="${a.cost ?? ''}"></label>
      <label>Purchase date<input name="purchaseDate" type="date" value="${esc(a.purchaseDate || new Date().toISOString().slice(0, 10))}"></label></div>
    <div class="row"><label>Status<select name="status">${ASSET_STATUS.map((s) => `<option ${s === (a.status || 'Available') ? 'selected' : ''}>${s}</option>`).join('')}</select></label>
      <label>Assigned to<select name="assignedTo"><option value="">— Unassigned —</option>${users.map((u) => `<option value="${u.id}" ${u.id === a.assignedTo ? 'selected' : ''}>${esc(u.name)}</option>`).join('')}</select></label></div>
  </form>`;
}

/* ============================================================
   Requests
   ============================================================ */
function viewRequests(u) {
  const f = filters.requests;
  const all = visibleRequests(u).slice().sort((a, b) => b.createdAt.localeCompare(a.createdAt));
  const rows = all.filter((r) => !f.status || r.status === f.status);
  return `
    <div class="page-head">
      <div><h1>Asset requests</h1><p>${u.role === 'user' ? 'Your equipment requests.' : u.role === 'manager' ? 'Requests from you and your team.' : 'All requests across the company.'}</p></div>
      <button class="btn btn-primary" data-action="new-request">+ New request</button>
    </div>
    <div class="toolbar"><select data-filter="requests.status"><option value="">All statuses</option>${REQUEST_STATUS.map((s) => `<option ${s === f.status ? 'selected' : ''}>${s}</option>`).join('')}</select></div>
    <div class="card table-wrap">
      ${rows.length ? `<table><thead><tr><th>Request</th><th>Requester</th><th>Category</th><th>Priority</th><th>Status</th><th>Submitted</th><th></th></tr></thead><tbody>
        ${rows.map((r) => `<tr>
          <td><div><strong>${esc(r.item)}</strong></div><div class="sub mono">${r.number}</div></td>
          <td>${esc(nameOf(r.requesterId))}</td><td>${esc(r.category)}</td>
          <td><span class="badge ${{ High: 'b-bad', Normal: 'b-info', Low: 'b-muted' }[r.priority] || 'b-muted'}">${esc(r.priority)}</span></td>
          <td>${badge(r.status)}</td><td>${timeAgo(r.createdAt)}</td>
          <td class="actions"><button class="btn btn-sm" data-action="view-request" data-id="${r.id}">Open</button></td>
        </tr>`).join('')}</tbody></table>` : '<div class="empty"><div class="big">📝</div>No requests yet.</div>'}
    </div>`;
}

function requestForm() {
  return `<form class="form" id="request-form">
    <div class="row"><label>Category<select name="category">${CATEGORIES.map((c) => `<option>${c}</option>`).join('')}</select></label>
      <label>Priority<select name="priority"><option>Low</option><option selected>Normal</option><option>High</option></select></label></div>
    <label>What do you need?<input name="item" required placeholder='e.g. 27" 4K monitor'></label>
    <label>Reason<textarea name="reason" required placeholder="Why is this needed?"></textarea></label>
  </form>`;
}

/* ============================================================
   Purchase orders
   ============================================================ */
function viewPOs(u) {
  const f = filters.pos;
  const all = visiblePOs(u).slice().sort((a, b) => b.createdAt.localeCompare(a.createdAt));
  const rows = all.filter((p) => !f.status || p.status === f.status);
  return `
    <div class="page-head">
      <div><h1>Purchase orders</h1><p>POs over ${money(PO_ADMIN_THRESHOLD)} need manager <em>and</em> admin approval.</p></div>
      <button class="btn btn-primary" data-action="new-po">+ Fill purchase order</button>
    </div>
    <div class="toolbar"><select data-filter="pos.status"><option value="">All statuses</option>${PO_STATUS.map((s) => `<option ${s === f.status ? 'selected' : ''}>${s}</option>`).join('')}</select></div>
    <div class="card table-wrap">
      ${rows.length ? `<table><thead><tr><th>PO</th><th>Requester</th><th>Cost center</th><th>Needed by</th><th>Total</th><th>Status</th><th></th></tr></thead><tbody>
        ${rows.map((p) => `<tr>
          <td><div><strong>${esc(p.vendor)}</strong></div><div class="sub mono">${p.number} · ${p.items.length} line${p.items.length === 1 ? '' : 's'}</div></td>
          <td>${esc(nameOf(p.requesterId))}</td><td class="mono">${esc(p.costCenter)}</td><td>${esc(p.neededBy)}</td>
          <td><strong>${money(poTotal(p))}</strong></td><td>${badge(p.status)}</td>
          <td class="actions"><button class="btn btn-sm" data-action="view-po" data-id="${p.id}">Open</button></td>
        </tr>`).join('')}</tbody></table>` : '<div class="empty"><div class="big">🧾</div>No purchase orders yet.</div>'}
    </div>`;
}

function poLine(l = { desc: '', qty: 1, unitPrice: '' }) {
  return `<div class="line">
    <input name="desc" placeholder="Item description" required value="${esc(l.desc)}">
    <input name="qty" type="number" min="1" step="1" value="${l.qty}" required>
    <input name="unitPrice" type="number" min="0" step="0.01" placeholder="Unit price" value="${l.unitPrice}" required>
    <div class="total">${money(l.qty * (l.unitPrice || 0))}</div>
    <button type="button" class="btn btn-ghost btn-sm" data-action="remove-line" aria-label="Remove line">✕</button>
  </div>`;
}

function poForm() {
  const u = me();
  return `<form class="form" id="po-form">
    <div class="row"><label>Vendor<input name="vendor" required placeholder="e.g. Dell Technologies"></label>
      <label>Category<select name="category">${CATEGORIES.map((c) => `<option>${c}</option>`).join('')}</select></label></div>
    <div class="row"><label>Cost center<input name="costCenter" required value="${esc(u.dept.slice(0, 3).toUpperCase())}-100"></label>
      <label>Needed by<input name="neededBy" type="date" required value="${new Date(Date.now() + 14 * 864e5).toISOString().slice(0, 10)}"></label></div>
    <div>
      <div class="line line-head"><span>Description</span><span>Qty</span><span>Unit price</span><span class="total">Line total</span><span></span></div>
      <div class="lines" id="po-lines">${poLine()}</div>
      <button type="button" class="btn btn-sm" style="margin-top:10px" data-action="add-line">+ Add line</button>
    </div>
    <label>Justification<textarea name="justification" required placeholder="Business reason for this purchase"></textarea></label>
    <div class="po-total"><div><div>Total</div><div class="hint" id="po-route"></div></div><strong id="po-total">${money(0)}</strong></div>
  </form>`;
}

function updatePoTotal() {
  const lines = [...document.querySelectorAll('#po-lines .line')];
  let total = 0;
  lines.forEach((ln) => {
    const qty = Number(ln.querySelector('[name=qty]').value) || 0;
    const price = Number(ln.querySelector('[name=unitPrice]').value) || 0;
    total += qty * price;
    ln.querySelector('.total').textContent = money(qty * price);
  });
  document.getElementById('po-total').textContent = money(total);
  const route = document.getElementById('po-route');
  const u = me();
  const steps = [];
  if (u.role === 'user') steps.push('Manager');
  if (total > PO_ADMIN_THRESHOLD && u.role !== 'admin') steps.push('Admin');
  route.textContent = steps.length ? `Approval route: ${steps.join(' → ')}` : 'Auto-approved on submit';
}

/* ============================================================
   Users (admin)
   ============================================================ */
function viewUsers() {
  const users = S().users;
  return `
    <div class="page-head"><div><h1>Users</h1><p>${users.length} people · roles control what each person can see and approve.</p></div>
      <button class="btn btn-primary" data-action="new-user">+ Add user</button></div>
    <div class="card table-wrap"><table><thead><tr><th>Name</th><th>Department</th><th>Role</th><th>Manager</th><th>Assets</th><th>Status</th><th></th></tr></thead><tbody>
      ${users.map((u) => `<tr>
        <td><div style="display:flex;gap:10px;align-items:center"><div class="avatar">${initials(u.name)}</div><div><strong>${esc(u.name)}</strong><div class="sub">${esc(u.email)}</div></div></div></td>
        <td>${esc(u.dept)}</td><td><span class="role-pill">${ROLES[u.role].label}</span></td>
        <td>${esc(nameOf(u.managerId))}</td><td>${S().assets.filter((a) => a.assignedTo === u.id).length}</td>
        <td><span class="badge ${u.active === false ? 'b-bad' : 'b-ok'}">${u.active === false ? 'Disabled' : 'Active'}</span></td>
        <td class="actions"><button class="btn btn-sm" data-action="edit-user" data-id="${u.id}">Edit</button></td>
      </tr>`).join('')}</tbody></table></div>
    <div class="section-title" style="margin-top:56px;text-align:left"><h2 style="font-size:24px">Permission matrix</h2></div>
    <div class="card table-wrap"><table><thead><tr><th>Permission</th>${Object.values(ROLES).map((r) => `<th>${r.label}</th>`).join('')}</tr></thead><tbody>
      ${[...new Set(Object.values(ROLES).flatMap((r) => r.can))].map((p) => `<tr><td class="mono">${p}</td>${Object.values(ROLES).map((r) => `<td>${r.can.includes(p) ? '<span style="color:var(--accent)">●</span>' : '<span class="sub">—</span>'}</td>`).join('')}</tr>`).join('')}
    </tbody></table></div>`;
}

function userForm(u = {}) {
  const managers = S().users.filter((x) => x.role !== 'user' && x.id !== u.id);
  return `<form class="form" id="user-form">
    <div class="row"><label>Full name<input name="name" required value="${esc(u.name)}"></label>
      <label>Email<input name="email" type="email" required value="${esc(u.email)}"></label></div>
    <div class="row"><label>Department<input name="dept" required value="${esc(u.dept)}"></label>
      <label>Role<select name="role">${Object.entries(ROLES).map(([k, r]) => `<option value="${k}" ${k === u.role ? 'selected' : ''}>${r.label}</option>`).join('')}</select></label></div>
    <div class="row"><label>${u.id ? 'New password' : 'Password'}<input name="password" type="password" autocomplete="new-password" ${u.id ? '' : 'required'} minlength="${MIN_PASSWORD}" placeholder="${u.id ? 'Leave blank to keep current' : `At least ${MIN_PASSWORD} characters`}"></label>
      <label>Status<select name="active"><option value="true" ${u.active !== false ? 'selected' : ''}>Active</option><option value="false" ${u.active === false ? 'selected' : ''}>Disabled (can't sign in)</option></select></label></div>
    <label>Reports to<select name="managerId"><option value="">— None —</option>${managers.map((m) => `<option value="${m.id}" ${m.id === u.managerId ? 'selected' : ''}>${esc(m.name)} (${ROLES[m.role].label})</option>`).join('')}</select></label>
  </form>`;
}

/* ============================================================
   Modal + toast
   ============================================================ */
function openModal(title, body, foot = '') {
  closeModal();
  const el = document.createElement('div');
  el.className = 'modal-backdrop';
  el.innerHTML = `<div class="modal" role="dialog" aria-modal="true" aria-label="${esc(title)}">
    <div class="card-head"><h3>${esc(title)}</h3><button class="btn btn-ghost btn-sm" data-action="close-modal" aria-label="Close">✕</button></div>
    <div class="card-body">${body}</div>${foot ? `<div class="modal-foot">${foot}</div>` : ''}</div>`;
  el.addEventListener('mousedown', (e) => { if (e.target === el) closeModal(); });
  document.body.appendChild(el);
  el.querySelector('input, select, textarea')?.focus();
  return el;
}
function closeModal() { document.querySelector('.modal-backdrop')?.remove(); }

function toast(msg) {
  document.querySelector('.toast')?.remove();
  const t = document.createElement('div');
  t.className = 'toast';
  t.textContent = msg;
  document.body.appendChild(t);
  setTimeout(() => t.remove(), 2600);
}

function formData(id) {
  const form = document.getElementById(id);
  if (!form.reportValidity()) return null;
  return Object.fromEntries(new FormData(form).entries());
}

/* ---------- Detail modals ---------- */
function showRequest(id) {
  const r = S().requests.find((x) => x.id === id);
  const u = me();
  const action = Rules.requestAction(S(), u, r);
  let foot = '';
  let extra = '';
  if (action === 'decide') {
    extra = '<label class="form" style="margin-top:16px"><span style="font-size:13px;color:var(--muted)">Note (optional)</span><input class="input" id="decision-note" placeholder="Add a comment for the requester"></label>';
    foot = `<button class="btn btn-bad" data-action="reject-request" data-id="${r.id}">Reject</button><button class="btn btn-ok" data-action="approve-request" data-id="${r.id}">Approve</button>`;
  } else if (action === 'fulfil') {
    const stock = S().assets.filter((a) => a.status === 'Available' && a.category === r.category);
    extra = stock.length
      ? `<label class="form" style="margin-top:16px"><span style="font-size:13px;color:var(--muted)">Assign from stock</span><select class="input" id="fulfil-asset">${stock.map((a) => `<option value="${a.id}">${esc(a.tag)} · ${esc(a.name)}</option>`).join('')}</select></label>`
      : `<p class="hint" style="margin-top:16px">No available ${esc(r.category)} in stock — raise a purchase order first.</p>`;
    foot = stock.length ? `<button class="btn btn-primary" data-action="fulfil-request" data-id="${r.id}">Assign & fulfil</button>` : '<button class="btn" data-action="new-po">Create PO</button>';
  }
  openModal(`${r.number} · ${r.item}`, `
    <dl class="kv"><dt>Status</dt><dd>${badge(r.status)}</dd><dt>Requester</dt><dd>${esc(nameOf(r.requesterId))}</dd>
      <dt>Category</dt><dd>${esc(r.category)}</dd><dt>Priority</dt><dd>${esc(r.priority)}</dd><dt>Reason</dt><dd>${esc(r.reason)}</dd></dl>
    <h4 style="margin:0 0 12px">History</h4>${timeline(r.history)}${extra}`, foot);
}

function showPO(id) {
  const p = S().purchaseOrders.find((x) => x.id === id);
  const acts = Rules.poActions(S(), me(), p);
  const label = { approve: ['btn-ok', 'Approve'], reject: ['btn-bad', 'Reject'], order: ['btn-primary', 'Mark as ordered'], receive: ['btn-primary', 'Receive into inventory'] };
  const foot = acts.map((a) => `<button class="btn ${label[a][0]}" data-action="po-${a}" data-id="${p.id}">${label[a][1]}</button>`).join('');
  const note = acts.includes('approve') ? '<label class="form" style="margin-top:16px"><span style="font-size:13px;color:var(--muted)">Note (optional)</span><input class="input" id="decision-note" placeholder="Add a comment"></label>' : '';
  openModal(`${p.number} · ${p.vendor}`, `
    <dl class="kv"><dt>Status</dt><dd>${badge(p.status)}</dd><dt>Requester</dt><dd>${esc(nameOf(p.requesterId))}</dd>
      <dt>Category</dt><dd>${esc(p.category || '—')}</dd><dt>Cost center</dt><dd class="mono">${esc(p.costCenter)}</dd><dt>Needed by</dt><dd>${esc(p.neededBy)}</dd>
      <dt>Justification</dt><dd>${esc(p.justification)}</dd></dl>
    <div class="card table-wrap" style="margin-bottom:20px"><table><thead><tr><th>Item</th><th>Qty</th><th>Unit</th><th style="text-align:right">Total</th></tr></thead><tbody>
      ${p.items.map((i) => `<tr><td>${esc(i.desc)}</td><td>${i.qty}</td><td>${money(i.unitPrice)}</td><td style="text-align:right">${money(i.qty * i.unitPrice)}</td></tr>`).join('')}
      <tr><td colspan="3"><strong>Total</strong></td><td style="text-align:right"><strong>${money(poTotal(p))}</strong></td></tr></tbody></table></div>
    <h4 style="margin:0 0 12px">History</h4>${timeline(p.history)}${note}`, foot);
}

const timeline = (h) => `<ul class="timeline">${h.map((e) => `<li><div><strong>${esc(e.action)}</strong> by ${esc(nameOf(e.by))} <span class="hint">· ${timeAgo(e.at)}</span>${e.note ? `<div class="hint">“${esc(e.note)}”</div>` : ''}</div></li>`).join('')}</ul>`;

const noteValue = () => document.getElementById('decision-note')?.value.trim() || undefined;

/* ============================================================
   Actions
   ============================================================ */
const showError = (id, msg) => { const e = document.getElementById(id); e.textContent = msg; e.hidden = false; };

function signedIn(state, message) {
  STATE = state;
  needsSetup = false;
  filters = emptyFilters();
  location.hash = '#dashboard';
  route();
  if (message) toast(message);
}

const ACTIONS = {
  async login() {
    const email = document.getElementById('login-email').value.trim();
    const password = document.getElementById('login-password').value;
    if (!email || !password) return showError('login-error', 'Enter your email and password.');
    try {
      const d = await api('/api/login', { email, password });
      signedIn(d.state, `Signed in as ${d.state.me.name}`);
    } catch (e) { showError('login-error', e.message); }
  },
  async setup() {
    const body = {
      name: document.getElementById('setup-name').value,
      email: document.getElementById('setup-email').value,
      dept: document.getElementById('setup-dept').value,
      password: document.getElementById('setup-password').value,
    };
    try {
      const d = await api('/api/setup', body);
      signedIn(d.state, d.message);
    } catch (e) { showError('setup-error', e.message); }
  },
  async 'change-password'() {
    const current = document.getElementById('pw-current').value;
    const next = document.getElementById('pw-new').value;
    if (next.length < MIN_PASSWORD) return showError('pw-error', `Use at least ${MIN_PASSWORD} characters for the new password.`);
    if (next !== document.getElementById('pw-confirm').value) return showError('pw-error', 'The new passwords don\'t match.');
    try {
      const d = await api('/api/actions/changePassword', { current, next });
      STATE = d.state;
      route();
      toast(d.message);
    } catch (e) { showError('pw-error', e.message); }
  },
  async logout() {
    try { await api('/api/logout', {}); } catch { /* signed out locally anyway */ }
    STATE = null;
    filters = emptyFilters();
    location.hash = '';
    route();
    toast('Signed out');
  },
  'close-modal': closeModal,

  /* Requests */
  'new-request'() {
    openModal('Request equipment', requestForm(), '<button class="btn" data-action="close-modal">Cancel</button><button class="btn btn-primary" data-action="submit-request">Submit request</button>');
  },
  'submit-request'() {
    const d = formData('request-form'); if (!d) return;
    return act('submitRequest', d);
  },
  'view-request'(el) { showRequest(el.dataset.id); },
  'approve-request'(el) { return act('decideRequest', { id: el.dataset.id, decision: 'approve', note: noteValue() }); },
  'reject-request'(el) { return act('decideRequest', { id: el.dataset.id, decision: 'reject', note: noteValue() }); },
  'fulfil-request'(el) { return act('fulfilRequest', { id: el.dataset.id, assetId: document.getElementById('fulfil-asset').value }); },

  /* Purchase orders */
  'new-po'() {
    openModal('Fill purchase order', poForm(), '<button class="btn" data-action="close-modal">Cancel</button><button class="btn btn-primary" data-action="submit-po">Submit PO</button>');
    updatePoTotal();
  },
  'add-line'() {
    document.getElementById('po-lines').insertAdjacentHTML('beforeend', poLine());
    updatePoTotal();
  },
  'remove-line'(el) {
    if (document.querySelectorAll('#po-lines .line').length > 1) el.closest('.line').remove();
    updatePoTotal();
  },
  'submit-po'() {
    const d = formData('po-form'); if (!d) return;
    const body = {
      vendor: d.vendor, category: d.category, costCenter: d.costCenter, neededBy: d.neededBy, justification: d.justification,
      items: [...document.querySelectorAll('#po-lines .line')].map((ln) => ({
        desc: ln.querySelector('[name=desc]').value.trim(),
        qty: Number(ln.querySelector('[name=qty]').value),
        unitPrice: Number(ln.querySelector('[name=unitPrice]').value),
      })),
    };
    return act('submitPO', body);
  },
  'view-po'(el) { showPO(el.dataset.id); },
  'po-approve'(el) { return act('poApprove', { id: el.dataset.id, note: noteValue() }); },
  'po-reject'(el) { return act('poReject', { id: el.dataset.id, note: noteValue() }); },
  'po-order'(el) { return act('poOrder', { id: el.dataset.id }); },
  'po-receive'(el) { return act('poReceive', { id: el.dataset.id }); },

  /* Assets (admin) */
  'new-asset'() {
    openModal('Add asset', assetForm(), '<button class="btn" data-action="close-modal">Cancel</button><button class="btn btn-primary" data-action="save-asset">Add asset</button>');
  },
  'edit-asset'(el) {
    const a = S().assets.find((x) => x.id === el.dataset.id);
    openModal(`Edit ${a.tag}`, assetForm(a), `<button class="btn" data-action="close-modal">Cancel</button><button class="btn btn-primary" data-action="save-asset" data-id="${a.id}">Save changes</button>`);
  },
  'save-asset'(el) {
    const d = formData('asset-form'); if (!d) return;
    return act('saveAsset', { ...d, id: el.dataset.id || undefined });
  },

  /* Users (admin) */
  'new-user'() {
    openModal('Add user', userForm({ role: 'user' }), '<button class="btn" data-action="close-modal">Cancel</button><button class="btn btn-primary" data-action="save-user">Add user</button>');
  },
  'edit-user'(el) {
    const u = userById(el.dataset.id);
    openModal(`Edit ${u.name}`, userForm(u), `<button class="btn" data-action="close-modal">Cancel</button><button class="btn btn-primary" data-action="save-user" data-id="${u.id}">Save changes</button>`);
  },
  'save-user'(el) {
    const d = formData('user-form'); if (!d) return;
    return act('saveUser', { ...d, id: el.dataset.id || undefined });
  },
};

/* ============================================================
   Wiring
   ============================================================ */
document.addEventListener('click', (e) => {
  const el = e.target.closest('[data-action]');
  if (!el) return;
  const fn = ACTIONS[el.dataset.action];
  if (!fn || el.disabled) return;
  e.preventDefault();
  const pending = fn(el);
  if (pending && typeof pending.then === 'function' && el.tagName === 'BUTTON') {
    el.disabled = true;
    pending.finally(() => { el.disabled = false; });
  }
});

document.addEventListener('input', (e) => {
  if (e.target.closest('#po-lines')) updatePoTotal();
  const key = e.target.dataset.filter;
  if (key) {
    const [group, field] = key.split('.');
    filters[group][field] = e.target.value;
    const pos = e.target.selectionStart;
    route(false);
    const again = document.querySelector(`[data-filter="${key}"]`);
    again?.focus();
    if (pos != null && again?.setSelectionRange) again.setSelectionRange(pos, pos);
  }
});

document.addEventListener('submit', (e) => {
  e.preventDefault();
  if (e.target.id === 'login-form') ACTIONS.login();
  if (e.target.id === 'password-form') ACTIONS['change-password']();
  if (e.target.id === 'setup-form') ACTIONS.setup();
});

document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeModal(); });
window.addEventListener('hashchange', route);

/* Boot: ask the server who is signed in. */
(async () => {
  try {
    const d = await api('/api/session');
    needsSetup = d.needsSetup;
    STATE = d.state;
  } catch (e) {
    toast(e.message);
  }
  route();
})();
