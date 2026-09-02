-- Local internet grant. Never talks to RouterOS / MikroTik.

local json = require("json")
local util = require("util")
local logs = require("logs")

local hotspot = {}

local function nft_ensure()
  if not util.have_cmd("nft") then
    return false
  end
  util.exec("nft add table inet renzfi 2>/dev/null")
  util.exec(
    "nft add set inet renzfi auth '{ type ether_addr; flags timeout; }' 2>/dev/null"
  )
  util.exec(
    "nft add chain inet renzfi forward '{ type filter hook forward priority 0; policy accept; }' 2>/dev/null"
  )
  return true
end

function hotspot.engine()
  if util.have_cmd("ndsctl") then
    local out = util.exec("ndsctl status")
    if out and out ~= "" then
      return "opennds"
    end
    return "opennds"
  end
  if util.have_cmd("nft") then
    return "nftables"
  end
  return "stub"
end

function hotspot.authorize(mac, seconds)
  mac = util.normalize_mac(mac)
  if not mac then
    return false, "invalid mac"
  end
  seconds = tonumber(seconds) or 900
  if seconds < 1 then
    seconds = 60
  end
  local engine = hotspot.engine()
  if engine == "opennds" then
    local out, ok = util.exec("ndsctl auth " .. util.shell_quote(mac))
    logs.append("INFO", "hotspot", "ndsctl auth " .. mac, { ok = ok, out = out })
    return ok, out
  end
  if engine == "nftables" then
    nft_ensure()
    local cmd = string.format(
      "nft add element inet renzfi auth { %s timeout %ds }",
      mac,
      seconds
    )
    local out, ok = util.exec(cmd)
    logs.append("INFO", "hotspot", "nft auth " .. mac, { ok = ok, seconds = seconds })
    return ok, out
  end
  logs.append("WARN", "hotspot", "stub authorize " .. mac, { seconds = seconds })
  return true, "stub"
end

function hotspot.deauthorize(mac)
  mac = util.normalize_mac(mac)
  if not mac then
    return false, "invalid mac"
  end
  local engine = hotspot.engine()
  if engine == "opennds" then
    local out, ok = util.exec("ndsctl deauth " .. util.shell_quote(mac))
    logs.append("INFO", "hotspot", "ndsctl deauth " .. mac, { ok = ok })
    return ok, out
  end
  if engine == "nftables" then
    nft_ensure()
    local out, ok = util.exec("nft delete element inet renzfi auth { " .. mac .. " }")
    logs.append("INFO", "hotspot", "nft deauth " .. mac, { ok = ok })
    return ok, out
  end
  logs.append("INFO", "hotspot", "stub deauth " .. mac, json.object({}))
  return true, "stub"
end

function hotspot.status()
  return {
    engine = hotspot.engine(),
    ok = hotspot.engine() ~= "stub",
    status = hotspot.engine() == "stub" and "unconfigured" or "available",
  }
end

return hotspot
