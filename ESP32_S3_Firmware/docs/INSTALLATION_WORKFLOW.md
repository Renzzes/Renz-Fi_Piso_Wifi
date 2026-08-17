# Renz-Fi Installation Workflow Contract (Frozen)

**Status:** Frozen — contract between ESP32 firmware and React Setup Wizard  
**Companion:** [PROVISIONING_API.md](./PROVISIONING_API.md) (HTTP mapping)  
**Architecture:** `Setup Wizard → Provisioning REST API → ProvisioningEngine → (internal managers)`

The wizard must use **workflow operations only**. It must never call `RouterPlatform`, `CoinManager`, `AssetManager`, or other managers directly.

---

## 1. Workflow overview

```
Factory (persisted)
    ↓
Welcome
    ↓
Router Detection
    ↓
Driver Selection
    ↓
Router Connection
    ↓
Portal Configuration
    ↓
Coin Configuration
    ↓
Validation
    ↓
Summary
    ↓
Ready (persisted)
```

### UI step ↔ persisted state

| UI step | `workflowStep` (API) | Installation state (after success) |
|---------|----------------------|-------------------------------------|
| Welcome | `welcome` | `factory` |
| Router Detection | `router_detection` | (no transition — informational) |
| Driver Selection | `driver_selection` | `router_selected` |
| Router Connection | `router_connection` | `router_connected` |
| Portal Configuration | `portal_configuration` | `portal_configured` |
| Coin Configuration | `coin_configuration` | `coin_configured` |
| Validation | `validation` | `validation_passed` |
| Summary | `summary` | `validation_passed` |
| Complete | `ready` | `ready` |

On reboot, call `GET /api/provisioning/installation/resume` — firmware returns `installation`, `workflowStep`, `nextState`, and `completedSteps`.

---

## 2. ProvisioningEngine operations

| Operation | Purpose |
|-----------|---------|
| `beginInstallation()` | Start wizard session |
| `resumeInstallation()` | Resume after reboot |
| `detectRouters()` | List drivers + detection metadata |
| `selectDriver()` | Choose driver + optional settings + firmware check |
| `connectRouter()` | Save credentials + connection test |
| `configurePortal()` | Apply/verify portal defaults |
| `configureCoin()` | Verify hardware + apply coin defaults |
| `validateInstallation()` | Run all checks |
| `finalizeInstallation()` | Mark ready + summary + SSE |
| `abortInstallation()` | Exit wizard (state preserved) |
| `factoryReset()` | Reset to factory |

---

## 3. Step specifications

### 3.1 Welcome

| Field | Value |
|-------|-------|
| **Purpose** | Introduce installer; confirm appliance is ready for setup |
| **Required inputs** | None |
| **Validation** | Admin authenticated |
| **API** | `POST /api/provisioning/installation/begin` |
| **State transition** | None (remains `factory` until driver selected) |
| **Rollback** | `POST /api/provisioning/installation/abort` — no state change |
| **Success criteria** | `{ "started": true }`; if already configured `{ "alreadyReady": true }` |

---

### 3.2 Router Detection

| Field | Value |
|-------|-------|
| **Purpose** | Present registered drivers and detection hints for installer |
| **Required inputs** | None |
| **Validation** | Admin authenticated |
| **API** | `GET /api/provisioning/routers/detect` |
| **State transition** | None |
| **Rollback** | N/A (read-only) |
| **Success criteria** | `available[]` driver manifests; `drivers[]` detection entries with `detected`, `confidence`, `reason` |

**Phase 6B completion:**
- Driver detection returns all registered drivers
- Detection metadata per driver (live probe deferred where not implemented)

---

### 3.3 Driver Selection

| Field | Value |
|-------|-------|
| **Purpose** | Select router driver; reject unsupported firmware early |
| **Required inputs** | `driverId` (required); optional `firmware`, `version`, router settings (`host`, `username`, `password`, `profile`) |
| **Validation** | Known `driverId`; firmware compatibility when `firmware`/`version` supplied |
| **API** | `POST /api/provisioning/routers/select` |
| **State transition** | → `router_selected` |
| **Rollback** | Re-run selection with different driver; `factory-reset` clears all |
| **Success criteria** | `{ "ok": true, "driverId", "manifest" }`; unsupported firmware returns error with human-readable `reason` |

**Phase 6B completion:**
- Driver selection
- Compatibility check
- Advance installation state

