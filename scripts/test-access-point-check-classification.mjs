#!/usr/bin/env node
/**
 * Host-side mirror of Stage C External Access Point reachability:
 * classification, exact route matching, single-flight jobs, RAM-only status.
 *
 * Run: node scripts/test-access-point-check-classification.mjs
 */

function classifyReachability(disabled, ethernetReady, icmpOk, tcpOk) {
  if (disabled) return "disabled";
  if (!ethernetReady) return "unknown";
  if (icmpOk && tcpOk) return "online";
  if (icmpOk && !tcpOk) return "network_reachable";
  if (!icmpOk && tcpOk) return "management_reachable";
  return "unreachable";
}

function parseItemPath(path) {
  const prefix = "/api/access-points/";
  if (!path.startsWith(prefix)) return null;
  const rest = path.slice(prefix.length);
  if (!rest || rest.includes("/") || rest === "jobs") return null;
  return rest;
}

function parseCheckPath(path) {
  const prefix = "/api/access-points/";
  const suffix = "/check";
  if (!path.startsWith(prefix) || !path.endsWith(suffix)) return null;
  if (path.length <= prefix.length + suffix.length) return null;
  const rest = path.slice(prefix.length, path.length - suffix.length);
  if (!rest || rest.includes("/") || rest === "jobs") return null;
  return rest;
}

function parseJobPath(path) {
  const prefix = "/api/access-points/jobs/";
  if (!path.startsWith(prefix)) return null;
  const rest = path.slice(prefix.length);
  if (!rest || rest.includes("/") || rest[0] < "0" || rest[0] > "9") return null;
  const jobId = Number.parseInt(rest, 10);
  return jobId > 0 ? jobId : null;
}

function parseDetectJobPath(path) {
  const prefix = "/api/access-points/detect/jobs/";
  if (!path.startsWith(prefix)) return null;
  const rest = path.slice(prefix.length);
  if (!rest || rest.includes("/") || rest[0] < "0" || rest[0] > "9") return null;
  const jobId = Number.parseInt(rest, 10);
  return jobId > 0 ? jobId : null;
}

function persistFields(record) {
  return {
    id: record.id,
    name: record.name,
    enabled: record.enabled,
    vendor: record.vendor,
    model: record.model,
    managementIp: record.managementIp,
    username: record.username,
    passwordProtected: record.passwordProtected,
    ssid: record.ssid,
    location: record.location,
    notes: record.notes,
  };
}

function createRegistry() {
  return {
    accessPoints: [],
    job: null,
    nextJobId: 1,
    sdRecovery: false,
    ethernetReady: true,
    workerAvailable: true,
    probes: [],
  };
}

function enqueueCheck(state, id) {
  if (state.sdRecovery) {
    return { http: 503, code: "STORAGE_RECOVERY_IN_PROGRESS" };
  }
  if (!state.workerAvailable) {
    return { http: 503, code: "CHECK_FAILED" };
  }
  const ap = state.accessPoints.find((row) => row.id === id);
  if (!ap) {
    return { http: 404, code: "ACCESS_POINT_NOT_FOUND" };
  }
  if (state.job && (state.job.state === "queued" || state.job.state === "running")) {
    return { http: 503, code: "ACCESS_POINT_CHECK_BUSY" };
  }
  const jobId = state.nextJobId++;
  state.job = {
    jobId,
    accessPointId: id,
    state: "queued",
    ok: false,
    status: "unknown",
  };
  return {
    http: 202,
    body: { jobId, accessPointId: id, state: "queued" },
  };
}

function runQueuedJob(state, probe) {
  if (!state.job || state.job.state !== "queued") return null;
  state.job.state = "running";
  const ap = state.accessPoints.find((row) => row.id === state.job.accessPointId);
  if (!ap) {
    state.job.state = "failed";
    state.job.errorCode = "ACCESS_POINT_NOT_FOUND";
    return state.job;
  }
  if (!ap.enabled) {
    ap.status = "disabled";
    state.job.state = "completed";
    state.job.ok = true;
    state.job.status = "disabled";
    state.job.errorCode = "ACCESS_POINT_DISABLED";
    return state.job;
  }
  if (!state.ethernetReady) {
    ap.status = "unknown";
    state.job.state = "completed";
    state.job.ok = false;
    state.job.status = "unknown";
    state.job.errorCode = "ETHERNET_NOT_READY";
    return state.job;
  }
  state.probes.push(ap.managementIp);
  const icmpOk = Boolean(probe?.icmpOk);
  const tcpOk = Boolean(probe?.tcpOk);
  const status = classifyReachability(false, true, icmpOk, tcpOk);
  ap.status = status;
  ap.latencyMs = icmpOk ? probe.icmpLatencyMs : tcpOk ? probe.tcpLatencyMs : null;
  state.job.state = "completed";
  state.job.ok = status === "online" || status === "network_reachable" || status === "management_reachable";
  state.job.status = status;
  return state.job;
}

