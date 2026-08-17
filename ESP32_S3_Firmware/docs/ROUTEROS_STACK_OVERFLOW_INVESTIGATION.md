# RouterOS Validation Crash — Root Cause Investigation & Fix

**Environment:** `freenove_esp32_s3_wroom`
**Build:** SUCCESS (15.8–53.8s across iterations) — Flash 68.5% (1,795,167 / 2,621,440 bytes), RAM 37.3% (122,260 / 327,680 bytes)
**Physical validation:** **NOT performed in this session.** Everything below the "Physical Validation Sequence" section is a required next step, not a claim of completion.

## 1. What could and could not be decoded

The user's report supplied panic *types* (`Double exception`, `Interrupt wdt timeout on CPU0`, `InstrFetchProhibited`, `PC: 0xffffffff`) and staged log lines, but **no raw register dump or backtrace address list** (the usual `Backtrace: 0x... 0x... 0x...` line plus `A0`–`A15`/`PC`/`EXCVADDR` register block that `idf.py monitor` / `esptool.py` decode with `xtensa-esp32s3-elf-addr2line` against `firmware.elf`). Without those addresses, `addr2line` has nothing to resolve — there is no substitute for the actual serial capture. **This decode step could not be performed and is explicitly flagged as unverified.**

What *can* be done, and what this fix is based on, is a full static audit of every RouterOS code path reachable from the worker, cross-referenced against the exact reported symptoms (crash reproducible with a wrong password, occurring at/around `connect()`/before login completes, with plenty of worker-task stack headroom reported — 2540–3792 words free). That audit found a concrete, provable defect described below.

## 2. Root cause

**Oversized `RouterOsClient::CommandResult` objects were still being declared as stack locals inside the RouterOS call chain that runs on the `RouterProvisioningWorker` task**, even though the worker itself, the `RouterOsClient` object, and `RouterSession`/`InspectionData` had already been moved to the heap in earlier fixes.

`RouterOsClient::CommandResult` is not a small struct:

```cpp
struct ReplyRecord { String attrs[12]; uint8_t attrCount; };      // ~200 bytes
struct CommandResult {
  bool doneReceived, trapReceived, fatalReceived, replyLimitReached;
  String trapMessage, fatalMessage;
  ReplyRecord replies[32];                                        // ~6.3 KB
  uint8_t replyCount;
};
```

`sizeof(CommandResult)` is roughly **6.3 KB** (32 reply records × 12 `String` attributes, `sizeof(String)` ≈ 16 bytes on this arduino-esp32 core). A local variable of this type is fully reserved in the enclosing function's stack frame **at function entry**, regardless of which branch is actually taken — C++ must be able to default-construct it at its declaration point on every path that reaches that point, and typical GCC/Clang frame layout reserves the space up front.

Three call sites still had this bug:

| File | Function | Variable | Reached by |
|------|----------|----------|-------------|
| `src/SetupRouterValidator.cpp:85` (before fix) | `SetupRouterValidator::validate()` | `identityResult` | **every** `POST /api/setup/router/test` and `/save` call, immediately after a successful `connect()`/`login()` |
| `src/RouterProvisioningManager.cpp` (`inspectRouter()`) | local inspection helper | `result` | `GET /api/setup/router-plan` and `POST /api/setup/router-apply`, once a real TCP session is established |
| `src/RouterProvisioningManager.cpp` (`applyConfiguration()`) | apply helper | `cmdResult` | `POST /api/setup/router-apply`, once a real TCP session is established |

