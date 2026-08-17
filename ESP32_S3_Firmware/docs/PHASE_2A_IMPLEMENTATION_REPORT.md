# Phase 2A — First-Run Owner Provisioning

**Status:** Implemented  
**Scope:** Management AP setup plane only (`192.168.4.1`)

## Summary

Phase 2A turns `/admin/setup` into a two-step first-run wizard:

1. **Device Check** — device ID, firmware, Ethernet link/IP, storage status
2. **Create Owner Account** — display name, username, password (with confirmation)

Owner credentials persist across reboot. Production Ethernet behavior is unchanged.

## Storage

**Path:** `/config/provisioning.json` (`StoragePaths::ProvisioningFile`)

**Schema version:** `1`

```json
{
  "schemaVersion": 1,
  "installationState": "factory",
  "ownerCreated": false,
  "ownerUsername": "",
  "ownerDisplayName": "",
  "ownerPasswordHash": "<sha256 hex, never exposed via HTTP>",
  "createdAt": 0,
  "updatedAt": 0
}
```

**`installationState` values:** `factory` | `owner_created` | `router_configured` | `provisioned`

Phase 2A sets `owner_created` after successful owner creation. Later phases advance
`router_configured` and `provisioned`.

**Password handling:**

- Plaintext passwords are never persisted or logged.
- NVS admin login uses `AuthCredentials::hashPassword()` (SHA-256 hex via mbedtls).
- SD record stores the same hash in `ownerPasswordHash` for audit/recovery metadata.
- `AuthManager::provisionOwnerCredentials()` sets NVS credentials without requiring the default admin password.

## Setup-plane API

| Method | Path | Plane | Purpose |
|--------|------|-------|---------|
| GET | `/api/setup/status` | Setup only | Wizard status (no password/hash) |
| POST | `/api/setup/owner` | Setup only | Create owner account |

Ethernet requests to these routes receive `403 SETUP_PLANE_REQUIRED`.

## Serial logs

- `[setup] owner account created`
- `[setup] owner creation rejected: <error code>` — never logs passwords or request bodies

## Files added/changed

| File | Change |
|------|--------|
| `src/SetupProvisioningManager.h/.cpp` | Persistent provisioning record + validation |
| `src/AuthManager.h/.cpp` | `provisionOwnerCredentials()` |
| `src/web/SetupServer.h/.cpp` | Two-step wizard UI + setup API routes |
| `src/web/WebServerManager.h/.cpp` | Wire `SetupProvisioningManager` into setup server |
| `src/FirmwareApp.h/.cpp` | Boot `SetupProvisioningManager` |
| `src/StoragePaths.h` | `ProvisioningFile` constant |
| `docs/HTTP_ROUTE_CONTRACT.md` | Route table + spec |
| `docs/NETWORK_PLANE_ARCHITECTURE.md` | Setup plane route list |

## Acceptance

- Fresh device on Renz-Fi Setup shows Device Check, then Create Owner Account.
- Successful owner creation survives reboot; `GET /api/setup/status` reports `owner_created` / `ownerCreated: true`.
- Re-submitting `POST /api/setup/owner` returns `409 OWNER_ALREADY_EXISTS`.
- Password/hash never returned by endpoints or serial logs.
