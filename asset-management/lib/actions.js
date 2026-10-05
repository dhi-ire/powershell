/* Every change to the data goes through one of these actions.
   Each checks the signed-in user's permissions and validates input. */
const crypto = require('crypto');
const R = require('../public/js/rules');
const auth = require('./auth');

class HttpError extends Error {
  constructor(status, message) { super(message); this.status = status; }
}
const deny = (msg = 'You don\'t have permission to do that.') => { throw new HttpError(403, msg); };
const bad = (msg) => { throw new HttpError(400, msg); };
const notFound = (what) => { throw new HttpError(404, `${what} not found.`); };

const now = () => new Date().toISOString();
const uid = () => crypto.randomBytes(8).toString('hex');

/* ---------- Input helpers ---------- */
function str(v, field, { max = 200, required = true } = {}) {
  const s = typeof v === 'string' ? v.trim() : '';
  if (required && !s) bad(`${field} is required.`);
  if (s.length > max) bad(`${field} must be ${max} characters or fewer.`);
  return s;
}
function oneOf(v, list, field) {
  if (!list.includes(v)) bad(`${field} must be one of: ${list.join(', ')}.`);
  return v;
}
function num(v, field, { min = 0, max = 1e9, int = false } = {}) {
  const n = Number(v);
  if (!Number.isFinite(n) || n < min || n > max || (int && !Number.isInteger(n))) bad(`${field} must be a number from ${min} to ${max}.`);
  return n;
}
function date(v, field) {
  const s = str(v, field, { max: 10 });
  if (!/^\d{4}-\d{2}-\d{2}$/.test(s) || Number.isNaN(Date.parse(s))) bad(`${field} must be a date (YYYY-MM-DD).`);
  return s;
}
function email(v) {
  const s = str(v, 'Email', { max: 254 }).toLowerCase();
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(s)) bad('Enter a valid email address.');
  return s;
}
function password(v) {
  if (typeof v !== 'string' || v.length < R.MIN_PASSWORD) bad(`Passwords need at least ${R.MIN_PASSWORD} characters.`);
  if (v.length > 200) bad('Password is too long.');
  return v;
}
const note = (v) => str(v, 'Note', { max: 500, required: false }) || undefined;

const find = (list, id, what) => list.find((x) => x.id === id) || notFound(what);

