#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>

#include "ExistingNetworkScan.h"
#include "RouterOsClient.h"
#include "SetupRouterConnectionManager.h"

class EthernetManager;
class InstallationStateManager;

// Existing Network Scan V2 — sole owner of the read-only scan lifecycle.
//
//   construct -> connect -> login -> read RouterOS -> evaluate -> serialize
//   -> disconnect -> destroy
//
// Everything the scan allocates (compact row tables, correlation lookup
// maps, the scratch RouterOS reply buffer) lives inside this object and
// dies with it. Callers only ever hold a std::unique_ptr<ExistingNetworkScanner>
// on the stack — nothing else.
//
// RouterOsClient is injected by reference, not owned: the transport stays
// independent of scan orchestration so it can be reused (or swapped for a
// different driver's client) without touching this class.
//
// The JSON contract produced by run() (via ExistingNetworkScan::serializeScanJson)
// is byte-identical to the previous implementation. Only the internal path
// that builds the ScanResult changed.
class ExistingNetworkScanner {
 public:
  struct Outcome {
    bool   success = false;
    int    httpStatus = 200;
    String errorCode;
    String errorMessage;
    String stage;
  };

  explicit ExistingNetworkScanner(RouterOsClient &client);
  ~ExistingNetworkScanner();

  ExistingNetworkScanner(const ExistingNetworkScanner &) = delete;
  ExistingNetworkScanner &operator=(const ExistingNetworkScanner &) = delete;

  Outcome run(EthernetManager *eth,
              SetupRouterConnectionManager *routerConnection,
              InstallationStateManager *installation, JsonObject dataOut,
              void (*progressFn)(void *ctx, const char *stageId,
                                 const char *label) = nullptr,
              void *progressCtx = nullptr);

 private:
  static constexpr uint8_t kMaxRows = ExistingNetworkScan::kReplyCap;

  struct BridgeRow {
    String name;
    String comment;
  };
  struct AddressRow {
    String address;
    String interfaceName;
    String comment;
  };
  struct PoolRow {
    String name;
    String ranges;
    String comment;
  };
  struct DhcpServerRow {
    String name;
    String interfaceName;
    String poolRef;
    String comment;
    bool   disabled = false;
  };
  struct DhcpNetworkRow {
    String address;
    String comment;
  };

  RouterOsClient &_client;
  // One scratch reply buffer, reused for all 7 RouterOS commands: execute ->
  // distill into compact rows -> next command re-initializes it. Never more
  // than one RouterOsClient reply is alive at a time.
  BridgeRow      _bridges[kMaxRows];
  uint8_t        _bridgeCount = 0;
  AddressRow     _addresses[kMaxRows];
  uint8_t        _addressCount = 0;
  PoolRow        _pools[kMaxRows];
  uint8_t        _poolCount = 0;
  DhcpServerRow  _dhcpServers[kMaxRows];
  uint8_t        _dhcpServerCount = 0;
  DhcpNetworkRow _dhcpNetworks[kMaxRows];
  uint8_t        _dhcpNetworkCount = 0;

  bool _apiAccessOk = false;
  bool _apiAccessManaged = false;
  bool _firewallLimited = false;
  bool _hotspotDetectedGlobal = false;
  bool _hotspotInspectionAttempted = false;
  bool _hotspotInspectionOk = false;

  // O(1) join maps, populated once right after the relevant table is
  // fetched. Exist only for the lifetime of this scanner.
  std::map<String, uint8_t>      _poolIndexByName;
  std::map<String, uint8_t>      _dhcpNetworkIndexByAddress;
  std::map<String, uint8_t>      _firstEnabledDhcpServerByBridge;
  std::multimap<String, uint8_t> _addressIndicesByInterface;
  std::map<String, bool>         _hotspotEnabledByInterface;

  bool connectAndLogin(const SetupRouterConnectionManager::RouterInput &input,
                       EthernetManager *eth, String &errorOut,
                       String &errorCodeOut,
                       void (*progressFn)(void *, const char *, const char *),
                       void *progressCtx);

  bool fetchBridges(String &errorOut, String &errorCodeOut,
                    void (*progressFn)(void *, const char *, const char *),
                    void *progressCtx);
  bool fetchAddresses(String &errorOut, String &errorCodeOut,
                      void (*progressFn)(void *, const char *, const char *),
                      void *progressCtx);
  bool fetchPools(String &errorOut, String &errorCodeOut,
                  void (*progressFn)(void *, const char *, const char *),
                  void *progressCtx);
  bool fetchDhcpServers(String &errorOut, String &errorCodeOut,
                        void (*progressFn)(void *, const char *, const char *),
                        void *progressCtx);
  bool fetchDhcpNetworks(String &errorOut, String &errorCodeOut,
                         void (*progressFn)(void *, const char *, const char *),
                         void *progressCtx);
  bool fetchFirewall(const String &espIp, String &errorOut, String &errorCodeOut,
                     void (*progressFn)(void *, const char *, const char *),
                     void *progressCtx);
  void fetchHotspot(uint32_t deadlineMs,
                    void (*progressFn)(void *, const char *, const char *),
                    void *progressCtx);

  void correlateCandidates(const String &espIp, const String &espSubnetCidr,
                           ExistingNetworkScan::ScanResult &out);
};
