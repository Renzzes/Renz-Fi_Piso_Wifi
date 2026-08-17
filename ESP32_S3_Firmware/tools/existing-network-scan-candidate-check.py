#!/usr/bin/env python3
"""Regression guards + fixture simulation for ExistingNetworkScan candidate
evaluation.

Replays the attached bridge-lan MikroTik configuration and asserts that the
scan produces exactly one generic-compatible candidate instead of incorrectly
returning no_compatible_candidate.

Root causes guarded against:
  1. networkFromGatewayCidr() must mask the gateway IP to the network address
     (10.10.10.1/24 -> 10.10.10.0/24) so /ip/dhcp-server/network/print rows
     match.
  2. ESP subnet overlap must NOT reject the candidate when the ESP already
     lives on the guest LAN (10.10.10.2 on 10.10.10.0/24).
"""

from __future__ import annotations

import ipaddress
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SCAN_CPP = ROOT.parent / "src" / "ExistingNetworkScan.cpp"

# ── Fixture: verified physical router (no Renz-Fi comments) ─────────────────
BRIDGE_LAN_FIXTURE = {
    "bridges": [{"name": "bridge-lan", "comment": ""}],
    "addresses": [{"address": "10.10.10.1/24", "interface": "bridge-lan", "comment": ""}],
    "pools": [{"name": "pool-guest", "ranges": "10.10.10.100-10.10.10.254", "comment": ""}],
    "dhcp_servers": [{
        "name": "dhcp-guest",
        "interface": "bridge-lan",
        "address-pool": "pool-guest",
        "disabled": "false",
        "comment": "",
    }],
    "dhcp_networks": [{"address": "10.10.10.0/24", "comment": ""}],
    "filter_rules": [{
        "chain": "input",
        "protocol": "tcp",
        "dst-port": "8728",
        "src-address": "10.10.10.2",
        "action": "accept",
        "disabled": "false",
        "comment": "",
    }],
    "hotspots": [{"name": "hotspot1", "interface": "bridge-lan", "disabled": "false"}],
    "esp_ip": "10.10.10.2",
    "esp_subnet_cidr": "10.10.10.0/24",
}


def network_from_gateway_cidr(gateway_cidr: str) -> str:
    """Mirror the fixed C++ networkFromGatewayCidr()."""
    net = ipaddress.ip_network(gateway_cidr, strict=False)
    return f"{net.network_address}/{net.prefixlen}"


def cidr_overlaps(a: str, b: str) -> bool:
    na = ipaddress.ip_network(a, strict=False)
    nb = ipaddress.ip_network(b, strict=False)
    return na.overlaps(nb)


def is_host_in_cidr(host_ip: str, network_cidr: str) -> bool:
    return ipaddress.ip_address(host_ip) in ipaddress.ip_network(network_cidr, strict=False)


def is_problematic_esp_subnet_overlap(guest_network: str, esp_subnet: str, esp_ip: str) -> bool:
    """Mirror the fixed C++ isProblematicEspSubnetOverlap()."""
    if not esp_subnet or not guest_network:
        return False
    if guest_network == esp_subnet:
        return False
    if esp_ip and is_host_in_cidr(esp_ip, guest_network):
        return False
    return cidr_overlaps(guest_network, esp_subnet)


def api_access_ok(filter_rules: list[dict], esp_ip: str) -> bool:
    for rule in filter_rules:
        if rule.get("chain") != "input":
            continue
        if rule.get("protocol") != "tcp":
            continue
        if rule.get("dst-port") != "8728":
            continue
        if rule.get("disabled") == "true":
            continue
        if rule.get("action") != "accept":
            continue
        src = rule.get("src-address", "")
        if src and src != esp_ip:
            continue
        return True
    return False


PASS = "pass"
FAIL = "fail"
UNKNOWN = "unknown"
WARNING = "warning"

COMPLETE = "complete"
SKIPPED = "skipped"
NOT_SUPPORTED = "not_supported"
FAILED = "failed"


def build_compatibility(
    *,
    bridge: bool,
    gateway: bool,
    dhcp: bool,
    pool: bool,
    dhcp_network: bool,
    api_ok: bool,
    firewall_limited: bool,
    hotspot_on_bridge: bool,
    hotspot_inspection_ok: bool,
) -> dict[str, str]:
    compat = {
        "bridge": PASS if bridge else FAIL,
        "gateway": PASS if gateway else FAIL,
        "dhcp": PASS if dhcp else FAIL,
        "pool": PASS if pool else FAIL,
        "dhcpNetwork": PASS if dhcp_network else FAIL,
        "firewall": PASS if api_ok else (WARNING if firewall_limited else FAIL),
        "hotspot": (
            UNKNOWN if not hotspot_inspection_ok
            else PASS if hotspot_on_bridge else FAIL
        ),
    }
    return compat


