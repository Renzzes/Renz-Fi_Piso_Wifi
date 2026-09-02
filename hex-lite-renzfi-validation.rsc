# Renz-Fi hEX lite — post-migration validation (non-destructive)
# Run: /import file-name=hex-lite-renzfi-validation.rsc
# Does NOT change configuration.

:global renzfiValPass 0
:global renzfiValFail 0
:global renzfiValWarn 0
:set renzfiValPass 0
:set renzfiValFail 0
:set renzfiValWarn 0

:put "========================================"
:put "RENZFI hEX lite validation — begin"
:put "========================================"

# --- Interfaces ---
:put ""
:put "[INTERFACES]"
:if ([:len [/interface ethernet find where name="ether1-WAN"]] > 0) do={
  :put "PASS ether1-WAN exists"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL ether1-WAN missing"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/interface ethernet find where name="ether2-ESP32"]] > 0) do={
  :put "PASS ether2-ESP32 exists"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL ether2-ESP32 missing"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/interface wireless find]] = 0) do={
  :put "PASS no wireless interfaces (hEX lite)"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL wireless interfaces present — must not exist on hEX lite"
  :set renzfiValFail ($renzfiValFail + 1)
}

# --- Factory default cleanup ---
:put ""
:put "[FACTORY DEFAULT CLEANUP]"
:if ([:len [/ip address find where interface=bridge and address~"192.168.88."]] = 0) do={
  :put "PASS no 192.168.88.1 on factory bridge"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL 192.168.88.0/24 still on factory bridge"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip dhcp-server find where interface=bridge]] = 0) do={
  :put "PASS no DHCP server on factory bridge"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL factory bridge DHCP server still active"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip pool find where name="default-dhcp"]] = 0) do={
  :put "PASS factory default-dhcp pool removed"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "WARN factory default-dhcp pool still present"
  :set renzfiValWarn ($renzfiValWarn + 1)
}

# --- Bridge topology / no duplicate membership ---
:put ""
:put "[BRIDGE TOPOLOGY]"
:if ([:len [/interface bridge find where name="bridgeGuest"]] > 0) do={
  :put "PASS bridgeGuest exists"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL bridgeGuest missing"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/interface bridge port find where bridge="bridgeGuest" and interface="ether2-ESP32"]] = 0) do={
  :put "PASS ether2-ESP32 NOT in bridgeGuest"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL ether2-ESP32 in bridgeGuest — management must be isolated"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/interface bridge port find where bridge="bridge" and interface="ether2-ESP32"]] = 0) do={
  :put "PASS ether2-ESP32 NOT on factory bridge"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL ether2-ESP32 still on factory bridge"
  :set renzfiValFail ($renzfiValFail + 1)
}
:foreach p in={"ether3";"ether4";"ether5"} do={
  :if ([:len [/interface bridge port find where bridge="bridgeGuest" and interface=$p]] > 0) do={
    :put ("PASS " . $p . " in bridgeGuest")
    :set renzfiValPass ($renzfiValPass + 1)
  } else={
    :put ("FAIL " . $p . " not in bridgeGuest")
    :set renzfiValFail ($renzfiValFail + 1)
  }
  :if ([:len [/interface bridge port find where bridge="bridge" and interface=$p]] = 0) do={
    :put ("PASS " . $p . " NOT on factory bridge")
    :set renzfiValPass ($renzfiValPass + 1)
  } else={
    :put ("FAIL " . $p . " still on factory bridge — duplicate membership risk")
    :set renzfiValFail ($renzfiValFail + 1)
  }
}
:local dupPorts 0
:foreach p in={"ether2-ESP32";"ether3";"ether4";"ether5"} do={
  :local portCount [:len [/interface bridge port find where interface=$p]]
  :if ($portCount > 1) do={
    :put ("FAIL " . $p . " is in " . $portCount . " bridges")
    :set renzfiValFail ($renzfiValFail + 1)
    :set dupPorts ($dupPorts + 1)
  }
}
:if ($dupPorts = 0) do={
  :put "PASS no duplicate bridge port membership on ether2-5"
  :set renzfiValPass ($renzfiValPass + 1)
}
:if ([:len [/interface bridge port find where bridge="bridgeGuest" and interface="ether1-WAN"]] = 0) do={
  :put "PASS ether1-WAN NOT in bridgeGuest"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL ether1-WAN in bridgeGuest"
  :set renzfiValFail ($renzfiValFail + 1)
}

