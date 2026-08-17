# Renz-Fi Android Manual Portal — Coin Session Forensic (2026-08-15)

**Mode:** SOURCE-ONLY FORENSICS  
**Status:** Application root cause for *session eligibility* is **ruled out**; failure boundary is **narrowed to the browser ↔ ESP32 POST path** (CORS/preflight/network class). Exact wire mechanism remains **unproven** without one Network+Serial capture.  
**NO CODE CHANGES IN THIS PASS.**  
**NO FIRMWARE FLASH. NO RouterOS CHANGES. NO PORTAL/CORS/API EDITS.**

Related prior art (not re-opened as Model B / uptime work):

- `docs/RENZFI_TERMINATE_FAILED_FETCH_FORENSIC.md` — GET works, POST JSON `Failed to fetch`, handler not entered  
- `docs/MANUAL_VS_AUTOMATIC_CAPTIVE_PORTAL_FORENSIC.md` — automatic Android captive vs manual browser divergence  
- Session desync / Model B docs — **separate problems**; do not conflate unless new evidence ties them  

---

## 1. Executive Summary

For MAC `06:36:E3:2C:C4:E8` the ESP32 reports `sessionState=idle`, `secondsLeft=0`, `connected=false`, `coinWindowActive=false`, `canInsertCoin=true`, `source=portal`.

Source proves that under that state, `POST /api/portal/start-coin-session` should open a coin window in RAM with **zero RouterOS commands** and return success — unless the request never completes as a successful JSON POST from the portal page, or `_coin` is null (ruled out by laptop success on the same appliance), or the session is voucher-sourced (contradicted by the session payload).

Therefore the P0 failure is **not** “previous session still blocking insert” and **not** “route missing.” The remaining boundary is: **why Android’s portal-origin `fetch(POST application/json)` fails while laptop’s identical call succeeds**, and/or whether the visible message is a masked transport failure.

---

## 2. Incident Description

After a prior session ended, the customer manually opens the HotSpot login/portal UI on Android and taps **INSERT COIN**. The UI shows a failure described as **“Could not start coin session.”** The same manual concept on a laptop opens **Please Insert Money** successfully.

Automatic Android captive-portal redirect still loads the portal; manual access must also work (P0/P1).

---

## 3. Exact reproduction path (operator)

```text
Android (SSID Test2 Piso Wifi)
  → associate → 10.20.0.251 / MAC 06:36:E3:2C:C4:E8
  → manually open http://10.20.0.1/login
     (and/or land on wifi.renz-fi.local)
  → Renz-Fi portal UI loads
  → tap INSERT COIN
  → failure UI (“Could not start coin session” / service notice)
```

Laptop control:

```text
Laptop → same portal URL concept → INSERT MONEY → Please Insert Money (works)
```

---

## 4. Expected behavior

1. Portal JS POSTs `{mac, ip}` to `http://10.10.10.2/api/portal/start-coin-session`.  
2. ESP32 opens coin window (`coinWindowActive=true`, countdown).  
3. UI shows **Please Insert Money** with ~60s wait.  
4. No RouterOS work is required to *start* the insert window.

---

## 5. Actual behavior

- Android: coin modal does not open; error path runs.  
- Laptop: coin modal opens.  
- Direct Android browser GET of `start-coin-session` returns `NOT_FOUND` (expected for GET — see §10).  
- Authoritative GET `/api/portal/session` for this MAC shows idle + `canInsertCoin=true`.

---

## 6. Device / network topology

```text
Android / Laptop
        │
        ▼
MikroTik HotSpot  10.20.0.1  (hotspot-renzfi, dns-name wifi.renz-fi.local)
        │  serves login.html + renzfi-app.js (html-directory)
        ▼
Browser origin: http://10.20.0.1  and/or  http://wifi.renz-fi.local
        │  fetch (cross-origin)
        ▼
ESP32 API  10.10.10.2  /api/portal/*
        │
        ▼
PortalSessionManager::startCoinWindow  (RAM only — no RouterWorker for this call)
```

Intended architecture (unchanged): MikroTik hosts portal assets; ESP32 hosts API. Do **not** “fix” by pointing `API_BASE` at `window.location.origin`.

