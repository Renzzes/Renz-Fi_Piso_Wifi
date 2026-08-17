# Manual vs Automatic Captive Portal — Forensic Investigation

**Date:** 2026-08-11  
**Mode:** READ-ONLY forensic investigation  
**Status:** Investigation complete for source/docs; live request capture not performed in this pass  

**NO CODE CHANGES MADE.**  
**NO MIKROTIK CHANGES MADE.**  
**NO FIRMWARE FLASHING PERFORMED.**  
**NO PORTAL FILE MODIFICATIONS.**

---

## Binding statements

1. **Automatic captive-portal flow is a known-good production path and must not be broken by future manual-login, portal, Hotspot, frontend, API, or router changes.**
2. Previous Guru Meditation / TWDT fixes remain a **frozen production stability baseline** and must not be weakened (`docs/RENZFI_GURU_MEDITATION_PREVENTION_BASELINE.md`).
3. Prior lifecycle conclusions (sales uptime markers, Active Users entitlement vs heartbeat, Done Paying activation path, sales-vs-activation decoupling) remain in force; this document does not reopen them without new evidence.
4. MikroTik NAT/filter evidence supplied by the operator is treated as **captured production evidence**, not assumptions.

---

## Incident summary

| Item | Detail |
|------|--------|
| Working path | Android **automatic** captive portal → Renz-Fi UI works (rates, coin, payment, Internet grant) |
| Failing path | Manual browser `http://10.20.0.1/login` → redirect to `http://wifi.renz-fi.local` → UI **looks** like Renz-Fi but API-backed behavior differs |
| Reported manual symptoms | Rates incorrect / missing; coin start fails with message described as “Could not start a coin session.”; other API-backed features diverge |
| Scope | Prove **where** Flow A and Flow B diverge; do **not** implement a fix |

---

## Observed behavior

### Known-good — FLOW A (automatic Android captive portal)

Customer associates to Guest Wi-Fi → Android captive detection → Hotspot interception → login page → Renz-Fi portal initializes → rates / coin / Done Paying / MikroTik authorization → Internet works.

### Failing — FLOW B (manual browser)

Customer on Guest Wi-Fi opens `http://10.20.0.1/login` → MikroTik redirects to `wifi.renz-fi.local` → portal chrome loads → rates / coin session / other API features do **not** behave like Flow A.

### What this already proves (operator evidence)

- Hotspot **can** serve the portal HTML (manual path loads UI).
- DNS for `wifi.renz-fi.local` → `10.20.0.1` works (redirect destination resolves).
- Therefore “DNS completely broken” and “Hotspot cannot serve login.html” are **rejected**.

---

## MikroTik topology and evidence (operator-captured)

### Hotspot server

```
name=hotspot-renzfi
interface=bridgeGuest
address-pool=pool-guest
profile=RenzFi-Hotspot
ip-of-dns-name=10.20.0.1
proxy-status=running
```

### Hotspot profile

```
name=RenzFi-Hotspot
hotspot-address=10.20.0.1
dns-name=wifi.renz-fi.local
html-directory=hotspot
login-by=cookie,http-chap,http-pap
http-cookie-lifetime=3d
```

### DNS

```
wifi.renz-fi.local  A  10.20.0.1
```

### Client (prior test unit)

| Field | Value |
|-------|--------|
| IP | `10.20.0.253` |
| MAC | `06:36:E3:2C:C4:E8` |
| Hotspot host | present on `hotspot-renzfi` |
| Hotspot active (paid) | observed then removed at expiry |
| Hotspot cookie for user | none at expiry capture |

Expiration / cookie absence is **not** used as the explanation for the manual portal bug unless a new live correlation proves it. It remains separate evidence that Hotspot deauthorization can clear Active.

### NAT (relevant excerpts)

- Dynamic Hotspot redirect of unauth HTTP to Hotspot servlet ports.
- **`hs-unauth` return for `dst-address=10.10.10.2`** — comment: `RenzFi ESP32 appliance API`.
- Intentional design: unauthenticated guests may reach the ESP32 appliance API IP.

