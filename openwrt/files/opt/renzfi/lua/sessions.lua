local json = require("json")
local store = require("store")
local util = require("util")
local eventbus = require("eventbus")
local hotspot = require("hotspot")
local sales = require("sales")
local logs = require("logs")

local sessions = {}

local function load_all()
  local data = store.read("sessions.json", nil)
  if type(data) ~= "table" then
    return json.object({})
  end
  return data
end

local function save_all(data)
  store.write("sessions.json", data)
end

local function remaining_seconds(row, now)
  now = now or util.now()
  if row.paused then
    return tonumber(row.secondsLeft) or 0
  end
  local exp = tonumber(row.expiresAt) or 0
  if exp <= 0 then
    return tonumber(row.secondsLeft) or 0
  end
  local rem = exp - now
  if rem < 0 then
    return 0
  end
  return rem
end

function sessions.get(mac)
  mac = util.normalize_mac(mac)
  if not mac then
    return nil
  end
  local all = load_all()
  return all[mac], mac, all
end

function sessions.public(row, mac, ip)
  if not row then
    return {
      mac = mac,
      ip = ip or "",
      state = "idle",
      active = false,
      paused = false,
      secondsLeft = 0,
      grantedSeconds = 0,
      credits = 0,
      amount = 0,
      sessionType = "none",
    }
  end
  local rem = remaining_seconds(row)
  return {
    mac = row.mac or mac,
    ip = ip or row.ip or "",
    state = row.state or "idle",
    active = row.state == "active",
    paused = row.paused and true or false,
    secondsLeft = rem,
    grantedSeconds = tonumber(row.grantedSeconds) or 0,
    credits = tonumber(row.credits) or 0,
    amount = tonumber(row.amount) or 0,
    sessionType = row.sessionType or "coin",
    voucherCode = row.voucherCode,
  }
end

function sessions.start_coin_window(mac, ip)
  mac = util.normalize_mac(mac)
  if not mac then
    return nil, "MISSING_MAC"
  end
  local all = load_all()
  local row = all[mac]
  if row and row.state == "active" and remaining_seconds(row) > 0 then
    return row
  end
  row = {
    mac = mac,
    ip = ip or "",
    state = "waiting_coin",
    sessionType = "coin",
    paused = false,
    credits = 0,
    amount = 0,
    secondsLeft = 0,
    grantedSeconds = 0,
    expiresAt = 0,
    openedAt = util.now(),
  }
  all[mac] = row
  save_all(all)
  eventbus.emit("sessions.changed", json.object({}))
  return row
end

function sessions.add_credit(mac, amount, minutes)
  local row, key, all = sessions.get(mac)
  if not key then
    return nil, "MISSING_MAC"
  end
  if not row or row.state ~= "waiting_coin" then
    row = {
      mac = key,
      ip = row and row.ip or "",
      state = "waiting_coin",
      sessionType = "coin",
      paused = false,
      credits = 0,
      amount = 0,
      secondsLeft = 0,
      grantedSeconds = 0,
      expiresAt = 0,
    }
  end
  row.amount = (tonumber(row.amount) or 0) + (tonumber(amount) or 0)
  row.credits = (tonumber(row.credits) or 0) + 1
  row.pendingMinutes = (tonumber(row.pendingMinutes) or 0) + (tonumber(minutes) or 0)
  all[key] = row
  save_all(all)
  eventbus.emit("coin.diagnostics", json.object({}))
  return row
end

