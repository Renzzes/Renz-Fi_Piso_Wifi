#include "SetupDnsPolicy.h"

#include <lwip/dns.h>
#include <lwip/err.h>

#include "EthernetManager.h"
#include "InstallationState.h"
#include "InstallationStateManager.h"

InstallationStateManager *SetupDnsPolicy::_installation = nullptr;
EthernetManager *SetupDnsPolicy::_eth = nullptr;
bool SetupDnsPolicy::_ethDnsBlocked = false;
bool SetupDnsPolicy::_loggedSetupPolicy = false;
uint32_t SetupDnsPolicy::_apDnsRequests = 0;
uint32_t SetupDnsPolicy::_apDnsLocalResponses = 0;
uint32_t SetupDnsPolicy::_ethDnsAttempts = 0;

namespace {

void clearLwIpDnsServers() {
  ip_addr_t none;
  IP4_ADDR(ip_2_ip4(&none), 0, 0, 0, 0);
  none.type = IPADDR_TYPE_V4;
  dns_setserver(0, &none);
  dns_setserver(1, &none);
}

void restoreLwIpDnsFromEth(EthernetManager *eth) {
  if (!eth || !eth->hasIp()) return;

  IPAddress dnsIp;
  if (!dnsIp.fromString(eth->dns())) return;

  ip_addr_t dns;
  IP4_ADDR(ip_2_ip4(&dns), dnsIp[0], dnsIp[1], dnsIp[2], dnsIp[3]);
  dns.type = IPADDR_TYPE_V4;
  dns_setserver(0, &dns);
}

}  // namespace

void SetupDnsPolicy::begin(InstallationStateManager *installation,
                           EthernetManager *eth) {
  _installation = installation;
  _eth = eth;
}

bool SetupDnsPolicy::isSetupLifecycleActive() {
  return _installation && _installation->needsSetup();
}

void SetupDnsPolicy::applySetupPhasePolicy() {
  if (!isSetupLifecycleActive()) return;

  clearLwIpDnsServers();
  _ethDnsBlocked = true;

  if (!_loggedSetupPolicy) {
    Serial.println(
        "[setup-dns] Ethernet lwIP DNS cleared — no outbound DNS during setup "
        "lifecycle");
    _loggedSetupPolicy = true;
  }
}

void SetupDnsPolicy::restoreProductionDns() {
  if (!_ethDnsBlocked) return;

  restoreLwIpDnsFromEth(_eth);
  _ethDnsBlocked = false;
  _loggedSetupPolicy = false;
  Serial.println(
      "[setup-dns] Ethernet lwIP DNS restored from active interface config");
}

void SetupDnsPolicy::onEthernetIpChanged() {
  if (!isSetupLifecycleActive()) return;
  applySetupPhasePolicy();
}

void SetupDnsPolicy::noteApDnsRequest() { ++_apDnsRequests; }

void SetupDnsPolicy::noteApDnsLocalResponse() { ++_apDnsLocalResponses; }

void SetupDnsPolicy::noteEthernetDnsAttempt(const char *hostname) {
  ++_ethDnsAttempts;
  if (hostname && hostname[0] != '\0') {
    Serial.printf(
        "[setup-dns] blocked outbound DNS during setup (host=%s, total=%lu)\n",
        hostname, static_cast<unsigned long>(_ethDnsAttempts));
  } else {
    Serial.printf(
        "[setup-dns] blocked outbound DNS during setup (total=%lu)\n",
        static_cast<unsigned long>(_ethDnsAttempts));
  }
}

uint32_t SetupDnsPolicy::apDnsRequestCount() { return _apDnsRequests; }

uint32_t SetupDnsPolicy::apDnsLocalResponseCount() {
  return _apDnsLocalResponses;
}

uint32_t SetupDnsPolicy::ethernetDnsAttemptCount() { return _ethDnsAttempts; }

void SetupDnsPolicy::logDiagnostics() {
  Serial.printf(
      "[setup-dns] ap_req=%lu ap_local=%lu eth_attempts=%lu blocked=%s\n",
      static_cast<unsigned long>(_apDnsRequests),
      static_cast<unsigned long>(_apDnsLocalResponses),
      static_cast<unsigned long>(_ethDnsAttempts),
      _ethDnsBlocked ? "yes" : "no");
}

// lwIP hook — intercept hostname resolution before packets leave the device.
extern "C" err_t lwip_hook_dns_external_resolve(
    const char *name, int addrtype, ip_addr_t *addr,
    void (*found)(const char *name, void *callback_arg, ip_addr_t *addr),
    void *callback_arg) {
  (void)addrtype;
  (void)addr;
  if (!SetupDnsPolicy::isSetupLifecycleActive()) {
    return ERR_ARG;
  }

  SetupDnsPolicy::noteEthernetDnsAttempt(name);
  if (found) {
    found(name, callback_arg, nullptr);
  }
  return ERR_OK;
}
