# Activation vs Sales Architecture Forensic

Date: 2026-08-10  
Scope: Architecture intent only — **no code changes, no implementation**  
Prior finding (accepted): `donePaying()` aborts when `salesRecordedAtNow()` is empty  
Question: Should customer Internet activation depend on sales wall-clock success?

---

## Executive verdict

| Question | Architecture conclusion |
|---|---|
| Is sales recording **required** for Internet grant? | **No — it should be downstream** |
| Is sales recording **downstream** of activation? | **Yes (business + most of firmware design)** |
| Does current coin Done Paying match that intent? | **No — it incorrectly hard-couples activation to wall clock** |
| Does this violate appliance resilience? | **Yes — fail-closed sales gate on a fail-operational product path** |
| Confidence | **High (~90%)** |

**Intended model for a commercial Piso WiFi appliance:**  
Coins → entitlement → Router authorize → Internet.  
Sales/history/reports attach when durable timestamps (or deferred markers) are available — they must not block paid Internet.

---

## 1. Current architecture diagram (as implemented)

```
                    ┌─────────────────────┐
                    │   Coin ISR / Promo  │
                    │  credits + minutes  │  ← millis-based; no NTP required
                    └──────────┬──────────┘
                               ▼
                    ┌─────────────────────┐
                    │  Done Paying API    │
                    └──────────┬──────────┘
                               ▼
                    ┌─────────────────────┐
                    │ salesRecordedAtNow()│  ← WALL CLOCK GATE (hard fail)
                    └──────────┬──────────┘
                         empty │
                               ▼ FAIL
                    Internet NEVER granted
                    RouterWorker NEVER queued

                         OK │
                               ▼
              ┌────────────────┴────────────────┐
              │ session → activating            │
              │ enqueueRecordSale (sales)       │
              │ enqueueActivateSession          │
              └────────────────┬────────────────┘
                               ▼
                    ┌─────────────────────┐
                    │ RouterWorker        │
                    │ MikroTik authorize  │
                    └──────────┬──────────┘
                               ▼
                         Internet Access
```

**Coupling proven:** activation enqueue is after the clock gate in `donePaying()` (see prior `DONE_PAYING_ROOT_CAUSE_FORENSIC.md`).

---

## 2. Business dependency graph (intended vs coded)

### Business / commercial order (Piso WiFi)

```
Coins
  → Credits
  → Purchased Minutes
  → Done Paying
  → Router Authorization
  → Internet Access          ★ customer-facing product
  → Sales Recording          ★ owner bookkeeping
  → History / Reports / Analytics
```

Sales is **DOWNSTREAM** of Internet. Customer already paid with coins; withholding Internet because a report timestamp is missing is commercially incorrect.

### What the coin Done Paying code does today

```
Done Paying
  → Sales timestamp MUST succeed
  → else abort (no activating, no RouterWorker)
  → only then sales queue + activation queue
```

Here sales timestamp is treated as **REQUIRED** for activation — **architecture mismatch**.

---

## 3. Investigation B — Repository callers

### `salesRecordedAtNow()`

| Caller | Role | Blocks customer Internet if empty? |
|---|---|---|
| `PortalSessionManager::donePaying` | Coin activation | **Yes** (`return false`) |
| `PortalSessionManager::redeemVoucher` | Voucher activation | **Yes** (`CLOCK_NOT_READY`) |
| `PortalSessionManager` (complete/expire sale helpers) | Sale completion metadata | No activation gate at Done Paying |
| `PortalSessionManager` (hotspot outcome sale updates) | Status timestamps | Post-activation |
| `SessionManager` (legacy coin path) | Sets `recordedAt` on sale; still records | Does not gate portal Done Paying |
| `VoucherManager` | Event timestamp when caller omits | Manager-level, not portal gate alone |
| `Logger::appendHistory` | Log history eventAt may be empty | Logging only — does not block Internet |
| `CoinManager::formatTimestamp` | **Prefer wall clock; else `uptime-ms:`** | **Fail-operational** for coin diagnostics |

### `enqueueRecordSale()`

| Caller | Notes |
|---|---|
| `PortalSessionManager::donePaying` only | Sole producer of deferred coin `RecordSale` work |

No separate `SalesLedger` / `SalesHistory` / `TransactionHistory` / `SalesStorage` types beyond `SessionManager::upsertSale` + NDJSON history + portal deferred `RecordSale`.

### Conclusion

**Only activation entry points that hard-require wall clock before granting Internet are coin `donePaying` and voucher `redeemVoucher`.**  
Coin hardware path and Logger already tolerate missing wall clock. That inconsistency is the architectural smell.

---

## 4. Investigation C — Activation contract inventory

| Activation path | Requires `salesRecordedAtNow()` before activate? | Notes |
|---|---|---|
| Coin `donePaying` | **Yes** | Hard fail |
| Voucher `redeemVoucher` | **Yes** | `CLOCK_NOT_READY` (503) |
| Voucher `reconnectVoucher` | Partial | Needs wall clock to interpret `serviceExpiresAt`; reconnect entitlement math |
| Resume (`resume` → `enqueueActivateSession`) | **No** | Re-queues activate from existing `secondsLeft` |
| Tick / retry activate | **No** | Uses pending session state |
| RouterWorker outcome → Active | N/A | Consumer of prior enqueue |
| Admin “grant” (if any) | Not a separate portal clock gate in Done Paying | SessionManager legacy coin still stamps sales optionally |