def compat_passes(level: str) -> bool:
    return level == PASS


def compute_adoption_score(compat: dict[str, str]) -> int:
    weights = {
        "bridge": 5,
        "gateway": 15,
        "dhcp": 15,
        "pool": 15,
        "dhcpNetwork": 15,
        "firewall": 20,
        "hotspot": 15,
    }
    points = {PASS: 100, WARNING: 50, UNKNOWN: 25, FAIL: 0}
    total = 0
    for key, weight in weights.items():
        total += (points.get(compat.get(key, FAIL), 0) * weight) // 100
    return min(total, 100)


def build_inspection(
    *,
    firewall_print_ok: bool,
    hotspot_inspection_attempted: bool,
    hotspot_inspection_ok: bool,
) -> dict[str, str]:
    inspection = {
        "bridge": COMPLETE,
        "gateway": COMPLETE,
        "dhcp": COMPLETE,
        "pool": COMPLETE,
        "dhcpNetwork": COMPLETE,
        "firewall": COMPLETE if firewall_print_ok else FAILED,
        "hotspot": (
            SKIPPED if not hotspot_inspection_attempted
            else COMPLETE if hotspot_inspection_ok
            else FAILED
        ),
        "nat": NOT_SUPPORTED,
    }
    return inspection


def make_candidate_id(bridge_name: str, gateway_ip: str, sequence: int) -> str:
    if bridge_name and gateway_ip:
        return f"{bridge_name}@{gateway_ip}"
    return f"candidate-{sequence}"


def build_requirements(compat: dict[str, str], renzfi_managed: bool) -> dict:
    return {
        "hard": {
            "gateway": compat_passes(compat["gateway"]),
            "dhcp": compat_passes(compat["dhcp"]),
            "pool": compat_passes(compat["pool"]),
            "dhcpNetwork": compat_passes(compat["dhcpNetwork"]),
            "firewall": compat_passes(compat["firewall"]),
        },
        "soft": {
            "hotspot": compat_passes(compat["hotspot"]),
            "renzfiMarkers": renzfi_managed,
        },
    }


def assign_candidate_outcome(
    *,
    requirements: dict,
    compat: dict[str, str],
    overlap_rejected: bool,
) -> tuple[str, str, str, list[str]]:
    """Mirror C++ assignCandidateOutcome() — status and confidence are independent."""
    reasons: list[str] = []
    hard = requirements["hard"]
    soft = requirements["soft"]
    if overlap_rejected:
        return "rejected_overlap", "low", "", ["network_overlap"]

    core_complete = all(hard[k] for k in ("gateway", "dhcp", "pool", "dhcpNetwork", "firewall"))
    if core_complete:
        if not soft["hotspot"]:
            if compat["hotspot"] == UNKNOWN:
                reasons.append("hotspot_not_inspected")
            else:
                reasons.append("hotspot_not_detected")
        if compat["firewall"] == WARNING:
            reasons.append("firewall_inspection_limited")
        confidence = "high" if not reasons else "medium"
        return "compatible_candidate", confidence, "generic", reasons

    if not hard["gateway"]:
        reasons.append("gateway_not_on_bridge")
    if not hard["dhcp"]:
        reasons.append("missing_dhcp_server")
    if not hard["pool"]:
        reasons.append("pool_not_linked")
    if not hard["dhcpNetwork"]:
        reasons.append("missing_dhcp_network")
    if compat["firewall"] == WARNING:
        reasons.append("firewall_inspection_limited")
    elif not hard["firewall"]:
        reasons.append("firewall_rule_not_detected")
    if compat["hotspot"] == UNKNOWN:
        reasons.append("hotspot_not_inspected")
    elif not soft["hotspot"]:
        reasons.append("hotspot_not_detected")

    missing_core = sum(1 for k in ("dhcp", "pool", "dhcpNetwork", "firewall") if not hard[k])
    if hard["gateway"] and 1 <= missing_core <= 2:
        confidence = "high"
    elif (not hard["gateway"]) or missing_core >= 3:
        confidence = "low"
    else:
        confidence = "medium"
    return "partial_candidate", confidence, "", reasons


