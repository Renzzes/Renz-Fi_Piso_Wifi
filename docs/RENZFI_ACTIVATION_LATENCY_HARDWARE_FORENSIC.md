# Activation latency — hardware-backed forensic (2026-08-13)

Hardware: COM19, mac `06:36:E3:2C:C4:E8`. No architecture rewrite.

## 1. Files / functions

| Stage | File | Function |
|-------|------|----------|
| DONE PAYING | `ApiServer.cpp` | `POST /api/portal/done-paying` |
| Reserve + Activating | `PortalSessionManager.cpp` | `donePaying()` sets `sessionState=activating`, `connected=false` |
| RouterWorker enqueue | `PortalSessionManager.cpp` | `onSessionActivated()` → `tryEnqueueActivateHotspotUser` |
| Worker | `RouterProvisioningWorker.cpp` | `runOp` `ActivateHotspotUser` |
| RouterOS | `MikroTikDriver.cpp` | `createHotspotUser` → `openRouterSession` → `loginHotspotActive` |
| TCP/login | `RouterOsClient.cpp` | `connect()`, `login()` |
| Connect gate | `RouterApiTransportGate.cpp` | `waitUntilConnectAllowed()` |
| Outcome | `PortalSessionManager.cpp` | `drainHotspotOutcomes()` |
| SSE | `emitSessionEvent(..., "portal.session.connected")` |
| UI Connected | `portal/renzfi-app.js` | `renderStatus()` only if `sessionState==="active" && connected` |

## 2–6. Current sequences (source)

**State:** DONE PAYING → `Activating` + `connected=false` → RouterOS `active/login` ok → `Active` + `connected=true` → SSE.

**Frontend Connected sources:** SSE `portal.session.connected`, then HTTP GET fallback. Not optimistic. `handleDonePaying` forces “Activating…” until `waitForActivation` sees `active && connected`.

**RouterOS (unchanged budget):** user/print, user/add\|set, active/print, active/login\|set. One API session. One RouterWorker.

## 7. Measured boundaries (this log)

| Metric | ms | Meaning |
|--------|----|---------|
| T1−T0 | 0 | validation |
| T2−T0 | 2075 | entitlement reserve (includes promo SD `resolveHighestProfileForAmount`) |
| T3−T0 enqueue | 2327 | includes that reserve |
| workerQ | −251 | **instrumentation:** worker started before `markT3` (not a time machine) |
| **rosLogin T5−T4** | **3580** | **openRouterSession: gate + TCP + login** (not the 4 cmds) |
| user print/add/active print/login | 231+195+134+132 = **692** | actual Hotspot cmds |
| router-budget | 4286 | ≈ 3580 + 692 + close |
| rosAuth T8−T6 | 700 | cmds + command pacing |
| resultPub | 191 | outcome → SSE |
| total_esp | 6631 | T10−T0 |

## 8. Remaining bottleneck (proven by subtraction, split next)

Four Hotspot commands ≠ 4286 ms. Remainder **~3.6 s = rosLogin**.

That interval is **inside** `RouterOsClient::connect()` + `login()`, which includes:

1. `waitUntilConnectAllowed()` — **`ROUTER_API_MIN_CONNECT_INTERVAL_MS = 5000`**. Remaining wait of **3580 ms** matches “last API connect ~1.4 s earlier”. Failure backoff is 10 s (does not match 3580).
2. W5500 `EthernetClient::connect()` TCP to `:8728` (SYN retries can also be ~3 s).
3. `/login` sentence (usually tens–hundreds of ms if TCP is already up).

**Not yet split in the old log.** Next Serial lines will print `connect_gate_wait`, `tcp_connect`, `ros_login` separately.

Do **not** rewrite the RouterOS login protocol. If `ros_login` is small and `tcp_connect` or `connect_gate_wait` is ~3.5 s, the driver handshake is not the problem.

## 9. CONNECTED vs Internet (already correct in source)

`connected=true` is set only in `drainHotspotOutcomes` after `outcome.ok` from `loginHotspotActive` (`/ip/hotspot/active/login`). UI cannot show Connected from the DONE PAYING HTTP body (`connected=false`).

No second grant path.

## 10. Coin dQueueWait=13776 (separate path)

`portal.coin.credit` is `PortalWorkType::EmitSessionEvent` on the **portal deferred FIFO** (`processDeferredWork` = **one item per `loop()`**).

T4_q=86922 (queued at credit). T5_emit=100698 (**after** activation T10=96420).

So the coin SSE job sat in that FIFO for 13.8 s while later DONE PAYING `SaveSessions` / `RecordSale` / loop work ran. RouterOS is on **router_worker** (different task); the stall is **portal deferred queue + likely SD `SaveSessions` at the head**, not Hotspot login.

Do not “fix” coin by adding RouterOS workers or faster polling.

## Safe change this round

Implemented (smallest, no architecture change):

1. Split Serial timings: `connect_gate_wait`, `tcp_connect`, `ros_login`, `load_credentials`.
   Next hardware log proves whether the 3580 ms is the 5s gate, W5500 TCP, or `/login`.
2. Stamp T3 **before** `onSessionActivated()` so `workerQ` is not negative.
3. Coin deferred-queue snapshot at T4 (`[coin-latency] queue depth=… items=…`) and drain lines while the coin trace is armed. **No queue reordering yet.**
4. Lifecycle test: Connected requires `sessionState=active` **and** `connected=true`.

**Not changed (unproven or out of scope):**

- `ROUTER_API_MIN_CONNECT_INTERVAL_MS = 5000` still applies to every job, including Critical.
  `endJob(success)` already clears `g_lastConnectAttemptMs`, so a successful previous job does **not** impose the 5s wait. 3580 ms is therefore more likely **TCP connect or SPI contention** than the gate. Do not skip the interval until `connect_gate_wait` is measured large.
- RouterOS login protocol / driver: not rewritten.
- Portal UI Connected rules: already `active && connected` only after `portal.session.connected`.
- Coin FIFO order / one-item-per-loop: measured only.
- No second Hotspot grant path.

## Hardware follow-up (required before claiming improvement)

Capture ≥10 DONE PAYING activations. For each, record:

```
[activate-latency] load_credentials=…
[activate-latency] connect_gate_wait=… tcp_connect=… ok=1
[activate-latency] ros_login=… ok=1
[activate-latency] mac=… rosLogin=… rosAuth=… total_esp=…
[coin-latency] queue depth=… items=…
```

If `connect_gate_wait≈3580`: then (and only then) consider skipping min-interval for Critical, keeping failure backoff.

If `tcp_connect≈3580` and `ros_login` is small: remaining latency is W5500 TCP to `:8728` (possibly SD/W5500 SPI contention). Do not rewrite the RouterOS login driver.

If `ros_login≈3580`: remaining latency is RouterOS session/login. Do not randomly rewrite the driver.
