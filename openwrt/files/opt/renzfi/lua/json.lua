-- Minimal JSON encode/decode for Lua 5.1 (OpenWrt).
-- Prefers cjson / luci.jsonc when present.

local json = {}

json.null = {}
setmetatable(json.null, {
  __tostring = function()
    return "null"
  end,
})

local ok_cjson, cjson = pcall(require, "cjson")
if not ok_cjson then
  ok_cjson, cjson = pcall(require, "cjson.safe")
end
local ok_luci, luci_jsonc = pcall(require, "luci.jsonc")

local function is_array(t)
  local mt = getmetatable(t)
  if mt and mt.__jsontype == "array" then
    return true
  end
  if mt and mt.__jsontype == "object" then
    return false
  end
  local n = 0
  local count = 0
  for k, _ in pairs(t) do
    if type(k) ~= "number" or k < 1 or k % 1 ~= 0 then
      return false
    end
    count = count + 1
    if k > n then
      n = k
    end
  end
  if count == 0 then
    return false
  end
  return n == count
end

function json.array(t)
  return setmetatable(t or {}, { __jsontype = "array" })
end

function json.object(t)
  return setmetatable(t or {}, { __jsontype = "object" })
end

local escape_map = {
  ["\\"] = "\\\\",
  ["\""] = "\\\"",
  ["\b"] = "\\b",
  ["\f"] = "\\f",
  ["\n"] = "\\n",
  ["\r"] = "\\r",
  ["\t"] = "\\t",
}

local function escape_str(s)
  return s:gsub('[%z\1-\31\\"]', function(c)
    return escape_map[c] or string.format("\\u%04x", c:byte())
  end)
end

local encode

encode = function(v)
  local t = type(v)
  if v == json.null or v == nil then
    return "null"
  elseif t == "boolean" then
    return v and "true" or "false"
  elseif t == "number" then
    if v ~= v or v == math.huge or v == -math.huge then
      return "null"
    end
    return tostring(v)
  elseif t == "string" then
    return '"' .. escape_str(v) .. '"'
  elseif t == "table" then
    if is_array(v) then
      local parts = {}
      for i = 1, #v do
        parts[i] = encode(v[i])
      end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    local parts = {}
    for k, val in pairs(v) do
      local key = type(k) == "string" and k or tostring(k)
      parts[#parts + 1] = '"' .. escape_str(key) .. '":' .. encode(val)
    end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  return "null"
end

local function skip_ws(s, i)
  local _, j = s:find("^[ \t\n\r]+", i)
  return j and (j + 1) or i
end

local parse_value

local function parse_string(s, i)
  i = i + 1
  local out = {}
  while i <= #s do
    local c = s:sub(i, i)
    if c == '"' then
      return table.concat(out), i + 1
    elseif c == "\\" then
      local n = s:sub(i + 1, i + 1)
      local map = {
        ['"'] = '"',
        ["\\"] = "\\",
        ["/"] = "/",
        b = "\b",
        f = "\f",
        n = "\n",
        r = "\r",
        t = "\t",
      }
      if n == "u" then
        local hex = s:sub(i + 2, i + 5)
        local cp = tonumber(hex, 16) or 0
        if cp < 128 then
          out[#out + 1] = string.char(cp)
        elseif cp < 2048 then
          out[#out + 1] = string.char(192 + math.floor(cp / 64), 128 + (cp % 64))
        else
          out[#out + 1] = string.char(
            224 + math.floor(cp / 4096),
            128 + (math.floor(cp / 64) % 64),
            128 + (cp % 64)
          )
        end
        i = i + 6
      else
        out[#out + 1] = map[n] or n
        i = i + 2
      end
    else
      local j = s:find('[\\"]', i)
      if not j then
        error("unterminated string")
      end
      out[#out + 1] = s:sub(i, j - 1)
      i = j
    end
  end
  error("unterminated string")
end

local function parse_number(s, i)
  local num = s:match("^-?%d+%.?%d*[eE]?[+-]?%d*", i)
  if not num then
    error("invalid number at " .. i)
  end
  return tonumber(num), i + #num
end

local function parse_literal(s, i)
  if s:sub(i, i + 3) == "true" then
    return true, i + 4
  elseif s:sub(i, i + 4) == "false" then
    return false, i + 5
  elseif s:sub(i, i + 3) == "null" then
    return json.null, i + 4
  end
  error("invalid literal at " .. i)
end

local function parse_array(s, i)
  i = skip_ws(s, i + 1)
  local arr = json.array({})
  if s:sub(i, i) == "]" then
    return arr, i + 1
  end
  while true do
    local v
    v, i = parse_value(s, i)
    arr[#arr + 1] = v
    i = skip_ws(s, i)
    local c = s:sub(i, i)
    if c == "]" then
      return arr, i + 1
    elseif c ~= "," then
      error("expected ',' or ']' at " .. i)
    end
    i = skip_ws(s, i + 1)
  end
end

local function parse_object(s, i)
  i = skip_ws(s, i + 1)
  local obj = json.object({})
  if s:sub(i, i) == "}" then
    return obj, i + 1
  end
  while true do
    i = skip_ws(s, i)
    if s:sub(i, i) ~= '"' then
      error("expected string key at " .. i)
    end
    local key
    key, i = parse_string(s, i)
    i = skip_ws(s, i)
    if s:sub(i, i) ~= ":" then
      error("expected ':' at " .. i)
    end
    local val
    val, i = parse_value(s, skip_ws(s, i + 1))
    obj[key] = val
    i = skip_ws(s, i)
    local c = s:sub(i, i)
    if c == "}" then
      return obj, i + 1
    elseif c ~= "," then
      error("expected ',' or '}' at " .. i)
    end
    i = skip_ws(s, i + 1)
  end
end

parse_value = function(s, i)
  i = skip_ws(s, i)
  local c = s:sub(i, i)
  if c == '"' then
    return parse_string(s, i)
  elseif c == "{" then
    return parse_object(s, i)
  elseif c == "[" then
    return parse_array(s, i)
  elseif c == "-" or c:match("%d") then
    return parse_number(s, i)
  else
    return parse_literal(s, i)
  end
end

function json.encode(v)
  if ok_cjson and cjson and cjson.encode then
    local encoded, err = cjson.encode(v == json.null and nil or v)
    if encoded then
      return encoded
    end
    if err then
      -- fall through to bundled encoder
    end
  end
  if ok_luci and luci_jsonc and luci_jsonc.stringify then
    return luci_jsonc.stringify(v, false)
  end
  return encode(v)
end

function json.decode(s)
  if type(s) ~= "string" then
    return nil, "not a string"
  end
  if s == "" then
    return nil, "empty"
  end
  if ok_cjson and cjson and cjson.decode then
    local ok, result = pcall(cjson.decode, s)
    if ok then
      return result
    end
  end
  if ok_luci and luci_jsonc and luci_jsonc.parse then
    local result = luci_jsonc.parse(s)
    if result ~= nil then
      return result
    end
  end
  local ok, result = pcall(parse_value, s, 1)
  if not ok then
    return nil, result
  end
  return result
end

return json
