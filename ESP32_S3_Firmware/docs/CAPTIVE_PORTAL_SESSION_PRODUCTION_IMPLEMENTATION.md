# Captive Portal Session — Production Implementation

**Verdict target:** SOURCE FIXED — HARDWARE VALIDATION REQUIRED  
**Topology (do not redesign):** Phone → production SSID → wireless bridge → `bridgeGuest` → Hotspot `hotspot-renzfi` → `/ip/hotspot/host`

## Architecture

| Plane | Authority |
|-------|-----------|
| ESP32 | MAC session, money, purchased/remaining time, promo, pause flag, persistence |
| MikroTik | Hotspot authorization, traffic access, user/profile, active host, rate-limit |
| Browser | Presentation only |

All RouterOS mutations are serialized through `router_worker` (Critical priority for activate/pause/deauth). Idle portal operation issues **0 RouterOS commands/min**.

## Canonical session states

`idle` → `waiting_coin` → `activating` → `active` ↔ `paused` → `expired`  
Recoverable: `activation_error` (purchased `secondsLeft` preserved; Resume retries activation).

## Money / time / promo

1. GPIO coin ISR → debounce → pulse group → `CoinManager` → `PortalSessionManager::onCoinInserted`
2. Promo resolution (`PromoManager::resolveForAmount`): **highest single promo with `coin <= amount`**; fallback `amount * 5` only when no promos load
3. Same algorithm for View Rates, Insert Money `purchasedMinutes` (GET `/api/portal/session`), Done Paying, Add Time
4. Browser never invents `credits * 5`

## Done Paying auth contract

- Hotspot **username** = MAC with `:` removed, uppercase  
- Hotspot **password** = same as username  
- Worker job: `/ip/hotspot/user/print` + `set|add`, then `/ip/hotspot/active/print` + `login` (or `active/set` limit-uptime if already active)  
- UI shows **Connected** only after worker outcome `ok` (`connected=true`, `sessionState=active`)

## Add Time

`newRemaining = existingRemaining + purchasedSeconds`  
Profile: keep existing `hotspotProfile` when adding time. RouterOS: in-place user update with **cumulative uptime compensation** (`limit-uptime = user.uptime + secondsLeft + grace`) + active remaining refresh (no disconnect when already active).

## Pause / Resume

- **Pause:** freeze ESP32 timer; worker `pauseHotspotUser` = remove active + targeted cookie; keep user  
- **Resume:** keep timer frozen until activate outcome; then `active` + `connected`

## Terminate / Expire

- One enqueue per session (`routerCleanupQueued` guard)  
- Worker: active remove + user remove + targeted cookie remove  
- `cleanupExpired()` does not delete JSON until `routerCleanupComplete` (or never had router auth)

## Command budgets (target)

| Operation | Sessions | Commands (approx) |
|-----------|----------|-------------------|
| Idle portal | 0/min | 0/min |
| Activate / Resume / Add Time | 1 | ~4 (user print+set/add, active print+login/set) |
| Pause | 1 | ~4 (active + cookie) |
| Terminate / Expire | 1 | ~6 (active + user + cookie) |
| Speed profile create | 1 | existing managed-profile path (idempotent) |

## Concurrency

Sessions keyed by MAC. Many customers may enqueue independently; `router_worker` runs **one** RouterOS API session at a time. No per-heartbeat / per-tick / per-coin RouterOS.

## Portal build

- Canonical source: `portal/`  
- Output: `deployment/mikrotik-hotspot/`  
- `Captive Portal/` is deprecated (see its README)  
- Build fails if `RENZFI_APPLIANCE_BASE_URL` declaration still unsubstituted or if production JS contains `credits*5`

## Hardware validation

Run checklist A–Q from the production fix brief (captive intercept, rates, coins, Done Paying with `/ip/hotspot/active` proof, speed, add time, pause/resume, terminate, expire, reconnect, multi-client, command storm, MikroTik CPU, ESP32 stability). Do **not** claim hardware PASS from source/build alone.
