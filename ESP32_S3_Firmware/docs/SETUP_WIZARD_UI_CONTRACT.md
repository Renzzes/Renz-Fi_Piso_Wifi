# Renz-Fi Setup Wizard — UI Interaction Contract (Frozen)

**Status:** Frozen — contract between React Setup Wizard and Provisioning REST API  
**Not firmware.** **Not component code.** Interaction only.

**Companion documents:**

| Document | Role |
|----------|------|
| [INSTALLATION_WORKFLOW.md](./INSTALLATION_WORKFLOW.md) | Firmware workflow + state machine |
| [PROVISIONING_API.md](./PROVISIONING_API.md) | HTTP endpoints + envelopes |
| [HTTP_ROUTE_CONTRACT.md](./HTTP_ROUTE_CONTRACT.md) | General web platform (auth, SSE) |

**Rule:** The wizard calls **`/api/provisioning/*` only**. Never `/api/router/*`, never manager-specific routes.

**Auth:** All provisioning endpoints require an admin session cookie (same as existing admin dashboard login).

**SSE:** Subscribe to `GET /api/events` on wizard mount. Listen for `installation.progress`, `installation.state_changed`, `installation.completed`, `installation.aborted`.

**Response envelope:** `{ "success", "data", "message" }` — UI reads **`data`** for screen logic.

---

## 1. Screen flow

```
[Bootstrap / Resume Gate]
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
Complete (Ready)
```

**Progress bar:** Use `data.installation.progressPercent` and `data.installation.nextState` from API responses — do not hardcode step order.

**Back button:** UI navigation only. Persisted installation state is **monotonic** — going back does not regress firmware state.

---

## 2. Global behaviors

### 2.1 Bootstrap / Resume Gate (no dedicated screen route required)

| Field | Specification |
|-------|----------------|
| **Purpose** | Decide initial screen after login; handle power-loss resume |
| **API calls** | `GET /api/provisioning/installation/resume` (always on wizard mount) |
| **Inputs** | None |
| **Success** | Route to screen matching `data.workflowStep` (see §3 mapping table) |
| **Failure** | If 401 → redirect to admin login. If 5xx → show retry + "Unable to load installation status" |
| **Resume** | If `data.resumed && data.resumePrompt` → show modal: *"Resume installation started {elapsedMinutes} minutes ago?"* with **Continue** / **Start over** (Start over → `POST /api/provisioning/installation/factory-reset` then Welcome) |
| **Ready redirect** | If `data.installation.ready === true` → exit wizard to admin dashboard |

### 2.2 Abort (available on every screen)

| Field | Specification |
|-------|----------------|
| **API** | `POST /api/provisioning/installation/abort` |
| **Behavior** | Exit wizard to dashboard; **preserve** installation state |
| **SSE** | `installation.aborted` |
| **UI copy** | "Setup paused. You can resume later from the admin menu." |

### 2.3 Factory reset (Welcome + Resume modal only)

| Field | Specification |
|-------|----------------|
| **API** | `POST /api/provisioning/installation/factory-reset` |
| **Behavior** | Clears installation state + session; returns to Welcome |
| **UI copy** | Confirm dialog: "This will reset setup progress. Router settings and portal files are not erased." |

---

## 3. `workflowStep` → screen routing

| `data.workflowStep` | Render screen |
|---------------------|---------------|
| `welcome` | Welcome |
| `router_detection` | Router Detection *(or skip — see screen spec)* |
| `driver_selection` | Driver Selection |
| `router_connection` | Router Connection |
| `portal_configuration` | Portal Configuration |
| `coin_configuration` | Coin Configuration |
| `validation` | Validation *(or Summary if checks already passed)* |
| `summary` | Summary |
| `ready` | Complete → redirect dashboard |

**State-based skip (resume):** If `installation.state` is already past a step, skip forward per each screen's **Resume behavior** below.

---

## 4. Screen specifications

---

### Screen: Welcome

| Field | Specification |
|-------|----------------|
| **Purpose** | Introduce installer; start or acknowledge installation session |
| **API calls** | `POST /api/provisioning/installation/begin` on **Start setup** |
| **Required inputs** | Optional: `installerName` (text), `isRecovery` (boolean, default false) |
| **Validation messages** | None required before submit |
| **Progress events** | `installation.progress` → `step: "welcome"`, message: *"Installation workflow started"* |
| **Success transition** | If `data.alreadyReady === true` → redirect dashboard. Else → **Router Detection** |
| **Failure behavior** | Show `message` or `error` from envelope. Button: **Retry** (re-call begin) |
| **Resume behavior** | Skip entire Welcome if `installation.state !== "factory"` and user chose **Continue** on resume modal → route via `workflowStep` |

**Request body example:**

```json
{ "installerName": "Field Tech", "isRecovery": false }
```

---

### Screen: Router Detection