---

## 7. Evidence inventory

| Evidence | Class |
|---|---|
| Session JSON for MAC/IP (idle, `canInsertCoin=true`) | Operator capture |
| Laptop manual insert works | Operator capture |
| Android manual insert fails | Operator capture |
| GET `start-coin-session` → `NOT_FOUND` | Operator capture (method mismatch) |
| Route registration, `startCoinWindow`, CORS, portal JS | Repository source (this pass) |
| Prior terminate Failed-to-fetch forensic | Historical (same POST JSON class) |
| Live OPTIONS/POST wire + ESP32 Serial for *this* INSERT COIN click | **Missing** |
| Live MikroTik `hotspot/` file hash vs `Final_Build_Portal` | **Missing** |
| Exact on-screen string vs statusEl vs serviceNotice | **Ambiguous** (user paraphrase possible) |

---

## 8. Latest `/api/portal/session` evidence

```json
{
  "success": true,
  "data": {
    "macAddress": "06:36:E3:2C:C4:E8",
    "ipAddress": "10.20.0.251",
    "secondsLeft": 0,
    "connected": false,
    "coinWindowActive": false,
    "sessionState": "idle",
    "source": "portal",
    "canInsertCoin": true,
    "timerRunning": false
  }
}
```

**Interpretation (source):** There is **no** proven active paid session, coin window, or voucher source blocking insert. Eligibility flag is true.

**Caveat:** If this JSON was obtained by navigating the address bar to `http://10.10.10.2/api/portal/session?...`, that proves **IP reachability** but is **same-origin to the API host** and does **not** by itself prove that **cross-origin** `fetch` from `wifi.renz-fi.local` / `10.20.0.1` succeeds. If it was obtained via portal DevTools from the HotSpot origin, it *does* prove cross-origin GET. Capture method was not stated → treat as **reachability proven; cross-origin GET from portal page only strongly indicated if UI sync populated from appliance**.

---

## 9. Desktop vs Android comparison

| Factor | Laptop (works) | Android manual (fails) |
|---|---|---|
| Portal UI loads | Yes | Yes |
| Same intended API base (`10.10.10.2`) | Yes (built JS) | Yes (if serving Final_Build-equivalent JS) |
| Session idle / canInsertCoin | Assumed eligible | Proven eligible for this MAC |
| INSERT opens modal | Yes | No |
| Browser engine | Desktop Chromium/Edge/etc. | Phone Chrome / Samsung Internet / etc. (manual) |
| CaptivePortalLogin WebView | N/A | Different path (automatic captive historically works — prior forensic) |

Source cannot “prove Android is broken.” It **can** prove that the **server-side start-coin eligibility** is satisfied, so the differential must be sought in **client transport / origin / cache / error masking**, not in “session still Connected.”

---

## 10. GET vs POST distinction (PROVEN)

| Method | Route registered? | Result of browsing URL |
|---|---|---|
| **POST** | **Yes** — `ApiServer.cpp` `_server->on("/api/portal/start-coin-session", HTTP_POST, …)` | Intended portal call |
| **GET** | **No** | Falls through `handleNotFound` → `{ success:false, error:"API endpoint not found", code:"NOT_FOUND" }` |

**PROVEN:** Android address-bar GET `NOT_FOUND` does **not** prove the POST route is missing. It proves method mismatch only.

Boot log also prints: `POST /api/portal/start-coin-session`.

---

## 11. Full request lifecycle (source)

```text
#insertCoinBtn click
  → handleInsertCoin()                    portal/renzfi-app.js
  → getDeviceMAC() must be non-empty
  → startCoinSessionAPI()
  → portalPost("/start-coin-session", deviceParams())
  → apiPost() → fetch(API_BASE + path, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ mac, ip })
     })
  → [browser may OPTIONS preflight first — cross-origin + JSON CT]
  → ESP32 OPTIONS /api/portal/* → WebResponse::serveOptions (204 + CORS)
  → ESP32 POST handler:
        RENZFI_PORTAL_GATE (ensureProductionPlane)
        bodyCollect → getBody → mac/ip
        if !_coin → 503 COIN_DISABLED
        startCoinWindow(mac, ip) → getSession → 200 OK
        else → 500 SESSION_ERROR
  → unwrapPortalResponse / normalizeSession
  → applyNormalizedSession + startCoinSessionUI
```

