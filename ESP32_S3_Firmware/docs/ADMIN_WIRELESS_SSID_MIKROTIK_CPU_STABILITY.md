# Admin Wireless SSID — MikroTik CPU / Connection Stability

## Hardware evidence (pre-fix)

SSID Admin save (`admin-save-wireless`) on MikroTik hAP lite / RouterOS 7.20.7:

- SET succeeded — client could join the new SSID
- Post-set `/interface/wireless/print` ran **8109 ms** then failed (`ROUTEROS_IO_TIMEOUT_MS=8000`)
- Immediate `/interface/wifi/print` → `not connected` (session already dead)
- Worker still returned `ok=yes` / HTTP 200
- Job elapsed ≈ **15376 ms**
- MikroTik profiler: **total 100%** (management / console / spi / routing dominant)
- After job: ESP32 observed `ETH_DISCONNECTED` / `ETH_CONNECTED` / ping timeouts / eventually `ETH_GOT_IP`
- ESP32: no Guru Meditation, no TWDT, no intentional Ethernet restart from wireless-save code

## Command sequence BEFORE (root cause)

For SSID-only save the path was:

1. `findWirelessInterface` → **unfiltered** `/interface/wireless/print` `.proplist=.id,name` (all ifaces) ≈631 ms  
2. Targeted `/interface/wireless/print` `?name=` `.proplist=.id,name` ≈130 ms  
3. `/interface/wireless/set` `=.id=` `=ssid=`  
4. `readInterface` → **`findWirelessInterface` again** → unfiltered wireless print while radio reconfiguring → **~8.1 s IO timeout** → session disconnect  
5. Same `findWirelessInterface` then **`/interface/wifi/print`** on dead session → `not connected` (0 ms)  
6. `saveWireless` initialized `verifiedSsid = nextSsid`, so failed verify still compared equal → **false verified success**

No causal `ETH.end` / `ETH.begin` from wireless save — Ethernet flaps were external link events after MikroTik saturation.

## Command sequence AFTER (first pass — still had post-SET verify)

Intermediate hardening still did: settle 1500 ms → ≤1 targeted post-SET print → often ~8011 ms timeout → `verification=deferred`. See **Final production path** below for the production sequence.

## Why 8.1 s verification failed

Post-set wireless print collided with radio reconfiguration; client waited until `ROUTEROS_IO_TIMEOUT_MS` (8000 ms). Hardware elapsed ≈8011–8109 ms matches that timeout. The timeout itself is not the defect — issuing the read immediately after SET is.

## Why `/interface/wifi` ran (pre-fix)

`findWirelessInterface` always tried legacy wireless inventory, then wifiwave2. After a timed-out wireless print, the session was dead — wifi print returned `not connected`. SSID-only save no longer uses that inventory path after SET.

## Semantics

| State | Meaning |
|-------|---------|
| `applied=true`, `verified=false`, `verification=deferred` | SET ACK received — normal SSID Save success |
| `applied=true`, `verified=true` | Only if a later independent observation confirms SSID (Sync / Refresh / Test) |
| `applied=false` | SET failed — no cache patch |

Canonical SSID + cache updated when **applied**. UI does not claim Verified on Save.

## Ethernet

ESP32 wireless-save path does **not** call Ethernet restart. Documented physical link events were observed after MikroTik CPU saturation. Fix reduces RouterOS load; do not add Ethernet recovery retries for this path.

## Files changed

- `Config.h` — removed `ROUTER_WIRELESS_SETTLE_MS` (only used for immediate verify)
- `RouterWirelessAdapter.cpp` — `applySsidOnly`: SET ACK → stop (no post-SET print)
- `MikroTikDriver.cpp` — truthful applied / deferred verify; cache patch after ACK
- `RouterPlatform.cpp` — cache comment (SET ACK, not same-session verify)
- `RouterProvisioningWorker.cpp` — applied toast message
- `SystemConfigurationPage.tsx` — deferred toast; cache-only refetch after Save
- This document

## No-change areas

router_worker scheduling, transport gate, connect cooldown, CPU pacing tiers, MAX_ATTRS, Test/Sync/profile/promo paths, W5500/SD/AsyncTCP, Setup Wizard.

## Final production path (immediate verify removed)

### Previous residual defect (still present after first stability pass)

```
targeted print (~32 ms)
/interface/wireless/set (~1125 ms)
settle 1500 ms
post-SET /interface/wireless/print → ~8011 ms FAIL (ROUTEROS_IO_TIMEOUT)
verification=deferred, job ok=yes (~12350 ms)
```

Ethernet stayed UP; gateway ping healthy; ESP32 stable. Defect was **only** the unnecessary immediate post-SET read during radio reconfiguration.

### Final path

```
SSID Save
  → optional targeted /interface/wireless/print (?name=, .proplist=.id,name,ssid)
  → /interface/wireless/set =.id= =ssid=
  → SET ACK
  → patch canonical + RouterCacheManager ssid (merge, no to<> wipe)
  → close session
  → applied=true, verified=false, verification=deferred
```

**No** post-SET wireless print. **No** settle delay on the save critical path.
`ROUTER_WIRELESS_SETTLE_MS` removed (only existed for immediate verify).

### Why immediate readback is avoided

Changing SSID causes the wireless radio subsystem to reconfigure. Issuing `/interface/wireless/print` immediately after SET collides with that window and burns a full I/O timeout (~8 s) on low-resource hAP lite hardware — even when the SET already succeeded and Ethernet remains healthy.

Verification is deferred to explicit later observation:
- Synchronize Router
- Refresh Router Information
- Test Connection

### Frontend after Save

`refetchWireless()` + `refetchCache()` are ESP-local GETs (`/api/router/wireless`, `/api/router/cache`) — **zero** RouterOS sessions. No automatic Test/Sync.

### DNS observation

No ESP32 SSID-save code path issues RouterOS DNS commands. Temporary MikroTik DNS CPU spikes during radio reconfiguration remain an observation only — not a firmware defect to “fix.”
