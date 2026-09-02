local json = require("json")
local store = require("store")
local util = require("util")
local eventbus = require("eventbus")
local sessions = require("sessions")
local logs = require("logs")

local vouchers = {}

local ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

local function load_all()
  local data = store.read("vouchers.json", nil)
  if type(data) ~= "table" then
    return json.array({})
  end
  return data
end

local function save_all(list)
  store.write("vouchers.json", list)
end

local function rand_part(n)
  local out = {}
  for i = 1, n do
    local idx = (util.now_ms() + i * 17 + math.random(0, 31)) % 32
    out[i] = ALPHABET:sub(idx + 1, idx + 1)
  end
  return table.concat(out)
end

local seq = 0

local function new_code()
  seq = seq + 1
  math.randomseed((util.now_ms() + seq * 7919) % 2147483647)
  return rand_part(4) .. "-" .. rand_part(4)
end

function vouchers.list()
  local all = load_all()
  local out = json.array({})
  for i = 1, #all do
    local v = all[i]
    out[#out + 1] = {
      code = v.code,
      amount = v.amount,
      minutes = v.minutes,
      status = v.status,
      expires = v.expires,
    }
  end
  return out
end

function vouchers.find(code)
  code = string.upper(util.trim(code or ""))
  local all = load_all()
  for i = 1, #all do
    if all[i].code == code then
      return all[i], i, all
    end
  end
  return nil
end

function vouchers.generate(opts)
  opts = opts or {}
  local count = tonumber(opts.count) or 3
  if count < 1 then
    count = 1
  end
  if count > 20 then
    count = 20
  end
  local amount = tonumber(opts.amount) or 10
  local minutes = tonumber(opts.minutes) or 60
  local expires = opts.expires or "2026-12-31"
  local all = load_all()
  local created = json.array({})
  for _ = 1, count do
    local code = new_code()
    all[#all + 1] = {
      code = code,
      amount = amount,
      minutes = minutes,
      status = "unused",
      expires = expires,
      created_at = util.iso8601(),
    }
    created[#created + 1] = code
  end
  save_all(all)
  eventbus.emit("vouchers.changed", { count = #created })
  logs.append("INFO", "voucher", "generated " .. tostring(#created) .. " vouchers", json.object({}))
  return created
end

function vouchers.delete(code)
  local _, idx, all = vouchers.find(code)
  if not idx then
    return false
  end
  table.remove(all, idx)
  save_all(all)
  eventbus.emit("vouchers.changed", json.object({}))
  return true
end

function vouchers.redeem(code, mac, ip)
  local row, idx, all = vouchers.find(code)
  if not row then
    return nil, "NOT_FOUND"
  end
  if row.status ~= "unused" then
    return nil, "USED"
  end
  local today = util.today()
  if row.expires and row.expires < today then
    row.status = "expired"
    all[idx] = row
    save_all(all)
    return nil, "EXPIRED"
  end
  row.status = "active"
  row.redeemedAt = util.iso8601()
  row.mac = mac
  all[idx] = row
  save_all(all)
  local session = sessions.activate(mac, {
    ip = ip,
    minutes = row.minutes,
    amount = row.amount,
    sessionType = "voucher",
    voucherCode = row.code,
  })
  eventbus.emit("vouchers.changed", json.object({}))
  return session, row
end

return vouchers