function sessions.activate(mac, opts)
  opts = opts or {}
  local row, key, all = sessions.get(mac)
  if not key then
    return nil, "MISSING_MAC"
  end
  row = row or {
    mac = key,
    ip = opts.ip or "",
    sessionType = opts.sessionType or "coin",
  }
  if row.state == "active" and remaining_seconds(row) > 0 then
    return row, "already_active"
  end
  local minutes = tonumber(opts.minutes) or tonumber(row.pendingMinutes) or 0
  if minutes <= 0 then
    return nil, "NO_CREDITS"
  end
  local seconds = minutes * 60
  local now = util.now()
  row.state = "active"
  row.paused = false
  row.ip = opts.ip or row.ip or ""
  row.sessionType = opts.sessionType or row.sessionType or "coin"
  row.grantedSeconds = seconds
  row.secondsLeft = seconds
  row.expiresAt = now + seconds
  row.pendingMinutes = 0
  row.activatedAt = now
  if opts.voucherCode then
    row.voucherCode = opts.voucherCode
  end
  all[key] = row
  save_all(all)

  sales.record({
    amount = tonumber(opts.amount) or tonumber(row.amount) or 0,
    minutes = minutes,
    sessions = 1,
    source = row.sessionType,
    mac = key,
    ip = row.ip,
    voucherCode = row.voucherCode,
  })
  hotspot.authorize(key, seconds)
  logs.append("INFO", "session", "session activated " .. key, {
    minutes = minutes,
    type = row.sessionType,
  })
  eventbus.emit("sessions.changed", json.object({}))
  eventbus.emit("users.active", json.object({}))
  return row
end

function sessions.pause(mac)
  local row, key, all = sessions.get(mac)
  if not row then
    return nil, "USER_NOT_FOUND"
  end
  if row.paused then
    return row
  end
  row.secondsLeft = remaining_seconds(row)
  row.paused = true
  row.state = "paused"
  row.expiresAt = 0
  all[key] = row
  save_all(all)
  hotspot.deauthorize(key)
  eventbus.emit("sessions.changed", json.object({}))
  return row
end

function sessions.resume(mac)
  local row, key, all = sessions.get(mac)
  if not row then
    return nil, "USER_NOT_FOUND"
  end
  local rem = tonumber(row.secondsLeft) or 0
  if rem <= 0 then
    return nil, "EXPIRED"
  end
  row.paused = false
  row.state = "active"
  row.expiresAt = util.now() + rem
  all[key] = row
  save_all(all)
  hotspot.authorize(key, rem)
  eventbus.emit("sessions.changed", json.object({}))
  return row
end

function sessions.disconnect(mac)
  local row, key, all = sessions.get(mac)
  if not key then
    return nil, "MISSING_MAC"
  end
  if row then
    all[key] = nil
    save_all(all)
  end
  hotspot.deauthorize(key)
  eventbus.emit("sessions.changed", json.object({}))
  eventbus.emit("users.active", json.object({}))
  return true
end

function sessions.list_active()
  local all = load_all()
  local now = util.now()
  local out = json.array({})
  for mac, row in pairs(all) do
    local rem = remaining_seconds(row, now)
    if row.state == "active" or row.state == "paused" then
      out[#out + 1] = {
        mac = mac,
        ip = row.ip or "",
        sessionType = row.sessionType or "coin",
        remainingMinutes = math.floor(rem / 60),
        credits = tonumber(row.credits) or 0,
        paused = row.paused and true or false,
        active = row.state == "active",
        state = row.state,
        source = row.sessionType == "voucher" and "voucher" or "portal",
      }
    end
  end
  return out
end

function sessions.stats()
  local list = sessions.list_active()
  local count = 0
  local paused = 0
  for i = 1, #list do
    count = count + 1
    if list[i].paused then
      paused = paused + 1
    end
  end
  return { count = count, paused = paused, idle = 0 }
end

function sessions.expire_due()
  local all = load_all()
  local now = util.now()
  local changed = false
  for mac, row in pairs(all) do
    if row.state == "active" and not row.paused then
      if remaining_seconds(row, now) <= 0 then
        all[mac] = nil
        hotspot.deauthorize(mac)
        logs.append("INFO", "session", "session expired " .. mac, json.object({}))
        changed = true
      else
        hotspot.authorize(mac, remaining_seconds(row, now))
      end
    end
  end
  if changed then
    save_all(all)
    eventbus.emit("sessions.changed", json.object({}))
    eventbus.emit("users.active", json.object({}))
  end
end

return sessions
