# Role Permission Matrix

Renz-Fi dashboard sessions carry an `owner` or `operator` role after login.

## Roles

| Role | Description |
|------|-------------|
| `owner` | Full system access — setup, network, router, coin hardware, firmware, backup, owner management |
| `operator` | Limited vendo operation — rates, banner/music, basic reports, active users, vouchers, customer actions |

## Owner-Only Routes (production API)

Operator sessions receive HTTP 403 `OWNER_REQUIRED`:

- `POST/PUT /api/system/wifi/config` — Ethernet/IP settings
- `GET/POST /api/access-points`, `GET/PUT/DELETE /api/access-points/{id}` — External Access Point registry (owner-only; not the ESP32 Management AP)
- `POST /api/access-points/{id}/check`, `POST /api/access-points/{id}/sync`, `GET /api/access-points/jobs/{jobId}` — External Access Point reachability / sync job (owner-only; no RouterOS; does not configure the AP)
- `GET/POST/PUT /api/router/settings`, `POST /api/router/test`
- `POST /api/system/reboot`, `POST /api/system/factory-reset`
- `GET /api/settings/backup`, `POST /api/settings/restore`

## Operator-Allowed Examples

- `GET/POST /api/coin/settings` (rates — when exposed)
- Promo, voucher, portal content routes requiring `Session` auth
- Sales and active-user read routes

## Login

`POST /api/auth/login` accepts optional `username`. Empty username with owner password remains supported for backward compatibility.

Response includes `role`: `owner` or `operator`.
