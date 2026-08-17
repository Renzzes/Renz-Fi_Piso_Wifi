#pragma once

#include <Arduino.h>

class EthernetManager;

// Setup-plane MikroTik RouterOS API validation (TCP connect + login only).
class SetupRouterValidator {
 public:
  struct Input {
    String   host;
    uint16_t apiPort = 8728;
    String   username;
    String   password;
  };

  struct Result {
    bool   success = false;
    String code;
    String message;
    String stage;
    String identity;
    String board;
    String routerOsVersion;
  };

  static Result validate(const Input &input, EthernetManager *eth);
};
