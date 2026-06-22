#pragma once

#include <esp_err.h>

// Central GPIO ISR dispatcher — installed exactly once before any
// gpio_isr_handler_add() use. ETH.begin() also installs the service;
// noteExternalInstall() after ETH.begin() records that so ensureInstalled()
// never invokes gpio_install_isr_service() again.
class GpioIsrService {
 public:
  static esp_err_t ensureInstalled(const char *requester = nullptr);
  static bool isInstalled();
  static void noteExternalInstall(const char *source);

 private:
  GpioIsrService() = delete;
};
