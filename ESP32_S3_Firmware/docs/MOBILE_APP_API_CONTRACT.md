# Renz-Fi Manager Mobile App — API Contract (Setup Plane)

**Status:** Phase 2C — setup endpoints implemented on Management AP  
Related docs:

- [HTTP_ROUTE_CONTRACT.md](./HTTP_ROUTE_CONTRACT.md)
- [NETWORK_PLANE_ARCHITECTURE.md](./NETWORK_PLANE_ARCHITECTURE.md)
- [PHASE_2C_IMPLEMENTATION_REPORT.md](./PHASE_2C_IMPLEMENTATION_REPORT.md)

---

## 1. Network handoff

1. **Factory setup:** phone connects to `Renz-Fi Setup` and talks to `http://192.168.4.1`.
2. **After setup:** phone uses the ESP32 Ethernet/LAN address through MikroTik (e.g. `http://10.10.10.2`).

No cloud dependencies.

---

## 2. Management AP endpoints (Renz-Fi Setup)

Use **only** these URLs while joined to `Renz-Fi Setup` (`192.168.4.1`):

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/healthz` | Reachability probe |
| GET | `/api/setup/status` | Wizard status + installation state |
| POST | `/api/setup/owner` | Create owner account (first run) |
| GET | `/api/setup/router-status` | Ethernet/DHCP detection |
| GET | `/api/setup/router-config` | Saved router metadata (**no password**; includes `hasSavedPassword`) |
| POST | `/api/setup/router/test` | Test MikroTik RouterOS API login (blank password uses saved credential) |
| POST | `/api/setup/router/save` | Save validated router connection (blank password preserves saved credential) |
| GET | `/api/setup/router-plan` | Read-only MikroTik provisioning plan (default settings) |
| POST | `/api/setup/router-plan` | Preview plan with optional `{ "settings": { ... } }` |
| POST | `/api/setup/router-apply` | Apply guest bridge + DHCP foundation only (no Hotspot activation) |

Do **not** call production routes (`/api/health`, `/api/auth/*`, `/admin`, etc.) from the Management AP.

---

## 3. Security rules (mobile app)

- The app must **never** receive, store, or display a saved router password from the device.
- `GET /api/setup/router-config` returns only: `routerType`, `host`, `apiPort`, `username`, `connectionVerified`, `verifiedAt`, `hasSavedPassword`.
- Password fields are **write-only** on `POST /api/setup/router/test` and `POST /api/setup/router/save`.
- When `hasSavedPassword` is true, an empty `password` in test/save uses the saved encrypted credential without returning it to the client.
- Clear password fields from memory after each request.

---

## 4. Response models

### GET `/api/setup/status`

```json
{
  "success": true,
  "data": {
    "deviceId": "RF-A1B2C3",
    "firmwareVersion": "0.5.0-w5500",
    "installationState": "owner_created",
    "ownerCreated": true,
    "wizardStep": "router_check",
    "ethernet": { "link": true, "hasIp": true, "ip": "10.10.10.2" },
    "storage": { "ok": true, "sdMounted": true }
  }
}
```

### GET `/api/setup/router-config`

```json
{
  "success": true,
  "data": {
    "routerType": "mikrotik",
    "host": "10.10.10.1",
    "apiPort": 8728,
    "username": "admin",
    "connectionVerified": false,
    "verifiedAt": 0,
    "hasSavedPassword": false
  }
}
```

### POST `/api/setup/router/test` body

```json
{
  "host": "10.10.10.1",
  "apiPort": 8728,
  "username": "admin",
  "password": "<write-only; omit or empty to use saved credential when hasSavedPassword>"
}
```

Success:

```json
{
  "success": true,
  "message": "RouterOS API connection validated",
  "data": {
    "validationCode": "ROUTER_VALIDATED",
    "routerIdentity": "MikroTik"
  }
}
```

### POST `/api/setup/router/save`

Same request body as test. On success:

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

---

## 5. Error codes

| Code | Meaning |
|------|---------|
| `SETUP_PLANE_RESTRICTED` | Route called from Ethernet instead of Management AP |
| `SETUP_OWNER_REQUIRED` | Owner account not created yet |
| `INVALID_HOST` | Router IP/port invalid |
| `INVALID_PASSWORD` | Password missing and no saved credential to reuse |
| `CREDENTIAL_UNAVAILABLE` | Saved encrypted password could not be loaded |
| `ETHERNET_NOT_READY` | Renz-Fi Ethernet link or DHCP not ready |
| `TCP_CONNECT_FAILED` | Cannot open TCP to RouterOS API port |
| `API_LOGIN_FAILED` | RouterOS login rejected |
| `ROUTEROS_API_UNAVAILABLE` | Connected but identity query failed |
| `ROUTEROS_API_UNSUPPORTED` | RouterOS API fatal/unsupported response |
| `ROUTER_VALIDATED` | Success code on test/save validation |
| `STORAGE_WRITE_FAILED` | Could not persist router-connection.json |

---

## 6. Production plane (after setup)

On Ethernet/LAN (`http://10.10.10.2` or DHCP address):

- `GET /api/health` — full system health
- Admin dashboard at `/admin` (not available on Management AP)
