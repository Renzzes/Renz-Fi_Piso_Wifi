#!/usr/bin/env node
/**
 * Host-side mirror of ExternalAccessPointTypes.h live-LAN IP validation
 * and Stage B public-record serialization rules.
 *
 * Run: node scripts/test-access-point-ip-validation.mjs
 */

const MANAGEMENT_AP_NETWORK = 0xc0a80400;
const MANAGEMENT_AP_MASK = 0xffffff00;
const MAX_APS = 8;
const NAME_MIN = 1;
const NAME_MAX = 32;

function parseIpv4Packed(text) {
  if (typeof text !== "string") return null;
  const value = text.trim();
  if (!value) return null;
  const octets = [];
  let current = 0;
  let digitSeen = false;
  for (let i = 0; i < value.length; i += 1) {
    const c = value[i];
    if (c >= "0" && c <= "9") {
      digitSeen = true;
      current = current * 10 + (c.charCodeAt(0) - 48);
      if (current > 255) return null;
    } else if (c === ".") {
      if (!digitSeen || octets.length >= 3) return null;
      octets.push(current);
      current = 0;
      digitSeen = false;
    } else {
      return null;
    }
  }
  if (!digitSeen || octets.length !== 3) return null;
  octets.push(current);
  return (
    ((octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]) >>> 0
  );
}

function ipv4Network(ip, mask) {
  return (ip & mask) >>> 0;
}

function ipv4Broadcast(ip, mask) {
  return ((ip & mask) | (~mask >>> 0)) >>> 0;
}

function ipv4OnSubnet(ip, networkIp, mask) {
  return (ip & mask) >>> 0 === (networkIp & mask) >>> 0;
}

function ipv4IsPrivateLan(ip) {
  ip >>>= 0;
  return (
    (ip & 0xff000000) >>> 0 === 0x0a000000 ||
    (ip & 0xfff00000) >>> 0 === 0xac100000 ||
    (ip & 0xffff0000) >>> 0 === 0xc0a80000
  );
}

function ipv4IsUnusableHost(ip) {
  if (ip === 0 || ip === 0xffffffff) return true;
  if ((ip & 0xff000000) === 0x7f000000) return true;
  if ((ip & 0xffff0000) === 0xa9fe0000) return true;
  if ((ip & 0xf0000000) >= 0xe0000000) return true;
  return false;
}

function validateManagementIp(candidateIp, liveEsp32Ip, liveGatewayIp, liveSubnetMask) {
  const candidate = parseIpv4Packed(candidateIp);
  if (candidate == null) return "INVALID_IP";
  if (ipv4IsUnusableHost(candidate)) return "INVALID_IP";

  const esp32Ip = parseIpv4Packed(liveEsp32Ip);
  const mask = parseIpv4Packed(liveSubnetMask);
  if (esp32Ip == null || mask == null || mask === 0 || esp32Ip === 0) {
    return "ETHERNET_NOT_READY";
  }

  if (ipv4OnSubnet(candidate, MANAGEMENT_AP_NETWORK, MANAGEMENT_AP_MASK)) {
    return "IP_RESERVED";
  }

  if (candidate === esp32Ip) return "IP_RESERVED";

  const gateway = parseIpv4Packed(liveGatewayIp);
  if (gateway != null && gateway !== 0 && candidate === gateway) {
    return "IP_RESERVED";
  }

  if (ipv4OnSubnet(candidate, esp32Ip, mask)) {
    const network = ipv4Network(esp32Ip, mask);
    const broadcast = ipv4Broadcast(esp32Ip, mask);
    if (candidate === network || candidate === broadcast) return "IP_RESERVED";
    return "OK";
  }

  if (!ipv4IsPrivateLan(candidate)) return "IP_NOT_ON_LAN";
  return "OK";
}

function parseVendor(raw) {
  const value = String(raw ?? "")
    .trim()
    .toLowerCase();
  if (value === "tp-link" || value === "ruijie" || value === "tenda" || value === "other") {
    return value;
  }
  return "generic";
}

function validateName(name) {
  const value = String(name ?? "").trim();
  if (value.length < NAME_MIN || value.length > NAME_MAX) return "INVALID_REQUEST";
  return "OK";
}

function validateCreate({ name, managementIp, existingIps, live, count }) {
  if (count >= MAX_APS) return "LIMIT_REACHED";
  const nameStatus = validateName(name);
  if (nameStatus !== "OK") return nameStatus;
  const ipStatus = validateManagementIp(
    managementIp,
    live.esp32Ip,
    live.gateway,
    live.mask,
  );
  if (ipStatus !== "OK") return ipStatus;
  if (existingIps.includes(managementIp.trim())) return "DUPLICATE_IP";
  return "OK";
}

