# Phase 2C — MikroTik Router Credentials + Safe Connection Validation

**Status:** Implemented  
**Scope:** Management AP setup plane only (`192.168.4.1`)

## Summary

Phase 2C adds **Step 4 — Connect MikroTik Router** to the first-run wizard. The step
validates RouterOS API access over TCP port 8728 (read-only identity check) and persists
device-bound protected credentials. No RouterOS configuration changes are made.

## Wizard flow

1. Device Check  
2. Create Owner Account  
3. Check Router Connection  
4. Connect MikroTik Router  
5. Success — “Router configuration will be added in the next step.”

`installationState` advances to `router_configured` **only** after a successful
**Save Router Connection** (which re-validates credentials). Test Connection does not
persist or advance state.

## Persistence

**Path:** `/config/router-connection.json` (`StoragePaths::RouterConnectionFile`)

```json
{
  "schemaVersion": 1,
  "routerType": "mikrotik",
  "host": "10.10.10.1",
  "apiPort": 8728,
  "username": "admin",
  "passwordProtected": "enc:v1:<nonce_hex>:<cipher_hex>",
  "connectionVerified": true,
  "verifiedAt": 123456,
  "createdAt": 123000,
  "updatedAt": 123456
}
```

This file is **not** included in `BackupManager` export bundles (same as
`provisioning.json`).

## Security

| Decision | Detail |
|----------|--------|
| Password storage | `CredentialProtector` — AES-128-CTR, key derived from chip eFuse MAC + salt |
| HTTP responses | Never return password or `passwordProtected` blob |
| Serial logs | Never log passwords, request bodies, or credential blobs |
| Limitation | Device-bound obfuscation only — not secure against combined physical ESP32 + SD access |

## Setup-plane API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/setup/router-config` | Safe metadata (no password) |
| POST | `/api/setup/router/test` | Validate RouterOS API login + identity |
| POST | `/api/setup/router/save` | Revalidate, persist, advance to `router_configured` |

All require `owner_created` minimum; otherwise `403 SETUP_OWNER_REQUIRED`.

### Validation error codes

`INVALID_HOST`, `ETHERNET_NOT_READY`, `TCP_CONNECT_FAILED`, `API_LOGIN_FAILED`,
`ROUTEROS_API_UNAVAILABLE`, `ROUTEROS_API_UNSUPPORTED`, `ROUTER_VALIDATED`

### Example: POST `/api/setup/router/save` success

```json
{
  "success": true,
  "message": "MikroTik connection saved",
  "data": {
    "validationCode": "ROUTER_VALIDATED",
    "routerIdentity": "MikroTik",
    "nextStep": "router_configuration",
    "installationState": "router_configured",
    "wizardStep": "router_complete"
  }
}
```

## Setup-plane routing fix

- `GET /favicon.ico` on Management AP returns **204 No Content** (SetupServer + AdminServer)
- `PortalServer` rejects setup-plane requests before any SPIFFS read (`SETUP_PLANE_RESTRICTED`)
- All other non-allowlisted AP paths remain `403 SETUP_PLANE_RESTRICTED` via `onNotFound`

## Architecture

- `SetupRouterValidator` — RouterOS API validation (wraps `RouterOsClient`, no logic in SetupServer)
- `SetupRouterConnectionManager` — persistence + save/test orchestration
- `CredentialProtector` — isolated device-bound encryption abstraction

Validation uses bounded timeouts (`SETUP_ROUTER_CONNECT_TIMEOUT_MS=2500`,
`SETUP_ROUTER_IO_TIMEOUT_MS=3500`).

## Files changed

| File | Change |
|------|--------|
| `src/CredentialProtector.h/.cpp` | **New** — device-bound secret protection |
| `src/SetupRouterValidator.h/.cpp` | **New** — RouterOS API validation |
| `src/SetupRouterConnectionManager.h/.cpp` | **New** — router-connection.json |
| `src/web/SetupServer.cpp` | Step 4 UI + router API routes + favicon |
| `src/web/PortalServer.cpp` | Block setup-plane SPIFFS leak |
| `src/web/AdminServer.cpp` | Favicon 204 on setup plane |
| `src/Config.h` | Setup router timeouts |
| `src/StoragePaths.h` | `RouterConnectionFile` |
| `src/FirmwareApp.h/.cpp` | Boot `SetupRouterConnectionManager` |
| `src/web/WebServerManager.h/.cpp` | Wire dependencies |
| `docs/HTTP_ROUTE_CONTRACT.md` | Route table |
| `docs/NETWORK_PLANE_ARCHITECTURE.md` | Allowlist + routes |
| `docs/MOBILE_APP_API_CONTRACT.md` | Mobile setup contract |
