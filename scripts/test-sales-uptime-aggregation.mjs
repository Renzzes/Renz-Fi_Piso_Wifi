#!/usr/bin/env node
/**
 * Regression tests for sales timestamp aggregation policy.
 * Mirrors SessionManager::aggregateSales undated/uptime-ms behavior.
 *
 * Run: node scripts/test-sales-uptime-aggregation.mjs
 */

function salesParseRecordedAt(recordedAt) {
  if (!recordedAt || recordedAt.length < 10) return null;
  if (recordedAt[4] !== "-" || recordedAt[7] !== "-") return null;
  const y = Number(recordedAt.slice(0, 4));
  const m = Number(recordedAt.slice(5, 7));
  const d = Number(recordedAt.slice(8, 10));
  if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31) return null;
  return { y, m, d };
}

function salesIsUptimeMarker(s) {
  return typeof s === "string" && s.startsWith("uptime-ms:");
}

function salesEffectiveIsoStamp(recordedAt, reportingAt) {
  if (salesParseRecordedAt(reportingAt)) return reportingAt;
  if (salesParseRecordedAt(recordedAt)) return recordedAt;
  return "";
}

function saleIsUptimeUndated(sale) {
  const recordedAt = sale.recorded_at || "";
  const timestamp = sale.timestamp || "";
  return (
    salesIsUptimeMarker(recordedAt) ||
    (!recordedAt && salesIsUptimeMarker(timestamp))
  );
}

function sameYmd(a, b) {
  return a && b && a.y === b.y && a.m === b.m && a.d === b.d;
}

function parseTodayIso(todayIso) {
  return salesParseRecordedAt(todayIso);
}

function inWeek(parsed, today) {
  if (!parsed || !today) return false;
  const a = Date.UTC(parsed.y, parsed.m - 1, parsed.d);
  const b = Date.UTC(today.y, today.m - 1, today.d);
  const dayMs = 86400000;
  const todayDow = new Date(b).getUTCDay(); // 0 Sun
  // Match firmware Monday-start week if possible — approximate: within 6 days back
  const start = b - ((todayDow + 6) % 7) * dayMs;
  return a >= start && a <= b;
}

function inMonth(parsed, today) {
  return parsed && today && parsed.y === today.y && parsed.m === today.m;
}

function aggregateSales(sales, period, { clockReady, todayIso }) {
  const totals = {
    amount: 0,
    sessions: 0,
    undatedAmount: 0,
    undatedSessions: 0,
  };
  const today = clockReady ? parseTodayIso(todayIso) : null;

  for (const sale of sales) {
    const amount = Number(sale.amount || 0);
    const iso = salesEffectiveIsoStamp(sale.recorded_at, sale.reporting_at);
    const parsed = salesParseRecordedAt(iso);

    if (parsed) {
      let match = false;
      if (period === "today") match = sameYmd(parsed, today);
      else if (period === "week") match = inWeek(parsed, today);
      else if (period === "month") match = inMonth(parsed, today);
      if (match) {
        totals.amount += amount;
        totals.sessions++;
      }
      continue;
    }

    if (!saleIsUptimeUndated(sale)) continue;
    totals.undatedAmount += amount;
    totals.undatedSessions++;

    if (clockReady && today) {
      let match = false;
      if (period === "today") match = true;
      else if (period === "week") match = true;
      else if (period === "month") match = true;
      if (match) {
        totals.amount += amount;
        totals.sessions++;
      }
    }
  }
  return totals;
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

const TODAY = "2026-08-11T12:00:00";
const YESTERDAY = "2026-08-10T12:00:00";
const LAST_MONTH = "2026-07-15T12:00:00";
const FUTURE = "2099-01-01T00:00:00";

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

check("ISO today → Today", () => {
  const t = aggregateSales(
    [{ recorded_at: TODAY, amount: 10 }],
    "today",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 10 && t.sessions === 1, JSON.stringify(t));
});

check("ISO yesterday not in Today", () => {
  const t = aggregateSales(
    [{ recorded_at: YESTERDAY, amount: 10 }],
    "today",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 0, JSON.stringify(t));
});

check("ISO week includes yesterday", () => {
  const t = aggregateSales(
    [{ recorded_at: YESTERDAY, amount: 7 }],
    "week",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 7, JSON.stringify(t));
});

check("ISO month includes today", () => {
  const t = aggregateSales(
    [{ recorded_at: TODAY, amount: 5 }],
    "month",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 5, JSON.stringify(t));
});

check("ISO last month not in Month", () => {
  const t = aggregateSales(
    [{ recorded_at: LAST_MONTH, amount: 50 }],
    "month",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 0, JSON.stringify(t));
});

check("uptime marker does not disappear when clock ready", () => {
  const t = aggregateSales(
    [{ recorded_at: "uptime-ms:12345", amount: 20 }],
    "today",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 20 && t.undatedAmount === 20, JSON.stringify(t));
});

check("uptime marker visible as undated when clock unavailable", () => {
  const t = aggregateSales(
    [{ recorded_at: "uptime-ms:999", amount: 15 }],
    "today",
    { clockReady: false, todayIso: TODAY },
  );
  assert(t.amount === 0 && t.undatedAmount === 15, JSON.stringify(t));
});

check("multiple COIN transactions sum", () => {
  const t = aggregateSales(
    [
      { recorded_at: TODAY, amount: 5 },
      { recorded_at: "uptime-ms:1", amount: 10 },
      { recorded_at: "uptime-ms:2", amount: 10 },
    ],
    "today",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 25 && t.sessions === 3, JSON.stringify(t));
});

check("empty sales database", () => {
  const t = aggregateSales([], "today", { clockReady: true, todayIso: TODAY });
  assert(t.amount === 0 && t.undatedAmount === 0, JSON.stringify(t));
});

check("malformed timestamp ignored (not undated)", () => {
  const t = aggregateSales(
    [{ recorded_at: "not-a-date", amount: 99 }],
    "today",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 0 && t.undatedAmount === 0, JSON.stringify(t));
});

check("future ISO timestamp not in Today", () => {
  const t = aggregateSales(
    [{ recorded_at: FUTURE, amount: 40 }],
    "today",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 0, JSON.stringify(t));
});

check("reporting_at preferred over uptime recorded_at", () => {
  const t = aggregateSales(
    [
      {
        recorded_at: "uptime-ms:1",
        reporting_at: TODAY,
        amount: 12,
      },
    ],
    "today",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 12 && t.undatedAmount === 0, JSON.stringify(t));
});

check("wall clock becomes available later → attributed", () => {
  const sales = [{ recorded_at: "uptime-ms:42", amount: 8 }];
  const before = aggregateSales(sales, "today", {
    clockReady: false,
    todayIso: TODAY,
  });
  const after = aggregateSales(sales, "today", {
    clockReady: true,
    todayIso: TODAY,
  });
  assert(before.amount === 0 && before.undatedAmount === 8, JSON.stringify(before));
  assert(after.amount === 8 && after.undatedAmount === 8, JSON.stringify(after));
});

check("ISO sale still aggregates exactly as before alongside undated", () => {
  const t = aggregateSales(
    [
      { recorded_at: TODAY, amount: 3 },
      { recorded_at: YESTERDAY, amount: 4 },
    ],
    "today",
    { clockReady: true, todayIso: TODAY },
  );
  assert(t.amount === 3, JSON.stringify(t));
});

console.log(`\n${passed} checks passed`);
if (process.exitCode) {
  console.error("Sales aggregation regression FAILED");
  process.exit(1);
}
console.log("Sales aggregation regression OK");