**RouterProvisioningWorker / MikroTikDriver:** **not called** on this path. Opening the coin window is ESP32 session RAM + SSE emit enqueue only.

---

## 12. Route registration analysis

| Item | Finding |
|---|---|
| POST `/api/portal/start-coin-session` | Registered with `bodyCollect` |
| GET same path | Not registered → `NOT_FOUND` |
| Auth | `RENZFI_PORTAL_GATE` only (production plane); **no** owner/admin auth |
| Middleware | Plane gate; CORS via DefaultHeaders + `addCorsHeaders` on responses |
| OPTIONS | Explicit `_server->on("/api/portal/*", HTTP_OPTIONS, …)` + notFound OPTIONS fallback |
| Ordering | Exact POST registration; OPTIONS prefix match for `/api/portal/` |

---

## 13. CORS analysis

| Question | Source answer |
|---|---|
| ACAO? | `DefaultHeaders` → `Access-Control-Allow-Origin: *` (once) |
| Methods? | `GET, POST, PUT, DELETE, OPTIONS` |
| Headers? | `Content-Type, Authorization` |
| OPTIONS? | `serveOptions` → 204 + those headers |
| Allow Private Network? | **Not set** |
| GET `/session` preflight? | Typically **no** (simple GET, no custom CT) |
| POST JSON preflight? | **Yes** (PROVEN request shape) |

**CORS is configured to allow the portal pattern.** That does **not** prove every Android browser accepts the response (PNA / duplicate ACAO history / captive quirks remain hypotheses). Prior terminate forensic: same POST class failed with `Failed to fetch` while GET session worked.

---

## 14–15. Captive portal / Origin / Host

| Item | Finding |
|---|---|
| Portal served by | MikroTik HotSpot `html-directory` (`login.html`, `renzfi-app.js`) |
| API served by | ESP32 `10.10.10.2` |
| `$(mac)` / `$(ip)` | Substituted by HotSpot into `#macAddress` / `#ipAddress` |
| API base | Built bundle substitutes `http://10.10.10.2` (not `window.location.origin`) |
| Origin examples | `http://10.20.0.1`, `http://wifi.renz-fi.local` → both cross-origin to API |
| Host header on API | ESP32 sees Host `10.10.10.2`; plane gate uses local/remote IP, not portal Host |

Automatic vs manual may change **which browser engine** opens the page; both still depend on the same HotSpot assets + same API base when the Final_Build JS is what HotSpot serves.

---

## 16. Device MAC/IP analysis

```js
getDeviceMAC() → #macAddress text, reject if contains "$("
getDeviceIP()  → #ipAddress text
deviceParams() → { mac, ip }
```

`handleInsertCoin` aborts early with **“Device MAC unavailable”** if MAC empty — **different string** from coin-session failure default.

Session payload already shows correct MAC/IP for this device. If INSERT COIN ran past the MAC guard, DOM MAC was present at click time.

---

## 17. Session state analysis

`canInsertCoin` computation (`enrichSessionCapabilities`):

```text
canInsertCoin = !isVoucher && !cleanupInFlight
```

where `cleanupInFlight` = `sessionState == expiring` and cleanup not stuck.

It does **not** require `idle`, nor `secondsLeft==0`. For the captured idle portal session, **canInsertCoin=true is consistent**.

`startCoinWindow`:

1. Closes other MACs’ coin windows.  
2. `findOrCreate` session.  
3. **Reject only if `source == "voucher"`** → returns false → API `SESSION_ERROR`.  
4. Else sets `coinWindowActive`, `WaitingCoin`, returns true.

**PROVEN:** For `source=portal` idle session, start-coin **should succeed** if the POST handler runs with a non-empty MAC and `_coin != nullptr`.

**Mismatch check:** `canInsertCoin` vs `startCoinWindow` — for voucher, both deny (canInsertCoin false; start returns false). For cleanup-in-flight, canInsertCoin false but startCoinWindow would still open a window if called — **minor contract asymmetry**, **not** applicable to this idle capture.