| Field | Specification |
|-------|----------------|
| **Purpose** | Show available router drivers and detection hints |
| **API calls** | `GET /api/provisioning/routers/detect` on mount |
| **Required inputs** | None |
| **Validation messages** | N/A (read-only) |
| **Progress events** | `step: "detect_routers"`, message: *"Scanning for router drivers"* |
| **Success transition** | Display `data.available[]` + `data.drivers[]` → enable **Continue** → **Driver Selection** |
| **Failure behavior** | Show: *"Unable to detect router drivers."* Button: **Retry** (re-call detect) |
| **Resume behavior** | **Skip** if `installation.state` ≥ `router_selected` (i.e. `router_selected`, `router_connected`, …) → go to **Router Connection** or `workflowStep` |

**Display:**

- Driver name, vendor, stability (`stable` / `experimental`)
- Detection: `confidence`, `reason`, `configured` flag per driver

---

### Screen: Driver Selection

| Field | Specification |
|-------|----------------|
| **Purpose** | Choose router driver; block unsupported firmware before connection |
| **API calls** | `POST /api/provisioning/routers/select` on submit |
| **Required inputs** | `driverId` (required). Recommended: `firmware`, `version` when known. Optional pre-fill: `host`, `username`, `password`, `profile` |
| **Validation messages** | Client-side: *"Please select a router driver."* Server: show `data.error` or `data.reason` — e.g. *"Firmware version 5.2 is below the minimum supported 6.0"* |
| **Progress events** | `step: "select_driver"`, message: *"Selecting router driver"* |
| **Success transition** | → **Router Connection** (show manifest summary from `data.manifest`) |
| **Failure behavior** | Inline error from `data.error`. Buttons: **Retry**, **Change driver** |
| **Resume behavior** | **Skip** if `installation.state` ≥ `router_connected` → **Portal Configuration** |

**Request body example:**

```json
{
  "driverId": "mikrotik",
  "firmware": "RouterOS",
  "version": "7.12",
  "host": "10.40.0.1"
}
```

---

### Screen: Router Connection

| Field | Specification |
|-------|----------------|
| **Purpose** | Collect credentials; verify router API and hotspot profile |
| **API calls** | `GET /api/provisioning/routers/profiles` on mount (populate profile dropdown). `POST /api/provisioning/routers/connect` on submit |
| **Required inputs** | `host`, `username`, `password`, `profile` |
| **Validation messages** | Client-side: *"Router IP is required."* / *"Username is required."* / *"Password is required."* / *"Hotspot profile is required."* Server: show `data.error` — e.g. *"Hotspot profile not found: default"*, *"RouterOS API password is not configured"* |
| **Progress events** | `step: "connect_router"`, message: *"Testing router connection"* (also emitted during profiles load) |
| **Success transition** | Show identity from response (`identity`, `profileFound`) → **Portal Configuration** |
| **Failure behavior** | Show test failure details from `data`. Buttons: **Retry**, **Edit credentials** |
| **Resume behavior** | **Skip** if `installation.state` ≥ `portal_configured` → **Coin Configuration** or next incomplete step |

**Success indicator:** `data.connected === true && data.ok === true`

---

### Screen: Portal Configuration

| Field | Specification |
|-------|----------------|
| **Purpose** | Confirm captive portal is ready with default/bundled branding |
| **API calls** | `POST /api/provisioning/portal/configure` on **Verify portal** (or auto on mount) |
| **Required inputs** | None (Phase 6C). Body reserved for future custom branding |
| **Validation messages** | Server: *"Portal branding verification failed"* / *"Unable to load portal configuration"* |
| **Progress events** | `step: "configure_portal"`, message: *"Applying portal configuration"* |
| **Success transition** | Show `revision`, `hasBanner`, `hasMusic` → **Coin Configuration** |
| **Failure behavior** | Show error. Buttons: **Retry** |
| **Resume behavior** | **Skip** if `installation.state` ≥ `coin_configured` → **Validation** |

---

### Screen: Coin Configuration

| Field | Specification |
|-------|----------------|
| **Purpose** | Verify coin acceptor hardware; apply default coin settings |
| **API calls** | `POST /api/provisioning/coin/configure` on submit |
| **Required inputs** | Optional `coin` object: `pulsesPerPeso`, `debounceMs`, `settleMs`, `defaultMinutesPerPeso`, `enabled` |
| **Validation messages** | Server: *"Coin hardware fault detected"* / *"Coin diagnostics unavailable"*. If `data.skipped === true`: show info *"Coin hardware disabled in this firmware build — step skipped."* |
| **Progress events** | `step: "configure_coin"`, message: *"Configuring coin acceptor"* |
| **Success transition** | If `hardwareOk` or `skipped` → **Validation** |
| **Failure behavior** | Show hardware diagnostics from `data.hardware`. Buttons: **Retry**, **Adjust settings** |
| **Resume behavior** | **Skip** if `installation.state` ≥ `validation_passed` → **Summary** |

