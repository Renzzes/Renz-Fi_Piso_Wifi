import type { AccessPointRecord } from "@/services/accessPoints";

export type NetworkAddressSnapshot = {
  interface: string;
  address: string;
  network?: string;
  actualInterface?: string;
};

function parseIpv4Octets(ip: string): number[] | null {
  const parts = ip.trim().split(".");
  if (parts.length !== 4) return null;
  const octets: number[] = [];
  for (const part of parts) {
    if (!/^\d{1,3}$/.test(part)) return null;
    const n = Number(part);
    if (!Number.isInteger(n) || n < 0 || n > 255) return null;
    octets.push(n);
  }
  return octets;
}

function ipToNumber(octets: number[]): number {
  return ((octets[0]! << 24) | (octets[1]! << 16) | (octets[2]! << 8) | octets[3]!) >>> 0;
}

function numberToIp(n: number): string {
  return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255].join(".");
}

function ipStringToNumber(ip: string | undefined): number | null {
  const trimmed = ip?.trim();
  if (!trimmed) return null;
  const octets = parseIpv4Octets(trimmed.split("/")[0] ?? trimmed);
  return octets ? ipToNumber(octets) : null;
}

/** Parse CIDR like 10.20.0.0/24 — used for display/mask only when RouterOS validates a subnet. */
export function parseGuestNetworkCidr(cidr: string | undefined): {
  network: number;
  broadcast: number;
  mask: string;
  prefix: number;
  cidr: string;
} | null {
  const trimmed = cidr?.trim();
  if (!trimmed || !trimmed.includes("/")) return null;
  const [addr, prefixRaw] = trimmed.split("/");
  const prefix = Number(prefixRaw);
  if (!Number.isInteger(prefix) || prefix < 8 || prefix > 30) return null;
  const octets = parseIpv4Octets(addr ?? "");
  if (!octets) return null;
  const maskNum = (0xffffffff << (32 - prefix)) >>> 0;
  const network = ipToNumber(octets) & maskNum;
  const broadcast = network | (~maskNum >>> 0);
  return {
    network,
    broadcast,
    mask: numberToIp(maskNum),
    prefix,
    cidr: trimmed,
  };
}

function parseAddressField(address: string): {
  ip: number;
  network: number;
  broadcast: number;
  prefix: number;
  cidr: string;
  mask: string;
  gatewayCandidate: string;
} | null {
  const trimmed = address.trim();
  if (!trimmed.includes("/")) return null;
  const parsed = parseGuestNetworkCidr(trimmed);
  if (!parsed) return null;
  const host = trimmed.split("/")[0]?.trim() ?? "";
  const hostNum = ipStringToNumber(host);
  if (hostNum == null) return null;
  return {
    ip: hostNum,
    network: parsed.network,
    broadcast: parsed.broadcast,
    prefix: parsed.prefix,
    cidr: `${numberToIp(parsed.network)}/${parsed.prefix}`,
    mask: parsed.mask,
    gatewayCandidate: host,
  };
}

/** Parse MikroTik-style pool like 10.20.0.100-10.20.0.200. */
export function parseDhcpPoolRange(pool: string | undefined): {
  start: number;
  end: number;
} | null {
  const trimmed = pool?.trim();
  if (!trimmed || !trimmed.includes("-")) return null;
  const [startRaw, endRaw] = trimmed.split("-");
  const startOctets = parseIpv4Octets(startRaw?.trim() ?? "");
  const endOctets = parseIpv4Octets(endRaw?.trim() ?? "");
  if (!startOctets || !endOctets) return null;
  const start = ipToNumber(startOctets);
  const end = ipToNumber(endOctets);
  if (start > end) return null;
  return { start, end };
}

function isInRange(ip: number, start: number, end: number): boolean {
  return ip >= start && ip <= end;
}

function isHostInNetwork(ip: number, network: number, broadcast: number): boolean {
  return ip > network && ip < broadcast;
}

export type ValidatedApManagementNetwork = {
  validated: boolean;
  source: "routeros_bridge_address" | "unavailable";
  sourceLabel: string;
  cidr: string | null;
  gateway: string | null;
  mask: string | null;
  dhcpPool: string | null;
  explanation: string;
  recommendations: string[];
  /** Guest HotSpot network from provisioning — informational, not AP management subnet. */
  guestNetworkLabel: string | null;
};

/**
 * Validates AP management subnet ONLY from RouterOS /ip/address data (+ registered AP IP).
 * Never recommends addresses from guestNetwork provisioning alone.
 */
