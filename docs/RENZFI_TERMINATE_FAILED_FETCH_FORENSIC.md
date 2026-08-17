# Renz-Fi Terminate “Failed to fetch” — Forensic Investigation

**Date:** 2026-08-11  
**Mode:** READ-ONLY investigation (Outcome F — no implementation)  
**Status:** Root cause **NOT YET PROVEN** to a single mechanism  

**NO CODE CHANGES IN THIS PASS.**  
**NO MIKROTIK CHANGES.**  
**NO FIRMWARE FLASH.**  
**NO PORTAL REBUILD AS A “FIX”.**

---

## Incident

| Item | Value |
|------|--------|
| User action | TERMINATE SESSION → confirm **Terminate now** |
| Visible error | `Failed to fetch — your session was not changed.` |
| Same client | Guest `10.20.0.253` → ESP32 `10.10.10.2` |
| Working | `GET /api/portal/session`, `GET /api/portal/branding` (logged `[portal-api]`) |
| Not observed | `[portal] heap before terminate`, POST terminate handler execution |

---

## Exact first failed boundary (PROVEN)

```text
confirmTerminateBtn click
  → handleTerminateConfirm()
  → terminateSessionAPI() → portalPost → apiPost → fetch(POST JSON)
  → Promise rejects with err.message === "Failed to fetch"
  → UI: err.message + " — your session was not changed."
  ✗ ESP32 POST /api/portal/terminate handler not entered
     (no unconditional [portal] heap before terminate)
```

Do **not** reinterpret `"Failed to fetch"` as an ESP32 application/JSON error. That string is the browser `fetch` TypeError / network failure message. Firmware HTTP errors would surface as `"HTTP …"` or JSON `error` text instead.

---

## Architecture (frozen context)

```text
MikroTik Hotspot (HTML/JS)  origin typically 10.20.0.1 / wifi.renz-fi.local
        │
        │  browser fetch → http://10.10.10.2/api/portal/*
        ▼
ESP32 Ethernet 10.10.10.2  (ApiServer portal routes)
```

Guru Meditation / TWDT baseline remains **FROZEN**. This incident must not be “fixed” by delays, watchdog feeds, storage/RouterOS on `async_tcp`, or MikroTik rule speculation.

---

## Source answers (A–AK)

### A. Call chain (PROVEN)

```text
#terminateBtn click
  → openTerminateModal()          [portal/renzfi-app.js]
#confirmTerminateBtn click
  → handleTerminateConfirm()
  → terminateSessionAPI()
  → portalPost("/terminate", deviceParams())
  → apiPost(path, body)
  → fetch(API_BASE + path, { method:"POST", headers:{Content-Type:application/json}, body:JSON.stringify(...) })
```

Production copies: `Final_Build_Portal/renzfi-app.js`, `deployment/mikrotik-hotspot/renzfi-app.js` (same logic; substituted base URL).

### B. Exact URL (PROVEN in built bundle)

`http://10.10.10.2/api/portal/terminate`  
(`API_BASE = APPLIANCE_BASE_URL + "/api/portal"`, base = `http://10.10.10.2`)

### C. Method (PROVEN)

`POST`

### D. Headers (PROVEN)

```text
Content-Type: application/json
```

No `Authorization`. No `credentials: "include"` (fetch default `same-origin`).

### E. Body (PROVEN)

```json
{ "mac": "<#macAddress text>", "ip": "<#ipAddress text>" }
```

from `deviceParams()` ← `#macAddress` / `#ipAddress` (`$(mac)` / `$(ip)` in `login.html`).

### F. CORS preflight required? (PROVEN)

**Yes**, for MikroTik-origin page → `http://10.10.10.2` with non-simple `Content-Type: application/json` + body. Browser should send `OPTIONS` then, if allowed, `POST`.

### G. OPTIONS response implementation (PROVEN)

`WebResponse::serveOptions`:

| Field | Value |
|-------|--------|
| Status | **204** |
| Body Content-Type | `text/plain` (empty body) |
| `Access-Control-Allow-Origin` | `*` (via `addCorsHeaders`) |
| `Access-Control-Allow-Methods` | `GET, POST, PUT, DELETE, OPTIONS` |
| `Access-Control-Allow-Headers` | `Content-Type, Authorization` |
| `Access-Control-Allow-Private-Network` | **not set** |
| Also | `X-Content-Type-Options`, `X-Frame-Options`, `Referrer-Policy` |

Plus `DefaultHeaders` globally adds `Access-Control-Allow-Origin: *` on every response construction (copied into response header list at `AsyncWebServerResponse` ctor). Explicit `addHeader("Access-Control-Allow-Origin","*")` then runs; library `headerMustBePresentOnce` behavior may keep a single ACAO depending on replace semantics — **duplicate ACAO is possible in principle; not proven as the browser failure cause without Network capture.**

