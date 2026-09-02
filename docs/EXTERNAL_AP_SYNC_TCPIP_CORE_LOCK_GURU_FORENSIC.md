# External AP Sync Guru Meditation — Forensic

**Date:** 2026-08-23  
**Component:** `ESP32_S3_Firmware/src/ap/GenericApDriver.cpp`  
**Symptom:** Admin Sync → “Unable to sync access point” then **Guru Meditation** / reboot  
**Status:** Root cause **PROVEN**. Fix applied (TCPIP core lock around lwIP netif/ARP APIs).

---

## Serial evidence (crash)

```
[dma] ap-check-before-icmp free=39400 ...
assert failed: netif_get_by_index /IDF/components/lwip/lwip/src/core/netif.c:1742
  (Required to lock TCPIP core functionality!)

Backtrace: ... → GenericApDriver probe → ap_check_worker
Rebooting...
```

Timeline:

1. `[ap-check] queued ... ip=10.10.10.20 sync=yes`
2. `[ap-check] started`
3. `[dma] ap-check-before-icmp`
4. **Assert in `netif_get_by_index`** — never reaches ICMP success/fail log
5. Whole appliance reboots

This is not a MikroTik timeout and not DMA exhaustion. The assert text is definitive.

---

## Proven root cause

### What we added

To reach an AP at `10.10.10.20` that lives on **bridgeGuest** while the ESP32 is on **ether2** (same IPv4 subnet, different L2), `GatewayArpRoute` temporarily installs a static ARP entry: map AP IP → **gateway MAC** (`10.10.10.1`) so probes go through MikroTik.

That helper called lwIP core APIs from `ap_check_worker`:

| Call | Purpose |
|------|---------|
| `netif_get_by_index()` | Resolve Ethernet `struct netif *` |
| `etharp_find_addr()` / `etharp_query()` | Learn gateway MAC |
| `etharp_add_static_entry()` / `etharp_remove_static_entry()` | Install / tear down override |

### Why it crashes

ESP-IDF lwIP is built with **`LWIP_TCPIP_CORE_LOCKING`**. Any non–TCPIP-thread code that touches netif/ARP internals must take:

```c
LOCK_TCPIP_CORE();
// lwIP core API
UNLOCK_TCPIP_CORE();
```

`ap_check_worker` is a normal FreeRTOS task. It called `netif_get_by_index()` **without** that lock. lwIP’s debug assert fires immediately:

> Required to lock TCPIP core functionality!

Hence Guru Meditation on Sync, every time the routed-via-gw path runs.

### Why Sync “failed” then crashed

The crash happens **before** ICMP/TCP results are written. The UI sees Sync fail / connection drop because the ESP32 reboots mid-job. The network path may still be broken afterward; the **reboot** is specifically this lock bug.

---

## Solution (implemented)

1. `#include <lwip/tcpip.h>`
2. Wrap every `netif_*` / `etharp_*` call in `LOCK_TCPIP_CORE()` / `UNLOCK_TCPIP_CORE()`
3. **Never** hold the lock across `vTaskDelay` (ARP wait loop unlocks between polls)
4. Tear down static ARP in the destructor under the same lock

File: `ESP32_S3_Firmware/src/ap/GenericApDriver.cpp` (`GatewayArpRoute`).

---

## What this does / does not fix

| Fixed | Not fixed by this alone |
|-------|-------------------------|
| Guru Meditation on Sync when routed-via-gw runs | MikroTik must still forward ether2 ↔ bridgeGuest for `10.10.10.20` |
| Safe temporary ARP override for same-subnet, cross-bridge AP | Phase 3 laptop-on-ether2 may still fail (PC has no static ARP trick) |

After flash, serial should show either:

```
[ap-check] routed-via-gw target=10.10.10.20 gw=10.10.10.1
[ap-check] icmp result=success|timeout
```

**without** a `netif_get_by_index` assert. If icmp/tcp still fail, that is remaining L3 path — not a crash.

---

## Verification checklist

1. Flash firmware with this fix  
2. ESP32 on ether2, AP at `10.10.10.20` on guest bridge  
3. Admin → Sync once  
4. Confirm: no Guru Meditation; look for `[ap-check] routed-via-gw`  
5. If still unreachable: keep MikroTik `/32` route + forward accepts; do not reintroduce unlocked lwIP calls  

---

## Classification

| Item | Value |
|------|--------|
| Confidence | **PROVEN** (assert text + call site + IDF locking rule) |
| Severity | High (full reboot on Sync) |
| Regression source | Gateway ARP route helper without TCPIP core lock |
| Fix type | Correctness / concurrency — lock lwIP core APIs |
