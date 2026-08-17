# TWDT Owner Endpoint Root Cause

**Mode:** Forensic only — no code changes, no TWDT/policy changes.  
**Scope:** `POST /api/setup/owner` → `"Owner credentials provisioned"` → `async_tcp` task watchdog → reboot.  
**Activation failure is out of scope** (already proven elsewhere).

**ELF:** workspace  
`ESP32_S3_Firmware/.pio/build/freenove_esp32_s3_wroom/firmware.elf`  
SHA256 prefix **`a4ba411dd`** (matches the build cited for this crash).

---

## 0. Executive verdict — ONE definitive root cause

**Root cause:**  
`POST /api/setup/owner` runs **synchronous durable SD I/O on the TWDT-subscribed `async_tcp` task**. Before the visible log line, that task already completes a **full transactional rewrite** of `/sessions/admin.json` via `invalidateAllSessions()` → `clearJsonArray()` → `writeJson()`. The Serial line `"Owner credentials provisioned"` is printed **inside** `Logger::write` **before** the next durable call. The **first blocking instruction after that Serial print** is:

```text
Logger::write
  → StorageManager::appendHistory(...)
    → NdjsonLedger::appendSd(...)
      → NdjsonLedger::appendLineSd(...)   // SD.open APPEND + print + flush + close
```

**File / lines:**

| Step | File | Lines |
|---|---|---|
| Log + first post-log block | `src/Logger.cpp` | `127–150` (Serial at `132`; `appendHistory` at `148–150`) |
| Append implementation | `src/NdjsonLedger.cpp` | `153–173`, `138–150` (`file.flush()` at `148`) |
| Pre-log budget burn | `src/AuthManager.cpp` | `91–93` → `187–189` → `StorageManager.cpp` `1322–1327` → `1245–1270` |

**Why this is definitive (without inventing a backtrace):**

1. The log string exists in only one place: `AuthManager::provisionOwnerCredentials` → `Logger::info` → `Logger::write`.
2. In `Logger::write`, `Serial.printf` of that message runs **before** `appendHistory`. Runtime “log then immediate TWDT” therefore places the hang **at or after** `appendHistory`, still on `async_tcp`.
3. `invalidateAllSessions` has **already** finished a transactional SD write on the same task **before** that Serial line — so the TWDT budget is already largely consumed when the log appears; `appendHistory` is the **tipping** call.
4. There is no RouterOS work on this path.

**Part A limitation:** this query still contains **no `Backtrace: 0x…` PC list**. Addresses therefore **cannot** be resolved to instruction-level symbols. The conclusion above is **source-order + runtime log ordering**, not addr2line. Paste the backtrace from the same `a4ba411dd` ELF to pin the exact PC inside `appendLineSd` / FAT / SPI.

---

## 1. Part A — Backtrace resolution

| Item | Status |
|---|---|
| ELF SHA `a4ba411dd` | Confirmed match to current `firmware.elf` |
| `xtensa-esp32s3-elf-addr2line` | Available under PlatformIO packages |
| Hardware `Backtrace:` addresses in this investigation request | **Not provided** |
| Per-frame address → function → file → line → instruction | **Blocked** |

When PCs are available, decode with:

```text
xtensa-esp32s3-elf-addr2line -pfiaC -e ESP32_S3_Firmware/.pio/build/freenove_esp32_s3_wroom/firmware.elf <addrs>
```

Expected stack shape (source-proven, not decoded):

```text
async_tcp HTTP callback
  SetupServer lambda  (/api/setup/owner)
    SetupProvisioningManager::createOwner
      AuthManager::provisionOwnerCredentials
        Logger::write                     ← Serial line already emitted
          StorageManager::appendHistory   ← FIRST post-log blocking call
            NdjsonLedger::appendSd
              NdjsonLedger::appendLineSd  ← SD flush
```

---

## 2. Part B — Execution timeline after the log

### 2.1 Inside the same `Logger::write` call (after Serial)

| # | Function | Blocking class | Notes |
|---|---|---|---|
| B1 | `Logger::write` continues | — | After `Serial.printf` at `Logger.cpp:132` |
| B2 | `pushRam` | RAM only | Negligible |
| B3 | `emitEntry` → EventBus SSE | Network queue copy | Not durable storage; not 5 s class |
| B4 | **`StorageManager::appendHistory`** | **ScopedStorageLock + SD append + flush** | **First durable block after log** |
| B5 | `NdjsonLedger::appendSd` | JSON serialize + path resolve | |
| B6 | `NdjsonLedger::appendLineSd` | `SD.open(FILE_APPEND)`, seek/read, `print`, **`flush`**, `close` | Likely stall site inside B4 |

`allowSpool=false` for this call (`Logger.cpp:150`) ⇒ **no SPIFFS spool fallback** on failure; on healthy SD it still does the SD append.

### 2.2 After `provisionOwnerCredentials` returns (still on `async_tcp`)

