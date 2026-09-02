local json = require("json")
local util = require("util")
local auth = require("auth")
local sales = require("sales")
local sessions = require("sessions")
local coin = require("coin")
local router = require("router")
local hotspot = require("hotspot")
local settings = require("settings")
local store = require("store")

local status = {}

local function build_info()
  local info = store.read("/www/renzfi/build-info.json", nil)
    or store.read(util.WWW_DIR .. "/build-info.json", nil)
  if type(info) ~= "table" then
    info = {
      firmwareVersion = util.VERSION,
      adminBuild = "",
      portalRevision = "unknown",
      gitCommit = "unknown",
      edition = "openwrt",
    }
  end
  return info
end

function status.health(env)
  local session = auth.public_session(env)
  local snap = router.cache()
  local storage = settings.storage_status()
  local coin_st = coin.status()
  local hs = hotspot.status()
  local build = build_info()
  return {
    ok = true,
    httpPlane = "production",
    service = "renz-fi-openwrt",
    edition = "openwrt",
    storage = storage,
    installation = {
      state = "ready",
      ready = true,
      needsSetup = false,
      progressPercent = 100,
    },
    installationState = "ready",
    ethernet = {
      driverReady = true,
      link = true,
      hasIp = (snap.lanIp or "") ~= "",
      mode = "dhcp",
      ip = snap.lanIp or "",
      gateway = "",
      mac = "",
    },
    router = {
      configured = true,
      driverId = "openwrt",
      status = "local",
    },
    portal = {
      revision = 1,
      hasBanner = false,
      hasMusic = false,
      assetsReady = true,
    },
    coin = coin_st,
    uptimeSeconds = util.uptime_seconds(),
    serverTimeMs = util.now_ms(),
    session = session,
    deviceId = "RF-OPENWRT",
    deviceName = (settings.get().deviceName) or "Renz-Fi Router",
    version = util.VERSION,
    device = {
      deviceId = "RF-OPENWRT",
      serialNumber = "",
      friendlyName = (settings.get().deviceName) or "Renz-Fi Router",
      deviceName = (settings.get().deviceName) or "Renz-Fi Router",
      firmwareVersion = util.VERSION,
      version = util.VERSION,
      hardwareRevision = "openwrt",
      macAddress = "",
      ipAddress = snap.lanIp or "",
      routerDriver = "openwrt",
      online = true,
      capabilities = {
        coin = coin_st.enabled and true or false,
        voucher = true,
        assetUpload = false,
        router = "openwrt",
        fleet = false,
      },
    },
    build = build,
    managementAp = {
      enabled = false,
      running = false,
      mode = "disabled",
      ssid = json.null,
      ip = json.null,
    },
    hotspot = hs,
  }
end

function status.snapshot()
  local snap = router.cache()
  local obs = snap.observation or {}
  local wan = obs.wan or {}
  local coin_st = coin.status()
  local storage = settings.storage_status()
  local users = sessions.stats()
  return {
    server = { ok = true, uptimeSeconds = util.uptime_seconds() },
    database = { ok = true, path = util.DATA_DIR },
    sales = {
      today = sales.today(),
      weekly = sales.weekly(),
      monthly = sales.monthly(),
    },
    activeUsers = users,
    mikrotik = router.fill_mikrotik_compat(snap),
    internet = {
      ok = wan.internet == "online",
      known = wan.known and true or false,
      latencyMs = wan.internet == "online" and 1 or 0,
    },
    wan = wan,
    hotspot = {
      ok = hotspot.status().ok,
      status = hotspot.status().status,
      ssid = snap.ssid or "",
      engine = hotspot.engine(),
    },
    coinSlot = coin_st,
    routerCache = snap,
    storageStatus = storage,
    storage = {
      ramUsedKb = 0,
      ramTotalKb = 0,
      ramLabel = "openwrt",
      sd = {
        present = storage.sdPresent,
        mounted = storage.sdMounted,
        usedMb = 0,
        totalMb = 0,
        freeMb = 0,
        status = storage.fallbackActive and "overlay" or "Ready",
        fallback = storage.fallbackActive,
        pollingDisabled = false,
        recoveryAttempts = 0,
        mode = storage.storageMode,
      },
    },
    esp32 = {
      uptime = tostring(util.uptime_seconds()) .. "s",
      lastSeen = json.null,
    },
    sync = { pending = 0, lastSyncAt = json.null },
    edition = "openwrt",
  }
end

function status.system_health()
  local snap = router.cache()
  return {
    level = "HEALTHY",
    ethernet = {
      driver = "UP",
      link = (snap.lanIp or "") ~= "" and "UP" or "DOWN",
      ip = snap.lanIp or "",
    },
    storage = settings.storage_status(),
    coin = coin.status(),
    rgb = { enabled = false, mode = "DISABLED" },
    memory = { heap = 0, minimumHeap = 0 },
  }
end

function status.network()
  local snap = router.cache()
  return {
    interfaces = {
      ethernet = {
        link = true,
        linkUp = true,
        ip = snap.lanIp or "",
        gateway = "",
        subnet = "",
        dns = "",
        mac = "",
      },
      managementAp = {
        enabled = false,
        running = false,
        mode = "disabled",
      },
    },
    ethernet = {
      link = true,
      ip = snap.lanIp or "",
    },
    mode = "openwrt",
    modeLabel = "OpenWrt Router Edition",
    mdns = {
      hostname = snap.identity or "renzfi",
      adminUrl = "http://" .. (snap.lanIp ~= "" and snap.lanIp or "renzfi.lan") .. "/",
    },
  }
end

return status
