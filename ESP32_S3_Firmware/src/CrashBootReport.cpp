#include "CrashBootReport.h"

#include <esp_system.h>
#include <time.h>

#include "JsonHeap.h"
#include "Logger.h"
#include "NdjsonLedger.h"
#include "SalesTime.h"
#include "StorageManager.h"

namespace {

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW_REBOOT";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "UNKNOWN";
  }
}

bool isInterestingReset(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
    case ESP_RST_SDIO:
    case ESP_RST_SW:
      return true;
    default:
      return false;
  }
}

String crashEventAt() {
  String wall = salesRecordedAtNow();
  if (wall.length() > 0) return wall;

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 50) && (timeinfo.tm_year + 1900) >= 2024) {
    char buf[20];
    if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo) != 0) {
      return String(buf);
    }
  }
  return String("uptime-ms:") + millis();
}

}  // namespace

namespace CrashBootReport {

void reportPreviousReset(StorageManager *storage, Logger *logger) {
  const esp_reset_reason_t reason = esp_reset_reason();
  if (!isInterestingReset(reason)) {
    Serial.printf("[crash-report] boot reset=%s (not recorded)\n",
                  resetReasonName(reason));
    return;
  }

  const String eventAt = crashEventAt();
  const String reasonName = resetReasonName(reason);

  String msg = String("Unexpected reset reason=") + reasonName;
  msg += " date=";
  msg += eventAt;

  Serial.printf("[crash-report] %s\n", msg.c_str());

  if (logger) {
    logger->errorLocal("crash", msg);
  }

  if (!storage) return;

  PsramJsonDocument itemHeap;
  JsonDocument &item = itemHeap.doc();
  item["id"] = millis();
  item["t"] = eventAt;
  item["lvl"] = "ERROR";
  item["type"] = "crash";
  item["msg"] = msg;
  item["resetReason"] = reasonName;
  item["resetCode"] = static_cast<int>(reason);

  const String eventId =
      String("crash:") + reasonName + ":" + String(millis());
  const bool ok = storage->appendHistory(NdjsonLedger::Kind::Logs, eventId,
                                         eventAt, item.as<JsonObjectConst>(),
                                         true);
  Serial.printf("[crash-report] history append %s path=/history/logs\n",
                ok ? "ok" : "failed");
}

}  // namespace CrashBootReport
