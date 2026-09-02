# Cutover Runbook — Minute-by-Minute

Use during **Phase 3** of [MIGRATION_PLAN.md](./MIGRATION_PLAN.md).  
Have [ROLLBACK_PLAN.md](./ROLLBACK_PLAN.md) open.

**Estimated duration:** 45–90 minutes  
**Best time:** Lowest customer traffic (e.g. early morning)

---

## T-24 hours

- [ ] Confirm backup complete ([BACKUP_CHECKLIST.md](./BACKUP_CHECKLIST.md))
- [ ] hEX + AP bench-tested off-site
- [ ] Portal files built with correct `RENZFI_APPLIANCE_BASE_URL`
- [ ] Owner notified of downtime window
- [ ] hAP lite labeled; rollback cables ready

---

## T-30 min — Prepare site

| Time | Action | ✓ |
|------|--------|---|
| 0:00 | Photograph current cable layout (hAP lite) | ☐ |
| 0:05 | Label: ISP cable, ESP32 cable, LAN cables | ☐ |
| 0:10 | Notify owner: migration starting | ☐ |
| 0:15 | Open Winbox to hAP lite — confirm still online | ☐ |
| 0:20 | Open Admin dashboard — screenshot sales/status | ☐ |

---

## T-0 — Cutover start

| Time | Action | ✓ |
|------|--------|---|
| 0:00 | **Stop new customers** (optional sign: "Wi‑Fi maintenance") | ☐ |
| 0:02 | Power off hAP lite | ☐ |
| 0:05 | Move ISP cable → hEX **ether1** (WAN) | ☐ |
| 0:08 | Connect ESP32 Ethernet → hEX **ether2** | ☐ |
| 0:10 | Connect external AP LAN → hEX **ether3** | ☐ |
| 0:12 | Power on hEX → wait 2 min boot | ☐ |
| 0:14 | Winbox to hEX — verify WAN up, bridge up | ☐ |
| 0:16 | Power on ESP32 | ☐ |
| 0:18 | Power on / verify external AP | ☐ |

---

## T+20 min — Network verification

| Time | Action | ✓ |
|------|--------|---|
| 0:20 | Ping gateway IP from laptop on guest LAN | ☐ |
| 0:22 | Ping ESP32 IP | ☐ |
| 0:24 | Phone joins **AP SSID** — gets DHCP IP | ☐ |
| 0:26 | Phone browser → HotSpot redirect / login page | ☐ |
| 0:28 | If portal broken: check HotSpot on **bridge**, HTML files uploaded | ☐ |

---

## T+30 min — Renz-Fi configuration

| Time | Action | ✓ |
|------|--------|---|
| 0:30 | Admin login (`http://ESP32_IP/dashboard`) | ☐ |
| 0:32 | System Configuration → update router IP/credentials if changed | ☐ |
| 0:35 | **Synchronize Router** — wait for job complete | ☐ |
| 0:38 | Dashboard → MikroTik connectivity **Online** | ☐ |
| 0:40 | Access Points → register AP management IP → **Check** | ☐ |

---

## T+40 min — Production tests

| Time | Action | ✓ |
|------|--------|---|
| 0:42 | Insert test coin → portal credits | ☐ |
| 0:45 | Done Paying → internet works | ☐ |
| 0:48 | Admin → Today sales increased | ☐ |
| 0:50 | Second phone test (optional) | ☐ |

Full checklist: [POST_MIGRATION_VERIFICATION.md](./POST_MIGRATION_VERIFICATION.md)

---

## T+60 min — Go live or rollback

**Go live if:** B1–C5 in verification pass.

| Decision | Action |
|----------|--------|
| **Success** | Remove maintenance notice; monitor 24 h; complete SITE_WORKSHEET hEX section |
| **Failure** | Execute [ROLLBACK_PLAN.md](./ROLLBACK_PLAN.md) immediately |

---

## Post-cutover notes

```
Issues encountered:
_____________________________________________________________

Resolution:
_____________________________________________________________

Actual cutover duration: _______ minutes
Signed: _________________________ Date: __________
```