| # | Function | Blocking class |
|---|---|---|
| C1 | `AuthManager::setOwnerUsername` | NVS `putString` |
| C2 | `AuthCredentials::hashPassword` ×2 | CPU (mbedtls SHA-256) — ms-class |
| C3 | `SetupProvisioningManager::persist` | **Transactional SD `writeJson` + SPIFFS checkpoint** (`ProvisioningFile` is checkpoint-eligible) |
| C4 | `syncInstallationState` → `InstallationStateManager::persist` | **Transactional SD + SPIFFS checkpoint** |
| C5 | `fillSetupStatusData` + `serveJsonEnvelope` | RAM + TCP send |

If TWDT is truly “immediate” after the Serial line, the crash is **B4–B6**, not C3–C4. C3–C4 remain the **largest remaining** durable ops if B4 completes.

### 2.3 Work already finished *before* the log (same TWDT window)

| # | Function | Blocking class |
|---|---|---|
| A1 | `hashPassword` (owner) | CPU |
| A2 | `saveCredentials` | NVS puts |
| A3 | `invalidateAllSessions` | RAM clear + **`clearJsonArray(/sessions/admin.json)`** |
| A4 | `clearJsonArray` → **`writeJson`** | **Full transactional SD rewrite** (stage/backup/rename/verify). **Not** SPIFFS-checkpointed (`AdminSessionsFile` ∉ `isContinuousCheckpointEligible`) |

A3–A4 burn the majority of the 5 s TWDT budget **before** the human-visible log.

---

## 3. Part C — Storage lock analysis

### 3.1 Is `ScopedStorageLock` held across the log line?

**No.**  
`invalidateAllSessions` → `clearJsonArray` acquires and releases the recursive storage mutex **before** `Logger::info` is called.

At the Serial print, **no** storage lock is held from the owner path.

### 3.2 Who owns the lock during post-log `appendHistory`?

| Question | Answer |
|---|---|
| Who takes the lock? | **`async_tcp` itself** inside `StorageManager::appendHistory` (`StorageManager.cpp:1335–1336`) |
| Waiting on another owner? | **Not evidenced.** Crash logs cited for this incident do not include `[storage] Timed out waiting for storage transaction lock` (`lockStorage` at `:93`). That print is the only proof of contended wait. |
| Actual vs theoretical | **Actual (source):** caller is `async_tcp` performing I/O under its own lock. **Unproven:** another task holding the mutex. |

### 3.3 Nested lock note (pre-log)

`clearJsonArray` holds the recursive mutex, then calls `writeJson`, which takes the **same** recursive mutex again (`StorageManager.cpp:1322–1327`, `1245–1248`). That does **not** deadlock; it does keep the mutex held for the entire transactional write.

---

## 4. Part D — Transaction timing

### 4.1 Honest limit

**Wall-clock milliseconds cannot be proven from source alone.** No `millis()`/`esp_timer` instrumentation exists around these calls, and no timed serial trace was supplied. Inventing “12 ms / 81 ms / 204 ms” would be false forensic evidence.

### 4.2 Ranked cost (I/O operation count = relative wall time proxy)

| Operation | On path? | Relative cost | Why |
|---|---|---|---|
| `writeJson` transactional SD (`writeJsonToSdOnce`) | Yes — **before log** (`admin.json`); **after log** (`provisioning.json`, `installation.json`) | **Highest** | recover → stage write → flush → read-back verify → rename dance → verify again; optional **retry ×2** (`STORAGE_WRITE_ATTEMPTS`) |
| `checkpointToSpiffs` / `spiffsWriteFile` | Yes — **after log** on provisioning + installation only | **Very high** | Second transactional filesystem (SPIFFS stage/backup/verify) |
| `appendHistory` / `appendLineSd` | Yes — **immediately after log** | **Medium** (usually); **can tip TWDT** when budget nearly gone | Single append + **flush**; first post-log durable op |
| NVS `Preferences::put*` | Yes | Low–medium | Flash page update |
| SHA-256 | Yes | Low | CPU only |
| SSE `emitEntry` | Yes | Low | Queue, not SD |

### 4.3 Cumulative picture (qualitative)

```text
async_tcp HTTP callback starts
  hash + NVS
  transactional write /sessions/admin.json     ← large pre-log burn
  Serial: "Owner credentials provisioned"
  appendHistory (SD flush)                     ← FIRST post-log block / tip
  NVS username
  2× SHA-256
  transactional write provisioning.json
    + SPIFFS checkpoint                        ← largest post-log if reached
  transactional write installation.json
    + SPIFFS checkpoint
  JSON response
```

TWDT period is **5 s** (documented for `async_tcp`). `STORAGE_LOCK_TIMEOUT_MS` is also **5000** (`Config.h:352`) — same order as TWDT, so a contended lock wait alone can exhaust the budget (not observed in the cited log text).

---

## 5. Part E — Regression analysis

