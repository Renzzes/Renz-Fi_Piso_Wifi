# Provisioning REST API

**Status:** Frozen workflow API for Phase 6B Setup Wizard  
**Workflow contract:** [INSTALLATION_WORKFLOW.md](./INSTALLATION_WORKFLOW.md)  
**UI interaction contract:** [SETUP_WIZARD_UI_CONTRACT.md](./SETUP_WIZARD_UI_CONTRACT.md)  
**Owner:** `ProvisioningServer` → `ProvisioningEngine`  
**Prefix:** `/api/provisioning/*`  
**Authentication:** Admin session cookie

---

## Architecture

```
Admin Setup Wizard
        ↓
/api/provisioning/*     (this document)
        ↓
ProvisioningEngine      (workflow operations — no manager names)
        ↓
Internal managers
```

Response envelope (all endpoints): `{ "success", "data", "message" }`

Every successful workflow response includes in `data`:
- `installation` — state, progress, **session** block (`sessionId`, `elapsedMinutes`, etc.)
- `workflowStep` — UI routing hint (see INSTALLATION_WORKFLOW.md)
- `ok` — operation result

### Begin installation body (optional)

```json
{
  "installerName": "Field Tech",
  "isRecovery": false
}
```

### SSE events

Subscribe to `/api/events` for:
- `installation.progress` — `{ step, percent, message, sessionId }`
- `installation.state_changed` / `installation.completed` / `installation.aborted`

---

## Endpoints

### Installation lifecycle

| Method | Route | Engine operation |
|--------|-------|------------------|
| `POST` | `/api/provisioning/installation/begin` | `beginInstallation()` |
| `GET` | `/api/provisioning/installation/resume` | `resumeInstallation()` |
| `POST` | `/api/provisioning/installation/abort` | `abortInstallation()` |
| `POST` | `/api/provisioning/installation/factory-reset` | `factoryReset()` |

### Router workflow

| Method | Route | Engine operation |
|--------|-------|------------------|
| `GET` | `/api/provisioning/routers/detect` | `detectRouters()` |
| `POST` | `/api/provisioning/routers/select` | `selectDriver()` |
| `POST` | `/api/provisioning/routers/connect` | `connectRouter()` |
| `GET` | `/api/provisioning/routers/profiles` | `listRouterProfiles()` |

### Appliance configuration

| Method | Route | Engine operation |
|--------|-------|------------------|
| `POST` | `/api/provisioning/portal/configure` | `configurePortal()` |
| `POST` | `/api/provisioning/coin/configure` | `configureCoin()` |

### Validation & finish

| Method | Route | Engine operation |
|--------|-------|------------------|
| `POST` | `/api/provisioning/validate` | `validateInstallation()` |
| `POST` | `/api/provisioning/finish` | `finalizeInstallation()` |

---

## Example: select driver

**Request** `POST /api/provisioning/routers/select`

```json
{
  "driverId": "mikrotik",
  "firmware": "RouterOS",
  "version": "7.12",
  "host": "10.40.0.1",
  "username": "admin",
  "profile": "default"
}
```

**Response** (success)

```json
{
  "success": true,
  "data": {
    "ok": true,
    "driverId": "mikrotik",
    "manifest": { "...": "..." },
    "workflowStep": "driver_selection",
    "installation": {
      "state": "router_selected",
      "nextState": "router_connected",
      "completedSteps": ["router"]
    }
  }
}
```

---

## Legacy routes

`/api/router/*` in `ApiServer` remains for the existing admin dashboard. **The Setup Wizard must use this API only.**