# --- IP addressing ---
:put ""
:put "[IP ADDRESSES]"
:if ([:len [/ip address find where address="10.10.10.1/24" and interface="ether2-ESP32"]] > 0) do={
  :put "PASS 10.10.10.1/24 on ether2-ESP32"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL management address missing on ether2-ESP32"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip address find where address="10.20.0.1/24" and interface="bridgeGuest"]] > 0) do={
  :put "PASS 10.20.0.1/24 on bridgeGuest"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL guest gateway missing on bridgeGuest"
  :set renzfiValFail ($renzfiValFail + 1)
}

# --- DHCP ---
:put ""
:put "[DHCP]"
:if ([:len [/ip pool find where name="pool-guest" and ranges="10.20.0.10-10.20.0.254"]] > 0) do={
  :put "PASS pool-guest"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL pool-guest missing or wrong range"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip pool find where name="pool-mgmt" and ranges="10.10.10.2-10.10.10.20"]] > 0) do={
  :put "PASS pool-mgmt"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL pool-mgmt missing or wrong range"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip dhcp-server find where name="dhcp-guest" and interface="bridgeGuest"]] > 0) do={
  :put "PASS dhcp-guest on bridgeGuest"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL dhcp-guest missing or wrong interface"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip dhcp-server find where name="dhcp-mgmt" and interface="ether2-ESP32"]] > 0) do={
  :put "PASS dhcp-mgmt on ether2-ESP32"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL dhcp-mgmt missing or wrong interface"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip dhcp-server lease find where address="10.10.10.2" and mac-address="A2:CB:8F:F8:97:B5"]] > 0) do={
  :put "PASS ESP32 lease 10.10.10.2 with correct MAC"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :if ([:len [/ip dhcp-server lease find where address="10.10.10.2"]] > 0) do={
    :put "WARN ESP32 lease exists but MAC may differ — verify W5500"
    :set renzfiValWarn ($renzfiValWarn + 1)
  } else={
    :put "FAIL ESP32 lease 10.10.10.2 missing"
    :set renzfiValFail ($renzfiValFail + 1)
  }
}
:if ([:len [/ip dhcp-client find where interface="ether1-WAN" and disabled=no]] > 0) do={
  :put "PASS WAN DHCP client on ether1-WAN"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "WARN WAN DHCP client not enabled"
  :set renzfiValWarn ($renzfiValWarn + 1)
}

# --- HotSpot ---
:put ""
:put "[HOTSPOT]"
:if ([:len [/ip hotspot find where name="hotspot-renzfi" and interface="bridgeGuest"]] > 0) do={
  :put "PASS hotspot-renzfi on bridgeGuest"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL hotspot-renzfi missing or wrong interface"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip hotspot profile find where name="RenzFi-Hotspot" and hotspot-address="10.20.0.1" and dns-name="wifi.renz-fi.local"]] > 0) do={
  :put "PASS RenzFi-Hotspot profile"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL RenzFi-Hotspot profile"
  :set renzfiValFail ($renzfiValFail + 1)
}
:local hsProf ""
:do {
  :set hsProf [/ip hotspot profile get [find name="RenzFi-Hotspot"] html-directory]
} on-error={
  :set hsProf "missing"
}
:if ($hsProf = "hotspot") do={
  :put "PASS html-directory=hotspot"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put ("WARN html-directory=" . $hsProf . " — upload Files/hotspot/")
  :set renzfiValWarn ($renzfiValWarn + 1)
}
:if ([:len [/ip hotspot walled-garden ip find where dst-address="10.10.10.2" and action="accept" and disabled=no]] > 0) do={
  :put "PASS walled-garden 10.10.10.2"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL walled-garden missing for ESP32"
  :set renzfiValFail ($renzfiValFail + 1)
}

