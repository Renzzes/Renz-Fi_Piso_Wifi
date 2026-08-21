#include "AuthCredentials.h"

#include <Preferences.h>
#include <mbedtls/sha256.h>

#include "Config.h"

namespace AuthCredentials {

String hashPassword(const String &password) {
  uint8_t                hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(
      &ctx, reinterpret_cast<const unsigned char *>(password.c_str()),
      password.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char out[65];
  for (int i = 0; i < 32; i++) sprintf(out + (i * 2), "%02x", hash[i]);
  out[64] = 0;
  return String(out);
}

bool loadPasswordHash(String &outHash) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, true)) return false;
  outHash = prefs.getString("passwordHash", "");
  prefs.end();
  return outHash.length() > 0;
}

void savePasswordHash(const String &hash) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, false)) return;
  prefs.putString("passwordHash", hash);
  prefs.end();
}

bool loadMustChangePassword() {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, true)) return true;
  const bool value = prefs.getBool("mustChange", true);
  prefs.end();
  return value;
}

void saveMustChangePassword(bool value) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, false)) return;
  prefs.putBool("mustChange", value);
  prefs.end();
}

bool loadFirstBootCompleted() {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, true)) return false;
  const bool value = prefs.getBool("firstBootDone", false);
  prefs.end();
  return value;
}

void saveFirstBootCompleted(bool value) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, false)) return;
  prefs.putBool("firstBootDone", value);
  prefs.end();
}

void applyRecoveryReset() {
  savePasswordHash(hashPassword(RenzFiConfig::DEFAULT_ADMIN_PASSWORD));
  saveMustChangePassword(true);
  saveFirstBootCompleted(false);
  clearSetupUnlockCredentials();
}

bool loadSetupUnlockHash(String &outHash) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, true)) return false;
  outHash = prefs.getString("unlockHash", "");
  prefs.end();
  return outHash.length() > 0;
}

void saveSetupUnlockHash(const String &hash) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, false)) return;
  if (hash.isEmpty()) {
    prefs.remove("unlockHash");
  } else {
    prefs.putString("unlockHash", hash);
  }
  prefs.end();
}

bool loadSetupUnlockProtected(String &outBlob) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, true)) return false;
  outBlob = prefs.getString("unlockBlob", "");
  prefs.end();
  return outBlob.length() > 0;
}

void saveSetupUnlockProtected(const String &blob) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, false)) return;
  if (blob.isEmpty()) {
    prefs.remove("unlockBlob");
  } else {
    prefs.putString("unlockBlob", blob);
  }
  prefs.end();
}

void clearSetupUnlockCredentials() {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_AUTH_NS, false)) return;
  prefs.remove("unlockHash");
  prefs.remove("unlockBlob");
  prefs.end();
}

}  // namespace AuthCredentials