### Firewall (relevant excerpts)

- Matching `hs-unauth` return for `10.10.10.2`.
- Unauth TCP otherwise rejected with tcp-reset on `hs-unauth`.

**Do not change these rules without proof they are the failure boundary.**

---

## Portal bundle evidence (repository — not live MikroTik flash contents)

### Canonicality (re-verified)

| Tree | Role |
|------|------|
| `portal/` | Canonical editable source |
| `Final_Build_Portal/` | Generated MikroTik upload bundle |
| `deployment/mikrotik-hotspot/` | Generated deployment output |
| `Captive Portal/` | Legacy / deprecated (different SHA256; must not be treated as production) |

### SHA256 comparison (this workspace)

| File | `portal/` | `Final_Build_Portal/` | `deployment/mikrotik-hotspot/` | `Captive Portal/` |
|------|-----------|------------------------|--------------------------------|-------------------|
| `login.html` | `6865748C…770DDD` | **same as portal** | **same** | **different** |
| `renzfi-style.css` | `EF3DD648…950CC9` | **same** | **same** | **different** |
| `md5.js` | `D98CB21A…7B931F` | **same** | **same** | **same** |
| `renzfi-app.js` | `C316DC62…FD0C91` | `C82BD27D…136292B` | **same as Final** | **different** |

### Critical `renzfi-app.js` difference

| Copy | `RENZFI_APPLIANCE_BASE_URL` |
|------|------------------------------|
| `portal/renzfi-app.js` | `"__RENZFI_APPLIANCE_BASE_URL__"` (build placeholder) |
| `Final_Build_Portal/renzfi-app.js` | `"http://10.10.10.2"` |
| `deployment/mikrotik-hotspot/renzfi-app.js` | `"http://10.10.10.2"` |

Build pipeline: `scripts/build-mikrotik-portal.mjs` substitutes the placeholder. Production Hotspot **must** receive the substituted bundle.

**Unproven on the live unit:** whether the files currently stored under MikroTik `hotspot/` match `Final_Build_Portal/` byte-for-byte. Repository evidence cannot substitute for `/file print` + hash on the router.

---

## Architecture that both flows share (source-proven)

```
Phone browser / CaptivePortal WebView
  → Guest Wi-Fi → bridgeGuest → Hotspot
  → login.html (+ CSS/JS/media) served by MikroTik Hotspot servlet/html-directory
  → renzfi-app.js runs in the browser
  → ALL portal APIs go to APPLIANCE_BASE_URL = http://10.10.10.2  (production bundle)
       /api/portal/session
       /api/portal/rates
       /api/portal/start-coin-session
       /api/portal/branding
       /api/events  (SSE)
  → ESP32 ApiServer → PortalSessionManager / PromoManager
```

**Proven implication:** Seeing the Renz-Fi **visual** shell only proves MikroTik served HTML/CSS/static assets. It does **not** prove ESP32 API reachability or successful application initialization.

`login.html` even documents the appliance outage banner:

```61:64:portal/login.html
      <!-- Shown only when the appliance API cannot be reached (see renzfi-app.js) -->
      <p id="serviceNotice" class="service-notice" hidden>
        Payment service temporarily unavailable. Please try again shortly.
      </p>
```

---

## Side-by-side flow trace

### FLOW A — Automatic Android captive portal

| Step | Expected path | Evidence class |
|------|---------------|----------------|
| A1 | Android probes connectivity URL (vendor-specific) | Platform behavior (not captured this pass) |
| A2 | Hotspot `hs-unauth` redirects HTTP → Hotspot login servlet | Matches supplied NAT rules |
| A3 | Browser/WebView opens Hotspot login (`dns-name` / hotspot-address) | Operational success proves this works |
| A4 | Servlet expands `$(mac)`, `$(ip)`, optional `$(chap-*)`, `$(link-*)` into `login.html` | Required by portal JS (see identity) — **live HTML not captured this pass** |
| A5 | `renzfi-app.js` sets API base `http://10.10.10.2` | Source + Final_Build hash |
| A6 | `GET /api/portal/session?mac=…&ip=…` succeeds | Operational success |
| A7 | Rates / coin / SSE / Done Paying succeed | Operational success |
| A8 | RouterWorker authorizes Hotspot user → Internet | Prior forensic / operational |