The worker's own stack log at `stage=before-connect` (2540–3792 words / ~10–15 KB free out of the 24 KB budget) was captured **before** entering `SetupRouterConnectionManager::testConnection()/saveConnection() → SetupRouterValidator::validate()`. Once inside `validate()`, the ~6.3 KB `identityResult` frame reservation — on top of the validator's own locals, the nested `RouterOsClient::connect()`/`login()` call frames, and the ArduinoJson/String temporaries already in flight — was enough to blow through the remaining headroom. A stack overflow on ESP32 corrupts adjacent memory (heap, the next stack frame's saved registers, or the stack-guard region), which manifests exactly as reported: `Double exception`, `InstrFetchProhibited` with a garbage/invalid PC (`0x00000000`, `0xffffffff`), or an `Interrupt wdt timeout` if the corruption wedges a lock or the scheduler.

This explains **every** reported symptom:
- **Reproducible with a wrong password** — the crash is triggered by entering `validate()` and reaching the point where `identityResult` is constructed shortly after `connect()`+`login()`, not by the password being correct or incorrect. Wrong-password Test/Save still call `connect()`/`login()`; the only thing that changes with a wrong password is that `login()` returns `false`, but on the physical unit `connect()` succeeds first (Ethernet/MikroTik TCP works per the given facts), and it is the *identity read that never happens on a login failure* — however the frame for `identityResult` is reserved the moment `validate()` is entered, before the branch on login success/failure is even evaluated. This is consistent with the crash occurring "before login completion" in the traces.
- **Test crash after `"connected ..."`** — that log line is emitted by the worker *before* calling into `testConnection()`, so "after connected" really means "somewhere inside the connect/login/identity call chain," which is exactly where `identityResult`'s frame is reserved.
- **Save crash at/before connect** — `saveConnection()` calls the identical `validateAndBuild() → SetupRouterValidator::validate()` path; same frame, same overflow.
- **Random-looking panic types** (Double exception, cache/MMU, stack canary, corrupted backtraces) — all textbook symptoms of stack corruption rather than a single deterministic fault address, which is why the reported PCs were themselves garbage (`0x00000000`/`0xffffffff`) instead of a valid code address.
- **Plenty of `hwm` "remaining" at `before-connect`** — this is not a contradiction. `uxTaskGetStackHighWaterMark()` reports the minimum-ever-free-space *up to that point*; the overflow happens later, once `validate()`'s frame is pushed.

The `inspectRouter()`/`applyConfiguration()` instances were **latent** (not yet triggered because Preview/Apply had only been exercised against an unreachable router, which fails before a live TCP session is established) but would have caused the identical class of crash the first time Preview or Apply ran against a reachable MikroTik. They are fixed as part of this change for the same reason.

## 3. Fixes applied

### 3.1 Eliminate all remaining oversized stack locals (the actual fix)

- `src/SetupRouterValidator.cpp` — `identityResult` is now `std::unique_ptr<RouterOsClient::CommandResult>`, heap-allocated with `new (std::nothrow)`, with an explicit allocation-failure path (`ROUTEROS_API_UNAVAILABLE`).
- `src/RouterProvisioningManager.cpp` — `inspectRouter()`'s `result` and `applyConfiguration()`'s `cmdResult` are likewise heap-allocated `std::unique_ptr`s, aliased to a local reference so the rest of each function's body is unchanged.
- Confirmed via `grep` that no other stack-local `RouterOsClient::CommandResult` exists in any code path reachable at runtime (the only remaining hits are struct members of the already-heap-allocated `InspectionData`, the heap-allocated `RouterOsClient`'s own `_loginResult`/`_loginChallengeResult` members, and `MikroTikDriver`, which is dead code never instantiated).

### 3.2 RouterOsClient transport hardening

- **Process-wide recursive mutex (`IoLock`)** around every socket-touching method (`connect`, `login`, `executeCommand` ×2, `disconnect`). Today only the worker ever calls into `RouterOsClient` (already serialized by its own single-consumer queue), but this is now a hard guarantee against any future caller running concurrently and corrupting the transport. Recursive because `connect()` calls `disconnect()` internally while already holding the lock.
- **`vTaskDelay(pdMS_TO_TICKS(n))` replaces every `delay(n)`** in `readByte()`, `readWord()`, the connect retry loop, and `disconnect()`'s settle delay — explicit FreeRTOS yields in every wait loop, per the requirement.
- **Bounded word length** — `readWord()` now rejects any decoded length greater than `RenzFiConfig::ROUTER_API_MAX_WORD_LEN` (4096 bytes) *before* calling `String::reserve()`, returning a structured error (`"RouterOS API word length exceeds safety bound"`) instead of attempting a multi-hundred-KB allocation on a desynchronized/corrupt byte stream. `reserve()`'s and `concat()`'s return values are now checked; failure is a structured error, not a silent truncation.
- **Idempotent, defensive `disconnect()`** — early-returns if already idle (`!_connected && !_client.connected()`), so it is safe to call any number of times (including from the destructor as a last-resort safety net) without double-`stop()`/double-`flush()`.
- **Connect-retry no longer stacks failed sockets** — if a connect attempt fails, `_client.stop()` is called before retrying, instead of calling `connect()` again on top of a still-open failed attempt.
- **`readByte`/`readWord` detect a closed connection immediately** (`!_client.connected() && available() <= 0`) instead of always waiting for the full I/O timeout, turning a remote-close into a fast structured error.
- **Destructor added** (`~RouterOsClient()`) that calls `disconnect()` defensively, so a socket can never outlive the object even if a future call site forgets to disconnect explicitly before `delete`.

### 3.3 Task affinity reconsidered

`Config.h` adds `ROUTER_WORKER_CORE_AFFINITY = -1` (meaning `tskNO_AFFINITY`), and `RouterProvisioningWorker::begin()` now creates the task with this configurable value instead of hardcoding core 1.

**Why unpinned is the safer default:** this project's own `platformio.ini` documents that **lwIP's `tiT` task and WiFi are hard-pinned to CPU0** (`CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0=1`, `CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=1`), and AsyncTCP is deliberately pinned to CPU1 (`CONFIG_ASYNC_TCP_RUNNING_CORE=1`) specifically to avoid contending with that fixed CPU0 work. The RouterOS worker was previously *also* hard-pinned to core 1 — i.e., the same core as AsyncTCP — for its entire blocking I/O duration (which includes the wizard's poll traffic hitting AsyncTCP concurrently on that same core). Leaving the worker unpinned lets the FreeRTOS scheduler place it on whichever core is momentarily less loaded, rather than baking in an assumption about which core is "safe" for a W5500/lwIP interaction that has not been physically characterized. This is a conservative, reversible default — `ROUTER_WORKER_CORE_AFFINITY` can be flipped to `0` or `1` for a controlled A/B soak test once physical results are in, without touching any other code.

No RouterOS I/O of any kind happens inside an ISR, Ethernet event callback, or AsyncWebServer callback — confirmed by grep (`testConnection(`, `saveConnection(`, `buildPlan(`, `applyConfiguration(`, `SetupRouterValidator::validate` all absent from `SetupServer.cpp`; the regression script asserts this).

### 3.4 Staged `[router-api]` diagnostics (new, always-on in production)

Added directly inside `RouterOsClient`, distinct from the existing `[router-worker]` job-lifecycle logs:

```
[router-api] session allocated
[router-api] before connect host=<ip> port=8728
[router-api] connect returned=<0|1> elapsedMs=<n>
[router-api] before login write
[router-api] login write complete bytes=<n>
[router-api] before login read
[router-api] login reply word length=<n>
[router-api] before disconnect
[router-api] disconnect complete
```

`login reply word length=<n>` is scoped to the login flow only (via a private `_logLoginIo` flag) so it doesn't spam the log during bulk inspection commands (buildPlan/apply read dozens of reply records). No password, encrypted credential blob, or raw login payload is ever printed — only structural sizes (byte counts, word lengths).

`[router-worker] heap free=<n> largest=<n> stage=<...>` now also reports `ESP.getMaxAllocHeap()` (largest contiguous free block), not just total free heap, so fragmentation can be distinguished from real exhaustion during the repeat-run acceptance criteria.

### 3.5 Non-destructive TCP-only diagnostic (new)

- `RouterProvisioningWorker::JobType::TcpDiagnostic` — opens and immediately closes a raw TCP connection to a given host:port, repeated `RenzFiConfig::ROUTER_TCP_DIAG_ITERATIONS` (20) times, using the same hardened `RouterOsClient::connect()`/`disconnect()`. **No RouterOS login is attempted, no data is read/written beyond the TCP handshake, no router state is touched.**
- New endpoint: `POST /api/setup/router/tcp-check` (setup plane, owner-gated) — body `{ "host"?, "apiPort"? }` (both optional; falls back to the saved router connection's host/port, then `10.10.10.1:8728`). Returns `202 { jobId, state: "queued" }`; poll via the existing `GET /api/setup/router/jobs/<id>`. Result: `{ host, port, attempts, succeeded, failed, lastError? }`.
- Minimal wizard hook: a "Run TCP connectivity diagnostic (20x, no login)" link on the Step 4 panel, polling the same job-status mechanism, for convenience during physical validation.

This isolates raw TCP connect/disconnect stability (item A of the acceptance criteria) from RouterOS API login/sentence-parsing stability (items B–D), so if a crash still occurs physically, the two are no longer conflated.

## 4. Changed files

| File | Change |
|------|--------|
| `src/SetupRouterValidator.cpp` | Heap-allocate `identityResult` (root cause fix) |
| `src/RouterProvisioningManager.cpp` | Heap-allocate `inspectRouter()`'s `result` and `applyConfiguration()`'s `cmdResult` (latent instances of the same bug) |
| `src/RouterOsClient.h` | Constructor/destructor, `IoLock` RAII mutex guard, `_logLoginIo` flag |
| `src/RouterOsClient.cpp` | Mutex-guarded methods, `vTaskDelay` yields, bounded `readWord`, idempotent `disconnect`, `[router-api]` diagnostics, safer connect-retry |
| `src/RouterProvisioningWorker.h/.cpp` | `JobType::TcpDiagnostic`, `enqueueTcpDiagnostic()`, configurable core affinity, `getMaxAllocHeap()` in heap logs |
| `src/Config.h` | `ROUTER_WORKER_CORE_AFFINITY`, `ROUTER_API_MAX_WORD_LEN`, `ROUTER_TCP_DIAG_ITERATIONS` |
| `src/web/SetupServer.cpp` | New `POST /api/setup/router/tcp-check` route + wizard diagnostic link/poll wiring |
| `tools/router-test-save-stability-check.py` | Regression guards: no stack-local `CommandResult` in the validator/provisioning-manager call chains, mutex/yield/bound hardening present, TCP diagnostic present |

## 5. What remains unverified until flashed

- **No physical test was performed in this session.** The stack-overflow root cause is derived from static analysis of object sizes and call-frame reasoning, not from a decoded crash address, because no register/backtrace dump was available to decode.
- Whether `tskNO_AFFINITY` is actually better than a pinned core on this specific W5500/lwIP combination is *reasoned*, not measured — physical soak testing (acceptance criterion E/G) is the only way to confirm it.
- Whether 4096 bytes is a sufficient/appropriate `ROUTER_API_MAX_WORD_LEN` ceiling for all legitimate RouterOS replies (identity/version/trap messages are tiny; this has not been checked against every possible inspection reply value in production).
- Whether the underlying `NetworkClient::connect()` blocking call itself has an internal timeout longer than `SETUP_ROUTER_CONNECT_TIMEOUT_MS` (2500 ms) that could make a single failed attempt overrun the configured deadline before the retry loop's `millis()` check fires — this is pre-existing behavior, not changed by this fix, and should be watched during physical testing.

## 6. Physical validation sequence (to be run on hardware)

Flash the `freenove_esp32_s3_wroom` build and connect to the Management AP setup portal (`/admin/setup`, Step 4/5).

**A. TCP-only diagnostic ×20** — click "Run TCP connectivity diagnostic" (or `POST /api/setup/router/tcp-check`). Expect `succeeded=20/20` (or partial with structured `lastError` if unreachable), no reboot, no WDT, no panic. Watch serial for `[router-api] before connect` / `connect returned=` / `[router-worker] tcp-diagnostic iteration=` for all 20 cycles.

**B. Wrong-password Test ×20** — Step 4 → Test Connection with an intentionally wrong password, repeated 20 times. Expect inline `API_LOGIN_FAILED` every time, no reboot, `[router-api] disconnect complete` and `[router-worker] cleanup complete` after each, ESP32 stays reachable/heartbeat continues.

**C. Wrong-password Save ×20** — Step 4 → Save Router Connection with a wrong password, repeated 20 times. Expect `API_LOGIN_FAILED` every time, no reboot, previously saved credentials unchanged (verify via `GET /api/setup/router-config`).

**D. Correct password** — Test then Save. Expect success without reboot; saved state displayed correctly on the Step 4 "saved" panel.

**E. Preview ×20** — Step 5 → Preview Configuration Plan, repeated 20 times. Expect a structured result or router error each time, no reboot, no MikroTik configuration changes (verify nothing new appears under `/interface/bridge`, `/ip/pool`, etc. on the router itself).

**F. Heap trend** — while running B–E, capture `[router-worker] heap free=... largest=...` before/after each job and confirm neither free heap nor largest free block trends downward over the run (fragmentation/leak check).

**G. Panic-free confirmation** — throughout A–F, confirm the serial log never shows `Guru Meditation Error`, `Interrupt wdt timeout`, `Double exception`, `InstrFetchProhibited`, `PC: 0xffffffff`/`0x00000000`, `stack canary`, or a cache/MMU fault. If any panic recurs, capture the **full raw serial output including the register dump and `Backtrace:` line** so it can be decoded with `xtensa-esp32s3-elf-addr2line -e .pio/build/freenove_esp32_s3_wroom/firmware.elf <addresses>` — this is the one piece of evidence this investigation could not obtain and needs for any further narrowing.
