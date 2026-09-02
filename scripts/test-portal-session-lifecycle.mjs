/**
 * Headless lifecycle test for the production captive portal bundle.
 *
 * Loads Final_Build_Portal/renzfi-app.js — the generated MikroTik upload
 * artifact — only after verifying it matches portal/ (URL-substituted).
 * Stale Final_Build_Portal fails before any UI assertions run.
 *
 *   waiting payment -> coin window -> done paying -> activating ->
 *   connected -> paused -> connected -> expired -> waiting payment
 */
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const BUNDLE = join(root, "Final_Build_Portal", "renzfi-app.js");
const MAC = "AA:BB:CC:DD:EE:FF";
const IP = "10.10.10.50";

function assertGeneratedMatchesPortalSource() {
  const check = join(root, "scripts", "check-captive-portal-source-sync.mjs");
  const result = spawnSync(process.execPath, [check], {
    cwd: root,
    encoding: "utf8",
    env: process.env,
  });
  if (result.stdout) process.stdout.write(result.stdout);
  if (result.stderr) process.stderr.write(result.stderr);
  if (result.status !== 0) {
    throw new Error(
      "Generated Final_Build_Portal / deployment portal is stale or missing.\n" +
        "Edit only portal/, then run:\n" +
        "  RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal\n" +
        "  OR scripts\\export-captive-portal.bat\n" +
        "Refusing to test a stale artifact.",
    );
  }
  if (!existsSync(BUNDLE)) {
    throw new Error(`Missing generated bundle: ${BUNDLE}`);
  }
}

assertGeneratedMatchesPortalSource();

// Checks run the instant the scenario reaches them — the fake appliance state
// keeps moving, so a deferred assertion would inspect the wrong moment.
const results = [];
function check(name, fn) {
  try {
    fn();
    results.push({ name, ok: true });
    console.log(`  PASS  ${name}`);
  } catch (err) {
    results.push({ name, ok: false });
    console.log(`  FAIL  ${name}`);
    console.log(
      err.message
        .split("\n")
        .map((line) => `        ${line}`)
        .join("\n"),
    );
  }
}

// ── controllable clock ───────────────────────────────────────────────────────
let now = 1_700_000_000_000;
let nextTimerId = 1;
const intervals = new Map();
const timeouts = new Map();

const clock = {
  setInterval(fn, ms) {
    const id = nextTimerId++;
    intervals.set(id, { fn, ms: Math.max(1, ms | 0), next: now + Math.max(1, ms | 0) });
    return id;
  },
  clearInterval(id) {
    intervals.delete(id);
  },
  setTimeout(fn, ms) {
    const id = nextTimerId++;
    timeouts.set(id, { fn, at: now + Math.max(0, ms | 0) });
    return id;
  },
  clearTimeout(id) {
    timeouts.delete(id);
  },
};

const flushMicrotasks = () =>
  new Promise((resolve) => setImmediate(resolve));

/** Advances virtual time in 100 ms slices, running timers and pending promises. */
async function advance(ms) {
  const target = now + ms;
  while (now < target) {
    now = Math.min(target, now + 100);
    for (const [id, t] of [...timeouts]) {
      if (t.at <= now) {
        timeouts.delete(id);
        t.fn();
      }
    }
    for (const t of [...intervals.values()]) {
      while (t.next <= now) {
        t.next += t.ms;
        t.fn();
      }
    }
    await flushMicrotasks();
  }
  await flushMicrotasks();
}

async function settle(rounds = 12) {
  for (let i = 0; i < rounds; i += 1) await flushMicrotasks();
}

// ── DOM stub ─────────────────────────────────────────────────────────────────
const elements = new Map();

