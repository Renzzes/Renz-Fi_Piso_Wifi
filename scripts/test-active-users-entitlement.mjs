#!/usr/bin/env node
/**
 * Active Users entitlement filter regression (mirrors isPortalSessionActive).
 * Run: node scripts/test-active-users-entitlement.mjs
 */

const PORTAL_HEARTBEAT_STALE_SEC = 120;

function isPortalSessionActive(session, nowSec) {
  const state = session.sessionState || "idle";
  if (state === "expired" || state === "idle") return false;
  if (state === "expiring") return (session.secondsLeft || 0) > 0;

  const coinWindow = !!session.coinWindowActive;
  const credits = Number(session.credits || 0);
  const secondsLeft = Number(session.secondsLeft || 0);
  const paused = !!session.paused;
  const lastSeen = Number(session.lastSeen || 0);
  const heartbeatFresh =
    lastSeen > 0 &&
    nowSec >= lastSeen &&
    nowSec - lastSeen <= PORTAL_HEARTBEAT_STALE_SEC;

  if (state === "waiting_coin") return coinWindow && heartbeatFresh;
  if (
    coinWindow &&
    credits > 0 &&
    state !== "active" &&
    state !== "paused" &&
    state !== "activating" &&
    state !== "activation_error"
  ) {
    return heartbeatFresh;
  }
  if (
    credits > 0 &&
    !coinWindow &&
    state !== "active" &&
    state !== "paused" &&
    state !== "activating" &&
    state !== "activation_error"
  ) {
    return false;
  }
  if (state === "paused") return secondsLeft > 0;
  if (state === "activating" || state === "activation_error") return secondsLeft > 0;
  if (state === "active") return secondsLeft > 0 || paused;
  return false;
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

const now = 10000;
let passed = 0;
function check(name, fn) {
  try {
    fn();
    passed++;
    console.log(`PASS ${name}`);
  } catch (e) {
    console.error(`FAIL ${name}: ${e.message}`);
    process.exitCode = 1;
  }
}

check("active with stale heartbeat still listed", () => {
  assert(
    isPortalSessionActive(
      {
        sessionState: "active",
        secondsLeft: 600,
        lastSeen: now - 180,
        paused: false,
      },
      now,
    ),
    "should remain visible",
  );
});

check("active expired time removed", () => {
  assert(
    !isPortalSessionActive(
      { sessionState: "active", secondsLeft: 0, lastSeen: now, paused: false },
      now,
    ),
    "should hide",
  );
});

check("paused with time remains without heartbeat", () => {
  assert(
    isPortalSessionActive(
      {
        sessionState: "paused",
        secondsLeft: 120,
        lastSeen: now - 999,
        paused: true,
      },
      now,
    ),
    "paused entitlement",
  );
});

check("waiting_coin requires heartbeat", () => {
  assert(
    !isPortalSessionActive(
      {
        sessionState: "waiting_coin",
        coinWindowActive: true,
        credits: 5,
        lastSeen: now - 180,
      },
      now,
    ),
    "stale waiting_coin hidden",
  );
  assert(
    isPortalSessionActive(
      {
        sessionState: "waiting_coin",
        coinWindowActive: true,
        credits: 5,
        lastSeen: now - 10,
      },
      now,
    ),
    "fresh waiting_coin shown",
  );
});

check("expired never listed", () => {
  assert(
    !isPortalSessionActive(
      { sessionState: "expired", secondsLeft: 0, lastSeen: now },
      now,
    ),
    "expired",
  );
});

check("activating with time remains without heartbeat", () => {
  assert(
    isPortalSessionActive(
      {
        sessionState: "activating",
        secondsLeft: 300,
        lastSeen: now - 200,
      },
      now,
    ),
    "activating entitlement",
  );
});

console.log(`\n${passed} checks passed`);
if (process.exitCode) process.exit(1);
console.log("Active users entitlement regression OK");
