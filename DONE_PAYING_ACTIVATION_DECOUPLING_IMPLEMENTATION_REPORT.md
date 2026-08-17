# Done Paying Activation Decoupling — Implementation Report

Date: 2026-08-10  
Scope: Coin `donePaying` only — wall clock no longer blocks Internet; voucher unchanged  
Verdict: **IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

## Release verdict

Software integration: **PASS**  
Production ready: **NO** until hardware checklist below passes on ESP32-S3 + W5500 + SD + MikroTik + coin + captive portal.

## Why this change

Architecture forensic concluded sales wall clock is **downstream bookkeeping**. Coin entitlement (`credits` / `purchasedMinutes` / `secondsLeft`) is millis-based and complete without NTP. Blocking RouterWorker on empty `salesRecordedAtNow()` denied paid customers Internet.

## Why only the coin path

- Coin sessions: timer = `secondsLeft` + millis tick — no calendar required  
- Voucher: absolute `serviceExpiresAt` / calendar rules intentionally **unchanged**  
- Coin and voucher remain separated product paths  

## Files modified

| File | Change |
|---|---|
| `ESP32_S3_Firmware/src/PortalSessionManager.h` | `donePaying(mac, errorCode)` |
| `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | Decouple clock; activate-then-sale; credit rollback on enqueue fail |
| `ESP32_S3_Firmware/src/ApiServer.cpp` | Map real error codes; `NO_CREDITS` only when credits are zero |
| `DONE_PAYING_ACTIVATION_DECOUPLING_IMPLEMENTATION_REPORT.md` | This report |

**Unchanged:** RouterWorker, MikroTik driver, VoucherManager, redeem/reconnect, StorageManager, Captive Portal JS, Final_Build_Portal, Coin ISR, Sales storage schema.

## Exact behavior change

### Before

```
donePaying
  → salesRecordedAtNow() empty
  → return false
  → HTTP 400 NO_CREDITS
  → no Activating, no RouterWorker, no Internet
```

### After

```
donePaying
  → validate session / credits / minutes
  → recordedAt = wall clock OR "uptime-ms:<millis>"
  → session → Activating, secondsLeft set, credits cleared
  → enqueueActivateSession (RouterWorker path)
       fail → restore credits + WaitingCoin/Active, HTTP 503 ACTIVATION_QUEUE_FULL
  → enqueueRecordSale (best-effort; failure does not revoke activation)
  → HTTP 200 Session activating
```

## Functions modified

- `PortalSessionManager::donePaying`
- ApiServer `POST /api/portal/done-paying` error branch

## Error handling (Implementation E)

| Condition | HTTP | Code |
|---|---|---|
| credits == 0 (non-idempotent) | 400 | `NO_CREDITS` |
| no purchased minutes | 400 | `NO_MINUTES` |
| activation queue full (credits restored) | 503 | `ACTIVATION_QUEUE_FULL` |
| session missing | 404 | `SESSION_NOT_FOUND` |
| voucher session | 409 | `VOUCHER_SESSION` |
| success | 200 | Session activating |

## Atomicity (Implementation F)

| Event | Behavior |
|---|---|
| `enqueueActivateSession` fails | Credits / minutes / prior remaining restored; state `waiting_coin` (or Active for add-time); no Internet |
| RouterWorker later fails authorize | Existing outcome path → `activation_error` / retry (unchanged worker) |
| Sale enqueue fails | Activation **continues**; log `salesSaved=false` |

Note: HTTP handler cannot await MikroTik authorize (async RouterWorker). “Grant Internet” remains async after successful enqueue — same as prior design. Enqueue failure is now credit-safe.

## Build / resources

Env: `freenove_esp32_s3_wroom` — **SUCCESS**

| Metric | Result |
|---|---|
| Compile | PASS (`freenove_esp32_s3_wroom`) |
| Static RAM | 106,292 / 327,680 (32.4%) — unchanged vs prior build |
| Flash | 2,383,327 / 2,621,440 (90.9%) |
| Idle RouterOS commands | Unchanged (0 when idle) — no new polls |
| RouterWorker / MikroTik command set | Unchanged |
| CPU / DMA architecture | Unchanged |

## Hardware validation checklist (required)

- [ ] **Test 1** Cold boot, no NTP → insert coin → Done Paying → Internet granted  
- [ ] **Test 2** ₱2 / 10 min → Connected, timer, RouterWorker, MikroTik auth  
- [ ] **Test 3** Router disconnected → activation fails, credits remain, no timer  
- [ ] **Test 4** NTP unavailable → Internet granted; sale uses `uptime-ms:` marker  
- [ ] **Test 5** NTP later OK → deferred/existing sales compatible; no replay redesign  
- [ ] **Test 6** Session expiry → disconnect → Waiting for Payment  
- [ ] **Test 7** Idle RouterOS = 0 cmds/min  
- [ ] **Test 8** Heap / DMA / ESP32 CPU / Router CPU / TWDT stable  
- [ ] Voucher redeem/reconnect unchanged (still clock-gated as before)  
- [ ] HTTP never returns `NO_CREDITS` when credits &gt; 0  

## Regression analysis

| Area | Expected |
|---|---|
| Coin insert / credits / minutes / promos | Unchanged |
| Voucher | Unchanged |
| RouterWorker / ROS cmd count | Unchanged protocol |
| SD / SPIFFS / replay / backup | Unchanged |
| Admin / portal JS | Works with 200 activating; better errors on 503 |
| Sales reports “today” | May under-count until wall clock available for new uptime-marked sales — accepted tradeoff |

## Final statement

**IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

Do not mark RELEASE CANDIDATE or Production Ready until the appliance checklist passes.