function makeElement(id) {
  const classes = new Set();
  const listeners = {};
  const attrs = {};
  const el = {
    id,
    textContent: "",
    innerHTML: "",
    hidden: false,
    disabled: false,
    value: "",
    title: "",
    src: "",
    currentTime: 0,
    style: {},
    classList: {
      add: (c) => classes.add(c),
      remove: (c) => classes.delete(c),
      contains: (c) => classes.has(c),
      toggle: (c, force) => {
        const on = force === undefined ? !classes.has(c) : Boolean(force);
        if (on) classes.add(c);
        else classes.delete(c);
        return on;
      },
    },
    setAttribute: (k, v) => {
      attrs[k] = String(v);
    },
    getAttribute: (k) => (k in attrs ? attrs[k] : null),
    removeAttribute: (k) => {
      delete attrs[k];
    },
    addEventListener: (type, fn) => {
      (listeners[type] || (listeners[type] = [])).push(fn);
    },
    removeEventListener: () => {},
    querySelector: (sel) =>
      sel === ".action-label" ? element(`${id}:label`) : null,
    closest: () => null,
    play: () => Promise.resolve(),
    pause: () => {},
    load: () => {},
    fire(type, event) {
      (listeners[type] || []).forEach((fn) =>
        fn(Object.assign({ target: el, preventDefault() {} }, event)),
      );
    },
  };
  return el;
}

function element(id) {
  if (!elements.has(id)) elements.set(id, makeElement(id));
  return elements.get(id);
}

const documentStub = {
  readyState: "complete",
  body: { style: {} },
  getElementById: (id) => element(id),
  querySelector: (sel) => {
    if (sel === "#ratesModal .rates-list") return element("ratesList");
    if (sel === ".voucher-card") return element("voucherCard");
    return null; // ".modal:not([hidden])" — nothing left open
  },
  querySelectorAll: () => [],
  addEventListener: () => {},
};

// ── fake ESP32 ───────────────────────────────────────────────────────────────
const PAUSE_LIMIT = 3;

function freshSession() {
  return {
    macAddress: MAC,
    ipAddress: IP,
    sessionId: "test-session",
    credits: 0,
    insertedAmount: 0,
    purchasedMinutes: 0,
    secondsLeft: 0,
    paused: false,
    connected: false,
    coinWindowActive: false,
    coinWindowRemaining: 0,
    sessionState: "idle",
    activationError: false,
    pausesUsed: 0,
    source: "portal",
  };
}

const server = {
  session: freshSession(),
  requests: [],
  failTerminate: false,
  failPauseWith: null,
};

/** Mirrors PortalSessionManager::enrichSessionCapabilities. */
function withCapabilities(s) {
  const ended = s.sessionState === "expiring" || s.sessionState === "expired";
  const pausesRemaining = Math.max(0, PAUSE_LIMIT - (s.pausesUsed || 0));
  return Object.assign({}, s, {
    pausesRemaining,
    pauseLimit: PAUSE_LIMIT,
    canInsertCoin: s.sessionState !== "expiring",
    canPause:
      !ended &&
      !s.coinWindowActive &&
      !s.paused &&
      s.secondsLeft > 0 &&
      s.sessionState === "active" &&
      pausesRemaining > 0,
    canResume: !ended && s.paused && s.secondsLeft > 0,
    canTerminate: !ended && (s.secondsLeft > 0 || s.paused),
    canReconnect: false,
    timerRunning:
      s.sessionState === "active" && !s.paused && s.secondsLeft > 0,
  });
}

function ok(data, status = 200, code = "") {
  return Promise.resolve({
    ok: status < 400,
    status,
    json: () =>
      Promise.resolve(
        status < 400
          ? { success: true, data }
          : { success: false, error: data, code },
      ),
  });
}

function fetchStub(url, init) {
  const path = String(url).replace(/^https?:\/\/[^/]+/, "").split("?")[0];
  server.requests.push(path);
  const s = server.session;

  switch (path) {
    case "/api/portal/branding":
      return ok({ hasCustomBanner: false, hasCustomMusic: false, revision: 1 });

    case "/api/portal/session":
    case "/api/portal/heartbeat":
      return ok(withCapabilities(s));

    case "/api/portal/start-coin-session":
      s.coinWindowActive = true;
      s.coinWindowRemaining = 60;
      s.sessionState = "waiting_coin";
      return ok(withCapabilities(s));

    case "/api/portal/done-paying":
      s.coinWindowActive = false;
      s.coinWindowRemaining = 0;
      s.secondsLeft += s.purchasedMinutes * 60;
      s.grantedSeconds = s.secondsLeft;
      s.purchasedMinutes = 0;
      s.credits = 0;
      s.insertedAmount = 0;
      s.sessionState = "activating";
      s.connected = false;
      return ok(withCapabilities(s));

    case "/api/portal/pause":
      if (server.failPauseWith) {
        return ok(server.failPauseWith, 409, "PAUSE_LIMIT_REACHED");
      }
      s.pausesUsed += 1;
      s.paused = true;
      s.connected = false;
      s.sessionState = "paused";
      return ok(withCapabilities(s));

    case "/api/portal/resume":
      s.paused = false;
      s.connected = true;
      s.sessionState = "active";
      return ok(withCapabilities(s));

    case "/api/portal/terminate":
      if (server.failTerminate) {
        return ok("Router unreachable", 503, "ROUTER_UNAVAILABLE");
      }
      Object.assign(s, freshSession());
      return ok(withCapabilities(s));

    case "/api/portal/cancel-modal":
      s.coinWindowActive = false;
      s.coinWindowRemaining = 0;
      if (s.secondsLeft > 0) {
        s.sessionState = "active";
        if (s.hadRouterAuth) s.connected = true;
      } else if (s.sessionState === "waiting_coin") {
        s.sessionState = "idle";
      }
      return ok(withCapabilities(s));

    case "/api/portal/rates":
      return ok({ rates: [] });

    default:
      return ok("not found", 404);
  }
}

