import type { StatusTone } from "@/lib/dashboardDisplay";
import type { AccessPointRecord } from "@/services/accessPoints";
import type {
  NetworkAddressSnapshot,
  ValidatedApManagementNetwork,
} from "@/lib/apManagementIpRecommendations";

export type EthernetPortSnapshot = {
  name: string;
  defaultName?: string;
  running?: boolean | string;
  disabled?: boolean | string;
  comment?: string;
  bridge?: string;
};

export type PortKind = "wan" | "esp32" | "accessPoint" | "available" | "unknown";

export type PortDetailField = {
  label: string;
  value: string;
  mono?: boolean;
  multiline?: boolean;
};

export type PortLayoutEntry = {
  slot: string;
  routerOsName?: string;
  defaultName?: string;
  kind: PortKind;
  role: string;
  linkLabel: string;
  linkTone: StatusTone;
  linkSource: "routeros" | "wan_observation" | "unavailable";
  detail?: string;
  bridge?: string;
  comment?: string;
  cardLines: string[];
  setupSteps: string[] | null;
  details: PortDetailField[];
};

export type PortObservationFreshness = "last_observed" | "unavailable";

function inferEtherPortNames(boardName?: string, identity?: string): string[] | null {
  const label = `${boardName ?? ""} ${identity ?? ""}`.toLowerCase();
  if (label.includes("hex")) return ["ether1", "ether2", "ether3", "ether4", "ether5"];
  if (
    label.includes("hap ac2") ||
    label.includes("hap ac³") ||
    label.includes("hap ac3") ||
    label.includes("hap lite") ||
    label.includes("hap mini")
  ) {
    return ["ether1", "ether2", "ether3", "ether4", "ether5"];
  }
  return null;
}

function normalizePortKey(name: string): string {
  const lower = name.trim().toLowerCase();
  const match = lower.match(/^(ether\d+)/);
  return match?.[1] ?? lower;
}

function normalizeBool(value: boolean | string | undefined): boolean | undefined {
  if (value === true || value === "true") return true;
  if (value === false || value === "false") return false;
  return undefined;
}

function normalizeEthernetPort(raw: EthernetPortSnapshot): EthernetPortSnapshot {
  return {
    ...raw,
    running: normalizeBool(raw.running),
    disabled: normalizeBool(raw.disabled),
  };
}

function mapLivePortsBySlot(
  ethernetPorts?: EthernetPortSnapshot[],
): Map<string, EthernetPortSnapshot> {
  const map = new Map<string, EthernetPortSnapshot>();
  for (const raw of ethernetPorts ?? []) {
    const port = normalizeEthernetPort(raw);
    const defaultKey = port.defaultName ? normalizePortKey(port.defaultName) : "";
    if (/^ether\d+$/.test(defaultKey)) map.set(defaultKey, port);
    const nameKey = normalizePortKey(port.name);
    if (/^ether\d+$/.test(nameKey)) map.set(nameKey, port);
    if (!map.has(nameKey)) map.set(nameKey, port);

    const blob = `${port.name} ${port.comment ?? ""}`.toLowerCase();
    if ((blob.includes("esp32") || blob.includes("controller")) && !map.has("ether2")) {
      map.set("ether2", port);
    }
    if (blob.includes("wan") && !map.has("ether1")) {
      map.set("ether1", port);
    }
  }
  return map;
}

function linkFromPort(port: EthernetPortSnapshot | undefined): {
  label: string;
  tone: StatusTone;
  source: PortLayoutEntry["linkSource"];
} {
  if (!port) {
    return { label: "Unavailable", tone: "unknown", source: "unavailable" };
  }
  if (port.disabled === true) {
    return { label: "Disabled", tone: "neutral", source: "routeros" };
  }
  if (port.running === true) {
    return { label: "Running", tone: "ok", source: "routeros" };
  }
  if (port.running === false) {
    return { label: "Link down", tone: "bad", source: "routeros" };
  }
  return { label: "Unavailable", tone: "unknown", source: "unavailable" };
}

function isEsp32Port(port?: EthernetPortSnapshot): boolean {
  if (!port) return false;
  const blob = `${port.name} ${port.comment ?? ""}`.toLowerCase();
  return blob.includes("esp32") || blob.includes("controller");
}

function isWanPort(
  slot: string,
  port: EthernetPortSnapshot | undefined,
  wanInterface?: string,
): boolean {
  if (!wanInterface) return false;
  const wanKey = normalizePortKey(wanInterface);
  if (slot === wanKey) return true;
  if (port?.name?.toLowerCase() === wanInterface.toLowerCase()) return true;
  const blob = `${port?.name ?? ""} ${port?.comment ?? ""}`.toLowerCase();
  return blob.includes("wan");
}

