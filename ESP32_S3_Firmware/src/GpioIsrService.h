#pragma once

#include <esp_err.h>

// Singleton GPIO ISR dispatcher — gpio_install_isr_service() runs at most once.
// ETH.begin() installs the service on arduino-esp32 3.x; call noteExternalInstall()
// immediately after a successful ETH.begin() so ensureInstalled() stays a no-op.
class GpioIsrService {
 public:
  static esp_err_t ensureInstalled(const char *requester = nullptr);
  static bool isInstalled();
  static void noteExternalInstall(const char *source);

 private:
  GpioIsrService() = delete;
};