**Not all activation paths require wall clock — resume/retry do not.** Coin/voucher **first grant** currently do.

---

## 5. Investigation D — Voucher vs coin (side-by-side)

| Step | Coin `donePaying` | Voucher `redeemVoucher` |
|---|---|---|
| Prefetch wall clock | Yes | Yes |
| If clock empty | `return false` → API **400 NO_CREDITS** (misleading) | `CLOCK_NOT_READY` → API **503** |
| Mutate session to activating | Only after clock OK | Only after clock OK (+ reserve/sale) |
| `enqueueActivateSession` | After clock + sale enqueue | After clock + reserve + session write |
| RouterWorker | Never if clock empty | Never if clock empty |
| Sale persistence | Deferred `RecordSale` after clock | Sync `upsertSale` (new vouchers) after clock |
| Absolute expiry math | Optional `startedAt` wall string | `serviceExpiresAt` / `secondsUntilRecordedAt` **needs** wall clock for time-bound vouchers |

**Same coupling pattern:** both abort Internet when clock empty.  
**Difference:** voucher error signaling is honest (`CLOCK_NOT_READY`); coin is not.  
**Difference:** vouchers with absolute calendar expiry have a **stronger** technical need for wall clock than coin sessions, whose entitlement is **`secondsLeft` + millis tick**.

---

## 6. Coin / voucher / sales / activation sequences

### Coin (intended commercial)

```
Insert coins → credits/minutes (millis)
→ Done Paying
→ set secondsLeft, Activating
→ RouterWorker authorize
→ Connected / Active (millis countdown)
→ Record sale (wall or deferred uptime marker)
→ History / reports
```

### Coin (current)

```
Done Paying → wall clock OR abort → (no Internet)
```

### Voucher (current)

```
Redeem → wall clock OR abort
→ reserve → sale → Activating → RouterWorker
```

### Sales (deferred RecordSale worker)

Already stores **both**:

- `sale.timestamp = "uptime-ms:" + millis()`
- `sale.recordedAt = item.saleRecordedAt` (wall when available)

So the sales pipeline **already anticipates** uptime-based markers. The Done Paying gate refuses to reach that pipeline.

---

## 7. Wall-clock dependency graph

```
NTP / installation ready
  → salesTimeBegin / salesRecordedAtNow
       ├─ blocks: donePaying activation
       ├─ blocks: redeemVoucher activation
       ├─ stamps: sale.recordedAt, startedAt, voucher expiry math
       ├─ optional: CoinManager UI timestamps (else uptime-ms)
       └─ optional: Logger history eventAt (may be empty)

millis()
  → coin debounce/pulses
  → secondsLeft countdown (tickSessions)
  → updatedAt / lastSeen / heartbeat freshness
  → sale.timestamp uptime-ms
  → RouterWorker timeoutSeconds from secondsLeft
```

**Customer remaining time does not require NTP.** It is **`secondsLeft` decremented on a millis-based session tick.**

---

## 8. Subsystem dependency map

| Subsystem | Needs wall clock for core function? | Philosophy today |
|---|---|---|
| Coin reader | No | Fail-operational |
| Promo / rates | No | Local |
| Portal session timer | No (`millis`) | Fail-operational |
| RouterWorker / MikroTik auth | No (uses remaining seconds) | Fail-operational when queued |
| SD / SPIFFS | No for activate | Fail-operational fallback |
| Sales reports / day filters | **Yes** for calendar analytics | Fail-closed for **quality**, should not block Internet |
| Voucher absolute expiry | **Yes** for calendar expiry | Fail-closed may be justified for that product rule |
| Logger history | Prefer wall; tolerate empty | Soft |

---

## 9. Failure matrix (architecture intent)

| Scenario | Customer Internet should be granted? | Sale recorded now? | Sale deferred / marker? | Activation should fail? |
|---|---|---|---|---|
| NTP unavailable | **Yes** (coin) | Prefer no wall stamp | **Yes** (`uptime-ms` / later backfill) | **No** (coin) |
| Internet unavailable (no NTP) | **Yes** if router LAN auth works | Marker | Yes | No (coin) |
| RTC/clock 1970 | **Yes** (coin) | Reject wall stamp | Yes | No (coin) |
| DNS failure (NTP) | **Yes** | Marker | Yes | No |
| Router disconnected | **No** (cannot authorize) | Optional pending | Yes | Fail at RouterWorker, not at clock |
| Router connected, clock bad | **Yes** | Marker | Yes | No |
| SD unavailable | **Yes** if SPIFFS session path works | Fallback/spool | Yes | Only if session persist policy fails |
| SPIFFS fallback | **Yes** | Spool/sync later | Yes | No |
| Power failure mid-session | Recover from persisted `secondsLeft` | Incomplete sale handling | Existing replay | Separate |
| Cold/warm boot, NTP not yet synced | **Yes** once coins paid | Marker until NTP | Yes | **Current code wrongly says Yes fail** |