// ── EventSource stub ─────────────────────────────────────────────────────────
const sse = { listeners: {} };

class EventSourceStub {
  constructor() {
    sse.listeners = {};
  }
  addEventListener(type, fn) {
    (sse.listeners[type] || (sse.listeners[type] = [])).push(fn);
  }
  close() {}
}

function push(eventName, sessionLike) {
  const handlers = sse.listeners[eventName] || [];
  const data = JSON.stringify(withCapabilities(sessionLike));
  handlers.forEach((fn) => fn({ data }));
}

// ── boot the bundle ──────────────────────────────────────────────────────────
const store = new Map();
const sandbox = {
  console,
  document: documentStub,
  localStorage: {
    getItem: (k) => (store.has(k) ? store.get(k) : null),
    setItem: (k, v) => store.set(k, String(v)),
    removeItem: (k) => store.delete(k),
  },
  fetch: fetchStub,
  EventSource: EventSourceStub,
  Promise,
  JSON,
  Math,
  Number,
  String,
  Boolean,
  Array,
  Object,
  Error,
  isNaN,
  encodeURIComponent,
  setInterval: clock.setInterval,
  clearInterval: clock.clearInterval,
  setTimeout: clock.setTimeout,
  clearTimeout: clock.clearTimeout,
};
sandbox.window = sandbox;
sandbox.self = sandbox;
sandbox.location = { origin: "http://10.10.10.2" };
sandbox.window.location = sandbox.location;
sandbox.Date = new Proxy(Date, {
  construct: (T, args) => (args.length ? new T(...args) : new T(now)),
  get: (T, prop) => (prop === "now" ? () => now : Reflect.get(T, prop)),
});

element("macAddress").textContent = MAC;
element("ipAddress").textContent = IP;

const context = vm.createContext(sandbox);
vm.runInContext(readFileSync(BUNDLE, "utf8"), context, { filename: BUNDLE });

const timerText = () => element("mainTimer").textContent;
const statusText = () => element("connectionStatus").textContent;
const seconds = () => {
  const [h, m, s] = timerText().split(":").map(Number);
  return h * 3600 + m * 60 + s;
};

/** The repaint loop lands on a 1 s boundary, so allow a single second of slack. */
function assertNear(actual, expected, what) {
  assert.ok(
    Math.abs(actual - expected) <= 1,
    `${what}: expected ~${expected}, got ${actual}`,
  );
}

// ── scenarios ────────────────────────────────────────────────────────────────
await settle();

check("boots into Waiting Payment with a zeroed countdown", () => {
  assert.equal(sandbox.window.RenzFiPortalReady, true);
  assert.equal(timerText(), "00:00:00");
});

// 1. Coin window: credit must land from the SSE push, with no HTTP poll.
element("insertCoinBtn").fire("click");
await settle();

check("insert coin opens the firmware coin window", () => {
  assert.equal(server.session.coinWindowActive, true);
  assert.equal(element("coinModal").hidden, false);
});

const coinSecs = () => Number(element("coinCountdown").textContent);

await advance(5000);
const afterFive = coinSecs();

check("coin modal countdown decreases monotonically (60→~55)", () => {
  assertNear(afterFive, 55, "coin countdown after 5 s");
  assert.ok(afterFive < 60, "countdown must fall below the insert timeout");
});

