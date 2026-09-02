-- Admin session cookie auth. Default password "admin" until changed.
-- SHA256("admin") = 8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918

local json = require("json")
local store = require("store")
local util = require("util")
local http = require("http")

local auth = {}

local DEFAULT_HASH = "8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918"
local LOCK_AFTER = 8
local LOCK_SECONDS = 300

local function load_auth()
  local data = store.read("etc/auth.json", nil)
  if type(data) ~= "table" then
    data = {
      passwordHash = DEFAULT_HASH,
      mustChangePassword = true,
      firstBootCompleted = false,
      username = "admin",
      failedAttempts = 0,
      lockedUntil = 0,
    }
    store.write("etc/auth.json", data)
  end
  return data
end

local function save_auth(data)
  store.write("etc/auth.json", data)
end

local function load_sessions()
  local data = store.read("tmp/sessions.json", nil)
  if type(data) ~= "table" then
    return json.object({})
  end
  return data
end

function auth.public_session(env)
  local token = http.cookie(env)
  if not token then
    return {
      authenticated = false,
      mustChangePassword = load_auth().mustChangePassword and true or false,
      firstBootCompleted = load_auth().firstBootCompleted and true or false,
      role = "none",
    }
  end
  local sessions = load_sessions()
  local row = sessions[token]
  if not row then
    return {
      authenticated = false,
      mustChangePassword = load_auth().mustChangePassword and true or false,
      firstBootCompleted = load_auth().firstBootCompleted and true or false,
      role = "none",
    }
  end
  if (row.expiresAt or 0) < util.now() then
    sessions[token] = nil
    store.write("tmp/sessions.json", sessions)
    return {
      authenticated = false,
      mustChangePassword = load_auth().mustChangePassword and true or false,
      firstBootCompleted = load_auth().firstBootCompleted and true or false,
      role = "none",
    }
  end
  local a = load_auth()
  return {
    authenticated = true,
    mustChangePassword = a.mustChangePassword and true or false,
    firstBootCompleted = a.firstBootCompleted and true or false,
    role = row.role or "owner",
    username = a.username or "admin",
  }
end

function auth.require(env)
  local session = auth.public_session(env)
  if not session.authenticated then
    http.err(401, "Authentication required", "UNAUTHORIZED")
    return nil
  end
  return session
end

function auth.login(env)
  local body = http.parse_json_body(env)
  local password = tostring(body.password or "")
  local username = tostring(body.username or "admin")
  if password == "" then
    http.err(400, "Invalid login payload", "BAD_REQUEST")
    return
  end
  local a = load_auth()
  local now = util.now()
  if (a.lockedUntil or 0) > now then
    http.err(423, "Account temporarily locked. Try again later.", "LOCKED")
    return
  end
  local expected = a.passwordHash or DEFAULT_HASH
  local hash = util.sha256(password)
  local ok_pw = hash and hash == expected
  if not ok_pw and expected == DEFAULT_HASH and password == "admin" then
    ok_pw = true
  end
  if not ok_pw then
    a.failedAttempts = (a.failedAttempts or 0) + 1
    if a.failedAttempts >= LOCK_AFTER then
      a.lockedUntil = now + LOCK_SECONDS
      a.failedAttempts = 0
    end
    save_auth(a)
    http.err(401, "Invalid password.", "INVALID")
    return
  end
  a.failedAttempts = 0
  a.lockedUntil = 0
  save_auth(a)
  local token = util.random_hex(24)
  local sessions = load_sessions()
  local ttl = body.rememberIp and (30 * 86400) or (12 * 3600)
  sessions[token] = {
    role = "owner",
    username = username,
    createdAt = now,
    expiresAt = now + ttl,
    ip = env.REMOTE_ADDR or "",
  }
  store.write("tmp/sessions.json", sessions)
  http.ok({
    authenticated = true,
    username = a.username or "admin",
    rememberIp = body.rememberIp and true or false,
    mustChangePassword = a.mustChangePassword and true or false,
    firstBootCompleted = a.firstBootCompleted and true or false,
    role = "owner",
  }, "OK", { ["Set-Cookie"] = http.set_cookie_header(token, ttl) })
end

function auth.logout(env)
  local token = http.cookie(env)
  if token then
    local sessions = load_sessions()
    sessions[token] = nil
    store.write("tmp/sessions.json", sessions)
  end
  http.ok({ ok = true }, "OK", { ["Set-Cookie"] = http.clear_cookie_header() })
end

function auth.change_password(env)
  local session = auth.require(env)
  if not session then
    return
  end
  local body = http.parse_json_body(env)
  local old_password = tostring(body.oldPassword or "")
  local new_password = tostring(body.newPassword or "")
  if #new_password < 8 then
    http.err(400, "Unable to change password", "PASSWORD_CHANGE_FAILED")
    return
  end
  local a = load_auth()
  local old_hash = util.sha256(old_password)
  if old_hash ~= (a.passwordHash or DEFAULT_HASH) then
    http.err(400, "Unable to change password", "PASSWORD_CHANGE_FAILED")
    return
  end
  a.passwordHash = util.sha256(new_password)
  a.mustChangePassword = false
  a.firstBootCompleted = true
  save_auth(a)
  http.ok({
    ok = true,
    mustChangePassword = false,
    firstBootCompleted = true,
  })
end

return auth