function classifyPort(input: {
  slot: string;
  port?: EthernetPortSnapshot;
  wanInterface?: string;
  detectedBridgePort?: string;
}): PortKind {
  const { slot, port, wanInterface, detectedBridgePort } = input;
  if (isWanPort(slot, port, wanInterface)) return "wan";
  if (isEsp32Port(port)) return "esp32";
  if (detectedBridgePort && slot === normalizePortKey(detectedBridgePort)) return "accessPoint";
  if (port?.running === false && port.disabled !== true) return "available";
  if (port?.bridge && !detectedBridgePort) return "available";
  if (!port) return "unknown";
  return "available";
}

function roleLabel(kind: PortKind): string {
  switch (kind) {
    case "wan":
      return "WAN";
    case "esp32":
      return "ESP32 / Controller";
    case "accessPoint":
      return "Access Point";
    case "available":
      return "Available";
    default:
      return "Unknown";
  }
}

function buildSetupSteps(input: {
  kind: PortKind;
  validated: ValidatedApManagementNetwork;
  guestDns?: string;
}): string[] | null {
  if (input.kind !== "available") return null;
  const steps = [
    "Connect the AP to this port.",
    "Set AP mode / Access Point mode on the AP.",
    "Disable DHCP Server on the AP.",
    "Configure a static management IP on the AP.",
  ];
  if (input.validated.validated && input.validated.mask) {
    steps.push(`Subnet mask: ${input.validated.mask}`);
    if (input.validated.gateway) steps.push(`Gateway: ${input.validated.gateway}`);
    if (input.guestDns?.trim()) steps.push(`DNS: ${input.guestDns.trim()}`);
    if (input.validated.recommendations.length > 0) {
      steps.push(
        `Suggested addresses (${input.validated.sourceLabel}): ${input.validated.recommendations.join(", ")}`,
      );
    }
  } else {
    steps.push("Renz-Fi cannot safely determine a static AP management IP yet.");
    steps.push(
      "Run Router Sync, then use an IP on the subnet RouterOS assigns to the guest bridge.",
    );
  }
  steps.push("Save the AP configuration in its own web interface.");
  return steps;
}

function buildCardLines(input: {
  kind: PortKind;
  port?: EthernetPortSnapshot;
  registeredAp?: AccessPointRecord | null;
  validated: ValidatedApManagementNetwork;
  guestNetwork?: string;
  guestGateway?: string;
}): string[] {
  const lines: string[] = [];
  const { kind, port, registeredAp, validated, guestNetwork, guestGateway } = input;

  if (kind === "accessPoint" && registeredAp) {
    if (registeredAp.managementIp?.trim()) {
      lines.push(`AP IP: ${registeredAp.managementIp.trim()}`);
    }
    if (registeredAp.model?.trim()) lines.push(`Model: ${registeredAp.model.trim()}`);
    else if (registeredAp.name?.trim()) lines.push(`AP: ${registeredAp.name.trim()}`);
    if (registeredAp.ssid?.trim()) lines.push(`SSID: ${registeredAp.ssid.trim()}`);
    if (port?.bridge) lines.push(`Bridge: ${port.bridge}`);
  } else if (kind === "esp32") {
    lines.push("Renz-Fi controller Ethernet link.");
    if (port?.bridge) lines.push(`Bridge: ${port.bridge}`);
  } else if (kind === "wan") {
    lines.push("Internet uplink.");
  } else if (kind === "available") {
    if (validated.validated && validated.recommendations[0]) {
      lines.push(`Suggested AP IP: ${validated.recommendations[0]}`);
    } else {
      lines.push("AP setup port — no safe IP recommendation yet.");
    }
    if (port?.bridge) lines.push(`Bridge: ${port.bridge}`);
  }

  if (guestNetwork?.trim() && (kind === "accessPoint" || kind === "available")) {
    lines.push(`Guest HotSpot: ${guestNetwork.trim()}`);
  }
  if (guestGateway?.trim() && kind === "accessPoint") {
    lines.push(`Guest gateway: ${guestGateway.trim()}`);
  }

  return lines;
}