---

### 3.4 Router Connection

| Field | Value |
|-------|-------|
| **Purpose** | Save router credentials and verify API/hotspot connectivity |
| **Required inputs** | `host`, `username`, `password`, `profile` (driver-specific; may be pre-saved from select step) |
| **Validation** | Non-empty credentials; RouterOS test passes (MikroTik); profile exists |
| **API** | `POST /api/provisioning/routers/connect`; helper `GET /api/provisioning/routers/profiles` |
| **State transition** | → `router_connected` on successful test |
| **Rollback** | Re-submit connect with corrected credentials; state stays below `router_connected` until success |
| **Success criteria** | `{ "connected": true, "ok": true }` + test payload (`identity`, `profileFound`, etc.) |

**Phase 6B completion:**
- Connection test
- Advance installation state

---

### 3.5 Portal Configuration

| Field | Value |
|-------|-------|
| **Purpose** | Confirm captive portal is served with default/bundled branding |
| **Required inputs** | Optional body (reserved for custom title/theme in future phases) |
| **Validation** | `fillBrandingJson` succeeds; portal meta loads |
| **API** | `POST /api/provisioning/portal/configure` |
| **State transition** | → `portal_configured` |
| **Rollback** | Re-run configure; does not remove uploaded assets |
| **Success criteria** | `{ "verified": true, "revision", "hasBanner", "hasMusic" }` |

**Phase 6B completion:**
- Apply default configuration (verify bundled/SPIFFS portal)
- Verify configuration
- Advance installation state

---

### 3.6 Coin Configuration

| Field | Value |
|-------|-------|
| **Purpose** | Verify coin acceptor hardware and persist default coin settings |
| **Required inputs** | Optional `coin` object (pulsesPerPeso, debounceMs, enabled, etc.) |
| **Validation** | Diagnostics run; no hardware fault (`isFault` false). Skipped when coin manager disabled in build |
| **API** | `POST /api/provisioning/coin/configure` |
| **State transition** | → `coin_configured` |
| **Rollback** | Re-run with updated settings |
| **Success criteria** | `{ "hardwareOk": true }` or `{ "skipped": true }` when coin disabled |

**Phase 6B completion:**
- Verify coin hardware
- Configure defaults
- Advance installation state

---

### 3.7 Validation

| Field | Value |
|-------|-------|
| **Purpose** | Execute installation checks before marking appliance ready |
| **Required inputs** | None |
| **Validation** | All checks in `checks[]` must pass |
| **API** | `POST /api/provisioning/validate` |
| **State transition** | → `validation_passed` when all checks pass |
| **Rollback** | Fix failing subsystem; re-run validate (monotonic — state does not regress automatically) |
| **Success criteria** | `{ "passed": true, "checks": [{ "id", "passed", "detail" }] }` |

**Checks (Phase 6B):**

| Check ID | Rule |
|----------|------|
| `router_connected` | State ≥ `router_connected` |
| `portal_configured` | Portal branding JSON fills |
| `coin_ready` | Coin diagnostics OK or coin disabled |
| `storage_healthy` | SD storage healthy |

**Phase 6B completion:**
- Execute installation checks
- Produce result
- Advance to `validation_passed`

---

### 3.8 Summary & Finish

| Field | Value |
|-------|-------|
| **Purpose** | Present installation summary; mark appliance ready for production |
| **Required inputs** | None (requires validation passed) |
| **Validation** | State ≥ `validation_passed` |
| **API** | `POST /api/provisioning/finish` |
| **State transition** | → `ready` |
| **Rollback** | `factory-reset` only (destructive) |
| **Success criteria** | `{ "finished": true, "summary": { router, portal, coin, firmwareVersion } }`; SSE `installation.completed` |

**Phase 6B completion:**
- Set Ready
- Generate installation summary
- Emit completion event

---

## 4. Lifecycle operations

### Resume (reboot / power loss)

| Field | Value |
|-------|-------|
| **API** | `GET /api/provisioning/installation/resume` |
| **Behavior** | Returns persisted `installation` block + `workflowStep` so UI jumps to correct screen |
| **Persistence** | `/config/installation.json` — `state`, `completedSteps`, `firmwareVersion`, `installationVersion` |

### Abort

| Field | Value |
|-------|-------|
| **API** | `POST /api/provisioning/installation/abort` |
| **Behavior** | Exit wizard; **preserve** installation state for resume |
| **SSE** | `installation.aborted` |

