#include "GpioIsrService.h"

#include <Arduino.h>
#include <driver/gpio.h>

static bool g_gpioIsrInstalled = false;

bool GpioIsrService::isInstalled() {
  return g_gpioIsrInstalled;
}

void GpioIsrService::noteExternalInstall(const char *source) {
  if (g_gpioIsrInstalled) {
    Serial.println("[GPIO_ISR] Service already installed");
    return;
  }
  g_gpioIsrInstalled = true;
  if (source && source[0] != '\0') {
    Serial.printf("[GPIO_ISR] Service registered (installed by %s)\n", source);
  }
}

esp_err_t GpioIsrService::ensureInstalled(const char *requester) {
  if (g_gpioIsrInstalled) {
    Serial.println("[GPIO_ISR] Service already installed");
    return ESP_OK;
  }

  if (requester && requester[0] != '\0') {
    Serial.printf("[GPIO_ISR] install requested by %s\n", requester);
  }

  esp_err_t err = gpio_install_isr_service(0);

  if (err == ESP_OK) {
    g_gpioIsrInstalled = true;
    Serial.println("[GPIO_ISR] install successful");
    return ESP_OK;
  }

  if (err == ESP_ERR_INVALID_STATE) {
    g_gpioIsrInstalled = true;
    Serial.println("[GPIO_ISR] Service already installed");
    return ESP_OK;
  }

  Serial.printf("[GPIO_ISR] install failed: %s\n", esp_err_to_name(err));
  return err;
}
