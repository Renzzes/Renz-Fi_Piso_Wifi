#pragma once

#include <Arduino.h>

class EthernetManager;
class InstallationStateManager;

// Setup-phase DNS isolation: AP clients get local captive answers only;
// the ESP32 must not issue outbound DNS queries over Ethernet while setup
// lifecycle states are active (factory / owner_created / router_configured and
// any pre-ready state where the Management AP is running).
class SetupDnsPolicy {
 public:
  static void begin(InstallationStateManager *installation,
                    EthernetManager *eth);

  static bool isSetupLifecycleActive();

  static void applySetupPhasePolicy();
  static void restoreProductionDns();
  static void onEthernetIpChanged();

  static void noteApDnsRequest();
  static void noteApDnsLocalResponse();
  static void noteEthernetDnsAttempt(const char *hostname);

  static uint32_t apDnsRequestCount();
  static uint32_t apDnsLocalResponseCount();
  static uint32_t ethernetDnsAttemptCount();

  static void logDiagnostics();

 private:
  static InstallationStateManager *_installation;
  static EthernetManager *_eth;
  static bool _ethDnsBlocked;
  static bool _loggedSetupPolicy;
  static uint32_t _apDnsRequests;
  static uint32_t _apDnsLocalResponses;
  static uint32_t _ethDnsAttempts;
};