### FLOW B — Manual `http://10.20.0.1/login` → `wifi.renz-fi.local`

| Step | Observed / expected | Evidence class |
|------|---------------------|----------------|
| B1 | User types `http://10.20.0.1/login` | Operator report |
| B2 | Redirect to `wifi.renz-fi.local` | Operator report + profile `dns-name` |
| B3 | Portal **UI** loads | Operator report |
| B4 | Servlet variable expansion for `$(mac)` / `$(ip)` | **NOT LIVE-CAPTURED** |
| B5 | Same `renzfi-app.js` API base `http://10.10.10.2` | True **if** Hotspot serves Final_Build-equivalent JS — **not live-hashed** |
| B6 | Rates / coin / API features fail | Operator report |
| B7 | Exact HTTP status / body / console errors | **NOT CAPTURED this pass** |

### First architectural divergence (platform)

| Dimension | FLOW A | FLOW B |
|-----------|--------|--------|
| Entry trigger | Captive detection redirect | Manual URL |
| Browser engine | Android Captive Portal Login **WebView** (typical) | Full browser (Chrome / Samsung Internet / etc.) |
| Cookie jar | Captive WebView store | Browser store |
| Final host (manual evidence) | Often also `wifi.renz-fi.local` when `dns-name` set | Confirmed `wifi.renz-fi.local` |

Platform difference is **real** but **not by itself a proven root cause** of rates/coin failure without Network-tab evidence.

---

## MikroTik Hotspot variables vs portal JS

### Variables present in `login.html` (source)

| Variable / construct | Location | Purpose in Renz-Fi portal |
|----------------------|----------|---------------------------|
| `$(if chap-id)` / `$(endif)` | CHAP form block | MikroTik HTTP-CHAP helper only |
| `$(link-login-only)` | hidden form action | CHAP postback — **not** used by coin/rates API |
| `$(link-orig)` | `dst` hidden field | Original URL — **not** used by coin/rates API |
| `$(chap-id)` / `$(chap-challenge)` | `doLogin()` | CHAP only |
| **`$(ip)`** | `#ipAddress` | **Primary device IP for API** |
| **`$(mac)`** | `#macAddress` | **Primary device MAC for API** |

### How JS consumes identity (exact source)

```80:98:portal/renzfi-app.js
  function getDeviceMAC() {
    var mac = textOf("macAddress");
    return mac && mac.indexOf("$(") === -1 ? mac : "";
  }

  function getDeviceIP() {
    var ip = textOf("ipAddress");
    return ip && ip.indexOf("$(") === -1 ? ip : "";
  }
  ...
  function deviceParams() {
    return { mac: getDeviceMAC(), ip: getDeviceIP() };
  }
```

**Proven rules from source:**

1. If the browser still shows literal `$(mac)` / `$(ip)`, JS treats identity as **empty**.
2. If servlet expands them to real values, identity is available.
3. Portal does **not** read MAC from Android APIs, cookies, or ESP32 ARP in the frontend.

### Which APIs require MAC (firmware + JS)

| Endpoint | Method | MAC required? | Source |
|----------|--------|---------------|--------|
| `/api/portal/rates` | GET | **No** (JS `fetchRatesAPI` has no MAC; firmware `getRates` lists promos) | `renzfi-app.js` ~620; `ApiServer.cpp` rates handler; `PortalSessionManager::getRates` |
| `/api/portal/session` | GET | **Yes** (JS rejects before fetch if no MAC) | `fetchSession` ~594–599 |
| `/api/portal/start-coin-session` | POST | **Yes** (JS gate + firmware `mac field required`) | `handleInsertCoin` ~858; `ApiServer.cpp` ~2928–2932 |
| `/api/portal/heartbeat` | POST | Yes (body) | deviceParams |
| `/api/portal/branding` | GET | No | `loadBranding` |
| `/api/events` | SSE | No for connect; push filtered by MAC client-side | `handleSessionPush` ~551–553 |

