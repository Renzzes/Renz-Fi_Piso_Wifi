-- Renz-Fi Core loop. Equivalent of firmware loopTask.
-- Started by /etc/init.d/renzfi via procd. Must not depend on Admin.

local lua_dir = "/opt/renzfi/lua"
if os.getenv("RENZFI_LUA_PATH") and #os.getenv("RENZFI_LUA_PATH") > 0 then
  lua_dir = os.getenv("RENZFI_LUA_PATH")
end
package.path = lua_dir .. "/?.lua;" .. package.path

local util = require("util")
local routes = require("routes")
local router = require("router")
local logs = require("logs")

util.ensure_dirs()
logs.append("INFO", "system", "renzfi-tick started", { edition = "openwrt" })
router.observe()

while true do
  local ok, err = pcall(routes.tick)
  if not ok then
    logs.append("ERROR", "tick", tostring(err))
  end
  util.sleep(5)
end
