# Resource, WAN Sync, Storage & Stability — Forensic Audit

**Date:** 2026-08-06  
**Mode:** FORENSIC ONLY — no functional code, RouterOS, or filesystem changes  
**Constraint:** Preserve fully operational production behavior

## Executive verdict

**FORENSIC COMPLETE — SAFE FIXES IDENTIFIED**

Three high-confidence, bounded defects (WAN false-negative after route-print failure; 1970 last-sync display; dashboard RAM metric meaning) plus optional later storage/command-budget optimizations. Do **not** redesign working Hotspot/Admin/coin/worker architecture.

## WAN false-negative (source-proven)

`MikroTikDriver::observeAndRepairWan` already issues:

```
/ip/route/print
?dst-address=0.0.0.0/0
=.proplist=.id,gateway,active,dynamic,comment,distance
```

Hardware: that command ends `read failed` (~5425 ms) → code sets `defaultRoute=unavailable` → **`internet=offline`** even when `dhcp=bound` and gateway known. **Do not add another default route.**

## 1970 last-sync (source-proven)

`RouterCacheManager::isoTimestampNow()` formats `time(nullptr)` as ISO. Without NTP, ESP clock advances from Unix epoch → `1970-01-01T…`. UI prints stamp as-is (`routerCacheLastSyncLabel`).

## Dashboard RAM ~210/240 KB

`/api/status` uses `ESP.getHeapSize()` / `ESP.getFreeHeap()` (~internal Arduino heap). `[mem]` `heap≈8MB` is `MALLOC_CAP_8BIT` (PSRAM-inclusive). Metrics are **not** the same pool.

## SPIFFS pressure (staged tree)

~2.0 MB used; largest: `portal/bg_music.mp3` (~915 KB), Admin JS chunks (~740 KB combined).

Full sectioned report delivered in chat (issues 1–23).