const beforeStaleCoinPoll = coinSecs();
server.session.coinWindowRemaining = beforeStaleCoinPoll + 1; // older snapshot
push("portal.session.updated", server.session);
await settle();

check("a stale coin-window poll cannot increase the displayed countdown", () => {
  assert.ok(
    coinSecs() <= beforeStaleCoinPoll,
    `coin countdown jumped up: ${beforeStaleCoinPoll} -> ${coinSecs()}`,
  );
});

check("Promo Grant / voucher-time row is not in the portal shell", () => {
  const html = readFileSync(join(root, "Final_Build_Portal", "login.html"), "utf8");
  assert.ok(!html.includes("Promo Grant"));
  assert.ok(!html.includes("coinVoucherTime"));
  assert.match(html, />\s*Time:\s*</);
  assert.match(html, /Total Amount:/);
});

const requestsBeforeCoin = server.requests.length;
server.session.credits = 1;
server.session.insertedAmount = 1;
server.session.purchasedMinutes = 5;
server.session.coinWindowRemaining = 60;
push("portal.coin.credit", server.session);
await settle();

check("a new coin legitimately resets the coin-window countdown", () => {
  assertNear(coinSecs(), 60, "coin countdown after accepted pulse");
});

check("coin credit renders from the SSE push without an extra request", () => {
  assert.equal(element("credits").textContent, "\u20B11.00");
  assert.equal(element("insertedAmount").textContent, "1.00");
  assert.equal(element("coinTime").textContent, "5m");
  assert.equal(server.requests.length, requestsBeforeCoin);
});

server.session.credits = 2;
server.session.insertedAmount = 2;
server.session.purchasedMinutes = 10;
server.session.coinWindowRemaining = 60;
push("portal.coin.credit", server.session);
await settle();

check("a second coin accumulates credit and time", () => {
  assert.equal(element("credits").textContent, "\u20B12.00");
  assert.equal(element("coinTime").textContent, "10m");
});

check("a second coin also resets the coin-window countdown without oscillation", () => {
  assertNear(coinSecs(), 60, "coin countdown after second pulse");
});

await advance(2000);
const afterSecondTick = coinSecs();
server.session.coinWindowRemaining = afterSecondTick + 1;
push("portal.session.updated", server.session);
await settle();

check("post-reset stale poll still cannot rewind the coin countdown", () => {
  assert.ok(
    coinSecs() <= afterSecondTick,
    `coin countdown jumped up: ${afterSecondTick} -> ${coinSecs()}`,
  );
});

// 2. Done paying: activating, then connected via the router worker.
element("donePayingBtn").fire("click");
await settle();

check("done paying enters Activating and does not start the clock", () => {
  assert.equal(server.session.sessionState, "activating");
  assert.equal(statusText(), "Activating…");
  assert.equal(seconds(), 600);
});

const secondsWhileActivating = seconds();
await advance(3000);

check("the countdown stays frozen while activation is in flight", () => {
  assert.equal(seconds(), secondsWhileActivating);
});

check("done paying HTTP snapshot cannot display Connected", () => {
  assert.equal(server.session.connected, false);
  assert.equal(statusText(), "Activating…");
});

server.session.sessionState = "active";
server.session.connected = false;
push("portal.session.updated", server.session);
await settle();

check("sessionState=active without connected is never Connected", () => {
  assert.notEqual(statusText(), "Connected");
});

server.session.sessionState = "activating";
server.session.connected = false;
push("portal.session.updated", server.session);
await settle();

server.session.sessionState = "active";
server.session.connected = true;
push("portal.session.connected", server.session);
await settle();

check("router authorization flips the portal to Connected", () => {
  assert.equal(statusText(), "Connected");
});

// 3. Countdown: decreases smoothly, and a stale payload cannot push it back up.
await advance(5000);

check("the countdown advances in real time once connected", () => {
  assertNear(seconds(), 595, "countdown after 5 s connected");
});

const beforeStalePayload = seconds();
push("portal.session.updated", server.session); // still says 600 — 5 s stale
await settle();

check("a stale payload never rewinds the visible countdown", () => {
  assert.ok(
    seconds() <= beforeStalePayload,
    `countdown jumped up: ${beforeStalePayload} -> ${seconds()}`,
  );
});

// Keep the fake appliance honest so heartbeats agree with the browser.
server.session.secondsLeft = seconds();
const beforeResync = seconds();
await advance(4000);

