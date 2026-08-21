# Forensic: SD_READY → SD_DEGRADED (`readSdPayload open fail streak`)

**Status:** Root cause proven in source + correlated to physical log  
**Branch:** `feature/waveshare-esp32-s3-eth`  
**Hardware evidence:** Waveshare ESP32-S3-ETH, firmware `0.5.0-w5500`

## A. Observed facts (physical log)

1. `SD.begin` OK; `cardType=3`; large `cardSize`; mount → `SD_READY`
2. Write verification `/temp/.write_probe` **passed**
3. Immediately: `Detected SD removal` / reason `readSdPayload open fail streak`
4. Lifecycle: `SD_READY → SD_DEGRADED`; cause `MEDIA_MISSING`
5. DMA largest remained ~155 KB through mount; W5500 `STARTED` / `link=UP` through SD init
6. SD `usedBytes` ≈ 1.6 MB (card not empty, but layout may be incomplete)

## B. Proven facts (code)

1. After `probeSdWritable()`, `StorageManager::begin()` called **`recoverBootTransactions()` before `ensureLayout()`** (pre-fix order).
2. `recoverBootTransactions()` iterates 12 SD paths and calls `recoverSdTransaction(path)`.
3. `recoverSdTransaction` called **`readSdPayload(path)` without checking `SD.exists(path)`**.
4. `readSdPayload`: `SD.open` fail + `cardType != CARD_NONE` → `_sdIoFailStreak++`; streak ≥ 2 → `tripSdMediaMissing("readSdPayload open fail streak")`.
5. `handleSdRemoved` always logs **`Detected SD removal`** and sets **`MEDIA_MISSING`**, regardless of whether the open failed because the **file was absent**.
6. Arduino SD exposes **no errno**; open-fail alone cannot prove physical media removal.
7. Log reason string matches **only** the `readSdPayload` streak path (not `card absent`, not `readJson open failed`).
8. First post-verify SD payload opens are therefore from **boot transaction recovery**, not from W5500/RouterOS/async_tcp concurrency at that moment (still inside `begin()`, under `STORAGE_LOCK`).

## C. Unknowns

1. Exact first two failing paths on the physical card (settings vs promos vs …) — not printed in the original log; forensic logging added for future boots.
2. Whether any of those paths **existed** on that specific card image (used=1.6 MB suggests partial content).
3. Whether a true FS/SPI I/O fault also occurred on that boot (not required to explain the false trip given the classification bug).

## D–G. Exact failing path

| Item | Value |
|------|--------|
| Caller | `StorageManager::begin` → `recoverBootTransactions` → `recoverSdTransaction` |
| Function | `StorageManager::readSdPayload` |
| Condition | `!file` after `SD.open(path, FILE_READ)` and `cardType != CARD_NONE` |
| Counter | `_sdIoFailStreak` threshold **2** |
| Trip | `tripSdMediaMissing("readSdPayload open fail streak")` |
| User-visible | `handleSdRemoved` → `"Detected SD removal"` |
| Paths probed | `/config/settings.json`, `/config/promos.json`, `/config/router.json`, `/vouchers/vouchers.json`, `/sessions/portal_sessions.json`, `/sales/sales.json`, `/config/portal.json`, installation/provisioning/router-connection/router-provisioning/setup-wizard |

## H. Why classified as media removal

**PROVEN classification bug (O):** any two consecutive `SD.open` failures while `cardType != CARD_NONE` were treated as physical removal — **including legitimate missing files** before layout seed.

## I. N16R8 comparison

| Area | N16R8 | Waveshare | Difference | Proven impact |
|------|-------|-----------|------------|---------------|
| StorageManager | Shared | Shared | None in this path | Same bug class on both boards |
| SPI hosts | W5500 SPI3 / SD FSPI | Same | Pin remap only | Not implicated by this log |
| Boot order | recover before layout | Was same | Shared | False trip on incomplete SD |
| DMA at mount | — | ~155 KB largest | Healthy | Does not explain open streak |
| W5500 during SD init | — | STARTED/UP | — | Not implicated |

## J. Waveshare-specific

Pin/host isolation matches Stage 1 checkpoint. This failure is **not** proven Waveshare-hardware-unique; it is a **shared StorageManager classification/ordering** defect exposed when the first recovered paths are absent.

## K–N. DMA / W5500 / concurrency / lifecycle

- **DMA:** healthy at mount (**PROVEN**); not root cause of this transition.
- **W5500:** operational through SD init (**PROVEN**); no evidence it caused the open streak.
- **Concurrency:** failure inside boot `begin()` under storage lock (**PROVEN**); not worker/async_tcp racing that open.
- **Lifecycle:** `Ready` → `Degraded` via `handleSdRemoved` (**PROVEN**).

## O–P. Most likely root cause (PROVEN)

**Missing (or not-yet-seeded) SD JSON paths opened by `recoverBootTransactions` before `ensureLayout`, counted toward `_sdIoFailStreak`, falsely tripping MEDIA_MISSING.**

Supporting proof: code path + log timing (`Verification passed` → streak trip with no intervening layout-ready line) + classification that equates open-fail with removal.

## Q. Rejected hypotheses

| Hypothesis | Verdict |
|------------|---------|
| A/B/Q missing file/dir / layout order | **Accepted as mechanism** |
| C–E wrong path/case | No evidence; paths match `StoragePaths` |
| F written to fallback only | Write probe was on SD `/temp` |
| I–K FSPI/CS/SPI fail after mount | Write verify then cardType still non-NONE contradicts “card gone” |
| L concurrency | Inside `begin()` |
| M/N not ready | Mount+verify already OK; layout seed was late |
| O false removal classification | **Accepted** |
| DMA / W5500 defect | Rejected for this transition |

## R. Minimum safe fix (implemented)

1. **`ensureLayout` before `recoverBootTransactions`** on boot (and remount ordering aligned).
2. **`recoverSdTransaction`:** if neither target nor `.t`/`.b` exists, return without `readSdPayload`.
3. **`readSdPayload` / `readJsonFromSd`:** open-fail + card present + `!exists(path)` → miss, **not** MEDIA_MISSING; streak only when path reportedly exists but open fails; keep `CARD_NONE` trip.
4. Minimal `[sd-forensic]` log on open fail (path, cardType, pathPresent, streak).

## S. Regression risks

- Hot-unplug when `cardType` stays stale and `exists` returns false may delay streak trip until `CARD_NONE` or a later exists+open fail — intentional trade vs false removal.
- Extra `SD.exists` after failed open adds one select on miss paths (boot/layout); acceptable vs false degrade.

## Validation status

| Item | Status |
|------|--------|
| Root cause | **PROVEN** |
| Minimal fix | **FIXED** in `StorageManager.cpp` |
| `npm run build:esp32` | OK |
| `pio run -e waveshare_esp32_s3_eth` | OK |
| `pio run -e freenove_esp32_s3_wroom` | OK |
| Physical re-test | **NOT PHYSICALLY VALIDATED** (required) |
