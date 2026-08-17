# Authenticated customer portal re-entry — forensic

**Mode:** FORENSIC ONLY. No code, MikroTik, portal rebuild, or flash.  
**Date:** 2026-08-13  
**Symptom:** After CONNECTED, typing `http://10.20.0.1/login` ends at `http://wifi.renz-fi.local/status` → browser “This site can't be reached”. Unauthenticated `http://10.20.0.1/login` still opens the Renz-Fi portal.

---

## 1. Executive finding

**ROOT CAUSE: PROVEN**

An already-authenticated Hotspot client that requests `/login` never receives Renz-Fi `login.html`. MikroTik Hotspot answers that servlet with its **authenticated status URL**, built from the live profile `dns-name`:

`http://wifi.renz-fi.local/status`

That hostname string does not exist in `portal/renzfi-app.js`, `portal/login.html`, or ESP32 firmware sources. Firmware never emits `=dns-name=`. Production overlay does not ship `status.html`.

**FIRST FAILURE BOUNDARY:** authenticated `GET /login` is consumed by the Hotspot servlet **before** Renz-Fi HTML/JS runs.

A **second, separate failure** then occurs: the redirected host is unreachable (“can't be reached”). That error is **not** an HTTP 404 from a missing `status.html`. The exact DNS vs mDNS vs TCP mechanism is **UNPROVEN** without a packet capture.

---

## 2. Exact observed behavior

| State | Customer action | Result |
|-------|-----------------|--------|
| Not authenticated | `http://10.20.0.1/login` | Renz-Fi captive portal opens |
| CONNECTED (Internet authorized) | same URL | Browser navigates to `http://wifi.renz-fi.local/status` |
| CONNECTED | that status URL | “This site can't be reached” |

Pause / Resume / Terminate / Done Paying / Internet grant are out of scope and reported working.

---

## 3. Existing MikroTik evidence (operator-captured, not changed here)

Hotspot server `hotspot-renzfi`:

- `interface=bridgeGuest`
- `address-pool=pool-guest`
- `profile=RenzFi-Hotspot`
- `ip-of-dns-name=10.20.0.1`

Hotspot profile `RenzFi-Hotspot`:

- `hotspot-address=10.20.0.1`
- `dns-name="wifi.renz-fi.local"`
- `html-directory=hotspot`
- `login-by=cookie,http-chap,http-pap`

DNS: `wifi.renz-fi.local` → `10.20.0.1`

Authorized example: MAC `06:36:E3:2C:C4:E8`, IP `10.20.0.253`, `login-by="admin"` (RouterOS API Hotspot login, not the HTML CHAP form).

---

## 4. Customer portal architecture

```
Guest browser
  → MikroTik Hotspot HTTP (10.20.0.1, html-directory=hotspot)
  → login.html + renzfi-app.js (only if the /login servlet serves them)
  → ESP32 API http://10.10.10.2/api/portal/*  (walled-garden)
```

Canonical source: `portal/`  
Build: `RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal`  
Upload overlay (`scripts/captive-portal-upload-files.mjs`): `login.html`, `renzfi-app.js`, `renzfi-style.css`, `md5.js`, banner, audio. **No `status.html`.**

This repository already documents RouterOS Hotspot **servlet types** (`/`, `/login`, `/status`, `/logout`) mapping to fixed filenames (`login.html`, `status.html`, …) in `ESP32_S3_Firmware/docs/ADMIN_HOTSPOT_CLEAN_URL_FORENSIC.md`. `/status` is a Hotspot servlet, not an ESP32 route and not a Renz-Fi SPA path.

---

## 5. Complete /login request flow (NOT authenticated)

```
Wi-Fi associate
  → Hotspot intercept (client not in /ip/hotspot/active)
  → GET http://10.20.0.1/login   (or any HTTP, rewritten to login)
  → Hotspot servlet type /login
  → hotspot/login.html
  → MikroTik substitutes $(ip) $(mac) $(chap-id) $(link-login-only) …
  → renzfi-app.js init()
  → GET http://10.10.10.2/api/portal/session?mac=…&ip=…
  → customer UI (Waiting / coin / Activating / …)
```

`portal/login.html` lines 15–37 (`$(if chap-id)` / `doLogin`) are CHAP boilerplate. There is **no** `document.login` form. `doLogin()` is never bound. It does not run on page load.

---

## 6. Complete authenticated /login request flow (FAILING)

```
Client already in /ip/hotspot/active (login-by=admin)
  → browser GET http://10.20.0.1/login
  → Hotspot servlet: client is logged in
  → HTTP redirect to status URL using profile dns-name
       http://wifi.renz-fi.local/status
  → browser leaves 10.20.0.1
  → connection to wifi.renz-fi.local fails
  → login.html NEVER served
  → renzfi-app.js NEVER runs
```

Renz-Fi JS cannot be the navigator: it is not loaded on this path.

---

## 7–8. Exact redirect origin

**Component:** MikroTik Hotspot HTTP servlet (wproxy / hotspot www), not Renz-Fi.

**Proof the URL is Hotspot-constructed:**

| Fact | Evidence |
|------|----------|
| Observed host | `wifi.renz-fi.local` |
| Live profile | `dns-name="wifi.renz-fi.local"` |
| Observed path | `/status` = documented Hotspot servlet |
| Portal JS | no `wifi.renz-fi.local`, no `/status` navigation |
| Firmware C++ | **zero** matches for `wifi.renz-fi.local` or `10.20.0.1` |
| Firmware RouterOS | **zero** `=dns-name=` commands |
| Firmware unused default | `RouterProvisioningTypes.h:53` `dnsName = "wifi.renzfi.local"` (**different spelling**, never written to Hotspot) |

Exact Renz-Fi function/line that issues the redirect: **none**. There is no such function.

Exact MikroTik HTTP status code (302 vs meta-refresh): **UNPROVEN** without capturing the `/login` response headers. The destination URL is proven by the address bar and by `dns-name`+servlet mapping.

---

## 9. renzfi-app.js involvement

**NO** on the failing path.

Navigation search in `portal/renzfi-app.js`:

| Line | Code | Role |
|------|------|------|
| 28 | `window.location.origin.replace(...)` | Appliance URL fallback when placeholder not substituted (ESP32-served copy only) |
| 80–98 | `getDeviceMAC` / `getDeviceIP` / `deviceParams` | Read `$(mac)` / `$(ip)` from DOM |
| 617–622 | `fetchSession` | `GET /api/portal/session?mac=&ip=` |
| 1480–1534 | `init` | Fetch session, bind pause/terminate/coin — **no location change** |

No `location.href`, `location.replace`, `location.assign`, `link-status`, `dst` navigation, or `/status` construction.

`portal/admin.html` line 9 `location.replace(.../admin)` is the **owner** launcher, not customer `/login`.

---

## 10. login.html involvement

**NO** as the redirector on the failing path (page is not served).

| Lines | Content | After auth? |
|-------|---------|-------------|
| 15–37 | `$(if chap-id)` hidden POST to `$(link-login-only)`, `doLogin()` | Only if login.html is served **and** chap-id present **and** something calls `doLogin` — nothing does |
| 17–20 | `dst=$(link-orig)` | CHAP post target, not a GET redirect |
| 70–74 | `$(ip)` `$(mac)` | Identity when page is served |
| 239 | `renzfi-app.js` | Runs only if HTML loads |

No `$(if logged-in)`, no meta-refresh to `/status`, no `$(link-status)`.

---

## 11. Firmware involvement

**NO** for generating `wifi.renz-fi.local/status`.

| Route | File | Role |
|-------|------|------|
| `GET /api/portal/session` | `ApiServer.cpp:2910` | Requires `mac`; returns session JSON |
| `/api/portal/branding`, heartbeat, terminate, rates, done-paying, events | `ApiServer.cpp` | JSON/SSE. No 302 to Hotspot `/status` |
| `GET /api/status` | `ApiServer.cpp:912` | **Owner health JSON on ESP32**, not Hotspot `/status` |
| `GET /api/setup/status` | `SetupServer.cpp:618` | Wizard |
| Portal HTML substitute | `PortalTemplate.cpp:60–86` | ESP32 **recovery** `/portal` only: `$(link-login-only)` → `http://<W5500Config::GATEWAY>/login`. Not the production Hotspot path. Does not emit `wifi.renz-fi.local` or `/status`. |
| `PortalServer.cpp:32–33` | Comment: customer portal is served by MikroTik; ESP32 `/portal` is recovery only |

---

## 12. MikroTik involvement

**YES — PROVEN** as the component that selects `/status` and fills the host from `dns-name`.

Supporting in-repo documentation (not a live packet, but the servlet contract):

- `ESP32_S3_Firmware/docs/ADMIN_HOTSPOT_CLEAN_URL_FORENSIC.md` lines 7–9: servlet types `/`, `/login`, `/status`, `/logout`.
- `ESP32_S3_Firmware/docs/ADMIN_GATEWAY_10_20_0_1_FORENSIC.md`: `10.20.0.1/login` owned by Hotspot `html-directory=hotspot`.
- `ESP32_S3_Firmware/docs/FINAL_RELEASE_FORENSIC_VALIDATION.md` line 38: production overlay is Renz-Fi `login.html`; `status.html` is **not** emitted by `build-mikrotik-portal.mjs`.

---

## 13. DNS involvement

**YES as the redirected hostname. UNPROVEN as the sole reason “can't be reached”.**

Proven:

- Redirect host = profile `dns-name`.
- Unauthenticated success used the **IP** `10.20.0.1` (no DNS).
- Authenticated failure used the **name** `wifi.renz-fi.local`.

UNPROVEN without capture:

- Client after auth still uses MikroTik DNS vs public DNS vs Chrome/Safari **mDNS for `.local`**.
- Name resolves to `10.20.0.1` but TCP/80 fails.
- Name does not resolve at all.

**RULED OUT as the “can't be reached” error:** missing `status.html`. A missing file yields an HTTP response (404 or default Hotspot HTML), not a transport-level “can't be reached”.

---

## 14. Browser involvement

**YES as the client that follows the Hotspot redirect and then fails to open the name.**  
**UNPROVEN:** captive-portal helper vs ordinary navigation (user typed the URL).  
**RULED OUT:** Renz-Fi JS `location.*` navigation.

---

## 15. First divergence (working vs failing)

| Step | Flow A (not auth) | Flow B (CONNECTED) |
|------|-------------------|--------------------|
| 1 | GET `10.20.0.1/login` | GET `10.20.0.1/login` |
| 2 | Hotspot: not logged in → serve `login.html` | Hotspot: logged in → **status URL** |
| 3 | Renz-Fi UI + ESP32 API | Browser opens `wifi.renz-fi.local/status` |
| 4 | Session render | **FAIL — host unreachable** |

**First divergence: step 2.** Same request, different Hotspot servlet outcome based on `/ip/hotspot/active`.

---

## 16–17. Proven root cause and evidence

**PROVEN root cause:**  
MikroTik Hotspot authenticated `/login` handling redirects the client to the Hotspot **status servlet** at `http://<dns-name>/status` (`http://wifi.renz-fi.local/status`). Renz-Fi `login.html` / `renzfi-app.js` do not run and do not construct that URL.

**Evidence:**

1. Observed destination matches live `dns-name` + servlet `/status`.
2. Exhaustive portal search: no status navigation.
3. Exhaustive firmware search: no `wifi.renz-fi.local`, no `10.20.0.1`, no `=dns-name=`.
4. Production upload list has no `status.html`.
5. In-repo Hotspot servlet documentation.
6. Working path uses IP `/login` while unauthenticated (interception + `login.html`).

**PROVEN secondary symptom:** after that redirect, the browser cannot open the status host.

**UNPROVEN secondary mechanism:** DNS NXDOMAIN vs `.local` mDNS vs TCP to `10.20.0.1:80`.

---

## 18. Investigated and ruled out

| Hypothesis | Verdict |
|------------|---------|
| `renzfi-app.js` detects auth and sets `location` to `/status` | **RULED OUT** — no such code; JS not loaded on fail path |
| `login.html` meta-refresh / `$(link-status)` | **RULED OUT** — not in source |
| `doLogin()` POST | **RULED OUT** — never called; no `document.login` |
| ESP32 `/api/portal/*` 302 | **RULED OUT** — JSON/SSE only |
| ESP32 `GET /api/status` | **RULED OUT** — different origin (`10.10.10.2`) and JSON health |
| Firmware writes `dns-name=wifi.renz-fi.local` | **RULED OUT** — no RouterOS `dns-name` set in source |
| Missing `status.html` causes “can't be reached” | **RULED OUT** — that error is not an HTTP body |
| Coin / promo / sales / pause backend | **RULED OUT** — not on this HTTP path |
| W5500 / Ethernet / TWDT | **RULED OUT** — not on this HTTP path |
| Wizard / SD / Active Users | **RULED OUT** |

---

## 19–21. Can `/login` remain the single customer entry point?

### As the browser URL for an already-logged-in Hotspot client

**NO**, not while RouterOS keeps the documented servlet rule: logged-in `/login` → `/status`. Changing that is a **MikroTik Hotspot behavior/config** change. This forensic pass does not authorize it.

A `status.html` that 302s back to `/login` would **loop** (`/login` → `/status` → `/login`).

### As the single Renz-Fi **application** (same UI, possibly different servlet path)

**YES**, with a **Hotspot HTML overlay** change only (no NAT/firewall/DNS redesign required for the first implementation):

**Minimum safe implementation boundary (DO NOT IMPLEMENT IN THIS PASS):**

1. Treat Hotspot servlets as two filenames for one app: `login.html` (unauthenticated `/login`) and `status.html` (authenticated `/status`).
2. `status.html` must be the same Renz-Fi shell as `login.html` (`$(ip)`, `$(mac)`, `renzfi-app.js`). MikroTik substitutes those variables on `status.html` as well.
3. Do **not** invent a second session. `init()` already `GET /api/portal/session?mac=&ip=` and renders Connected / Paused / Add Time / Terminate from firmware capabilities (`canPause`, `canResume`, `canTerminate`, `canInsertCoin`).
4. Reachability of `wifi.renz-fi.local` is a **separate** hardware check. If `http://10.20.0.1/status` works but the `.local` name does not, DNS/mDNS is the second bug. Do not change Hotspot DNS until that test is captured.
5. Do not add polling, RouterOS from async_tcp, or a second Internet-grant path.
6. Canonical edit remains `portal/`. Generate overlay; do not hand-edit `Final_Build_Portal/`.

`alogin.html` is not required for this re-entry bug: CONNECTED used `login-by=admin` (API), not the HTML CHAP form, so `alogin.html` was never the re-entry page.

---

## Session identity (Part 5 / 9 / 10)

| Item | Source |
|------|--------|
| MAC | `login.html:74` `$(mac)` → `getDeviceMAC()` `renzfi-app.js:80-83` |
| IP | `login.html:70` `$(ip)` → `getDeviceIP()` `:85-88` |
| API | `GET /api/portal/session?mac=&ip=` `renzfi-app.js:617-622`; firmware `ApiServer.cpp:2914-2918` rejects empty MAC |
| Unauthenticated captive | Hotspot fills `$(mac)` `$(ip)` — **PROVEN** by working portal |
| Manual `/login` while unauth | Same `login.html` — **PROVEN** |
| Authenticated `/login` | HTML not served — identity N/A until `status.html` (or `/login` without redirect) is served |
| Connected dashboard | Same `login.html` UI already has Pause / Resume / Terminate / Add Time — **PROVEN** in `renzfi-app.js` render/bind. Missing piece is **getting that HTML to load**, not missing session fields. |

If `$(mac)` were left unsubstituted, `getDeviceMAC()` returns `""` and `fetchSession` rejects `"MAC address unavailable"`. That is a different failure (portal visible, API blocked), not “can't be reached”.

---

## 22. Required hardware verification (next pass, still no product claim)

Capture **while CONNECTED**, same phone:

1. `GET http://10.20.0.1/login` — HTTP status, `Location` header, whether body is `login.html`.
2. `GET http://10.20.0.1/status` — status, body (default Hotspot vs 404 vs empty).
3. `GET http://wifi.renz-fi.local/status` — DNS result, TCP, HTTP.
4. nslookup/mDNS of `wifi.renz-fi.local` **after** auth vs **before**.
5. Confirm `/ip/hotspot/active` still shows the client.

Do not flash, do not change Hotspot config, do not implement overlay until this capture exists for the reachability half.

---

## Confidence split

| Claim | Grade |
|-------|--------|
| Redirect target is Hotspot `dns-name` + `/status` | **PROVEN** |
| Renz-Fi JS/HTML/firmware do not emit that URL | **PROVEN** |
| First divergence is authenticated `/login` servlet | **PROVEN** |
| HTTP 302 vs other redirect mechanism | **UNPROVEN** (need headers) |
| Why `.local` is unreachable after auth | **UNPROVEN** |
| `/login` can be the authenticated browser URL without Hotspot change | **PROVEN NO** |
| Same Renz-Fi UI can be served from `status.html` | **PROVEN** as architecture; not hardware-tested |

---

## Files inspected

- `portal/login.html`
- `portal/renzfi-app.js`
- `portal/admin.html`
- `portal/md5.js`
- `portal/renzfi-style.css`
- `scripts/captive-portal-upload-files.mjs`
- `scripts/build-mikrotik-portal.mjs`
- `deployment/mikrotik-hotspot/index.html` (admin launcher; not in overlay file list)
- `ESP32_S3_Firmware/src/ApiServer.cpp` (portal + `/api/status`)
- `ESP32_S3_Firmware/src/web/PortalTemplate.cpp`
- `ESP32_S3_Firmware/src/web/PortalServer.cpp`
- `ESP32_S3_Firmware/src/RouterWirelessAdapter.cpp` (html-directory only)
- `ESP32_S3_Firmware/src/RouterProvisioningTypes.h`
- `ESP32_S3_Firmware/src/RouterProvisioningManager.cpp` (dnsName JSON only)
- `ESP32_S3_Firmware/docs/ADMIN_HOTSPOT_CLEAN_URL_FORENSIC.md`
- `ESP32_S3_Firmware/docs/ADMIN_GATEWAY_10_20_0_1_FORENSIC.md`
- `ESP32_S3_Firmware/docs/FINAL_RELEASE_FORENSIC_VALIDATION.md`
- `ESP32_S3_Firmware/docs/ADMIN_CAPTIVE_PORTAL_HOTSPOT_INTERCEPTION.md`

No `status.html` under `portal/` or the production overlay list.
