# Renz-Fi — hAP lite (RB941-2nD) → hEX lite (RB750r2) migration
# Source reference: hap-lite-migration.rsc (RouterOS 7.20.7 — DO NOT import on hEX)
# Target: factory-reset RB750r2 (RouterOS 7.x)
# Generated: 2026-08-29 (factory-reset aware)
#
# EXPECTED STARTING STATE: factory default RouterOS 7 on hEX lite
#   - bridge "bridge" with ether2–ether5
#   - 192.168.88.1/24 on bridge
#   - defconf DHCP server on bridge (pool default-dhcp)
#   - ether1 WAN DHCP client
#   - defconf firewall + NAT
#
# MANUAL BEFORE IMPORT
#   1. Connect Winbox to ether2 (192.168.88.1) — session drops when 10.10.10.1 is applied
#   2. Verify ESP32 W5500 MAC = A2:CB:8F:F8:97:B5 (edit lease below if different)
#   3. Create RouterOS API user for Renz-Fi (not in hAP export) — MANUAL
#   4. After import: upload portal to Files/hotspot/
#   5. RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal
#
# NO wireless / wlan1 / CAP on hEX lite.
# NO /system reset-configuration.
# NO credentials in this script.

:log info "RENZFI: hAP-lite to hEX-lite migration — begin (factory-reset target)"

# ===========================================================================
# PHASE 0 — Remove factory-default configuration that conflicts with Renz-Fi
# INTENTIONALLY DESTRUCTIVE to factory defconf only (not Renz-Fi objects).
# Safe on factory-reset hEX; do not run on a configured production router.
# ===========================================================================

:log info "RENZFI: phase 0 — factory default cleanup"

# 0a. Disable and remove factory DHCP server on default bridge
:if ([:len [/ip dhcp-server find where interface=bridge]] > 0) do={
  /ip dhcp-server set [find where interface=bridge] disabled=yes
  /ip dhcp-server remove [find where interface=bridge]
}
:if ([:len [/ip dhcp-server find where name=defconf]] > 0) do={
  /ip dhcp-server set [find where name=defconf] disabled=yes
  /ip dhcp-server remove [find where name=defconf]
}
:if ([:len [/ip dhcp-server find where name=default]] > 0) do={
  /ip dhcp-server set [find where name=default] disabled=yes
  /ip dhcp-server remove [find where name=default]
}

# 0b. Remove factory DHCP network and pool
:if ([:len [/ip dhcp-server network find where address="192.168.88.0/24"]] > 0) do={
  /ip dhcp-server network remove [find where address="192.168.88.0/24"]
}
:if ([:len [/ip pool find where name="default-dhcp"]] > 0) do={
  /ip pool remove [find where name="default-dhcp"]
}

# 0c. Remove ALL factory bridge ports (ether2–ether5) BEFORE interface rename
#     RouterOS does not auto-move ports between bridges.
:foreach p in={"ether2";"ether3";"ether4";"ether5"} do={
  :if ([:len [/interface bridge port find where interface=$p]] > 0) do={
    /interface bridge port remove [find where interface=$p]
  }
}

# 0d. Remove factory LAN IP on default bridge
:if ([:len [/ip address find where interface=bridge and address~"192.168.88."]] > 0) do={
  /ip address remove [find where interface=bridge and address~"192.168.88."]
}

# 0e. Remove factory defconf firewall/NAT (Renz-Fi rules added in phase 8–9)
:if ([:len [/ip firewall filter find where comment~"defconf"]] > 0) do={
  /ip firewall filter remove [find where comment~"defconf"]
}
:if ([:len [/ip firewall nat find where comment~"defconf"]] > 0) do={
  /ip firewall nat remove [find where comment~"defconf"]
}
:if ([:len [/ip firewall filter find where comment~"default configuration"]] > 0) do={
  /ip firewall filter remove [find where comment~"default configuration"]
}
:if ([:len [/ip firewall nat find where comment~"default configuration"]] > 0) do={
  /ip firewall nat remove [find where comment~"default configuration"]
}