def evaluate_fixture(fixture: dict) -> dict:
    """Minimal Python mirror of detectCandidates() for the bridge-lan fixture."""
    esp_ip = fixture["esp_ip"]
    esp_subnet = fixture["esp_subnet_cidr"]
    api_ok = api_access_ok(fixture["filter_rules"], esp_ip)
    hotspot = any(h.get("disabled") != "true" for h in fixture["hotspots"])

    candidates: list[dict] = []

    for bridge in fixture["bridges"]:
        bridge_name = bridge["name"]
        for addr_row in fixture["addresses"]:
            if addr_row["interface"] != bridge_name:
                continue
            gateway_cidr = addr_row["address"]
            guest_network = network_from_gateway_cidr(gateway_cidr)

            overlap = is_problematic_esp_subnet_overlap(guest_network, esp_subnet, esp_ip)
            if overlap:
                candidates.append({"status": "rejected_overlap", "bridgeName": bridge_name})
                continue

            dhcp_found = False
            pool_found = False
            dhcp_network_found = False
            dhcp_server_name = ""
            pool_name = ""
            pool_range = ""
            dhcp_network = ""

            for dhcp in fixture["dhcp_servers"]:
                if dhcp["interface"] != bridge_name or dhcp.get("disabled") == "true":
                    continue
                dhcp_found = True
                dhcp_server_name = dhcp["name"]
                pool_ref = dhcp.get("address-pool") or dhcp.get("address-pools", "")
                for pool in fixture["pools"]:
                    if pool["name"] == pool_ref:
                        pool_found = True
                        pool_name = pool["name"]
                        pool_range = pool["ranges"]
                        break
                for net in fixture["dhcp_networks"]:
                    if net["address"] == guest_network:
                        dhcp_network_found = True
                        dhcp_network = net["address"]
                        break
                break

            hotspot_on_bridge = any(
                h.get("interface") == bridge_name and h.get("disabled") != "true"
                for h in fixture["hotspots"]
            ) or hotspot
            hotspot_inspection_ok = bool(fixture.get("hotspots"))
            firewall_limited = fixture.get("firewall_limited", False)

            compat = build_compatibility(
                bridge=True,
                gateway=True,
                dhcp=dhcp_found and bool(dhcp_server_name),
                pool=pool_found and bool(pool_name),
                dhcp_network=dhcp_network_found and bool(dhcp_network),
                api_ok=api_ok,
                firewall_limited=firewall_limited,
                hotspot_on_bridge=hotspot_on_bridge,
                hotspot_inspection_ok=hotspot_inspection_ok,
            )
            inspection = build_inspection(
                firewall_print_ok=not firewall_limited,
                hotspot_inspection_attempted=bool(fixture.get("hotspots")),
                hotspot_inspection_ok=hotspot_inspection_ok,
            )
            requirements = build_requirements(compat, renzfi_managed=False)
            status, confidence, origin, reasons = assign_candidate_outcome(
                requirements=requirements,
                compat=compat,
                overlap_rejected=False,
            )
            score = compute_adoption_score(compat)
            gateway_ip = gateway_cidr.split("/")[0]

            candidates.append({
                "id": make_candidate_id(bridge_name, gateway_ip, len(candidates) + 1),
                "status": status,
                "origin": origin,
                "confidence": confidence,
                "confidenceReasons": reasons,
                "adoptionScore": score,
                "compatibility": compat,
                "inspection": inspection,
                "requirements": requirements,
                "bridgeName": bridge_name,
                "gatewayCidr": gateway_cidr,
                "guestNetwork": guest_network,
                "dhcpServerName": dhcp_server_name,
                "poolName": pool_name,
                "poolRange": pool_range,
                "dhcpNetwork": dhcp_network,
                "genericCompatible": origin == "generic",
                "apiAccessOk": api_ok,
                "hotspotDetected": hotspot,
            })

    compatible = [c for c in candidates if c["status"] == "compatible_candidate"]
    partial = [c for c in candidates if c["status"] == "partial_candidate"]
    rejected = [c for c in candidates if c["status"] == "rejected_overlap"]

    if len(compatible) >= 1:
        scan_status = "compatible_candidate"
    elif partial:
        scan_status = "partial_only"
    elif rejected:
        scan_status = "no_compatible_candidate"
    else:
        scan_status = "no_compatible_candidate"

    return {
        "candidates": candidates,
        "candidateCount": len(candidates),
        "compatibleCount": len(compatible),
        "scanStatus": scan_status,
    }