### H. AsyncWebServer path matching (PROVEN for this library)

ESPAsyncWebServer `AsyncURIMatcher` (project libdeps):

- `"/api/portal/*"` → strip `*` → **Prefix** → `path.startsWith("/api/portal/")`
- Therefore **`OPTIONS /api/portal/terminate` matches** the registered OPTIONS handler
- `POST /api/portal/terminate` is a separate **exact** `HTTP_POST` registration
- Method filter: handler requires `(_method & request->method())`

### I. `RENZFI_PORTAL_GATE` (PROVEN)

```cpp
ensureProductionPlane(req)  // else rejectPlane → 403 JSON + CORS via serveErrorJson
```

Applied on OPTIONS `/api/portal/*` and on POST terminate (and session/branding GETs).

### J. OPTIONS can be Serial-invisible? (PROVEN)

**Yes.** Explicit OPTIONS handler: **no** `RequestTimer`, **no** `logPortalApiDebug`, **no** heap line. With `RENZFI_DEBUG_HTTP=0` and only `RENZFI_NETWORK_DIAG` on handlers that call `logRequest`/`logPortalApiDebug`, a successful OPTIONS can leave **no** serial footprint.  
**“No OPTIONS in Serial” ≠ “no OPTIONS on the wire.”**

### K. POST can fail gate before heap log? (PROVEN)

Yes. Order inside POST terminate lambda:

1. `RENZFI_PORTAL_GATE` (may return after 403, **before** `logRequest` / heap)  
2. `logRequest`  
3. `[portal] heap before terminate`

### L. Gate reject response (PROVEN)

`HttpPlaneGate::rejectPlane` → `WebResponse::serveErrorJson` → **403** JSON  
`{ success:false, error:"…", code:"PRODUCTION_PLANE_REQUIRED" | … }` + CORS headers.

Same client already passed production plane for GET session/branding → **HIGH CONFIDENCE** gate is not the terminate-specific issue for this capture.

### M. Transport comparison (PROVEN)

| | session | branding | terminate |
|--|---------|----------|-----------|
| Method | GET | GET | **POST** |
| Content-Type | (none) | (none) | **application/json** |
| Preflight | no | no | **yes** |
| Query/body identity | `?mac&ip` required before fetch | none | body `{mac,ip}` (no pre-fetch MAC check) |
| ESP32 log | `[portal-api]` | `[portal-api]` | heap + `logRequest` (if past gate) |

### N. Heartbeat (PROVEN)

`heartbeatAPI()` → same `portalPost` → same `apiPost` → same JSON POST + CORS preflight class as terminate.  
ESP32: POST `/api/portal/heartbeat`, gate, **no** heap line, `sendOk`.  
**Whether heartbeat succeeds live during the failing session: UNKNOWN (not in capture).**

### O. Must terminate be JSON? (analysis only)

Technically a “simple” POST (e.g. `text/plain` / form) could avoid preflight. **Not recommended without proof** that preflight is the failure; would change API contract. **Do not change yet.**

### P–R. Build pipeline / hashes (PROVEN in repo)

- `scripts/build-mikrotik-portal.mjs`: requires `RENZFI_APPLIANCE_BASE_URL`, substitutes into `renzfi-app.js`, writes `deployment/mikrotik-hotspot/` and syncs `Final_Build_Portal/`
- `portal/renzfi-app.js`: placeholder `__RENZFI_APPLIANCE_BASE_URL__`
- `Final_Build_Portal/renzfi-app.js` **SHA256 ==** `deployment/mikrotik-hotspot/renzfi-app.js` (verified this pass)
- Both built copies: `http://10.10.10.2`

### S. Live MikroTik == Final_Build_Portal? (UNKNOWN)

Repository cannot prove router `hotspot/` bytes. Requires `/file` hash on device.

### T. Cache busting (PROVEN absent)

`login.html`: `<script src="renzfi-app.js"></script>` — **no** query cache-buster. Stale JS = **LIKELY** factor, **not** proven root cause.

### U–V. Manual vs automatic (PROVEN design)

Both intended to load Hotspot `html-directory` assets; API base is appliance URL in built JS. MikroTik remains portal delivery; ESP32 is API. Origins may differ; API base should not.

### W. `$(mac)` / `$(ip)` (PROVEN in HTML)

`login.html` embeds `$(ip)` / `$(mac)`. Hotspot substitutes when serving login. Session GET success in this incident ⇒ substitution worked for that load (**HIGH CONFIDENCE**).

### X–Y. MAC/IP requirements

