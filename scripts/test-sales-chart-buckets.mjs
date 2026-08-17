#!/usr/bin/env node
/**
 * Regression tests for bounded sales-chart aggregation.
 * Mirrors SessionManager::salesChart after the DMA hardening:
 *   read records → bucket into fixed int32 amounts[days] → release the record.
 *
 * Run: node scripts/test-sales-chart-buckets.mjs
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

function pad2(n) {
  return String(n).padStart(2, "0");
}

function addDaysUtc(isoDay, delta) {
  const d = new Date(`${isoDay}T00:00:00Z`);
  d.setUTCDate(d.getUTCDate() + delta);
  return `${d.getUTCFullYear()}-${pad2(d.getUTCMonth() + 1)}-${pad2(d.getUTCDate())}`;
}

function buildDateKeys(todayIso, days) {
  const keys = [];
  for (let i = 0; i < days; i++) {
    keys.push(addDaysUtc(todayIso, -(days - 1 - i)));
  }
  return keys;
}

function aggregateChart(sales, { todayIso, days, clockReady }) {
  const dateKeys = buildDateKeys(todayIso, days);
  const amounts = new Array(days).fill(0);
  let matched = 0;
  for (const sale of sales) {
    const amount = Number(sale.amount || 0);
    const iso = salesEffectiveIsoStamp(sale.recorded_at, sale.reporting_at);
    let dateKey = "";
    if (iso.length >= 10) dateKey = iso.slice(0, 10);
    else if (clockReady && saleIsUptimeUndated(sale)) dateKey = dateKeys[days - 1];
    else continue;
    const idx = dateKeys.indexOf(dateKey);
    if (idx < 0) continue;
    amounts[idx] += amount;
    matched += 1;
  }
  return { labels: dateKeys, data: amounts, matched };
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

const today = "2026-08-17";
const inside180 = addDaysUtc(today, -100);
const sales = [
  { amount: 1, recorded_at: "2026-08-17T10:00:00" },
  { amount: 5, recorded_at: "2026-08-16T10:00:00" },
  { amount: 10, recorded_at: `${inside180}T10:00:00` },
  { amount: 2, recorded_at: "uptime-ms:123", timestamp: "uptime-ms:123" },
];

const seven = aggregateChart(sales, { todayIso: today, days: 7, clockReady: true });
assert(seven.labels.length === 7, "7-day chart has 7 labels");
assert(seven.labels[6] === today, "last 7-day bucket is today");
assert(seven.data[6] === 3, "today + undated uptime coin (1+2)");
assert(seven.data[5] === 5, "yesterday");
assert(seven.data.reduce((a, b) => a + b, 0) === 8, "old sale is outside 7 days");

const monthly = aggregateChart(sales, { todayIso: today, days: 180, clockReady: true });
assert(monthly.labels.length === 180, "180-day chart has 180 labels");
assert(monthly.labels[179] === today, "last 180-day bucket is today");
assert(monthly.data[179] === 3, "today + undated");
const oldIdx = monthly.labels.indexOf(inside180);
assert(oldIdx >= 0, "day -100 is inside 180 days");
assert(monthly.data[oldIdx] === 10, "older sale lands in its day bucket");
assert(
  monthly.data.reduce((a, b) => a + b, 0) === 18,
  "all four sales match in 180-day window",
);

// Working set is O(days), not O(sales * days) JSON objects.
assert(monthly.data.length === 180, "bounded amounts[]");
assert(monthly.labels.every((k) => k.length === 10), "YYYY-MM-DD keys");

console.log("test-sales-chart-buckets: ok");