/* ---------- Actions ---------- */
const actions = {
  submitRequest(db, u, b) {
    const auto = u.role !== 'user'; // managers and admins skip manager review
    const r = {
      id: uid(), number: db.nextNumber('request', 'REQ'), requesterId: u.id,
      category: oneOf(b.category, R.CATEGORIES, 'Category'),
      priority: oneOf(b.priority, R.PRIORITIES, 'Priority'),
      item: str(b.item, 'Item'),
      reason: str(b.reason, 'Reason', { max: 1000 }),
      status: auto ? 'Approved' : 'Pending', createdAt: now(),
      history: [{ by: u.id, action: 'Submitted', at: now() }],
    };
    if (auto) r.history.push({ by: u.id, action: 'Auto-approved', at: now() });
    db.data.requests.push(r);
    return auto ? `${r.number} submitted and approved` : `${r.number} sent to your manager`;
  },

  decideRequest(db, u, b) {
    const r = find(db.data.requests, b.id, 'Request');
    if (R.requestAction(db.data, u, r) !== 'decide') deny();
    const status = b.decision === 'approve' ? 'Approved' : b.decision === 'reject' ? 'Rejected' : bad('Decision must be approve or reject.');
    r.status = status;
    r.history.push({ by: u.id, action: status, at: now(), note: note(b.note) });
    return `${r.number} ${status.toLowerCase()}`;
  },

  fulfilRequest(db, u, b) {
    const r = find(db.data.requests, b.id, 'Request');
    if (R.requestAction(db.data, u, r) !== 'fulfil') deny();
    const a = find(db.data.assets, b.assetId, 'Asset');
    if (a.status !== 'Available') bad(`${a.tag} is not available.`);
    Object.assign(a, { status: 'Assigned', assignedTo: r.requesterId });
    r.status = 'Fulfilled';
    r.history.push({ by: u.id, action: 'Fulfilled', at: now(), note: `Assigned ${a.tag}` });
    return `${a.tag} assigned`;
  },

  submitPO(db, u, b) {
    if (!Array.isArray(b.items) || !b.items.length || b.items.length > 50) bad('Add between 1 and 50 line items.');
    const items = b.items.map((i, n) => ({
      desc: str(i.desc, `Line ${n + 1} description`),
      qty: num(i.qty, `Line ${n + 1} quantity`, { min: 1, max: 10000, int: true }),
      unitPrice: num(i.unitPrice, `Line ${n + 1} unit price`, { min: 0, max: 1e7 }),
    }));
    const po = {
      id: uid(), number: db.nextNumber('po', 'PO'), requesterId: u.id,
      vendor: str(b.vendor, 'Vendor'),
      category: oneOf(b.category, R.CATEGORIES, 'Category'),
      costCenter: str(b.costCenter, 'Cost center', { max: 40 }),
      neededBy: date(b.neededBy, 'Needed by'),
      justification: str(b.justification, 'Justification', { max: 1000 }),
      items, createdAt: now(),
      history: [{ by: u.id, action: 'Submitted', at: now() }],
    };
    po.status = R.initialPoStatus(u, R.poTotal(po));
    if (u.role !== 'user') po.history.push({ by: u.id, action: po.status === 'Approved' ? 'Auto-approved' : 'Manager approved', at: now() });
    db.data.purchaseOrders.push(po);
    return `${po.number} submitted · ${po.status}`;
  },

  poApprove(db, u, b) {
    const p = find(db.data.purchaseOrders, b.id, 'Purchase order');
    if (!R.poActions(db.data, u, p).includes('approve')) deny();
    if (p.status === 'Pending Manager') {
      p.status = R.poTotal(p) > R.PO_ADMIN_THRESHOLD ? 'Pending Admin' : 'Approved';
      p.history.push({ by: u.id, action: 'Manager approved', at: now(), note: note(b.note) });
    } else {
      p.status = 'Approved';
      p.history.push({ by: u.id, action: 'Admin approved', at: now(), note: note(b.note) });
    }
    return `${p.number} → ${p.status}`;
  },

  poReject(db, u, b) {
    const p = find(db.data.purchaseOrders, b.id, 'Purchase order');
    if (!R.poActions(db.data, u, p).includes('reject')) deny();
    p.status = 'Rejected';
    p.history.push({ by: u.id, action: 'Rejected', at: now(), note: note(b.note) });
    return `${p.number} rejected`;
  },

  poOrder(db, u, b) {
    const p = find(db.data.purchaseOrders, b.id, 'Purchase order');
    if (!R.poActions(db.data, u, p).includes('order')) deny();
    p.status = 'Ordered';
    p.history.push({ by: u.id, action: 'Ordered', at: now() });
    return `${p.number} marked as ordered`;
  },

  poReceive(db, u, b) {
    const p = find(db.data.purchaseOrders, b.id, 'Purchase order');
    if (!R.poActions(db.data, u, p).includes('receive')) deny();
    let added = 0;
    if (p.category !== 'Software') {
      for (const i of p.items) {
        for (let n = 0; n < Math.min(i.qty, 500); n++) {
          db.data.assets.push({
            id: uid(), tag: db.nextNumber('asset', 'AST'), name: i.desc, category: p.category,
            serial: '', status: 'Available', assignedTo: null, location: 'IT Store', cost: i.unitPrice,
            purchaseDate: now().slice(0, 10),
          });
          added++;
        }
      }
    }
    p.status = 'Received';
    p.history.push({ by: u.id, action: 'Received', at: now(), note: added ? `${added} asset(s) added to inventory` : undefined });
    return `${p.number} received · ${added} asset(s) added`;
  },

  saveAsset(db, u, b) {
    if (!R.can(u, 'manage:assets')) deny();
    const assignedTo = b.assignedTo || null;
    if (assignedTo) find(db.data.users, assignedTo, 'User');
    let status = oneOf(b.status, R.ASSET_STATUS, 'Status');
    if (assignedTo && status === 'Available') status = 'Assigned';
    if (!assignedTo && status === 'Assigned') status = 'Available';
    const fields = {
      name: str(b.name, 'Name'),
      category: oneOf(b.category, R.CATEGORIES, 'Category'),
      serial: str(b.serial, 'Serial number', { max: 100, required: false }),
      location: str(b.location, 'Location', { max: 100, required: false }),
      cost: num(b.cost || 0, 'Cost', { max: 1e7 }),
      purchaseDate: b.purchaseDate ? date(b.purchaseDate, 'Purchase date') : '',
      status, assignedTo,
    };
    if (b.id) {
      Object.assign(find(db.data.assets, b.id, 'Asset'), fields);
      return 'Asset updated';
    }
    const a = { id: uid(), tag: db.nextNumber('asset', 'AST'), ...fields };
    db.data.assets.push(a);
    return `${a.tag} added`;
  },

  saveUser(db, u, b, ctx) {
    if (!R.can(u, 'manage:users')) deny();
    const fields = {
      name: str(b.name, 'Full name', { max: 100 }),
      email: email(b.email),
      dept: str(b.dept, 'Department', { max: 100 }),
      role: oneOf(b.role, Object.keys(R.ROLES), 'Role'),
      managerId: b.managerId || null,
      active: b.active !== false && b.active !== 'false',
    };
    if (fields.managerId) {
      const m = find(db.data.users, fields.managerId, 'Manager');
      if (m.role === 'user' || m.id === b.id) bad('Choose a manager or admin as "Reports to".');
    }
    if (db.data.users.some((x) => x.email === fields.email && x.id !== b.id)) bad('Another user already has that email.');
    const pw = b.password ? password(b.password) : null;

    if (b.id) {
      const target = find(db.data.users, b.id, 'User');
      if (target.id === u.id && fields.role !== 'admin') bad('You can\'t remove your own admin role.');
      if (target.id === u.id && !fields.active) bad('You can\'t disable your own account.');
      Object.assign(target, fields);
      if (pw) target.passwordHash = auth.hashPassword(pw);
      if (pw || !fields.active) auth.destroyUserSessions(db, target.id, target.id === u.id ? ctx.token : null);
      return pw ? 'User updated and password reset' : 'User updated';
    }
    if (!pw) bad('Set a starting password for the new user.');
    db.data.counters.user += 1;
    db.data.users.push({ id: `u${db.data.counters.user}`, ...fields, passwordHash: auth.hashPassword(pw), createdAt: now() });
    return `${fields.name} can now sign in`;
  },

  changePassword(db, u, b, ctx) {
    if (!auth.verifyPassword(String(b.current || ''), u.passwordHash)) bad('Your current password is wrong.');
    u.passwordHash = auth.hashPassword(password(b.next));
    auth.destroyUserSessions(db, u.id, ctx.token); // sign out other devices
    return 'Password updated';
  },
};

module.exports = { actions, HttpError };