**Critical forensic discriminator:**

- If **VIEW RATES** fails while MAC is correctly shown → failure is **not** identity; first suspect is **ESP32 API path** (`http://10.10.10.2/api/portal/rates`).
- If MAC DOM is empty/`$(mac)` **and** rates still fail → either two failures, or “rates” means coin-modal purchased-time (which needs a working coin/session path), not `/rates`.

---

## API base URL / Origin / CORS analysis

### API base resolution (production bundle)

```17:34:Final_Build_Portal/renzfi-app.js
  var RENZFI_APPLIANCE_BASE_URL = "http://10.10.10.2";
  ...
  var APPLIANCE_BASE_URL = resolveApplianceBaseUrl();
  var API_BASE           = APPLIANCE_BASE_URL + "/api/portal";
```

Comments in source state explicitly: **browser hostname is never used to guess the ESP32 address** once the placeholder is replaced.

Therefore origin `http://wifi.renz-fi.local` vs `http://10.20.0.1` does **not** change the configured API base in the production bundle. Both become **cross-origin** callers of `http://10.10.10.2`.

### CORS on ESP32 JSON APIs (source)

`WebResponse::addCorsHeaders` sets:

- `Access-Control-Allow-Origin: *`
- Methods: GET, POST, PUT, DELETE, OPTIONS
- Headers: Content-Type, Authorization

Portal routes use `RENZFI_PORTAL_GATE` → `HttpPlaneGate::ensureProductionPlane` (Ethernet production plane), **not** admin auth. Hotspot guests are an intended client class (`CustomerPortal` when remote is outside appliance LAN).

OPTIONS `/api/` is registered for CORS preflight and also portal-gated.

**CORS-as-sole-root-cause is not proven.** Firmware is written to allow cross-origin portal calls. Live proof still requires seeing whether the browser blocks the request before it leaves the device.

### EventSource note

`EventBus::emit` / SSE registration does **not** independently document CORS headers in `EventBus.cpp`. Session **push** may be flaky cross-origin even when REST works. Rates and start-coin use **fetch**, not SSE, so SSE CORS cannot alone explain rates failure.

---

## Rates request analysis (exact frontend chain)

| Item | Value |
|------|-------|
| UI trigger | `#viewRatesBtn` → `handleViewRates` |
| File | `portal/renzfi-app.js` / `Final_Build_Portal/renzfi-app.js` |
| Function | `handleViewRates` → `fetchRatesAPI` → `portalGet("/rates")` → `apiGet` |
| URL | `http://10.10.10.2/api/portal/rates` |
| Method | `GET` |
| Body | none |
| Query | none |
| Identity | **not sent** |
| Firmware | `ApiServer` GET `/api/portal/rates` → `PortalSessionManager::getRates` → `PromoManager::list` |
| Success UI | `renderRatesModal` replaces `.rates-list` |
| Failure UI | `catch` still `openModal(dom.ratesModal)` **without** clearing “Loading rates…” |

```1018:1026:portal/renzfi-app.js
  function handleViewRates() {
    fetchRatesAPI()
      .then(function (viewModel) {
        renderRatesModal(viewModel);
        openModal(dom.ratesModal);
      })
      .catch(function () {
        openModal(dom.ratesModal);
      });
  }
```

**Source-proven failure UX:** A failed rates fetch presents a modal that still looks like “Loading rates…” — matches “rates do not appear correctly” **if** the operator meant VIEW RATES.

**First failed boundary candidates for rates (ordered):**