- Session GET: **requires** MAC before `fetch` (`MAC address unavailable` otherwise)  
- Terminate: sends mac/ip in JSON; **no** pre-fetch MAC guard  
- Session GET succeeded ⇒ MAC identity available (**HIGH CONFIDENCE**)

### Z–AA. MikroTik rules

Documented `hs-unauth` / `hs-input` **return** for `dst-address=10.10.10.2`. GETs prove reachability.  
RouterOS filter/NAT rules are **IP/chain based**, not path-based for `/api/portal/terminate` vs `/session` in the documented config.  
**URL-specific MikroTik block: UNPROVEN / not supported by existing evidence.** Do **not** change MikroTik rules for this incident without new proof.

### AB. Duplicate ACAO (LIKELY possible, UNPROVEN causal)

`DefaultHeaders` ACAO `*` + `addCorsHeaders` ACAO `*`. Library may collapse once-only headers; **causal link to Failed to fetch: UNPROVEN**.

### AC. Content-Type allowed (PROVEN in policy)

`Access-Control-Allow-Headers` includes `Content-Type`.

### AD. Private Network Access (APPLICABLE AS HYPOTHESIS, UNPROVEN)

Portal on `10.20.0.1` / `wifi.renz-fi.local` → API `10.10.10.2` (different private subnet). Some Chromium builds may send PNA preflight and require `Access-Control-Allow-Private-Network: true`.  
**Current `serveOptions` does not send that header.**  
Whether **this** customer browser requires it: **UNPROVEN** without Network panel.

### AE–AF. CORS reject after ESP32 success / silent success

If POST handler ran, `[portal] heap before terminate` would appear. Capture lacks it ⇒ **not** “ESP32 succeeded but browser lied” for handler execution. CORS can still block *visibility* of a response to JS after a request is sent; that would usually still leave ESP32 logs for POST. **PROVEN:** handler not entered.

### AG–AI. `PortalSessionManager::terminateSession` (PROVEN; separate TWDT note)

On HTTP callback path after POST accepted:

- Locks RAM session state, mutates fields, unlocks  
- `enqueueSaveSessions()` / `enqueueWork(ExpireSession, …)` / event emits — **queued**, not synchronous RouterOS in the snippet  
- Durable `writeJson` for sessions occurs on **worker/loop** path for `SaveSessions` (not inline SD in the terminate function body shown)  
- HTTP handler then `getSession` + `sendOk`  

**Current Failed to fetch is not explained by terminateSession business logic** (handler never reached).  
**Separate TWDT note:** enqueue + later ExpireSession/RouterOS must remain off `async_tcp` durable/blocking path (baseline). Do not “fix” that in this incident without proof.

### AJ. Socket close / GM / W5500 as terminate cause

No capture tying Guru Meditation / TWDT / W5500 RX RSR to the terminate click in the provided evidence. **UNPROVEN.**

### AK. Heap/CPU around terminate

Not provided in the incident log window. **UNKNOWN.**

---

## Boundary table

| Boundary | Evidence | Status |
|----------|----------|--------|
| UI confirm runs terminate handler | Error suffix construction after “Ending…” path | **PROVEN** |
| `fetch(POST JSON)` invoked | `err.message === "Failed to fetch"` | **PROVEN** |
| URL/method/headers/body as above | Source + Final_Build | **PROVEN** |
| CORS preflight *required* by request shape | Fetch + cross-origin + JSON CT | **PROVEN** |
| OPTIONS reaches ESP32 | Silent OPTIONS handler; no wire capture | **UNPROVEN** |
| OPTIONS CORS response insufficient | Speculative (PNA / headers) | **UNPROVEN** |
| POST handler entered | Missing `[portal] heap before terminate` | **RULED OUT** (for this capture) |
| Missing POST route registration | Source registers POST | **RULED OUT** |
| Different API base for terminate | Built JS | **RULED OUT** |
| Generic MikroTik block of 10.10.10.2 | GETs work | **RULED OUT** |
| Missing MAC alone | Session GET worked; would not yield “Failed to fetch” | **RULED OUT** |
| ESP32 terminate business logic | Handler not entered | **RULED OUT** (this event) |
| Stale MikroTik JS | No live hash | **LIKELY** / **UNPROVEN** cause |
| Heartbeat POST health | Same `apiPost` | **UNPROVEN** live |
| PNA header missing | Header absent in source | **LIKELY** candidate / **UNPROVEN** cause |
| Duplicate ACAO | Possible | **UNPROVEN** cause |

---

## OUTCOME

### **OUTCOME F: Root cause still not proven**

Do **not** implement a speculative fix (CORS tweak, MikroTik rule change, request-format change, or Hotspot file overwrite) until the distinguishing hardware tests below are completed.

---