def static_source_guards(source: str) -> list[str]:
    errors: list[str] = []
    if "networkFromGatewayCidr" not in source:
        errors.append("ExistingNetworkScan must define networkFromGatewayCidr()")
    if "ipv4ToHostOrder(ip) & mask" not in source.replace(" ", ""):
        if "netHost = ipv4ToHostOrder(ip) & mask" not in source:
            errors.append(
                "networkFromGatewayCidr must mask gateway IP to network address "
                "(10.10.10.1/24 -> 10.10.10.0/24)")
    if "isProblematicEspSubnetOverlap" not in source:
        errors.append(
            "ExistingNetworkScan must use isProblematicEspSubnetOverlap() so "
            "ESP-on-guest-LAN is not rejected as overlap")
    if "logCandidateEvaluation" not in source:
        errors.append("ExistingNetworkScan must log per-bridge candidate diagnostics")
    if "[scan] evaluating bridge=" not in source:
        errors.append("Candidate diagnostics must log [scan] evaluating bridge=...")
    if '"missing_dhcp_network"' not in source:
        errors.append("Rejections must log explicit missing_dhcp_network reason")
    if '"firewall_api_not_allowed"' not in source:
        errors.append("Rejections must log explicit firewall_api_not_allowed reason")
    if '"network_overlap"' not in source:
        errors.append("Rejections must log explicit network_overlap reason")
    if "singleCompatibleStatus" in source:
        errors.append(
            "scanStatus must not mirror per-candidate origin — "
            "use scanStatus=compatible_candidate with candidate.origin instead")
    if 'row["origin"]' not in source:
        errors.append("serializeScanJson must emit candidate origin (generic|renzfi|imported)")
    if 'row["confidence"]' not in source:
        errors.append("serializeScanJson must emit candidate confidence (high|medium|low)")
    if "confidenceReasons" not in source:
        errors.append("serializeScanJson must emit confidenceReasons array")
    if 'createNestedObject("compatibility")' not in source:
        errors.append("serializeScanJson must emit compatibility breakdown object")
    if "assignCandidateOutcome" not in source:
        errors.append(
            "ExistingNetworkScan must compute status and confidence independently "
            "via assignCandidateOutcome()")
    if "kScanSchemaVersion" not in source:
        errors.append("ExistingNetworkScan must define kScanSchemaVersion")
    if 'dataOut["schemaVersion"]' not in source:
        errors.append("serializeScanJson must emit schemaVersion")
    if "CompatLevel::Pass" not in source:
        errors.append("compatibility checks must use pass/fail/unknown/warning enums")
    if "adoptionScore" not in source:
        errors.append("serializeScanJson must emit adoptionScore")
    if 'createNestedObject("inspection")' not in source:
        errors.append("serializeScanJson must emit inspection breakdown object")
    if 'dataOut["scanDurationMs"]' not in source:
        errors.append("serializeScanJson must emit scanDurationMs")
    if 'createNestedObject("driver")' not in source:
        errors.append("serializeScanJson must emit driver metadata")
    if "makeCandidateId" not in source:
        errors.append("ExistingNetworkScan must assign stable candidate ids via makeCandidateId()")
    if 'createNestedObject("requirements")' not in source:
        errors.append("serializeScanJson must emit requirements.hard/soft breakdown")
    if "generic_compatible_candidate" in source:
        errors.append(
            "ExistingNetworkScan must not use generic_compatible_candidate status — "
            "use status=compatible_candidate with origin=generic|renzfi")
    if 'row["type"]' in source and 'row["origin"]' not in source:
        errors.append("Candidate provenance must use origin, not type")
    return errors


