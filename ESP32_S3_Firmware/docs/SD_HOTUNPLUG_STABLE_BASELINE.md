# SD Hot-Unplug — Stable Physical Baseline (Fallback Reference)

**Date:** 2026-08-17  
**Status:** FROZEN REFERENCE — physical validation passed  
**Do not regress** the SD fail-fast / remount / DMA / SPI / WDT behavior documented here.

This file is the fallback snapshot of a **working** SD hot-unplug path. Later router-sync work must not change:

- SD SPI = FSPI (MOSI=6 MISO=5 SCK=7 CS=18)
- W5500 SPI = SPI3 (MOSI=11 MISO=13 SCK=12 CS=10 RST=14)
- Watchdog timeout
- StorageManager conflict policy (no auto-merge)
- SD fail-fast (`tripSdMediaMissing`) and remount lifecycle

---

## Proven earlier crash (pre-fix)

See `SD_HOTUNPLUG_DMA_WDT_FORENSIC.md`.

- `async_tcp` blocked in `sdSelectCard` / `sdWait(500)` via `readJsonFromSd` → WDT.
- W5500 `emac_w5500_task` then failed a 504-byte DMA RX alloc → LoadProhibited.

## Physical result after fail-fast remount (this baseline)

Observed on ESP32-S3 / W5500 hardware:

| Check | Result |
|-------|--------|
| `SD_READY` → `SD_DEGRADED` | Occurs |
| Ethernet | Remains UP |
| Ping `10.10.10.1` | Continues |
| HTTP | Remains responsive |
| WDT | None |
| Guru Meditation | None |
| `emac_w5500_task` DMA panic | None |
| DMA largest block | ~24564, recovers after transient allocations |
| Reinsert | `SD.begin()`, write verification, `SD_READY` |
| SD recovery | Working |

Expected serial (do not lose these transitions):

```
[storage-lifecycle] state=SD_READY -> SD_DEGRADED reason=...
[storage-lifecycle] state=SD_DEGRADED -> SD_REMOUNTING
[SD] SD remount: SD.begin OK
[storage] Verifying write capability
[storage] Verification passed
[storage] SD recovered successfully
[storage-lifecycle] ... -> SD_READY reason=remount verified
```

No `sdSelectCard(): Select Failed` storm after the first media trip.

---

## Out of scope for this baseline

Router Refresh / Synchronize failures after remount (`RouterOS API username is not configured`, `admin job deferred reason=recovery`) are a **separate** credential + ROS health-gate problem. They are not SD/DMA defects. See `ROUTER_SYNC_CREDENTIAL_RECOVERY_FIX_REPORT.md`.