# ===========================================================================
# PHASE 1 — Interface naming (WAN + ESP32 management)
# ===========================================================================

:if ([:len [/interface ethernet find where name="ether1-WAN"]] = 0) do={
  /interface ethernet set [find default-name=ether1] name=ether1-WAN
}
:if ([:len [/interface ethernet find where name="ether2-ESP32"]] = 0) do={
  /interface ethernet set [find default-name=ether2] name=ether2-ESP32
}

# ===========================================================================
# PHASE 2 — Guest bridge + port topology
# ether1-WAN and ether2-ESP32 must NEVER join bridgeGuest
# ether3/ether4/ether5 → bridgeGuest only
# ===========================================================================

:if ([:len [/interface bridge find where name="bridgeGuest"]] = 0) do={
  /interface bridge add name=bridgeGuest comment="Renz-Fi guest bridge" auto-mac=yes protocol-mode=rstp
}

:foreach p in={"ether3";"ether4";"ether5"} do={
  :if ([:len [/interface bridge port find where bridge="bridgeGuest" and interface=$p]] = 0) do={
    /interface bridge port add bridge=bridgeGuest interface=$p comment="Renz-Fi guest LAN"
  }
}

# Factory LAN list: add bridgeGuest so forward path works if any list rules remain
:if ([:len [/interface list find where name="LAN"]] > 0) do={
  :if ([:len [/interface list member find where list="LAN" and interface="bridgeGuest"]] = 0) do={
    /interface list member add list=LAN interface=bridgeGuest comment="Renz-Fi guest LAN"
  }
}
# Ensure WAN list includes renamed WAN port
:if ([:len [/interface list find where name="WAN"]] > 0) do={
  :if ([:len [/interface list member find where list="WAN" and interface="ether1-WAN"]] = 0) do={
    :if ([:len [/interface list member find where list="WAN" and interface="ether1"]] > 0) do={
      /interface list member set [find where list="WAN" and interface="ether1"] interface=ether1-WAN
    } else={
      /interface list member add list=WAN interface=ether1-WAN comment="Renz-Fi WAN"
    }
  }
}

# Renz-Fi production ingress marker — external AP uplink replaces hAP wlan1
:if ([:len [/interface list find where name="RENZFI_PRODUCTION"]] = 0) do={
  /interface list add name=RENZFI_PRODUCTION comment="Renz-Fi Production Guest Ingress"
}
:if ([:len [/interface list member find where list="RENZFI_PRODUCTION" and interface="ether3"]] = 0) do={
  /interface list member add list=RENZFI_PRODUCTION interface=ether3
}

# ===========================================================================
# PHASE 3 — IP addressing (split management + guest)
# ===========================================================================

:if ([:len [/ip address find where address="10.10.10.1/24" and interface="ether2-ESP32"]] = 0) do={
  /ip address add address=10.10.10.1/24 interface=ether2-ESP32 network=10.10.10.0 comment="Renz-Fi ESP32 Management"
}
:if ([:len [/ip address find where address="10.20.0.1/24" and interface="bridgeGuest"]] = 0) do={
  /ip address add address=10.20.0.1/24 interface=bridgeGuest network=10.20.0.0 comment="Renz-Fi Guest Gateway"
}

# ===========================================================================
# PHASE 4 — DHCP pools, servers, networks, ESP32 reservation
# ===========================================================================

