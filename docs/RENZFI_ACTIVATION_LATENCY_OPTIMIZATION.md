# Paid session activation latency optimization

**Mode:** source-validated · hardware unvalidated  
**Date:** 2026-08-13  
**Class:** latency only — no RouterOS/MikroTik/config/payment/entitlement rewrite

## Forensic path (before)

```
POST /api/portal/done-paying  (async_tcp)
  → PortalSessionManager::donePaying
      [SD] PromoManager::resolveHighestProfileForAmount  UNDER session lock
      reserve entitlement (Activating, secondsLeft, connected=false)
      enqueueSaveSessions()          ← FIRST deferred item
      enqueueActivateSession()       ← SECOND
      enqueueRecordSale()
  → loop: processDeferredWork() ONE item/loop
      SaveSessions → saveToSD          ← blocks RouterWorker dispatch
  → next loop: onSessionActivated
      tryEnqueueActivateHotspotUser
  → RouterWorker ActivateHotspotUser
      openRouterSession (connect+login)
      /ip/hotspot/user/print
      /ip/hotspot/user/add|set
      /ip/hotspot/active/print
      /ip/hotspot/active/login|set
      closeRouterSession
  → HotspotOutcome
  → drainHotspotOutcomes
      [SD] markSalesActivatedBySessionId   ← before CONNECTED
      enqueueSaveSessions()                ← before SSE
      enqueue EmitSessionEvent connected   ← waits another loop
  → portal waitForActivation HTTP GET every 1s
```

## Bottlenecks addressed (smallest safe changes)

1. **Queue order:** dispatch RouterWorker **before** SaveSessions/sale.
2. **Double hop:** `donePaying` calls `onSessionActivated` immediately (queue only — still no RouterOS on async_tcp). Fallback remains `enqueueActivateSession` if the worker is busy.
3. **Promo SD** moved off the session lock (still before reserve; not removed).
4. **CONNECTED publish:** `emitSessionEvent` immediately on loopTask; coin sale activation **after** SSE.
5. **UI:** observe SSE every 250ms locally; HTTP GET fallback 2s (was 1s HTTP).

## Preserved

- Single RouterWorker, serialized API, command pacing, deadlines, idempotent user print+set/add+active login
- Timer still frozen until Active+connected (`timerRunning`)
- No RouterOS polling, no parallel API sessions, no MikroTik config change
- Pause/Resume, coin accounting, promo insertion contract, CORS, TWDT baseline

## Instrumentation

Serial one-liner after SSE:

`[activate-latency] mac=… enqueue= workerQ= rosLogin= rosAuth= resultPub= total_esp=`

Browser console: `[activate-latency] T11_wait_start` / `T12_connected`

## RouterOS command budget (unchanged)

~4 commands, 1 API session: user/print, user/add|set, active/print, active/login|set

## BEFORE / AFTER (source estimate — not hardware)

BEFORE  
DONE PAYING → RouterWorker: **SaveSessions SD + one extra loop** (often hundreds of ms–>1s+)  
RouterWorker → RouterOS auth: **unchanged (~4 paced cmds + login)**  
Authorization → UI: **SaveSessions + queued SSE + up to 1s HTTP poll**  
TOTAL: **dominated by deferred-work ordering + poll**

AFTER  
DONE PAYING → RouterWorker: **enqueue on the HTTP path (ms)**  
RouterWorker → RouterOS auth: **same command budget**  
Authorization → UI: **immediate SSE; UI tick ≤250ms**  
TOTAL: **RouterOS round-trips remain the floor**

Hardware 10× laptop/phone matrix is still required. Do not claim “instant.”