**Default coin form (suggested defaults for UI):**

| Field | Default |
|-------|---------|
| `pulsesPerPeso` | 1 |
| `defaultMinutesPerPeso` | 5 |
| `debounceMs` | 35 |
| `enabled` | true |

---

### Screen: Validation

| Field | Specification |
|-------|----------------|
| **Purpose** | Run installation checks; show pass/fail per subsystem |
| **API calls** | `POST /api/provisioning/validate` on mount or **Run checks** |
| **Required inputs** | None |
| **Validation messages** | Per check: show `checks[].detail` when `passed === false`. Global: *"One or more installation checks failed"* |
| **Progress events** | `step: "validate"`, message: *"Running installation checks"* |
| **Success transition** | All checks pass (`data.passed === true`) → **Summary** |
| **Failure behavior** | List failed checks with `id` + `detail`. Buttons: **Retry**, **Go to step** (navigate UI to failing section — no API state regression) |
| **Resume behavior** | **Skip** if `installation.state === "validation_passed"` → **Summary** |

**Check IDs to display:**

| ID | User-facing label |
|----|-------------------|
| `router_connected` | Router connection |
| `portal_configured` | Portal configuration |
| `coin_ready` | Coin acceptor |
| `storage_healthy` | Storage (SD card) |

---

### Screen: Summary

| Field | Specification |
|-------|----------------|
| **Purpose** | Review installation before marking appliance production-ready |
| **API calls** | None required (display from last validate response + session). Optional refresh: `GET /api/provisioning/installation/resume` |
| **Required inputs** | None |
| **Validation messages** | If state < `validation_passed`: *"Complete validation before finishing."* Disable **Finish** |
| **Progress events** | None (display only) |
| **Success transition** | User taps **Finish setup** → call finish screen API |
| **Failure behavior** | N/A |
| **Resume behavior** | Show Summary when `workflowStep === "summary"` or `state === "validation_passed"` |

**Display:** `installation.completedSteps`, `installation.session`, driver/portal/coin summary from prior steps.

---

### Screen: Complete (Finish)

| Field | Specification |
|-------|----------------|
| **Purpose** | Finalize installation; show success summary |
| **API calls** | `POST /api/provisioning/finish` on **Finish setup** (from Summary) |
| **Required inputs** | None |
| **Validation messages** | Server: *"Validation must pass before finishing installation"* |
| **Progress events** | `step: "finish"`, message: *"Finalizing installation"*. Then SSE: `installation.completed` |
| **Success transition** | Show `data.summary` (router, portal, coin, firmwareVersion) → **Go to dashboard** button |
| **Failure behavior** | Show error. Button: **Back to validation** |
| **Resume behavior** | If `installation.ready === true` → show Complete screen or redirect dashboard |

---

## 5. SSE progress display (all screens)

Subscribe once on wizard mount. On `installation.progress`:

| Field | UI use |
|-------|--------|
| `step` | Optional step indicator highlight |
| `percent` | Global progress bar (0–100) |
| `message` | Status line / spinner caption |
| `sessionId` | Debug/support (optional display) |

On `installation.state_changed`: refresh local `installation` state from next API response or merge `state` + `progressPercent` from event payload.

On `installation.completed`: navigate to Complete screen or dashboard.

---

## 6. Error code reference

| HTTP | `code` | UI action |
|------|--------|-----------|
| 401 | `UNAUTHENTICATED` | Redirect to admin login |
| 400 | `PROVISIONING_ERROR` | Show `error` / `data.error`; enable Retry |
| 500 | `PROVISIONING_ERROR` | Show generic failure + Retry |
| 404 | `NOT_FOUND` | Driver/manifest not found — return to Driver Selection |

---

## 7. Phase 6C implementation checklist (React)

| # | Task |
|---|------|
| 1 | Wizard route guarded by admin auth |
| 2 | Mount: SSE connect + `installation/resume` |
| 3 | Resume modal when `resumePrompt === true` |
| 4 | One screen component per §4 |
| 5 | Progress bar driven by `installation.progressPercent` |
| 6 | Status line driven by `installation.progress` SSE |
| 7 | Skip logic per screen **Resume behavior** |
| 8 | No calls outside `/api/provisioning/*` |
| 9 | Ready → exit wizard to dashboard |
| 10 | Abort + factory-reset wired on Welcome / header |

---

## 8. Document index

| Layer | Document |
|-------|----------|
| UI interaction (this file) | `SETUP_WIZARD_UI_CONTRACT.md` |
| Firmware workflow | `INSTALLATION_WORKFLOW.md` |
| HTTP API | `PROVISIONING_API.md` |
| Web platform | `HTTP_ROUTE_CONTRACT.md` |

**This document is frozen for Phase 6C React development.** Changes require explicit contract revision.