:if ([:len [/ip pool find where name="pool-guest"]] = 0) do={
  /ip pool add name=pool-guest ranges=10.20.0.10-10.20.0.254
}
:if ([:len [/ip pool find where name="pool-mgmt"]] = 0) do={
  /ip pool add name=pool-mgmt ranges=10.10.10.2-10.10.10.20
}
:if ([:len [/ip dhcp-server find where name="dhcp-guest"]] = 0) do={
  /ip dhcp-server add name=dhcp-guest interface=bridgeGuest address-pool=pool-guest lease-time=1h
} else={
  /ip dhcp-server set [find name="dhcp-guest"] interface=bridgeGuest address-pool=pool-guest disabled=no
}
:if ([:len [/ip dhcp-server find where name="dhcp-mgmt"]] = 0) do={
  /ip dhcp-server add name=dhcp-mgmt interface=ether2-ESP32 address-pool=pool-mgmt lease-time=1h
} else={
  /ip dhcp-server set [find name="dhcp-mgmt"] interface=ether2-ESP32 address-pool=pool-mgmt disabled=no
}
:if ([:len [/ip dhcp-server lease find where address="10.10.10.2"]] = 0) do={
  /ip dhcp-server lease add address=10.10.10.2 mac-address=A2:CB:8F:F8:97:B5 comment="Renz-Fi ESP32"
}
:if ([:len [/ip dhcp-server network find where address="10.10.10.0/24"]] = 0) do={
  /ip dhcp-server network add address=10.10.10.0/24 gateway=10.10.10.1 dns-server=10.10.10.1
}
:if ([:len [/ip dhcp-server network find where address="10.20.0.0/24"]] = 0) do={
  /ip dhcp-server network add address=10.20.0.0/24 gateway=10.20.0.1 dns-server=8.8.8.8,1.1.1.1,10.20.0.1
}

# ===========================================================================
# PHASE 5 — WAN DHCP client (reuse factory client; no :local reassignment)
# Phase 1 renames ether1 → ether1-WAN; factory client normally follows rename.
# RouterOS 7.18.x: :local variables are immutable — do not use :set on :local.
# ===========================================================================

:if ([:len [/ip dhcp-client find where interface="ether1-WAN"]] > 0) do={
  /ip dhcp-client set [find where interface="ether1-WAN"] disabled=no use-peer-dns=yes add-default-route=yes
} else={
  :if ([:len [/ip dhcp-client find where interface="ether1"]] > 0) do={
    /ip dhcp-client set [find where interface="ether1"] interface=ether1-WAN disabled=no use-peer-dns=yes add-default-route=yes
  } else={
    /ip dhcp-client add interface=ether1-WAN disabled=no use-peer-dns=yes add-default-route=yes
  }
}

# ===========================================================================
# PHASE 6 — DNS
# ===========================================================================

/ip dns set allow-remote-requests=yes servers=1.1.1.1,8.8.8.8

# ===========================================================================
# PHASE 7 — HotSpot profile, user profiles, server
# ===========================================================================

:if ([:len [/ip hotspot profile find where name="RenzFi-Hotspot"]] = 0) do={
  /ip hotspot profile add name=RenzFi-Hotspot dns-name=wifi.renz-fi.local hotspot-address=10.20.0.1 \
    html-directory=hotspot login-by=cookie,http-chap,http-pap
} else={
  /ip hotspot profile set [find name="RenzFi-Hotspot"] dns-name=wifi.renz-fi.local hotspot-address=10.20.0.1 \
    html-directory=hotspot login-by=cookie,http-chap,http-pap
}
:if ([:len [/ip hotspot user profile find where name="test1"]] = 0) do={
  /ip hotspot user profile add name=test1 rate-limit=5M/5M
}
:if ([:len [/ip hotspot user profile find where name="renzfi-speed-10m-10m"]] = 0) do={
  /ip hotspot user profile add name=renzfi-speed-10m-10m rate-limit=10M/10M
}
:if ([:len [/ip hotspot user profile find where name="renzfi-speed-15m-15m"]] = 0) do={
  /ip hotspot user profile add name=renzfi-speed-15m-15m rate-limit=15M/15M
}
:if ([:len [/ip hotspot user profile find where name="renzfi-speed-50m-50m"]] = 0) do={
  /ip hotspot user profile add name=renzfi-speed-50m-50m rate-limit=50M/50M
}
:if ([:len [/ip hotspot find where name="hotspot-renzfi"]] = 0) do={
  /ip hotspot add name=hotspot-renzfi interface=bridgeGuest address-pool=pool-guest profile=RenzFi-Hotspot disabled=no
} else={
  /ip hotspot set [find name="hotspot-renzfi"] interface=bridgeGuest address-pool=pool-guest profile=RenzFi-Hotspot disabled=no
}

