# Post-Forensic Validation + Implementation

**Verdict:** VALIDATED FIXES IMPLEMENTED — HARDWARE VALIDATION REQUIRED

---

## 1. Forensic claim validation

### Claim A — WAN ping fails because Sync exhausts the 20s job budget

**VERIFIED** (strong causal evidence; not a lab-instrumented `jobExpired` log at the ping line, but mechanism + timing match).

| Evidence | Source |
|----------|--------|
| Job deadline = `millis() + ROUTER_WORKER_JOB_TIMEOUT_MS` (20000) | `RouterProvisioningWorker.cpp` ~679 |
| `readByte`/`readWord` abort when `jobExpired()` | `RouterOsClient.cpp` |
| Abort → `(read failed)` | `executeCommandImpl` |
| Hardware: Sync ~20774 ms > 20000; `/ping` last; 291 ms `(read failed)` | Runtime log |
| `cmds=3` (ping not counted) + `internet=unknown` | `observeAndRepairWan` |

**Not proven exclusively:** other causes (socket close, framing) could also yield `(read failed)`, but 291 ms after a Sync that already exceeded 20s is the expected signature of an already-expired job deadline, not an ICMP wait (which would approach the 8s IO timeout).

**Why not raise timeout first:** Extending `ROUTER_WORKER_JOB_TIMEOUT_MS` hides Sync cost and lengthens hang windows under RouterOS stalls. Prefer skip-when-budget-low.

### Claim B — `provisionStatus` blank because Sync uses `synchronizeRouterCache(false)`

**VERIFIED**

| Evidence | Source |
|----------|--------|
| Admin Sync: `synchronizeRouterCache(false)` | `RouterProvisioningWorker.cpp` ~1190 |
| `markProvisioned` only when `true` | `RouterPlatform::refreshRouterCache` |
| UI: `routerCache.provisionStatus \|\| "—"` | `SystemConfigurationPage.tsx` |
| `ssid` is copied from Sync snapshot | `applyLiveSnapshot` / sync log |
| `productionNetwork` only from Finish verify | `applyProductionNetworkVerification` |

React Query invalidation after Sync **is present** — not the root cause.

### Claim C — bridge port print requests unnecessary attrs

**VERIFIED**

| Evidence | Source |
|----------|--------|
| Unfiltered `/interface/bridge/port/print` | `RouterWirelessAdapter.cpp`, `RouterProvisioningEngine.cpp` |
| Loop only reads `bridge` + `interface` | same |
| `MAX_ATTRS=24` → attr-limit warning | `RouterOsClient` |

---

## 2. Fixes implemented (verified only)

### FIX 1 — WAN (budget-aware ping)
- `RouterApiTransportGate::remainingJobBudgetMs()`
- Skip `/ping` if remaining budget &lt; 3000 ms → `internet=unknown` + note
- UI: route available + internet unknown → **Reachability Unverified** (not Offline)

### FIX 2 — Provision Status — **Option B** (recommended & implemented)

| | Option A (backend) | Option B (frontend) |
|--|--|--|
| Maintainability | Couples Sync to Finish vocabulary | Display-only; Finish stays authoritative |
| Correctness | Risk of writing `provisioned` without Finish | Shows `Synchronized` / `SSID synchronized` without claiming verified |
| Architecture | Mutates cache semantics | Preserves Sync vs Finish contract |
| Future Finish | Must not overwrite wrongly | Finish still sets real `provisionStatus` / `productionNetwork` |

**Safer: Option B.** Implemented in `routerCacheStatus.ts` + System Configuration rows.

### FIX 3 — Bridge port proplist
Both call sites now use `=.proplist=bridge,interface`.

---

## 3. Router Sync command classification (review only — not implemented)

| Command | Class | Notes |
|---------|-------|-------|
| `/system/identity/print` | Static | Rarely changes |
| `/system/resource/print` | Dynamic | CPU/memory/uptime; also feeds pacing |
| Wireless read / print | Semi-static | SSID/security change on admin save |
| Security profile print | Semi-static | |
| Bridge / hotspot reconcile | Semi-static | Needed for captive-path repair on Sync |
| Hotspot / profile print | Semi-static | |
| User profile print | Semi-static | Rate limits change occasionally |
| WAN iface / DHCP / route | Dynamic | Reachability-critical |
| `/ping` | Dynamic | Optional if budget low |

**FULL CONFIG SYNC + FAST STATUS REFRESH:** Would reduce RouterOS load for dashboard “refresh” if Status only needed WAN/resource. **Do not implement yet** — Sync is explicit admin action; idle already generates 0 RouterOS cmds/min. Split adds API/UX surface without a reported idle-CPU customer problem. Revisit only if Sync duration or hAP CPU during Sync becomes a field complaint.

---

## 4. InstallationState / lifecycle

**No Admin Dashboard field blanking from `lifecycle=FactoryProvisioning`.**

Frontend uses installation state only to hide `setupOnly` nav when state is `ready`/`provisioned` (`AdminLayout`). Provision Status blank was Option B issue, not factory gate. **Leave InstallationState unchanged.**

---

## 5. Memory review

SPIFFS audio already removed. DMA minimum-ever is a watermark; free recovers post-Sync. No safe allocator/buffer move without architecture change.

**Current memory usage is acceptable. No further optimization recommended.**

---

## 6. CPU / polling review

- `refreshHealthCache()` reads local storage only — no RouterOS.
- Dashboard polls HTTP `/api/status` from cache — no RouterOS on poll.
- Idle Admin/portal: 0 RouterOS cmds/min by design.
- Bridge proplist removes attr-limit waste (implemented).
- Duplicate wireless in one Sync is partially mitigated (`wirelessCached`); further dedupe not proven safe for Hotspot reconcile.

**Current polling strategy is already appropriate.**

---

## 7. Files changed

- `RouterApiTransportGate.h/.cpp`
- `MikroTikDriver.cpp`
- `RouterWirelessAdapter.cpp`
- `RouterProvisioningEngine.cpp`
- `adminStatus.ts`, `systemConfigurationStatus.ts`, `routerCacheStatus.ts`
- `SystemConfigurationPage.tsx`

## 8. Hardware validation required

1. Sync with known Internet → WAN **Online** or **Reachability Unverified** (never false Offline / No default route when route=available)
2. Log may show `[wan] ping skipped remaining_budget_ms=...`
3. Provision Status → **Synchronized**; Production Wi-Fi → **SSID synchronized** after Sync without Finish
4. No `reply attr limit` on bridge port print during Sync/Finish
5. Coin/voucher/portal/Admin unchanged
