#!/usr/bin/env node
/**
 * Storage snapshot health must distinguish media health from reconciliation.
 * Mirrors StorageManager::refreshRuntimeSnapshot after the conflict/DEGRADED split.
 *
 * Run: node scripts/test-storage-health-semantics.mjs
 */

function snapshotHealth({
  sdMounted,
  sdReadable,
  sdWritable,
  usingFallback,
  layoutValid,
  pendingReplay,
  recoveryQueue,
  pendingConflicts,
  emergencyPercent,
  fallbackActive,
}) {
  const emergencyActive = Boolean(fallbackActive || pendingReplay > 0);
  if (emergencyActive && emergencyPercent >= 90) return "CRITICAL";
  if (sdMounted && sdReadable && !sdWritable) return "READ_ONLY";
  if (emergencyActive && emergencyPercent >= 70) return "WARNING";
  if (!sdMounted || usingFallback || !layoutValid || pendingReplay > 0 || recoveryQueue > 0) {
    return "DEGRADED";
  }
  if (sdWritable) return "HEALTHY";
  return "UNKNOWN";
}

function reconciliationStatus(pendingConflicts) {
  return pendingConflicts > 0 ? "conflict" : "ok";
}

function assertEqual(actual, expected, msg) {
  if (actual !== expected) {
    throw new Error(`${msg}: expected ${expected}, got ${actual}`);
  }
}

const healthySd = {
  sdMounted: true,
  sdReadable: true,
  sdWritable: true,
  usingFallback: false,
  layoutValid: true,
  pendingReplay: 0,
  recoveryQueue: 0,
  emergencyPercent: 1,
  fallbackActive: false,
};

assertEqual(
  snapshotHealth({ ...healthySd, pendingConflicts: 1 }),
  "HEALTHY",
  "unresolved SPIFFS/SD conflict must not set DEGRADED",
);
assertEqual(reconciliationStatus(1), "conflict", "conflict list stays visible");

assertEqual(
  snapshotHealth({ ...healthySd, pendingConflicts: 0 }),
  "HEALTHY",
  "healthy SD with no conflicts",
);

assertEqual(
  snapshotHealth({
    ...healthySd,
    sdMounted: false,
    usingFallback: true,
    fallbackActive: true,
    pendingConflicts: 0,
  }),
  "DEGRADED",
  "missing SD is still DEGRADED",
);

assertEqual(
  snapshotHealth({
    ...healthySd,
    sdWritable: false,
    pendingConflicts: 1,
  }),
  "READ_ONLY",
  "read-only media wins over conflict",
);

console.log("test-storage-health-semantics: ok");
