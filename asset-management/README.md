# AssetFlow — Asset Management, Purchase Orders & Approvals

A web app for tracking company assets, requesting equipment, filling purchase
orders and routing them through approvals. It has three roles: **user**,
**manager** and **admin**.

It runs on Node.js 18+ and has no npm dependencies. Data is stored in one JSON
file on the server.

## Run it

```bash
cd asset-management
npm start            # http://localhost:3000
```

The first time you open it, it asks you to create the **admin account**. The
admin then adds everyone else from the **Users** page.

```bash
npm test             # API tests: setup, sign-in, approval flow, permissions
```

## Deploy

### Docker Compose

```bash
cd asset-management
docker compose up -d --build     # http://localhost:3000
```

Data is kept in the `assetflow-data` volume. Behind HTTPS, start it with
`COOKIE_SECURE=true TRUST_PROXY=true docker compose up -d`. Use `PORT=8080`
to change the port on your machine.

### Docker

```bash
docker build -t assetflow ./asset-management
docker run -d --name assetflow -p 3000:3000 \
  -v assetflow-data:/data \
  -e COOKIE_SECURE=true -e TRUST_PROXY=true \
  assetflow
```

Keep the `/data` volume: it holds the database (`db.json`). Back it up like any
other database file.

### Any Node host (VM, Render, Railway, Fly.io…)

- Start command: `node server.js`
- Mount persistent storage and point `DATA_DIR` at it. Without persistent
  storage, data is lost on every redeploy.
- Run a single instance. The JSON file store does not support several
  instances writing at once.

### Settings

| Variable        | Default  | Purpose |
|-----------------|----------|---------|
| `PORT`          | `3000`   | Port to listen on |
| `DATA_DIR`      | `./data` | Folder for `db.json` |
| `COOKIE_SECURE` | `false`  | Set `true` when served over HTTPS (required in production) |
| `TRUST_PROXY`   | `false`  | Set `true` behind a reverse proxy or load balancer, so sign-in throttling uses the real client IP |

Always serve the app over HTTPS in production (a reverse proxy such as
nginx or Caddy, or your host's built-in TLS).

## Security

- Passwords are hashed with scrypt. Hashes never leave the server.
- Sessions use a random token in an `HttpOnly`, `SameSite=Lax` cookie and last
  7 days. The server stores only a hash of each token.
- Every change is checked on the server against the signed-in person's role.
- Five wrong passwords for an email from one IP lock sign-in for 5 minutes.
- Resetting a password or disabling a user signs them out everywhere.
- Changes must be JSON requests with an app header, which blocks cross-site
  form posts. Responses send a strict Content-Security-Policy.

## Roles

| Role        | Sees                          | Can do |
|-------------|-------------------------------|--------|
| **User**    | Own assets, requests, POs     | Request equipment, fill POs, change own password |
| **Manager** | Own + direct reports' items   | Everything a user can, plus approve/reject team requests and POs |
| **Admin**   | Everything                    | Final PO approval, order/receive POs, fulfil requests from stock, manage assets, add users, reset passwords, disable accounts |

A user's manager is set with **Reports to** on the Users page.

## Workflows

**Equipment request:** user submits → manager approves → admin assigns an
asset from stock → fulfilled. Requests from managers and admins are approved
automatically.

**Purchase order:** user fills it in → manager approves → orders over
**€5,000** also need admin approval → ordered → received. Receiving adds one
asset per unit to stock. The threshold is `PO_ADMIN_THRESHOLD` in
`public/js/rules.js`.

## Structure

```
asset-management/
├── server.js          # HTTP server: static files + JSON API
├── lib/
│   ├── actions.js     # Every data change, with permission checks and validation
│   ├── auth.js        # Password hashing, sessions, sign-in throttling
│   └── db.js          # JSON file storage (atomic writes)
├── public/
│   ├── index.html
│   ├── css/styles.css
│   └── js/
│       ├── rules.js   # Roles and workflow rules, shared by server and browser
│       └── app.js     # Browser UI
├── test/api.test.js
├── Dockerfile
└── package.json
```
