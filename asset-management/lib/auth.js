/* Password hashing (scrypt) and cookie sessions. */
const crypto = require('crypto');

const SESSION_DAYS = 7;
const COOKIE = 'af_session';

function hashPassword(password) {
  const salt = crypto.randomBytes(16).toString('hex');
  const hash = crypto.scryptSync(password, salt, 64).toString('hex');
  return `scrypt:${salt}:${hash}`;
}

function verifyPassword(password, stored) {
  if (typeof stored !== 'string') return false;
  const [scheme, salt, hash] = stored.split(':');
  if (scheme !== 'scrypt' || !salt || !hash) return false;
  const expected = Buffer.from(hash, 'hex');
  const actual = crypto.scryptSync(password, salt, expected.length);
  return crypto.timingSafeEqual(expected, actual);
}

/* Session tokens are stored hashed, so a leaked db.json can't be used to sign in. */
const tokenKey = (token) => crypto.createHash('sha256').update(token).digest('hex');

function createSession(db, userId) {
  const token = crypto.randomBytes(32).toString('base64url');
  db.data.sessions[tokenKey(token)] = { userId, expires: Date.now() + SESSION_DAYS * 864e5 };
  pruneSessions(db);
  db.save();
  return token;
}

function pruneSessions(db) {
  const t = Date.now();
  for (const [k, s] of Object.entries(db.data.sessions)) if (s.expires < t) delete db.data.sessions[k];
}

function sessionUser(db, token) {
  if (!token) return null;
  const s = db.data.sessions[tokenKey(token)];
  if (!s || s.expires < Date.now()) return null;
  const user = db.data.users.find((u) => u.id === s.userId);
  return user && user.active ? user : null;
}

function destroySession(db, token) {
  if (token) { delete db.data.sessions[tokenKey(token)]; db.save(); }
}

/* Sign out every session for a user (after a password reset or disable). */
function destroyUserSessions(db, userId, keepToken) {
  const keep = keepToken ? tokenKey(keepToken) : null;
  for (const [k, s] of Object.entries(db.data.sessions)) if (s.userId === userId && k !== keep) delete db.data.sessions[k];
}

function sessionCookie(token, secure) {
  const parts = [`${COOKIE}=${token}`, 'Path=/', 'HttpOnly', 'SameSite=Lax', `Max-Age=${SESSION_DAYS * 86400}`];
  if (secure) parts.push('Secure');
  return parts.join('; ');
}
const clearCookie = (secure) => `${COOKIE}=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0${secure ? '; Secure' : ''}`;

function readToken(req) {
  const header = req.headers.cookie || '';
  for (const part of header.split(';')) {
    const [k, ...v] = part.trim().split('=');
    if (k === COOKIE) return v.join('=');
  }
  return null;
}

/* Simple in-memory throttle: 5 failed sign-ins per email+IP locks for 5 minutes. */
const failures = new Map();
const LOCK_MS = 5 * 60 * 1000;
function throttleKey(ip, email) { return `${ip}|${email}`; }
function isLocked(key) {
  const f = failures.get(key);
  return f && f.lockedUntil > Date.now() ? Math.ceil((f.lockedUntil - Date.now()) / 1000) : 0;
}
function recordFailure(key) {
  const f = failures.get(key) || { count: 0, lockedUntil: 0 };
  f.count += 1;
  if (f.count >= 5) { f.count = 0; f.lockedUntil = Date.now() + LOCK_MS; }
  failures.set(key, f);
}
const clearFailures = (key) => failures.delete(key);

module.exports = {
  hashPassword, verifyPassword, createSession, sessionUser, destroySession, destroyUserSessions,
  sessionCookie, clearCookie, readToken, throttleKey, isLocked, recordFailure, clearFailures,
};
