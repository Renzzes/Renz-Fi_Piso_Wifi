#pragma once

#include <Arduino.h>

// Wall-clock helpers for sales recorded_at timestamps and date filtering.
// Requires Ethernet link + salesTimeBegin() (NTP) for accurate aggregation.

void salesTimeBegin();

bool salesTimeReady();

String salesRecordedAtNow();

bool salesParseRecordedAt(const char *recordedAt, int &year, int &month, int &day);

bool salesIsToday(const char *recordedAt);

bool salesIsThisWeek(const char *recordedAt);

bool salesIsThisMonth(const char *recordedAt);

bool salesDateWithinLastDays(const char *recordedAt, int days);

void salesLogDiagnostics(int todayAmount, int weekAmount, int monthAmount);
