#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Lightweight persisted installation session (wizard context beyond state alone).
struct InstallationSession {
  String sessionId;
  uint32_t startedAt   = 0;
  uint32_t lastActivity = 0;
  String installerName;
  String deviceId;
  String resumeToken;
  bool isRecovery = false;
  uint16_t attempt = 0;

  bool active() const { return sessionId.length() > 0 && attempt > 0; }

  void toJson(JsonObject obj) const {
    obj["sessionId"]     = sessionId;
    obj["startedAt"]     = startedAt;
    obj["lastActivity"]  = lastActivity;
    obj["installerName"] = installerName;
    obj["deviceId"]      = deviceId;
    if (resumeToken.length() > 0) {
      obj["resumeToken"] = resumeToken;
    }
    obj["isRecovery"] = isRecovery;
    obj["attempt"]    = attempt;
  }

  void fromJson(JsonObjectConst obj) {
    sessionId     = obj["sessionId"] | "";
    startedAt     = obj["startedAt"] | 0U;
    lastActivity  = obj["lastActivity"] | 0U;
    installerName = obj["installerName"] | "";
    deviceId      = obj["deviceId"] | "";
    resumeToken   = obj["resumeToken"] | "";
    isRecovery    = obj["isRecovery"] | false;
    attempt       = obj["attempt"] | 0U;
  }

  void clear() {
    sessionId     = "";
    startedAt     = 0;
    lastActivity  = 0;
    installerName = "";
    deviceId      = "";
    resumeToken   = "";
    isRecovery    = false;
    attempt       = 0;
  }
};

String generateInstallationSessionId(const String &deviceId);
String generateInstallationResumeToken(const String &sessionId);
