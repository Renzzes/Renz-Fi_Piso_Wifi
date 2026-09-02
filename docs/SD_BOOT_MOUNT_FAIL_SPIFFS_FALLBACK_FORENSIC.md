# Forensic: SD Boot Mount Failed → SPIFFS Fallback

**Date:** 2026-08-22  
**Firmware:** `0.5.0-w5500`  
**Hardware profile (compile-time):** `ESP32-S3-ETH-WAVESHARE` (`env:waveshare_esp32_s3_eth`)  
**Status:** Root cause proven from serial log + source correlation

---

## What the user saw

```
Storage: SPIFFS Fallback
[ERROR] SD card mount failed
[storage] SD unavailable, using SPIFFS fallback
Health=DEGRADED Cause=MEDIA_MISSING
```

The card was believed to be physically inserted.

---

## What actually happened (proven)

SPIFFS fallback is **not a bug**. It is the designed recovery path when `SD.begin()` fails at boot.

| Step | Log evidence | Meaning |
|------|----------------|---------|
| 1 | `[boot] Phase 3: SD card initialization` | Normal boot order (SPIFFS first, then SD on FSPI) |
| 2 | `renzFiSdSpiBegin host=FSPI … sdCs=4=1` | Waveshare SD map: CS=**GPIO4**, CS driven HIGH before init |
| 3 | `SD mount: mounting SD (cs=4 freq=1000000 Hz csLevel=1)` | SPI bus init succeeded; mount attempted on GPIO4 |
| 4 | `sdcard_mount(): f_mount failed: (3) The physical drive cannot work` | FatFS **FR_NOT_READY** — SD card did not come up on SPI |
| 5 | `SD.begin FAILED` → `SD_FAILED` → `SD_DEGRADED` | Mount failure handled correctly |
| 6 | `SD Missing` / `Cause=MEDIA_MISSING` | After failed begin, `SD.cardType() == CARD_NONE` (no card on bus) |
| 7 | `[storage] Entering fallback mode` | SPIFFS already mounted in Phase 2; operational fallback enabled |

**Proven conclusion:** The ESP32 never saw an SD card on **FSPI + GPIO4 CS**. The failure is at **SPI card detection**, not JSON layout, not false hot-unplug, and not W5500 SPI interference (Ethernet stayed `link=UP` through SD init; W5500 CS=14 untouched).

---

## What this is NOT

| Rejected for this boot | Why |
|------------------------|-----|
| SPIFFS/SD “conflict” | SPIFFS and SD are separate; fallback is intentional after SD failure |
| False `SD_READY → SD_DEGRADED` after successful mount | Documented in `SD_READY_FALSE_REMOVAL_FORENSIC.md` — **different failure** (that boot had `SD.begin OK`) |
| W5500 CS `pinMode()` regression | Fixed in `SdSpi.cpp`; ETH remained up after SD fail |
| Software “choosing” SPIFFS while SD works | SD never mounted; no SD I/O occurred |

---

## Root cause (proven + most likely field explanation)

### Proven (software / log)

**No SD response on the configured CS pin (Waveshare: GPIO4) at boot.**

`StorageManager::begin()` after failed mount:

```cpp
_sdPresent = SD.cardType() != CARD_NONE;  // false → MEDIA_MISSING
```

So the Arduino SD layer reports **CARD_NONE** — the card did not answer CMD0/init on that bus.

### Most likely physical cause (matches “worked before” + Waveshare migration)

**Board profile / SD slot mismatch — GPIO4 vs GPIO18 CS swap.**

Stage 1 Waveshare migration moved SD chip-select:

| Board | PlatformIO env | SD CS |
|-------|----------------|-------|
| Freenove N8R8 + external SD | `freenove_esp32_s3_wroom` | **GPIO18** |
| Waveshare ESP32-S3-ETH onboard TF | `waveshare_esp32_s3_eth` | **GPIO4** |

This boot used **Waveshare firmware** (`Hardware: ESP32-S3-ETH-WAVESHARE`, `sdCs=4`, W5500 `CS=14`, `INT=10`).

If the microSD is still in an **external module wired to GPIO18** (Freenove habit) while firmware probes **onboard GPIO4**, the slot firmware talks to is **empty** → exact log pattern: immediate `SD.begin FAILED`, `CARD_NONE`, SPIFFS fallback.

Ethernet can still work because W5500 pins on this build match the Waveshare PCB (11/12/13/14/10).

### Other possible causes (if CS/slot are already correct)

- TF card not fully seated in **onboard** Waveshare slot  
- Faulty card or slot contact  
- Power/card quality (less common when init fails instantly with CARD_NONE)

---

## Misleading log note

Later in the same boot:

```
[sales-cache] action=start source=SD
```

That string is **hard-coded** in `SessionManager::refreshSalesSummarySnapshot()` and does **not** mean SD mounted. Sales aggregation uses `StorageManager` read paths (SPIFFS fallback when SD is degraded). Do not use this line as proof that SD was available.

---

## Fix (implementation + field procedure)

### Field / operator (primary fix)

1. **Match firmware env to PCB**
   - Waveshare ESP32-S3-ETH → build/flash `pio run -e waveshare_esp32_s3_eth`
   - Freenove + external SD → `pio run -e freenove_esp32_s3_wroom`
2. **Insert microSD in the correct slot**
   - Waveshare: **onboard TF** (CS GPIO4)
   - Freenove: module on **GPIO18**
3. Reboot and confirm serial:
   ```
   [SD] SD mount: SD.begin OK
   [SD] SD mount: cardType=3 cardSize=…
   [storage-lifecycle] state=SD_MOUNTING -> SD_READY
   Storage: SD Ready
   ```

### Firmware (diagnostics added)

`StorageManager::mountSdCard()` now logs on failure:

- `cardType` after failed begin  
- `HARDWARE_REVISION` and `PIN_SD_CS`  
- One-line hint for Waveshare vs Freenove CS slot expectation  

This does not change mount behavior; it makes the next field boot self-explanatory.

### Related fix already shipped (different symptom)

If the card **does** mount (`SD.begin OK`) then immediately degrades with `readSdPayload open fail streak`, see `SD_READY_FALSE_REMOVAL_FORENSIC.md` (layout order + missing-file classification — fixed in commit `fe3683e`).

---

## Validation checklist

| Check | Pass criteria |
|-------|----------------|
| Correct env for PCB | Boot shows expected `hw=` and `sdCs=GPIO…` |
| Card in correct slot | `SD.begin OK`, `cardType != CARD_NONE` |
| No false fallback | Appliance summary `Storage: SD Ready` (not SPIFFS Fallback) |
| Hot-unplug still safe | Unplug → degraded + SPIFFS; reinsert → remount (separate test) |

**Physical re-test on the reported unit:** required to confirm which of GPIO4 mismatch vs slot/seating applied.
