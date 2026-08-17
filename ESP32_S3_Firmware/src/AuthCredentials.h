#pragma once

#include <Arduino.h>

// NVS-backed admin credentials shared by AuthManager and RecoveryManager.
// Auth data lives only in NVS namespace renz-auth — not duplicated in SD JSON.
namespace AuthCredentials {

String hashPassword(const String &password);
bool loadPasswordHash(String &outHash);
void savePasswordHash(const String &hash);

bool loadMustChangePassword();
void saveMustChangePassword(bool value);

bool loadFirstBootCompleted();
void saveFirstBootCompleted(bool value);

// Factory/recovery auth reset: password=admin, mustChange=true, firstBoot=false.
void applyRecoveryReset();

}  // namespace AuthCredentials
