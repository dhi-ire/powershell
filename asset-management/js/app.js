/* ============================================================
   App — routing, views, and workflow actions.
   ============================================================ */

const S = () => Store.state;
const me = () => S().users.find((u) => u.id === S().currentUserId) || null;
const can = (perm) => !!me() && ROLES[me().role].can.includes(perm);
const userById = (id) => S().users.find((u) => u.id === id);
const nameOf = (id) => userById(id)?.name || '—';
const initials = (name) => name.split(' ').map((p) => p[0]).slice(0, 2).join('').toUpperCase();
const teamIds = (u) => S().users.filter((x) => x.managerId === u.id).map((x) => x.id);

/* ---------- Scoping: what each role can see ---------- */
function scopeIds(u) {
  if (u.role === 'admin') return null;               // null = everything
  if (u.role === 'manager') return [u.id, ...teamIds(u)];
  return [u.id];
}
function visibleAssets(u) {
  const ids = scopeIds(u);
  return ids ? S().assets.filter((a) => ids.includes(a.assignedTo)) : S().assets;
}
function visibleRequests(u) {
  const ids = scopeIds(u);
  return ids ? S().requests.filter((r) => ids.includes(r.requesterId)) : S().requests;
}
function visiblePOs(u) {
  const ids = scopeIds(u);
  return ids ? S().purchaseOrders.filter((p) => ids.includes(p.requesterId)) : S().purchaseOrders;
}