---

## 18. Coin-window state analysis

Captured `coinWindowActive=false`. No evidence of a hidden second window object: window state lives on the same session JSON document. `_activeInsertMac` is set when a window opens; boot recovery clears stale windows.

Multiple tabs: two clients same MAC would share one session document; last `startCoinWindow` wins. Does not explain laptop OK / phone fail with different MACs.

---

## 19. Previous-session cleanup analysis

Prior desync (portal Connected while Active empty) is a **display/authorization** issue after activation. **startCoinWindow does not consult RouterOS Active, cookies, or limit-uptime.**

Model B / uptime-limit affects **activation** (`createHotspotUser`), not **opening the insert window**.

**RULED OUT as cause of this INSERT COIN failure:** leftover HotSpot user uptime, leftover Active, leftover cookie — unless somehow `_coin` disabled or session forced to voucher (contradicted).

---

## 20. RouterProvisioningWorker analysis

**Not involved** in `start-coin-session`. Failure is **not** worker starvation / queue full for this endpoint.

---

## 21. MikroTik interaction analysis

No `active/login`, `user/set`, or other RouterOS commands on start-coin. MikroTik only:

1. Serves portal files.  
2. Expands `$(mac)`/`$(ip)`.  
3. Allows guest → `10.10.10.2` (walled-garden / hs-unauth return — prior operator evidence).

CPU spike from this click: **not expected** (no ROS). Do not add Active polling to “fix” this.

---

## 22. Error propagation analysis (CRITICAL)

Current `handleInsertCoin` catch (`portal/renzfi-app.js`):

```text
msg = err.message || "Could not start a coin session."
if business codes (COIN_DISABLED, SESSION_ERROR, MISSING_MAC, INVALID_SESSION, VOUCHER_SESSION):
  noteApplianceSuccess()
else:
  noteApplianceFailure() + showServiceNotice()   // "Payment service temporarily unavailable…"
showPortalError(msg)  // status line
```

| Visible UI | Source |
|---|---|
| Status strong text | `showPortalError(msg)` — API error, `Failed to fetch`, or default |
| Banner | `#serviceNotice` — “Payment service temporarily unavailable…” |
| Exact user phrase “Could not start coin session” | Matches **default fallback** closely (`…a coin session.`); may also be paraphrase of banner |

**PROVEN:** Catch **masks** transport failures into a generic user story unless `err.message` is inspected (e.g. `Failed to fetch` would be the real message if thrown). Serial/Network evidence is still required to see whether ESP32 received POST.

---

## 23. Browser / cache / service worker

| Item | Finding |
|---|---|
| Service worker in `portal/` | **None** (no `navigator.serviceWorker` / `sw.js`) |
| Cache-bust on `renzfi-app.js` | **None** (`<script src="renzfi-app.js">`) |
| ESP32 PortalServer JS cache policy | `ShortCache` = `max-age=86400` (only if ESP32 serves portal — production HotSpot serves files) |
| HotSpot file cache | Browser/MikroTik dependent — **unproven** live |
| Stale JS phone vs laptop | **Possible** (independent browser caches); **not proven** |
| `Captive Portal/` legacy tree | Different SHA historically — must not be live HotSpot content |

---

## 24. Race / concurrency

In-flight GET `/session` could theoretically race after open; current code bumps `sessionSyncGen` on successful start. That race **cannot** produce start failure **before** the modal opens. Ruled out as primary cause of “could not start.”

---

## 25–28. Root-cause candidates

### PROVEN

1. **POST route exists; GET does not** — GET `NOT_FOUND` is expected.  
2. **Captured session is eligible** — idle portal + `canInsertCoin=true` ⇒ `startCoinWindow` should accept.  
3. **start-coin does not use RouterOS / RouterWorker.**  
4. **Portal INSERT uses cross-origin POST JSON** → **CORS preflight required.**  
5. **GET `/session` is a different CORS class** (typically no preflight).  
6. **Error UI can hide the real exception** behind service notice / generic wording.  
7. **Laptop success on same architecture** ⇒ ESP32 start-coin path and coin hardware pointer are capable of working.

### STRONGLY INDICATED