| Era | Behavior | Effect on this crash |
|---|---|---|
| Admin AsyncTCP TWDT fix | Moved **Admin RouterOS** off `async_tcp` via worker + 202 | **Does not apply** to `/api/setup/owner` (still fully synchronous on `async_tcp`) |
| Phase 9 `CONFIG_ASYNC_TCP_RUNNING_CORE=1` | Pins `async_tcp` to CPU1 | Still present; does not remove SD work from the callback |
| **SD Storage Production Hardening (2026-08-09 report)** | Transactional SD writes, verify/rename, recursive storage mutex, SPIFFS checkpoints for eligible config files, Logger → `appendHistory` NDJSON | **Introduces / amplifies** durable cost on every `writeJson` and every `Logger::info` |

**Regression source (exact):**  
Not removal of the Admin worker fix.  
**Additive blocking from SD hardening + pre-existing design that runs owner provisioning + durable logging on `async_tcp`.**

Concrete amplifiers on this endpoint:

1. `invalidateAllSessions` → full transactional rewrite of `/sessions/admin.json` even on first boot.  
2. `Logger::write` always calling `appendHistory` for Info (including this exact log).  
3. Later `persist` / installation writes using transactional SD **plus** SPIFFS checkpoint (eligible paths).

---

## 6. Part F — CPU ownership

| Task | Core | Evidence |
|---|---|---|
| `async_tcp` | **CPU 1** | `platformio.ini` `CONFIG_ASYNC_TCP_RUNNING_CORE=1` |
| Arduino `loopTask` | **CPU 1** (Arduino default) | Same core as `async_tcp` by design (Phase 9 docs) |
| `router_worker` | Unpinned (`ROUTER_WORKER_CORE_AFFINITY = -1`) | `Config.h:193`, `RouterProvisioningWorker.cpp:201–207` |
| Storage / Logger | **No dedicated task** — run on the **caller’s** task | Here: **`async_tcp`** |

**Cross-core sync:** not required for this failure. Storage mutex is recursive FreeRTOS mutex; owner path takes it on `async_tcp`. Contended wait with `loopTask` is possible in theory but **not proven** by a lock-timeout log on this incident.

Shared CPU1 with `loopTask` can worsen scheduling under load; the proven hang class here is **`async_tcp` executing SD I/O**, not waiting on RouterOS.

---

## 7. Deliverable summary

| # | Finding |
|---|---|
| **1. Exact blocking function (post-log tip)** | `StorageManager::appendHistory` ← `Logger::write` |
| **2. Exact blocking instruction (source)** | `NdjsonLedger::appendLineSd` → `file.flush()` (`NdjsonLedger.cpp:148`); PC unknown until backtrace |
| **3. Execution timeline** | §2 |
| **4. Storage ownership timeline** | Pre-log: `async_tcp` owns lock during `clearJsonArray`/`writeJson`. At log: unlocked. Post-log: `async_tcp` owns lock during `appendHistory`. |
| **5. Mutex ownership timeline** | Same as §3 / §7.4 — no evidence of foreign owner |
| **6. Wall-time measurements** | **Not available** without hardware instrumentation; relative ranking in §4 |
| **7. Regression source** | SD hardening transactional/`appendHistory` cost on an endpoint that still runs fully on `async_tcp` |
| **8. Recommended implementation strategy** | See §8 — **do not implement here** |

---

## 8. Recommended implementation strategy (smallest production-safe — NOT implemented)

Constraints honored: do **not** disable TWDT, do **not** raise TWDT timeout, do **not** busy-wait/poll, do **not** add RouterOS work, do **not** redesign storage architecture, do **not** require a new task priority scheme as the first move.

**Smallest safe directions (pick one primary, keep others as follow-ups):**

1. **Eliminate unnecessary durable work on this callback (preferred smallest):**  
   - On first-boot / empty session table, skip `clearJsonArray` SD rewrite inside `invalidateAllSessions` when there is nothing to clear.  
   - Do not force a durable `appendHistory` for the `"Owner credentials provisioned"` info line on the HTTP path (RAM/Serial/SSE remain).

2. **Preserve durability without holding `async_tcp` across multi-write transactions:**  
   Complete NVS + in-memory owner state, **send HTTP 200 first**, then perform `provisioning.json` / `installation.json` persistence from an **already existing** deferred path (loop/deferred work queue already used elsewhere) — without inventing RouterOS traffic or a storage redesign.

3. **Do not** “fix” by moving arbitrary code onto `router_worker` or by changing TWDT/core pins.

Hardware validation after a future fix must show: owner POST completes with no `task_wdt` / no reboot; provisioning + installation files still durable across power loss.

---

## 9. What is still required to close Part A at instruction level

Provide the serial block from the failing boot:

```text
E task_wdt: Task watchdog got triggered. ...
Task/user that failed to reset: async_tcp ...
Backtrace: 0x........:0x........ ...
```

With ELF `a4ba411dd`, each PC can be filled into the address → function → file → line → instruction table. Until then, the **definitive functional root cause** remains:

> **Synchronous SD durability on `async_tcp` during owner setup, tipping at `Logger::write` → `appendHistory` immediately after the provisioned log, after a pre-log transactional rewrite of `/sessions/admin.json`.**
