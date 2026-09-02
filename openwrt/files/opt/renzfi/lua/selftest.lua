-- Host smoke test for Renz-Fi Lua Core (no uhttpd required).
-- Usage: RENZFI_* dirs set; lua openwrt/files/opt/renzfi/lua/selftest.lua

local lua_dir = os.getenv("RENZFI_LUA_PATH") or "./openwrt/files/opt/renzfi/lua"
package.path = lua_dir .. "/?.lua;" .. package.path

local json = require("json")
local http = require("http")
local routes = require("routes")
local util = require("util")
local vouchers = require("vouchers")
local sessions = require("sessions")
local sales = require("sales")
local coin = require("coin")

local failed = 0
local function assert_eq(a, b, msg)
  if a ~= b then
    failed = failed + 1
    io.stderr:write("FAIL: " .. msg .. " got=" .. tostring(a) .. " expected=" .. tostring(b) .. "\n")
  else
    print("ok  " .. msg)
  end
end

-- JSON roundtrip
local encoded = json.encode({ ok = true, n = 3, list = json.array({ 1, 2 }) })
local decoded = json.decode(encoded)
assert_eq(decoded.ok, true, "json bool")
assert_eq(decoded.n, 3, "json number")
assert_eq(decoded.list[2], 2, "json array")

local buf = {}
http.set_sink(function(chunk)
  buf[#buf + 1] = chunk
end)

local function dispatch(method, uri, body, cookie)
  buf = {}
  local env = {
    REQUEST_METHOD = method,
    REQUEST_URI = uri,
    CONTENT_LENGTH = body and #body or 0,
    body = body,
    HTTP_COOKIE = cookie,
    REMOTE_ADDR = "127.0.0.1",
  }
  routes.dispatch(env)
  return table.concat(buf)
end

local function envelope(raw)
  local body = raw:match("\r\n\r\n(.*)$") or raw
  return json.decode(body), raw
end

local health_raw = dispatch("GET", "/api/health")
local health = envelope(health_raw)
assert_eq(health.success, true, "GET /api/health success")
assert_eq(health.data.edition, "openwrt", "health edition")
assert_eq(health.data.ok, true, "health ok")
assert_eq(health.data.session.authenticated, false, "health unauthenticated")

local login_raw = dispatch(
  "POST",
  "/api/auth/login",
  '{"password":"admin"}'
)
local login = envelope(login_raw)
assert_eq(login.success, true, "login success")
assert_eq(login.data.authenticated, true, "login authenticated")
assert_eq(login.data.mustChangePassword, true, "must change default password")

local cookie = login_raw:match("Set%-Cookie: " .. util.COOKIE .. "=([^;]+)")
assert_eq(cookie ~= nil, true, "session cookie set")

local cookie_header = util.COOKIE .. "=" .. (cookie or "")
local status_raw = dispatch("GET", "/api/status", nil, cookie_header)
local st = envelope(status_raw)
assert_eq(st.success, true, "GET /api/status")
assert_eq(st.data.edition, "openwrt", "status edition")
assert_eq(st.data.mikrotik.driverId, "openwrt", "status driver is openwrt not mikrotik creds")

local created = vouchers.generate({ count = 3, amount = 10, minutes = 60 })
assert_eq(#created, 3, "generate 3 vouchers")

local mac = "AA:BB:CC:DD:EE:FF"
local sess, verr = vouchers.redeem(created[1], mac, "10.0.0.50")
assert_eq(sess ~= nil, true, "redeem voucher")
assert_eq(verr.code, created[1], "redeemed code")
assert_eq((sales.today().sessions or 0) >= 1, true, "sale persisted before grant")

local active = sessions.list_active()
assert_eq(#active >= 1, true, "active session listed")

coin.save_settings({ enabled = true, amount_per_coin = 5, minutes_per_coin = 15 })
sessions.start_coin_window("11:22:33:44:55:66", "10.0.0.51")
coin.register_pulse()
local waiting = sessions.get("11:22:33:44:55:66")
assert_eq(waiting ~= nil, true, "coin window exists")
assert_eq((waiting.pendingMinutes or 0) >= 15, true, "coin credit applied without Admin")

if failed > 0 then
  os.exit(1)
end
print("all tests passed")
