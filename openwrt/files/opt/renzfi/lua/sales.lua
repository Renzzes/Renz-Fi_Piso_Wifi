local json = require("json")
local store = require("store")
local util = require("util")
local eventbus = require("eventbus")

local sales = {}

local function rows()
  return store.read_jsonl("sales.jsonl")
end

local function in_range(iso, since_ymd)
  local day = tostring(iso or ""):sub(1, 10)
  return day >= since_ymd
end

function sales.record(entry)
  entry.id = entry.id or util.random_hex(8)
  entry.recorded_at = entry.recorded_at or util.iso8601()
  entry.amount = tonumber(entry.amount) or 0
  entry.sessions = tonumber(entry.sessions) or 1
  entry.source = entry.source or "coin"
  store.append_jsonl("sales.jsonl", entry)
  eventbus.emit("sale.created", {
    event = "sale.created",
    id = entry.id,
    amount = entry.amount,
    source = entry.source,
    mac = entry.mac,
  })
  eventbus.emit("sales.changed", json.object({}))
  return entry
end

function sales.aggregate_since(ymd)
  local amount = 0
  local sessions = 0
  local all = rows()
  for i = 1, #all do
    local row = all[i]
    if in_range(row.recorded_at, ymd) then
      amount = amount + (tonumber(row.amount) or 0)
      sessions = sessions + (tonumber(row.sessions) or 1)
    end
  end
  return { amount = amount, sessions = sessions }
end

function sales.today()
  return sales.aggregate_since(util.today())
end

function sales.weekly()
  local t = os.time() - (7 * 86400)
  return sales.aggregate_since(os.date("!%Y-%m-%d", t))
end

function sales.monthly()
  return sales.aggregate_since(os.date("!%Y-%m-01"))
end

function sales.history()
  local all = rows()
  local out = json.array({})
  for i = #all, 1, -1 do
    out[#out + 1] = all[i]
  end
  return out
end

function sales.records()
  return sales.history()
end

function sales.export_csv()
  local lines = {
    "Date,Time,Amount,Minutes,Voucher,MAC Address,IP Address,Profile,Status",
  }
  local all = sales.history()
  for i = 1, #all do
    local row = all[i]
    local recorded = tostring(row.recorded_at or "")
    local date_part, time_part = recorded:match("^([^T]+)T([^.Z]+)")
    date_part = date_part or recorded:sub(1, 10)
    time_part = time_part or ""
    local minutes = row.minutes or row.durationMinutes or ""
    local voucher = row.voucherCode or row.code or ""
    lines[#lines + 1] = table.concat({
      date_part,
      time_part,
      tostring(row.amount or 0),
      tostring(minutes),
      tostring(voucher),
      tostring(row.mac or ""),
      tostring(row.ip or ""),
      tostring(row.profile or ""),
      "completed",
    }, ",")
  end
  return table.concat(lines, "\n") .. "\n"
end

function sales.reset()
  util.write_file_atomic(store.path("sales.jsonl"), "")
  eventbus.emit("sales.changed", json.object({}))
end

return sales
