# Hardware Validation Forensic — WAN / Provision / DMA / Sync Latency

**Mode:** FORENSIC ONLY (no code changes)  
**Verdict:** FORENSIC COMPLETE — SAFE PATCH IDENTIFIED

Hardware evidence matched to source:

- `[wan] link=up dhcp=bound route=available internet=unknown gateway=192.168.50.1 cmds=3`
- `/ping` → `END elapsed=291 ms (read failed)`
- Sync total ~20774 ms; job budget `ROUTER_WORKER_JOB_TIMEOUT_MS=20000`
- Sync log: `ssid=Test2 Piso Wifi` / Hotspot ok
- Heartbeat: `lifecycle=FactoryProvisioning setup=ready production=ready install=factory`
- DMA free recovers ~38 KB; minimum-ever ~1680 B during Sync

---

## Issue 1 — WAN “Unable to Verify”

### Exact command sent
```
/ping
=address=8.8.8.8
=count=1
```
(`MikroTikDriver::observeAndRepairWan`)

### Expected reply shape (RouterOS API)
Unlike `/print`, `/ping` streams one or more `!re` sentences (per ICMP) carrying attrs such as `host`, `time`, `ttl`, `sent`, `received`, `packet-loss`, then terminal `!done`.

Firmware `applySentenceToResult` stores attrs only from `!re`; `!done` sets `doneReceived=true` and **discards** `!done` attributes. For ping, cumulative `received` is typically on `!re` (not only on `!done`), so when the command **completes**, `attrFromResult(..., "received")` can work. Fallback: `replyCount > 0` → online.

### Exact parser / transport failure (this hardware run)
`executeCommandImpl` → `drainCommandResult` → `readSentence`/`readWord` returned **false** → log `(read failed)`.

That is **not** “ping returned offline”. It is a **read/transport abort** before a successful drain.

Strongest evidence for **why** at 291 ms:

| Fact | Implication |
|------|-------------|
| Sync wall time **20774 ms** | Exceeds `ROUTER_WORKER_JOB_TIMEOUT_MS` **20000** |
| `/ping` is last WAN step after long Sync | Job deadline already expired or expires mid-drain |
| `jobExpired()` checked in `readByte`/`readWord` | Immediate fail → short elapsed (~291 ms ≈ pacing + abort) |
| `cmds=3` | iface+dhcp+route counted; ping failure path does **not** increment cmds |
| Prior patch | Ping fail → `internet=unknown` (correct; UI “Unable to verify”) |

Secondary latent risk (not this run’s abort): if ping ever completed but only summary lived on `!done`, `received` would be missed — mitigated today by `replyCount > 0` → online.

### Safest WAN verification fix (do not change topology)
1. **Primary:** Skip `/ping` (or treat as non-fatal inference) when remaining job budget is insufficient; **or** modestly raise Sync job timeout **or** run WAN observe earlier / with Critical priority headroom.
2. **Display/semantics:** If `dhcp=bound` + `defaultRoute=available` and ping API fails → keep observation honest but UI can show **Online (reachability unverified)** / still not “No default route”.
3. Do **not** add routes/DHCP/NAT. Do **not** require ping success to claim route health (already correct).

---

## Issue 2 — Provision Status / Production Wi-Fi blank

### Frontend source
`SystemConfigurationPage.tsx` reads **`GET /api/router/cache`** via React Query `["router","cache"]`:

- Provision Status → `routerCache.provisionStatus`
- Production Wi-Fi → `routerCache.productionNetwork` (healthy/reason) else **—**
- Production SSID → `productionNetwork.ssid || routerCache.ssid`

Invalidation after Sync: `refreshProductionRouterViews` **does** invalidate/refetch `["router","cache"]`. **Not** an invalidation bug.

### Backend containing “Test2 Piso Wifi”
`MikroTikDriver::collectCacheSnapshot` writes top-level **`ssid`**.  
`RouterCacheManager::applyLiveSnapshot` copies `ssid`.  
`fillPublic` exposes `ssid`. Sync worker logs that identity/ssid.

### Why Provision Status stays blank
`AdminSyncCache` calls:

