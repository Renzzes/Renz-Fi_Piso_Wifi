-- uhttpd-mod-lua entry. Configured as:
--   list lua_prefix '/api=/opt/renzfi/lua/main.lua'

local lua_dir = "/opt/renzfi/lua"
if os.getenv("RENZFI_LUA_PATH") and #os.getenv("RENZFI_LUA_PATH") > 0 then
  lua_dir = os.getenv("RENZFI_LUA_PATH")
end
package.path = lua_dir .. "/?.lua;" .. package.path

local routes = require("routes")

function handle_request(env)
  routes.dispatch(env or {})
end

return {
  handle_request = handle_request,
}
