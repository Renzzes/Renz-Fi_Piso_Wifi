-- Optional GPIO coin slot. Tick daemon calls poll(). Core never needs Admin.

local json = require("json")
local store = require("store")
local util = require("util")
local eventbus = require("eventbus")
local sessions = require("sessions")
local logs = require("logs")

local coin = {}

local function defaults()
  return {
    enabled = false,
    gpio_pin = 18,
    pulses_per_coin = 1,
    active_low = true,
    amount_per_coin = 5,
    minutes_per_coin = 15,
    totalPulseCount = 0,
    totalCoinCount = 0,
    uptimePulseCount = 0,
    uptimeCoinCount = 0,
    lastPulseTimestamp = json.null,
    lastCoinTimestamp = json.null,
    hardwareState = "DISABLED",
    pendingPulses = 0,
    lastLevel = nil,
  }
end

local function load()
  local data = store.read("etc/coin.json", nil)
  if type(data) ~= "table" then
    data = defaults()
    store.write("etc/coin.json", data)
  end
  return data
end

local function save(data)
  store.write("etc/coin.json", data)
end

local function gpio_path(pin)
  return "/sys/class/gpio/gpio" .. tostring(pin) .. "/value"
end

local function gpio_export(pin)
  if util.file_exists(gpio_path(pin)) then
    return true
  end
  local f = io.open("/sys/class/gpio/export", "w")
  if not f then
    return false
  end
  f:write(tostring(pin))
  f:close()
  local dir = io.open("/sys/class/gpio/gpio" .. tostring(pin) .. "/direction", "w")
  if dir then
    dir:write("in")
    dir:close()
  end
  return util.file_exists(gpio_path(pin))
end

local function read_level(pin)
  local raw = util.read_file(gpio_path(pin))
  if not raw then
    return nil
  end
  return tonumber(util.trim(raw))
end

function coin.settings()
  return load()
end

function coin.save_settings(body)
  local data = load()
  if body.enabled ~= nil then
    data.enabled = body.enabled and true or false
  end
  if body.gpio_pin ~= nil then
    data.gpio_pin = tonumber(body.gpio_pin) or data.gpio_pin
  end
  if body.pulses_per_coin ~= nil then
    data.pulses_per_coin = tonumber(body.pulses_per_coin) or data.pulses_per_coin
  end
  if body.amount_per_coin ~= nil then
    data.amount_per_coin = tonumber(body.amount_per_coin) or data.amount_per_coin
  end
  if body.minutes_per_coin ~= nil then
    data.minutes_per_coin = tonumber(body.minutes_per_coin) or data.minutes_per_coin
  end
  if body.active_low ~= nil then
    data.active_low = body.active_low and true or false
  end
  if data.enabled then
    data.hardwareState = "WAITING_FOR_ACTIVITY"
  else
    data.hardwareState = "DISABLED"
  end
  save(data)
  eventbus.emit("coin.diagnostics", json.object({}))
  return data
end

function coin.status()
  local data = load()
  return {
    enabled = data.enabled and true or false,
    ok = data.enabled and true or false,
    state = data.hardwareState or "DISABLED",
    hardwareState = data.hardwareState or "DISABLED",
    totalPulseCount = tonumber(data.totalPulseCount) or 0,
    totalCoinCount = tonumber(data.totalCoinCount) or 0,
    uptimePulseCount = tonumber(data.uptimePulseCount) or 0,
    uptimeCoinCount = tonumber(data.uptimeCoinCount) or 0,
    lastPulseTimestamp = data.lastPulseTimestamp,
    lastCoinTimestamp = data.lastCoinTimestamp,
    gpio_pin = data.gpio_pin,
  }
end

local function accept_coin(data)
  data.totalCoinCount = (tonumber(data.totalCoinCount) or 0) + 1
  data.uptimeCoinCount = (tonumber(data.uptimeCoinCount) or 0) + 1
  data.lastCoinTimestamp = util.iso8601()
  data.pendingPulses = 0
  data.hardwareState = "COIN_ACCEPTED"
  save(data)
  logs.append("INFO", "coin", "Coin accepted: ₱" .. tostring(data.amount_per_coin or 5), json.object({}))
  local all = store.read("sessions.json", json.object({}))
  for mac, row in pairs(all) do
    if row.state == "waiting_coin" then
      sessions.add_credit(mac, data.amount_per_coin or 5, data.minutes_per_coin or 15)
      break
    end
  end
  eventbus.emit("coin.diagnostics", json.object({}))
end

function coin.register_pulse()
  local data = load()
  if not data.enabled then
    return data
  end
  data.totalPulseCount = (tonumber(data.totalPulseCount) or 0) + 1
  data.uptimePulseCount = (tonumber(data.uptimePulseCount) or 0) + 1
  data.lastPulseTimestamp = util.iso8601()
  data.pendingPulses = (tonumber(data.pendingPulses) or 0) + 1
  data.hardwareState = "PULSE"
  local need = tonumber(data.pulses_per_coin) or 1
  if data.pendingPulses >= need then
    accept_coin(data)
  else
    save(data)
    eventbus.emit("coin.diagnostics", json.object({}))
  end
  return data
end

function coin.poll()
  local data = load()
  if not data.enabled then
    return
  end
  local pin = tonumber(data.gpio_pin)
  if not pin then
    return
  end
  if not gpio_export(pin) then
    return
  end
  local level = read_level(pin)
  if level == nil then
    return
  end
  local active = data.active_low and (level == 0) or (level == 1)
  if data.lastLevel == nil then
    data.lastLevel = active and 0 or 1
    save(data)
    return
  end
  if active and data.lastLevel ~= 0 then
    coin.register_pulse()
    data = load()
  end
  data.lastLevel = active and 0 or 1
  save(data)
end

function coin.reset_counters()
  local data = load()
  data.uptimePulseCount = 0
  data.uptimeCoinCount = 0
  data.pendingPulses = 0
  data.hardwareState = data.enabled and "WAITING_FOR_ACTIVITY" or "DISABLED"
  save(data)
  eventbus.emit("coin.diagnostics", json.object({}))
  return data
end

function coin.diagnostics()
  return {
    stats = coin.status(),
    logs = logs.list(20),
  }
end

return coin