function pollJob(state, jobId) {
  if (!state.job || state.job.jobId !== jobId) return { http: 404, code: "NOT_FOUND" };
  return { http: 200, body: { ...state.job } };
}

function assert(name, condition) {
  if (!condition) {
    console.error(`FAIL ${name}`);
    process.exitCode = 1;
    return;
  }
  console.log(`ok ${name}`);
}

assert(
  "1 AP not found",
  enqueueCheck(createRegistry(), "ap_missing").code === "ACCESS_POINT_NOT_FOUND",
);

{
  const state = createRegistry();
  state.accessPoints.push({
    id: "ap_disabled",
    enabled: false,
    managementIp: "10.10.10.10",
    status: "unknown",
  });
  const queued = enqueueCheck(state, "ap_disabled");
  const job = runQueuedJob(state, { icmpOk: true, tcpOk: true });
  assert("2 AP disabled no probe", queued.http === 202 && job.status === "disabled" && state.probes.length === 0);
}

{
  const state = createRegistry();
  state.ethernetReady = false;
  state.accessPoints.push({
    id: "ap_1",
    enabled: true,
    managementIp: "10.10.10.10",
    status: "unknown",
  });
  enqueueCheck(state, "ap_1");
  const job = runQueuedJob(state, { icmpOk: true, tcpOk: true });
  assert(
    "3 Ethernet unavailable is unknown not offline",
    job.status === "unknown" && job.errorCode === "ETHERNET_NOT_READY" && state.probes.length === 0,
  );
}

assert(
  "4 ICMP+TCP80 => online",
  classifyReachability(false, true, true, true) === "online",
);
assert(
  "5 ICMP+TCP443 (tcpOk) => online",
  classifyReachability(false, true, true, true) === "online",
);
assert(
  "6 ICMP success + TCP fail => network_reachable",
  classifyReachability(false, true, true, false) === "network_reachable",
);
assert(
  "7 ICMP fail + TCP success => management_reachable",
  classifyReachability(false, true, false, true) === "management_reachable",
);
assert(
  "8 ICMP fail + TCP fail => unreachable",
  classifyReachability(false, true, false, false) === "unreachable",
);
assert(
  "Stage C never classifies auth_failed",
  classifyReachability(false, true, false, false) !== "auth_failed",
);

{
  const state = createRegistry();
  state.accessPoints.push({
    id: "ap_1",
    enabled: true,
    managementIp: "10.10.10.10",
    status: "unknown",
  });
  const first = enqueueCheck(state, "ap_1");
  const second = enqueueCheck(state, "ap_1");
  assert("9 duplicate Check request", first.http === 202 && second.code === "ACCESS_POINT_CHECK_BUSY");
}

{
  const state = createRegistry();
  state.accessPoints.push(
    { id: "ap_1", enabled: true, managementIp: "10.10.10.10", status: "unknown" },
    { id: "ap_2", enabled: true, managementIp: "10.10.10.11", status: "unknown" },
  );
  enqueueCheck(state, "ap_1");
  const busy = enqueueCheck(state, "ap_2");
  assert("10 worker already busy", busy.code === "ACCESS_POINT_CHECK_BUSY");
}

{
  const state = createRegistry();
  state.sdRecovery = true;
  state.accessPoints.push({
    id: "ap_1",
    enabled: true,
    managementIp: "10.10.10.10",
    status: "unknown",
  });
  const result = enqueueCheck(state, "ap_1");
  assert("11 SD recovery active", result.code === "STORAGE_RECOVERY_IN_PROGRESS" && result.http === 503);
}

{
  const state = createRegistry();
  assert("12 empty AP registry", state.accessPoints.length === 0 && enqueueCheck(state, "ap_1").http === 404);
  assert("12 empty registry stays idle", state.job === null && state.probes.length === 0);
}

