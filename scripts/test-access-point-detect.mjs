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

function parseDetectResultEnvelope(rawResult) {
  const root =
    typeof rawResult === "string"
      ? JSON.parse(rawResult)
      : rawResult && typeof rawResult === "object"
        ? rawResult
        : null;
  if (!root || typeof root !== "object") {
    throw new Error("DETECT_BAD_RESULT");
  }
  if (root.success === false) {
    throw new Error("DETECT_FAILED");
  }
  const data = root.data && typeof root.data === "object" ? root.data : root;
  const devices = Array.isArray(data.devices) ? data.devices : [];
  return { devices };
}

function classifyDetectUiState({ state, result }) {
  if (state === "queued" || state === "running") return "detecting";
  if (state !== "completed") return "failed";
  try {
    const parsed = parseDetectResultEnvelope(result);
    return parsed.devices.length > 0 ? "found" : "empty";
  } catch (error) {
    if (String(error.message) === "DETECT_FAILED") return "failed";
    return "failed";
  }
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

const objectEnvelope = {
  success: true,
  data: {
    devices: [
      {
        ip: "10.10.10.20",
        mac: "B8:FB:B3:7D:8F:A0",
        interface: "bridgeGuest",
        bridgePort: "ether4",
        status: "reachable",
      },
    ],
  },
};
const parsedObject = parseDetectResultEnvelope(objectEnvelope);
assert(
  "detect object envelope keeps C24 candidate",
  parsedObject.devices.some(
    (row) => row.ip === "10.10.10.20" && row.mac === "B8:FB:B3:7D:8F:A0",
  ),
);
assert(
  "detect completed with candidate state",
  classifyDetectUiState({ state: "completed", result: objectEnvelope }) === "found",
);

const emptyEnvelope = { success: true, data: { devices: [] } };
assert(
  "detect completed empty state",
  classifyDetectUiState({ state: "completed", result: emptyEnvelope }) === "empty",
);

const failedEnvelope = { success: false, error: "router unavailable" };
assert(
  "detect failed state",
  classifyDetectUiState({ state: "completed", result: failedEnvelope }) === "failed",
);

assert(
  "detect timeout/running state remains detecting",
  classifyDetectUiState({ state: "running", result: null }) === "detecting",
);

if (process.exitCode) {
  process.exit(1);
}
console.log("access-point detect tests passed");
