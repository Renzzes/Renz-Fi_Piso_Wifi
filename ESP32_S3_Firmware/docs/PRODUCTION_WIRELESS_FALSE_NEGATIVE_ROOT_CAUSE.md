# Production Wireless False-Negative Root Cause

## 1. Symptom

Setup Finish fails at `production-network` with:

```
WIRELESS RUNNING=no
reason=interface-not-running
error=Production wireless interface is not running
INSTALLATION READY=no
```

Wizard returns to Step 5 instead of Installation Complete / Ready.

## 2. Setup Finish failure

Stage: `production-network` inside `RouterProvisioningEngine::runFinishPipeline()`  
Function: `MikroTikDriver::activateProductionNetworkForFinish()`

## 3. Exact failing stage

After hotspot-verify PASS (bridge fallback), Finish enters production-network,
queries `/interface/wireless/print`, then declares the AP down.

## 4. Existing ESP32 logs

```
WIRELESS DISABLED AFTER CONFIG=no
WIRELESS ENABLE COMMAND SENT=no
WIRELESS ENABLE VERIFIED=yes
WIRELESS RUNNING=no
[router-api] reply attr limit reached (max=24)
```

## 5. MikroTik forensic evidence

### `/interface wireless print detail`

- `wlan1` present
- No `X` (disabled) flag
- `mode=ap-bridge`
- `ssid="Renz-Fi Piso Wifi"`

### `/interface wireless monitor wlan1 once`

- `status: running-ap`
- `registered-clients: 0` (idle AP — valid)

### `/interface wireless registration-table print`

- empty (expected with zero associated clients)

## 6. Why MikroTik itself is healthy

Hotspot-verify already PASS with:

- `wlan1` in `bridgeGuest`
- `hotspot-renzfi` on bridge
- BRIDGE_FALLBACK accepted

Manual monitor proves runtime AP is up.

## 7. Administrative vs runtime vs unknown

| Concept | Meaning | Source |
|---------|---------|--------|
| Administrative enabled | `disabled!=true` | `/interface/wireless/print` (proplist) |
| Runtime running | AP radio operational | Prefer monitor `status=running-ap` |
| Unknown | Runtime not affirmatively known | Missing print attr / monitor unavailable |

**Never** equate “missing running attribute” with “not running”.

## 8. RouterOS API parsing behavior

Firmware previously did:

```cpp
out.running = attrFromReply(..., "running");
const bool running = state.running == "true";
if (!running && !disabledAfterConfig) FAIL;
```

Empty/`!=true` → false → hard fail.

RouterOS CLI “R” flag is **not** reliably exposed as API `running=true` on
legacy `/interface/wireless/print` for this package. Authoritative runtime is
`/interface/wireless/monitor ... once` → `status=running-ap`.

## 9. max=24 attribute finding

Full wireless print replies often exceed `ReplyRecord::MAX_ATTRS` (24).
Truncation is **safe** (attrs dropped), but it can drop late attributes.

Contribution to this bug:

- **Contributing / amplifying**, not sole cause.
- Even with a complete reply, `running` may be absent from API.
- Fix uses bounded `.proplist` for admin fields and does **not** depend on a
  truncated full print for runtime.

Do **not** globally raise 24 without a separate DMA/heap safety review.

## 10. Exact root cause

**ROOT CAUSE:** False negative validator.

`activateProductionNetworkForFinish` treated missing/`!=true` wireless print
`running` as definitive “not running”, then failed Finish — despite MikroTik
proving `disabled=no` and `status=running-ap`.

## 11. Fix implemented

1. Tri-state runtime: `Running | NotRunning | Unknown`
2. Admin query uses `.proplist=.id,name,disabled,running,ssid,mode,frequency,channel`
3. If print does not affirm `running=true`, perform **one**  
   `/interface/wireless/monitor` with `=.id=...` + `=once=`
4. `status=running-ap` → Running → PASS
5. Explicit non-running monitor status → FAIL
6. Monitor unavailable + admin-enabled AP + SSID/mode OK →  
   `SUCCESS_WITH_WARNING` / `ok-runtime-unknown` (CASE D)
7. Zero clients never consulted

## 12. Why zero registered clients is valid

Monitor returned `registered-clients: 0` while `status: running-ap`.
Idle APs have no clients; Finish must not require associations.

## 13. Validation decision matrix

| Case | Result |
|------|--------|
| A: iface missing | FAIL `WIRELESS_INTERFACE_MISSING` / missing-interface |
| B: disabled=yes, enable fails | FAIL interface-disabled |
| C: disabled=no, status=running-ap | PASS |
| D: disabled=no, runtime UNKNOWN, AP+SSID OK | SUCCESS_WITH_WARNING |
| E: monitor proves not running | FAIL interface-not-running |

## 14. Resource / DMA safety

- At most: 1 admin print + 1 monitor-once (when needed)
- No streaming monitor, no poll loops, no broad file inventory
- Portal-verify MANUAL_EXTERNAL/SKIPPED unchanged
- No global attr-cap increase

## 15. Idempotency

If `disabled=no`, enable is not sent again.

## 16. Files changed

- `src/router/drivers/MikroTikDriver.cpp`
- `src/router/drivers/MikroTikDriver.h`
- `src/ProductionNetworkTrace.cpp`
- `src/ProductionNetworkTrace.h`
- `docs/PRODUCTION_WIRELESS_FALSE_NEGATIVE_ROOT_CAUSE.md`

## 17. Build result

`platformio run -e freenove_esp32_s3_wroom` → **SUCCESS** (29.83s).

## 18. Test matrix

| Test | Code-level | Hardware |
|------|------------|----------|
| 1 Current MikroTik (running-ap, 0 clients) | PASS expected | REQUIRED |
| 2 Zero clients | PASS | REQUIRED |
| 3 Disabled then enable | PASS expected | REQUIRED |
| 4 running attr absent | UNKNOWN→monitor | REQUIRED |
| 5 monitor unavailable + admin OK | SUCCESS_WITH_WARNING | REQUIRED |
| 6 iface missing | FAIL | REQUIRED |
| 7 !trap | controlled error | REQUIRED |
| 8 repeated Finish | idempotent | REQUIRED |
| 9 Skip portal | unchanged | REQUIRED |
| 10 Job poll responsive | async worker unchanged | REQUIRED |

## 19. Hardware validation still required

Flash firmware and confirm serial shows `WIRELESS RUNNING=yes` (or
`SUCCESS_WITH_WARNING`) and installation reaches Ready.

## 20. Before / after

**Before:** missing print `running` → FAIL → Step 5 loop  
**After:** monitor-once `running-ap` → PASS → Ready / Installation Complete
