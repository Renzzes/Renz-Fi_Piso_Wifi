# Rollback Plan — Restore hAP lite

Use if hEX migration fails verification or site cannot operate.

**Target time:** ≤ 30 minutes if hAP lite was kept intact and backup exists.

---

## When to rollback

- No customer Wi‑Fi after 30 min troubleshooting
- HotSpot not intercepting traffic
- ESP32 cannot reach RouterOS API on hEX
- Widespread coin/activate failures
- Owner decision to abort cutover

---

## Prerequisites

- [ ] hAP lite hardware available (same unit or spare)
- [ ] `haplite-YYYY-MM-DD.backup` OR `/export` + known good config
- [ ] ISP cable labeling documented
- [ ] ESP32 SD **not** factory-reset (router.json can be reverted)

---

## Rollback steps

| Step | Action |
|------|--------|
| 1 | Announce service restore in progress |
| 2 | Power off hEX; disconnect from ISP and LAN |
| 3 | Reconnect ISP → hAP lite WAN |
| 4 | Connect ESP32 → hAP lite LAN port |
| 5 | Connect AP/cables as **before migration** (phones on hAP Wi‑Fi if no external AP) |
| 6 | Power hAP lite; wait for boot |
| 7 | Optional: restore `.backup` if hAP config was changed during attempt |
| 8 | Verify hAP IP (original gateway IP) |
| 9 | On ESP32 SD: restore `router.json` from backup if edited for hEX |
| 10 | Reboot ESP32 or power-cycle |
| 11 | Admin → verify MikroTik **Online** |
| 12 | Test coin → portal → internet |
| 13 | Document failure reason for next hEX attempt |

---

## Restore router.json from backup

Replace SD file `/config/router.json` with backed-up copy:

- `host` → hAP lite IP
- `username` / `password` → hAP API credentials

Then Admin → Synchronize Router.

---

## After rollback

- Keep hEX for bench testing; do not discard
- Update MIGRATION_PLAN.md status — note failed phase
- Fix root cause before second cutover attempt

---

## Contact / escalation

| Item | Detail |
|------|--------|
| Installer | |
| Owner | |
| Backup location | `KonekSik-fi Migration/backups/` |
