/* AssetFlow server: static files + JSON API. No dependencies beyond Node 18+.
   Config (environment variables):
     PORT           port to listen on (default 3000)
     DATA_DIR       where db.json is stored (default ./data)
     COOKIE_SECURE  "true" to mark cookies Secure (set this when served over HTTPS)
     TRUST_PROXY    "true" when behind a reverse proxy, to read the client IP from X-Forwarded-For */
const http = require('http');
const fs = require('fs');
const path = require('path');
const R = require('./public/js/rules');
const Db = require('./lib/db');
const auth = require('./lib/auth');
const { actions, HttpError } = require('./lib/actions');

const PUBLIC = path.join(__dirname, 'public');
const TYPES = { '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.png': 'image/png' };
const SECURITY_HEADERS = {
  'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; font-src https://fonts.gstatic.com; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'",
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'same-origin',
  'X-Frame-Options': 'DENY',
};

function createServer({ dataDir = process.env.DATA_DIR || path.join(__dirname, 'data'), cookieSecure = process.env.COOKIE_SECURE === 'true', trustProxy = process.env.TRUST_PROXY === 'true' } = {}) {
  const db = Db.open(dataDir);

  const send = (res, status, body, headers = {}) => {
    res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...SECURITY_HEADERS, ...headers });
    res.end(JSON.stringify(body));
  };

  const readJson = (req) => new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (c) => {
      size += c.length;
      if (size > 100 * 1024) { reject(new HttpError(413, 'Request is too large.')); req.destroy(); return; }
      chunks.push(c);
    });
    req.on('end', () => {
      try { resolve(chunks.length ? JSON.parse(Buffer.concat(chunks).toString('utf8')) : {}); } catch { reject(new HttpError(400, 'Request body must be JSON.')); }
    });
    req.on('error', reject);
  });

  const clientIp = (req) => (trustProxy && req.headers['x-forwarded-for'] ? req.headers['x-forwarded-for'].split(',')[0].trim() : req.socket.remoteAddress);

  /* What a signed-in person is allowed to see. Password hashes and sessions never leave the server. */
  function stateFor(u) {
    const d = db.data;
    const isAdmin = u.role === 'admin';
    return {
      me: { id: u.id, name: u.name, email: u.email, role: u.role, dept: u.dept, managerId: u.managerId, active: u.active },
      users: d.users.map((x) => ({ id: x.id, name: x.name, role: x.role, dept: x.dept, managerId: x.managerId, active: x.active, ...(isAdmin || x.id === u.id ? { email: x.email } : {}) })),
      assets: isAdmin ? d.assets : R.visibleAssets(d, u),
      requests: R.visibleRequests(d, u),
      purchaseOrders: R.visiblePOs(d, u),
    };
  }

  async function api(req, res, url) {
    const route = `${req.method} ${url.pathname}`;
    const token = auth.readToken(req);

    // Mutating requests must be JSON with our header: blocks cross-site form posts (CSRF).
    if (req.method !== 'GET' && (req.headers['x-requested-with'] !== 'AssetFlow' || !String(req.headers['content-type'] || '').startsWith('application/json'))) {
      throw new HttpError(403, 'Request blocked.');
    }

    if (route === 'GET /api/health') return send(res, 200, { ok: true });

    if (route === 'GET /api/session') {
      const u = auth.sessionUser(db, token);
      return send(res, 200, { needsSetup: db.data.users.length === 0, state: u ? stateFor(u) : null });
    }

    if (route === 'POST /api/setup') {
      if (db.data.users.length) throw new HttpError(409, 'Setup is already done. Sign in instead.');
      const b = await readJson(req);
      const { email, name, dept, pw } = { email: String(b.email || '').trim().toLowerCase(), name: String(b.name || '').trim(), dept: String(b.dept || '').trim() || 'IT', pw: String(b.password || '') };
      if (!name || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) throw new HttpError(400, 'Enter your name and a valid email.');
      if (pw.length < R.MIN_PASSWORD) throw new HttpError(400, `Passwords need at least ${R.MIN_PASSWORD} characters.`);
      db.data.counters.user += 1;
      const u = { id: `u${db.data.counters.user}`, name, email, dept, role: 'admin', managerId: null, active: true, passwordHash: auth.hashPassword(pw), createdAt: new Date().toISOString() };
      db.data.users.push(u);
      const t = auth.createSession(db, u.id);
      return send(res, 200, { state: stateFor(u), message: 'Admin account created' }, { 'Set-Cookie': auth.sessionCookie(t, cookieSecure) });
    }

    if (route === 'POST /api/login') {
      const b = await readJson(req);
      const email = String(b.email || '').trim().toLowerCase();
      const key = auth.throttleKey(clientIp(req), email);
      const wait = auth.isLocked(key);
      if (wait) throw new HttpError(429, `Too many attempts. Try again in ${Math.ceil(wait / 60)} minute(s).`);
      const u = db.data.users.find((x) => x.email === email);
      if (!u || !auth.verifyPassword(String(b.password || ''), u.passwordHash)) {
        auth.recordFailure(key);
        throw new HttpError(401, 'That email and password don\'t match. Check both and try again.');
      }
      if (!u.active) throw new HttpError(403, 'This account is disabled. Ask an admin to turn it back on.');
      auth.clearFailures(key);
      const t = auth.createSession(db, u.id);
      return send(res, 200, { state: stateFor(u) }, { 'Set-Cookie': auth.sessionCookie(t, cookieSecure) });
    }

    if (route === 'POST /api/logout') {
      auth.destroySession(db, token);
      return send(res, 200, { ok: true }, { 'Set-Cookie': auth.clearCookie(cookieSecure) });
    }

    const m = url.pathname.match(/^\/api\/actions\/([A-Za-z]+)$/);
    if (req.method === 'POST' && m && Object.hasOwn(actions, m[1])) {
      const u = auth.sessionUser(db, token);
      if (!u) throw new HttpError(401, 'Your session has ended. Sign in again.');
      const body = await readJson(req);
      const message = actions[m[1]](db, u, body, { token });
      db.save();
      return send(res, 200, { message, state: stateFor(u) });
    }

    throw new HttpError(404, 'Not found.');
  }

  function serveStatic(req, res, url) {
    let rel = decodeURIComponent(url.pathname);
    if (rel === '/') rel = '/index.html';
    const file = path.normalize(path.join(PUBLIC, rel));
    if (!file.startsWith(PUBLIC + path.sep)) { res.writeHead(403); return res.end(); }
    fs.readFile(file, (err, buf) => {
      if (err) { res.writeHead(404, { 'Content-Type': 'text/plain' }); return res.end('Not found'); }
      res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-cache', ...SECURITY_HEADERS });
      res.end(buf);
    });
  }

  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost');
    try {
      if (url.pathname.startsWith('/api/')) await api(req, res, url);
      else if (req.method === 'GET' || req.method === 'HEAD') serveStatic(req, res, url);
      else send(res, 405, { error: 'Method not allowed.' });
    } catch (err) {
      if (err instanceof HttpError) return send(res, err.status, { error: err.message });
      console.error(err);
      send(res, 500, { error: 'Something went wrong on the server. Try again.' });
    }
  });
  return { server, db };
}

if (require.main === module) {
  const port = Number(process.env.PORT) || 3000;
  const { server } = createServer();
  server.listen(port, () => console.log(`AssetFlow running on http://localhost:${port}`));
  const stop = () => server.close(() => process.exit(0));
  process.on('SIGTERM', stop);
  process.on('SIGINT', stop);
}

module.exports = { createServer };
