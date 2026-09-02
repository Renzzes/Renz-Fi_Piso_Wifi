# KonekSik-Fi Migration — hAP lite → hEX Refresh

This folder is the **official migration reference** for moving from MikroTik **hAP lite (RB941-2nD-TC)** to **hEX refresh (E50UG)** while keeping the same **Renz-Fi / KonekSik-Fi ESP32 appliance**.

## Documents

| File | Purpose |
|------|---------|
| [MIGRATION_PLAN.md](./MIGRATION_PLAN.md) | Master plan — phases, timeline, risks, decisions |
| [BACKUP_CHECKLIST.md](./BACKUP_CHECKLIST.md) | What to back up from hAP lite + ESP32 before any change |
| [TOPOLOGY_AND_WIRING.md](./TOPOLOGY_AND_WIRING.md) | Physical ports, cables, AP placement |
| [ROUTEROS_HEX_SETUP.md](./ROUTEROS_HEX_SETUP.md) | MikroTik hEX configuration (bridge, HotSpot, DHCP) |
| [RENZFI_CONFIG_CHANGES.md](./RENZFI_CONFIG_CHANGES.md) | ESP32 firmware, Admin, portal, provisioning — what changes |
| [POST_MIGRATION_VERIFICATION.md](./POST_MIGRATION_VERIFICATION.md) | Test checklist after cutover |
| [ROLLBACK_PLAN.md](./ROLLBACK_PLAN.md) | Restore hAP lite if migration fails |
| [SITE_WORKSHEET.md](./SITE_WORKSHEET.md) | Fill-in site-specific IPs, SSIDs, hardware |
| [CUTOVER_RUNBOOK.md](./CUTOVER_RUNBOOK.md) | Minute-by-minute cutover script |

## Quick summary

| Area | Change required? |
|------|------------------|
| ESP32 firmware binary | **No** — same build |
| New external Wi‑Fi AP | **Yes** — hEX has no radio |
| MikroTik RouterOS config | **Yes** — reconfigure on hEX |
| Renz-Fi SD config files | **Partial** — router IP/credentials, re-sync cache |
| Setup wizard Step 4 (Wi‑Fi) | **Not usable on hEX** — configure AP manually |
| Captive portal on MikroTik | **Redeploy** — same files, new router |

## Reference hardware

| Role | Before (v1) | After (target) |
|------|-------------|----------------|
| Gateway | hAP lite RB941-2nD-TC | hEX refresh E50UG |
| Controller | ESP32-S3 + W5500 | **Unchanged** |
| Customer Wi‑Fi | Built-in 2.4 GHz on hAP | **External AP** (bridge mode) |

## Status

- [ ] Phase 0 — Backup complete
- [ ] Phase 1 — hEX + AP procured
- [ ] Phase 2 — Lab / parallel build on hEX
- [ ] Phase 3 — Cutover window
- [ ] Phase 4 — Verification sign-off
- [ ] Phase 5 — hAP lite archived

**Owner / site:** ___________________  
**Migration date (planned):** ___________________  
**Migration date (actual):** ___________________
