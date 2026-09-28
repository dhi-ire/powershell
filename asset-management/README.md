# AssetFlow — Asset Management, Purchase Orders & Approvals

A dependency-free web app (HTML/CSS/JS) for tracking assets, filling purchase
orders and routing requests through approvals. Open `index.html` in a browser —
no build step. Demo data is stored in `localStorage` (use **Reset demo data** on
the landing page to start over).

## Structure

```
asset-management/
├── index.html        # Entry point
├── css/styles.css    # Dark UI theme (tokens at :root)
└── js/
    ├── data.js       # Roles/permissions, seed data, Store (persistence), helpers
    └── app.js        # Router, views, workflow actions
```

`data.js` is the only place that touches storage — replace `Store` with API
calls to plug in a real backend.

## Roles

| Role        | Sees                          | Can do |
|-------------|-------------------------------|--------|
| **User**    | Own assets, requests, POs     | Request equipment, fill POs |
| **Manager** | Own + direct reports' items   | Everything a user can, plus approve/reject team requests and POs |
| **Admin**   | Everything                    | Final PO approval, order/receive POs, fulfil requests from stock, manage assets & users |

Permissions are defined once in `ROLES` (`js/data.js`); the Users page renders
the full permission matrix.

## Workflows

**Asset request**

```
User submits → Pending → Manager approves → Approved → Admin assigns stock → Fulfilled
                        ↘ Rejected
```
Requests raised by a manager or admin are auto-approved.

**Purchase order**

```
User fills PO → Pending Manager → (total > €5,000) → Pending Admin → Approved → Ordered → Received
                              ↘ (total ≤ €5,000) ─────────────────↗
Any approval step can → Rejected
```
- Threshold is `PO_ADMIN_THRESHOLD` in `js/data.js`.
- Receiving a PO adds one inventory asset per unit (status *Available*), ready to
  fulfil requests.

## Data model

- **User** `id, name, email, role, dept, managerId`
- **Asset** `id, tag, name, category, serial, status (Available|Assigned|Maintenance|Retired), assignedTo, location, cost, purchaseDate`
- **Request** `id, number, requesterId, category, item, reason, priority, status, history[]`
- **PurchaseOrder** `id, number, requesterId, vendor, category, costCenter, neededBy, justification, items[{desc, qty, unitPrice}], status, history[]`

Every request and PO keeps an audit `history` of `{by, action, at, note}`.
