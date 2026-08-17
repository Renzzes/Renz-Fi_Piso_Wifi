#pragma once

#include <Arduino.h>
#include <IPAddress.h>

// AP-local captive DNS: answers every A/ANY query with the Management AP IP.
// Never forwards, proxies, or resolves through Ethernet DNS.
class CaptiveApDnsServer {
 public:
  ~CaptiveApDnsServer();
  bool start(const IPAddress &apIp, const IPAddress &listenIp);
  void stop();
  bool isRunning() const { return _running; }

 private:
  class Impl;
  Impl *_impl = nullptr;
  bool _running = false;
};
