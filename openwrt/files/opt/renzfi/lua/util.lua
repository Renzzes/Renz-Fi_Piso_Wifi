-- Shared helpers for Renz-Fi Lua Core (Lua 5.1).

local util = {}

util.VERSION = "0.5.0-openwrt"
util.COOKIE = "renz_session"

local function env_or(name, fallback)
  local v = os.getenv(name)
  if v and #v > 0 then
    return v
  end
  return fallback
end

util.LUA_DIR = env_or("RENZFI_LUA_PATH", "/opt/renzfi/lua")
util.ETC_DIR = env_or("RENZFI_ETC", "/etc/renzfi")
util.DATA_DIR = env_or("RENZFI_DATA", "/opt/renzfi/data")
util.TMP_DIR = env_or("RENZFI_TMP", "/tmp/renzfi")
util.WWW_DIR = env_or("RENZFI_WWW", "/www/renzfi")

function util.mkdir_p(path)
  if not path or path == "" then
    return
  end
  os.execute("mkdir -p " .. util.shell_quote(path) .. " 2>/dev/null")
end

function util.shell_quote(s)
  s = tostring(s or "")
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

function util.trim(s)
  return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

function util.now()
  return os.time()
end

function util.now_ms()
  -- OpenWrt date may not have %N. Prefer /proc/uptime fractional seconds.
  local f = io.open("/proc/uptime", "r")
  if f then
    local line = f:read("*l")
    f:close()
    local sec = tonumber((line or ""):match("^([%d%.]+)"))
    if sec then
      return math.floor(sec * 1000)
    end
  end
  return os.time() * 1000
end

function util.iso8601(ts)
  ts = ts or os.time()
  return os.date("!%Y-%m-%dT%H:%M:%SZ", ts)
end

function util.today()
  return os.date("!%Y-%m-%d")
end

function util.file_exists(path)
  local f = io.open(path, "r")
  if not f then
    return false
  end
  f:close()
  return true
end

function util.read_file(path)
  local f = io.open(path, "rb")
  if not f then
    return nil
  end
  local data = f:read("*a")
  f:close()
  return data
end

function util.write_file_atomic(path, data)
  local dir = path:match("(.+)/[^/]+$")
  if dir then
    util.mkdir_p(dir)
  end
  local tmp = path .. ".tmp." .. tostring(util.now_ms())
  local f, err = io.open(tmp, "wb")
  if not f then
    return false, err
  end
  f:write(data or "")
  f:close()
  local ok = os.rename(tmp, path)
  if not ok then
    os.remove(tmp)
    return false, "rename failed"
  end
  return true
end

function util.hex(n)
  return string.format("%02x", n)
end

function util.random_hex(nbytes)
  nbytes = nbytes or 16
  local f = io.open("/dev/urandom", "rb")
  if f then
    local bytes = f:read(nbytes)
    f:close()
    if bytes and #bytes == nbytes then
      local parts = {}
      for i = 1, nbytes do
        parts[i] = string.format("%02x", bytes:byte(i))
      end
      return table.concat(parts)
    end
  end
  local parts = {}
  math.randomseed(util.now_ms() % 2147483647)
  for i = 1, nbytes do
    parts[i] = string.format("%02x", math.random(0, 255))
  end
  return table.concat(parts)
end

function util.sha256(s)
  local cmd = "printf '%s' " .. util.shell_quote(s) .. " | sha256sum 2>/dev/null"
  local f = io.popen(cmd)
  if not f then
    return nil
  end
  local out = f:read("*l") or ""
  f:close()
  return out:match("^[0-9a-fA-F]+")
end

function util.have_cmd(name)
  local f = io.popen("command -v " .. util.shell_quote(name) .. " 2>/dev/null")
  if not f then
    return false
  end
  local out = util.trim(f:read("*l") or "")
  f:close()
  return #out > 0
end

function util.exec(cmd)
  local f = io.popen(cmd .. " 2>/dev/null")
  if not f then
    return "", false
  end
  local out = f:read("*a") or ""
  local ok = f:close()
  return out, ok and true or false
end

function util.normalize_mac(mac)
  mac = string.upper(util.trim(mac or ""):gsub("-", ":"))
  if not mac:match("^%x%x:%x%x:%x%x:%x%x:%x%x:%x%x$") then
    return nil
  end
  return mac
end

function util.parse_query(qs)
  local out = {}
  qs = qs or ""
  for pair in qs:gmatch("[^&]+") do
    local k, v = pair:match("^([^=]+)=(.*)$")
    if k then
      out[util.url_decode(k)] = util.url_decode(v or "")
    end
  end
  return out
end

function util.url_decode(s)
  s = tostring(s or ""):gsub("+", " ")
  s = s:gsub("%%(%x%x)", function(h)
    return string.char(tonumber(h, 16))
  end)
  return s
end

function util.split_path(uri)
  uri = uri or "/"
  local path, query = uri:match("^([^?]*)%??(.*)$")
  path = path or "/"
  if #path > 1 then
    path = path:gsub("/+$", "")
  end
  return path, query
end

function util.ensure_dirs()
  util.mkdir_p(util.ETC_DIR)
  util.mkdir_p(util.DATA_DIR)
  util.mkdir_p(util.TMP_DIR)
  util.mkdir_p(util.TMP_DIR .. "/sse")
end

function util.sleep(seconds)
  seconds = tonumber(seconds) or 1
  local ok_nixio, nixio = pcall(require, "nixio")
  if ok_nixio and nixio and nixio.nanosleep then
    nixio.nanosleep(seconds)
    return
  end
  os.execute("sleep " .. tostring(seconds))
end

function util.uptime_seconds()
  local f = io.open("/proc/uptime", "r")
  if not f then
    return 0
  end
  local line = f:read("*l")
  f:close()
  return math.floor(tonumber((line or ""):match("^([%d%.]+)")) or 0)
end

return util
