#pragma once

#include <Arduino.h>

class InstallationStateManager;

// Wall-clock helpers for sales recorded_at timestamps and date filtering.
// NTP starts only after installation reaches provisioned/ready.

void salesTimeBindInstallation(InstallationStateManager *installation);

void salesTimeBegin();

bool salesTimeReady();

String salesRecordedAtNow();

bool salesParseRecordedAt(const char *recordedAt, int &year, int &month, int &day);

/** True when stamp is the offline fallback form "uptime-ms:<millis>". */
bool salesIsUptimeMarker(const char *recordedAt);

bool salesIsToday(const char *recordedAt);

bool salesIsThisWeek(const char *recordedAt);

bool salesIsThisMonth(const char *recordedAt);

bool salesDateWithinLastDays(const char *recordedAt, int days);

/**
 * Prefer reporting_at (ISO) when present, else recorded_at when ISO-parseable.
 * Uptime markers return empty — callers must use undated attribution policy.
 */
String salesEffectiveIsoStamp(const char *recordedAt, const char *reportingAt);

void salesLogDiagnostics(int todayAmount, int weekAmount, int monthAmount);

/** Add seconds to an ISO local stamp "YYYY-MM-DDTHH:MM:SS". Empty on parse failure. */
String salesAddSecondsToIso(const String &isoStamp, uint32_t seconds);

/**
 * Seconds remaining until ISO local stamp.
 * Returns -1 if stamp unparseable or wall clock not ready; 0 if already past.
 */
long salesSecondsUntilIso(const String &isoStamp);