1. Browser never sends request (blocked / wrong base URL / offline JS error) — **needs Network tab**
2. Request leaves phone, MikroTik resets/intercepts despite `10.10.10.2` return — **needs packet/MikroTik torch**
3. Request reaches ESP32, plane gate rejects — **needs ESP32 serial `[http]` / portal debug**
4. ESP32 returns error / empty promos — **needs response body**
5. Response OK but `normalizeRatesPayload` returns null — **needs body shape**
6. UI catch path leaves “Loading rates…” — **matches (5) or network fail**

Without a captured response, the **exact** first failed boundary among 1–5 is **NOT YET PROVEN**.

---

## Coin session request analysis (exact frontend chain)

| Item | Value |
|------|-------|
| UI trigger | `#insertCoinBtn` → `handleInsertCoin` |
| Client pre-check | `if (!getDeviceMAC()) { showPortalError("Device MAC unavailable"); return; }` |
| URL | `http://10.10.10.2/api/portal/start-coin-session` |
| Method | `POST` |
| Headers | `Content-Type: application/json` (triggers CORS preflight) |
| Body | `{ "mac": "<from DOM>", "ip": "<from DOM>" }` |
| Firmware reject empty MAC | HTTP 400 `mac field required` / `MISSING_MAC` |
| Firmware success | `startCoinWindow(mac, ip)` → session WaitingCoin |
| Client API failure UX | `noteApplianceFailure()` + `showServiceNotice()` — **does not** surface `err.message` |

### Exact strings in current production JS vs operator report

| String | Present in `Final_Build_Portal/renzfi-app.js`? |
|--------|-----------------------------------------------|
| `Could not start a coin session.` | **NO** |
| `Device MAC unavailable` | **YES** (client pre-check / status line) |
| `Payment service temporarily unavailable…` | **YES** (`#serviceNotice`) |
| Firmware `Failed to open coin window` | Only inside JSON `error` if API returns 500 — **not shown** by `handleInsertCoin` catch |

**Evidence conflict:** The operator’s exact phrase does **not** exist in the current repository production portal bundle. Possibilities constrained by evidence:

1. Operator paraphrase of service notice / status text.
2. Live MikroTik Hotspot still has an **older / different** `renzfi-app.js` than `Final_Build_Portal`.
3. Message came from a different UI surface not found in this search.

This conflict **blocks** claiming a single exact UI string as the proven failure mode without a screenshot or console capture.

---

## Session identity analysis

| Source | Automatic flow | Manual flow | Proven? |
|--------|----------------|-------------|---------|
| MikroTik `$(mac)` / `$(ip)` into DOM | Required for app | Required for app | Mechanism proven in source; **live values not captured** |
| Client IP `10.20.0.253` | Available to Hotspot | Available if same client | Prior Hotspot host print |
| MAC `06:36:E3:2C:C4:E8` | Available to Hotspot | Same | Prior print |
| Hotspot cookie | Optional; none in expiry capture | Unknown for manual | Cookie **not** used by `renzfi-app.js` for API identity |
| ESP32 session key | MAC string from JSON/query | Same | Firmware |

**Rates do not require customer identity.**  
**Coin session does.**

Therefore identity alone **cannot** explain a pure `/api/portal/rates` failure. Identity **can** explain coin/session/heartbeat failure if `$(mac)` is missing.

---

## SSE analysis

| Item | Detail |
|------|--------|
| URL | `http://10.10.10.2/api/events` |
| Connect | `connectPortalEvents()` at `init` |
| Session events | Applied only if push MAC equals `getDeviceMAC()` |
| Relation to rates | None |
| Relation to start-coin response | Start-coin uses HTTP response first; SSE is supplemental |

SSE differences between Captive WebView and Chrome remain **unproven** as the rates/coin root cause.

---

## Cookies / authentication-state analysis

| Cookie type | Role in portal API |
|-------------|--------------------|
| MikroTik Hotspot cookie | Hotspot login-by cookie — **not read by renzfi-app.js** for ESP32 calls |
| ESP32 admin session cookie | Not used on portal routes (`RENZFI_PORTAL_GATE`) |
| Portal localStorage | UI cache keyed by device key from MAC/IP |