def main() -> int:
    errors: list[str] = []

    source = SCAN_CPP.read_text(encoding="utf-8")
    errors.extend(static_source_guards(source))

    result = evaluate_fixture(BRIDGE_LAN_FIXTURE)

    if result["candidateCount"] != 1:
        errors.append(
            f"bridge-lan fixture: expected candidateCount==1, got {result['candidateCount']}")
    if result["compatibleCount"] != 1:
        errors.append(
            f"bridge-lan fixture: expected 1 compatible candidate, got {result['compatibleCount']}")
    if not result["candidates"]:
        errors.append("bridge-lan fixture: no candidates produced")
    else:
        cand = result["candidates"][0]
        if cand["status"] != "compatible_candidate":
            errors.append(
                f"bridge-lan fixture: expected status compatible_candidate, "
                f"got {cand['status']}")
        if cand.get("origin") != "generic":
            errors.append(
                f"bridge-lan fixture: expected origin generic, got {cand.get('origin')}")
        if cand.get("confidence") != "high":
            errors.append(
                f"bridge-lan fixture: expected confidence high, got {cand.get('confidence')}")
        if cand.get("confidenceReasons"):
            errors.append(
                "bridge-lan fixture: expected empty confidenceReasons for full match")
        compat = cand.get("compatibility") or {}
        for key in ("bridge", "gateway", "dhcp", "pool", "dhcpNetwork", "firewall", "hotspot"):
            if compat.get(key) != PASS:
                errors.append(
                    f"bridge-lan fixture: compatibility.{key} must be pass, got {compat.get(key)}")
        if cand.get("adoptionScore", 0) < 95:
            errors.append(
                f"bridge-lan fixture: expected adoptionScore >= 95, got {cand.get('adoptionScore')}")
        if cand.get("id") != "bridge-lan@10.10.10.1":
            errors.append(
                f"bridge-lan fixture: expected id bridge-lan@10.10.10.1, got {cand.get('id')}")
        inspection = cand.get("inspection") or {}
        for key in ("bridge", "gateway", "dhcp", "pool", "dhcpNetwork", "firewall", "hotspot"):
            if inspection.get(key) != COMPLETE:
                errors.append(
                    f"bridge-lan fixture: inspection.{key} must be complete, got {inspection.get(key)}")
        if inspection.get("nat") != NOT_SUPPORTED:
            errors.append(
                f"bridge-lan fixture: inspection.nat must be not_supported, got {inspection.get('nat')}")
        req = cand.get("requirements") or {}
        hard = req.get("hard") or {}
        if not all(hard.get(k) for k in ("gateway", "dhcp", "pool", "dhcpNetwork", "firewall")):
            errors.append("bridge-lan fixture: all hard requirements must be satisfied")
        soft = req.get("soft") or {}
        if not soft.get("hotspot"):
            errors.append("bridge-lan fixture: soft hotspot requirement should be satisfied")
        if cand.get("guestNetwork") != "10.10.10.0/24":
            errors.append(
                f"bridge-lan fixture: guestNetwork must be 10.10.10.0/24, "
                f"got {cand.get('guestNetwork')}")
        if cand.get("dhcpNetwork") != "10.10.10.0/24":
            errors.append(
                f"bridge-lan fixture: dhcpNetwork must be 10.10.10.0/24, "
                f"got {cand.get('dhcpNetwork')}")
        if not cand.get("apiAccessOk"):
            errors.append("bridge-lan fixture: apiAccessOk must be true")
        if cand.get("poolName") != "pool-guest":
            errors.append(
                f"bridge-lan fixture: poolName must be pool-guest, got {cand.get('poolName')}")

    if result["scanStatus"] != "compatible_candidate":
        errors.append(
            f"bridge-lan fixture: expected scanStatus compatible_candidate, "
            f"got {result['scanStatus']}")

    # Regression sentinel: network must be masked to .0/24, not gateway/24.
    if network_from_gateway_cidr("10.10.10.1/24") != "10.10.10.0/24":
        errors.append(
            "networkFromGatewayCidr must return 10.10.10.0/24 for gateway 10.10.10.1/24")

    # Regression sentinel: old overlap logic rejected identical /24.
    if is_problematic_esp_subnet_overlap("10.10.10.0/24", "10.10.10.0/24", "10.10.10.2"):
        errors.append(
            "overlap logic still rejects ESP on identical guest /24 subnet")

    # Independent status/confidence scenarios (must not be 1:1 mapped).
    compat_ok = build_compatibility(
        bridge=True, gateway=True, dhcp=True, pool=True, dhcp_network=True,
        api_ok=True, firewall_limited=False, hotspot_on_bridge=False,
        hotspot_inspection_ok=True)
    req_ok = build_requirements(compat_ok, renzfi_managed=False)
    st, conf, _, reasons = assign_candidate_outcome(
        requirements=req_ok, compat=compat_ok, overlap_rejected=False)
    if st != "compatible_candidate" or conf != "medium" or "hotspot_not_detected" not in reasons:
        errors.append(
            "compatible_candidate without hotspot must stay adoptable but "
            "confidence=medium with hotspot_not_detected reason")
    compat_partial = build_compatibility(
        bridge=True, gateway=True, dhcp=True, pool=True, dhcp_network=False,
        api_ok=False, firewall_limited=False, hotspot_on_bridge=False,
        hotspot_inspection_ok=True)
    req_partial = build_requirements(compat_partial, renzfi_managed=False)
    st, conf, _, _ = assign_candidate_outcome(
        requirements=req_partial, compat=compat_partial, overlap_rejected=False)
    if st != "partial_candidate" or conf != "high":
        errors.append(
            "partial_candidate with known missing pieces must allow confidence=high")

    if errors:
        for err in errors:
            print(f"existing-network-scan-candidate-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("existing-network-scan-candidate-check: OK (bridge-lan candidate evaluation passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
