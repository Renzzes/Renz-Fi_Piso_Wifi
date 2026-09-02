-- Atomic JSON document + JSONL helpers.

local json = require("json")
local util = require("util")

local store = {}

function store.path(name)
  if name:sub(1, 1) == "/" then
    return name
  end
  if name:match("^etc/") then
    return util.ETC_DIR .. "/" .. name:sub(5)
  end
  if name:match("^tmp/") then
    return util.TMP_DIR .. "/" .. name:sub(5)
  end
  return util.DATA_DIR .. "/" .. name
end

function store.read(name, default)
  util.ensure_dirs()
  local raw = util.read_file(store.path(name))
  if not raw or raw == "" then
    return default
  end
  local data, err = json.decode(raw)
  if data == nil then
    return default, err
  end
  if data == json.null then
    return default
  end
  return data
end

function store.write(name, data)
  util.ensure_dirs()
  local encoded = json.encode(data)
  return util.write_file_atomic(store.path(name), encoded)
end

function store.update(name, default, fn)
  local data = store.read(name, default) or default
  local next_data = fn(data)
  if next_data == nil then
    next_data = data
  end
  store.write(name, next_data)
  return next_data
end

function store.append_jsonl(name, row)
  util.ensure_dirs()
  local path = store.path(name)
  local dir = path:match("(.+)/[^/]+$")
  if dir then
    util.mkdir_p(dir)
  end
  local f, err = io.open(path, "ab")
  if not f then
    return false, err
  end
  f:write(json.encode(row))
  f:write("\n")
  f:close()
  return true
end

function store.read_jsonl(name, limit)
  local raw = util.read_file(store.path(name))
  local rows = json.array({})
  if not raw or raw == "" then
    return rows
  end
  for line in raw:gmatch("[^\n]+") do
    local row = json.decode(line)
    if row and row ~= json.null then
      rows[#rows + 1] = row
    end
  end
  if limit and #rows > limit then
    local sliced = json.array({})
    local start = #rows - limit + 1
    for i = start, #rows do
      sliced[#sliced + 1] = rows[i]
    end
    return sliced
  end
  return rows
end

return store
