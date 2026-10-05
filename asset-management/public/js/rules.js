/* ============================================================
   Business rules shared by the server (Node) and the browser.
   The server is the authority; the browser uses the same rules
   only to decide what to show.
   ============================================================ */
(function (root, factory) {
  const rules = factory();
  if (typeof module === 'object' && module.exports) module.exports = rules;
  else root.Rules = rules;
})(typeof self !== 'undefined' ? self : this, () => {
  const ROLES = {
    user: {
      label: 'User',
      can: ['view:own-assets', 'create:request', 'create:po', 'view:own-requests', 'view:own-pos'],
    },
    manager: {
      label: 'Manager',
      can: [
        'view:own-assets', 'view:team-assets',
        'create:request', 'create:po',
        'view:team-requests', 'view:team-pos',
        'approve:request', 'approve:po-manager',
      ],
    },
    admin: {
      label: 'Admin',
      can: [
        'view:all-assets', 'manage:assets',
        'create:request', 'create:po',
        'view:all-requests', 'view:all-pos',
        'approve:request', 'approve:po-manager', 'approve:po-admin',
        'fulfill:request', 'order:po', 'receive:po',
        'manage:users',
      ],
    },
  };

  /* POs above this total need a second (admin) approval. */
  const PO_ADMIN_THRESHOLD = 5000;
  const MIN_PASSWORD = 8;

  const CATEGORIES = ['Laptop', 'Monitor', 'Phone', 'Peripheral', 'Software', 'Furniture', 'Network'];
  const PRIORITIES = ['Low', 'Normal', 'High'];
  const REQUEST_STATUS = ['Pending', 'Approved', 'Rejected', 'Fulfilled'];
  const PO_STATUS = ['Pending Manager', 'Pending Admin', 'Approved', 'Rejected', 'Ordered', 'Received'];
  const ASSET_STATUS = ['Available', 'Assigned', 'Maintenance', 'Retired'];

  const can = (u, perm) => !!u && !!ROLES[u.role] && ROLES[u.role].can.includes(perm);
  const poTotal = (po) => po.items.reduce((sum, i) => sum + (Number(i.qty) || 0) * (Number(i.unitPrice) || 0), 0);
  const teamIds = (db, u) => db.users.filter((x) => x.managerId === u.id).map((x) => x.id);

  /* Whose records a person can see: null means everyone. */
  function scopeIds(db, u) {
    if (u.role === 'admin') return null;
    if (u.role === 'manager') return [u.id, ...teamIds(db, u)];
    return [u.id];
  }
  const scoped = (list, ids, key) => (ids ? list.filter((x) => ids.includes(x[key])) : list);
  const visibleAssets = (db, u) => scoped(db.assets, scopeIds(db, u), 'assignedTo');
  const visibleRequests = (db, u) => scoped(db.requests, scopeIds(db, u), 'requesterId');
  const visiblePOs = (db, u) => scoped(db.purchaseOrders, scopeIds(db, u), 'requesterId');

  /* Items waiting on this person. */
  function myQueue(db, u) {
    const team = teamIds(db, u);
    const reqs = [];
    const pos = [];
    if (u.role === 'manager') {
      reqs.push(...db.requests.filter((r) => r.status === 'Pending' && team.includes(r.requesterId)));
      pos.push(...db.purchaseOrders.filter((p) => p.status === 'Pending Manager' && team.includes(p.requesterId)));
    }
    if (u.role === 'admin') {
      reqs.push(...db.requests.filter((r) => r.status === 'Pending' || r.status === 'Approved'));
      pos.push(...db.purchaseOrders.filter((p) => ['Pending Manager', 'Pending Admin', 'Approved', 'Ordered'].includes(p.status)));
    }
    return { reqs, pos, total: reqs.length + pos.length };
  }

  /* What this person may do with a request: 'decide', 'fulfil' or null. */
  function requestAction(db, u, r) {
    if (r.status === 'Pending' && (u.role === 'admin' || (u.role === 'manager' && teamIds(db, u).includes(r.requesterId)))) return 'decide';
    if (r.status === 'Approved' && can(u, 'fulfill:request')) return 'fulfil';
    return null;
  }

  /* Buttons this person gets on a purchase order. */
  function poActions(db, u, p) {
    const acts = [];
    if (p.status === 'Pending Manager' && (u.role === 'admin' || (u.role === 'manager' && teamIds(db, u).includes(p.requesterId)))) acts.push('approve', 'reject');
    if (p.status === 'Pending Admin' && can(u, 'approve:po-admin')) acts.push('approve', 'reject');
    if (p.status === 'Approved' && can(u, 'order:po')) acts.push('order');
    if (p.status === 'Ordered' && can(u, 'receive:po')) acts.push('receive');
    return acts;
  }

  /* Starting status for a new PO, based on who submits it and how much it costs. */
  function initialPoStatus(u, total) {
    if (u.role === 'user') return 'Pending Manager';
    if (u.role === 'manager') return total > PO_ADMIN_THRESHOLD ? 'Pending Admin' : 'Approved';
    return 'Approved';
  }

  return {
    ROLES, PO_ADMIN_THRESHOLD, MIN_PASSWORD, CATEGORIES, PRIORITIES, REQUEST_STATUS, PO_STATUS, ASSET_STATUS,
    can, poTotal, teamIds, scopeIds, visibleAssets, visibleRequests, visiblePOs, myQueue, requestAction, poActions, initialPoStatus,
  };
});