export function resolveValidatedApManagementNetwork(input: {
  networkAddresses?: NetworkAddressSnapshot[];
  networkAddressesKnown?: boolean;
  guestBridgeName?: string;
  registeredApManagementIp?: string;
  guestNetwork?: string;
  guestGateway?: string;
  dhcpPool?: string;
  esp32Ip?: string;
  esp32Gateway?: string;
  registeredAps?: AccessPointRecord[];
  count?: number;
}): ValidatedApManagementNetwork {
  const guestNetworkLabel = input.guestNetwork?.trim() || null;
  const unavailable = (explanation: string): ValidatedApManagementNetwork => ({
    validated: false,
    source: "unavailable",
    sourceLabel: "Not validated",
    cidr: null,
    gateway: null,
    mask: null,
    dhcpPool: input.dhcpPool?.trim() || null,
    explanation,
    recommendations: [],
    guestNetworkLabel,
  });

  const apIp = input.registeredApManagementIp?.trim();
  const apNum = ipStringToNumber(apIp);
  const bridgeName = input.guestBridgeName?.trim().toLowerCase();

  const addresses = (input.networkAddresses ?? []).filter(
    (row) => row.interface?.trim() && row.address?.trim(),
  );

  if (!input.networkAddressesKnown || addresses.length === 0) {
    if (apIp) {
      return unavailable(
        `Registered AP management IP ${apIp} is authoritative observed data. Renz-Fi cannot safely determine a static AP management subnet until Router Sync provides RouterOS address assignments.`,
      );
    }
    return unavailable(
      "Renz-Fi cannot safely determine a static AP management IP from the available router data. Run Router Sync to refresh RouterOS observations.",
    );
  }

  // Find RouterOS address entries where the registered AP IP lies in the same subnet.
  let matched: ReturnType<typeof parseAddressField> | null = null;
  let matchedIface: string | null = null;

  const considerAddress = (row: NetworkAddressSnapshot) => {
    const parsed = parseAddressField(row.address);
    if (!parsed) return;
    if (apNum != null && isHostInNetwork(apNum, parsed.network, parsed.broadcast)) {
      matched = parsed;
      matchedIface = row.interface.trim();
      return;
    }
    if (apNum == null && bridgeName && row.interface.trim().toLowerCase() === bridgeName) {
      if (!matched) {
        matched = parsed;
        matchedIface = row.interface.trim();
      }
    }
  };

  if (bridgeName) {
    for (const row of addresses) {
      if (row.interface.trim().toLowerCase() === bridgeName) considerAddress(row);
    }
  }

  if (!matched && apNum != null) {
    for (const row of addresses) {
      considerAddress(row);
    }
  }

  if (!matched) {
    if (apIp) {
      return unavailable(
        `Registered AP ${apIp} is working, but RouterOS address data does not show a matching management subnet on ${input.guestBridgeName ?? "the guest bridge"}. Do not assume guest network ${guestNetworkLabel ?? "—"} is the AP management subnet.`,
      );
    }
    return unavailable(
      "Renz-Fi cannot safely determine a static AP management IP from RouterOS address data.",
    );
  }

  const parsed = matched;
  const sourceLabel =
    matchedIface != null
      ? `Validated from RouterOS address on ${matchedIface}`
      : "Validated from RouterOS address data";

  const excluded = new Set<number>();
  excluded.add(parsed.network);
  excluded.add(parsed.broadcast);
  excluded.add(parsed.ip);

  const gatewayNum = ipStringToNumber(input.esp32Gateway);
  const esp32Num = ipStringToNumber(input.esp32Ip);
  if (gatewayNum != null && isHostInNetwork(gatewayNum, parsed.network, parsed.broadcast)) {
    excluded.add(gatewayNum);
  }
  if (esp32Num != null && isHostInNetwork(esp32Num, parsed.network, parsed.broadcast)) {
    excluded.add(esp32Num);
  }

  const pool = parseDhcpPoolRange(input.dhcpPool);
  if (pool) {
    for (let ip = pool.start; ip <= pool.end; ip += 1) {
      if (isHostInNetwork(ip, parsed.network, parsed.broadcast)) excluded.add(ip);
    }
  }

  for (const ap of input.registeredAps ?? []) {
    const n = ipStringToNumber(ap.managementIp);
    if (n != null && isHostInNetwork(n, parsed.network, parsed.broadcast)) excluded.add(n);
  }

  const want = input.count ?? 3;
  const recommendations: string[] = [];
  const tryCandidate = (candidate: number) => {
    if (!isHostInNetwork(candidate, parsed.network, parsed.broadcast)) return false;
    if (excluded.has(candidate)) return false;
    const ipStr = numberToIp(candidate);
    if (recommendations.includes(ipStr)) return false;
    recommendations.push(ipStr);
    return true;
  };

  for (let offset = 20; offset <= 250 && recommendations.length < want; offset += 1) {
    tryCandidate(parsed.network + offset);
  }
  if (recommendations.length < want) {
    for (
      let candidate = parsed.network + 1;
      candidate < parsed.broadcast && recommendations.length < want;
      candidate += 1
    ) {
      tryCandidate(candidate);
    }
  }

  let explanation = `RouterOS shows ${parsed.cidr} on ${matchedIface ?? "an interface"}.`;
  if (apIp) {
    explanation += ` Registered AP ${apIp} is on this management subnet.`;
  }
  if (guestNetworkLabel && guestNetworkLabel !== parsed.cidr) {
    explanation += ` Guest HotSpot network ${guestNetworkLabel} is separate and is not used for AP management IP recommendations.`;
  }

  return {
    validated: true,
    source: "routeros_bridge_address",
    sourceLabel,
    cidr: parsed.cidr,
    gateway: parsed.gatewayCandidate,
    mask: parsed.mask,
    dhcpPool: input.dhcpPool?.trim() || null,
    explanation,
    recommendations,
    guestNetworkLabel,
  };
}

/** @deprecated Use resolveValidatedApManagementNetwork — never recommends from guestNetwork alone. */
export function recommendApManagementIps(input: {
  networkAddresses?: NetworkAddressSnapshot[];
  networkAddressesKnown?: boolean;
  guestBridgeName?: string;
  registeredApManagementIp?: string;
  guestNetwork?: string;
  guestGateway?: string;
  dhcpPool?: string;
  esp32Ip?: string;
  esp32Gateway?: string;
  registeredAps?: AccessPointRecord[];
  count?: number;
}): string[] {
  return resolveValidatedApManagementNetwork(input).recommendations;
}