function toPublicRecord(stored) {
  const { password, passwordProtected, ...rest } = stored;
  void password;
  return {
    id: rest.id,
    name: rest.name,
    enabled: rest.enabled,
    vendor: rest.vendor,
    model: rest.model,
    managementIp: rest.managementIp,
    hasCredentials: Boolean(passwordProtected && String(passwordProtected).length > 0),
    ssid: rest.ssid,
    location: rest.location,
    notes: rest.notes,
  };
}

function assertEqual(actual, expected, msg) {
  if (actual !== expected) {
    throw new Error(`${msg}: expected ${expected}, got ${actual}`);
  }
}

function assert(condition, msg) {
  if (!condition) throw new Error(msg);
}

const live = {
  esp32Ip: "10.10.10.2",
  gateway: "10.10.10.1",
  mask: "255.255.255.0",
};

assertEqual(
  validateManagementIp("10.10.10.10", live.esp32Ip, live.gateway, live.mask),
  "OK",
  "valid same-subnet IP",
);
assertEqual(
  validateManagementIp("10.10.10", live.esp32Ip, live.gateway, live.mask),
  "INVALID_IP",
  "invalid IPv4",
);
assertEqual(
  validateManagementIp("10.10.10.0", live.esp32Ip, live.gateway, live.mask),
  "IP_RESERVED",
  "network address",
);
assertEqual(
  validateManagementIp("10.10.10.255", live.esp32Ip, live.gateway, live.mask),
  "IP_RESERVED",
  "broadcast address",
);
assertEqual(
  validateManagementIp("10.10.10.2", live.esp32Ip, live.gateway, live.mask),
  "IP_RESERVED",
  "ESP32 IP",
);
assertEqual(
  validateManagementIp("10.10.10.1", live.esp32Ip, live.gateway, live.mask),
  "IP_RESERVED",
  "gateway IP",
);
assertEqual(
  validateCreate({
    name: "AP-02",
    managementIp: "10.10.10.10",
    existingIps: ["10.10.10.10"],
    live,
    count: 1,
  }),
  "DUPLICATE_IP",
  "duplicate AP IP",
);
assertEqual(
  validateManagementIp("10.20.20.10", live.esp32Ip, live.gateway, live.mask),
  "OK",
  "routed RFC1918 private LAN",
);
assertEqual(
  validateManagementIp("192.168.88.20", live.esp32Ip, live.gateway, live.mask),
  "OK",
  "routed 192.168.88.x AP subnet",
);
assertEqual(
  validateManagementIp("8.8.8.8", live.esp32Ip, live.gateway, live.mask),
  "IP_NOT_ON_LAN",
  "public IP rejected",
);
assertEqual(
  validateManagementIp("192.168.4.10", live.esp32Ip, live.gateway, live.mask),
  "IP_RESERVED",
  "Management AP subnet",
);
assertEqual(
  validateManagementIp("10.10.10.10", "", "", ""),
  "ETHERNET_NOT_READY",
  "Ethernet not ready",
);
assertEqual(
  validateCreate({
    name: "AP-09",
    managementIp: "10.10.10.20",
    existingIps: [],
    live,
    count: 8,
  }),
  "LIMIT_REACHED",
  "maximum 8 APs",
);
assertEqual(validateName("A"), "OK", "name length 1");
assertEqual(validateName("A".repeat(32)), "OK", "name length 32");
assertEqual(validateName("A".repeat(33)), "INVALID_REQUEST", "name length >32");
assertEqual(validateName(""), "INVALID_REQUEST", "empty name");
assertEqual(parseVendor("tp-link"), "tp-link", "vendor tp-link");
assertEqual(parseVendor("Ruijie"), "ruijie", "vendor normalization");
assertEqual(parseVendor("unknown-brand"), "generic", "unknown vendor → generic");
assertEqual(parseVendor(""), "generic", "empty vendor → generic");

const publicRecord = toPublicRecord({
  id: "ap_a1b2c3d4",
  name: "AP-01",
  enabled: true,
  vendor: "generic",
  model: "Archer C6",
  managementIp: "10.10.10.10",
  username: "admin",
  password: "secret",
  passwordProtected: "enc:v1:aabbcc",
  ssid: "RENZ-FI-EXT",
  location: "Counter",
  notes: "",
});
const serialized = JSON.stringify(publicRecord);
assert(!serialized.includes("secret"), "API output must not contain plaintext password");
assert(
  !serialized.includes("passwordProtected"),
  "API output must not contain passwordProtected",
);
assertEqual(publicRecord.hasCredentials, true, "hasCredentials true when protected blob exists");
assert(!("password" in publicRecord), "password field absent from public record");

console.log("test-access-point-ip-validation: pass");