# --- NAT ---
:put ""
:put "[NAT]"
:if ([:len [/ip firewall nat find where comment="Renz-Fi Internet NAT"]] > 0) do={
  :put "PASS Renz-Fi Internet NAT"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL Renz-Fi Internet NAT missing"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip firewall nat find where comment="Renz-Fi Guest Internet NAT"]] > 0) do={
  :put "PASS Renz-Fi Guest Internet NAT"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL Renz-Fi Guest Internet NAT missing"
  :set renzfiValFail ($renzfiValFail + 1)
}

# --- Firewall ---
:put ""
:put "[FIREWALL]"
:if ([:len [/ip firewall filter find where comment="ESP32 RouterOS API" and protocol=tcp and dst-port=8728 and src-address="10.10.10.2"]] > 0) do={
  :put "PASS ESP32 API input rule"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL ESP32 API input rule missing"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:len [/ip firewall filter find where comment="Renz-Fi ESP32 trusted"]] > 0) do={
  :put "PASS ESP32 trusted input rule"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL ESP32 trusted input rule missing"
  :set renzfiValFail ($renzfiValFail + 1)
}

# --- API service ---
:put ""
:put "[API SERVICE]"
:local apiDisabled true
:local apiPort 0
:local apiAddr ""
:do {
  :set apiDisabled [/ip service get api disabled]
  :set apiPort [/ip service get api port]
  :set apiAddr [/ip service get api address]
} on-error={}
:if ($apiDisabled = false) do={
  :put "PASS API enabled"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL API disabled"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ($apiPort = 8728) do={
  :put "PASS API port 8728"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put ("FAIL API port " . $apiPort)
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:find $apiAddr "10.10.10.0/24"] >= 0) do={
  :put "PASS API allows 10.10.10.0/24"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL API missing 10.10.10.0/24"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:find $apiAddr "10.20.0.0/24"] >= 0) do={
  :put "PASS API allows 10.20.0.0/24"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "FAIL API missing 10.20.0.0/24"
  :set renzfiValFail ($renzfiValFail + 1)
}
:if ([:find $apiAddr "0.0.0.0/0"] >= 0) do={
  :put "FAIL API exposed globally (0.0.0.0/0)"
  :set renzfiValFail ($renzfiValFail + 1)
} else={
  :put "PASS API not globally exposed"
  :set renzfiValPass ($renzfiValPass + 1)
}

# --- Interface list ---
:put ""
:put "[RENZFI INTERFACE LIST]"
:if ([:len [/interface list member find where list="RENZFI_PRODUCTION" and interface="ether3"]] > 0) do={
  :put "PASS RENZFI_PRODUCTION includes ether3"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "WARN RENZFI_PRODUCTION missing ether3"
  :set renzfiValWarn ($renzfiValWarn + 1)
}

# --- Routes / WAN ---
:put ""
:put "[ROUTES / WAN]"
:if ([:len [/ip route find where dst-address=0.0.0.0/0]] > 0) do={
  :put "PASS default route present"
  :set renzfiValPass ($renzfiValPass + 1)
} else={
  :put "WARN no default route — connect ISP to ether1"
  :set renzfiValWarn ($renzfiValWarn + 1)
}

:put ""
:put "========================================"
:put ("SUMMARY pass=" . $renzfiValPass . " fail=" . $renzfiValFail . " warn=" . $renzfiValWarn)
:put "Physical acceptance tests still required after portal upload + wiring."
:put "========================================"