1. **Failure boundary is browser `fetch(POST JSON)` from HotSpot origin → `10.10.10.2`**, not PortalSessionManager eligibility (same pattern as terminate “Failed to fetch” forensic: GET OK, POST fails before / without handler entry).  
2. **Android manual browser** differs from **laptop** and from **Android CaptivePortalLogin WebView** (prior: automatic captive worked).  
3. **Missing `Access-Control-Allow-Private-Network`** may matter on some Chromium builds (portal private subnet A → API private subnet B) — **unproven for this phone.**

### POSSIBLE

- Stale `renzfi-app.js` on Android cache only.  
- OPTIONS blocked / dropped uniquely on that client.  
- Body not delivered (`MISSING_MAC`) — would show API text if response readable.  
- User paraphrased status line vs banner.

### RULED OUT

| Hypothesis | Why |
|---|---|
| POST route missing | Registered; GET NOT_FOUND expected |
| Session still active / Connected blocking | Captured idle; startCoinWindow ignores Active |
| Model B / uptime limit | Affects activate, not start-coin window |
| Need RouterOS Active for insert window | No ROS on this path |
| CORS completely unimplemented | ACAO * + OPTIONS + Allow-Headers present |
| Service worker intercept | No SW in portal source |
| `canInsertCoin` true but start always rejects for idle portal | Only voucher rejects |
| Changing API_BASE to MikroTik origin as investigation conclusion | Architecture forbids; not evidenced as required |

---

## 29. Remaining unknown boundary

```text
Android portal page
  → fetch POST JSON /api/portal/start-coin-session
        │
        ├─ A) OPTIONS fails / CORS / PNA → JS sees Failed to fetch (ESP32 may never see POST)
        ├─ B) OPTIONS OK; POST never arrives / blocked
        ├─ C) POST arrives; ESP32 returns error (COIN_DISABLED / SESSION_ERROR / MISSING_MAC)
        └─ D) POST succeeds; JS normalize/unwrap fails (INVALID_SESSION)
```

Source + eligibility evidence **favor A/B** over C/D for idle portal + laptop OK, but **A vs B vs C is not closed without one capture.**

---

## 30. Exact evidence required to close the boundary

**Minimum (one Android fail + optional laptop success):**

1. **Browser Network** on the HotSpot-origin portal page at INSERT COIN:  
   - OPTIONS `/api/portal/start-coin-session` — status + response headers (ACAO, Allow-Methods, Allow-Headers, any PNA).  
   - POST same — status, response body, or `(failed) / CORS error`.  
2. **ESP32 Serial** same second: presence/absence of `[portal-api] … start-coin-session` (or equivalent NetworkDiagnostics line).  
3. **Exact UI string** on `#connectionStatus` vs `#serviceNotice`.  
4. Optional: `curl`/fetch from laptop DevTools copy-as-cURL of the failing Android request.

**Do not** require repeated full payment cycles; this is a single POST open-window probe.

---

## 31. Recommended remediation — DESIGN ONLY (DO NOT IMPLEMENT)

Only after capture classifies A/B/C/D:

| If | Design direction |
|---|---|
| A/B CORS/PNA | Minimal OPTIONS/response header fix (e.g. PNA if proven); keep ACAO single-owner; **no** second worker; **no** ROS |
| A/B preflight avoidance | Alternate non-preflight POST content-type **only if** proven necessary and contract-safe |
| C business error | Surface `code` already partially done; fix specific ESP32 cause |
| D normalize | Align response shape |
| Observability | Ensure start-coin logs like terminate heap line so “handler entered?” is trivial |

**Do not:** poll Active, add ROS to start-coin, route API via MikroTik, continuous heartbeat ROS, parallel RouterOS sessions.

---

## 32. Stability requirements for future fix

- Single Router Worker preserved.  
- No RouterOS from `async_tcp` / HTTP handler for this path (already true).  
- No Active poll storm.  
- CORS/header changes must not reintroduce duplicate `ACAO *, *`.  
- Automatic Android captive path must remain green.

---

## 33. Regression risks

- CORS/PNA header experiments affecting Admin SPA or terminate/heartbeat.  
- Content-Type changes breaking `bodyCollect` / JSON parse.  
- Portal rebuild without MikroTik upload leaving Android on stale JS.  
- Confusing this with Model B / voucher absolute expiry work.

