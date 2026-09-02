# Crash / Guru Meditation — Boot Report to SD

**Purpose:** When the appliance is not attached to a laptop Serial monitor, unexpected reboots (Guru Meditation / panic, WDT, brownout, software reboot) are recorded once on the **next boot** with a **date stamp**, so the owner can review them later from Admin Logs / history export.

**Not continuous monitoring:** no loop polling, no core-dump decode on-device, no extra DMA load. One Serial line + one NDJSON append on boot when the previous reset was interesting.

## What is recorded

| Field | Meaning |
|-------|---------|
| `date` / `t` / `eventAt` | Wall clock `YYYY-MM-DDTHH:MM:SS` (Asia/Manila) when NTP/setup allows; otherwise `uptime-ms:N` |
| `resetReason` | `PANIC`, `TASK_WDT`, `INT_WDT`, `WDT`, `BROWNOUT`, `SDIO`, `SW_REBOOT` |
| `msg` | Human-readable line also shown in Admin RAM logs |

**Skipped (normal boots):** `POWERON`, `EXT`, `DEEPSLEEP`.

## Where it is stored

1. **SD (preferred):** `/history/logs/YYYY-MM.ndjson` (same Logs history ledger as Logger)
2. **SPIFFS fallback:** `/fb/hl.ndjson` spool, replayed to SD when the card returns
3. **Admin UI:** appears in RAM Logs immediately via `Logger::errorLocal("crash", …)` + SSE

## Code

- `src/CrashBootReport.cpp` / `.h` — one-shot reporter
- Called from `FirmwareApp::begin` after Logger + Installation (and NTP bind when ready)
- Logs spool wired in `NdjsonLedger::spoolFor(Logs)` + `replayHistorySpools`

## How to review in the field

1. Open Admin → Logs (recent crash line in RAM after reboot), or
2. Download `/api/history/logs` / export Logs for the month, look for `"type":"crash"`.
