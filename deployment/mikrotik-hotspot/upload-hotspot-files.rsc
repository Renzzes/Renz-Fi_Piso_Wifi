# Renz-Fi MikroTik Hotspot — post-upload configuration script
#
# Prerequisites:
#   - RouterOS hotspot package enabled (/system package print)
#   - Portal files uploaded to Files -> hotspot/ (see README.md step 2)
#
# Edit renzfiApplianceIp below to match the ESP32 DHCP reservation on the
# hAP lite guest LAN (10.10.10.0/24). It must match RENZFI_APPLIANCE_BASE_URL
# used when running npm run build:mikrotik-portal.
#
# Run from RouterOS terminal (RouterOS 7.20+):
#   /import file-name=upload-hotspot-files.rsc
#
:local renzfiApplianceIp "10.10.10.2"

:if ([:len [/system package find name=hotspot disabled=no]] = 0) do={
  :put "ERROR: hotspot package is not enabled. Enable it first, then re-import."
  :error "hotspot package required"
}

/ip hotspot profile set [find] html-directory=hotspot

:local existingRules [/ip hotspot walled-garden ip find comment="Renz-Fi ESP32 appliance API"]
:foreach id in=$existingRules do={
  /ip hotspot walled-garden ip remove $id
}
/ip hotspot walled-garden ip add action=accept dst-address=($renzfiApplianceIp . "/32") comment="Renz-Fi ESP32 appliance API"
:log info ("Renz-Fi: walled-garden rule set for appliance API at " . $renzfiApplianceIp)

:put "Renz-Fi Hotspot configuration applied."
:put "Verify: /ip hotspot profile print ; /ip hotspot walled-garden ip print"