function buildIpAddressingExplanation(input: {
  kind: PortKind;
  registeredAp?: AccessPointRecord | null;
  guestNetwork?: string;
  validated: ValidatedApManagementNetwork;
  bridge?: string;
  guestBridgeName?: string;
}): string | undefined {
  if (input.kind !== "accessPoint" && input.kind !== "available") return undefined;

  const apIp = input.registeredAp?.managementIp?.trim();
  const guest = input.guestNetwork?.trim();
  const bridge = input.bridge ?? input.guestBridgeName;

  if (input.kind === "accessPoint" && apIp) {
    let text = `AP management IP ${apIp} belongs to the connected AP device, not this Ethernet port.`;
    if (input.validated.validated && input.validated.cidr) {
      text += ` RouterOS validates management subnet ${input.validated.cidr}.`;
    }
    if (guest && input.validated.cidr && guest !== input.validated.cidr) {
      text += ` Guest HotSpot network ${guest} is used for client addressing and is separate from AP management.`;
    }
    if (bridge) text += ` Port is a member of ${bridge}.`;
    return text;
  }

  if (input.kind === "available") {
    return bridge
      ? `No device detected on this port. When an AP is connected to ${bridge}, assign its management IP on a subnet RouterOS validates — not inferred from guest HotSpot network alone.`
      : "No device detected. Configure AP management IP on the AP device after Router Sync confirms the management subnet.";
  }

  return undefined;
}

function buildPortDetails(input: {
  entry: Omit<PortLayoutEntry, "details" | "cardLines" | "setupSteps">;
  registeredAp?: AccessPointRecord | null;
  guestNetwork?: string;
  guestGateway?: string;
  guestDns?: string;
  guestBridgeName?: string;
  validated: ValidatedApManagementNetwork;
  networkAddresses?: NetworkAddressSnapshot[];
  lastObservedAt?: string;
  freshness: PortObservationFreshness;
}): PortDetailField[] {
  const fields: PortDetailField[] = [];
  const { entry, registeredAp, guestNetwork, guestGateway, guestDns, validated, networkAddresses } =
    input;

  fields.push({
    label: "Observation",
    value:
      input.freshness === "last_observed" && input.lastObservedAt
        ? `Last observed ${input.lastObservedAt} (Router Sync / Refresh)`
        : "Not available — run Router Sync",
  });

  fields.push({
    label: "Interface",
    value: entry.routerOsName ?? entry.slot,
    mono: true,
  });

  if (entry.defaultName && entry.defaultName !== entry.routerOsName) {
    fields.push({ label: "Default name", value: entry.defaultName, mono: true });
  }

  fields.push({ label: "Role", value: entry.role });
  fields.push({ label: "Link", value: entry.linkLabel });

  if (entry.bridge) {
    fields.push({ label: "Bridge", value: entry.bridge, mono: true });
  }

  if (entry.comment) {
    fields.push({ label: "Comment", value: entry.comment });
  }

  const bridgeAddrs = (networkAddresses ?? []).filter(
    (row) =>
      entry.bridge && row.interface?.trim().toLowerCase() === entry.bridge.trim().toLowerCase(),
  );
  for (const row of bridgeAddrs.slice(0, 3)) {
    fields.push({
      label: `RouterOS address (${row.interface})`,
      value: row.address,
      mono: true,
    });
  }

  if (entry.kind === "accessPoint" && registeredAp) {
    if (registeredAp.name?.trim()) {
      fields.push({ label: "Detected AP", value: registeredAp.name.trim() });
    }
    if (registeredAp.vendor) fields.push({ label: "Brand", value: String(registeredAp.vendor) });
    if (registeredAp.model?.trim())
      fields.push({ label: "Model", value: registeredAp.model.trim() });
    if (registeredAp.managementIp?.trim()) {
      fields.push({ label: "AP IP", value: registeredAp.managementIp.trim(), mono: true });
    }
    if (registeredAp.ssid?.trim()) fields.push({ label: "SSID", value: registeredAp.ssid.trim() });
  }

  if (guestNetwork?.trim() && (entry.kind === "accessPoint" || entry.kind === "available")) {
    fields.push({
      label: "Guest network (HotSpot)",
      value: guestNetwork.trim(),
      mono: true,
    });
  }
  if (guestGateway?.trim() && entry.kind === "accessPoint") {
    fields.push({ label: "Guest gateway", value: guestGateway.trim(), mono: true });
  }
  if (guestDns?.trim() && entry.kind === "accessPoint") {
    fields.push({ label: "DNS", value: guestDns.trim(), mono: true });
  }

  if (validated.validated && validated.cidr && entry.kind === "available") {
    fields.push({
      label: "Validated AP management subnet",
      value: `${validated.cidr} (${validated.sourceLabel})`,
      mono: true,
    });
    if (validated.gateway) {
      fields.push({ label: "Management gateway", value: validated.gateway, mono: true });
    }
  }

  const ipExplain = buildIpAddressingExplanation({
    kind: entry.kind,
    registeredAp,
    guestNetwork,
    validated,
    bridge: entry.bridge,
    guestBridgeName: input.guestBridgeName,
  });
  if (ipExplain) {
    fields.push({ label: "IP addressing", value: ipExplain, multiline: true });
  }

  return fields;
}