check("the countdown keeps falling after a resync", () => {
  assertNear(seconds(), beforeResync - 4, "countdown 4 s after resync");
});

// A genuine top-up raises grantedSeconds (Add Time). A stale secondsLeft
  // GET/SSE without that increase must not move the deadline later.
  server.session.secondsLeft = 1200;
  server.session.grantedSeconds = 1200;
  push("portal.session.updated", server.session);
await settle();

check("added time is applied immediately", () => {
  assert.equal(seconds(), 1200);
});

// 4. Pause freezes the countdown and drops the connection.
element("pauseButton").fire("click");
await settle();
const pausedAt = seconds();
await advance(6000);

check("pause freezes the countdown", () => {
  assert.equal(seconds(), pausedAt);
  assert.equal(statusText(), "Paused");
});

check("pause is reported as used against the session budget", () => {
  assert.equal(element("pauseButtonText").textContent, "RESUME");
  assert.equal(server.session.pausesUsed, 1);
});

element("pauseButton").fire("click");
await settle();
const resumedAt = seconds();
await advance(3000);

check("resume continues from the remaining time, without a reset", () => {
  assertNear(resumedAt, pausedAt, "countdown at the moment of resume");
  assertNear(seconds(), pausedAt - 3, "countdown 3 s after resume");
  assert.equal(statusText(), "Connected");
});

check("the pause budget is shown on the button", () => {
  assert.equal(element("pauseButtonText").textContent, "PAUSE (2 left)");
});

// 5. Pause limit is a rule, not an outage.
server.failPauseWith = "Pause limit reached for this session";
server.session.pausesUsed = PAUSE_LIMIT;
element("pauseButton").fire("click");
await settle();

check("a refused pause explains the limit instead of an outage banner", () => {
  assert.match(element("sessionNotice").textContent, /all 3 pauses/i);
  assert.equal(element("serviceNotice").hidden, true);
});
server.failPauseWith = null;

// 6. Terminate: confirmation shows the cost, failure is not silent.
element("terminateBtn").fire("click");
await settle();

check("the terminate dialog states exactly what is forfeited", () => {
  assert.equal(element("terminateModal").hidden, false);
  assert.equal(element("terminateRemaining").textContent, timerText());
  assert.equal(element("terminateCredits").textContent, "\u20B10.00");
});

server.failTerminate = true;
element("confirmTerminateBtn").fire("click");
await settle();

check("a failed terminate keeps the session and reports the reason", () => {
  assert.equal(element("terminateModal").hidden, false);
  assert.equal(element("terminateStatus").hidden, false);
  assert.match(element("terminateStatus").textContent, /not changed/i);
  assert.ok(seconds() > 0);
  assert.equal(element("confirmTerminateBtn").disabled, false);
});

server.failTerminate = false;
element("confirmTerminateBtn").fire("click");
await settle();

check("a successful terminate clears the session and closes the dialog", () => {
  assert.equal(element("terminateModal").hidden, true);
  assert.equal(seconds(), 0);
  assert.equal(server.session.secondsLeft, 0);
});

check("terminating returns the portal to Waiting Payment", () => {
  assert.equal(element("insertCoinBtn").hidden, false);
  assert.equal(element("terminateBtn").hidden, true);
});

// 7. Natural expiry: the countdown floors at zero and never goes negative.
Object.assign(server.session, freshSession(), {
  secondsLeft: 3,
  sessionState: "active",
  connected: true,
});
push("portal.session.updated", server.session);
await settle();
await advance(10_000);

check("the countdown floors at zero instead of going negative", () => {
  assert.equal(timerText(), "00:00:00");
  assert.ok(seconds() >= 0);
});

Object.assign(server.session, freshSession());
push("portal.session.expired", server.session);
await settle();

check("expiry restores Waiting Payment and the Insert Coin button", () => {
  assert.equal(element("insertCoinBtn").hidden, false);
  assert.equal(element("insertCoinBtn:label").textContent, "INSERT COIN");
  assert.equal(element("terminateBtn").hidden, true);
  assert.equal(timerText(), "00:00:00");
});

// ── report ───────────────────────────────────────────────────────────────────
const failed = results.filter((r) => !r.ok).length;
console.log(
  `[test:portal-lifecycle] ${results.length - failed}/${results.length} checks passed`,
);
if (failed > 0) process.exit(1);
