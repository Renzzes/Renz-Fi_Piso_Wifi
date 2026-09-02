-- HTTP dispatcher for Renz-Fi Lua Core. Same envelope as firmware / simulator.

local json = require("json")
local util = require("util")
local http = require("http")
local auth = require("auth")
local eventbus = require("eventbus")
local status = require("status")
local sales = require("sales")
local sessions = require("sessions")
local vouchers = require("vouchers")
local coin = require("coin")
local portal = require("portal")
local router = require("router")
local settings = require("settings")
local logs = require("logs")
local hotspot = require("hotspot")

local routes = {}

local function match(path, pattern)
  local names = {}
  local re = "^" .. pattern:gsub(":(%w+)", function(name)
    names[#names + 1] = name
    return "([^/]+)"
  end) .. "$"
  local caps = { path:match(re) }
  if not caps[1] and not path:match(re) then
    return nil
  end
  -- path:match returns nil if no match; for patterns without captures it returns the string
  if pattern:find(":%w+") then
    if not path:match(re) then
      return nil
    end
    local params = {}
    for i = 1, #names do
      params[names[i]] = util.url_decode(caps[i] or "")
    end
    return params
  end
  if path == pattern or path:match(re) then
    return {}
  end
  return nil
end

local function require_auth(env)
  return auth.require(env)
end

local HANDLERS = {}

local function add(method, pattern, opts, fn)
  HANDLERS[#HANDLERS + 1] = {
    method = method,
    pattern = pattern,
    auth = opts.auth and true or false,
    fn = fn,
  }
end

add("GET", "/api/health", {}, function(env)
  http.ok(status.health(env))
end)

add("POST", "/api/auth/login", {}, function(env)
  auth.login(env)
end)

add("POST", "/api/auth/logout", {}, function(env)
  auth.logout(env)
end)

add("POST", "/api/auth/change-password", { auth = true }, function(env)
  auth.change_password(env)
end)

add("GET", "/api/events", { auth = true }, function(env)
  eventbus.stream(env)
end)

add("GET", "/api/events/stream", { auth = true }, function(env)
  eventbus.stream(env)
end)

add("GET", "/api/status", { auth = true }, function()
  http.ok(status.snapshot())
end)

add("GET", "/api/system/health", { auth = true }, function()
  http.ok(status.system_health())
end)

add("GET", "/api/system/network", { auth = true }, function()
  http.ok(status.network())
end)

add("GET", "/api/system/wifi", { auth = true }, function()
  http.ok(status.network())
end)

add("GET", "/api/system/coin", { auth = true }, function()
  http.ok(coin.status())
end)

add("GET", "/api/system/rgb", { auth = true }, function()
  http.ok({ enabled = false, mode = "DISABLED" })
end)

add("GET", "/api/rgb/status", { auth = true }, function()
  http.ok({ enabled = false, mode = "DISABLED" })
end)

add("POST", "/api/system/reboot", { auth = true }, function()
  logs.append("WARN", "system", "reboot requested", json.object({}))
  eventbus.emit("system.status", json.object({}))
  http.ok({ ok = true }, "Reboot scheduled")
  os.execute("reboot >/dev/null 2>&1 &")
end)

add("GET", "/api/sales/today", { auth = true }, function()
  http.ok(sales.today())
end)

add("GET", "/api/sales/weekly", { auth = true }, function()
  http.ok(sales.weekly())
end)

add("GET", "/api/sales/monthly", { auth = true }, function()
  http.ok(sales.monthly())
end)

add("GET", "/api/sales/history", { auth = true }, function()
  http.ok(sales.history())
end)

add("GET", "/api/sales/records", { auth = true }, function()
  http.ok(sales.records())
end)

add("GET", "/api/sales/export", { auth = true }, function()
  local csv = sales.export_csv()
  local stamp = util.today()
  http.send(200, {
    ["Content-Type"] = "text/csv; charset=utf-8",
    ["Content-Disposition"] = 'attachment; filename="sales-report-' .. stamp .. '.csv"',
  }, csv)
end)

add("POST", "/api/sales/reset", { auth = true }, function()
  sales.reset()
  http.ok({ ok = true })
end)

add("GET", "/api/vouchers", { auth = true }, function()
  http.ok(vouchers.list())
end)

add("POST", "/api/vouchers", { auth = true }, function(env)
  local body = http.parse_json_body(env)
  local created = vouchers.generate(body)
  http.ok({ created = created })
end)

add("POST", "/api/vouchers/generate", { auth = true }, function(env)
  local body = http.parse_json_body(env)
  local created = vouchers.generate(body)
  http.ok({ created = created })
end)

add("GET", "/api/vouchers/:code", { auth = true }, function(_, params)
  local row = vouchers.find(params.code)
  if not row then
    http.err(404, "Voucher not found", "NOT_FOUND")
    return
  end
  http.ok(row)
end)

add("DELETE", "/api/vouchers/:code", { auth = true }, function(_, params)
  if not vouchers.delete(params.code) then
    http.err(404, "Voucher not found", "NOT_FOUND")
    return
  end
  http.ok({ ok = true })
end)

add("GET", "/api/users", { auth = true }, function()
  http.ok(sessions.list_active())
end)

add("GET", "/api/users/active", { auth = true }, function()
  http.ok(sessions.list_active())
end)

add("POST", "/api/users/pause", { auth = true }, function(env)
  local body = http.parse_json_body(env)
  local row, err = sessions.pause(body.mac)
  if not row then
    http.err(err == "USER_NOT_FOUND" and 404 or 400, "Active user not found", err or "BAD_REQUEST")
    return
  end
  http.ok({ ok = true })
end)

add("POST", "/api/users/resume", { auth = true }, function(env)
  local body = http.parse_json_body(env)
  local row, err = sessions.resume(body.mac)
  if not row then
    http.err(404, "Active user not found", err or "USER_NOT_FOUND")
    return
  end
  http.ok({ ok = true })
end)

add("POST", "/api/users/disconnect", { auth = true }, function(env)
  local body = http.parse_json_body(env)
  local ok, err = sessions.disconnect(body.mac)
  if not ok then
    http.err(400, "mac required", err or "BAD_REQUEST")
    return
  end
  http.ok({ ok = true })
end)

add("GET", "/api/coin/settings", { auth = true }, function()
  http.ok(coin.settings())
end)

add("PUT", "/api/coin/settings", { auth = true }, function(env)
  http.ok(coin.save_settings(http.parse_json_body(env)))
end)

add("POST", "/api/coin/settings", { auth = true }, function(env)
  http.ok(coin.save_settings(http.parse_json_body(env)))
end)

add("GET", "/api/coin/diagnostics", { auth = true }, function()
  http.ok(coin.diagnostics())
end)

add("POST", "/api/coin/test", { auth = true }, function()
  coin.register_pulse()
  logs.append("INFO", "diagnostics", "Coin test pulse requested", json.object({}))
  http.ok({ ok = true }, "Test pulse acknowledged")
end)

add("POST", "/api/coin/reset", { auth = true }, function()
  http.ok(coin.reset_counters())
end)

add("GET", "/api/router/settings", { auth = true }, function()
  http.ok(router.public_settings())
end)

add("GET", "/api/router/cache", { auth = true }, function()
  http.ok(router.cache())
end)

add("POST", "/api/router/cache/sync", { auth = true }, function()
  -- Local ubus/uci refresh only. Never RouterOS.
  local job_id = util.random_hex(4)
  router.observe()
  http.accepted({
    jobId = job_id,
    status = "completed",
    type = "router.cache.sync",
    driverId = "openwrt",
  }, "Local router cache refreshed")
end)

add("GET", "/api/router/profiles", { auth = true }, function()
  http.ok(json.array({
    { name = "default", idleTimeout = 0 },
  }), "Profiles loaded")
end)

add("GET", "/api/storage/status", { auth = true }, function()
  http.ok(settings.storage_status())
end)

add("GET", "/api/settings", { auth = true }, function()
  http.ok(settings.get())
end)

add("POST", "/api/settings", { auth = true }, function(env)
  http.ok(settings.save(http.parse_json_body(env)))
end)

add("PUT", "/api/settings", { auth = true }, function(env)
  http.ok(settings.save(http.parse_json_body(env)))
end)

add("GET", "/api/settings/admin", { auth = true }, function()
  http.ok(settings.admin())
end)

add("GET", "/api/settings/operator", { auth = true }, function()
  http.ok(settings.operator())
end)

add("GET", "/api/settings/portal", { auth = true }, function()
  http.ok(portal.settings())
end)

add("GET", "/api/logs", { auth = true }, function()
  http.ok(logs.list(200))
end)

add("DELETE", "/api/logs", { auth = true }, function()
  logs.clear()
  http.ok({ ok = true })
end)

add("GET", "/api/promos", { auth = true }, function()
  http.ok(portal.rates())
end)

add("GET", "/api/portal/session", {}, function(env)
  portal.session(env)
end)

add("POST", "/api/portal/start-coin-session", {}, function(env)
  portal.start_coin(env)
end)

add("POST", "/api/portal/done-paying", {}, function(env)
  portal.done_paying(env)
end)

add("POST", "/api/portal/redeem-voucher", {}, function(env)
  portal.redeem(env)
end)

add("GET", "/api/portal/branding", {}, function()
  http.ok(portal.branding())
end)

add("GET", "/api/portal/rates", {}, function()
  http.ok(portal.rates())
end)

add("OPTIONS", "/api/", {}, function()
  http.send(204, {
    ["Access-Control-Allow-Origin"] = "*",
    ["Access-Control-Allow-Methods"] = "GET,POST,PUT,DELETE,OPTIONS",
    ["Access-Control-Allow-Headers"] = "Content-Type",
  }, "")
end)

function routes.dispatch(env)
  util.ensure_dirs()
  local method = http.method(env)
  local path = http.path(env)
  if method == "OPTIONS" then
    http.send(204, {
      ["Access-Control-Allow-Origin"] = "*",
      ["Access-Control-Allow-Methods"] = "GET,POST,PUT,DELETE,OPTIONS",
      ["Access-Control-Allow-Headers"] = "Content-Type",
    }, "")
    return
  end
  for i = 1, #HANDLERS do
    local h = HANDLERS[i]
    if h.method == method then
      local params = match(path, h.pattern)
      if params then
        if h.auth and not require_auth(env) then
          return
        end
        local ok, err = pcall(h.fn, env, params)
        if not ok then
          logs.append("ERROR", "http", tostring(err), { path = path })
          http.err(500, "Internal error", "INTERNAL_ERROR")
        end
        return
      end
    end
  end
  http.err(404, "Not found", "NOT_FOUND")
end

-- Used by tick.lua so internet grant does not depend on Admin.
routes.tick = function()
  util.ensure_dirs()
  coin.poll()
  sessions.expire_due()
  local cache = router.cache()
  if cache.stale then
    router.observe()
  end
end

routes.hotspot_engine = hotspot.engine

return routes
