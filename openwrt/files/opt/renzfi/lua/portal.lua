local json = require("json")
local store = require("store")
local util = require("util")
local http = require("http")
local sessions = require("sessions")
local vouchers = require("vouchers")
local eventbus = require("eventbus")

local portal = {}

local DEFAULT_RATES = {
  { coin = 5, minutes = 15, name = "15 minutes" },
  { coin = 10, minutes = 60, name = "1 hour" },
  { coin = 20, minutes = 180, name = "3 hours" },
}

local function load_portal()
  local data = store.read("etc/portal.json", nil)
  if type(data) ~= "table" then
    data = {
      revision = 1,
      hasBanner = false,
      hasMusic = false,
      rates = DEFAULT_RATES,
    }
    store.write("etc/portal.json", data)
  end
  return data
end

function portal.branding()
  local p = load_portal()
  return {
    revision = p.revision or 1,
    hasCustomBanner = p.hasBanner and true or false,
    hasCustomMusic = p.hasMusic and true or false,
    bannerUrl = p.hasBanner and "/portal/Default-Banner.png" or json.null,
    musicUrl = json.null,
  }
end

function portal.rates()
  local p = load_portal()
  return p.rates or DEFAULT_RATES
end

function portal.settings()
  local p = load_portal()
  return {
    revision = p.revision or 1,
    has_banner = p.hasBanner and true or false,
    has_music = p.hasMusic and true or false,
    bannerConfigured = p.hasBanner and true or false,
    musicConfigured = p.hasMusic and true or false,
    rates = p.rates or DEFAULT_RATES,
  }
end

function portal.session(env)
  local q = http.query(env)
  local body = {}
  if http.method(env) ~= "GET" then
    body = http.parse_json_body(env)
  end
  local mac = util.normalize_mac(q.mac or body.mac)
  local ip = q.ip or body.ip or env.REMOTE_ADDR or ""
  if not mac then
    http.err(400, "mac parameter required", "MISSING_MAC")
    return
  end
  local row = sessions.get(mac)
  http.ok(sessions.public(row, mac, ip))
end

function portal.start_coin(env)
  local body = http.parse_json_body(env)
  local mac = util.normalize_mac(body.mac)
  local ip = body.ip or env.REMOTE_ADDR or ""
  if not mac then
    http.err(400, "mac field required", "MISSING_MAC")
    return
  end
  local coin = require("coin")
  if not coin.settings().enabled then
    http.err(503, "Coin slot hardware is disabled", "COIN_DISABLED")
    return
  end
  local row = sessions.start_coin_window(mac, ip)
  http.ok(sessions.public(row, mac, ip), "Coin window opened")
end

function portal.done_paying(env)
  local body = http.parse_json_body(env)
  local mac = util.normalize_mac(body.mac)
  local ip = body.ip or env.REMOTE_ADDR or ""
  if not mac then
    http.err(400, "mac field required", "MISSING_MAC")
    return
  end
  local row = sessions.get(mac)
  if not row then
    http.err(400, "No credits to activate", "NO_CREDITS")
    return
  end
  if row.state == "active" then
    http.ok(sessions.public(row, mac, ip), "Already active")
    return
  end
  local minutes = tonumber(row.pendingMinutes) or 0
  if minutes <= 0 then
    http.err(400, "No credits to activate", "NO_CREDITS")
    return
  end
  local activated, err = sessions.activate(mac, {
    ip = ip,
    minutes = minutes,
    amount = row.amount,
    sessionType = "coin",
  })
  if not activated then
    http.err(400, "Failed to activate session", err or "SESSION_ERROR")
    return
  end
  http.ok(sessions.public(activated, mac, ip), "Internet granted")
end

function portal.redeem(env)
  local body = http.parse_json_body(env)
  local mac = util.normalize_mac(body.mac)
  local ip = body.ip or env.REMOTE_ADDR or ""
  local code = string.upper(util.trim(body.code or body.voucher or ""))
  if not mac then
    http.err(400, "mac field required", "MISSING_MAC")
    return
  end
  if code == "" then
    http.err(400, "voucher code required", "BAD_REQUEST")
    return
  end
  local session, err = vouchers.redeem(code, mac, ip)
  if not session then
    local map = {
      NOT_FOUND = { 404, "Voucher not found", "NOT_FOUND" },
      USED = { 409, "Voucher already used", "USED" },
      EXPIRED = { 410, "Voucher expired", "EXPIRED" },
    }
    local m = map[err] or { 400, "Unable to redeem voucher", "SESSION_ERROR" }
    http.err(m[1], m[2], m[3])
    return
  end
  http.ok(sessions.public(session, mac, ip), "Voucher redeemed")
end

function portal.save_settings(body)
  local p = load_portal()
  if body.rates then
    p.rates = body.rates
  end
  p.revision = (tonumber(p.revision) or 1) + 1
  store.write("etc/portal.json", p)
  eventbus.emit("portal.changed", json.object({}))
  return portal.settings()
end

return portal
