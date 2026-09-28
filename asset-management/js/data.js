/* ============================================================
   Data layer — seed data, persistence (localStorage), helpers.
   Swap this file for real API calls when a backend exists.
   ============================================================ */

const STORAGE_KEY = 'assetflow.v1';

/* Roles & permissions — single source of truth for access rules. */
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

const CATEGORIES = ['Laptop', 'Monitor', 'Phone', 'Peripheral', 'Software', 'Furniture', 'Network'];

const REQUEST_STATUS = ['Pending', 'Approved', 'Rejected', 'Fulfilled'];
const PO_STATUS = ['Pending Manager', 'Pending Admin', 'Approved', 'Rejected', 'Ordered', 'Received'];
const ASSET_STATUS = ['Available', 'Assigned', 'Maintenance', 'Retired'];

const now = () => new Date().toISOString();
const daysAgo = (d) => new Date(Date.now() - d * 864e5).toISOString();

function seed() {
  return {
    users: [
      { id: 'u1', name: 'Aisha Khan',    email: 'aisha@company.com', role: 'admin',   dept: 'IT',          managerId: null },
      { id: 'u2', name: 'Marco Rossi',   email: 'marco@company.com', role: 'manager', dept: 'Engineering', managerId: 'u1' },
      { id: 'u3', name: 'Priya Sharma',  email: 'priya@company.com', role: 'user',    dept: 'Engineering', managerId: 'u2' },
      { id: 'u4', name: 'Liam O\'Brien', email: 'liam@company.com',  role: 'user',    dept: 'Engineering', managerId: 'u2' },
      { id: 'u5', name: 'Sara Lee',      email: 'sara@company.com',  role: 'manager', dept: 'Sales',       managerId: 'u1' },
      { id: 'u6', name: 'Tom Becker',    email: 'tom@company.com',   role: 'user',    dept: 'Sales',       managerId: 'u5' },
    ],
    assets: [
      { id: 'a1', tag: 'AST-1001', name: 'MacBook Pro 14"',     category: 'Laptop',     serial: 'C02XK1', status: 'Assigned',    assignedTo: 'u3', location: 'Dublin HQ', cost: 2399, purchaseDate: '2025-02-11' },
      { id: 'a2', tag: 'AST-1002', name: 'Dell U2723QE 27"',    category: 'Monitor',    serial: 'DL8831', status: 'Assigned',    assignedTo: 'u3', location: 'Dublin HQ', cost: 579,  purchaseDate: '2025-02-11' },
      { id: 'a3', tag: 'AST-1003', name: 'ThinkPad X1 Carbon',  category: 'Laptop',     serial: 'PF3K9Q', status: 'Assigned',    assignedTo: 'u4', location: 'Remote',    cost: 1899, purchaseDate: '2024-11-02' },
      { id: 'a4', tag: 'AST-1004', name: 'iPhone 16',           category: 'Phone',      serial: 'F2LX77', status: 'Assigned',    assignedTo: 'u6', location: 'Cork',      cost: 999,  purchaseDate: '2025-06-20' },
      { id: 'a5', tag: 'AST-1005', name: 'ThinkPad T14',        category: 'Laptop',     serial: 'PF4M21', status: 'Available',   assignedTo: null, location: 'IT Store',  cost: 1299, purchaseDate: '2025-08-01' },
      { id: 'a6', tag: 'AST-1006', name: 'LG 27UL500 27"',      category: 'Monitor',    serial: 'LG2290', status: 'Available',   assignedTo: null, location: 'IT Store',  cost: 329,  purchaseDate: '2025-08-01' },
      { id: 'a7', tag: 'AST-1007', name: 'Logitech MX Keys',    category: 'Peripheral', serial: 'LMX001', status: 'Available',   assignedTo: null, location: 'IT Store',  cost: 119,  purchaseDate: '2025-07-15' },
      { id: 'a8', tag: 'AST-1008', name: 'Cisco Meraki MR46',   category: 'Network',    serial: 'Q2KD88', status: 'Maintenance', assignedTo: null, location: 'Dublin HQ', cost: 1150, purchaseDate: '2023-04-09' },
      { id: 'a9', tag: 'AST-1009', name: 'Herman Miller Aeron', category: 'Furniture',  serial: 'HMA552', status: 'Assigned',    assignedTo: 'u2', location: 'Dublin HQ', cost: 1450, purchaseDate: '2022-09-30' },
      { id: 'a10', tag: 'AST-1010', name: 'MacBook Air 13"',    category: 'Laptop',     serial: 'C02ZZ4', status: 'Retired',     assignedTo: null, location: 'IT Store',  cost: 1099, purchaseDate: '2020-01-12' },
    ],
    requests: [
      { id: 'r1', number: 'REQ-2001', requesterId: 'u4', category: 'Monitor',    item: '27" 4K monitor',    reason: 'Second screen for code reviews', priority: 'Normal', status: 'Pending',  createdAt: daysAgo(1),
        history: [{ by: 'u4', action: 'Submitted', at: daysAgo(1) }] },
      { id: 'r2', number: 'REQ-2002', requesterId: 'u3', category: 'Peripheral', item: 'Wireless keyboard', reason: 'Current one is failing',         priority: 'Low',    status: 'Approved', createdAt: daysAgo(3),
        history: [{ by: 'u3', action: 'Submitted', at: daysAgo(3) }, { by: 'u2', action: 'Approved', at: daysAgo(2), note: 'OK' }] },
      { id: 'r3', number: 'REQ-2003', requesterId: 'u6', category: 'Laptop',     item: 'Lightweight laptop', reason: 'Travel for client visits',      priority: 'High',   status: 'Pending',  createdAt: daysAgo(0.2),
        history: [{ by: 'u6', action: 'Submitted', at: daysAgo(0.2) }] },
    ],
    purchaseOrders: [
      { id: 'p1', number: 'PO-3001', requesterId: 'u3', vendor: 'Dell Technologies', category: 'Monitor', costCenter: 'ENG-100', neededBy: '2026-10-20',
        justification: 'Monitors for 4 new hires', status: 'Pending Manager', createdAt: daysAgo(0.5),
        items: [{ desc: 'Dell U2723QE 27" monitor', qty: 4, unitPrice: 579 }],
        history: [{ by: 'u3', action: 'Submitted', at: daysAgo(0.5) }] },
      { id: 'p2', number: 'PO-3002', requesterId: 'u2', vendor: 'Apple Business', category: 'Laptop', costCenter: 'ENG-100', neededBy: '2026-11-01',
        justification: 'Laptop refresh for senior engineers', status: 'Pending Admin', createdAt: daysAgo(2),
        items: [{ desc: 'MacBook Pro 14" M4', qty: 3, unitPrice: 2399 }, { desc: 'AppleCare+', qty: 3, unitPrice: 279 }],
        history: [{ by: 'u2', action: 'Submitted', at: daysAgo(2) }, { by: 'u2', action: 'Manager approved', at: daysAgo(2) }] },
      { id: 'p3', number: 'PO-3003', requesterId: 'u6', vendor: 'Logitech', category: 'Peripheral', costCenter: 'SAL-200', neededBy: '2026-10-05',
        justification: 'Headsets for sales calls', status: 'Ordered', createdAt: daysAgo(9),
        items: [{ desc: 'Logitech Zone Vibe 100', qty: 6, unitPrice: 99 }],
        history: [{ by: 'u6', action: 'Submitted', at: daysAgo(9) }, { by: 'u5', action: 'Manager approved', at: daysAgo(8) }, { by: 'u1', action: 'Ordered', at: daysAgo(7) }] },
    ],
    counters: { asset: 1010, request: 2003, po: 3003, user: 6 },
    currentUserId: null,
  };
}

const Store = {
  state: null,

  load() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      this.state = raw ? JSON.parse(raw) : seed();
    } catch {
      this.state = seed();
    }
    return this.state;
  },

  save() {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(this.state)); } catch { /* private mode */ }
  },

  reset() {
    this.state = seed();
    this.save();
  },

  nextId(kind, prefix) {
    this.state.counters[kind] += 1;
    return `${prefix}-${this.state.counters[kind]}`;
  },
};

/* ---------- Helpers ---------- */
const uid = () => Math.random().toString(36).slice(2, 10);

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

const poTotal = (po) => po.items.reduce((sum, i) => sum + (Number(i.qty) || 0) * (Number(i.unitPrice) || 0), 0);