```cpp
synchronizeRouterCache(false);  // markProvisioned = false
```

So Sync **never** sets `provisionStatus`. That field is set by Finish/`markProvisioned`, not by Admin Sync.

### Why Production Wi-Fi stays blank
`productionNetwork` is written only by `applyProductionNetworkVerification` (Finish/verify path), **not** by `collectCacheSnapshot`. Sync populates `ssid` but not `productionNetwork` → UI shows **—** for Production Wi-Fi even when SSID row can show `Test2 Piso Wifi`.

---

## Issue 3 — Installation state

Heartbeat fields are **different planes**:

| Heartbeat token | Meaning |
|-----------------|--------|
| `setup=ready` | Setup HTTP plane started (`_setupServerStarted`) |
| `production=ready` | Production routes registered (`_productionRegistered`) |
| `install=factory` | Persisted `InstallationState::Factory` in `installation.json` |
| `lifecycle=FactoryProvisioning` | `needsSetup() && Management AP running` && state==Factory |

**Sync Router does not alter InstallationState** (by design).

`install=factory` remains because Finish never committed Ready/Provisioned (or `installation.json` still says factory). Inference from storage only runs when the file is **missing**.

**Does factory blank the Provision UI?** No. Blank Provision Status/Production Wi-Fi come from empty `provisionStatus` / missing `productionNetwork`, not from install=factory gating on that page.

---

## Issue 4 — DMA

- **Current free DMA ~38 KB** after Sync → recovered.
- **minimum=1680** = **historical watermark** (`heap_caps_get_minimum_free_size`), not current free.
- Transient pressure during Sync + job poll + SSE + JSON + RouterOS socket is expected.
- **Currently safe** if free/largest stay ~30–40 KB; watermark is a stress signal, not proof of ongoing exhaustion. No allocator/PSRAM move indicated.

---

## Issue 5 — Latency / attr limit

### Route print ~6455 ms
Elapsed includes **CPU pacing** (`waitBeforeCommand`) + write + drain. Under high RouterOS CPU after a long Sync burst, paced delay + slow hAP lite API response explain multi-second filtered `/ip/route/print`. Not evidence of missing filter (filter already present). Job budget exhaustion at Sync end is the bigger product issue.

### `reply attr limit reached (max=24)`
`/interface/bridge/port/print` in `bridgeHasInterface` is called **without `.proplist`**. Full port rows exceed 24 attrs → truncate. Needed fields: **`bridge`, `interface` only**. Safe fix: add `=.proplist=bridge,interface` (or `.id,bridge,interface`). **Do not** raise `MAX_ATTRS` first.

Duplicate wireless prints in Sync: expected (canonical `readInterface` + possible list path / hotspot reconcile); already partially cached.

---

## Minimal patch plan (for a later implementation turn)

1. **WAN:** Skip ping near job deadline **or** infer/display Online when route+dhcp OK and ping transport fails; optional Sync timeout headroom.
2. **Provision UI:** On successful Admin Sync, set `provisionStatus` from live evidence (e.g. `synchronized`) **and/or** map Production Wi-Fi UI to cache `ssid` + observation hotspot when `productionNetwork` absent.
3. **Install state:** Separate forensic/product decision — do **not** force Ready from Sync; optional migration/admin repair tool after evidence.
4. **Bridge port print:** Add bounded proplist.
5. **DMA:** Observe only; no allocator change.

## Regression risks
- Inferring Online without ICMP: false Online if upstream dead but default route present.
- Marking provisioned from Sync: may hide unfinished Finish portal deploy.
- Raising job timeout: longer hung Sync under RouterOS stall.
- Forcing InstallationState Ready: reopens setup/production plane assumptions.

## Hardware validation plan (post-patch)
- Sync with known Internet → WAN Online or explicit “unverified” only when intended.
- Force ping skip/fail → never “No default route” when route=available.
- Provision Status / Production SSID populate after Sync without Finish.
- Bridge port print: no attr-limit spam; Hotspot reconcile still OK.
- DMA free recovers post-Sync; no TWDT.