Manual vs automatic cookie jars differ by browser engine, but **source does not gate rates/coin on Hotspot cookies**. Cookie divergence is **not proven** as the root cause.

Hotspot auth state (auth vs !auth) **does** change NAT path (`hs-auth` vs `hs-unauth`). The `10.10.10.2` return rule sits on **`hs-unauth`**. Whether an **already-authorized** client still reaches `10.10.10.2` the same way is a separate question; the reported bug is about the **paying / portal** path that works automatically while unauthenticated. Do **not** assume auth-state is the cause without a simultaneous `/ip hotspot active` + browser Network capture.

---

## Android captive portal vs manual browser (comparison constraints)

| Factor | Captive WebView | Manual browser |
|--------|-----------------|----------------|
| Entry URL | Connectivity intercept chain | Typed `/login` |
| `$(link-orig)` | Often connectivity-check URL | Often empty or `/login` |
| Used by Renz-Fi API? | **No** (source) | **No** |
| Network policy | Vendor WebView | Full browser (may differ on cleartext / local network prompts) |
| Proven to explain rates+coin? | **Not without captures** | **Not without captures** |

Do **not** conclude “Android issue” or “Chrome issue” until Network evidence shows which side fails first.

---

## Competing hypotheses

### H1 — MikroTik did not expand `$(mac)` / `$(ip)` on manual `/login`

| | |
|--|--|
| Predicts | Coin/session/heartbeat fail; `#macAddress` empty or literal `$(mac)`; status may show `Device MAC unavailable` |
| Does **not** alone predict | `/api/portal/rates` failure (no MAC) |
| Evidence for | Source hard-depends on servlet expansion |
| Evidence against / gap | Operator says UI loads; MAC field contents **not captured**; rates symptom may be independent |
| Status | **PLAUSIBLE PARTIAL** — not sufficient for rates |

### H2 — Cross-origin calls to `http://10.10.10.2` fail in manual browser but succeed in Captive WebView

| | |
|--|--|
| Predicts | Rates stuck on Loading; coin API fails; branding silent-fail; service notice after ≥2 failures; UI shell still OK |
| Matches | Visual OK + API-backed broken |
| Evidence for | Dual-plane architecture; branding `.catch(() => {})`; rates catch leaves Loading; coin catch → service notice |
| Evidence against / gap | No Network/HAR/serial capture in this pass; NAT return for `10.10.10.2` exists |
| Status | **LEADING HYPOTHESIS for combined rates+coin** — **NOT YET PROVEN** |

### H3 — CORS blocked

| | |
|--|--|
| Evidence against | Firmware sets `Access-Control-Allow-Origin: *` on JSON responses; intentional portal cross-origin design |
| Gap | Live preflight not captured; POST needs OPTIONS |
| Status | **NOT PROVEN**; weak as sole cause given source CORS policy |

### H4 — Live Hotspot bundle still has placeholder / wrong appliance URL

| | |
|--|--|
| Predicts | API base becomes `window.location.origin` (`http://wifi.renz-fi.local`) → portal calls MikroTik, not ESP32 |
| Evidence for | `resolveApplianceBaseUrl` fallback when placeholder remains |
| Evidence against | Final_Build has `http://10.10.10.2`; automatic flow works on same Hotspot html-directory (same files for A and B) |
| Status | **UNLIKELY if A and B load the same `renzfi-app.js`**; still verify live file contents |

### H5 — Hotspot cookie / expiry behavior

| | |
|--|--|
| Status | **REJECTED as root cause for this bug** without new correlation — prior cookie absence was during expiry investigation |

### H6 — ESP32 rejects production plane / guest access only on manual

| | |
|--|--|
| Predicts | 403 JSON from `HttpPlaneGate` |
| Gap | Would appear in Network response; not captured |
| Status | **NOT PROVEN** |