export function buildPortLayoutEntries(input: {
  boardName?: string;
  identity?: string;
  wanInterface?: string;
  wanLink?: string;
  detectedBridgePort?: string;
  registeredAp?: AccessPointRecord | null;
  ethernetPorts?: EthernetPortSnapshot[];
  ethernetPortsKnown?: boolean;
  networkAddresses?: NetworkAddressSnapshot[];
  guestNetwork?: string;
  guestGateway?: string;
  guestDns?: string;
  guestBridgeName?: string;
  validatedManagement: ValidatedApManagementNetwork;
  lastObservedAt?: string;
}): {
  ports: PortLayoutEntry[];
  boardLabel: string;
  freshness: PortObservationFreshness;
} {
  const portNames = inferEtherPortNames(input.boardName, input.identity);
  const liveBySlot = mapLivePortsBySlot(input.ethernetPorts);
  const boardLabel =
    input.identity?.trim() ||
    input.boardName?.trim() ||
    (portNames ? "MikroTik router" : "MikroTik router (model unknown)");

  const freshness: PortObservationFreshness =
    input.ethernetPortsKnown === true && (input.ethernetPorts?.length ?? 0) > 0
      ? "last_observed"
      : "unavailable";

  if (!portNames) {
    return { boardLabel, ports: [], freshness: "unavailable" };
  }

  const wanIface = input.wanInterface?.trim();
  const wanUp =
    input.wanLink === "true" ||
    input.wanLink === "up" ||
    input.wanLink === "UP" ||
    input.wanLink === "connected";

  const ports: PortLayoutEntry[] = portNames.map((slot) => {
    const live = liveBySlot.get(slot);
    const kind = classifyPort({
      slot,
      port: live,
      wanInterface: wanIface,
      detectedBridgePort: input.detectedBridgePort,
    });
    const role = roleLabel(kind);

    let linkLabel: string;
    let linkTone: StatusTone;
    let linkSource: PortLayoutEntry["linkSource"];

    const isWan = kind === "wan";
    if (isWan && input.wanLink && freshness === "unavailable") {
      linkLabel = wanUp ? "Running" : "Link down";
      linkTone = wanUp ? "ok" : "bad";
      linkSource = "wan_observation";
    } else {
      const link = linkFromPort(live);
      linkLabel = link.label;
      linkTone = link.tone;
      linkSource = link.source;
    }

    let detail: string | undefined;
    if (kind === "accessPoint" && input.registeredAp?.managementIp) {
      detail = `AP: ${input.registeredAp.managementIp}`;
    } else if (kind === "esp32") {
      detail = "ESP32 controller";
    } else if (isWan && freshness === "unavailable" && !input.wanLink) {
      detail = "Run Router Sync for interface state";
    } else if (live?.bridge) {
      detail = live.bridge;
    }

    const shell: Omit<PortLayoutEntry, "details" | "cardLines" | "setupSteps"> = {
      slot,
      routerOsName: live?.name,
      defaultName: live?.defaultName,
      kind,
      role,
      linkLabel,
      linkTone,
      linkSource,
      detail,
      bridge: live?.bridge,
      comment: live?.comment,
    };

    const cardLines = buildCardLines({
      kind,
      port: live,
      registeredAp: input.registeredAp,
      validated: input.validatedManagement,
      guestNetwork: input.guestNetwork,
      guestGateway: input.guestGateway,
    });

    const setupSteps = buildSetupSteps({
      kind,
      validated: input.validatedManagement,
      guestDns: input.guestDns,
    });

    return {
      ...shell,
      cardLines,
      setupSteps,
      details: buildPortDetails({
        entry: shell,
        registeredAp: input.registeredAp,
        guestNetwork: input.guestNetwork,
        guestGateway: input.guestGateway,
        guestDns: input.guestDns,
        guestBridgeName: input.guestBridgeName,
        validated: input.validatedManagement,
        networkAddresses: input.networkAddresses,
        lastObservedAt: input.lastObservedAt,
        freshness,
      }),
    };
  });

  return { boardLabel, ports, freshness };
}
