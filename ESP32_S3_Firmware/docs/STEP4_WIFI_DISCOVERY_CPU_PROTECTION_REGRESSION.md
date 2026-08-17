# Step 4 Wi-Fi Discovery — CPU Protection Regression

**MODE:** FORENSIC → MINIMAL FIX → BUILD → DOCUMENTATION  
**Date:** 2026-08-03  
**Firmware env:** `freenove_esp32_s3_wroom`

**RELEASE VERDICT: HARDWARE VALIDATION REQUIRED**

---

## 1. Root cause

Setup Wizard Step 4 wireless discovery (`OpType::ListWifiNetworks`) was assigned
`RouterApiTransportGate::RouterJobPriority::Low`.

When the last opportunistic `/system/resource/print` sample reported
`cpu-load >= ROUTER_CPU_PAUSE_THRESHOLD_PERCENT` (85%),
`RouterApiTransportGate::waitBeforeCommand()` **paused** Low-priority commands
(100 ms `vTaskDelay` loop) until the worker job deadline
(`ROUTER_WORKER_JOB_TIMEOUT_MS` = 20 s) expired.

`/interface/wireless/print` was therefore never written. Serial showed:

```
cpu-load=87%
CPU protection … pausing Low-priority command
/interface/wireless/print
elapsed=19100 ms (job expired before write)
merge interfaces count=0
WIFI_SCAN_FAILED
```

MikroTik was reachable and logged in. The timeout was **self-inflicted** by
pausing an essential Setup read until the deadline.

`ExistingNetworkScan` had the same Low classification (Step 3) and the same
failure mode under high CPU; it is also a Setup-essential read.

---

## 2. Serial evidence (operator-reported)

| Event | Meaning |
|-------|---------|
| `/system/resource/print` → `cpu-load=87%` | Opportunistic sample stored |
| `pausing Low-priority command` | `waitBeforeCommand` Low + pause threshold |
| `/interface/wireless/print` + `job expired before write` | Command never sent; 20 s deadline hit |
| `merge interfaces count=0` / `WIFI_SCAN_FAILED` | Empty SSID list on Step 4 |

---

## 3. Why MikroTik was not at fault

Login succeeded. RouterOS responded to resource print. Wireless print was never
issued because the firmware withheld the write while waiting for CPU recovery
that did not arrive before the job deadline.

---

## 4. Why CPU protection caused the timeout

```
ListWifiNetworks → priority=Low
       ↓
waitBeforeCommand()
       ↓
cpu >= 85% → pause (no send)
       ↓
deadline (20 s)
       ↓
jobExpired() → ROUTEROS_API_READ_TIMEOUT
       ↓
WIFI_SCAN_FAILED / empty SSIDs
```

CPU protection itself remains valid for **optional/background** Low work.
Misclassification of Setup-essential discovery as Low was the defect.

---

## 5. Classification

| Class | Behavior under high CPU | Examples |
|-------|-------------------------|----------|
| **ESSENTIAL (Normal)** | Tier pacing delays only; **never** pause-until-deadline | Login, ListWifiNetworks, ExistingNetworkScan, Test/Save/Finish/Admin |
| **CRITICAL** | Same pacing; highest intent | Hotspot activate/deauth |
| **LOW PRIORITY** | May pause above 85% | Optional/background inventory (none currently assigned) |

Per-row security-profile enrichment in `listNetworks` step 7 remains skippable
under `cpuUnderPressure()` — that is detail, not SSID list population.

---

## 6. Fix (minimal)

In `RouterProvisioningWorker::runOp`:

- **Removed** Low assignment for `ListWifiNetworks` and `ExistingNetworkScan`.
- They now run at **Normal** (default), like other Setup-required work.
- CPU protection **unchanged**: tier delays still apply; Low pause logic kept
  for future optional inventory.
- No new sessions, retries, polling, cooldown changes, or wizard flow changes.

---

## 7. Before / after

| | Before | After |
|--|--------|-------|
| ListWifiNetworks priority | Low | **Normal** |
| ExistingNetworkScan priority | Low | **Normal** |
| CPU 87% + Step 4 | Pause → deadline → empty SSIDs | Paced send → wireless print proceeds |
| Admin idle RouterOS | 0 | 0 (unchanged) |
| Session count | 1 per discovery job | 1 (unchanged) |

---

## 8. RouterOS request count

Unchanged command set per discovery job (still one session).  
Only timing policy changes: no multi-second pause that burns the deadline
before the essential print.

---

## 9. CPU / memory / DMA impact

- **CPU:** Under high load, Normal jobs use tier5 command spacing (500 ms), not
  indefinite pause. Does not add commands or sessions.
- **Memory/DMA:** No change to reply bounds / proplist / MAX_ATTRS.
- **W5500:** No extra SPI traffic from this fix.

---

## 10. Regression verification (static)

| Area | Status |
|------|--------|
| Admin idle 0 RouterOS | Unchanged |
| Test / Save / Wireless Save / Sync | Still Normal |
| Finish / Portal Verify | Unchanged |
| Captive / Coin / Voucher / Pause | Unchanged |
| Setup Unlock / Operator | Unchanged |
| Cooldown / worker / AsyncTCP | Unchanged |
| CPU pause for Low | Preserved (unused by Setup discovery now) |
| Security-detail skip under pressure | Preserved |

---

## 11. Build result

```
platformio run -e freenove_esp32_s3_wroom → SUCCESS (~54s)
RAM:   31.8% (104140 / 327680)
Flash: 84.5% (2215327 / 2621440)
```

---

## 12. Hardware validation required

1. Flash `freenove_esp32_s3_wroom`.  
2. Scenario A: CPU &lt; 70% → Step 4 SSIDs populate.  
3. Scenario B: Force/observe CPU ~87% → Step 4 SSIDs still populate; Serial must
   **not** show `pausing Low-priority command` for `list-wifi-networks`.  
4. Scenario C: Background Low work (if any) may still pause; discovery succeeds.  
5. Scenario D: Router unreachable → controlled error, no retry storm.