### H7 — Promo storage empty only for manual

| | |
|--|--|
| Status | **REJECTED** — same ESP32 backend; path does not vary by browser |

### H8 — DNS / Hotspot serving completely broken

| | |
|--|--|
| Status | **REJECTED** — operator evidence shows UI loads and DNS resolves |

---

## Exact first divergence (what is proven vs not)

### Proven divergence class (architecture)

1. **Shell plane:** MikroTik Hotspot static/servlet HTML — succeeds on Flow B.  
2. **Application plane:** Browser → `http://10.10.10.2/api/portal/*` (+ optional SSE) — **must succeed** for rates/coin; Flow A proves this plane can work; Flow B symptoms show application plane is **not** healthy in that browser session.

### Not proven (hardware)

The **first failing hop** on Flow B among:

`JS identity gate` → `browser network stack` → `MikroTik forward to 10.10.10.2` → `ESP32 HttpPlaneGate` → `handler` → `JSON parse/UI`

has **not** been isolated with a packet/HTTP capture in this investigation.

---

## Exact proven root cause

### ROOT CAUSE: **NOT YET PROVEN** (end-to-end)

**Confidence in a single named root cause: 40%.**

**Confidence in the dual-plane diagnosis (UI ≠ API init): 95%.**

**Confidence that Flow B’s failure is in the ESP32 API / identity application path rather than “Hotspot cannot serve login.html”: 90%.**

What **is** proven from source + operator evidence:

1. Manual path can load the Hotspot-hosted portal shell.  
2. Application behavior depends on ESP32 at `http://10.10.10.2` and (for coin/session) on MikroTik-expanded `$(mac)`/`$(ip)`.  
3. Rates endpoint does not need MAC; coin does.  
4. Current production JS does not contain the exact user string “Could not start a coin session.”  
5. Automatic success proves the ESP32 API + Hotspot walled/NAT path **can** work from *some* client context on this unit.

What is **not** proven:

1. Whether Flow B’s `#macAddress` / `#ipAddress` are expanded.  
2. Whether Flow B’s browser issues `GET http://10.10.10.2/api/portal/rates` and what status returns.  
3. Whether Flow B’s `POST .../start-coin-session` leaves the device.  
4. Whether live Hotspot `renzfi-app.js` matches `Final_Build_Portal`.  
5. Whether Captive WebView vs Chrome differs in Local Network / cleartext policy on this phone.

---

## Required hardware verification (mandatory before any fix)

Perform on the **same phone**, same Guest SSID, back-to-back.

### Matrix

| Test | Action |
|------|--------|
| A | Automatic captive portal only |
| B | Manual `http://10.20.0.1/login` |
| C | Manual `http://wifi.renz-fi.local/login` |
| D | From phone browser: `http://10.10.10.2/api/portal/rates` (existing route — do not add routes) |

### For A/B/C capture

1. **View Source** (or Elements): exact text of `#macAddress` and `#ipAddress` (screenshot).  
2. **Network**:  
   - `GET http://10.10.10.2/api/portal/rates` — status, CORS errors, body  
   - `GET http://10.10.10.2/api/portal/session?mac=…&ip=…`  
   - `POST http://10.10.10.2/api/portal/start-coin-session` — request JSON + response  
   - `GET http://10.10.10.2/api/events` (SSE)  
3. Console errors (copy verbatim).  
4. Final document URL after redirects.  
5. Parallel MikroTik: `/ip hotspot host print detail` + `/ip hotspot active print detail` at the same moment.  
6. ESP32 serial during B: portal/API log lines for the same timestamps.  
7. On MikroTik: confirm `hotspot/renzfi-app.js` contains `http://10.10.10.2` not the placeholder (file read / export — **read-only**).

### Decision tree after capture

