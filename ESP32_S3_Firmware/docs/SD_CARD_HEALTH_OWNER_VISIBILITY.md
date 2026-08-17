# SD Card Health Owner Visibility

## Contract

`GET /api/storage/status` retains its authenticated production route and its
legacy fields (`storageMode`, `sdPresent`, `sdMounted`, `fallbackActive`,
`capacity`, and `used`). New fields are additive.

Health is computed only when `StorageManager::refreshRuntimeSnapshot()` already
runs. Request handling serializes cached values and never scans history
ledgers. Pending replay counts records in the three fixed, bounded emergency
spools; recovery counts inspect only the bounded fallback manifest,
quarantine, and restore-journal filenames.

Emergency usage is measured against `FB_HARD_LIMIT_BYTES` (currently 320 KiB):

- `WARNING` at 70% or greater while emergency storage is active
- `CRITICAL` at 90% or greater while emergency storage is active
- `DEGRADED` when SD is unavailable below those thresholds and SPIFFS is active
- `READ_ONLY` when SD is readable but not writable below the critical threshold
- `UNKNOWN` when a claim cannot be supported by cached runtime evidence

CRC health and wall-clock timestamps are nullable. A successful write or backup
is tracked in RAM only and is reset by reboot.

The legacy `storageMode` field remains `SD` or `SPIFFS`. The additive `mode`
field is owner-facing: `Normal SD Storage`, `Emergency Internal Storage`,
`Read Only`, or `Unknown`. The additive `mounted` field refers specifically to
the SD filesystem.

## Regression guard

Before release:

1. Build the production PlatformIO environment.
2. Build the embedded frontend.
3. Confirm a storage-status request performs no ledger iteration or RouterOS
   call.
4. Confirm `storage.changed` invalidates `["storage", "status"]`.
5. Confirm the first UI health value produces no toast and only a later state
   transition does.
6. Confirm transaction, CRC, rollback, restore, journal, and replay algorithms
   are unchanged; telemetry may only observe their existing outcomes.

Run `tools/storage-health-regression-check.py` to enforce the additive API,
state, event, notification, polling, and no-animation contracts.