/* Items waiting on the current user. */
function myQueue(u) {
  const team = teamIds(u);
  const reqs = [];
  const pos = [];
  if (u.role === 'manager') {
    reqs.push(...S().requests.filter((r) => r.status === 'Pending' && team.includes(r.requesterId)));
    pos.push(...S().purchaseOrders.filter((p) => p.status === 'Pending Manager' && team.includes(p.requesterId)));
  }
  if (u.role === 'admin') {
    reqs.push(...S().requests.filter((r) => r.status === 'Pending' || r.status === 'Approved'));
    pos.push(...S().purchaseOrders.filter((p) => ['Pending Manager', 'Pending Admin', 'Approved', 'Ordered'].includes(p.status)));
  }
  return { reqs, pos, total: reqs.length + pos.length };
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

const VIEWS = { dashboard: viewDashboard, approvals: viewApprovals, assets: viewAssets, requests: viewRequests, pos: viewPOs, users: viewUsers };

function route(scroll = true) {
  const u = me();
  const page = (location.hash.replace(/^#\/?/, '') || 'dashboard').split('?')[0];
  const root = document.getElementById('app');
  if (!u) { root.innerHTML = viewLanding(); return; }
  const allowed = NAV[u.role].map(([k]) => k);
  const key = allowed.includes(page) ? page : 'dashboard';
  root.innerHTML = shell(key, VIEWS[key](u));
  if (scroll) window.scrollTo(0, 0);
}

function shell(active, content) {
  const u = me();
  const q = myQueue(u).total;
  const links = NAV[u.role].map(([k, label]) =>
    `<a href="#/${k}" class="${k === active ? 'active' : ''}">${label}${k === 'approvals' && q ? `<span class="count">${q}</span>` : ''}</a>`).join('');
  return `
    <header class="nav"><div class="container nav-inner">
      <a class="logo" href="#/dashboard"><span class="logo-mark">A</span>AssetFlow</a>
      <nav class="nav-links">${links}</nav>
      <div class="nav-user">
        <span class="role-pill">${ROLES[u.role].label}</span>
        <span class="name" style="font-size:14px">${esc(u.name)}</span>
        <div class="avatar" title="${esc(u.email)}">${initials(u.name)}</div>
        <button class="btn btn-ghost btn-sm" data-action="logout">Switch</button>
      </div>
    </div></header>
    <main><div class="container">${content}</div></main>`;
}

/* ============================================================
   Landing — role picker, styled like a download page
   ============================================================ */
function viewLanding() {
  const roleCard = (role, title, desc, bullets, featured) => {
    const people = S().users.filter((u) => u.role === role);
    return `
      <div class="role-card ${featured ? 'featured' : ''}">
        <div class="role-icon">${ICON[role]}</div>
        <h3>${title}</h3>
        <p>${desc}</p>
        <ul>${bullets.map((b) => `<li>${b}</li>`).join('')}</ul>
        <select id="pick-${role}" aria-label="Choose ${title} account">
          ${people.map((p) => `<option value="${p.id}">${esc(p.name)} · ${esc(p.dept)}</option>`).join('')}
        </select>
        <button class="btn ${featured ? 'btn-primary' : ''} btn-lg" data-action="login" data-role="${role}">Continue as ${title} →</button>
      </div>`;
  };
  return `
    <header class="nav"><div class="container nav-inner">
      <a class="logo" href="#/"><span class="logo-mark">A</span>AssetFlow</a>
      <nav class="nav-links"></nav>
      <button class="btn btn-ghost btn-sm" data-action="reset">Reset demo data</button>
    </div></header>
    <main><div class="container">
      <section class="hero">
        <span class="eyebrow"><span class="dot"></span>Assets · Requests · Purchase orders</span>
        <h1>Every asset tracked.<br><em>Every approval</em> in one click.</h1>
        <p class="lead">Request equipment, raise purchase orders and route them through approvals — with a clear view for users, managers and admins.</p>
      </section>

      <section class="role-grid">
        ${roleCard('user', 'User', 'For employees who need equipment.', ['See assets assigned to you', 'Request new equipment', 'Fill purchase orders', 'Track approval status'], true)}
        ${roleCard('manager', 'Manager', 'For team leads who approve spend.', ['Approve team requests', `Approve POs (up to ${money(PO_ADMIN_THRESHOLD)})`, 'View team assets', 'Raise your own POs'], false)}
        ${roleCard('admin', 'Admin', 'For IT / procurement owners.', ['Manage full inventory', `Final approval over ${money(PO_ADMIN_THRESHOLD)}`, 'Fulfil requests & receive POs', 'Manage users and roles'], false)}
      </section>
      <p class="fine">Demo mode — data is stored in your browser. Pick any account to explore.</p>

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
    : `<div class="card"><div class="card-head"><h3>Waiting on you</h3><a class="btn btn-sm" href="#/approvals">Open inbox</a></div>
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
const filters = { assets: { q: '', status: '', category: '' }, requests: { status: '' }, pos: { status: '' } };

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

function canActOnRequest(u, r) {
  if (r.status === 'Pending') return u.role === 'admin' || (u.role === 'manager' && teamIds(u).includes(r.requesterId));
  if (r.status === 'Approved') return can('fulfill:request');
  return false;
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

/* Initial status for a new PO based on who submits it and how much it costs. */
function initialPoStatus(u, total) {
  if (u.role === 'user') return 'Pending Manager';
  if (u.role === 'manager') return total > PO_ADMIN_THRESHOLD ? 'Pending Admin' : 'Approved';
  return 'Approved';
}

function poActions(u, p) {
  const team = teamIds(u);
  const acts = [];
  if (p.status === 'Pending Manager' && (u.role === 'admin' || (u.role === 'manager' && team.includes(p.requesterId)))) acts.push('approve', 'reject');
  if (p.status === 'Pending Admin' && can('approve:po-admin')) acts.push('approve', 'reject');
  if (p.status === 'Approved' && can('order:po')) acts.push('order');
  if (p.status === 'Ordered' && can('receive:po')) acts.push('receive');
  return acts;
}

/* ============================================================
   Users (admin)
   ============================================================ */
function viewUsers() {
  const users = S().users;
  return `
    <div class="page-head"><div><h1>Users</h1><p>${users.length} people · roles control what each person can see and approve.</p></div>
      <button class="btn btn-primary" data-action="new-user">+ Add user</button></div>
    <div class="card table-wrap"><table><thead><tr><th>Name</th><th>Department</th><th>Role</th><th>Manager</th><th>Assets</th><th></th></tr></thead><tbody>
      ${users.map((u) => `<tr>
        <td><div style="display:flex;gap:10px;align-items:center"><div class="avatar">${initials(u.name)}</div><div><strong>${esc(u.name)}</strong><div class="sub">${esc(u.email)}</div></div></div></td>
        <td>${esc(u.dept)}</td><td><span class="role-pill">${ROLES[u.role].label}</span></td>
        <td>${esc(nameOf(u.managerId))}</td><td>${S().assets.filter((a) => a.assignedTo === u.id).length}</td>
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

function commit(msg) {
  Store.save();
  closeModal();
  route();
  if (msg) toast(msg);
}

/* ---------- Detail modals ---------- */
function showRequest(id) {
  const r = S().requests.find((x) => x.id === id);
  const u = me();
  const actionable = canActOnRequest(u, r);
  let foot = '';
  let extra = '';
  if (actionable && r.status === 'Pending') {
    extra = '<label class="form" style="margin-top:16px"><span style="font-size:13px;color:var(--muted)">Note (optional)</span><input class="input" id="decision-note" placeholder="Add a comment for the requester"></label>';
    foot = `<button class="btn btn-bad" data-action="reject-request" data-id="${r.id}">Reject</button><button class="btn btn-ok" data-action="approve-request" data-id="${r.id}">Approve</button>`;
  } else if (actionable && r.status === 'Approved') {
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
  const acts = poActions(me(), p);
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
const ACTIONS = {
  login(el) {
    S().currentUserId = document.getElementById(`pick-${el.dataset.role}`).value;
    Store.save();
    location.hash = '#/dashboard';
    route();
  },
  logout() { S().currentUserId = null; Store.save(); location.hash = '#/'; route(); },
  reset() { if (confirm('Reset all demo data?')) { Store.reset(); route(); toast('Demo data reset'); } },
  'close-modal': closeModal,

  /* Requests */
  'new-request'() {
    openModal('Request equipment', requestForm(), '<button class="btn" data-action="close-modal">Cancel</button><button class="btn btn-primary" data-action="submit-request">Submit request</button>');
  },
  'submit-request'() {
    const d = formData('request-form'); if (!d) return;
    const u = me();
    const auto = u.role !== 'user';   // managers/admins skip manager review
    S().requests.push({
      id: uid(), number: Store.nextId('request', 'REQ'), requesterId: u.id, ...d,
      status: auto ? 'Approved' : 'Pending', createdAt: now(),
      history: [{ by: u.id, action: 'Submitted', at: now() }, ...(auto ? [{ by: u.id, action: 'Auto-approved', at: now() }] : [])],
    });
    commit(auto ? 'Request submitted and approved' : 'Request sent to your manager');
  },
  'view-request'(el) { showRequest(el.dataset.id); },
  'approve-request'(el) { decideRequest(el.dataset.id, 'Approved'); },
  'reject-request'(el) { decideRequest(el.dataset.id, 'Rejected'); },
  'fulfil-request'(el) {
    const r = S().requests.find((x) => x.id === el.dataset.id);
    const a = S().assets.find((x) => x.id === document.getElementById('fulfil-asset').value);
    Object.assign(a, { status: 'Assigned', assignedTo: r.requesterId });
    r.status = 'Fulfilled';
    r.history.push({ by: me().id, action: 'Fulfilled', at: now(), note: `Assigned ${a.tag}` });
    commit(`${a.tag} assigned to ${nameOf(r.requesterId)}`);
  },

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
    const lines = document.querySelectorAll('#po-lines .line');
    if (lines.length > 1) el.closest('.line').remove();
    updatePoTotal();
  },
  'submit-po'() {
    const d = formData('po-form'); if (!d) return;
    const items = [...document.querySelectorAll('#po-lines .line')].map((ln) => ({
      desc: ln.querySelector('[name=desc]').value.trim(),
      qty: Number(ln.querySelector('[name=qty]').value),
      unitPrice: Number(ln.querySelector('[name=unitPrice]').value),
    }));
    const u = me();
    const po = { id: uid(), number: Store.nextId('po', 'PO'), requesterId: u.id, vendor: d.vendor, category: d.category,
      costCenter: d.costCenter, neededBy: d.neededBy, justification: d.justification, items, createdAt: now(),
      history: [{ by: u.id, action: 'Submitted', at: now() }] };
    po.status = initialPoStatus(u, poTotal(po));
    if (u.role !== 'user') po.history.push({ by: u.id, action: po.status === 'Approved' ? 'Auto-approved' : 'Manager approved', at: now() });
    S().purchaseOrders.push(po);
    commit(`${po.number} submitted · ${po.status}`);
  },
  'view-po'(el) { showPO(el.dataset.id); },
  'po-approve'(el) {
    const p = S().purchaseOrders.find((x) => x.id === el.dataset.id);
    const note = noteValue();
    if (p.status === 'Pending Manager') {
      p.status = poTotal(p) > PO_ADMIN_THRESHOLD ? 'Pending Admin' : 'Approved';
      p.history.push({ by: me().id, action: 'Manager approved', at: now(), note });
    } else {
      p.status = 'Approved';
      p.history.push({ by: me().id, action: 'Admin approved', at: now(), note });
    }
    commit(`${p.number} → ${p.status}`);
  },
  'po-reject'(el) {
    const p = S().purchaseOrders.find((x) => x.id === el.dataset.id);
    p.status = 'Rejected';
    p.history.push({ by: me().id, action: 'Rejected', at: now(), note: noteValue() });
    commit(`${p.number} rejected`);
  },
  'po-order'(el) {
    const p = S().purchaseOrders.find((x) => x.id === el.dataset.id);
    p.status = 'Ordered';
    p.history.push({ by: me().id, action: 'Ordered', at: now() });
    commit(`${p.number} marked as ordered`);
  },
  'po-receive'(el) {
    const p = S().purchaseOrders.find((x) => x.id === el.dataset.id);
    let added = 0;
    if (p.category !== 'Software') {
      p.items.forEach((i) => {
        for (let n = 0; n < Math.min(i.qty, 50); n++) {
          S().assets.push({ id: uid(), tag: Store.nextId('asset', 'AST'), name: i.desc, category: p.category || 'Peripheral',
            serial: '', status: 'Available', assignedTo: null, location: 'IT Store', cost: i.unitPrice, purchaseDate: new Date().toISOString().slice(0, 10) });
          added++;
        }
      });
    }
    p.status = 'Received';
    p.history.push({ by: me().id, action: 'Received', at: now(), note: added ? `${added} asset(s) added to inventory` : undefined });
    commit(`${p.number} received · ${added} asset(s) added`);
  },

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
    d.cost = Number(d.cost) || 0;
    d.assignedTo = d.assignedTo || null;
    if (d.assignedTo && d.status === 'Available') d.status = 'Assigned';
    if (!d.assignedTo && d.status === 'Assigned') d.status = 'Available';
    if (el.dataset.id) {
      Object.assign(S().assets.find((x) => x.id === el.dataset.id), d);
      commit('Asset updated');
    } else {
      S().assets.push({ id: uid(), tag: Store.nextId('asset', 'AST'), ...d });
      commit('Asset added');
    }
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
    d.managerId = d.managerId || null;
    if (el.dataset.id) {
      if (el.dataset.id === me().id && d.role !== 'admin' && !confirm('You are removing your own admin role. Continue?')) return;
      Object.assign(userById(el.dataset.id), d);
      commit('User updated');
    } else {
      S().counters.user += 1;
      S().users.push({ id: `u${S().counters.user}`, ...d });
      commit('User added');
    }
  },
};

function decideRequest(id, status) {
  const r = S().requests.find((x) => x.id === id);
  r.status = status;
  r.history.push({ by: me().id, action: status, at: now(), note: noteValue() });
  commit(`${r.number} ${status.toLowerCase()}`);
}

/* ============================================================
   Wiring
   ============================================================ */
document.addEventListener('click', (e) => {
  const el = e.target.closest('[data-action]');
  if (!el) return;
  const fn = ACTIONS[el.dataset.action];
  if (fn) { e.preventDefault(); fn(el); }
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

document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeModal(); });
window.addEventListener('hashchange', route);

Store.load();
route();
