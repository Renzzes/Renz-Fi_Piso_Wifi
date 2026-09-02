-- Firmware-compatible EventBus: no-op when no SSE clients are connected.

local json = require("json")
local util = require("util")
local http = require("http")
local store = require("store")

local eventbus = {}

local CLIENT_DIR = nil
local QUEUE_PATH = nil

local function paths()
  CLIENT_DIR = CLIENT_DIR or (util.TMP_DIR .. "/sse")
  QUEUE_PATH = QUEUE_PATH or (util.TMP_DIR .. "/sse-queue.jsonl")
  util.mkdir_p(CLIENT_DIR)
end

local function list_clients()
  paths()
  local ids = {}
  local f = io.popen("ls -1 " .. util.shell_quote(CLIENT_DIR) .. " 2>/dev/null")
  if not f then
    return ids
  end
  for name in f:lines() do
    if name ~= "" then
      ids[#ids + 1] = name
    end
  end
  f:close()
  return ids
end

function eventbus.client_count()
  return #list_clients()
end

function eventbus.emit(name, payload)
  paths()
  if eventbus.client_count() == 0 then
    return
  end
  local row = {
    ts = util.iso8601(),
    event = name,
    data = payload or json.object({}),
  }
  local f = io.open(QUEUE_PATH, "ab")
  if not f then
    return
  end
  f:write(json.encode(row))
  f:write("\n")
  f:close()
  local raw = util.read_file(QUEUE_PATH)
  if raw and #raw > 262144 then
    -- Keep the last ~64KiB so flash/tmpfs does not grow unbounded.
    local keep = raw:sub(-65536)
    local cut = keep:find("\n")
    if cut then
      keep = keep:sub(cut + 1)
    end
    util.write_file_atomic(QUEUE_PATH, keep)
  end
end

function eventbus.register()
  paths()
  local id = util.random_hex(8)
  util.write_file_atomic(CLIENT_DIR .. "/" .. id, tostring(util.now()))
  return id
end

function eventbus.unregister(id)
  paths()
  if id then
    os.remove(CLIENT_DIR .. "/" .. id)
  end
end

local function queue_size()
  paths()
  local f = io.open(QUEUE_PATH, "rb")
  if not f then
    return 0
  end
  local size = f:seek("end")
  f:close()
  return size or 0
end

local function read_from(offset)
  paths()
  local f = io.open(QUEUE_PATH, "rb")
  if not f then
    return "", offset
  end
  f:seek("set", offset)
  local chunk = f:read("*a") or ""
  local new_off = offset + #chunk
  f:close()
  return chunk, new_off
end

function eventbus.stream(_env)
  local id = eventbus.register()
  http.begin_sse()
  http.sse("system.status", '{"ok":true}')
  http.sse("ping", '"connected"')
  local offset = queue_size()
  local started = util.now()
  -- Hold the worker ~30s then let the browser reconnect (uhttpd worker budget).
  while (util.now() - started) < 30 do
    local chunk
    chunk, offset = read_from(offset)
    if chunk ~= "" then
      for line in chunk:gmatch("[^\n]+") do
        local row = json.decode(line)
        if type(row) == "table" and row.event then
          local data = row.data
          if type(data) == "table" then
            data = json.encode(data)
          end
          http.sse(row.event, data or "{}")
        end
      end
    else
      http.sse_comment("heartbeat")
    end
    util.sleep(1)
  end
  eventbus.unregister(id)
end

return eventbus
