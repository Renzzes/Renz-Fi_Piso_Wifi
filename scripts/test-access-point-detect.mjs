#!/usr/bin/env node

function filterCandidates(arpRows, registeredIps) {
  const devices = [];
  for (const row of arpRows) {
    if (!row.ip || row.ip === "0.0.0.0" || !row.mac) continue;
    if (registeredIps.includes(row.ip)) continue;
    if (devices.some((item) => item.ip === row.ip)) continue;
    devices.push(row);
  }
  return devices;
}

function assert(name, condition) {
  if (!condition) {
    console.error(`FAIL ${name}`);
    process.exitCode = 1;
    return;
  }
  console.log(`ok ${name}`);
}

const rows = [
  { ip: "10.10.10.20", mac: "B8:FB:B3:7D:8F:A0", interface: "bridgeGuest", bridgePort: "ether4", status: "reachable" },
  { ip: "10.10.10.20", mac: "B8:FB:B3:7D:8F:A0", interface: "bridgeGuest", bridgePort: "ether4", status: "reachable" },
  { ip: "10.10.10.1", mac: "00:11:22:33:44:55", interface: "bridgeGuest", bridgePort: "ether1", status: "reachable" },
  { ip: "", mac: "AA:BB:CC:DD:EE:FF", interface: "bridgeGuest", bridgePort: "ether5", status: "reachable" },
];

const filtered = filterCandidates(rows, ["10.10.10.1"]);
assert("C24 candidate kept", filtered.some((row) => row.ip === "10.10.10.20" && row.bridgePort === "ether4"));
assert("registered IP excluded", !filtered.some((row) => row.ip === "10.10.10.1"));
assert("duplicate IP collapsed", filtered.filter((row) => row.ip === "10.10.10.20").length === 1);

if (process.exitCode) {
  process.exit(1);
}
console.log("access-point detect tests passed");
