/* End-to-end API test: setup → users → request/PO approval flow → permissions. Run with `npm test`. */
const { test, before, after } = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { createServer } = require('../server');

let server;
let base;
const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'assetflow-test-'));

before(async () => {
  ({ server } = createServer({ dataDir }));
  await new Promise((r) => server.listen(0, r));
  base = `http://127.0.0.1:${server.address().port}`;
});
after(() => { server.close(); fs.rmSync(dataDir, { recursive: true, force: true }); });

/* A tiny client that keeps its own session cookie. */
function client() {
  let cookie = '';
  return async (pathname, body) => {
    const res = await fetch(base + pathname, {
      method: body === undefined ? 'GET' : 'POST',
      headers: { cookie, ...(body === undefined ? {} : { 'Content-Type': 'application/json', 'X-Requested-With': 'AssetFlow' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const set = res.headers.get('set-cookie');
    if (set) cookie = set.split(';')[0];
    return { status: res.status, body: await res.json() };
  };
}

test('full workflow with permissions', async () => {
  const admin = client();
  let r = await admin('/api/session');
  assert.equal(r.body.needsSetup, true);

  r = await admin('/api/setup', { name: 'Ada Admin', email: 'ada@example.com', dept: 'IT', password: 'admin-pass-1' });
  assert.equal(r.status, 200);
  assert.equal(r.body.state.me.role, 'admin');
  assert.equal((await admin('/api/setup', { name: 'X', email: 'x@example.com', password: 'whatever12' })).status, 409, 'setup only once');

  r = await admin('/api/actions/saveUser', { name: 'Mo Manager', email: 'mo@example.com', dept: 'Eng', role: 'manager', active: 'true', password: 'manager-pass' });
  const moId = r.body.state.users.find((u) => u.email === 'mo@example.com').id;
  r = await admin('/api/actions/saveUser', { name: 'Uma User', email: 'uma@example.com', dept: 'Eng', role: 'user', managerId: moId, active: 'true', password: 'user-pass-1' });
  assert.equal(r.status, 200);
  assert.ok(!JSON.stringify(r.body).includes('passwordHash'), 'hashes never leave the server');

  const uma = client();
  assert.equal((await uma('/api/login', { email: 'uma@example.com', password: 'wrong-pass' })).status, 401);
  assert.equal((await uma('/api/login', { email: 'UMA@example.com', password: 'user-pass-1' })).status, 200);

  // A user can't do admin things.
  assert.equal((await uma('/api/actions/saveAsset', { name: 'x', category: 'Laptop', status: 'Available' })).status, 403);
  assert.equal((await uma('/api/actions/saveUser', { name: 'x', email: 'y@example.com', dept: 'x', role: 'admin', password: 'xxxxxxxx' })).status, 403);

  r = await uma('/api/actions/submitPO', { vendor: 'Lenovo', category: 'Laptop', costCenter: 'ENG-100', neededBy: '2026-12-01', justification: 'New hires', items: [{ desc: 'ThinkPad', qty: 4, unitPrice: 1500 }] });
  assert.equal(r.status, 200);
  const po = r.body.state.purchaseOrders[0];
  assert.equal(po.status, 'Pending Manager');
  assert.equal((await uma('/api/actions/poApprove', { id: po.id })).status, 403, 'requester cannot approve own PO');

  const mo = client();
  await mo('/api/login', { email: 'mo@example.com', password: 'manager-pass' });
  r = await mo('/api/actions/poApprove', { id: po.id, note: 'ok' });
  assert.equal(r.body.state.purchaseOrders.find((p) => p.id === po.id).status, 'Pending Admin', 'over threshold needs admin');
  assert.equal((await mo('/api/actions/poOrder', { id: po.id })).status, 403);

  await admin('/api/actions/poApprove', { id: po.id });
  await admin('/api/actions/poOrder', { id: po.id });
  r = await admin('/api/actions/poReceive', { id: po.id });
  assert.equal(r.body.state.assets.filter((a) => a.name === 'ThinkPad').length, 4);

  r = await uma('/api/actions/submitRequest', { category: 'Laptop', priority: 'High', item: 'Laptop', reason: 'Old one broke' });
  const req = r.body.state.requests[0];
  assert.equal((await uma('/api/actions/decideRequest', { id: req.id, decision: 'approve' })).status, 403);
  await mo('/api/actions/decideRequest', { id: req.id, decision: 'approve' });
  const stock = (await admin('/api/session')).body.state.assets.find((a) => a.status === 'Available');
  await admin('/api/actions/fulfilRequest', { id: req.id, assetId: stock.id });
  r = await uma('/api/session');
  assert.equal(r.body.state.assets.length, 1, 'user sees the asset assigned to them');
  assert.equal(r.body.state.requests[0].status, 'Fulfilled');

  // Disabling a user ends their session.
  await admin('/api/actions/saveUser', { id: r.body.state.me.id, name: 'Uma User', email: 'uma@example.com', dept: 'Eng', role: 'user', managerId: moId, active: 'false' });
  assert.equal((await uma('/api/session')).body.state, null);
  assert.equal((await uma('/api/login', { email: 'uma@example.com', password: 'user-pass-1' })).status, 403);

  // Data survives a restart.
  assert.ok(JSON.parse(fs.readFileSync(path.join(dataDir, 'db.json'), 'utf8')).purchaseOrders.length === 1);
});

test('blocks requests without the app header (CSRF)', async () => {
  const res = await fetch(`${base}/api/login`, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'email=a&password=b' });
  assert.equal(res.status, 403);
});

test('does not serve files outside public/', async () => {
  const res = await fetch(`${base}/..%2Fserver.js`);
  assert.notEqual(res.status, 200);
});