Voucher with **absolute** `serviceExpiresAt`: clock failure may justify refusing redeem/reconnect **for that voucher type**; that is a voucher product rule, not a reason for coin Done Paying to share the same hard gate.

---

## 10. System philosophy (Investigation H)

| Subsystem | Observed posture |
|---|---|
| Coin | Fail-operational (accept pulses, uptime timestamps) |
| Storage | Fail-operational (SPIFFS fallback) |
| Router | Fail-operational when possible; auth fails closed only when router path fails |
| Session timer | Fail-operational on millis |
| Sales calendar quality | Fail-closed on bad timestamps for **reporting accuracy** |
| Coin Done Paying | **Fail-closed on sales clock** ← inconsistent |
| Voucher redeem | Fail-closed on sales clock (partially justifiable) |

**Sales is inconsistent with the appliance’s operational posture** when it blocks coin Internet.

---

## 11. Investigation I — Future / resilience coupling

Hard coupling activation → wall clock **obstructs**:

| Capability | Effect of coupling |
|---|---|
| Offline / pre-NTP operation | Paid customers stuck Waiting for Payment |
| Pause / Resume | Resume OK once Active; never reach Active if Done Paying blocked |
| Power / session recovery | Cannot start session without clock |
| History replay / SD fallback | Orthogonal; but unpaid-looking UI while credits sit |
| Cloud sync later | Would prefer durable sale IDs + uptime, backfill wall time — coupling prevents creating the sale row |
| Voucher reconnect | Already clock-sensitive for absolute expiry (separate issue) |

Unnecessary for **coin** product path: entitlement is already fully known (MAC, credits, minutes, profile, `secondsLeft`).

---

## 12. Minimum sale data (Investigation F) — determine only

To reconstruct a sale later, minimum durable facts already available at Done Paying **without** NTP:

| Field | Available without wall clock? |
|---|---|
| MAC | Yes |
| Credits / amount | Yes |
| Minutes | Yes |
| Session ID | Yes |
| Promo / hotspot profile | Yes (resolveForAmount) |
| Start tick | Yes (`millis` / `uptime-ms`) |
| UUID / sale id | Yes (generated) |
| Wall `recordedAt` | **Optional**; attach when `salesTimeReady()` |

Firmware already writes `timestamp = uptime-ms:…` in deferred RecordSale and voucher sale objects. That evidences an existing design for **non-wall** durability.

---

## 13. Should sales block activation?

| | |
|---|---|
| **Business answer** | **No** |
| **Timer/tech answer** | **No** for coin (`secondsLeft` + millis) |
| **Reporting answer** | Wall clock desirable for day/week/month reports, not for authorize |
| **Current coin code** | Wrongly **Yes** |
| **Architecture recommendation (intent only)** | Activation and sales recording should be **independent**: activate first; record sale best-effort / deferred |

No implementation in this document.

---

## 14. Does architecture violate appliance resilience?

**Yes, for coin.** A working captive portal + coins + MikroTik stack still denies Internet when NTP is late — contrary to fail-operational storage/coin/router patterns and contrary to CoinManager’s own `uptime-ms` fallback.

---

## 15. If decoupled — subsystems affected (Investigation J)

Analysis only — not a change plan:

| Subsystem | Impact of decoupling (conceptual) |
|---|---|
| `PortalSessionManager::donePaying` | Clock gate would no longer abort activate |
| Sales / `enqueueRecordSale` | May record with empty/`uptime-ms` recordedAt; filters that need calendar dates must tolerate markers |
| Reports / dashboard “today” | May under-count until backfill — reporting quality issue, not Internet |
| Voucher | Separate product rules for absolute expiry |
| RouterWorker / RouterOS | Unchanged protocol; more successful enqueues when clock down |
| Portal / Admin UI | Would see activating/active instead of false NO_CREDITS |
| Session restore / pause/resume | Unblocked for new sessions |
| Regression risk if later implemented carelessly | Double sales, missing recordedAt in analytics, voucher expiry edge cases — **must be scoped**; coin-only decoupling is lower risk than changing voucher absolute-expiry rules |

**Regression risk of the *idea* of decoupling (coin):** Moderate for reports; **Low** for RouterWorker/MikroTik CPU if activate path unchanged aside from removing the gate; **High** if voucher absolute expiry is naively weakened.

---

## 16. Answers summary

1. Diagram — §1  
2. Dependency graph — §2  
3–6. Sequences — §5–6  
7. Wall-clock graph — §7  
8. Subsystem map — §8  
9. Failure matrix — §9  
10. Business logic — sales downstream  
11. **Sales should not block coin activation**  
12. **Current coin coupling violates resilience**  
13. Affected if decoupled — §15  
14. Regression — reports/voucher careful; router path low  
15. **Confidence ~90%**

---

## 17. Investigation status

**COMPLETE.**  

Intended architecture for this appliance: **Internet activation independent of sales wall-clock success; sales downstream / best-effort.**  

Current coin Done Paying **does not** match that intent.  

**NO IMPLEMENTATION** performed.
