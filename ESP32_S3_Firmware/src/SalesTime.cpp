#include "SalesTime.h"

#include <cstring>
#include <limits.h>
#include <time.h>

#include "InstallationStateManager.h"

namespace {

InstallationStateManager *s_installation = nullptr;
bool s_timeConfigured = false;
bool s_loggedNtpDeferred = false;

bool installationAllowsNtp() {
  return s_installation && s_installation->isReady();
}

bool readNowYmd(int &year, int &month, int &day) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  year  = timeinfo.tm_year + 1900;
  month = timeinfo.tm_mon + 1;
  day   = timeinfo.tm_mday;
  return true;
}

time_t ymdToDayStart(int year, int month, int day) {
  struct tm timeinfo = {};
  timeinfo.tm_year  = year - 1900;
  timeinfo.tm_mon   = month - 1;
  timeinfo.tm_mday  = day;
  timeinfo.tm_hour  = 12;
  timeinfo.tm_min   = 0;
  timeinfo.tm_sec   = 0;
  return mktime(&timeinfo);
}

}  // namespace

void salesTimeBindInstallation(InstallationStateManager *installation) {
  s_installation = installation;
}

void salesTimeBegin() {
  if (s_timeConfigured) return;

  if (!installationAllowsNtp()) {
    if (!s_loggedNtpDeferred) {
      Serial.println(
          "[sales] NTP deferred until setup complete (provisioned/ready)");
      s_loggedNtpDeferred = true;
    }
    return;
  }

  setenv("TZ", "Asia/Manila", 1);
  tzset();
  configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
  s_timeConfigured = true;
  s_loggedNtpDeferred = false;
  Serial.println("[sales] NTP time sync started (Asia/Manila)");
}

bool salesTimeReady() {
  if (!installationAllowsNtp()) return false;
  salesTimeBegin();
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  return (timeinfo.tm_year + 1900) >= 2024;
}

String salesRecordedAtNow() {
  if (!installationAllowsNtp()) return "";
  salesTimeBegin();
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 500)) return "";
  if (timeinfo.tm_year + 1900 < 2024) return "";

  char buf[20];
  if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo) == 0) {
    return "";
  }
  return String(buf);
}

bool salesParseRecordedAt(const char *recordedAt, int &year, int &month, int &day) {
  if (!recordedAt || recordedAt[0] == '\0') return false;
  if (strlen(recordedAt) < 10) return false;
  if (recordedAt[4] != '-' || recordedAt[7] != '-') return false;

  year  = 0;
  month = 0;
  day   = 0;
  if (sscanf(recordedAt, "%d-%d-%d", &year, &month, &day) != 3) return false;
  if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) return false;
  return true;
}

bool salesIsUptimeMarker(const char *recordedAt) {
  return recordedAt && recordedAt[0] != '\0' &&
         strncmp(recordedAt, "uptime-ms:", 10) == 0;
}

String salesEffectiveIsoStamp(const char *recordedAt, const char *reportingAt) {
  int y = 0, m = 0, d = 0;
  if (reportingAt && salesParseRecordedAt(reportingAt, y, m, d)) {
    return String(reportingAt);
  }
  if (recordedAt && salesParseRecordedAt(recordedAt, y, m, d)) {
    return String(recordedAt);
  }
  return String();
}

bool salesIsToday(const char *recordedAt) {
  int sy, sm, sd, ty, tm, td;
  if (!salesParseRecordedAt(recordedAt, sy, sm, sd)) return false;
  if (!readNowYmd(ty, tm, td)) return false;
  return sy == ty && sm == tm && sd == td;
}

bool salesIsThisMonth(const char *recordedAt) {
  int sy, sm, sd, ty, tm, td;
  if (!salesParseRecordedAt(recordedAt, sy, sm, sd)) return false;
  if (!readNowYmd(ty, tm, td)) return false;
  return sy == ty && sm == tm;
}

bool salesIsThisWeek(const char *recordedAt) {
  int sy, sm, sd;
  if (!salesParseRecordedAt(recordedAt, sy, sm, sd)) return false;

  struct tm nowInfo;
  if (!getLocalTime(&nowInfo)) return false;

  const int daysFromMonday = (nowInfo.tm_wday + 6) % 7;
  struct tm weekStart     = nowInfo;
  weekStart.tm_mday -= daysFromMonday;
  weekStart.tm_hour = 0;
  weekStart.tm_min  = 0;
  weekStart.tm_sec  = 0;
  const time_t weekStartTs = mktime(&weekStart);

  struct tm weekEnd = weekStart;
  weekEnd.tm_mday += 6;
  weekEnd.tm_hour = 23;
  weekEnd.tm_min  = 59;
  weekEnd.tm_sec  = 59;
  const time_t weekEndTs = mktime(&weekEnd);

  const time_t saleTs = ymdToDayStart(sy, sm, sd);
  return saleTs >= weekStartTs && saleTs <= weekEndTs;
}

bool salesDateWithinLastDays(const char *recordedAt, int days) {
  if (days < 1) return false;

  int sy, sm, sd;
  if (!salesParseRecordedAt(recordedAt, sy, sm, sd)) return false;

  struct tm nowInfo;
  if (!getLocalTime(&nowInfo)) return false;

  struct tm cutoff = nowInfo;
  cutoff.tm_mday -= (days - 1);
  cutoff.tm_hour = 0;
  cutoff.tm_min  = 0;
  cutoff.tm_sec  = 0;
  const time_t cutoffTs = mktime(&cutoff);
  const time_t saleTs   = ymdToDayStart(sy, sm, sd);
  return saleTs >= cutoffTs;
}

void salesLogDiagnostics(int todayAmount, int weekAmount, int monthAmount) {
  Serial.printf("[sales] today=%d week=%d month=%d\n", todayAmount, weekAmount,
                monthAmount);
}

String salesAddSecondsToIso(const String &isoStamp, uint32_t seconds) {
  int year, month, day, hour, minute, second;
  if (sscanf(isoStamp.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour,
             &minute, &second) != 6) {
    return "";
  }
  struct tm value = {};
  value.tm_year = year - 1900;
  value.tm_mon = month - 1;
  value.tm_mday = day;
  value.tm_hour = hour;
  value.tm_min = minute;
  value.tm_sec = second;
  const time_t epoch = mktime(&value) + static_cast<time_t>(seconds);
  struct tm output;
  localtime_r(&epoch, &output);
  char buffer[20];
  if (strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &output) == 0) {
    return "";
  }
  return String(buffer);
}

long salesSecondsUntilIso(const String &isoStamp) {
  int year, month, day, hour, minute, second;
  if (sscanf(isoStamp.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour,
             &minute, &second) != 6) {
    return -1;
  }
  struct tm value = {};
  value.tm_year = year - 1900;
  value.tm_mon = month - 1;
  value.tm_mday = day;
  value.tm_hour = hour;
  value.tm_min = minute;
  value.tm_sec = second;
  const time_t target = mktime(&value);
  const time_t now = time(nullptr);
  if (now < 1704067200) return -1;
  if (target <= now) return 0;
  const time_t remaining = target - now;
  return remaining > LONG_MAX ? LONG_MAX : static_cast<long>(remaining);
}