{
  const state = createRegistry();
  state.accessPoints.push(
    { id: "ap_1", enabled: true, managementIp: "10.10.10.10", status: "unknown" },
    { id: "ap_2", enabled: true, managementIp: "10.10.10.11", status: "unknown" },
  );
  enqueueCheck(state, "ap_1");
  runQueuedJob(state, { icmpOk: true, tcpOk: true, icmpLatencyMs: 3 });
  assert(
    "13 two APs only one manual check",
    state.probes.length === 1 &&
      state.probes[0] === "10.10.10.10" &&
      state.accessPoints[1].status === "unknown",
  );
}

{
  const state = createRegistry();
  state.accessPoints.push({
    id: "ap_1",
    enabled: true,
    managementIp: "10.10.10.10",
    status: "unknown",
  });
  const queued = enqueueCheck(state, "ap_1");
  const job = runQueuedJob(state, { icmpOk: true, tcpOk: true, icmpLatencyMs: 3 });
  assert("14 job completion", queued.http === 202 && job.state === "completed" && job.status === "online" && job.ok === true);
}

{
  const state = createRegistry();
  state.accessPoints.push({
    id: "ap_1",
    enabled: true,
    managementIp: "10.10.10.10",
    status: "unknown",
  });
  enqueueCheck(state, "ap_1");
  state.accessPoints = [];
  const job = runQueuedJob(state, { icmpOk: true, tcpOk: true });
  assert("15 job failure", job.state === "failed" && job.errorCode === "ACCESS_POINT_NOT_FOUND");
}

{
  const state = createRegistry();
  state.accessPoints.push({
    id: "ap_1",
    enabled: true,
    managementIp: "10.10.10.10",
    status: "unknown",
  });
  const queued = enqueueCheck(state, "ap_1");
  runQueuedJob(state, { icmpOk: true, tcpOk: true, icmpLatencyMs: 4 });
  const polled = pollJob(state, queued.body.jobId);
  assert("16 job polling after completion", polled.http === 200 && polled.body.state === "completed");
}

assert("17 nonexistent job ID", pollJob(createRegistry(), 99).http === 404);

assert(
  "18 nonexistent AP ID",
  enqueueCheck(createRegistry(), "ap_nope").http === 404,
);

assert("19 malformed route extra segment", parseCheckPath("/api/access-points/ap_1/check/extra") === null);
assert("19 malformed jobs path", parseJobPath("/api/access-points/jobs/") === null);
assert("19 collection is not a check", parseCheckPath("/api/access-points") === null);

assert("20 exact collection is not item", parseItemPath("/api/access-points") === null);
assert("20 exact item", parseItemPath("/api/access-points/ap_a1b2c3d4") === "ap_a1b2c3d4");
assert("20 exact check", parseCheckPath("/api/access-points/ap_a1b2c3d4/check") === "ap_a1b2c3d4");
assert("20 jobs is not item", parseItemPath("/api/access-points/jobs/1") === null);
assert("20 exact job", parseJobPath("/api/access-points/jobs/1") === 1);
assert("20 check is not item", parseItemPath("/api/access-points/ap_a1b2c3d4/check") === null);
assert("20 detect route does not parse as check", parseCheckPath("/api/access-points/detect") === null);
assert("20 detect jobs parse", parseDetectJobPath("/api/access-points/detect/jobs/7") === 7);
assert("20 detect jobs reject malformed", parseDetectJobPath("/api/access-points/detect/jobs/7/extra") === null);

{
  const persisted = persistFields({
    id: "ap_1",
    name: "Lobby",
    enabled: true,
    vendor: "generic",
    model: "X",
    managementIp: "10.10.10.10",
    username: "admin",
    passwordProtected: "enc:v2:blob",
    ssid: "RENZ-FI",
    location: "hall",
    notes: "",
    status: "online",
    latencyMs: 3,
    lastCheck: 123,
    lastError: "ICMP_TIMEOUT",
  });
  assert("RAM status is not persisted", !("status" in persisted) && !("latencyMs" in persisted) && !("lastCheck" in persisted));
}

if (process.exitCode) {
  console.error("access-point check classification tests failed");
  process.exit(1);
}

console.log("access-point check classification tests passed");