### Factory reset

| Field | Value |
|-------|-------|
| **API** | `POST /api/provisioning/installation/factory-reset` |
| **Behavior** | Reset installation state to `factory`; clear completed steps |
| **Note** | Does not erase router credentials or portal assets (installer may re-run wizard) |

---

## 5. Events (SSE `/api/events`)

| Event | When |
|-------|------|
| `installation.state_changed` | Any installation state advance |
| `installation.progress` | Workflow step activity (UI progress bar) |
| `installation.completed` | `finalizeInstallation()` success |
| `installation.aborted` | `abortInstallation()` |

### `installation.progress` payload

```json
{
  "step": "connect_router",
  "percent": 35,
  "message": "Testing router connection...",
  "sessionId": "ins-AA:BB:CC-..."
}
```

Clients (React, Android, future LCD) should subscribe to this event for consistent progress display.

---

## 6. Installation session

Persisted in `/config/installation.json` under `session`:

```json
{
  "sessionId": "ins-DE:AD:BE-0012a3b4-9f2a",
  "startedAt": 120000,
  "lastActivity": 960000,
  "installerName": "Juan",
  "deviceId": "DE:AD:BE:EF:00:01",
  "resumeToken": "a1b2c3d4",
  "isRecovery": false,
  "attempt": 1
}
```

| Field | Purpose |
|-------|---------|
| `sessionId` | Unique wizard session identifier |
| `startedAt` / `lastActivity` | Timestamps (millis) for resume prompts |
| `installerName` | Optional — from `begin` request body |
| `deviceId` | Appliance MAC (W5500) |
| `resumeToken` | Optional short token for future mobile/LCD pairing |
| `isRecovery` | True when launched from recovery flow |
| `attempt` | Session attempt counter |

**Resume UX:** `GET /api/provisioning/installation/resume` returns `session.elapsedMinutes` and `resumePrompt: true` so the UI can show *"Resume installation started 14 minutes ago?"*

Schema version: `installationVersion: 2` (auto-migrates v1 files).

---

## 7. Persistence schema

```json
{
  "state": "portal_configured",
  "updatedAt": 123456,
  "completedSteps": ["router", "portal"],
  "firmwareVersion": "0.5.0-w5500",
  "installationVersion": 2,
  "session": {
    "sessionId": "ins-...",
    "startedAt": 120000,
    "lastActivity": 840000,
    "installerName": "",
    "deviceId": "DE:AD:BE:EF:00:01",
    "resumeToken": "a1b2c3d4",
    "isRecovery": false,
    "attempt": 1,
    "elapsedMinutes": 12
  }
}
```

Schema migrations use `installationVersion` (see `INSTALLATION_SCHEMA_VERSION` in firmware).

---

## 7. Dependency rules (frozen)

```
React Setup Wizard
        ↓  (workflow API only)
ProvisioningServer  /api/provisioning/*
        ↓
ProvisioningEngine
        ↓  (internal — never exposed)
RouterPlatform · InstallationStateManager · PortalConfigManager · CoinManager · StorageManager
```

- Legacy `/api/router/*` remains for existing admin dashboard — **new wizard must not use it**
- Portal guest APIs unchanged
- HTTP route ownership: `ProvisioningServer` owns `/api/provisioning/*`

---

## 8. Phase 6B acceptance checklist

| Area | Criterion |
|------|-----------|
| **Router** | Detection, selection, compatibility check, connection test, state advance |
| **Portal** | Default config verify, state advance |
| **Coin** | Hardware verify, defaults, state advance (or skip when disabled) |
| **Validation** | All checks, result payload, advance to `validation_passed` |
| **Finish** | Ready state, summary, `installation.completed` event |
| **Resume** | Reboot resumes at correct `workflowStep` |
| **Contract** | This document + PROVISIONING_API.md + SETUP_WIZARD_UI_CONTRACT.md match implemented firmware |

---

## 9. React wizard guidance

1. On mount: `resume` → if `ready`, redirect to dashboard; else show `workflowStep`
2. Linear flow with back navigation UI-only (call previous step screen; do not regress persisted state)
3. Use `installation.nextState` from API for progress bar — never hardcode step order
4. On firmware mismatch: show `reason` from select/connect before allowing continue
5. Only call `/api/provisioning/*` — never manager-specific endpoints