| Observation | Conclusion |
|-------------|------------|
| DOM shows `$(mac)` or blank; rates GET **200** with promos | Root cause = servlet identity expansion / client MAC gate |
| DOM shows real MAC; rates GET **fails** (blocked/reset/timeout) | Root cause = path to `10.10.10.2` from that browser context |
| Rates GET **403/401** from ESP32 | Root cause = HttpPlaneGate / plane classification |
| Rates GET **200**; UI still Loading | Root cause = response shape / frontend normalize |
| Test D works in same browser as B, but portal rates fail | Root cause = portal JS base URL / request construction |
| Test D fails in Chrome but A works | Root cause = browser/WebView network policy difference |

---

## Recommended smallest safe fix (**DO NOT IMPLEMENT YET**)

Only after the matrix names the boundary:

1. **If MAC not expanded:** Keep serving login via Hotspot servlet; add a **read-only** diagnostic overlay showing raw MAC/IP; consider ESP32-side MAC resolution from client IP via Hotspot host table **on router_worker**, not in `async_tcp` — only if proven necessary.  
2. **If API unreachable from Chrome only:** Prefer fixing **reachability/policy**, not RouterOS polling; do not add Hotspot command storms.  
3. **If live JS has placeholder:** Re-upload `Final_Build_Portal` bundle (operational, not a code change).  
4. **Never** “fix” by weakening TWDT, moving RouterOS into HTTP, or removing `10.10.10.2` hs-unauth return without proof.

---

## Regression risks (for any future fix)

| Risk | Why |
|------|-----|
| Break Flow A while fixing Flow B | Automatic path is production-known-good |
| RouterOS CPU | Any new Hotspot polling / sync loops |
| TWDT / Guru Meditation | Durable FS or RouterOS on `async_tcp` |
| Opening guest → ESP32 beyond intentional API | Security / plane separation |
| Dual timer ownership | Already fixed; do not regress |

---

## Frozen baselines (do not weaken)

- `docs/RENZFI_GURU_MEDITATION_PREVENTION_BASELINE.md`  
- Owner setup / Wi-Fi selection async_tcp deferral  
- `STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS` throttle  
- Done Paying → RouterWorker activation path  
- Sales bookkeeping must not gate Internet grant  
- Firmware owns session countdown  

### MikroTik CPU / ESP32 stability constraints

- No continuous `/ip/hotspot/active` polling from ESP32 for this issue.  
- No NAT/DNS/Hotspot profile edits without proven necessity.  
- No watchdog disable/feed/timeout increase.  
- No SD/SPIFFS on `async_tcp`.

---

## Unproven items (explicit list)

1. Live Hotspot file hashes vs `Final_Build_Portal`.  
2. DOM MAC/IP values on Flow B.  
3. HTTP status/bodies for rates and start-coin on Flow B.  
4. Whether operator’s exact error string exists on-device.  
5. Captive WebView vs Chrome Local Network / cleartext differences on the test phone.  
6. Whether Flow B client was auth vs !auth at failure time.  
7. Ethernet/W5500 flap (out of scope; still unproven).  
8. Android “!” captive validation (out of scope; still unproven).

---

## Production-readiness status

| Gate | Status |
|------|--------|
| Forensic source map of divergence class | Complete |
| Exact end-to-end root cause | **NOT PROVEN** |
| Hardware request matrix | **NOT RUN** |
| Fix | **NOT STARTED** (correct) |
| Production readiness for a “manual login fix” | **NOT READY** |

---

## Final statements

**NO CODE CHANGES MADE.**  
**NO MIKROTIK CHANGES MADE.**  
**NO FIRMWARE FLASHING PERFORMED.**  

**ROOT CAUSE: NOT PROVEN** (end-to-end).  

**CONFIDENCE: 40%** for any single named root cause; **95%** that Flow B fails in the ESP32 API / identity application plane while the MikroTik-hosted shell succeeds.

**Do not proceed to implementation until the hardware verification matrix isolates the first failed boundary.**

---

## Permanent regression note

> **Automatic captive-portal flow is a known-good production path and must not be broken by future manual-login, portal, Hotspot, frontend, API, or router changes.**
