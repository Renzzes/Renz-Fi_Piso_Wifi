-- uhttpd request/response helpers. Safe to load without uhttpd (host tests).

local json = require("json")
local util = require("util")

local http = {}

http._sink = nil

local function send_raw(chunk)
  if http._sink then
    http._sink(chunk)
    return
  end
  if uhttpd and uhttpd.send then
    uhttpd.send(chunk)
  end
end

function http.set_sink(fn)
  http._sink = fn
end

function http.read_body(env)
  env = env or {}
  local len = tonumber(env.CONTENT_LENGTH or env.content_length or 0) or 0
  if len <= 0 then
    return ""
  end
  if len > 65536 then
    len = 65536
  end
  if uhttpd and uhttpd.recv then
    local parts = {}
    local remain = len
    while remain > 0 do
      local n = remain > 4096 and 4096 or remain
      local chunk = uhttpd.recv(n)
      if not chunk or #chunk == 0 then
        break
      end
      parts[#parts + 1] = chunk
      remain = remain - #chunk
    end
    return table.concat(parts)
  end
  return env.body or ""
end

function http.parse_json_body(env)
  local raw = http.read_body(env)
  if not raw or raw == "" then
    return {}
  end
  local data = json.decode(raw)
  if type(data) ~= "table" then
    return {}
  end
  return data
end

function http.cookie(env, name)
  local header = env.HTTP_COOKIE or env.http_cookie or ""
  name = name or util.COOKIE
  for part in header:gmatch("[^;]+") do
    local k, v = part:match("^%s*([^=]+)=(.*)$")
    if k == name then
      return util.trim(v or "")
    end
  end
  return nil
end

function http.path(env)
  local uri = env.REQUEST_URI or env.request_uri or env.PATH_INFO or "/"
  local path = util.split_path(uri)
  if path == "" then
    path = "/"
  end
  -- lua_prefix /api → PATH_INFO may be /health while SCRIPT_NAME is /api
  local script = env.SCRIPT_NAME or ""
  local info = env.PATH_INFO or ""
  if script == "/api" and info ~= "" then
    if info:sub(1, 4) == "/api" then
      path = info
    else
      path = "/api" .. (info:sub(1, 1) == "/" and info or ("/" .. info))
    end
  end
  return path
end

function http.query(env)
  local uri = env.REQUEST_URI or ""
  local _, qs = util.split_path(uri)
  if not qs or qs == "" then
    qs = env.QUERY_STRING or ""
  end
  return util.parse_query(qs)
end

function http.method(env)
  return string.upper(env.REQUEST_METHOD or env.request_method or "GET")
end

local function send_headers(status, headers)
  local reason = ({
    [200] = "OK",
    [202] = "Accepted",
    [400] = "Bad Request",
    [401] = "Unauthorized",
    [403] = "Forbidden",
    [404] = "Not Found",
    [405] = "Method Not Allowed",
    [423] = "Locked",
    [500] = "Internal Server Error",
    [503] = "Service Unavailable",
  })[status] or "OK"
  send_raw(string.format("Status: %d %s\r\n", status, reason))
  headers = headers or {}
  if not headers["Cache-Control"] then
    headers["Cache-Control"] = "no-store"
  end
  if not headers["X-Content-Type-Options"] then
    headers["X-Content-Type-Options"] = "nosniff"
  end
  for k, v in pairs(headers) do
    if type(v) == "table" then
      for i = 1, #v do
        send_raw(k .. ": " .. v[i] .. "\r\n")
      end
    else
      send_raw(k .. ": " .. v .. "\r\n")
    end
  end
  send_raw("\r\n")
end

function http.send(status, headers, body)
  send_headers(status, headers)
  if body then
    send_raw(body)
  end
end

function http.json(status, obj, extra_headers)
  local headers = extra_headers or {}
  headers["Content-Type"] = "application/json; charset=utf-8"
  http.send(status, headers, json.encode(obj))
end

function http.ok(data, message, extra_headers)
  http.json(200, {
    success = true,
    data = data or json.object({}),
    message = message or "OK",
  }, extra_headers)
end

function http.accepted(data, message, extra_headers)
  http.json(202, {
    success = true,
    data = data or json.object({}),
    message = message or "Accepted",
  }, extra_headers)
end

function http.err(status, error_text, code, extra_headers)
  http.json(status, {
    success = false,
    error = error_text or "Error",
    code = code or "INTERNAL_ERROR",
  }, extra_headers)
end

function http.set_cookie_header(token, max_age)
  local parts = {
    util.COOKIE .. "=" .. token,
    "Path=/",
    "HttpOnly",
    "SameSite=Lax",
  }
  if max_age then
    parts[#parts + 1] = "Max-Age=" .. tostring(max_age)
  end
  return table.concat(parts, "; ")
end

function http.clear_cookie_header()
  return util.COOKIE .. "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0"
end

function http.begin_sse()
  send_headers(200, {
    ["Content-Type"] = "text/event-stream",
    ["Cache-Control"] = "no-cache",
    ["Connection"] = "keep-alive",
    ["X-Accel-Buffering"] = "no",
  })
end

function http.sse(event, data)
  send_raw("event: " .. event .. "\n")
  send_raw("data: " .. (data or "{}") .. "\n\n")
end

function http.sse_comment(text)
  send_raw(": " .. (text or "") .. "\n\n")
end

return http
