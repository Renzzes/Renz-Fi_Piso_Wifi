# Setup Plane AsyncTCP TWDT — Prevention Documentation

**Purpose:** Stop repeating Guru Meditation / Task Watchdog resets on setup pages.  
**Status:** Mandatory process control — not an implementation change.  
**Basis:**  
- `TWDT_OWNER_ENDPOINT_ROOT_CAUSE.md` / `OWNER_SETUP_TWDT_IMPLEMENTATION_REPORT.md`  
- `TWDT_WIFI_SELECTION_ROOT_CAUSE.md` (this incident)  
- `ESP32_S3_Firmware/docs/ADMIN_DASHBOARD_ASYNCTCP_WATCHDOG_FORENSIC.md`

---

## 1. Hard rule

> **Every setup HTTP endpoint must be audited for synchronous SD/NVS/network/worker-wait operations on `async_tcp` before future implementation changes are accepted.**

A fix for one setup page does **not** protect other setup pages. Endpoint-scoped mitigations leave siblings vulnerable to the same class of failure.

---

## 2. Why this keeps coming back

| Incident | Endpoint | Unsafe work on `async_tcp` |
|---|---|---|
| Owner TWDT | `POST /api/setup/owner` | NVS + transactional SD + history flush + provisioning/installation persist |
| Wi-Fi selection TWDT | `POST /api/setup/router/wifi/selection` | `persist()` → transactional SD + SPIFFS continuous checkpoint |
| Admin test (related class) | Admin RouterOS test | Synchronous wait for RouterOS / worker |

Common unsafe pattern:

```text
HTTP arrives on async_tcp (TWDT-subscribed)
  → durable StorageManager writeJson / appendHistory / SPIFFS checkpoint
     OR blocking worker/RouterOS wait
  → budget exceeded → Guru Meditation → reboot
  → setup cannot finish
```

SD durability, transactional writes, CRC, rollback, and SPIFFS checkpoints remain **required**. They must not run as long synchronous work on `async_tcp`.

### New incident class (must also be audited)

Even when the HTTP route is read-only/fast, watchdog can still occur if a **loopTask** helper on the same CPU monopolizes flash/filesystem work long enough to starve `async_tcp`.

Observed class:

```text
loopTask periodic telemetry
  → StorageManager::refreshRuntimeSnapshot()
  → fallbackTotalBytes()/SPIFFS exists/stat walk
  → flash read critical sections
  → async_tcp starved on CPU1
  → task_wdt reports failed async_tcp while CPU1 runs loopTask
```

Prevention rule extension:

- Audit both **HTTP callbacks** and **their coupled periodic/deferred loop paths**.
- A route is not “safe” just because its handler is lightweight if its polling cadence triggers heavy loopTask work on shared CPU.

---

## 3. Pre-merge audit checklist (setup routes)

For **each** changed or newly touched `SetupServer` / setup API route:

### A. Task ownership

- [ ] Confirm whether the handler body runs on `async_tcp`, `router_worker`, or `loopTask`.
- [ ] If boot/registration log says `(sync)`, treat as high risk until proven short.

### B. Forbidden on `async_tcp` (unless proven < WDT budget with hardware evidence)

- [ ] `StorageManager::writeJson` / `writeJsonToSdSerialized`
- [ ] SPIFFS `checkpointToSpiffs` / `spiffsWriteFile` / transactional recover
- [ ] `appendHistory` / `NdjsonLedger` flush
- [ ] Multi-put NVS bursts that accompany large FS work
- [ ] Synchronous `RouterProvisioningWorker::dispatch` / semaphore wait for RouterOS
- [ ] Network connect / scan / DHCP waits

### C. Allowed patterns (already proven in-tree)

- [ ] Validate + update RAM; return quickly; durable commit from `loop()` (owner pattern).
- [ ] Enqueue RouterWorker job; return `202` / job id (configure / finish / router save-test patterns).
- [ ] Background refresh that does not block the HTTP callback (wifi networks discovery).

### D. Regression gate

- [ ] Compare against prior TWDT forensics — is this the same class on a new endpoint?
- [ ] If yes: reject “it builds” as success; require timing/offload plan before merge.
- [ ] Confirm previous endpoint fixes remain intact (do not reintroduce owner sync persist).
- [ ] Confirm loopTask/deferred telemetry on shared CPU is bounded/throttled and cannot starve async_tcp.

### E. Hardware proof for setup-critical paths

- [ ] No `task_wdt` / `async_tcp` abort while walking Owner → Router → Scan → Wi-Fi → Apply → Finish.
- [ ] Serial shows durable commit completion **off** the HTTP callback when deferred.

---

## 4. Known setup endpoints to keep on the watchlist

Re-audit when StorageManager durability or SetupServer registration changes:

| Endpoint | Current risk note (forensic/source) |
|---|---|
| `POST /api/setup/owner` | Mitigated (deferred durable commit) — do not regress |
| `POST /api/setup/router/wifi/selection` | **Proven TWDT** — still sync `persist()`/`writeJson` |
| `POST /api/setup/router/save` / `test` | Worker path — keep off sync RouterOS waits |
| `POST /api/setup/router/existing-network/configure` | Worker `202` — keep |
| `POST /api/setup/finish` | Worker when not already ready — keep |
| Any new `SetupServer` `(sync)` route that calls `persist`/`writeJson` | Treat as TWDT candidate |

This list is a living gate, not permission to redesign the wizard.

---

## 5. What prevention is *not*

- Do not disable or widen TWDT.
- Do not remove SD transactional durability or checkpoints.
- Do not increase RouterOS command count or polling to “work around” setup hangs.
- Do not redesign RouterWorker / StorageManager / setup wizard structure for convenience.
- Do not assume “we fixed setup watchdog once” means setup is safe.

---

## 6. Required question before accepting any setup change

> Does this setup HTTP callback perform durable SD/SPIFFS/NVS work or a blocking wait on `async_tcp`?

If **yes**, the change is incomplete until that work is deferred or offloaded using an existing safe pattern.

---

## 7. References

- `TWDT_WIFI_SELECTION_ROOT_CAUSE.md` — Wi-Fi selection ELF-proven tip  
- `TWDT_OWNER_ENDPOINT_ROOT_CAUSE.md` — owner endpoint class definition  
- `OWNER_SETUP_TWDT_IMPLEMENTATION_REPORT.md` — owner-scoped mitigation (not global)  
- `SD_STORAGE_PRODUCTION_HARDENING_IMPLEMENTATION_REPORT.md` — durability amplifier context  
- `ESP32_S3_Firmware/docs/ADMIN_DASHBOARD_ASYNCTCP_WATCHDOG_FORENSIC.md` — related async_tcp wait class  
