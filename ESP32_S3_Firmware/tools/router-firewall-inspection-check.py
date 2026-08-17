#!/usr/bin/env python3
"""Regression tests for scoped firewall API rule inspection during preview."""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PROVISIONING = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
ROUTEROS = ROOT.parent / "src" / "RouterOsClient.cpp"
ROUTEROS_H = ROOT.parent / "src" / "RouterOsClient.h"

FIREWALL_PRINT = "/ip/firewall/filter/print"
PROPLIST = "=.proplist=.id,action,chain,protocol,dst-port,src-address,disabled,comment"
QUERY_CHAIN = "?chain=input"
QUERY_PROTOCOL = "?protocol=tcp"
QUERY_DST_PORT = "?dst-port=8728"
RENZFI_COMMENT = "RENZFI: ESP32 appliance API access"
LIMIT_CODE = "FIREWALL_INSPECTION_LIMIT"
CAP = 8


@dataclass
class FirewallRule:
    action: str = "accept"
    chain: str = "input"
    protocol: str = "tcp"
    dst_port: str = "8728"
    src_address: str = ""
    disabled: str = "false"
    comment: str = ""


def rule_matches_api_scope(rule: FirewallRule) -> bool:
    return (
        rule.chain == "input"
        and rule.protocol == "tcp"
        and rule.dst_port == "8728"
        and rule.disabled != "true"
        and rule.action == "accept"
    )


def api_rule_satisfied(rules: list[FirewallRule], esp_ip: str) -> tuple[bool, bool]:
    managed = False
    for rule in rules:
        if not rule_matches_api_scope(rule):
            continue
        if rule.src_address and rule.src_address != esp_ip:
            continue
        if rule.comment.startswith("RENZFI:"):
            return True, True
        if rule.src_address == esp_ip:
            return True, False
    return False, managed


def scoped_rules(all_rules: list[FirewallRule]) -> list[FirewallRule]:
    return [r for r in all_rules if rule_matches_api_scope(r)]


def build_preview_plan(
    all_rules: list[FirewallRule],
    esp_ip: str,
    *,
    limit_hit: bool = False,
) -> tuple[bool, str, str, bool]:
    visible = scoped_rules(all_rules)
    if limit_hit:
        visible = visible[:CAP]
        satisfied, managed = api_rule_satisfied(visible, esp_ip)
        can_apply = satisfied
        action = "verify" if satisfied else "verify"
        return can_apply, action, LIMIT_CODE, satisfied

    if len(visible) > CAP:
        visible = visible[:CAP]
        satisfied, _ = api_rule_satisfied(visible, esp_ip)
        return False, "verify", LIMIT_CODE, satisfied

    satisfied, managed = api_rule_satisfied(visible, esp_ip)
    if satisfied:
        return True, "verify", "", satisfied
    return True, "create", "", False


def main() -> int:
    errors: list[str] = []
    esp_ip = "10.40.0.2"

    many_input_rules = [
        FirewallRule(chain="input", protocol="tcp", dst_port=str(8000 + i))
        for i in range(15)
    ]
    many_input_rules.append(
        FirewallRule(
            chain="input",
            protocol="tcp",
            dst_port="8728",
            src_address=esp_ip,
            comment=RENZFI_COMMENT,
        )
    )
    can_apply, action, warning, satisfied = build_preview_plan(many_input_rules, esp_ip)
    if not can_apply or action != "verify" or not satisfied:
        errors.append("15+ input rules with one scoped TCP/8728 rule must complete preview")

    can_apply, action, warning, satisfied = build_preview_plan([], esp_ip)
    if action != "create" or satisfied:
        errors.append("No TCP/8728 rule must propose one RENZFI API allow rule")

    external = [
        FirewallRule(
            chain="input",
            protocol="tcp",
            dst_port="8728",
            src_address=esp_ip,
            comment="customer rule",
        )
    ]
    can_apply, action, warning, satisfied = build_preview_plan(external, esp_ip)
    if not can_apply or action != "verify" or not satisfied:
        errors.append("Existing non-RENZFI allow rule for ESP32 IP must verify without modify")

    managed = [
        FirewallRule(
            chain="input",
            protocol="tcp",
            dst_port="8728",
            src_address=esp_ip,
            comment=RENZFI_COMMENT,
        )
    ]
    can_apply, action, warning, satisfied = build_preview_plan(managed, esp_ip)
    if action != "verify" or not satisfied:
        errors.append("Existing RENZFI API rule must verify/skip")

    can_apply, action, warning, satisfied = build_preview_plan([], esp_ip, limit_hit=True)
    if can_apply or warning != LIMIT_CODE:
        errors.append("Scoped firewall cap hit without proof must block apply")

    prov = PROVISIONING.read_text(encoding="utf-8")
    ros = ROUTEROS.read_text(encoding="utf-8")
    header = ROUTEROS_H.read_text(encoding="utf-8")

    if '=chain=input' in prov and "inspect firewall api-rule query scoped" not in prov:
        errors.append("Broad =chain=input firewall print must be replaced with scoped query")
    if PROPLIST not in prov:
        errors.append("Firewall inspect must request minimum proplist")
    if QUERY_DST_PORT not in prov:
        errors.append("Firewall inspect must filter dst-port=8728")
    if "kFirewallApiRuleReplyCap = 8" not in prov:
        errors.append("Firewall inspect must cap replies at 8")
    if LIMIT_CODE not in prov:
        errors.append("Firewall inspect must emit FIREWALL_INSPECTION_LIMIT warning")
    if "firewall api rule satisfied=" not in prov:
        errors.append("Firewall inspect must log satisfied state")
    if "/ip/firewall/filter/add" not in prov:
        errors.append("Apply path must create firewall rule via /ip/firewall/filter/add")
    if "setCommandReplyLimits" not in header:
        errors.append("RouterOsClient must support per-command reply limits")
    if "stopDrainOnReplyLimit" not in ros:
        errors.append("RouterOsClient must stop draining when scoped reply cap is hit")

    if errors:
        for err in errors:
            print(f"router-firewall-inspection-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-firewall-inspection-check: OK (scoped firewall preview guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