## Smallest hardware tests (distinguish remaining possibilities)

Perform **without** code changes first:

1. **Browser Network (mandatory)** on the failing phone/session at Terminate:  
   - Is **OPTIONS** `/api/portal/terminate` present? Status? Response headers (ACAO, ACAM, ACAH, **Allow-Private-Network**)?  
   - Is **POST** present? Status? `(blocked:cors)` / failed / pending?  
2. **ESP32 serial** during the same click:  
   - Any new line at all?  
   - Specifically `[portal] heap before terminate`?  
3. **Heartbeat control:** while session is active, do periodic POSTs to `/api/portal/heartbeat` appear (or `[HTTP]` URL heartbeat)? Same `apiPost` class as terminate.  
4. **MikroTik file hash:** SHA256 of `hotspot/renzfi-app.js` vs `Final_Build_Portal/renzfi-app.js`.  
5. Optional: packet capture `10.20.0.253` ↔ `10.10.10.2:80` for OPTIONS/POST only.

### Decision matrix

| Browser Network | ESP32 heap log | Interpretation |
|-----------------|----------------|----------------|
| OPTIONS fail / CORS error; no POST | no | Preflight/CORS (incl. possible PNA) — then Outcome A direction |
| OPTIONS 204; POST blocked CORS | maybe | Response CORS headers on POST/OPTIONS |
| OPTIONS+POST 200 on wire; heap yes; UI still Failed to fetch | yes | Response parsing / CORS on POST response (rare) |
| No OPTIONS, no POST in browser | no | JS/cache/abort before network — Outcome B lean |
| POST on wire; heap yes | yes | Move boundary into handler (not current capture) |
| Hash mismatch Hotspot vs Final | — | Outcome B deploy |

---

## Files inspected

- `portal/renzfi-app.js`, `portal/login.html`
- `Final_Build_Portal/renzfi-app.js`, `Final_Build_Portal/login.html`
- `deployment/mikrotik-hotspot/renzfi-app.js`
- `scripts/build-mikrotik-portal.mjs`
- `ESP32_S3_Firmware/src/ApiServer.cpp`
- `ESP32_S3_Firmware/src/web/WebResponse.cpp`, `WebServerManager.cpp`, `HttpPlaneGate.cpp`, `WebRequestDiagnostics.cpp`
- `ESP32_S3_Firmware/src/NetworkDiagnostics.cpp`, `RenzFiDebug.h`
- `ESP32_S3_Firmware/src/PortalSessionManager.cpp` (`terminateSession`)
- ESPAsyncWebServer `WebServer.cpp` / `WebHandlers.cpp` / `WebResponses.cpp` (matcher + headers)
- Docs: Guru Meditation baseline, SETUP/TWDT wifi selection, customer lifecycle audit, manual vs automatic portal, reset persistence, sales/active users; root `ADMIN_LOGIN_TWDT_ROOT_CAUSE.md`, `SETUP_ASYNCTCP_TWDT_PREVENTION.md`, `TWDT_WIFI_SELECTION_ROOT_CAUSE.md`
- Missing as `docs/ADMIN_LOGIN_TWDT_ROOT_CAUSE.md` (exists at repo root)

## Files explicitly not changed

All application sources. No StorageManager, installation, router cache, MikroTik, W5500, TWDT, Sales, Active Users, coin, Done Paying, activation, portal UI, or CORS implementation changes in this pass.

## Files that would change only after proof

| Proven cause | Minimal touch (later) |
|--------------|------------------------|
| Preflight/CORS/PNA | OPTIONS/`serveOptions` headers only (+ optional OPTIONS diag log) |
| Stale Hotspot JS | Rebuild + upload Hotspot files; optional cache-bust on `login.html` |
| MikroTik transport | Only with packet proof — not IP return rules already working for GET |
| ESP32 gate/route | Only if Network shows POST reaching device |

---

## Stability constraints (reminder)

Any future fix must **not** introduce: `delay()`, TWDT feeds/timeout hikes, SD/SPIFFS/NVS on `async_tcp`, RouterOS waits on `async_tcp`, polling storms, or MikroTik CPU spikes.

`terminateSession` already enqueues RouterOS expire / save — keep that boundary.

---

## Validation status (this pass)

| Gate | Result |
|------|--------|
| Build | **NOT RUN** (investigation only) |
| Portal build | **NOT RUN** as fix |
| Lifecycle / Sales / Active Users | **NOT RUN** |
| Guru Meditation regression audit | **N/A** (no code change) |
| Hardware verification | **NOT TESTED** (tests listed above required) |
| **Production readiness** | **NOT READY** |

---

## Final production readiness

**NOT READY** — terminate reliability unproven; root cause Outcome **F**.
