local json = require("json")
local store = require("store")
local util = require("util")
local eventbus = require("eventbus")

local settings = {}

local function load()
  local data = store.read("etc/settings.json", nil)
  if type(data) ~= "table" then
    data = {
      deviceName = "Renz-Fi Router",
      timezone = "Asia/Manila",
    }
    store.write("etc/settings.json", data)
  end
  return data
end

function settings.get()
  return load()
end

function settings.save(body)
  local data = load()
  for k, v in pairs(body or {}) do
    if k ~= "password" and k ~= "mikrotikPassword" and k ~= "routerPassword" then
      data[k] = v
    end
  end
  store.write("etc/settings.json", data)
  eventbus.emit("system.status", json.object({}))
  return data
end

function settings.admin()
  return {
    username = "admin",
    mustChangePassword = (store.read("etc/auth.json", {}) or {}).mustChangePassword and true or false,
  }
end

function settings.operator()
  return json.object({
    enabled = false,
    username = json.null,
  })
end

function settings.storage_status()
  local usb = util.file_exists("/mnt/sda1") or util.file_exists("/mnt/usb")
  return {
    storageMode = usb and "USB" or "overlay",
    sdPresent = usb,
    sdMounted = usb,
    fallbackActive = not usb,
    spiffsReady = true,
    ok = true,
    capacity = 0,
    used = 0,
  }
end

return settings
