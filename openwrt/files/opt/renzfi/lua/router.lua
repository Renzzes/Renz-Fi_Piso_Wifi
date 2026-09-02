-- Observational router cache from local ubus/uci. Never RouterOS.

local json = require("json")
local store = require("store")
local util = require("util")

local router = {}

local STALE_SECONDS = 24 * 3600

local function ubus_call(path, method)
  if not util.have_cmd("ubus") then
    return nil
  end
  local cmd = string.format("ubus call %s %s", path, method or "status")
  local out = util.exec(cmd)
  if not out or out == "" then
    return nil
  end
  return json.decode(out)
end

local function uci_get(tuple)
  if not util.have_cmd("uci") then
    return ""
  end
  local out = util.exec("uci -q get " .. tuple)
  return util.trim(out:gsub("\n", ""))
end

function router.observe()
  local wan = ubus_call("network.interface.wan", "status") or {}
  local lan = ubus_call("network.interface.lan", "status") or {}
  local now = util.now()
  local wan_up = wan.up == true
  local ipv4 = ""
  if type(wan["ipv4-address"]) == "table" and wan["ipv4-address"][1] then
    ipv4 = wan["ipv4-address"][1].address or ""
  end
  local lan_ip = ""
  if type(lan["ipv4-address"]) == "table" and lan["ipv4-address"][1] then
    lan_ip = lan["ipv4-address"][1].address or ""
  end
  local ssid = uci_get("wireless.@wifi-iface[0].ssid")
  local identity = uci_get("system.@system[0].hostname")
  if identity == "" then
    identity = "renzfi"
  end
  local snapshot = {
    populated = true,
    stale = false,
    lastSynchronizedAt = util.iso8601(now),
    lastSyncAt = now,
    driverId = "openwrt",
    observation = {
      connectivity = wan_up and "online" or "offline",
      lastSuccessfulContactAt = wan_up and util.iso8601(now) or "",
      lastContactError = wan_up and "" or "wan down",
      hotspotStatus = "available",
      hotspotServer = "local",
      hotspotInterface = "br-lan",
      wan = {
        known = true,
        interface = "wan",
        link = wan_up and "up" or "down",
        dhcp = wan.proto or "dhcp",
        ip = ipv4,
        gateway = "",
        defaultRoute = wan_up and "yes" or "no",
        internet = wan_up and "online" or "offline",
        dns = "unknown",
        note = "local ubus observation",
      },
    },
    lanIp = lan_ip,
    ssid = ssid,
    identity = identity,
  }
  store.write("tmp/router-cache.json", snapshot)
  return snapshot
end

function router.cache()
  local snap = store.read("tmp/router-cache.json", nil)
  if type(snap) ~= "table" then
    snap = router.observe()
  else
    local age = util.now() - (snap.lastSyncAt or 0)
    snap.stale = age > STALE_SECONDS
  end
  return snap
end

function router.public_settings()
  local snap = router.cache()
  return {
    driverId = "openwrt",
    driverType = "openwrt",
    host = snap.lanIp or "",
    identity = snap.identity or "renzfi",
    ssid = snap.ssid or "",
    configured = true,
    -- Never return passwords. There is no MikroTik credential on this edition.
  }
end

function router.fill_mikrotik_compat(snap)
  snap = snap or router.cache()
  local obs = snap.observation or {}
  return {
    configured = true,
    ok = true,
    host = snap.lanIp or "",
    latencyMs = 0,
    connectivity = obs.connectivity or "unknown",
    lastSuccessfulContactAt = obs.lastSuccessfulContactAt or "",
    lastContactError = obs.lastContactError or "",
    driverId = "openwrt",
  }
end

return router