---

## 34. Hardware validation plan (after a future fix — not this pass)

1. Android **manual** `10.20.0.1/login` → INSERT COIN → modal opens.  
2. Android **automatic** captive still works.  
3. Laptop manual still works.  
4. Network: OPTIONS 204 + POST 200 with `coinWindowActive=true`.  
5. Serial: start-coin debug line present.  
6. No MikroTik CPU spike on insert click.

---

# FINAL OUTPUT BLOCK

## A. ONE-SENTENCE ROOT CAUSE

**Server-side coin-session eligibility is not the failure:** the idle portal session should accept `POST /start-coin-session`; the unresolved P0 boundary is the Android HotSpot-origin **cross-origin JSON POST** (preflight/CORS/network class) versus the laptop’s successful identical call — exact wire mechanism **not yet proven**.

## B. PROOF CHAIN

```text
Session JSON: idle, source=portal, canInsertCoin=true
       ↓
startCoinWindow rejects only voucher; no RouterOS
       ↓
POST route exists; GET NOT_FOUND is method mismatch
       ↓
INSERT uses fetch(POST + Content-Type: application/json) cross-origin → preflight required
       ↓
GET /session is a different CORS class; laptop POST works on same appliance
       ↓
Failure is not “session still active”; remaining boundary = Android POST transport / CORS visibility
```

## C. WHAT IS DEFINITELY NOT THE CAUSE

- Missing POST route (GET NOT_FOUND ≠ missing POST)  
- Captured session still paid/Connected/coin-window active  
- Model B / RouterOS uptime limit for *opening* the insert window  
- RouterProvisioningWorker / Active polling requirement  
- Service worker interception (none in portal source)

## D. WHAT REMAINS UNPROVEN

- Whether OPTIONS or POST fails on the wire for this Android click  
- Whether Private Network Access header absence is causal  
- Exact on-screen string source (status vs banner vs paraphrase)  
- Whether Android’s HotSpot-origin GET `/session` via portal JS succeeded (vs address-bar API GET)  
- Live HotSpot file hash vs `Final_Build_Portal`

## E. EXACT FILES / FUNCTIONS

| File | Function / site |
|---|---|
| `portal/renzfi-app.js` | `handleInsertCoin`, `startCoinSessionAPI`, `apiPost`, `apiGet`, `deviceParams`, `showServiceNotice` |
| `portal/login.html` | `#macAddress` `$(mac)`, `#ipAddress` `$(ip)`, `#serviceNotice` |
| `ESP32_S3_Firmware/src/ApiServer.cpp` | POST `/api/portal/start-coin-session`, OPTIONS `/api/portal/*`, `handleNotFound` |
| `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | `startCoinWindow`, `enrichSessionCapabilities` (`canInsertCoin`) |
| `ESP32_S3_Firmware/src/web/WebResponse.cpp` | `serveOptions`, `addCorsHeaders` |
| `ESP32_S3_Firmware/src/web/WebServerManager.cpp` | DefaultHeaders ACAO `*` |
| `ESP32_S3_Firmware/src/web/HttpPlaneGate.cpp` | `ensureProductionPlane` |

## F. SAFE REMEDIATION DESIGN

Capture-first classification (A/B/C/D), then minimal transport/header or error-surfacing change only; **never** add RouterOS to start-coin; preserve single worker and captive automatic path.

## G. HARDWARE VALIDATION AFTER REMEDIATION

Manual Android insert modal + automatic captive + laptop + Network/Serial confirmation; no ROS CPU spike.

## H. MIKROTIK CPU / STABILITY SAFEGUARDS

No Active polling, no per-heartbeat ROS, no second worker, no ROS from HTTP for coin-window open (already free of ROS).

## I. NO-CODE-CHANGES-IN-THIS-PASS CONFIRMATION

**Confirmed.** This pass produced **documentation only** (`docs/RENZFI_ANDROID_MANUAL_PORTAL_COIN_SESSION_FORENSIC_2026-08-15.md`). No firmware, portal, CORS, API, RouterOS, or flash actions were performed.
