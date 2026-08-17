#include "Models.h"

#include <strings.h>

const char *ethernetAddressModeLabel(EthernetAddressMode mode) {
  switch (mode) {
    case EthernetAddressMode::Static: return "static";
    case EthernetAddressMode::Dhcp:
    default:
      return "dhcp";
  }
}

EthernetAddressMode parseEthernetAddressMode(const char *label) {
  if (label && strcasecmp(label, "static") == 0) {
    return EthernetAddressMode::Static;
  }
  return EthernetAddressMode::Dhcp;
}
