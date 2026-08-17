# Router Test/Save POST Stability Fix

## Root cause

After `POST /api/setup/router/test` returned `API_LOGIN_FAILED` and sent the HTTP response, `WebRequestDiagnostics::RequestTimer` ran its destructor and dereferenced `AsyncWebServerRequest*` **after** `req->send()`. That post-send access caused Core 1 `LoadProhibited` (`EXCVADDR=0x00000000`).

Secondary risk: router test/save used `req->_tempObject` + `bodyCollect`, coupling handler lifetime to request-owned memory freed in `~AsyncWebServerRequest()`.

## Fix

1. **`RequestTimer::finish()`** — log the `[http] end` line while the request is still valid, then set `_req = nullptr` so the destructor is a no-op after send.
2. **`SetupRouterOwnedBodyStore`** — body chunks copied into an owned heap buffer keyed by request pointer; handler takes ownership via `take()`; never uses `req->_tempObject`.
3. **`handleRouterConnectionPost()`** — shared safe path for test/save:
   - plane/owner gates before `RequestTimer` construction
   - parse JSON into owned `RouterInput` values
   - run validation synchronously
   - serialize response `String` before send
   - `timer.finish()` then `serveJson()` then staged logs; no post-send work

## Staged logs

```
[router-test|router-save] body parsed
[router-test|router-save] credentials resolved
[router-test|router-save] validation started
[router-test|router-save] validation finished code=<...>
[router-test|router-save] response sent
[router-test|router-save] handler complete
```

## Regression guard

```bash
python ESP32_S3_Firmware/tools/router-test-save-stability-check.py
```

Validates source patterns and runs 20 heap cycles per logical test case (host-side sanity only; no device reboot claim).

## Files changed

- `src/web/WebRequestDiagnostics.h/.cpp`
- `src/web/SetupServer.cpp`
- `src/SetupRouterConnectionManager.cpp` (explicit `result.success = true` on success)
- `tools/router-test-save-stability-check.py`
- `docs/ROUTER_TEST_SAVE_STABILITY_REPORT.md`