# ===========================================================================
# PHASE 8 — Firewall filter (source hAP Renz-Fi rules)
# ===========================================================================

:if ([:len [/ip firewall filter find where comment="Input established"]] = 0) do={
  /ip firewall filter add chain=input action=accept connection-state=established,related,untracked comment="Input established" place-before=0
}
:if ([:len [/ip firewall filter find where comment="Return traffic"]] = 0) do={
  /ip firewall filter add chain=forward action=accept connection-state=established,related comment="Return traffic" place-before=0
}
:if ([:len [/ip firewall filter find where comment="Renz-Fi ESP32 trusted"]] = 0) do={
  /ip firewall filter add chain=input action=accept src-address=10.10.10.2 comment="Renz-Fi ESP32 trusted" place-before=0
}
:if ([:len [/ip firewall filter find where comment="ESP32 ICMP"]] = 0) do={
  /ip firewall filter add chain=input action=accept protocol=icmp src-address=10.10.10.2 comment="ESP32 ICMP" place-before=0
}
:if ([:len [/ip firewall filter find where comment="ESP32 RouterOS API"]] = 0) do={
  /ip firewall filter add chain=input action=accept protocol=tcp dst-port=8728 src-address=10.10.10.2 comment="ESP32 RouterOS API" place-before=0
}

# ===========================================================================
# PHASE 9 — NAT (both source masquerade rules preserved)
# ===========================================================================

:if ([:len [/ip firewall nat find where comment="Renz-Fi Internet NAT"]] = 0) do={
  /ip firewall nat add chain=srcnat action=masquerade out-interface=ether1-WAN comment="Renz-Fi Internet NAT"
}
:if ([:len [/ip firewall nat find where comment="Renz-Fi Guest Internet NAT"]] = 0) do={
  /ip firewall nat add chain=srcnat action=masquerade out-interface=ether1-WAN src-address=10.20.0.0/24 comment="Renz-Fi Guest Internet NAT"
}

# ===========================================================================
# PHASE 10 — HotSpot walled garden (ESP32 appliance API for guest clients)
# ===========================================================================

:if ([:len [/ip hotspot walled-garden ip find where dst-address="10.10.10.2" and action="accept" and disabled=no]] = 0) do={
  /ip hotspot walled-garden ip add dst-address=10.10.10.2 action=accept disabled=no comment="Renz-Fi ESP32 appliance API"
}

# ===========================================================================
# PHASE 11 — RouterOS API restriction (does NOT create API user)
# ===========================================================================

/ip service set api disabled=no port=8728 address=10.10.10.0/24,10.20.0.0/24

# ===========================================================================
# PHASE 12 — Renz-Fi managed marker script
# ===========================================================================

:if ([:len [/system script find where name="renzfi-hotspot-ready"]] = 0) do={
  /system script add name=renzfi-hotspot-ready owner=admin policy=ftp,reboot,read,write,policy,test,password,sniff,sensitive,romon \
    dont-require-permissions=no comment="RENZFI: managed script" source=":log info \"RENZFI: hotspot provisioning marker\""
}

# ===========================================================================
# NOT MIGRATED (manual / site-specific)
# - HotSpot operational users / voucher passwords
# - RouterOS API user credentials
# - 10.10.10.20/32 via bridgeGuest (MANUAL if external AP uses 10.10.10.20)
# - Captive portal files (Files/hotspot/)
# ===========================================================================

:log info "RENZFI: hAP-lite to hEX-lite migration — end (reconnect Winbox at 10.10.10.1 on ether2-ESP32)"
