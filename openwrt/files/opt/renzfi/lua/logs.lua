local json = require("json")
local store = require("store")
local util = require("util")

local logs = {}

function logs.append(level, category, message, metadata)
  store.append_jsonl("logs.jsonl", {
    ts = util.iso8601(),
    level = level or "INFO",
    category = category or "system",
    message = message or "",
    metadata = metadata or json.object({}),
  })
end

function logs.list(limit)
  limit = tonumber(limit) or 200
  local rows = store.read_jsonl("logs.jsonl", limit)
  local out = json.array({})
  for i = #rows, 1, -1 do
    out[#out + 1] = rows[i]
  end
  return out
end

function logs.clear()
  util.write_file_atomic(store.path("logs.jsonl"), "")
end

return logs
